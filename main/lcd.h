#pragma once
#include <stdbool.h>
#include <stdint.h>

// Pixels per LED while a swipe drags the picture.
#define LCD_PITCH 4

/** How the 72x16 picture fills the 320x170 screen. A tap steps through these. */
typedef enum {
    LAYOUT_ORIGINAL,  // 4 px LEDs, centred, black bars
    LAYOUT_TALL,      // resampled onto fine, even 3 px dots, with a black border and round corners
    LAYOUT_PAN,       // 10 px LEDs, the view sliding across the picture
    LAYOUT_GLOW,      // 4 px LEDs, the edge colours fading out into the bars
    LAYOUT_COUNT,
} layout_t;

/** How far LAYOUT_PAN's view can slide, in pixels. */
#define LCD_PAN_MAX 400

void lcd_init(void);

/** Turns the picture upside down, or back. */
void lcd_set_flipped(bool flipped);

/**
 * Draws the 72x16 RGB565 picture as LEDs, moved [shift] LEDs sideways with black coming in. [pan]
 * is LAYOUT_PAN's view position, 0 to LCD_PAN_MAX. Returns once the transfer has started; the next
 * call waits for it to finish.
 */
void lcd_draw(const uint16_t *leds, int shift, layout_t layout, int pan);
