/*
 * ButtonBox Firmware
 * Copyright (c) 2026 Yi donghoon <donghoon@studio42.kr>
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#include "Adafruit_TinyUSB.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include "RotaryStateMachine.h"

// ===== USB HID =====
uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), 
                          HID_ITF_PROTOCOL_NONE, 2, false);
hid_gamepad_report_t gp;

// ===== MCP23017 =====
Adafruit_MCP23X17 mcp1;  // 0x20: encoders, button
Adafruit_MCP23X17 mcp2;  // 0x21: self return selectors, buttons, KEYBOX

// ===== ROCKER SWITCH (MASTER) =====
#define ROCKER_PIN 2   // GP2
#define ROCKER_LED 0   // GP0

// === LED PIN
#define LED_RPM 6
#define LED_TC  7
#define LED_ABS 8

// RPM STATUS
#define RPM_OFF     0
#define RPM_OPTIMAL 1
#define RPM_REDZONE 2

uint8_t rpmState = RPM_OFF;
uint32_t now;
unsigned long lastRpmBlink = 0;
bool rpmBlinkState = false;

// ===== 5 rotary encoder =====
RotaryStateMachine encoders[5];

uint16_t mcp1Status, mcp2Status;

// ===== HOLD TIMER =====
#define HOLD_TIME 50
unsigned long encHoldUntil[10] = {0};
unsigned long ignitionHoldUntil = 0;

// ===== KEYBOX STATE =====
#define KEY_NC    0  // KEY OFF
#define KEY_ACC   1  // ACC 
#define KEY_START 2  // START (SPRING RETURN to ACC)
uint8_t keyState = KEY_NC;
uint8_t keyStateRaw = KEY_NC;
unsigned long keyDebounceTime = 0;
#define KEY_DEBOUNCE 20  // 20ms

void setup() {
    usb_hid.begin();
    Serial.begin(115200);
    
    // rocker switch & led
    pinMode(ROCKER_PIN, INPUT);
    pinMode(ROCKER_LED, OUTPUT);
    digitalWrite(ROCKER_LED, HIGH);       // PUSH POWER to ROCKER_LED
    pinMode(ROCKER_PIN, INPUT_PULLDOWN);  // ← INPUT → INPUT_PULLDOWN

    // LED out setup
    pinMode(LED_RPM, OUTPUT);
    pinMode(LED_TC, OUTPUT);
    pinMode(LED_ABS, OUTPUT);
    digitalWrite(LED_RPM, LOW);
    digitalWrite(LED_TC, LOW);
    digitalWrite(LED_ABS, LOW);
    
    Wire.begin();
    Wire.setClock(400000);
    
    mcp1.begin_I2C(0x20);
    mcp2.begin_I2C(0x21);
    
    for (int i = 0; i < 16; i++) {
        mcp1.pinMode(i, INPUT_PULLUP);
        mcp2.pinMode(i, INPUT_PULLUP);
    }
    
    mcp1Status = mcp1.readGPIOAB();
    mcp2Status = mcp2.readGPIOAB();
    
    // initial state of keybox
    bool acc = !(mcp2Status & (1 << 14));
    bool start = !(mcp2Status & (1 << 15));
    if (acc && start) keyState = KEY_START;
    else if (acc) keyState = KEY_ACC;
    else keyState = KEY_NC;
    keyStateRaw = keyState;
    
    while (!TinyUSBDevice.mounted()) delay(1);
}

bool pressed(uint16_t val, int bit) {
    return !(val & (1 << bit));
}

void processSimHub() {
    while (Serial.available()) {
        char cmd = Serial.read();
        
        switch (cmd) {
            // RPM status
            case 'R':
                rpmState = RPM_REDZONE;
                break;
            case 'O':
                rpmState = RPM_OPTIMAL;
                digitalWrite(LED_RPM, HIGH);
                break;
            case 'r':
                rpmState = RPM_OFF;
                digitalWrite(LED_RPM, LOW);
                break;
                
            // TC
            case 'T':
                digitalWrite(LED_TC, HIGH);
                break;
            case 't':
                digitalWrite(LED_TC, LOW);
                break;
                
            // ABS
            case 'A':
                digitalWrite(LED_ABS, HIGH);
                break;
            case 'a':
                digitalWrite(LED_ABS, LOW);
                break;
        }
    }
    
    // RPM redzone blinking
    if (rpmState == RPM_REDZONE) {
        if (now - lastRpmBlink > 50) {
            rpmBlinkState = !rpmBlinkState;
            digitalWrite(LED_RPM, rpmBlinkState);
            lastRpmBlink = millis();
        }
    }
}

// state or rocker switch (LOW = ON)
bool isRockerOn() {
    return digitalRead(ROCKER_PIN);
}

uint8_t readKeyState() {
    bool acc = pressed(mcp2Status, 14);    // GPB6
    bool start = pressed(mcp2Status, 15);  // GPB7
    
    if (acc && start) return KEY_START;
    if (acc) return KEY_ACC;
    return KEY_NC;
}

void processKeybox(unsigned long now) {
    // ignore if rocker switch OFF
    if (!isRockerOn()) {
        keyState = readKeyState();
        keyStateRaw = keyState;
        return;
    }
    
    uint8_t newStateRaw = readKeyState();
    
    // detect state change → start debounce timer
    if (newStateRaw != keyStateRaw) {
        keyStateRaw = newStateRaw;
        keyDebounceTime = now;
    }
    
    // update after debounce time 
    if ((now - keyDebounceTime) > KEY_DEBOUNCE) {
        if (keyStateRaw != keyState) {
            // NC → ACC: IGNITION pulse (IGNITION ON)
            if (keyState == KEY_NC && keyStateRaw == KEY_ACC) {
                ignitionHoldUntil = now + HOLD_TIME;
            }
            // ACC → NC: IGNITION pulse (IGNITION OFF)
            else if (keyState == KEY_ACC && keyStateRaw == KEY_NC) {
                ignitionHoldUntil = now + HOLD_TIME;
            }
            // START → ACC: ignored (spring return)
            
            keyState = keyStateRaw;
        }
    }
    
    // 버튼 28: keybox IGNITION (pulse)
    if (now < ignitionHoldUntil) gp.buttons |= (1UL << 28);
    
    // 버튼 29: keybox STARTER (hold)
    if (keyState == KEY_START) gp.buttons |= (1UL << 29);
}

void loop() {
    gp.buttons = 0;
    now = millis();
    
    mcp1Status = mcp1.readGPIOAB();
    mcp2Status = mcp2.readGPIOAB();
    
    // ===== rotary encoder 1~4 CW/CCW (MCP1 GPA0-7) → button 0~7 =====
    for (int i = 0; i < 4; i++) {
        bool pinA = pressed(mcp1Status, i * 2);
        bool pinB = pressed(mcp1Status, i * 2 + 1);
        
        unsigned char result = encoders[i].process(pinB, pinA);
        if (result == DIR_CW)  encHoldUntil[i * 2] = now + HOLD_TIME;
        if (result == DIR_CCW) encHoldUntil[i * 2 + 1] = now + HOLD_TIME;
    }
    
    // ===== rotary encoder 5 CW/CCW (MCP1 GPB0-1) → button 8~9 =====
    {
        bool pinA = pressed(mcp1Status, 8);
        bool pinB = pressed(mcp1Status, 9);
        
        unsigned char result = encoders[4].process(pinB, pinA);
        if (result == DIR_CW)  encHoldUntil[8] = now + HOLD_TIME;
        if (result == DIR_CCW) encHoldUntil[9] = now + HOLD_TIME;
    }
    
    // rotary encoder CW/CCW to button hold (button 0~9)
    for (int i = 0; i < 10; i++) {
        if (now < encHoldUntil[i]) {
            gp.buttons |= (1UL << i);
        }
    }
    
    // ===== BTN_5~10 (MCP1 GPB2-7) → button 10~15 =====
    for (int i = 0; i < 6; i++) {
        if (pressed(mcp1Status, 10 + i)) 
            gp.buttons |= (1UL << (10 + i));
    }
    
    // ===== self return selector 1~3 (MCP2 GPA2-7) → button 16~21 =====
    for (int i = 0; i < 3; i++) {
        if (pressed(mcp2Status, 2 + i * 2)) 
            gp.buttons |= (1UL << (16 + i * 2));
        if (pressed(mcp2Status, 3 + i * 2)) 
            gp.buttons |= (1UL << (17 + i * 2));
    }
    
    // ===== self return selector 4~5 (MCP2 GPB0-3) → button 22~25 =====
    for (int i = 0; i < 2; i++) {
        if (pressed(mcp2Status, 8 + i * 2)) 
            gp.buttons |= (1UL << (22 + i * 2));
        if (pressed(mcp2Status, 9 + i * 2)) 
            gp.buttons |= (1UL << (23 + i * 2));
    }
    
    // ===== ENC1_BTN, ENC2_BTN (MCP2 GPB4-5) → button 26~27 =====
    if (pressed(mcp2Status, 12)) gp.buttons |= (1UL << 26);
    if (pressed(mcp2Status, 13)) gp.buttons |= (1UL << 27);
    
    // ===== keybox → button 28~29 =====
    processKeybox(now);
    processSimHub();
    
    static hid_gamepad_report_t prev = {};
    if (usb_hid.ready() && memcmp(&gp, &prev, sizeof(gp)) != 0) {
        usb_hid.sendReport(0, &gp, sizeof(gp));
        prev = gp;
    }  
}