#include <stdio.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include "calib.h"
#include "display.h"
#include "text.h"
#include "adc.h"

#define CAL_MAGIC 0xCA

typedef struct {
    uint8_t magic;
    uint16_t zero_raw[2]; // raw값 @ 0V (GND) - [0]=CH1,[1]=CH2
    uint16_t ref_raw[2];  // raw값 @ +5V(기준전압)
} eeprom_calib_t;

static eeprom_calib_t EEMEM ee_calib;

static uint16_t zero_raw[2];
static uint16_t ref_raw[2];
static uint8_t active = 0;
static uint8_t active_ch = 0;
static calib_state_t state = CAL_IDLE;

static uint16_t average_buf(const volatile uint16_t *buf)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < ADC_BUFFER_SIZE; i++)
        sum += buf[i];
    return (uint16_t)(sum / ADC_BUFFER_SIZE);
}

static uint8_t has_calib(uint8_t ch_idx)
{
    return ref_raw[ch_idx] != zero_raw[ch_idx];
}

static void render(void)
{
    lcd_fill_rect(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, COLOR_NAVY);

    char title[20];
    snprintf_P(title, sizeof(title), PSTR("CH%d CALIBRATION"), active_ch + 1);
    lcd_draw_string(20, 30, title, COLOR_YELLOW);

    // 문자열들은 PSTR()로 플래시에 두고 lcd_draw_string_P()로 그림(§9.1, SRAM 절약).
    // sw_name/line2처럼 런타임에 조립하지 않고 SW3/SW4 두 경우를 아예 별도 리터럴로
    // 나눠서 PSTR() 안에서 바로 고르게 함 - snprintf 안 거쳐도 되니 더 단순함.
    switch (state) {
        case CAL_WAIT_GND:
            lcd_draw_string_P(20, 60, PSTR("STEP 1/2: CONNECT GND TO INPUT"), COLOR_WHITE);
            lcd_draw_string_P(20, 80, active_ch == 0 ? PSTR("PRESS SW3 TO CONFIRM") : PSTR("PRESS SW4 TO CONFIRM"), COLOR_WHITE);
            break;
        case CAL_WAIT_REF:
            lcd_draw_string_P(20, 60, PSTR("STEP 2/2: CONNECT +5V REF"), COLOR_WHITE);
            lcd_draw_string_P(20, 80, active_ch == 0 ? PSTR("PRESS SW3 TO CONFIRM") : PSTR("PRESS SW4 TO CONFIRM"), COLOR_WHITE);
            break;
        case CAL_DONE:
            lcd_draw_string_P(20, 60, PSTR("SAVED TO EEPROM"), COLOR_GREEN);
            lcd_draw_string_P(20, 80, active_ch == 0 ? PSTR("PRESS SW3 TO CLOSE") : PSTR("PRESS SW4 TO CLOSE"), COLOR_WHITE);
            break;
        default:
            break;
    }
    lcd_draw_string_P(20, 110, PSTR("SW1 TO CANCEL"), COLOR_DKGREY);
}

void calib_init(void)
{
    uint8_t magic = eeprom_read_byte(&ee_calib.magic);
    if (magic == CAL_MAGIC) {
        eeprom_read_block((void *)zero_raw, (const void *)ee_calib.zero_raw, sizeof(zero_raw));
        eeprom_read_block((void *)ref_raw, (const void *)ee_calib.ref_raw, sizeof(ref_raw));
    } else {
        // 미보정 상태: zero==ref로 둬서 has_calib()이 false가 되게 함
        zero_raw[0] = zero_raw[1] = 0;
        ref_raw[0] = ref_raw[1] = 0;
    }
}

int32_t calib_raw_to_mv(uint8_t ch_idx, uint16_t raw)
{
    if (!has_calib(ch_idx))
        return adc_raw_to_mv(raw); // 보정 전엔 기존 이상적 계산식 그대로

    int32_t span = (int32_t)ref_raw[ch_idx] - (int32_t)zero_raw[ch_idx];
    return ((int32_t)raw - (int32_t)zero_raw[ch_idx]) * 5000L / span;
}

void calib_start(uint8_t ch_idx)
{
    active = 1;
    active_ch = ch_idx;
    state = CAL_WAIT_GND;
    render();
}

uint8_t calib_active(void)
{
    return active;
}

uint8_t calib_active_channel(void)
{
    return active_ch;
}

calib_state_t calib_get_state(void)
{
    return state;
}

void calib_confirm(const volatile uint16_t *adc_buf)
{
    if (!active)
        return;

    switch (state) {
        case CAL_WAIT_GND:
            zero_raw[active_ch] = average_buf(adc_buf);
            state = CAL_WAIT_REF;
            render();
            break;
        case CAL_WAIT_REF:
            ref_raw[active_ch] = average_buf(adc_buf);

            eeprom_write_byte(&ee_calib.magic, CAL_MAGIC);
            eeprom_write_block((const void *)zero_raw, (void *)ee_calib.zero_raw, sizeof(zero_raw));
            eeprom_write_block((const void *)ref_raw, (void *)ee_calib.ref_raw, sizeof(ref_raw));

            state = CAL_DONE;
            render();
            break;
        case CAL_DONE:
            active = 0;
            state = CAL_IDLE;
            break;
        default:
            break;
    }
}

void calib_cancel(void)
{
    active = 0;
    state = CAL_IDLE;
}
