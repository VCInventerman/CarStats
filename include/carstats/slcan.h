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

class SLCan {
public:

    constexpr static unsigned long DEFAULT_SERIAL_TIMEOUT = 50; // Wait 250ms for commands to complete
    constexpr static unsigned long DEFAULT_SERIAL_BITRATE = 115200; // Wait 250ms for commands to complete
    constexpr static std::string_view DEFAULT_HELLO = "CAN reader\r\n";

    bool _sendTimestamp = false; // Whether to include a timestamp with each packet sent to serial
    bool _listenOnly = true; // Whether CAN packets can be sent on the bus
    bool _autoPoll = false;
    uint32_t _bitrate = 0; // Bitrate for the channel, if it is open
    char _nextCmd[50] = {};

    SLCan() {
        
    }

    void init() {
        Serial.setTimeout(DEFAULT_SERIAL_TIMEOUT);
        Serial.begin(DEFAULT_SERIAL_BITRATE);

        Serial.write(DEFAULT_HELLO.data(), DEFAULT_HELLO.size() - 1);
    }

    void sendStatusToHost(int status) {
        Serial.write(status);
    }

    void sendErrorToHost() {
        sendStatusToHost('\r'); // 13 CR for OK
    }

    void sendSuccessToHost() {
        sendStatusToHost(7); // BELL for ERROR
    }

    void handleShell() {
        while (Serial.available()) {
            size_t length = Serial.readBytesUntil(13, _nextCmd, sizeof(_nextCmd));

            if (length == 0) {
                return; // Uh oh
            }

            handleShellCommand(_nextCmd, length);
        }
    }

    void sendPacketToHost() {
        if (messageQueue.available()) {
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
            if (isExtended) {
                for (int i = 0; i < msg.msg.dlc; i++) {
                    *cur++ = msg.msg.data[i] >> 4;
                    *cur++ = msg.msg.data[i] & 0xF;
                }
            }
            else {
                *cur++ = binToHexChar(canId >> 8 & 0xF);
                *cur++ = binToHexChar(canId >> 4 & 0xF);
                *cur++ = binToHexChar(canId >> 0 & 0xF);
            }

            if (_sendTimestamp) {
                *cur++ = binToHexChar(msg.timestamp >> 12 & 0xF);
                *cur++ = binToHexChar(msg.timestamp >> 8 & 0xF);
                *cur++ = binToHexChar(msg.timestamp >> 4 & 0xF);
                *cur++ = binToHexChar(msg.timestamp >> 0 & 0xF);
            }

            *cur++ = '\r';

            Serial.write(out, cur - out);
        }
    }

