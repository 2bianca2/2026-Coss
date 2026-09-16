/*
 * spectrum.h - FFT 결과(fft.c)를 막대그래프로 그리는 렌더러. 부분갱신 방식.
 */
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>
#include "grid.h"

void spectrum_draw(const scope_region_t *region, uint8_t ch_idx, const volatile uint16_t *adc_buf,
                    uint8_t r, uint8_t g, uint8_t b);
void spectrum_reset(void);

#endif
