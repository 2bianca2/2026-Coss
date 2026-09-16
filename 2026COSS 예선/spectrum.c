#include <stdio.h>
#include <avr/pgmspace.h>
#include "spectrum.h"
#include "fft.h"
#include "display.h"
#include "grid.h"
#include "text.h"
#include "adc.h"

#define HEADROOM_NUM 4
#define HEADROOM_DEN 5   // 막대 최대 높이를 80%로 제한
#define AXIS_LABEL_H 8   // 축 라벨용으로 비워두는 높이
#define PEAK_LABEL_W 140
#define AXIS_END_W   70

static uint16_t prev_bar_h[FFT_BINS];
static uint8_t has_prev = 0;

void spectrum_reset(void)
{
    has_prev = 0;
}

void spectrum_draw(const scope_region_t *region, uint8_t ch_idx, const volatile uint16_t *adc_buf,
                    uint8_t r, uint8_t g, uint8_t b)
{
    static uint16_t mag[FFT_BINS];
    fft_compute_magnitude(ch_idx, adc_buf, mag);

    // DC(0번 빈) 제외 최댓값으로 자동 스케일 + 피크 빈 탐색.
    uint16_t maxmag = 1;
    uint8_t peak_bin = 1;
    for (uint8_t k = 1; k < FFT_BINS; k++) {
        if (mag[k] > maxmag) { maxmag = mag[k]; peak_bin = k; }
    }

    uint16_t bar_w = (uint16_t)(LCD_WIDTH / FFT_BINS);
    uint16_t plot_bottom = (uint16_t)(region->y1 - AXIS_LABEL_H);
    uint16_t plot_range_full = (uint16_t)(plot_bottom - region->y0);
    uint16_t plot_range = (uint16_t)((uint32_t)plot_range_full * HEADROOM_NUM / HEADROOM_DEN);

    for (uint8_t k = 0; k < FFT_BINS; k++) {
        uint32_t h32 = ((uint32_t)mag[k] * plot_range) / maxmag;
        if (h32 > plot_range) h32 = plot_range;
        uint16_t h = (uint16_t)h32;

        uint16_t x0 = (uint16_t)(k * bar_w);
        uint16_t x1 = (uint16_t)(x0 + bar_w - 2);
        uint16_t ybar = (uint16_t)(plot_bottom - h);

        uint16_t prev_h = has_prev ? prev_bar_h[k] : 0;
        if (h < prev_h) {
            uint16_t prev_ybar = (uint16_t)(plot_bottom - prev_h);
            grid_redraw_rect(region, x0, prev_ybar, x1, (uint16_t)(ybar - 1));
        }
        lcd_fill_rect(x0, ybar, x1, plot_bottom, r, g, b);

        prev_bar_h[k] = h;
    }
    has_prev = 1;

    uint16_t rate = adc_get_sample_rate();
    uint32_t peak_hz = ((uint32_t)peak_bin * rate) / FFT_N;
    uint32_t nyq_hz = rate / 2;
    char buf[24];

    uint16_t peak_y = (uint16_t)(region->y0 + 2);
    lcd_fill_rect(0, peak_y, PEAK_LABEL_W, (uint16_t)(peak_y + 6), COLOR_NAVY);
    snprintf_P(buf, sizeof(buf), PSTR("PEAK:%luHZ"), (unsigned long)peak_hz);
    lcd_draw_string(4, peak_y, buf, COLOR_WHITE);

    uint16_t axis_y = (uint16_t)(region->y1 - AXIS_LABEL_H + 1);
    lcd_fill_rect(0, axis_y, AXIS_END_W, region->y1, COLOR_NAVY);
    lcd_draw_string_P(2, axis_y, PSTR("0HZ"), COLOR_LTGREY);

    lcd_fill_rect((uint16_t)(LCD_WIDTH - 1 - AXIS_END_W), axis_y, LCD_WIDTH - 1, region->y1, COLOR_NAVY);
    snprintf_P(buf, sizeof(buf), PSTR("%luHZ"), (unsigned long)nyq_hz);
    lcd_draw_string((uint16_t)(LCD_WIDTH - 1 - AXIS_END_W + 4), axis_y, buf, COLOR_LTGREY);
}
