#include "lcd.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SCREEN_W 320
#define SCREEN_H 170
// The 170-pixel side sits in the middle of the controller's 240 rows, so this holds when flipped.
#define ROW_OFFSET 35
#define SCREEN_BYTES (SCREEN_W * SCREEN_H * 2)
// Original: the picture's position on the screen.
#define PIC_X ((SCREEN_W - LED_W * 4) / 2)
#define PIC_Y ((SCREEN_H - LED_H * 4) / 2)
// Pan: LED pitch.
#define PAN_PITCH 10
// Tall and Glow: a full-screen grid of even 4 px dots.
#define DOTS_W 80
#define DOTS_H 42
// Tall: its own finer grid of 3 px dots (2 lit, 1 dark), the black border around the picture and
// the corner radius, in dots.
#define TALL_PITCH 3
#define TALL_GRID_W (SCREEN_W / TALL_PITCH)
#define TALL_GRID_H (SCREEN_H / TALL_PITCH)
#define TALL_BORDER 3
#define TALL_W (TALL_GRID_W - 2 * TALL_BORDER)
#define TALL_H (TALL_GRID_H - 2 * TALL_BORDER)
#define TALL_RADIUS 13
// Glow: the picture's position in the grid.
#define GLOW_X 4
#define GLOW_Y 13
#define GLOW_REACH 14
#define SPI_HZ (40 * 1000 * 1000)
#define BACKLIGHT_PERCENT 60

// Rotating 180 degrees reverses both directions: MX|MV|ML, then MY|MV.
#define MADCTL_LANDSCAPE 0x70
#define MADCTL_FLIPPED 0xA0

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint16_t *s_pixels;
// The LED each screen column and row shows, or -1 for a dark gap.
static int8_t s_cols[LAYOUT_COUNT][SCREEN_W];
static int8_t s_rows[LAYOUT_COUNT][SCREEN_H];
// Tall: where each dot samples the picture: the two nearest LEDs and the weight of the second, out
// of 256.
typedef struct {
    uint8_t a, b;
    uint16_t weight;
} sample_t;
static sample_t s_tall_x[TALL_W];
static sample_t s_tall_y[TALL_H];
// Tall: dots left dark at each end of a row, which rounds the corners.
static uint8_t s_tall_inset[TALL_H];
// Glow: brightness out of 256 at each distance from the picture.
static uint16_t s_fade[GLOW_REACH];

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[14];
    uint16_t delay_ms;
} init_cmd_t;

// Waveshare's panel setup (08_FactoryProgram), plus RGB565.
static const init_cmd_t INIT[] = {
    {0x36, 1, {MADCTL_LANDSCAPE}, 0},
    {0x3A, 1, {0x55}, 0},
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0},
    {0xB7, 1, {0x35}, 0},
    {0xBB, 1, {0x13}, 0},
    {0xC0, 1, {0x2C}, 0},
    {0xC2, 1, {0x01}, 0},
    {0xC3, 1, {0x0B}, 0},
    {0xC4, 1, {0x20}, 0},
    {0xC6, 1, {0x0F}, 0},
    {0xD0, 2, {0xA4, 0xA1}, 0},
    {0xD6, 1, {0xA1}, 0},
    {0xE0, 14, {0x00, 0x03, 0x07, 0x08, 0x07, 0x15, 0x2A, 0x44, 0x42, 0x0A, 0x17, 0x18, 0x25, 0x27}, 0},
    {0xE1, 14, {0x00, 0x03, 0x08, 0x07, 0x07, 0x23, 0x2A, 0x43, 0x42, 0x09, 0x18, 0x17, 0x25, 0x27}, 0},
    {0x21, 0, {0}, 0},
    {0x11, 0, {0}, 120},
    {0x29, 0, {0}, 0},
};

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void set_window(int x, int y, int w, int h)
{
    uint8_t col[] = {x >> 8, x & 0xFF, (x + w - 1) >> 8, (x + w - 1) & 0xFF};
    uint8_t row[] = {y >> 8, y & 0xFF, (y + h - 1) >> 8, (y + h - 1) & 0xFF};
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x2A, col, sizeof(col)));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x2B, row, sizeof(row)));
}

/** Fills the tables for cells that stretch [leds] LEDs over [size] pixels, with a 1 px gap. */
static void stretch(int8_t *table, int size, int leds)
{
    for (int led = 0; led < leds; led++) {
        int from = led * size / leds, to = (led + 1) * size / leds;
        for (int i = from; i < to; i++) table[i] = i == to - 1 ? -1 : led;
    }
}

