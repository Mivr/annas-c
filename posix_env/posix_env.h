/* posix_env/posix_env.h: the production ports_env_t over a caller-supplied `envp` snapshot (the third argument of
 * `main`, or `environ` captured by the composition root). There is no getenv and no global state, and this module
 * makes no OS call, so it has no syscall seam: the snapshot array IS the seam, and the tests build their own.
 *
 * Snapshot format: a NULL-terminated array of NUL-terminated "NAME=VALUE" strings, as in `environ`. The adapter
 * borrows the array and the strings: they must stay valid and unchanged for as long as the port is used. Slices
 * returned by get() point INTO those strings (zero copy), so they live exactly as long as the snapshot.
 *
 * Entry rules: the name of an entry is the bytes before its FIRST '='; the value is everything after it (it may
 * contain further '=' and may be empty). An entry with no '=' and an entry whose name is empty ("=x") are ignored
 * (never found). A lookup name that contains '=' or a NUL byte never matches (no entry can have such a name).
 * Lookup is byte for byte and case-sensitive; if several entries have the same name, the FIRST one wins. All other
 * ports_env_t rules (empty name, NULL `value`, {NULL,0} on not found) are those of ports/ports.h. */
#ifndef POSIX_ENV_POSIX_ENV_H
#define POSIX_ENV_POSIX_ENV_H

#include <stdbool.h>

#include "ports/ports.h"

/* Caller-provided storage; treat the field as opaque. */
typedef struct {
    char *const *envp;
} posix_env_t;

/* Borrow `envp`. Returns false and leaves *e untouched when `e` or `envp` is NULL (an empty environment is an
 * array holding just the terminating NULL). The array is not scanned here. */
bool posix_env_init(posix_env_t *e, char *const *envp);

/* The port view of `e` (valid while `e` lives; one shared const ops table). A NULL `e` gives a port whose get()
 * always returns false. */
ports_env_t posix_env_port(posix_env_t *e);

#endif
