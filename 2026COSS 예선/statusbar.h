/*
 * statusbar.h - 화면 맨 위 상태바(3줄: 전역, CH1, CH2).
 */
#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <stdint.h>
#include "ui.h"

void statusbar_draw_full(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const volatile uint16_t *adc_buf2);
void statusbar_update_measure_values(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const volatile uint16_t *adc_buf2);
void statusbar_update_trigger_status(const ui_state_t *ui);

#endif
