// Host-side R-Type/Irem M72 display-only harness.
// Reference layout: MAME src/mame/irem/m72.cpp and m72_v.cpp (BSD-3-Clause).
// This file contains project-local clean-room code for ROM loading, M72 memory,
// a small 8086-family bootstrap executor, and graphics-only rendering.

#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr uint32_t MEM_SIZE = 0x100000;
constexpr int FB_W = 384;
constexpr int FB_H = 256;

enum Reg16 { AX, CX, DX, BX, SP, BP, SI, DI };
enum Seg { ES, CS, SS, DS };

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
}

static uint8_t pal5(uint16_t v) {
    v &= 0x1f;
    return uint8_t((v << 3) | (v >> 2));
}

static std::vector<uint8_t> read_file(const std::string &path, size_t expected = 0) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "missing file: %s\n", path.c_str());
        std::exit(2);
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    if (expected && data.size() != expected) {
        std::fprintf(stderr, "bad size for %s: got %zu expected %zu\n", path.c_str(), data.size(), expected);
        std::exit(2);
    }
    return data;
}

static void copy_file(std::vector<uint8_t> &dst, size_t off, const std::string &path, size_t expected) {
    auto data = read_file(path, 0);
    if (data.size() < expected) {
        std::fprintf(stderr, "bad size for %s: got %zu expected at least %zu\n", path.c_str(), data.size(), expected);
        std::exit(2);
    }
    if (off + expected > dst.size()) {
        std::fprintf(stderr, "copy overflow %s\n", path.c_str());
        std::exit(2);
    }
    std::memcpy(dst.data() + off, data.data(), expected);
}

struct M72 {
    std::array<uint8_t, MEM_SIZE> mem{};
    std::vector<uint8_t> sprites = std::vector<uint8_t>(0x80000, 0xff);
    std::vector<uint8_t> tiles0 = std::vector<uint8_t>(0x20000, 0xff);
    std::vector<uint8_t> tiles1 = std::vector<uint8_t>(0x20000, 0xff);
    std::array<uint16_t, 512> palette{};
    std::array<uint8_t, 0x400> sprite_buffer{};
    std::array<uint16_t, FB_W * FB_H> framebuffer{};
    uint16_t scrollx[2] = {0, 0};
    uint16_t scrolly[2] = {0, 0};
    uint16_t raster_irq_position = 0;
    bool video_off = false;
    uint64_t mem_writes = 0;
    uint64_t port_writes = 0;
    uint64_t writes_spr = 0;
    uint64_t writes_pal0 = 0;
    uint64_t writes_pal1 = 0;
    uint64_t writes_vram0 = 0;
    uint64_t writes_vram1 = 0;
    mutable uint64_t render_palette_pixels = 0;
    mutable uint64_t render_fallback_pixels = 0;
    mutable uint64_t render_tile_pixels = 0;
    mutable uint64_t render_sprite_pixels = 0;
    mutable uint64_t render_visible_nonblack = 0;
    mutable uint64_t render_visible_tile_pixels = 0;
    mutable uint64_t render_visible_sprite_pixels = 0;
    mutable int render_min_x = 9999;
    mutable int render_min_y = 9999;
    mutable int render_max_x = -1;
    mutable int render_max_y = -1;
    mutable int render_raw_min_x = 9999;
    mutable int render_raw_min_y = 9999;
    mutable int render_raw_max_x = -1;
    mutable int render_raw_max_y = -1;

    uint32_t count_nonzero(uint32_t begin, uint32_t end) const {
        uint32_t n = 0;
        for (uint32_t a = begin; a < end; a++) if (mem[a & 0xfffff] != 0) n++;
        return n;
    }

    void init_palette() {
        for (unsigned i = 0; i < palette.size(); i++) {
            uint8_t r = uint8_t((i * 37u) & 0xffu);
            uint8_t g = uint8_t((i * 73u) & 0xffu);
            uint8_t b = uint8_t((i * 17u) & 0xffu);
            if ((i & 15u) == 0) r = g = b = 0;
            palette[i] = rgb565(r, g, b);
        }
    }

    void load_from_artifacts(const std::string &packed_dir, const std::string &rom_dir) {
        mem.fill(0xff);
        auto main_map = read_file(packed_dir + "/maincpu-map.bin", MEM_SIZE);
        std::memcpy(mem.data(), main_map.data(), main_map.size());

        // MAME R-Type/Japan ROM region layout for the user-supplied names.
        copy_file(sprites, 0x00000, rom_dir + "/cpu-00.bin", 0x10000);
        copy_file(sprites, 0x10000, rom_dir + "/cpu-01.bin", 0x08000);
        std::memcpy(sprites.data() + 0x18000, sprites.data() + 0x10000, 0x08000);
        copy_file(sprites, 0x20000, rom_dir + "/cpu-10.bin", 0x10000);
        copy_file(sprites, 0x30000, rom_dir + "/cpu-11.bin", 0x08000);
        std::memcpy(sprites.data() + 0x38000, sprites.data() + 0x30000, 0x08000);
        copy_file(sprites, 0x40000, rom_dir + "/cpu-20.bin", 0x10000);
        copy_file(sprites, 0x50000, rom_dir + "/cpu-21.bin", 0x08000);
        std::memcpy(sprites.data() + 0x58000, sprites.data() + 0x50000, 0x08000);
        copy_file(sprites, 0x60000, rom_dir + "/cpu-30.bin", 0x10000);
        copy_file(sprites, 0x70000, rom_dir + "/cpu-31.bin", 0x08000);
        std::memcpy(sprites.data() + 0x78000, sprites.data() + 0x70000, 0x08000);

        copy_file(tiles0, 0x00000, rom_dir + "/cpu-a0.bin", 0x08000);
        copy_file(tiles0, 0x08000, rom_dir + "/cpu-a1.bin", 0x08000);
        copy_file(tiles0, 0x10000, rom_dir + "/cpu-a2.bin", 0x08000);
        copy_file(tiles0, 0x18000, rom_dir + "/cpu-a3.bin", 0x08000);
        copy_file(tiles1, 0x00000, rom_dir + "/cpu-b0.bin", 0x08000);
        copy_file(tiles1, 0x08000, rom_dir + "/cpu-b1.bin", 0x08000);
        copy_file(tiles1, 0x10000, rom_dir + "/cpu-b2.bin", 0x08000);
        copy_file(tiles1, 0x18000, rom_dir + "/cpu-b3.bin", 0x08000);
        init_palette();
    }

    uint8_t read8(uint32_t addr) const {
        return mem[addr & 0xfffff];
    }

    uint16_t read16(uint32_t addr) const {
        return uint16_t(read8(addr) | (uint16_t(read8(addr + 1)) << 8));
    }

    void refresh_palette_group(unsigned group, uint32_t byte_addr) {
        // Palette RAM is byte-addressed in our mem array; MAME stores 16-bit words
        // with only D0-D4 connected and R/G/B split at offsets +0x000/+0x200/+0x400.
        uint32_t base = group ? 0xcc000 : 0xc8000;
        uint32_t word_off = ((byte_addr - base) >> 1) & ~0x100u;
        uint32_t color = word_off & 0x0ffu;
        uint16_t r = read16(base + ((color + 0x000u) << 1));
        uint16_t g = read16(base + ((color + 0x200u) << 1));
        uint16_t b = read16(base + ((color + 0x400u) << 1));
        palette[color + (group << 8)] = rgb565(pal5(r), pal5(g), pal5(b));
    }

    void write8(uint32_t addr, uint8_t value) {
        addr &= 0xfffff;
        // ROM areas are read-only except RAM/VRAM/palette/sound RAM. Keep reset mirror intact.
        if ((addr <= 0x3ffff) || (addr >= 0xffff0)) return;
        mem[addr] = value;
        mem_writes++;
        if (addr >= 0xc0000 && addr < 0xc0400) writes_spr++;
        if (addr >= 0xc8000 && addr <= 0xc8bff) { writes_pal0++; refresh_palette_group(0, addr); }
        if (addr >= 0xcc000 && addr <= 0xccbff) { writes_pal1++; refresh_palette_group(1, addr); }
        if (addr >= 0xd0000 && addr < 0xd4000) writes_vram0++;
        if (addr >= 0xd8000 && addr < 0xdc000) writes_vram1++;
    }

    void write16(uint32_t addr, uint16_t value) {
        write8(addr, uint8_t(value & 0xff));
        write8(addr + 1, uint8_t(value >> 8));
    }

    uint16_t in16(uint16_t port) {
        switch (port & 0xff) {
        case 0x00: return 0xffff; // IN0 all idle
        case 0x02: return 0xffff; // IN1 all idle
        case 0x04: return 0xfdfb; // R-Type MAME default DSW
        case 0x40: return 0x0000; // PIC low byte placeholder
        case 0x42: return 0x0000;
        default: return 0xffff;
        }
    }

    uint8_t in8(uint16_t port) {
        uint16_t v = in16(port);
        return (port & 1) ? uint8_t(v >> 8) : uint8_t(v & 0xff);
    }

