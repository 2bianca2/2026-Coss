#include "ui.h"
#include "hc165.h"

// 각 이름은 플래시(PROGMEM)에 두고, 포인터 배열 자체도 PROGMEM에 둠 - 그래서
// 읽을 땐 pgm_read_word()로 배열에서 포인터부터 꺼낸 다음 그 포인터로 문자열을 읽는다
// (ui_measure_name() 참고). statusbar.c의 VOLT_DIV_STR/TIME_DIV_STR도 같은 패턴.
static const char MNAME_0[] PROGMEM = "RMS";
static const char MNAME_1[] PROGMEM = "MAX";
static const char MNAME_2[] PROGMEM = "MIN";
static const char MNAME_3[] PROGMEM = "FFT";
static const char MNAME_4[] PROGMEM = "FREQ";
static const char MNAME_5[] PROGMEM = "PERIOD";
static const char *const MEASURE_NAMES[MEASURE_ITEM_COUNT] PROGMEM = {
    MNAME_0, MNAME_1, MNAME_2, MNAME_3, MNAME_4, MNAME_5
};

// 표시 문자열(statusbar.c)과 짝이 맞는 실제 값들. PROGMEM(플래시)에 둠 - 읽을 땐
// pgm_read_word()/pgm_read_dword() 필요(adc.c/waveform.c 사용부 참고).
// 입력 신호가 -5V~+5V(10Vpp) 고정이라, 그 범위에서 실제로 쓸모 있는 값들로만 구성.
const uint16_t VOLT_PER_DIV_MV[VOLT_DIV_TABLE_SIZE] PROGMEM = {
    100, 200, 300, 500, 700, 1000, 1500, 2000, 3000, 5000
};
// 200us/div까지 빠른 옵션을 넣어야 1kHz 근처 신호(RC 충방전 등)의 파형 모양이 보임 -
// ADC 변환속도(adc.c 프리스케일러) 한계상 이보다 더 빠른 값은 요청해도 못 따라감
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
    ui->ch2_enabled = 0; // CH2 하드웨어 붙으면 SW4로 켜면 됨
    ui->measure_mode = 0;
    ui->ch1_axis = AXIS_VERTICAL;
    ui->volt_div_idx[0] = 5; // 1V/div 기본값 (전체화면 12칸*1V=12V 범위라 -5~+5V 신호가 여유있게 다 보임)
    ui->volt_div_idx[1] = 5;
    ui->time_div_idx = 5;    // 10ms/div 기본값 (CH1/CH2 공유)
    ui->measure_menu_idx[0] = 0;
    ui->measure_menu_idx[1] = 0;
    ui->measure_selected[0] = 0;
    ui->measure_selected[1] = 0;
    ui->trigger_enabled = 1; // 기본은 자동 트리거 켜짐
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
            ui->time_div_idx = clamp_time_div((int8_t)(ui->time_div_idx + ch1_delta)); // 공유 T/DIV

        // CH2는 축 토글 없이 항상 자기 V/DIV만 조절 (T/DIV는 ADC 공유라 CH1 쪽에서만 바꿈)
        ui->volt_div_idx[1] = clamp_volt_div((int8_t)(ui->volt_div_idx[1] + ch2_delta));
    }
}

const char *ui_measure_name(uint8_t idx)
{
    // 리턴값은 항상 PROGMEM 포인터(호출부는 lcd_draw_string_P/strncpy_P로 읽음) -
    // "?" 폴백도 PSTR()로 플래시에 둬야 함(안 그러면 SRAM 주소를 플래시 주소인 척 리턴하는 버그).
    if (idx >= MEASURE_ITEM_COUNT)
        return PSTR("?");
    return (const char *)pgm_read_word(&MEASURE_NAMES[idx]);
}
