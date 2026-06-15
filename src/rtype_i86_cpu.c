#include "rtype_i86_cpu.h"

#include <stdarg.h>
#include <stdio.h>
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

static void set_add8(rtype_i86_cpu_t *cpu, uint8_t a, uint8_t b, uint8_t res) {
    cpu->zf = res == 0;
    cpu->sf = (res & 0x80u) != 0;
    cpu->cf = (uint16_t)a + b > 0xffu;
    cpu->of = ((~(a ^ b) & (a ^ res)) & 0x80u) != 0;
}

static void set_add16(rtype_i86_cpu_t *cpu, uint16_t a, uint16_t b, uint16_t res) {
    cpu->zf = res == 0;
    cpu->sf = (res & 0x8000u) != 0;
    cpu->cf = (uint32_t)a + b > 0xffffu;
    cpu->of = ((~(a ^ b) & (a ^ res)) & 0x8000u) != 0;
}

static void set_sub8(rtype_i86_cpu_t *cpu, uint8_t a, uint8_t b, uint8_t res) {
    cpu->zf = res == 0;
    cpu->sf = (res & 0x80u) != 0;
    cpu->cf = a < b;
    cpu->of = ((a ^ b) & (a ^ res) & 0x80u) != 0;
}

static void set_sub16(rtype_i86_cpu_t *cpu, uint16_t a, uint16_t b, uint16_t res) {
    cpu->zf = res == 0;
    cpu->sf = (res & 0x8000u) != 0;
    cpu->cf = a < b;
    cpu->of = ((a ^ b) & (a ^ res) & 0x8000u) != 0;
}

static uint16_t make_flags(const rtype_i86_cpu_t *cpu) {
    uint16_t f = 0xf002u;
    if (cpu->cf) f |= 0x0001u;
    if (cpu->zf) f |= 0x0040u;
    if (cpu->sf) f |= 0x0080u;
    if (cpu->iff) f |= 0x0200u;
    if (cpu->df) f |= 0x0400u;
    if (cpu->of) f |= 0x0800u;
    return f;
}

