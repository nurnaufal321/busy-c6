#pragma once
#include "driver/i2c_master.h"

/** The bus the touch panel and the IMU share; created in main.c. */
extern i2c_master_bus_handle_t g_i2c_bus;
