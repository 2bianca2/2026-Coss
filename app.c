/*
 * app.c - 애플리케이션 로직: ADC0(PF0,CH1)/ADC1(PF1,CH2) 10bit 파형을 LCD에 그리고,
 * 74HC165(버튼/로터리엔코더 2개)로 RUN/STOP, CH1/CH2 on-off, measure 모드, V-DIV/T-DIV
 * 조절, measure 메뉴 선택까지 처리하는 전체 UI 루프.
 *
 *   - V/DIV(전압스케일)는 CH1/CH2가 완전히 독립 (waveform_ch_t 두 개, 각자 세로 스케일)
 *   - T/DIV(시간축)는 ADC가 1개뿐이라 CH1/CH2가 공유 (adc.c가 두 채널을 번갈아 샘플링)
 *   - measure(RMS/MAX/MIN/FFT/FREQ/PERIOD)도 CH1/CH2 각자 커서/선택값을 따로 가짐
 *
 *   - SW3/SW4를 짧게 누르면 원래대로 CH1/CH2 on-off.
 *     1.5초 이상 누르고 있으면(롱프레스) 그 채널 캘리브레이션(calib.c) 모드로 진입.
 *     캘리브레이션 중엔 같은 버튼(SW3=CH1,SW4=CH2)으로 단계 확정, SW1로 취소.
 *
 * 74HC165(버튼/엔코더)는 LCD와 SPI 버스(SCK/MISO)를 공유하고 있어서 인터럽트로
 * 안전하게 읽을 수 없다(LCD 전송 중간에 끼어들면 둘 다 깨짐). 대신 poll_input()을
 * waveform_draw() 안에서 몇 컬럼마다 한 번씩 콜백으로 불러서, 파형을 그리는 긴
 * 시간 동안에도 버튼/엔코더를 계속 폴링할 수 있게 했다 - 단, poll_input()은
 * ui_state_t와 상태바 텍스트만 건드리고, apply_layout/waveform_set_*처럼
 * 그리기 중인 waveform_ch_t 자체를 바꾸는 건 ui_dirty 플래그로 미뤄뒀다가
 * 이번 프레임을 다 그린 뒤 apply_ui_changes()에서 한 번에 반영한다
 * (그리는 도중에 그리는 대상 자체가 바뀌면 위험하기 때문).
 *
 * UART는 뺐다 - printf/uart_putc가 blocking이라 한 줄 찍는 동안 hc165_read()가
 * 멈춰서 버튼/엔코더 입력을 놓치는 문제가 있었음.
 *
 * 화면 레이아웃: CH1만 켜지면 전체화면, CH2만 켜지면 전체화면, CH1+CH2 둘 다 켜지면
 * 위(CH1)/아래(CH2) 분할(가운데 빨간 구분띠) - 두 채널 다 실제 트레이스를 그린다.
 *
 * 자동 트리거: adc.c가 화면표시분(ADC_BUFFER_SIZE개) 앞에 여유분(ADC_TRIGGER_MARGIN개)을
 * 더 캡처해두고, 매 프레임 CH1 기준으로 그 여유분에서 상승 크로싱을 찾아 거기서부터
 * 보여준다(adc_find_trigger). 못 찾으면 가장 최근 구간으로 조용히 폴백한다 - RUN/STOP과는
 * 완전히 별개 레이어. SW1 롱프레스로 켜고 끌 수 있고(ui.trigger_enabled, 기본 켜짐),
 * 끄면 탐색 없이 항상 가장 최근 구간만 보여줘서 위상이 안 맞아 화면이 흘러다니는
 * (트리거 도입 전) 원래 모습으로 돌아간다. 상태바에 T:OFF/TRIG/SEARCH로 항상 표시됨.
 */
#include "app.h"

#include "adc.h"
#include "display.h"
#include "grid.h"
#include "waveform.h"
#include "spectrum.h"
#include "hc165.h"
#include "input.h"
#include "ui.h"
#include "statusbar.h"
#include "millis.h"
#include "calib.h"

#define LONG_PRESS_MS 1500
#define MIN_REDRAW_MS 30 // 화면 갱신 최대 속도 제한(약 33Hz) - 빠른 T/DIV에서 입력 폴링 시간 확보용

