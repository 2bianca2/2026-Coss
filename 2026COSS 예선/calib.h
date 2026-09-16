/*
 * calib.h - CH1/CH2 2점(GND, +5V) 캘리브레이션. EEPROM 저장.
 */
#ifndef CALIB_H
#define CALIB_H

#include <stdint.h>

typedef enum { CAL_IDLE, CAL_WAIT_GND, CAL_WAIT_REF, CAL_DONE } calib_state_t;

void calib_init(void);
int32_t calib_raw_to_mv(uint8_t ch_idx, uint16_t raw);

void calib_start(uint8_t ch_idx);
uint8_t calib_active(void);
uint8_t calib_active_channel(void);
calib_state_t calib_get_state(void);

void calib_confirm(const volatile uint16_t *adc_buf);
void calib_cancel(void);

#endif
