/*
 * app.c - UI 상태, 레이아웃, 트리거, 그리기 오케스트레이션.
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
#define MIN_REDRAW_MS 30

typedef enum { LAYOUT_NONE, LAYOUT_CH1_FULL, LAYOUT_CH2_FULL, LAYOUT_SPLIT } layout_t;

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
static uint8_t prev_measure_selected[2];
static uint8_t ui_dirty = 0;
static uint32_t last_draw_ms = 0;

static uint16_t trigger_offset = ADC_TRIGGER_MARGIN;

static void apply_layout(void)
{
    layout_t want;
    if (ui.ch1_enabled && ui.ch2_enabled)
        want = LAYOUT_SPLIT;
    else if (ui.ch2_enabled)
        want = LAYOUT_CH2_FULL;
    else
        want = LAYOUT_CH1_FULL;

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

// waveform_draw() 콜백으로 그리기 중에도 불릴 수 있어서 ui_state_t만 건드림.
static void poll_input(void)
{
    uint16_t raw = hc165_read();
    uint16_t prev_stable = stable;
    stable = input_debounce(&deb, raw);
    uint16_t pressed = input_pressed_edges(prev_stable, stable);

    int8_t ch1_delta = input_quad_decode(&quad1, raw, HC165_CH1_CLK, HC165_CH1_DT);
    int8_t ch2_delta = input_quad_decode(&quad2, raw, HC165_CH2_CLK, HC165_CH2_DT);

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

    uint8_t sw1_now = (stable & HC165_SW1) != 0;
    uint8_t sw1_short_confirmed = 0;

    if (sw1_now && !sw1_was) { sw1_press_start = millis(); sw1_long_fired = 0; }
    if (sw1_now && !sw1_long_fired && (millis() - sw1_press_start >= LONG_PRESS_MS)) {
        if (!calib_active()) {
            ui.trigger_enabled ^= 1;
            statusbar_update_trigger_status(&ui);
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

    uint16_t ui_pressed = (uint16_t)(pressed & ~(HC165_SW1 | HC165_SW3 | HC165_SW4));
    if (sw1_short_confirmed) ui_pressed |= HC165_SW1;
    if (sw3_short_confirmed) ui_pressed |= HC165_SW3;
    if (sw4_short_confirmed) ui_pressed |= HC165_SW4;

    if (ui_pressed || ch1_delta || ch2_delta) {
        ui_update(&ui, ui_pressed, ch1_delta, ch2_delta);
        adc_read_lock();
        statusbar_draw_full(&ui, &adc_buffer_ch1[trigger_offset], &adc_buffer_ch2[trigger_offset]);
        adc_read_unlock();
        ui_dirty = 1;
    }
}

// 반드시 waveform_draw() 바깥에서만 호출.
static void apply_ui_changes(void)
{
    static uint8_t was_calibrating = 0;

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

    apply_layout();

    if (ui.measure_selected[0] != prev_measure_selected[0] ||
        ui.measure_selected[1] != prev_measure_selected[1]) {
        prev_measure_selected[0] = ui.measure_selected[0];
        prev_measure_selected[1] = ui.measure_selected[1];
        layout = LAYOUT_NONE;
        apply_layout();
        spectrum_reset();
    }

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
        adc_set_time_div((uint8_t)ui.time_div_idx);
        prev_time_div = ui.time_div_idx;
    }
}

void app_init(void)
{
    ui_init(&ui);

    sampling_timer_init(1000);
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
    apply_layout();
    statusbar_draw_full(&ui, &adc_buffer_ch1[trigger_offset], &adc_buffer_ch2[trigger_offset]);
}

void app_run_frame(void)
{
    poll_input();

    if (adc_frame_ready) {
        adc_frame_ready = 0;
        if (!calib_active() && !ui.run_stopped && (millis() - last_draw_ms >= MIN_REDRAW_MS)) {
            last_draw_ms = millis();
            adc_read_lock();

            if (ui.trigger_enabled) {
                trigger_offset = adc_find_trigger(adc_buffer_ch1);
            } else {
                trigger_offset = ADC_TRIGGER_MARGIN;
                adc_trigger_locked = 0;
            }
            statusbar_update_trigger_status(&ui);
            const volatile uint16_t *win1 = &adc_buffer_ch1[trigger_offset];
            const volatile uint16_t *win2 = &adc_buffer_ch2[trigger_offset];

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

    apply_ui_changes();
}
