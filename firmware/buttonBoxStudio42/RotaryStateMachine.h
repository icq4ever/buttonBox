/*
 * ButtonBox Firmware
 * Copyright (c) 2026 Yi donghoon <donghoon@studio42.kr>
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */


#ifndef ROTARY_STATE_MACHINE_H
#define ROTARY_STATE_MACHINE_H

/*
 * Rotary Encoder 상태 머신
 * 원본: brianlow/Rotary 라이브러리 https://github.com/brianlow/Rotary 
 *
 * 원리:
 * - 엔코더는 그레이 코드로 동작 (한 번에 1비트만 변화)
 * - CW:  00 → 01 → 11 → 10 → 00
 * - CCW: 00 → 10 → 11 → 01 → 00
 * - 상태 전이 테이블로 방향 판별
 * - 노이즈/바운싱은 무시됨 (불가능한 전이)
 */

// 상태 정의
#define R_START     0x0
#define R_CW_FINAL  0x1
#define R_CW_BEGIN  0x2
#define R_CW_NEXT   0x3
#define R_CCW_BEGIN 0x4
#define R_CCW_FINAL 0x5
#define R_CCW_NEXT  0x6

// 방향 결과
#define DIR_NONE 0x0
#define DIR_CW   0x10   // 시계 방향
#define DIR_CCW  0x20   // 반시계 방향

/*
 * 상태 전이 테이블
 * 
 * [현재상태][입력] → 다음상태
 * 입력: (pinB << 1) | pinA = 0~3
 * 
 * 상위 4비트: 결과 (DIR_CW, DIR_CCW, DIR_NONE)
 * 하위 4비트: 다음 상태
 */
const unsigned char ttable[7][4] = {
  // 입력:     00           01           10           11
  /* START */    {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
  /* CW_FINAL */ {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
  /* CW_BEGIN */ {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
  /* CW_NEXT */  {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
  /* CCW_BEGIN */{R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
  /* CCW_FINAL */{R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START | DIR_CCW},
  /* CCW_NEXT */ {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START},
};

class RotaryStateMachine {
private:
    unsigned char state;
    
public:
    RotaryStateMachine() : state(R_START) {}
    
    /*
     * 엔코더 입력 처리
     * 
     * @param pinA - A핀 상태 (true = pressed/LOW)
     * @param pinB - B핀 상태 (true = pressed/LOW)
     * @return DIR_CW, DIR_CCW, 또는 DIR_NONE
     * 
     * 매 루프마다 호출해야 함
     */
    unsigned char process(bool pinA, bool pinB) {
        // 2비트 입력 생성: (B << 1) | A
        unsigned char pinstate = (pinB << 1) | pinA;
        
        // 테이블에서 다음 상태 조회
        state = ttable[state & 0x0f][pinstate];
        
        // 상위 4비트 = 결과 (DIR_CW/CCW/NONE)
        return state & 0x30;
    }
    
    // 상태 리셋
    void reset() {
        state = R_START;
    }
    
    // 현재 상태 확인 (디버깅용)
    unsigned char getState() {
        return state & 0x0f;
    }
};

#endif