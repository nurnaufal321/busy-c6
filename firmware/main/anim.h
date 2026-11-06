#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "board.h"

/** Maps the pack from flash and loads any custom status. Returns the status count, 0 if bad. */
int anim_init(void);

/** How many statuses there are now: the pack's, plus the custom one once set. */
int anim_count(void);

/** Saves a still 72x16 RGB565 picture as the custom status, last in the order. */
void anim_set_custom(const uint16_t *pixels);

/** Removes the custom status. */
void anim_clear_custom(void);

/** Whether a custom status is set. */
bool anim_has_custom(void);

const char *anim_name(int status);

/** Starts [status] from its first frame. */
void anim_select(int status, int64_t now_us);

/** Decodes up to the frame due at [now_us]. Returns the 72x16 RGB565 picture. */
const uint16_t *anim_update(int64_t now_us);
