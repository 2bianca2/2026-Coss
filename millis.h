/*
 * millis.h - Timer0으로 1ms마다 증가하는 카운터. 롱프레스(홀드시간) 판정에 씀.
 */
#ifndef MILLIS_H
#define MILLIS_H

#include <stdint.h>

void millis_init(void);
uint32_t millis(void);

#endif
