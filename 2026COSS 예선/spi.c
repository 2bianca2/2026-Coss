/*
 * spi.c - ATmega128 hardware SPI master driver
 * Mode 0 (CPOL=0, CPHA=0), F_CPU/4 clock.
 * Shared by hc165.c (read) and display.c, the ILI9488 LCD driver (write).
 */
#include <avr/io.h>
#include "spi.h"

#define SPI_DDR   DDRB
#define SPI_PORT  PORTB
#define SPI_SS    PB0
#define SPI_SCK   PB1
#define SPI_MOSI  PB2
#define SPI_MISO  PB3

void spi_init(void)
{
    // SS, SCK, MOSI as output; MISO as input.
    // SS must stay an output (and HIGH) in master mode, even if unused,
    // otherwise the AVR can auto-switch itself into SPI slave mode.
    SPI_DDR |= (1 << SPI_SS) | (1 << SPI_SCK) | (1 << SPI_MOSI);
    SPI_DDR &= ~(1 << SPI_MISO);
    SPI_PORT |= (1 << SPI_SS);

    // Enable SPI, Master mode, Mode 0, clock = F_CPU/4
    SPCR = (1 << SPE) | (1 << MSTR);
    SPSR = 0;
}

uint8_t spi_transfer(uint8_t data)
{
    SPDR = data;
    while (!(SPSR & (1 << SPIF)))
        ;
    return SPDR;
}
