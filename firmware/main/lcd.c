#include "lcd.h"

#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "font.h"
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
// Clock: where the picture's rows start, and the row of text below it.
#define CLOCK_PIC_TOP 4
#define CLOCK_TEXT_X 10
// The status's dots light 4 pixels in every 9, so solid text this bright (out of 256) gives off
// about the same light.
#define CLOCK_TEXT_LEVEL 178
#define CLOCK_TIME_BASELINE 152
#define CLOCK_DATE1_BASELINE 121
#define CLOCK_DATE2_BASELINE 152
#define SPI_HZ (40 * 1000 * 1000)
#define BACKLIGHT_PERCENT 60

// Rotating 180 degrees reverses both directions: MX|MV|ML, then MY|MV.
#define MADCTL_LANDSCAPE 0x70
#define MADCTL_FLIPPED 0xA0

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint16_t *s_pixels;
// The LED each screen column and row shows in Original, or -1 for a dark gap.
static int8_t s_cols[SCREEN_W];
static int8_t s_rows[SCREEN_H];

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

static void build_tables(void)
{
    memset(s_cols, -1, sizeof(s_cols));
    memset(s_rows, -1, sizeof(s_rows));
    for (int led = 0; led < LED_W; led++) {
        for (int i = 0; i < 3; i++) s_cols[PIC_X + led * 4 + i] = led;
    }
    for (int led = 0; led < LED_H; led++) {
        for (int i = 0; i < 3; i++) s_rows[PIC_Y + led * 4 + i] = led;
    }
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

/** Original: each screen pixel shows one LED or a gap. */
static void draw_original(const uint16_t *leds, int shift, int ox, int oy)
{
    uint16_t *out = s_pixels;
    int prev = -2;
    for (int y = 0; y < SCREEN_H; y++, out += SCREEN_W) {
        int row = y >= oy ? s_rows[y - oy] : -1;
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
            int col = x >= ox ? s_cols[x - ox] : -1;
            int src = col - shift;
            out[x] = col >= 0 && src >= 0 && src < LED_W ? to_panel(line[src]) : 0;
        }
    }
}

static const glyph_t *find_glyph(const font_t *font, char c)
{
    for (int i = 0; i < font->count; i++) {
        if (font->glyphs[i].c == c) return &font->glyphs[i];
    }
    return NULL;
}

/** Draws white text with its baseline at [y]; returns the pen position after it. */
static int draw_text(const font_t *font, const char *text, int x, int y, char skip)
{
    for (; *text; text++) {
        const glyph_t *g = find_glyph(font, *text);
        if (!g) continue;
        if (*text != skip) {
            const uint8_t *bits = font->bits + g->offset;
            for (int gy = 0; gy < g->h; gy++) {
                int py = y - g->top + gy;
                if (py < 0 || py >= SCREEN_H) continue;
                uint16_t *line = s_pixels + py * SCREEN_W;
                for (int gx = 0; gx < g->w; gx++) {
                    int px = x + g->left + gx;
                    uint8_t a = bits[gy * g->w + gx];
                    if (a == 0 || px < 0 || px >= SCREEN_W) continue;
                    uint32_t v = a * CLOCK_TEXT_LEVEL >> 8;
                    uint16_t c = (uint16_t)((v >> 3) << 11 | (v >> 2) << 5 | v >> 3);
                    line[px] = to_panel(c);
                }
            }
        }
        x += g->advance;
    }
    return x;
}

static int text_width(const font_t *font, const char *text)
{
    int w = 0;
    for (; *text; text++) {
        const glyph_t *g = find_glyph(font, *text);
        if (g) w += g->advance;
    }
    return w;
}

/** The picture exactly as in Original, moved to the top, with the date and time below. */
static void draw_clock(const uint16_t *leds, int shift, int ox, int oy, const lcd_clock_t *clock)
{
    memset(s_pixels, 0, SCREEN_BYTES);
    for (int row = 0; row < LED_H; row++) {
        uint16_t *line = s_pixels + SCREEN_W * (CLOCK_PIC_TOP + oy + row * 4);
        for (int col = 0; col < LED_W; col++) {
            int src = col - shift;
            if (src < 0 || src >= LED_W) continue;
            uint16_t c = to_panel(leds[row * LED_W + src]);
            uint16_t *px = line + PIC_X + ox + col * 4;
            px[0] = c;
            px[1] = c;
            px[2] = c;
        }
        // 3 lit lines, 1 dark.
        memcpy(line + SCREEN_W, line, SCREEN_W * 2);
        memcpy(line + 2 * SCREEN_W, line, SCREEN_W * 2);
    }
    draw_text(&font_date, clock->day, CLOCK_TEXT_X, CLOCK_DATE1_BASELINE, 0);
    draw_text(&font_date, clock->date, CLOCK_TEXT_X, CLOCK_DATE2_BASELINE, 0);
    int time_x = SCREEN_W - CLOCK_TEXT_X - text_width(&font_time, clock->time);
    draw_text(&font_time, clock->time, time_x, CLOCK_TIME_BASELINE, clock->colon ? 0 : ':');
}

void lcd_draw(const uint16_t *leds, int shift, layout_t layout, int half, const lcd_clock_t *clock)
{
    // Half an LED is 2 of the 4 pixels between LEDs.
    int ox = half & 1 ? 2 : 0, oy = half & 2 ? 2 : 0;
    xSemaphoreTake(s_done, portMAX_DELAY);
    if (layout == LAYOUT_CLOCK) {
        draw_clock(leds, shift, ox, oy, clock);
    } else {
        draw_original(leds, shift, ox, oy);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_pixels, SCREEN_BYTES));
}
