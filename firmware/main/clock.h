#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lcd.h"

/** Loads the saved UTC offset. Time set earlier survives a reset, not a power-off. */
void clock_init(void);

/** Sets the time from the page: unix seconds, and the computer's UTC offset in seconds. */
void clock_set(int64_t unix_s, int utc_offset_s);

bool clock_is_set(void);

/** Fills in LAYOUT_CLOCK's row. */
void clock_read(lcd_clock_t *row);
