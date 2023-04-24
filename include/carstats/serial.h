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

struct SLCan {

    constexpr static unsigned long DEFAULT_SERIAL_TIMEOUT = 50; // Wait 250ms for commands to complete
    constexpr static unsigned long DEFAULT_SERIAL_BITRATE = 115200; // Wait 250ms for commands to complete
    constexpr static std::string_view DEFAULT_HELLO = "CAN reader\r\n";

    bool _timestampEnabled = false; // Whether to include a timestamp with each packet sent to serial
    bool _listenOnly = true; // Whether CAN packets can be sent on the bus
    uint32_t _bitrate = 0; // Bitrate for the channel, if it is open
    char _nextCmd[50] = {};

    SLCan() {
        
    }

    void init() {
        Serial.setTimeout(DEFAULT_SERIAL_TIMEOUT);
        Serial.begin(DEFAULT_SERIAL_BITRATE);

        Serial.write(DEFAULT_HELLO.data(), DEFAULT_HELLO.size() - 1);
    }

    // Check for packets coming from the attached CAN bus
    void handleCanPacketIn() {
        if (messageQueue.available()) {
            can2040_msg& msg = *messageQueue.get();

            char buf[256] = {};
            int size = snprintf(buf, 255, "%u %u %X %X\n", msg.id, msg.dlc, msg.data32[0], msg.data32[1]);

            


            Serial.write(buf, size);
        }
    }

    void sendStatusToHost(int status) {
        Serial.write(status);
    }

    void sendErrorToHost() {
        sendStatusToHost(13); // CR for OK
    }

    void sendSuccessToHost() {
        sendStatusToHost(7); // BELL for ERROR
    }

    // Wait up to a short period to get length bytes over the serial bus
    bool expectToRead(char* inBuf, char* outBuf, size_t length) {
        size_t read = Serial.readBytes(buf, length);

        return read == length;
    }

    template <typename IterT, typename CondT>
    bool matches(IterT begin, IterT end, CondT cond) {
        for (auto i = begin; i != end; ++i) {
            if (!cond(*i)) { 
                return false; 
                }
        }

        return true;
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

    void handleShellCommand(char* const cmd, const size_t length) {
        char* cur = cmd;

        // Checks if a condition is true, returning an error to the serial host if it is not and cancelling command parsing
        #define SHELL_ASSERT(cond) if (!(cond)) { sendErrorToHost(); return; }

        #define EXPECT_BYTES(buf, len) if ((cmd + length) > (cur + (len))) \
            { memcpy(buf, cur, len); cur += (len); } else { sendErrorToHost(); return; }

        #define EXPECT_BYTE(name) char name = 0; if (cur < cmd + length) { name = *cur++; } else { sendErrorToHost(); return; }

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

            SHELL_ASSERT(matches(std::begin(params), std::end(params), [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')}));

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

            char params[3] = {}; // 3 char id, 1 char len
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
        else if (first == 'T') {
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



        #undef SHELL_ASSERT

    }

    bool isNormalMode() {
        return !_listenOnly;
    }

    bool canbusIsOpen() {
        return cbus.rx_cb != nullptr;
    }
};

#endif