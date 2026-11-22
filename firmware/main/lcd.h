#pragma once
#include <stdbool.h>
#include <stdint.h>

// Pixels per LED while a swipe drags the picture.
#define LCD_PITCH 4

/** How the 72x16 picture fills the 320x170 screen. A tap steps through these. */
typedef enum {
    LAYOUT_ORIGINAL,  // 4 px LEDs, centred, black bars
    LAYOUT_CLOCK,     // the picture at the top, with the date and time below
    LAYOUT_COUNT,
} layout_t;

/** What LAYOUT_CLOCK's row shows. Empty strings for the date when there's no time yet. */
typedef struct {
    char time[6];  // "20:45", or "--:--"
    char day[4];   // "THU"
    char date[8];  // "6 NOV"
    bool colon;    // the colon's blink
} lcd_clock_t;

void lcd_init(void);

/** Turns the picture upside down, or back. */
void lcd_set_flipped(bool flipped);

/**
 * Draws the 72x16 RGB565 picture as LEDs, moved [shift] LEDs sideways with black coming in. [half]
 * moves it half an LED (bit 0 right, bit 1 down); [clock] is LAYOUT_CLOCK's row. Returns once the transfer has started; the next
 * call waits for it to finish.
 */
void lcd_draw(const uint16_t *leds, int shift, layout_t layout, int half, const lcd_clock_t *clock);
