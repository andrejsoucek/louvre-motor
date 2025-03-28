#include <AccelStepper.h>

AccelStepper stepper(AccelStepper::DRIVER, 2, 3);

#define enablePin 4

// Protocol constants
#define STX 0xAA
#define ETX 0x55
#define CMD_MOVE        0x01
#define CMD_SET_POS     0x02
#define CMD_SET_MIN     0x03
#define CMD_SET_MAX     0x04
#define CMD_GET_POS     0x05
#define CMD_RESET_CAL   0x06
#define CMD_GET_CAL     0x07
#define CMD_SET_REVERSE 0x08
#define CMD_GET_REVERSE 0x09
#define CMD_ACK         0x06
#define CMD_NACK        0x15

unsigned int speed = 500;
unsigned int minPosition = 0;
unsigned int maxPosition = 0;
int direction = 1;
bool isMinSet = false;
bool isMaxSet = false;
bool isCalibrated = false;
bool isMoving = false;

void setup() {
  Serial.begin(9600);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  stepper.setMaxSpeed(800);

  while(!Serial) {} 

  sendCalibrationStatus();
}

void loop() {
  handleSerialCommands();
  updateMotor();
}

void updateMotor() {
  if (stepper.distanceToGo() != 0) {
    isMoving = true;
    digitalWrite(enablePin, LOW);
    stepper.runSpeed();
  } else if (stepper.distanceToGo() == 0) {
    if (isMoving) {
      isMoving = false;
      if (isCalibrated) {
        sendPosition();
      }
      digitalWrite(enablePin, HIGH);
    }
  }
}

void handleSerialCommands() {
  static byte buffer[32];
  static byte index = 0;
  
  while (Serial.available()) {
    byte b = Serial.read();
    
    if(b == STX && index == 0) {
      index = 1;
      buffer[0] = b;
    } 
    else if(index > 0) {
      if(index < sizeof(buffer)) {
        buffer[index++] = b;
        
        if(b == ETX && index >= 5) {
          processFrame(buffer, index);
          index = 0;
        }
      } else {
        index = 0;
      }
    }
  }
}

void processFrame(byte* frame, byte length) {
  if(!validateChecksum(frame, length)) {
    sendNack(0x01);
    return;
  }

  byte cmd = frame[2];
  byte dataLen = frame[1];
  
  switch(cmd) {
    case CMD_MOVE:
      if(dataLen == 5) {
        long steps = (long)frame[3] << 24 | 
                    (long)frame[4] << 16 |
                    (long)frame[5] << 8 |
                    (long)frame[6];
        stepper.move(steps * direction);
        stepper.setSpeed(500 * direction);
        sendAck();
      }
      break;

    case CMD_SET_POS:
      if(dataLen == 3 && isCalibrated) {
        byte percent = frame[3];
        long target = map(percent, 0, 100, minPosition, maxPosition);
        stepper.moveTo(target);
        stepper.setSpeed(500 * (target > stepper.currentPosition() ? 1 : -1));
        sendAck();
      }
      break;

    case CMD_SET_MIN:
      minPosition = stepper.currentPosition();
      isMinSet = true;
      checkCalibration();
      sendAck();
      break;

    case CMD_SET_MAX:
      maxPosition = stepper.currentPosition();
      isMaxSet = true;
      checkCalibration();
      sendAck();
      break;

    case CMD_GET_POS:
      if (isCalibrated) {
        sendPosition();
      } else {
        sendNack(0x03);
      }
      break;

    case CMD_RESET_CAL:
      resetCalibration();
      sendAck();
      break;

    case CMD_GET_CAL:
      sendCalibrationStatus();
      break;

    case CMD_SET_REVERSE:
      direction *= -1;
      sendAck();
      break;

    case CMD_GET_REVERSE:
      sendReverseStatus();
      break;

    default:
      sendNack(0x02);
      break;
  }
}

bool validateChecksum(byte* frame, byte length) {
  byte checksum = 0;
  for(int i = 1; i < length - 2; i++) {
    checksum ^= frame[i];
  }
  return checksum == frame[length-2];
}

void sendAck() {
  byte frame[] = {STX, 0x03, CMD_ACK, 0x00, ETX};
  frame[3] = frame[1] ^ frame[2];
  Serial.write(frame, sizeof(frame));
}

void sendNack(byte errorCode) {
  byte frame[] = {STX, 0x04, CMD_NACK, errorCode, 0x00, ETX};
  frame[4] = frame[1] ^ frame[2] ^ frame[3];
  Serial.write(frame, sizeof(frame));
}

void sendPosition() {
  byte frame[8];
  long absPos = stepper.currentPosition();
  float percentage = map(absPos, minPosition, maxPosition, 0, 100);
  
  frame[0] = STX;
  frame[1] = 0x06;
  frame[2] = CMD_GET_POS;
  frame[3] = (byte)percentage;
  frame[4] = frame[1] ^ frame[2] ^ frame[3];
  frame[5] = ETX;
  
  Serial.write(frame, sizeof(frame));
}

void sendCalibrationStatus() {
  byte frame[] = {STX, 0x04, CMD_GET_CAL, isCalibrated, 0x00, ETX};
  frame[4] = frame[1] ^ frame[2] ^ frame[3];
  Serial.write(frame, sizeof(frame));
}

void sendReverseStatus() {
  byte frame[] = {STX, 0x04, CMD_GET_REVERSE, (byte)(direction == -1), 0x00, ETX};
  frame[4] = frame[1] ^ frame[2] ^ frame[3];
  Serial.write(frame, sizeof(frame));
}

void checkCalibration() {
  isCalibrated = isMinSet && isMaxSet && (minPosition != maxPosition);
}

void resetCalibration() {
  isCalibrated = false;
  isMinSet = false;
  isMaxSet = false;
  minPosition = 0;
  maxPosition = 0;
}
