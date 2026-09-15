#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "adc.h"
#include "ui.h"    // TIME_PER_DIV_US[], TIME_DIV_TABLE_SIZE
#include "grid.h"  // GRID_H_DIVS (가로 칸 수)
#include "calib.h" // calib_raw_to_mv() - 트리거 탐색에서 씀

#define ADC_MUX_CH1 0 // ADC0 = PF0
#define ADC_MUX_CH2 1 // ADC1 = PF1

// 핑퐁 버퍼 2쌍(A/B) - ISR은 "쓰기용"에만 쓰고, 프레임 완성 시 포인터만 바꿔치기해서
// "읽기용"으로 내놓는다 (adc.h 주석 참고 - 레이스 컨디션 방지용)
static volatile uint16_t capture_a_ch1[ADC_CAPTURE_SIZE];
static volatile uint16_t capture_b_ch1[ADC_CAPTURE_SIZE];
static volatile uint16_t capture_a_ch2[ADC_CAPTURE_SIZE];
static volatile uint16_t capture_b_ch2[ADC_CAPTURE_SIZE];

static volatile uint16_t *write_buf_ch1 = capture_a_ch1; // ISR이 지금 채우는 중
static volatile uint16_t *write_buf_ch2 = capture_a_ch2;
volatile uint16_t *adc_buffer_ch1 = capture_b_ch1; // 메인루프가 읽는 "완성된" 버퍼
volatile uint16_t *adc_buffer_ch2 = capture_b_ch2;

volatile uint8_t adc_frame_ready = 0;
static volatile uint8_t adc_index = 0;
static volatile uint8_t adc_active_ch = 0; // 0=지금 CH1 변환 중, 1=CH2 변환 중
static uint16_t current_sample_rate_hz = 1000; // FREQ/PERIOD 계산(제로크로싱)에 필요
// 0보다 크면 메인루프가 읽는 중 - ISR은 스왑을 건너뜀. poll_input()이 최상위 루프에서도
// 불리고 waveform_draw() 콜백으로 중첩되어서도 불려서(그때는 바깥쪽이 이미 락 건 상태),
// 단순 0/1 플래그면 안쪽 unlock이 바깥쪽 락을 너무 일찍 풀어버림 - 그래서 중첩 카운터로 둠.
static volatile uint8_t adc_buf_lock_count = 0;

void adc_read_lock(void)
{
    adc_buf_lock_count++;
}

void adc_read_unlock(void)
{
    if (adc_buf_lock_count > 0)
        adc_buf_lock_count--;
}

void adc_init(void)
{
    // PF0(CH1), PF1(CH2) 입력, 내부 풀업 꺼짐
    DDRF &= ~((1 << PF0) | (1 << PF1));
    PORTF &= ~((1 << PF0) | (1 << PF1));

    // 기준전압 AVCC, 우측정렬(ADLAR=0 -> 10bit 그대로), CH1(ADC0)부터 시작
    ADMUX = (1 << REFS0) | ADC_MUX_CH1;

    // ADC 활성화 + 변환완료 인터럽트 + 프리스케일러 32(16MHz/32=500kHz).
    // 데이터시트 권장(≤200kHz)보다 빠르지만, 10bit 정밀 측정보다 파형 모양(1kHz 근처
    // RC 충방전 등)을 빠르게 그리는 게 더 중요해서 속도 쪽으로 택함 - 변환 1회 약 26us,
    // CH1+CH2 한 쌍 약 52us -> 최대 약 19kHz까지 샘플링 가능(이전 128프리스케일러 대비 4배).
    ADCSRA = (1 << ADEN) | (1 << ADIE) |
             (1 << ADPS2) | (1 << ADPS0);
}

void sampling_timer_init(uint16_t sample_rate_hz)
{
    // Timer1, CTC 모드, 프리스케일러 64
    TCCR1A = 0;
    TCCR1B = (1 << WGM12) | (1 << CS11) | (1 << CS10);
    TIMSK |= (1 << OCIE1A);
    adc_set_sample_rate(sample_rate_hz);
}

