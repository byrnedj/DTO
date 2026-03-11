/*
 * dto_inline_memcpy.cc - GCC plugin for inlining small bounded memcpy/memset
 *
 * Uses GCC's Value Range Propagation (VRP) to detect memcpy/memset calls
 * where the size is provably < 512 bytes. Replaces those calls with tiered
 * inline assembly, eliminating PLT calls so DTO/glibc interposition cannot
 * intercept them.
 *
 * Tiers (memcpy):
 *   <= 16B:  two overlapping 8-byte mov copies
 *   <= 32B:  two overlapping 16-byte xmm copies
 *   <= 64B:  two overlapping 32-byte ymm copies
 *   <= 128B: four overlapping 32-byte ymm copies
 *   > 128B:  AVX2 64-byte loop + overlapping tail
 *
 * Tiers (memset):
 *   <= 8B:   overlapping 8-byte stores (broadcast via imul)
 *   <= 32B:  overlapping 16-byte xmm stores
 *   <= 64B:  overlapping 32-byte ymm stores
 *   <= 128B: four 32-byte ymm stores
 *   > 128B:  AVX2 64-byte loop + overlapping tail
 *
 * Build the plugin:
 *   g++ -shared -fPIC -fno-rtti -o dto_inline_memcpy.so \
 *       dto_inline_memcpy.cc \
 *       -I$(gcc -print-file-name=plugin)/include
 *
 * Compile an application with DTO + plugin:
 *   gcc -O2 -fno-builtin -fplugin=./dto_inline_memcpy.so \
 *       -o myapp myapp.c -L<dto_path> -ldto -Wl,-rpath,<dto_path>
 *
 * The plugin requires -fno-builtin so that memcpy/memset go through PLT
 * (allowing DTO to intercept large operations). The plugin then inlines
 * the small ones that VRP can prove are bounded.
 */

#include "gcc-plugin.h"
#include "plugin-version.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "tree-pass.h"
#include "context.h"
#include "basic-block.h"
#include "ssa.h"
#include "gimple-expr.h"
#include "tree-cfg.h"
#include "value-range.h"
#include "gimple-range.h"

int plugin_is_GPL_compatible;

#define DTO_INLINE_THRESHOLD 512

/* build_string length does NOT include the NUL terminator */
static tree
constraint(const char *s)
{
	return build_string(strlen(s), s);
}

/*
 * Build an asm operand TREE_LIST as GCC's gimplifier expects:
 *   TREE_PURPOSE = TREE_LIST(NULL_TREE, constraint_STRING_CST)
 *   TREE_VALUE   = operand expression
 */
static tree
asm_operand(const char *c, tree operand)
{
	return build_tree_list(
		build_tree_list(NULL_TREE, constraint(c)),
		operand);
}

/* Clobber entries are simpler: TREE_LIST(NULL_TREE, string) */
static tree
asm_clobber(const char *c)
{
	return build_tree_list(NULL_TREE, constraint(c));
}

namespace {

const pass_data dto_pass_data = {
	GIMPLE_PASS,
	"dto_inline_memcpy",
	OPTGROUP_NONE,
	TV_NONE,
	PROP_ssa | PROP_cfg,
	0, 0, 0,
	TODO_update_ssa
};

class dto_inline_pass : public gimple_opt_pass {
public:
	dto_inline_pass(gcc::context *ctx)
		: gimple_opt_pass(dto_pass_data, ctx) {}

	opt_pass *clone() final override {
		return new dto_inline_pass(m_ctxt);
	}

	bool gate(function *) final override {
		return optimize > 0;
	}

	unsigned int execute(function *fun) final override;

private:
	bool size_is_bounded(gimple_ranger *ranger, gimple *stmt,
			     tree size, unsigned HOST_WIDE_INT threshold);

	bool replace_memcpy(gimple_stmt_iterator *gsi, gcall *call,
			    gimple_ranger *ranger);

