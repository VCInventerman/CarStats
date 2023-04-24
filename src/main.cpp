#include <charconv>

#include "pico/cyw43_arch.h"

#include "carstats/server.h"
#include "carstats/can.h"
#include "carstats/serial.h"

#define CAR_BITRATE 500000
#define ROBOT_BITRATE 1000000

int failCnt = 0;
int prevMs = 0;

std::optional<HttpServer> server;

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
  //waitConnectToWifi();

  while (true) {Serial.println();}

  const static constexpr HttpPathHandler handlers[] = {
    { "/failCnt", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", failCnt);
      client._client.write(buf, size);
      client.close();
      }},
    { "/time", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", millis());
      client._client.write(buf, size);
      client.close();
      }},
    { "/canErrorCount", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", canErrorCount);
      client._client.write(buf, size);
      client.close();
      }},
    { "/canRxCount", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", canRxCount);
      client._client.write(buf, size);
      client.close();
      }},
    { "/canTxCount", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", canTxCount);
      client._client.write(buf, size);
      client.close();
      }},
    
    { "/can1", HttpPathHandler::SEND_HEADER_AND_RESP, [](void* state, HttpClient& client) {
      //client._client.write("Hello<br>");

      if (messageQueue.available()) {
        //can2040_msg msg;
        //messages.lockedPop(msg);
        can2040_msg& msg = *messageQueue.get();


        char buf[256] = {};
        int size = snprintf(buf, 255, "%u %u %X %X %X %X<br>", msg.id, msg.dlc, msg.data32[0], msg.data32[1], msg.data32[2], msg.data32[3]);
        //int size = snprintf(buf, sizeof(buf), "%u core<br>", i);
        client._client.write(buf, size);

      }}},

    {"/stream1", HttpPathHandler::SEND_HEADER, [](void* state, HttpClient& client) {
      if (messageQueue.available()) {
        can2040_msg& msg = *messageQueue.get();
        char buf[256] = {};
        int size = snprintf(buf, 255, "%X,%X,%X,%X,%X,%X<br>", msg.id, msg.dlc, msg.data32[0], msg.data32[1], msg.data32[2], msg.data32[3]);
        client._client.write(buf, size);
      }}
    },

    {"/setBitrate", HttpPathHandler::NO_HEADER, [](void* state, HttpClient& client) {
      std::string_view req = client.getReqPath(true);

      const char* pathQueryStart = std::find(req.begin(), req.end(), '?');
      if (pathQueryStart == req.end() || pathQueryStart == req.end() - 1) { client.close(); return; }

      std::string_view query(pathQueryStart + 1, req.size());

      int bitrate = 0;
      auto result = std::from_chars(query.data(), query.data() + query.size(), bitrate);
      if (result.ec == std::errc::invalid_argument) {
          client.abort();
          return;
      }

      canbusShutdown();
      canbusSetup(bitrate);

      client.sendStatus("401 OK", "OK");
    }}
  };

  //server = HttpServer(handlers);
  //server->begin();

  multicore_launch_core1(main2);

  prevMs = millis();
}

void loop() {
  uint64_t currentMs = millis();

  //digitalWrite(CYW43_WL_GPIO_LED_PIN, HIGH);
  //sleep_ms(500);
  //digitalWrite(CYW43_WL_GPIO_LED_PIN, LOW);

  //Serial.println("Hello!");

  //run_cannelloni();

  if (messageQueue.available()) {
        //can2040_msg msg;
        //messages.lockedPop(msg);
        can2040_msg& msg = *messageQueue.get();


        char buf[256] = {};
        int size = snprintf(buf, 255, "%u %u %X %X\n", msg.id, msg.dlc, msg.data32[0], msg.data32[1]);
        //int size = snprintf(buf, sizeof(buf), "%u core<br>", i);
        Serial.write(buf, size);
  }

  //server->run();




  if (currentMs - prevMs > 1000) {
    prevMs = currentMs;

    //digitalWrite(33, currentMs % 2 == 1 ? HIGH : LOW);
    static bool led = false;
    
    
    //cyw43_arch_gpio_put(0, false);
    //cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);

    can2040_msg msg = {};
    msg.id = 0xFFFFFFFFu;
    msg.dlc = 5;
    msg.data32[0] = 1;
    msg.data32[1] = 1;

    if (cbus.rx_cb && can2040_transmit(&cbus, &msg) != 0) {
      failCnt++;
      //cyw43_arch_gpio_put(0, led);
      led = !led;
    }
  }

}



void setup2() {
  canbusSetup(CAR_BITRATE);

  
}

void loop2() {
  tight_loop_contents();
}