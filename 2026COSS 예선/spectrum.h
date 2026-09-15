/*
 * spectrum.h - FFT 결과(fft.c)를 막대그래프로 그리는 렌더러.
 *   waveform.c처럼 막대별 이전 높이를 기억해서 줄어든 부분만 지우는 부분갱신 방식이라
 *   화면이 위→아래로 훑으며 깜빡이지 않는다.
 */
#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stdint.h>
#include "grid.h" // scope_region_t

// FFT 스펙트럼을 region 안에 막대그래프로 그린다. adc_buf(ADC_BUFFER_SIZE개)로 매번 새로 계산.
// ch_idx는 calib_raw_to_mv() 보정값 조회용. r,g,b는 막대 색.
void spectrum_draw(const scope_region_t *region, uint8_t ch_idx, const volatile uint16_t *adc_buf,
                    uint8_t r, uint8_t g, uint8_t b);

// FFT 화면에 새로 진입할 때(다른 측정 항목에서 FFT로 바뀌는 등) 호출 - 이전 막대 높이
// 기억(dirty-tracking)을 리셋해서, 방금 새로 그려진 격자를 엉뚱하게 다시 지우지 않게 함.
void spectrum_reset(void);

#endif
