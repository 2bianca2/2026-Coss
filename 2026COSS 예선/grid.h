/*
 * grid.h - 오실로스코프 격자(그리드) + 화면 분할(전체화면/CH1-CH2 분할) 영역 정의.
 *   화면 위 STATUS_BAR_H(px)는 statusbar.c가 쓰고, 그 아래가 파형/격자 영역.
 *   그 아래를 다시 "region"으로 쪼개서 전체화면 1개 또는 위/아래 절반 2개로 씀.
 */
#ifndef GRID_H
#define GRID_H

#include <stdint.h>
#include "display.h" // LCD_WIDTH/LCD_HEIGHT (아래 매크로 계산에 필요)

#define STATUS_BAR_H 38   // 상태바 높이(px) - 3줄(전역상태 + CH1 + CH2, 각 7px 폰트 + 여백)
#define GRID_TOP     STATUS_BAR_H

#define GRID_H_DIVS      20  // 가로 칸 수
#define GRID_V_DIVS_FULL 12  // 전체화면일 때 세로 칸 수
#define GRID_V_DIVS_HALF 6   // 분할화면(반쪽)일 때 세로 칸 수 - 칸 픽셀 크기가 전체화면과 비슷하게 맞춤

typedef struct {
    uint16_t y0, y1; // 이 region이 차지하는 세로 픽셀 범위(양끝 포함)
    uint8_t v_divs;  // 이 region 안의 세로 칸 수
} scope_region_t;

// 미리 계산해둔 레이아웃들: CH1 하나만 켜지면 REGION_FULL,
// CH1+CH2 둘 다 켜지면 위쪽 REGION_CH1_HALF / 아래쪽 REGION_CH2_HALF로 분할.
extern const scope_region_t REGION_FULL;
extern const scope_region_t REGION_CH1_HALF;
extern const scope_region_t REGION_CH2_HALF;

// 분할 모드에서 CH1/CH2 사이에 넣는 빨간 구분띠 두께(px) - 잘 보이게 3px
#define DIVIDER_THICKNESS 3

// REGION_CH1_HALF.y1과 같은 값 (매크로라 REGION_CH1_HALF 정의와 DIVIDER_Y0/Y1 양쪽에서 씀)
#define REGION_CH1_HALF_Y1 (GRID_TOP + (LCD_HEIGHT - 1 - GRID_TOP - DIVIDER_THICKNESS) / 2 - 1)
#define DIVIDER_Y0 (REGION_CH1_HALF_Y1 + 1)          // 구분띠 시작 y좌표
#define DIVIDER_Y1 (DIVIDER_Y0 + DIVIDER_THICKNESS - 1) // 구분띠 끝 y좌표

void grid_draw_region(const scope_region_t *r);
// 파형을 지운 자리(x0..x1, y0..y1)에 그 구간과 겹치는 격자선만 다시 그려서
// "지우기"가 격자까지 함께 지워버리지 않게 한다. r은 그 자리가 속한 region.
void grid_redraw_rect(const scope_region_t *r, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
// 분할 모드에서 CH1/CH2 region 사이 구분선
void grid_draw_divider(void);

#endif
