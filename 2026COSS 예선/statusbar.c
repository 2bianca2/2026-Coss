#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "statusbar.h"
#include "grid.h"
#include "display.h"
#include "text.h"
#include "adc.h"
#include "calib.h" // calib_raw_to_mv()

#define ROW1_Y 4  // 전역: RUN/STOP, CH1/CH2 on-off, T/DIV
#define ROW2_Y 16 // CH1
#define ROW3_Y 28 // CH2

#define CH_LABEL_X    4
#define CH_CONTENT_X  16
#define MEASURE_ITEM_STRIDE 40
#define MEASURE_VALUE_X (CH_CONTENT_X + MEASURE_ITEM_COUNT * MEASURE_ITEM_STRIDE + 6)

#define TRIG_STATUS_X 340 // ROW1(전역줄)의 T:xxx(x=260) 뒤 여유 공간
#define TRIG_STATUS_W 60  // "SEARCH" 기준 폭 - 지우기용

// 문자열 리터럴은 PROGMEM 없이 그냥 const로만 두면 AVR에서도 SRAM에 중복 저장됨 -
// 각 문자열은 물론 포인터 배열(VOLT_DIV_STR/TIME_DIV_STR 자체)도 PROGMEM에 둠. 그래서 쓸 땐
// pgm_read_word()로 배열에서 포인터부터 꺼낸 다음, strncpy_P()로 필요한 문자열만 잠깐 SRAM
// 버퍼에 복사해 씀 (draw_channel_row/statusbar_draw_full 참고).
// ui.h의 VOLT_PER_DIV_MV[]와 순서가 같아야 함 (-5V~+5V 기준으로 다시 잡은 값)
static const char VSTR_0[] PROGMEM = "100MV";
static const char VSTR_1[] PROGMEM = "200MV";
static const char VSTR_2[] PROGMEM = "300MV";
static const char VSTR_3[] PROGMEM = "500MV";
static const char VSTR_4[] PROGMEM = "700MV";
static const char VSTR_5[] PROGMEM = "1V";
static const char VSTR_6[] PROGMEM = "1.5V";
static const char VSTR_7[] PROGMEM = "2V";
static const char VSTR_8[] PROGMEM = "3V";
static const char VSTR_9[] PROGMEM = "5V";
static const char *const VOLT_DIV_STR[VOLT_DIV_TABLE_SIZE] PROGMEM = {
    VSTR_0, VSTR_1, VSTR_2, VSTR_3, VSTR_4, VSTR_5, VSTR_6, VSTR_7, VSTR_8, VSTR_9
};
// ui.h의 TIME_PER_DIV_US[]와 순서가 같아야 함 (200us~1s)
static const char TSTR_0[]  PROGMEM = "200US";
static const char TSTR_1[]  PROGMEM = "500US";
static const char TSTR_2[]  PROGMEM = "1MS";
static const char TSTR_3[]  PROGMEM = "2MS";
static const char TSTR_4[]  PROGMEM = "5MS";
static const char TSTR_5[]  PROGMEM = "10MS";
static const char TSTR_6[]  PROGMEM = "20MS";
static const char TSTR_7[]  PROGMEM = "50MS";
static const char TSTR_8[]  PROGMEM = "100MS";
static const char TSTR_9[]  PROGMEM = "200MS";
static const char TSTR_10[] PROGMEM = "500MS";
static const char TSTR_11[] PROGMEM = "1S";
static const char *const TIME_DIV_STR[TIME_DIV_TABLE_SIZE] PROGMEM = {
    TSTR_0, TSTR_1, TSTR_2, TSTR_3, TSTR_4, TSTR_5, TSTR_6, TSTR_7, TSTR_8, TSTR_9, TSTR_10, TSTR_11
};