    void out8(uint16_t port, uint8_t value) {
        port_writes++;
        switch (port & 0xff) {
        case 0x00: /* sound latch ignored */ break;
        case 0x02: video_off = (value & 0x08) != 0; break;
        case 0x04: std::memcpy(sprite_buffer.data(), mem.data() + 0xc0000u, sprite_buffer.size()); break;
        default: break;
        }
    }

    void out16(uint16_t port, uint16_t value) {
        port_writes++;
        switch (port & 0xff) {
        case 0x02: video_off = (value & 0x0008) != 0; break;
        case 0x04: std::memcpy(sprite_buffer.data(), mem.data() + 0xc0000u, sprite_buffer.size()); break;
        case 0x06: raster_irq_position = uint16_t((value & 0x1ffu) - 128u); break;
        case 0x80: scrolly[0] = value; break;
        case 0x82: scrollx[0] = value; break;
        case 0x84: scrolly[1] = value; break;
        case 0x86: scrollx[1] = value; break;
        default: break;
        }
    }

    uint8_t decode_tile_pixel(const std::vector<uint8_t> &region, unsigned code, unsigned x, unsigned y) const {
        const unsigned quarter = unsigned(region.size() / 4);
        const unsigned plane_offsets[4] = {quarter * 3, quarter * 2, quarter * 1, quarter * 0};
        const unsigned base = code * 8u;
        uint8_t pix = 0;
        for (unsigned p = 0; p < 4; p++) {
            unsigned byte_off = plane_offsets[p] + base + y;
            uint8_t b = (byte_off < region.size()) ? region[byte_off] : 0;
            pix |= ((b >> (7 - x)) & 1u) << p;
        }
        return pix;
    }

    uint8_t decode_sprite_pixel(unsigned code, unsigned x, unsigned y) const {
        const unsigned quarter = unsigned(sprites.size() / 4);
        const unsigned plane_offsets[4] = {quarter * 3, quarter * 2, quarter * 1, quarter * 0};
        const unsigned row = y & 15u;
        const unsigned col = x & 15u;
        const unsigned bit_off = (row * 8u) + ((col >= 8u) ? (16u * 8u + (col - 8u)) : col);
        const unsigned byte_in_char = bit_off >> 3;
        const unsigned bit_in_byte = 7u - (bit_off & 7u);
        const unsigned base = code * 32u;
        uint8_t pix = 0;
        for (unsigned p = 0; p < 4; p++) {
            unsigned byte_off = plane_offsets[p] + base + byte_in_char;
            uint8_t b = (byte_off < sprites.size()) ? sprites[byte_off] : 0;
            pix |= ((b >> bit_in_byte) & 1u) << p;
        }
        return pix;
    }

    uint16_t visible_color(unsigned palette_index, uint8_t pen) const {
        uint16_t c = palette[palette_index & 0x1ffu];
        if (pen != 0) {
            if (c == 0) render_fallback_pixels++; // actually black palette hits; retained as a diagnostic counter
            else render_palette_pixels++;
        }
        return c;
    }

    bool put_pixel(int x, int y, uint16_t color) {
        if (color != 0) {
            if (x < render_raw_min_x) render_raw_min_x = x;
            if (y < render_raw_min_y) render_raw_min_y = y;
            if (x > render_raw_max_x) render_raw_max_x = x;
            if (y > render_raw_max_y) render_raw_max_y = y;
        }
        // M72 raw screen is 512 pixels wide, with visible X range 64..447.
        // Convert raw hardware X into our 384-wide framebuffer coordinate.
        x -= 64;
        if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) {
            framebuffer[size_t(y) * FB_W + x] = color;
            if (color != 0) {
                render_visible_nonblack++;
                if (x < render_min_x) render_min_x = x;
                if (y < render_min_y) render_min_y = y;
                if (x > render_max_x) render_max_x = x;
                if (y > render_max_y) render_max_y = y;
                return true;
            }
        }
        return false;
    }

    void draw_tile_layer(const std::vector<uint8_t> &region, uint32_t vram_base, unsigned palette_base,
                         uint16_t sx_scroll, uint16_t sy_scroll, bool transparent) {
        for (unsigned ty = 0; ty < 64; ty++) {
            for (unsigned tx = 0; tx < 64; tx++) {
                uint32_t off = vram_base + ((ty * 64u + tx) * 4u);
                uint16_t code = read16(off) & 0x3fffu;
                uint16_t attr = read16(off + 2);
                unsigned color = attr & 0x0fu;
                bool flipx = (read16(off) & 0x4000u) != 0;
                bool flipy = (read16(off) & 0x8000u) != 0;
                int base_x = int(tx * 8u) - int(sx_scroll & 0x1ffu);
                int base_y = int(ty * 8u) - int(sy_scroll & 0x1ffu) - 128;
                while (base_x < -8) base_x += 512;
                while (base_y < -8) base_y += 512;
                for (unsigned py = 0; py < 8; py++) {
                    for (unsigned px = 0; px < 8; px++) {
                        unsigned rx = flipx ? (7 - px) : px;
                        unsigned ry = flipy ? (7 - py) : py;
                        uint8_t pen = decode_tile_pixel(region, code, rx, ry);
                        if (transparent && pen == 0) continue;
                        uint16_t rgb = visible_color(palette_base + color * 16u + pen, pen);
                        if (pen != 0) render_tile_pixels++;
                        if (put_pixel(base_x + int(px), base_y + int(py), rgb) && pen != 0) render_visible_tile_pixels++;
                    }
                }
            }
        }
    }

    void draw_sprites() {
        for (int offs = 0x400 - 8; offs >= 0; offs -= 8) {
            const uint8_t *sp = sprite_buffer.data() + offs;
            uint16_t syw = (uint16_t)sp[0] | ((uint16_t)sp[1] << 8);
            uint16_t code = (uint16_t)sp[2] | ((uint16_t)sp[3] << 8);
            uint16_t attr = (uint16_t)sp[4] | ((uint16_t)sp[5] << 8);
            uint16_t sxw = (uint16_t)sp[6] | ((uint16_t)sp[7] << 8);
            if ((syw | code | attr | sxw) == 0) continue;
            unsigned color = attr & 0x0fu;
            int sx = -256 + int(sxw & 0x03ffu);
            int sy = 384 - int(syw & 0x01ffu);
            bool flipx = (attr & 0x0800u) != 0;
            bool flipy = (attr & 0x0400u) != 0;
            unsigned w = 1u << ((attr >> 14) & 3u);
            unsigned h = 1u << ((attr >> 12) & 3u);
            sy -= int(16u * h);
            for (unsigned x = 0; x < w; x++) {
                for (unsigned y = 0; y < h; y++) {
                    unsigned c = code;
                    c += flipx ? 8u * (w - 1u - x) : 8u * x;
                    c += flipy ? (h - 1u - y) : y;
                    for (unsigned py = 0; py < 16; py++) {
                        for (unsigned px = 0; px < 16; px++) {
                            unsigned rx = flipx ? (15 - px) : px;
                            unsigned ry = flipy ? (15 - py) : py;
                            uint8_t pen = decode_sprite_pixel(c, rx, ry);
                            if (pen == 0) continue;
                            render_sprite_pixels++;
                            if (put_pixel(sx + int(x * 16u + px), sy + int(y * 16u + py), visible_color(color * 16u + pen, pen))) render_visible_sprite_pixels++;
                        }
                    }
                }
            }
        }
    }

    void seed_static_probe_vram() {
        // Fallback visible frame before the CPU has populated VRAM: real decoded ROM tiles,
        // arranged through the same M72 tile renderer. This keeps the host/firmware display
        // path end-to-end while CPU coverage is being deepened.
        for (unsigned i = 0; i < 64u * 64u; i++) {
            write16(0xd8000 + i * 4u, uint16_t(i & 0x0fffu));
            write16(0xd8000 + i * 4u + 2u, uint16_t((i >> 8) & 0x0f));
            write16(0xd0000 + i * 4u, uint16_t((i + 0x80u) & 0x0fffu));
            write16(0xd0000 + i * 4u + 2u, uint16_t(((i >> 7) + 2u) & 0x0f));
        }
    }

    void render_frame(bool seed_if_blank) {
        render_palette_pixels = 0;
        render_fallback_pixels = 0;
        render_tile_pixels = 0;
        render_sprite_pixels = 0;
        render_visible_nonblack = 0;
        render_visible_tile_pixels = 0;
        render_visible_sprite_pixels = 0;
        render_min_x = 9999;
        render_min_y = 9999;
        render_max_x = -1;
        render_max_y = -1;
        render_raw_min_x = 9999;
        render_raw_min_y = 9999;
        render_raw_max_x = -1;
        render_raw_max_y = -1;
        bool any_vram = false;
        for (uint32_t a = 0xd0000; a < 0xdc000; a++) any_vram |= mem[a] != 0;
        if (seed_if_blank && !any_vram) seed_static_probe_vram();
        std::fill(framebuffer.begin(), framebuffer.end(), video_off ? 0 : rgb565(4, 8, 16));
        if (video_off) return;
        draw_tile_layer(tiles1, 0xd8000, 256, scrollx[1], scrolly[1], false); // BG from Bx
        draw_tile_layer(tiles0, 0xd0000, 256, scrollx[0], scrolly[0], true);  // FG from Ax
        draw_sprites();
    }

    uint32_t palette_nonzero(unsigned begin, unsigned end) const {
        uint32_t n = 0;
        if (end > palette.size()) end = (unsigned)palette.size();
        for (unsigned i = begin; i < end; i++) if (palette[i] != 0) n++;
        return n;
    }

    uint32_t palette_ram_nonzero(uint32_t base) const {
        uint32_t n = 0;
        for (uint32_t a = base; a <= base + 0xbffu; a++) if (mem[a & 0xfffff] != 0) n++;
        return n;
    }

    void write_ppm(const std::string &path) const {
        std::ofstream out(path, std::ios::binary);
        out << "P6\n" << FB_W << " " << FB_H << "\n255\n";
        for (uint16_t c : framebuffer) {
            uint8_t r = uint8_t(((c >> 11) & 0x1f) * 255 / 31);
            uint8_t g = uint8_t(((c >> 5) & 0x3f) * 255 / 63);
            uint8_t b = uint8_t((c & 0x1f) * 255 / 31);
            out.put(char(r)); out.put(char(g)); out.put(char(b));
        }
    }
};

