/*
 * spi.h - ATmega128 hardware SPI master driver
 */
#ifndef SPI_H
#define SPI_H

#include <stdint.h>

// SPI pins (PORTB, fixed by hardware): SS=PB0, SCK=PB1, MOSI=PB2, MISO=PB3
void spi_init(void);
uint8_t spi_transfer(uint8_t data);

#endif
