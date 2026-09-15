# 코드 구조 설명

ATmega128 기반 미니 오실로스코프 펌웨어(bare-metal, 레지스터 직접 제어, 라이브러리 없음)의
파일별/함수별 설명입니다. 전체 설계 개요는 `app.c` 상단 주석에도 있습니다.

## 전체 구조 한눈에 보기

```
main.c  ─▶ 하드웨어 초기화 순서만 정함 (spi/hc165/lcd/adc/millis/calib init)
             │
             ▼
app.c   ─▶ 애플리케이션 로직(입력 폴링, 레이아웃 전환, 트리거, 그리기 오케스트레이션)
             │           UI 상태(ui_state_t)/파형 인스턴스/트리거 오프셋을 여기서 소유
             │
   ┌─────────┼─────────────┬─────────────┬─────────────┬─────────────┐
   ▼         ▼             ▼             ▼             ▼             ▼
adc.c    waveform.c    spectrum.c    statusbar.c    calib.c       ui.c
(샘플링)  (파형 그리기)  (FFT 그래프)  (상태바 텍스트) (보정 마법사)  (UI 상태머신)
   │         │             │
   │         └──────┬──────┘
   │                ▼
   │            grid.c (격자/화면분할 영역)
   │
   └── fft.c (고정소수점 FFT 계산)

공통 하위 계층: display.c(LCD 드라이버) / text.c(폰트) / spi.c(SPI) / hc165.c(버튼·엔코더 읽기)
              / input.c(디바운스·엣지·쿼드러처 디코딩) / millis.c(타이머 기반 ms 카운터)
```

**핵심 하드웨어 제약**: 74HC165(버튼/엔코더)와 ILI9488 LCD가 SPI 버스(SCK/MISO)를 공유해서
인터럽트로 버튼을 안전하게 읽을 수 없다. 그래서 `app.c`의 `poll_input()`을 파형을 그리는
도중(`waveform_draw()` 내부) 콜백으로 계속 불러서 버튼/엔코더 입력이 안 씹히게 한다.

---

## main.c

엔트리 포인트. 하드웨어 초기화 순서만 정하고 실제 로직은 전부 `app.c`에 위임한다.

- `int main(void)` — `spi_init → hc165_init → lcd_init → adc_init → millis_init → calib_init
  → app_init() → sei() → while(1) { app_run_frame(); }` 순서로 실행.

---

## app.c / app.h

메인 애플리케이션 로직. UI 상태, 파형 인스턴스(`wf_ch1`, `wf_ch2`), 트리거 오프셋 등
애플리케이션 전역 상태를 전부 이 모듈이 소유한다(파일 static 변수).

**공개 함수 (app.h)**
- `void app_init(void)` — UI 상태 초기화, 초기 샘플링레이트/T-DIV 설정, 초기 화면(격자+상태바)
  그리기. `main()`에서 하드웨어 init 끝난 뒤 한 번만 호출.
- `void app_run_frame(void)` — 메인루프 한 바퀴. 입력 폴링 → (새 ADC 프레임 준비됐고 RUN
  상태고 재그리기 주기가 됐으면) 트리거 탐색 + 파형/스펙트럼 그리기 + measure 값 갱신 →
  지연된 UI 변경사항(레이아웃/스케일/샘플링레이트) 반영.

**내부(static) 함수**
- `apply_layout(void)` — `ch1_enabled`/`ch2_enabled` 조합에 따라 전체화면(CH1만/CH2만) 또는
  분할화면(CH1+CH2)으로 전환. 실제로 레이아웃 종류가 바뀔 때만 다시 그림.
- `poll_input(void)` — 74HC165를 읽어서 `ui_state_t`와 상태바 텍스트만 갱신. **주의**:
  `waveform_ch_t`나 grid/adc 설정은 절대 안 건드림 — 파형 그리는 도중 콜백으로 불려도
  안전해야 하기 때문. SW1(RUN/STOP·트리거on/off 롱프레스), SW2(measure모드),
  SW3/SW4(채널on/off·캘리브레이션 진입 롱프레스), 엔코더 CLK/DT를 여기서 처리.
- `apply_ui_changes(void)` — `poll_input()`이 `ui_dirty` 플래그로 미뤄둔 변경사항(레이아웃,
  V/DIV, T/DIV, FFT⇄파형 전환)을 실제로 반영. 반드시 `waveform_draw()` 호출 바깥(최상위
  루프)에서만 불러야 함 — 그리는 도중에 그리기 대상 자체가 바뀌면 위험하기 때문.

