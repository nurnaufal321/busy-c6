#include "lcd.h"

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
#define PIC_W (LED_W * LCD_PITCH)
#define PIC_H (LED_H * LCD_PITCH)
#define PIC_X ((SCREEN_W - PIC_W) / 2)
#define PIC_Y ((SCREEN_H - PIC_H) / 2 + ROW_OFFSET)
#define SPI_HZ (40 * 1000 * 1000)
#define BACKLIGHT_PERCENT 60

// Rotating 180 degrees reverses both directions: MX|MV|ML, then MY|MV.
#define MADCTL_LANDSCAPE 0x70
#define MADCTL_FLIPPED 0xA0

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;
static uint16_t *s_pixels;

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

static void send(int bytes)
{
    xSemaphoreTake(s_done, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_pixels, bytes));
}

static void clear_screen(void)
{
    memset(s_pixels, 0, PIC_W * PIC_H * 2);
    for (int y = 0; y < SCREEN_H; y += PIC_H) {
        int h = SCREEN_H - y < PIC_H ? SCREEN_H - y : PIC_H;
        xSemaphoreTake(s_done, portMAX_DELAY);
        xSemaphoreGive(s_done);
        set_window(0, y + ROW_OFFSET, PIC_W, h);
        send(PIC_W * h * 2);
        xSemaphoreTake(s_done, portMAX_DELAY);
        xSemaphoreGive(s_done);
        set_window(PIC_W, y + ROW_OFFSET, SCREEN_W - PIC_W, h);
        send((SCREEN_W - PIC_W) * h * 2);
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
    s_pixels = heap_caps_malloc(PIC_W * PIC_H * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_pixels);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_LCD_MOSI,
        .sclk_io_num = PIN_LCD_CLK,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PIC_W * PIC_H * 2 + 16,
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
    clear_screen();
    backlight_init();
}

void lcd_set_flipped(bool flipped)
{
    xSemaphoreTake(s_done, portMAX_DELAY);
    uint8_t mad = flipped ? MADCTL_FLIPPED : MADCTL_LANDSCAPE;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x36, &mad, 1));
    xSemaphoreGive(s_done);
}

void lcd_draw(const uint16_t *leds, int shift)
{
    xSemaphoreTake(s_done, portMAX_DELAY);
    uint16_t *out = s_pixels;
    for (int row = 0; row < LED_H; row++) {
        uint16_t *first = out;
        for (int col = 0; col < LED_W; col++) {
            int src = col - shift;
            uint16_t c = src >= 0 && src < LED_W ? leds[row * LED_W + src] : 0;
            // The panel takes RGB565 high byte first.
            c = (uint16_t)(c << 8 | c >> 8);
            *out++ = c;
            *out++ = c;
            *out++ = c;
            *out++ = 0;
        }
        // 3 lit lines, 1 dark.
        memcpy(out, first, PIC_W * 2);
        out += PIC_W;
        memcpy(out, first, PIC_W * 2);
        out += PIC_W;
        memset(out, 0, PIC_W * 2);
        out += PIC_W;
    }
    set_window(PIC_X, PIC_Y, PIC_W, PIC_H);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_pixels, PIC_W * PIC_H * 2));
}
