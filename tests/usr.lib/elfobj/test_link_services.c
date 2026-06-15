#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int archive_calls;
    int gc_calls;
    int incr_calls;
    int version_calls;
} hook_state_t;

static void fail(const char *msg) {
    fprintf(stderr, "test_link_services: %s\n", msg);
    exit(1);
}

static elf_link_merge_action_t merge_hook(const char *name, const elf_section_t *existing,
                                          const elf_section_t *incoming, void *user) {
    (void)existing;
    (void)incoming;
    (void)user;
    if (name != NULL && strcmp(name, ".note.replace") == 0) {
        return ELF_LINK_MERGE_REPLACE;
    }
    return ELF_LINK_MERGE_APPEND;
}

static int archive_hook(const char *archive_path, const char *member_name, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    st->archive_calls++;
    if (archive_path == NULL) {
        return 0;
    }
    if (member_name != NULL && strcmp(member_name, "skip.o") == 0) {
        return 0;
    }
    return 1;
}

static int gc_hook(const elf_section_t *section, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    st->gc_calls++;
    if (section != NULL && elf_section_name(section) != NULL &&
        strcmp(elf_section_name(section), ".dropme") == 0) {
        return 0;
    }
    return 1;
}

static void incr_hook(const char *key, const char *value, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    if (key != NULL && value != NULL) {
        st->incr_calls += 0;
    }
    st->incr_calls++;
}

static int version_hook(const char *symbol_name, const char *version_name, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    (void)version_name;
    st->version_calls++;
    if (symbol_name != NULL && strcmp(symbol_name, "skipver") == 0) {
        return 0;
    }
    return 1;
}

static elfobj_t *mk_obj_a(void) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_section_t *drop;
    elf_section_t *note;
    elf_symbol_t *s;
    uint8_t text_bytes[] = {0x90};
    uint8_t drop_bytes[] = {0xaa};
    uint8_t note_bytes[] = {1};

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) return NULL;
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    drop = elf_add_section(obj, ".dropme", SHT_PROGBITS, SHF_ALLOC);
    note = elf_add_section(obj, ".note.replace", SHT_NOTE, SHF_ALLOC);
    if (!text || !drop || !note) return NULL;
    if (elf_section_set_data(text, text_bytes, sizeof(text_bytes)) != ELF_OK) return NULL;
    if (elf_section_set_data(drop, drop_bytes, sizeof(drop_bytes)) != ELF_OK) return NULL;
    if (elf_section_set_data(note, note_bytes, sizeof(note_bytes)) != ELF_OK) return NULL;
    s = elf_add_symbol(obj, "keep", 0, 1, STB_GLOBAL, STT_FUNC);
    if (!s) return NULL;
    if (elf_symbol_define(s, text, 0) != ELF_OK) return NULL;
    if (!elf_add_symbol(obj, "skipver", 0, 1, STB_GLOBAL, STT_FUNC)) return NULL;
    return obj;
}

static elfobj_t *mk_obj_b(void) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_section_t *note;
    elf_symbol_t *s;
    uint8_t text_bytes[] = {0x90, 0xC3};
    uint8_t note_bytes[] = {2, 2};

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) return NULL;
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    note = elf_add_section(obj, ".note.replace", SHT_NOTE, SHF_ALLOC);
    if (!text || !note) return NULL;
    if (elf_section_set_data(text, text_bytes, sizeof(text_bytes)) != ELF_OK) return NULL;
    if (elf_section_set_data(note, note_bytes, sizeof(note_bytes)) != ELF_OK) return NULL;
    s = elf_add_symbol(obj, "weak_keep", 0, 2, STB_WEAK, STT_FUNC);
    if (!s) return NULL;
    if (elf_symbol_define(s, text, 0) != ELF_OK) return NULL;
    return obj;
}

