#include <avr/io.h>
#include <avr/interrupt.h>
#include "millis.h"

static volatile uint32_t millis_count = 0;

void millis_init(void)
{
    // Timer0, CTC, 프리스케일러 64 -> 16MHz/64 = 250kHz -> 250카운트 = 1ms
    TCCR0 = (1 << WGM01) | (1 << CS01) | (1 << CS00);
    OCR0 = 249;
    TIMSK |= (1 << OCIE0);
}

ISR(TIMER0_COMP_vect)
{
    millis_count++;
}

uint32_t millis(void)
{
    uint8_t sreg = SREG;
    cli();
    uint32_t m = millis_count;
    SREG = sreg;
    return m;
}