---

## adc.c / adc.h

ADC0(PF0,CH1)/ADC1(PF1,CH2) 2채널 샘플링 드라이버. ADC 하드웨어가 1개뿐이라 MUX를
번갈아가며 CH1→CH2 순서로 변환하고, 핑퐁(더블) 버퍼링으로 ISR과 메인루프 간 데이터
레이스를 막는다.

**주요 상수**
- `ADC_BUFFER_SIZE` (96) — 화면에 보여주는 샘플 수. waveform/grid의 가로 컬럼 수와 1:1.
- `ADC_TRIGGER_MARGIN` (128) — 트리거(제로크로싱) 탐색용 여유 캡처 샘플 수.
- `ADC_CAPTURE_SIZE` — 실제 raw 캡처 버퍼 크기 (`ADC_BUFFER_SIZE + ADC_TRIGGER_MARGIN` = 224).

**공개 함수**
- `void adc_init(void)` — PF0/PF1 입력 설정, ADC 활성화(프리스케일러 32, 변환완료 인터럽트).
- `void sampling_timer_init(uint16_t sample_rate_hz)` — Timer1 CTC 모드로 샘플링 타이머 설정.
- `void adc_set_sample_rate(uint16_t sample_rate_hz)` — 실행 중 샘플링 레이트 변경(버퍼/프레임
  상태도 리셋). CH1/CH2 공유.
- `void adc_set_time_div(uint8_t time_div_idx)` — `TIME_PER_DIV_US[idx]` 기준으로 "가로 한
  칸 = 그 시간"이 되도록 샘플링 레이트를 역산해서 설정. 실측 변환+오버헤드 시간을 고려해
  12000Hz로 클램프.
- `uint16_t adc_get_sample_rate(void)` — 현재 채널당 샘플링레이트(Hz). FREQ/PERIOD 계산에 씀.
- `int32_t adc_raw_to_mv(uint16_t raw)` — raw(0~1023) → mV. 프론트엔드가 -5V~+5V를
  0~5V(2.5V=0V 기준)로 분배했다는 전제의 이상적 계산식(미보정 상태용).
- `uint16_t adc_find_trigger(const volatile uint16_t *capture_buf_ch1)` — CH1 마진 구간에서
  상승 크로싱(기준선은 그 구간의 (최댓값+최솟값)/2, 실제 스코프의 auto-level 트리거 방식)을
  찾아 시작 인덱스를 리턴. 못 찾으면 `ADC_TRIGGER_MARGIN`(가장 최근 구간) 폴백.
- `void adc_read_lock(void)` / `void adc_read_unlock(void)` — 중첩 카운터 락. `adc_buffer_ch1
  /ch2`를 읽는 동안 감싸서 호출하면 ISR이 그 버퍼를 스왑/덮어쓰지 않고 프레임 하나를
  버린다(핑퐁 2개만으론 못 막는 레이스가 있어서 추가된 안전장치).

**전역 변수 (extern)**
- `volatile uint16_t *adc_buffer_ch1/ch2` — 메인루프가 읽는 "완성된" 버퍼(포인터, 매 프레임
  바뀜 — 캐싱하지 말고 매번 새로 참조).
- `volatile uint8_t adc_frame_ready` — CH1/CH2 버퍼가 같이 다 찼을 때 1.
- `uint8_t adc_trigger_locked` — 마지막 트리거 탐색에서 실제로 크로싱을 찾았으면 1.

**인터럽트**
- `ISR(TIMER1_COMPA_vect)` — 한 쌍(CH1+CH2) 변환 시작, CH1부터.
- `ISR(ADC_vect)` — 변환 완료마다 호출. CH1 변환 완료 시 MUX를 CH2로 전환(10us 안정화
  딜레이 후 다음 변환 시작), CH2 변환 완료 시 버퍼에 기록하고 인덱스 증가, 캡처 한 바퀴
  다 돌면(락 안 걸려있을 때) 핑퐁 버퍼 스왑 + `adc_frame_ready=1`.

---

## waveform.c / waveform.h

