#include "imu.h"

#include <stdint.h>

#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus.h"

#define REG_WHO_AM_I 0x00
#define REG_CTRL1 0x02
#define REG_CTRL2 0x03
#define REG_CTRL7 0x08
#define REG_AX_L 0x35

// +-2 g full scale.
#define ONE_G 16384
#define TILT_THRESHOLD (ONE_G / 2)
#define HOLD_US (400 * 1000)

// Gravity along the screen's short side: about -1 g right way up, +1 g upside down.
#define UP_AXIS 0
#define UP_SIGN 1

static const char *TAG = "imu";
static i2c_master_dev_handle_t s_dev;
static bool s_ok;
static bool s_flipped;
static int64_t s_pending_since;
static int64_t s_last_log;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t b[] = {reg, value};
    return i2c_master_transmit(s_dev, b, sizeof(b), 50);
}

void imu_init(void)
{
    i2c_device_config_t cfg = {.device_address = I2C_ADDR_IMU, .scl_speed_hz = 200000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(g_i2c_bus, &cfg, &s_dev));
    uint8_t reg = REG_WHO_AM_I, id = 0;
    if (i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1, 50) != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "no QMI8658 (id 0x%02x); auto-rotate is off", id);
        return;
    }
    write_reg(REG_CTRL1, 0x40);  // address auto-increment, little endian
    write_reg(REG_CTRL2, 0x06);  // accelerometer: +-2 g, about 117 Hz
    write_reg(REG_CTRL7, 0x01);  // accelerometer on
    s_ok = true;
}

bool imu_flipped(void)
{
    if (!s_ok) return false;
    uint8_t reg = REG_AX_L, d[6];
    if (i2c_master_transmit_receive(s_dev, &reg, 1, d, sizeof(d), 20) != ESP_OK) return s_flipped;
    int16_t a[3];
    for (int i = 0; i < 3; i++) a[i] = (int16_t)(d[i * 2] | d[i * 2 + 1] << 8);

    int64_t now = esp_timer_get_time();
    if (now - s_last_log > 2000000) {
        ESP_LOGI(TAG, "accel x=%d y=%d z=%d", a[0], a[1], a[2]);
        s_last_log = now;
    }

    int g = a[UP_AXIS] * UP_SIGN;
    bool wants = g > TILT_THRESHOLD ? true : g < -TILT_THRESHOLD ? false : s_flipped;
    if (wants == s_flipped) {
        s_pending_since = 0;
    } else if (s_pending_since == 0) {
        s_pending_since = now;
    } else if (now - s_pending_since > HOLD_US) {
        s_flipped = wants;
        s_pending_since = 0;
    }
    return s_flipped;
}
