#ifndef CARSTATS_SERIAL_H
#define CARSTATS_SERIAL_H

#include "carstats/can.h"

char binToHexChar(uint8_t binary) {
    assert(binary < 16);

    static constexpr const char table[] = 
        { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    return table[binary];
}

int hexCharToBin(char hex) {
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    }
    else if (hex >= 'A' && hex <= 'F') {
        return hex - 'A' + 10;
    }
    else if (hex >= 'a' && hex <= 'f') {
        return hex - 'a' + 10;
    }
    else {
        // Failure to convert
        return 0;
    }
}

int hexStrToNum(const char* str, size_t len) {
    int pow = len - 2;
    int val = 0;

    for (int i = 0; i < len; i++) {
        int o = hexCharToBin(str[len - i - 1]);
        val += o << (pow * 4);

        pow -= 1;
    }

    return val;
}

bool isHexStr(const char* str, size_t len) {
    return std::all_of(str, str + len, [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); });
}

struct SLCanConfig {
    enum class AutoStartupMode : uint8_t {
        DISABLED,
        NORMAL,
        LISTEN
    };

    bool autoPoll; // Whether to send packets without polling
    bool dualFilter;
    uint32_t baudRate;
    bool sendTimestamp; // Whether to include a timestamp with each packet sent to serial
    AutoStartupMode autoStartupMode; // Mode to automatically load when device starts

    static SLCanConfig defaults() {
        return { false, true, 9600, false, AutoStartupMode::DISABLED,};
    }
};

class SLCan {
public:
    constexpr static unsigned long DEFAULT_SERIAL_TIMEOUT = 50; // Wait 250ms for commands to complete
    constexpr static unsigned long DEFAULT_SERIAL_BITRATE = 9600; // Wait 250ms for commands to complete
    constexpr static std::string_view DEFAULT_HELLO = "CAN reader\r";

    bool _listenOnly = true; // Whether CAN packets can be sent on the bus
    uint32_t _bitrate = 0; // Bitrate for the channel, if it is open

    SLCanConfig _config;

    char _nextCmd[50] = {};
    size_t _cmdLength = 0;

    SLCan() {
        config = SLCanConfig::defaults();
    }

    void init() {
        Serial.setTimeout(DEFAULT_SERIAL_TIMEOUT);
        //Serial.begin(DEFAULT_SERIAL_BITRATE); // Does nothing since the tinyusb serial bus is both started by default and ignorant of bitrate (always 115200)

        Serial.write(DEFAULT_HELLO.data(), DEFAULT_HELLO.size() - 1);
    }

    void sendStatusToHost(int status) {
        Serial.write(status);
    }

    void sendSuccessToHost() {
        sendStatusToHost('\r'); // 13 CR for OK
    }

    void sendErrorToHost() {
        sendStatusToHost(7); // BELL for ERROR
    }

    void handleShell() {
        while (Serial.available() != 0) {
            char next = Serial.read();

            if (next == -1) {
                return;
            }
            if (next == '\r') {
                if (_cmdLength > 0) {
                    handleShellCommand(_nextCmd, _cmdLength);
                    _cmdLength = 0;
                }
            }
            else {
                _nextCmd[_cmdLength++] = next;
            }
        }
    }

    // Requires that a packet be waiting in messageQueue
    void sendPacketToHost() {
        char out[50] = {};
        char* cur = out;

        CanMsg& msg = *messageQueue.get();

        bool remoteRequest = bool(msg.msg.id & CAN2040_ID_RTR);
        bool isExtended = bool(msg.msg.id & CAN2040_ID_EFF);

        *cur = remoteRequest ? 'r' : 't';
        *cur++ ^= isExtended ? ' ' : 0;

        uint32_t canId = msg.msg.id & ~(CAN2040_ID_RTR | CAN2040_ID_EFF);

        // ID
        if (isExtended) {
            for (int i = 28; i > 0; i -= 4) {
                *cur++ = binToHexChar(canId >> i & 0xF);
            }
        }
        else {
            *cur++ = binToHexChar(canId >> 8 & 0xF);
            *cur++ = binToHexChar(canId >> 4 & 0xF);
            *cur++ = binToHexChar(canId >> 0 & 0xF);
        }

        *cur++ = msg.msg.dlc + '0';

        // Data
        for (int i = msg.msg.dlc * 4; i > 0; i -= 4) {
            *cur++ = binToHexChar(canId >> i & 0xF);
        }

        if (_config.sendTimestamp) {
            *cur++ = binToHexChar(msg.timestamp >> 12 & 0xF);
            *cur++ = binToHexChar(msg.timestamp >> 8 & 0xF);
            *cur++ = binToHexChar(msg.timestamp >> 4 & 0xF);
            *cur++ = binToHexChar(msg.timestamp >> 0 & 0xF);
        }

        *cur++ = '\r';

        Serial.write(out, cur - out);
    }

