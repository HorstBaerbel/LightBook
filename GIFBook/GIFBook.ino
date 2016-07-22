// GIFBook sketch
// Version: v1, 21.07.2016
// Author: Bim Overbohm (bim.overbohm@googlemail.com)

// define this to have debug messages printed to the serial port
//#define OUTPUT_TO_SERIAL
#ifdef OUTPUT_TO_SERIAL
    #define DEBUG_OUTPUT(x) x
#else
    #define DEBUG_OUTPUT(x)
#endif

//#define FASTLED_ALLOW_INTERRUPTS 0 // define this if you have issues with flickering
#define FASTLED_ESP8266_RAW_PIN_ORDER // to use regular ESP GPIO numbers like GPIO13
#include "FastLED.h"

#define LED_DATA_PIN 4
//#define LED_CLOCK_PIN 5
#define LEDS_WIDTH 8
#define LEDS_HEIGHT 8
#define NUM_LEDS LEDS_WIDTH * LEDS_HEIGHT
CRGB leds[NUM_LEDS];
int16_t ledIndex[LEDS_HEIGHT][LEDS_WIDTH];
bool colorChanged = false;

//-----------------------------------------------------------------------------

#include <SPI.h>
#include <SD.h>
// set up variables using the SD utility library functions:
#define SD_CARD_CS_PIN 2
bool sdCardAvailable = false;

//-----------------------------------------------------------------------------

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>

const char * AP_SSID = "LightBook";
// const char * AP_PASSWORD = "87654321";
IPAddress AP_IP(192, 168, 1, 1);

const uint8_t DNS_PORT = 53;
DNSServer dnsServer;

WebSocketsServer webSocket = WebSocketsServer(81);
const uint8_t webSocketResponseOk[] = { 1 };
const uint8_t webSocketResponseNOk[] = { 0 };

const uint8_t MESSAGE_GIF_SELECT = 'g';

ESP8266WebServer webServer(80);
const String gifDirectory = "/gifs";
File uploadFile;

//-----------------------------------------------------------------------------

#define BUTTON_PIN 5 // button to cycle GIFs

//button press durations in ms
#define RELEASE_DURATION 50
#define SHORT_PRESS_DURATION 50
#define LONG_PRESS_DURATION 4000

//button state variables.
uint8_t lastButtonPin = LOW;
int32_t lastButtonStart = 0;
uint8_t currentButtonState = 0; //0 = released, 1 = short pressed, 2 = long pressed
bool buttonWasPressedShort = false;
bool buttonWasPressedLong = false;

//-----------------------------------------------------------------------------