typedef enum { LAYOUT_NONE, LAYOUT_CH1_FULL, LAYOUT_CH2_FULL, LAYOUT_SPLIT } layout_t;

// ---- 애플리케이션 전체에서 공유하는 상태 (poll_input/apply_ui_changes/app_run_frame에서 같이 씀) ----
static ui_state_t ui;
static waveform_ch_t wf_ch1, wf_ch2;
static layout_t layout = LAYOUT_NONE;

static debounce_state_t deb = { 0, 0 };
static quad_state_t quad1 = { 0 };
static quad_state_t quad2 = { 0 };
static uint16_t stable = 0;

static uint8_t sw1_was = 0, sw3_was = 0, sw4_was = 0;
static uint8_t sw1_long_fired = 0, sw3_long_fired = 0, sw4_long_fired = 0;
static uint32_t sw1_press_start = 0, sw3_press_start = 0, sw4_press_start = 0;

static int8_t prev_volt_div[2];
static int8_t prev_time_div;
static uint8_t prev_ch1_enabled, prev_ch2_enabled;
static uint8_t prev_measure_selected[2]; // FFT 선택 바뀌면(파형<->스펙트럼) 강제 재그리기용
static uint8_t ui_dirty = 0; // ui_update가 뭔가 바꿨으면 1 - 실제 반영은 그리기 끝난 뒤에
static uint32_t last_draw_ms = 0;

// 트리거(제로크로싱)로 찾은 "화면에 보여줄 샘플 시작 인덱스" - CH1 기준으로 한 번
// 찾아서 CH1/CH2 둘 다 같은 오프셋을 쓴다. adc_frame_ready마다 갱신되고, 그 사이엔
// poll_input()이 상태바를 다시 그릴 때도 이 오프셋 그대로 재사용한다.
static uint16_t trigger_offset = ADC_TRIGGER_MARGIN;

// ch1_enabled/ch2_enabled 조합에 맞춰 전체화면<->분할화면을 전환한다.
// 실제로 바뀔 때만 다시 그리므로 매 루프 호출해도 부담 없음.
static void apply_layout(void)
{
    layout_t want;
    if (ui.ch1_enabled && ui.ch2_enabled)
        want = LAYOUT_SPLIT;
    else if (ui.ch2_enabled) // CH2만 켜짐 (CH1 꺼짐)
        want = LAYOUT_CH2_FULL;
    else
        want = LAYOUT_CH1_FULL; // CH1만, 또는 둘 다 꺼짐

    if (want == layout)
        return;
    layout = want;

    switch (want) {
        case LAYOUT_CH1_FULL:
            waveform_set_region(&wf_ch1, &REGION_FULL);
            waveform_set_volt_div(&wf_ch1, (uint8_t)ui.volt_div_idx[0]);
            break;
        case LAYOUT_CH2_FULL:
            waveform_set_region(&wf_ch2, &REGION_FULL);
            waveform_set_volt_div(&wf_ch2, (uint8_t)ui.volt_div_idx[1]);
            break;
        case LAYOUT_SPLIT:
            waveform_set_region(&wf_ch1, &REGION_CH1_HALF);
            waveform_set_volt_div(&wf_ch1, (uint8_t)ui.volt_div_idx[0]);
            waveform_set_region(&wf_ch2, &REGION_CH2_HALF);
            waveform_set_volt_div(&wf_ch2, (uint8_t)ui.volt_div_idx[1]);
            grid_draw_divider();
            break;
        default:
            break;
    }
}