struct Cpu8086 {
    M72 &m;
    uint16_t r[8]{};
    uint16_t s[4]{};
    uint16_t ip = 0xfff0;
    bool cf = false, pf = false, af = false, zf = false, sf = false, of = false, df = false, iff = false;
    bool seg_override_active = false;
    uint16_t seg_override_value = 0;
    unsigned interrupt_depth = 0;
    uint64_t interrupt_count = 0;
    uint64_t iret_count = 0;
    uint16_t pending_frame_sp = 0;
    bool pending_frame_sp_valid = false;
    bool halted = false;
    uint64_t insn = 0;
    uint8_t last_opcode = 0;
    static constexpr unsigned TRACE_LEN = 4096;
    std::array<uint32_t, TRACE_LEN> trace_pc{};
    std::array<uint8_t, TRACE_LEN> trace_op{};
    unsigned trace_pos = 0;
    uint32_t current_pc_for_write = 0;
    std::array<char, 4> watch_desc{{0, 0, 0, 0}};
    std::array<uint32_t, 4> watch_pc{{0, 0, 0, 0}};
    std::array<uint16_t, 4> watch_value{{0, 0, 0, 0}};
    uint16_t min_sp = 0xffff;
    uint32_t min_sp_pc = 0;
    uint32_t suspicious_sp_pc = 0;
    uint16_t suspicious_sp = 0;
    char stop_reason[160] = "";

    explicit Cpu8086(M72 &machine) : m(machine) {
        s[CS] = 0xf000;
        // R-Type work RAM is mapped at 0x40000. The ROM explicitly initializes
        // SP but never writes SS before using CALL/IRQ stack traffic; model the
        // board/CPU reset stack segment as the work-RAM segment for this harness.
        s[SS] = 0x4000;
    }

    uint32_t lin(uint16_t seg, uint16_t off) const { return ((uint32_t(seg) << 4) + off) & 0xfffff; }
    uint32_t pc() const { return lin(s[CS], ip); }
    uint8_t rb(uint32_t a) { return m.read8(a); }
    uint16_t rw(uint32_t a) { return m.read16(a); }
    void note_watch(uint32_t a, uint16_t v) {
        uint32_t x = a & 0xfffff;
        int idx = -1;
        if (x >= 0x40000 && x <= 0x40001) idx = 3;
        else if (x >= 0x43090 && x <= 0x43091) idx = 0;
        else if (x >= 0x43092 && x <= 0x43093) idx = 1;
        else if (x >= 0x430d9 && x <= 0x430da) idx = 2;
        if (idx >= 0) {
            watch_pc[idx] = current_pc_for_write;
            watch_value[idx] = v;
            watch_desc[idx] = (x & 1) ? 'B' : 'W';
        }
    }
    void wb(uint32_t a, uint8_t v) { note_watch(a, v); m.write8(a, v); }
    void ww(uint32_t a, uint16_t v) { note_watch(a, v); m.write16(a, v); }
    uint8_t fetch8() { uint8_t v = rb(pc()); ip++; return v; }
    uint16_t fetch16() { uint16_t v = uint16_t(fetch8()); v |= uint16_t(fetch8()) << 8; return v; }
    uint8_t &rl8(unsigned id) {
        static uint8_t dummy;
        dummy = 0;
        switch (id & 7) {
        case 0: return *reinterpret_cast<uint8_t *>(&r[AX]);
        case 1: return *(reinterpret_cast<uint8_t *>(&r[CX]));
        case 2: return *(reinterpret_cast<uint8_t *>(&r[DX]));
        case 3: return *(reinterpret_cast<uint8_t *>(&r[BX]));
        case 4: return *(reinterpret_cast<uint8_t *>(&r[AX]) + 1);
        case 5: return *(reinterpret_cast<uint8_t *>(&r[CX]) + 1);
        case 6: return *(reinterpret_cast<uint8_t *>(&r[DX]) + 1);
        case 7: return *(reinterpret_cast<uint8_t *>(&r[BX]) + 1);
        }
        return dummy;
    }
    uint16_t &rwreg(unsigned id) { return r[id & 7]; }
    static bool parity_even(uint8_t v) { v ^= v >> 4; v &= 0x0f; return ((0x6996u >> v) & 1u) == 0; }
    void set_szp8(uint8_t v) { zf = (v == 0); sf = (v & 0x80) != 0; pf = parity_even(v); }
    void set_szp16(uint16_t v) { zf = (v == 0); sf = (v & 0x8000) != 0; pf = parity_even((uint8_t)v); }
    void set_logic8(uint8_t v) { set_szp8(v); cf = of = false; af = false; }
    void set_logic16(uint16_t v) { set_szp16(v); cf = of = false; af = false; }
    void set_add8(uint8_t a, uint8_t b, uint8_t res) { set_szp8(res); cf = uint16_t(a) + b > 0xff; af = ((a ^ b ^ res) & 0x10) != 0; of = ((~(a ^ b) & (a ^ res)) & 0x80) != 0; }
    void set_add16(uint16_t a, uint16_t b, uint16_t res) { set_szp16(res); cf = uint32_t(a) + b > 0xffff; af = ((a ^ b ^ res) & 0x10) != 0; of = ((~(a ^ b) & (a ^ res)) & 0x8000) != 0; }
    void set_sub8(uint8_t a, uint8_t b, uint8_t res) { set_szp8(res); cf = a < b; af = ((a ^ b ^ res) & 0x10) != 0; of = ((a ^ b) & (a ^ res) & 0x80) != 0; }
    void set_sub16(uint16_t a, uint16_t b, uint16_t res) { set_szp16(res); cf = a < b; af = ((a ^ b ^ res) & 0x10) != 0; of = ((a ^ b) & (a ^ res) & 0x8000) != 0; }
    uint16_t make_flags() const {
        uint16_t f = 0xf002;
        if (cf) f |= 0x0001;
        if (pf) f |= 0x0004;
        if (af) f |= 0x0010;
        if (zf) f |= 0x0040;
        if (sf) f |= 0x0080;
        if (iff) f |= 0x0200;
        if (df) f |= 0x0400;
        if (of) f |= 0x0800;
        return f;
    }
    void set_flags_word(uint16_t f) {
        cf = (f & 0x0001) != 0;
        pf = (f & 0x0004) != 0;
        af = (f & 0x0010) != 0;
        zf = (f & 0x0040) != 0;
        sf = (f & 0x0080) != 0;
        iff = (f & 0x0200) != 0;
        df = (f & 0x0400) != 0;
        of = (f & 0x0800) != 0;
    }
    void note_sp() {
        if (r[SP] < min_sp) { min_sp = r[SP]; min_sp_pc = current_pc_for_write; }
        if (suspicious_sp_pc == 0 && r[SP] < 0x3800) { suspicious_sp_pc = current_pc_for_write; suspicious_sp = r[SP]; }
    }
    void push(uint16_t v) { r[SP] -= 2; note_sp(); ww(lin(s[SS], r[SP]), v); }
    uint16_t pop() { uint16_t v = rw(lin(s[SS], r[SP])); r[SP] += 2; return v; }
    void interrupt(uint8_t vector) {
        pending_frame_sp = r[SP];
        pending_frame_sp_valid = true;
        push(make_flags());
        push(s[CS]);
        push(ip);
        iff = false;
        interrupt_depth++;
        interrupt_count++;
        ip = rw(uint32_t(vector) * 4u);
        s[CS] = rw(uint32_t(vector) * 4u + 2u);
    }
    void vblank_callback(uint8_t vector) {
        // Host display-only model: fire the M72 frame vector from the idle loop
        // without stacking a synthetic CPU interrupt frame. The R-Type path we
        // observe behaves as a frame callback and returns to the wait loop via
        // game code, while full PIC/IRET fidelity can be deepened later.
        interrupt_count++;
        ip = rw(uint32_t(vector) * 4u);
        s[CS] = rw(uint32_t(vector) * 4u + 2u);
    }

