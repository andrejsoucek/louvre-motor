# Soucek Motor Protocol

## Overview

This document describes the serial protocol implemented by the motor firmware in `motor.ino` and the Zigbee bridge behavior implemented in `soucek-motor.js`.

There are two layers involved:

1. The native motor protocol on the UART link.
2. A Zigbee transport wrapper used by the custom converter.

The device is identified in the Zigbee metadata as:

- Manufacturer: `andrejsoucek`
- Model ID: `soucek-motor`
- Board: `CC2530`
- Device type: `Router`

According to `motor.txt`, the UART is configured as `9600 8N1`.

## Transport Layers

### 1. Native UART protocol

The firmware reads raw bytes from `Serial` and starts buffering only after it sees `STX = 0xAA`. A frame ends when `ETX = 0x55` is received.

Native frame format:

| Byte | Name | Description |
| --- | --- | --- |
| 0 | `STX` | Start byte, always `0xAA` |
| 1 | `LEN` | Length of `CMD + PAYLOAD` |
| 2 | `CMD` | Command byte |
| 3..n | `PAYLOAD` | Optional payload |
| last-1 | `CHECKSUM` | XOR of bytes `LEN`, `CMD`, and all payload bytes |
| last | `ETX` | End byte, always `0x55` |

Checksum formula:

```text
CHECKSUM = LEN ^ CMD ^ PAYLOAD[0] ^ PAYLOAD[1] ^ ...
```

### 2. Zigbee wrapper

The Zigbee converter writes to cluster `genMultistateValue`, attribute `14`, with Zigbee type `0x42`.

When sending a command from Zigbee to the device, `soucek-motor.js` prepends one extra byte before the native UART frame:

```text
[FRAME_LENGTH, STX, LEN, CMD, PAYLOAD..., CHECKSUM, ETX]
```

`FRAME_LENGTH` is the total length of the native frame starting at `STX`.

The Arduino parser ignores any bytes before `STX`, so this extra prefix does not affect the firmware parser.

Responses produced by the firmware are plain native UART frames beginning with `STX`; the Zigbee `fromZigbee` converter parses those directly.

## Constants

| Name | Value |
| --- | --- |
| `STX` | `0xAA` |
| `ETX` | `0x55` |
| `CMD_MOVE` | `0x01` |
| `CMD_SET_POS` | `0x02` |
| `CMD_HOME` | `0x03` |
| `CMD_GET_POS` | `0x04` |
| `CMD_GET_CAL` | `0x05` |
| `CMD_SET_SPEED` | `0x06` |
| `CMD_GET_SPEED` | `0x07` |
| `CMD_STOP` | `0x08` |
| `CMD_SET_TRAVEL` | `0x09` |
| `CMD_GET_TRAVEL` | `0x0A` |
| `CMD_NACK` | `0x0B` |

## Command Reference

### `0x01` Move relative: `CMD_MOVE`

Request payload:

- 4 bytes
- Signed 32-bit integer
- Big-endian order

Native request:

```text
[AA, 05, 01, step3, step2, step1, step0, checksum, 55]
```

Meaning:

- Moves the motor by a relative number of steps.
- Positive steps move in the positive direction.
- Negative steps move in the negative direction.

Response behavior:

- No immediate acknowledgement is sent.
- When motion finishes and calibration is available, the firmware sends a position report using `CMD_GET_POS`.

### `0x02` Set absolute position: `CMD_SET_POS`

Request payload:

- 1 byte position in percent
- Expected range: `0..100`

Native request:

```text
[AA, 02, 02, percent, checksum, 55]
```

Meaning:

- `0%` is the absolute closed position after a successful home.
- Any percentage is mapped to the persisted travel range `0..travelSteps`.
- Absolute positioning only works when the device is homed and a non-zero travel range has been stored.

Response behavior:

- Non-zero positioning sends `CMD_GET_POS` when motion finishes.

### `0x03` Start homing: `CMD_HOME`

