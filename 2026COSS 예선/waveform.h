/*
 * waveform.h - ADC 버퍼를 LCD에 파형으로 그리는 모듈. 부분갱신 방식.
 */
#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdint.h>
#include "grid.h"
#include "adc.h"

typedef struct {
    const scope_region_t *region;
    uint16_t prev_y0[ADC_BUFFER_SIZE];
    uint16_t prev_y1[ADC_BUFFER_SIZE];
    uint8_t has_prev[ADC_BUFFER_SIZE];
    int32_t half_range_mv;
} waveform_ch_t;

void waveform_init(waveform_ch_t *wf);
void waveform_set_region(waveform_ch_t *wf, const scope_region_t *region);
void waveform_set_volt_div(waveform_ch_t *wf, uint8_t volt_div_idx);

typedef void (*waveform_poll_fn)(void);

void waveform_draw(waveform_ch_t *wf, const volatile uint16_t *adc_buf, uint8_t ch_idx,
                    uint8_t r, uint8_t g, uint8_t b, waveform_poll_fn poll);
void waveform_clear(waveform_ch_t *wf);

#endif
