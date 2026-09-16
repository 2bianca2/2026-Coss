#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "statusbar.h"
#include "grid.h"
#include "display.h"
#include "text.h"
#include "adc.h"
#include "calib.h"

#define ROW1_Y 4
#define ROW2_Y 16
#define ROW3_Y 28

#define CH_LABEL_X    4
#define CH_CONTENT_X  16
#define MEASURE_ITEM_STRIDE 40
#define MEASURE_VALUE_X (CH_CONTENT_X + MEASURE_ITEM_COUNT * MEASURE_ITEM_STRIDE + 6)

#define TRIG_STATUS_X 340
#define TRIG_STATUS_W 60

// 문자열/포인터 배열 전부 PROGMEM. 순서는 ui.h의 VOLT_PER_DIV_MV[]와 동일해야 함.
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
// 순서는 ui.h의 TIME_PER_DIV_US[]와 동일해야 함.
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

// 기준선은 고정 0V가 아니라 (최댓값+최솟값)/2 - 신호가 한쪽으로 치우쳐도 동작.
// Q8 고정소수점 선형보간으로 서브샘플 정밀도 확보.
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
        case 4: {
            uint32_t period_us;
            if (find_period_us(ch_idx, adc_buf, &period_us)) {
                uint32_t freq_centihz = 100000000UL / period_us;
                snprintf_P(out, out_size, PSTR("FREQ:%lu.%02luHZ"),
                         (unsigned long)(freq_centihz / 100), (unsigned long)(freq_centihz % 100));
            } else {
                snprintf_P(out, out_size, PSTR("FREQ:---"));
            }
            break;
        }
        case 5: {
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

static void draw_measure_value(const ui_state_t *ui, uint8_t ch_idx, uint16_t y, const volatile uint16_t *adc_buf, uint8_t sel_idx)
{
    char buf[24];
    format_measure_value(ui, ch_idx, adc_buf, sel_idx, buf, sizeof(buf));

    lcd_fill_rect(MEASURE_VALUE_X, y, LCD_WIDTH - 1, y + 6, COLOR_NAVY);
    lcd_draw_string(MEASURE_VALUE_X, y, buf, COLOR_WHITE);
}

static void draw_channel_row(uint8_t ch_idx, uint16_t y, const ui_state_t *ui, const volatile uint16_t *adc_buf)
{
    uint8_t enabled = (ch_idx == 0) ? ui->ch1_enabled : ui->ch2_enabled;
    char label[2] = { (char)('1' + ch_idx), '\0' };
    if (!enabled)
        lcd_draw_string(CH_LABEL_X, y, label, COLOR_DKGREY);
    else if (ch_idx == 0)
        lcd_draw_string(CH_LABEL_X, y, label, COLOR_GREEN);
    else
        lcd_draw_string(CH_LABEL_X, y, label, COLOR_CYAN);

    if (!enabled)
        return;

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
        if (i == menu_idx)      { r = 255; g = 210; b = 0; }
        else if (i == sel_idx)  { r = 0;   g = 220; b = 255; }
        else                     { r = 90;  g = 100; b = 90; }
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