Request payload: none

Native request:

```text
[AA, 01, 03, checksum, 55]
```

Meaning:

- Starts a homing cycle toward the closed endstop.
- The motor moves in the negative direction until the endstop is hit.
- When the endstop is reached, the current position is forced to `0`.

Response behavior:

- Sends `CMD_GET_CAL` when homing completes.
- Sends `CMD_GET_POS` if travel is configured.

### `0x04` Get current position: `CMD_GET_POS`

Request payload: none

Native request:

```text
[AA, 01, 04, checksum, 55]
```

Response payload:

- 1 byte position in percent

Native response:

```text
[AA, 02, 04, percent, checksum, 55]
```

Behavior:

- Returns the current position only if the device is calibrated.
- If not calibrated, the firmware returns `CMD_NACK` with error code `0x03`.

### `0x05` Get calibration state: `CMD_GET_CAL`

Request payload: none

Native request:

```text
[AA, 01, 05, checksum, 55]
```

Response payload:

- 1 byte boolean-like status
- `0x00` = not calibrated
- `0x01` = calibrated

Native response:

```text
[AA, 02, 05, status, checksum, 55]
```

### `0x06` Set speed: `CMD_SET_SPEED`

Request payload:

- 2 bytes unsigned integer
- Big-endian order

Native request:

```text
[AA, 03, 06, speed_hi, speed_lo, checksum, 55]
```

Meaning:

- Updates the internal `speed` variable.
- Calls `stepper.setMaxSpeed(speed)`.

Response behavior:

- Immediately sends the current speed using `CMD_GET_SPEED`.

### `0x07` Get speed: `CMD_GET_SPEED`

Request payload: none

Native request:

```text
[AA, 01, 07, checksum, 55]
```

Response payload:

- 2 bytes unsigned integer
- Big-endian order

Native response:

```text
[AA, 03, 07, speed_hi, speed_lo, checksum, 55]
```

### `0x08` Stop motion: `CMD_STOP`

Request payload: none

Native request:

```text
[AA, 01, 08, checksum, 55]
```

Meaning:

- Stops the current motion.

Response behavior:

- Sends `CMD_GET_POS` when the device is calibrated.

### `0x09` Set open travel steps: `CMD_SET_TRAVEL`

Request payload:

- 4 bytes unsigned integer
- Big-endian order

Native request:

```text
[AA, 05, 09, step3, step2, step1, step0, checksum, 55]
```

Meaning:

- Stores the number of steps from closed (`0%`) to open (`100%`).
- The value is persisted in EEPROM.
- The device becomes fully calibrated only after it has both a valid stored travel and a successful homing cycle.

Response behavior:

- Sends calibration status with `CMD_GET_CAL`.
- Sends the stored travel with `CMD_GET_TRAVEL`.
- If the device is already homed, also sends `CMD_GET_POS`.

### `0x0A` Get open travel steps: `CMD_GET_TRAVEL`

Request payload: none

Native request:

```text
[AA, 01, 0A, checksum, 55]
```

Response payload:

- 4 bytes unsigned integer
- Big-endian order

Native response:

```text
[AA, 05, 0A, step3, step2, step1, step0, checksum, 55]
```

### `0x0B` Negative acknowledgement: `CMD_NACK`

Response payload:

- 1 byte error code

Native response:

```text
[AA, 02, 0B, error, checksum, 55]
```

Observed error codes in the firmware:

| Code | Meaning |
| --- | --- |
| `0x01` | Invalid checksum |
| `0x02` | Unknown command |
| `0x03` | Position requested while not calibrated |

## Data Encoding

### Relative move value

The `move` command uses a signed 32-bit integer in big-endian order.

Example for `+400` steps:

```text
00 00 01 90
```

Example for `-400` steps:

```text
FF FF FE 70
```

### Speed value

Speed is transferred as a 16-bit big-endian integer.

Example for speed `300`:

```text
01 2C
```

