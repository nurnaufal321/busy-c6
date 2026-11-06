#pragma once
#include <stdbool.h>

void imu_init(void);

/** Reads the accelerometer and decides which way up the board is held; sticks when unsure. */
bool imu_flipped(void);
