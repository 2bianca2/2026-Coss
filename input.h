/*
 * input.h - hc165_read()의 raw 16bit 값을 가공하는 계층.
 *   - 디바운스: 2번 연속 같은 값이어야 "확정"으로 인정
 *   - 엣지검출: 눌리는 순간(0->1)만 골라냄
 *   - 쿼드러처 디코딩: CLK/DT로 회전 방향(+1/-1) 판별
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

typedef struct {
    uint16_t stable;    // 마지막으로 확정된 안정 상태
    uint16_t candidate; // 직전에 읽은 raw 값 (다음번에 또 같으면 stable로 승격)
} debounce_state_t;

uint16_t input_debounce(debounce_state_t *st, uint16_t raw);
uint16_t input_pressed_edges(uint16_t prev_stable, uint16_t curr_stable);

typedef struct {
    uint8_t prev_state; // 직전 (CLK<<1 | DT) 2비트 상태
    int8_t accum;        // 4스텝(그레이코드 한 바퀴)이 쌓여야 디텐트 1칸으로 침
} quad_state_t;

// clk_mask/dt_mask: hc165.h의 HC165_CH1_CLK/HC165_CH1_DT 같은 비트마스크.
// CLK가 "막 올라가는 순간" DT 하나만 찍어서 방향을 판단하던 예전 방식은 폴링
// 타이밍에 너무 민감해서(정확히 그 엣지를 못 잡으면 방향이 헷갈림) 표준 그레이코드
// 전이표 방식으로 바꿨다 - 폴링이 늦어도 상태변화를 누적해서 방향을 정확히 재구성한다.
// 반환값: 디텐트 한 칸을 다 채웠을 때만 +1/-1, 아니면 0
int8_t input_quad_decode(quad_state_t *st, uint16_t stable, uint16_t clk_mask, uint16_t dt_mask);

#endif
