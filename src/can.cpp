#include "pico/cyw43_arch.h"

#include <RingBuf.h>
extern "C" {
    #include <can2040.h>
}

#include "carstats/can.h"

//tccollection::GenericCircularBuffer<can2040_msg> messagePool(10, tccollection::GenericCircularBuffer<can2040_msg>::MEMORY_POOL);
//tccollection::GenericCircularBuffer<can2040_msg*> messageQueue(10); 

//can2040 cbus;
//can2040 cbus2;


void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg)
{
    if (notify == CAN2040_NOTIFY_RX) {
        //messages.lockedPush(*msg);

        //static bool led = false;
        //if (msg->data32[0] == 0) {led = !led;
        //cyw43_arch_gpio_put(0, led);
        //}

        //blink = !blink;

        CanMsg& buf = messagePool.get();
        memcpy(&buf.msg, msg, sizeof(can2040_msg));
        buf.timestamp = millis() % 60000;
        messageQueue.put(&buf);

        ++canRxCount;
    }
    else if (notify == CAN2040_NOTIFY_TX) {
        ++canTxCount;
    }
    else if (notify == CAN2040_NOTIFY_ERROR) {
        ++canErrorCount;

        //messages.lockedPush(*msg);
    }



    //messages.push(*msg);
    
    //usedCore.push(get_core_num());
}

void PIO1_IRQHandler()
{
    can2040_pio_irq_handler(&cbus);
}

void PIO2_IRQHandler()
{
    can2040_pio_irq_handler(&cbus2);
}

int _bitrate = 0;
void canbusSetupImpl()
{
    setupStage = 1;
    //Serial.printf("SETUP WITH btr %d\r", _bitrate);
    sleep_ms(500);
    setupStage = 2;

    int bitrate = _bitrate;

    uint32_t pio_num = 0;
    uint32_t sys_clock = F_CPU;
    uint32_t gpio_rx = 4, gpio_tx = 5;
    setupStage = 3;

    // Setup canbus
    can2040_setup(&cbus, pio_num);
    setupStage = 4;
    can2040_callback_config(&cbus, can2040_cb);
    setupStage = 5;

    // Enable irqs
    irq_set_exclusive_handler(PIO0_IRQ_0_IRQn, PIO1_IRQHandler);
    setupStage = 6;
    NVIC_SetPriority(PIO0_IRQ_0_IRQn, 1);
    setupStage= 7;
    NVIC_EnableIRQ(PIO0_IRQ_0_IRQn);
    setupStage = 8;

    // Start canbus
    can2040_start(&cbus, sys_clock, (uint32_t)bitrate, gpio_rx, gpio_tx);
    setupStage = 9;



    uint32_t pio2_num = 1;
    uint32_t sys2_clock = F_CPU;
    uint32_t gpio2_rx = 6, gpio2_tx = 7;

    // Setup canbus
    can2040_setup(&cbus2, pio2_num);
    setupStage = 10;
    can2040_callback_config(&cbus2, can2040_cb);
    setupStage = 11;

    // Enable irqs
    irq_set_exclusive_handler(PIO1_IRQ_0_IRQn, PIO2_IRQHandler);
    setupStage = 12;
    //irq_add_shared_handler(PIO1_IRQ_0_IRQn, PIO2_IRQHandler, 1);
    NVIC_SetPriority(PIO1_IRQ_0_IRQn, 1);
    setupStage= 13;
    NVIC_EnableIRQ(PIO1_IRQ_0_IRQn);
    setupStage = 14;
    // Start canbus
    can2040_start(&cbus2, sys2_clock, (uint32_t)bitrate, gpio2_rx, gpio2_tx);
    
    //todo: check for irqs already open, etc
    
    setupStage = 15;
    
}

bool canbusSetup(int bitrate) {
    barrier();
    _bitrate = bitrate;
    barrier();
    enableCanbus = true;
    barrier();
    //multicore_launch_core1(canbusSetupImpl);
    return true;
}

void canbusShutdown() {
    // can2040_shutdown(&cbus); // no-op

    //NVIC_DisableIRQ(PIO1_IRQ_1_IRQn);
    //irq_clear(PIO1_IRQ_1_IRQn);*/
}

