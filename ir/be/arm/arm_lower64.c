/*
 * This file is part of libFirm.
 * Copyright (C) 2014 University of Karlsruhe.
 */

/**
 * @file
 * @brief    ARM 64bit lowering
 * @author   Matthias Braun
 */
#include "arm_nodes_attr.h"
#include "bearch_arm_t.h"
#include "gen_arm_new_nodes.h"
#include "gen_arm_regalloc_if.h"
#include "ircons_t.h"
#include "lower_dw.h"
#include "panic.h"
#include "util.h"

static void lower64_add(ir_node *node, ir_mode *mode)
{
	dbg_info *dbgi       = get_irn_dbg_info(node);
	ir_node  *block      = get_nodes_block(node);
	ir_node  *left       = get_Add_left(node);
	ir_node  *right      = get_Add_right(node);
	ir_node  *left_low   = get_lowered_low(left);
	ir_node  *left_high  = get_lowered_high(left);
	ir_node  *right_low  = get_lowered_low(right);
	ir_node  *right_high = get_lowered_high(right);
	ir_node  *adds       = new_bd_arm_AddS_t(dbgi, block, left_low, right_low);
	ir_mode  *mode_low   = get_irn_mode(left_low);
	ir_node  *res_low    = new_r_Proj(adds, mode_low, pn_arm_AddS_t_res);
	ir_node  *res_flags  = new_r_Proj(adds, mode_ANY, pn_arm_AddS_t_flags);
	ir_node  *adc        = new_bd_arm_AdC_t(dbgi, block, left_high,
	                                        right_high, res_flags, mode);
	ir_set_dw_lowered(node, res_low, adc);
}

static void lower64_sub(ir_node *node, ir_mode *mode)
{
	dbg_info *dbgi       = get_irn_dbg_info(node);
	ir_node  *block      = get_nodes_block(node);
	ir_node  *left       = get_Sub_left(node);
	ir_node  *right      = get_Sub_right(node);
	ir_node  *left_low   = get_lowered_low(left);
	ir_node  *left_high  = get_lowered_high(left);
	ir_node  *right_low  = get_lowered_low(right);
	ir_node  *right_high = get_lowered_high(right);
	ir_node  *subs       = new_bd_arm_SubS_t(dbgi, block, left_low, right_low);
	ir_mode  *mode_low   = get_irn_mode(left_low);
	ir_node  *res_low    = new_r_Proj(subs, mode_low, pn_arm_SubS_t_res);
	ir_node  *res_flags  = new_r_Proj(subs, mode_ANY, pn_arm_SubS_t_flags);
	ir_node  *sbc        = new_bd_arm_SbC_t(dbgi, block, left_high,
	                                        right_high, res_flags, mode);
	ir_set_dw_lowered(node, res_low, sbc);
}

static void lower64_minus(ir_node *node, ir_mode *mode)
{
	dbg_info *dbgi         = get_irn_dbg_info(node);
	ir_graph *irg          = get_irn_irg(node);
	ir_node  *block        = get_nodes_block(node);
	ir_node  *op           = get_Minus_op(node);
	ir_node  *right_low    = get_lowered_low(op);
	ir_node  *right_high   = get_lowered_high(op);
	ir_mode  *low_unsigned = get_irn_mode(right_low);
	ir_node  *left_low     = new_r_Const_null(irg, low_unsigned);
	ir_node  *left_high    = new_r_Const_null(irg, mode);
	ir_node  *subs         = new_bd_arm_SubS_t(dbgi, block, left_low,
	                                           right_low);
	ir_node  *res_low      = new_r_Proj(subs, low_unsigned, pn_arm_SubS_t_res);
	ir_node  *res_flags    = new_r_Proj(subs, mode_ANY, pn_arm_SubS_t_flags);
	ir_node  *sbc          = new_bd_arm_SbC_t(dbgi, block, left_high,
	                                          right_high, res_flags, mode);
	ir_set_dw_lowered(node, res_low, sbc);
}

