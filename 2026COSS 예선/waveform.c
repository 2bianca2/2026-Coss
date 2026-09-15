#include <avr/io.h>
#include "waveform.h"
#include "display.h"
#include "grid.h"
#include "adc.h"
#include "ui.h"    // VOLT_PER_DIV_MV[], VOLT_DIV_TABLE_SIZE
#include "calib.h" // calib_raw_to_mv(), calib_active()

#define LINE_THICKNESS 0 // 선분 두께(±px) - 0이면 딱 1px짜리 얇은 선

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
    // VOLT_PER_DIV_MV는 PROGMEM(ui.c)이라 pgm_read_word로 읽어야 함.
    uint16_t volt_per_div_mv = pgm_read_word(&VOLT_PER_DIV_MV[volt_div_idx]);
    wf->half_range_mv = ((int32_t)wf->region->v_divs * volt_per_div_mv) / 2;
}

// mV -> y좌표. 중앙(wave_mid_y)이 0V, half_range_mv를 넘어가면 화면 끝에서 클리핑.
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

#define DIAG_STEPS 3 // 컬럼당 보간 단계 수 - 픽셀 단위(최대 8번)로 하면 SPI 전송이 너무 늘어서
                     // 트리거 없는 파형(매 프레임 옆으로 미묘하게 밀림)에서 거의 모든 컬럼이
                     // "바뀜"으로 잡혀 메인루프가 거의 계속 그리기에 묶여버림(버튼 다 씹힘)

// prev_x..x 사이를 몇 단계로 나눠 선형보간해서 대각선에 가깝게 그린다(고정 SPI 비용).
// 사각형 하나로 통째로 채우면 값이 확 튀는 구간에서 대각선이 아니라 두꺼운 세로
// 블록처럼 보이기 때문.
static void draw_diagonal_segment(uint16_t prev_x, uint16_t prev_y, uint16_t x, uint16_t y,
                                   uint16_t lo, uint16_t hi, uint8_t r, uint8_t g, uint8_t b)
{
    if (x == prev_x) { // 첫 샘플(i==0): 이을 이전 점이 없으니 점 하나만
        uint16_t sy0 = (y > lo + LINE_THICKNESS) ? (uint16_t)(y - LINE_THICKNESS) : lo;
        uint16_t sy1 = (uint16_t)(y + LINE_THICKNESS);
        if (sy1 > hi) sy1 = hi;
        lcd_fill_rect(x, sy0, x, sy1, r, g, b);
        return;
    }

    int32_t dx = (int32_t)x - (int32_t)prev_x;
    int32_t dy = (int32_t)y - (int32_t)prev_y;
    uint16_t seg_x0 = prev_x;
    uint16_t seg_y_start = prev_y; // 이 조각이 시작하는 높이(직전 조각의 끝 높이와 항상 같음)

    for (uint8_t s = 1; s <= DIAG_STEPS; s++) {
        uint16_t seg_x1 = (s == DIAG_STEPS) ? x : (uint16_t)(prev_x + dx * s / DIAG_STEPS);
        int32_t yi = (int32_t)prev_y + dy * ((int32_t)seg_x1 - (int32_t)prev_x) / dx;
        uint16_t seg_y_end = (uint16_t)yi;

        // 조각 하나가 "시작높이~끝높이"를 통째로 채워야 다음 조각과 안 끊긴다.
        // (한 점만 칠하면 LINE_THICKNESS가 작을 때 조각 사이에 빈틈이 생김)
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
    wf->half_range_mv = ((int32_t)GRID_V_DIVS_FULL * 1000) / 2; // 임시값, set_volt_div로 바로 덮어씀
    reset_trace_tracking(wf);
}

void waveform_set_region(waveform_ch_t *wf, const scope_region_t *region)
{
    wf->region = region;
    grid_draw_region(region);
    reset_trace_tracking(wf);
}

#define POLL_EVERY_N_COLS 2 // 이 컬럼 수마다 한 번씩 poll() 호출 (버튼/엔코더 폴링 기회)
                            // - 4였을 때는 그레이코드 누적(4스텝/디텐트)이 다 못 쌓여서
                            //   그리기 부하가 있는 T/DIV(예: 500us)에서 엔코더가 씹혔음

void waveform_draw(waveform_ch_t *wf, const volatile uint16_t *adc_buf, uint8_t ch_idx,
                    uint8_t r, uint8_t g, uint8_t b, waveform_poll_fn poll)
{
    uint16_t lo = wf->region->y0;
    uint16_t hi = wf->region->y1;
    uint16_t prev_x = 0, prev_y = 0;

    for (uint8_t i = 0; i < ADC_BUFFER_SIZE; i++) {
        uint16_t x = col_x(i);
        uint16_t y = mv_to_y(wf, calib_raw_to_mv(ch_idx, adc_buf[i]));

        // 이 샘플과 이전 샘플을 잇는 선분의 y범위 (한 샘플뿐인 첫 컬럼은 그 점 자체)
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

        // 지난 프레임과 이 컬럼의 선분 범위가 완전히 같으면 건드리지 않음 (깜빡임 방지)
        if (!wf->has_prev[i] || y0 != wf->prev_y0[i] || y1 != wf->prev_y1[i]) {
            if (wf->has_prev[i]) {
                lcd_fill_rect(prev_x, wf->prev_y0[i], x, wf->prev_y1[i], COLOR_NAVY); // 이전 선분 지움(범위 전체)
                grid_redraw_rect(wf->region, prev_x, wf->prev_y0[i], x, wf->prev_y1[i]); // 지워진 격자선 복원
            }
            // 새 선분은 대각선으로 그림 - 지우는 범위(y0..y1)는 대각선을 항상 감싸므로 안전
            draw_diagonal_segment(prev_x, prev_y, x, y, lo, hi, r, g, b);

            wf->prev_y0[i] = y0;
            wf->prev_y1[i] = y1;
            wf->has_prev[i] = 1;
        }

        prev_x = x;
        prev_y = y;

        if (poll && (i % POLL_EVERY_N_COLS) == (POLL_EVERY_N_COLS - 1)) {
            poll(); // 74HC165가 LCD랑 SPI를 공유해서 인터럽트로는 못 읽으니, 그리는 도중 짬을 내서 폴링
            // 폴링 중에 캘리브레이션이 시작됐으면(롱프레스) 화면이 이미 캘리브레이션
            // 안내문구로 덮였을 테니, 여기서 계속 그려서 그 위에 덧칠하면 안 됨
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
