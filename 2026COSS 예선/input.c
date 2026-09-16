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

// 그레이코드 전이표. idx = (prev_state<<2)|cur_state, state = (CLK<<1)|DT.
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
