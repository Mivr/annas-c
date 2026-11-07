/* Tests for fsname/fsname.h: argument checking at the edge (a NULL slice of length 1). Public header only. */
#include <stddef.h>
#include <string.h>

#include "ctest/ctest.h"
#include "fsname/fsname.h"

#define CANARY ((char)0xA5)

static void t_null_slice_of_any_positive_length_is_rejected(ctest_t *t) {
    static const size_t lens[] = {1, 2, 3, 8, 200, 201, 1000};
    char out[FSNAME_BUF_SIZE];
    size_t i, k;
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        memset(out, CANARY, sizeof out);
        CHECK_EQ_U64(t, fsname_sanitize((core_str_t){NULL, lens[i]}, out, sizeof out), 0);
        for (k = 0; k < sizeof out; k++) {
            if (out[k] != CANARY) {
                CHECK(t, out[k] == CANARY);
                break;
            }
        }
    }
}

static void t_null_slice_of_length_zero_is_an_empty_title(ctest_t *t) {
    char out[FSNAME_BUF_SIZE];
    memset(out, CANARY, sizeof out);
    CHECK_EQ_U64(t, fsname_sanitize((core_str_t){NULL, 0}, out, sizeof out), 8);
    CHECK_EQ_MEM(t, out, 9, "untitled", 9);
}

CTEST_SUITE("fsname_edge",
    {"null_slice_of_any_positive_length_is_rejected", t_null_slice_of_any_positive_length_is_rejected},
    {"null_slice_of_length_zero_is_an_empty_title", t_null_slice_of_length_zero_is_an_empty_title})