ADC 버퍼를 LCD에 파형으로 그리는 모듈. 부분갱신(이전에 그렸던 구간만 지우고 새로 그림)
방식이라 화면 전체를 지웠다 다시 그리는 깜빡임이 없다. CH1/CH2가 동시에 그릴 수 있도록
상태를 `waveform_ch_t` 구조체 하나에 담고 채널마다 인스턴스를 따로 갖는다(재진입 가능).

**타입**
- `waveform_ch_t` — `region`(그릴 영역), `prev_y0/prev_y1[ADC_BUFFER_SIZE]`(컬럼별 지난
  프레임 선분 y범위), `has_prev[]`, `half_range_mv`(세로 스케일).
- `waveform_poll_fn` — `void(*)(void)`. 그리는 도중 몇 컬럼마다 호출할 콜백 타입(버튼/엔코더
  폴링용).

**공개 함수**
- `void waveform_init(waveform_ch_t *wf)` — REGION_FULL로 초기화, dirty-tracking 리셋.
- `void waveform_set_region(waveform_ch_t *wf, const scope_region_t *region)` — 그릴 영역
  변경(전체화면⇄분할화면 전환 시). 영역을 격자로 새로 그리고 추적 상태 리셋.
- `void waveform_set_volt_div(waveform_ch_t *wf, uint8_t volt_div_idx)` — V/DIV 기준으로
  세로 스케일(`half_range_mv`) 재계산. `VOLT_PER_DIV_MV`는 PROGMEM이라 `pgm_read_word` 사용.
- `void waveform_draw(waveform_ch_t *wf, const volatile uint16_t *adc_buf, uint8_t ch_idx,
  uint8_t r, uint8_t g, uint8_t b, waveform_poll_fn poll)` — 파형을 그린다. 컬럼별로 지난
  프레임과 선분 범위가 같으면 건드리지 않고(깜빡임 방지), 다르면 지우고 대각선 보간
  (`DIAG_STEPS=3` 단계)으로 다시 그림. `POLL_EVERY_N_COLS`(2)마다 `poll()` 호출. 폴링 중
  캘리브레이션이 시작되면 즉시 리턴(화면 덮어쓰기 방지).
- `void waveform_clear(waveform_ch_t *wf)` — 화면에 남은 파형 자취를 지우고 격자만 남김
  (채널 꺼졌을 때 등).

**내부(static) 함수**
- `wave_y_range/wave_mid_y` — region의 세로 픽셀 범위/중앙(0V) y좌표.
- `mv_to_y` — mV → y좌표 변환(범위 초과 시 화면 끝에서 클리핑).
- `col_x` — 샘플 인덱스 → x좌표(`LCD_WIDTH`를 `ADC_BUFFER_SIZE`로 나눠서 균등 분배).
- `draw_diagonal_segment` — 두 점 사이를 `DIAG_STEPS` 단계로 선형보간해서 대각선에 가깝게
  그림(고정 SPI 비용, 사각형 통째로 채우면 급격한 값 변화 구간이 세로 블록처럼 보이는 걸
  방지).
- `reset_trace_tracking` — `prev_y0/prev_y1/has_prev` 초기화.

---

## spectrum.c / spectrum.h

FFT 결과(`fft.c`)를 막대그래프로 그리는 렌더러. `waveform.c`처럼 막대별 이전 높이를
기억해서 줄어든 부분만 지우는 부분갱신 방식.

- `void spectrum_draw(const scope_region_t *region, uint8_t ch_idx, const volatile uint16_t
  *adc_buf, uint8_t r, uint8_t g, uint8_t b)` — FFT 계산 → DC(0번 빈) 제외한 최댓값/피크
  빈으로 자동 스케일 → 막대 32개 그림(최대 높이는 영역의 80%로 제한해 위쪽 여백 확보,
  맨 아래 8px는 축 라벨 전용 공간) → 맨 위에 `PEAK:xxxHZ`, 맨 아래 양끝에 `0HZ`/나이퀴스트
  주파수 라벨.
- `void spectrum_reset(void)` — 막대 높이 기억(dirty-tracking) 리셋. FFT 화면에 새로
  진입할 때(다른 measure 항목→FFT 전환) 호출해서 방금 그린 새 격자를 잘못 지우지 않게 함.

---

## fft.c / fft.h

64포인트 고정소수점(Q15) radix-2 FFT. `FFT_N`(64)은 `ADC_BUFFER_SIZE`(96)와 독립적으로
고정 — radix-2 FFT는 2의 거듭제곱이어야 해서 96에 못 맞추기 때문에 앞 64개만 사용.

