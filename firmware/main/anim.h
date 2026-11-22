#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "board.h"

/** Maps the pack from flash and loads the saved custom statuses. Returns the status count, 0 if bad. */
int anim_init(void);

/** How many statuses there are now: the pack's, then the saved custom ones. */
int anim_count(void);

// How many custom statuses the board can keep.
#define ANIM_CUSTOM_MAX 8

/** How many custom statuses are saved. They come after the pack's statuses, in the order saved. */
int anim_custom_count(void);

/**
 * Saves a still 72x16 RGB565 picture as the custom status [name] (letters, digits and _), replacing
 * one of that name. [half] moves it half an LED: bit 0 right, bit 1 down. Returns its status index,
 * or -1 when all ANIM_CUSTOM_MAX are taken.
 */
int anim_custom_save(const char *name, const uint16_t *pixels, int half);

/** Deletes the custom status [name]. Returns the status index it had, or -1 if there's none. */
int anim_custom_delete(const char *name);

/** Deletes every custom status. */
void anim_custom_clear(void);

/** The picture and half-LED offset of custom status [name]; false if there's none. */
bool anim_custom_get(const char *name, const uint16_t **pixels, int *half);

/** The half-LED offset of the picture showing now: a custom status's, or 0. */
int anim_half(void);

const char *anim_name(int status);

/** Starts [status] from its first frame. */
void anim_select(int status, int64_t now_us);

/** Decodes up to the frame due at [now_us]. Returns the 72x16 RGB565 picture. */
const uint16_t *anim_update(int64_t now_us);
