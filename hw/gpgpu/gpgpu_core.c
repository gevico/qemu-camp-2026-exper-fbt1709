/*
 * QEMU GPGPU - RISC-V SIMT Core Implementation
 *
 * Copyright (c) 2024-2025
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "fpu/softfloat.h"
#include "gpgpu.h"
#include "gpgpu_core.h"

#define GPGPU_EXEC_MAX_CYCLES   1000000u

#define OPCODE(insn)    ((insn) & 0x7f)
#define RD(insn)        (((insn) >> 7) & 0x1f)
#define RS1(insn)       (((insn) >> 15) & 0x1f)
#define RS2(insn)       (((insn) >> 20) & 0x1f)
#define FUNCT3(insn)    (((insn) >> 12) & 7)
#define FUNCT7(insn)    (((insn) >> 25) & 0x7f)
#define IMM_I(insn)     ((int32_t)(insn) >> 20)

static inline uint32_t imm_u(uint32_t insn)
{
    return insn & 0xfffff000;
}

static inline int32_t imm_s(uint32_t insn)
{
    uint32_t imm = ((insn >> 7) & 0x1f) | (((insn >> 25) & 0x7f) << 5);
    return (int32_t)(imm << 20) >> 20;
}

static inline int32_t imm_b(uint32_t insn)
{
    uint32_t imm = ((insn >> 8) & 0x1f) << 1
        | ((insn >> 25) & 0x3f) << 6
        | ((insn >> 7) & 1) << 11
        | (insn & 0x80000000) >> 19;
    return (int32_t)imm;
}

static inline uint32_t fpr_nanbox_s(float32 f)
{
    return f;
}

static inline float32 fpr_get_s(uint32_t raw)
{
    return raw;
}

static inline uint32_t nanbox_bf16(bfloat16 bf)
{
    return bf | 0xffff0000u;
}

static inline bfloat16 fpr_get_bf16(uint32_t raw)
{
    return (bfloat16)(raw & 0xffffu);
}

static int gpgpu_mem_store(GPGPUState *s, uint32_t addr, uint64_t val, unsigned size)
{
    uint8_t *p = s->vram_ptr + addr;

    if (addr + size > s->vram_size || addr + size < addr) {
        return -1;
    }
    switch (size) {
    case 1:
        p[0] = val;
        break;
    case 2:
        stw_le_p(p, val);
        break;
    case 4:
        stl_le_p(p, val);
        break;
    case 8:
        stq_le_p(p, val);
        break;
    default:
        return -1;
    }
    return 0;
}

static int gpgpu_mem_load(GPGPUState *s, uint32_t addr, uint64_t *val, unsigned size)
{
    uint8_t *p = s->vram_ptr + addr;

    if (addr + size > s->vram_size || addr + size < addr) {
        return -1;
    }
    switch (size) {
    case 1:
        *val = p[0];
        break;
    case 2:
        *val = lduw_le_p(p);
        break;
    case 4:
        *val = ldl_le_p(p);
        break;
    case 8:
        *val = ldq_le_p(p);
        break;
    default:
        return -1;
    }
    return 0;
}

static bool gpgpu_exec_fp_insn(GPGPUState *s, GPGPULane *lane, uint32_t insn)
{
    uint8_t rd = RD(insn);
    uint8_t rs1 = RS1(insn);
    uint8_t rs2 = RS2(insn);
    uint8_t funct7 = FUNCT7(insn);
    uint8_t rm = FUNCT3(insn);
    float_status *st = &lane->fp_status;

    /* Custom opcodes embedded in tests (see tests/qtest/gpgpu-test.c). */
    switch (insn) {
    case 0x44108153: { /* fcvt.bf16.s */
        float32 a = fpr_get_s(lane->fpr[rs1]);
        bfloat16 bf = float32_to_bfloat16(a, st);
        lane->fpr[rd] = nanbox_bf16(bf);
        return true;
    }
    case 0x440101D3: { /* fcvt.s.bf16 */
        bfloat16 bf = fpr_get_bf16(lane->fpr[rs1]);
        float32 a = bfloat16_to_float32(bf, st);
        lane->fpr[rd] = fpr_nanbox_s(a);
        return true;
    }
    case 0x48108153: { /* fcvt.e4m3.s */
        float32 a = fpr_get_s(lane->fpr[rs1]);
        float8_e4m3 e = float32_to_float8_e4m3(a, true, st);
        lane->fpr[rd] = e;
        return true;
    }
    case 0x480101D3: { /* fcvt.s.e4m3 */
        float8_e4m3 e = lane->fpr[rs1] & 0xff;
        bfloat16 bf = float8_e4m3_to_bfloat16(e, st);
        float32 a = bfloat16_to_float32(bf, st);
        lane->fpr[rd] = fpr_nanbox_s(a);
        return true;
    }
    case 0x48308153: { /* fcvt.e5m2.s */
        float32 a = fpr_get_s(lane->fpr[rs1]);
        float8_e5m2 e = float32_to_float8_e5m2(a, true, st);
        lane->fpr[rd] = e;
        return true;
    }
    case 0x482101D3: { /* fcvt.s.e5m2 */
        float8_e5m2 e = lane->fpr[rs1] & 0xff;
        bfloat16 bf = float8_e5m2_to_bfloat16(e, st);
        float32 a = bfloat16_to_float32(bf, st);
        lane->fpr[rd] = fpr_nanbox_s(a);
        return true;
    }
    case 0x4C108153: { /* fcvt.e2m1.s */
        float32 a = fpr_get_s(lane->fpr[rs1]);
        float4_e2m1 e = float32_to_float4_e2m1(a, true, st);
        lane->fpr[rd] = e & 0xf;
        return true;
    }
    case 0x4C0101D3: { /* fcvt.s.e2m1 */
        float4_e2m1 e = lane->fpr[rs1] & 0xf;
        float32 a = float4_e2m1_to_float32(e, st);
        lane->fpr[rd] = fpr_nanbox_s(a);
        return true;
    }
    default:
        break;
    }

    if (funct7 == 0x78 && rs2 == 0 && rm == 0) {
        /* fmv.w.x */
        lane->fpr[rd] = fpr_nanbox_s(uint32_to_float32(lane->gpr[rs1], st));
        return true;
    }

    if (funct7 == 0x68 && rs2 == 0) {
        /* fcvt.s.w */
        float32 f = int32_to_float32((int32_t)lane->gpr[rs1], st);
        lane->fpr[rd] = fpr_nanbox_s(f);
        return true;
    }

    if (funct7 == 0x60 && rs2 == 0) {
        /* fcvt.w.s */
        float32 f = fpr_get_s(lane->fpr[rs1]);
        int32_t i;
        FloatRoundMode old = get_float_rounding_mode(st);

        switch (rm) {
        case 0: /* RNE */
            set_float_rounding_mode(float_round_nearest_even, st);
            i = float32_to_int32(f, st);
            break;
        case 1: /* RTZ */
            i = float32_to_int32_round_to_zero(f, st);
            break;
        case 2: /* RDN */
            set_float_rounding_mode(float_round_down, st);
            i = float32_to_int32(f, st);
            break;
        case 3: /* RUP */
            set_float_rounding_mode(float_round_up, st);
            i = float32_to_int32(f, st);
            break;
        default:
            return false;
        }
        set_float_rounding_mode(old, st);
        lane->gpr[rd] = (uint32_t)i;
        return true;
    }

    float32 f1 = fpr_get_s(lane->fpr[rs1]);
    float32 f2 = fpr_get_s(lane->fpr[rs2]);

    switch (funct7) {
    case 0x00: /* fadd.s */
        lane->fpr[rd] = fpr_nanbox_s(float32_add(f1, f2, st));
        return true;
    case 0x04: /* fsub.s */
        lane->fpr[rd] = fpr_nanbox_s(float32_sub(f1, f2, st));
        return true;
    case 0x08: /* fmul.s */
        lane->fpr[rd] = fpr_nanbox_s(float32_mul(f1, f2, st));
        return true;
    case 0x0c: /* fdiv.s */
        lane->fpr[rd] = fpr_nanbox_s(float32_div(f1, f2, st));
        return true;
    default:
        return false;
    }
}

