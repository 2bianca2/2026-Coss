/*
 * Coshow_practice.c - 엔트리 포인트. 하드웨어 초기화 순서만 정하고, 실제 UI/그리기
 * 로직은 전부 app.c(app_init/app_run_frame)에 있다 - 전체 설계는 app.c 상단 주석 참고.
 *
 * Created: 2026-09-14
 * Author : LG
 */

#include <avr/interrupt.h>

#include "spi.h"
#include "adc.h"
#include "display.h"
#include "hc165.h"
#include "millis.h"
#include "calib.h"
#include "app.h"

int main(void)
{
    spi_init();
    hc165_init();
    lcd_init();
    adc_init();
    millis_init();
    calib_init();

    app_init();

    sei();

    while (1) {
        app_run_frame();
    }
}