/**
 * Samples for [dots] dots across [leds] LEDs. The blend is a smoothstep rather than a straight
 * line, so edges in the picture stay crisp instead of going soft.
 */
static void build_samples(sample_t *table, int dots, int leds)
{
    for (int i = 0; i < dots; i++) {
        float pos = (i + 0.5f) * leds / dots - 0.5f;
        if (pos < 0) pos = 0;
        int a = (int)pos;
        int b = a + 1 < leds ? a + 1 : a;
        float t = pos - a;
        t = t * t * (3 - 2 * t);
        table[i] = (sample_t){.a = a, .b = b, .weight = (uint16_t)(t * 256)};
    }
}

static void build_tables(void)
{
    memset(s_cols, -1, sizeof(s_cols));
    memset(s_rows, -1, sizeof(s_rows));
    for (int led = 0; led < LED_W; led++) {
        for (int i = 0; i < 3; i++) s_cols[LAYOUT_ORIGINAL][PIC_X + led * 4 + i] = led;
    }
    for (int led = 0; led < LED_H; led++) {
        for (int i = 0; i < 3; i++) s_rows[LAYOUT_ORIGINAL][PIC_Y + led * 4 + i] = led;
    }
    build_samples(s_tall_x, TALL_W, LED_W);
    build_samples(s_tall_y, TALL_H, LED_H);
    for (int i = 0; i < TALL_RADIUS; i++) {
        float dy = TALL_RADIUS - i - 0.5f;
        int inset = (int)(TALL_RADIUS - sqrtf(TALL_RADIUS * TALL_RADIUS - dy * dy) + 0.5f);
        s_tall_inset[i] = inset;
        s_tall_inset[TALL_H - 1 - i] = inset;
    }
    stretch(s_rows[LAYOUT_PAN], SCREEN_H, LED_H);
    for (int d = 0; d < GLOW_REACH; d++) s_fade[d] = d == 0 ? 256 : 141 * (GLOW_REACH - d) / GLOW_REACH;
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50 * 1000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        // The demo's duty values are inverted: 0 is brightest.
        .duty = 255 - 255 * BACKLIGHT_PERCENT / 100,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void lcd_init(void)
{
    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);
    s_pixels = heap_caps_calloc(1, SCREEN_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_pixels);
    build_tables();

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI,
        .sclk_io_num = PIN_LCD_CLK,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SCREEN_BYTES + 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &s_io));

    gpio_set_direction(PIN_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < sizeof(INIT) / sizeof(INIT[0]); i++) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, INIT[i].cmd, INIT[i].data, INIT[i].len));
        if (INIT[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(INIT[i].delay_ms));
    }
    set_window(0, ROW_OFFSET, SCREEN_W, SCREEN_H);
    backlight_init();
}

void lcd_set_flipped(bool flipped)
{
    xSemaphoreTake(s_done, portMAX_DELAY);
    uint8_t mad = flipped ? MADCTL_FLIPPED : MADCTL_LANDSCAPE;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x36, &mad, 1));
    xSemaphoreGive(s_done);
}

// The panel takes RGB565 high byte first.
static inline uint16_t to_panel(uint16_t c)
{
    return (uint16_t)(c << 8 | c >> 8);
}

/** Draws the layouts that are just a grid: each screen pixel shows one LED or a gap. */
static void draw_cells(const uint16_t *leds, int shift, layout_t layout, int pan)
{
    const int8_t *cols = s_cols[layout];
    const int8_t *rows = s_rows[layout];
    uint16_t *out = s_pixels;
    int prev = -2;
    for (int y = 0; y < SCREEN_H; y++, out += SCREEN_W) {
        int row = rows[y];
        if (row == prev) {
            // Same LEDs as the line above.
            memcpy(out, out - SCREEN_W, SCREEN_W * 2);
            continue;
        }
        prev = row;
        if (row < 0) {
            memset(out, 0, SCREEN_W * 2);
            continue;
        }
        const uint16_t *line = &leds[row * LED_W];
        for (int x = 0; x < SCREEN_W; x++) {
            int col;
            if (layout == LAYOUT_PAN) {
                int p = x + pan;
                col = p % PAN_PITCH == PAN_PITCH - 1 ? -1 : p / PAN_PITCH;
            } else {
                col = cols[x];
            }
            int src = col - shift;
            out[x] = col >= 0 && src >= 0 && src < LED_W ? to_panel(line[src]) : 0;
        }
    }
}