### Position value

Absolute position is always represented as a single percentage byte.

## Device Behavior

### Calibration model

The device becomes calibrated when:

- it has successfully homed to the closed endstop in the current boot session,
- and a non-zero open travel distance is available from EEPROM or from `CMD_SET_TRAVEL`.

Homing uses the sensor on `D9` configured as `INPUT_PULLUP`, so the endstop is active when the pin reads `LOW`.

### Homing model

- Homing direction is negative.
- Opening movement is positive.
- Boot starts a homing cycle automatically.
- Manual homing is triggered with `CMD_HOME`.
- When the endstop is hit, the firmware calls `setCurrentPosition(0)`.
- If the endstop is not reached within the configured search distance, the homing attempt fails and the device remains uncalibrated.

### Position semantics in the Zigbee converter

The Zigbee `fromZigbee` converter interprets a position response as:

- `position = 0` -> `state = CLOSE`
- any non-zero position -> `state = OPEN`

This means the public Zigbee state is binary-like, even though the firmware supports intermediate percentages.

### Startup reports

After boot, the firmware begins homing immediately. It still emits the startup reports after about `15 seconds`:

1. calibration status (`CMD_GET_CAL` response format)
2. speed report (`CMD_GET_SPEED` response format) after an additional `500 ms`

### Motion completion report

When a move finishes:

- the motor driver is disabled,
- `isMoving` becomes `false`,
- and if the device is calibrated, a position report is sent.

## Zigbee Converter Mapping

The custom Zigbee converter exposes these high-level controls:

| Zigbee key | Protocol mapping |
| --- | --- |
| `state=OPEN` | `CMD_SET_POS` with `100` |
| `state=CLOSE` | `CMD_SET_POS` with `0` |
| `state=STOP` | `CMD_STOP` |
| `position` | `CMD_SET_POS` with `0..100` |
| `home=start` | `CMD_HOME` |
| `move` | `CMD_MOVE` |
| `speed` | `CMD_SET_SPEED` |
| `travel_steps` | `CMD_SET_TRAVEL` / `CMD_GET_TRAVEL` |
| `get position` | `CMD_GET_POS` |
| `get calibrated` | `CMD_GET_CAL` |
| `get speed` | `CMD_GET_SPEED` |
| `get travel_steps` | `CMD_GET_TRAVEL` |

The converter currently limits `move` in its expose definition to these string values:

```text
-1600, -800, -400, 100, 400, 800, 1600
```

However, the firmware itself accepts any signed 32-bit step count that fits the frame.

## Example Frames

### Get calibration status

Request:

```text
AA 01 05 04 55
```

Reason: `01 ^ 05 = 04`

### Calibration status = calibrated

Response:

```text
AA 02 05 01 06 55
```

Reason: `02 ^ 05 ^ 01 = 06`

### Set position to 100%

Native frame:

```text
AA 02 02 64 64 55
```

Reason: `02 ^ 02 ^ 64 = 64`

Zigbee-written payload:

```text
06 AA 02 02 64 64 55
```

The leading `06` is the converter-added frame length.

### Set open travel to 16000 steps

Native frame:

```text
AA 05 09 00 00 3E 80 B2 55
```

Reason: `05 ^ 09 ^ 00 ^ 00 ^ 3E ^ 80 = B2`

### Stop

Native frame:

```text
AA 01 08 09 55
```

Reason: `01 ^ 08 = 09`

## Notes and Constraints

- There is no positive acknowledgement frame for successful write commands.
- Some commands produce a state report rather than a dedicated acknowledgement.
- `CMD_SET_POS` requires a successful home and a stored travel distance.
- `CMD_HOME` is the explicit way to re-home the motor after boot or after a mechanical desynchronization.
- The firmware validates only the checksum before dispatching a command. Payload length checks are command-specific.
- Unknown or malformed commands return `CMD_NACK` only in some cases. For example, bad checksum and unknown command are explicitly handled.