    //todo: save persistent preferences
    void handleShellCommand(char* const cmd, const size_t length) {
        char* cur = cmd;

        // Checks if a condition is true, returning an error to the serial host if it is not and cancelling command parsing
        #define SHELL_ASSERT(cond) if (!(cond)) { Serial.print("ERROR ON LINE "); Serial.println(__LINE__); sendErrorToHost(); return; }

        #define EXPECT_BYTES(buf, len) if ((cmd + length) > (cur + (len))) \
            { memcpy(buf, cur, len); cur += (len); } else { Serial.print("ERROR ON LINE "); Serial.println(__LINE__); sendErrorToHost(); return; }

        #define EXPECT_BYTE(name) char name = 0; if (cur < cmd + length) { name = *cur++; } else { Serial.print("ERROR ON LINE "); Serial.println(__LINE__); sendErrorToHost(); return; }

        EXPECT_BYTE(first);

        if (first == 'S') {
            // Set CAN bitrate from dictionary

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

            // Not available when bus is open
            SHELL_ASSERT(!canbusIsOpen());

            _bitrate = rates[rate];
            SHELL_ASSERT(canbusSetup(_bitrate));
        }
        else if (first == 's') {
            // Set CAN bitrate (I don't know how to implement this algorithm, so it uses the single example given in the spec)

            char params[4] = {};
            EXPECT_BYTES(params, 4);

            SHELL_ASSERT(std::all_of(std::begin(params), std::end(params), [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }));

            int rate1 = hexCharToBin(params[0]) * 16 + hexCharToBin(params[1]);
            int rate2 = hexCharToBin(params[2]) * 16 + hexCharToBin(params[3]);

            // Not available when bus is open
            SHELL_ASSERT(!canbusIsOpen());

            if (rate1 == 0x03 && rate2 == 0x1C) {
                _bitrate = 125000;
                SHELL_ASSERT(canbusSetup(125000));
            }
            else {
                SHELL_ASSERT(false);
            }
        }
        else if (first == 'O') {
            SHELL_ASSERT(_bitrate != 0 && !canbusIsOpen());

            SHELL_ASSERT(canbusSetup(_bitrate));
        }
        else if (first == 'L') {
            SHELL_ASSERT(_bitrate != 0 && !canbusIsOpen());

            _listenOnly = true;
            SHELL_ASSERT(canbusSetup(_bitrate));
        }
        else if (first == 'C') {
            SHELL_ASSERT(canbusIsOpen());

            SHELL_ASSERT(false);

            //canbusShutdown(); // todo: this will not work
        }
        else if (first == 't') {
            SHELL_ASSERT(isNormalMode());

            char params[4] = {}; // 3 char id, 1 char len
            EXPECT_BYTES(params, 4);

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
        }
        else if (first == 'T') {
            SHELL_ASSERT(isNormalMode());

            char params[9] = {}; // 8 char id, 1 char len
            EXPECT_BYTES(params, 8);

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
        }
        else if (first == 'r') {
            SHELL_ASSERT(isNormalMode());

            char params[3] = {}; // 3 char id, 1 char len
            EXPECT_BYTES(params, 4);

            int id = hexCharToBin(params[0]) * 256 + hexCharToBin(params[1]) * 16 + hexCharToBin(params[2]);
            SHELL_ASSERT(id >= 0 && id <= 0x7FF);

            int len = params[3] - '0';
            SHELL_ASSERT(len >= 0 && len <= 8);

            can2040_msg msg = {};
            msg.id = id | CAN2040_ID_RTR;
            msg.dlc = len;

            SHELL_ASSERT(can2040_transmit(&cbus, &msg) == 0);
        }
        else if (first == 'R') {
            SHELL_ASSERT(isNormalMode());

            char params[9] = {}; // 8 char id, 1 char len
            EXPECT_BYTES(params, 8);

            int id = hexStrToNum(params, 8);
            SHELL_ASSERT(id >= 0 && id <= 0x1FFFFFFF);

            int len = params[8] - '0';
            SHELL_ASSERT(len >= 0 && len <= 8);

            can2040_msg msg = {};
            msg.id = id | CAN2040_ID_EFF | CAN2040_ID_RTR;
            msg.dlc = len;

            SHELL_ASSERT(can2040_transmit(&cbus, &msg) == 0);
        }
        else if (first == 'P') {
            SHELL_ASSERT(!_autoPoll);

            sendPacketToHost();
        }
        else if (first == 'A') {
            SHELL_ASSERT(!_autoPoll);

            while (messageQueue.available()) {
                sendPacketToHost();
            }
            
            Serial.write('A');
        }
        else if (first == 'F') {
            SHELL_ASSERT(canbusIsOpen());

            char resp[50] = {};
            resp[0] = 'F';

            char status = 0;
            //status |= () << 0; // not supported by ring buffer // CAN receive FIFO queue full
            status |= can2040_check_transmit(&cbus) << 1; // CAN transmit FIFO queue full
            status |= 0 << 2; // Error warning (EI)
            status |= 0 << 3; // Data Overrun (DOI)
            status |= 0 << 4; // Unused
            status |= 0 << 5; // Error Passive (EPI)
            status |= 0 << 6; // Arbitration Lost (ALI)
            status |= 0 << 7; // Bus error (BEI)

            resp[1] = binToHexChar(status >> 4 & 0xF);
            resp[2] = binToHexChar(status >> 0 & 0xF);
        }
        else if (first == 'X') {
            EXPECT_BYTE(mode);

            SHELL_ASSERT(mode == '0' || mode == '1')

            _autoPoll = mode == '1';
        }
        else if (first == 'W') {
            SHELL_ASSERT(false);
        }
        else if (first == 'M') {
            //todo: implement acceptance code register
            SHELL_ASSERT(false);
        }
        else if (first == 'm') {
            SHELL_ASSERT(false);
        }
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

            SHELL_ASSERT(false);

            //Serial.end(); // NOT IMPLEMENTED ON RP2040
            //Serial.begin(speeds[speed]);
        }
        else if (first == 'V') {
            Serial.write("V1013", 5); // Sample from spec
        }
        else if (first == 'N') {
            Serial.write("NA123", 5); // Sample from spec, not unique
        }
        else if (first == 'Z') {
            EXPECT_BYTE(mode);

            SHELL_ASSERT(mode == '0' || mode == '1')

            _sendTimestamp = mode == '1';
        }
        else if (first == 'Q') {
            EXPECT_BYTE(mode);

            SHELL_ASSERT(mode == '0' || mode == '1' || mode == '2')

            SHELL_ASSERT(false);
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

        if (_autoPoll) {
            while (messageQueue.available()) {
                    sendPacketToHost();
            }
        }
    }
};

#endif