#include "anim.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"

// See tools/pack_status.py for the format.
#define NAME_LEN 24
#define SUBTYPE_STATUS 0x40
#define NVS_NS "status"
// Where firmware before 1.2.0 kept its one custom status; moved into a slot on first start.
#define NVS_KEY_CUSTOM "custom"
#define NVS_KEY_CUSTOM_HALF "customhalf"

// Saved custom statuses: one flash sector each at the end of the status partition, past the pack
// (tools/pack_status.py keeps it clear). Flashing the pack only rewrites the start of the
// partition, so they survive firmware updates.
#define SECTOR 4096
#define SLOT_MAGIC "BCS1"

// Laid out so it needs no padding: the same bytes in flash as in memory.
typedef struct {
    char magic[4];
    uint8_t half;
    uint8_t reserved[3];
    char name[NAME_LEN];
    uint16_t pixels[LED_PIXELS];
} slot_t;
_Static_assert(sizeof(slot_t) == 32 + LED_PIXELS * 2, "slot_t has padding");

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

static const esp_partition_t *s_part;
static slot_t s_slots[ANIM_CUSTOM_MAX];
static bool s_used[ANIM_CUSTOM_MAX];
static int s_shown_slot = -1;  // the custom status showing, or -1

static const entry_t *s_cur;  // NULL while showing a custom status
static const uint8_t *s_pos;
static uint16_t s_buf[LED_PIXELS];
static int s_frame;  // the frame in s_buf, -1 before the first
static int64_t s_start_us;

static size_t slot_offset(int slot)
{
    return s_part ? s_part->size - (size_t)(slot + 1) * SECTOR : 0;
}

static bool write_slot(int slot)
{
    if (esp_partition_erase_range(s_part, slot_offset(slot), SECTOR) != ESP_OK) return false;
    if (!s_used[slot]) return true;
    return esp_partition_write(s_part, slot_offset(slot), &s_slots[slot], sizeof(slot_t)) == ESP_OK;
}

/** The status index of [slot]: saved statuses follow the pack's, in slot order. */
static int status_of(int slot)
{
    int index = s_count;
    for (int i = 0; i < slot; i++) index += s_used[i];
    return index;
}

/** The slot of status [status], or -1 for one of the pack's. */
static int slot_of(int status)
{
    int index = s_count;
    for (int i = 0; i < ANIM_CUSTOM_MAX; i++) {
        if (!s_used[i]) continue;
        if (index++ == status) return i;
    }
    return -1;
}

static int find_slot(const char *name)
{
    for (int i = 0; i < ANIM_CUSTOM_MAX; i++) {
        if (s_used[i] && strncmp(s_slots[i].name, name, NAME_LEN) == 0) return i;
    }
    return -1;
}

static void migrate_old_custom(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    size_t len = sizeof(s_slots[0].pixels);
    uint16_t *pixels = malloc(len);
    if (pixels && nvs_get_blob(h, NVS_KEY_CUSTOM, pixels, &len) == ESP_OK && len == sizeof(s_slots[0].pixels)) {
        uint8_t half = 0;
        nvs_get_u8(h, NVS_KEY_CUSTOM_HALF, &half);
        if (anim_custom_save("CUSTOM", pixels, half) >= 0) {
            nvs_erase_key(h, NVS_KEY_CUSTOM);
            nvs_erase_key(h, NVS_KEY_CUSTOM_HALF);
            nvs_commit(h);
            ESP_LOGI(TAG, "moved the custom status into a slot");
        }
    }
    free(pixels);
    nvs_close(h);
}

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

    s_part = part;
    for (int i = 0; i < ANIM_CUSTOM_MAX; i++) {
        memcpy(&s_slots[i], s_pack + slot_offset(i), sizeof(slot_t));
        s_used[i] = memcmp(s_slots[i].magic, SLOT_MAGIC, 4) == 0;
        s_slots[i].name[NAME_LEN - 1] = 0;
    }
    migrate_old_custom();
    return anim_count();
}

int anim_count(void)
{
    return s_count + anim_custom_count();
}

int anim_custom_count(void)
{
    int n = 0;
    for (int i = 0; i < ANIM_CUSTOM_MAX; i++) n += s_used[i];
    return n;
}

int anim_half(void)
{
    return s_cur || s_shown_slot < 0 ? 0 : s_slots[s_shown_slot].half;
}

int anim_custom_save(const char *name, const uint16_t *pixels, int half)
{
    int slot = find_slot(name);
    for (int i = 0; slot < 0 && i < ANIM_CUSTOM_MAX; i++) {
        if (!s_used[i]) slot = i;
    }
    if (slot < 0) return -1;
    slot_t *t = &s_slots[slot];
    memset(t, 0, sizeof(*t));
    memcpy(t->magic, SLOT_MAGIC, 4);
    t->half = half & 3;
    strncpy(t->name, name, NAME_LEN - 1);
    memcpy(t->pixels, pixels, sizeof(t->pixels));
    s_used[slot] = true;
    if (!write_slot(slot)) ESP_LOGE(TAG, "couldn't save %s", name);
    return status_of(slot);
}

int anim_custom_delete(const char *name)
{
    int slot = find_slot(name);
    if (slot < 0) return -1;
    int status = status_of(slot);
    s_used[slot] = false;
    if (s_shown_slot == slot) s_shown_slot = -1;
    write_slot(slot);
    return status;
}

void anim_custom_clear(void)
{
    for (int i = 0; i < ANIM_CUSTOM_MAX; i++) {
        if (!s_used[i]) continue;
        s_used[i] = false;
        write_slot(i);
    }
    s_shown_slot = -1;
}

bool anim_custom_get(const char *name, const uint16_t **pixels, int *half)
{
    int slot = find_slot(name);
    if (slot < 0) return false;
    *pixels = s_slots[slot].pixels;
    *half = s_slots[slot].half;
    return true;
}

const char *anim_name(int status)
{
    if (status < s_count) return s_entries[status].name;
    int slot = slot_of(status);
    return slot >= 0 ? s_slots[slot].name : "";
}

void anim_select(int status, int64_t now_us)
{
    if (status >= s_count) {
        s_cur = NULL;
        s_shown_slot = slot_of(status);
        if (s_shown_slot >= 0) memcpy(s_buf, s_slots[s_shown_slot].pixels, sizeof(s_buf));
        else memset(s_buf, 0, sizeof(s_buf));
        return;
    }
    s_shown_slot = -1;
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
    if (!s_cur) return s_buf;
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
