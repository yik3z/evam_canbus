/*
SAS CODE FOR EVAM

FUNCTIONS
- Outputs the steering angle
- calibrates steering
  - min/max angle
  - center position

TODO: 
- test with CAN Bus

Designed to run on an Arduino Nano (ARDUINO_AVR_NANO)

!This code is not millis() overflow protected!
*/

#include "Arduino.h"
#include <EEPROM.h>
#include <SPI.h>
#include <mcp2515.h>
#include <Wire.h>
#include <AS5600.h> //Seeed Library for AS5600:https://github.com/Seeed-Studio/Seeed_Arduino_AS5600
#define DEBUG

//timing stuff
const unsigned long messageInterval = 10;  //timing delay in ms between steering messages sent by node
unsigned long lastMessageTime = 0;  //keeps track of the timestamp of the last steering message sent


//node status
uint8_t errorState = 255;  //state of the node. 0: error, 1: ok, 255: offline

//steering angle values. All values are relative to min lock, and are 12 bit (0-4095), not actual angles
AMS_5600 ams5600; //instantiate 
uint16_t steeringAngle = 0;  //for CAN message. Split into 2 bytes
uint16_t centreAngle = 2047;
#define CENTRE_ANGLE_DEVIATION_MAX 500  //maximum amount the (raw) centre angle is allowed to deviate from the ideal (2047), before the calibration function assumes something is wrong 
#define CENTRE_CAL_EEPROM_ADDRESS 0

/***CAN BUS STUFF***/
MCP2515 mcp2515(10);
struct can_frame canStatusMsg;  //status of the node
struct can_frame canMsg;  //generic CAN message for recieving data
struct can_frame calibMsg;  //cailbration CAN message
struct can_frame steeringMsg;  //main CAN message

/***FUNCTIONS***/
void sendCanMessage(){
  steeringMsg.data[0] = steeringAngle & 0xFF ; //MSB, rightshift 8 bits
  steeringMsg.data[1] = steeringAngle >> 8; //LSB, mask in only the lowest byte
  mcp2515.sendMessage(&steeringMsg);
  lastMessageTime = millis();
  #ifdef DEBUG  //print brake and throttle values
  Serial.print("Steering Angle = ");
  Serial.println(int16_t(steeringAngle - centreAngle) / 130);
  #endif  //DEBUG
}

void readFilterSteering(){
  steeringAngle = ams5600.getScaledAngle();  //from sensor
  //seems like no need to filter anything
}

void calibrate(uint8_t mode = 0){
  uint8_t res = mode;
  //cal min angle
  if ((mode == 0) || (mode == 1)){ 
    #ifdef DEBUG
    Serial.println("Calibrate Min. Turn steering to min end.");
    for (uint8_t i = 5; i>0;i--){ //countdown
      Serial.println(i);
      delay(1000);
    }
    Serial.println("calibrating Min");
    #else //ndef DEBUG
    delay(5000);
    #endif  //DEBUG
    uint16_t rawAngle = ams5600.setStartPosition();
    #ifdef DEBUG
    Serial.println("Min set to" + String(rawAngle));
    #endif  //DEBUG
  }
  //cal max angle
  if ((mode == 0) || (mode == 2)){ 
    #ifdef DEBUG
    Serial.println("Calibrate max. Turn steering to max end.");
    for (uint8_t i = 5; i>0;i--){ //countdown
      Serial.println(i);
      delay(1000);
    }
    Serial.println("calibrating Max");
    #else //ndef DEBUG
    delay(5000);
    #endif  //DEBUG
    uint16_t rawAngle = ams5600.setEndPosition();
    #ifdef DEBUG
    Serial.println("Max set to" + String(rawAngle));
    #endif  //DEBUG
  }
  //calibrate centre
  if (mode == 3){ 
    #ifdef DEBUG
    Serial.println("Calibrate centre. Turn steering to centre.");
    for (uint8_t i = 5; i>0;i--){ //countdown
      Serial.println(i);
      delay(1000);
    }
    Serial.println("calibrating Centre");
    #else //ndef DEBUG
    delay(5000);
    #endif  //DEBUG
    uint16_t centreAngleTemp = ams5600.getScaledAngle();

    //compare default centre point and the new centre point. If deviation is too large ( greater than CENTRE_ANGLE_DEVIATION_MAX) then reject new calibration.
    if ((int16_t(centreAngle - centreAngleTemp) > CENTRE_ANGLE_DEVIATION_MAX) || (int16_t(centreAngleTemp - centreAngle) >  CENTRE_ANGLE_DEVIATION_MAX)){ 
      res = 255;  //failed
      #ifdef DEBUG
      Serial.println("Calibration Failed! Centre point is too far away. Check your steering angle again.");
      #endif  //DEBUG
    }
    if (res != 255){  //if calibration is accepted
      centreAngle = centreAngleTemp;
      EEPROM.update(CENTRE_CAL_EEPROM_ADDRESS, centreAngle & 0xFF);
      EEPROM.update(CENTRE_CAL_EEPROM_ADDRESS+1, centreAngle >> 8);
      #ifdef DEBUG
      Serial.println("Max set to" + String(centreAngle));
      #endif  //DEBUG
    }
  }
  //reset centre calibration
  if (mode == 4){ 
    centreAngle = 2047;
    EEPROM.update(CENTRE_CAL_EEPROM_ADDRESS, centreAngle & 0xFF);
    EEPROM.update(CENTRE_CAL_EEPROM_ADDRESS+1, centreAngle >> 8);
    #ifdef DEBUG
    Serial.println("Centre calibration reset");
    Serial.println("To" + String(centreAngle));
    #endif  //DEBUG
  }
  //send message
  calibMsg.data[0] = 0;
  calibMsg.data[1] = res;
  calibMsg.data[2] = centreAngle & 0xFF;
  calibMsg.data[3] = centreAngle >> 8;
  #ifdef DEBUG
  Serial.println("Calibration Completed: res = " + String(res) + "| Centre Angle = " + String(centreAngle));
  #endif
  mcp2515.sendMessage(&calibMsg);
}

