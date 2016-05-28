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

// Include ATEMbase library and make an instance:
// The port number is chosen randomly among high numbers.
#include <ATEMbase.h>
#include <ATEMstd.h>
ATEMstd AtemSwitcher;

// Constants
enum mode {
    automatic,
    manual
};

const uint32_t buttonBounceDelay = 50; //ms
const uint32_t micBounceDelay = 500; //ms
const uint32_t sampleWindow = 50; //ms
const uint16_t micThresholdLevel = 512; // 0-1023 5v

// Pins
const uint8_t modeButtonPin = A4;
const uint8_t modeButtonLED = 13;

const uint8_t cutButtonPin = A5;

const uint8_t videoSourcePin[6] = {0,1,2,3,4,5};
const uint8_t videoSourceLED[6] = {6,7,8,9,11,12};
const uint8_t micPin[4] = {A0,A1,A2,A3};

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
IPAddress clientIP(10,64,160,134);
IPAddress switcherIP(10,64,160,133);

// States
uint32_t debounceButton[8]; // 0 mode, 1-6 source, 7 cut
uint8_t lastButtonState;
uint8_t buttonTrigger;
uint8_t buttonState = B11111111;
uint32_t debounceMic[4];
uint8_t lastMicState;
uint8_t micTrigger;
uint8_t micState = B00000000;

mode modeState = manual;
uint16_t videoSourceState;
uint16_t lastVideoSourceState;
bool doCut = false;
uint32_t lastAutoChange;

void setup() {

    // Set up pins
    pinMode(modeButtonPin, INPUT_PULLUP);
    pinMode(modeButtonLED, OUTPUT);
    pinMode(cutButtonPin, INPUT_PULLUP);
    for(uint8_t i = 0; i < 6; i++) {
        pinMode(videoSourcePin[i], INPUT_PULLUP);
        pinMode(videoSourceLED[i], OUTPUT);
    }
    for(uint8_t i = 0; i < 4; i++) {
        pinMode(micPin[i], INPUT);
    }

    // Start up Ethernet and Serial (debugging)
    Ethernet.begin(mac,clientIP);
    Serial.begin(115200);
    Serial << F("\n- - - - - - - -\nSerial Started\n");

    AtemSwitcher.begin(switcherIP);
    AtemSwitcher.serialOutput(1);
    AtemSwitcher.connect();

}

void loop() {
    // Keep ATEM connection alive and receive any pending data
    AtemSwitcher.runLoop();
    updateFromATEM();

    readInputs();
    updateState();
    updateATEM();
}

void readInputs() {
    readButton(modeButtonPin, 0);
    for(uint8_t i = 0; i < 6; i++) {
        readButton(videoSourcePin[i], i+1);
    }
    readButton(cutButtonPin, 7);

    for(uint8_t i = 0; i < 4; i++) {
        readMic(i);
    }
}

void readButton(uint8_t button, uint8_t index) {
    uint8_t reading = digitalRead(button);
    debounceTrigger(reading, index, &lastButtonState, &buttonState, &buttonTrigger, debounceButton, buttonBounceDelay);
}

void readMic(uint8_t index) {
    uint32_t startTime = millis();
    uint16_t reading;
    uint16_t readingMin = 1024;
    uint16_t readingMax = 0;

    while(millis() - startTime < sampleWindow) {
        // Sample min and max levels
        reading = analogRead(micPin[index]);
        if (reading > readingMax) {
            readingMax = reading;
        } else if (reading < readingMin) {
            readingMin = reading;
        }
    }
    reading = readingMax - readingMin;

    if (reading > micThresholdLevel) {
        debounceTrigger(LOW, index, &lastMicState, &micState, &micTrigger, debounceMic, micBounceDelay);
    } else {
        debounceTrigger(HIGH, index, &lastMicState, &micState, &micTrigger, debounceMic, micBounceDelay);
    }
}

void debounceTrigger(uint8_t reading, uint8_t index, uint8_t *lastState, uint8_t *state, uint8_t *trigger, uint32_t *debounceTime, uint32_t debounceDelay) {
    if (reading != bitRead(*lastState, index)) {
        // State changed: reset de-bounce timer
        debounceTime[index] = millis();
    }

    if ((millis() - debounceTime[index]) > debounceDelay) {
        // State is stable
        if (reading != bitRead(*state, index)) {
            // State changed: update state
            bitWrite(*state, index, reading);
            if (reading == LOW) {
                // Trigger once per input change
                bitWrite(*trigger, index, HIGH);
            }
        }
    }
    // Update last read
    bitWrite(*lastState, index, reading);
}

void updateState() {
    if (modeState == automatic) {
        if (bitRead(buttonTrigger, 0)) {
            Serial << F("Change to Manual\n");
            modeState = manual;
            bitWrite(buttonTrigger, 0, 0);
        } else {
            videoSourceState = updateFromMics();
        }
    } else { // modeState == manual
        if (bitRead(buttonTrigger, 0)) {
            Serial << F("Change to Auto\n");
            modeState = automatic;
            bitWrite(buttonTrigger, 0, 0);
            configureATEM();
        } else if (bitRead(buttonTrigger, 1)) {
            Serial << F("Preview Input 1\n");
            videoSourceState = 1;
            bitWrite(buttonTrigger, 1, 0);
        } else if (bitRead(buttonTrigger, 2)) {
            Serial << F("Preview Input 2\n");
            videoSourceState = 2;
            bitWrite(buttonTrigger, 2, 0);
        } else if (bitRead(buttonTrigger, 3)) {
            Serial << F("Preview Input 3\n");
            videoSourceState = 3;
            bitWrite(buttonTrigger, 3, 0);
        } else if (bitRead(buttonTrigger, 4)) {
            Serial << F("Preview Input 4\n");
            videoSourceState = 4;
            bitWrite(buttonTrigger, 4, 0);
        } else if (bitRead(buttonTrigger, 5)) {
            Serial << F("Preview Input 5\n");
            videoSourceState = 5;
            bitWrite(buttonTrigger, 5, 0);
        } else if (bitRead(buttonTrigger, 6)) {
            Serial << F("Preview Input 6\n");
            videoSourceState = 6;
            bitWrite(buttonTrigger, 6, 0);
        } else if (bitRead(buttonTrigger, 7)) {
            Serial << F("Cut\n");
            doCut = true;
            bitWrite(buttonTrigger, 7, 0);
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
        if (millis() - lastAutoChange > 2000 && (videoSourceState != lastVideoSourceState)) {
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
            AtemSwitcher.doCut();
            doCut = false;
        }
    }
    lastVideoSourceState = videoSourceState;
}

void updateFromATEM() {
    // Update mode LED
    if (modeState == automatic) {
        digitalWrite(modeButtonLED, HIGH);
    } else {
        digitalWrite(modeButtonLED, LOW);
    }

    // Update Preview LEDs
    for(int i =0; i < 6; i++) {
        digitalWrite(videoSourceLED[i], AtemSwitcher.getPreviewTally(i+1) ? HIGH : LOW);
    }
}
