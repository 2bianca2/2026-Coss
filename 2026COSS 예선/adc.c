#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "adc.h"
#include "ui.h"
#include "grid.h"
#include "calib.h"

#define ADC_MUX_CH1 0 // ADC0 = PF0
#define ADC_MUX_CH2 1 // ADC1 = PF1

// 핑퐁 버퍼 2쌍(A/B) - ISR은 쓰기용에만 쓰고, 프레임 완성 시 포인터만 스왑해서 읽기용으로 내놓음.
static volatile uint16_t capture_a_ch1[ADC_CAPTURE_SIZE];
static volatile uint16_t capture_b_ch1[ADC_CAPTURE_SIZE];
static volatile uint16_t capture_a_ch2[ADC_CAPTURE_SIZE];
static volatile uint16_t capture_b_ch2[ADC_CAPTURE_SIZE];

static volatile uint16_t *write_buf_ch1 = capture_a_ch1;
static volatile uint16_t *write_buf_ch2 = capture_a_ch2;
volatile uint16_t *adc_buffer_ch1 = capture_b_ch1;
volatile uint16_t *adc_buffer_ch2 = capture_b_ch2;

volatile uint8_t adc_frame_ready = 0;
static volatile uint8_t adc_index = 0;
static volatile uint8_t adc_active_ch = 0; // 0=CH1 변환 중, 1=CH2 변환 중
static uint16_t current_sample_rate_hz = 1000;
static volatile uint8_t adc_buf_lock_count = 0; // 중첩 카운터(재진입 호출 때문)

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
    DDRF &= ~((1 << PF0) | (1 << PF1));
    PORTF &= ~((1 << PF0) | (1 << PF1));

    ADMUX = (1 << REFS0) | ADC_MUX_CH1;

    // 프리스케일러 32(500kHz) - 데이터시트 권장(≤200kHz)보다 빠르지만 파형 갱신 속도 우선.
    ADCSRA = (1 << ADEN) | (1 << ADIE) |
             (1 << ADPS2) | (1 << ADPS0);
}

void sampling_timer_init(uint16_t sample_rate_hz)
{
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
        ocr = 0xFFFF;

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

    uint32_t time_per_div_us = pgm_read_dword(&TIME_PER_DIV_US[time_div_idx]);
    uint32_t rate = ((uint32_t)ADC_BUFFER_SIZE * 1000000UL) /
                     ((uint32_t)GRID_H_DIVS * time_per_div_us);
    if (rate < 1)
        rate = 1;
    // 실측 최소 변환+오버헤드 시간(~67~70us) 대비 여유를 두기 위한 클램프.
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
    // 기준선은 고정 0V가 아니라 (최댓값+최솟값)/2 - 신호가 한쪽으로 치우쳐도 동작.
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
            return i;
        }
        prev_mv = cur_mv;
    }
    adc_trigger_locked = 0;
    return ADC_TRIGGER_MARGIN;
}

ISR(TIMER1_COMPA_vect)
{
    ADMUX = (uint8_t)((ADMUX & ~0x1F) | ADC_MUX_CH1);
    adc_active_ch = 0;
    ADCSRA |= (1 << ADSC);
}

ISR(ADC_vect)
{
    uint8_t lo = ADCL; // ADLAR=0: ADCL 먼저 읽어야 함
    uint8_t hi = ADCH;
    uint16_t val = ((uint16_t)hi << 8) | lo;

    if (adc_active_ch == 0) {
        write_buf_ch1[adc_index] = val;
        ADMUX = (uint8_t)((ADMUX & ~0x1F) | ADC_MUX_CH2);
        _delay_us(10); // MUX 전환 후 안정화
        adc_active_ch = 1;
        ADCSRA |= (1 << ADSC);
    } else {
        write_buf_ch2[adc_index] = val;

        adc_index++;
        if (adc_index >= ADC_CAPTURE_SIZE) {
            adc_index = 0;

            if (adc_buf_lock_count == 0) {
                volatile uint16_t *tmp1 = adc_buffer_ch1;
                adc_buffer_ch1 = write_buf_ch1;
                write_buf_ch1 = tmp1;

                volatile uint16_t *tmp2 = adc_buffer_ch2;
                adc_buffer_ch2 = write_buf_ch2;
                write_buf_ch2 = tmp2;

                adc_frame_ready = 1;
            }
        }
    }
}
