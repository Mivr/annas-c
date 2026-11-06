#include <string.h>

#include "ports/fakes.h"

static unsigned char *slot_data(const ports_fake_fs_t *fs, size_t idx) { return fs->store + idx * fs->slot_cap; }

static ports_fake_fs_t *as_fs(void *ctx) { return (ports_fake_fs_t *)ctx; }

/* Counts the call and fires a pending injection. Returns true (and *err set) when the call must fail. */
static bool take_injection(ports_fake_fs_t *fs, ports_fake_fs_op_t op, ports_fs_err_t *err) {
    fs->calls[op]++;
    if (fs->inject_nth[op] != 0u && fs->calls[op] == fs->inject_nth[op]) {
        fs->inject_nth[op] = 0u;
        *err = fs->inject_err[op];
        return true;
    }
    return false;
}

static bool name_valid(core_str_t n) {
    if (n.len == 0u || n.len > PORTS_FAKE_FS_NAME_MAX || n.ptr == NULL) {
        return false;
    }
    if (memchr(n.ptr, '/', n.len) != NULL || memchr(n.ptr, '\0', n.len) != NULL) {
        return false;
    }
    if (n.len == 1u && n.ptr[0] == '.') {
        return false;
    }
    return !(n.len == 2u && n.ptr[0] == '.' && n.ptr[1] == '.');
}

/* Index of the slot with this id and state, or PORTS_FAKE_FS_SLOTS. */
static size_t find_id(const ports_fake_fs_t *fs, ports_fs_file_t id, bool committed) {
    if (id == PORTS_FS_NO_FILE) {
        return PORTS_FAKE_FS_SLOTS;
    }
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        if (fs->slots[i].id == id && fs->slots[i].committed == committed) {
            return i;
        }
    }
    return PORTS_FAKE_FS_SLOTS;
}

static size_t find_name(const ports_fake_fs_t *fs, core_str_t name) {
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        const ports_fake_fs_slot_t *s = &fs->slots[i];
        if (s->id != 0u && s->committed && s->name_len == name.len && memcmp(s->name, name.ptr, name.len) == 0) {
            return i;
        }
    }
    return PORTS_FAKE_FS_SLOTS;
}

static size_t free_slot(const ports_fake_fs_t *fs) {
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        if (fs->slots[i].id == 0u) {
            return i;
        }
    }
    return PORTS_FAKE_FS_SLOTS;
}

static void clear_slot(ports_fake_fs_slot_t *s) {
    s->id = 0u;
    s->committed = false;
    s->name[0] = '\0';
    s->name_len = 0u;
    s->len = 0u;
}

static uint32_t take_id(ports_fake_fs_t *fs) {
    uint32_t id = fs->next_id;
    fs->next_id++;
    return id;
}

static ports_fs_err_t fs_create(void *ctx, ports_fs_file_t *file) {
    ports_fake_fs_t *fs = as_fs(ctx);
    ports_fs_err_t err = PORTS_FS_OK;
    if (fs == NULL) {
        return PORTS_FS_E_ARG;
    }
    if (take_injection(fs, PORTS_FAKE_FS_OP_CREATE, &err)) {
        return err;
    }
    if (file == NULL) {
        return PORTS_FS_E_ARG;
    }
    size_t i = free_slot(fs);
    if (i == PORTS_FAKE_FS_SLOTS || fs->next_id == 0u) {
        return PORTS_FS_E_NOSPACE;
    }
    fs->slots[i].id = take_id(fs);
    fs->slots[i].committed = false;
    fs->slots[i].len = 0u;
    *file = fs->slots[i].id;
    return PORTS_FS_OK;
}

static ports_fs_err_t fs_write(void *ctx, ports_fs_file_t file, const void *buf, size_t len) {
    ports_fake_fs_t *fs = as_fs(ctx);
    ports_fs_err_t err = PORTS_FS_OK;
    if (fs == NULL) {
        return PORTS_FS_E_ARG;
    }
    if (take_injection(fs, PORTS_FAKE_FS_OP_WRITE, &err)) {
        return err;
    }
    size_t i = find_id(fs, file, false);
    if (i == PORTS_FAKE_FS_SLOTS || (buf == NULL && len > 0u)) {
        return PORTS_FS_E_ARG;
    }
    if (len > fs->slot_cap - fs->slots[i].len) {
        return PORTS_FS_E_NOSPACE;
    }
    if (len > 0u) {
        memcpy(slot_data(fs, i) + fs->slots[i].len, buf, len);
        fs->slots[i].len += len;
    }
    return PORTS_FS_OK;
}

static ports_fs_err_t fs_commit(void *ctx, ports_fs_file_t file, core_str_t final_name) {
    ports_fake_fs_t *fs = as_fs(ctx);
    ports_fs_err_t err = PORTS_FS_OK;
    if (fs == NULL) {
        return PORTS_FS_E_ARG;
    }
    if (take_injection(fs, PORTS_FAKE_FS_OP_COMMIT, &err)) {
        return err;
    }
    size_t i = find_id(fs, file, false);
    if (i == PORTS_FAKE_FS_SLOTS || !name_valid(final_name)) {
        return PORTS_FS_E_ARG;
    }
    if (find_name(fs, final_name) != PORTS_FAKE_FS_SLOTS) {
        return PORTS_FS_E_EXISTS;
    }
    memcpy(fs->slots[i].name, final_name.ptr, final_name.len);
    fs->slots[i].name[final_name.len] = '\0';
    fs->slots[i].name_len = final_name.len;
    fs->slots[i].committed = true;
    fs->commits++;
    return PORTS_FS_OK;
}

