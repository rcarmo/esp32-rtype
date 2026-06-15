#include "rtype_rom.h"
#include "rtype_board.h"
#include "rtype_m72_video.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "rtype_rom";

static const rtype_rom_expected_t k_expected[] = {
    {"cpu-30.bin", 65536, "main-v30-program/high-or-bank"},
    {"cpu-20.bin", 65536, "main-v30-program/high-or-bank"},
    {"cpu-10.bin", 65536, "main-v30-program/high-or-bank"},
    {"cpu-00.bin", 65536, "main-v30-program/high-or-bank"},
    {"cpu-b3.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-b2.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-b1.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-b0.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-a3.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-a2.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-a1.bin", 32768, "graphics/tiles-or-sprites"},
    {"cpu-a0.bin", 32768, "graphics/tiles-or-sprites"},
    {"rt_r-l1-.bin", 65536, "graphics/tiles-or-sprites"},
    {"rt_r-l0-.bin", 65536, "graphics/tiles-or-sprites"},
    {"rt_r-h1-.bin", 65536, "graphics/tiles-or-sprites"},
    {"rt_r-h0-.bin", 65536, "graphics/tiles-or-sprites"},
    {"cpu-01.bin", 65536, "main-v30-program/alternate-revision"},
    {"cpu-11.bin", 65536, "main-v30-program/alternate-revision"},
    {"cpu-21.bin", 65536, "main-v30-program/alternate-revision"},
    {"cpu-31.bin", 65536, "main-v30-program/alternate-revision"},
};

const rtype_rom_expected_t *rtype_rom_expected_table(unsigned *count) {
    if (count) *count = sizeof(k_expected) / sizeof(k_expected[0]);
    return k_expected;
}

void rtype_rom_log_expected(void) {
    unsigned count = 0;
    const rtype_rom_expected_t *table = rtype_rom_expected_table(&count);
    ESP_LOGI(TAG, "expected R-Type ROM set: files=%u total=%u", count, RTYPE_EXPECTED_ZIP_TOTAL);
    for (unsigned i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  %-14s %6u %s", table[i].name, (unsigned)table[i].expected_size, table[i].region_hint);
    }
}

static uint32_t fnv1a_file(FILE *fp, size_t *size_out) {
    uint8_t buf[512];
    uint32_t h = 2166136261u;
    size_t total = 0;
    while (!feof(fp)) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        total += n;
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 16777619u;
        }
    }
    if (size_out) *size_out = total;
    return h;
}

esp_err_t rtype_rom_probe_project_copy(const char *path, rtype_rom_set_info_t *info) {
    if (path == NULL || info == NULL) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));

    unsigned count = 0;
    const rtype_rom_expected_t *table = rtype_rom_expected_table(&count);
    char full[256];
    for (unsigned i = 0; i < count && i < RTYPE_ROM_FILE_MAX; i++) {
        snprintf(full, sizeof(full), "%s/%s", path, table[i].name);
        FILE *fp = fopen(full, "rb");
        if (fp == NULL) continue;
        rtype_rom_file_info_t *file = &info->files[info->file_count++];
        strncpy(file->name, table[i].name, sizeof(file->name) - 1);
        file->fnv1a = fnv1a_file(fp, &file->size);
        info->total_size += file->size;
        fclose(fp);
    }
    return ESP_OK;
}

