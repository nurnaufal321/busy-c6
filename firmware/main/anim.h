#pragma once
#include <stdint.h>

#include "board.h"

/** Maps the pack from flash. Returns how many statuses it holds, or 0 if it's missing or bad. */
int anim_init(void);

const char *anim_name(int status);

/** Starts [status] from its first frame. */
void anim_select(int status, int64_t now_us);

/** Decodes up to the frame due at [now_us]. Returns the 72x16 RGB565 picture. */
const uint16_t *anim_update(int64_t now_us);