- `void fft_compute_magnitude(uint8_t ch_idx, const volatile uint16_t *adc_buf, uint16_t
  *mag_out)` — adc_buf 앞 64개를 읽어 FFT 수행 후 `mag_out[FFT_BINS]`(32개)에 빈별 진폭을
  채운다. 절대 mV가 아니라 그래프 자동 스케일용 상대값.

**내부(static) 함수**
- `q15_mul` — Q15 고정소수점 곱셈.
- `bit_reverse` — FFT 전처리용 비트 반전 정렬.
- `isqrt32` — 정수 제곱근(이진 탐색), 진폭 크기 계산용.

**내부 상수**: `FFT_COS`/`FFT_SIN`(PROGMEM) — 트위들 팩터 테이블. `work_re`/`work_im` —
CH1/CH2 공용 작업버퍼(메모리 절약 위해 하나만 둠 — FFT는 채널 하나만 켜진 전체화면일
때만 동작하므로 동시에 두 채널을 계산할 일이 없음).

---

## statusbar.c / statusbar.h

화면 맨 위 상태바(3줄: 전역 RUN/STOP·CH1/CH2 on-off·T/DIV, CH1 줄, CH2 줄) 렌더링과
RMS/MAX/MIN/FREQ/PERIOD 계산.

**공개 함수**
- `void statusbar_draw_full(const ui_state_t *ui, const volatile uint16_t *adc_buf1, const
  volatile uint16_t *adc_buf2)` — 상태바 전체를 다시 그림.
- `void statusbar_update_measure_values(...)` — measure 모드일 때 CH1/CH2 값만 가볍게
  다시 그림(ADC 새 프레임마다 호출).
- `void statusbar_update_trigger_status(const ui_state_t *ui)` — 트리거 상태 표시
  (`T:OFF`/`TRIG`/`SEARCH`) 갱신.

**내부(static) 함수**
- `isqrt32` — 정수 제곱근(RMS 계산용).
- `find_period_us` — 제로크로싱 2개 사이 간격으로 주기를 구함. 기준선은 고정 0V가 아니라
  그 프레임의 (최댓값+최솟값)/2(신호가 한쪽으로 치우친 RC 충방전 곡선 등에서도 동작하게).
  Q8 고정소수점 선형보간으로 서브샘플 정밀도 확보. 한 주기를 못 찾으면 0 리턴.
- `compute_stats` — RMS/MAX/MIN(mV) 계산.
- `format_measure_value` — 선택된 measure 항목에 맞는 표시 문자열 생성(`snprintf_P`로
  PROGMEM 포맷 문자열 사용).
- `draw_measure_value` / `draw_channel_row` — 채널 한 줄(라벨 + V/DIV 또는 measure
  메뉴+값) 그리기.

**PROGMEM 데이터**: `VOLT_DIV_STR`/`TIME_DIV_STR` — V/DIV·T/DIV 표시 문자열 테이블(배열
자체와 각 문자열 모두 플래시에 둠, SRAM 절약).

---

## calib.c / calib.h

CH1/CH2 2점(GND, +5V) 캘리브레이션. EEPROM에 저장해서 전원 꺼도 유지.

**상태 흐름**: `calib_start(ch)`(롱프레스로 진입) → `CAL_WAIT_GND` → 버튼 확인 →
`CAL_WAIT_REF` → 버튼 확인(EEPROM 저장) → `CAL_DONE` → 버튼 확인(닫기). 언제든
`calib_cancel()`(SW1)로 취소 가능.

**공개 함수**
- `void calib_init(void)` — 부팅 시 EEPROM에서 보정값 로드(없으면 미보정 상태).
- `int32_t calib_raw_to_mv(uint8_t ch_idx, uint16_t raw)` — 채널별 보정값이 있으면 그걸로,
  없으면 `adc_raw_to_mv()`의 이상적 계산식으로 mV 환산. **이 프로젝트의 모든 mV 표시/계산이
  이 함수를 거친다** (waveform, statusbar, adc 트리거 탐색 등).
- `void calib_start(uint8_t ch_idx)` / `uint8_t calib_active(void)` / `uint8_t
  calib_active_channel(void)` / `calib_state_t calib_get_state(void)`