    uint32_t ea(unsigned mod, unsigned rm) {
        int16_t disp = 0;
        if (mod == 1) disp = int8_t(fetch8());
        else if (mod == 2 || (mod == 0 && rm == 6)) disp = int16_t(fetch16());
        uint16_t base = 0;
        switch (rm) {
        case 0: base = r[BX] + r[SI]; break;
        case 1: base = r[BX] + r[DI]; break;
        case 2: base = r[BP] + r[SI]; break;
        case 3: base = r[BP] + r[DI]; break;
        case 4: base = r[SI]; break;
        case 5: base = r[DI]; break;
        case 6: base = (mod == 0) ? 0 : r[BP]; break;
        case 7: base = r[BX]; break;
        }
        uint16_t seg = (rm == 2 || rm == 3 || (rm == 6 && mod != 0)) ? s[SS] : s[DS];
        if (seg_override_active) seg = seg_override_value;
        return lin(seg, uint16_t(base + disp));
    }

    bool jcc(uint8_t op) const {
        switch (op & 0x0f) {
        case 0x0: return of;
        case 0x1: return !of;
        case 0x2: return cf;
        case 0x3: return !cf;
        case 0x4: return zf;
        case 0x5: return !zf;
        case 0x6: return cf || zf;
        case 0x7: return !cf && !zf;
        case 0x8: return sf;
        case 0x9: return !sf;
        case 0xa: return pf;
        case 0xb: return !pf;
        case 0xc: return sf != of;
        case 0xd: return sf == of;
        case 0xe: return zf || (sf != of);
        case 0xf: return !zf && (sf == of);
        default: return false;
        }
    }

    void fail(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(stop_reason, sizeof(stop_reason), fmt, ap);
        va_end(ap);
        halted = true;
    }

