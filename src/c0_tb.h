#include "../tb.h"

#ifdef C0_TB_IMPL

void c0_tb_emit_proc(TB_Module *mod, C0Proc *p) {
	assert(p->sig->kind == C0AggType_proc);

	usize param_count = c0array_len(p->sig->proc.types);
	for (usize i = 0; i < n; i++) {
		C0AggType *pt = type->proc.types[i];

		// lower parameter into TB stuff
		switch (pt->kind) {
			case C0AggType_basic:
			if (type->basic.type == C0Basic_ptr) {
			} else {
			}
			break;
		}
	}

	TB_FunctionPrototype* proto = tb_prototype_create(mod, TB_CDECL, TB_TYPE_VOID, NULL, 1, false);
	tb_prototype_add_param(proto, TB_TYPE_PTR);

	TB_Function* func = tb_function_create(mod, "entry", TB_LINKAGE_PUBLIC);
	tb_function_set_prototype(func, proto);
	tb_inst_ret(func, TB_NULL_REG);

	tb_function_print(func, tb_default_print_callback, stdout, false);
	// tb_module_compile_function(mod, func, TB_ISEL_FAST);
}

#endif /* C0_TB_IMPL */