void adc_set_sample_rate(uint16_t sample_rate_hz)
{
    uint32_t ocr = (F_CPU / 64UL / sample_rate_hz);
    ocr = (ocr > 0) ? ocr - 1 : 0;
    if (ocr > 0xFFFF)
        ocr = 0xFFFF; // 너무 느린 레이트를 요청하면 Timer1(16bit)이 못 담으니 최대치로 클램프

    uint8_t sreg = SREG;
    cli();
    OCR1A = (uint16_t)ocr;
    TCNT1 = 0;
    adc_index = 0;
    adc_active_ch = 0;
    ADMUX = (uint8_t)((ADMUX & ~0x1F) | ADC_MUX_CH1);
    adc_frame_ready = 0;
    current_sample_rate_hz = sample_rate_hz;
    SREG = sreg;
}

uint16_t adc_get_sample_rate(void)
{
    return current_sample_rate_hz;
}

void adc_set_time_div(uint8_t time_div_idx)
{
    if (time_div_idx >= TIME_DIV_TABLE_SIZE)
        time_div_idx = TIME_DIV_TABLE_SIZE - 1;

    // 가로 화면 전체(GRID_H_DIVS칸)가 ADC_BUFFER_SIZE개 샘플이 되게 하는 레이트.
    // 즉 "한 칸 = TIME_PER_DIV_US[idx] us"가 되도록 역산. (CH1/CH2 채널당 레이트 - 반토막 안 남)
    // TIME_PER_DIV_US는 PROGMEM(ui.c)이라 pgm_read_dword로 읽어야 함.
    uint32_t time_per_div_us = pgm_read_dword(&TIME_PER_DIV_US[time_div_idx]);
    uint32_t rate = ((uint32_t)ADC_BUFFER_SIZE * 1000000UL) /
                     ((uint32_t)GRID_H_DIVS * time_per_div_us);
    if (rate < 1)
        rate = 1;
    // CH1+CH2 한 쌍 변환에 최소 52us(26us×2, 프리스케일러32) + MUX 전환 후 안정화 딜레이
    // 10us(ISR 안 _delay_us, 크로스토크 방지용) + 인터럽트 오버헤드까지 더하면 실제로는
    // ~67~70us 근처 - 19000Hz(52.6us)는 그 턱밑이라 타이머가 다음 변환을 걸 때 이전
    // 변환이 아직 안 끝나서 샘플이 꼬이는 문제가 있었음(200us/div에서 화면이 깨짐).
    // 12000Hz(83.3us)로 낮춰서 실제 여유를 둠.
    if (rate > 12000UL)
        rate = 12000UL;

    adc_set_sample_rate((uint16_t)rate);
}

int32_t adc_raw_to_mv(uint16_t raw)
{
    return (int32_t)raw * 10000L / 1023L - 5000L;
}

uint8_t adc_trigger_locked = 0;

