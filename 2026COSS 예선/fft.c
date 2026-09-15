#include <avr/pgmspace.h>
#include "fft.h"
#include "calib.h" // calib_raw_to_mv()

#define FFT_STAGES 6 // log2(FFT_N) = log2(64)

// W_N^k = cos(-2*pi*k/N) + j*sin(-2*pi*k/N), k=0..FFT_N/2-1, Q15 고정소수점(±32767=±1.0).
// FFT_N=64 기준으로 미리 계산해둔 값 - PROGMEM(플래시)에 둬서 SRAM을 안 먹게 함.
static const int16_t FFT_COS[FFT_BINS] PROGMEM = {
    32767, 32609, 32137, 31356, 30273, 28898, 27245, 25329,
    23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
    0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609
};
static const int16_t FFT_SIN[FFT_BINS] PROGMEM = {
    0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
    -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609,
    -32767, -32609, -32137, -31356, -30273, -28898, -27245, -25329,
    -23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212
};

// CH1/CH2 공용 작업버퍼(한 번에 한 채널만 계산 - fft.h 주석 참고)
static int16_t work_re[FFT_N];
static int16_t work_im[FFT_N];

static inline int16_t q15_mul(int16_t a, int16_t b)
{
    return (int16_t)(((int32_t)a * (int32_t)b) >> 15);
}

static void bit_reverse(void)
{
    uint8_t j = 0;
    for (uint8_t i = 0; i < FFT_N - 1; i++) {
        if (i < j) {
            int16_t tr = work_re[i]; work_re[i] = work_re[j]; work_re[j] = tr;
            int16_t ti = work_im[i]; work_im[i] = work_im[j]; work_im[j] = ti;
        }
        uint8_t m = FFT_N >> 1;
        while (m >= 1 && (j & m)) {
            j &= (uint8_t)~m;
            m >>= 1;
        }
        j |= m;
    }
}

// 정수 제곱근(이진 탐색) - statusbar.c의 isqrt32()와 같은 방식, 크기 진폭 계산용
static uint32_t isqrt32(uint32_t v)
{
    uint32_t res = 0;
    uint32_t bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

void fft_compute_magnitude(uint8_t ch_idx, const volatile uint16_t *adc_buf, uint16_t *mag_out)
{
    for (uint8_t i = 0; i < FFT_N; i++) {
        work_re[i] = (int16_t)calib_raw_to_mv(ch_idx, adc_buf[i]); // 대략 -5000~5000 범위, int16 여유있게 들어감
        work_im[i] = 0;
    }

    bit_reverse();

    // 반복형 Cooley-Tukey(DIT). 스테이지마다 결과를 절반으로 스케일해서(>>1) 오버플로 방지 -
    // 버터플라이 덧셈이 매 스테이지 최대 2배까지 커질 수 있는데 이러면 6스테이지 누적시
    // int16 범위를 넘어감. 절댓값이 아니라 "상대적 진폭"만 필요해서 스케일 손실은 무해함.
    for (uint8_t stage = 0; stage < FFT_STAGES; stage++) {
        uint8_t half_size = (uint8_t)(1 << stage);
        uint8_t tw_step = (uint8_t)(FFT_BINS >> stage); // 트위들 인덱스 간격

        for (uint8_t start = 0; start < FFT_N; start = (uint8_t)(start + (half_size << 1))) {
            for (uint8_t k = 0; k < half_size; k++) {
                uint8_t idx = (uint8_t)(start + k);
                uint8_t pair = (uint8_t)(idx + half_size);
                uint8_t tw_idx = (uint8_t)(k * tw_step);

                int16_t wc = (int16_t)pgm_read_word(&FFT_COS[tw_idx]);
                int16_t ws = (int16_t)pgm_read_word(&FFT_SIN[tw_idx]);

                int16_t tre = (int16_t)(q15_mul(work_re[pair], wc) - q15_mul(work_im[pair], ws));
                int16_t tim = (int16_t)(q15_mul(work_re[pair], ws) + q15_mul(work_im[pair], wc));

                int16_t ure = work_re[idx];
                int16_t uim = work_im[idx];

                work_re[idx]  = (int16_t)((ure + tre) >> 1);
                work_im[idx]  = (int16_t)((uim + tim) >> 1);
                work_re[pair] = (int16_t)((ure - tre) >> 1);
                work_im[pair] = (int16_t)((uim - tim) >> 1);
            }
        }
    }

    for (uint8_t k = 0; k < FFT_BINS; k++) {
        int32_t r = work_re[k];
        int32_t im = work_im[k];
        mag_out[k] = (uint16_t)isqrt32((uint32_t)(r * r + im * im));
    }
}
