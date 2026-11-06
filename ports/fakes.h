/* ports/fakes.h: deterministic, strict fakes of the four ports. No malloc, no real IO, no time source:
 * everything lives in caller-provided storage and in the structs below (treat their fields as opaque;
 * use only the functions). Used by the tests of later modules and tested here through the port tables.
 * A fake that is misused never crashes: it returns an error and records the deviation.
 * Every `*_init` returns false and leaves the object unusable when an argument is invalid. */
#ifndef PORTS_FAKES_H
#define PORTS_FAKES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/str.h"
#include "ports/ports.h"

/* ---- manual clock ----------------------------------------------------------------------------------- */

typedef struct {
    uint64_t now;
} ports_fake_clock_t;

/* Clock showing `start_ms`. Returns false only when `c` is NULL. */
bool ports_fake_clock_init(ports_fake_clock_t *c, uint64_t start_ms);
/* The port view of `c` (valid while `c` lives). A NULL `c` gives a port whose now_ms returns 0. */
ports_clock_t ports_fake_clock_port(ports_fake_clock_t *c);
/* now += ms, saturating at UINT64_MAX (never wraps). NULL: no effect. */
void ports_fake_clock_advance(ports_fake_clock_t *c, uint64_t ms);
/* Set now to `ms`. Refuses to go backwards: returns false and changes nothing when ms < now or `c` is NULL. */
bool ports_fake_clock_set(ports_fake_clock_t *c, uint64_t ms);

/* ---- table env -------------------------------------------------------------------------------------- */

typedef struct {
    core_str_t name;  /* non-empty */
    core_str_t value; /* may be empty */
} ports_fake_env_entry_t;

typedef struct {
    const ports_fake_env_entry_t *entries;
    size_t count;
} ports_fake_env_t;

/* Borrow `entries[0..count)` (must outlive the fake). The first entry with a given name wins. Returns false
 * when `e` is NULL, `entries` is NULL with count > 0, or any entry has an empty name or a NULL ptr with
 * len > 0. count == 0 is a valid empty environment. */
bool ports_fake_env_init(ports_fake_env_t *e, const ports_fake_env_entry_t *entries, size_t count);
ports_env_t ports_fake_env_port(ports_fake_env_t *e);

/* ---- in-memory fs ----------------------------------------------------------------------------------- */

#define PORTS_FAKE_FS_SLOTS 8u      /* temp + committed files together */
#define PORTS_FAKE_FS_NAME_MAX 255u /* longest accepted final name */

typedef enum {
    PORTS_FAKE_FS_OP_CREATE = 0,
    PORTS_FAKE_FS_OP_WRITE = 1,
    PORTS_FAKE_FS_OP_COMMIT = 2,
    PORTS_FAKE_FS_OP_DISCARD = 3
} ports_fake_fs_op_t;

typedef struct {
    uint32_t id;       /* 0 = free slot */
    bool committed;
    char name[PORTS_FAKE_FS_NAME_MAX + 1u];
    size_t name_len;
    size_t len;
} ports_fake_fs_slot_t;

typedef struct {
    ports_fake_fs_slot_t slots[PORTS_FAKE_FS_SLOTS];
    unsigned char *store;
    size_t slot_cap;
    uint32_t next_id;
    uint32_t calls[4];
    uint32_t inject_nth[4];
    ports_fs_err_t inject_err[4];
    uint32_t commits;
    uint32_t discards;
} ports_fake_fs_t;

/* Empty fs over storage[0..cap). Every file (temp or committed) may hold at most slot_cap = cap / 8 bytes.
 * Returns false when `fs` or `storage` is NULL or cap < PORTS_FAKE_FS_SLOTS (slot_cap would be 0).
 * Rules of the port as in ports.h, plus: create_temp with no free slot -> NOSPACE; a write that would
 * exceed slot_cap -> NOSPACE with the content unchanged; handles are unique and never reused. */
bool ports_fake_fs_init(ports_fake_fs_t *fs, void *storage, size_t cap);
ports_fs_t ports_fake_fs_port(ports_fake_fs_t *fs);
/* Put an already committed file `name` with `content` (for EXISTS tests). False if no free slot, the name
 * is invalid (as for commit), the name exists, or content is longer than slot_cap. */
bool ports_fake_fs_preload(ports_fake_fs_t *fs, core_str_t name, core_str_t content);
/* Make the `nth` call (1-based, counting every call of that op since init, failed ones included) of `op`
 * return `err` (any non-OK value) WITHOUT changing any state; one-shot. nth == 0 cancels. A second
 * injection for the same op replaces the first. False on NULL, bad op, or err == PORTS_FS_OK. */
bool ports_fake_fs_inject(ports_fake_fs_t *fs, ports_fake_fs_op_t op, uint32_t nth, ports_fs_err_t err);
/* Committed (visible) file called `name`: true and *content (into the fake's storage) if present. A temp
 * file is never found by name. `content` may be NULL. */
bool ports_fake_fs_find(const ports_fake_fs_t *fs, core_str_t name, core_str_t *content);
/* Current content of the open temp file `file`; false if no such open temp file. */
bool ports_fake_fs_temp_content(const ports_fake_fs_t *fs, ports_fs_file_t file, core_str_t *content);
size_t ports_fake_fs_committed_count(const ports_fake_fs_t *fs); /* visible files (preloaded included) */
size_t ports_fake_fs_temp_count(const ports_fake_fs_t *fs);      /* open temp files */
uint32_t ports_fake_fs_commits(const ports_fake_fs_t *fs);       /* successful commit calls */
uint32_t ports_fake_fs_discards(const ports_fake_fs_t *fs);      /* successful discard calls */

