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
#include "ATEMbase.h"
#include "ATEMstd.h"
ATEMstd AtemSwitcher;

// Constants
enum mode {
    automatic,
    manual
};

uint32_t debounceDelay = 50;

// Pins
uint8_t modeButtonPin = A4;
uint8_t modeButtonLED = A5;

uint8_t cutButtonPin = 13;

uint8_t videoSourcePin[6] = {0,1,2,3,4,5};
uint8_t videoSourceLED[6] = {6,7,8,9,11,12};
uint8_t micPin[4] = {A0,A1,A2,A3};

// Mapping
uint16_t micToVideoSource[4] = {1,5,5,5};
uint16_t defaultVideoSource = 5;

// Network Config
byte mac[] = {0x90, 0xA2, 0xDA, 0x0D, 0x6B, 0xB9};
IPAddress clientIP(10,64,160,134);
IPAddress switcherIP(10,64,160,133);

// States
uint32_t debounceButton[8]; // 0 mode, 1-6 source, 7 cut
uint8_t buttonState;
uint16_t micLevel[4];
mode modeState = automatic;
uint16_t videoSourceState;
uint16_t lastVideoSourceState;
bool doCut = false;

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
    AtemSwitcher.serialOutput(0x80);
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
        micLevel[i] = analogRead(micPin[i]);
    }
}

void readButton(uint8_t button, uint8_t index) {
    uint8_t reading = digitalRead(button);
    if (reading != bitRead(buttonState, index)) {
        // State changed reset de-bounce timer
        debounceButton[index] = millis();
    }

    if ((millis() - debounceButton[index]) > debounceDelay) {
        // State is stable
        if (reading != bitRead(buttonState, index)) {
            // State changed, update state
            bitWrite(buttonState, index, reading);
        }
    }
}

void updateState() {
    if (modeState == automatic) {
        if (bitRead(buttonState, 0)) {
            modeState = manual;
        } else {
            videoSourceState = updateFromMics();
        }
    } else { // mode == manual
        if (bitRead(buttonState, 0)) {
            modeState = automatic;
            configureATEM();
        } else if (bitRead(buttonState, 1)) {
            videoSourceState = 1;
        } else if (bitRead(buttonState, 2)) {
            videoSourceState = 2;
        } else if (bitRead(buttonState, 3)) {
            videoSourceState = 3;
        } else if (bitRead(buttonState, 4)) {
            videoSourceState = 4;
        } else if (bitRead(buttonState, 5)) {
            videoSourceState = 5;
        } else if (bitRead(buttonState, 6)) {
            videoSourceState = 6;
        } else if (bitRead(buttonState, 7)) {
            doCut = true;
        }
    }
}

uint16_t updateFromMics() {
    return 0;
}

void configureATEM() {
    // Set default output
    AtemSwitcher.changeProgramInput(defaultVideoSource);
    // Set cut to fast mix
    AtemSwitcher.changeTransitionType(0);
    AtemSwitcher.changeTransitionMixTime(10);
}

void updateATEM() {
    if (videoSourceState != lastVideoSourceState) {
        AtemSwitcher.changePreviewInput(videoSourceState);
    }
    if (doCut) {
        AtemSwitcher.doCut();
        doCut = false;
    }
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
