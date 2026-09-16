/*
 * adc.h - ADC0(PF0,CH1)/ADC1(PF1,CH2) 2채널 샘플링 드라이버. MUX로 번갈아 변환.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_BUFFER_SIZE 96      // 화면 표시 샘플 수 (waveform/grid 컬럼 수와 1:1)
#define ADC_TRIGGER_MARGIN 128  // 트리거 탐색용 여유 캡처 샘플 수
#define ADC_CAPTURE_SIZE (ADC_BUFFER_SIZE + ADC_TRIGGER_MARGIN)
#define ADC_MAX_VALUE 1023

// 핑퐁(더블) 버퍼링: ISR은 쓰기용 버퍼에만 쓰고, 프레임 완성 시 포인터를 스왑해서
// 읽기용으로 내놓는다. 포인터이므로 매번 새로 참조할 것 - 캐싱하지 말 것.
extern volatile uint16_t *adc_buffer_ch1;
extern volatile uint16_t *adc_buffer_ch2;
extern volatile uint8_t adc_frame_ready;

// adc_buffer_ch1/ch2를 읽는 동안 반드시 감싸서 호출 - ISR이 그 버퍼를 스왑/덮어쓰지 않게 함.
void adc_read_lock(void);
void adc_read_unlock(void);

void adc_init(void);
void sampling_timer_init(uint16_t sample_rate_hz);
void adc_set_sample_rate(uint16_t sample_rate_hz);
void adc_set_time_div(uint8_t time_div_idx);
uint16_t adc_get_sample_rate(void);

int32_t adc_raw_to_mv(uint16_t raw);

// CH1 앞쪽 ADC_TRIGGER_MARGIN 구간에서 상승 크로싱을 찾아 시작 인덱스를 리턴.
// 못 찾으면 ADC_TRIGGER_MARGIN(가장 최근 구간) 폴백. CH1/CH2 둘 다 같은 오프셋을 씀.
uint16_t adc_find_trigger(const volatile uint16_t *capture_buf_ch1);

extern uint8_t adc_trigger_locked;

#endif
