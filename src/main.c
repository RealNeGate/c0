#include "c0.c"

#define C0_TB_IMPL
#include "c0_tb.h"

C0Proc *test_factorial(C0Gen *gen) {
	C0AggType *agg_u32 = c0_agg_type_basic(gen, C0Basic_u32);

	C0Array(C0AggType *) sig_types = NULL;
	c0array_push(sig_types, agg_u32);

	C0Array(C0String) sig_names = NULL;
	c0array_push(sig_names, C0STR("n"));

	C0Proc *p = c0_proc_create(gen, C0STR("factorial"), c0_agg_type_proc(gen, agg_u32, sig_names, sig_types, 0));

	C0Instr *n = p->parameters[0];

	C0Instr *cond = c0_push_lt(p, n, c0_push_basic_u32(p, 2));
	c0_push_if(p, cond);
	{
		c0_push_return(p, c0_push_basic_u32(p, 1));
	}
	c0_pop_if(p);
	{
		C0Instr *one_below = c0_push_call_proc1(p, p, c0_push_sub(p, n, c0_push_basic_u32(p, 1)));
		C0Instr *res = c0_push_mul(p, n, one_below);
		c0_push_return(p, res);
	}


	return c0_proc_finish(p);
}

C0Proc *test_fibonacci(C0Gen *gen) {
	C0AggType *agg_u32 = c0_agg_type_basic(gen, C0Basic_u32);

	C0Array(C0AggType *) sig_types = NULL;
	c0array_push(sig_types, agg_u32);

	C0Array(C0String) sig_names = NULL;
	c0array_push(sig_names, C0STR("n"));

	C0Proc *p = c0_proc_create(gen, C0STR("fibonacci"), c0_agg_type_proc(gen, agg_u32, sig_names, sig_types, 0));

	C0Instr *n = p->parameters[0];

	C0Instr *cond = c0_push_lteq(p, n, c0_push_basic_u32(p, 2));
	c0_push_if(p, cond);
	{
		c0_push_return(p, n);
	}
	c0_pop_if(p);
	{
		C0Instr *a = c0_push_call_proc1(p, p, c0_push_sub(p, n, c0_push_basic_u32(p, 1)));
		C0Instr *b = c0_push_call_proc1(p, p, c0_push_sub(p, n, c0_push_basic_u32(p, 2)));
		C0Instr *res = c0_push_add(p, a, b);
		c0_push_return(p, res);
	}


	return c0_proc_finish(p);
}


int main(int argc, char const **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	c0_platform_virtual_memory_init();
	C0Gen gen = {0};
	c0_gen_init(&gen);

	C0Proc *factorial = test_factorial(&gen);
	C0Proc *fibonacci = test_fibonacci(&gen);

	C0Printer printer = {0};
	printer.flags |= C0PrinterFlag_UseInlineArgs;

	c0_gen_instructions_print(&printer, &gen);
	c0_print_proc(&printer, factorial);
	c0_print_proc(&printer, fibonacci);

	// TB_FeatureSet features = { 0 };
	// TB_Module* mod = tb_module_create_for_host(&features, true);

	arena_free_all(&printer.arena);

	fflush(stderr);
	fflush(stdout);
	c0_gen_destroy(&gen);
	return 0;
}
