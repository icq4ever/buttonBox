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
Adafruit_MCP23X17 mcp1;  // 0x20: 엔코더, 버튼
Adafruit_MCP23X17 mcp2;  // 0x21: 셀렉터, 버튼, 키박스

// ===== 로커 스위치 (마스터) =====
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
unsigned long lastRpmBlink = 0;
bool rpmBlinkState = false;

// ===== 엔코더 5개 =====
RotaryStateMachine encoders[5];

uint16_t mcp1Status, mcp2Status;

// ===== 홀드 타이머 =====
#define HOLD_TIME 50
unsigned long encHoldUntil[10] = {0};
unsigned long ignitionHoldUntil = 0;

// ===== 키박스 상태 =====
#define KEY_NC    0  // 키 빠짐
#define KEY_ACC   1  // ACC 고정
#define KEY_START 2  // START (스프링 리턴)
uint8_t keyState = KEY_NC;
uint8_t keyStateRaw = KEY_NC;
unsigned long keyDebounceTime = 0;
#define KEY_DEBOUNCE 20  // 20ms

void setup() {
    usb_hid.begin();
    Serial.begin(115200);      
    
    // 로커 스위치 & LED
    pinMode(ROCKER_PIN, INPUT);
    pinMode(ROCKER_LED, OUTPUT);
    digitalWrite(ROCKER_LED, HIGH);       // PUSH POWER to ROCKER_LED
    pinMode(ROCKER_PIN, INPUT_PULLDOWN);  // ← INPUT → INPUT_PULLDOWN

    // LED 출력 설정
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
    
    // 키박스 초기 상태
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
        
        // SimHub 핸드셰이크
        if (cmd == '?') {
            Serial.println("studio42-buttonBox");  // 장치 이름 응답
            return;
        }
        
        switch (cmd) {
            // RPM 상태
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
    
    // RPM 레드존 깜빡임 처리
    if (rpmState == RPM_REDZONE) {
        if (millis() - lastRpmBlink > 50) {
            rpmBlinkState = !rpmBlinkState;
            digitalWrite(LED_RPM, rpmBlinkState);
            lastRpmBlink = millis();
        }
    }
}

// 로커 상태 (LOW = ON)
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
    // 로커 OFF면 키박스 무시
    if (!isRockerOn()) {
        keyState = readKeyState();
        keyStateRaw = keyState;
        return;
    }
    
    uint8_t newStateRaw = readKeyState();
    
    // 상태 변화 감지 → 디바운스 타이머 시작
    if (newStateRaw != keyStateRaw) {
        keyStateRaw = newStateRaw;
        keyDebounceTime = now;
    }
    
    // 디바운스 시간 지나면 실제 상태 업데이트
    if ((now - keyDebounceTime) > KEY_DEBOUNCE) {
        if (keyStateRaw != keyState) {
            // NC → ACC: IGNITION 펄스 (시동 ON)
            if (keyState == KEY_NC && keyStateRaw == KEY_ACC) {
                ignitionHoldUntil = now + HOLD_TIME;
            }
            // ACC → NC: IGNITION 펄스 (시동 OFF)
            else if (keyState == KEY_ACC && keyStateRaw == KEY_NC) {
                ignitionHoldUntil = now + HOLD_TIME;
            }
            // START → ACC: 무시 (스프링 리턴)
            
            keyState = keyStateRaw;
        }
    }
    
    // 버튼 28: IGNITION (펄스)
    if (now < ignitionHoldUntil) gp.buttons |= (1UL << 28);
    
    // 버튼 29: STARTER (홀드)
    if (keyState == KEY_START) gp.buttons |= (1UL << 29);
}

void loop() {
    gp.buttons = 0;
    unsigned long now = millis();
    
    mcp1Status = mcp1.readGPIOAB();
    mcp2Status = mcp2.readGPIOAB();
    
    // ===== 엔코더 1~4 (MCP1 GPA0-7) → 버튼 0~7 =====
    for (int i = 0; i < 4; i++) {
        bool pinA = pressed(mcp1Status, i * 2);
        bool pinB = pressed(mcp1Status, i * 2 + 1);
        
        unsigned char result = encoders[i].process(pinB, pinA);
        if (result == DIR_CW)  encHoldUntil[i * 2] = now + HOLD_TIME;
        if (result == DIR_CCW) encHoldUntil[i * 2 + 1] = now + HOLD_TIME;
    }
    
    // ===== 엔코더 5 (MCP1 GPB0-1) → 버튼 8~9 =====
    {
        bool pinA = pressed(mcp1Status, 8);
        bool pinB = pressed(mcp1Status, 9);
        
        unsigned char result = encoders[4].process(pinB, pinA);
        if (result == DIR_CW)  encHoldUntil[8] = now + HOLD_TIME;
        if (result == DIR_CCW) encHoldUntil[9] = now + HOLD_TIME;
    }
    
    // 엔코더 버튼 홀드 (버튼 0~9)
    for (int i = 0; i < 10; i++) {
        if (now < encHoldUntil[i]) {
            gp.buttons |= (1UL << i);
        }
    }
    
    // ===== BTN_5~10 (MCP1 GPB2-7) → 버튼 10~15 =====
    for (int i = 0; i < 6; i++) {
        if (pressed(mcp1Status, 10 + i)) 
            gp.buttons |= (1UL << (10 + i));
    }
    
    // ===== 셀렉터 1~3 (MCP2 GPA2-7) → 버튼 16~21 =====
    for (int i = 0; i < 3; i++) {
        if (pressed(mcp2Status, 2 + i * 2)) 
            gp.buttons |= (1UL << (16 + i * 2));
        if (pressed(mcp2Status, 3 + i * 2)) 
            gp.buttons |= (1UL << (17 + i * 2));
    }
    
    // ===== 셀렉터 4~5 (MCP2 GPB0-3) → 버튼 22~25 =====
    for (int i = 0; i < 2; i++) {
        if (pressed(mcp2Status, 8 + i * 2)) 
            gp.buttons |= (1UL << (22 + i * 2));
        if (pressed(mcp2Status, 9 + i * 2)) 
            gp.buttons |= (1UL << (23 + i * 2));
    }
    
    // ===== ENC1_BTN, ENC2_BTN (MCP2 GPB4-5) → 버튼 26~27 =====
    if (pressed(mcp2Status, 12)) gp.buttons |= (1UL << 26);
    if (pressed(mcp2Status, 13)) gp.buttons |= (1UL << 27);
    
    // ===== 키박스 → 버튼 28~29 =====
    processKeybox(now);

    processSimHub();
    
    usb_hid.sendReport(0, &gp, sizeof(gp));
}