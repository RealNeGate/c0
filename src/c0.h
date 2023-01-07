#ifndef C0_HEADER_DEFINE
#define C0_HEADER_DEFINE

#if !defined(__cplusplus)
#include <stdalign.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t      u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef size_t    usize;

typedef int8_t       i8;
typedef int16_t     i16;
typedef int32_t     i32;
typedef int64_t     i64;
typedef ptrdiff_t isize;

typedef float       f32;
typedef double      f64;

typedef struct C0String      C0String;
typedef struct C0Array       C0Array;
typedef struct C0Arena       C0Arena;
typedef struct C0MemoryBlock C0MemoryBlock;

struct C0String {
	char const *text;
	isize       len;
};

// C0String s = C0STR("Hellope");
#ifndef C0STR
#if defined(__cplusplus)
	#define C0STR(lit) (C0String{(lit), sizeof(lit)-1})
#else
	#define C0STR(lit) ((C0String){(lit), sizeof(lit)-1})
#endif
#endif

#ifndef C0_ASSERT
#include <assert.h>
#define C0_ASSERT(cond) assert(cond)
#endif

#ifndef C0_PANIC
#define C0_PANIC(msg) C0_ASSERT(0 && msg)
#endif


// printf("%.*s", C0PSTR(s));
#ifndef C0PSTR
#define C0PSTR(s) (int)(s).len, (s).text
#endif

struct C0Array {
	alignas(2*sizeof(usize))
	usize len;
	usize cap;
};

#define C0Array(T) T *

#define c0array_meta(array) \
	(&((C0Array*)(array))[-1])


#define c0array_len(array) \
	((array) ? c0array_meta(array)->len : 0)

#define c0array_cap(array) \
	((array) ? c0array_meta(array)->cap : 0)

#define c0array_try_grow(array, size_) \
	(((array) && c0array_meta(array)->len + (size_) < c0array_meta(array)->cap) \
		? true \
		: c0array_grow_internal((void **)&(array), (size_), sizeof(*(array))))

#define c0array_expand(array, size_) \
	(c0array_try_grow((array), (size_)) \
		? (c0array_meta(array)->len += (size_), true) \
		: false)

#define c0array_push(array, value) \
	(c0array_try_grow((array), 1) \
		? ((array)[c0array_meta(array)->len++] = (value), true) \
		: false)

#define c0array_pop(array) \
	(c0array_len(array) > 0 \
		? (--c0array_meta(array)->len) \
		: 0)

#define c0array_free(array) \
	(void)((array) ? (c0array_delete(array), (array) = 0) : 0)

#define c0array_insert(array, index, value) \
	(c0array_expand(array, 1) \
		? (memmove(&(array)[index+1], &(array)[index], (c0array_len(array) - (index) - 1) * sizeof(*(array))), (array)[index] = (value), true) \
		: false)

#define c0array_ordered_remove(array, index) do { \
	assert((usize)index < c0array_len(array)); \
	memmove(&(array)[index], &(array)[index+1], (c0array_len(array) - (index)-1) * sizeof(*(array))); \
	c0array_meta(array)->len -= 1; \
} while (0)

#define c0array_resize(array, size_) \
	((array) \
		? (c0array_meta(array)->len >= (size_) \
			? (c0array_meta(array)->len = (size_), true) \
			: c0array_expand((array), (size_) - c0array_meta(array)->len)) \
		: (c0array_grow_internal((void **)&(array), (size_), sizeof(*(array))) \
			? (c0array_meta(array)->len = (size_), true) \
			: false))

#define c0array_last(array) \
	((array)[c0array_len(array) - 1])

#define c0array_clear(array) \
	(void)((array) ? c0array_meta(array)->len = 0 : 0)

bool c0array_grow_internal(void **const array, usize elements, usize type_size);
void c0array_delete(void *const array);

struct C0MemoryBlock {
	C0MemoryBlock *prev;
	u8 *           base;
	usize          size;
	usize          used;
};

