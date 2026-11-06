#include "touch.h"

#include <stdint.h>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

static const char *TAG = "touch";
static i2c_master_dev_handle_t s_dev;
static bool s_down;
static int s_start;
static int s_last;

void touch_init(void)
{
    i2c_device_config_t cfg = {.device_address = I2C_ADDR_TOUCH, .scl_speed_hz = 200000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_i2c_bus, &cfg, &s_dev));
    // Normal mode. The chip can take a moment to answer after power-up.
    uint8_t cmd[] = {0x00, 0x00};
    for (int i = 0; i < 10; i++) {
        if (i2c_master_transmit(s_dev, cmd, sizeof(cmd), 50) == ESP_OK) return;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGW(TAG, "no answer from the touch panel");
}

touch_state_t touch_poll(void)
{
    touch_state_t st = {0};
    uint8_t reg = 0, d[7];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), 20) != ESP_OK) {
        // Keep the last state; a missed read shouldn't end a swipe.
        st.down = s_down;
        st.travel = s_last - s_start;
        return st;
    }
    bool down = d[2] != 0;
    int y = (d[5] & 0x0F) << 8 | d[6];  // the panel's y runs along the long side
    if (down) {
        if (!s_down) s_start = y;
        s_last = y;
    } else if (s_down) {
        st.released = true;
    }
    s_down = down;
    st.down = down;
    st.travel = s_last - s_start;
    return st;
}
