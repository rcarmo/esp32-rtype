#ifndef RTYPE_ROM_H
#define RTYPE_ROM_H

#include "esp_err.h"
#include "rtype_m72_core.h"
#include "rtype_m72_video.h"
#include <stddef.h>
#include <stdint.h>

#define RTYPE_ROM_FILE_MAX 20

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    size_t expected_size;
    const char *region_hint;
} rtype_rom_expected_t;

typedef struct {
    char name[32];
    size_t size;
    uint32_t fnv1a;
} rtype_rom_file_info_t;

typedef struct {
    unsigned file_count;
    size_t total_size;
    rtype_rom_file_info_t files[RTYPE_ROM_FILE_MAX];
} rtype_rom_set_info_t;

const rtype_rom_expected_t *rtype_rom_expected_table(unsigned *count);
esp_err_t rtype_rom_probe_project_copy(const char *path, rtype_rom_set_info_t *info);
esp_err_t rtype_rom_load_m72_maincpu(const char *path, rtype_m72_core_t *core);
esp_err_t rtype_rom_load_m72_graphics(const char *path, rtype_m72_video_t *video);
void rtype_rom_log_expected(void);
void rtype_rom_log_probe_result(const rtype_rom_set_info_t *info);


#ifdef __cplusplus
}
#endif
#endif