    bool step() {
        if (halted) return false;
        uint32_t before = pc();
        uint8_t op = fetch8();
        current_pc_for_write = before;
        trace_pc[trace_pos % TRACE_LEN] = before;
        trace_op[trace_pos % TRACE_LEN] = op;
        trace_pos++;
        last_opcode = op;
        insn++;
        if (op >= 0x70 && op <= 0x7f) { int8_t d = int8_t(fetch8()); if (jcc(op)) ip = uint16_t(ip + d); return true; }
        if (op >= 0xb8 && op <= 0xbf) { r[op - 0xb8] = fetch16(); return true; }
        if (op >= 0xb0 && op <= 0xb7) { rl8(op - 0xb0) = fetch8(); return true; }
        if (op >= 0x50 && op <= 0x57) { push(r[op - 0x50]); return true; }
        if (op >= 0x58 && op <= 0x5f) { r[op - 0x58] = pop(); return true; }
        if (op == 0x60) { current_pc_for_write = before; uint16_t oldsp = r[SP]; push(r[AX]); push(r[CX]); push(r[DX]); push(r[BX]); push(oldsp); push(r[BP]); push(r[SI]); push(r[DI]); return true; }
        if (op == 0x61) { r[DI] = pop(); r[SI] = pop(); r[BP] = pop(); pop(); r[BX] = pop(); r[DX] = pop(); r[CX] = pop(); r[AX] = pop(); return true; }
        if (op >= 0x40 && op <= 0x47) { uint16_t &v = r[op - 0x40]; v++; zf = v == 0; sf = v & 0x8000; return true; }
        if (op >= 0x48 && op <= 0x4f) { uint16_t &v = r[op - 0x48]; v--; zf = v == 0; sf = v & 0x8000; return true; }

        switch (op) {
        case 0x06: push(s[ES]); return true;
        case 0x07: s[ES] = pop(); return true;
        case 0x0e: push(s[CS]); return true;
        case 0x16: push(s[SS]); return true;
        case 0x17: s[SS] = pop(); return true;
        case 0x1e: push(s[DS]); return true;
        case 0x1f: s[DS] = pop(); return true;
        case 0x0f: /* NEC V30/80186 extra prefix/opcode: currently treated as NOP until traced */ return true;
        case 0x26: { bool old_active = seg_override_active; uint16_t old_value = seg_override_value; seg_override_active = true; seg_override_value = s[ES]; bool ok = step(); seg_override_active = old_active; seg_override_value = old_value; return ok; }
        case 0x2e: { bool old_active = seg_override_active; uint16_t old_value = seg_override_value; seg_override_active = true; seg_override_value = s[CS]; bool ok = step(); seg_override_active = old_active; seg_override_value = old_value; return ok; }
        case 0x36: { bool old_active = seg_override_active; uint16_t old_value = seg_override_value; seg_override_active = true; seg_override_value = s[SS]; bool ok = step(); seg_override_active = old_active; seg_override_value = old_value; return ok; }
        case 0x3e: { bool old_active = seg_override_active; uint16_t old_value = seg_override_value; seg_override_active = true; seg_override_value = s[DS]; bool ok = step(); seg_override_active = old_active; seg_override_value = old_value; return ok; }
        case 0x08: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t dst = 0; if (mod == 3) dst = rl8(rm); else { a = ea(mod, rm); dst = rb(a); } dst |= rl8(reg); set_logic8(dst); if (mod == 3) rl8(rm) = dst; else wb(a, dst); return true; }
        case 0x09: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t dst = 0; if (mod == 3) dst = r[rm]; else { a = ea(mod, rm); dst = rw(a); } dst |= r[reg]; set_logic16(dst); if (mod == 3) r[rm] = dst; else ww(a, dst); return true; }
        case 0x0a: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); rl8(reg) |= src; set_logic8(rl8(reg)); return true; }
        case 0x0b: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); r[reg] |= src; set_logic16(r[reg]); return true; }
        case 0x0c: { uint8_t imm = fetch8(); rl8(0) |= imm; set_logic8(rl8(0)); return true; }
        case 0x10: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t dst = 0; if (mod == 3) dst = rl8(rm); else { a = ea(mod, rm); dst = rb(a); } uint16_t sum = uint16_t(dst) + rl8(reg) + (cf ? 1 : 0); uint8_t res = uint8_t(sum); cf = sum > 0xff; zf = res == 0; sf = res & 0x80; if (mod == 3) rl8(rm) = res; else wb(a, res); return true; }
        case 0x11: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t dst = 0; if (mod == 3) dst = r[rm]; else { a = ea(mod, rm); dst = rw(a); } uint32_t sum = uint32_t(dst) + r[reg] + (cf ? 1 : 0); uint16_t res = uint16_t(sum); cf = sum > 0xffff; zf = res == 0; sf = res & 0x8000; if (mod == 3) r[rm] = res; else ww(a, res); return true; }
        case 0x12: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); uint16_t sum = uint16_t(rl8(reg)) + src + (cf ? 1 : 0); rl8(reg) = uint8_t(sum); cf = sum > 0xff; zf = rl8(reg) == 0; sf = rl8(reg) & 0x80; return true; }
        case 0x13: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); uint32_t sum = uint32_t(r[reg]) + src + (cf ? 1 : 0); r[reg] = uint16_t(sum); cf = sum > 0xffff; zf = r[reg] == 0; sf = r[reg] & 0x8000; return true; }
        case 0x20: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t dst = 0; if (mod == 3) dst = rl8(rm); else { a = ea(mod, rm); dst = rb(a); } dst &= rl8(reg); set_logic8(dst); if (mod == 3) rl8(rm) = dst; else wb(a, dst); return true; }
        case 0x21: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t dst = 0; if (mod == 3) dst = r[rm]; else { a = ea(mod, rm); dst = rw(a); } dst &= r[reg]; set_logic16(dst); if (mod == 3) r[rm] = dst; else ww(a, dst); return true; }
        case 0x22: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); rl8(reg) &= src; set_logic8(rl8(reg)); return true; }
        case 0x23: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); r[reg] &= src; set_logic16(r[reg]); return true; }
        case 0x24: { uint8_t imm = fetch8(); rl8(0) &= imm; set_logic8(rl8(0)); return true; }
        case 0x25: { uint16_t imm = fetch16(); r[AX] &= imm; set_logic16(r[AX]); return true; }
        case 0x28: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t dst = 0; if (mod == 3) dst = rl8(rm); else { a = ea(mod, rm); dst = rb(a); } uint8_t res = uint8_t(dst - rl8(reg)); set_sub8(dst, rl8(reg), res); if (mod == 3) rl8(rm) = res; else wb(a, res); return true; }
        case 0x29: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t dst = 0; if (mod == 3) dst = r[rm]; else { a = ea(mod, rm); dst = rw(a); } uint16_t res = uint16_t(dst - r[reg]); set_sub16(dst, r[reg], res); if (mod == 3) r[rm] = res; else ww(a, res); return true; }
        case 0x2a: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); uint8_t old = rl8(reg); rl8(reg) = uint8_t(old - src); set_sub8(old, src, rl8(reg)); return true; }
        case 0x2b: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); uint16_t old = r[reg]; r[reg] = uint16_t(old - src); set_sub16(old, src, r[reg]); return true; }
        case 0x2c: { uint8_t imm = fetch8(); uint8_t a = rl8(0); uint8_t res = uint8_t(a - imm); rl8(0) = res; set_sub8(a, imm, res); return true; }
        case 0x2d: { uint16_t imm = fetch16(); uint16_t old = r[AX]; r[AX] = uint16_t(old - imm); set_sub16(old, imm, r[AX]); return true; }
        case 0x27: { uint8_t old = rl8(0); bool oldcf = cf; if (((rl8(0) & 0x0f) > 9) || af) { rl8(0) = uint8_t(rl8(0) + 6); af = true; } else af = false; if (old > 0x99 || oldcf) { rl8(0) = uint8_t(rl8(0) + 0x60); cf = true; } else cf = false; set_szp8(rl8(0)); return true; }
        case 0x2f: { uint8_t old = rl8(0); bool oldcf = cf; if (((rl8(0) & 0x0f) > 9) || af) { rl8(0) = uint8_t(rl8(0) - 6); af = true; } else af = false; if (old > 0x99 || oldcf) { rl8(0) = uint8_t(rl8(0) - 0x60); cf = true; } else cf = false; set_szp8(rl8(0)); return true; }
        case 0x00: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t dst = 0; if (mod == 3) dst = rl8(rm); else { a = ea(mod, rm); dst = rb(a); } uint8_t res = uint8_t(dst + rl8(reg)); set_add8(dst, rl8(reg), res); if (mod == 3) rl8(rm) = res; else wb(a, res); return true; }
        case 0x01: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t dst = 0; if (mod == 3) dst = r[rm]; else { a = ea(mod, rm); dst = rw(a); } uint16_t res = uint16_t(dst + r[reg]); set_add16(dst, r[reg], res); if (mod == 3) r[rm] = res; else ww(a, res); return true; }
        case 0x02: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); uint8_t old = rl8(reg); rl8(reg) = uint8_t(old + src); set_add8(old, src, rl8(reg)); return true; }
        case 0x03: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); uint16_t old = r[reg]; r[reg] = uint16_t(old + src); set_add16(old, src, r[reg]); return true; }
        case 0x04: { uint8_t imm = fetch8(); uint8_t old = rl8(0); rl8(0) = uint8_t(old + imm); set_add8(old, imm, rl8(0)); return true; }
        case 0x05: { uint16_t imm = fetch16(); uint16_t old = r[AX]; r[AX] = uint16_t(old + imm); set_add16(old, imm, r[AX]); return true; }
        case 0x32: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); rl8(reg) ^= src; set_logic8(rl8(reg)); return true; }
        case 0x33: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); r[reg] ^= src; set_logic16(r[reg]); return true; }
        case 0x34: { uint8_t imm = fetch8(); rl8(0) ^= imm; set_logic8(rl8(0)); return true; }
        case 0x35: { uint16_t imm = fetch16(); r[AX] ^= imm; set_logic16(r[AX]); return true; }
        case 0x38: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t dst = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); uint8_t res = uint8_t(dst - rl8(reg)); set_sub8(dst, rl8(reg), res); return true; }
        case 0x39: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t dst = (mod == 3) ? r[rm] : rw(ea(mod, rm)); uint16_t res = uint16_t(dst - r[reg]); set_sub16(dst, r[reg], res); return true; }
        case 0x3a: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t src = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); uint8_t res = uint8_t(rl8(reg) - src); set_sub8(rl8(reg), src, res); return true; }
        case 0x3b: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t src = (mod == 3) ? r[rm] : rw(ea(mod, rm)); uint16_t res = uint16_t(r[reg] - src); set_sub16(r[reg], src, res); return true; }
        case 0x3c: { uint8_t imm = fetch8(); uint8_t res = uint8_t(rl8(0) - imm); set_sub8(rl8(0), imm, res); return true; }
        case 0x3d: { uint16_t imm = fetch16(); uint16_t res = uint16_t(r[AX] - imm); set_sub16(r[AX], imm, res); return true; }
        case 0x80: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t v = 0; if (mod == 3) v = rl8(rm); else { a = ea(mod, rm); v = rb(a); } uint8_t imm = fetch8(); uint8_t res = v; bool write = true; if (sub == 0) { res = uint8_t(v + imm); set_add8(v, imm, res); } else if (sub == 1) { res = uint8_t(v | imm); set_logic8(res); } else if (sub == 4) { res = uint8_t(v & imm); set_logic8(res); } else if (sub == 5) { res = uint8_t(v - imm); set_sub8(v, imm, res); } else if (sub == 6) { res = uint8_t(v ^ imm); set_logic8(res); } else if (sub == 7) { res = uint8_t(v - imm); set_sub8(v, imm, res); write = false; } else { fail("unsupported 80/%u at %05x", sub, before); return false; } if (write) { if (mod == 3) rl8(rm) = res; else wb(a, res); } return true; }
        case 0x81: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } uint16_t imm = fetch16(); uint16_t res = v; bool write = true; if (sub == 0) { res = uint16_t(v + imm); set_add16(v, imm, res); } else if (sub == 1) { res = uint16_t(v | imm); set_logic16(res); } else if (sub == 4) { res = uint16_t(v & imm); set_logic16(res); } else if (sub == 5) { res = uint16_t(v - imm); set_sub16(v, imm, res); } else if (sub == 7) { res = uint16_t(v - imm); set_sub16(v, imm, res); write = false; } else { fail("unsupported 81/%u at %05x", sub, before); return false; } if (write) { if (mod == 3) r[rm] = res; else ww(a, res); } return true; }
        case 0x83: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } uint16_t imm = uint16_t(int16_t(int8_t(fetch8()))); uint16_t res = v; bool write = true; if (sub == 0) { res = uint16_t(v + imm); set_add16(v, imm, res); } else if (sub == 1) { res = uint16_t(v | imm); set_logic16(res); } else if (sub == 5) { res = uint16_t(v - imm); set_sub16(v, imm, res); } else if (sub == 7) { res = uint16_t(v - imm); set_sub16(v, imm, res); write = false; } else { fail("unsupported 83/%u at %05x", sub, before); return false; } if (write) { if (mod == 3) r[rm] = res; else ww(a, res); } return true; }
        case 0x84: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint8_t v = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); set_logic8(uint8_t(v & rl8(reg))); return true; }
        case 0x85: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; uint16_t v = (mod == 3) ? r[rm] : rw(ea(mod, rm)); set_logic16(uint16_t(v & r[reg])); return true; }
        case 0x86: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; if (mod == 3) { uint8_t tmp = rl8(rm); rl8(rm) = rl8(reg); rl8(reg) = tmp; } else { uint32_t a = ea(mod, rm); uint8_t tmp = rb(a); wb(a, rl8(reg)); rl8(reg) = tmp; } return true; }
        case 0x87: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; if (mod == 3) { uint16_t tmp = r[rm]; r[rm] = r[reg]; r[reg] = tmp; } else { uint32_t a = ea(mod, rm); uint16_t tmp = rw(a); ww(a, r[reg]); r[reg] = tmp; } return true; }
        case 0x8c: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 3, rm = mr & 7; if (mod == 3) r[rm] = s[reg]; else ww(ea(mod, rm), s[reg]); return true; }
        case 0x8e: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 3, rm = mr & 7; s[reg] = (mod == 3) ? r[rm] : rw(ea(mod, rm)); return true; }
        case 0x8f: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; if (sub != 0) { fail("unsupported 8F/%u at %05x", sub, before); return false; } uint16_t v = pop(); if (mod == 3) r[rm] = v; else ww(ea(mod, rm), v); return true; }
        case 0x8b: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; r[reg] = (mod == 3) ? r[rm] : rw(ea(mod, rm)); return true; }
        case 0x89: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; if (mod == 3) r[rm] = r[reg]; else ww(ea(mod, rm), r[reg]); return true; }
        case 0x88: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; if (mod == 3) rl8(rm) = rl8(reg); else wb(ea(mod, rm), rl8(reg)); return true; }
        case 0x8a: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, reg = (mr >> 3) & 7, rm = mr & 7; rl8(reg) = (mod == 3) ? rl8(rm) : rb(ea(mod, rm)); return true; }
        case 0xa0: { uint16_t off = fetch16(); uint16_t seg = seg_override_active ? seg_override_value : s[DS]; rl8(0) = rb(lin(seg, off)); return true; }
        case 0xa1: { uint16_t off = fetch16(); uint16_t seg = seg_override_active ? seg_override_value : s[DS]; r[AX] = rw(lin(seg, off)); return true; }
        case 0xa2: { uint16_t off = fetch16(); uint16_t seg = seg_override_active ? seg_override_value : s[DS]; wb(lin(seg, off), rl8(0)); return true; }
        case 0xa3: { uint16_t off = fetch16(); uint16_t seg = seg_override_active ? seg_override_value : s[DS]; ww(lin(seg, off), r[AX]); return true; }
        case 0xbc: r[SP] = fetch16(); return true;
        case 0x98: r[AX] = uint16_t(int16_t(int8_t(rl8(0)))); return true;
        case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xffff : 0x0000; return true;
        case 0x9a: { uint16_t off = fetch16(); uint16_t seg = fetch16(); push(s[CS]); push(ip); s[CS] = seg; ip = off; return true; }
        case 0xc0: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t v = 0; if (mod == 3) v = rl8(rm); else { a = ea(mod, rm); v = rb(a); } uint8_t count = fetch8() & 0x1f; uint8_t res = v; while (count--) { if (sub == 0) { cf = (res & 0x80) != 0; res = uint8_t((res << 1) | (res >> 7)); } else if (sub == 1) { cf = (res & 1) != 0; res = uint8_t((res >> 1) | (res << 7)); } else if (sub == 2) { bool oldcf = cf; cf = (res & 0x80) != 0; res = uint8_t((res << 1) | (oldcf ? 1 : 0)); } else if (sub == 3) { bool oldcf = cf; cf = (res & 1) != 0; res = uint8_t((res >> 1) | (oldcf ? 0x80 : 0)); } else if (sub == 4) { cf = (res & 0x80) != 0; res = uint8_t(res << 1); } else if (sub == 5) { cf = (res & 1) != 0; res = uint8_t(res >> 1); } else if (sub == 7) { cf = (res & 1) != 0; res = uint8_t(int8_t(res) >> 1); } else { fail("unsupported C0/%u at %05x", sub, before); return false; } } if (sub >= 4) set_logic8(res); if (mod == 3) rl8(rm) = res; else wb(a, res); return true; }
        case 0xc1: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } uint8_t count = fetch8() & 0x1f; uint16_t res = v; while (count--) { if (sub == 0) { cf = (res & 0x8000) != 0; res = uint16_t((res << 1) | (res >> 15)); } else if (sub == 1) { cf = (res & 1) != 0; res = uint16_t((res >> 1) | (res << 15)); } else if (sub == 2) { bool oldcf = cf; cf = (res & 0x8000) != 0; res = uint16_t((res << 1) | (oldcf ? 1 : 0)); } else if (sub == 3) { bool oldcf = cf; cf = (res & 1) != 0; res = uint16_t((res >> 1) | (oldcf ? 0x8000 : 0)); } else if (sub == 4) { cf = (res & 0x8000) != 0; res = uint16_t(res << 1); } else if (sub == 5) { cf = (res & 1) != 0; res = uint16_t(res >> 1); } else if (sub == 7) { cf = (res & 1) != 0; res = uint16_t(int16_t(res) >> 1); } else { fail("unsupported C1/%u at %05x", sub, before); return false; } } if (sub >= 4) set_logic16(res); if (mod == 3) r[rm] = res; else ww(a, res); return true; }
        case 0xc2: { uint16_t adj = fetch16(); if (adj > 0x40) { fail("implausible RET imm16 adj=0x%04x at %05x", adj, before); return false; } ip = pop(); r[SP] = uint16_t(r[SP] + adj); return true; }
        case 0xc3: ip = pop(); return true;
        case 0xca: { uint16_t adj = fetch16(); ip = pop(); s[CS] = pop(); r[SP] = uint16_t(r[SP] + adj); return true; }
        case 0xcb: ip = pop(); s[CS] = pop(); return true;
        case 0xc6: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; if (sub != 0) { fail("unsupported C6/%u at %05x", sub, before); return false; } uint32_t a = 0; if (mod != 3) a = ea(mod, rm); uint8_t imm = fetch8(); if (mod == 3) rl8(rm) = imm; else wb(a, imm); return true; }
        case 0xc7: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; if (sub != 0) { fail("unsupported C7/%u at %05x", sub, before); return false; } uint32_t a = 0; if (mod != 3) a = ea(mod, rm); uint16_t imm = fetch16(); if (mod == 3) r[rm] = imm; else ww(a, imm); return true; }
        case 0xcd: { uint8_t num = fetch8(); fail("INT %02x at %05x", num, before); return false; }
        case 0xd0: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t v = 0; if (mod == 3) v = rl8(rm); else { a = ea(mod, rm); v = rb(a); } uint8_t res = v; if (sub == 0) { cf = (v & 0x80) != 0; res = uint8_t((v << 1) | (v >> 7)); } else if (sub == 1) { cf = (v & 1) != 0; res = uint8_t((v >> 1) | (v << 7)); } else if (sub == 2) { bool oldcf = cf; cf = (v & 0x80) != 0; res = uint8_t((v << 1) | (oldcf ? 1 : 0)); } else if (sub == 3) { bool oldcf = cf; cf = (v & 1) != 0; res = uint8_t((v >> 1) | (oldcf ? 0x80 : 0)); } else if (sub == 4) { cf = (v & 0x80) != 0; res = uint8_t(v << 1); set_logic8(res); } else if (sub == 5) { cf = (v & 1) != 0; res = uint8_t(v >> 1); set_logic8(res); } else if (sub == 7) { cf = (v & 1) != 0; res = uint8_t(int8_t(v) >> 1); set_logic8(res); } else { fail("unsupported D0/%u at %05x", sub, before); return false; } if (mod == 3) rl8(rm) = res; else wb(a, res); return true; }
        case 0xd1: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } uint16_t res = v; if (sub == 0) { cf = (v & 0x8000) != 0; res = uint16_t((v << 1) | (v >> 15)); } else if (sub == 1) { cf = (v & 1) != 0; res = uint16_t((v >> 1) | (v << 15)); } else if (sub == 2) { bool oldcf = cf; cf = (v & 0x8000) != 0; res = uint16_t((v << 1) | (oldcf ? 1 : 0)); } else if (sub == 3) { bool oldcf = cf; cf = (v & 1) != 0; res = uint16_t((v >> 1) | (oldcf ? 0x8000 : 0)); } else if (sub == 4) { cf = (v & 0x8000) != 0; res = uint16_t(v << 1); set_logic16(res); } else if (sub == 5) { cf = (v & 1) != 0; res = uint16_t(v >> 1); set_logic16(res); } else if (sub == 7) { cf = (v & 1) != 0; res = uint16_t(int16_t(v) >> 1); set_logic16(res); } else { fail("unsupported D1/%u at %05x", sub, before); return false; } if (mod == 3) r[rm] = res; else ww(a, res); return true; }
        case 0xd2: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint8_t count = rl8(1) & 0x1f; uint32_t a = 0; uint8_t v = 0; if (mod == 3) v = rl8(rm); else { a = ea(mod, rm); v = rb(a); } uint8_t res = v; while (count--) { if (sub == 0) { cf = (res & 0x80) != 0; res = uint8_t((res << 1) | (res >> 7)); } else if (sub == 1) { cf = (res & 1) != 0; res = uint8_t((res >> 1) | (res << 7)); } else if (sub == 2) { bool oldcf = cf; cf = (res & 0x80) != 0; res = uint8_t((res << 1) | (oldcf ? 1 : 0)); } else if (sub == 3) { bool oldcf = cf; cf = (res & 1) != 0; res = uint8_t((res >> 1) | (oldcf ? 0x80 : 0)); } else if (sub == 4) { cf = (res & 0x80) != 0; res = uint8_t(res << 1); } else if (sub == 5) { cf = (res & 1) != 0; res = uint8_t(res >> 1); } else if (sub == 7) { cf = (res & 1) != 0; res = uint8_t(int8_t(res) >> 1); } else { fail("unsupported D2/%u at %05x", sub, before); return false; } } if (sub >= 4) set_logic8(res); if (mod == 3) rl8(rm) = res; else wb(a, res); return true; }
        case 0xd3: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint8_t count = rl8(1) & 0x1f; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } uint16_t res = v; while (count--) { if (sub == 0) { cf = (res & 0x8000) != 0; res = uint16_t((res << 1) | (res >> 15)); } else if (sub == 1) { cf = (res & 1) != 0; res = uint16_t((res >> 1) | (res << 15)); } else if (sub == 2) { bool oldcf = cf; cf = (res & 0x8000) != 0; res = uint16_t((res << 1) | (oldcf ? 1 : 0)); } else if (sub == 3) { bool oldcf = cf; cf = (res & 1) != 0; res = uint16_t((res >> 1) | (oldcf ? 0x8000 : 0)); } else if (sub == 4) { cf = (res & 0x8000) != 0; res = uint16_t(res << 1); } else if (sub == 5) { cf = (res & 1) != 0; res = uint16_t(res >> 1); } else if (sub == 7) { cf = (res & 1) != 0; res = uint16_t(int16_t(res) >> 1); } else { fail("unsupported D3/%u at %05x", sub, before); return false; } } if (sub >= 4) set_logic16(res); if (mod == 3) r[rm] = res; else ww(a, res); return true; }
        case 0xcf: ip = pop(); s[CS] = pop(); set_flags_word(pop()); iret_count++; if (interrupt_depth > 0) interrupt_depth--; if (interrupt_depth == 0) pending_frame_sp_valid = false; return true;
        case 0xe0: { int8_t d = int8_t(fetch8()); r[CX]--; if (r[CX] != 0 && !zf) ip = uint16_t(ip + d); return true; }
        case 0xe1: { int8_t d = int8_t(fetch8()); r[CX]--; if (r[CX] != 0 && zf) ip = uint16_t(ip + d); return true; }
        case 0xe2: { int8_t d = int8_t(fetch8()); r[CX]--; if (r[CX] != 0) ip = uint16_t(ip + d); return true; }
        case 0xe4: { uint8_t p = fetch8(); rl8(0) = m.in8(p); return true; }
        case 0xe5: { uint8_t p = fetch8(); r[AX] = m.in16(p); return true; }
        case 0xe6: { uint8_t p = fetch8(); m.out8(p, rl8(0)); return true; }
        case 0xe7: { uint8_t p = fetch8(); m.out16(p, r[AX]); return true; }
        case 0xe8: { int16_t d = int16_t(fetch16()); push(ip); ip = uint16_t(ip + d); return true; }
        case 0xe9: { int16_t d = int16_t(fetch16()); ip = uint16_t(ip + d); return true; }
        case 0xea: { uint16_t off = fetch16(); uint16_t seg = fetch16(); ip = off; s[CS] = seg; return true; }
        case 0xeb: { int8_t d = int8_t(fetch8()); ip = uint16_t(ip + d); return true; }
        case 0xec: rl8(0) = m.in8(r[DX]); return true;
        case 0xed: r[AX] = m.in16(r[DX]); return true;
        case 0xee: m.out8(r[DX], rl8(0)); return true;
        case 0xef: m.out16(r[DX], r[AX]); return true;
        case 0xa4: { uint16_t seg = seg_override_active ? seg_override_value : s[DS]; uint8_t v = rb(lin(seg, r[SI])); wb(lin(s[ES], r[DI]), v); r[SI] = uint16_t(r[SI] + (df ? -1 : 1)); r[DI] = uint16_t(r[DI] + (df ? -1 : 1)); return true; }
        case 0xa5: { uint16_t seg = seg_override_active ? seg_override_value : s[DS]; uint16_t v = rw(lin(seg, r[SI])); ww(lin(s[ES], r[DI]), v); r[SI] = uint16_t(r[SI] + (df ? -2 : 2)); r[DI] = uint16_t(r[DI] + (df ? -2 : 2)); return true; }
        case 0xa8: { uint8_t imm = fetch8(); set_logic8(uint8_t(rl8(0) & imm)); return true; }
        case 0xa9: { uint16_t imm = fetch16(); uint16_t res = uint16_t(r[AX] & imm); set_logic16(res); return true; }
        case 0xaa: wb(lin(s[ES], r[DI]), rl8(0)); r[DI] = uint16_t(r[DI] + (df ? -1 : 1)); return true;
        case 0xab: ww(lin(s[ES], r[DI]), r[AX]); r[DI] = uint16_t(r[DI] + (df ? -2 : 2)); return true;
        case 0xac: { uint16_t seg = seg_override_active ? seg_override_value : s[DS]; rl8(0) = rb(lin(seg, r[SI])); r[SI] = uint16_t(r[SI] + (df ? -1 : 1)); return true; }
        case 0xad: { uint16_t seg = seg_override_active ? seg_override_value : s[DS]; r[AX] = rw(lin(seg, r[SI])); r[SI] = uint16_t(r[SI] + (df ? -2 : 2)); return true; }
        case 0xae: { uint8_t v = rb(lin(s[ES], r[DI])); uint8_t res = uint8_t(rl8(0) - v); set_sub8(rl8(0), v, res); r[DI] = uint16_t(r[DI] + (df ? -1 : 1)); return true; }
        case 0xaf: { uint16_t v = rw(lin(s[ES], r[DI])); uint16_t res = uint16_t(r[AX] - v); set_sub16(r[AX], v, res); r[DI] = uint16_t(r[DI] + (df ? -2 : 2)); return true; }
        case 0xf3: { uint8_t next = fetch8();
            if (next == 0xa4) { uint16_t seg = seg_override_active ? seg_override_value : s[DS]; while (r[CX] != 0) { uint8_t v = rb(lin(seg, r[SI])); wb(lin(s[ES], r[DI]), v); r[SI] = uint16_t(r[SI] + (df ? -1 : 1)); r[DI] = uint16_t(r[DI] + (df ? -1 : 1)); r[CX]--; } return true; }
            if (next == 0xa5) { uint16_t seg = seg_override_active ? seg_override_value : s[DS]; while (r[CX] != 0) { uint16_t v = rw(lin(seg, r[SI])); ww(lin(s[ES], r[DI]), v); r[SI] = uint16_t(r[SI] + (df ? -2 : 2)); r[DI] = uint16_t(r[DI] + (df ? -2 : 2)); r[CX]--; } return true; }
            if (next == 0xaa) { while (r[CX] != 0) { wb(lin(s[ES], r[DI]), rl8(0)); r[DI] = uint16_t(r[DI] + (df ? -1 : 1)); r[CX]--; } return true; }
            if (next == 0xab) { while (r[CX] != 0) { ww(lin(s[ES], r[DI]), r[AX]); r[DI] = uint16_t(r[DI] + (df ? -2 : 2)); r[CX]--; } return true; }
            if (next == 0xae) { while (r[CX] != 0) { uint8_t v = rb(lin(s[ES], r[DI])); uint8_t res = uint8_t(rl8(0) - v); set_sub8(rl8(0), v, res); r[DI] = uint16_t(r[DI] + (df ? -1 : 1)); r[CX]--; if (!zf) break; } return true; }
            if (next == 0xaf) { while (r[CX] != 0) { uint16_t v = rw(lin(s[ES], r[DI])); uint16_t res = uint16_t(r[AX] - v); set_sub16(r[AX], v, res); r[DI] = uint16_t(r[DI] + (df ? -2 : 2)); r[CX]--; if (!zf) break; } return true; }
            fail("unsupported REP opcode %02x at %05x", next, before); return false; }
        case 0xf4: fail("HLT at %05x", before); return false;
        case 0xf6: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t v = 0; if (mod == 3) v = rl8(rm); else { a = ea(mod, rm); v = rb(a); } if (sub == 0) { uint8_t imm = fetch8(); set_logic8(uint8_t(v & imm)); return true; } if (sub == 2) { v = uint8_t(~v); if (mod == 3) rl8(rm) = v; else wb(a, v); return true; } fail("unsupported F6/%u at %05x", sub, before); return false; }
        case 0xf8: cf = false; return true;
        case 0xf9: cf = true; return true;
        case 0xf5: cf = !cf; return true;
        case 0xfa: iff = false; return true;
        case 0xfb: iff = true; return true;
        case 0xfc: df = false; return true;
        case 0xfd: df = true; return true;
        case 0xf7: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } if (sub == 0) { uint16_t imm = fetch16(); set_logic16(uint16_t(v & imm)); return true; } if (sub == 2) { v = uint16_t(~v); if (mod == 3) r[rm] = v; else ww(a, v); return true; } if (sub == 3) { uint16_t old = v; v = uint16_t(0 - v); cf = old != 0; zf = v == 0; sf = (v & 0x8000) != 0; of = old == 0x8000; if (mod == 3) r[rm] = v; else ww(a, v); return true; } fail("unsupported F7/%u at %05x", sub, before); return false; }
        case 0xfe: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint8_t v = 0; if (mod == 3) v = rl8(rm); else { a = ea(mod, rm); v = rb(a); } if (sub == 0) v++; else if (sub == 1) v--; else { fail("unsupported FE/%u at %05x", sub, before); return false; } zf = v == 0; sf = v & 0x80; if (mod == 3) rl8(rm) = v; else wb(a, v); return true; }
        case 0xff: { uint8_t mr = fetch8(); unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7; uint32_t a = 0; uint16_t v = 0; if (mod == 3) v = r[rm]; else { a = ea(mod, rm); v = rw(a); } if (sub == 0) { v++; zf = v == 0; sf = v & 0x8000; if (mod == 3) r[rm] = v; else ww(a, v); return true; } if (sub == 1) { v--; zf = v == 0; sf = v & 0x8000; if (mod == 3) r[rm] = v; else ww(a, v); return true; } if (sub == 2) { push(ip); ip = v; return true; } if (sub == 4) { ip = v; return true; } if (sub == 6) { push(v); return true; } fail("unsupported FF/%u at %05x", sub, before); return false; }
        default: fail("unsupported opcode %02x at %05x cs:ip=%04x:%04x", op, before, s[CS], uint16_t(ip - 1)); return false;
        }
    }
};

