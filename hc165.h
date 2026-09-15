/*
 * hc165.h - Read two cascaded 74HC165 shift registers (U4, U5) over SPI.
 *
 * Wiring:
 *   CP (clock, both chips, shared)   -> SPI SCK  (PB1, via spi.c)
 *   Q7 of U5 ("출력단", last in chain) -> SPI MISO (PB3, via spi.c)
 *   U4.Q7 -> U5.DS  (cascade link, on the board, not an MCU pin)
 *   PL (parallel load, both chips, shared) -> PD0
 *
 * hc165_read() returns 16 bits: bits[15:8] = U5, bits[7:0] = U4
 * (U5 is read out first because it is last in the chain / closest to MISO).
 */
#ifndef HC165_H
#define HC165_H

#include <stdint.h>

#define HC165_PL_DDR   DDRD
#define HC165_PL_PORT  PORTD
#define HC165_PL_PIN   PD0

void hc165_init(void);
uint16_t hc165_read(void);

// ---- Bit map of the combined 16-bit value returned by hc165_read() ----
// U5 side (bits 15..8)
#define HC165_SW4        (1 << 8)   // Switch4 (CH_2 선택)
#define HC165_SW1        (1 << 9)   // Switch1 (Run/Stop)
#define HC165_SW2        (1 << 10)  // Switch2 (measure)
#define HC165_SW3        (1 << 11)  // Switch3 (CH_1 선택)
#define HC165_CH1_SW     (1 << 12)  // RotaryEncoder1 누름버튼
#define HC165_CH2_SW     (1 << 13)  // RotaryEncoder2 누름버튼
// U4 side (bits 7..0)
#define HC165_CH1_CLK    (1 << 0)   // RotaryEncoder1 CLK
#define HC165_CH2_CLK    (1 << 1)   // RotaryEncoder2 CLK
#define HC165_CH1_DT     (1 << 2)   // RotaryEncoder1 DT
#define HC165_CH2_DT     (1 << 3)   // RotaryEncoder2 DT

#endif