	bool replace_memset(gimple_stmt_iterator *gsi, gcall *call,
			    gimple_ranger *ranger);
};

bool
dto_inline_pass::size_is_bounded(gimple_ranger *ranger, gimple *stmt,
				 tree size, unsigned HOST_WIDE_INT threshold)
{
	if (TREE_CODE(size) == INTEGER_CST)
		return tree_to_uhwi(size) < threshold;

	if (TREE_CODE(size) != SSA_NAME)
		return false;

	int_range_max vr;
	if (!ranger->range_of_expr(vr, size, stmt))
		return false;

	if (vr.undefined_p() || vr.varying_p())
		return false;

	wide_int ub = vr.upper_bound();
	return wi::ltu_p(ub, threshold);
}

bool
dto_inline_pass::replace_memcpy(gimple_stmt_iterator *gsi, gcall *call,
				gimple_ranger *ranger)
{
	tree size = gimple_call_arg(call, 2);

	/*
	 * Skip compile-time constants — GCC already inlines those
	 * optimally (register moves, XMM stores, etc.)
	 */
	if (TREE_CODE(size) == INTEGER_CST)
		return false;

	if (!size_is_bounded(ranger, call, size, DTO_INLINE_THRESHOLD))
		return false;

	tree dst = gimple_call_arg(call, 0);
	tree src = gimple_call_arg(call, 1);
	tree lhs = gimple_call_lhs(call);

	/*
	 * Tiered inline memcpy:
	 *   <= 16:  two overlapping 8-byte mov copies
	 *   <= 32:  two overlapping 16-byte xmm copies
	 *   <= 64:  two overlapping 32-byte ymm copies
	 *   <= 128: four overlapping 32-byte ymm copies
	 *   > 128:  AVX2 64-byte loop + overlapping tail
	 *
	 * The overlap technique: copy the first N bytes and the
	 * last N bytes. For sizes between N and 2*N they overlap
	 * harmlessly. This avoids all branches within a tier.
	 */
	const char *asm_template =
		/* > 128: AVX2 loop */
		"cmpq $128, %2\n\t"
		"ja 5f\n\t"
		/* <= 16: two 8-byte movs */
		"cmpq $16, %2\n\t"
		"ja 2f\n\t"
		"cmpq $1, %2\n\t"
		"je 1f\n\t"
		"movq (%1), %%rax\n\t"
		"movq -8(%1,%2), %%r8\n\t"
		"movq %%rax, (%0)\n\t"
		"movq %%r8, -8(%0,%2)\n\t"
		"jmp 9f\n"
		"1:\n\t"
		"movzbl (%1), %%eax\n\t"
		"movb %%al, (%0)\n\t"
		"jmp 9f\n"
		/* <= 32: two 16-byte xmm copies */
		"2:\n\t"
		"cmpq $32, %2\n\t"
		"ja 3f\n\t"
		"vmovdqu (%1), %%xmm0\n\t"
		"vmovdqu -16(%1,%2), %%xmm1\n\t"
		"vmovdqu %%xmm0, (%0)\n\t"
		"vmovdqu %%xmm1, -16(%0,%2)\n\t"
		"jmp 9f\n"
		/* <= 64: two 32-byte ymm copies */
		"3:\n\t"
		"cmpq $64, %2\n\t"
		"ja 4f\n\t"
		"vmovdqu (%1), %%ymm0\n\t"
		"vmovdqu -32(%1,%2), %%ymm1\n\t"
		"vmovdqu %%ymm0, (%0)\n\t"
		"vmovdqu %%ymm1, -32(%0,%2)\n\t"
		"vzeroupper\n\t"
		"jmp 9f\n"
		/* <= 128: four 32-byte ymm copies */
		"4:\n\t"
		"vmovdqu (%1), %%ymm0\n\t"
		"vmovdqu 0x20(%1), %%ymm1\n\t"
		"vmovdqu -64(%1,%2), %%ymm2\n\t"
		"vmovdqu -32(%1,%2), %%ymm3\n\t"
		"vmovdqu %%ymm0, (%0)\n\t"
		"vmovdqu %%ymm1, 0x20(%0)\n\t"
		"vmovdqu %%ymm2, -64(%0,%2)\n\t"
		"vmovdqu %%ymm3, -32(%0,%2)\n\t"
		"vzeroupper\n\t"
		"jmp 9f\n"
		/* > 128: AVX2 64-byte loop + tail */
		".p2align 4\n"
		"5:\n\t"
		"vmovdqu -64(%1,%2), %%ymm2\n\t"
		"vmovdqu -32(%1,%2), %%ymm3\n\t"
		".p2align 4\n"
		"6:\n\t"
		"vmovdqu (%1), %%ymm0\n\t"
		"vmovdqu 0x20(%1), %%ymm1\n\t"
		"vmovdqu %%ymm0, (%0)\n\t"
		"vmovdqu %%ymm1, 0x20(%0)\n\t"
		"addq $64, %1\n\t"
		"addq $64, %0\n\t"
		"subq $64, %2\n\t"
		"cmpq $64, %2\n\t"
		"ja 6b\n\t"
		"vmovdqu %%ymm2, (%0)\n\t"
		"vmovdqu %%ymm3, 0x20(%0)\n\t"
		"vzeroupper\n"
		"9:";

	vec<tree, va_gc> *outputs = NULL;
	vec_safe_push(outputs, asm_operand("=D", create_tmp_var(ptr_type_node, "dto_dst")));
	vec_safe_push(outputs, asm_operand("=S", create_tmp_var(ptr_type_node, "dto_src")));
	vec_safe_push(outputs, asm_operand("=c", create_tmp_var(size_type_node, "dto_cnt")));

	vec<tree, va_gc> *inputs = NULL;
	vec_safe_push(inputs, asm_operand("0", dst));
	vec_safe_push(inputs, asm_operand("1", src));
	vec_safe_push(inputs, asm_operand("2", size));

	vec<tree, va_gc> *clobbers = NULL;
	vec_safe_push(clobbers, asm_clobber("memory"));
	vec_safe_push(clobbers, asm_clobber("cc"));
	vec_safe_push(clobbers, asm_clobber("rax"));
	vec_safe_push(clobbers, asm_clobber("r8"));
	vec_safe_push(clobbers, asm_clobber("xmm0"));
	vec_safe_push(clobbers, asm_clobber("xmm1"));
	vec_safe_push(clobbers, asm_clobber("xmm2"));
	vec_safe_push(clobbers, asm_clobber("xmm3"));

	gasm *asm_stmt = gimple_build_asm_vec(
		asm_template, inputs, outputs, clobbers, NULL);
	gimple_asm_set_volatile(asm_stmt, true);

	/* memcpy returns dst — preserve the return value */
	if (lhs) {
		gimple *assign = gimple_build_assign(lhs, dst);
		gsi_insert_before(gsi, assign, GSI_SAME_STMT);
	}

	gsi_replace(gsi, asm_stmt, true);

	if (dump_file)
		fprintf(dump_file,
			"DTO: replaced bounded memcpy with tiered inline copy\n");

	return true;
}

bool
dto_inline_pass::replace_memset(gimple_stmt_iterator *gsi, gcall *call,
				gimple_ranger *ranger)
{
	tree size = gimple_call_arg(call, 2);

	if (TREE_CODE(size) == INTEGER_CST)
		return false;

	if (!size_is_bounded(ranger, call, size, DTO_INLINE_THRESHOLD))
		return false;

	tree dst = gimple_call_arg(call, 0);
	tree val = gimple_call_arg(call, 1);
	tree lhs = gimple_call_lhs(call);

	/*
	 * Tiered inline memset:
	 *   <= 8:   broadcast byte to 8-byte reg, overlapping stores
	 *   <= 32:  broadcast to xmm, overlapping 16-byte stores
	 *   <= 64:  broadcast to ymm, overlapping 32-byte stores
	 *   <= 128: broadcast to ymm, two pairs of 32-byte stores
	 *   > 128:  AVX2 64-byte loop + overlapping tail
	 *
	 * %0 = dst (RDI), %1 = count (RCX), val in EAX
	 */
	const char *asm_template =
		/* Broadcast AL -> 8 bytes in RAX: 0x0101010101010101 * AL */
		"movzbl %%al, %%eax\n\t"
		"movabs $0x0101010101010101, %%r8\n\t"
		"imulq %%r8, %%rax\n\t"
		/* > 128: AVX2 loop */
		"cmpq $128, %1\n\t"
		"ja 5f\n\t"
		/* <= 8: overlapping 8-byte stores */
		"cmpq $8, %1\n\t"
		"ja 2f\n\t"
		"cmpq $1, %1\n\t"
		"je 1f\n\t"
		"movq %%rax, (%0)\n\t"
		"movq %%rax, -8(%0,%1)\n\t"
		"jmp 9f\n"
		"1:\n\t"
		"movb %%al, (%0)\n\t"
		"jmp 9f\n"
		/* <= 32: overlapping 16-byte xmm stores */
		"2:\n\t"
		"cmpq $32, %1\n\t"
		"ja 3f\n\t"
		"vmovq %%rax, %%xmm0\n\t"
		"vpbroadcastq %%xmm0, %%xmm0\n\t"
		"vmovdqu %%xmm0, (%0)\n\t"
		"vmovdqu %%xmm0, -16(%0,%1)\n\t"
		"jmp 9f\n"
		/* <= 64: overlapping 32-byte ymm stores */
		"3:\n\t"
		"cmpq $64, %1\n\t"
		"ja 4f\n\t"
		"vmovq %%rax, %%xmm0\n\t"
		"vpbroadcastq %%xmm0, %%ymm0\n\t"
		"vmovdqu %%ymm0, (%0)\n\t"
		"vmovdqu %%ymm0, -32(%0,%1)\n\t"
		"vzeroupper\n\t"
		"jmp 9f\n"
		/* <= 128: four 32-byte ymm stores */
		"4:\n\t"
		"vmovq %%rax, %%xmm0\n\t"
		"vpbroadcastq %%xmm0, %%ymm0\n\t"
		"vmovdqu %%ymm0, (%0)\n\t"
		"vmovdqu %%ymm0, 0x20(%0)\n\t"
		"vmovdqu %%ymm0, -64(%0,%1)\n\t"
		"vmovdqu %%ymm0, -32(%0,%1)\n\t"
		"vzeroupper\n\t"
		"jmp 9f\n"
		/* > 128: AVX2 64-byte loop + overlapping tail */
		".p2align 4\n"
		"5:\n\t"
		"vmovq %%rax, %%xmm0\n\t"
		"vpbroadcastq %%xmm0, %%ymm0\n\t"
		"leaq -64(%0,%1), %%r8\n\t"
		".p2align 4\n"
		"6:\n\t"
		"vmovdqu %%ymm0, (%0)\n\t"
		"vmovdqu %%ymm0, 0x20(%0)\n\t"
		"addq $64, %0\n\t"
		"cmpq %%r8, %0\n\t"
		"jb 6b\n\t"
		"vmovdqu %%ymm0, (%%r8)\n\t"
		"vmovdqu %%ymm0, 0x20(%%r8)\n\t"
		"vzeroupper\n"
		"9:";

	vec<tree, va_gc> *outputs = NULL;
	vec_safe_push(outputs, asm_operand("=D", create_tmp_var(ptr_type_node, "dto_dst")));
	vec_safe_push(outputs, asm_operand("=c", create_tmp_var(size_type_node, "dto_cnt")));

	vec<tree, va_gc> *inputs = NULL;
	vec_safe_push(inputs, asm_operand("0", dst));
	vec_safe_push(inputs, asm_operand("1", size));
	vec_safe_push(inputs, asm_operand("a", val));

	vec<tree, va_gc> *clobbers = NULL;
	vec_safe_push(clobbers, asm_clobber("memory"));
	vec_safe_push(clobbers, asm_clobber("cc"));
	vec_safe_push(clobbers, asm_clobber("r8"));
	vec_safe_push(clobbers, asm_clobber("xmm0"));

	gasm *asm_stmt = gimple_build_asm_vec(
		asm_template, inputs, outputs, clobbers, NULL);
	gimple_asm_set_volatile(asm_stmt, true);

	if (lhs) {
		gimple *assign = gimple_build_assign(lhs, dst);
		gsi_insert_before(gsi, assign, GSI_SAME_STMT);
	}

	gsi_replace(gsi, asm_stmt, true);

	if (dump_file)
		fprintf(dump_file,
			"DTO: replaced bounded memset with rep stosb\n");

	return true;
}

unsigned int
dto_inline_pass::execute(function *fun)
{
	gimple_ranger *ranger = enable_ranger(fun);
	bool changed = false;

	basic_block bb;
	FOR_EACH_BB_FN(bb, fun) {
		for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
		     !gsi_end_p(gsi); ) {
			gimple *stmt = gsi_stmt(gsi);

			if (!is_gimple_call(stmt)) {
				gsi_next(&gsi);
				continue;
			}

			gcall *call = as_a<gcall *>(stmt);
			tree fndecl = gimple_call_fndecl(call);

			if (!fndecl) {
				gsi_next(&gsi);
				continue;
			}

			bool replaced = false;

			bool is_memcpy = fndecl_built_in_p(fndecl, BUILT_IN_MEMCPY);
			bool is_memset = fndecl_built_in_p(fndecl, BUILT_IN_MEMSET);

			/* Also match by name for -fno-builtin */
			if (!is_memcpy && !is_memset && DECL_NAME(fndecl)) {
				const char *name = IDENTIFIER_POINTER(DECL_NAME(fndecl));
				if (strcmp(name, "memcpy") == 0)
					is_memcpy = true;
				else if (strcmp(name, "memset") == 0)
					is_memset = true;
			}

			if (is_memcpy)
				replaced = replace_memcpy(&gsi, call, ranger);
			else if (is_memset)
				replaced = replace_memset(&gsi, call, ranger);

			if (replaced)
				changed = true;

			gsi_next(&gsi);
		}
	}

	disable_ranger(fun);
	return changed ? TODO_update_ssa : 0;
}

} /* anonymous namespace */

int
plugin_init(struct plugin_name_args *plugin_info,
	    struct plugin_gcc_version *version)
{
	if (!plugin_default_version_check(version, &gcc_version)) {
		fprintf(stderr,
			"dto_inline_memcpy: GCC version mismatch\n");
		return 1;
	}

	struct register_pass_info pass_info;
	pass_info.pass = new dto_inline_pass(g);
	pass_info.reference_pass_name = "optimized";
	pass_info.ref_pass_instance_number = 0;
	pass_info.pos_op = PASS_POS_INSERT_BEFORE;

	register_callback(plugin_info->base_name,
			  PLUGIN_PASS_MANAGER_SETUP,
			  NULL, &pass_info);

	return 0;
}