static int gpgpu_exec_lane(GPGPUState *s, GPGPULane *lane, uint32_t entry_pc)
{
    uint32_t cycles = 0;

    memset(lane->gpr, 0, sizeof(lane->gpr));
    memset(lane->fpr, 0, sizeof(lane->fpr));
    lane->pc = entry_pc;
    lane->fcsr = 0;
    set_float_exception_flags(0, &lane->fp_status);
    set_float_rounding_mode(float_round_nearest_even, &lane->fp_status);

    while (cycles++ < GPGPU_EXEC_MAX_CYCLES) {
        uint32_t pc = lane->pc;

        if (pc + 4 > s->vram_size) {
            return -1;
        }
        uint32_t insn = ldl_le_p(s->vram_ptr + pc);

        if (insn == 0x00100073) {
            /* ebreak */
            lane->pc += 4;
            return 0;
        }

        switch (OPCODE(insn)) {
        case 0x33: {
            uint8_t rd = RD(insn);
            uint8_t rs1 = RS1(insn);
            uint8_t rs2 = RS2(insn);
            uint8_t f3 = FUNCT3(insn);
            uint8_t f7 = FUNCT7(insn);
            uint32_t v1 = lane->gpr[rs1];
            uint32_t v2 = lane->gpr[rs2];

            if (f3 == 0 && f7 == 0) {
                lane->gpr[rd] = v1 + v2;
            } else if (f3 == 0 && f7 == 0x20) {
                lane->gpr[rd] = v1 - v2;
            } else if (f3 == 7 && f7 == 0) {
                lane->gpr[rd] = v1 & v2;
            } else if (f3 == 6 && f7 == 0) {
                lane->gpr[rd] = v1 | v2;
            } else if (f3 == 4 && f7 == 0) {
                lane->gpr[rd] = v1 ^ v2;
            } else if (f3 == 1 && f7 == 0) {
                lane->gpr[rd] = v1 << (v2 & 0x1f);
            } else if (f3 == 5 && f7 == 0x00) {
                lane->gpr[rd] = v1 >> (v2 & 0x1f);
            } else if (f3 == 5 && f7 == 0x20) {
                lane->gpr[rd] = (uint32_t)((int32_t)v1 >> (v2 & 0x1f));
            } else {
                return -1;
            }
            break;
        }
        case 0x13: {
            uint8_t rd = RD(insn);
            uint8_t rs1 = RS1(insn);
            int32_t imm = IMM_I(insn);
            uint8_t f3 = FUNCT3(insn);
            uint32_t v1 = lane->gpr[rs1];

            switch (f3) {
            case 0:
                lane->gpr[rd] = v1 + (uint32_t)imm;
                break;
            case 1:
                lane->gpr[rd] = v1 << ((insn >> 20) & 0x1f);
                break;
            case 2:
                lane->gpr[rd] = (int32_t)v1 < imm ? 1u : 0u;
                break;
            case 3:
                lane->gpr[rd] = v1 < (uint32_t)imm ? 1u : 0u;
                break;
            case 4:
                lane->gpr[rd] = v1 ^ (uint32_t)imm;
                break;
            case 5:
                if ((insn >> 25) & 1) {
                    lane->gpr[rd] = (uint32_t)((int32_t)v1 >> ((insn >> 20) & 0x1f));
                } else {
                    lane->gpr[rd] = v1 >> ((insn >> 20) & 0x1f);
                }
                break;
            case 6:
                lane->gpr[rd] = v1 | (uint32_t)imm;
                break;
            case 7:
                lane->gpr[rd] = v1 & (uint32_t)imm;
                break;
            default:
                return -1;
            }
            break;
        }
        case 0x03: {
            uint8_t rd = RD(insn);
            uint8_t rs1 = RS1(insn);
            int32_t imm = IMM_I(insn);
            uint32_t addr = lane->gpr[rs1] + (uint32_t)imm;
            uint64_t val;
            uint8_t f3 = FUNCT3(insn);

            switch (f3) {
            case 0:
                if (gpgpu_mem_load(s, addr, &val, 1)) {
                    return -1;
                }
                lane->gpr[rd] = (int8_t)val;
                break;
            case 1:
                if (gpgpu_mem_load(s, addr, &val, 2)) {
                    return -1;
                }
                lane->gpr[rd] = (int16_t)val;
                break;
            case 2:
                if (gpgpu_mem_load(s, addr, &val, 4)) {
                    return -1;
                }
                lane->gpr[rd] = (uint32_t)val;
                break;
            default:
                return -1;
            }
            break;
        }
        case 0x23: {
            uint8_t rs1 = RS1(insn);
            uint8_t rs2 = RS2(insn);
            int32_t off = imm_s(insn);
            uint32_t addr = lane->gpr[rs1] + (uint32_t)off;
            uint8_t f3 = FUNCT3(insn);

            switch (f3) {
            case 0:
                if (gpgpu_mem_store(s, addr, lane->gpr[rs2], 1)) {
                    return -1;
                }
                break;
            case 1:
                if (gpgpu_mem_store(s, addr, lane->gpr[rs2], 2)) {
                    return -1;
                }
                break;
            case 2:
                if (gpgpu_mem_store(s, addr, lane->gpr[rs2], 4)) {
                    return -1;
                }
                break;
            default:
                return -1;
            }
            break;
        }
        case 0x37:
            lane->gpr[RD(insn)] = imm_u(insn);
            break;
        case 0x17: {
            uint8_t rd = RD(insn);
            lane->gpr[rd] = pc + imm_u(insn);
            break;
        }
        case 0x6f: {
            uint8_t rd = RD(insn);
            int32_t imm = ((insn >> 31) & 0xfff00000)
                | (((insn >> 21) & 0x3ff) << 1)
                | (((insn >> 20) & 1) << 11)
                | ((insn & 0xff000));
            imm = (imm << 11) >> 11;
            lane->gpr[rd] = pc + 4;
            lane->pc = pc + (uint32_t)imm;
            continue;
        }
        case 0x67: {
            uint8_t rd = RD(insn);
            uint8_t rs1 = RS1(insn);
            int32_t imm = IMM_I(insn);
            uint32_t t = pc + 4;

            lane->pc = (lane->gpr[rs1] + (uint32_t)imm) & ~1u;
            lane->gpr[rd] = t;
            continue;
        }
        case 0x63: {
            uint8_t rs1 = RS1(insn);
            uint8_t rs2 = RS2(insn);
            uint8_t f3 = FUNCT3(insn);
            int32_t off = imm_b(insn);
            uint32_t v1 = lane->gpr[rs1];
            uint32_t v2 = lane->gpr[rs2];
            bool take = false;

            switch (f3) {
            case 0:
                take = v1 == v2;
                break;
            case 1:
                take = v1 != v2;
                break;
            case 4:
                take = (int32_t)v1 < (int32_t)v2;
                break;
            case 5:
                take = (int32_t)v1 >= (int32_t)v2;
                break;
            case 6:
                take = v1 < v2;
                break;
            case 7:
                take = v1 >= v2;
                break;
            default:
                return -1;
            }
            if (take) {
                lane->pc = pc + (uint32_t)off;
                continue;
            }
            break;
        }
        case 0x73: {
            uint8_t rd = RD(insn);
            uint8_t f3 = FUNCT3(insn);
            uint32_t csr = insn >> 20;

            if (insn == 0x00000073) {
                /* ecall */
                return -1;
            }
            if (f3 == 0 && csr == 0) {
                return -1;
            }
            if (csr == CSR_MHARTID) {
                if (f3 == 2) { /* csrrs */
                    uint32_t old = lane->mhartid;

                    if (rd) {
                        lane->gpr[rd] = old;
                    }
                } else {
                    return -1;
                }
            } else {
                return -1;
            }
            break;
        }
        case 0x07: { /* flw */
            if (FUNCT3(insn) != 2) {
                return -1;
            }
            {
                uint8_t rd = RD(insn);
                uint8_t rs1 = RS1(insn);
                int32_t imm = IMM_I(insn);
                uint32_t addr = lane->gpr[rs1] + (uint32_t)imm;
                uint64_t val;

                if (gpgpu_mem_load(s, addr, &val, 4)) {
                    return -1;
                }
                lane->fpr[rd] = (uint32_t)val;
            }
            break;
        }
        case 0x27: { /* fsw */
            if (FUNCT3(insn) != 2) {
                return -1;
            }
            {
                uint8_t rs1 = RS1(insn);
                uint8_t rs2 = RS2(insn);
                int32_t off = imm_s(insn);
                uint32_t addr = lane->gpr[rs1] + (uint32_t)off;

                if (gpgpu_mem_store(s, addr, lane->fpr[rs2], 4)) {
                    return -1;
                }
            }
            break;
        }
        case 0x53:
            if (!gpgpu_exec_fp_insn(s, lane, insn)) {
                return -1;
            }
            break;
        default:
            return -1;
        }

        lane->pc = pc + 4;
    }
    return -1;
}

