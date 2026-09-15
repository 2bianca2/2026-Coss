/*
 * hc165.c - Read two cascaded 74HC165 shift registers (U4, U5) over SPI.
 */
#include <avr/io.h>
#include <util/delay.h>
#include "hc165.h"
#include "spi.h"

void hc165_init(void)
{
    HC165_PL_DDR |= (1 << HC165_PL_PIN);
    HC165_PL_PORT |= (1 << HC165_PL_PIN); // idle HIGH; PL latches on the LOW pulse
}

uint16_t hc165_read(void)
{
    // Pulse PL low->high to latch D0..D7 of BOTH chips into their shift registers
    HC165_PL_PORT &= ~(1 << HC165_PL_PIN);
    _delay_us(5);
    HC165_PL_PORT |= (1 << HC165_PL_PIN);
    _delay_us(5);

    // MOSI content is irrelevant (165 has no serial data input besides the
    // cascade line DS, which is wired chip-to-chip on the board, not to the MCU).
    // Each spi_transfer() just generates 8 clocks and captures 8 bits on MISO.
    uint8_t byte_u5 = spi_transfer(0x00); // first out: U5 ("출력단"), D7..D0
    uint8_t byte_u4 = spi_transfer(0x00); // second out: U4, D7..D0

    return ((uint16_t)byte_u5 << 8) | byte_u4;
}
