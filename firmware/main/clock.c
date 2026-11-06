#include "clock.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "nvs.h"

#define NVS_NS "status"
#define NVS_KEY_OFFSET "utcoffset"
// Before this (2024), the chip's clock can only be counting up from a power-on.
#define EARLIEST_VALID 1704067200

static int s_offset_s;

void clock_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(h, NVS_KEY_OFFSET, &v) == ESP_OK) s_offset_s = v;
        nvs_close(h);
    }
}

void clock_set(int64_t unix_s, int utc_offset_s)
{
    struct timeval tv = {.tv_sec = unix_s, .tv_usec = 0};
    settimeofday(&tv, NULL);
    if (utc_offset_s == s_offset_s) return;
    s_offset_s = utc_offset_s;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, NVS_KEY_OFFSET, s_offset_s);
    nvs_commit(h);
    nvs_close(h);
}

bool clock_is_set(void)
{
    return time(NULL) >= EARLIEST_VALID;
}

void clock_read(lcd_clock_t *row)
{
    static const char *const DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (!clock_is_set()) {
        strcpy(row->time, "--:--");
        row->day[0] = 0;
        row->date[0] = 0;
        row->colon = true;
        return;
    }
    time_t local = time(NULL) + s_offset_s;
    struct tm t;
    gmtime_r(&local, &t);
    row->time[0] = '0' + t.tm_hour / 10;
    row->time[1] = '0' + t.tm_hour % 10;
    row->time[2] = ':';
    row->time[3] = '0' + t.tm_min / 10;
    row->time[4] = '0' + t.tm_min % 10;
    row->time[5] = 0;
    snprintf(row->day, sizeof(row->day), "%s", DAYS[t.tm_wday % 7]);
    snprintf(row->date, sizeof(row->date), "%d %s", (t.tm_mday % 32 + 32) % 32, MONTHS[t.tm_mon % 12]);
    row->colon = t.tm_sec % 2 == 0;
}
