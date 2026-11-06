#pragma once
#include <stdbool.h>
#include <stdint.h>

// LED pitch in pixels; the picture is drawn centred on the 320x170 screen.
#define LCD_PITCH 4

void lcd_init(void);

/** Turns the picture upside down, or back. */
void lcd_set_flipped(bool flipped);

/**
 * Draws the 72x16 RGB565 picture as LEDs, moved [shift] LEDs sideways with black coming in.
 * Returns once the transfer has started; the next call waits for it to finish.
 */
void lcd_draw(const uint16_t *leds, int shift);