- `void calib_confirm(const volatile uint16_t *adc_buf)` — 현재 단계 버튼을 눌렀을 때 호출.
  단계 진행/저장/화면닫기.
- `void calib_cancel(void)` — 취소.

**내부(static) 함수**
- `average_buf` — 버퍼 전체 평균(안정된 DC 전압 측정용).
- `has_calib` — `ref_raw != zero_raw`면 보정됨으로 판단.
- `render` — 캘리브레이션 마법사 화면(단계별 안내 문구, `PSTR()`로 플래시에서 읽음).

---

## ui.c / ui.h

스위치/엔코더 입력을 UI 상태(`ui_state_t`)로 변환하는 순수 상태머신. 하드웨어를 직접
건드리지 않음(`app.c`의 `poll_input()`이 `hc165_read()` 결과를 여기로 넘겨줌).

**타입**: `ui_state_t` — `run_stopped`, `ch1_enabled`/`ch2_enabled`, `measure_mode`,
`ch1_axis`(CH1 엔코더가 V/DIV를 조절할지 공유 T/DIV를 조절할지), `volt_div_idx[2]`(채널별
독립), `time_div_idx`(공유), `measure_menu_idx[2]`/`measure_selected[2]`(채널별 독립),
`trigger_enabled`.

**공개 함수**
- `void ui_init(ui_state_t *ui)` — 초기값 설정(CH1만 켜짐, 1V/div, 10ms/div, 트리거 켜짐 등).
- `void ui_update(ui_state_t *ui, uint16_t pressed, int8_t ch1_delta, int8_t ch2_delta)` —
  버튼 엣지 + 엔코더 델타를 받아 상태 갱신. SW1=RUN/STOP, SW3/SW4=채널 on/off,
  SW2=measure모드, CH1/CH2 엔코더는 모드에 따라 V/DIV·T/DIV·메뉴 이동·확정으로 분기.
- `const char *ui_measure_name(uint8_t idx)` — measure 항목 이름(PROGMEM 포인터 리턴,
  호출부는 `lcd_draw_string_P`/`strncpy_P`로 읽어야 함).

**PROGMEM 데이터**: `MEASURE_NAMES`(RMS/MAX/MIN/FFT/FREQ/PERIOD), `VOLT_PER_DIV_MV`,
`TIME_PER_DIV_US` — 전부 배열 자체와 내용물 모두 플래시에 둠.

---

## grid.c / grid.h

오실로스코프 격자(그리드) + 화면 분할(전체화면/CH1-CH2 분할) 영역 정의.

**타입**: `scope_region_t` — `y0`,`y1`(세로 픽셀 범위), `v_divs`(세로 칸 수).

**전역 상수 (extern const)**: `REGION_FULL`, `REGION_CH1_HALF`, `REGION_CH2_HALF` — 미리
계산해둔 레이아웃.

**공개 함수**
- `void grid_draw_region(const scope_region_t *r)` — 영역을 지우고 격자선 전체를 그림
  (가로/세로 중앙 기준선은 더 밝게).
- `void grid_redraw_rect(const scope_region_t *r, uint16_t x0, uint16_t y0, uint16_t x1,
  uint16_t y1)` — 지정 사각형 범위와 겹치는 격자선만 다시 그림(파형 지우기가 격자까지
  같이 지워버리지 않게 복원하는 용도).
- `void grid_draw_divider(void)` — 분할화면에서 CH1/CH2 사이 빨간 구분띠.

**내부(static) 함수**: `vline_x`(세로선 x좌표, 화면 전체 폭 기준) / `hline_y`(가로선
y좌표, region의 y0..y1을 v_divs 칸으로 분할).

---

## display.c / display.h

ILI9488(480x320, 18bit/SPI) LCD 구동 드라이버.

**공개 함수**
- `void lcd_init(void)` — 하드웨어 리셋 + 초기화 커맨드 시퀀스(픽셀 포맷, 화면 방향/BGR
  설정, 디스플레이 반전 ON, Display ON).
- `void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)` — 컬럼/페이지
  주소 설정(이후 픽셀 데이터가 이 영역에 쓰임).
- `void lcd_fill_rect(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t r, uint8_t
  g, uint8_t b)` — 사각형을 단색으로 채움. 이 프로젝트의 거의 모든 그리기(선, 격자, 텍스트
  글자, 막대그래프)가 결국 이 함수 호출로 귀결됨.
