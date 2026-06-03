#ifdef RAVLIGHT_FIXTURE_ELYON
#include "core/i2s_parallel_output.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <rom/lldesc.h>
#include <driver/gpio.h>
#include <driver/periph_ctrl.h>
#include <esp_intr_alloc.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <soc/i2s_struct.h>
#include <soc/gpio_sig_map.h>
#include <soc/periph_defs.h>

static const char* TAG = "I2SPAR";

// ── Clock ────────────────────────────────────────────────────────────────────
// LCD-parallel mode: f_pixel = APB_CLK / (CLKM_DIV_NUM × 2 × BCK_DIV_NUM)
// CLKM_DIV_NUM=13, BCK=1 → 80 MHz / 26 ≈ 3.077 MHz → 1 sample = 325 ns
// 4 I2S samples per WS2815 bit (HIGH·DATA·DATA·LOW) → TDATA = 4×325 = 1300 ns ✓
// WS2815 spec: T0H 220–380ns ✓  T1H 580–1600ns ✓  T0L 580–1600ns ✓  T1L 220–420ns ✓  TDATA ≥1250ns ✓
//   T0: 1×325=325ns HIGH + 3×325=975ns LOW   T1: 3×325=975ns HIGH + 1×325=325ns LOW
#define I2S_CLKM_DIV   13
#define I2S_BCK_DIV     1

// ── Sizes ────────────────────────────────────────────────────────────────────
#define SAMPLES_PER_BIT   4      // WS2815 NZR: HIGH·DATA·DATA·LOW → 4 I2S ticks
#define BYTES_PER_SAMPLE  2      // 16-bit parallel → 2 bytes per sample
// Chunk buffer holds I2S_PAR_CHUNK_PX pixels worth of encoded data.
// Use RGBW (4 bytes/px) sizing so the same buffer works for both 3- and 4-byte strips.
#define CHUNK_BUF_BYTES   (I2S_PAR_CHUNK_PX * 4 * 8 * SAMPLES_PER_BIT * BYTES_PER_SAMPLE)
// Reset: WS2815 requires >280 µs low. At 3.077 MHz: 862 samples min → 2048 bytes = 333 µs (19% margin).
#define RESET_BUF_BYTES   2048

// ── Module state ─────────────────────────────────────────────────────────────

// Source pixel buffers (registered via i2s_par_set_source)
static struct {
    const uint8_t* buf;
    uint16_t       n_pixels;
    uint8_t        bytes_pp;
} s_src[I2S_PAR_MAX_CH];

static uint8_t   s_n_ch        = 0;
static uint16_t  s_active_mask = 0;  // bit k = 1 → channel k has a valid source buffer
static uint8_t   s_bytes_pp    = 3;  // max bytes_pp across active channels for current frame

// DMA chunk buffers (triple-buffered, allocated in DMA-capable DRAM)
static uint8_t* s_chunk_buf[I2S_PAR_BUFS];
static uint8_t* s_reset_buf = NULL;

// DMA descriptor pool: one per chunk + 1 reset.
// Allocated at init; rebuilt each trigger_frame.
static lldesc_t* s_desc   = NULL;
static uint16_t  s_n_desc = 0;
static lldesc_t  s_reset_desc;

// Streaming state (written only by encoder_task, read by ISR indirectly via semaphore)
static volatile uint16_t s_cursor  = 0;
static volatile uint16_t s_n_total = 0;

// Synchronisation
static SemaphoreHandle_t s_chunk_sem = NULL; // counting — one credit per data-desc EOF
static SemaphoreHandle_t s_done_sem  = NULL; // binary  — one credit per reset-desc EOF
static intr_handle_t     s_isr_hdl  = NULL;
static TaskHandle_t      s_enc_task = NULL;

// ── ISR (IRAM — must not touch flash) ────────────────────────────────────────

// OUT_EOF_INT is bit 12 of the I2S interrupt register (ESP32 TRM Table 12-11).
#define I2S_OUT_EOF_BIT  (1u << 12)