uint16_t adc_find_trigger(const volatile uint16_t *capture_buf_ch1)
{
    // 기준선은 고정 0V가 아니라 이 마진 구간의 (최댓값+최솟값)/2로 매번 다시 잡는다.
    // RC 충방전 곡선처럼 신호가 한쪽으로 치우쳐 0V를 안 지나가면 고정 0V 기준으론
    // 크로싱을 영영 못 찾아서 매번 폴백(가장 최근 구간)으로 떨어지고, 그러면 신호
    // 위상이랑 상관없이 캡처 경계에 걸리는 대로 잘려서 화면에서 파형이 계속 흘러가는
    // 것처럼 보임 - statusbar.c의 find_period_us()와 같은 이유, 같은 해법.
    int32_t mx = calib_raw_to_mv(0, capture_buf_ch1[0]);
    int32_t mn = mx;
    for (uint16_t i = 1; i <= ADC_TRIGGER_MARGIN; i++) {
        int32_t mv = calib_raw_to_mv(0, capture_buf_ch1[i]);
        if (mv > mx) mx = mv;
        if (mv < mn) mn = mv;
    }
    int32_t mid = (mx + mn) / 2;

    int32_t prev_mv = calib_raw_to_mv(0, capture_buf_ch1[0]) - mid;

    for (uint16_t i = 1; i <= ADC_TRIGGER_MARGIN; i++) {
        int32_t cur_mv = calib_raw_to_mv(0, capture_buf_ch1[i]) - mid;
        if (prev_mv < 0 && cur_mv >= 0) {
            adc_trigger_locked = 1;
            return i; // 상승 크로싱 발견 - 여기부터 ADC_BUFFER_SIZE개를 보여주면 됨
        }
        prev_mv = cur_mv;
    }
    adc_trigger_locked = 0;
    return ADC_TRIGGER_MARGIN; // 못 찾음(완전 평탄한 DC 등) - 가장 최근 ADC_BUFFER_SIZE개(폴백)
}

// 한 쌍(CH1+CH2)의 시작 - CH1부터 변환
ISR(TIMER1_COMPA_vect)
{
    ADMUX = (uint8_t)((ADMUX & ~0x1F) | ADC_MUX_CH1);
    adc_active_ch = 0;
    ADCSRA |= (1 << ADSC);
}

ISR(ADC_vect)
{
    uint8_t lo = ADCL; // ADLAR=0일 땐 반드시 ADCL을 먼저 읽어야 함(데이터시트 요구사항)
    uint8_t hi = ADCH;
    uint16_t val = ((uint16_t)hi << 8) | lo;

    if (adc_active_ch == 0) {
        write_buf_ch1[adc_index] = val;
        // CH1 변환이 끝나자마자 바로 CH2로 전환해서 이어서 변환 (같은 타이머 주기 안에서 둘 다 채움).
        // MUX 전환 직후 살짝 기다려서 ADC 내부 샘플홀드 커패시터가 새 채널 전압으로 안정될
        // 시간을 줌(10us - 500kHz ADC클럭 기준 5클럭 정도, 82.3us 예산 안에서 충분히 여유있음).
        // (참고: CH2가 CH1이랑 똑같이 나왔던 이전 버그는 이거랑 무관하게 불량 칩(ADC1 채널
        // 자체 고장)이 원인이었음 - 칩 교체로 해결됨. 이 딜레이는 일반적인 안전장치로 유지.)
        ADMUX = (uint8_t)((ADMUX & ~0x1F) | ADC_MUX_CH2);
        _delay_us(10);
        adc_active_ch = 1;
        ADCSRA |= (1 << ADSC);
    } else {
        write_buf_ch2[adc_index] = val;

        adc_index++;
        if (adc_index >= ADC_CAPTURE_SIZE) {
            adc_index = 0;

            if (adc_buf_lock_count == 0) {
                // 핑퐁 스왑: 방금 다 채운 버퍼를 "읽기용"으로 내놓고, 이전 읽기용 버퍼를
                // 다음 쓰기 대상으로 돌린다 - 데이터 복사 없이 포인터만 바꿔치기.
                volatile uint16_t *tmp1 = adc_buffer_ch1;
                adc_buffer_ch1 = write_buf_ch1;
                write_buf_ch1 = tmp1;

                volatile uint16_t *tmp2 = adc_buffer_ch2;
                adc_buffer_ch2 = write_buf_ch2;
                write_buf_ch2 = tmp2;

                adc_frame_ready = 1;
            }
            // 락 걸려있으면: 스왑/frame_ready 둘 다 스킵하고 write_buf를 처음부터 다시
            // 채움 - 메인루프가 지금 읽고 있는 adc_buffer_ch1/ch2를 절대 건드리지 않음.
        }
    }
}
