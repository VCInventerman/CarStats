#include <utility>
#include <optional>
#include <charconv>

#include <Arduino.h>
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "carstats/can.h"
#include "carstats/server.h"

#include <WiFiUdp.h>

#define PROG_VERSION "1.0.0"
#define CYW43_WL_GPIO_LED_PIN 0

static std::optional<HttpServer> server;

static int lastMessageMs = 0;

int prevMs = 0;

#define CAR_BITRATE 500000
#define ROBOT_BITRATE 1000000

void setup() {
  //stdio_init_all();

  Serial.begin(9600);
  Serial.println("Begin CarStats v" PROG_VERSION);


  waitConnectToWifi();

  //pinMode(33, OUTPUT);

  const static constexpr HttpPathHandler handlers[] = {
    { "/canErrorCount", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", canErrorCount);
      client._client.write(buf, size);
      }},
    { "/canRxCount", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", canRxCount);
      client._client.write(buf, size);
      }},
    { "/canTxCount", HttpPathHandler::SEND_HEADER, [](void*, HttpClient& client) { 
      char buf[256] = {};
      int size = snprintf(buf, sizeof(buf), "%d", canTxCount);
      client._client.write(buf, size);
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

  server = HttpServer(handlers);
  server->begin();

  lastMessageMs = millis();

  prevMs = millis();
}

void loop() {
  uint64_t currentMs = millis();

  //digitalWrite(CYW43_WL_GPIO_LED_PIN, HIGH);
  //sleep_ms(500);
  //digitalWrite(CYW43_WL_GPIO_LED_PIN, LOW);

  //Serial.println("Hello!");

  //run_cannelloni();

  



  if (currentMs - prevMs > 1000) {
    prevMs = currentMs;

    //digitalWrite(33, currentMs % 2 == 1 ? HIGH : LOW);

    static bool led = false;
    led = !led;

    //if (blink) {
      //cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);
    //}
    //cyw43_arch_gpio_put(0, false);
    //cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led);

    can2040_msg msg = {};
    msg.id = 0xFFFFFFFF;
    msg.dlc = 5;
    msg.data32[2] = 0x52434;
    msg.data32[3] = currentMs;
    //messages.lockedPush(&msg);

    //can2040_transmit(&cbus2, &msg);
  }

  server->run();

  /*can2040_msg test = {};
  test.id = 0xFFFFFFFF;
  test.dlc = 5;
  test.data32[2] = 0x52434;
  messages.lockedPush(&test);*/
}



void setup1() {
  canbusSetup(ROBOT_BITRATE);
}

void loop1() {
  tight_loop_contents();
}