// 버튼/엔코더를 읽어서 ui_state_t만 갱신한다 (+ 상태바 텍스트 갱신).
// waveform_ch_t나 grid/adc 설정은 절대 안 건드림 - 파형 그리는 도중에 콜백으로
// 불려도 안전해야 하기 때문. 실제 레이아웃/스케일 반영은 apply_ui_changes()가 함.
static void poll_input(void)
{
    uint16_t raw = hc165_read();
    uint16_t prev_stable = stable;
    stable = input_debounce(&deb, raw); // 버튼(콘택트 바운스)용
    uint16_t pressed = input_pressed_edges(prev_stable, stable);

    // 엔코더는 raw를 바로 씀 - 디바운스(2번 연속 일치 요구)를 거치면 CLK/DT가
    // 계속 바뀌는 회전 중엔 좀처럼 "확정"되지 않아서 클릭이 씹히기 쉬움
    int8_t ch1_delta = input_quad_decode(&quad1, raw, HC165_CH1_CLK, HC165_CH1_DT);
    int8_t ch2_delta = input_quad_decode(&quad2, raw, HC165_CH2_CLK, HC165_CH2_DT);

    // ---- SW3/SW4: 롱프레스 = 캘리브레이션 진입, 숏프레스 = 원래 채널 on/off ----
    uint8_t sw3_now = (stable & HC165_SW3) != 0;
    uint8_t sw4_now = (stable & HC165_SW4) != 0;
    uint8_t sw3_short_confirmed = 0, sw4_short_confirmed = 0;

    if (sw3_now && !sw3_was) { sw3_press_start = millis(); sw3_long_fired = 0; }
    if (sw3_now && !sw3_long_fired && (millis() - sw3_press_start >= LONG_PRESS_MS)) {
        if (!calib_active())
            calib_start(0);
        sw3_long_fired = 1;
    }
    if (!sw3_now && sw3_was && !sw3_long_fired) {
        if (calib_active() && calib_active_channel() == 0) {
            adc_read_lock();
            calib_confirm(adc_buffer_ch1);
            adc_read_unlock();
        } else if (!calib_active())
            sw3_short_confirmed = 1;
    }
    sw3_was = sw3_now;

    if (sw4_now && !sw4_was) { sw4_press_start = millis(); sw4_long_fired = 0; }
    if (sw4_now && !sw4_long_fired && (millis() - sw4_press_start >= LONG_PRESS_MS)) {
        if (!calib_active())
            calib_start(1);
        sw4_long_fired = 1;
    }
    if (!sw4_now && sw4_was && !sw4_long_fired) {
        if (calib_active() && calib_active_channel() == 1) {
            adc_read_lock();
            calib_confirm(adc_buffer_ch2);
            adc_read_unlock();
        } else if (!calib_active())
            sw4_short_confirmed = 1;
    }
    sw4_was = sw4_now;

    // ---- SW1: 롱프레스 = 자동 트리거 켜기/끄기, 숏프레스 = 원래 RUN/STOP ----
    // (calib_active() 중엔 SW1이 "캘리브레이션 취소" 전용이라 롱프레스 액션은 안 냄)
    uint8_t sw1_now = (stable & HC165_SW1) != 0;
    uint8_t sw1_short_confirmed = 0;

    if (sw1_now && !sw1_was) { sw1_press_start = millis(); sw1_long_fired = 0; }
    if (sw1_now && !sw1_long_fired && (millis() - sw1_press_start >= LONG_PRESS_MS)) {
        if (!calib_active()) {
            ui.trigger_enabled ^= 1;
            statusbar_update_trigger_status(&ui); // 토글 즉시 반영 (다음 프레임까지 안 기다리게)
        }
        sw1_long_fired = 1;
    }
    if (!sw1_now && sw1_was && !sw1_long_fired) {
        if (!calib_active())
            sw1_short_confirmed = 1;
    }
    sw1_was = sw1_now;

    if (calib_active()) {
        if (pressed & HC165_SW1)
            calib_cancel();
        return;
    }

    // SW1/SW3/SW4의 원래 rising-edge 대신, 위에서 판정한 "짧게 눌렀다 뗌"을 ui_update에 넣어준다.
    uint16_t ui_pressed = (uint16_t)(pressed & ~(HC165_SW1 | HC165_SW3 | HC165_SW4));
    if (sw1_short_confirmed) ui_pressed |= HC165_SW1;
    if (sw3_short_confirmed) ui_pressed |= HC165_SW3;
    if (sw4_short_confirmed) ui_pressed |= HC165_SW4;

    if (ui_pressed || ch1_delta || ch2_delta) {
        ui_update(&ui, ui_pressed, ch1_delta, ch2_delta);
        // 상태바 영역만 건드림 - 파형 그리기 중간에 불려도 안전. trigger_offset은 마지막으로
        // 계산해둔 값을 그대로 재사용(지금 화면에 보이는 것과 같은 구간을 보여주기 위함).
        // adc_read_lock은 중첩 카운터라 이미 바깥(메인 그리기 블록)에서 락 걸린 상태로
        // 콜백으로 불려도, 최상위 루프에서 단독으로 불려도 둘 다 안전함.
        adc_read_lock();
        statusbar_draw_full(&ui, &adc_buffer_ch1[trigger_offset], &adc_buffer_ch2[trigger_offset]);
        adc_read_unlock();
        ui_dirty = 1;
    }
}