#include "GifDecoder.h"
GifDecoder<LEDS_WIDTH, LEDS_HEIGHT, 10> gifDecoder;
File gifFile;
String gifFileName = "";

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
    DEBUG_OUTPUT(Serial.print("Message from client #");) DEBUG_OUTPUT(Serial.println(num);)
    //for (int i = 0; i < length; ++i) { DEBUG_OUTPUT(Serial.print(payload[i]);) DEBUG_OUTPUT(Serial.print(" ");) } DEBUG_OUTPUT(Serial.println();)
    // check the type of event
    switch(type)
    {
    case WStype_DISCONNECTED:
        DEBUG_OUTPUT(Serial.println("Websocket disonnected");)
        break;
    case WStype_CONNECTED:
        DEBUG_OUTPUT(Serial.println("Websocket connected");)
        break;
    case WStype_TEXT:
        // unknown message, send negative response
        DEBUG_OUTPUT(Serial.println("Unknown text message received");)
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
            DEBUG_OUTPUT(Serial.print("Binary message id: ");) DEBUG_OUTPUT(Serial.print(messageId);) DEBUG_OUTPUT(Serial.print(" type: ");) DEBUG_OUTPUT(Serial.print(messageType);) DEBUG_OUTPUT(Serial.println(" received");)
            // check if this is a color for a specific led. format is ID, 'g', file name.gif
            if (messageType == MESSAGE_GIF_SELECT && length >= 6)
            {
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

void returnOk()
{
  webServer.send(200, "text/plain", "");
}

void returnFail(String msg)
{
  webServer.send(500, "text/plain", msg + "\r\n");
}

void returnDoesNotExist(String msg)
{
  webServer.send(404, "text/plain", msg + "\r\n");
}

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
    DEBUG_OUTPUT(Serial.print("Opening: ");) DEBUG_OUTPUT(Serial.println(path);)
    File dataFile = SD.open(path.c_str());
    // check if directory and add index.htm to it
    if (dataFile.isDirectory()) {
        path += "/index.htm";
        dataType = "text/html";
        dataFile = SD.open(path.c_str());
    }
    // check if opened
    if (!dataFile) {
        DEBUG_OUTPUT(Serial.println("Failed to open file.");)
        return false;
    }
    // send data to client
    //if (server.hasArg("download")) dataType = "application/octet-stream";
    if (webServer.streamFile(dataFile, dataType) != dataFile.size()) {
        DEBUG_OUTPUT(Serial.println("Sent less data than expected!");)
    }
    dataFile.close();
    return true;
}

void handleClientRequest()
{
    DEBUG_OUTPUT(Serial.print("Page request: ");) DEBUG_OUTPUT(Serial.println(webServer.uri());)
    if (!loadFileFromSDCard(webServer.uri())) {
        returnDoesNotExist("Page " + webServer.uri() + " does not exist or no SD card inserted!");
    }
    returnOk();
}

void handleFileUpload()
{
    // check URI for image directory
    if (webServer.uri() != gifDirectory) {
        returnFail("Bad path");
        return;
    }
    // check if the upload path contains the image directory
    HTTPUpload & upload = webServer.upload();
    if (!upload.filename.startsWith(gifDirectory + "/")) {
        returnFail("Can only write in " + gifDirectory);
        return;
    }
    // ok check what phase of the upload we're in
    if (upload.status == UPLOAD_FILE_START) {
        // if the file exists, delete it first
        if (SD.exists((char *)upload.filename.c_str())) {
            SD.remove((char *)upload.filename.c_str());
        }
        // open file for writing
        uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
        DEBUG_OUTPUT(Serial.print("Upload: START, filename: ");) DEBUG_OUTPUT(Serial.println(upload.filename);)
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        // if the file is open, write data to it
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
        DEBUG_OUTPUT(Serial.print("Upload: WRITE, Bytes: ");) DEBUG_OUTPUT(Serial.println(upload.currentSize);)
    }
    else if (upload.status == UPLOAD_FILE_END) {
        // if the file is open, close it
        if (uploadFile) {
            uploadFile.close();
        }
        DEBUG_OUTPUT(Serial.print("Upload: END, Size: ");) DEBUG_OUTPUT(Serial.println(upload.totalSize);)
    }
}

void handleDelete()
{
    /*if (webServer.args() == 0) {
        returnFail("Bad args");
        return;
    }
    String path = webServer.arg(0);
    if (path == "/") {
        returnFail("Bad path");
        return;
    }
    if (!SD.exists((char *)path.c_str())) {
        returnFail(path + " does not exist");
        return;
    }
    if (!path.startsWith(gifDirectory + "/")) {
        returnFail("Can only write in " + gifDirectory);
        return;
    }
    File file = SD.open((char *)path.c_str());
    if (!file.isDirectory()) {
        file.close();
        returnFail("Can't delete directories");
        return;
    }
    file.close();
    SD.remove((char *)path.c_str());*/
    returnOk();
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

void setColor(CRGB incol)
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = incol;
    }
}

void screenClearCallback(void) { const CRGB black = {0, 0, 0}; setColor(black); }

void updateScreenCallback(void) { FastLED.show(); }

void drawPixelCallback(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) { const CRGB col = {r, g, b}; leds[ledIndex[y][x]] = col; }

