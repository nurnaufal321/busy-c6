#pragma once
#include <stdbool.h>
#include <stdint.h>

// The web page's commands over USB serial; see ../../PROTOCOL.md.
typedef enum {
    LINK_HELLO,
    LINK_TIME,
    LINK_STATUS,
    LINK_LAYOUT,
    LINK_CUSTOM,
    LINK_CUSTOM_CLEAR,
} link_type_t;

typedef struct {
    link_type_t type;
    int64_t unix_s;          // LINK_TIME
    int value;               // LINK_TIME: UTC offset in seconds; LINK_STATUS, LINK_LAYOUT: the index
    const uint16_t *pixels;  // LINK_CUSTOM: valid until the next call
} link_cmd_t;

/** Reads whatever has arrived without blocking. True when a whole command is ready. */
bool link_poll(link_cmd_t *cmd);
