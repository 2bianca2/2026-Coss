/*
 * calib.h - CH1/CH2 2점(GND, +5V) 캘리브레이션. EEPROM에 저장해서 전원 꺼도 유지.
 *
 * 사용법(main.c에서):
 *   calib_init()              -> 부팅 시 1번, EEPROM에서 로드(없으면 미보정 상태)
 *   calib_start(ch_idx)       -> 롱프레스로 진입
 *   calib_confirm(adc_buf)    -> 그 채널 버튼 짧게 누를 때마다: GND확정 -> REF확정+저장 -> 화면닫기
 *   calib_cancel()            -> SW1로 취소
 *   calib_raw_to_mv(ch_idx,raw) -> waveform.c/statusbar.c가 이걸로 mV 환산 (미보정이면 이상적 계산식 사용)
 */
#ifndef CALIB_H
#define CALIB_H

#include <stdint.h>

typedef enum { CAL_IDLE, CAL_WAIT_GND, CAL_WAIT_REF, CAL_DONE } calib_state_t;

void calib_init(void);

// raw(0~1023) -> mV. 채널별 보정값이 있으면 그걸로, 없으면 adc_raw_to_mv()의 이상적 계산식.
int32_t calib_raw_to_mv(uint8_t ch_idx, uint16_t raw);

void calib_start(uint8_t ch_idx); // 0=CH1, 1=CH2
uint8_t calib_active(void);
uint8_t calib_active_channel(void);
calib_state_t calib_get_state(void);

// 캘리브레이션 중일 때 해당 채널 버튼을 짧게 눌렀을 때 호출 - 단계 진행/저장/화면닫기
void calib_confirm(const volatile uint16_t *adc_buf);
void calib_cancel(void);

#endif