static void lower64_mul(ir_node *node, ir_mode *mode)
{
	dbg_info *dbgi       = get_irn_dbg_info(node);
	ir_node  *block      = get_nodes_block(node);
	ir_node  *left       = get_Mul_left(node);
	ir_node  *right      = get_Mul_right(node);
	ir_node  *left_low   = get_lowered_low(left);
	ir_node  *left_high  = get_lowered_high(left);
	ir_node  *right_low  = get_lowered_low(right);
	ir_node  *right_high = get_lowered_high(right);
	ir_node  *conv_l_low = new_rd_Conv(dbgi, block, left_low, mode);
	ir_node  *mul1       = new_rd_Mul(dbgi, block, conv_l_low, right_high,
	                                  mode);
	ir_node  *umull      = new_bd_arm_UMulL_t(dbgi, block, left_low, right_low);
	ir_mode  *umode      = get_irn_mode(right_low);
	ir_node  *umull_low  = new_r_Proj(umull, umode, pn_arm_UMulL_t_low);
	ir_node  *umull_high = new_r_Proj(umull, mode, pn_arm_UMulL_t_high);
	ir_node  *conv_r_low = new_rd_Conv(dbgi, block, right_low, mode);
	ir_node  *mul2       = new_rd_Mul(dbgi, block, conv_r_low, left_high, mode);
	ir_node  *add1       = new_rd_Add(dbgi, block, mul2, mul1, mode);
	ir_node  *add2       = new_rd_Add(dbgi, block, add1, umull_high, mode);
	ir_set_dw_lowered(node, umull_low, add2);
}

static ir_entity *ldivmod;
static ir_entity *uldivmod;

static void create_divmod_intrinsics(ir_mode *mode_unsigned,
                                     ir_mode *mode_signed)
{
	ir_type *tp_unsigned  = get_type_for_mode(mode_unsigned);
	ir_type *mtp_unsigned = new_type_method(4, 4);
	set_method_param_type(mtp_unsigned, 0, tp_unsigned);
	set_method_param_type(mtp_unsigned, 1, tp_unsigned);
	set_method_param_type(mtp_unsigned, 2, tp_unsigned);
	set_method_param_type(mtp_unsigned, 3, tp_unsigned);
	set_method_res_type(mtp_unsigned, 0, tp_unsigned);
	set_method_res_type(mtp_unsigned, 1, tp_unsigned);
	set_method_res_type(mtp_unsigned, 2, tp_unsigned);
	set_method_res_type(mtp_unsigned, 3, tp_unsigned);
	ident     *id_uldivmod = new_id_from_str("__aeabi_uldivmod");
	ir_type   *glob        = get_glob_type();
	uldivmod = new_entity(glob, id_uldivmod, mtp_unsigned);
	set_entity_ld_ident(uldivmod, id_uldivmod);
	set_entity_visibility(uldivmod, ir_visibility_external);

	ir_type *tp_signed  = get_type_for_mode(mode_signed);
	ir_type *mtp_signed = new_type_method(4, 4);
	if (arm_cg_config.big_endian) {
		set_method_param_type(mtp_signed, 0, tp_signed);
		set_method_param_type(mtp_signed, 1, tp_unsigned);
		set_method_param_type(mtp_signed, 2, tp_signed);
		set_method_param_type(mtp_signed, 3, tp_unsigned);
		set_method_res_type(mtp_signed, 0, tp_signed);
		set_method_res_type(mtp_signed, 1, tp_unsigned);
		set_method_res_type(mtp_signed, 2, tp_signed);
		set_method_res_type(mtp_signed, 3, tp_unsigned);
	} else {
		set_method_param_type(mtp_signed, 0, tp_unsigned);
		set_method_param_type(mtp_signed, 1, tp_signed);
		set_method_param_type(mtp_signed, 2, tp_unsigned);
		set_method_param_type(mtp_signed, 3, tp_signed);
		set_method_res_type(mtp_signed, 0, tp_unsigned);
		set_method_res_type(mtp_signed, 1, tp_signed);
		set_method_res_type(mtp_signed, 2, tp_unsigned);
		set_method_res_type(mtp_signed, 3, tp_signed);
	}
	ident *id_ldivmod = new_id_from_str("__aeabi_ldivmod");
	ldivmod = new_entity(glob, id_ldivmod, mtp_signed);
	set_entity_ld_ident(ldivmod, id_ldivmod);
	set_entity_visibility(ldivmod, ir_visibility_external);
}

