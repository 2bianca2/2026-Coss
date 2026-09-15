/*
 * statusbar.h - 화면 맨 위 STATUS_BAR_H(px) 영역, 3줄 구성:
 *   1줄: RUN/STOP, CH1/CH2 on-off, (공유)T/DIV
 *   2줄: CH1 - V/DIV 또는 measure 메뉴+값
 *   3줄: CH2 - V/DIV 또는 measure 메뉴+값 (CH2 꺼져있으면 비워둠)
 */
#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <stdint.h>
#include "ui.h"

// adc_buf1/adc_buf2: 지금 화면에 그리고 있는 채널별 64샘플 버퍼(트리거 오프셋 반영된 포인터).
// measure 값(RMS/MAX/MIN/FREQ/PERIOD)을 이 값으로 계산해서 그래프랑 항상 일치시킨다.
void statusbar_draw_full(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const volatile uint16_t *adc_buf2);

// measure 모드일 때 CH1/CH2 값(RMS/MAX/MIN)만 가볍게 다시 그림 (ADC 새 프레임마다 호출)
void statusbar_update_measure_values(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const volatile uint16_t *adc_buf2);

// 1줄(전역)에 트리거 상태를 항상 표시: ui->trigger_enabled가 꺼져있으면 "T:OFF",
// 켜져있으면 adc_trigger_locked(adc.h)를 읽어서 TRIG(잠김)/SEARCH(폴백중).
// 새 프레임마다, 그리고 SW1 롱프레스로 토글될 때 가볍게 호출.
void statusbar_update_trigger_status(const ui_state_t *ui);

#endif
