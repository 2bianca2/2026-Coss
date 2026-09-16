/*
 * ui.h - 스위치/엔코더로 조작하는 UI 상태머신.
 *
 *   SW1(Run/Stop)  -> run_stopped 토글, 롱프레스 -> trigger_enabled 토글
 *   SW2(measure)   -> measure_mode 토글
 *   SW3(CH1)       -> ch1_enabled 토글, 롱프레스 -> 캘리브레이션
 *   SW4(CH2)       -> ch2_enabled 토글, 롱프레스 -> 캘리브레이션
 *   CH1 엔코더 푸시 -> ch1_axis 전환 / measure_selected[0] 확정
 *   CH1 CLK/DT     -> volt_div_idx[0] 또는 공유 time_div_idx / measure_menu_idx[0]
 *   CH2 엔코더 푸시 -> measure_selected[1] 확정
 *   CH2 CLK/DT     -> volt_div_idx[1] / measure_menu_idx[1]
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <avr/pgmspace.h>

#define MEASURE_ITEM_COUNT 6   // RMS, MAX, MIN, FFT, FREQ, PERIOD
#define MEASURE_FFT_IDX 3
#define VOLT_DIV_TABLE_SIZE 10
#define TIME_DIV_TABLE_SIZE 12

extern const uint16_t VOLT_PER_DIV_MV[VOLT_DIV_TABLE_SIZE] PROGMEM;
extern const uint32_t TIME_PER_DIV_US[TIME_DIV_TABLE_SIZE] PROGMEM;

typedef enum { AXIS_VERTICAL, AXIS_HORIZONTAL } axis_mode_t;

typedef struct {
    uint8_t run_stopped;
    uint8_t ch1_enabled;
    uint8_t ch2_enabled;
    uint8_t measure_mode;

    axis_mode_t ch1_axis;
    int8_t volt_div_idx[2];
    int8_t time_div_idx;

    uint8_t measure_menu_idx[2];
    uint8_t measure_selected[2];

    uint8_t trigger_enabled;
} ui_state_t;

void ui_init(ui_state_t *ui);
void ui_update(ui_state_t *ui, uint16_t pressed, int8_t ch1_delta, int8_t ch2_delta);
const char *ui_measure_name(uint8_t idx);

#endif
