/*
 * grid.h - 오실로스코프 격자 + 화면 분할(전체화면/CH1-CH2 분할) 영역 정의.
 */
#ifndef GRID_H
#define GRID_H

#include <stdint.h>
#include "display.h"

#define STATUS_BAR_H 38
#define GRID_TOP     STATUS_BAR_H

#define GRID_H_DIVS      20
#define GRID_V_DIVS_FULL 12
#define GRID_V_DIVS_HALF 6

typedef struct {
    uint16_t y0, y1;
    uint8_t v_divs;
} scope_region_t;

extern const scope_region_t REGION_FULL;
extern const scope_region_t REGION_CH1_HALF;
extern const scope_region_t REGION_CH2_HALF;

#define DIVIDER_THICKNESS 3

#define REGION_CH1_HALF_Y1 (GRID_TOP + (LCD_HEIGHT - 1 - GRID_TOP - DIVIDER_THICKNESS) / 2 - 1)
#define DIVIDER_Y0 (REGION_CH1_HALF_Y1 + 1)
#define DIVIDER_Y1 (DIVIDER_Y0 + DIVIDER_THICKNESS - 1)

void grid_draw_region(const scope_region_t *r);
void grid_redraw_rect(const scope_region_t *r, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void grid_draw_divider(void);

#endif
