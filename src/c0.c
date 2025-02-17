#include "c0.h"
#include <stdlib.h>

#if !defined(C0_NO_WINDOWS_H)
	#define NOMINMAX            1
	#if !defined(C0_WINDOWS_H_INCLUDED)
	#define WIN32_LEAN_AND_MEAN 1
	#define WIN32_MEAN_AND_LEAN 1
	#define VC_EXTRALEAN        1
	#endif
	#include <windows.h>
	#undef NOMINMAX
	#if !defined(C0_WINDOWS_H_INCLUDED)
	#undef WIN32_LEAN_AND_MEAN
	#undef WIN32_MEAN_AND_LEAN
	#undef VC_EXTRALEAN
	#endif
#endif

#if defined(__cplusplus)
	#include <atomic>
	#define C0Atomic(T) std::atomic<T>
	#define c0_atomic_fetch_add(ptr, x) (ptr)->fetch_add((x))
	#define c0_atomic_fetch_sub(ptr, x) (ptr)->fetch_sub((x))
	#define c0_atomic_load(ptr)         (ptr)->load()
	#define c0_atomic_store(ptr, x)     (ptr)->store((x))
#else
	#define C0Atomic(T) _Atomic T
	#define c0_atomic_fetch_add(ptr, x) atomic_fetch_add((ptr), (x))
	#define c0_atomic_fetch_sub(ptr, x) atomic_fetch_sub((ptr), (x))
	#define c0_atomic_load(ptr)         atomic_load((ptr))
	#define c0_atomic_store(ptr, x)     atomic_store((ptr), (x))
#endif

void c0_assert_handler(char const *prefix, char const *condition, char const *file, int line, char const *msg, ...) {
	fprintf(stderr, "%s(%d): %s: ", file, line, prefix);
	if (condition)
		fprintf(stderr,  "`%s` ", condition);
	if (msg) {
		va_list va;
		va_start(va, msg);
		vfprintf(stderr, msg, va);
		va_end(va);
	}
	fprintf(stderr, "\n");
}


void *c0_heap_calloc(isize elem_size, isize count) {
	C0_ASSERT(elem_size > 0);
	if (count == 0) {
		return NULL;
	}
	C0_ASSERT(count > 0);
	void *ptr = calloc(elem_size, count);
	C0_ASSERT(ptr);
	return ptr;
}
void *c0_heap_alloc(isize size) {
	C0_ASSERT(size >= 0);
	return c0_heap_calloc(1, size);
}
void c0_heap_free(void *ptr) {
	if (ptr) {
		free(ptr);
	}
}
void *c0_heap_resize(void *ptr, isize size) {
	if (ptr == NULL) {
		return c0_heap_alloc(size);
	} else if (size == 0) {
		c0_heap_free(ptr);
		return NULL;
	}
	C0_ASSERT(size > 0);
	return realloc(ptr, size);
}

bool c0array_grow_internal(void **const array, usize elements, usize type_size) {
	usize count = 0;
	void *data = 0;
	if (*array) {
		C0Array *const meta = c0array_meta(*array);
		count = 2 * meta->cap + elements;
		data = c0_heap_resize(meta, type_size * count + sizeof(*meta));
		if (!data) {
			c0_heap_free(meta);
			return false;
		}
	} else {
		count = elements + 1;
		data = c0_heap_alloc(type_size * count + sizeof(C0Array));
		if (!data) {
			return false;
		}
		((C0Array *)data)->len = 0;
	}
	C0Array *meta = (C0Array *)data;
	meta->cap = count;
	*array = meta + 1;
	return true;
}
void c0array_delete(void *const array) {
	if (array) {
		c0_heap_free(c0array_meta(array));
	}
}


C0String c0_arena_str_dup(C0Arena *arena, C0String str) {
	char *text = NULL;
	if (str.len) {
		text = (char *)c0_arena_alloc(arena, str.len, 1);
		memcpy(text, str.text, str.len);
	}
	C0String res;
	res.text = text;
	res.len = str.len;
	return res;
}
char const *c0_arena_cstr_dup(C0Arena *arena, char const *str) {
	char *text = NULL;
	if (str) {
		usize len = strlen(str);
		text = (char *)c0_arena_alloc(arena, len+1, 1);
		memcpy(text, str, len);
		text[len] = 0;
	}
	return text;
}


static usize c0_align_formula(usize size, usize align) {
	usize result = size + align-1;
	return result - result%align;
}

enum { C0_DEFAULT_MINIMUM_BLOCK_SIZE = 8ll*1024ll*1024ll };

static usize DEFAULT_PAGE_SIZE = 4096;

static C0MemoryBlock *c0_virtual_memory_alloc(usize size);
static void c0_virtual_memory_dealloc(C0MemoryBlock *block);


static usize arena_align_forward_offset(C0Arena *arena, usize alignment) {
	usize alignment_offset = 0;
	usize ptr = (usize)(arena->curr_block->base + arena->curr_block->used);
	usize mask = alignment-1;
	if (ptr & mask) {
		alignment_offset = alignment - (ptr & mask);
	}
	return alignment_offset;
}

void *c0_arena_alloc(C0Arena *arena, usize min_size, usize alignment) {
	// mutex_lock(&arena->mutex);

	usize size = 0;
	if (arena->curr_block != NULL) {
		size = min_size + arena_align_forward_offset(arena, alignment);
	}

	if (arena->curr_block == NULL || (arena->curr_block->used + size) > arena->curr_block->size) {
		size = c0_align_formula(min_size, alignment);
		if (arena->minimum_block_size < C0_DEFAULT_MINIMUM_BLOCK_SIZE) {
			arena->minimum_block_size = C0_DEFAULT_MINIMUM_BLOCK_SIZE;
		}

		usize block_size = size;
		if (block_size < arena->minimum_block_size) {
			block_size = arena->minimum_block_size;
		}

		C0MemoryBlock *new_block = c0_virtual_memory_alloc(block_size);
		new_block->prev = arena->curr_block;
		arena->curr_block = new_block;
	}

	C0MemoryBlock *curr_block = arena->curr_block;
	C0_ASSERT((curr_block->used + size) <= curr_block->size);

	u8 *ptr = curr_block->base + curr_block->used;
	ptr += arena_align_forward_offset(arena, alignment);

	curr_block->used += size;
	C0_ASSERT(curr_block->used <= curr_block->size);

	// mutex_unlock(&arena->mutex);

	// NOTE(bill): memory will be zeroed by default due to virtual memory
	return ptr;
}

void arena_free_all(C0Arena *arena) {
	while (arena->curr_block != NULL) {
		C0MemoryBlock *free_block = arena->curr_block;
		arena->curr_block = free_block->prev;
		c0_virtual_memory_dealloc(free_block);
	}
}


struct C0PlatformMemoryBlock {
	C0MemoryBlock block; // IMPORTANT NOTE: must be at the start
	usize total_size;
	C0PlatformMemoryBlock *prev, *next;
};


static C0Atomic(usize) c0_global_platform_memory_total_usage;
static C0PlatformMemoryBlock c0_global_platform_memory_block_sentinel;

static C0PlatformMemoryBlock *c0_platform_virtual_memory_alloc(isize total_size);
static void c0_platform_virtual_memory_free(C0PlatformMemoryBlock *block);
static void c0_platform_virtual_memory_protect(void *memory, isize size);