// poll_input()이 미뤄둔 변경사항(레이아웃/스케일/샘플링레이트)을 실제로 반영한다.
// 반드시 waveform_draw() 호출 바깥(최상위)에서만 불러야 함.
static void apply_ui_changes(void)
{
    static uint8_t was_calibrating = 0;

    // 캘리브레이션 화면에서 막 빠져나왔으면 일반 UI를 강제로 다시 그린다
    // (apply_layout은 레이아웃 "종류"가 안 바뀌면 다시 안 그리는데, 캘리브레이션
    //  화면이 그 위를 덮어썼기 때문에 종류와 상관없이 무조건 다시 그려야 함)
    if (was_calibrating && !calib_active()) {
        layout = LAYOUT_NONE;
        apply_layout();
        adc_read_lock();
        statusbar_draw_full(&ui, &adc_buffer_ch1[trigger_offset], &adc_buffer_ch2[trigger_offset]);
        adc_read_unlock();
    }
    was_calibrating = calib_active();

    if (!ui_dirty)
        return;
    ui_dirty = 0;

    apply_layout(); // CH1/CH2 on-off가 바뀌었으면 전체화면<->분할 전환

    // measure_selected가 바뀌면(특히 FFT를 골랐다/뺐다 하면) 그래프 영역 내용 자체가
    // 파형<->스펙트럼으로 바뀌는데, 레이아웃 "종류"(전체/분할)는 그대로일 수 있어서
    // apply_layout()만으론 다시 안 그려짐 - layout을 강제로 NONE으로 만들어서 다시
    // 부르면 waveform_set_region()이 grid도 다시 그리고 dirty-tracking도 리셋해줌
    // (스펙트럼이 그려놓은 픽셀을 파형이 "이전 프레임 값"으로 착각하는 것 방지).
    if (ui.measure_selected[0] != prev_measure_selected[0] ||
        ui.measure_selected[1] != prev_measure_selected[1]) {
        prev_measure_selected[0] = ui.measure_selected[0];
        prev_measure_selected[1] = ui.measure_selected[1];
        layout = LAYOUT_NONE;
        apply_layout();
        spectrum_reset(); // 방금 새로 그려진 격자를 이전 막대 높이 기억 때문에 잘못 지우지 않게
    }

    // 채널이 막 꺼졌는데 레이아웃 자체는 안 바뀐 경우(둘 다 꺼짐 등) 트레이스가
    // 화면에 그대로 남으니 별도로 지워준다. 레이아웃이 바뀐 경우엔 이미 지워진
    // 상태라 waveform_clear()는 그냥 아무 일도 안 하고 끝남(안전).
    if (!ui.ch1_enabled && prev_ch1_enabled)
        waveform_clear(&wf_ch1);
    if (!ui.ch2_enabled && prev_ch2_enabled)
        waveform_clear(&wf_ch2);
    prev_ch1_enabled = ui.ch1_enabled;
    prev_ch2_enabled = ui.ch2_enabled;

    if (ui.volt_div_idx[0] != prev_volt_div[0]) {
        waveform_set_volt_div(&wf_ch1, (uint8_t)ui.volt_div_idx[0]);
        prev_volt_div[0] = ui.volt_div_idx[0];
    }
    if (ui.volt_div_idx[1] != prev_volt_div[1]) {
        waveform_set_volt_div(&wf_ch2, (uint8_t)ui.volt_div_idx[1]);
        prev_volt_div[1] = ui.volt_div_idx[1];
    }
    if (ui.time_div_idx != prev_time_div) {
        adc_set_time_div((uint8_t)ui.time_div_idx); // 샘플링 레이트 자체를 바꿈 (CH1/CH2 공유)
        prev_time_div = ui.time_div_idx;
    }
}

