#include <avr/pgmspace.h>
#include "input.h"

uint16_t input_debounce(debounce_state_t *st, uint16_t raw)
{
    if (raw == st->candidate) {
        st->stable = raw;
    } else {
        st->candidate = raw;
    }
    return st->stable;
}

uint16_t input_pressed_edges(uint16_t prev_stable, uint16_t curr_stable)
{
    return (uint16_t)(~prev_stable) & curr_stable;
}

// 그레이코드 전이표: 이전 상태(2비트) -> 현재 상태(2비트)로 가는 16가지 조합 중
// 유효한 8가지(CW 4개, CCW 4개)만 ±1이고 나머지(제자리/바운스/불가능한 전이)는 0.
// idx = (prev_state << 2) | cur_state, state = (CLK<<1) | DT
// PROGMEM(플래시)에 둠(§9.1, SRAM 절약) - poll마다 읽히는 핫패스지만 1바이트 테이블
// 조회라 pgm_read_byte 오버헤드(몇 사이클)는 무시할 만함.
static const int8_t QUAD_TRANSITION[16] PROGMEM = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0,
};

int8_t input_quad_decode(quad_state_t *st, uint16_t stable, uint16_t clk_mask, uint16_t dt_mask)
{
    uint8_t clk = (stable & clk_mask) ? 1 : 0;
    uint8_t dt  = (stable & dt_mask)  ? 1 : 0;
    uint8_t cur_state = (uint8_t)((clk << 1) | dt);

    uint8_t idx = (uint8_t)((st->prev_state << 2) | cur_state);
    int8_t step = (int8_t)pgm_read_byte(&QUAD_TRANSITION[idx & 0x0F]);
    st->accum = (int8_t)(st->accum + step);
    st->prev_state = cur_state;

    // 방향이 반대로 느껴지면 아래 두 if의 +1/-1만 서로 바꾸면 됨.
    int8_t delta = 0;
    if (st->accum >= 4) {
        delta = +1;
        st->accum = 0;
    } else if (st->accum <= -4) {
        delta = -1;
        st->accum = 0;
    }
    return delta;
}
