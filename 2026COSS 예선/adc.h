/*
 * adc.h - ADC0(PF0,CH1)/ADC1(PF1,CH2) 2채널 샘플링 드라이버.
 *   ADC 하드웨어는 1개뿐이라 MUX를 번갈아가며 두 채널을 순서대로 변환한다.
 *   Timer1(CTC) COMPA 인터럽트가 "한 쌍(CH1+CH2)"의 시작을 알리면 CH1을 먼저
 *   변환하고, ADC_vect 안에서 곧바로 CH2 변환을 이어서 시작한다 - 그래서
 *   Timer1 주기 = "채널당" 샘플 주기가 되고(반토막 안 남), 두 채널은 ADC 변환
 *   한 번(~수십us) 정도의 시간차만 두고 거의 같은 순간을 잰다.
 *   10bit(ADLAR=0, ADCL 먼저 + ADCH, 0~1023) 우측정렬 모드로 고정.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#define ADC_BUFFER_SIZE 96     // 화면에 보여주는 샘플 수 (waveform/grid의 컬럼 수와 1:1)
                                // FFT(fft.h, FFT_N=64)는 2의 거듭제곱이 필요해서 독립적으로
                                // 유지 - 96개 중 앞 64개만 씀.
#define ADC_TRIGGER_MARGIN 128 // 트리거(제로크로싱) 찾을 여유분으로 앞에 더 캡처해두는 샘플 수
#define ADC_CAPTURE_SIZE (ADC_BUFFER_SIZE + ADC_TRIGGER_MARGIN) // 실제 raw 캡처 버퍼 크기(224)
#define ADC_MAX_VALUE 1023 // 10bit 최댓값

// 핑퐁(더블) 버퍼링: ISR은 항상 "쓰기용" 버퍼에만 쓰고, 프레임이 다 차면 방금
// 완성된 버퍼를 "읽기용"으로 내놓는다(포인터만 바꿔치기, 복사 없음) - 메인루프가
// 느긋하게 처리하는 동안 ISR이 그 자리를 새 데이터로 덮어써버리는 레이스를 막기 위함
// (빠른 T/DIV에서 버퍼가 메인루프의 처리 주기보다 빨리 다 차버려서 화면이 깨지는 문제였음).
// 그래서 adc_buffer_ch1/ch2는 "값"이 아니라 "포인터"이고, 매번 최신 값을 다시 읽어야 한다
// (한 번 읽은 포인터를 오래 들고 있지 말고, 프레임 처리할 때마다 새로 참조할 것).
extern volatile uint16_t *adc_buffer_ch1;
extern volatile uint16_t *adc_buffer_ch2;
extern volatile uint8_t adc_frame_ready; // CH1/CH2 버퍼가 같이 다 찼을 때 1

// adc_buffer_ch1/ch2를 실제로 읽는 동안(트리거 탐색~그리기~measure 계산 끝날 때까지)
// 반드시 감싸서 호출할 것. 그리기는 30ms 넘게 걸릴 수 있는데, 캡처는 그보다 훨씬
// 빨리(224샘플 @ 12kHz면 ~19ms) 한 바퀴 도니까, 락 없이는 읽는 도중에 ISR이 버퍼를
// 다시 스왑해서 지금 읽고 있는 바로 그 메모리에 새 샘플을 덮어쓸 수 있음(핑퐁 2개로는
// 이 경우를 못 막음 - 3번째 스왑이 오기 전에 다 읽어야 하는데 못 그럴 때가 있어서).
// 락이 걸려있으면 ISR은 스왑도, adc_frame_ready=1도 안 하고 그냥 같은 write_buf를
// 계속 덮어써서 프레임 하나를 버린다(깨진 데이터를 보여주는 것보단 나음).
void adc_read_lock(void);
void adc_read_unlock(void);

void adc_init(void);
void sampling_timer_init(uint16_t sample_rate_hz);

// 샘플링 레이트를 실행 중에 바꿈 (버퍼/프레임 상태 리셋 포함). CH1/CH2 공유.
void adc_set_sample_rate(uint16_t sample_rate_hz);
// ui.h의 TIME_PER_DIV_US[idx] 기준으로 "가로 한 칸 = 그 시간"이 되도록 샘플링 레이트를 맞춤
void adc_set_time_div(uint8_t time_div_idx);
// 현재 채널당 샘플링레이트(Hz) - FREQ/PERIOD 계산(statusbar.c)에서 씀
uint16_t adc_get_sample_rate(void);

// raw(0~1023) -> mV. 프론트엔드가 -5V~+5V 입력을 0~5V(2.5V=0V 기준)로 분배했다는 전제.
// CH1/CH2 프론트엔드가 동일한 분배회로라는 전제로 두 채널 다 이 식 하나를 씀.
int32_t adc_raw_to_mv(uint16_t raw);

// CH1(capture_buf) 앞쪽 ADC_TRIGGER_MARGIN 구간에서 상승 크로싱을 찾아
// "여기서부터 ADC_BUFFER_SIZE개를 보여주면 됨"하는 시작 인덱스를 리턴한다.
// 못 찾으면(신호 없음/DC 등) ADC_TRIGGER_MARGIN을 리턴 - 가장 최근 구간을 보여주는 폴백.
// CH1 기준 하나만 찾아서 CH1/CH2 둘 다 같은 오프셋을 쓴다(두 채널 타이밍이 어긋나지 않게).
uint16_t adc_find_trigger(const volatile uint16_t *capture_buf_ch1);

// 마지막 adc_find_trigger() 호출에서 실제로 크로싱을 찾았으면(트리거 잠김) 1,
// 못 찾아서 폴백을 썼으면 0. 상태바에 TRIG/SEARCH 표시하는 용도(SW1 롱프레스로 토글).
extern uint8_t adc_trigger_locked;

#endif
