#include "clock.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "nvs.h"

#define NVS_NS "status"
#define NVS_KEY_OFFSET "utcoffset"
#define NVS_KEY_LAST "lasttime"
// Before this (2024), the chip's clock can only be counting up from a power-on.
#define EARLIEST_VALID 1704067200
// How often the time is saved, so a power cut loses at most this much plus the time it was off.
// Two writes a minute leave the NVS partition's flash good for decades.
#define SAVE_EVERY_US (30 * 1000 * 1000LL)

static int s_offset_s;
static bool s_synced;
static int64_t s_next_save;

static void save(bool with_offset)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i64(h, NVS_KEY_LAST, time(NULL));
    if (with_offset) nvs_set_i32(h, NVS_KEY_OFFSET, s_offset_s);
    nvs_commit(h);
    nvs_close(h);
}

void clock_init(void)
{
    int64_t last = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        int32_t v = 0;
        if (nvs_get_i32(h, NVS_KEY_OFFSET, &v) == ESP_OK) s_offset_s = v;
        nvs_get_i64(h, NVS_KEY_LAST, &last);
        nvs_close(h);
    }
    if (clock_is_set()) {
        // Only a reset: the chip kept counting, so the time is still exact.
        s_synced = true;
    } else if (last >= EARLIEST_VALID) {
        // A power cut: carry on from the last saved time, a little behind, until the page syncs.
        struct timeval tv = {.tv_sec = last, .tv_usec = 0};
        settimeofday(&tv, NULL);
    }
}

void clock_set(int64_t unix_s, int utc_offset_s)
{
    struct timeval tv = {.tv_sec = unix_s, .tv_usec = 0};
    settimeofday(&tv, NULL);
    s_synced = true;
    bool offset_changed = utc_offset_s != s_offset_s;
    s_offset_s = utc_offset_s;
    save(offset_changed);
}

void clock_tick(int64_t now_us)
{
    if (now_us < s_next_save) return;
    s_next_save = now_us + SAVE_EVERY_US;
    if (clock_is_set()) save(false);
}

bool clock_is_set(void)
{
    return time(NULL) >= EARLIEST_VALID;
}

bool clock_is_synced(void)
{
    return s_synced;
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
    // A steady colon means the time was restored after a power cut and may be a little behind.
    row->colon = !s_synced || t.tm_sec % 2 == 0;
}
