#pragma once
#include <stdbool.h>

typedef struct {
    bool down;
    /** Pixels along the screen's long side since the touch began; the sign is the panel's. */
    int travel;
    /** True once, when a finger lifts. */
    bool released;
} touch_state_t;

void touch_init(void);
touch_state_t touch_poll(void);
