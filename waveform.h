/*
 * waveform.h - ADC 버퍼를 LCD에 파형으로 그리는 모듈.
 * 인접 샘플끼리 선(사각형 채움)으로 이어서 그린다 - 샘플이 드문드문 찍히는 빠른
 * 과도현상(RC 충방전 등)에서도 점 사이가 뻥 뚫려 보이지 않고 이어진 곡선처럼 보임.
 * 부분갱신(이전에 그렸던 구간만 지우고 새로 그림) 방식이라 화면 전체를
 * 지웠다 다시 그리는 깜빡임이 없다. 격자(grid.h)는 지워지면 같이 다시 그려준다.
 *
 * CH1/CH2가 동시에 파형을 그릴 수 있어야 해서 상태를 waveform_ch_t 하나에 담고,
 * 채널마다 인스턴스를 따로 들고 있는 식(재진입 가능)으로 만들었다. main.c에서
 * waveform_ch_t wf_ch1, wf_ch2; 이렇게 두 개 선언해서 각각 쓰면 됨.
 */
#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdint.h>
#include "grid.h"
#include "adc.h"

typedef struct {
    const scope_region_t *region;
    uint16_t prev_y0[ADC_BUFFER_SIZE]; // 컬럼별로 지난 프레임에 그렸던 선분의 위쪽 y
    uint16_t prev_y1[ADC_BUFFER_SIZE]; // 아래쪽 y
    uint8_t has_prev[ADC_BUFFER_SIZE];
    int32_t half_range_mv;
} waveform_ch_t;

void waveform_init(waveform_ch_t *wf);

// 그릴 영역을 바꾼다 (예: REGION_FULL <-> REGION_CH1_HALF). 바뀌면 그 영역을
// 격자로 새로 그리고 부분갱신 추적 상태를 리셋한다.
void waveform_set_region(waveform_ch_t *wf, const scope_region_t *region);

// ui.h의 VOLT_PER_DIV_MV[idx] 기준으로 세로 스케일(V/DIV)을 바꾼다.
void waveform_set_volt_div(waveform_ch_t *wf, uint8_t volt_div_idx);

// 파형을 그리는 도중(수십 개 SPI 전송) 몇 컬럼마다 한 번씩 불러주는 콜백 타입.
// 74HC165가 LCD와 SPI 버스를 공유해서 인터럽트로 안전하게 못 읽으니, 그 대신 그리기
// 루프 중간중간에 이걸 불러서 메인루프가 버튼/엔코더를 놓치지 않게 한다.
typedef void (*waveform_poll_fn)(void);

// adc_buf: 이 채널의 샘플 버퍼(adc_buffer_ch1/ch2). ch_idx: 0=CH1,1=CH2(calib.c 보정값 조회용).
// r,g,b: 트레이스 색(채널마다 다르게 줄 수 있음). poll: NULL이면 안 부름.
void waveform_draw(waveform_ch_t *wf, const volatile uint16_t *adc_buf, uint8_t ch_idx,
                    uint8_t r, uint8_t g, uint8_t b, waveform_poll_fn poll);

// 채널이 꺼졌을 때 등, 화면에 남아있는 파형 자취를 지우고 격자만 남긴다.
void waveform_clear(waveform_ch_t *wf);

#endif
