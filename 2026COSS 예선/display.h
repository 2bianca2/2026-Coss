/*
 * display.h - ILI9488 (480x320, 18bit/SPI) 구동 드라이버.
 *
 * 배선:
 *   SCK  = PB1 (레벨시프터 경유, 하드웨어 SPI)
 *   MOSI = PB2 (레벨시프터 경유, 하드웨어 SPI)
 *   DC   = PA0 (51번 핀, 레벨시프터 경유)
 *   RST  = PA1 (50번 핀, 레벨시프터 경유)
 *   CS   = PA2 (49번 핀, 레벨시프터 경유)
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define LCD_WIDTH   480
#define LCD_HEIGHT  320

void lcd_init(void);
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_fill_screen(uint8_t r, uint8_t g, uint8_t b);
void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                    uint8_t r, uint8_t g, uint8_t b);
void lcd_draw_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b);

// 색상 상수 (r,g,b 세 값으로 확장됨 - lcd_fill_rect(...,COLOR_GREEN) 처럼 사용)
#define COLOR_BLACK   0,   0,   0
#define COLOR_WHITE   255, 255, 255
#define COLOR_GREEN   0,   255, 0     // 파형 트레이스
#define COLOR_YELLOW  255, 255, 0     // 강조/선택 중인 항목
#define COLOR_CYAN    0,   255, 255   // CH2 / 확정된 항목
#define COLOR_RED     255, 0,   0     // STOP 상태
#define COLOR_NAVY    40,  40,  40    // 배경(상태바+화면 전체)
#define COLOR_GRID    132, 130, 132   // 격자
#define COLOR_LTGREY  197, 194, 197   // 테두리/중앙 기준선
#define COLOR_DKGREY  70,  70,  70    // 비활성 텍스트

#endif