static void lower_divmod(ir_node *node, ir_node *left, ir_node *right,
                         ir_node *mem, ir_mode *mode, int res_offset)
{
	dbg_info  *dbgi       = get_irn_dbg_info(node);
	ir_node   *block      = get_nodes_block(node);
	ir_node   *left_low   = get_lowered_low(left);
	ir_node   *left_high  = get_lowered_high(left);
	ir_node   *right_low  = get_lowered_low(right);
	ir_node   *right_high = get_lowered_high(right);
	ir_mode   *node_mode  = get_irn_mode(left);
	ir_entity *entity     = mode_is_signed(node_mode) ? ldivmod : uldivmod;
	ir_type   *mtp        = get_entity_type(entity);
	ir_graph  *irg        = get_irn_irg(node);
	ir_node   *addr       = new_r_Address(irg, entity);
	ir_node   *in[4];
	if (arm_cg_config.big_endian) {
		in[0] = left_high;
		in[1] = left_low;
		in[2] = right_high;
		in[3] = right_low;
	} else {
		in[0] = left_low;
		in[1] = left_high;
		in[2] = right_low;
		in[3] = right_high;
	}
	ir_node *call    = new_rd_Call(dbgi, block, mem, addr, ARRAY_SIZE(in), in,
	                               mtp);
	ir_node *resproj = new_r_Proj(call, mode_T, pn_Call_T_result);
	set_irn_pinned(call, get_irn_pinned(node));
	foreach_out_edge_safe(node, edge) {
		ir_node *proj = get_edge_src_irn(edge);
		if (!is_Proj(proj))
			continue;

		switch ((pn_Div)get_Proj_proj(proj)) {
		case pn_Div_M:
			/* reroute to the call */
			set_Proj_pred(proj, call);
			set_Proj_proj(proj, pn_Call_M);
			break;
		case pn_Div_X_regular:
			set_Proj_pred(proj, call);
			set_Proj_proj(proj, pn_Call_X_regular);
			break;
		case pn_Div_X_except:
			set_Proj_pred(proj, call);
			set_Proj_proj(proj, pn_Call_X_except);
			break;
		case pn_Div_res: {
			ir_mode *low_mode = get_irn_mode(left_low);
			if (arm_cg_config.big_endian) {
				ir_node *res_low  = new_r_Proj(resproj, low_mode, res_offset+1);
				ir_node *res_high = new_r_Proj(resproj, mode,     res_offset);
				ir_set_dw_lowered(proj, res_low, res_high);
			} else {
				ir_node *res_low  = new_r_Proj(resproj, low_mode, res_offset);
				ir_node *res_high = new_r_Proj(resproj, mode,     res_offset+1);
				ir_set_dw_lowered(proj, res_low, res_high);
			}
			break;
		}
		}
		/* mark this proj: we have handled it already, otherwise we might fall
		 * into out new nodes. */
		mark_irn_visited(proj);
	}
}

static void lower64_div(ir_node *node, ir_mode *mode)
{
	ir_node *left  = get_Div_left(node);
	ir_node *right = get_Div_right(node);
	ir_node *mem   = get_Div_mem(node);
	lower_divmod(node, left, right, mem, mode, 0);
}

static void lower64_mod(ir_node *node, ir_mode *mode)
{
	ir_node *left  = get_Mod_left(node);
	ir_node *right = get_Mod_right(node);
	ir_node *mem   = get_Mod_mem(node);
	lower_divmod(node, left, right, mem, mode, 2);
}

