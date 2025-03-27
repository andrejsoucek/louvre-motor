const exposes = require('zigbee-herdsman-converters/lib/exposes');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');

const ea = exposes.access;
const e = exposes.presets;

// Protocol constants
const STX = 0xAA;
const CMD_MOVE = 0x01;
const CMD_SET_MIN = 0x03;
const CMD_SET_MAX = 0x04;
const CMD_GET_POS = 0x05;
const CMD_GET_CAL = 0x07;
const ETX = 0x55;

function createMoveCmd(steps) {
    // Protocol frame structure: [STX, LEN, CMD, STEPS (4 bytes), CHECKSUM, ETX]
    // Convert steps to 32-bit signed integer (4 bytes)
    const stepBytes = new Uint8Array(4);
    stepBytes[0] = (steps >> 24) & 0xFF;
    stepBytes[1] = (steps >> 16) & 0xFF;
    stepBytes[2] = (steps >> 8) & 0xFF;
    stepBytes[3] = steps & 0xFF;

    // Calculate checksum (XOR of LEN + CMD + all step bytes)
    const len = 0x05; // 1 byte (CMD) + 4 bytes (steps)
    let checksum = len ^ CMD_MOVE;
    for (const byte of stepBytes) {
        checksum ^= byte;
    }

    const cmd = [STX, len, CMD_MOVE, ...stepBytes, checksum, ETX];
    return [cmd.length, ...cmd];
}

const fz_uart = {
    cluster: "genMultistateValue",
    type: ["attributeReport", "readResponse"],
    convert: (model, msg, publish, options, meta) => {
        const result = {};
        const data = msg.data.stateText;

        if (data.length >= 5 && data[0] === STX && data[data.length-1] === ETX) {
            const checksum = data.slice(1, -2).reduce((sum, byte) => sum ^ byte, 0);
            if (checksum !== data[data.length-2]) {
                return
            }
            // TODO switch
            if (data[2] === CMD_GET_POS) {
                result.position = data[3]
            }
            if (data[2] === CMD_GET_CAL) {
                result.calibration_status = data[3] === 0x01;
            }
        } else {
            // Original text handling
            let text = '';
            for (const byte of data) {
                text += byte >= 32 && byte <= 127 ? String.fromCharCode(byte) : `\\x${byte.toString(16).padStart(2, '0')}`;
            }
            result.action = text;
        }

        return result;
    },
};

const tz_uart = {
    key: ["action", "move", "calibrate", "calibration_status"],
    convertSet: async (entity, key, value, meta) => {
        //TODO switch
        if (key === 'move') {
            const payload = {14: {value: createMoveCmd(Number(value)), type: 0x42}};
            await entity.write('genMultistateValue', payload);
        }
        if (key === 'calibrate') {
            if (value === "min") {
                const payload = {14: {value: [0x05, STX, 0x02, CMD_SET_MIN, 0x02 ^ CMD_SET_MIN, ETX], type: 0x42}};
                await entity.write('genMultistateValue', payload);
            }
            if (value === 'max') {
                const payload = {14: {value: [0x05, STX, 0x02, CMD_SET_MAX, 0x02 ^ CMD_SET_MAX, ETX], type: 0x42}};
                await entity.write('genMultistateValue', payload);
            }
        }
        if (key === 'action' && value) {
            const payload = {14: {value, type: 0x42}};
            await entity.write('genMultistateValue', payload);
        }
    },
    convertGet: async (entity, key, meta) => {
        if (key === 'calibration_status') {
            const frame = [
                0x05,
                STX, 
                0x02,       // Length (CMD + checksum)
                CMD_GET_CAL,
                0x02 ^ CMD_GET_CAL, // Checksum
                ETX
            ];
            
            await entity.write('genMultistateValue', {
                14: {value: frame, type: 0x42}
            });
        }
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
        exposes.enum('move', ea.SET, ["100", "400", "800", "1600"]).withDescription("Move the motor by desired amount of steps."),
        e.cover_position(),
        exposes.enum('calibration_status', ea.STATE_GET, ['calibrated', 'uncalibrated'])
            .withDescription('Motor calibration status.'),
        exposes.enum('calibrate', ea.SET, ["min", "max"]).withDescription("Set min and max limits of the motor."),
        exposes.text('action', ea.STATE_SET)
            .withDescription('UART commands'),
    ],
    meta: {
        multiEndpoint: true,
    },
    endpoint: (device) => ({
        l1: 1, action: 1, l2: 2,
    }),
    configure: async (device, coordinatorEndpoint, logger) => {
        const endpoint = device.getEndpoint(1);
        await endpoint.read('genBasic', ['modelId', 'swBuildId', 'powerSource']);
    },
};

module.exports = device;

