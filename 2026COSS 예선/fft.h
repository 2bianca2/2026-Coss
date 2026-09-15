/*
 * fft.h - 64포인트 고정소수점(Q15) radix-2 FFT. FFT measure 항목(스펙트럼 그래프)에서 씀.
 *   FFT_N은 ADC_BUFFER_SIZE(96)와 별개로 64 고정 - radix-2 FFT는 2의 거듭제곱이어야
 *   해서 96에 맞출 수가 없음. adc_buf로 받은 96개 중 앞 64개만 떼서 씀(fft_compute_magnitude
 *   내부에서 FFT_N개만 읽으니 호출부는 그냥 평소처럼 adc_buffer_ch1/ch2를 넘기면 됨).
 *   메모리 아끼려고 CH1/CH2 공용 작업버퍼 하나만 씀 - 그래서 한 번에 한 채널만 계산 가능
 *   (어차피 FFT는 채널 하나만 켜진 전체화면일 때만 켜지게 해놔서 문제 없음).
 */
#ifndef FFT_H
#define FFT_H

#include <stdint.h>

#define FFT_N    64        // ADC_BUFFER_SIZE(96)와 독립적으로 64 고정(2의 거듭제곱, radix-2라 필수)
#define FFT_BINS (FFT_N/2) // 실수 입력 FFT라 나이퀴스트까지 앞쪽 절반만 의미 있음(뒤쪽은 대칭)

// adc_buf 앞쪽 FFT_N(64)개만 읽어서 FFT를 돌려 mag_out[FFT_BINS]에 빈(bin)별 진폭을 채운다.
// (adc_buf 자체는 ADC_BUFFER_SIZE=96개짜리를 넘겨받아도 됨 - 앞부분만 씀.)
// ch_idx는 calib_raw_to_mv() 보정값 조회용. 절대 mV가 아니라 그래프 자동 스케일용 상대값 -
// 스펙트럼 막대그래프 높이 비교에는 충분하지만 정밀 계측 용도는 아님.
void fft_compute_magnitude(uint8_t ch_idx, const volatile uint16_t *adc_buf, uint16_t *mag_out);

#endif
