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
#define CMD_SET_SPEED   0x0A
#define CMD_GET_SPEED   0x0B
#define CMD_STOP        0x0C
#define CMD_NACK        0x15

int speed = 300;
long minPosition = 0;
long maxPosition = 0;
bool isMinSet = false;
bool isMaxSet = false;
bool isCalibrated = false;
bool isMoving = false;

void setup() {
  Serial.begin(9600);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);
  stepper.setMaxSpeed(speed);

  while(!Serial) {}

  delay(15000);
  sendCalibrationStatus();
  delay(500);
  sendSpeed();
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

        stepper.move(steps);
        stepper.setSpeed(steps > 0 ? speed : -speed);
      }
      break;

    case CMD_SET_POS:
      if(dataLen == 2 && isCalibrated) {
        byte percent = frame[3];
        long target = map(percent, 0, 100, minPosition, maxPosition);
        stepper.moveTo(target);
        stepper.setSpeed(speed * (target > stepper.currentPosition() ? 1 : -1));
      }
      break;

    case CMD_SET_MIN:
      stepper.setCurrentPosition(0);
      minPosition = stepper.currentPosition();
      isMinSet = true;
      checkCalibration();
      sendCalibrationStatus();
      if (isCalibrated) {
        delay(500);
        sendPosition();
      }
      break;

    case CMD_SET_MAX:
      maxPosition = stepper.currentPosition();
      isMaxSet = true;
      Serial.println("Max position set");
      Serial.println(maxPosition);
      Serial.println("Min position set");
      Serial.println(minPosition);

      checkCalibration();
      sendCalibrationStatus();
      if (isCalibrated) {
        delay(500);
        sendPosition();
      }
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
      sendCalibrationStatus();
      break;

    case CMD_GET_CAL:
      sendCalibrationStatus();
      break;

    case CMD_SET_SPEED:
      if(dataLen == 3) {
        int val = (int)frame[3] << 8 | (int)frame[4];
        stepper.setMaxSpeed(val);
        speed = val;
      }
      sendSpeed();
      break;

    case CMD_GET_SPEED:
      sendSpeed();
      break;

    case CMD_STOP:
      stepper.stop();
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

void sendNack(byte errorCode) {
  byte frame[] = {STX, 0x02, CMD_NACK, errorCode, 0x00, ETX};
  frame[4] = frame[1] ^ frame[2] ^ frame[3];
  Serial.write(frame, sizeof(frame));
}

void sendPosition() {
  long absPos = stepper.currentPosition();
  long percentage = map(absPos, minPosition, maxPosition, 0, 100);
  byte frame[] = {STX, 0x02, CMD_GET_POS, (byte)percentage, 0x00, ETX};
  frame[4] = frame[1] ^ frame[2] ^ frame[3];

  Serial.write(frame, sizeof(frame));
}

void sendCalibrationStatus() {
  byte frame[] = {STX, 0x02, CMD_GET_CAL, isCalibrated, 0x00, ETX};
  frame[4] = frame[1] ^ frame[2] ^ frame[3];
  Serial.write(frame, sizeof(frame));
}

void sendSpeed() {
  byte frame[] = {STX, 0x03, CMD_GET_SPEED, (byte)(speed >> 8), (byte)(speed & 0xFF), 0x00, ETX};
  frame[5] = frame[1] ^ frame[2] ^ frame[3] ^ frame[4];
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