static ports_fs_err_t fs_discard(void *ctx, ports_fs_file_t file) {
    ports_fake_fs_t *fs = as_fs(ctx);
    ports_fs_err_t err = PORTS_FS_OK;
    if (fs == NULL) {
        return PORTS_FS_E_ARG;
    }
    if (take_injection(fs, PORTS_FAKE_FS_OP_DISCARD, &err)) {
        return err;
    }
    size_t i = find_id(fs, file, false);
    if (i == PORTS_FAKE_FS_SLOTS) {
        return PORTS_FS_E_ARG;
    }
    clear_slot(&fs->slots[i]);
    fs->discards++;
    return PORTS_FS_OK;
}

static const ports_fs_ops_t k_ops = {fs_create, fs_write, fs_commit, fs_discard};

bool ports_fake_fs_init(ports_fake_fs_t *fs, void *storage, size_t cap) {
    if (fs == NULL || storage == NULL || cap < PORTS_FAKE_FS_SLOTS) {
        return false;
    }
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        clear_slot(&fs->slots[i]);
    }
    fs->store = (unsigned char *)storage;
    fs->slot_cap = cap / PORTS_FAKE_FS_SLOTS;
    fs->next_id = 1u;
    for (size_t k = 0; k < 4u; k++) {
        fs->calls[k] = 0u;
        fs->inject_nth[k] = 0u;
        fs->inject_err[k] = PORTS_FS_OK;
    }
    fs->commits = 0u;
    fs->discards = 0u;
    return true;
}

ports_fs_t ports_fake_fs_port(ports_fake_fs_t *fs) {
    ports_fs_t p = {&k_ops, fs};
    return p;
}

bool ports_fake_fs_preload(ports_fake_fs_t *fs, core_str_t name, core_str_t content) {
    if (fs == NULL || !name_valid(name) || (content.ptr == NULL && content.len > 0u)) {
        return false;
    }
    size_t i = free_slot(fs);
    if (i == PORTS_FAKE_FS_SLOTS || fs->next_id == 0u || content.len > fs->slot_cap ||
        find_name(fs, name) != PORTS_FAKE_FS_SLOTS) {
        return false;
    }
    if (content.len > 0u) {
        memcpy(slot_data(fs, i), content.ptr, content.len);
    }
    memcpy(fs->slots[i].name, name.ptr, name.len);
    fs->slots[i].name[name.len] = '\0';
    fs->slots[i].name_len = name.len;
    fs->slots[i].len = content.len;
    fs->slots[i].committed = true;
    fs->slots[i].id = take_id(fs);
    return true;
}

bool ports_fake_fs_inject(ports_fake_fs_t *fs, ports_fake_fs_op_t op, uint32_t nth, ports_fs_err_t err) {
    if (fs == NULL || (unsigned)op > (unsigned)PORTS_FAKE_FS_OP_DISCARD || err == PORTS_FS_OK) {
        return false;
    }
    fs->inject_nth[op] = nth;
    fs->inject_err[op] = err;
    return true;
}

static void set_content(const ports_fake_fs_t *fs, size_t i, core_str_t *content) {
    if (content != NULL) {
        content->ptr = (const char *)slot_data(fs, i);
        content->len = fs->slots[i].len;
    }
}

bool ports_fake_fs_find(const ports_fake_fs_t *fs, core_str_t name, core_str_t *content) {
    if (fs == NULL || (name.ptr == NULL && name.len > 0u)) {
        return false;
    }
    size_t i = find_name(fs, name);
    if (i == PORTS_FAKE_FS_SLOTS) {
        return false;
    }
    set_content(fs, i, content);
    return true;
}

bool ports_fake_fs_temp_content(const ports_fake_fs_t *fs, ports_fs_file_t file, core_str_t *content) {
    if (fs == NULL) {
        return false;
    }
    size_t i = find_id(fs, file, false);
    if (i == PORTS_FAKE_FS_SLOTS) {
        return false;
    }
    set_content(fs, i, content);
    return true;
}

static size_t count_slots(const ports_fake_fs_t *fs, bool committed) {
    size_t n = 0u;
    if (fs == NULL) {
        return 0u;
    }
    for (size_t i = 0; i < PORTS_FAKE_FS_SLOTS; i++) {
        if (fs->slots[i].id != 0u && fs->slots[i].committed == committed) {
            n++;
        }
    }
    return n;
}

size_t ports_fake_fs_committed_count(const ports_fake_fs_t *fs) { return count_slots(fs, true); }

size_t ports_fake_fs_temp_count(const ports_fake_fs_t *fs) { return count_slots(fs, false); }

uint32_t ports_fake_fs_commits(const ports_fake_fs_t *fs) { return fs == NULL ? 0u : fs->commits; }

uint32_t ports_fake_fs_discards(const ports_fake_fs_t *fs) { return fs == NULL ? 0u : fs->discards; }
