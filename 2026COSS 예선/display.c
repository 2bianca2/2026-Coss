/*
 * display.c - ILI9488 구동 드라이버.
 */
#include <avr/io.h>
#include <util/delay.h>
#include "display.h"
#include "spi.h"

#define LCD_DDR   DDRA
#define LCD_PORT  PORTA
#define LCD_DC    PA0  // 51번
#define LCD_RST   PA1  // 50번
#define LCD_CS    PA2  // 49번

#define DC_CMD()   (LCD_PORT &= ~(1 << LCD_DC))
#define DC_DATA()  (LCD_PORT |=  (1 << LCD_DC))
#define CS_LOW()   (LCD_PORT &= ~(1 << LCD_CS))
#define CS_HIGH()  (LCD_PORT |=  (1 << LCD_CS))
#define RST_LOW()  (LCD_PORT &= ~(1 << LCD_RST))
#define RST_HIGH() (LCD_PORT |=  (1 << LCD_RST))

static void lcd_write_cmd(uint8_t cmd)
{
    CS_LOW();
    DC_CMD();
    spi_transfer(cmd);
    CS_HIGH();
}

static void lcd_write_data(uint8_t data)
{
    CS_LOW();
    DC_DATA();
    spi_transfer(data);
    CS_HIGH();
}

static void lcd_write_data_burst_start(void)
{
    CS_LOW();
    DC_DATA();
}

static void lcd_write_data_burst_end(void)
{
    CS_HIGH();
}

void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_write_cmd(0x2A); // Column Address Set
    lcd_write_data_burst_start();
    spi_transfer(x0 >> 8); spi_transfer(x0 & 0xFF);
    spi_transfer(x1 >> 8); spi_transfer(x1 & 0xFF);
    lcd_write_data_burst_end();

    lcd_write_cmd(0x2B); // Page Address Set
    lcd_write_data_burst_start();
    spi_transfer(y0 >> 8); spi_transfer(y0 & 0xFF);
    spi_transfer(y1 >> 8); spi_transfer(y1 & 0xFF);
    lcd_write_data_burst_end();

    lcd_write_cmd(0x2C); // Memory Write (이어서 픽셀 데이터가 옴)
}

void lcd_init(void)
{
    LCD_DDR |= (1 << LCD_DC) | (1 << LCD_RST) | (1 << LCD_CS);
    CS_HIGH();

    // 하드웨어 리셋
    RST_HIGH();
    _delay_ms(10);
    RST_LOW();
    _delay_ms(10);
    RST_HIGH();
    _delay_ms(120);

    lcd_write_cmd(0x01); // Software Reset
    _delay_ms(120);

    lcd_write_cmd(0x11); // Sleep Out
    _delay_ms(120);

    lcd_write_cmd(0x3A); // Pixel Format Set
    lcd_write_data(0x66); // 18bit/pixel

    lcd_write_cmd(0x36); // Memory Access Control - MV비트(0x20)로 90도 회전, BGR비트는 끔
    lcd_write_data(0x20);

    lcd_write_cmd(0x21); // Display Inversion ON

    lcd_write_cmd(0x29); // Display ON
    _delay_ms(20);
}

void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                    uint8_t r, uint8_t g, uint8_t b)
{
    lcd_set_window(x0, y0, x1, y1);

    uint32_t npixels = (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);

    lcd_write_data_burst_start();
    for (uint32_t i = 0; i < npixels; i++) {
        spi_transfer(r);
        spi_transfer(g);
        spi_transfer(b);
    }
    lcd_write_data_burst_end();
}

void lcd_fill_screen(uint8_t r, uint8_t g, uint8_t b)
{
    lcd_fill_rect(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, r, g, b);
}

void lcd_draw_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
    lcd_fill_rect(x, y, x, y, r, g, b);
}
