#pragma once
#include <stdint.h>

typedef struct {
    char c;
    uint8_t w, h;
    int8_t left;   // from the pen position to the glyph's left edge
    int8_t top;    // from the baseline up to the glyph's top edge
    uint8_t advance;
    uint32_t offset;  // into the font's coverage bytes, row-major
} glyph_t;

typedef struct {
    const glyph_t *glyphs;
    int count;
    const uint8_t *bits;
} font_t;

extern const font_t font_time;  // large digits for the clock
extern const font_t font_date;  // small digits and capitals for the date
