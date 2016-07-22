// LightBook sketch
// Version: v3, 03.06.2016
// Author: Bim Overbohm (bim.overbohm@googlemail.com)

//#define FASTLED_ALLOW_INTERRUPTS 0 // define this if you have issues with flickering
#define FASTLED_ESP8266_RAW_PIN_ORDER // to use regular ESP GPIO numbers like GPIO13
#include "FastLED.h"

#define LED_DATA_PIN 4
//#define LED_CLOCK_PIN 5
#define LEDS_WIDTH 8
#define LEDS_HEIGHT 8
#define NUM_LEDS LEDS_WIDTH * LEDS_HEIGHT
CRGB leds[NUM_LEDS];
int ledIndex[LEDS_HEIGHT][LEDS_WIDTH];
bool colorChanged = false;

//-----------------------------------------------------------------------------

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>

const char * AP_SSID = "LightBook";
// const char * AP_PASSWORD = "87654321";
IPAddress AP_IP(192, 168, 1, 1);
ESP8266WebServer webServer(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebSocketsServer webSocket = WebSocketsServer(81);
const uint8_t webSocketResponseOk[] = { 1 };
const uint8_t webSocketResponseNOk[] = { 0 };

const uint8_t MESSAGE_LED_COLOR = 'l';
const uint8_t MESSAGE_LED_FRAME = 'f';
const uint8_t MESSAGE_DISPLAY_SIZE = 's';

//-----------------------------------------------------------------------------

#include <SPI.h>
#include <SD.h>
// set up variables using the SD utility library functions:
#define SD_CARD_CS_PIN 2
bool sdCardAvailable = false;

//-----------------------------------------------------------------------------

void webSocketSendServerMessage(uint8_t num, const uint8_t * payload, size_t length)
{
    uint8_t * message = new uint8_t[sizeof(uint32_t) + length];
    *((uint32_t *)message) = 0;
    memcpy(&message[sizeof(uint32_t)], payload, length);
    webSocket.sendBIN(num, message, sizeof(uint32_t) + length);
    delete message;
}

void webSocketSendResponse(uint8_t num, uint32_t messageId, bool ok)
{
    uint8_t response[5];
    *((uint32_t *)response) = messageId;
    response[4] = (uint8_t)ok;
    webSocket.sendBIN(num, response, sizeof(response));
}

uint32_t webSocketMessageIdFromData(const uint8_t * payload, size_t length)
{
    if (payload != nullptr && length >= 4) {
        uint32_t id = *((const uint32_t *)payload);
        return id;
    }
    return 0;
}

void webSocketHandleMessage(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
    Serial.print("Message from client #"); Serial.println(num);
    //for (int i = 0; i < length; ++i) { Serial.print(payload[i]); Serial.print(" "); } Serial.println();
    // check the type of event
    switch(type)
    {
    case WStype_DISCONNECTED:
        Serial.println("Websocket disonnected");
        break;
    case WStype_CONNECTED: {
            Serial.println("Websocket connected");
            IPAddress ip = webSocket.remoteIP(num);
            //build message for setting up the client page with out width / height
            const uint8_t message[3] = { MESSAGE_DISPLAY_SIZE, LEDS_WIDTH, LEDS_HEIGHT };
            webSocketSendServerMessage(num, message, sizeof(message));
        }
        break;
    case WStype_TEXT:
        // unknown message, send negative response
        Serial.println("Unknown test message received");
        webSocketSendResponse(num, 0, false);
        //webSocket.sendTXT(num, payload, length); // send to client
        //webSocket.broadcastTXT(payload, length); // send to all clients
        break;
    case WStype_BIN:
        // check if the packet contained data
        if (payload != nullptr && length > 4)
        {
            const uint32_t messageId = webSocketMessageIdFromData(payload, length);
            const char messageType = (char)payload[4];
            const uint8_t * messageData = (const uint8_t *)&payload[5];
            //Serial.print("Binary message id: "); Serial.print(messageId); Serial.print(" type: "); Serial.print(messageType); Serial.println(" received");
            // check if this is a color for a specific led. format is ID, 'l', X, Y, R, G, B
            if (messageType == MESSAGE_LED_COLOR && length == (4 + 1 + 2 + 3))
            {
                // extract colors from data
                CRGB col = {messageData[2], messageData[3], messageData[4]};
                // the LED string is in a zig-zag configuration, so map position to index first
                leds[ledIndex[messageData[1]][messageData[0]]] = col;
                //Serial.println(messageData[0]);
                //Serial.println(messageData[1]);
                //Serial.print(col[0]); Serial.print(","); Serial.print(col[1]); Serial.print(","); Serial.print(col[2]);
                colorChanged = true;
                webSocketSendResponse(num, messageId, true);
            }
            // check if this is a full frame. format is ID, 'f', W, H, then (W * H) R, G, B triplets
            else if (messageType == MESSAGE_LED_FRAME && length == (4 + 1 + 2 + NUM_LEDS * 3))
            {
                //Serial.println(messageData[0]);
                //Serial.println(messageData[1]);
                // we treat the ledIndex 2d array as a 1d array here
                const int * pIndex = &ledIndex[0][0];
                // skip to color data in payload
                const uint8_t * colorData = &messageData[2];
                for (int i = 0; i < NUM_LEDS; i++, colorData+=3)
                {
                    CRGB col = {colorData[0], colorData[1], colorData[2]};
                    leds[pIndex[i]] = col;
                    //Serial.print(col[0]); Serial.print(","); Serial.print(col[1]); Serial.print(","); Serial.print(col[2]);
                }
                colorChanged = true;
                webSocketSendResponse(num, messageId, true);
            }
            else
            {
                // unknown message, send negative response
                webSocketSendResponse(num, messageId, false);
            }
        }
        break;
    }
}

//-----------------------------------------------------------------------------

String getContentType(String filename){
    if (webServer.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

bool loadFileFromSDCard(String path)
{
    if (!sdCardAvailable) {
        return false;
    }
    String dataType = getContentType(path);
    // try opening file
    Serial.print("Opening: "); Serial.println(path);
    File dataFile = SD.open(path.c_str());
    // check if directory and add index.htm to it
    if (dataFile.isDirectory()) {
        path += "/index.htm";
        dataType = "text/html";
        dataFile = SD.open(path.c_str());
    }
    // check if opened
    if (!dataFile) {
        Serial.println("Failed to open file.");
        return false;
    }
    // send data to client
    //if (server.hasArg("download")) dataType = "application/octet-stream";
    if (webServer.streamFile(dataFile, dataType) != dataFile.size()) {
        Serial.println("Sent less data than expected!");
    }
    dataFile.close();
    return true;
}

void handleClientRequest()
{
    Serial.print("Page request: "); Serial.println(webServer.uri());
    if (!loadFileFromSDCard(webServer.uri())) {
        String message = "Page " + webServer.uri() + " does not exist or no SD card inserted!";
        webServer.send(404, "text/plain", message);
    }
    //webServer.send(200, "text/html", "");
}

//-----------------------------------------------------------------------------

void buildLedIndices()
{
    for (int y = 0; y < LEDS_HEIGHT; y++) {
        int index = y * LEDS_WIDTH;
        for (int x = 0; x < LEDS_WIDTH; x++) {
            // if y is odd, we need to turn x around
            ledIndex[y][x] = index + ((y & 1) ? (LEDS_WIDTH - 1 - x) : x);
        }
    }
}

void showColor(CRGB incol)
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = incol;
    }
    FastLED.show();
}

//-----------------------------------------------------------------------------

void setup()
{
    // set up serial port for debug output
    Serial.begin(115200);
    while (!Serial) { delay(500); }
    Serial.println("LightBook starting...");
    // initialize SD card
    if (SD.begin(SD_CARD_CS_PIN)) {
        Serial.println("Initialized SD card");
        sdCardAvailable = true;
    }
    else {
        Serial.println("Failed to initialize SD card!");
    }
    // set up WiFi access point
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID);
    // set up DNS server as a captive portal. we reply with the AP_IP for all DNS requests 
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", AP_IP);
    // set up web server
    webServer.on("/", handleClientRequest);
    webServer.onNotFound(handleClientRequest);
    webServer.begin();
    // set up websockets
    webSocket.begin();
    webSocket.onEvent(webSocketHandleMessage);
    // set up LED strip output
    FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setCorrection(TypicalLEDStrip);
    FastLED.setDither(DISABLE_DITHER);
    FastLED.setBrightness(64);
    // build indices for LED indexing
    buildLedIndices();
    // show initial rgb "flash"
    showColor(CRGB(255, 0, 0)); delay(250);
    showColor(CRGB(0, 255, 0)); delay(250);
    showColor(CRGB(0, 0, 255)); delay(250);
    showColor(CRGB(0, 0, 0));
}

//-----------------------------------------------------------------------------

void loop()
{
    // handle DNS requests
    dnsServer.processNextRequest();
    // handle http request from clients
    webServer.handleClient();
    // handle websocket requests
    webSocket.loop();
    // update color if new
    if (colorChanged)
    {
        FastLED.show();
        colorChanged = false;
    }
}