static esp_err_t load_file_slice(uint8_t *dst, size_t dst_size, size_t dst_off,
                                 const char *base_path, const char *name,
                                 size_t required_size, size_t copy_size) {
    if (dst == NULL || base_path == NULL || name == NULL || dst_off + copy_size > dst_size) {
        return ESP_ERR_INVALID_ARG;
    }
    char full[256];
    snprintf(full, sizeof(full), "%s/%s", base_path, name);
    FILE *fp = fopen(full, "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "ROM file unavailable: %s", full);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0 || (size_t)size < required_size) {
        ESP_LOGE(TAG, "ROM file too small: %s got=%ld required=%u", full, size, (unsigned)required_size);
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t got = fread(dst + dst_off, 1, copy_size, fp);
    fclose(fp);
    if (got != copy_size) {
        ESP_LOGE(TAG, "ROM short read: %s got=%u expected=%u", full, (unsigned)got, (unsigned)copy_size);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "loaded %-14s -> region+0x%05x bytes=%u", name, (unsigned)dst_off, (unsigned)copy_size);
    return ESP_OK;
}

static esp_err_t ensure_region(uint8_t **ptr, size_t bytes, const char *name) {
    if (*ptr != NULL) return ESP_OK;
    *ptr = rtype_m72_alloc_region(bytes, name);
    return *ptr != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t rtype_rom_load_m72_graphics(const char *path, rtype_m72_video_t *video) {
    if (path == NULL || video == NULL) return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(ensure_region((uint8_t **)&video->sprites, 0x80000u, "m72-sprites-rom"), TAG, "sprites alloc failed");
    ESP_RETURN_ON_ERROR(ensure_region((uint8_t **)&video->tiles0, 0x20000u, "m72-tiles0-rom"), TAG, "tiles0 alloc failed");
    ESP_RETURN_ON_ERROR(ensure_region((uint8_t **)&video->tiles1, 0x20000u, "m72-tiles1-rom"), TAG, "tiles1 alloc failed");
    video->sprites_size = 0x80000u;
    video->tiles0_size = 0x20000u;
    video->tiles1_size = 0x20000u;

    uint8_t *sprites = (uint8_t *)video->sprites;
    uint8_t *tiles0 = (uint8_t *)video->tiles0;
    uint8_t *tiles1 = (uint8_t *)video->tiles1;
    memset(sprites, 0xff, video->sprites_size);
    memset(tiles0, 0xff, video->tiles0_size);
    memset(tiles1, 0xff, video->tiles1_size);

    esp_err_t err = ESP_OK;
#define TRY_LOAD(dst, size, off, name, required, copy) do { \
        esp_err_t e = load_file_slice((dst), (size), (off), path, (name), (required), (copy)); \
        if (e != ESP_OK) err = e; \
    } while (0)

    TRY_LOAD(sprites, video->sprites_size, 0x00000u, "cpu-00.bin", 0x10000u, 0x10000u);
    TRY_LOAD(sprites, video->sprites_size, 0x10000u, "cpu-01.bin", 0x08000u, 0x08000u);
    memcpy(sprites + 0x18000u, sprites + 0x10000u, 0x08000u);
    TRY_LOAD(sprites, video->sprites_size, 0x20000u, "cpu-10.bin", 0x10000u, 0x10000u);
    TRY_LOAD(sprites, video->sprites_size, 0x30000u, "cpu-11.bin", 0x08000u, 0x08000u);
    memcpy(sprites + 0x38000u, sprites + 0x30000u, 0x08000u);
    TRY_LOAD(sprites, video->sprites_size, 0x40000u, "cpu-20.bin", 0x10000u, 0x10000u);
    TRY_LOAD(sprites, video->sprites_size, 0x50000u, "cpu-21.bin", 0x08000u, 0x08000u);
    memcpy(sprites + 0x58000u, sprites + 0x50000u, 0x08000u);
    TRY_LOAD(sprites, video->sprites_size, 0x60000u, "cpu-30.bin", 0x10000u, 0x10000u);
    TRY_LOAD(sprites, video->sprites_size, 0x70000u, "cpu-31.bin", 0x08000u, 0x08000u);
    memcpy(sprites + 0x78000u, sprites + 0x70000u, 0x08000u);

    TRY_LOAD(tiles0, video->tiles0_size, 0x00000u, "cpu-a0.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles0, video->tiles0_size, 0x08000u, "cpu-a1.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles0, video->tiles0_size, 0x10000u, "cpu-a2.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles0, video->tiles0_size, 0x18000u, "cpu-a3.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles1, video->tiles1_size, 0x00000u, "cpu-b0.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles1, video->tiles1_size, 0x08000u, "cpu-b1.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles1, video->tiles1_size, 0x10000u, "cpu-b2.bin", 0x08000u, 0x08000u);
    TRY_LOAD(tiles1, video->tiles1_size, 0x18000u, "cpu-b3.bin", 0x08000u, 0x08000u);
#undef TRY_LOAD

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "M72 graphics ROM regions loaded from %s", path);
    } else {
        ESP_LOGW(TAG, "M72 graphics ROM load incomplete from %s; renderer can still use fallback pixels", path);
    }
    return err;
}

void rtype_rom_log_probe_result(const rtype_rom_set_info_t *info) {
    if (info == NULL) return;
    ESP_LOGI(TAG, "ROM probe result: files=%u total=%u", info->file_count, (unsigned)info->total_size);
    for (unsigned i = 0; i < info->file_count; i++) {
        ESP_LOGI(TAG, "  %-14s %6u fnv1a=0x%08x", info->files[i].name, (unsigned)info->files[i].size, info->files[i].fnv1a);
    }
}