//-----------------------------------------------------------------------------

bool showNextGIF()
{
    DEBUG_OUTPUT(Serial.println("Opening GIF directory");)
    // try opening GIF directory
    File directory = SD.open(gifDirectory);
    if (!directory) {
        return false;
    }
    DEBUG_OUTPUT(Serial.println("Opened GIF directory");)
    // look for the current GIF in the files
    uint16_t currentIndex = 0;
    uint16_t fileCount = 0;
    File file = directory.openNextFile();
    while (file) {
        String fileName = file.name();
        fileName.toLowerCase();
        DEBUG_OUTPUT(Serial.println("Current file: " + fileName);)
        // check if the file is a gif
        if (!fileName.startsWith(".") && fileName.endsWith(".gif")) {
            // check if it is the current file
            if (gifFileName.endsWith(fileName)) {
                currentIndex = fileCount;
                DEBUG_OUTPUT(Serial.println("Current GIF file " + fileName + " found");)
            }
        }
        fileCount++;
        file.close();
        file = directory.openNextFile();
    }
    file.close();
    // now we have found the file, or the index is still zero. we don't care and increase it. this is the next file we show
    const uint16_t newIndex = (currentIndex + 1) % fileCount;
    currentIndex = 0;
    String newFileName = "";
    directory.rewindDirectory();
    file = directory.openNextFile();
    while (file) {
        String fileName = file.name();
        fileName.toLowerCase();
        // check if the file is a gif
        if (!fileName.startsWith(".") && fileName.endsWith(".gif")) {
            // check if it is the current file
            if (newIndex == currentIndex) {
                newFileName = fileName;
                DEBUG_OUTPUT(Serial.println("New GIF file " + newFileName + " found");)
            }
        }
        currentIndex++;
        file.close();
        file = directory.openNextFile();
    }
    file.close();
    directory.close();
    // now open file if we've found one
    if (newFileName != "") {
        return showGIF(gifDirectory + "/" + newFileName);
    }
    return false;
}

bool showGIF(String fileName)
{
    if (gifFile) {
        //gifDecoder.stopDecoding();
        gifFile.close();
        gifFileName = "";
    }
    // attempt to open the file for reading
    gifFile = SD.open(fileName);
    if (gifFile) {
        gifFileName = fileName;
        gifDecoder.startDecoding();
        DEBUG_OUTPUT(Serial.println("Opened GIF file " + fileName);)
        return true;
    }
    DEBUG_OUTPUT(Serial.println("Failed to open GIF file " + fileName);)
    return false;
}

void handleGIF()
{
    if (gifFile) {
        gifDecoder.decodeFrame();
    }
}

bool fileSeekCallback(unsigned long position) { return gifFile.seek(position); }

unsigned long filePositionCallback(void) { return gifFile.position(); }

int fileReadCallback(void) { return gifFile.read(); }

int fileReadBlockCallback(void * buffer, int numberOfBytes) { return gifFile.read(buffer, numberOfBytes); }

//-----------------------------------------------------------------------------

