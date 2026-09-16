/*
 * fft.h - 64포인트 고정소수점(Q15) radix-2 FFT.
 *   FFT_N은 ADC_BUFFER_SIZE(96)와 별개로 64 고정 - radix-2는 2의 거듭제곱이 필수라
 *   96에 못 맞춰서, adc_buf 앞 64개만 사용.
 */
#ifndef FFT_H
#define FFT_H

#include <stdint.h>

#define FFT_N    64
#define FFT_BINS (FFT_N/2)

// adc_buf 앞쪽 FFT_N개만 읽어 mag_out[FFT_BINS]에 빈별 상대 진폭을 채운다.
void fft_compute_magnitude(uint8_t ch_idx, const volatile uint16_t *adc_buf, uint16_t *mag_out);

#endif
