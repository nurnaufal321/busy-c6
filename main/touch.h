#pragma once
#include <stdbool.h>

typedef struct {
    bool down;
    /** Pixels along the screen's long side since the touch began; the sign is the panel's. */
    int travel;
    /** True once, when a finger lifts. */
    bool released;
    /** True once, when a finger lifts after a quick touch that barely moved. */
    bool tap;
} touch_state_t;

void touch_init(void);
touch_state_t touch_poll(void);