static void lower64_shl(ir_node *node, ir_mode *mode)
{
	/* the following algo works, because we have modulo shift 256 */
	assert(get_mode_modulo_shift(mode) == 256);
	dbg_info *dbgi       = get_irn_dbg_info(node);
	ir_node  *block      = get_nodes_block(node);
	ir_node  *left       = get_Shl_left(node);
	ir_node  *left_low   = get_lowered_low(left);
	ir_node  *left_high  = get_lowered_high(left);
	ir_node  *right      = get_Shl_right(node);
	ir_mode  *umode      = get_irn_mode(left_low);
	ir_node  *right_low;
	if (get_mode_size_bits(get_irn_mode(right)) == 64) {
		right_low = get_lowered_low(right);
	} else {
		right_low = new_rd_Conv(dbgi, block, right, umode);
	}
	ir_node  *shl1       = new_rd_Shl(dbgi, block, left_high, right_low, mode);
	ir_graph *irg        = get_irn_irg(node);
	ir_node  *c32        = new_r_Const_long(irg, umode, 32);
	ir_node  *sub        = new_rd_Sub(dbgi, block, right_low, c32, umode);
	ir_node  *shl2       = new_rd_Shl(dbgi, block, left_low, sub, umode);
	ir_node  *shl2_conv  = new_rd_Conv(dbgi, block, shl2, mode);
	ir_node  *or         = new_rd_Or(dbgi, block, shl1, shl2_conv, mode);
	ir_node  *sub2       = new_rd_Sub(dbgi, block, c32, right_low, umode);
	ir_node  *shr        = new_rd_Shr(dbgi, block, left_low, sub2, umode);
	ir_node  *shr_conv   = new_rd_Conv(dbgi, block, shr, mode);
	ir_node  *or2        = new_rd_Or(dbgi, block, or, shr_conv, mode);
	ir_node  *low        = new_rd_Shl(dbgi, block, left_low, right_low, umode);
	ir_set_dw_lowered(node, low, or2);
}

static void lower64_shr(ir_node *node, ir_mode *mode)
{
	/* the following algo works, because we have modulo shift 256 */
	assert(get_mode_modulo_shift(mode) == 256);
	dbg_info *dbgi       = get_irn_dbg_info(node);
	ir_node  *block      = get_nodes_block(node);
	ir_node  *left       = get_Shr_left(node);
	ir_node  *left_low   = get_lowered_low(left);
	ir_node  *left_high  = get_lowered_high(left);
	ir_node  *right      = get_Shr_right(node);
	ir_mode  *umode      = get_irn_mode(left_low);
	ir_node  *right_low;
	if (get_mode_size_bits(get_irn_mode(right)) == 64) {
		right_low = get_lowered_low(right);
	} else {
		right_low = new_rd_Conv(dbgi, block, right, umode);
	}
	ir_node  *shr1       = new_rd_Shr(dbgi, block, left_low, right_low, umode);
	ir_graph *irg        = get_irn_irg(node);
	ir_node  *c32        = new_r_Const_long(irg, umode, 32);
	ir_node  *sub        = new_rd_Sub(dbgi, block, right_low, c32, mode);
	ir_node  *shr2       = new_rd_Shr(dbgi, block, left_high, sub, mode);
	ir_node  *shr1_conv  = new_rd_Conv(dbgi, block, shr1, mode);
	ir_node  *or         = new_rd_Or(dbgi, block, shr1_conv, shr2, mode);
	ir_node  *sub2       = new_rd_Sub(dbgi, block, c32, right_low, mode);
	ir_node  *shl        = new_rd_Shl(dbgi, block, left_high, sub2, mode);
	ir_node  *or2        = new_rd_Or(dbgi, block, or, shl, mode);
	ir_node  *shr3       = new_rd_Shr(dbgi, block, left_high, right_low, mode);
	ir_set_dw_lowered(node, or2, shr3);
}