static void set_flags_word(rtype_i86_cpu_t *cpu, uint16_t f) {
    cpu->cf = (f & 0x0001u) != 0;
    cpu->zf = (f & 0x0040u) != 0;
    cpu->sf = (f & 0x0080u) != 0;
    cpu->iff = (f & 0x0200u) != 0;
    cpu->df = (f & 0x0400u) != 0;
    cpu->of = (f & 0x0800u) != 0;
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

static uint32_t ea(rtype_i86_cpu_t *cpu, unsigned mod, unsigned rm) {
    int16_t disp = 0;
    if (mod == 1) disp = (int8_t)fetch8(cpu);
    else if (mod == 2 || (mod == 0 && rm == 6)) disp = (int16_t)fetch16(cpu);
    uint16_t base = 0;
    switch (rm) {
    case 0: base = (uint16_t)(cpu->r[RTYPE_I86_BX] + cpu->r[RTYPE_I86_SI]); break;
    case 1: base = (uint16_t)(cpu->r[RTYPE_I86_BX] + cpu->r[RTYPE_I86_DI]); break;
    case 2: base = (uint16_t)(cpu->r[RTYPE_I86_BP] + cpu->r[RTYPE_I86_SI]); break;
    case 3: base = (uint16_t)(cpu->r[RTYPE_I86_BP] + cpu->r[RTYPE_I86_DI]); break;
    case 4: base = cpu->r[RTYPE_I86_SI]; break;
    case 5: base = cpu->r[RTYPE_I86_DI]; break;
    case 6: base = (mod == 0) ? 0 : cpu->r[RTYPE_I86_BP]; break;
    default: base = cpu->r[RTYPE_I86_BX]; break;
    }
    uint16_t seg = (rm == 2 || rm == 3 || (rm == 6 && mod != 0)) ? cpu->s[RTYPE_I86_SS] : cpu->s[RTYPE_I86_DS];
    if (cpu->seg_override_active) seg = cpu->seg_override_value;
    return lin(cpu, seg, (uint16_t)(base + disp));
}

static bool jcc(const rtype_i86_cpu_t *cpu, uint8_t op) {
    switch (op & 0x0fu) {
    case 0x0: return cpu->of;
    case 0x1: return !cpu->of;
    case 0x2: return cpu->cf;
    case 0x3: return !cpu->cf;
    case 0x4: return cpu->zf;
    case 0x5: return !cpu->zf;
    case 0x6: return cpu->cf || cpu->zf;
    case 0x7: return !cpu->cf && !cpu->zf;
    case 0x8: return cpu->sf;
    case 0x9: return !cpu->sf;
    case 0xc: return cpu->sf != cpu->of;
    case 0xd: return cpu->sf == cpu->of;
    case 0xe: return cpu->zf || (cpu->sf != cpu->of);
    case 0xf: return !cpu->zf && (cpu->sf == cpu->of);
    default: return false;
    }
}

static void halt_reason(rtype_i86_cpu_t *cpu, const char *fmt, ...) {
    cpu->halted = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cpu->stop_reason, sizeof(cpu->stop_reason), fmt, ap);
    va_end(ap);
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

static void cpu_interrupt(rtype_i86_cpu_t *cpu, uint8_t vector) {
    cpu->pending_frame_sp = cpu->r[RTYPE_I86_SP];
    cpu->pending_frame_sp_valid = true;
    push(cpu, make_flags(cpu));
    push(cpu, cpu->s[RTYPE_I86_CS]);
    push(cpu, cpu->ip);
    cpu->iff = false;
    cpu->interrupt_depth++;
    cpu->interrupt_count++;
    cpu->ip = rtype_m72_core_read16(cpu->core, (uint32_t)vector * 4u);
    cpu->s[RTYPE_I86_CS] = rtype_m72_core_read16(cpu->core, (uint32_t)vector * 4u + 2u);
}

static void step_group_shift8(rtype_i86_cpu_t *cpu, uint8_t mr, uint8_t count, uint32_t before) {
    unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7;
    uint32_t a = 0;
    uint8_t res = 0;
    if (mod == 3) res = *reg8(cpu, rm); else { a = ea(cpu, mod, rm); res = rb(cpu, a); }
    while (count--) {
        if (sub == 0) { cpu->cf = (res & 0x80u) != 0; res = (uint8_t)((res << 1) | (res >> 7)); }
        else if (sub == 1) { cpu->cf = (res & 1u) != 0; res = (uint8_t)((res >> 1) | (res << 7)); }
        else if (sub == 2) { bool oldcf = cpu->cf; cpu->cf = (res & 0x80u) != 0; res = (uint8_t)((res << 1) | (oldcf ? 1u : 0u)); }
        else if (sub == 3) { bool oldcf = cpu->cf; cpu->cf = (res & 1u) != 0; res = (uint8_t)((res >> 1) | (oldcf ? 0x80u : 0u)); }
        else if (sub == 4) { cpu->cf = (res & 0x80u) != 0; res = (uint8_t)(res << 1); }
        else if (sub == 5) { cpu->cf = (res & 1u) != 0; res = (uint8_t)(res >> 1); }
        else if (sub == 7) { cpu->cf = (res & 1u) != 0; res = (uint8_t)((int8_t)res >> 1); }
        else { halt_reason(cpu, "unsupported shift8/%u at %05x", sub, before); return; }
    }
    if (sub >= 4) set_logic8(cpu, res);
    if (mod == 3) *reg8(cpu, rm) = res; else wb(cpu, a, res);
}

static void step_group_shift16(rtype_i86_cpu_t *cpu, uint8_t mr, uint8_t count, uint32_t before) {
    unsigned mod = mr >> 6, sub = (mr >> 3) & 7, rm = mr & 7;
    uint32_t a = 0;
    uint16_t res = 0;
    if (mod == 3) res = cpu->r[rm]; else { a = ea(cpu, mod, rm); res = rw(cpu, a); }
    while (count--) {
        if (sub == 0) { cpu->cf = (res & 0x8000u) != 0; res = (uint16_t)((res << 1) | (res >> 15)); }
        else if (sub == 1) { cpu->cf = (res & 1u) != 0; res = (uint16_t)((res >> 1) | (res << 15)); }
        else if (sub == 2) { bool oldcf = cpu->cf; cpu->cf = (res & 0x8000u) != 0; res = (uint16_t)((res << 1) | (oldcf ? 1u : 0u)); }
        else if (sub == 3) { bool oldcf = cpu->cf; cpu->cf = (res & 1u) != 0; res = (uint16_t)((res >> 1) | (oldcf ? 0x8000u : 0u)); }
        else if (sub == 4) { cpu->cf = (res & 0x8000u) != 0; res = (uint16_t)(res << 1); }
        else if (sub == 5) { cpu->cf = (res & 1u) != 0; res = (uint16_t)(res >> 1); }
        else if (sub == 7) { cpu->cf = (res & 1u) != 0; res = (uint16_t)((int16_t)res >> 1); }
        else { halt_reason(cpu, "unsupported shift16/%u at %05x", sub, before); return; }
    }
    if (sub >= 4) set_logic16(cpu, res);
    if (mod == 3) cpu->r[rm] = res; else ww(cpu, a, res);
}

bool rtype_i86_step(rtype_i86_cpu_t *cpu) {
    if (cpu == NULL || cpu->core == NULL || cpu->halted) return false;
    uint32_t before = rtype_i86_pc(cpu);
    uint8_t op = fetch8(cpu);
    cpu->last_opcode = op;
    cpu->insn++;

    if (op >= 0x70 && op <= 0x7f) { int8_t d = (int8_t)fetch8(cpu); if (jcc(cpu, op)) cpu->ip = (uint16_t)(cpu->ip + d); return true; }
    if (op >= 0xb8 && op <= 0xbf) { cpu->r[op - 0xb8u] = fetch16(cpu); return true; }
    if (op >= 0xb0 && op <= 0xb7) { *reg8(cpu, op - 0xb0u) = fetch8(cpu); return true; }
    if (op >= 0x50 && op <= 0x57) { push(cpu, cpu->r[op - 0x50u]); return true; }
    if (op >= 0x58 && op <= 0x5f) { cpu->r[op - 0x58u] = pop(cpu); return true; }
    if (op >= 0x40 && op <= 0x47) { uint16_t *v = &cpu->r[op - 0x40u]; (*v)++; cpu->zf = *v == 0; cpu->sf = (*v & 0x8000u) != 0; return true; }
    if (op >= 0x48 && op <= 0x4f) { uint16_t *v = &cpu->r[op - 0x48u]; (*v)--; cpu->zf = *v == 0; cpu->sf = (*v & 0x8000u) != 0; return true; }
    if (op == 0x60) { uint16_t oldsp = cpu->r[RTYPE_I86_SP]; push(cpu,cpu->r[RTYPE_I86_AX]); push(cpu,cpu->r[RTYPE_I86_CX]); push(cpu,cpu->r[RTYPE_I86_DX]); push(cpu,cpu->r[RTYPE_I86_BX]); push(cpu,oldsp); push(cpu,cpu->r[RTYPE_I86_BP]); push(cpu,cpu->r[RTYPE_I86_SI]); push(cpu,cpu->r[RTYPE_I86_DI]); return true; }
    if (op == 0x61) { cpu->r[RTYPE_I86_DI]=pop(cpu); cpu->r[RTYPE_I86_SI]=pop(cpu); cpu->r[RTYPE_I86_BP]=pop(cpu); pop(cpu); cpu->r[RTYPE_I86_BX]=pop(cpu); cpu->r[RTYPE_I86_DX]=pop(cpu); cpu->r[RTYPE_I86_CX]=pop(cpu); cpu->r[RTYPE_I86_AX]=pop(cpu); return true; }

    switch (op) {
    case 0x06: push(cpu, cpu->s[RTYPE_I86_ES]); return true;
    case 0x07: cpu->s[RTYPE_I86_ES] = pop(cpu); return true;
    case 0x0e: push(cpu, cpu->s[RTYPE_I86_CS]); return true;
    case 0x16: push(cpu, cpu->s[RTYPE_I86_SS]); return true;
    case 0x17: cpu->s[RTYPE_I86_SS] = pop(cpu); return true;
    case 0x1e: push(cpu, cpu->s[RTYPE_I86_DS]); return true;
    case 0x1f: cpu->s[RTYPE_I86_DS] = pop(cpu); return true;
    case 0x26: { bool oa=cpu->seg_override_active; uint16_t ov=cpu->seg_override_value; cpu->seg_override_active=true; cpu->seg_override_value=cpu->s[RTYPE_I86_ES]; bool ok=rtype_i86_step(cpu); cpu->seg_override_active=oa; cpu->seg_override_value=ov; return ok; }
    case 0x2e: { bool oa=cpu->seg_override_active; uint16_t ov=cpu->seg_override_value; cpu->seg_override_active=true; cpu->seg_override_value=cpu->s[RTYPE_I86_CS]; bool ok=rtype_i86_step(cpu); cpu->seg_override_active=oa; cpu->seg_override_value=ov; return ok; }
    case 0x36: { bool oa=cpu->seg_override_active; uint16_t ov=cpu->seg_override_value; cpu->seg_override_active=true; cpu->seg_override_value=cpu->s[RTYPE_I86_SS]; bool ok=rtype_i86_step(cpu); cpu->seg_override_active=oa; cpu->seg_override_value=ov; return ok; }
    case 0x3e: { bool oa=cpu->seg_override_active; uint16_t ov=cpu->seg_override_value; cpu->seg_override_active=true; cpu->seg_override_value=cpu->s[RTYPE_I86_DS]; bool ok=rtype_i86_step(cpu); cpu->seg_override_active=oa; cpu->seg_override_value=ov; return ok; }
    case 0x0f: return true;
    case 0x01: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t dst=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); uint16_t res=(uint16_t)(dst+cpu->r[reg]); set_add16(cpu,dst,cpu->r[reg],res); if(mod==3)cpu->r[rm]=res; else ww(cpu,a,res); return true; }
    case 0x02: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t src=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); uint8_t old=*reg8(cpu,reg); *reg8(cpu,reg)=(uint8_t)(old+src); set_add8(cpu,old,src,*reg8(cpu,reg)); return true; }
    case 0x03: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); uint16_t old=cpu->r[reg]; cpu->r[reg]=(uint16_t)(old+src); set_add16(cpu,old,src,cpu->r[reg]); return true; }
    case 0x05: { uint16_t imm=fetch16(cpu); uint16_t old=cpu->r[RTYPE_I86_AX]; cpu->r[RTYPE_I86_AX]=(uint16_t)(old+imm); set_add16(cpu,old,imm,cpu->r[RTYPE_I86_AX]); return true; }
    case 0x08: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint8_t dst=(mod==3)?*reg8(cpu,rm):(a=ea(cpu,mod,rm),rb(cpu,a)); dst|=*reg8(cpu,reg); set_logic8(cpu,dst); if(mod==3)*reg8(cpu,rm)=dst; else wb(cpu,a,dst); return true; }
    case 0x09: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t dst=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); dst|=cpu->r[reg]; set_logic16(cpu,dst); if(mod==3)cpu->r[rm]=dst; else ww(cpu,a,dst); return true; }
    case 0x0a: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t src=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); *reg8(cpu,reg)|=src; set_logic8(cpu,*reg8(cpu,reg)); return true; }
    case 0x0b: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); cpu->r[reg]|=src; set_logic16(cpu,cpu->r[reg]); return true; }
    case 0x0c: { uint8_t imm=fetch8(cpu); *reg8(cpu,0)|=imm; set_logic8(cpu,*reg8(cpu,0)); return true; }
    case 0x10: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint8_t dst=(mod==3)?*reg8(cpu,rm):(a=ea(cpu,mod,rm),rb(cpu,a)); uint16_t sum=(uint16_t)dst+*reg8(cpu,reg)+(cpu->cf?1u:0u); uint8_t res=(uint8_t)sum; cpu->cf=sum>0xffu; cpu->zf=res==0; cpu->sf=(res&0x80u)!=0; if(mod==3)*reg8(cpu,rm)=res; else wb(cpu,a,res); return true; }
    case 0x11: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t dst=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); uint32_t sum=(uint32_t)dst+cpu->r[reg]+(cpu->cf?1u:0u); uint16_t res=(uint16_t)sum; cpu->cf=sum>0xffffu; cpu->zf=res==0; cpu->sf=(res&0x8000u)!=0; if(mod==3)cpu->r[rm]=res; else ww(cpu,a,res); return true; }
    case 0x12: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t src=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); uint16_t sum=(uint16_t)*reg8(cpu,reg)+src+(cpu->cf?1u:0u); *reg8(cpu,reg)=(uint8_t)sum; cpu->cf=sum>0xffu; cpu->zf=*reg8(cpu,reg)==0; cpu->sf=(*reg8(cpu,reg)&0x80u)!=0; return true; }
    case 0x13: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); uint32_t sum=(uint32_t)cpu->r[reg]+src+(cpu->cf?1u:0u); cpu->r[reg]=(uint16_t)sum; cpu->cf=sum>0xffffu; cpu->zf=cpu->r[reg]==0; cpu->sf=(cpu->r[reg]&0x8000u)!=0; return true; }
    case 0x20: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint8_t dst=(mod==3)?*reg8(cpu,rm):(a=ea(cpu,mod,rm),rb(cpu,a)); dst&=*reg8(cpu,reg); set_logic8(cpu,dst); if(mod==3)*reg8(cpu,rm)=dst; else wb(cpu,a,dst); return true; }
    case 0x21: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t dst=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); dst&=cpu->r[reg]; set_logic16(cpu,dst); if(mod==3)cpu->r[rm]=dst; else ww(cpu,a,dst); return true; }
    case 0x22: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t src=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); *reg8(cpu,reg)&=src; set_logic8(cpu,*reg8(cpu,reg)); return true; }
    case 0x23: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); cpu->r[reg]&=src; set_logic16(cpu,cpu->r[reg]); return true; }
    case 0x24: { uint8_t imm=fetch8(cpu); *reg8(cpu,0)&=imm; set_logic8(cpu,*reg8(cpu,0)); return true; }
    case 0x25: { uint16_t imm=fetch16(cpu); cpu->r[RTYPE_I86_AX]&=imm; set_logic16(cpu,cpu->r[RTYPE_I86_AX]); return true; }
    case 0x2b: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); uint16_t old=cpu->r[reg]; cpu->r[reg]=(uint16_t)(old-src); set_sub16(cpu,old,src,cpu->r[reg]); return true; }
    case 0x2c: { uint8_t imm=fetch8(cpu); uint8_t old=*reg8(cpu,0); *reg8(cpu,0)=(uint8_t)(old-imm); set_sub8(cpu,old,imm,*reg8(cpu,0)); return true; }
    case 0x2d: { uint16_t imm=fetch16(cpu); uint16_t old=cpu->r[RTYPE_I86_AX]; cpu->r[RTYPE_I86_AX]=(uint16_t)(old-imm); set_sub16(cpu,old,imm,cpu->r[RTYPE_I86_AX]); return true; }
    case 0x2f: { uint8_t old=*reg8(cpu,0); bool oldcf=cpu->cf; if((old&0x0fu)>9) *reg8(cpu,0)=(uint8_t)(*reg8(cpu,0)-6); if(old>0x99u||oldcf){*reg8(cpu,0)=(uint8_t)(*reg8(cpu,0)-0x60); cpu->cf=true;} else cpu->cf=false; cpu->zf=*reg8(cpu,0)==0; cpu->sf=(*reg8(cpu,0)&0x80u)!=0; return true; }
    case 0x32: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t src=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); *reg8(cpu,reg)^=src; set_logic8(cpu,*reg8(cpu,reg)); return true; }
    case 0x33: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); cpu->r[reg]^=src; set_logic16(cpu,cpu->r[reg]); return true; }
    case 0x38: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t dst=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); set_sub8(cpu,dst,*reg8(cpu,reg),(uint8_t)(dst-*reg8(cpu,reg))); return true; }
    case 0x39: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t dst=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); set_sub16(cpu,dst,cpu->r[reg],(uint16_t)(dst-cpu->r[reg])); return true; }
    case 0x3a: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint8_t src=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); set_sub8(cpu,*reg8(cpu,reg),src,(uint8_t)(*reg8(cpu,reg)-src)); return true; }
    case 0x3b: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; uint16_t src=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); set_sub16(cpu,cpu->r[reg],src,(uint16_t)(cpu->r[reg]-src)); return true; }
    case 0x3c: { uint8_t imm=fetch8(cpu); set_sub8(cpu,*reg8(cpu,0),imm,(uint8_t)(*reg8(cpu,0)-imm)); return true; }
    case 0x3d: { uint16_t imm=fetch16(cpu); set_sub16(cpu,cpu->r[RTYPE_I86_AX],imm,(uint16_t)(cpu->r[RTYPE_I86_AX]-imm)); return true; }
    case 0x80: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint8_t v=(mod==3)?*reg8(cpu,rm):(a=ea(cpu,mod,rm),rb(cpu,a)); uint8_t imm=fetch8(cpu),res=v; bool write=true; if(sub==0){res=(uint8_t)(v+imm);set_add8(cpu,v,imm,res);} else if(sub==4){res=(uint8_t)(v&imm);set_logic8(cpu,res);} else if(sub==5){res=(uint8_t)(v-imm);set_sub8(cpu,v,imm,res);} else if(sub==6){res=(uint8_t)(v^imm);set_logic8(cpu,res);} else if(sub==7){res=(uint8_t)(v-imm);set_sub8(cpu,v,imm,res);write=false;} else {halt_reason(cpu,"unsupported 80/%u at %05x",sub,before);return false;} if(write){if(mod==3)*reg8(cpu,rm)=res; else wb(cpu,a,res);} return true; }
    case 0x81: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t v=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); uint16_t imm=fetch16(cpu),res=v; bool write=true; if(sub==0){res=(uint16_t)(v+imm);set_add16(cpu,v,imm,res);} else if(sub==4){res=(uint16_t)(v&imm);set_logic16(cpu,res);} else if(sub==5){res=(uint16_t)(v-imm);set_sub16(cpu,v,imm,res);} else if(sub==7){res=(uint16_t)(v-imm);set_sub16(cpu,v,imm,res);write=false;} else {halt_reason(cpu,"unsupported 81/%u at %05x",sub,before);return false;} if(write){if(mod==3)cpu->r[rm]=res; else ww(cpu,a,res);} return true; }
    case 0x83: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t v=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); uint16_t imm=(uint16_t)(int16_t)(int8_t)fetch8(cpu),res=v; bool write=true; if(sub==0){res=(uint16_t)(v+imm);set_add16(cpu,v,imm,res);} else if(sub==5){res=(uint16_t)(v-imm);set_sub16(cpu,v,imm,res);} else if(sub==7){res=(uint16_t)(v-imm);set_sub16(cpu,v,imm,res);write=false;} else {halt_reason(cpu,"unsupported 83/%u at %05x",sub,before);return false;} if(write){if(mod==3)cpu->r[rm]=res; else ww(cpu,a,res);} return true; }
    case 0x88: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; if(mod==3)*reg8(cpu,rm)=*reg8(cpu,reg); else wb(cpu,ea(cpu,mod,rm),*reg8(cpu,reg)); return true; }
    case 0x89: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; if(mod==3)cpu->r[rm]=cpu->r[reg]; else ww(cpu,ea(cpu,mod,rm),cpu->r[reg]); return true; }
    case 0x8a: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; *reg8(cpu,reg)=(mod==3)?*reg8(cpu,rm):rb(cpu,ea(cpu,mod,rm)); return true; }
    case 0x8b: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&7,rm=mr&7; cpu->r[reg]=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); return true; }
    case 0x8c: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&3,rm=mr&7; if(mod==3)cpu->r[rm]=cpu->s[reg]; else ww(cpu,ea(cpu,mod,rm),cpu->s[reg]); return true; }
    case 0x8e: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,reg=(mr>>3)&3,rm=mr&7; cpu->s[reg]=(mod==3)?cpu->r[rm]:rw(cpu,ea(cpu,mod,rm)); return true; }
    case 0x98: cpu->r[RTYPE_I86_AX]=(uint16_t)(int16_t)(int8_t)*reg8(cpu,0); return true;
    case 0x99: cpu->r[RTYPE_I86_DX]=(cpu->r[RTYPE_I86_AX]&0x8000u)?0xffffu:0; return true;
    case 0x9a: { uint16_t off=fetch16(cpu),seg=fetch16(cpu); push(cpu,cpu->s[RTYPE_I86_CS]); push(cpu,cpu->ip); cpu->ip=off; cpu->s[RTYPE_I86_CS]=seg; return true; }
    case 0xa0: { uint16_t off=fetch16(cpu); uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; *reg8(cpu,0)=rb(cpu,lin(cpu,seg,off)); return true; }
    case 0xa1: { uint16_t off=fetch16(cpu); uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; cpu->r[RTYPE_I86_AX]=rw(cpu,lin(cpu,seg,off)); return true; }
    case 0xa2: { uint16_t off=fetch16(cpu); uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; wb(cpu,lin(cpu,seg,off),*reg8(cpu,0)); return true; }
    case 0xa3: { uint16_t off=fetch16(cpu); uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; ww(cpu,lin(cpu,seg,off),cpu->r[RTYPE_I86_AX]); return true; }
    case 0xa4: { uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; uint8_t v=rb(cpu,lin(cpu,seg,cpu->r[RTYPE_I86_SI])); wb(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),v); cpu->r[RTYPE_I86_SI]+=(cpu->df?-1:1); cpu->r[RTYPE_I86_DI]+=(cpu->df?-1:1); return true; }
    case 0xa5: { uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; uint16_t v=rw(cpu,lin(cpu,seg,cpu->r[RTYPE_I86_SI])); ww(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),v); cpu->r[RTYPE_I86_SI]+=(cpu->df?-2:2); cpu->r[RTYPE_I86_DI]+=(cpu->df?-2:2); return true; }
    case 0xa9: { uint16_t imm=fetch16(cpu); set_logic16(cpu,(uint16_t)(cpu->r[RTYPE_I86_AX]&imm)); return true; }
    case 0xaa: wb(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),*reg8(cpu,0)); cpu->r[RTYPE_I86_DI]+=(cpu->df?-1:1); return true;
    case 0xab: ww(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),cpu->r[RTYPE_I86_AX]); cpu->r[RTYPE_I86_DI]+=(cpu->df?-2:2); return true;
    case 0xac: { uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; *reg8(cpu,0)=rb(cpu,lin(cpu,seg,cpu->r[RTYPE_I86_SI])); cpu->r[RTYPE_I86_SI]+=(cpu->df?-1:1); return true; }
    case 0xad: { uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; cpu->r[RTYPE_I86_AX]=rw(cpu,lin(cpu,seg,cpu->r[RTYPE_I86_SI])); cpu->r[RTYPE_I86_SI]+=(cpu->df?-2:2); return true; }
    case 0xae: { uint8_t v=rb(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI])); set_sub8(cpu,*reg8(cpu,0),v,(uint8_t)(*reg8(cpu,0)-v)); cpu->r[RTYPE_I86_DI]+=(cpu->df?-1:1); return true; }
    case 0xaf: { uint16_t v=rw(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI])); set_sub16(cpu,cpu->r[RTYPE_I86_AX],v,(uint16_t)(cpu->r[RTYPE_I86_AX]-v)); cpu->r[RTYPE_I86_DI]+=(cpu->df?-2:2); return true; }
    case 0xc0: { uint8_t mr=fetch8(cpu),cnt=fetch8(cpu)&0x1fu; step_group_shift8(cpu,mr,cnt,before); return !cpu->halted; }
    case 0xc1: { uint8_t mr=fetch8(cpu),cnt=fetch8(cpu)&0x1fu; step_group_shift16(cpu,mr,cnt,before); return !cpu->halted; }
    case 0xc2: { uint16_t adj=fetch16(cpu); cpu->ip=pop(cpu); cpu->r[RTYPE_I86_SP]=(uint16_t)(cpu->r[RTYPE_I86_SP]+adj); return true; }
    case 0xc3: cpu->ip=pop(cpu); return true;
    case 0xca: { uint16_t adj=fetch16(cpu); cpu->ip=pop(cpu); cpu->s[RTYPE_I86_CS]=pop(cpu); cpu->r[RTYPE_I86_SP]=(uint16_t)(cpu->r[RTYPE_I86_SP]+adj); return true; }
    case 0xcb: cpu->ip=pop(cpu); cpu->s[RTYPE_I86_CS]=pop(cpu); return true;
    case 0xc6: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; if(sub){halt_reason(cpu,"unsupported C6/%u at %05x",sub,before);return false;} uint8_t imm=fetch8(cpu); if(mod==3)*reg8(cpu,rm)=imm; else wb(cpu,ea(cpu,mod,rm),imm); return true; }
    case 0xc7: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; if(sub){halt_reason(cpu,"unsupported C7/%u at %05x",sub,before);return false;} uint16_t imm=fetch16(cpu); if(mod==3)cpu->r[rm]=imm; else ww(cpu,ea(cpu,mod,rm),imm); return true; }
    case 0xcd: { uint8_t num=fetch8(cpu); halt_reason(cpu,"INT %02x at %05x",num,before); return false; }
    case 0xcf: cpu->ip=pop(cpu); cpu->s[RTYPE_I86_CS]=pop(cpu); set_flags_word(cpu,pop(cpu)); cpu->iret_count++; if(cpu->interrupt_depth)cpu->interrupt_depth--; if(cpu->interrupt_depth==0)cpu->pending_frame_sp_valid=false; return true;
    case 0xd0: { uint8_t mr=fetch8(cpu); step_group_shift8(cpu,mr,1,before); return !cpu->halted; }
    case 0xd1: { uint8_t mr=fetch8(cpu); step_group_shift16(cpu,mr,1,before); return !cpu->halted; }
    case 0xd2: { uint8_t mr=fetch8(cpu); step_group_shift8(cpu,mr,*reg8(cpu,RTYPE_I86_CX)&0x1f,before); return !cpu->halted; }
    case 0xd3: { uint8_t mr=fetch8(cpu); step_group_shift16(cpu,mr,*reg8(cpu,RTYPE_I86_CX)&0x1f,before); return !cpu->halted; }
    case 0xe0: { int8_t d=(int8_t)fetch8(cpu); cpu->r[RTYPE_I86_CX]--; if(cpu->r[RTYPE_I86_CX]&&!cpu->zf)cpu->ip=(uint16_t)(cpu->ip+d); return true; }
    case 0xe1: { int8_t d=(int8_t)fetch8(cpu); cpu->r[RTYPE_I86_CX]--; if(cpu->r[RTYPE_I86_CX]&&cpu->zf)cpu->ip=(uint16_t)(cpu->ip+d); return true; }
    case 0xe2: { int8_t d=(int8_t)fetch8(cpu); cpu->r[RTYPE_I86_CX]--; if(cpu->r[RTYPE_I86_CX])cpu->ip=(uint16_t)(cpu->ip+d); return true; }
    case 0xe4: { uint8_t p=fetch8(cpu); *reg8(cpu,0)=rtype_m72_core_in8(cpu->core,p); return true; }
    case 0xe5: { uint8_t p=fetch8(cpu); cpu->r[RTYPE_I86_AX]=rtype_m72_core_in16(cpu->core,p); return true; }
    case 0xe6: { uint8_t p=fetch8(cpu); rtype_m72_core_out8(cpu->core,p,*reg8(cpu,0)); return true; }
    case 0xe7: { uint8_t p=fetch8(cpu); rtype_m72_core_out16(cpu->core,p,cpu->r[RTYPE_I86_AX]); return true; }
    case 0xe8: { int16_t d=(int16_t)fetch16(cpu); push(cpu,cpu->ip); cpu->ip=(uint16_t)(cpu->ip+d); return true; }
    case 0xe9: { int16_t d=(int16_t)fetch16(cpu); cpu->ip=(uint16_t)(cpu->ip+d); return true; }
    case 0xea: { uint16_t off=fetch16(cpu),seg=fetch16(cpu); cpu->ip=off; cpu->s[RTYPE_I86_CS]=seg; return true; }
    case 0xeb: { int8_t d=(int8_t)fetch8(cpu); cpu->ip=(uint16_t)(cpu->ip+d); return true; }
    case 0xec: *reg8(cpu,0)=rtype_m72_core_in8(cpu->core,cpu->r[RTYPE_I86_DX]); return true;
    case 0xed: cpu->r[RTYPE_I86_AX]=rtype_m72_core_in16(cpu->core,cpu->r[RTYPE_I86_DX]); return true;
    case 0xee: rtype_m72_core_out8(cpu->core,cpu->r[RTYPE_I86_DX],*reg8(cpu,0)); return true;
    case 0xef: rtype_m72_core_out16(cpu->core,cpu->r[RTYPE_I86_DX],cpu->r[RTYPE_I86_AX]); return true;
    case 0xf3: { uint8_t next=fetch8(cpu); if(next==0xa4){uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; while(cpu->r[RTYPE_I86_CX]){uint8_t v=rb(cpu,lin(cpu,seg,cpu->r[RTYPE_I86_SI])); wb(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),v); cpu->r[RTYPE_I86_SI]+=(cpu->df?-1:1); cpu->r[RTYPE_I86_DI]+=(cpu->df?-1:1); cpu->r[RTYPE_I86_CX]--; } return true;} if(next==0xa5){uint16_t seg=cpu->seg_override_active?cpu->seg_override_value:cpu->s[RTYPE_I86_DS]; while(cpu->r[RTYPE_I86_CX]){uint16_t v=rw(cpu,lin(cpu,seg,cpu->r[RTYPE_I86_SI])); ww(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),v); cpu->r[RTYPE_I86_SI]+=(cpu->df?-2:2); cpu->r[RTYPE_I86_DI]+=(cpu->df?-2:2); cpu->r[RTYPE_I86_CX]--; } return true;} if(next==0xaa){while(cpu->r[RTYPE_I86_CX]){wb(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),*reg8(cpu,0)); cpu->r[RTYPE_I86_DI]+=(cpu->df?-1:1); cpu->r[RTYPE_I86_CX]--; } return true;} if(next==0xab){while(cpu->r[RTYPE_I86_CX]){ww(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI]),cpu->r[RTYPE_I86_AX]); cpu->r[RTYPE_I86_DI]+=(cpu->df?-2:2); cpu->r[RTYPE_I86_CX]--; } return true;} if(next==0xae){while(cpu->r[RTYPE_I86_CX]){uint8_t v=rb(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI])); set_sub8(cpu,*reg8(cpu,0),v,(uint8_t)(*reg8(cpu,0)-v)); cpu->r[RTYPE_I86_DI]+=(cpu->df?-1:1); cpu->r[RTYPE_I86_CX]--; if(!cpu->zf)break;} return true;} if(next==0xaf){while(cpu->r[RTYPE_I86_CX]){uint16_t v=rw(cpu,lin(cpu,cpu->s[RTYPE_I86_ES],cpu->r[RTYPE_I86_DI])); set_sub16(cpu,cpu->r[RTYPE_I86_AX],v,(uint16_t)(cpu->r[RTYPE_I86_AX]-v)); cpu->r[RTYPE_I86_DI]+=(cpu->df?-2:2); cpu->r[RTYPE_I86_CX]--; if(!cpu->zf)break;} return true;} halt_reason(cpu,"unsupported REP %02x at %05x",next,before); return false; }
    case 0xf4: cpu->halted=true; return false;
    case 0xf5: cpu->cf=!cpu->cf; return true;
    case 0xf6: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint8_t v=(mod==3)?*reg8(cpu,rm):(a=ea(cpu,mod,rm),rb(cpu,a)); if(sub==0){uint8_t imm=fetch8(cpu); set_logic8(cpu,(uint8_t)(v&imm)); return true;} if(sub==2){v=(uint8_t)~v; if(mod==3)*reg8(cpu,rm)=v; else wb(cpu,a,v); return true;} halt_reason(cpu,"unsupported F6/%u at %05x",sub,before); return false; }
    case 0xf7: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t v=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); if(sub==0){uint16_t imm=fetch16(cpu); set_logic16(cpu,(uint16_t)(v&imm)); return true;} if(sub==2){v=(uint16_t)~v; if(mod==3)cpu->r[rm]=v; else ww(cpu,a,v); return true;} if(sub==3){uint16_t old=v; v=(uint16_t)(0-v); cpu->cf=old!=0; cpu->zf=v==0; cpu->sf=(v&0x8000u)!=0; cpu->of=old==0x8000u; if(mod==3)cpu->r[rm]=v; else ww(cpu,a,v); return true;} halt_reason(cpu,"unsupported F7/%u at %05x",sub,before); return false; }
    case 0xf8: cpu->cf=false; return true;
    case 0xf9: cpu->cf=true; return true;
    case 0xfa: cpu->iff=false; return true;
    case 0xfb: cpu->iff=true; return true;
    case 0xfc: cpu->df=false; return true;
    case 0xfd: cpu->df=true; return true;
    case 0xfe: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint8_t v=(mod==3)?*reg8(cpu,rm):(a=ea(cpu,mod,rm),rb(cpu,a)); if(sub==0)v++; else if(sub==1)v--; else {halt_reason(cpu,"unsupported FE/%u at %05x",sub,before);return false;} cpu->zf=v==0; cpu->sf=(v&0x80u)!=0; if(mod==3)*reg8(cpu,rm)=v; else wb(cpu,a,v); return true; }
    case 0xff: { uint8_t mr=fetch8(cpu); unsigned mod=mr>>6,sub=(mr>>3)&7,rm=mr&7; uint32_t a=0; uint16_t v=(mod==3)?cpu->r[rm]:(a=ea(cpu,mod,rm),rw(cpu,a)); if(sub==0){v++;cpu->zf=v==0;cpu->sf=(v&0x8000u)!=0;if(mod==3)cpu->r[rm]=v;else ww(cpu,a,v);return true;} if(sub==1){v--;cpu->zf=v==0;cpu->sf=(v&0x8000u)!=0;if(mod==3)cpu->r[rm]=v;else ww(cpu,a,v);return true;} if(sub==2){push(cpu,cpu->ip);cpu->ip=v;return true;} if(sub==4){cpu->ip=v;return true;} if(sub==6){push(cpu,v);return true;} halt_reason(cpu,"unsupported FF/%u at %05x",sub,before); return false; }
    default:
        halt_reason(cpu, "unsupported opcode %02x at %05x", op, before);
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