    //todo: save persistent preferences
    void handleShellCommand(char* const cmd, const size_t length) {
        char* cur = cmd;

        // Checks if a condition is true, returning an error to the serial host if it is not and cancelling command parsing
        #define SHELL_ASSERT(cond) if (!(cond)) { Serial.printf("ERROR ON LINE %d\r", __LINE__); sendErrorToHost(); return; }

        #define EXPECT_BYTES(buf, len) if ((cmd + length) > (cur + (len))) \
            { memcpy(buf, cur, len); cur += (len); } else { Serial.printf("ERROR ON LINE %d\r", __LINE__); sendErrorToHost(); return; }

        #define EXPECT_BYTE(name) char name = 0; if (cur < cmd + length) { name = *cur++; } else { Serial.printf("ERROR ON LINE %d\r", __LINE__); sendErrorToHost(); return; }

        // Every command has a unique first letter
        EXPECT_BYTE(first);

        // Set CAN bitrate from dictionary
        if (first == 'S') {
            // Not available when bus is open
            SHELL_ASSERT(!canbusIsOpen());

            constexpr static const uint32_t rates[] = {
                10000,
                20000,
                50000,
                100000,
                125000,
                250000,
                500000,
                800000,
                1000000
            };

            EXPECT_BYTE(rateByte)
            int rate = rateByte - '0';
            SHELL_ASSERT(rate >= 0 && rate <= 8);

            _bitrate = rates[rate];
        }
        // Set CAN bitrate with an equation
        else if (first == 's') {
            // Not available when bus is open
            SHELL_ASSERT(!canbusIsOpen());

            char params[4] = {};
            EXPECT_BYTES(params, 4);

            // Check that all 4 parameters are hex
            SHELL_ASSERT(isHexStr(params, 4));

            int rate1 = hexCharToBin(params[0]) * 16 + hexCharToBin(params[1]);
            int rate2 = hexCharToBin(params[2]) * 16 + hexCharToBin(params[3]);

            if (rate1 == 0x03 && rate2 == 0x1C) {
                _bitrate = 125000;
            }
            else {
                // I don't know how to implement this algorithm, so it uses the single example given in the spec

                SHELL_ASSERT(false);
            }
        }
        // Open CAN bus
        else if (first == 'O') {
            SHELL_ASSERT(_bitrate != 0 && !canbusIsOpen());

            SHELL_ASSERT(canbusSetup(_bitrate));
        }
        // Open CAN bus in listen-only mode
        else if (first == 'L') {
            SHELL_ASSERT(_bitrate != 0 && !canbusIsOpen());

            _listenOnly = true;
            SHELL_ASSERT(canbusSetup(_bitrate));
        }
        // Shutdown CAN bus
        else if (first == 'C') {
            SHELL_ASSERT(canbusIsOpen());

            SHELL_ASSERT(false);

            //canbusShutdown(); // todo: implement shutdown
        }
        // Transmit standard frame on CAN bus
        else if (first == 't') {
            SHELL_ASSERT(isNormalMode());

            char params[4] = {}; // 3 char id, 1 char len
            EXPECT_BYTES(params, 4);
            
            SHELL_ASSERT(isHexStr(params, 3));
            int id = hexCharToBin(params[0]) * 256 + hexCharToBin(params[1]) * 16 + hexCharToBin(params[2]);
            SHELL_ASSERT(id >= 0 && id <= 0x7FF);

            int len = params[3] - '0';
            SHELL_ASSERT(len >= 0 && len <= 8);

            char data[8 * 2] = {};
            EXPECT_BYTES(data, len * 2);

            uint8_t bytes[8] = {};
            for (int i = 0; i < len; i++) {
                bytes[i] = hexCharToBin(data[i * 2]) * 16 + hexCharToBin(params[i * 2 + 1]);
            }

            can2040_msg msg = {};
            msg.id = id;
            msg.dlc = len; // dlc is length in bytes of data section
            memcpy(msg.data, bytes, len);

            SHELL_ASSERT(can2040_transmit(&cbus, &msg) == 0);

            if (_config.autoPoll) {
                Serial.write('z');
            }
        }
        // Send extended frame on CAN bus
        else if (first == 'T') {
            SHELL_ASSERT(isNormalMode());

            char params[9] = {}; // 8 char id, 1 char len
            EXPECT_BYTES(params, 8);

            SHELL_ASSERT(isHexStr(params, 8));
            int id = hexStrToNum(params, 8);
            SHELL_ASSERT(id >= 0 && id <= 0x1FFFFFFF);

            int len = params[8] - '0';
            SHELL_ASSERT(len >= 0 && len <= 8);

            char data[8 * 2] = {};
            EXPECT_BYTES(data, len * 2);

            uint8_t bytes[8] = {};
            for (int i = 0; i < len; i++) {
                bytes[i] = hexCharToBin(data[i * 2]) * 16 + hexCharToBin(params[i * 2 + 1]);
            }

            can2040_msg msg = {};
            msg.id = id | CAN2040_ID_EFF;
            msg.dlc = len; // dlc is length in bytes of data section
            memcpy(msg.data, bytes, len);

            SHELL_ASSERT(can2040_transmit(&cbus, &msg) == 0);

            if (_config.autoPoll) {
                Serial.write('Z');
            }
        }
        // Send a standard RTR (retransmit) frame on the CAN bus
        else if (first == 'r') {
            SHELL_ASSERT(isNormalMode());

            char params[4] = {}; // 3 char id, 1 char len
            EXPECT_BYTES(params, 4);

            SHELL_ASSERT(isHexStr(params, 3));
            int id = hexCharToBin(params[0]) * 256 + hexCharToBin(params[1]) * 16 + hexCharToBin(params[2]);
            SHELL_ASSERT(id >= 0 && id <= 0x7FF);

            int len = params[3] - '0';
            SHELL_ASSERT(len >= 0 && len <= 8);

            can2040_msg msg = {};
            msg.id = id | CAN2040_ID_RTR;
            msg.dlc = len;

            SHELL_ASSERT(can2040_transmit(&cbus, &msg) == 0);
        }
        // Send an extended RTR (retransmit) frame on the CAN bus
        else if (first == 'R') {
            SHELL_ASSERT(isNormalMode());

            char params[9] = {}; // 8 char id, 1 char len
            EXPECT_BYTES(params, 8);

            SHELL_ASSERT(isHexStr(params, 8));
            int id = hexStrToNum(params, 8);
            SHELL_ASSERT(id >= 0 && id <= 0x1FFFFFFF);

            int len = params[8] - '0';
            SHELL_ASSERT(len >= 0 && len <= 8);

            can2040_msg msg = {};
            msg.id = id | CAN2040_ID_EFF | CAN2040_ID_RTR;
            msg.dlc = len;

            SHELL_ASSERT(can2040_transmit(&cbus, &msg) == 0);
        }
        // Poll for CAN frames
        else if (first == 'P') {
            SHELL_ASSERT(!_config.autoPoll && canbusIsOpen());

            if (messageQueue.available()) {
                sendPacketToHost();
                return; // Use the single CR provided by sendPacketToHost()
            }
        }
        // Poll for all CAN frames
        else if (first == 'A') {
            SHELL_ASSERT(!_config.autoPoll && canbusIsOpen());

            while (messageQueue.available()) {
                sendPacketToHost();
            }
            
            Serial.write('A');
        }
        // Read status flags
        else if (first == 'F') {
            SHELL_ASSERT(canbusIsOpen());

            char resp[3] = {};
            resp[0] = 'F';

            char status = 0;
            //status |= () << 0; // not supported by ring buffer // CAN receive FIFO queue full
            status |= can2040_check_transmit(&cbus) << 1; // CAN transmit FIFO queue full
            status |= 0 << 2; // Error Warning (EI)
            status |= (canErrorCount != 0) << 3; // Data Overrun (DOI)
            status |= 0 << 4; // Unused
            status |= 0 << 5; // Error Passive (EPI)
            status |= 0 << 6; // Arbitration Lost (ALI)
            status |= 0 << 7; // Bus error (BEI)

            resp[1] = binToHexChar(status >> 4 & 0xF);
            resp[2] = binToHexChar(status >> 0 & 0xF);

            Serial.write(resp);
        }
        // Set auto poll (persistent)
        else if (first == 'X') {
            SHELL_ASSERT(!canbusIsOpen())

            EXPECT_BYTE(mode);

            SHELL_ASSERT(mode == '0' || mode == '1')

            _config.autoPoll = mode == '1';
            updateConfig();
        }
        // Set filter mode (persistent)
        else if (first == 'W') {
            SHELL_ASSERT(false);

            EXPECT_BYTE(mode);
            SHELL_ASSERT(mode == '0' || mode == '1')

            _config.dualFilter = mode == '0';
            updateConfig();
        }
        // Set acceptance code register
        else if (first == 'M') {
            SHELL_ASSERT(_bitrate != 0 && !canbusIsOpen());

            union {
                uint32_t full,
                char bytes[4]
            } acceptanceCode;

            char params[8] = {};
            EXPECT_BYTES(params, 8);

            SHELL_ASSERT(isHexStr(params, 8));
            for (int i = 0; i < 4; i++) {
                bytes[i] = hexCharToBin(params[i * 2 + 1]) * 16 + hexCharToBin(params[i * 2])
            }
        }
        // Set acceptance mask register
        else if (first == 'm') {
            SHELL_ASSERT(_bitrate != 0 && !canbusIsOpen());

            union {
                uint32_t full,
                char bytes[4]
            } acceptanceMask;

            char params[8] = {};
            EXPECT_BYTES(params, 8);

            SHELL_ASSERT(isHexStr(params, 8));
            for (int i = 0; i < 4; i++) {
                acceptanceMask.bytes[i] = hexCharToBin(params[i * 2 + 1]) * 16 + hexCharToBin(params[i * 2])
            }
        }
        // Set serial baud rate (persistent)
        // DOES NOTHING ON RP2040
        else if (first == 'U') {
            constexpr const static int speeds[] = {
                230400,
                115200,
                57600,
                38400,
                19200,
                9600,
                2400
            };

            EXPECT_BYTE(speed);
            speed -= '0';

            SHELL_ASSERT(speed >= 0 && speed <= 7);

            _config.baudRate = speeds[speed];
            updateConfig();

            SHELL_ASSERT(false);

            //Serial.end(); // NOT IMPLEMENTED ON RP2040
            //Serial.begin(speeds[speed]);
        }
        // Get software and hardware version
        else if (first == 'V') {
            Serial.write("V1013", 5); // Sample from spec
        }
        // Get serial number
        else if (first == 'N') {
            Serial.write("NA123", 5); // Sample from spec, not unique
        }
        // Enable/disable timestamps (persistent)
        else if (first == 'Z') {
            SHELL_ASSERT(!canbusIsOpen());

            EXPECT_BYTE(mode);

            SHELL_ASSERT(mode == '0' || mode == '1')

            _config.sendTimestamp = mode == '1';
            updateConfig();
        }
        // Auto startup (persistent)
        else if (first == 'Q') {
            EXPECT_BYTE(mode);

            SHELL_ASSERT(mode == '0' || mode == '1' || mode == '2');

            SLCanConfig::AutoStartupMode map[3] = {
                SLCanConfig::AutoStartupMode::DISABLED,
                SLCanConfig::AutoStartupMode::NORMAL,
                SLCanConfig::AutoStartupMode::LISTEN,
            };

            _config.autoStartupMode = map[mode - '0'];
            updateConfig();
        }
        // Dump internal counters (not in spec)
        else if (first == 'D') {
            Serial.printf("rx: %d, tx: %d, err: %d", canRxCount, canTxCount, canErrorCount);
        }

        // Assume success (send a newline) if a command did not exit early
        sendSuccessToHost();

        #undef SHELL_ASSERT
        #undef EXPECT_BYTES
        #undef EXPECT_BYTE
    }

    bool isNormalMode() {
        return !_listenOnly;
    }

    bool canbusIsOpen() {
        return cbus.rx_cb != nullptr;
    }

    void run() {
        handleShell();

        if (_config.autoPoll) {
            while (messageQueue.available()) {
                    sendPacketToHost();
            }
        }
    }

    void updateConfig() {
        // todo: commit to flash
    }
};

#endif