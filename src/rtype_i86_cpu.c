#include "rtype_i86_cpu.h"

#include <string.h>

static uint32_t lin(const rtype_i86_cpu_t *cpu, uint16_t seg, uint16_t off) {
    (void)cpu;
    return (((uint32_t)seg << 4) + off) & 0xfffffu;
}

static uint8_t rb(rtype_i86_cpu_t *cpu, uint32_t addr) {
    return rtype_m72_core_read8(cpu->core, addr);
}

static uint16_t rw(rtype_i86_cpu_t *cpu, uint32_t addr) {
    return rtype_m72_core_read16(cpu->core, addr);
}

static void wb(rtype_i86_cpu_t *cpu, uint32_t addr, uint8_t v) {
    rtype_m72_core_write8(cpu->core, addr, v);
}

static void ww(rtype_i86_cpu_t *cpu, uint32_t addr, uint16_t v) {
    rtype_m72_core_write16(cpu->core, addr, v);
}

static uint8_t fetch8(rtype_i86_cpu_t *cpu) {
    uint8_t v = rb(cpu, rtype_i86_pc(cpu));
    cpu->ip++;
    return v;
}

static uint16_t fetch16(rtype_i86_cpu_t *cpu) {
    uint16_t v = fetch8(cpu);
    v |= (uint16_t)fetch8(cpu) << 8;
    return v;
}

static void push(rtype_i86_cpu_t *cpu, uint16_t v) {
    cpu->r[RTYPE_I86_SP] -= 2;
    ww(cpu, lin(cpu, cpu->s[RTYPE_I86_SS], cpu->r[RTYPE_I86_SP]), v);
}

static uint16_t pop(rtype_i86_cpu_t *cpu) {
    uint16_t v = rw(cpu, lin(cpu, cpu->s[RTYPE_I86_SS], cpu->r[RTYPE_I86_SP]));
    cpu->r[RTYPE_I86_SP] += 2;
    return v;
}

static uint8_t *reg8(rtype_i86_cpu_t *cpu, unsigned id) {
    uint8_t *w = (uint8_t *)cpu->r;
    switch (id & 7u) {
    case 0: return w + RTYPE_I86_AX * 2u;
    case 1: return w + RTYPE_I86_CX * 2u;
    case 2: return w + RTYPE_I86_DX * 2u;
    case 3: return w + RTYPE_I86_BX * 2u;
    case 4: return w + RTYPE_I86_AX * 2u + 1u;
    case 5: return w + RTYPE_I86_CX * 2u + 1u;
    case 6: return w + RTYPE_I86_DX * 2u + 1u;
    default: return w + RTYPE_I86_BX * 2u + 1u;
    }
}

static void set_logic8(rtype_i86_cpu_t *cpu, uint8_t v) {
    cpu->zf = v == 0;
    cpu->sf = (v & 0x80u) != 0;
    cpu->cf = false;
    cpu->of = false;
}

static void set_logic16(rtype_i86_cpu_t *cpu, uint16_t v) {
    cpu->zf = v == 0;
    cpu->sf = (v & 0x8000u) != 0;
    cpu->cf = false;
    cpu->of = false;
}

void rtype_i86_reset(rtype_i86_cpu_t *cpu, rtype_m72_core_t *core) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->core = core;
    cpu->s[RTYPE_I86_CS] = 0xf000u;
    cpu->s[RTYPE_I86_SS] = 0x4000u;
    cpu->ip = 0xfff0u;
}

uint32_t rtype_i86_pc(const rtype_i86_cpu_t *cpu) {
    if (cpu == NULL) return 0;
    return lin(cpu, cpu->s[RTYPE_I86_CS], cpu->ip);
}

void rtype_i86_frame_vector(rtype_i86_cpu_t *cpu, uint8_t vector) {
    if (cpu == NULL || cpu->core == NULL) return;
    cpu->pending_frame_sp = cpu->r[RTYPE_I86_SP];
    cpu->pending_frame_sp_valid = true;
    cpu->interrupt_count++;
    cpu->ip = rtype_m72_core_read16(cpu->core, (uint32_t)vector * 4u);
    cpu->s[RTYPE_I86_CS] = rtype_m72_core_read16(cpu->core, (uint32_t)vector * 4u + 2u);
}

bool rtype_i86_step(rtype_i86_cpu_t *cpu) {
    if (cpu == NULL || cpu->core == NULL || cpu->halted) return false;
    uint8_t op = fetch8(cpu);
    cpu->last_opcode = op;
    cpu->insn++;

    if (op >= 0xb8 && op <= 0xbf) {
        cpu->r[op - 0xb8u] = fetch16(cpu);
        return true;
    }
    if (op >= 0xb0 && op <= 0xb7) {
        *reg8(cpu, op - 0xb0u) = fetch8(cpu);
        return true;
    }
    if (op >= 0x50 && op <= 0x57) {
        push(cpu, cpu->r[op - 0x50u]);
        return true;
    }
    if (op >= 0x58 && op <= 0x5f) {
        cpu->r[op - 0x58u] = pop(cpu);
        return true;
    }

    switch (op) {
    case 0x24: {
        uint8_t imm = fetch8(cpu);
        *reg8(cpu, 0) &= imm;
        set_logic8(cpu, *reg8(cpu, 0));
        return true;
    }
    case 0x25: {
        uint16_t imm = fetch16(cpu);
        cpu->r[RTYPE_I86_AX] &= imm;
        set_logic16(cpu, cpu->r[RTYPE_I86_AX]);
        return true;
    }
    case 0xe2: {
        int8_t d = (int8_t)fetch8(cpu);
        cpu->r[RTYPE_I86_CX]--;
        if (cpu->r[RTYPE_I86_CX] != 0) cpu->ip = (uint16_t)(cpu->ip + d);
        return true;
    }
    case 0xe4: {
        uint8_t p = fetch8(cpu);
        *reg8(cpu, 0) = rtype_m72_core_in8(cpu->core, p);
        return true;
    }
    case 0xe5: {
        uint8_t p = fetch8(cpu);
        cpu->r[RTYPE_I86_AX] = rtype_m72_core_in16(cpu->core, p);
        return true;
    }
    case 0xe6: {
        uint8_t p = fetch8(cpu);
        rtype_m72_core_out8(cpu->core, p, *reg8(cpu, 0));
        return true;
    }
    case 0xe7: {
        uint8_t p = fetch8(cpu);
        rtype_m72_core_out16(cpu->core, p, cpu->r[RTYPE_I86_AX]);
        return true;
    }
    case 0xea: {
        uint16_t off = fetch16(cpu);
        uint16_t seg = fetch16(cpu);
        cpu->ip = off;
        cpu->s[RTYPE_I86_CS] = seg;
        return true;
    }
    case 0xfa: cpu->iff = false; return true;
    case 0xfb: cpu->iff = true; return true;
    case 0xfc: cpu->df = false; return true;
    case 0xf4: cpu->halted = true; return false;
    default:
        cpu->halted = true;
        return false;
    }
}

uint64_t rtype_i86_run(rtype_i86_cpu_t *cpu, uint64_t instruction_budget) {
    uint64_t start = cpu ? cpu->insn : 0;
    while (cpu != NULL && !cpu->halted && cpu->insn - start < instruction_budget) {
        rtype_i86_step(cpu);
    }
    return cpu ? (cpu->insn - start) : 0;
}