struct C0Arena {
	C0MemoryBlock *curr_block;
	usize minimum_block_size;
	// TODO(bill): use an arena here
};


void *c0_arena_alloc   (C0Arena *arena, usize min_size, usize alignment);
void  c0_arena_free_all(C0Arena *arena);

#ifndef c0_arena_new
#define c0_arena_new(arena, T) (T *)c0_arena_alloc((arena), sizeof(T), alignof(T))
#endif

#ifndef c0_arena_new
#define c0_arena_alloc_array(arena, T, len) (T *)c0_arena_alloc((arena), sizeof(T)*(len), alignof(T))
#endif

C0String    c0_arena_str_dup (C0Arena *arena, C0String str);
char const *c0_arena_cstr_dup(C0Arena *arena, char const *str);


///////


typedef struct C0Gen     C0Gen;
typedef struct C0Instr   C0Instr;
typedef struct C0Proc    C0Proc;
typedef struct C0AggType C0AggType;
typedef struct C0Loc     C0Loc;

typedef u8 C0BasicType;
enum C0BasicType_enum {
	C0Basic_void,
	C0Basic_i8,
	C0Basic_u8,
	C0Basic_i16,
	C0Basic_u16,
	C0Basic_i32,
	C0Basic_u32,
	C0Basic_i64,
	C0Basic_u64,
	C0Basic_i128,
	C0Basic_u128,
	C0Basic_f16,
	C0Basic_f32,
	C0Basic_f64,
	C0Basic_ptr,

	C0Basic_COUNT
};

static i32 const c0_basic_type_sizes[C0Basic_COUNT] = {
	0  /* void */,
	1  /* i8   */,
	1  /* u8   */,
	2  /* i16  */,
	2  /* u16  */,
	4  /* i32  */,
	4  /* u32  */,
	8  /* i64  */,
	8  /* u64  */,
	16 /* i128 */,
	16 /* u128 */,
	2  /* f16  */,
	4  /* f32  */,
	8  /* f64  */,
	-1 /* ptr  */, // -1 denotes that the size is `-(-1) * pointer_size`
};

static char const *const c0_basic_names[C0Basic_COUNT] = {
	"void",
	"i8",
	"u8",
	"i16",
	"u16",
	"i32",
	"u32",
	"i64",
	"u64",
	"i128",
	"u128",
	"f16",
	"f32",
	"f64",
	"void *",
};


typedef u16 C0InstrKind;
enum C0InstrKind_enum {
	C0Instr_invalid,

	// ptr
	C0Instr_load,
	C0Instr_store,

	// unary
	C0Instr_negf,

	// binary
	C0Instr_add,
	C0Instr_sub,
	C0Instr_mul,
	C0Instr_quoi,
	C0Instr_quou,
	C0Instr_remi,
	C0Instr_remu,
	C0Instr_shli,
	C0Instr_shlu,
	C0Instr_shri,
	C0Instr_shru,

	C0Instr_and,
	C0Instr_or,
	C0Instr_xor,
	C0Instr_eq,
	C0Instr_neq,
	C0Instr_lti,
	C0Instr_ltu,
	C0Instr_gti,
	C0Instr_gtu,
	C0Instr_lteqi,
	C0Instr_ltequ,
	C0Instr_gteqi,
	C0Instr_gtequ,

	C0Instr_addf,
	C0Instr_subf,
	C0Instr_mulf,
	C0Instr_divf,
	C0Instr_eqf,
	C0Instr_neqf,
	C0Instr_ltf,
	C0Instr_gtf,
	C0Instr_lteqf,
	C0Instr_gteqf,

	// conversion
	C0Instr_cvt,

	// atomic
	C0Instr_atomic_thread_fence,
	C0Instr_atomic_signal_fence,

	C0Instr_atomic_load,
	C0Instr_atomic_store,
	C0Instr_atomic_xchg,
	C0Instr_atomic_cas,
	C0Instr_atomic_add,
	C0Instr_atomic_sub,
	C0Instr_atomic_and,
	C0Instr_atomic_or,
	C0Instr_atomic_xor,

