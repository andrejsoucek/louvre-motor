#include <AccelStepper.h>
#include <EEPROM.h>

AccelStepper stepper(AccelStepper::DRIVER, 2, 3);

#define enablePin 4
#define endstopPin 9

// Protocol constants
#define STX 0xAA
#define ETX 0x55
#define CMD_MOVE        0x01
#define CMD_SET_POS     0x02
#define CMD_HOME        0x03
#define CMD_GET_POS     0x04
#define CMD_GET_CAL     0x05
#define CMD_SET_SPEED   0x06
#define CMD_GET_SPEED   0x07
#define CMD_STOP        0x08
#define CMD_SET_TRAVEL  0x09
#define CMD_GET_TRAVEL  0x0A
#define CMD_NACK        0x0B

#define EEPROM_SIGNATURE 0x534D

struct PersistedConfig {
  uint16_t signature;
  long travelSteps;
};

const int defaultSpeed = 1000;

int speed = defaultSpeed;
long maxPosition = 0;
bool isCalibrated = false;
bool isMoving = false;
bool isHomed = false;
bool isHoming = false;

const long homingSearchSteps = 120000;
const unsigned long startupStatusDelayMs = 15000;
const unsigned long startupSpeedDelayMs = 500;

long homingTarget = 0;
unsigned long bootMillis = 0;
bool startupCalibrationReported = false;
bool startupSpeedReported = false;

bool isEndstopTriggered();
void loadPersistedConfig();
void saveTravelSteps(long travelSteps);
void updateCalibrationStatus();
void startHoming();
void handleHoming();
void finishHoming();
void haltMotion();
void sendTravelSteps();
void sendStartupReports();

void setup() {
  Serial.begin(9600);
  pinMode(enablePin, OUTPUT);
  pinMode(endstopPin, INPUT_PULLUP);
  digitalWrite(enablePin, HIGH);
  stepper.setMaxSpeed(speed);

  while(!Serial) {}

  loadPersistedConfig();
  bootMillis = millis();
  startHoming();
}

void loop() {
  handleSerialCommands();
  updateMotor();
  sendStartupReports();
}

void updateMotor() {
  if (isHoming) {
    handleHoming();
    return;
  }

  if (stepper.distanceToGo() != 0) {
    isMoving = true;
    digitalWrite(enablePin, LOW);
    stepper.runSpeed();
  } else if (isMoving) {
    isMoving = false;
    if (isCalibrated) {
      sendPosition();
    }
    digitalWrite(enablePin, HIGH);
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
      if (dataLen == 2) {
        if (isCalibrated) {
          byte percent = frame[3];
          long target = map(percent, 0, 100, 0, maxPosition);
          stepper.moveTo(target);
          stepper.setSpeed(target > stepper.currentPosition() ? speed : -speed);
        } else {
          sendNack(0x03);
        }
      }
      break;

    case CMD_HOME:
      startHoming();
      break;

    case CMD_GET_POS:
      if (isCalibrated) {
        sendPosition();
      } else {
        sendNack(0x03);
      }
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
      isHoming = false;
      haltMotion();
      if (isCalibrated) {
        sendPosition();
      }
      break;

    case CMD_SET_TRAVEL:
      if (dataLen == 5) {
        long travelSteps = (long)frame[3] << 24 |
                          (long)frame[4] << 16 |
                          (long)frame[5] << 8 |
                          (long)frame[6];
        if (travelSteps > 0) {
          saveTravelSteps(travelSteps);
          sendCalibrationStatus();
          sendTravelSteps();
          if (isCalibrated) {
            sendPosition();
          }
        } else {
          sendNack(0x04);
        }
      }
      break;

    case CMD_GET_TRAVEL:
      sendTravelSteps();
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
  long absPos = constrain(stepper.currentPosition(), 0L, maxPosition);
  long percentage = maxPosition > 0 ? map(absPos, 0, maxPosition, 0, 100) : 0;
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

void sendTravelSteps() {
  byte frame[] = {
    STX,
    0x05,
    CMD_GET_TRAVEL,
    (byte)(maxPosition >> 24),
    (byte)(maxPosition >> 16),
    (byte)(maxPosition >> 8),
    (byte)(maxPosition & 0xFF),
    0x00,
    ETX,
  };
  frame[7] = frame[1] ^ frame[2] ^ frame[3] ^ frame[4] ^ frame[5] ^ frame[6];
  Serial.write(frame, sizeof(frame));
}

bool isEndstopTriggered() {
  return digitalRead(endstopPin) == LOW;
}

void loadPersistedConfig() {
  PersistedConfig config;
  EEPROM.get(0, config);

  if (config.signature == EEPROM_SIGNATURE && config.travelSteps > 0) {
    maxPosition = config.travelSteps;
  } else {
    maxPosition = 0;
  }

  updateCalibrationStatus();
}

void saveTravelSteps(long travelSteps) {
  PersistedConfig config = {EEPROM_SIGNATURE, travelSteps};
  EEPROM.put(0, config);
  maxPosition = travelSteps;
  updateCalibrationStatus();
}

void updateCalibrationStatus() {
  isCalibrated = isHomed && maxPosition > 0;
}

void startHoming() {
  isHomed = false;
  updateCalibrationStatus();

  if (isEndstopTriggered()) {
    finishHoming();
    return;
  }

  isHoming = true;
  isMoving = true;
  homingTarget = stepper.currentPosition() - homingSearchSteps;
  stepper.moveTo(homingTarget);
  stepper.setSpeed(-speed);
  digitalWrite(enablePin, LOW);
}

void handleHoming() {
  if (isEndstopTriggered()) {
    finishHoming();
    return;
  }

  if (stepper.distanceToGo() == 0) {
    isHoming = false;
    isHomed = false;
    updateCalibrationStatus();
    haltMotion();
    sendCalibrationStatus();
    return;
  }

  stepper.runSpeed();
}

void finishHoming() {
  isHoming = false;
  isHomed = true;
  stepper.setCurrentPosition(0);
  updateCalibrationStatus();
  haltMotion();
  sendCalibrationStatus();
  if (isCalibrated) {
    sendPosition();
  }
}

void haltMotion() {
  stepper.moveTo(stepper.currentPosition());
  stepper.setSpeed(0);
  isMoving = false;
  digitalWrite(enablePin, HIGH);
}

void sendStartupReports() {
  unsigned long elapsed = millis() - bootMillis;

  if (!startupCalibrationReported && elapsed >= startupStatusDelayMs) {
    sendCalibrationStatus();
    startupCalibrationReported = true;
  }

  if (startupCalibrationReported && !startupSpeedReported && elapsed >= (startupStatusDelayMs + startupSpeedDelayMs)) {
    sendSpeed();
    startupSpeedReported = true;
  }
}