void app_init(void)
{
    ui_init(&ui);

    sampling_timer_init(1000);              // 타이머/인터럽트 초기 설정 (레이트는 바로 아래서 실제 값으로 맞춤)
    adc_set_time_div((uint8_t)ui.time_div_idx);

    prev_volt_div[0] = ui.volt_div_idx[0];
    prev_volt_div[1] = ui.volt_div_idx[1];
    prev_time_div = ui.time_div_idx;
    prev_ch1_enabled = ui.ch1_enabled;
    prev_ch2_enabled = ui.ch2_enabled;
    prev_measure_selected[0] = ui.measure_selected[0];
    prev_measure_selected[1] = ui.measure_selected[1];

    waveform_init(&wf_ch1);
    waveform_init(&wf_ch2);
    apply_layout(); // 초기 레이아웃(전체화면) 그리기
    statusbar_draw_full(&ui, &adc_buffer_ch1[trigger_offset], &adc_buffer_ch2[trigger_offset]);
}

void app_run_frame(void)
{
    poll_input();

    if (adc_frame_ready) {
        adc_frame_ready = 0;
        // 빠른 T/DIV(예: 200us/div)에서는 프레임이 초당 수백 번 들어오는데, 그때마다
        // LCD를 다시 그리면 SPI 전송 시간이 hc165_read() 폴링을 거의 다 잡아먹어서
        // 버튼/엔코더가 죽은 것처럼 보인다. 샘플링은 빠르게 유지하되(파형 디테일 유지),
        // 실제 화면 갱신은 MIN_REDRAW_MS 간격으로 속도제한을 걸어 입력 폴링 시간을 확보한다.
        if (!calib_active() && !ui.run_stopped && (millis() - last_draw_ms >= MIN_REDRAW_MS)) {
            last_draw_ms = millis();
            // 지금부터 adc_buffer_ch1/ch2를 다 읽어 쓸 때까지 락 - 그리기(30ms+)가
            // 캡처 한 바퀴(~19ms@12kHz)보다 오래 걸려도 ISR이 이 버퍼를 스왑/덮어쓰지
            // 않게 막는다(핑퐁 2개만으로는 못 막는 구간 - adc.h 주석 참고).
            adc_read_lock();

            // CH1 기준으로 트리거(제로크로싱)를 찾아서 CH1/CH2 둘 다 같은 구간을 보여준다.
            // 못 찾으면 가장 최근 구간(margin 끝)으로 폴백 - 지금까지 하던 대로.
            // 트리거가 꺼져있으면(SW1 롱프레스) 탐색 자체를 안 하고 항상 가장 최근
            // 구간만 보여줌 - 트리거 도입 전 원래 동작(위상 안 맞아서 화면이 흘러다님).
            if (ui.trigger_enabled) {
                trigger_offset = adc_find_trigger(adc_buffer_ch1);
            } else {
                trigger_offset = ADC_TRIGGER_MARGIN;
                adc_trigger_locked = 0;
            }
            statusbar_update_trigger_status(&ui); // 트리거 상태 갱신됐으니 표시도 갱신
            const volatile uint16_t *win1 = &adc_buffer_ch1[trigger_offset];
            const volatile uint16_t *win2 = &adc_buffer_ch2[trigger_offset];

            // FFT는 채널 하나만 켜진 전체화면일 때만 실제 스펙트럼 그래프로 그림
            // (분할화면에선 가로축이 시간/주파수로 서로 안 맞아서 안 켬 - ui.h 주석 참고).
            uint8_t ch1_fft = ui.ch1_enabled && !ui.ch2_enabled &&
                               ui.measure_selected[0] == MEASURE_FFT_IDX;
            uint8_t ch2_fft = ui.ch2_enabled && !ui.ch1_enabled &&
                               ui.measure_selected[1] == MEASURE_FFT_IDX;

            if (ch1_fft)
                spectrum_draw(&REGION_FULL, 0, win1, COLOR_GREEN);
            else if (ui.ch1_enabled)
                waveform_draw(&wf_ch1, win1, 0, COLOR_GREEN, poll_input);

            if (ch2_fft)
                spectrum_draw(&REGION_FULL, 1, win2, COLOR_CYAN);
            else if (ui.ch2_enabled)
                waveform_draw(&wf_ch2, win2, 1, COLOR_CYAN, poll_input);

            statusbar_update_measure_values(&ui, win1, win2);

            adc_read_unlock();
        }
    }

    apply_ui_changes(); // 그리는 도중 poll_input()이 ui_dirty를 세웠을 수 있으니 그리기 끝난 뒤 반영
}