#if defined(_WIN32)
	void c0_platform_virtual_memory_init(void) {
		c0_global_platform_memory_block_sentinel.prev = &c0_global_platform_memory_block_sentinel;
		c0_global_platform_memory_block_sentinel.next = &c0_global_platform_memory_block_sentinel;

		SYSTEM_INFO sys_info = {0};
		GetSystemInfo(&sys_info);
		usize sys_page_size = sys_info.dwPageSize;
		if (DEFAULT_PAGE_SIZE < sys_page_size) {
			DEFAULT_PAGE_SIZE = sys_page_size;
		}
	}

	static C0PlatformMemoryBlock *c0_platform_virtual_memory_alloc(usize total_size) {
		C0PlatformMemoryBlock *pmblock = (C0PlatformMemoryBlock *)VirtualAlloc(0, total_size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
		if (pmblock == NULL) {
			fprintf(stderr, "Out of Virtual memory, oh no...\n");
			fprintf(stderr, "Requested: %llu bytes\n", (unsigned long long)total_size);
			fprintf(stderr, "Total Usage: %llu bytes\n", (unsigned long long)c0_global_platform_memory_total_usage);
			C0_ASSERT(pmblock != NULL && "Out of Virtual Memory, oh no...");
		}
		c0_global_platform_memory_total_usage += total_size; // @atomic
		return pmblock;
	}
	static void c0_platform_virtual_memory_free(C0PlatformMemoryBlock *block) {
		c0_global_platform_memory_total_usage -= block->total_size; // @atomic
		C0_ASSERT(VirtualFree(block, 0, MEM_RELEASE));
	}
	static void c0_platform_virtual_memory_protect(void *memory, isize size) {
		DWORD old_protect = 0;
		BOOL is_protected = VirtualProtect(memory, size, PAGE_NOACCESS, &old_protect);
		C0_ASSERT(is_protected);
	}
#else
	static void c0_platform_virtual_memory_init(void) {
		c0_global_platform_memory_block_sentinel.prev = &c0_global_platform_memory_block_sentinel;
		c0_global_platform_memory_block_sentinel.next = &c0_global_platform_memory_block_sentinel;

		usize sys_page_size = sysconf(_SC_PAGE_SIZE);
		if (DEFAULT_PAGE_SIZE < sys_page_size) {
			DEFAULT_PAGE_SIZE = sys_page_size;
		}
	}

	static C0PlatformMemoryBlock *c0_platform_virtual_memory_alloc(isize total_size) {
		C0PlatformMemoryBlock *pmblock = (C0PlatformMemoryBlock *)mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (pmblock == NULL) {
			fprintf(stderr, "Out of Virtual memory, oh no...\n");
			fprintf(stderr, "Requested: %lld bytes\n", (long long)total_size);
			fprintf(stderr, "Total Usage: %lld bytes\n", (long long)c0_global_platform_memory_total_usage);
			C0_ASSERT(pmblock != NULL, "Out of Virtual Memory, oh no...");
		}
		c0_global_platform_memory_total_usage += total_size;
		return pmblock;
	}
	static void c0_platform_virtual_memory_free(C0PlatformMemoryBlock *block) {
		isize size = block->total_size;
		c0_global_platform_memory_total_usage -= size;
		munmap(block, size);
	}
	static void c0_platform_virtual_memory_protect(void *memory, isize size) {
		int err = mprotect(memory, size, PROT_NONE);
		C0_ASSERT(err == 0);
	}
#endif

static C0MemoryBlock *c0_virtual_memory_alloc(usize size) {
	usize const page_size = DEFAULT_PAGE_SIZE;

	usize total_size     = size + sizeof(C0PlatformMemoryBlock);
	usize base_offset    = sizeof(C0PlatformMemoryBlock);
	usize protect_offset = 0;

	bool do_protection = false;
	{ // overflow protection
		usize rounded_size = c0_align_formula(size, page_size);
		total_size     = rounded_size + 2*page_size;
		base_offset    = page_size + rounded_size - size;
		protect_offset = page_size + rounded_size;
		do_protection  = true;
	}

	C0PlatformMemoryBlock *pmblock = c0_platform_virtual_memory_alloc(total_size);
	C0_ASSERT(pmblock != NULL && "Out of Virtual Memory, oh no...");

	pmblock->block.base = (u8 *)pmblock + base_offset;
	// Should be zeroed
	C0_ASSERT(pmblock->block.used == 0);
	C0_ASSERT(pmblock->block.prev == NULL);

	if (do_protection) {
		c0_platform_virtual_memory_protect((u8 *)pmblock + protect_offset, page_size);
	}

	pmblock->block.size = size;
	pmblock->total_size = total_size;

	C0PlatformMemoryBlock *sentinel = &c0_global_platform_memory_block_sentinel;
	// mutex_lock(&global_memory_block_mutex);
	pmblock->next = sentinel;
	pmblock->prev = sentinel->prev;
	pmblock->prev->next = pmblock;
	pmblock->next->prev = pmblock;
	// mutex_unlock(&global_memory_block_mutex);

	return &pmblock->block;
}

static void c0_virtual_memory_dealloc(C0MemoryBlock *block_to_free) {
	C0PlatformMemoryBlock *block = (C0PlatformMemoryBlock *)block_to_free;
	if (block != NULL) {
		// mutex_lock(&global_memory_block_mutex);
		block->prev->next = block->next;
		block->next->prev = block->prev;
		// mutex_unlock(&global_memory_block_mutex);

		c0_platform_virtual_memory_free(block);
	}
}


///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


static char *c0_type_to_cdecl(C0Arena *a, C0AggType *type, char const *str);

static void c0_warning(char const *msg) {
	fputs("WARNING: ", stderr);
	fputs(msg, stderr);
	fputs("\n", stderr);
}


static void c0_errorf(char const *fmt, ...) {
	fputs("ERROR: ", stderr);
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fputs("\n", stderr);
	C0_DEBUG_TRAP();
}

i64 c0_basic_type_size(C0Gen *gen, C0BasicType type) {
	i64 size = c0_basic_type_sizes[type];
	if (size < 0) {
		size = (-size) * gen->ptr_size;
	}
	return size;
}

void c0_gen_init(C0Gen *gen) {
	memset(gen, 0, sizeof(*gen));

	gen->ptr_size = 8;
	for (C0BasicType kind = C0Basic_void; kind < C0Basic_COUNT; kind++) {
		C0AggType *t = c0_arena_new(&gen->arena, C0AggType);
		t->kind = C0AggType_basic;
		t->basic.type = kind;
		t->size  = c0_basic_type_size(gen, kind);
		t->align = t->size;
		gen->basic_agg[kind] = t;
	}
}

void c0_gen_destroy(C0Gen *gen) {
	arena_free_all(&gen->arena);
}

bool c0_basic_type_is_integer(C0BasicType type) {
	switch (type) {
	case C0Basic_i8:
	case C0Basic_u8:
	case C0Basic_i16:
	case C0Basic_u16:
	case C0Basic_i32:
	case C0Basic_u32:
	case C0Basic_i64:
	case C0Basic_u64:
	case C0Basic_i128:
	case C0Basic_u128:
		return true;
	case C0Basic_f16:
	case C0Basic_f32:
	case C0Basic_f64:
		return false;
	case C0Basic_ptr:
		return false;
	}
	return false;
}
bool c0_basic_type_is_float(C0BasicType type) {
	switch (type) {
	case C0Basic_i8:
	case C0Basic_u8:
	case C0Basic_i16:
	case C0Basic_u16:
	case C0Basic_i32:
	case C0Basic_u32:
	case C0Basic_i64:
	case C0Basic_u64:
	case C0Basic_i128:
	case C0Basic_u128:
		return false;
	case C0Basic_f16:
	case C0Basic_f32:
	case C0Basic_f64:
		return true;
	case C0Basic_ptr:
		return false;
	}
	return false;
}
bool c0_basic_type_is_ptr(C0BasicType type) {
	switch (type) {
	case C0Basic_i8:
	case C0Basic_u8:
	case C0Basic_i16:
	case C0Basic_u16:
	case C0Basic_i32:
	case C0Basic_u32:
	case C0Basic_i64:
	case C0Basic_u64:
	case C0Basic_i128:
	case C0Basic_u128:
		return false;
	case C0Basic_f16:
	case C0Basic_f32:
	case C0Basic_f64:
		return false;
	case C0Basic_ptr:
		return true;
	}
	return false;
}




C0AggType *c0_agg_type_basic(C0Gen *gen, C0BasicType type) {
	return gen->basic_agg[type];
}

C0AggType *c0_agg_type_array(C0Gen *gen, C0AggType *elem, i64 len) {
	C0_ASSERT(len >= 0);
	C0AggType *t = c0_arena_new(&gen->arena, C0AggType);
	t->kind = C0AggType_array;
	t->array.elem = elem;
	t->array.len = len;
	// TODO(bill): size of the array
	t->size  = len * elem->size;
	t->align = elem->align;
	return t;
}

C0AggType *c0_agg_type_proc(C0Gen *gen, C0AggType *ret, C0Array(C0String) names, C0Array(C0AggType *) types, C0ProcFlags flags) {
	C0AggType *t = c0_arena_new(&gen->arena, C0AggType);
	t->kind = C0AggType_proc;
	t->size = gen->ptr_size;
	t->align = gen->ptr_size;

	if (ret == NULL) {
		ret = c0_agg_type_basic(gen, C0Basic_void);
	}
	t->proc.ret = ret;
	t->proc.names = names;
	t->proc.types = types;
	t->proc.flags = flags;
	return t;
}
static bool c0_types_equal(C0AggType *a, C0AggType *b);

static bool c0_types_array_equal(C0Array(C0AggType *) a, C0Array(C0AggType *) b) {
	if (a == b) {
		return true;
	}
	if (c0array_len(a) != c0array_len(b)) {
		return false;
	}
	usize n = c0array_len(a);
	for (usize i = 0; i < n; i++) {
		if (!c0_types_equal(a[i], b[i])) {
			return false;
		}
	}
	return true;
}

static bool c0_strings_equal(C0String a, C0String b) {
	if (a.len != b.len) {
		return false;
	}
	if (a.text == b.text) {
		return true;
	}
	return memcmp(a.text, b.text, a.len) == 0;
}

static bool c0_string_array_equal(C0Array(C0String) a, C0Array(C0String) b) {
	if (a == b) {
		return true;
	}
	if (c0array_len(a) != c0array_len(b)) {
		return false;
	}
	usize n = c0array_len(a);
	for (usize i = 0; i < n; i++) {
		if (!c0_strings_equal(a[i], b[i])) {
			return false;
		}
	}
	return true;
}


static bool c0_types_equal(C0AggType *a, C0AggType *b) {
	if (a == b) {
		return true;
	}
	if (a->kind == b->kind) {
		switch (a->kind) {
		case C0AggType_basic:
			return a->basic.type == b->basic.type;
		case C0AggType_array:
			return a->array.len == b->array.len && a->array.elem == b->array.elem;
		case C0AggType_record:
			return c0_strings_equal(a->record.name, b->record.name);
		case C0AggType_proc:
			return c0_types_equal(a->proc.ret, b->proc.ret) &&
			       c0_types_array_equal(a->proc.types, b->proc.types) &&
			       a->proc.flags == b->proc.flags;

		}
	}
	return false;
}

static bool c0_types_agg_basic(C0AggType *a, C0BasicType b) {
	return a && a->kind == C0AggType_basic && c0_basic_unsigned_type[a->basic.type] == c0_basic_unsigned_type[b];
}

static bool c0_types_agg_agg(C0AggType *a, C0AggType *b) {
	if (a == b) {
		return true;
	}
	if (!a) {
		return false;
	}
	if (a->kind != b->kind) {
		return false;
	}
	switch (a->kind) {
	case C0AggType_proc:
		return false;
	case C0AggType_array:
		return a->array.len == b->array.len && c0_types_agg_agg(a->array.elem, b->array.elem);
	case C0AggType_record:
		return false;
	}
	return true;
}



C0Proc *c0_proc_create(C0Gen *gen, C0String name, C0AggType *sig) {
	C0Arena *arena = &gen->arena;
	C0Proc *p = c0_arena_new(arena, C0Proc);
	C0_ASSERT(p);
	p->gen = gen;
	p->arena = arena;
	p->name  = c0_arena_str_dup(p->arena, name);
	C0_ASSERT(sig && sig->kind == C0AggType_proc);
	p->sig = sig;

	isize n = c0array_len(sig->proc.names);
	if (n) {
		C0_ASSERT(n == c0array_len(sig->proc.types));
	}
	for (isize i = 0; i < n; i++) {
		C0AggType *type = sig->proc.types[i];
		C0String name = sig->proc.names[i];
		if (name.len > 0) {
			C0Instr *instr = c0_instr_create(p, C0Instr_decl);
			instr->name = name;
			if (type->kind == C0AggType_basic) {
				instr->basic_type = type->basic.type;
			} else {
				instr->agg_type = type;
			}

			c0array_push(p->parameters, instr);
		}
	}

	return p;
}
C0Instr *c0_instr_create(C0Proc *p, C0InstrKind kind) {
	C0Instr *instr = c0_arena_new(p->arena, C0Instr);
	instr->kind = kind;
	instr->basic_type = c0_instr_ret_type[kind];
	return instr;
}
C0Instr *c0_instr_last(C0Proc *p) {
	C0Array(C0Instr *) instrs = NULL;
	usize n = c0array_len(p->nested_blocks);
	if (n > 0) {
		instrs = p->nested_blocks[n-1]->nested_instrs;
	} else {
		instrs = p->instrs;
	}
	if (c0array_len(instrs) > 0) {
		return c0array_last(instrs);
	}
	return NULL;
}

static bool c0_is_instruction_any_break(C0Instr *instr, u32 loop_count) {
	if (instr) {
		if (instr->kind == C0Instr_break) {
			return true;
		}
		if (instr->kind == C0Instr_loop) {
			loop_count += 1;
		}

		if (loop_count > 1) {
			return false;
		}

		usize len = c0array_len(instr->nested_instrs);
		for (usize i = 0; i < len; i++) {
			if (c0_is_instruction_any_break(instr->nested_instrs[i], loop_count)) {
				return true;
			}
		}
	}
	return false;
}

static bool c0_is_instruction_terminating(C0Instr *instr) {
	if (!instr) {
		return false;
	}
	if (instr->kind == C0Instr_return) {
		return true;
	} else if (instr->kind == C0Instr_unreachable) {
		return true;
	} else if (instr->kind == C0Instr_if) {
		if (instr->args_len != 2) {
			return false;
		}
		bool terminating_if   = false;
		bool terminating_else = false;
		if (c0array_len(instr->nested_instrs) == 0) {
			return false;
		}
		C0Instr *last_if = c0array_last(instr->nested_instrs);
		terminating_if = c0_is_instruction_terminating(last_if);
		terminating_else = c0_is_instruction_terminating(instr->args[1]);

		return terminating_if && terminating_else;
	} else if (instr->kind == C0Instr_block) {
		if (c0array_len(instr->nested_instrs) == 0) {
			return false;
		}
		C0Instr *last = c0array_last(instr->nested_instrs);
		return c0_is_instruction_terminating(last);
	} else if (instr->kind == C0Instr_loop) {
		if (c0array_len(instr->nested_instrs) == 0) {
			return true;
		}
		if (c0_is_instruction_any_break(instr, 0)) {
			return false;
		}
		C0Instr *last = c0array_last(instr->nested_instrs);
		return c0_is_instruction_terminating(last);
	}
	return false;
}

C0Instr *c0_instr_push(C0Proc *p, C0Instr *instr) {
	if (c0_is_instruction_terminating(c0_instr_last(p))) {
		c0_warning("next instruction will never be executed");
		return NULL;
	}

	usize n = c0array_len(p->nested_blocks);
	if (n > 0) {
		c0array_push(p->nested_blocks[n-1]->nested_instrs, instr);
	} else {
		c0array_push(p->instrs, instr);
	}
	return instr;
}


C0Instr *c0_push_nested_block(C0Proc *p, C0Instr *block) {
	C0_ASSERT(block);
	switch (block->kind) {
	case C0Instr_if:
	case C0Instr_loop:
	case C0Instr_block:
		break;
	default:
		c0_errorf("invalid block kind");
		break;
	}
	c0array_push(p->nested_blocks, block);
	return block;
}

C0Instr *c0_pop_nested_block(C0Proc *p) {
	usize n = c0array_len(p->nested_blocks);
	C0_ASSERT(n > 0);
	C0Instr *block = p->nested_blocks[n-1];
	c0array_pop(p->nested_blocks);
	return block;
}


C0Instr *c0_use(C0Instr *instr) {
	instr->uses++;
	return instr;
}
C0Instr *c0_unuse(C0Instr *instr) {
	C0_ASSERT(instr->uses > 0);
	instr->uses--;
	return instr;
}

C0Instr *c0_push_basic_i8(C0Proc *p, i8 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_i8;
	val->value_i64 = (i64)value;
	return c0_instr_push(p, val);
}
C0Instr *c0_push_basic_u8(C0Proc *p, u8 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_u8;
	val->value_u64 = (u64)value;
	return c0_instr_push(p, val);
}

C0Instr *c0_push_basic_i16(C0Proc *p, i16 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_i16;
	val->value_i64 = (i64)value;
	return c0_instr_push(p, val);
}
C0Instr *c0_push_basic_u16(C0Proc *p, u16 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_u32;
	val->value_u64 = (u64)value;
	return c0_instr_push(p, val);
}

C0Instr *c0_push_basic_i32(C0Proc *p, i32 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_i32;
	val->value_i64 = (i64)value;
	return c0_instr_push(p, val);
}
C0Instr *c0_push_basic_u32(C0Proc *p, u32 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_u32;
	val->value_u64 = (u64)value;
	return c0_instr_push(p, val);
}

C0Instr *c0_push_basic_i64(C0Proc *p, i64 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_i64;
	val->value_i64 = (i64)value;
	return c0_instr_push(p, val);
}
C0Instr *c0_push_basic_u64(C0Proc *p, u64 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_u64;
	val->value_u64 = (u64)value;
	return c0_instr_push(p, val);
}

C0Instr *c0_push_basic_ptr(C0Proc *p, u64 value) {
	C0Instr *val = c0_instr_create(p, C0Instr_decl);
	val->basic_type = C0Basic_ptr;
	val->value_u64 = (u64)value;
	return c0_instr_push(p, val);
}


static void c0_alloc_args(C0Proc *p, C0Instr *instr, isize len) {
	typedef C0Instr *T;
	instr->args_len = len;
	if (len != 0) {
		instr->args = (T *)c0_arena_alloc(p->arena, sizeof(T)*len, alignof(T));
	}

}

C0Instr *c0_push_bin(C0Proc *p, C0InstrKind kind, C0BasicType type, C0Instr *left, C0Instr *right) {
	C0_ASSERT(left);
	C0_ASSERT(right);
	C0_ASSERT(type != C0Basic_void);
	C0_ASSERT_MSG(c0_basic_unsigned_type[left->basic_type] == c0_basic_unsigned_type[right->basic_type],
	              "%s != %s", c0_basic_names[left->basic_type], c0_basic_names[right->basic_type]);

	C0Instr *bin = c0_instr_create(p, kind);
	if (type != left->basic_type) {
		bin->basic_type = type;
	}
	c0_alloc_args(p, bin, 2);
	bin->args[0] = c0_use(left);
	bin->args[1] = c0_use(right);
	return c0_instr_push(p, bin);
}

#define C0_PUSH_BIN_INT_DEF(name) C0Instr *c0_push_##name(C0Proc *p, C0Instr *left, C0Instr *right) { \
	C0_ASSERT(left); \
	C0_ASSERT(right); \
	C0_ASSERT(left->basic_type != C0Basic_void); \
	C0_ASSERT(left->basic_type == right->basic_type); \
	C0_ASSERT(c0_basic_type_is_integer(left->basic_type)); \
	return c0_push_bin(p, C0Instr_##name##_i8 + (left->basic_type - C0Basic_i8), left->basic_type, left, right); \
}

#define C0_PUSH_BIN_UINT_DEF(name) C0Instr *c0_push_##name(C0Proc *p, C0Instr *left, C0Instr *right) { \
	C0_ASSERT(c0_basic_type_is_integer(left->basic_type)); \
	C0_ASSERT(c0_basic_unsigned_type[left->basic_type] == c0_basic_unsigned_type[right->basic_type]); \
	return c0_push_bin(p, C0Instr_##name##_u8 + c0_basic_unsigned_instr_offset[left->basic_type], left->basic_type, left, right); \
}


C0_PUSH_BIN_UINT_DEF(add);
C0_PUSH_BIN_UINT_DEF(sub);
C0_PUSH_BIN_UINT_DEF(mul);
C0_PUSH_BIN_INT_DEF(quo);
C0_PUSH_BIN_INT_DEF(rem);
C0_PUSH_BIN_INT_DEF(shlc); // masked C-like
C0_PUSH_BIN_INT_DEF(shrc); // masked C-like
C0_PUSH_BIN_INT_DEF(shlo); // Odin-like
C0_PUSH_BIN_INT_DEF(shro); // Odin-like
C0_PUSH_BIN_UINT_DEF(and);
C0_PUSH_BIN_UINT_DEF(or);
C0_PUSH_BIN_UINT_DEF(xor);
C0_PUSH_BIN_UINT_DEF(eq);
C0_PUSH_BIN_UINT_DEF(neq);
C0_PUSH_BIN_INT_DEF(lt);
C0_PUSH_BIN_INT_DEF(gt);
C0_PUSH_BIN_INT_DEF(lteq);
C0_PUSH_BIN_INT_DEF(gteq);

#define C0_PUSH_BIN_FLOAT_DEF(name) C0Instr *c0_push_##name(C0Proc *p, C0Instr *left, C0Instr *right) { \
	C0_ASSERT(c0_basic_type_is_float(left->basic_type)); \
	C0_ASSERT(left->basic_type == right->basic_type); \
	return c0_push_bin(p, C0Instr_##name##_f16 + (left->basic_type - C0Basic_f16), left->basic_type, left, right); \
}

C0_PUSH_BIN_FLOAT_DEF(addf);
C0_PUSH_BIN_FLOAT_DEF(subf);
C0_PUSH_BIN_FLOAT_DEF(mulf);
C0_PUSH_BIN_FLOAT_DEF(divf);
C0_PUSH_BIN_FLOAT_DEF(eqf);
C0_PUSH_BIN_FLOAT_DEF(neqf);
C0_PUSH_BIN_FLOAT_DEF(ltf);
C0_PUSH_BIN_FLOAT_DEF(gtf);
C0_PUSH_BIN_FLOAT_DEF(lteqf);
C0_PUSH_BIN_FLOAT_DEF(gteqf);


#define C0_PUSH_UN_INT_DEF(name) C0Instr *c0_push_##name(C0Proc *p, C0Instr *arg) { \
	C0_ASSERT(c0_basic_type_is_integer(arg->basic_type)); \
	C0Instr *val = c0_instr_create(p, C0Instr_##name##_i8 + (arg->basic_type - C0Basic_i8)); \
	c0_alloc_args(p, val, 1); \
	val->args[0] = c0_use(arg); \
	return c0_instr_push(p, val); \
}

#define C0_PUSH_UN_UINT_DEF(name) C0Instr *c0_push_##name(C0Proc *p, C0Instr *arg) { \
	C0_ASSERT(c0_basic_type_is_integer(arg->basic_type)); \
	C0Instr *val = c0_instr_create(p, C0Instr_##name##_u8 + c0_basic_unsigned_instr_offset[arg->basic_type]); \
	val->basic_type = arg->basic_type; \
	c0_alloc_args(p, val, 1); \
	val->args[0] = c0_use(arg); \
	return c0_instr_push(p, val); \
}

C0_PUSH_UN_UINT_DEF(clz);
C0_PUSH_UN_UINT_DEF(ctz);
C0_PUSH_UN_UINT_DEF(popcnt);

#undef C0_PUSH_UN_INT_DEF

#define C0_PUSH_UN_FLOAT_DEF(name) C0Instr *c0_push_##name(C0Proc *p, C0Instr *arg) { \
	C0_ASSERT(c0_basic_type_is_float(arg->basic_type)); \
	C0Instr *val = c0_instr_create(p, C0Instr_##name##_f16 + (arg->basic_type - C0Basic_f16)); \
	c0_alloc_args(p, val, 1); \
	val->args[0] = c0_use(arg); \
	return c0_instr_push(p, val); \
}

C0_PUSH_UN_FLOAT_DEF(absf);
C0_PUSH_UN_FLOAT_DEF(negf);
C0_PUSH_UN_FLOAT_DEF(ceilf);
C0_PUSH_UN_FLOAT_DEF(floorf);
C0_PUSH_UN_FLOAT_DEF(nearestf);
C0_PUSH_UN_FLOAT_DEF(truncf);
C0_PUSH_UN_FLOAT_DEF(sqrtf);

#undef C0_PUSH_UN_FLOAT_DEF


// pseudo-instruction
C0Instr *c0_push_noti(C0Proc *p, C0Instr *arg) {
	C0Instr *zero = NULL;
	switch (arg->basic_type) {
	case C0Basic_i8:  zero = c0_push_basic_i8(p, 0);
	case C0Basic_u8:  zero = c0_push_basic_u8(p, 0);
	case C0Basic_i16: zero = c0_push_basic_i16(p, 0);
	case C0Basic_u16: zero = c0_push_basic_u16(p, 0);
	case C0Basic_i32: zero = c0_push_basic_i32(p, 0);
	case C0Basic_u32: zero = c0_push_basic_u32(p, 0);
	case C0Basic_i64: zero = c0_push_basic_i64(p, 0);
	case C0Basic_u64: zero = c0_push_basic_u64(p, 0);
	case C0Basic_i128:
	case C0Basic_u128:
		c0_errorf("todo 128 bit integers");
		break;
	default:
		c0_errorf("invalid type to noti");
		break;
	}
	return c0_push_xor(p, arg, zero);
}

// pseudo-instruction
C0Instr *c0_push_notb(C0Proc *p, C0Instr *arg) {
	C0Instr *zero = NULL;
	switch (arg->basic_type) {
	case C0Basic_i8:  zero = c0_push_basic_i8(p, 0);
	case C0Basic_u8:  zero = c0_push_basic_u8(p, 0);
	case C0Basic_i16: zero = c0_push_basic_i16(p, 0);
	case C0Basic_u16: zero = c0_push_basic_u16(p, 0);
	case C0Basic_i32: zero = c0_push_basic_i32(p, 0);
	case C0Basic_u32: zero = c0_push_basic_u32(p, 0);
	case C0Basic_i64: zero = c0_push_basic_i64(p, 0);
	case C0Basic_u64: zero = c0_push_basic_u64(p, 0);
	case C0Basic_i128:
	case C0Basic_u128:
		c0_errorf("todo 128 bit integers");
		break;
	default:
		c0_errorf("invalid type to noti");
		break;
	}
	return c0_push_eq(p, arg, zero);
}

// pseudo-instruction
C0Instr *c0_push_to_bool(C0Proc *p, C0Instr *arg) {
	C0Instr *zero = NULL;
	switch (arg->basic_type) {
	case C0Basic_i8:  zero = c0_push_basic_i8(p, 0);
	case C0Basic_u8:  zero = c0_push_basic_u8(p, 0);
	case C0Basic_i16: zero = c0_push_basic_i16(p, 0);
	case C0Basic_u16: zero = c0_push_basic_u16(p, 0);
	case C0Basic_i32: zero = c0_push_basic_i32(p, 0);
	case C0Basic_u32: zero = c0_push_basic_u32(p, 0);
	case C0Basic_i64: zero = c0_push_basic_i64(p, 0);
	case C0Basic_u64: zero = c0_push_basic_u64(p, 0);
	case C0Basic_i128:
	case C0Basic_u128:
		c0_errorf("todo 128 bit integers");
		break;
	default:
		c0_errorf("invalid type to noti");
		break;
	}
	return c0_push_neq(p, arg, zero);
}


C0Instr *c0_push_unreachable(C0Proc *p) {
	C0Instr *ret = c0_instr_create(p, C0Instr_unreachable);
	c0_use(ret);
	return c0_instr_push(p, ret);
}

C0Instr *c0_push_return(C0Proc *p, C0Instr *arg) {
	C0Instr *last = c0_instr_last(p);
	if (c0_is_instruction_terminating(last)) {
		c0_warning("return no called after previous returns");
		return NULL;
	}

	C0Instr *ret = c0_instr_create(p, C0Instr_return);
	if (arg != NULL) {
		C0_ASSERT(p->sig);
		if (arg->agg_type) {
			if (!c0_types_agg_agg(p->sig->proc.ret, arg->agg_type)) {
				c0_errorf("mismatching types in return: expected %s, got %s\n", c0_type_to_cdecl(p->arena, p->sig->proc.ret, ""), c0_basic_names[arg->basic_type]);
			}
		} else if (!c0_types_agg_basic(p->sig->proc.ret, arg->basic_type)) {
			c0_errorf("mismatching types in return: expected %s, got %s\n", c0_type_to_cdecl(p->arena, p->sig->proc.ret, ""), c0_basic_names[arg->basic_type]);
		}
		c0_alloc_args(p, ret, 1);
		ret->args[0] = c0_use(arg);
	} else {
		if (!c0_types_agg_basic(p->sig->proc.ret, C0Basic_void)) {
			c0_errorf("mismatching types in return: expected void, got %s\n", c0_basic_names[C0Basic_void]);
		}
	}
	return c0_instr_push(p, ret);
}

C0Instr *c0_push_convert(C0Proc *p, C0BasicType type, C0Instr *arg) {
	C0_ASSERT(type != C0Basic_void);
	C0_ASSERT(arg->basic_type != C0Basic_void);
	if (arg->basic_type == type) {
		return arg;
	}
	C0Instr *cvt = c0_instr_create(p, C0Instr_convert);
	c0_alloc_args(p, cvt, 1);
	cvt->args[0] = c0_use(arg);
	cvt->basic_type = type;
	return c0_instr_push(p, cvt);
}
C0Instr *c0_push_reinterpret_basic(C0Proc *p, C0BasicType type, C0Instr *arg) {
	C0_ASSERT(type != C0Basic_void);
	C0_ASSERT(arg->basic_type != C0Basic_void);
	if (arg->basic_type == type) {
		return arg;
	}
	if (c0_basic_type_sizes[type] != c0_basic_type_sizes[arg->basic_type]) {
		c0_errorf("reinterpret requires both types to be of the same size, %s -> %s", c0_basic_names[arg->basic_type], c0_basic_names[type]);
	}
	C0Instr *rip = c0_instr_create(p, C0Instr_reinterpret);
	c0_alloc_args(p, rip, 1);
	rip->args[0] = c0_use(arg);
	rip->basic_type = type;
	return c0_instr_push(p, rip);
}

C0Instr *c0_push_load_basic(C0Proc *p, C0BasicType type, C0Instr *arg) {
	C0_ASSERT(type != C0Basic_void);
	C0_ASSERT(arg->basic_type == C0Basic_ptr);

	C0InstrKind kind = C0Instr_load_u8;

	switch (type) {
	case C0Basic_i8:
	case C0Basic_u8:
		kind = C0Instr_load_u8;
		break;
	case C0Basic_i16:
	case C0Basic_u16:
		kind = C0Instr_load_u16;
		break;
	case C0Basic_i32:
	case C0Basic_u32:
		kind = C0Instr_load_u32;
		break;
	case C0Basic_i64:
	case C0Basic_u64:
		kind = C0Instr_load_u64;
		break;
	case C0Basic_i128:
	case C0Basic_u128:
		kind = C0Instr_load_u128;
		break;
	case C0Basic_f16:
		kind = C0Instr_load_f16;
		break;
	case C0Basic_f32:
		kind = C0Instr_load_f32;
		break;
	case C0Basic_f64:
		kind = C0Instr_load_f64;
		break;
	case C0Basic_ptr:
		kind = C0Instr_load_ptr;
		break;
	}

	C0Instr *instr = c0_instr_create(p, kind);
	instr->basic_type = type;
	c0_alloc_args(p, instr, 1);
	instr->args[0] = c0_use(arg);
	return c0_instr_push(p, instr);
}


C0Instr *c0_push_addr_of_decl(C0Proc *p, C0Instr *decl) {
	C0_ASSERT(decl->kind == C0Instr_decl);
	C0Instr *instr = c0_instr_create(p, C0Instr_addr);
	c0_alloc_args(p, instr, 1);
	instr->args[0] = c0_use(decl);
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_index_ptr(C0Proc *p, C0AggType *array_type, C0Instr *array_ptr, C0Instr *index) {
	C0_ASSERT(array_type && array_type->kind == C0AggType_array);
	C0_ASSERT(array_ptr->basic_type == C0Basic_ptr);
	C0_ASSERT(c0_basic_type_is_integer(index->basic_type));

	C0Instr *instr = c0_instr_create(p, C0Instr_index_ptr);
	instr->agg_type = array_type;
	c0_alloc_args(p, instr, 2);
	instr->args[0] = c0_use(array_ptr);
	instr->args[1] = c0_use(index);
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_field_ptr(C0Proc *p, C0AggType *record_type, C0Instr *record_ptr, u32 field_index) {
	C0_ASSERT(record_type && record_type->kind == C0AggType_array);
	C0_ASSERT(record_ptr->basic_type == C0Basic_ptr);

	C0_ASSERT(field_index < c0array_len(record_type->record.types));

	C0Instr *instr = c0_instr_create(p, C0Instr_field_ptr);
	instr->agg_type = record_type;
	c0_alloc_args(p, instr, 1);
	instr->args[0] = c0_use(record_ptr);
	instr->value_u64 = (u64)field_index;
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src) {
	if (dst->kind == C0Instr_decl) {
		dst = c0_push_addr_of_decl(p, dst);
	}
	C0_ASSERT(dst->basic_type == C0Basic_ptr);

	C0InstrKind kind = C0Instr_store_u8;
	switch (src->basic_type) {
	case C0Basic_i8:
	case C0Basic_u8:
		kind = C0Instr_store_u8;
		break;
	case C0Basic_i16:
	case C0Basic_u16:
		kind = C0Instr_store_u16;
		break;
	case C0Basic_i32:
	case C0Basic_u32:
		kind = C0Instr_store_u32;
		break;
	case C0Basic_i64:
	case C0Basic_u64:
		kind = C0Instr_store_u64;
		break;
	case C0Basic_i128:
	case C0Basic_u128:
		kind = C0Instr_store_u128;
		break;
	case C0Basic_f16:
		kind = C0Instr_store_f16;
		break;
	case C0Basic_f32:
		kind = C0Instr_store_f32;
		break;
	case C0Basic_f64:
		kind = C0Instr_store_f64;
		break;
	case C0Basic_ptr:
		kind = C0Instr_store_ptr;
		break;
	}

	C0Instr *instr = c0_instr_create(p, kind);
	instr->basic_type = C0Basic_void;

	c0_alloc_args(p, instr, 2);
	instr->args[0] = c0_use(dst);
	instr->args[1] = c0_use(src);
	c0_use(instr);
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_copy_basic(C0Proc *p, C0Instr *arg) {
	C0_ASSERT(arg->basic_type != C0Basic_void);
	C0Instr *instr = c0_instr_create(p, C0Instr_decl);
	instr->basic_type = arg->basic_type;
	c0_instr_push(p, instr);
	c0_push_store_basic(p, instr, arg);
	return instr;
}


C0Instr *c0_push_unaligned_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src) {
	C0_ASSERT(src->basic_type != C0Basic_void);
	if (dst->kind == C0Instr_decl) {
		C0_ASSERT(dst->basic_type == src->basic_type);
		dst = c0_push_addr_of_decl(p, dst);
	}
	C0_ASSERT(dst->basic_type == C0Basic_ptr);
	i64 size = c0_basic_type_size(p->gen, src->basic_type);
	if (src->kind != C0Instr_decl) {
		src = c0_push_copy_basic(p, src);
	}
	src = c0_push_addr_of_decl(p, src);

	C0Instr *len = c0_push_basic_u32(p, (i32)size);
	return c0_push_memmove(p, dst, src, len);
}
C0Instr *c0_push_volatile_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src) {
	return c0_push_unaligned_store_basic(p, dst, src);
}
C0Instr *c0_push_unaligned_load_basic(C0Proc *p, C0BasicType type, C0Instr *ptr) {
	if (ptr->kind == C0Instr_decl) {
		C0_ASSERT(type == ptr->basic_type);
		ptr = c0_push_addr_of_decl(p, ptr);
	}
	C0_ASSERT(ptr->basic_type == C0Basic_ptr);

	C0Instr *val = c0_push_decl_basic(p, type, {0});
	C0Instr *val_ptr = c0_push_addr_of_decl(p, val);
	i64 size = c0_basic_type_size(p->gen, type);
	C0Instr *len = c0_push_basic_u32(p, (i32)size);
	c0_push_memmove(p, val_ptr, ptr, len);
	return val;
}
C0Instr *c0_push_volatile_load_basic(C0Proc *p, C0BasicType type, C0Instr *ptr) {
	return c0_push_unaligned_load_basic(p, type, ptr);
}



C0Instr *c0_push_atomic_thread_fence(C0Proc *p) {
	C0Instr *instr = c0_instr_create(p, C0Instr_atomic_thread_fence);
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_atomic_signal_fence(C0Proc *p) {
	C0Instr *instr = c0_instr_create(p, C0Instr_atomic_signal_fence);
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_atomic_load_basic(C0Proc *p, C0BasicType type, C0Instr *arg) {
	C0_ASSERT(type != C0Basic_void);
	C0_ASSERT(arg->basic_type == C0Basic_ptr);
	C0_ASSERT(arg->basic_type != C0Basic_void);

	C0Instr *instr = c0_instr_create(p, C0Instr_atomic_load_u8 + c0_basic_unsigned_instr_offset[type]);
	instr->basic_type = type;
	c0_alloc_args(p, instr, 1);
	instr->args[0] = c0_use(arg);
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_atomic_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src) {
	C0_ASSERT(dst->basic_type == C0Basic_ptr);
	C0_ASSERT(src->basic_type != C0Basic_void);

	C0Instr *instr = c0_instr_create(p, C0Instr_atomic_store_u8 +  + c0_basic_unsigned_instr_offset[src->basic_type]);
	c0_alloc_args(p, instr, 2);
	instr->args[0] = c0_use(dst);
	instr->args[1] = c0_use(src);
	return c0_instr_push(p, instr);
}


C0Instr *c0_push_atomic_cas(C0Proc *p, C0Instr *obj, C0Instr *expected, C0Instr *desired) {
	C0_ASSERT(obj->basic_type == C0Basic_ptr);
	C0_ASSERT(expected->basic_type == C0Basic_ptr);
	C0_ASSERT(desired->basic_type != C0Basic_void);

	C0Instr *instr = c0_instr_create(p, C0Instr_atomic_cas_u8 + c0_basic_unsigned_instr_offset[desired->basic_type]);
	c0_alloc_args(p, instr, 3);
	instr->args[0] = c0_use(obj);
	instr->args[1] = c0_use(expected);
	instr->args[2] = c0_use(desired);
	c0_use(instr);
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_atomic_bin(C0Proc *p, C0InstrKind kind, C0Instr *dst, C0Instr *src) {
	C0_ASSERT(dst->basic_type == C0Basic_ptr);
	C0_ASSERT(src->basic_type != C0Basic_void);

	C0Instr *instr = c0_instr_create(p, kind);
	c0_alloc_args(p, instr, 2);
	instr->args[0] = c0_use(dst);
	instr->args[1] = c0_use(src);
	c0_use(instr);
	return c0_instr_push(p, instr);
}


C0_PUSH_BIN_UINT_DEF(atomic_xchg);
C0_PUSH_BIN_UINT_DEF(atomic_add);
C0_PUSH_BIN_FLOAT_DEF(atomic_addf);
C0_PUSH_BIN_UINT_DEF(atomic_sub);
C0_PUSH_BIN_FLOAT_DEF(atomic_subf);
C0_PUSH_BIN_UINT_DEF(atomic_and);
C0_PUSH_BIN_UINT_DEF(atomic_or);
C0_PUSH_BIN_UINT_DEF(atomic_xor);

C0Instr *c0_push_memmove(C0Proc *p, C0Instr *dst, C0Instr *src, C0Instr *size) {
	C0_ASSERT(dst->basic_type == C0Basic_ptr);
	C0_ASSERT(src->basic_type == C0Basic_ptr);
	C0_ASSERT(c0_basic_type_is_integer(size->basic_type));

	C0Instr *instr = c0_instr_create(p, C0Instr_memmove);
	c0_alloc_args(p, instr, 3);
	instr->args[0] = c0_use(dst);
	instr->args[1] = c0_use(src);
	instr->args[2] = c0_use(size);
	c0_use(instr);
	return c0_instr_push(p, instr);


}
C0Instr *c0_push_memset(C0Proc *p, C0Instr *dst, u8 val, C0Instr *size) {
	C0_ASSERT(dst->basic_type == C0Basic_ptr);
	C0_ASSERT(c0_basic_type_is_integer(size->basic_type));

	C0Instr *instr = c0_instr_create(p, C0Instr_memset);
	c0_alloc_args(p, instr, 3);
	instr->args[0] = c0_use(dst);
	instr->args[1] = c0_use(c0_push_basic_u8(p, val));
	instr->args[2] = c0_use(size);
	c0_use(instr);
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_call_proc1(C0Proc *p, C0Proc *call_proc, C0Instr *arg0) {
	C0Instr *call = c0_instr_create(p, C0Instr_call);
	call->call_sig = call_proc->sig;
	call->basic_type = call_proc->sig->proc.ret->basic.type;

	call->call_proc = call_proc;
	c0_alloc_args(p, call, 1);
	call->args[0] = c0_use(arg0);

	return c0_instr_push(p, call);
}

C0Instr *c0_push_decl_basic_with_alignment(C0Proc *p, C0BasicType type, C0String name, u32 alignment) {
	C0_ASSERT((alignment & (alignment-1)) == 0);
	C0_ASSERT(type != C0Basic_void);
	C0Instr *instr = c0_instr_create(p, C0Instr_decl);
	instr->name = name;
	instr->basic_type = type;
	instr->alignment = alignment;
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_decl_basic(C0Proc *p, C0BasicType type, C0String name) {
	C0_ASSERT(type != C0Basic_void);
	C0Instr *instr = c0_instr_create(p, C0Instr_decl);
	instr->name = name;
	instr->basic_type = type;
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_decl_agg_with_alignment(C0Proc *p, C0AggType *type, C0String name, u32 alignment) {
	C0_ASSERT((alignment & (alignment-1)) == 0);
	C0_ASSERT(type);
	if (type->kind == C0AggType_basic) {
		return c0_push_decl_basic_with_alignment(p, type->basic.type, name, alignment);
	}
	C0Instr *instr = c0_instr_create(p, C0Instr_decl);
	instr->name = name;
	instr->agg_type = type;
	instr->alignment = alignment;
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_decl_agg(C0Proc *p, C0AggType *type, C0String name) {
	C0_ASSERT(type);
	if (type->kind == C0AggType_basic) {
		return c0_push_decl_basic(p, type->basic.type, name);
	}
	C0Instr *instr = c0_instr_create(p, C0Instr_decl);
	instr->name = name;
	instr->agg_type = type;
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_select_basic(C0Proc *p, C0Instr *cond, C0Instr *true_case, C0Instr *false_case) {
	C0_ASSERT(c0_basic_type_is_integer(cond->basic_type));
	if (c0_basic_type_sizes[cond->basic_type] == 16) {
		cond = c0_push_to_bool(p, cond);
	}

	C0_ASSERT(true_case->basic_type == false_case->basic_type);
	C0_ASSERT(true_case->basic_type != C0Basic_void);

	C0Instr *instr = c0_instr_create(p, C0Instr_select_u8 + c0_basic_unsigned_instr_offset[true_case->basic_type]);
	c0_alloc_args(p, instr, 3);
	instr->args[0] = c0_use(cond);
	instr->args[1] = c0_use(true_case);
	instr->args[2] = c0_use(false_case);
	return c0_instr_push(p, instr);
}

static bool c0_is_within_a_loop(C0Proc *p) {
	usize n = c0array_len(p->nested_blocks);
	for (usize i = n-1; i < n; i--) {
		if (p->nested_blocks[i]->kind == C0Instr_loop) {
			return true;
		}
	}
	return false;
}

C0Instr *c0_push_continue(C0Proc *p) {
	C0_ASSERT(c0_is_within_a_loop(p));
	C0Instr *instr = c0_instr_create(p, C0Instr_continue);
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_break(C0Proc *p) {
	C0_ASSERT(c0_is_within_a_loop(p));
	C0Instr *instr = c0_instr_create(p, C0Instr_break);
	return c0_instr_push(p, instr);
}

C0Instr *c0_push_goto(C0Proc *p, C0Instr *label) {
	C0_ASSERT(label->kind == C0Instr_label);
	C0Instr *instr = c0_instr_create(p, C0Instr_goto);
	c0_alloc_args(p, instr, 1);
	instr->args[0] = c0_use(label);
	return c0_instr_push(p, instr);
}
C0Instr *c0_push_label(C0Proc *p, C0String name) {
	// TODO(bill): make name unique if it isn't
	C0Instr *instr = c0_instr_create(p, C0Instr_label);
	instr->name = c0_arena_str_dup(p->arena, name);
	usize n = c0array_len(p->labels);
	for (usize i = 0; i < n; i++) {
		if (!c0_strings_equal(p->labels[i]->name, name)) {
			c0_errorf("non-unique label names: %.*s", C0PSTR(name));
		}
	}
	c0array_push(p->labels, instr);
	return c0_instr_push(p, instr);
}


////////////////////////////
// block
////////////////////////////



C0Instr *c0_push_if(C0Proc *p, C0Instr *cond) {
	C0Instr *block = c0_instr_create(p, C0Instr_if);
	c0_alloc_args(p, block, 2); // one for possible else
	block->args_len = 1;
	block->args[0] = c0_use(cond);
	C0_ASSERT(c0_basic_type_is_integer(cond->basic_type));

	c0_use(block);

	c0_instr_push(p, block);
	c0_push_nested_block(p, block);
	return block;
}

C0Instr *c0_pop_if(C0Proc *p) {
	C0Instr *block = c0_pop_nested_block(p);
	C0_ASSERT(block->kind == C0Instr_if);
	return block;
}


C0Instr *c0_block_create(C0Proc *p) {
	C0Instr *block = c0_instr_create(p, C0Instr_block);
	return block;
}

C0Instr *c0_block_start(C0Proc *p, C0Instr *block) {
	C0_ASSERT(block);
	C0_ASSERT(block->kind == C0Instr_block);
	c0_use(block);
	c0_push_nested_block(p, block);
	return block;
}
C0Instr *c0_block_push_and_start(C0Proc *p, C0Instr *block) {
	C0_ASSERT(block->kind == C0Instr_block);
	c0_use(block);
	c0_push_nested_block(p, block);
	return c0_instr_push(p, block);
}


C0Instr *c0_pop_block(C0Proc *p) {
	C0Instr *block = c0_pop_nested_block(p);
	C0_ASSERT(block->kind == C0Instr_block);
	return block;
}

C0Instr *c0_push_loop(C0Proc *p) {
	C0Instr *block = c0_instr_create(p, C0Instr_loop);
	c0_use(block);
	c0_instr_push(p, block);
	c0_push_nested_block(p, block);
	return block;
}

C0Instr *c0_pop_loop(C0Proc *p) {
	C0Instr *block = c0_pop_nested_block(p);
	C0_ASSERT(block->kind == C0Instr_loop);
	return block;
}




void c0_push_else_to_if(C0Proc *p, C0Instr *if_stmt, C0Instr *else_stmt) {
	C0_ASSERT(else_stmt);
	C0_ASSERT(if_stmt->kind == C0Instr_if);
	if_stmt->args_len = 2; // this is already preallocated for an else
	if_stmt->args[1] = c0_use(else_stmt);
	return;
}

void c0_block_start_else(C0Proc *p, C0Instr *if_stmt, C0Instr *else_stmt) {
	c0_block_start(p, else_stmt);
	c0_push_else_to_if(p, if_stmt, else_stmt);
}
void c0_block_else_block(C0Proc *p, C0Instr *if_stmt) {
	C0Instr *else_stmt = c0_block_create(p);
	c0_block_start(p, else_stmt);
	c0_push_else_to_if(p, if_stmt, else_stmt);
}


void c0_pass_remove_unused_instructions(C0Array(C0Instr *) *array) {
	if (!array || !*array) {
		return;
	}
	usize len = c0array_len((*array));
	for (isize i = len-1; i >= 0; i--) {
		C0Instr *instr = (*array)[i];
		if (instr->nested_instrs) {
			c0_pass_remove_unused_instructions(&instr->nested_instrs);
		}
		if (instr->basic_type != C0Basic_void) {
			if (instr->kind == C0Instr_call) {
				continue;
			}
			if (instr->uses == 0) {
				for (isize j = 0; j < instr->args_len; j++) {
					c0_unuse(instr->args[j]);
				}
				c0array_ordered_remove((*array), i);
				continue;
			}
		} else if (instr->kind == C0Instr_if) {
			if (c0array_len(instr->nested_instrs) == 0) {
				if (instr->args_len == 1) {
					c0array_ordered_remove((*array), i);
					continue;
				}
			}
		}
	}
}

void c0_assign_reg_id(C0Instr *instr, u32 *reg_id_) {
	i32 arg_count = c0_instr_arg_count[instr->kind];
	if (arg_count >= 0) {
		// validate instruction args_len
		C0_ASSERT(instr->args_len == arg_count);
	} else {
		switch (instr->kind) {
		case C0Instr_return:
			C0_ASSERT(instr->args_len == 0 || instr->args_len == 1);
			break;
		case C0Instr_if:
			C0_ASSERT(instr->args_len == 1 || instr->args_len == 2);
			break;
		case C0Instr_call:
			C0_ASSERT(instr->call_sig);
			C0_ASSERT(instr->call_sig->kind == C0AggType_proc);
			C0_ASSERT(instr->args_len == c0array_len(instr->call_sig->proc.types));
			break;
		}
	}
	if (instr->basic_type != C0Basic_void || instr->agg_type != NULL) {
		instr->id = (*reg_id_)++;
	}

	if (instr->nested_instrs) {
		for (isize i = 0; i < c0array_len(instr->nested_instrs); i++) {
			c0_assign_reg_id(instr->nested_instrs[i], reg_id_);
		}
	}
	if (instr->kind == C0Instr_if && instr->args_len == 2) {
		c0_assign_reg_id(instr->args[1], reg_id_);
	}
}

void c0_register_instr_to_gen(C0Gen *gen, C0Instr *instr) {
	if (!instr) {
		return;
	}

	gen->instrs_to_generate[instr->kind] = true;
	switch (instr->kind) {
	case C0Instr_convert:
		gen->convert_to_generate[instr->args[0]->basic_type][instr->basic_type] = true;
		break;
	case C0Instr_reinterpret:
		gen->reinterpret_to_generate[instr->args[0]->basic_type][instr->basic_type] = true;
		break;
	}
	if (instr->nested_instrs) {
		for (isize i = 0; i < c0array_len(instr->nested_instrs); i++) {
			c0_register_instr_to_gen(gen, instr->nested_instrs[i]);
		}
	}
	if (instr->kind == C0Instr_if && instr->args_len == 2) {
		c0_register_instr_to_gen(gen, instr->args[1]);
	}
}


C0Proc *c0_proc_finish(C0Proc *p) {
	C0_ASSERT(p->gen);
	C0_ASSERT(c0array_len(p->nested_blocks) == 0);

	c0_pass_remove_unused_instructions(&p->instrs);

	C0Instr *last = c0_instr_last(p);
	if (c0_is_instruction_terminating(last)) {
		if (last->kind == C0Instr_if || last->kind == C0Instr_loop) {
			c0array_push(p->instrs, c0_instr_create(p, C0Instr_unreachable));
		}
	} else if (!c0_types_agg_basic(p->sig->proc.ret, C0Basic_void)) {
		c0_errorf("procedure missing return statement, expected ??");
	}

	u32 reg_id = 0;

	for (isize i = 0; i < c0array_len(p->instrs); i++) {
		C0Instr *instr = p->instrs[i];
		c0_assign_reg_id(instr, &reg_id);
		c0_register_instr_to_gen(p->gen, instr);
	}
	return p;
}


#include "c0_print.c"
