/*
 * text.h - 5x7 비트맵 폰트로 LCD에 문자/문자열 그리기.
 */
#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

void lcd_draw_char(uint16_t x, uint16_t y, char c, uint8_t r, uint8_t g, uint8_t b);
void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint8_t r, uint8_t g, uint8_t b);

// lcd_draw_string()의 PROGMEM(플래시) 버전 - str_p가 SRAM이 아니라 플래시에 있는 문자열일
// 때 씀. 호출부에서 PSTR("문자열")로 감싸서 넘기면 그 문자열 자체가 SRAM에 안 실리고
// 플래시에만 있어서 SRAM을 절약함(AVR은 const만으론 플래시에 안 감 - PROGMEM 필수).
void lcd_draw_string_P(uint16_t x, uint16_t y, const char *str_p, uint8_t r, uint8_t g, uint8_t b);

#endif
