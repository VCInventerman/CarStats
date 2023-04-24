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
        canErrorCount++;

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
    int bitrate = _bitrate;

    uint32_t pio_num = 0;
    uint32_t sys_clock = 125000000;
    uint32_t gpio_rx = 4, gpio_tx = 5;

    // Setup canbus
    can2040_setup(&cbus, pio_num);
    can2040_callback_config(&cbus, can2040_cb);

    // Enable irqs
    irq_set_exclusive_handler(PIO0_IRQ_0_IRQn, PIO1_IRQHandler);
    NVIC_SetPriority(PIO0_IRQ_0_IRQn, 1);
    NVIC_EnableIRQ(PIO0_IRQ_0_IRQn);

    // Start canbus
    can2040_start(&cbus, sys_clock, (uint32_t)bitrate, gpio_rx, gpio_tx);



    uint32_t pio2_num = 1;
    uint32_t sys2_clock = 125000000;
    uint32_t gpio2_rx = 6, gpio2_tx = 7;

    // Setup canbus
    can2040_setup(&cbus2, pio2_num);
    can2040_callback_config(&cbus2, can2040_cb);

    // Enable irqs
    irq_set_exclusive_handler(PIO1_IRQ_0_IRQn, PIO2_IRQHandler);
    //irq_add_shared_handler(PIO1_IRQ_0_IRQn, PIO2_IRQHandler, 1);
    NVIC_SetPriority(PIO1_IRQ_0_IRQn, 1);
    NVIC_EnableIRQ(PIO1_IRQ_0_IRQn);

    // Start canbus
    can2040_start(&cbus2, sys2_clock, (uint32_t)bitrate, gpio2_rx, gpio2_tx);
    
    //todo: check for irqs already open, etc
}

bool canbusSetup(int bitrate) {
    _bitrate = bitrate;
    multicore_launch_core1(canbusSetupImpl);
    return true;
}

void canbusShutdown() {
    /*can2040_shutdown(&cbus);

    NVIC_DisableIRQ(PIO1_IRQ_1_IRQn);
    irq_clear(PIO1_IRQ_1_IRQn);*/
}