	// memory
	C0Instr_memmove,
	C0Instr_memset,

	// declarations
	C0Instr_decl,
	C0Instr_addr,

	// call
	C0Instr_call,

	// ternary
	C0Instr_select,

	// blocks
	C0Instr_if,
	C0Instr_loop,
	C0Instr_block,

	// branch
	C0Instr_continue,
	C0Instr_break,
	C0Instr_return,
	C0Instr_unreachable,
	C0Instr_goto,
	C0Instr_label,

	C0Instr_COUNT
};

static char const *const c0_instr_names[C0Instr_COUNT] = {
	"invalid",

	"load",
	"store",

	"negf",

	"add",
	"sub",
	"mul",
	"quoi",
	"quou",
	"remi",
	"remu",
	"shli",
	"shlu",
	"shri",
	"shru",

	"and",
	"or",
	"xor",
	"eq",
	"neq",
	"lti",
	"ltu",
	"gti",
	"gtu",
	"lteqi",
	"ltequ",
	"gteqi",
	"gtequ",

	"addf",
	"subf",
	"mulf",
	"divf",
	"eqf",
	"neqf",
	"ltf",
	"gtf",
	"lteqf",
	"gteqf",

	"cvt",

	"atomic_thread_fence",
	"atomic_signal_fence",

	"atomic_load",
	"atomic_store",
	"atomic_xchg",
	"atomic_cas",
	"atomic_add",
	"atomic_sub",
	"atomic_and",
	"atomic_or",
	"atomic_xor",

	"memmove",
	"memset",

	"decl",
	"addr",

	"call",

	"select",

	"if",
	"loop",
	"block",

	"continue",
	"break",
	"return",
	"unreachable",
	"goto",
	"label",
};

struct C0Gen {
	C0String name;
	C0Arena  arena;

	i64 ptr_size;

	C0Array(C0String)    files;
	C0Array(C0AggType *) types;
	C0Array(C0Proc *)    procs;
};

struct C0Loc {
	u32 file;
	i32 line;
	i32 column;
};

struct C0Instr {
	C0InstrKind kind;
	C0BasicType basic_type;
	u16         padding0;
	u32         uses;
	C0Instr *   parent;

	C0AggType *agg_type;

	u32      id;
	C0String name;
	C0Proc  *call_proc;

	C0Instr **args;
	usize     args_len;

	C0Array(C0Instr *) nested_instrs;

	union {
		i64 value_i64;
		u64 value_u64;

		u16 value_f16;
		f32 value_f32;
		f64 value_f64;
	};
};

struct C0Proc {
	C0Arena *  arena;
	C0Gen *    gen;
	C0String   name;
	C0AggType *sig;

	C0Array(C0Instr *) instrs;
	C0Array(C0Instr *) nested_blocks;
	C0Array(C0Instr *) labels;
};

typedef u32 C0AggTypeKind;
enum C0AggTypeKind_enum {
	C0AggType_basic,
	C0AggType_array,
	C0AggType_record,
	C0AggType_proc,

	C0AggType_COUNT
};

typedef u16 C0ProcCallConv;
enum C0ProcCallConv_enum {
	C0ProcCallConv_cdecl,
	C0ProcCallConv_stdcall,
	C0ProcCallConv_fastcall,
};
typedef u16 C0ProcFlags;
enum C0ProcFlags_enum {
	C0ProcFlag_diverging     = 1<<0,
	C0ProcFlag_variadic      = 1<<1,
	C0ProcFlag_always_inline = 1<<2, // should not be needed in the future
	C0ProcFlag_never_inline  = 1<<3,
};

struct C0AggType {
	C0AggTypeKind kind;
	u32        padding0;
	i64        size;
	i64        align;

	union {
		struct {
			C0BasicType type;
		} basic;
		struct {
			C0AggType *elem;
			i64        len;
		} array;
		struct {
			C0String name;
			C0Array(C0String)    names;
			C0Array(C0AggType *) types;
			C0Array(i64)         aligns;
		} record;
		struct {
			C0AggType *          ret;
			C0Array(C0String)    names;
			C0Array(C0AggType *) types;

