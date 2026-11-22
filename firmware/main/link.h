#pragma once
#include <stdbool.h>
#include <stdint.h>

// The web page's commands over USB serial; see ../../PROTOCOL.md.
typedef enum {
    LINK_HELLO,
    LINK_TIME,
    LINK_STATUS,
    LINK_LAYOUT,
    LINK_CUSTOM,         // save a custom status: name, pixels, half
    LINK_CUSTOM_DELETE,  // delete custom status name
    LINK_CUSTOM_GET,     // send custom status name's picture back
    LINK_CUSTOM_CLEAR,   // delete every custom status
} link_type_t;

typedef struct {
    link_type_t type;
    int64_t unix_s;          // LINK_TIME
    int value;               // LINK_TIME: UTC offset in seconds; LINK_STATUS, LINK_LAYOUT: the index
    const uint16_t *pixels;  // LINK_CUSTOM: valid until the next call
    int half;                // LINK_CUSTOM: half-LED offset, bit 0 right, bit 1 down
    char name[24];           // LINK_CUSTOM, LINK_CUSTOM_DELETE, LINK_CUSTOM_GET
} link_cmd_t;

/** Reads whatever has arrived without blocking. True when a whole command is ready. */
bool link_poll(link_cmd_t *cmd);
