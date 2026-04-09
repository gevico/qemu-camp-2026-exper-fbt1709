/*
 * RISC-V translation routines for the RV64A Standard Extension.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static bool trans_dma(DisasContext *ctx, arg_dma *a)
{
    gen_helper_dma(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_gemm(DisasContext *ctx, arg_gemm *a)
{
    gen_helper_gemm(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_sort(DisasContext *ctx, arg_sort *a)
{
    gen_helper_sort(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_vadd(DisasContext *ctx, arg_vadd *a)
{
    gen_helper_vadd(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_crush(DisasContext *ctx, arg_crush *a)
{
    gen_helper_crush(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_expand(DisasContext *ctx, arg_expand *a)
{
    gen_helper_expand(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_vdot(DisasContext *ctx, arg_vdot *a)
{
    gen_helper_vdot(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_vrelu(DisasContext *ctx, arg_vrelu *a)
{
    gen_helper_vrelu(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_vscale(DisasContext *ctx, arg_vscale *a)
{
    gen_helper_vscale(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}

static bool trans_vmax(DisasContext *ctx, arg_vmax *a)
{
    gen_helper_vmax(tcg_env, tcg_constant_tl(a->rd), tcg_constant_tl(a->rs1), tcg_constant_tl(a->rs2));
    return true;
}