			C0ProcCallConv call_conv;
			C0ProcFlags    flags;
		} proc;
	};
};

void c0_platform_virtual_memory_init(void);

void c0_gen_init(C0Gen *gen);
void c0_gen_destroy(C0Gen *gen);

C0Proc * c0_proc_create (C0Gen *gen, C0String name);
C0Instr *c0_instr_create(C0Proc *p,  C0InstrKind kind);
C0Instr *c0_instr_push  (C0Proc *p,  C0Instr *instr);


C0Instr *c0_push_nested_block(C0Proc *p, C0Instr *block);
C0Instr *c0_push_basic_i8(C0Proc *p, i8 value);
C0Instr *c0_push_basic_u8(C0Proc *p, u8 value);
C0Instr *c0_push_basic_i16(C0Proc *p, i16 value);
C0Instr *c0_push_basic_u16(C0Proc *p, u16 value);
C0Instr *c0_push_basic_i32(C0Proc *p, i32 value);
C0Instr *c0_push_basic_u32(C0Proc *p, u32 value);
C0Instr *c0_push_basic_i64(C0Proc *p, i64 value);
C0Instr *c0_push_basic_u64(C0Proc *p, u64 value);
C0Instr *c0_push_basic_ptr(C0Proc *p, u64 value);
C0Instr *c0_push_bin(C0Proc *p, C0InstrKind kind, C0Instr *left, C0Instr *right);

C0Instr *c0_push_negf(C0Proc *p, C0Instr *arg);
C0Instr *c0_push_noti(C0Proc *p, C0Instr *arg);
C0Instr *c0_push_notb(C0Proc *p, C0Instr *arg);
C0Instr *c0_push_unreachable(C0Proc *p);
C0Instr *c0_push_return(C0Proc *p, C0Instr *arg);
C0Instr *c0_push_convert(C0Proc *p, C0BasicType type, C0Instr *arg);
C0Instr *c0_push_load_basic(C0Proc *p, C0BasicType type, C0Instr *arg);
C0Instr *c0_push_addr_of_decl(C0Proc *p, C0Instr *decl);
C0Instr *c0_push_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src);
C0Instr *c0_push_copy_basic(C0Proc *p, C0Instr *arg);
C0Instr *c0_push_unaligned_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src);
C0Instr *c0_push_atomic_thread_fence(C0Proc *p);
C0Instr *c0_push_atomic_signal_fence(C0Proc *p);
C0Instr *c0_push_atomic_load_basic(C0Proc *p, C0BasicType type, C0Instr *arg);
C0Instr *c0_push_atomic_store_basic(C0Proc *p, C0Instr *dst, C0Instr *src);
C0Instr *c0_push_atomic_cas(C0Proc *p, C0Instr *obj, C0Instr *expected, C0Instr *desired);
C0Instr *c0_push_atomic_bin(C0Proc *p, C0InstrKind kind, C0Instr *dst, C0Instr *src);

C0Instr *c0_push_memmove(C0Proc *p, C0Instr *dst, C0Instr *src, C0Instr *size);
C0Instr *c0_push_memset(C0Proc *p, C0Instr *dst, u8 val, C0Instr *size);
C0Instr *c0_push_decl_basic(C0Proc *p, C0BasicType type, C0String name);
C0Instr *c0_push_select_basic(C0Proc *p, C0Instr *cond, C0Instr *true_case, C0Instr *false_case);
C0Instr *c0_push_continue(C0Proc *p);
C0Instr *c0_push_break(C0Proc *p);
C0Instr *c0_push_goto(C0Proc *p, C0Instr *label);
C0Instr *c0_push_label(C0Proc *p, C0String name);
C0Instr *c0_push_if(C0Proc *p, C0Instr *cond);
C0Instr *c0_push_loop(C0Proc *p);

#endif /*C0_HEADER_DEFINE*/