// atemSwitcher.ino

/**
 * ATEM H/W Switcher with auto and manual modes for changing the video output
 * based on signals from audio inputs or buttons.
 */

// Including libraries:
#include <SPI.h>
#include <SD.h>
#include <Ethernet2.h>
#include <EthernetUdp2.h>
#include <Streaming.h>
#include <Button.h>

// Include ATEMbase library and make an instance:
// The port number is chosen randomly among high numbers.
#include <ATEMbase.h>
#include <ATEMstd.h>
ATEMstd AtemSwitcher;

// Constants
#define PULLUP true
#define INVERT true
#define DEBOUNCE_MS 20
#define LONG_PRESS 1000

#define SAMPLE_WINDOW_MS 50
#define MIC_DEBOUNCE_MS 500
#define MIC_THRESHOLD 512 // 0-1023 5v

#define CUT_LIMIT 2000 // ms

enum mode {
    automatic,
    manual
};
enum cutMode {
    cut,
    fade
};

// Pins
const uint8_t micPin[4] = {A0,A1,A2,A3};
const uint8_t latchPin = 9;
const uint8_t clockPin = 10;
const uint8_t dataPin = 8;
const uint8_t resetPin = 11;
const uint8_t greenOE = A4;
const uint8_t redOE = A5;

Button vidSource1(0, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource2(1, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource3(2, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource4(3, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource5(4, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource6(5, PULLUP, INVERT, DEBOUNCE_MS);

Button cutButton(6, PULLUP, INVERT, DEBOUNCE_MS);
Button modeButton(7, PULLUP, INVERT, DEBOUNCE_MS);


// Mapping
const uint16_t micToVideoSource[16] = {
    0, // 0000 No mics
    1, // 0001 Presenter Mic
    5, // 0010 Guest 1
    5, // 0011 P & G1
    5, // 0100 G2
    5, // 0101 P & G2
    5, // 0110 G1 & G2
    5, // 0111 P & G1 & G2
    5, // 1000 G3
    5, // 1001 P & G3
    5, // 1010 G1 & G3
    5, // 1011 P & G1 & G3
    5, // 1100 G2 & G3
    5, // 1101 P & G2 & G3
    5, // 1110 G1 & G2 & G3
    5, // 1111 P & G2 & G3
};
const uint16_t defaultVideoSource = 5;

// Network Config
byte mac[] = {0x90, 0xA2, 0xDA, 0x0D, 0x6B, 0xB9};
IPAddress clientIP(192,168,12,39);
IPAddress switcherIP(192,168,12,40);

// States
uint32_t debounceMic[4];
uint8_t lastMicState;
uint8_t micState = B00000000;

uint8_t greenLeds = B11111111;
uint8_t redLeds = B11111111;

mode modeState = manual;
cutMode cutState = cut;
uint16_t videoSourceState;
uint16_t lastVideoSourceState;
bool doCut = false;
bool longCut = false;
uint32_t lastAutoChange;

void setup() {

    // Set up pins
    for(uint8_t i = 0; i < 4; i++) {
        pinMode(micPin[i], INPUT);
    }
    pinMode(latchPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    pinMode(dataPin, OUTPUT);
    pinMode(resetPin, OUTPUT);

    digitalWrite(resetPin, HIGH);
    digitalWrite(greenOE, LOW);
    digitalWrite(redOE, LOW);

    // Start up Ethernet and Serial (debugging)
    Ethernet.begin(mac,clientIP);
    Serial.begin(115200);

    delay(2000);

    Serial << F("\n- - - - - - - -\nSerial Started\n");

    AtemSwitcher.begin(switcherIP);
    AtemSwitcher.serialOutput(0x80);
    AtemSwitcher.connect();

}

void loop() {
    // Keep ATEM connection alive and receive any pending data
    AtemSwitcher.runLoop();
    updateFromATEM();

    for(uint8_t i = 0; i < 4; i++) {
        readMic(i);
    }
    vidSource1.read();
    vidSource2.read();
    vidSource3.read();
    vidSource4.read();
    vidSource5.read();
    vidSource6.read();
    cutButton.read();
    modeButton.read();

    updateState();
    updateATEM();
}

void readMic(uint8_t index) {
    uint32_t startTime = millis();
    uint16_t reading;
    uint16_t readingMin = 1024;
    uint16_t readingMax = 0;

    while(millis() - startTime < SAMPLE_WINDOW_MS) {
        // Sample min and max levels
        reading = analogRead(micPin[index]);
        if (reading > readingMax) {
            readingMax = reading;
        } else if (reading < readingMin) {
            readingMin = reading;
        }
    }
    reading = readingMax - readingMin;

    if (reading > MIC_THRESHOLD) {
        debounce(LOW, index, &lastMicState, &micState, debounceMic, MIC_DEBOUNCE_MS);
    } else {
        debounce(HIGH, index, &lastMicState, &micState, debounceMic, MIC_DEBOUNCE_MS);
    }
}

void debounce(uint8_t reading, uint8_t index, uint8_t *lastState, uint8_t *state, uint32_t *debounceTime, uint32_t debounceDelay) {
    if (reading != bitRead(*lastState, index)) {
        // State changed: reset de-bounce timer
        debounceTime[index] = millis();
    }

    if ((millis() - debounceTime[index]) > debounceDelay) {
        // State is stable
        if (reading != bitRead(*state, index)) {
            // State changed: update state
            bitWrite(*state, index, reading);
        }
    }
    // Update last read
    bitWrite(*lastState, index, reading);
}

void updateState() {
    if (modeState == automatic) {
        if (modeButton.wasReleased()) {
            Serial << F("Change to Manual\n");
            modeState = manual;
        } else {
            videoSourceState = updateFromMics();
        }
    } else { // modeState == manual
        if (modeButton.wasReleased()) {
            Serial << F("Change to Auto\n");
            modeState = automatic;
            configureATEM();
        } else if (vidSource1.wasReleased()) {
            Serial << F("Preview Input 1\n");
            videoSourceState = 1;
        } else if (vidSource2.wasReleased()) {
            Serial << F("Preview Input 2\n");
            videoSourceState = 2;
        } else if (vidSource3.wasReleased()) {
            Serial << F("Preview Input 3\n");
            videoSourceState = 3;
        } else if (vidSource4.wasReleased()) {
            Serial << F("Preview Input 4\n");
            videoSourceState = 4;
        } else if (vidSource5.wasReleased()) {
            Serial << F("Preview Input 5\n");
            videoSourceState = 5;
        } else if (vidSource6.wasReleased()) {
            Serial << F("Preview Input 6\n");
            videoSourceState = 6;
        } else if (cutButton.wasReleased()) {
            if (longCut) {
                if (cutState == cut) {
                    Serial << F("Fade Mode\n");
                    cutState = fade;
                } else {
                    Serial << F("Cut Mode\n");
                    cutState = cut;
                }
                longCut = false;
            } else {
                Serial << F("Cut\n");
                doCut = true;
            }
        } else if (cutButton.pressedFor(LONG_PRESS)) {
            Serial << F("Long Press\n");
            longCut = true;
        }
    }
}

uint16_t updateFromMics() {
    uint8_t index = 0x0F & ~micState;
    return micToVideoSource[index];
}

void configureATEM() {
    // Set default output
    AtemSwitcher.changeProgramInput(defaultVideoSource);
    // Set cut to fast mix
    AtemSwitcher.changeTransitionType(0);
    AtemSwitcher.changeTransitionMixTime(10);
}

void updateATEM() {
    if (modeState == automatic) {
        if (millis() - lastAutoChange > CUT_LIMIT && (videoSourceState != lastVideoSourceState)) {
            Serial << (0x0F & ~micState) << endl;
            Serial << F("Auto change to ") << videoSourceState << endl;
            AtemSwitcher.changePreviewInput(videoSourceState);
            AtemSwitcher.doCut();
            lastAutoChange = millis();
        }
    } else { // modeState == manual
        if (videoSourceState != lastVideoSourceState) {
            AtemSwitcher.changePreviewInput(videoSourceState);
        }
        if (doCut) {
            if (cutState == fade) {
                AtemSwitcher.doAuto();
            } else {
                AtemSwitcher.doCut();
            }
            doCut = false;
        }
    }
    lastVideoSourceState = videoSourceState;
}

void updateFromATEM() {
    // LOW is ON for LEDs as they are pull down

    // Get state from ATEM
    uint16_t prev = AtemSwitcher.getPreviewInput();
    uint16_t prog = AtemSwitcher.getProgramInput();

    // Serial << F("Prev: ") << prev << endl;
    // Serial << F("Prog: ") << prog << endl;

    // Update Green (Preview) LEDs
    for(uint16_t i = 0; i < 6; i++) {
        if (prev == (i+1)) {
            bitClear(greenLeds, i);
        } else {
            bitSet(greenLeds, i);
        }
    }

    // Update Red (Program) LEDs
    for(uint16_t i = 0; i < 6; i++) {
        if (prog == (i+1)) {
            bitClear(redLeds, i);
        } else {
            bitSet(redLeds, i);
        }
    }

    // Update cut LED
    if (cutState == cut) {
        bitWrite(greenLeds, 6, LOW);
        bitWrite(redLeds, 6, HIGH);
    } else {
        bitWrite(greenLeds, 6, HIGH);
        bitWrite(redLeds, 6, LOW);
    }

    // Update mode LED
    if (modeState == automatic) {
        bitWrite(greenLeds, 7, LOW);
        bitWrite(redLeds, 7, HIGH);
    } else {
        bitWrite(greenLeds, 7, HIGH);
        bitWrite(redLeds, 7, LOW);
    }

    // Serial << F("Green: ") << _BIN(greenLeds) << endl;
    // Serial << F("Red  : ") << _BIN(redLeds) << endl;

    digitalWrite(latchPin, LOW);
    shiftOut(dataPin, clockPin, MSBFIRST, greenLeds);
    shiftOut(dataPin, clockPin, MSBFIRST, redLeds);
    digitalWrite(latchPin, HIGH);

}