static void lower64_shrs(ir_node *node, ir_mode *mode)
{
	(void)mode;
	/* the following algo works, because we have modulo shift 256 */
	assert(get_mode_modulo_shift(mode) == 256);
	dbg_info *dbgi       = get_irn_dbg_info(node);
	ir_node  *block      = get_nodes_block(node);
	ir_node  *left       = get_Shrs_left(node);
	ir_node  *left_low   = get_lowered_low(left);
	ir_node  *left_high  = get_lowered_high(left);
	ir_node  *right      = get_Shrs_right(node);
	ir_mode  *umode      = get_irn_mode(left_low);
	ir_node  *right_low;
	if (get_mode_size_bits(get_irn_mode(right)) == 64) {
		right_low = get_lowered_low(right);
	} else {
		right_low = new_rd_Conv(dbgi, block, right, umode);
	}
	ir_node  *shr        = new_rd_Shr(dbgi, block, left_low, right_low, umode);
	ir_graph *irg        = get_irn_irg(node);
	ir_node  *c32        = new_r_Const_long(irg, umode, 32);
	ir_node  *sub        = new_rd_Sub(dbgi, block, c32, right_low, umode);
	ir_node  *subs       = new_bd_arm_SubS_t(dbgi, block, right_low, c32);
	ir_node  *subs_res   = new_r_Proj(subs, umode, pn_arm_SubS_t_res);
	ir_node  *subs_flags = new_r_Proj(subs, mode_ANY, pn_arm_SubS_t_flags);
	ir_node  *left_highu = new_rd_Conv(dbgi, block, left_high, umode);
	ir_node  *shl        = new_rd_Shl(dbgi, block, left_highu, sub, umode);
	ir_node  *or         = new_rd_Or(dbgi, block, shr, shl, umode);
	ir_node  *shrs       = new_rd_Shrs(dbgi, block, left_highu, subs_res,
	                                   umode);
	ir_node  *orpl       = new_bd_arm_OrPl_t(dbgi, block, or, shrs, or,
	                                         subs_flags, umode);
	ir_node  *shrs2      = new_rd_Shrs(dbgi, block, left_high, right_low, mode);
	ir_set_dw_lowered(node, orpl, shrs2);
}

static ir_entity *create_64_intrinsic_fkt(ir_type *method, const ir_op *op,
                                          const ir_mode *imode,
                                          const ir_mode *omode, void *context)
{
	(void)context;
	(void)omode;

	const char *name;
	if (op == op_Conv) {
		if (mode_is_float(imode)) {
			assert(get_mode_size_bits(omode) == 64);
			if (get_mode_size_bits(imode) == 64) {
				name = mode_is_signed(omode) ? "__fixdfdi" : "__fixunsdfdi";
			} else if (get_mode_size_bits(imode) == 32) {
				name = mode_is_signed(omode) ? "__fixsfdi" : "__fixunssfdi";
			} else {
				assert(get_mode_size_bits(imode) == 128);
				panic("can't conver long double to long long yet");
			}
		} else if (mode_is_float(omode)) {
			assert(get_mode_size_bits(imode) == 64);
			if (get_mode_size_bits(omode) == 64) {
				name = mode_is_signed(imode) ? "__floatdidf" : "__floatundidf";
			} else if (get_mode_size_bits(omode) == 32) {
				name = mode_is_signed(imode) ? "__floatdisf" : "__floatundisf";
			} else {
				assert(get_mode_size_bits(omode) == 128);
				panic("can't convert long long to long double yet");
			}
		} else {
			panic("can't lower 64bit Conv");
		}
	} else {
		panic("Cannot lower unexpected 64bit operation %s", get_op_name(op));
	}
	ident     *id     = new_id_from_str(name);
	ir_type   *glob   = get_glob_type();
	ir_entity *result = new_entity(glob, id, method);
	set_entity_ld_ident(result, id);
	set_entity_visibility(result, ir_visibility_external);
	return result;
}

void arm_lower_64bit(void)
{
	ir_mode *word_unsigned = arm_mode_gp;
	ir_mode *word_signed   = find_signed_mode(word_unsigned);
	lwrdw_param_t lower_dw_params = {
		create_64_intrinsic_fkt,
		NULL,
		word_unsigned,
		word_signed,
		64, /* doubleword size */
		arm_cg_config.big_endian,
	};

	create_divmod_intrinsics(word_unsigned, word_signed);

	/* make sure opcodes are initialized */
	arm_create_opcodes(&arm_irn_ops);

	ir_prepare_dw_lowering(&lower_dw_params);
	ir_register_dw_lower_function(op_Add,   lower64_add);
	ir_register_dw_lower_function(op_Div,   lower64_div);
	ir_register_dw_lower_function(op_Minus, lower64_minus);
	ir_register_dw_lower_function(op_Mod,   lower64_mod);
	ir_register_dw_lower_function(op_Mul,   lower64_mul);
	ir_register_dw_lower_function(op_Shl,   lower64_shl);
	ir_register_dw_lower_function(op_Shr,   lower64_shr);
	ir_register_dw_lower_function(op_Shrs,  lower64_shrs);
	ir_register_dw_lower_function(op_Sub,   lower64_sub);
	ir_lower_dw_ops();
}