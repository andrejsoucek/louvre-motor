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
| `CMD_SET_MIN` | `0x03` |
| `CMD_SET_MAX` | `0x04` |
| `CMD_GET_POS` | `0x05` |
| `CMD_RESET_CAL` | `0x06` |
| `CMD_GET_CAL` | `0x07` |
| `CMD_SET_SPEED` | `0x0A` |
| `CMD_GET_SPEED` | `0x0B` |
| `CMD_STOP` | `0x0C` |
| `CMD_NACK` | `0x15` |

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

- Maps `0..100` to the calibrated range `minPosition..maxPosition`.
- Only works when the motor is calibrated.

Response behavior:

- No immediate response is sent.
- When motion finishes, the firmware sends the current position with `CMD_GET_POS`.

### `0x03` Set minimum position: `CMD_SET_MIN`

Request payload: none

Native request:

```text
[AA, 01, 03, checksum, 55]
```

Meaning:

- Resets the current stepper position to `0` using `setCurrentPosition(0)`.
- Stores that position as `minPosition`.
- Marks the minimum limit as configured.

Response behavior:

- Sends calibration status with `CMD_GET_CAL`.
- If both limits are now valid, sends a position report after a `500 ms` delay.

### `0x04` Set maximum position: `CMD_SET_MAX`

Request payload: none

Native request:

```text
[AA, 01, 04, checksum, 55]
```

Meaning:

- Stores the current stepper position as `maxPosition`.
- Marks the maximum limit as configured.

Response behavior:

- Sends calibration status with `CMD_GET_CAL`.
- If both limits are now valid, sends a position report after a `500 ms` delay.

### `0x05` Get current position: `CMD_GET_POS`

Request payload: none

Native request:

```text
[AA, 01, 05, checksum, 55]
```

Response payload:

- 1 byte position in percent

Native response:

```text
[AA, 02, 05, percent, checksum, 55]
```

Behavior:

- Returns the current position only if the device is calibrated.
- If not calibrated, the firmware returns `CMD_NACK` with error code `0x03`.

### `0x06` Reset calibration: `CMD_RESET_CAL`

Request payload: none

Native request:

```text
[AA, 01, 06, checksum, 55]
```

Meaning:

- Clears both calibration flags.
- Resets `minPosition` and `maxPosition` to `0`.

Response behavior:

- Sends calibration status with `CMD_GET_CAL`.

### `0x07` Get calibration state: `CMD_GET_CAL`

Request payload: none

Native request:

```text
[AA, 01, 07, checksum, 55]
```

Response payload:

- 1 byte boolean-like status
- `0x00` = not calibrated
- `0x01` = calibrated

Native response:

```text
[AA, 02, 07, status, checksum, 55]
```

### `0x0A` Set speed: `CMD_SET_SPEED`

Request payload:

- 2 bytes unsigned integer
- Big-endian order

Native request:

```text
[AA, 03, 0A, speed_hi, speed_lo, checksum, 55]
```

Meaning:

- Updates the internal `speed` variable.
- Calls `stepper.setMaxSpeed(speed)`.

Response behavior:

- Immediately sends the current speed using `CMD_GET_SPEED`.

### `0x0B` Get speed: `CMD_GET_SPEED`

Request payload: none

Native request:

```text
[AA, 01, 0B, checksum, 55]
```

Response payload:

- 2 bytes unsigned integer
- Big-endian order

Native response:

```text
[AA, 03, 0B, speed_hi, speed_lo, checksum, 55]
```

### `0x0C` Stop motion: `CMD_STOP`

Request payload: none

Native request:

```text
[AA, 01, 0C, checksum, 55]
```

Meaning:

- Calls `stepper.stop()`.

Response behavior:

- No explicit response frame is sent.

### `0x15` Negative acknowledgement: `CMD_NACK`

Response payload:

- 1 byte error code

Native response:

```text
[AA, 02, 15, error, checksum, 55]
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

- minimum position has been set,
- maximum position has been set,
- and `minPosition != maxPosition`.

Setting the minimum limit always resets the current step count to zero first.

### Position semantics in the Zigbee converter

The Zigbee `fromZigbee` converter interprets a position response as:

- `position = 0` -> `state = CLOSE`
- any non-zero position -> `state = OPEN`

This means the public Zigbee state is binary-like, even though the firmware supports intermediate percentages.

### Startup reports

After boot, the firmware waits about `15 seconds`, then sends:

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
| `move` | `CMD_MOVE` |
| `speed` | `CMD_SET_SPEED` |
| `calibration=closed` | `CMD_SET_MIN` |
| `calibration=opened` | `CMD_SET_MAX` |
| `calibration=reset` | `CMD_RESET_CAL` |
| `get position` | `CMD_GET_POS` |
| `get calibrated` | `CMD_GET_CAL` |
| `get speed` | `CMD_GET_SPEED` |

The converter currently limits `move` in its expose definition to these string values:

```text
-1600, -800, -400, 100, 400, 800, 1600
```

However, the firmware itself accepts any signed 32-bit step count that fits the frame.

## Example Frames

### Get calibration status

Request:

```text
AA 01 07 06 55
```

Reason: `01 ^ 07 = 06`

### Calibration status = calibrated

Response:

```text
AA 02 07 01 04 55
```

Reason: `02 ^ 07 ^ 01 = 04`

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

### Stop

Native frame:

```text
AA 01 0C 0D 55
```

Reason: `01 ^ 0C = 0D`

## Notes and Constraints

- There is no positive acknowledgement frame for successful write commands.
- Some commands produce a state report rather than a dedicated acknowledgement.
- `CMD_SET_POS` does nothing unless calibration has already been completed.
- The firmware validates only the checksum before dispatching a command. Payload length checks are command-specific.
- Unknown or malformed commands return `CMD_NACK` only in some cases. For example, bad checksum and unknown command are explicitly handled.
