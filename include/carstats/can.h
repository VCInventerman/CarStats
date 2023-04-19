#ifndef CARSTATS_CAN_H
#define CARSTATS_CAN_H

#include <SCCircularBuffer.h>

extern "C" {
#include <can2040.h>
}

inline struct can2040 cbus;
inline struct can2040 cbus2;
//extern bool blink;


//extern RingBuf<can2040_msg, 10> messages;
//extern RingBuf<uint, 1024> usedCore;

inline tccollection::GenericCircularBuffer<can2040_msg> messagePool(10, tccollection::GenericCircularBuffer<can2040_msg>::MEMORY_POOL);
inline tccollection::GenericCircularBuffer<can2040_msg*> messageQueue(10); 

inline volatile int canErrorCount = 0;
inline volatile int canRxCount = 0;
inline volatile int canTxCount = 0;

void canbusSetup(int bitrate);
void canbusShutdown();

#endif