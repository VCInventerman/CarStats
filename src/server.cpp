#include <charconv>

#include "carstats/server.h"
#include "carstats/can.h"

void initHttpServer() {
    const static constexpr HttpPathHandler handlers[] = {
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
            CanMsg& packet = *messageQueue.get();
            can2040_msg& msg = packet.msg;
            char buf[256] = {};
            int size = snprintf(buf, 255, "%u %u %X %X %X %X<br>", msg.id, msg.dlc, msg.data32[0], msg.data32[1], msg.data32[2], msg.data32[3]);
            //int size = snprintf(buf, sizeof(buf), "%u core<br>", i);
            client._client.write(buf, size);
        }}},
        {"/stream1", HttpPathHandler::SEND_HEADER, [](void* state, HttpClient& client) {
        if (messageQueue.available()) {
            CanMsg& packet = *messageQueue.get();
            can2040_msg& msg = packet.msg;
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

    httpServer = HttpServer(handlers);
    httpServer->begin();
}