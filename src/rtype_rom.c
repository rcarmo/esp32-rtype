#include "rtype_rom.h"
#include "rtype_board.h"

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

void rtype_rom_log_probe_result(const rtype_rom_set_info_t *info) {
    if (info == NULL) return;
    ESP_LOGI(TAG, "ROM probe result: files=%u total=%u", info->file_count, (unsigned)info->total_size);
    for (unsigned i = 0; i < info->file_count; i++) {
        ESP_LOGI(TAG, "  %-14s %6u fnv1a=0x%08x", info->files[i].name, (unsigned)info->files[i].size, info->files[i].fnv1a);
    }
}
