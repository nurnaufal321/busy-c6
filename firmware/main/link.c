#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"

#define CUSTOM_HEX (LED_PIXELS * 4)
#define LINE_MAX (CUSTOM_HEX + 16)

static const char *TAG = "link";
static char s_line[LINE_MAX + 1];
static int s_len;
static bool s_overflow;
static uint16_t s_pixels[LED_PIXELS];

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** Hex is four digits per pixel, the RGB565 value as written, e.g. F800 for red. */
static bool parse_custom(const char *hex)
{
    if (strlen(hex) != CUSTOM_HEX) return false;
    for (int i = 0; i < LED_PIXELS; i++) {
        uint16_t v = 0;
        for (int k = 0; k < 4; k++) {
            int d = hex_digit(hex[i * 4 + k]);
            if (d < 0) return false;
            v = (uint16_t)(v << 4 | d);
        }
        s_pixels[i] = v;
    }
    return true;
}

static bool parse_line(char *line, link_cmd_t *cmd)
{
    if (strcmp(line, "HELLO") == 0) {
        cmd->type = LINK_HELLO;
        return true;
    }
    long long unix_s;
    int value;
    if (sscanf(line, "BUSYTIME %lld %d", &unix_s, &value) == 2) {
        cmd->type = LINK_TIME;
        cmd->unix_s = unix_s;
        cmd->value = value;
        return true;
    }
    if (sscanf(line, "STATUS %d", &value) == 1) {
        cmd->type = LINK_STATUS;
        cmd->value = value;
        return true;
    }
    if (sscanf(line, "LAYOUT %d", &value) == 1) {
        cmd->type = LINK_LAYOUT;
        cmd->value = value;
        return true;
    }
    if (strcmp(line, "CUSTOM CLEAR") == 0) {
        cmd->type = LINK_CUSTOM_CLEAR;
        return true;
    }
    if (strncmp(line, "CUSTOM ", 7) == 0 && parse_custom(line + 7)) {
        cmd->type = LINK_CUSTOM;
        cmd->pixels = s_pixels;
        return true;
    }
    ESP_LOGW(TAG, "ignored a %d-byte line", (int)strlen(line));
    return false;
}

bool link_poll(link_cmd_t *cmd)
{
    // The FIFO holds 64 bytes; when it's full the USB host waits, so polling loses nothing.
    while (usb_serial_jtag_ll_rxfifo_data_available()) {
        uint8_t c;
        if (usb_serial_jtag_ll_read_rxfifo(&c, 1) != 1) break;
        if (c == '\n' || c == '\r') {
            bool ready = false;
            if (s_len > 0 && !s_overflow) {
                s_line[s_len] = 0;
                ready = parse_line(s_line, cmd);
            }
            s_len = 0;
            s_overflow = false;
            if (ready) return true;
        } else if (s_len < LINE_MAX) {
            s_line[s_len++] = (char)c;
        } else {
            s_overflow = true;
        }
    }
    return false;
}
