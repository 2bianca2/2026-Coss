#include "grid.h"
#include "display.h"

const scope_region_t REGION_FULL     = { GRID_TOP, LCD_HEIGHT - 1, GRID_V_DIVS_FULL };
const scope_region_t REGION_CH1_HALF = { GRID_TOP, REGION_CH1_HALF_Y1, GRID_V_DIVS_HALF };
const scope_region_t REGION_CH2_HALF = { DIVIDER_Y1 + 1, LCD_HEIGHT - 1, GRID_V_DIVS_HALF };

// 세로선(x좌표)은 화면 전체 폭 기준이라 region과 상관없이 위치가 고정됨
static uint16_t vline_x(uint8_t k)
{
    return (uint16_t)((uint32_t)k * (LCD_WIDTH - 1) / GRID_H_DIVS);
}

// 가로선(y좌표)은 region의 y0..y1 안에서 v_divs 칸으로 나눔
static uint16_t hline_y(const scope_region_t *r, uint8_t k)
{
    return (uint16_t)(r->y0 + (uint32_t)k * (r->y1 - r->y0) / r->v_divs);
}

void grid_draw_region(const scope_region_t *r)
{
    lcd_fill_rect(0, r->y0, LCD_WIDTH - 1, r->y1, COLOR_NAVY);

    for (uint8_t k = 0; k <= GRID_H_DIVS; k++) {
        uint16_t x = vline_x(k);
        lcd_fill_rect(x, r->y0, x, r->y1, COLOR_GRID);
    }
    for (uint8_t k = 0; k <= r->v_divs; k++) {
        uint16_t y = hline_y(r, k);
        lcd_fill_rect(0, y, LCD_WIDTH - 1, y, COLOR_GRID);
    }

    uint16_t midx = vline_x(GRID_H_DIVS / 2);
    uint16_t midy = hline_y(r, r->v_divs / 2);
    lcd_fill_rect(midx, r->y0, midx, r->y1, COLOR_LTGREY);
    lcd_fill_rect(0, midy, LCD_WIDTH - 1, midy, COLOR_LTGREY);
}

void grid_redraw_rect(const scope_region_t *r, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t midx = vline_x(GRID_H_DIVS / 2);
    uint16_t midy = hline_y(r, r->v_divs / 2);

    for (uint8_t k = 0; k <= GRID_H_DIVS; k++) {
        uint16_t x = vline_x(k);
        if (x >= x0 && x <= x1) {
            if (x == midx)
                lcd_fill_rect(x, y0, x, y1, COLOR_LTGREY);
            else
                lcd_fill_rect(x, y0, x, y1, COLOR_GRID);
        }
    }
    for (uint8_t k = 0; k <= r->v_divs; k++) {
        uint16_t y = hline_y(r, k);
        if (y >= y0 && y <= y1) {
            if (y == midy)
                lcd_fill_rect(x0, y, x1, y, COLOR_LTGREY);
            else
                lcd_fill_rect(x0, y, x1, y, COLOR_GRID);
        }
    }
}

void grid_draw_divider(void)
{
    lcd_fill_rect(0, DIVIDER_Y0, LCD_WIDTH - 1, DIVIDER_Y1, COLOR_RED);
}