void buttonReadState()
{
    //the state must be button signal high, hold high SHORT_PRESS_DURATION, signal low, hold low RELEASE_DURATION -> valid short press
    //the state must be button signal high, hold high LONG_PRESS_DURATION, signal low, hold low RELEASE_DURATION -> valid long press
    //check what state the pin is in
    const byte state = digitalRead(BUTTON_PIN);
    //check if the state is the same as before
    if (state == lastButtonPin) {
      //state is the same. check what it is
      if (state == LOW) {
        //button still pressed
        if (currentButtonState == 0) {
          //button was released before. check if enough time has passed to get a short/long press
          if ((millis() - lastButtonStart) >= SHORT_PRESS_DURATION) {
            DEBUG_OUTPUT(Serial.print("Short press");)
            currentButtonState = 1;
          }
        }
        if (currentButtonState == 1) {
          //button was pressed before. check if enough time has passed to get a long press
          if ((millis() - lastButtonStart) >= LONG_PRESS_DURATION) {
            DEBUG_OUTPUT(Serial.print("Long press");)
            currentButtonState = 2;
          }
        }
      }
      else {
        //button still unpressed
        if (currentButtonState == 1) {
          //button was short pressed and released. was the release long enough
          if ((millis() - lastButtonStart) >= RELEASE_DURATION) {
            DEBUG_OUTPUT(Serial.print("Short release");)
            buttonWasPressedShort = true;
            buttonWasPressedLong = false;
            currentButtonState = 0;
          }
        }
        else if (currentButtonState == 2) {
          //button was pressed and released. was the release long enough
          if ((millis() - lastButtonStart) >= RELEASE_DURATION) {
            DEBUG_OUTPUT(Serial.print("Long release");)
            buttonWasPressedShort = false;
            buttonWasPressedLong = true;
            currentButtonState = 0;
          }
        }
      }
    }
    else {
      //state changed. store time
      lastButtonStart = millis();
      lastButtonPin = state;
      DEBUG_OUTPUT(Serial.print("Button state = ");) DEBUG_OUTPUT(Serial.println(state);)
    }
}

void buttonDoCommands()
{
    if (buttonWasPressedLong) {
        // what?
        buttonWasPressedLong = false;
        buttonWasPressedShort = false;
    }
    else if (buttonWasPressedShort) {
        showNextGIF();
        buttonWasPressedLong = false;
        buttonWasPressedShort = false;
    }
}

//-----------------------------------------------------------------------------

void setup()
{
    // set up GPIO5 as an input with a pull-up resistor
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    // set up serial port for debug output
    DEBUG_OUTPUT(Serial.begin(115200);)
    //while (!DEBUG_OUTPUT) { delay(500); }
    DEBUG_OUTPUT(Serial.println("LightBook starting...");)
    // initialize SD card
    if (SD.begin(SD_CARD_CS_PIN)) {
        DEBUG_OUTPUT(Serial.println("Initialized SD card");)
        sdCardAvailable = true;
    }
    else {
        DEBUG_OUTPUT(Serial.println("Failed to initialize SD card!");)
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
    webServer.on("/gifs", HTTP_POST, [](){ returnOk(); }, handleFileUpload);
    webServer.on("/gifs", HTTP_DELETE, handleDelete);
    webServer.begin();
    // set up websockets
    webSocket.begin();
    webSocket.onEvent(webSocketHandleMessage);
    // set up LED strip output
    FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(leds, NUM_LEDS);
    //FastLED.setCorrection(TypicalLEDStrip); //makes the colors look funny...
    FastLED.setTemperature(Tungsten40W); // gives me colors closer to what I see on my screen...
    FastLED.setDither(DISABLE_DITHER);
    FastLED.setBrightness(96);
    // build indices for LED indexing
    buildLedIndices();
    // show initial rgb "flash"
    setColor(CRGB(255, 0, 0)); FastLED.show(); delay(250);
    setColor(CRGB(0, 255, 0)); FastLED.show(); delay(250);
    setColor(CRGB(0, 0, 255)); FastLED.show(); delay(250);
    setColor(CRGB(0, 0, 0)); FastLED.show();
    // set up the GIF gifDecoder
    gifDecoder.setScreenClearCallback(screenClearCallback);
    gifDecoder.setUpdateScreenCallback(updateScreenCallback);
    gifDecoder.setDrawPixelCallback(drawPixelCallback);
    gifDecoder.setFileSeekCallback(fileSeekCallback);
    gifDecoder.setFilePositionCallback(filePositionCallback);
    gifDecoder.setFileReadCallback(fileReadCallback);
    gifDecoder.setFileReadBlockCallback(fileReadBlockCallback);
    // start displaying a GIF
    showGIF(gifDirectory + "/swirl.gif");
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
    // handle button presses
    buttonReadState();
    buttonDoCommands();
    // handle GIF frames or change images
    handleGIF();
}