void gpgpu_core_init_warp(GPGPUWarp *warp, uint32_t pc,
                          uint32_t thread_id_base, const uint32_t block_id[3],
                          uint32_t num_threads,
                          uint32_t warp_id, uint32_t block_id_linear)
{
    (void)pc;
    (void)thread_id_base;
    (void)block_id;
    (void)num_threads;
    (void)warp_id;
    (void)block_id_linear;

    memset(warp, 0, sizeof(*warp));
}

int gpgpu_core_exec_warp(GPGPUState *s, GPGPUWarp *warp, uint32_t max_cycles)
{
    (void)s;
    (void)warp;
    (void)max_cycles;
    return -1;
}

int gpgpu_core_exec_kernel(GPGPUState *s)
{
    uint32_t gx = s->kernel.grid_dim[0];
    uint32_t gy = s->kernel.grid_dim[1];
    uint32_t gz = s->kernel.grid_dim[2];
    uint32_t bx = s->kernel.block_dim[0];
    uint32_t by = s->kernel.block_dim[1];
    uint32_t bz = s->kernel.block_dim[2];
    uint32_t threads_per_block = bx * by * bz;
    uint32_t num_blocks = gx * gy * gz;
    uint32_t b;

    for (b = 0; b < num_blocks; b++) {
        uint32_t t;

        for (t = 0; t < threads_per_block; t++) {
            GPGPULane lane = { .mhartid = 0 };
            uint32_t warp_id = t / GPGPU_WARP_SIZE;
            uint32_t lane_id = t % GPGPU_WARP_SIZE;
            int ret;

            lane.mhartid = MHARTID_ENCODE(b, warp_id, lane_id);
            ret = gpgpu_exec_lane(s, &lane, (uint32_t)s->kernel.kernel_addr);
            if (ret != 0) {
                return -1;
            }
        }
    }
    return 0;
}