int main(void) {
    elfobj_t *a;
    elfobj_t *b;
    elfobj_t *out = NULL;
    elfobj_t *legacy_out = NULL;
    elfobj_t **loaded = NULL;
    size_t loaded_count = 0;
    elfobj_t *ins[2];
    elf_link_plan_t *plan;
    elf_link_map_entry_t ent;
    hook_state_t st;
    elf_section_t *note;
    int should_extract = 0;
    const char *paths[2] = {"tmp_link_service_a.o", "tmp_link_service_b.o"};
    size_t resolved_idx = 99;

    memset(&st, 0, sizeof(st));
    a = mk_obj_a();
    b = mk_obj_b();
    if (!a || !b) fail("create objects");

    if (elf_write_file(a, paths[0]) != ELF_OK) fail("write a");
    if (elf_write_file(b, paths[1]) != ELF_OK) fail("write b");

    if (elf_link_load_objects(paths, 2, &loaded, &loaded_count) != ELF_OK) fail("load objects");
    if (loaded_count != 2) fail("load count");
    if (!elf_link_resolve_symbol(loaded, loaded_count, "keep", &resolved_idx)) fail("resolve keep");
    if (resolved_idx != 0) fail("resolve index");

    plan = elf_link_plan_create();
    if (!plan) fail("plan create");
    if (elf_link_plan_add_input(plan, loaded[0], "a.o") != ELF_OK) fail("add input a");
    if (elf_link_plan_add_input(plan, loaded[1], "b.o") != ELF_OK) fail("add input b");
    if (elf_link_plan_input_count(plan) != 2) fail("input count");

    if (elf_link_plan_set_section_merge_hook(plan, merge_hook, &st) != ELF_OK) fail("set merge hook");
    if (elf_link_plan_set_archive_hook(plan, archive_hook, &st) != ELF_OK) fail("set archive hook");
    if (elf_link_plan_set_gc_hook(plan, gc_hook, &st) != ELF_OK) fail("set gc hook");
    if (elf_link_plan_set_incremental_hook(plan, incr_hook, &st) != ELF_OK) fail("set incremental hook");
    if (elf_link_plan_set_version_hook(plan, version_hook, &st) != ELF_OK) fail("set version hook");

    if (elf_link_plan_consider_archive_member(plan, "libx.a", "skip.o", &should_extract) != ELF_OK)
        fail("archive consider");
    if (should_extract != 0) fail("archive skip");
    if (elf_link_plan_consider_archive_member(plan, "libx.a", "keep.o", &should_extract) != ELF_OK)
        fail("archive consider keep");
    if (should_extract != 1) fail("archive keep");

    if (elf_link_plan_note_incremental(plan, "seed", "value") != ELF_OK) fail("note incremental");
    if (elf_link_plan_link(plan, &out) != ELF_OK) fail("plan link");

    if (!elf_find_symbol(out, "keep")) fail("keep symbol");
    if (!elf_find_symbol(out, "weak_keep")) fail("weak symbol");
    if (elf_find_symbol(out, "skipver")) fail("version hook should drop symbol");
    if (elf_find_section(out, ".dropme")) fail("gc hook should drop section");
    note = elf_find_section(out, ".note.replace");
    if (!note) fail("note section");
    if (elf_section_size(note) != 2) fail("replace policy");

    if (elf_link_plan_map_count(plan) < 2) fail("link map count");
    if (!elf_link_plan_map_entry(plan, 0, &ent)) fail("link map entry");
    if (!ent.symbol_name || !ent.input_name) fail("link map entry fields");

    if (!elf_link_add_got_section(out, 4)) fail("add got");
    if (!elf_link_add_plt_section(out, 2)) fail("add plt");
    if (elf_link_add_dynamic_entry(out, DT_NEEDED, 1) != ELF_OK) fail("add dynamic entry");
    if (!elf_find_section(out, ".got")) fail("got section");
    if (!elf_find_section(out, ".plt")) fail("plt section");
    if (!elf_find_section(out, ".dynamic")) fail("dynamic section");

    ins[0] = loaded[0];
    ins[1] = loaded[1];
    if (elf_link(ins, 2, &legacy_out) != ELF_OK) fail("legacy elf_link");
    if (!elf_find_symbol(legacy_out, "keep")) fail("legacy keep");

    if (st.archive_calls < 2) fail("archive hook calls");
    if (st.gc_calls == 0) fail("gc hook calls");
    if (st.incr_calls == 0) fail("incremental hook calls");
    if (st.version_calls == 0) fail("version hook calls");

    elf_close(out);
    elf_close(legacy_out);
    elf_link_plan_destroy(plan);
    elf_link_unload_objects(loaded, loaded_count);
    elf_close(a);
    elf_close(b);
    return 0;
}