/** Scales an RGB565 colour by [level] out of 256. */
static inline uint16_t dim(uint16_t c, uint32_t level)
{
    uint32_t r = (c >> 11) * level >> 8;
    uint32_t g = ((c >> 5) & 0x3F) * level >> 8;
    uint32_t b = (c & 0x1F) * level >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

static inline int clamp(int v, int max)
{
    return v < 0 ? 0 : v > max ? max : v;
}

/** The picture's LED at [x], [y], with black beyond the sides while a swipe drags it. */
static inline uint16_t led_at(const uint16_t *leds, int x, int y)
{
    return x >= 0 && x < LED_W ? leds[y * LED_W + x] : 0;
}

/** Blends two RGB565 colours; [w] out of 256 is the share of the second. */
static inline uint16_t blend(uint16_t c0, uint16_t c1, uint32_t w)
{
    if (c0 == c1) return c0;
    uint32_t v = 256 - w;
    uint32_t r = ((c0 >> 11) * v + (c1 >> 11) * w) >> 8;
    uint32_t g = (((c0 >> 5) & 0x3F) * v + ((c1 >> 5) & 0x3F) * w) >> 8;
    uint32_t b = ((c0 & 0x1F) * v + (c1 & 0x1F) * w) >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}

/** A row of dots is 3 lit lines and a dark one; [first] holds the first line already. */
static uint16_t *finish_dot_row(uint16_t *first)
{
    uint16_t *out = first + SCREEN_W;
    memcpy(out, first, SCREEN_W * 2);
    out += SCREEN_W;
    memcpy(out, first, SCREEN_W * 2);
    out += SCREEN_W;
    memset(out, 0, SCREEN_W * 2);
    return out + SCREEN_W;
}

/** The picture resampled onto fine, even dots, inside a black border with round corners. */
static void draw_tall(const uint16_t *leds, int shift)
{
    memset(s_pixels, 0, SCREEN_BYTES);
    int margin_x = (SCREEN_W - TALL_GRID_W * TALL_PITCH + 1) / 2;
    int margin_y = (SCREEN_H - TALL_GRID_H * TALL_PITCH + 1) / 2;
    int drag = shift * TALL_W / LED_W;
    for (int gy = 0; gy < TALL_H; gy++) {
        const sample_t *sy = &s_tall_y[gy];
        uint16_t *line = s_pixels + SCREEN_W * (margin_y + (TALL_BORDER + gy) * TALL_PITCH);
        int inset = s_tall_inset[gy];
        for (int dot = inset; dot < TALL_W - inset; dot++) {
            int src = dot - drag;
            if (src < 0 || src >= TALL_W) continue;
            const sample_t *sx = &s_tall_x[src];
            uint16_t top = blend(led_at(leds, sx->a, sy->a), led_at(leds, sx->b, sy->a), sx->weight);
            uint16_t bottom = blend(led_at(leds, sx->a, sy->b), led_at(leds, sx->b, sy->b), sx->weight);
            uint16_t c = to_panel(blend(top, bottom, sy->weight));
            uint16_t *px = line + margin_x + (TALL_BORDER + dot) * TALL_PITCH;
            px[0] = c;
            px[1] = c;
        }
        memcpy(line + SCREEN_W, line, SCREEN_W * 2);
    }
}

static void draw_glow(const uint16_t *leds, int shift)
{
    uint16_t *out = s_pixels;
    // 42 dots of 4 px leave one dark line above and below.
    memset(out, 0, SCREEN_W * 2);
    out += SCREEN_W;
    for (int gy = 0; gy < DOTS_H; gy++) {
        uint16_t *first = out;
        int py = gy - GLOW_Y;
        int dy = py < 0 ? -py : py >= LED_H ? py - (LED_H - 1) : 0;
        const uint16_t *line = &leds[clamp(py, LED_H - 1) * LED_W];
        for (int gx = 0; gx < DOTS_W; gx++) {
            int px = gx - GLOW_X - shift;
            int dx = px < 0 ? -px : px >= LED_W ? px - (LED_W - 1) : 0;
            int d = dx > dy ? dx : dy;
            uint16_t c = 0;
            if (d < GLOW_REACH) c = to_panel(d == 0 ? line[px] : dim(line[clamp(px, LED_W - 1)], s_fade[d]));
            *out++ = c;
            *out++ = c;
            *out++ = c;
            *out++ = 0;
        }
        out = finish_dot_row(first);
    }
}

void lcd_draw(const uint16_t *leds, int shift, layout_t layout, int pan)
{
    xSemaphoreTake(s_done, portMAX_DELAY);
    if (layout == LAYOUT_GLOW) {
        draw_glow(leds, shift);
    } else if (layout == LAYOUT_TALL) {
        draw_tall(leds, shift);
    } else {
        draw_cells(leds, shift, layout, pan);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_pixels, SCREEN_BYTES));
}