/* ---- scripted stream -------------------------------------------------------------------------------- */

typedef enum {
    PORTS_FAKE_STEP_CONNECT = 0,    /* expect connect(host = data, port); returns `err` (OK = success) */
    PORTS_FAKE_STEP_EXPECT = 1,     /* expect the next len(data) written bytes to equal `data` (any split) */
    PORTS_FAKE_STEP_RESPOND = 2,    /* deliver `data` through reads, at most `chunk` bytes per read (0 = no limit) */
    PORTS_FAKE_STEP_EOF = 3,        /* the next read is EOF (OK, n == 0); reads stay EOF until close */
    PORTS_FAKE_STEP_FAIL_WRITE = 4, /* the next write returns `err` (!= OK); the bytes are discarded */
    PORTS_FAKE_STEP_FAIL_READ = 5   /* the next read returns `err` (!= OK) */
} ports_fake_step_kind_t;

typedef struct {
    ports_fake_step_kind_t kind;
    core_str_t data; /* CONNECT: host; EXPECT, RESPOND: bytes; all non-empty with a non-NULL ptr; else unused */
    uint16_t port;   /* CONNECT: >= 1 */
    size_t chunk;    /* RESPOND only */
    ports_err_t err; /* a valid ports_err_t. CONNECT: outcome; FAIL_WRITE, FAIL_READ: must not be OK */
} ports_fake_step_t;

typedef enum {
    PORTS_FAKE_MM_NONE = 0,
    PORTS_FAKE_MM_CONNECT_TARGET = 1,  /* offset = first differing host byte (host length if the hosts are
                                          equal but the port differs) */
    PORTS_FAKE_MM_REQUEST_BYTES = 2,   /* offset = index in the EXPECT data of the first written byte that
                                          differs or lies beyond the end of the expectation */
    PORTS_FAKE_MM_UNEXPECTED_CALL = 3  /* a call the script does not allow at this point; offset = 0 */
} ports_fake_mismatch_kind_t;

typedef struct {
    ports_fake_mismatch_kind_t kind;
    size_t step;   /* index of the script step that was current */
    size_t offset;
} ports_fake_mismatch_t;

typedef struct {
    const ports_fake_step_t *steps;
    size_t count;
    size_t pos;         /* current step */
    size_t progress;    /* bytes of the current EXPECT/RESPOND step already matched/delivered */
    bool open;
    bool eof;
    bool failed;
    ports_fake_mismatch_t mm;
    ports_fake_clock_t *clock;
    uint64_t read_advance_ms;
    uint64_t deadline_ms;
    uint32_t closes;
} ports_fake_stream_t;

/* Strict scripted stream over `steps[0..count)` (borrowed; must outlive the fake). `clock` may be NULL (then
 * deadlines are ignored and nothing advances); otherwise every successful data read (n > 0, not EOF, not an
 * injected error) advances it by `read_advance_ms` AFTER delivering. Returns false when `s` is NULL, steps is
 * NULL with count > 0, or any step is invalid (unknown kind, bad data/port/err as listed in the step type).
 *
 * Behaviour, in this order for each call:
 *  - close: open -> OK, stream not open, counted, works even after a mismatch; not open -> E_CLOSED.
 *  - after a mismatch ("failed") every other call returns E_NET and records nothing more.
 *  - connect while open: UNEXPECTED_CALL mismatch, E_NET. read/write while not open: UNEXPECTED_CALL
 *    mismatch, E_CLOSED. Invalid arguments (NULL out/buffer with len > 0, cap == 0, empty host, port 0)
 *    return E_ARG with no mismatch and no state change.
 *  - connect: needs the current step to be CONNECT and host+port to equal it (else CONNECT_TARGET or
 *    UNEXPECTED_CALL mismatch, E_NET). If a clock is set, deadline != 0 and now >= deadline -> E_TIMEOUT, step
 *    not consumed. Otherwise the step is consumed; its `err` is returned (non-OK leaves the stream not open);
 *    OK opens it and stores the deadline.
 *  - write: the deadline check (E_TIMEOUT, nothing consumed) then, for the current step: EXPECT -> bytes are
 *    compared with the expectation, a difference or an excess is REQUEST_BYTES (E_NET); the step is consumed
 *    when all its bytes arrived; FAIL_WRITE -> consumed, returns its err; any other step -> UNEXPECTED_CALL.
 *    A zero-length write is OK and changes nothing (but is still checked for open/failed).
 *  - read: deadline check; then RESPOND -> min(cap, remaining, chunk) bytes (n >= 1), consumed when all
 *    delivered; EOF -> OK n = 0, step consumed, eof sticky until close; FAIL_READ -> consumed, returns err;
 *    while eof is set -> OK n = 0 whatever the step; any other step (including an unfinished EXPECT, a
 *    finished script) -> UNEXPECTED_CALL. */
bool ports_fake_stream_init(ports_fake_stream_t *s, const ports_fake_step_t *steps, size_t count,
                            ports_fake_clock_t *clock, uint64_t read_advance_ms);
ports_stream_t ports_fake_stream_port(ports_fake_stream_t *s);
/* True iff there was no mismatch and every step has been consumed. */
bool ports_fake_stream_done(const ports_fake_stream_t *s);
/* The FIRST mismatch (later ones are not recorded): true and *out filled, or false (out untouched). */
bool ports_fake_stream_mismatch(const ports_fake_stream_t *s, ports_fake_mismatch_t *out);
bool ports_fake_stream_is_open(const ports_fake_stream_t *s);
uint32_t ports_fake_stream_closes(const ports_fake_stream_t *s); /* closes that returned OK */
size_t ports_fake_stream_pos(const ports_fake_stream_t *s);      /* index of the current step */

#endif