- `void lcd_fill_screen(uint8_t r, uint8_t g, uint8_t b)` / `void lcd_draw_pixel(uint16_t x,
  uint16_t y, uint8_t r, uint8_t g, uint8_t b)`

**색상 상수**: `COLOR_BLACK/WHITE/GREEN/YELLOW/CYAN/RED/NAVY/GRID/LTGREY/DKGREY` — r,g,b
세 값으로 확장되는 매크로.

---

## text.c / text.h

5x7 비트맵 폰트로 LCD에 문자/문자열 그리기.

- `void lcd_draw_char(uint16_t x, uint16_t y, char c, uint8_t r, uint8_t g, uint8_t b)` —
  글자 하나를 5x7 비트맵으로 그림.
- `void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint8_t r, uint8_t g,
  uint8_t b)` — SRAM에 있는 일반 문자열용.
- `void lcd_draw_string_P(uint16_t x, uint16_t y, const char *str_p, uint8_t r, uint8_t g,
  uint8_t b)` — PROGMEM(플래시)에 있는 문자열용(`PSTR()`로 감싼 리터럴을 여기로 넘김).

**내부(static) 함수**: `get_char_idx` — 문자를 `font5x7` 테이블 인덱스로 변환(숫자/영문
대소문자/일부 기호만 지원).

---

## input.c / input.h

`hc165_read()`의 raw 16bit 값을 가공하는 계층.

**타입**: `debounce_state_t`(버튼 디바운스용 상태), `quad_state_t`(엔코더 쿼드러처 디코딩용
상태 — `prev_state`, `accum`).

**공개 함수**
- `uint16_t input_debounce(debounce_state_t *st, uint16_t raw)` — 2번 연속 같은 값이어야
  "확정"으로 인정.
- `uint16_t input_pressed_edges(uint16_t prev_stable, uint16_t curr_stable)` — 눌리는
  순간(0→1)만 골라냄.
- `int8_t input_quad_decode(quad_state_t *st, uint16_t stable, uint16_t clk_mask, uint16_t
  dt_mask)` — 표준 그레이코드 전이표(`QUAD_TRANSITION`, PROGMEM) 방식으로 CLK/DT 상태
  변화를 누적해서 회전 방향 판별. 폴링 타이밍이 불규칙해도(그리기 도중 콜백으로 호출되므로)
  정확하게 동작하도록 설계됨 — 디텐트 한 칸(그레이코드 한 바퀴, 4스텝)이 다 쌓여야 ±1 리턴.

---

## hc165.c / hc165.h

74HC165 시프트레지스터 2개(U4, U5)를 SPI로 읽어서 버튼 6개 + 엔코더 2개(CLK/DT)를
16비트 값 하나로 반환.

- `void hc165_init(void)` — PL(parallel load) 핀을 출력으로 설정, idle HIGH.
- `uint16_t hc165_read(void)` — PL을 LOW→HIGH 펄스로 래치한 뒤 U5, U4 순서로 8비트씩
  SPI로 읽어서 16비트로 합침(`bits[15:8]=U5`, `bits[7:0]=U4`).

**비트 매핑**: `HC165_SW1~4`(버튼), `HC165_CH1_SW`/`HC165_CH2_SW`(엔코더 누름버튼),
`HC165_CH1_CLK`/`HC165_CH1_DT`/`HC165_CH2_CLK`/`HC165_CH2_DT`(엔코더 회전).

---

## spi.c / spi.h

ATmega128 하드웨어 SPI 마스터 드라이버(Mode 0, F_CPU/4). `hc165.c`(읽기)와 `display.c`
(쓰기)가 공유해서 쓴다.

- `void spi_init(void)` — SS/SCK/MOSI 출력, MISO 입력, SPI 마스터 모드 활성화.
- `uint8_t spi_transfer(uint8_t data)` — 1바이트 송수신(블로킹).

---

## millis.c / millis.h

Timer0으로 1ms마다 증가하는 카운터. 롱프레스(홀드시간) 판정, 화면 갱신 속도 제한 등에 씀.

- `void millis_init(void)` — Timer0 CTC 모드, 1ms마다 인터럽트.
- `uint32_t millis(void)` — 현재까지 경과한 ms(인터럽트 중 읽기 안전하게 `cli()`/`sei()`로
  감쌈).
- `ISR(TIMER0_COMP_vect)` — 카운터 증가.
