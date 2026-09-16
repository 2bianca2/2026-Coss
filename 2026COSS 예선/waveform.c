#include <avr/io.h>
#include "waveform.h"
#include "display.h"
#include "grid.h"
#include "adc.h"
#include "ui.h"
#include "calib.h"

#define LINE_THICKNESS 0

static int32_t wave_y_range(const waveform_ch_t *wf)
{
    return (int32_t)(wf->region->y1 - wf->region->y0);
}

static uint16_t wave_mid_y(const waveform_ch_t *wf)
{
    return (uint16_t)(wf->region->y0 + wave_y_range(wf) / 2);
}

void waveform_set_volt_div(waveform_ch_t *wf, uint8_t volt_div_idx)
{
    if (volt_div_idx >= VOLT_DIV_TABLE_SIZE)
        volt_div_idx = VOLT_DIV_TABLE_SIZE - 1;
    uint16_t volt_per_div_mv = pgm_read_word(&VOLT_PER_DIV_MV[volt_div_idx]);
    wf->half_range_mv = ((int32_t)wf->region->v_divs * volt_per_div_mv) / 2;
}

static uint16_t mv_to_y(const waveform_ch_t *wf, int32_t mv)
{
    int32_t half = wf->half_range_mv;
    if (mv > half) mv = half;
    if (mv < -half) mv = -half;

    int32_t offset_px = mv * (wave_y_range(wf) / 2) / half;
    return (uint16_t)(wave_mid_y(wf) - offset_px);
}

static uint16_t col_x(uint8_t i)
{
    return (uint16_t)((uint32_t)i * LCD_WIDTH / ADC_BUFFER_SIZE);
}

#define DIAG_STEPS 3
#define POLL_EVERY_N_COLS 2

static void draw_diagonal_segment(uint16_t prev_x, uint16_t prev_y, uint16_t x, uint16_t y,
                                   uint16_t lo, uint16_t hi, uint8_t r, uint8_t g, uint8_t b)
{
    if (x == prev_x) {
        uint16_t sy0 = (y > lo + LINE_THICKNESS) ? (uint16_t)(y - LINE_THICKNESS) : lo;
        uint16_t sy1 = (uint16_t)(y + LINE_THICKNESS);
        if (sy1 > hi) sy1 = hi;
        lcd_fill_rect(x, sy0, x, sy1, r, g, b);
        return;
    }

    int32_t dx = (int32_t)x - (int32_t)prev_x;
    int32_t dy = (int32_t)y - (int32_t)prev_y;
    uint16_t seg_x0 = prev_x;
    uint16_t seg_y_start = prev_y;

    for (uint8_t s = 1; s <= DIAG_STEPS; s++) {
        uint16_t seg_x1 = (s == DIAG_STEPS) ? x : (uint16_t)(prev_x + dx * s / DIAG_STEPS);
        int32_t yi = (int32_t)prev_y + dy * ((int32_t)seg_x1 - (int32_t)prev_x) / dx;
        uint16_t seg_y_end = (uint16_t)yi;

        uint16_t sy0 = seg_y_start, sy1 = seg_y_end;
        if (sy0 > sy1) {
            uint16_t t = sy0;
            sy0 = sy1;
            sy1 = t;
        }
        sy0 = (sy0 > lo + LINE_THICKNESS) ? (uint16_t)(sy0 - LINE_THICKNESS) : lo;
        sy1 = (uint16_t)(sy1 + LINE_THICKNESS);
        if (sy1 > hi) sy1 = hi;

        lcd_fill_rect(seg_x0, sy0, seg_x1, sy1, r, g, b);
        seg_x0 = seg_x1;
        seg_y_start = seg_y_end;
    }
}

static void reset_trace_tracking(waveform_ch_t *wf)
{
    for (uint8_t i = 0; i < ADC_BUFFER_SIZE; i++) {
        wf->prev_y0[i] = 0;
        wf->prev_y1[i] = 0;
        wf->has_prev[i] = 0;
    }
}

void waveform_init(waveform_ch_t *wf)
{
    wf->region = &REGION_FULL;
    wf->half_range_mv = ((int32_t)GRID_V_DIVS_FULL * 1000) / 2;
    reset_trace_tracking(wf);
}

void waveform_set_region(waveform_ch_t *wf, const scope_region_t *region)
{
    wf->region = region;
    grid_draw_region(region);
    reset_trace_tracking(wf);
}

void waveform_draw(waveform_ch_t *wf, const volatile uint16_t *adc_buf, uint8_t ch_idx,
                    uint8_t r, uint8_t g, uint8_t b, waveform_poll_fn poll)
{
    uint16_t lo = wf->region->y0;
    uint16_t hi = wf->region->y1;
    uint16_t prev_x = 0, prev_y = 0;

    for (uint8_t i = 0; i < ADC_BUFFER_SIZE; i++) {
        uint16_t x = col_x(i);
        uint16_t y = mv_to_y(wf, calib_raw_to_mv(ch_idx, adc_buf[i]));

        uint16_t y0 = (i == 0) ? y : prev_y;
        uint16_t y1 = y;
        if (y0 > y1) {
            uint16_t t = y0;
            y0 = y1;
            y1 = t;
        }
        y0 = (y0 > lo + LINE_THICKNESS) ? (uint16_t)(y0 - LINE_THICKNESS) : lo;
        y1 = (uint16_t)(y1 + LINE_THICKNESS);
        if (y1 > hi)
            y1 = hi;

        if (!wf->has_prev[i] || y0 != wf->prev_y0[i] || y1 != wf->prev_y1[i]) {
            if (wf->has_prev[i]) {
                lcd_fill_rect(prev_x, wf->prev_y0[i], x, wf->prev_y1[i], COLOR_NAVY);
                grid_redraw_rect(wf->region, prev_x, wf->prev_y0[i], x, wf->prev_y1[i]);
            }
            draw_diagonal_segment(prev_x, prev_y, x, y, lo, hi, r, g, b);

            wf->prev_y0[i] = y0;
            wf->prev_y1[i] = y1;
            wf->has_prev[i] = 1;
        }

        prev_x = x;
        prev_y = y;

        if (poll && (i % POLL_EVERY_N_COLS) == (POLL_EVERY_N_COLS - 1)) {
            poll();
            if (calib_active())
                return;
        }
    }
}

void waveform_clear(waveform_ch_t *wf)
{
    uint16_t prev_x = 0;
    for (uint8_t i = 0; i < ADC_BUFFER_SIZE; i++) {
        uint16_t x = col_x(i);
        if (wf->has_prev[i]) {
            lcd_fill_rect(prev_x, wf->prev_y0[i], x, wf->prev_y1[i], COLOR_NAVY);
            grid_redraw_rect(wf->region, prev_x, wf->prev_y0[i], x, wf->prev_y1[i]);
            wf->prev_y0[i] = 0;
            wf->prev_y1[i] = 0;
            wf->has_prev[i] = 0;
        }
        prev_x = x;
    }
}