void usage(const char *argv0) {
    std::fprintf(stderr, "Usage: %s [--packed DIR] [--rom-dir DIR] [--instructions N] [--out PPM] [--no-seed] [--stop-after-first-irq-loop]\n", argv0);
}

} // namespace

int main(int argc, char **argv) {
    std::string packed = "artifacts/packed-rtype";
    std::string rom_dir = "roms/extracted/rtype";
    std::string out = "artifacts/host-rtype-frame.ppm";
    uint64_t instruction_limit = 2000000;
    bool seed = true;
    bool stop_after_first_irq_loop = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--packed" && i + 1 < argc) packed = argv[++i];
        else if (a == "--rom-dir" && i + 1 < argc) rom_dir = argv[++i];
        else if (a == "--instructions" && i + 1 < argc) instruction_limit = std::strtoull(argv[++i], nullptr, 0);
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--no-seed") seed = false;
        else if (a == "--stop-after-first-irq-loop") stop_after_first_irq_loop = true;
        else { usage(argv[0]); return 2; }
    }

    M72 m;
    m.load_from_artifacts(packed, rom_dir);
    Cpu8086 cpu(m);
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t next_vblank_irq = 120000;
    bool main_loop_seen = false;
    while (!cpu.halted && cpu.insn < instruction_limit) {
        if (cpu.s[CS] == 0x0040 && cpu.ip >= 0x00d0 && cpu.ip <= 0x00f8) {
            main_loop_seen = true;
            if (cpu.interrupt_depth > 0) {
                // Some R-Type frame paths tail-return to the idle loop rather
                // than executing the ISR epilogue/IRET. Restore the exact
                // pre-frame SP to avoid colliding with work buffers at 0x2fd0.
                if (cpu.pending_frame_sp_valid) cpu.r[SP] = cpu.pending_frame_sp;
                else cpu.r[SP] = uint16_t(cpu.r[SP] + 6u * cpu.interrupt_depth);
                cpu.interrupt_depth = 0;
                cpu.pending_frame_sp_valid = false;
            }
            if (stop_after_first_irq_loop && cpu.interrupt_count > 0) break;
        }
        const bool vector_ready = (m.read16(0x20u * 4u) == 0x00fe && m.read16(0x20u * 4u + 2u) == 0x0040);
        const bool in_main_loop = (cpu.s[CS] == 0x0040 && cpu.ip >= 0x00d0 && cpu.ip <= 0x00f8);
        if (main_loop_seen && in_main_loop && vector_ready && cpu.iff && cpu.interrupt_depth == 0 && cpu.insn >= next_vblank_irq) {
            cpu.interrupt(0x20);
            next_vblank_irq += 120000;
        }
        cpu.step();
    }
    const auto t1 = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(t1 - t0).count();
    m.render_frame(seed);
    m.write_ppm(out);

    const uint16_t vec20_off = m.read16(0x20u * 4u);
    const uint16_t vec20_seg = m.read16(0x20u * 4u + 2u);
    const uint16_t vec22_off = m.read16(0x22u * 4u);
    const uint16_t vec22_seg = m.read16(0x22u * 4u + 2u);
    const uint16_t state_ptr = m.read16(0x40000u + 0x3090u);
    const uint16_t state_sel = m.read16(0x40000u + 0x3092u);
    const uint16_t state_timer = m.read16(0x40000u + 0x30d9u);
    const uint8_t low_3c = m.read8(0x40000u + 0x003cu);
    const uint8_t low_3e = m.read8(0x40000u + 0x003eu);
    const uint16_t main_state = m.read16(0x40000u + 0x3060u);
    const uint16_t root_state = m.read16(0x40000u + 0x0000u);
    const uint16_t diag_state = m.read16(0x40000u + 0x308eu);
    const uint16_t frame_counter = m.read16(0x40000u + 0x2eb4u);
    const uint16_t input0_shadow = m.read16(0x40000u + 0x2040u);
    const uint16_t input1_shadow = m.read16(0x40000u + 0x2042u);
    const uint16_t dsw_shadow = m.read16(0x40000u + 0x2044u);
    const uint16_t q_head = m.read16(0x40000u + 0x2ed8u);
    const uint16_t q_tail = m.read16(0x40000u + 0x2edau);
    const uint16_t q0_func = m.read16(0x40000u + 0x1e20u);
    const uint16_t q0_cx = m.read16(0x40000u + 0x1e22u);
    const uint16_t q0_dx = m.read16(0x40000u + 0x1e24u);
    const uint16_t obj20_func = m.read16(0x40020u);
    const uint16_t obj40_func = m.read16(0x40040u);
    const uint16_t obj540_func = m.read16(0x40540u);
    const uint16_t obj540_next = m.read16(0x4055cu);
    const uint16_t obj560_func = m.read16(0x40560u);
    const uint32_t pal0_nz = m.palette_nonzero(0, 256);
    const uint32_t pal1_nz = m.palette_nonzero(256, 512);
    const uint32_t palram0_nz = m.palette_ram_nonzero(0xc8000);
    const uint32_t palram1_nz = m.palette_ram_nonzero(0xcc000);
    std::printf("HOST_RTYPE_RUN pc=%05x cs:ip=%04x:%04x sp=%04x ss=%04x min_sp=%04x@%05x suspicious_sp=%04x@%05x insn=%llu halted=%d irq_depth=%u irq_count=%llu iret_count=%llu reason='%s' last_opcode=%02x mem_writes=%llu port_writes=%llu region_writes=spr:%llu,pal0:%llu,pal1:%llu,vram0:%llu,vram1:%llu vram0_nz=%u vram1_nz=%u spr_nz=%u render_tile_px=%llu render_sprite_px=%llu render_palette_px=%llu render_blackpal_px=%llu visible_nonblack=%llu visible_tile_px=%llu visible_sprite_px=%llu visible_bbox=%d,%d-%d,%d raw_bbox=%d,%d-%d,%d pal0_nz=%u pal1_nz=%u palram0_nz=%u palram1_nz=%u pal_samples=%04x,%04x,%04x,%04x,%04x,%04x vec20=%04x:%04x vec22=%04x:%04x raster=%u main_state=%04x root_state=%04x diag_state=%04x frame_counter=%04x input_shadow=%04x,%04x,%04x queue=%04x/%04x q0=%04x,%04x,%04x obj=%04x,%04x,%04x,%04x,%04x state_ptr=%04x state_sel=%04x state_timer=%04x low3c=%02x low3e=%02x watch3090=%c@%05x:%04x watch3092=%c@%05x:%04x watch30d9=%c@%05x:%04x watch0000=%c@%05x:%04x scroll=(%u,%u)/(%u,%u) video_off=%d out=%s seconds=%.6f ips=%.0f\n",
                cpu.pc(), cpu.s[CS], cpu.ip, cpu.r[SP], cpu.s[SS], cpu.min_sp, cpu.min_sp_pc, cpu.suspicious_sp, cpu.suspicious_sp_pc,
                (unsigned long long)cpu.insn, cpu.halted ? 1 : 0,
                cpu.interrupt_depth, (unsigned long long)cpu.interrupt_count, (unsigned long long)cpu.iret_count,
                cpu.stop_reason, cpu.last_opcode,
                (unsigned long long)m.mem_writes, (unsigned long long)m.port_writes,
                (unsigned long long)m.writes_spr, (unsigned long long)m.writes_pal0, (unsigned long long)m.writes_pal1,
                (unsigned long long)m.writes_vram0, (unsigned long long)m.writes_vram1,
                m.count_nonzero(0xd0000, 0xd4000), m.count_nonzero(0xd8000, 0xdc000),
                m.count_nonzero(0xc0000, 0xc0400),
                (unsigned long long)m.render_tile_pixels, (unsigned long long)m.render_sprite_pixels,
                (unsigned long long)m.render_palette_pixels, (unsigned long long)m.render_fallback_pixels,
                (unsigned long long)m.render_visible_nonblack,
                (unsigned long long)m.render_visible_tile_pixels, (unsigned long long)m.render_visible_sprite_pixels,
                m.render_min_x, m.render_min_y, m.render_max_x, m.render_max_y,
                m.render_raw_min_x, m.render_raw_min_y, m.render_raw_max_x, m.render_raw_max_y,
                pal0_nz, pal1_nz, palram0_nz, palram1_nz,
                m.palette[0], m.palette[1], m.palette[15], m.palette[256], m.palette[257], m.palette[271],
                vec20_seg, vec20_off, vec22_seg, vec22_off, (unsigned)(m.raster_irq_position & 0x1ffu),
                main_state, root_state, diag_state, frame_counter, input0_shadow, input1_shadow, dsw_shadow,
                q_head, q_tail, q0_func, q0_cx, q0_dx,
                obj20_func, obj40_func, obj540_func, obj540_next, obj560_func,
                state_ptr, state_sel, state_timer, low_3c, low_3e,
                cpu.watch_desc[0] ? cpu.watch_desc[0] : '-', cpu.watch_pc[0], cpu.watch_value[0],
                cpu.watch_desc[1] ? cpu.watch_desc[1] : '-', cpu.watch_pc[1], cpu.watch_value[1],
                cpu.watch_desc[2] ? cpu.watch_desc[2] : '-', cpu.watch_pc[2], cpu.watch_value[2],
                cpu.watch_desc[3] ? cpu.watch_desc[3] : '-', cpu.watch_pc[3], cpu.watch_value[3],
                m.scrollx[0], m.scrolly[0], m.scrollx[1], m.scrolly[1], m.video_off ? 1 : 0,
                out.c_str(), sec, sec > 0 ? (double)cpu.insn / sec : 0.0);
    if (cpu.halted || cpu.interrupt_depth > 0 || stop_after_first_irq_loop) {
        std::printf("HOST_RTYPE_TRACE_TAIL");
        unsigned n = cpu.trace_pos < Cpu8086::TRACE_LEN ? cpu.trace_pos : Cpu8086::TRACE_LEN;
        for (unsigned i = 0; i < n; i++) {
            unsigned idx = (cpu.trace_pos + Cpu8086::TRACE_LEN - n + i) % Cpu8086::TRACE_LEN;
            std::printf(" %05x:%02x", cpu.trace_pc[idx], cpu.trace_op[idx]);
        }
        std::printf("\n");
    }
    return (m.mem_writes > 0 && m.port_writes > 0) ? 0 : 1;
}
