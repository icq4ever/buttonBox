#include "Adafruit_TinyUSB.h"
#include <Wire.h>
#include <Adafruit_MCP23X17.h>

// HID 게임패드 리포트 디스크립터
uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()
};

Adafruit_USBD_HID usb_hid(desc_hid_report, sizeof(desc_hid_report), 
                          HID_ITF_PROTOCOL_NONE, 2, false);

hid_gamepad_report_t gp;

Adafruit_MCP23X17 mcp1;  // 주소 0x20
Adafruit_MCP23X17 mcp2;  // 주소 0x21

bool rawBtnStatus[32];
bool lastRawBtnStatus[32];
bool encA_prev[5];

uint16_t mcp1Status;
uint16_t mcp2Status;

void setup() {
    Wire.begin();

    mcp1.begin_I2C(0x20);
    mcp2.begin_I2C(0x21);

    for(int i=0; i<16; i++){
        mcp1.pinMode(i, INPUT_PULLUP);
        mcp2.pinMode(i, INPUT_PULLUP);
    }

    usb_hid.begin();
    
    while (!TinyUSBDevice.mounted()) delay(1);
}

void readMcp(){
    // read button status from ioExpander
    mcp1Status = mcp1.readGPIOAB();
    mcp2Status = mcp2.readGPIOAB();

    // save to buttonStatusArray
    // 비트 → boolean 배열 변환
    for (int i = 0; i < 16; i++) {
        rawBtnStatus[i] = !(mcp1_state & (1 << i));      // LOW = true (눌림)
        rawBtnStatus[i + 16] = !(mcp2_state & (1 << i)); // LOW = true (눌림)
    }
}

void handleEncoders(){
    for(int i=0; i<5; i++){
        bool A = rawBtnStatus[i*2];
        bool B = rawBtnStatus[i*2+1];

        if(encA_prev[i] && !A){
            if(B)   gp.buttons |= (1UL << (i*2));
            else    gp.buttons |= (1UL << (i*2+1));
        }

        encA_prev[i] = A;
    }
}

void saveLastMcp(){
    for(int i=0; i<32; i++)     lastRawBtnStatus[i] = rawBtnStatus[i];
    handleEncoders();
}

void loop() {
    readMcp();
    
    handleEncoders();
    

    saveLastMcp();
}