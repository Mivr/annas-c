/* STUB (test-author task A009): compiles, always fails. Replaced by the implementation task. */
#include "ports/fakes.h"

static ports_fs_err_t stub_create(void *ctx, ports_fs_file_t *file) {
    (void)ctx;
    (void)file;
    return PORTS_FS_E_IO;
}

static ports_fs_err_t stub_write(void *ctx, ports_fs_file_t file, const void *buf, size_t len) {
    (void)ctx;
    (void)file;
    (void)buf;
    (void)len;
    return PORTS_FS_E_IO;
}

static ports_fs_err_t stub_commit(void *ctx, ports_fs_file_t file, core_str_t final_name) {
    (void)ctx;
    (void)file;
    (void)final_name;
    return PORTS_FS_E_IO;
}

static ports_fs_err_t stub_discard(void *ctx, ports_fs_file_t file) {
    (void)ctx;
    (void)file;
    return PORTS_FS_E_IO;
}

static const ports_fs_ops_t k_ops = {stub_create, stub_write, stub_commit, stub_discard};

bool ports_fake_fs_init(ports_fake_fs_t *fs, void *storage, size_t cap) {
    (void)fs;
    (void)storage;
    (void)cap;
    return false;
}

ports_fs_t ports_fake_fs_port(ports_fake_fs_t *fs) {
    ports_fs_t p = {&k_ops, fs};
    return p;
}

bool ports_fake_fs_preload(ports_fake_fs_t *fs, core_str_t name, core_str_t content) {
    (void)fs;
    (void)name;
    (void)content;
    return false;
}

bool ports_fake_fs_inject(ports_fake_fs_t *fs, ports_fake_fs_op_t op, uint32_t nth, ports_fs_err_t err) {
    (void)fs;
    (void)op;
    (void)nth;
    (void)err;
    return false;
}

bool ports_fake_fs_find(const ports_fake_fs_t *fs, core_str_t name, core_str_t *content) {
    (void)fs;
    (void)name;
    (void)content;
    return false;
}

bool ports_fake_fs_temp_content(const ports_fake_fs_t *fs, ports_fs_file_t file, core_str_t *content) {
    (void)fs;
    (void)file;
    (void)content;
    return false;
}

size_t ports_fake_fs_committed_count(const ports_fake_fs_t *fs) {
    (void)fs;
    return SIZE_MAX;
}

size_t ports_fake_fs_temp_count(const ports_fake_fs_t *fs) {
    (void)fs;
    return SIZE_MAX;
}

uint32_t ports_fake_fs_commits(const ports_fake_fs_t *fs) {
    (void)fs;
    return UINT32_MAX;
}

uint32_t ports_fake_fs_discards(const ports_fake_fs_t *fs) {
    (void)fs;
    return UINT32_MAX;
}
