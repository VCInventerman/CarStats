#ifndef CARSTATS_SERVER_H
#define CARSTATS_SERVER_H

#include <array>
#include <string_view>
#include <algorithm>

#include <WiFi.h>

#include "carstats/log.h"

constexpr static const char* ssid = "Code Reader 1";
constexpr static const char* password = "password";

inline const IPAddress local_ip(192,168,44,1);
inline const IPAddress gateway(192,168,44,1);
inline const IPAddress subnet(255,255,255,0);

static constexpr const char* hostname = "CarStats CAN Reader";

inline void connectToWifi2() {
    //WiFi.mode(WIFI_STA);
    //WiFi.setHostname(hostname);
    int ret = WiFi.begin(ssid, password);
}

inline void waitConnectToWifi() {
    //connectToWifi();

    WiFi.softAPConfig(local_ip, gateway, subnet);
    WiFi.softAP(ssid, password);

    /*while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }*/

    Serial.println("");
    Serial.print("WiFi connected, ");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

inline void waitConnectToWifi2() {
    // in initVariant
    /*if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA)) {
        while (true) {
            Serial.println("WiFi init fail");
        }
    }*/

    //cyw43_arch_enable_sta_mode();
    //cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);

    /*if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        while (true) { Serial.println("failed to connect\n"); }

    }*/

    Serial.println("");
    Serial.print("WiFi connected, ");
    Serial.print("IP address: ");
    //Serial.println(WiFi.localIP());

    
}

class HttpClient;
using HandlerFunc = void(*)(void* state, HttpClient& client);

struct HttpPathHandler {
    enum BeginAction {
        NO_HEADER,
        SEND_HEADER,
        SEND_HEADER_AND_RESP
    };

    std::string_view path;
    BeginAction beginAction;
    HandlerFunc handler;
};

class HttpClient {
public:
    enum class ConnState {
        CLOSED,
        READ_REQ,
        PARSING,
        RESPONDING,
    };


    WiFiClient _client;
    ConnState _state;

    char _buf[5000] = {};
    size_t _bufPos = 0;

    int _contentLength; 

    HandlerFunc _handler;
    void* _handlerState;

    HttpClient() = default;
    explicit HttpClient(WiFiClient&& client) : _client(std::move(client)) {
        _state = ConnState::READ_REQ;
    }

    int endOfRequest() {
        for (int i = 0; i < _bufPos; i++) {
            char& c = _buf[i];

            if (c == '\0') { return i; }

            if (c == '\r' && (sizeof(_buf) - i >= 3)) {
                if (_buf[i + 1] == '\n' && _buf[i + 2] == '\r' && _buf[i + 3] == '\n') {
                    return i + 3;
                }
            }
        }

        return -1;
    }

    void read() {
        if (_bufPos <= sizeof(_buf)) {
            _bufPos += _client.readBytes(_buf + _bufPos, sizeof(_buf) - _bufPos);

            // Detect if the request has been fully read
            if (endOfRequest() != -1) {
                _state = ConnState::PARSING;
            }
        }
    }

    std::string_view getReqPath(bool includeQuery = false) {
        char* endOfBuf = _buf + _bufPos;

        char* slashPos = std::find(std::begin(_buf), endOfBuf, '/');
        if (slashPos == endOfBuf) { abort(); return ""; }

        char* endOfPath = std::find(slashPos, endOfBuf, ' ');
        if (endOfPath == endOfBuf) { abort(); return ""; }

        char* pathQueryStart = std::find(slashPos, endOfBuf, '?');
        
        if (!includeQuery) {
            endOfPath = std::min(endOfPath, pathQueryStart);
        }

        std::string_view path(slashPos, endOfPath - slashPos);

        Serial.print(" Got path ");
        Serial.write(path.data(), path.size());
        Serial.println();

        return path;
    }

    void respond() {
        _handler(_handlerState, *this);
    }

    void sendHeaderOk() {
        const char* resp = "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8"
            "\r\n\r\n";

        _client.write(resp);
    }

    void sendStatus(std::string_view code, std::string_view message) {
        _client.write("HTTP/1.1 ");
        _client.write(code.data(), code.size());
        _client.write("\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: ");
        _client.write(message.size());
        _client.write("\r\n\r\n");
        _client.write(message.data(), message.size());
    }

    void abort(std::string_view code = "400 Bad Request", std::string_view message = "Bad Request") {
        sendStatus(code, message);
        _client.flush();
        close();
    }

    bool isClosed() {
        return !_client;
    }

    void close() {
        _client = WiFiClient();
    }
};

class HttpServer {
    public:

    static constexpr size_t MAX_CLIENTS = 10;


    std::array<HttpClient, MAX_CLIENTS> _clients;

    const HttpPathHandler* _paths;
    size_t _pathCount;

    WiFiServer _server;

    explicit HttpServer(const HttpPathHandler* paths, size_t pathCount, uint16_t port = 80) :
        _paths(paths), _pathCount(pathCount), _server(port) {}

    std::pair<int, HttpClient*> assignUnusedClientSlot(WiFiClient&& client) {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (_clients[i].isClosed()) {
            _clients[i] = HttpClient(std::move(client));

            return { i, &_clients[i] };
            }
        }

        return { -1, nullptr };
    }

public:
    template<size_t handlerCount>
    constexpr explicit HttpServer (const HttpPathHandler (&handlers)[handlerCount], uint16_t port = 80) 
        : _paths(handlers), _pathCount(handlerCount), _server(port) 
        {}

    void begin() {
        _server.begin();
    }

    void run() {
        while (_server.hasClient()) {
            CARSTATS_LOG("Connect on port 80");
            auto res = assignUnusedClientSlot(_server.accept());

            if (res.first != -1) { 
                res.second->_client.setNoDelay(true); // Disable Nagle's algorithm
                res.second->_client.setTimeout(0);
                res.second->_client.setSync(false);
                res.second->_client.keepAlive(7200, 75, 9);
            }
            else {
                break;
            }
        }

        for (auto& c : _clients) {
            if (!c.isClosed()) {
                
                if (c._state == HttpClient::ConnState::RESPONDING) {
                    c.respond();
                }
                else if (c._state == HttpClient::ConnState::READ_REQ) {
                    c.read();
                }
                else if (c._state == HttpClient::ConnState::PARSING) {
                    std::string_view path = c.getReqPath();

                    auto handler = std::find_if(_paths, _paths + _pathCount, [&](const HttpPathHandler& p) { return p.path == path; });
                    if (handler == _paths + _pathCount) {
                        Serial.println("No handler found!");
                        c.close();
                    }

                    c._handler = handler->handler;
                    c._handlerState = nullptr;

                    if (handler->beginAction == HttpPathHandler::SEND_HEADER || handler->beginAction == HttpPathHandler::SEND_HEADER_AND_RESP) {
                        c.sendHeaderOk();
                    }
                    if (handler->beginAction == HttpPathHandler::SEND_HEADER_AND_RESP) {
                        c._client.write("CarStats v1.0.0<br>");
                    }

                    c._state = HttpClient::ConnState::RESPONDING;
                }
            }
        }
    }
};

inline std::optional<HttpServer> httpServer;

void initHttpServer();

#endif