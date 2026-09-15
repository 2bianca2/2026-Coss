/*
 * ui.h - 스위치/엔코더로 조작하는 UI 상태머신.
 *
 *   SW1(Run/Stop)  -> run_stopped 토글
 *   SW2(measure)   -> measure_mode 토글
 *   SW3(CH1)       -> ch1_enabled 토글
 *   SW4(CH2)       -> ch2_enabled 토글
 *
 *   CH1 엔코더 푸시 -> (measure_mode 아닐 때) ch1_axis Vertical/Horizontal 전환
 *                    (measure_mode 일 때)    CH1의 현재 메뉴 항목을 measure_selected[0]로 확정
 *   CH1 CLK/DT     -> (measure_mode 아닐 때) ch1_axis가 Vertical이면 volt_div_idx[0],
 *                                            Horizontal이면 공유 time_div_idx(T/DIV) 조절
 *                    (measure_mode 일 때)    CH1의 measure_menu_idx[0] 위아래로 이동
 *
 *   CH2 엔코더 푸시 -> (measure_mode 아닐 때) 없음
 *                    (measure_mode 일 때)    CH2의 현재 메뉴 항목을 measure_selected[1]로 확정
 *   CH2 CLK/DT     -> (measure_mode 아닐 때) volt_div_idx[1](CH2 V/DIV)만 조절 - CH2는
 *                                            "축" 개념이 없음. T/DIV는 ADC가 1개뿐이라
 *                                            CH1/CH2가 항상 같은 샘플링 레이트를 공유하기
 *                                            때문에 CH2 엔코더로 따로 조절할 게 없음
 *                    (measure_mode 일 때)    CH2의 measure_menu_idx[1] 위아래로 이동
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <avr/pgmspace.h>

#define MEASURE_ITEM_COUNT 6   // RMS, MAX, MIN, FFT, FREQ, PERIOD
#define MEASURE_FFT_IDX 3      // MEASURE_NAMES(ui.c)에서 "FFT"의 인덱스 - 채널 하나만 켜진
                                // 전체화면일 때만 실제 스펙트럼 그래프로 바뀜(app.c 참고)
#define VOLT_DIV_TABLE_SIZE 10 // volt_div_idx가 가리키는 표의 칸 수 (statusbar.c 표와 짝)
#define TIME_DIV_TABLE_SIZE 12 // time_div_idx가 가리키는 표의 칸 수 - V/DIV보다 넓은 범위(us~s)가 필요해서 따로 둠

// volt_div_idx / time_div_idx 각 칸이 실제로 뜻하는 값.
// statusbar.c(표시 문자열), waveform.c(세로 스케일), adc.c(샘플링레이트/가로 시간축)가
// 전부 이 순서를 그대로 따라간다 - 하나만 바꾸면 다 같이 틀어지니 인덱스 순서 유지 필수.
// PROGMEM(플래시)에 있음 - 읽을 땐 pgm_read_word()/pgm_read_dword() 써야 함(SRAM 절약).
extern const uint16_t VOLT_PER_DIV_MV[VOLT_DIV_TABLE_SIZE] PROGMEM;
extern const uint32_t TIME_PER_DIV_US[TIME_DIV_TABLE_SIZE] PROGMEM; // us 단위(200us~1s까지 표현하려고 ms 대신 us로 통일)

typedef enum { AXIS_VERTICAL, AXIS_HORIZONTAL } axis_mode_t;

typedef struct {
    uint8_t run_stopped;
    uint8_t ch1_enabled;
    uint8_t ch2_enabled;
    uint8_t measure_mode;

    axis_mode_t ch1_axis;    // CH1 엔코더가 V/DIV를 조절할지 (공유)T/DIV를 조절할지
    int8_t volt_div_idx[2];  // [0]=CH1, [1]=CH2 - 완전 독립
    int8_t time_div_idx;     // ADC 1개를 CH1/CH2가 나눠 쓰므로 샘플링레이트는 공유 - CH1 엔코더로만 조절

    uint8_t measure_menu_idx[2]; // [0]=CH1, [1]=CH2 - 각자 커서
    uint8_t measure_selected[2]; // [0]=CH1, [1]=CH2 - 각자 확정된 항목

    // SW1 롱프레스로 토글. 켜져있으면(기본값) 자동 트리거(제로크로싱 탐색+실패시 폴백)가
    // 동작해서 화면이 고정돼 보이고, 끄면 트리거 없이 매 프레임 최신 구간을 그대로 보여줘서
    // 화면이 흘러다닌다. 상태바에 OFF/TRIG/SEARCH로 항상 표시됨.
    uint8_t trigger_enabled;
} ui_state_t;

void ui_init(ui_state_t *ui);
void ui_update(ui_state_t *ui, uint16_t pressed, int8_t ch1_delta, int8_t ch2_delta);
const char *ui_measure_name(uint8_t idx);

#endif
