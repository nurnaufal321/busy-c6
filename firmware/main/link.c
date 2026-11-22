#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"

#define CUSTOM_HEX (LED_PIXELS * 4)
#define LINE_MAX (CUSTOM_HEX + 64)

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

/**
 * Hex is four digits per pixel, the RGB565 value as written, e.g. F800 for red, then optionally
 * " <x> <y>": 1 to move the picture half an LED right or down, so it can sit exactly centred.
 */
static bool parse_custom(const char *hex, int *half)
{
    size_t len = strlen(hex);
    *half = 0;
    if (len > CUSTOM_HEX) {
        int x, y;
        if (hex[CUSTOM_HEX] != ' ' || sscanf(hex + CUSTOM_HEX, " %d %d", &x, &y) != 2) return false;
        *half = (x ? 1 : 0) | (y ? 2 : 0);
    } else if (len != CUSTOM_HEX) {
        return false;
    }
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

/** Copies a custom status's name: capitals, digits and _ only, as the page sends them. */
static bool parse_name(const char *from, size_t len, char *out)
{
    if (len == 0 || len >= sizeof(((link_cmd_t *)0)->name)) return false;
    for (size_t i = 0; i < len; i++) {
        char c = from[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
        out[i] = c;
    }
    out[len] = 0;
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
    if (strncmp(line, "CUSTOM ADD ", 11) == 0) {
        const char *name = line + 11, *end = strchr(name, ' ');
        if (end && parse_name(name, end - name, cmd->name) && parse_custom(end + 1, &cmd->half)) {
            cmd->type = LINK_CUSTOM;
            cmd->pixels = s_pixels;
            return true;
        }
    }
    if (strncmp(line, "CUSTOM DELETE ", 14) == 0 && parse_name(line + 14, strlen(line + 14), cmd->name)) {
        cmd->type = LINK_CUSTOM_DELETE;
        return true;
    }
    if (strncmp(line, "CUSTOM GET ", 11) == 0 && parse_name(line + 11, strlen(line + 11), cmd->name)) {
        cmd->type = LINK_CUSTOM_GET;
        return true;
    }
    // From pages before 1.2.0: one unnamed custom status.
    if (strncmp(line, "CUSTOM ", 7) == 0 && parse_custom(line + 7, &cmd->half)) {
        cmd->type = LINK_CUSTOM;
        strcpy(cmd->name, "CUSTOM");
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