static void IRAM_ATTR i2s_isr(void* arg) {
    uint32_t raw = I2S0.int_raw.val;
    I2S0.int_clr.val = raw;

    if (!(raw & I2S_OUT_EOF_BIT)) return;

    uint32_t eof_addr = I2S0.out_eof_des_addr;
    BaseType_t woken  = pdFALSE;

    if (eof_addr == (uint32_t)&s_reset_desc) {
        xSemaphoreGiveFromISR(s_done_sem, &woken);
    } else {
        xSemaphoreGiveFromISR(s_chunk_sem, &woken);
    }

    if (woken) portYIELD_FROM_ISR();
}

// ── Encoder ──────────────────────────────────────────────────────────────────

// Encode I2S_PAR_CHUNK_PX pixels starting at pixel offset p0 into dma_buf.
// dma_buf must be at least CHUNK_BUF_BYTES bytes.
// Writes logical sample i at DMA uint16_t slot (i ^ 1) to compensate for
// the ESP32 I2S FIFO byte-swap (first 16-bit sample in FIFO maps to the
// second uint16_t slot of each 4-byte DMA word and vice versa).
static IRAM_ATTR void encode_chunk(uint8_t* dma_buf, uint16_t p0) {
    const uint32_t bits_pp = (uint32_t)s_bytes_pp * 8;

    memset(dma_buf, 0, I2S_PAR_CHUNK_PX * bits_pp * SAMPLES_PER_BIT * BYTES_PER_SAMPLE);

    for (int px = 0; px < I2S_PAR_CHUNK_PX; px++) {
        uint16_t abs_px = p0 + (uint16_t)px;

        for (uint32_t bit = 0; bit < bits_pp; bit++) {
            uint8_t byte_idx = (uint8_t)(bit / 8);
            uint8_t bit_pos  = 7u - (bit % 8);   // MSB first

            // Build channel mask: bit k = 1 iff channel k has this bit set
            uint16_t ch_mask = 0;
            for (uint8_t ch = 0; ch < s_n_ch; ch++) {
                if (!(s_active_mask & (1u << ch))) continue;
                if (abs_px >= s_src[ch].n_pixels)  continue;
                // Clamp byte_idx for RGB channels when s_bytes_pp is 4 (RGBW frame):
                // the 4th byte (W) is omitted for RGB strips → treat as 0 (already cleared)
                if (byte_idx >= s_src[ch].bytes_pp) continue;
                uint8_t v = s_src[ch].buf[(uint32_t)abs_px * s_src[ch].bytes_pp + byte_idx];
                if ((v >> bit_pos) & 1u)
                    ch_mask |= (uint16_t)(1u << ch);
            }

            // 4-sample WS2815 NZR encoding (HIGH·DATA·DATA·LOW):
            //   sample 0: all active channels HIGH  (rising edge for every bit)
            //   sample 1: ch_mask HIGH (data; 0 → LOW for T0, 1 → HIGH extends T1)
            //   sample 2: ch_mask HIGH (same; T0: 1+3=975ns LOW, T1: 3×325=975ns HIGH)
            //   sample 3: all channels LOW  (falling edge, zero → from memset)
            // s0 is always a multiple of 4 → i^1 byte-swap aligns within each 32-bit DMA word.
            uint32_t s0 = (px * bits_pp + bit) * SAMPLES_PER_BIT;
            uint16_t* p = (uint16_t*)dma_buf;
            p[s0 ^ 1]       = s_active_mask; // sample 0: all HIGH
            p[(s0 + 1) ^ 1] = ch_mask;       // sample 1: data
            p[(s0 + 2) ^ 1] = ch_mask;       // sample 2: data (extends T1H / keeps T0L low)
            // sample 3 stays 0 (from memset)
        }
    }
}

// ── Encoder task ─────────────────────────────────────────────────────────────

