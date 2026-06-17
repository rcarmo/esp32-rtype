#include "rtype_board.h"
#include "rtype_display.h"
#include "rtype_m72_core.h"
#include "rtype_m72_video.h"
#include "rtype_rom.h"
#include "rtype_video.h"
#include "rtype_i86_cpu.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#if defined(RTYPE_BOARD_ESP32_8048S043C)
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>

static const char *TAG = "rtype_app";

#ifndef RTYPE_S3_RENDER_AUDIT
#define RTYPE_S3_RENDER_AUDIT 0
#endif

#if defined(RTYPE_BOARD_ESP32_8048S043C)
static wl_handle_t s_spiflash_wl = WL_INVALID_HANDLE;

static esp_err_t mount_spiflash_storage(void) {
    if (s_spiflash_wl != WL_INVALID_HANDLE) return ESP_OK;
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 24,
        .allocation_unit_size = 4096,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/spiflash", "storage", &cfg, &s_spiflash_wl);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "mounted FAT storage partition at /spiflash");
    } else {
        ESP_LOGW(TAG, "failed to mount /spiflash storage (%s); external ROM files unavailable", esp_err_to_name(err));
    }
    return err;
}
#endif

#if defined(RTYPE_BOARD_ESP32_8048S043C)
static uint32_t count_palette_nonzero(const rtype_m72_video_t *video, unsigned begin, unsigned end) {
    if (video == NULL) return 0;
    if (end > RTYPE_M72_PALETTE_COLORS) end = RTYPE_M72_PALETTE_COLORS;
    uint32_t n = 0;
    for (unsigned i = begin; i < end; i++) if (video->palette[i] != 0) n++;
    return n;
}
#endif

#if defined(RTYPE_BOARD_ESP32_8048S043C) && RTYPE_S3_RENDER_AUDIT
typedef struct {
    uint32_t nonzero;
    unsigned min_x;
    unsigned min_y;
    unsigned max_x;
    unsigned max_y;
} fb_audit_t;

static fb_audit_t audit_framebuffer(const uint16_t *fb) {
    fb_audit_t a = { .nonzero = 0, .min_x = RTYPE_GAME_W, .min_y = RTYPE_GAME_H, .max_x = 0, .max_y = 0 };
    if (fb == NULL) return a;
    for (unsigned y = 0; y < RTYPE_GAME_H; y++) {
        for (unsigned x = 0; x < RTYPE_GAME_W; x++) {
            uint16_t px = fb[(size_t)y * RTYPE_GAME_W + x];
            if (px == 0) continue;
            a.nonzero++;
            if (x < a.min_x) a.min_x = x;
            if (y < a.min_y) a.min_y = y;
            if (x > a.max_x) a.max_x = x;
            if (y > a.max_y) a.max_y = y;
        }
    }
    if (a.nonzero == 0) {
        a.min_x = a.min_y = a.max_x = a.max_y = 0;
    }
    return a;
}

#endif

static void log_system_info(void) {
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "R-Type display-only bring-up starting on %s", RTYPE_BOARD_NAME);
    ESP_LOGI(TAG, "chip model=%d cores=%d revision=%d features=0x%08" PRIx32,
             chip.model, chip.cores, chip.revision, (uint32_t)chip.features);
    ESP_LOGI(TAG, "flash size=%" PRIu32 " bytes", flash_size);
#if CONFIG_SPIRAM
    ESP_LOGI(TAG, "psram size=%zu bytes", esp_psram_get_size());
#else
    ESP_LOGW(TAG, "PSRAM disabled; this target expects PSRAM");