static uint32_t isqrt32(uint32_t v)
{
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

// 제로크로싱(상승) 2개를 찾아서 그 사이 간격 = 한 주기로 삼는다.
// 선형보간으로 샘플 사이의 정확한 교차 위치까지 구해서(Q8 고정소수점) 정밀도를 높인다.
// 한 주기를 못 찾으면(신호 없음/DC/버퍼보다 느린 신호) 0을 리턴.
//
// 기준선은 고정된 0V가 아니라 이 프레임의 (최댓값+최솟값)/2로 매번 다시 잡는다.
// 구형파(프론트엔드가 0V 중심으로 분배해줌)는 원래 0V를 지나가지만, RC 충방전
// 곡선처럼 신호가 한쪽으로 치우쳐서 0V를 아예 안 지나가는 경우 고정 0V 기준으론
// 크로싱을 영영 못 찾음 - 실제 스코프의 auto-level 트리거처럼 신호 자신의 중간값을
// 기준으로 삼으면 어떤 신호든 항상 그 지점을 지나가게 되어 동작함.
static uint8_t find_period_us(uint8_t ch_idx, const volatile uint16_t *adc_buf, uint32_t *out_period_us)
{
    int32_t mx = calib_raw_to_mv(ch_idx, adc_buf[0]);
    int32_t mn = mx;
    for (uint8_t i = 1; i < ADC_BUFFER_SIZE; i++) {
        int32_t mv = calib_raw_to_mv(ch_idx, adc_buf[i]);
        if (mv > mx) mx = mv;
        if (mv < mn) mn = mv;
    }
    int32_t mid = (mx + mn) / 2;

    int32_t prev_mv = calib_raw_to_mv(ch_idx, adc_buf[0]) - mid;
    int32_t first_pos_q8 = -1;
    int32_t second_pos_q8 = -1;

    for (uint8_t i = 1; i < ADC_BUFFER_SIZE; i++) {
        int32_t cur_mv = calib_raw_to_mv(ch_idx, adc_buf[i]) - mid;

        if (prev_mv < 0 && cur_mv >= 0) {
            // prev_mv(중간값 아래)와 cur_mv(중간값 이상) 사이 어디서 지났는지 보간
            int32_t frac_q8 = (int32_t)(((int64_t)(0 - prev_mv) * 256) / (cur_mv - prev_mv));
            int32_t pos_q8 = (int32_t)(i - 1) * 256 + frac_q8;

            if (first_pos_q8 < 0)
                first_pos_q8 = pos_q8;
            else {
                second_pos_q8 = pos_q8;
                break;
            }
        }
        prev_mv = cur_mv;
    }

    if (first_pos_q8 < 0 || second_pos_q8 < 0)
        return 0;

    uint32_t period_samples_q8 = (uint32_t)(second_pos_q8 - first_pos_q8);
    uint16_t rate = adc_get_sample_rate();
    *out_period_us = (uint32_t)(((uint64_t)period_samples_q8 * 1000000ULL) / ((uint32_t)256 * rate));
    return 1;
}

// adc_buf 현재 프레임에서 MAX/MIN/RMS(mV)를 계산 (RMS/MAX/MIN 메뉴용). ch_idx: calib.c 보정값 조회용.
static void compute_stats(uint8_t ch_idx, const volatile uint16_t *adc_buf,
                           int32_t *out_max, int32_t *out_min, int32_t *out_rms_mv)
{
    int32_t mx = calib_raw_to_mv(ch_idx, adc_buf[0]);
    int32_t mn = mx;
    int64_t sumsq = 0;

    for (uint8_t i = 0; i < ADC_BUFFER_SIZE; i++) {
        int32_t mv = calib_raw_to_mv(ch_idx, adc_buf[i]);
        if (mv > mx) mx = mv;
        if (mv < mn) mn = mv;
        sumsq += (int64_t)mv * (int64_t)mv;
    }

    *out_max = mx;
    *out_min = mn;
    *out_rms_mv = (int32_t)isqrt32((uint32_t)(sumsq / ADC_BUFFER_SIZE));
}

static void format_measure_value(const ui_state_t *ui, uint8_t ch_idx, const volatile uint16_t *adc_buf, uint8_t sel_idx, char *out, uint8_t out_size)
{
    switch (sel_idx) {
        case MEASURE_FFT_IDX: {
            // 실제 스펙트럼 그래프는 app.c가 그래프 영역에 그림(spectrum.c) - 여긴 상태 텍스트만.
            // 채널 하나만 켜진 전체화면일 때만 그래프가 실제로 뜸(ui.h 주석 참고).
            uint8_t single_ch = (ui->ch1_enabled != ui->ch2_enabled);
            snprintf_P(out, out_size, single_ch ? PSTR("FFT:GRAPH") : PSTR("FFT:N/A(1CH)"));
            break;
        }
        case 0: case 1: case 2: {
            int32_t mx, mn, rms;
            compute_stats(ch_idx, adc_buf, &mx, &mn, &rms);
            if (sel_idx == 0)      snprintf_P(out, out_size, PSTR("RMS:%ldMV"), (long)rms);
            else if (sel_idx == 1) snprintf_P(out, out_size, PSTR("MAX:%ldMV"), (long)mx);
            else                    snprintf_P(out, out_size, PSTR("MIN:%ldMV"), (long)mn);
            break;
        }
        case 4: { // FREQ - 제로크로싱으로 구한 주기의 역수
            uint32_t period_us;
            if (find_period_us(ch_idx, adc_buf, &period_us)) {
                uint32_t freq_centihz = 100000000UL / period_us; // Hz*100 (소수 2자리)
                snprintf_P(out, out_size, PSTR("FREQ:%lu.%02luHZ"),
                         (unsigned long)(freq_centihz / 100), (unsigned long)(freq_centihz % 100));
            } else {
                snprintf_P(out, out_size, PSTR("FREQ:---")); // 신호 없음/DC/버퍼보다 느린 신호
            }
            break;
        }
        case 5: { // PERIOD - 제로크로싱 사이 간격
            uint32_t period_us;
            if (find_period_us(ch_idx, adc_buf, &period_us)) {
                if (period_us < 100000UL)
                    snprintf_P(out, out_size, PSTR("PERIOD:%luUS"), (unsigned long)period_us);
                else
                    snprintf_P(out, out_size, PSTR("PERIOD:%luMS"), (unsigned long)(period_us / 1000));
            } else {
                snprintf_P(out, out_size, PSTR("PERIOD:---"));
            }
            break;
        }
        default: {
            char name[8];
            strncpy_P(name, ui_measure_name(sel_idx), sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            snprintf_P(out, out_size, PSTR("%s:N/A"), name);
            break;
        }
    }
}

// value 부분만 지우고 다시 그림 - ADC 프레임마다 불러도 되도록 가볍게.
static void draw_measure_value(const ui_state_t *ui, uint8_t ch_idx, uint16_t y, const volatile uint16_t *adc_buf, uint8_t sel_idx)
{
    char buf[24];
    format_measure_value(ui, ch_idx, adc_buf, sel_idx, buf, sizeof(buf));

    lcd_fill_rect(MEASURE_VALUE_X, y, LCD_WIDTH - 1, y + 6, COLOR_NAVY);
    lcd_draw_string(MEASURE_VALUE_X, y, buf, COLOR_WHITE);
}

// 채널 한 줄: 라벨("1"/"2") + (measure_mode면 메뉴+값, 아니면 V/DIV)
static void draw_channel_row(uint8_t ch_idx, uint16_t y, const ui_state_t *ui, const volatile uint16_t *adc_buf)
{
    uint8_t enabled = (ch_idx == 0) ? ui->ch1_enabled : ui->ch2_enabled;
    char label[2] = { (char)('1' + ch_idx), '\0' };
    if (!enabled)
        lcd_draw_string(CH_LABEL_X, y, label, COLOR_DKGREY);
    else if (ch_idx == 0)
        lcd_draw_string(CH_LABEL_X, y, label, COLOR_GREEN); // CH1 트레이스 색과 통일
    else
        lcd_draw_string(CH_LABEL_X, y, label, COLOR_CYAN);  // CH2 트레이스 색과 통일

    if (!enabled)
        return; // 꺼진 채널은 라벨만 어둡게 표시하고 나머지는 비움

    if (!ui->measure_mode) {
        char vstr[8];
        strncpy_P(vstr, (PGM_P)pgm_read_word(&VOLT_DIV_STR[ui->volt_div_idx[ch_idx]]), sizeof(vstr) - 1);
        vstr[sizeof(vstr) - 1] = '\0';
        char buf[12];
        snprintf_P(buf, sizeof(buf), PSTR("V:%s"), vstr);
        lcd_draw_string(CH_CONTENT_X, y, buf, COLOR_YELLOW);
        return;
    }

    uint8_t menu_idx = ui->measure_menu_idx[ch_idx];
    uint8_t sel_idx = ui->measure_selected[ch_idx];
    uint16_t x = CH_CONTENT_X;
    for (uint8_t i = 0; i < MEASURE_ITEM_COUNT; i++) {
        uint8_t r, g, b;
        if (i == menu_idx)      { r = 255; g = 210; b = 0; }   // 커서(노랑)
        else if (i == sel_idx)  { r = 0;   g = 220; b = 255; } // 확정(시안)
        else                     { r = 90;  g = 100; b = 90; } // 그 외(어둡게)
        lcd_draw_string_P(x, y, ui_measure_name(i), r, g, b);
        x += MEASURE_ITEM_STRIDE;
    }

    draw_measure_value(ui, ch_idx, y, adc_buf, sel_idx);
}

void statusbar_draw_full(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const volatile uint16_t *adc_buf2)
{
    lcd_fill_rect(0, 0, LCD_WIDTH - 1, STATUS_BAR_H - 1, COLOR_NAVY);

    if (ui->run_stopped)
        lcd_draw_string_P(4, ROW1_Y, PSTR("STOP"), COLOR_RED);
    else
        lcd_draw_string_P(4, ROW1_Y, PSTR("RUN"), COLOR_GREEN);

    if (ui->ch1_enabled)
        lcd_draw_string_P(60, ROW1_Y, PSTR("CH1:ON"), COLOR_GREEN);
    else
        lcd_draw_string_P(60, ROW1_Y, PSTR("CH1:OFF"), COLOR_DKGREY);

    if (ui->ch2_enabled)
        lcd_draw_string_P(160, ROW1_Y, PSTR("CH2:ON"), COLOR_CYAN);
    else
        lcd_draw_string_P(160, ROW1_Y, PSTR("CH2:OFF"), COLOR_DKGREY);

    // T/DIV는 ADC 1개를 CH1/CH2가 공유해서 채널별이 아니라 전역 한 줄에 표시
    char tstr[8];
    strncpy_P(tstr, (PGM_P)pgm_read_word(&TIME_DIV_STR[ui->time_div_idx]), sizeof(tstr) - 1);
    tstr[sizeof(tstr) - 1] = '\0';
    char buf[16];
    snprintf_P(buf, sizeof(buf), PSTR("T:%s"), tstr);
    if (ui->ch1_axis == AXIS_HORIZONTAL)
        lcd_draw_string(260, ROW1_Y, buf, COLOR_YELLOW);
    else
        lcd_draw_string(260, ROW1_Y, buf, COLOR_DKGREY);

    draw_channel_row(0, ROW2_Y, ui, adc_buf1);
    draw_channel_row(1, ROW3_Y, ui, adc_buf2);

    statusbar_update_trigger_status(ui);
}

void statusbar_update_trigger_status(const ui_state_t *ui)
{
    lcd_fill_rect(TRIG_STATUS_X, ROW1_Y, TRIG_STATUS_X + TRIG_STATUS_W, ROW1_Y + 6, COLOR_NAVY);

    if (!ui->trigger_enabled) {
        lcd_draw_string_P(TRIG_STATUS_X, ROW1_Y, PSTR("T:OFF"), COLOR_DKGREY);
        return;
    }

    if (adc_trigger_locked)
        lcd_draw_string_P(TRIG_STATUS_X, ROW1_Y, PSTR("TRIG"), COLOR_GREEN);
    else
        lcd_draw_string_P(TRIG_STATUS_X, ROW1_Y, PSTR("SEARCH"), COLOR_YELLOW);
}

void statusbar_update_measure_values(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const volatile uint16_t *adc_buf2)
{
    if (!ui->measure_mode)
        return;

    if (ui->ch1_enabled)
        draw_measure_value(ui, 0, ROW2_Y, adc_buf1, ui->measure_selected[0]);
    if (ui->ch2_enabled)
        draw_measure_value(ui, 1, ROW3_Y, adc_buf2, ui->measure_selected[1]);
}