static void encoder_task(void* arg) {
    for (;;) {
        xSemaphoreTake(s_chunk_sem, portMAX_DELAY);

        uint16_t ci = s_cursor;
        if (ci >= s_n_total) {
            // All data chunks have been encoded; reset descriptor handles the
            // WS2812 reset pulse and its EOF will give s_done_sem.
            continue;
        }

        uint8_t  bi = ci % I2S_PAR_BUFS;
        encode_chunk(s_chunk_buf[bi], (uint16_t)(ci * I2S_PAR_CHUNK_PX));
        // Memory barrier: ensure encode writes are visible to DMA before owner=1.
        __sync_synchronize();
        s_desc[ci].owner = 1;
        s_cursor = ci + 1;
    }
}

// ── I2S hardware init ─────────────────────────────────────────────────────────

static void i2s_hw_init(const i2s_par_cfg_t* cfg) {
    periph_module_enable(PERIPH_I2S0_MODULE);

    // Full reset
    I2S0.conf.tx_reset = 1;      I2S0.conf.tx_reset = 0;
    I2S0.conf.tx_fifo_reset = 1; I2S0.conf.tx_fifo_reset = 0;
    I2S0.lc_conf.in_rst  = 1;   I2S0.lc_conf.in_rst  = 0;
    I2S0.lc_conf.out_rst = 1;   I2S0.lc_conf.out_rst = 0;
    I2S0.lc_conf.ahbm_rst      = 1; I2S0.lc_conf.ahbm_rst      = 0;
    I2S0.lc_conf.ahbm_fifo_rst = 1; I2S0.lc_conf.ahbm_fifo_rst = 0;

    // LCD parallel mode (all 16 data bits driven simultaneously per pixel clock)
    I2S0.conf2.val      = 0;
    I2S0.conf2.lcd_en   = 1;
    I2S0.conf2.camera_en = 0;

    // Pixel clock: APB / (CLKM_DIV × 2 × BCK_DIV) = 80/26 ≈ 3.077 MHz → 325 ns/sample
    I2S0.clkm_conf.val          = 0;
    I2S0.clkm_conf.clkm_div_num = I2S_CLKM_DIV;
    I2S0.clkm_conf.clkm_div_a   = 1;
    I2S0.clkm_conf.clkm_div_b   = 0;
    I2S0.clkm_conf.clka_en      = 0; // APB clock source

    I2S0.sample_rate_conf.val           = 0;
    I2S0.sample_rate_conf.tx_bck_div_num = I2S_BCK_DIV;
    I2S0.sample_rate_conf.tx_bits_mod   = 16; // 16-bit parallel

    // FIFO: 16-bit right-justified mono
    I2S0.fifo_conf.val                 = 0;
    I2S0.fifo_conf.tx_fifo_mod         = 1;
    I2S0.fifo_conf.tx_fifo_mod_force_en = 1;
    I2S0.fifo_conf.tx_data_num         = 32;

    I2S0.conf.val           = 0;
    I2S0.conf.tx_slave_mod  = 0; // master
    I2S0.conf.tx_msb_shift  = 0;
    I2S0.conf.tx_right_first = 1;
    I2S0.conf.tx_mono       = 0;
    I2S0.conf1.val          = 0;
    I2S0.conf1.tx_stop_en   = 0;
    I2S0.conf1.tx_pcm_bypass = 1;

    // DMA burst mode
    I2S0.lc_conf.out_data_burst_en  = 1;
    I2S0.lc_conf.outdscr_burst_en   = 1;

    // Enable only the TX EOF interrupt (bit 12, see I2S_OUT_EOF_BIT)
    I2S0.int_ena.val = 0;
    I2S0.int_ena.val = I2S_OUT_EOF_BIT;

    // Route each active channel to its GPIO via the I2S parallel output signal matrix.
    // Bit k of the 16-bit I2S sample → I2S0O_DATA_OUT{k} signal → gpio_pins[k].
    for (int ch = 0; ch < cfg->n_channels && ch < I2S_PAR_MAX_CH; ch++) {
        int pin = cfg->gpio_pins[ch];
        if (pin < 0) continue;
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        // Drive the pin LOW before routing so no glitch on first frame
        gpio_set_level((gpio_num_t)pin, 0);
        gpio_matrix_out((uint32_t)pin, I2S0O_DATA_OUT0_IDX + ch, false, false);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t i2s_par_init(const i2s_par_cfg_t* cfg) {
    if (!cfg || cfg->n_channels == 0 || cfg->max_pixels_per_ch == 0)
        return ESP_ERR_INVALID_ARG;

    s_n_ch = cfg->n_channels < I2S_PAR_MAX_CH ? cfg->n_channels : I2S_PAR_MAX_CH;
    memset(s_src, 0, sizeof(s_src));
    s_active_mask = 0;

    // Chunk buffers in internal DMA-capable DRAM
    for (int b = 0; b < I2S_PAR_BUFS; b++) {
        s_chunk_buf[b] = (uint8_t*)heap_caps_malloc(CHUNK_BUF_BYTES,
                                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_chunk_buf[b]) {
            ESP_LOGE(TAG, "chunk_buf[%d] alloc failed (%d B)", b, CHUNK_BUF_BYTES);
            return ESP_ERR_NO_MEM;
        }
        memset(s_chunk_buf[b], 0, CHUNK_BUF_BYTES);
    }

    s_reset_buf = (uint8_t*)heap_caps_calloc(1, RESET_BUF_BYTES,
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_reset_buf) { ESP_LOGE(TAG, "reset_buf alloc failed"); return ESP_ERR_NO_MEM; }

    // Descriptor pool: one per chunk + 1 reset guard
    uint16_t max_n = (cfg->max_pixels_per_ch + I2S_PAR_CHUNK_PX - 1) / I2S_PAR_CHUNK_PX;
    s_n_desc = max_n; // reset desc is stored in s_reset_desc (separate)
    s_desc   = (lldesc_t*)heap_caps_calloc(s_n_desc, sizeof(lldesc_t),
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_desc) { ESP_LOGE(TAG, "desc alloc failed (%u descs)", s_n_desc); return ESP_ERR_NO_MEM; }

    // Semaphores
    s_chunk_sem = xSemaphoreCreateCounting(I2S_PAR_BUFS * 4, 0);
    s_done_sem  = xSemaphoreCreateBinary();
    if (!s_chunk_sem || !s_done_sem) return ESP_ERR_NO_MEM;

    i2s_hw_init(cfg);

    // ISR — IRAM so it survives flash cache misses (WiFi/Ethernet activity)
    esp_intr_alloc(ETS_I2S0_INTR_SOURCE,
                   ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3,
                   i2s_isr, NULL, &s_isr_hdl);

    // Encoder task: core 1 (APP_CPU), priority 22 — above render/DMX tasks, below WiFi (23)
    xTaskCreatePinnedToCore(encoder_task, "i2s_enc", 2048, NULL, 22, &s_enc_task, 1);

    ESP_LOGI(TAG, "init: %d ch, max_px=%d, descs=%d, chunk=%d B",
             s_n_ch, cfg->max_pixels_per_ch, s_n_desc, CHUNK_BUF_BYTES);
    return ESP_OK;
}

void i2s_par_set_source(uint8_t ch, const uint8_t* buf, uint16_t n_pixels, uint8_t bytes_pp) {
    if (ch >= s_n_ch) return;
    s_src[ch].buf      = buf;
    s_src[ch].n_pixels = n_pixels;
    s_src[ch].bytes_pp = bytes_pp;
    if (buf && n_pixels > 0)
        s_active_mask |= (uint16_t)(1u << ch);
    else
        s_active_mask &= (uint16_t)~(1u << ch);
}

void i2s_par_trigger_frame(void) {
    if (!s_active_mask) return;

    // Determine frame dimensions
    uint16_t max_px  = 0;
    uint8_t  max_bpp = 3;
    for (int ch = 0; ch < s_n_ch; ch++) {
        if (!(s_active_mask & (1u << ch))) continue;
        if (s_src[ch].n_pixels > max_px)  max_px  = s_src[ch].n_pixels;
        if (s_src[ch].bytes_pp > max_bpp) max_bpp = s_src[ch].bytes_pp;
    }
    if (!max_px) return;

    uint16_t n_total = (uint16_t)((max_px + I2S_PAR_CHUNK_PX - 1) / I2S_PAR_CHUNK_PX);
    if (n_total > s_n_desc) n_total = s_n_desc;

    s_n_total  = n_total;
    s_bytes_pp = max_bpp;

    // Build descriptor chain (all .next pointers, sizes, buf pointers)
    for (int i = 0; i < (int)n_total; i++) {
        s_desc[i].size   = CHUNK_BUF_BYTES;
        s_desc[i].length = CHUNK_BUF_BYTES;
        s_desc[i].offset = 0;
        s_desc[i].sosf   = 0;
        s_desc[i].eof    = 1;
        s_desc[i].owner  = 0; // CPU-owned until encoder fills it
        s_desc[i].buf    = s_chunk_buf[i % I2S_PAR_BUFS];
        s_desc[i].qe.stqe_next = (i < (int)n_total - 1) ? &s_desc[i + 1] : &s_reset_desc;
    }

    // Reset descriptor (zeros → all channels LOW for ≥ 333 µs, well above WS2815 280 µs min)
    s_reset_desc.size   = RESET_BUF_BYTES;
    s_reset_desc.length = RESET_BUF_BYTES;
    s_reset_desc.offset = 0;
    s_reset_desc.sosf   = 0;
    s_reset_desc.eof    = 1;
    s_reset_desc.owner  = 1; // always ready
    s_reset_desc.buf    = s_reset_buf;
    s_reset_desc.qe.stqe_next = NULL; // DMA stops after reset

    // Pre-encode the first I2S_PAR_BUFS chunks (or fewer if strip is very short)
    uint16_t pre = (n_total < I2S_PAR_BUFS) ? n_total : (uint16_t)I2S_PAR_BUFS;
    for (int b = 0; b < (int)pre; b++) {
        encode_chunk(s_chunk_buf[b], (uint16_t)(b * I2S_PAR_CHUNK_PX));
        __sync_synchronize();
        s_desc[b].owner = 1;
    }
    s_cursor = pre; // encoder starts at this chunk index

    // Drain stale semaphore credits from any aborted previous frame
    while (xSemaphoreTake(s_chunk_sem, 0) == pdTRUE) {}
    xSemaphoreTake(s_done_sem, 0);

    // Stop any in-progress DMA, reset FIFO, reload
    I2S0.out_link.stop  = 1;
    I2S0.conf.tx_start  = 0;
    I2S0.conf.tx_reset  = 1; I2S0.conf.tx_reset  = 0;
    I2S0.conf.tx_fifo_reset = 1; I2S0.conf.tx_fifo_reset = 0;
    I2S0.lc_conf.out_rst = 1; I2S0.lc_conf.out_rst = 0;
    I2S0.int_clr.val    = I2S0.int_raw.val;

    I2S0.out_link.addr  = (uint32_t)&s_desc[0];
    I2S0.out_link.start = 1;
    I2S0.conf.tx_start  = 1;
}

void i2s_par_wait_done(void) {
    // Block until the reset-pulse descriptor's EOF ISR fires.
    // Timeout set well beyond any realistic frame duration.
    if (xSemaphoreTake(s_done_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "wait_done timeout — DMA stalled?");
        I2S0.out_link.stop = 1;
        I2S0.conf.tx_start = 0;
    }
}

#endif // RAVLIGHT_FIXTURE_ELYON
