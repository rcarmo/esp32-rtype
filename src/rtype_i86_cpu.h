#ifndef RTYPE_I86_CPU_H
#define RTYPE_I86_CPU_H

#include "rtype_m72_core.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RTYPE_I86_AX = 0,
    RTYPE_I86_CX = 1,
    RTYPE_I86_DX = 2,
    RTYPE_I86_BX = 3,
    RTYPE_I86_SP = 4,
    RTYPE_I86_BP = 5,
    RTYPE_I86_SI = 6,
    RTYPE_I86_DI = 7,
} rtype_i86_reg_t;

typedef enum {
    RTYPE_I86_ES = 0,
    RTYPE_I86_CS = 1,
    RTYPE_I86_SS = 2,
    RTYPE_I86_DS = 3,
} rtype_i86_seg_t;

typedef struct {
    rtype_m72_core_t *core;
    uint16_t r[8];
    uint16_t s[4];
    uint16_t ip;
    bool cf;
    bool pf;
    bool af;
    bool zf;
    bool sf;
    bool of;
    bool df;
    bool iff;
    bool seg_override_active;
    uint16_t seg_override_value;
    unsigned interrupt_depth;
    uint64_t iret_count;
    char stop_reason[128];
    bool halted;
    uint8_t last_opcode;
    uint64_t insn;
    uint64_t interrupt_count;
    uint16_t pending_frame_sp;
    bool pending_frame_sp_valid;
} rtype_i86_cpu_t;

void rtype_i86_reset(rtype_i86_cpu_t *cpu, rtype_m72_core_t *core);
uint32_t rtype_i86_pc(const rtype_i86_cpu_t *cpu);
void rtype_i86_frame_vector(rtype_i86_cpu_t *cpu, uint8_t vector);
void rtype_i86_interrupt(rtype_i86_cpu_t *cpu, uint8_t vector);
void rtype_i86_complete_frame_if_idle(rtype_i86_cpu_t *cpu);
bool rtype_i86_step(rtype_i86_cpu_t *cpu);
uint64_t rtype_i86_run(rtype_i86_cpu_t *cpu, uint64_t instruction_budget);

#endif
