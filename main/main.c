// BUSY status display for the Waveshare ESP32-C6-Touch-LCD-1.9: the bar's status animations, swipe
// left and right to change, flips when the board is turned over.
#include <stdlib.h>


#include "anim.h"
#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "imu.h"
#include "lcd.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "touch.h"

#define SWIPE_MIN_PX 40
#define TOUCH_PERIOD_US (20 * 1000)
#define IMU_PERIOD_US (100 * 1000)
#define SAVE_DELAY_US (2 * 1000 * 1000)
#define NVS_NS "status"
#define NVS_KEY "current"

// The touch panel's travel runs the same way as the picture's x, measured on the board.
#define TOUCH_TO_SCREEN 1
// The animations are 60 fps; drawing faster only uses power.
#define FRAME_PERIOD_US (1000000 / 60)

static const char *TAG = "busy";
i2c_master_bus_handle_t g_i2c_bus;

static int load_status(int count)
{
    nvs_handle_t h;
    int32_t v = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, NVS_KEY, &v);
        nvs_close(h);
    }
    return v >= 0 && v < count ? v : 0;
}

static void save_status(int status)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, NVS_KEY, status);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved %d", status);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &g_i2c_bus));

    lcd_init();
    touch_init();
    imu_init();

    int count = anim_init();
    if (count == 0) {
        ESP_LOGE(TAG, "no statuses: run tools/pack_status.py, then idf.py flash");
        return;
    }
    int status = load_status(count);
    anim_select(status, esp_timer_get_time());
    ESP_LOGI(TAG, "%d statuses, showing %s", count, anim_name(status));

    bool flipped = false;
    int shift = 0;
    int64_t next_touch = 0, next_imu = 0, save_at = 0;
    int64_t fps_since = esp_timer_get_time();
    int frames = 0;
    int64_t next_frame = 0;

    while (true) {
        int64_t now = esp_timer_get_time();

        if (now >= next_imu) {
            next_imu = now + IMU_PERIOD_US;
            bool f = imu_flipped();
            if (f != flipped) {
                flipped = f;
                lcd_set_flipped(flipped);
                ESP_LOGI(TAG, "flipped: %d", flipped);
            }
        }

        if (now >= next_touch) {
            next_touch = now + TOUCH_PERIOD_US;
            touch_state_t t = touch_poll();
            // The screen's x, as the user sees it, whichever way up the board is.
            int dx = t.travel * TOUCH_TO_SCREEN * (flipped ? -1 : 1);
            shift = t.down ? dx / LCD_PITCH : 0;
            if (t.released && abs(dx) >= SWIPE_MIN_PX) {
                // Swiping left brings the next status in, as on the S8+.
                status = (status + (dx < 0 ? 1 : -1) + count) % count;
                anim_select(status, now);
                save_at = now + SAVE_DELAY_US;
                ESP_LOGI(TAG, "swipe dx=%d -> %s", dx, anim_name(status));
            }
        }

        if (save_at && now >= save_at) {
            save_at = 0;
            save_status(status);
        }

        if (now >= next_frame) {
            next_frame = now + FRAME_PERIOD_US;
            lcd_draw(anim_update(now), shift);
            if (++frames == 300) {
                ESP_LOGI(TAG, "%.1f fps", frames * 1e6 / (now - fps_since));
                fps_since = now;
                frames = 0;
            }
        }
        // Lets the idle task run, so the watchdog stays fed.
        vTaskDelay(1);
    }
}
