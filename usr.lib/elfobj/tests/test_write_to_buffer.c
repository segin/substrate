#include <elfobj.h>
#include "elf_private.h"

#include <elf.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PF_X
#define PF_X 0x1
#endif
#ifndef PF_W
#define PF_W 0x2
#endif
#ifndef PF_R
#define PF_R 0x4
#endif

typedef struct {
	elfobj_t *obj;
	uint8_t *buf;
	size_t sz;
} rt_obj_t;

static void failf(const char *test, const char *fmt, ...) {
	va_list ap;

	fprintf(stderr, "FAIL [%s]: ", test);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static void require_ok(const char *test, const char *what, elf_err_t err) {
	if(err != ELF_OK)
		failf(test, "%s failed (err=%d)", what, (int)err);
}

static void require_true(const char *test, int cond, const char *what) {
	if(!cond)
		failf(test, "%s", what);
}

static elf_section_t *require_section(const char *test, elfobj_t *obj, const char *name) {
	elf_section_t *s = elf_find_section(obj, name);
	if(s == NULL)
		failf(test, "missing section %s", name);
	return(s);
}

static elf_symbol_t *require_symbol(const char *test, elfobj_t *obj, const char *name) {
	elf_symbol_t *sym = elf_find_symbol(obj, name);
	if(sym == NULL)
		failf(test, "missing symbol %s", name);
	return(sym);
}

static int has_phdr_type(elfobj_t *obj, uint32_t type) {
	uint16_t i;
	uint16_t n = elf_program_header_count(obj);

	for(i = 0; i < n; ++i)
		if(elf_program_header_type(obj, i) == type)
			return(1);
	return(0);
}

static rt_obj_t roundtrip_validate(const char *test, elfobj_t *obj, elf_validate_mode_t mode) {
	rt_obj_t rt;
	char *diag = NULL;
	elf_err_t err;

	memset(&rt, 0, sizeof(rt));
	err = elf__write_to_buffer(obj, &rt.buf, &rt.sz);
	require_ok(test, "elf__write_to_buffer", err);
	require_true(test, rt.buf != NULL && rt.sz > 0, "writer produced empty buffer");

	err = elf_open_memory(rt.buf, rt.sz, &rt.obj);
	require_ok(test, "elf_open_memory", err);

	if(mode == ELF_VALIDATE_PERMISSIVE)
		require_ok(test, "elf_set_validation_mode", elf_set_validation_mode(rt.obj, mode));

	err = elf_validate(rt.obj, &diag);
	if(err != ELF_OK)
		failf(test, "elf_validate failed: %s (err=%d)", diag != NULL ? diag : "unknown", (int)err);
	free(diag);
	return(rt);
}

static void rt_close(rt_obj_t *rt) {
	if(rt->obj != NULL)
		elf_close(rt->obj);
	free(rt->buf);
	rt->obj = NULL;
	rt->buf = NULL;
	rt->sz = 0;
}

static void test_minimal(void) {
	const char *t = "minimal";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;

	require_true(t, obj != NULL, "elf_create failed");
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	require_true(t, elf_type(rt.obj) == ET_REL, "type mismatch");
	require_true(t, elf_machine(rt.obj) == EM_386, "machine mismatch");
	require_true(t, elf_class(rt.obj) == ELFOBJ_CLASS_32, "class mismatch");
	require_true(t, elf_endian(rt.obj) == ELFOBJ_ENDIAN_LE, "endian mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_many_sections(void) {
	const char *t = "many_sections";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	int i;

	require_true(t, obj != NULL, "elf_create failed");
	for(i = 0; i < 100; ++i) {
		char n[32];
		uint8_t b = (uint8_t)i;
		elf_section_t *s;
		snprintf(n, sizeof(n), ".sec%d", i);
		s = elf_add_section(obj, n, SHT_PROGBITS, 0);
		require_true(t, s != NULL, "elf_add_section failed");
		require_ok(t, "elf_section_set_data", elf_section_set_data(s, &b, 1));
	}
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	for(i = 0; i < 100; ++i) {
		char n[32];
		snprintf(n, sizeof(n), ".sec%d", i);
		(void)require_section(t, rt.obj, n);
	}
	rt_close(&rt);
	elf_close(obj);
}

static void test_many_symbols(void) {
	const char *t = "many_symbols";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	int i;

	require_true(t, obj != NULL, "elf_create failed");
	for(i = 0; i < 100; ++i) {
		char n[32];
		elf_symbol_t *sym;
		snprintf(n, sizeof(n), "sym%d", i);
		sym = elf_add_symbol(obj, n, 0, 0, STB_GLOBAL, STT_OBJECT);
		require_true(t, sym != NULL, "elf_add_symbol failed");
	}
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	require_true(t, elf_symbol_count(rt.obj) >= 100, "symbol count too small");
	for(i = 0; i < 100; ++i) {
		char n[32];
		snprintf(n, sizeof(n), "sym%d", i);
		(void)require_symbol(t, rt.obj, n);
	}
	rt_close(&rt);
	elf_close(obj);
}

static void test_nobits(void) {
	const char *t = "nobits";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;

	require_true(t, obj != NULL, "elf_create failed");
	s = elf_add_section(obj, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
	require_true(t, s != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, NULL, 1024));

	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	s = require_section(t, rt.obj, ".bss");
	require_true(t, elf_section_type(s) == SHT_NOBITS, "section type mismatch");
	require_true(t, elf_section_size(s) == 1024, "section size mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_large_align(void) {
	const char *t = "large_align";
	elfobj_t *obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;
	uint8_t b = 0;

	require_true(t, obj != NULL, "elf_create failed");
	s = elf_add_section(obj, ".aligned", SHT_PROGBITS, 0);
	require_true(t, s != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_align", elf_section_set_align(s, 4096));
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, &b, 1));

	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	s = require_section(t, rt.obj, ".aligned");
	require_true(t, elf_section_align(s) == 4096, "alignment mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_reloc_rel32(void) {
	const char *t = "reloc_rel32";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *text;
	elf_symbol_t *sym;
	uint8_t code[8] = {0};
	size_t i;

	require_true(t, obj != NULL, "elf_create failed");
	text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
	require_true(t, text != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(text, code, sizeof(code)));
	sym = elf_add_symbol(obj, "s32", 0, 0, STB_GLOBAL, STT_NOTYPE);
	require_true(t, sym != NULL, "elf_add_symbol failed");
	require_ok(t, "elf_add_relocation#0", elf_add_relocation(text, 0, sym, R_386_32, 0));
	require_ok(t, "elf_add_relocation#1", elf_add_relocation(text, 4, sym, R_386_PC32, 4));

	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	text = require_section(t, rt.obj, ".text");
	require_true(t, elf_section_reloc_count(text) == 2, "unexpected reloc count");
	for(i = 0; i < 2; ++i) {
		elf_reloc_t *r = elf_section_reloc_at(text, i);
		require_true(t, r != NULL, "missing relocation entry");
		require_true(t, elf_reloc_has_addend(r) == 0, "32-bit REL relocation unexpectedly has addend");
	}
	rt_close(&rt);
	elf_close(obj);
}

static void test_reloc_rela64(void) {
	const char *t = "reloc_rela64";
	elfobj_t *obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *text;
	elf_symbol_t *sym;
	uint8_t code[16] = {0};
	size_t i;

	require_true(t, obj != NULL, "elf_create failed");
	text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
	require_true(t, text != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(text, code, sizeof(code)));
	sym = elf_add_symbol(obj, "s64", 0, 0, STB_GLOBAL, STT_NOTYPE);
	require_true(t, sym != NULL, "elf_add_symbol failed");
	require_ok(t, "elf_add_relocation#0", elf_add_relocation(text, 0, sym, R_X86_64_64, 0));
	require_ok(t, "elf_add_relocation#1", elf_add_relocation(text, 8, sym, R_X86_64_PC32, -4));

	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	text = require_section(t, rt.obj, ".text");
	require_true(t, elf_section_reloc_count(text) == 2, "unexpected reloc count");
	for(i = 0; i < 2; ++i) {
		elf_reloc_t *r = elf_section_reloc_at(text, i);
		require_true(t, r != NULL, "missing relocation entry");
		require_true(t, elf_reloc_has_addend(r) != 0, "64-bit RELA relocation missing addend");
	}
	rt_close(&rt);
	elf_close(obj);
}

static void test_unnamed(void) {
	const char *t = "unnamed";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;
	elf_symbol_t *sym;
	uint8_t b = 0;

	require_true(t, obj != NULL, "elf_create failed");
	require_true(t, elf_add_section(obj, NULL, SHT_PROGBITS, 0) == NULL,
	             "unnamed section unexpectedly accepted");
	require_true(t, elf_add_symbol(obj, NULL, 0, 0, STB_LOCAL, STT_NOTYPE) == NULL,
	             "unnamed symbol unexpectedly accepted");
	s = elf_add_section(obj, ".ok", SHT_PROGBITS, 0);
	require_true(t, s != NULL, "fallback section add failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, &b, 1));
	sym = elf_add_symbol(obj, "ok", 0, 0, STB_LOCAL, STT_NOTYPE);
	require_true(t, sym != NULL, "fallback symbol add failed");
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	(void)require_section(t, rt.obj, ".ok");
	(void)require_symbol(t, rt.obj, "ok");
	rt_close(&rt);
	elf_close(obj);
}

static elfobj_t *make_exec_dyn_obj(const char *test, uint16_t etype) {
	elfobj_t *obj = elf_create(etype, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
	elf_section_t *text;
	elf_segment_t *seg;
	uint8_t code[1] = {0x90};

	require_true(test, obj != NULL, "elf_create failed");
	text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
	require_true(test, text != NULL, "elf_add_section failed");
	require_ok(test, "elf_section_set_align", elf_section_set_align(text, 16));
	require_ok(test, "elf_section_set_addr", elf_section_set_addr(text, 0x401000));
	require_ok(test, "elf_section_set_data", elf_section_set_data(text, code, sizeof(code)));
	seg = elf_add_load_segment(obj, PF_R | PF_X, 0x1000);
	require_true(test, seg != NULL, "elf_add_load_segment failed");
	require_ok(test, "elf_segment_add_section", elf_segment_add_section(seg, text));
	require_ok(test, "elf_set_entry", elf_set_entry(obj, 0x400000));
	return(obj);
}

static void test_et_exec(void) {
	const char *t = "et_exec";
	elfobj_t *obj = make_exec_dyn_obj(t, ET_EXEC);
	rt_obj_t rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, elf_type(rt.obj) == ET_EXEC, "type mismatch");
	require_true(t, has_phdr_type(rt.obj, PT_LOAD), "missing PT_LOAD");
	rt_close(&rt);
	elf_close(obj);
}

static void test_et_dyn(void) {
	const char *t = "et_dyn";
	elfobj_t *obj = make_exec_dyn_obj(t, ET_DYN);
	rt_obj_t rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, elf_type(rt.obj) == ET_DYN, "type mismatch");
	require_true(t, has_phdr_type(rt.obj, PT_LOAD), "missing PT_LOAD");
	rt_close(&rt);
	elf_close(obj);
}

static void test_segments(void) {
	const char *t = "segments";
	elfobj_t *obj = make_exec_dyn_obj(t, ET_EXEC);
	rt_obj_t rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, elf_program_header_count(rt.obj) >= 1, "missing program headers");
	require_true(t, has_phdr_type(rt.obj, PT_LOAD), "missing PT_LOAD");
	rt_close(&rt);
	elf_close(obj);
}

static void test_empty_segment(void) {
	const char *t = "empty_segment";
	elfobj_t *obj = elf_create(ET_EXEC, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_segment_t *seg;

	require_true(t, obj != NULL, "elf_create failed");
	seg = elf_add_segment(obj, PT_NOTE, PF_R, 1);
	require_true(t, seg != NULL, "elf_add_segment failed");
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, has_phdr_type(rt.obj, PT_NOTE), "missing PT_NOTE");
	rt_close(&rt);
	elf_close(obj);
}

static void test_osabi(void) {
	const char *t = "osabi";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;

	require_true(t, obj != NULL, "elf_create failed");
	require_ok(t, "elf_set_osabi", elf_set_osabi(obj, ELFOSABI_LINUX));
	require_ok(t, "elf_set_abiversion", elf_set_abiversion(obj, 1));
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, elf_osabi(rt.obj) == ELFOSABI_LINUX, "osabi mismatch");
	require_true(t, elf_abiversion(rt.obj) == 1, "abiversion mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_sym_bindings(void) {
	const char *t = "sym_bindings";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;

	require_true(t, obj != NULL, "elf_create failed");
	require_true(t, elf_add_symbol(obj, "local", 0, 0, STB_LOCAL, STT_NOTYPE) != NULL, "add local failed");
	require_true(t, elf_add_symbol(obj, "global", 0, 0, STB_GLOBAL, STT_NOTYPE) != NULL, "add global failed");
	require_true(t, elf_add_symbol(obj, "weak", 0, 0, STB_WEAK, STT_NOTYPE) != NULL, "add weak failed");
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, elf_symbol_bind(require_symbol(t, rt.obj, "local")) == STB_LOCAL, "local bind mismatch");
	require_true(t, elf_symbol_bind(require_symbol(t, rt.obj, "global")) == STB_GLOBAL, "global bind mismatch");
	require_true(t, elf_symbol_bind(require_symbol(t, rt.obj, "weak")) == STB_WEAK, "weak bind mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_sym_types(void) {
	const char *t = "sym_types";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;

	require_true(t, obj != NULL, "elf_create failed");
	require_true(t, elf_add_symbol(obj, "file", 0, 0, STB_LOCAL, STT_FILE) != NULL, "add file failed");
	require_true(t, elf_add_symbol(obj, "notype", 0, 0, STB_GLOBAL, STT_NOTYPE) != NULL, "add notype failed");
	require_true(t, elf_add_symbol(obj, "object", 0, 0, STB_GLOBAL, STT_OBJECT) != NULL, "add object failed");
	require_true(t, elf_add_symbol(obj, "func", 0, 0, STB_GLOBAL, STT_FUNC) != NULL, "add func failed");
	require_true(t, elf_add_symbol(obj, "section", 0, 0, STB_GLOBAL, STT_SECTION) != NULL, "add section failed");
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	require_true(t, elf_symbol_type(require_symbol(t, rt.obj, "notype")) == STT_NOTYPE, "notype mismatch");
	require_true(t, elf_symbol_type(require_symbol(t, rt.obj, "object")) == STT_OBJECT, "object mismatch");
	require_true(t, elf_symbol_type(require_symbol(t, rt.obj, "func")) == STT_FUNC, "func mismatch");
	require_true(t, elf_symbol_type(require_symbol(t, rt.obj, "section")) == STT_SECTION, "section mismatch");
	require_true(t, elf_symbol_type(require_symbol(t, rt.obj, "file")) == STT_FILE, "file mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_merge_strings(void) {
	const char *t = "merge_strings";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;
	const char data[] = "hello\0world\0";

	require_true(t, obj != NULL, "elf_create failed");
	s = elf_add_section(obj, ".rodata.str", SHT_PROGBITS, SHF_MERGE | SHF_STRINGS);
	require_true(t, s != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_merge", elf_section_set_merge(s, 1, 1));
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, data, sizeof(data)));
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	s = require_section(t, rt.obj, ".rodata.str");
	require_true(t, (elf_section_flags(s) & SHF_MERGE) != 0, "SHF_MERGE missing");
	require_true(t, (elf_section_flags(s) & SHF_STRINGS) != 0, "SHF_STRINGS missing");
	rt_close(&rt);
	elf_close(obj);
}

static void test_entsize(void) {
	const char *t = "entsize";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;
	uint8_t data[16] = {0};

	require_true(t, obj != NULL, "elf_create failed");
	s = elf_add_section(obj, ".mytab", SHT_PROGBITS, SHF_MERGE);
	require_true(t, s != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_merge", elf_section_set_merge(s, 8, 0));
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, data, sizeof(data)));
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	s = require_section(t, rt.obj, ".mytab");
	require_true(t, elf_section_size(s) == sizeof(data), "section size mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_reloc_local(void) {
	const char *t = "reloc_local";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;
	elf_symbol_t *sym;
	elf_reloc_t *r;
	uint8_t code[4] = {0};

	require_true(t, obj != NULL, "elf_create failed");
	s = elf_add_section(obj, ".text", SHT_PROGBITS, 0);
	require_true(t, s != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, code, sizeof(code)));
	sym = elf_add_symbol(obj, "local_sym", 0, 0, STB_LOCAL, STT_NOTYPE);
	require_true(t, sym != NULL, "elf_add_symbol failed");
	require_ok(t, "elf_add_relocation", elf_add_relocation(s, 0, sym, R_386_32, 0));
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	s = require_section(t, rt.obj, ".text");
	require_true(t, elf_section_reloc_count(s) == 1, "unexpected relocation count");
	r = elf_section_reloc_at(s, 0);
	require_true(t, r != NULL, "missing relocation");
	sym = elf_reloc_symbol(r);
	require_true(t, sym != NULL, "missing relocation symbol");
	require_true(t, elf_symbol_bind(sym) == STB_LOCAL, "local bind mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_reloc_weak(void) {
	const char *t = "reloc_weak";
	elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;
	elf_section_t *s;
	elf_symbol_t *sym;
	elf_reloc_t *r;
	uint8_t code[4] = {0};

	require_true(t, obj != NULL, "elf_create failed");
	s = elf_add_section(obj, ".text", SHT_PROGBITS, 0);
	require_true(t, s != NULL, "elf_add_section failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, code, sizeof(code)));
	sym = elf_add_symbol(obj, "weak_sym", 0, 0, STB_WEAK, STT_NOTYPE);
	require_true(t, sym != NULL, "elf_add_symbol failed");
	require_ok(t, "elf_add_relocation", elf_add_relocation(s, 0, sym, R_386_32, 0));
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_STRICT);
	s = require_section(t, rt.obj, ".text");
	require_true(t, elf_section_reloc_count(s) == 1, "unexpected relocation count");
	r = elf_section_reloc_at(s, 0);
	require_true(t, r != NULL, "missing relocation");
	sym = elf_reloc_symbol(r);
	require_true(t, sym != NULL, "missing relocation symbol");
	require_true(t, elf_symbol_bind(sym) == STB_WEAK, "weak bind mismatch");
	rt_close(&rt);
	elf_close(obj);
}

static void test_tls(void) {
	const char *t = "tls";
	elfobj_t *obj = make_exec_dyn_obj(t, ET_EXEC);
	rt_obj_t rt;
	elf_section_t *s;
	elf_segment_t *tls;
	elf_segment_t *load;
	uint8_t tdata[4] = {1, 2, 3, 4};

	s = elf_add_section(obj, ".tdata", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
	require_true(t, s != NULL, "elf_add_section tdata failed");
	require_ok(t, "elf_section_set_data", elf_section_set_data(s, tdata, sizeof(tdata)));
	load = elf_add_load_segment(obj, PF_R | PF_W, 0x1000);
	require_true(t, load != NULL, "elf_add_load_segment for TLS failed");
	require_ok(t, "elf_segment_add_section(load)", elf_segment_add_section(load, s));
	tls = elf_add_tls_segment(obj, 4);
	require_true(t, tls != NULL, "elf_add_tls_segment failed");
	require_ok(t, "elf_segment_add_section(tls)", elf_segment_add_section(tls, s));

	rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	s = require_section(t, rt.obj, ".tdata");
	require_true(t, (elf_section_flags(s) & SHF_TLS) != 0, "SHF_TLS missing");
	require_true(t, has_phdr_type(rt.obj, PT_TLS), "missing PT_TLS");
	rt_close(&rt);
	elf_close(obj);
}

static void test_entry_point(void) {
	const char *t = "entry_point";
	elfobj_t *obj = make_exec_dyn_obj(t, ET_EXEC);
	rt_obj_t rt;

	require_ok(t, "elf_set_entry", elf_set_entry(obj, 0x12345678));
	rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);
	require_true(t, elf_entry(rt.obj) == 0x12345678, "entry mismatch");
	rt_close(&rt);
	elf_close(obj);
}


static void test_build_dynstr(void) {
	const char *t = "build_dynstr";
	elfobj_t *obj = elf_create(ET_DYN, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
	rt_obj_t rt;

	require_true(t, obj != NULL, "elf_create failed");
	require_true(t, elf_add_symbol(obj, "my_global_sym", 0, 0, STB_GLOBAL, STT_FUNC) != NULL, "add global sym failed");
	require_true(t, elf_add_symbol(obj, "my_weak_sym", 0, 0, STB_WEAK, STT_OBJECT) != NULL, "add weak sym failed");

	rt = roundtrip_validate(t, obj, ELF_VALIDATE_PERMISSIVE);

	elf_section_t *dynstr = require_section(t, rt.obj, ".dynstr");
	require_true(t, dynstr != NULL, "missing synthesized .dynstr section");
	require_true(t, elf_section_size(dynstr) > 0, ".dynstr should not be empty");

	rt_close(&rt);
	elf_close(obj);
}

int main(void) {
	test_minimal();
	test_many_sections();
	test_many_symbols();
	test_nobits();
	test_large_align();
	test_reloc_rel32();
	test_reloc_rela64();
	test_unnamed();
	test_et_exec();
	test_et_dyn();
	test_segments();
	test_empty_segment();
	test_osabi();
	test_sym_bindings();
	test_sym_types();
	test_merge_strings();
	test_entsize();
	test_reloc_local();
	test_reloc_weak();
	test_tls();
	test_entry_point();
	test_build_dynstr();
	printf("test_write_to_buffer: ALL CHECKS PASSED\n");
	return(0);
}