void sendStatus(uint8_t status = 0){
  errorState = status;
  #ifdef DEBUG
  Serial.print("Node status: ");
  Serial.println(errorState);
  #endif //DEBUG
  canStatusMsg.data[0] = status;
  calibMsg.data[1] = centreAngle & 0xFF;
  calibMsg.data[2] = centreAngle >> 8;
  mcp2515.sendMessage(&canStatusMsg);
}


/***SETUP***/
void setup() {
  #ifdef DEBUG  //debug mode
  Serial.begin(115200);
  Serial.println("SAS Node");
  #ifndef ARDUINO_AVR_NANO
  Serial.println("WARNING: This sketch was designed for an arduino Nano");
  #endif //#ifndef ARDUINO_AVR_NANO
  #endif //#ifdef DEBUG

  Wire.begin();

  //SET SENSOR LIMITS
  //Once the magnet is glued on we can burn the limits in maybe?
  //may need to change these limits if the magnet is shifted
  ams5600.setStartPosition(450);
  ams5600.setEndPosition(3160);

  //check if EEPROM has a centre calibration stored
  centreAngle = (EEPROM.read(CENTRE_CAL_EEPROM_ADDRESS)) | 
                                (EEPROM.read(CENTRE_CAL_EEPROM_ADDRESS+1) << 8);

  //status message
  canStatusMsg.can_id  = 0x0B;
  canStatusMsg.can_dlc = 3;
  canStatusMsg.data[0] = errorState;
  canStatusMsg.data[1] = centreAngle & 0xFF;  //center low byte
  canStatusMsg.data[2] = centreAngle >> 8;  //centre high byte

  //steering angle message
  steeringMsg.can_id  = 0x2C;
  steeringMsg.can_dlc = 2;
  steeringMsg.data[0] = 0x00;
  steeringMsg.data[1] = 0x00;

  //calibration message
  calibMsg.can_id  = 0x50;
  calibMsg.can_dlc = 4;
  calibMsg.data[0] = 0x00;  //requested calibration
  calibMsg.data[1] = 0x00;  //calibration type carried out
  calibMsg.data[2] = centreAngle & 0xFF;  //center low byte
  calibMsg.data[3] = centreAngle >> 8;  //centre high byte
  
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  //check that magnet is detected
  if(ams5600.detectMagnet() == 0 ){
    unsigned long startMillis = millis();
    while(errorState == 255){
        if(ams5600.detectMagnet() == 1 ){
          #ifdef DEBUG
          SERIAL.print("Current Magnitude: ");
          SERIAL.println(ams5600.getMagnitude());
          #endif //DEBUG
          sendStatus(1);
        }
        else{
          #ifdef DEBUG
          SERIAL.println("Can not detect magnet");
          #endif
          if (millis() - startMillis > 3000){
            sendStatus(0);
          }
        }
        delay(300);
    }
  }
   

}


/***MAIN LOOP :) ***/
void loop() {
  //check for calibration message
  if (batteryMcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
    if(canMsg.can_id == 50){ //Calibrate Steering
      uint8_t calibrationMode = canMsg.data[0];
      calibrate(calibrationMode);
    }
  }

  //normal operation
 
  
  if(millis() - lastMessageTime >= messageInterval){
    readFilterSteering();
    sendCanMessage();
  }
 
}
