#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "lcd.h"

/**
 * Loads the saved UTC offset. Time survives a reset exactly; after a power-off the clock carries on
 * from the last saved time (a little behind) until the page sends the time again.
 */
void clock_init(void);

/** Sets the time from the page: unix seconds, and the computer's UTC offset in seconds. */
void clock_set(int64_t unix_s, int utc_offset_s);

/** Saves the time now and then, so it can be restored after a power cut. Call from the main loop. */
void clock_tick(int64_t now_us);

/** Whether there is a time to show at all, exact or restored. */
bool clock_is_set(void);

/** Whether the time is exact: sent by the page since the board last lost power. */
bool clock_is_synced(void);

/** Fills in LAYOUT_CLOCK's row. */
void clock_read(lcd_clock_t *row);
