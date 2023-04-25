#ifndef CARSTATS_CAN_H
#define CARSTATS_CAN_H

#include <SCCircularBuffer.h>

extern "C" {
#include <can2040.h>
}

#define barrier() __asm__ __volatile__("": : :"memory")

inline struct can2040 cbus = {};
inline struct can2040 cbus2 = {};
//extern bool blink;


//extern RingBuf<can2040_msg, 10> messages;
//extern RingBuf<uint, 1024> usedCore;

struct CanMsg {
    can2040_msg msg;
    int timestamp;
};

inline tccollection::GenericCircularBuffer<CanMsg> messagePool(10, tccollection::GenericCircularBuffer<CanMsg>::MEMORY_POOL);
inline tccollection::GenericCircularBuffer<CanMsg*> messageQueue(10); 

inline volatile int canErrorCount = 0;
inline volatile int canRxCount = 0;
inline volatile int canTxCount = 0;

inline volatile bool enableCanbus = false;

bool canbusSetup(int bitrate);
void canbusSetupImpl();
void canbusShutdown();

#endif