/* json/src/internal.h: helpers shared by the json implementation files. Not part of the public API. */
#ifndef JSON_SRC_INTERNAL_H
#define JSON_SRC_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "json/json.h"

/* Length (1..4) of the valid UTF-8 sequence starting at p[0], looking at no more than n bytes; 0 when the
 * sequence is malformed, truncated, overlong, a surrogate, or above U+10FFFF (or n == 0). */
size_t json_utf8_seq_len(const unsigned char *p, size_t n);

struct json_member;

/* DOM node. Every node keeps its `next` sibling and its `parent` after the parse, so the serialiser can walk
 * the tree without a stack. `items` (containers) is the random-access view used by the accessors. */
struct json_value {
    json_type_t type;
    bool b;
    bool red; /* parse-time duplicate-key index colour (object members only) */
    struct json_value *next;
    struct json_value *parent;
    core_str_t key; /* set for object members */
    union {
        core_str_t s; /* string value / number text */
        struct {
            union {
                struct json_value *const *items;
                struct json_member *tree; /* open object: root of its key index; replaced by items at close */
            };
            size_t n;
            struct json_value *first;
            struct json_value *last;
        } c;
    } u;
};

/* An object member: the node plus the two links of the red-black key index used to reject repeated keys in
 * O(log n) per member. This is the only extra memory of the index: 2 pointers per object member. */
struct json_member {
    struct json_value v;
    struct json_member *kid[2];
};

/* Parser state, shared by parse.c, num.c and utf8.c. */
typedef struct {
    const unsigned char *s;
    size_t n;
    size_t i;
    core_arena_t *a;
    json_limits_t lim;
    size_t depth;
    size_t elems;
    struct json_value *cur;  /* innermost open container */
    struct json_value *root;
    bool just_opened;
    size_t err;              /* error offset */
} json_parser_t;

/* p->i at the opening quote. Validates and decodes the string; on success p->i is past the closing quote and
 * *out_len is the decoded length. With dst != NULL the decoded bytes are written (dst has *out_len room). */
json_err_t json_str_decode(json_parser_t *p, char *dst, size_t *out_len);

/* p->i at '-' or a digit. On success p->i is past the number. */
json_err_t json_num_scan(json_parser_t *p);

/* Writer: appends verbatim number text as a value. */
void json_w_number_text(json_writer_t *w, core_str_t text);

#endif
