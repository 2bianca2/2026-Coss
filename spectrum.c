#include <stdio.h>
#include <avr/pgmspace.h>
#include "spectrum.h"
#include "fft.h"
#include "display.h"
#include "grid.h"
#include "text.h"
#include "adc.h" // adc_get_sample_rate() - 빈 번호를 실제 Hz로 환산하는 용도

#define HEADROOM_NUM 4
#define HEADROOM_DEN 5   // 막대 최대 높이를 80%로 제한 - 꽉 채우면 위쪽이 잘린 것처럼 보여서 여유를 둠
#define AXIS_LABEL_H 8   // 맨 아래 주파수축 라벨(0HZ / 나이퀴스트)용으로 막대가 안 침범하게 비워두는 높이
#define PEAK_LABEL_W 140 // 맨 위 PEAK 라벨 지우는 폭
#define AXIS_END_W   70  // 축 양끝 라벨 지우는 폭

static uint16_t prev_bar_h[FFT_BINS]; // 이전 프레임 막대 높이(px) - 줄어든 부분만 지우는 용도
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

    // 자동 스케일 - DC(0번 빈)는 보통 나머지보다 훨씬 커서 기준에서 빼야 AC 성분들이 다
    // 눌려서 안 보이는 걸 막을 수 있음. 동시에 "피크 주파수"도 여기서 같이 찾아둠(1번
    // 계산한 mag[]를 재사용 - 상태바 쪽에서 중복 계산 안 하려고 여기서 라벨까지 그림).
    uint16_t maxmag = 1; // 0으로 나누기 방지
    uint8_t peak_bin = 1;
    for (uint8_t k = 1; k < FFT_BINS; k++) {
        if (mag[k] > maxmag) { maxmag = mag[k]; peak_bin = k; }
    }

    uint16_t bar_w = (uint16_t)(LCD_WIDTH / FFT_BINS);
    // 맨 아래 AXIS_LABEL_H px는 축 라벨 전용으로 비워둠(막대가 안 겹침) + 나머지 중
    // 80%만 막대 최대 높이로 써서 위쪽에도 여유를 둠(둘 다 "위가 잘린다"는 문제 해결).
    uint16_t plot_bottom = (uint16_t)(region->y1 - AXIS_LABEL_H);
    uint16_t plot_range_full = (uint16_t)(plot_bottom - region->y0);
    uint16_t plot_range = (uint16_t)((uint32_t)plot_range_full * HEADROOM_NUM / HEADROOM_DEN);

    for (uint8_t k = 0; k < FFT_BINS; k++) {
        uint32_t h32 = ((uint32_t)mag[k] * plot_range) / maxmag;
        if (h32 > plot_range) h32 = plot_range;
        uint16_t h = (uint16_t)h32;

        uint16_t x0 = (uint16_t)(k * bar_w);
        uint16_t x1 = (uint16_t)(x0 + bar_w - 2); // 막대 사이 살짝 간격
        uint16_t ybar = (uint16_t)(plot_bottom - h);

        uint16_t prev_h = has_prev ? prev_bar_h[k] : 0;
        if (h < prev_h) {
            // 막대가 줄었으면 그만큼 남는 윗부분만 지우고 격자 복구 (waveform.c와 같은 방식)
            uint16_t prev_ybar = (uint16_t)(plot_bottom - prev_h);
            grid_redraw_rect(region, x0, prev_ybar, x1, (uint16_t)(ybar - 1));
        }
        lcd_fill_rect(x0, ybar, x1, plot_bottom, r, g, b);

        prev_bar_h[k] = h;
    }
    has_prev = 1;

    // ---- 주파수 라벨: 위에 피크 주파수, 맨 아래 양끝에 0HZ/나이퀴스트(축 범위) ----
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