#endif
    ESP_LOGI(TAG, "heap internal free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "heap 8-bit free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void app_main(void) {
    log_system_info();
    rtype_rom_log_expected();

#if defined(RTYPE_BOARD_ESP32_2432S028)
    ESP_ERROR_CHECK(rtype_display_init());
    ESP_ERROR_CHECK(rtype_display_set_brightness(100));
#endif

    static rtype_m72_core_t core;
    esp_err_t core_err = rtype_m72_core_init(&core);
    esp_err_t cpu_rom_err = ESP_FAIL;
    if (core_err != ESP_OK) {
        ESP_LOGE(TAG, "no M72 core state (%s); display-only fallback active", esp_err_to_name(core_err));
    } else {
#if defined(RTYPE_BOARD_ESP32_2432S028)
        cpu_rom_err = rtype_m72_core_map_maincpu_partition(&core, "storage");
        if (cpu_rom_err != ESP_OK) {
            ESP_LOGW(TAG, "main CPU ROM partition unavailable (%s); CYD will display embedded frame only", esp_err_to_name(cpu_rom_err));
        } else {
            ESP_LOGI(TAG, "CYD sparse M72 CPU core mapped from storage partition");
        }
#elif defined(RTYPE_BOARD_ESP32_8048S043C)
        ESP_LOGI(TAG, "S3 full-framebuffer M72 core + V30 backend active");
        cpu_rom_err = rtype_m72_core_map_maincpu_partition(&core, "maincpu");
        // The S3 maincpu partition contains only the 1MB V30 CPU map; graphics
        // still come from the FAT ROM set below.
        core.video.sprites = NULL;
        core.video.sprites_size = 0;
        core.video.tiles0 = NULL;
        core.video.tiles0_size = 0;
        core.video.tiles1 = NULL;
        core.video.tiles1_size = 0;
        if (cpu_rom_err != ESP_OK) {
            ESP_LOGW(TAG, "maincpu partition unavailable (%s); falling back to FAT-loaded main CPU ROMs", esp_err_to_name(cpu_rom_err));
        }
        (void)mount_spiflash_storage();
        if (cpu_rom_err != ESP_OK) {
            cpu_rom_err = rtype_rom_load_m72_maincpu("/spiflash/rtype", &core);
            if (cpu_rom_err != ESP_OK) {
                ESP_LOGW(TAG, "external main CPU ROMs not loaded (%s); S3 will use graphics-only fallback", esp_err_to_name(cpu_rom_err));
            }
        }
        esp_err_t rom_err = rtype_rom_load_m72_graphics("/spiflash/rtype", &core.video);
        ESP_LOGI(TAG, "S3 render stage ROMs: sprites=%p/%u tiles0=%p/%u tiles1=%p/%u",
                 (const void *)core.video.sprites, (unsigned)core.video.sprites_size,
                 (const void *)core.video.tiles0, (unsigned)core.video.tiles0_size,
                 (const void *)core.video.tiles1, (unsigned)core.video.tiles1_size);
        if (rom_err != ESP_OK) {
            ESP_LOGW(TAG, "external graphics ROMs not loaded (%s); using deterministic fallback pixels", esp_err_to_name(rom_err));
        }
#else
        ESP_LOGI(TAG, "Milestone graphics: M72 core + tile/sprite renderer active");
        esp_err_t rom_err = rtype_rom_load_m72_graphics("/spiflash/rtype", &core.video);
        if (rom_err != ESP_OK) {
            ESP_LOGW(TAG, "external graphics ROMs not loaded (%s); using deterministic fallback pixels", esp_err_to_name(rom_err));
        }
#endif
    }

#if !defined(RTYPE_BOARD_ESP32_2432S028)
    ESP_ERROR_CHECK(rtype_display_init());
    ESP_ERROR_CHECK(rtype_display_set_brightness(100));
#endif

    uint16_t *fb = rtype_video_alloc_framebuffer();
    if (fb == NULL) {
        ESP_LOGW(TAG, "no full source framebuffer; using board-specific no-framebuffer display path");
        rtype_i86_cpu_t cpu;
        bool cpu_running = (core_err == ESP_OK && cpu_rom_err == ESP_OK);
        if (cpu_running) {
            rtype_i86_reset(&cpu, &core);
            ESP_LOGI(TAG, "CYD CPU backend starting without full framebuffer");
        }
        uint64_t next_vblank = 120000;
        const uint64_t wait_skip_window = 118000; // run dispatcher, then skip only the queue-empty wait tail
        int64_t next_vblank_us = esp_timer_get_time();
        const int64_t frame_period_us = 16667; // pace vblank to real time; skip idle instructions, not game time
        bool main_loop_seen = false;
        bool frame_vector_ready = false;
        bool live_video_ready = false;
        uint64_t last_presented_irq = 0;
        uint64_t last_perf_insn = 0;
        uint64_t last_perf_irq = 0;
        int64_t last_perf_us = esp_timer_get_time();
        bool present_due = false;
        uint64_t idle_skips = 0;
        for (unsigned frame = 0;; frame++) {
            uint32_t vram0_nz = 0;
            uint32_t vram1_nz = 0;
            uint32_t spr_nz = 0;
            if (cpu_running && !frame_vector_ready) {
                frame_vector_ready = (rtype_m72_core_read16(&core, 0x20u * 4u) == 0x00fe &&
                                      rtype_m72_core_read16(&core, 0x20u * 4u + 2u) == 0x0040);
            }
            present_due = false;
            if (cpu_running) {
                uint64_t target = cpu.insn + 500000ull;
                while (!cpu.halted && cpu.insn < target) {
                    bool in_main_loop = (cpu.s[RTYPE_I86_CS] == 0x0040 && cpu.ip >= 0x00d0 && cpu.ip <= 0x00f8);
                    if (in_main_loop) {
                        main_loop_seen = true;
                        rtype_i86_complete_frame_if_idle(&cpu);
                        if (cpu.interrupt_count > 0 && cpu.interrupt_count >= last_presented_irq + 8u) {
                            present_due = true;
                            break;
                        }
                    }
                    bool idle_queue_empty_tail = (cpu.s[RTYPE_I86_CS] == 0x0040 && cpu.ip == 0x00ddu && cpu.zf);
                    bool late_wait_tail = idle_queue_empty_tail && cpu.insn + wait_skip_window >= next_vblank;
                    if (main_loop_seen && frame_vector_ready && cpu.iff && cpu.interrupt_depth == 0 &&
                        (late_wait_tail || (in_main_loop && cpu.insn >= next_vblank))) {
                        if (late_wait_tail && cpu.insn < next_vblank) { cpu.insn = next_vblank; idle_skips++; }
                        int64_t now_us = esp_timer_get_time();
                        if (now_us >= next_vblank_us) {
                            rtype_i86_interrupt(&cpu, 0x20);
                            next_vblank += 120000;
                            next_vblank_us += frame_period_us;
                            if (next_vblank_us < now_us - frame_period_us) next_vblank_us = now_us + frame_period_us;
                        } else {
                            break;
                        }
                    }
                    rtype_i86_step(&cpu);
                }
                if (cpu.halted) {
                    ESP_LOGW(TAG, "CYD CPU halted pc=0x%05" PRIx32 " opcode=0x%02x reason=%s",
                             rtype_i86_pc(&cpu), cpu.last_opcode, cpu.stop_reason);
                    cpu_running = false;
                }
            }
            if (cpu_running && (frame & 0x0fu) == 0) {
                vram0_nz = rtype_m72_core_count_nonzero(&core, RTYPE_M72_VRAM0_BASE, RTYPE_M72_VRAM0_BASE + RTYPE_M72_VRAM_BYTES);
                vram1_nz = rtype_m72_core_count_nonzero(&core, RTYPE_M72_VRAM1_BASE, RTYPE_M72_VRAM1_BASE + RTYPE_M72_VRAM_BYTES);
                spr_nz = rtype_m72_core_count_nonzero(&core, RTYPE_M72_SPRITE_RAM_BASE, RTYPE_M72_SPRITE_RAM_BASE + RTYPE_M72_SPRITERAM_BYTES);
                live_video_ready = !core.video.video_off && cpu.interrupt_count > 0 &&
                                   (vram0_nz != 0 || vram1_nz != 0 || spr_nz != 0);
            }
            esp_err_t err = ESP_OK;
            if (live_video_ready && present_due) {
                err = rtype_display_present_m72_core(&core);
                last_presented_irq = cpu.interrupt_count;
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "no-framebuffer present failed: %s", esp_err_to_name(err));
                rtype_display_heartbeat_loop();
            }
            if (cpu_running) {
                int64_t now_us = esp_timer_get_time();
                if (now_us - last_perf_us >= 5000000) {
                    uint64_t dinsn = cpu.insn - last_perf_insn;
                    uint64_t dirq = cpu.interrupt_count - last_perf_irq;
                    uint64_t dt_us = (uint64_t)(now_us - last_perf_us);
                    ESP_LOGI(TAG, "CYD PERF pc=0x%05" PRIx32 " ips=%llu irq_s=%llu frame=0x%04x root=0x%04x main=0x%04x q=%04x/%04x skip=%llu in=%04x/%04x dsw=%04x scroll=(%u,%u)/(%u,%u) live=%d vram=%u/%u spr=%u",
                             rtype_i86_pc(&cpu),
                             (unsigned long long)((dinsn * 1000000ull) / dt_us),
                             (unsigned long long)((dirq * 1000000ull) / dt_us),
                             (unsigned)rtype_m72_core_read16(&core, 0x42eb4u),
                             (unsigned)rtype_m72_core_read16(&core, 0x40000u),
                             (unsigned)rtype_m72_core_read16(&core, 0x43060u),
                             (unsigned)rtype_m72_core_read16(&core, 0x42ed8u),
                             (unsigned)rtype_m72_core_read16(&core, 0x42edau),
                             (unsigned long long)idle_skips,
                             (unsigned)rtype_m72_core_read16(&core, 0x42040u),
                             (unsigned)rtype_m72_core_read16(&core, 0x42042u),
                             (unsigned)rtype_m72_core_read16(&core, 0x42044u),
                             (unsigned)core.video.scrollx[0], (unsigned)core.video.scrolly[0],
                             (unsigned)core.video.scrollx[1], (unsigned)core.video.scrolly[1],
                             live_video_ready ? 1 : 0, (unsigned)vram0_nz, (unsigned)vram1_nz, (unsigned)spr_nz);
                    idle_skips = 0;
                    last_perf_insn = cpu.insn;
                    last_perf_irq = cpu.interrupt_count;
                    last_perf_us = now_us;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    uint16_t *fb_next = rtype_video_alloc_framebuffer();
    if (fb_next != NULL) {
        ESP_LOGI(TAG, "double framebuffer enabled for async display handoff");
    } else {
        ESP_LOGW(TAG, "single framebuffer only; display producer will throttle after present");
    }

#if defined(RTYPE_BOARD_ESP32_8048S043C)
    rtype_i86_cpu_t full_cpu;
    bool full_cpu_running = (core_err == ESP_OK && cpu_rom_err == ESP_OK);
    uint64_t full_next_vblank = 120000;
    const uint64_t full_wait_skip_window = 118000;
    int64_t full_next_vblank_us = esp_timer_get_time();
    const int64_t full_frame_period_us = 16667;
    bool full_main_loop_seen = false;
    bool full_frame_vector_ready = false;
    const uint64_t full_render_irq_interval = 5u;
    uint64_t full_last_presented_irq = 0;
    uint64_t full_render_due_irq = 0;
    bool full_render_due = false;
    uint64_t full_last_perf_irq = 0;
    uint64_t full_last_perf_insn = 0;
    uint64_t full_prof_cpu_us = 0;
    uint64_t full_prof_render_us = 0;
    uint64_t full_prof_present_us = 0;
    uint32_t full_prof_renders = 0;
#if RTYPE_S3_RENDER_AUDIT
    fb_audit_t full_last_fb_audit = {0};
#endif
    int64_t full_last_perf_us = esp_timer_get_time();
    if (full_cpu_running) {
        rtype_i86_reset(&full_cpu, &core);
        ESP_LOGI(TAG, "S3 full-framebuffer V30 backend starting");
    }
#endif

    for (unsigned frame = 0;; frame++) {
#if defined(RTYPE_BOARD_ESP32_8048S043C)
        if (full_cpu_running && !full_frame_vector_ready) {
            full_frame_vector_ready = (rtype_m72_core_read16(&core, 0x20u * 4u) == 0x00fe &&
                                       rtype_m72_core_read16(&core, 0x20u * 4u + 2u) == 0x0040);
        }
        if (full_cpu_running) {
            int64_t full_cpu_begin_us = esp_timer_get_time();
            uint64_t target = full_cpu.insn + 1000000ull;
            while (!full_cpu.halted && full_cpu.insn < target) {
                bool in_main_loop = (full_cpu.s[RTYPE_I86_CS] == 0x0040 && full_cpu.ip >= 0x00d0 && full_cpu.ip <= 0x00f8);
                if (in_main_loop) {
                    full_main_loop_seen = true;
                    rtype_i86_complete_frame_if_idle(&full_cpu);
                    uint16_t cur_root = rtype_m72_core_read16(&core, 0x40000u);
                    if (full_cpu.interrupt_count > 0 && cur_root == 0x0aa6u &&
                        full_cpu.interrupt_count >= full_last_presented_irq + full_render_irq_interval) {
                        full_render_due = true;
                        full_render_due_irq = full_cpu.interrupt_count;
                        break;
                    }
                }
                bool idle_queue_empty_tail = (full_cpu.s[RTYPE_I86_CS] == 0x0040 && full_cpu.ip == 0x00ddu && full_cpu.zf);
                bool late_wait_tail = idle_queue_empty_tail && full_cpu.insn + full_wait_skip_window >= full_next_vblank;
                if (full_main_loop_seen && full_frame_vector_ready && full_cpu.iff && full_cpu.interrupt_depth == 0 && late_wait_tail) {
                    if (late_wait_tail && full_cpu.insn < full_next_vblank) full_cpu.insn = full_next_vblank;
                    int64_t now_us = esp_timer_get_time();
                    if (now_us >= full_next_vblank_us) {
                        rtype_i86_interrupt(&full_cpu, 0x20);
                        full_next_vblank += 120000;
                        full_next_vblank_us += full_frame_period_us;
                        if (full_next_vblank_us < now_us - full_frame_period_us) full_next_vblank_us = now_us + full_frame_period_us;
                    } else {
                        break;
                    }
                }
                rtype_i86_step(&full_cpu);
            }
            full_prof_cpu_us += (uint64_t)(esp_timer_get_time() - full_cpu_begin_us);
            if (full_cpu.halted) {
                ESP_LOGW(TAG, "S3 CPU halted pc=0x%05" PRIx32 " opcode=0x%02x reason=%s",
                         rtype_i86_pc(&full_cpu), full_cpu.last_opcode, full_cpu.stop_reason);
                full_cpu_running = false;
            }
            int64_t now_us = esp_timer_get_time();
            if (now_us - full_last_perf_us >= 5000000) {
                uint64_t dirq = full_cpu.interrupt_count - full_last_perf_irq;
                uint64_t dinsn = full_cpu.insn - full_last_perf_insn;
                uint64_t dt_us = (uint64_t)(now_us - full_last_perf_us);
                uint64_t avg_render_us = full_prof_renders ? (full_prof_render_us / full_prof_renders) : 0;
                uint64_t avg_present_us = full_prof_renders ? (full_prof_present_us / full_prof_renders) : 0;
#if RTYPE_S3_RENDER_AUDIT
                uint32_t audit_vram0 = rtype_m72_core_count_nonzero(&core, RTYPE_M72_VRAM0_BASE, RTYPE_M72_VRAM0_BASE + RTYPE_M72_VRAM_BYTES);
                uint32_t audit_vram1 = rtype_m72_core_count_nonzero(&core, RTYPE_M72_VRAM1_BASE, RTYPE_M72_VRAM1_BASE + RTYPE_M72_VRAM_BYTES);
                uint32_t audit_spr = rtype_m72_core_count_nonzero(&core, RTYPE_M72_SPRITE_RAM_BASE, RTYPE_M72_SPRITE_RAM_BASE + RTYPE_M72_SPRITERAM_BYTES);
                uint32_t audit_pal = count_palette_nonzero(&core.video, 0, RTYPE_M72_PALETTE_COLORS);
                ESP_LOGI(TAG, "S3 PERF irq_s=%llu ips=%llu render_irq=%u cpu_us=%llu render_us=%llu/%u present_us=%llu/%u frame=0x%04x root=0x%04x scroll=(%u,%u)/(%u,%u) vram=%u/%u spr=%u pal=%u fb_nz=%u fb_box=%u,%u-%u,%u",
                         (unsigned long long)((dirq * 1000000ull) / dt_us),
                         (unsigned long long)((dinsn * 1000000ull) / dt_us),
                         (unsigned)full_render_irq_interval,
                         (unsigned long long)full_prof_cpu_us,
                         (unsigned long long)avg_render_us, (unsigned)full_prof_renders,
                         (unsigned long long)avg_present_us, (unsigned)full_prof_renders,
                         (unsigned)rtype_m72_core_read16(&core, 0x42eb4u),
                         (unsigned)rtype_m72_core_read16(&core, 0x40000u),
                         (unsigned)core.video.scrollx[0], (unsigned)core.video.scrolly[0],
                         (unsigned)core.video.scrollx[1], (unsigned)core.video.scrolly[1],
                         (unsigned)audit_vram0, (unsigned)audit_vram1, (unsigned)audit_spr, (unsigned)audit_pal,
                         (unsigned)full_last_fb_audit.nonzero,
                         (unsigned)full_last_fb_audit.min_x, (unsigned)full_last_fb_audit.min_y,
                         (unsigned)full_last_fb_audit.max_x, (unsigned)full_last_fb_audit.max_y);
#else
                ESP_LOGI(TAG, "S3 PERF irq_s=%llu ips=%llu render_irq=%u cpu_us=%llu render_us=%llu/%u present_us=%llu/%u frame=0x%04x root=0x%04x scroll=(%u,%u)/(%u,%u)",
                         (unsigned long long)((dirq * 1000000ull) / dt_us),
                         (unsigned long long)((dinsn * 1000000ull) / dt_us),
                         (unsigned)full_render_irq_interval,
                         (unsigned long long)full_prof_cpu_us,
                         (unsigned long long)avg_render_us, (unsigned)full_prof_renders,
                         (unsigned long long)avg_present_us, (unsigned)full_prof_renders,
                         (unsigned)rtype_m72_core_read16(&core, 0x42eb4u),
                         (unsigned)rtype_m72_core_read16(&core, 0x40000u),
                         (unsigned)core.video.scrollx[0], (unsigned)core.video.scrolly[0],
                         (unsigned)core.video.scrollx[1], (unsigned)core.video.scrolly[1]);
#endif
                full_prof_cpu_us = 0;
                full_prof_render_us = 0;
                full_prof_present_us = 0;
                full_prof_renders = 0;
                full_last_perf_insn = full_cpu.insn;
                full_last_perf_irq = full_cpu.interrupt_count;
                full_last_perf_us = now_us;
            }
        }
#endif
        if (core_err == ESP_OK) {
#if defined(RTYPE_BOARD_ESP32_8048S043C)
            if (full_cpu_running) {
                if (!full_render_due) {
                    uint64_t pending_irqs = full_cpu.interrupt_count - full_last_presented_irq;
                    uint16_t cur_root = rtype_m72_core_read16(&core, 0x40000u);
                    uint16_t q_head_now = rtype_m72_core_read16(&core, 0x42ed8u);
                    uint16_t q_tail_now = rtype_m72_core_read16(&core, 0x42edau);
                    if (pending_irqs < full_render_irq_interval || cur_root != 0x0aa6u) {
                        vTaskDelay(pdMS_TO_TICKS(1));
                        continue;
                    }
                    if (q_head_now != q_tail_now && pending_irqs < full_render_irq_interval * 4u) {
                        vTaskDelay(pdMS_TO_TICKS(1));
                        continue;
                    }
                    full_render_due_irq = full_cpu.interrupt_count;
                }
                full_render_due = false;
                full_last_presented_irq = full_render_due_irq;
                uint32_t drain_budget = 600000u;
                while (drain_budget-- && !full_cpu.halted) {
                    uint16_t q_head_now = rtype_m72_core_read16(&core, 0x42ed8u);
                    uint16_t q_tail_now = rtype_m72_core_read16(&core, 0x42edau);
                    if (q_head_now == q_tail_now) break;
                    rtype_i86_step(&full_cpu);
                }
                int64_t full_render_begin_us = esp_timer_get_time();
                rtype_m72_core_render_frame(&core, fb);
#if RTYPE_S3_RENDER_AUDIT
                full_last_fb_audit = audit_framebuffer(fb);
#endif
                full_prof_render_us += (uint64_t)(esp_timer_get_time() - full_render_begin_us);
                full_prof_renders++;
            } else
#endif
            {
                rtype_m72_video_seed_probe_scene(&core.video, frame);
                rtype_m72_core_render_frame(&core, fb);
            }
        } else {
            rtype_video_render_boot_pattern(fb, frame);
        }
        int64_t full_present_begin_us = esp_timer_get_time();
        esp_err_t err = rtype_display_present_rgb565(fb, RTYPE_GAME_W, RTYPE_GAME_H);
#if defined(RTYPE_BOARD_ESP32_8048S043C)
        full_prof_present_us += (uint64_t)(esp_timer_get_time() - full_present_begin_us);
#endif
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "present failed: %s", esp_err_to_name(err));
        }
        if (fb_next != NULL) {
            uint16_t *tmp = fb;
            fb = fb_next;
            fb_next = tmp;
#if defined(RTYPE_BOARD_ESP32_8048S043C)
            // S3 display has its own snapshot queue/task; keep the emulator core fed.
            vTaskDelay(pdMS_TO_TICKS(1));
#else
            vTaskDelay(pdMS_TO_TICKS(33));
#endif
        } else {
            // Give an async display worker time to finish reading the only
            // source buffer before the next render overwrites it.
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
