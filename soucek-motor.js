const exposes = require('zigbee-herdsman-converters/lib/exposes');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');

const ea = exposes.access;
const e = exposes.presets;

// Protocol constants
const STX = 0xaa;
const CMD_MOVE = 0x01;
const CMD_SET_POS = 0x02;
const CMD_SET_MIN = 0x03;
const CMD_SET_MAX = 0x04;
const CMD_GET_POS = 0x05;
const CMD_RESET_CAL = 0x06;
const CMD_GET_CAL = 0x07;
const CMD_SET_SPEED = 0x0a;
const CMD_GET_SPEED = 0x0b;
const CMD_STOP = 0x0c;
const ETX = 0x55;

function createMoveCmd(steps) {
    // Protocol frame structure: [STX, LEN, CMD, STEPS (4 bytes), CHECKSUM, ETX]
    // Convert steps to 32-bit signed integer (4 bytes)
    const stepBytes = new Uint8Array(4);
    stepBytes[0] = (steps >> 24) & 0xff;
    stepBytes[1] = (steps >> 16) & 0xff;
    stepBytes[2] = (steps >> 8) & 0xff;
    stepBytes[3] = steps & 0xff;

    // Calculate checksum (XOR of LEN + CMD + all step bytes)
    const len = 0x05; // 1 byte (CMD) + 4 bytes (steps)
    let checksum = len ^ CMD_MOVE;
    for (const byte of stepBytes) {
        checksum ^= byte;
    }

    const cmd = [STX, len, CMD_MOVE, ...stepBytes, checksum, ETX];
    return [cmd.length, ...cmd];
}

function createPositionCmd(percentage) {
    // Protocol frame structure: [STX, LEN, CMD, POS, CHECKSUM, ETX]
    const percentageByte = percentage & 0xff;
    const cmd = [STX, 0x02, CMD_SET_POS, percentageByte, 0x02 ^ CMD_SET_POS ^ percentageByte, ETX];
    return [cmd.length, ...cmd];
}

function createSpeedCmd(speed) {
    // Protocol frame structure: [STX, LEN, CMD, SPEED (2 bytes), CHECKSUM, ETX]
    const speedBytes = new Uint8Array(2);
    speedBytes[0] = (speed >> 8) & 0xff;
    speedBytes[1] = speed & 0xff;

    // Calculate checksum (XOR of LEN + CMD + all speed bytes)
    const len = 0x03; // 1 byte (CMD) + 2 bytes (speed)
    let checksum = len ^ CMD_SET_SPEED;
    for (const byte of speedBytes) {
        checksum ^= byte;
    }

    const cmd = [STX, len, CMD_SET_SPEED, ...speedBytes, checksum, ETX];
    return [cmd.length, ...cmd];
}

const asString = (data) => {
    let text = '';
    for (const byte of data) {
        text += byte >= 32 && byte <= 127 ? String.fromCharCode(byte) : `\\x${byte.toString(16).padStart(2, '0')}`;
    }
    return text;
};

const fz_uart = {
    cluster: 'genMultistateValue',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg, publish, options, meta) => {
        const result = {};
        /** @type string */
        const data = msg.data.stateText; // string

        if (data.length >= 5 && data[0] === STX && data[data.length - 1] === ETX) {
            const checksum = data.slice(1, -2).reduce((sum, byte) => sum ^ byte, 0);
            if (checksum !== data[data.length - 2]) {
                return;
            }
            switch (data[2]) {
                case CMD_GET_POS:
                    result.position = data[3];
                    if (result.position === 0) {
                        result.state = 'CLOSE';
                    } else {
                        result.state = 'OPEN';
                    }
                    break;
                case CMD_GET_CAL:
                    result.calibrated = data[3] === 0x01;
                    break;
                case CMD_GET_SPEED:
                    result.speed = (data[3] << 8) | data[4];
                    break;
                default:
                    result.action = asString(data);
            }
        } else {
            result.action = asString(data);
        }

        return result;
    },
};

const tz_uart = {
    key: ['action', 'move', 'calibration', 'calibrated', 'position', 'state', 'speed'],
    convertSet: async (entity, key, value, meta) => {
        let frame = [];
        switch (key) {
            case 'state':
                if (value === 'STOP') {
                    frame = [0x05, STX, 0x01, CMD_STOP, 0x01 ^ CMD_STOP, ETX];
                    break;
                }
                if (value === 'OPEN') {
                    frame = createPositionCmd(Number(100));
                    break;
                }
                if (value === 'CLOSE') {
                    frame = createPositionCmd(Number(0));
                    break;
                }
                break;
            case 'position':
                frame = createPositionCmd(Number(value));
                break;
            case 'move':
                frame = createMoveCmd(Number(value));
                break;
            case 'speed':
                frame = createSpeedCmd(Number(value));
                break;
            case 'calibration':
                if (value === 'closed') {
                    frame = [0x05, STX, 0x01, CMD_SET_MIN, 0x01 ^ CMD_SET_MIN, ETX];
                }
                if (value === 'opened') {
                    frame = [0x05, STX, 0x01, CMD_SET_MAX, 0x01 ^ CMD_SET_MAX, ETX];
                }
                if (value === 'reset') {
                    frame = [0x05, STX, 0x01, CMD_RESET_CAL, 0x01 ^ CMD_RESET_CAL, ETX];
                }
                break;
            case 'action':
                frame = value;
                break;
        }

        await entity.write('genMultistateValue', {
            14: { value: frame, type: 0x42 },
        });
    },
    convertGet: async (entity, key, meta) => {
        let frame = [];
        switch (key) {
            case 'position':
                frame = [0x05, STX, 0x01, CMD_GET_POS, 0x01 ^ CMD_GET_POS, ETX];
                break;
            case 'calibrated':
                frame = [0x05, STX, 0x01, CMD_GET_CAL, 0x01 ^ CMD_GET_CAL, ETX];
                break;
            case 'speed':
                frame = [0x05, STX, 0x01, CMD_GET_SPEED, 0x01 ^ CMD_GET_SPEED, ETX];
                break;
        }

        await entity.write('genMultistateValue', {
            14: { value: frame, type: 0x42 },
        });
    },
};

const device = {
    zigbeeModel: ['soucek-motor'],
    model: 'soucek-motor',
    vendor: 'andrejsoucek',
    description: 'Motor controller with calibration status',
    fromZigbee: [fz.ignore_basic_report, fz_uart],
    toZigbee: [tz_uart],
    exposes: [
        e.cover_position(),
        exposes.binary('calibrated', ea.STATE_GET).withDescription('Motor calibration status.'),
        exposes
            .numeric('speed', ea.ALL)
            .withDescription('Motor speed (0-500)')
            .withUnit('rpm')
            .withValueMin(0)
            .withValueMax(500),
        exposes
            .enum('move', ea.SET, ['-1600', '-800', '-400', '100', '400', '800', '1600'])
            .withDescription('Move the motor by desired amount of steps.'),
        exposes
            .enum('calibration', ea.SET, ['closed', 'opened', 'reset'])
            .withDescription('Set min and max limits of the motor.'),
        exposes.text('action', ea.STATE_SET).withDescription('UART commands'),
    ],
    meta: {
        multiEndpoint: true,
    },
    endpoint: (device) => ({
        l1: 1,
        action: 1,
        l2: 2,
    }),
    configure: async (device, coordinatorEndpoint, logger) => {
        const endpoint = device.getEndpoint(1);
        await endpoint.read('genBasic', ['modelId', 'swBuildId', 'powerSource']);
    },
};

module.exports = device;
