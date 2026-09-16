/*
 * input.h - hc165_read()의 raw 16bit 값을 가공하는 계층: 디바운스, 엣지검출, 쿼드러처 디코딩.
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

typedef struct {
    uint16_t stable;
    uint16_t candidate;
} debounce_state_t;

uint16_t input_debounce(debounce_state_t *st, uint16_t raw);
uint16_t input_pressed_edges(uint16_t prev_stable, uint16_t curr_stable);

typedef struct {
    uint8_t prev_state;
    int8_t accum;
} quad_state_t;

int8_t input_quad_decode(quad_state_t *st, uint16_t stable, uint16_t clk_mask, uint16_t dt_mask);

#endif
