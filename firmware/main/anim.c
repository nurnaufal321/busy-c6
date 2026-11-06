#include "anim.h"

#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"

// See tools/pack_status.py for the format.
#define NAME_LEN 24
#define SUBTYPE_STATUS 0x40

typedef struct __attribute__((packed)) {
    char name[NAME_LEN];
    uint16_t fps;
    uint16_t frames;
    uint32_t offset;
    uint32_t size;
} entry_t;

static const char *TAG = "anim";
static const uint8_t *s_pack;
static const entry_t *s_entries;
static int s_count;

static const entry_t *s_cur;
static const uint8_t *s_pos;
static uint16_t s_buf[LED_PIXELS];
static int s_frame;  // the frame in s_buf, -1 before the first
static int64_t s_start_us;

int anim_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, SUBTYPE_STATUS, "status");
    if (!part) return 0;
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) return 0;
    s_pack = map;
    if (memcmp(s_pack, "BSP1", 4) != 0) {
        ESP_LOGE(TAG, "no status pack in flash");
        return 0;
    }
    memcpy(&s_count, s_pack + 4, sizeof(s_count));
    s_entries = (const entry_t *)(s_pack + 8);
    return s_count;
}

const char *anim_name(int status)
{
    return s_entries[status].name;
}

void anim_select(int status, int64_t now_us)
{
    s_cur = &s_entries[status];
    s_frame = -1;
    s_start_us = now_us;
    s_pos = s_pack + s_cur->offset;
    memset(s_buf, 0, sizeof(s_buf));
}

static void decode_frame(void)
{
    const uint8_t *p = s_pos;
    int i = 0;
    while (i < LED_PIXELS) {
        uint8_t token = *p++;
        int n = (token & 0x3F) + 1;
        switch (token >> 6) {
        case 1:
            memcpy(&s_buf[i], p, n * 2);
            p += n * 2;
            break;
        case 2: {
            uint16_t value = p[0] | p[1] << 8;
            p += 2;
            for (int k = 0; k < n; k++) s_buf[i + k] = value;
            break;
        }
        default:
            break;
        }
        i += n;
    }
    s_pos = p;
}

const uint16_t *anim_update(int64_t now_us)
{
    int64_t due = (now_us - s_start_us) * s_cur->fps / 1000000;
    if (due >= s_cur->frames) {
        // Loop: start again from black, keeping the time left over.
        s_start_us += (int64_t)s_cur->frames * 1000000 / s_cur->fps;
        due -= s_cur->frames;
        if (due >= s_cur->frames) due %= s_cur->frames;
        s_frame = -1;
        s_pos = s_pack + s_cur->offset;
        memset(s_buf, 0, sizeof(s_buf));
    }
    // Frames build on each other, so a late one still decodes the ones it skips.
    while (s_frame < due) {
        decode_frame();
        s_frame++;
    }
    return s_buf;
}
