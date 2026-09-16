#include "ui.h"
#include "hc165.h"

// 이름/포인터 배열 전부 PROGMEM. statusbar.c의 VOLT_DIV_STR/TIME_DIV_STR도 같은 패턴.
static const char MNAME_0[] PROGMEM = "RMS";
static const char MNAME_1[] PROGMEM = "MAX";
static const char MNAME_2[] PROGMEM = "MIN";
static const char MNAME_3[] PROGMEM = "FFT";
static const char MNAME_4[] PROGMEM = "FREQ";
static const char MNAME_5[] PROGMEM = "PERIOD";
static const char *const MEASURE_NAMES[MEASURE_ITEM_COUNT] PROGMEM = {
    MNAME_0, MNAME_1, MNAME_2, MNAME_3, MNAME_4, MNAME_5
};

const uint16_t VOLT_PER_DIV_MV[VOLT_DIV_TABLE_SIZE] PROGMEM = {
    100, 200, 300, 500, 700, 1000, 1500, 2000, 3000, 5000
};
const uint32_t TIME_PER_DIV_US[TIME_DIV_TABLE_SIZE] PROGMEM = {
    200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000
};

static int8_t clamp_volt_div(int8_t v)
{
    if (v < 0) return 0;
    if (v >= VOLT_DIV_TABLE_SIZE) return VOLT_DIV_TABLE_SIZE - 1;
    return v;
}

static int8_t clamp_time_div(int8_t v)
{
    if (v < 0) return 0;
    if (v >= TIME_DIV_TABLE_SIZE) return TIME_DIV_TABLE_SIZE - 1;
    return v;
}

static uint8_t wrap_measure_idx(int8_t idx)
{
    if (idx < 0) idx += MEASURE_ITEM_COUNT;
    if (idx >= MEASURE_ITEM_COUNT) idx -= MEASURE_ITEM_COUNT;
    return (uint8_t)idx;
}

void ui_init(ui_state_t *ui)
{
    ui->run_stopped = 0;
    ui->ch1_enabled = 1;
    ui->ch2_enabled = 0;
    ui->measure_mode = 0;
    ui->ch1_axis = AXIS_VERTICAL;
    ui->volt_div_idx[0] = 5;
    ui->volt_div_idx[1] = 5;
    ui->time_div_idx = 5;
    ui->measure_menu_idx[0] = 0;
    ui->measure_menu_idx[1] = 0;
    ui->measure_selected[0] = 0;
    ui->measure_selected[1] = 0;
    ui->trigger_enabled = 1;
}

void ui_update(ui_state_t *ui, uint16_t pressed, int8_t ch1_delta, int8_t ch2_delta)
{
    if (pressed & HC165_SW1) ui->run_stopped ^= 1;
    if (pressed & HC165_SW3) ui->ch1_enabled ^= 1;
    if (pressed & HC165_SW4) ui->ch2_enabled ^= 1;
    if (pressed & HC165_SW2) ui->measure_mode ^= 1;

    if (ui->measure_mode) {
        if (pressed & HC165_CH1_SW)
            ui->measure_selected[0] = ui->measure_menu_idx[0];
        ui->measure_menu_idx[0] = wrap_measure_idx((int8_t)ui->measure_menu_idx[0] + ch1_delta);

        if (pressed & HC165_CH2_SW)
            ui->measure_selected[1] = ui->measure_menu_idx[1];
        ui->measure_menu_idx[1] = wrap_measure_idx((int8_t)ui->measure_menu_idx[1] + ch2_delta);
    } else {
        if (pressed & HC165_CH1_SW)
            ui->ch1_axis = (ui->ch1_axis == AXIS_VERTICAL) ? AXIS_HORIZONTAL : AXIS_VERTICAL;
        if (ui->ch1_axis == AXIS_VERTICAL)
            ui->volt_div_idx[0] = clamp_volt_div((int8_t)(ui->volt_div_idx[0] + ch1_delta));
        else
            ui->time_div_idx = clamp_time_div((int8_t)(ui->time_div_idx + ch1_delta));

        ui->volt_div_idx[1] = clamp_volt_div((int8_t)(ui->volt_div_idx[1] + ch2_delta));
    }
}

const char *ui_measure_name(uint8_t idx)
{
    if (idx >= MEASURE_ITEM_COUNT)
        return PSTR("?");
    return (const char *)pgm_read_word(&MEASURE_NAMES[idx]);
}
