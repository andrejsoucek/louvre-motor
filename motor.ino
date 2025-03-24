#include <AccelStepper.h>

AccelStepper stepper(AccelStepper::DRIVER, 2, 3);

#define enablePin 4

unsigned int speed = 500;
unsigned int minPosition = 0;
unsigned int maxPosition = 0;
int direction = 1;
bool isMinSet = false;
bool isMaxSet = false;
bool isCalibrated = false;

void setup() {
  Serial.begin(9600);
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, HIGH);

  stepper.setMaxSpeed(800);
  
  Serial.println("Motor is not calibrated");
}

void loop() {
  handleSerialCommands();
  
  unsigned long currentTime = micros();
  
  if (stepper.distanceToGo() != 0) {
    digitalWrite(enablePin, LOW);
    stepper.runSpeed();
  } else if (stepper.distanceToGo() == 0) {
    digitalWrite(enablePin, HIGH);
  }
}

void handleSerialCommands() {
  static String inputBuffer = "";
  
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processCommand(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}

void processCommand(String cmd) {
  cmd.trim();
  
  if (cmd == "GET_REVERSE") {
    return Serial.println(direction == -1);
  }
  else if (cmd == "SET_REVERSE") {
    resetCalibration();
    direction = -1 * direction;
  }
  else if (cmd == "IS_CALIBRATED") {
    Serial.println(isCalibrated);
  }
  else if (cmd == "RESET_CALIBRATION") {
    resetCalibration();
    Serial.println("OK CALIBRATION_RESET");
  }
  else if (cmd.startsWith("MIN")) {
    stepper.setCurrentPosition(0);
    minPosition = stepper.currentPosition();
    isMinSet = true;
    checkCalibration();
    Serial.println("OK MIN_SET");
    Serial.print(minPosition);
  }
  else if (cmd.startsWith("MAX")) {
    maxPosition = stepper.currentPosition();
    isMaxSet = true;
    checkCalibration();
    Serial.print("OK MAX_SET");
    Serial.println(maxPosition);
  }
  else if (cmd.startsWith("MOVE ")) {
    int steps = cmd.substring(5).toInt();
    stepper.move(steps * direction);
    stepper.setSpeed(500 * direction);
    Serial.print("OK MOVING ");
    Serial.println(steps);
  }
  else if (cmd.startsWith("GET_POS")) {
    if (!isCalibrated) {
      Serial.println("ERROR NOT_CALIBRATED");
      return;
    }
    float percentage = map(stepper.currentPosition(), minPosition, maxPosition, 0, 100);
    Serial.print("POS ");
    Serial.print(percentage);
    Serial.print(" %, absolute: ");
    Serial.println(stepper.currentPosition());
  }
  else if (cmd.startsWith("SET_POS ")) {
    if (!isCalibrated) {
      Serial.println("ERROR NOT_CALIBRATED");
      return;
    }
    float percentage = cmd.substring(8).toInt();
    long target = map(percentage, 0, 100, minPosition, maxPosition);
    stepper.moveTo(target);
    if (target < stepper.currentPosition()) {
      stepper.setSpeed(-500 * direction);
    } else {
      stepper.setSpeed(500 * direction);
    }
    Serial.print("OK MOVING_TO ");
    Serial.print(percentage);
    Serial.print(" %, absolute: ");
    Serial.println(target);
  }
  else {
    Serial.println("ERROR UNKNOWN_CMD");
  }
}

void checkCalibration() {
  if (isMinSet && isMaxSet) {
    isCalibrated = true;
  } else {
    isCalibrated = false;
  }
}

void resetCalibration() {
  isCalibrated = false;
  isMinSet = false;
  isMaxSet = false;
  minPosition = 0;
  maxPosition = 0;
}