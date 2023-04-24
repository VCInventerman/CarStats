#include <charconv>

#include "pico/cyw43_arch.h"

#include "carstats/server.h"
#include "carstats/can.h"
#include "carstats/slcan.h"

#define CAR_BITRATE 500000
#define ROBOT_BITRATE 1000000

int failCnt = 0;
int prevMs = 0;

std::optional<HttpServer> server;

SLCan can;

// Prevent initialization of cyw43 with international country in picow_init.cpp
//extern "C" void initVariant() {}

void setup2();
void loop2();

void main2() {
    rp2040.fifo.registerCore();
    if (setup2) {
        setup2();
    }
    while (true) {
        if (loop2) {
            loop2();
        }
    }
}

void setup() {
  can.init();

  prevMs = millis();
}

void loop() {
  uint64_t currentMs = millis();

  /*if (messageQueue.available()) {
        //can2040_msg msg;
        //messages.lockedPop(msg);
        CanMsg& msg = *messageQueue.get();


        char buf[256] = {};
        int size = snprintf(buf, 255, "%u %u %X %X\n", msg.id, msg.dlc, msg.data32[0], msg.data32[1]);
        //int size = snprintf(buf, sizeof(buf), "%u core<br>", i);
        Serial.write(buf, size);
  }*/

  //server->run();


  can.run();

  if (currentMs - prevMs > 1000) {
    prevMs = currentMs;

    static bool led = false;
    //led = !led;    
    //cyw43_arch_gpio_put(0, false);
    //cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);

    can2040_msg msg = {};
    msg.id = 0xFFFFFFFFu;
    msg.dlc = 5;
    msg.data32[0] = 1;
    msg.data32[1] = 1;

    if (cbus.rx_cb && can2040_transmit(&cbus, &msg) != 0) {
      failCnt++;
      led = !led;
      cyw43_arch_gpio_put(0, led);
    }
  }

}



void setup2() {
  
}

void loop2() {
  tight_loop_contents();
}