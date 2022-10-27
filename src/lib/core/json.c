//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "number.h"
#include "memory.h"
#include "string_buf.h"

//===========================================================================
#include "log.h"

#define SKY_JSON_PADDING_SIZE 4

/** Whitespace character: ' ', '\\t', '\\n', '\\r'. */
#define CHAR_TYPE_SPACE (1 << 0)

/** Number character: '-', [0-9]. */
#define CHAR_TYPE_NUMBER (1 << 1)

/** JSON Escaped character: '"', '\', [0x00-0x1F]. */
#define CHAR_TYPE_ESC_ASCII (1 << 2)

/** Non-ASCII character: [0x80-0xFF]. */
#define CHAR_TYPE_NON_ASCII (1 << 3)

/** JSON container character: '{', '['. */
#define CHAR_TYPE_CONTAINER (1 << 4)

/** Comment character: '/'. */
#define CHAR_TYPE_COMMENT (1 << 5)

/** Line end character '\\n', '\\r', '\0'. */
#define CHAR_TYPE_LINE_END (1 << 6)


/*
 Estimated initial ratio of the JSON data (data_size / value_count).
 For example:

    data:        {"id":12345678,"name":"Harry"}
    data_size:   30
    value_count: 5
    ratio:       6

 uses dynamic memory with a growth factor of 1.5 when reading and writing
 JSON, the ratios below are used to determine the initial memory size.

 A too large ratio will waste memory, and a too small ratio will cause multiple
 memory growths and degrade performance. Currently, these ratios are generated
 with some commonly used JSON datasets.
 */
#define READER_ESTIMATED_PRETTY_RATIO 16
#define READER_ESTIMATED_MINIFY_RATIO 6
#define WRITER_ESTIMATED_PRETTY_RATIO 32
#define WRITER_ESTIMATED_MINIFY_RATIO 18

#define F64_RAW_INF SKY_U64(0x7FF0000000000000)
#define F64_RAW_NAN SKY_U64(0x7FF8000000000000)

#define repeat16(x) { x x x x x x x x x x x x x x x x }
#define repeat16_incr(x) { x(0) x(1) x(2) x(3) x(4) x(5) x(6) x(7) \
                           x(8) x(9) x(10) x(11) x(12) x(13) x(14) x(15) }

static sky_usize_t json_get_len(const sky_json_val_t *val);

static sky_json_val_t *json_get_first(sky_json_val_t *ctn);

static sky_json_val_t *json_get_next(const sky_json_val_t *val);

static sky_bool_t char_is_type(sky_uchar_t c, sky_u8_t type);

static sky_bool_t char_is_space(sky_uchar_t c);

static sky_bool_t char_is_space_or_comment(sky_uchar_t c);

static sky_bool_t char_is_number(sky_uchar_t c);

static sky_bool_t char_is_container(sky_uchar_t c);

static sky_bool_t char_is_ascii_stop(sky_uchar_t c);

static sky_bool_t char_is_line_end(sky_uchar_t c);

static sky_bool_t skip_spaces_and_comments(sky_uchar_t **ptr);

static sky_json_doc_t *read_root_pretty(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts);

static sky_json_doc_t *read_root_minify(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts);

static sky_json_doc_t *read_root_single(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts);

static sky_bool_t read_number(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t ext);

static sky_bool_t read_string(sky_uchar_t **ptr, sky_uchar_t *end, sky_json_val_t *inv, sky_bool_t ext);

static sky_bool_t read_true(sky_uchar_t **ptr, sky_json_val_t *val);

static sky_bool_t read_false(sky_uchar_t **ptr, sky_json_val_t *val);

static sky_bool_t read_null(sky_uchar_t **ptr, sky_json_val_t *val);

static sky_bool_t read_inf(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign);

static sky_bool_t read_nan(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign);

static sky_bool_t read_inf_or_nan(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign);

static sky_bool_t read_hex_u16(const sky_uchar_t *cur, sky_u16_t *val);

sky_json_doc_t *
sky_json_read_opts(const sky_str_t *str, sky_u32_t opts) {
#define has_flag(_flag) sky_unlikely((opts & (_flag)) != 0)

#define return_err(_msg) do { \
    if (!has_flag(SKY_JSON_READ_IN_SITU)) { \
        sky_free(hdr);        \
    }                         \
    return null;              \
} while(false)

    if (sky_unlikely(!str)) {
        sky_log_error("input data is");
        return null;
    }
    if (sky_unlikely(!str->len)) {
        sky_log_error("input length is 0");
        return null;
    }
    sky_uchar_t *hdr, *cur, *end;

    if (has_flag(SKY_JSON_READ_IN_SITU)) {
        hdr = str->data;
        cur = hdr;
        end = hdr + str->len;
    } else {
        if (sky_unlikely(str->len > (SKY_USIZE_MAX - SKY_JSON_PADDING_SIZE))) {
            sky_log_error("memory allocation failed");
            return null;
        }
        hdr = sky_malloc(str->len + SKY_JSON_PADDING_SIZE);
        if (sky_unlikely(!hdr)) {
            sky_log_error("memory allocation failed");
            return null;
        }
        cur = hdr;
        end = hdr + str->len;

        sky_memcpy(hdr, str->data, str->len);
        sky_memzero(end, SKY_JSON_PADDING_SIZE);
    }

    if (sky_unlikely(char_is_space_or_comment(*cur))) {
        if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
            if (!skip_spaces_and_comments(&cur)) {
                return_err(unclosed multiline comment);
            }
        } else {
            if (sky_likely(char_is_space(*cur))) {
                while (char_is_space(*++cur));
            }
        }
        if (sky_unlikely(cur >= end)) {
            return_err("input data is empty");
        }
    }

    sky_json_doc_t *doc;

    if (sky_likely(char_is_container(*cur))) {
        if (char_is_space(cur[1]) && char_is_space(cur[2])) {
            doc = read_root_pretty(hdr, cur, end, opts);
        } else {
            doc = read_root_minify(hdr, cur, end, opts);
        }
    } else {
        doc = read_root_single(hdr, cur, end, opts);
    }

    if (sky_unlikely(!doc)) {
        if (!has_flag(SKY_JSON_READ_IN_SITU)) {
            sky_free(hdr);
        }
    }

    return doc;

#undef return_err
#undef has_flag
}

sky_json_val_t *
sky_json_doc_get_root(sky_json_doc_t *doc) {
    return sky_likely(doc) ? doc->root : null;
}

sky_json_val_t *
sky_json_obj_get(sky_json_val_t *obj, const sky_uchar_t *key, sky_u32_t key_len) {
    const sky_u64_t tag = (((sky_u64_t) key_len) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_STR;
    if (sky_likely(sky_json_is_obj(obj) && key_len)) {
        sky_usize_t len = json_get_len(obj);

        sky_json_val_t *item = json_get_first(obj);
        while (len-- > 0) {
            if (item->tag == tag && sky_str_len_unsafe_equals(item->ptr, key, key_len)) {
                return item + 1;
            }
            item = json_get_next(item + 1);
        }
    }
    return null;
}

void
sky_json_doc_free(sky_json_doc_t *doc) {
    if (sky_likely(doc)) {
        if (doc->str_pool) {
            sky_free(doc->str_pool);
            doc->str_pool = null;
        }
        sky_free(doc);
    }
}

static sky_inline sky_usize_t
json_get_len(const sky_json_val_t *val) {
    return (sky_usize_t) (val->tag >> SKY_JSON_TAG_BIT);
}

static sky_inline sky_json_val_t *
json_get_first(sky_json_val_t *ctn) {
    return ctn + 1;
}

static sky_inline sky_json_val_t *
json_get_next(const sky_json_val_t *val) {
    const sky_bool_t is_ctn = sky_json_unsafe_is_ctn(val);
    const sky_usize_t ctn_ofs = val->ofs;
    const sky_usize_t ofs = (is_ctn ? ctn_ofs : sizeof(sky_json_val_t));
    return (sky_json_val_t *) ((uint8_t *) val + ofs);
}


static sky_inline
sky_bool_t char_is_type(sky_uchar_t c, sky_u8_t type) {
    static const sky_uchar_t char_table[256] = {
            0x44, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
            0x04, 0x05, 0x45, 0x04, 0x04, 0x45, 0x04, 0x04,
            0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
            0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
            0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x20,
            0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
            0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x10, 0x04, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
            0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08
    };

    return (char_table[c] & type) != 0;
}

/**
 * Match a whitespace: ' ', '\\t', '\\n', '\\r'.
 * @param c char
 * @return true or false
 */
static sky_inline sky_bool_t
char_is_space(sky_uchar_t c) {
    return char_is_type(c, CHAR_TYPE_SPACE);
}

/**
 * Match a whitespace or comment: ' ', '\\t', '\\n', '\\r', '/'.
 * @param c char
 * @return true or false
 */
static sky_inline sky_bool_t
char_is_space_or_comment(sky_uchar_t c) {
    return char_is_type(c, CHAR_TYPE_SPACE | CHAR_TYPE_COMMENT);
}

/** Match a JSON number: '-', [0-9]. */

/**
 * Match a whitespace or comment: ' ', '\\t', '\\n', '\\r', '/'.
 * @param c char
 * @return true or false
 */
static sky_inline sky_bool_t
char_is_number(sky_uchar_t c) {
    return char_is_type(c, CHAR_TYPE_NUMBER);
}

/**
 * Match a JSON container: '{', '['.
 * @param c char
 * @return true or false
 */
static sky_inline sky_bool_t
char_is_container(sky_uchar_t c) {
    return char_is_type(c, CHAR_TYPE_CONTAINER);
}

/**
 * Match a stop character in ASCII string: '"', '\', [0x00-0x1F], [0x80-0xFF]
 * @param c char
 * @return true or false
 */
static sky_inline sky_bool_t
char_is_ascii_stop(sky_uchar_t c) {
    return char_is_type(c, CHAR_TYPE_ESC_ASCII | CHAR_TYPE_NON_ASCII);
}

/**
 * Match a line end character: '\\n', '\\r', '\0'
 * @param c char
 * @return true or false
 */
static sky_inline sky_bool_t
char_is_line_end(sky_uchar_t c) {
    return char_is_type(c, CHAR_TYPE_LINE_END);
}


/**
 *  Skips spaces and comments as many as possible.

    It will return false in these cases:
    1. No character is skipped. The 'end' pointer is set as input cursor.
    2. A multiline comment is not closed. The 'end' pointer is set as the head
       of this comment block.
 * @param ptr ptr
 * @return true or false
 */
static sky_inline sky_bool_t
skip_spaces_and_comments(sky_uchar_t **ptr) {
    sky_uchar_t *hdr = *ptr;
    sky_uchar_t *cur = *ptr;
    sky_uchar_t **end = ptr;
    for (;;) {
        if (sky_str2_cmp(cur, '/', '*')) {
            hdr = cur;
            cur += 2;
            for (;;) {
                if (sky_str2_cmp(cur, '*', '/')) {
                    cur += 2;
                    break;
                }
                if (*cur == 0) {
                    *end = hdr;
                    return false;
                }
                ++cur;
            }
            continue;
        }

        if (sky_str2_cmp(cur, '/', '/')) {
            cur += 2;
            while (!char_is_line_end(*cur)) {
                ++cur;
            }
            continue;
        }
        if (char_is_space(*cur)) {
            ++cur;
            while (char_is_space(*cur)) {
                ++cur;
            }
            continue;
        }
        break;
    }
    *end = cur;
    return hdr != cur;
}

/**
 * Get raw 'infinity' with sign.
 * @param sign sign
 * @return value
 */
static sky_inline sky_u64_t
f64_raw_get_inf(sky_bool_t sign) {
    return F64_RAW_INF | ((sky_u64_t) sign << 63);
}

/**
 * Get raw 'nan' with sign.
 * @param sign sign
 * @return value
 */
static sky_inline sky_u64_t
f64_raw_get_nan(sky_bool_t sign) {
    return F64_RAW_NAN | ((sky_u64_t) sign << 63);
}


static sky_json_doc_t *
read_root_pretty(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts) {
#define has_flag(_flag) sky_unlikely((opts & (_flag)) != 0)
#define has_flag2(_flag) ((opts & (_flag)) != 0)

#define val_incr() do { \
    ++val; \
    if (sky_unlikely(val >= val_end)) { \
        alc_len += alc_len >> 1; \
        if (sky_unlikely(alc_len >= alc_max)) goto fail_alloc; \
        val_tmp = (sky_json_val_t *) sky_realloc(val_hdr, alc_len * sizeof(sky_json_val_t)); \
        if (sky_unlikely(!val_tmp)) goto fail_alloc; \
        val = val_tmp + (sky_usize_t)(val - val_hdr); \
        ctn = val_tmp + (sky_usize_t)(ctn - val_hdr); \
        val_hdr = val_tmp; \
        val_end = val_tmp + (alc_len - 2); \
    } \
} while (false)

    const sky_usize_t dat_len = has_flag(SKY_JSON_READ_STOP_WHEN_DONE) ? 256 : (sky_usize_t) (end - cur);
    const sky_usize_t alc_max = SKY_USIZE_MAX / sizeof(sky_json_val_t);

    sky_usize_t hdr_len = sizeof(sky_json_doc_t) / sizeof(sky_json_val_t);
    hdr_len += (sizeof(sky_json_doc_t) % sizeof(sky_json_val_t)) > 0;

    sky_usize_t alc_len = hdr_len + (dat_len / READER_ESTIMATED_PRETTY_RATIO) + 4;
    alc_len = sky_min(alc_len, alc_max);

    sky_json_val_t *val_hdr = sky_malloc(alc_len * sizeof(sky_json_val_t));
    if (sky_unlikely(!val_hdr)) {
        goto fail_alloc;
    }
    sky_json_val_t *val_end = val_hdr + (alc_len - 2); /* padding for key-value pair reading */
    sky_json_val_t *val = val_hdr + hdr_len;

    sky_json_val_t *val_tmp, *ctn_parent;
    sky_json_val_t *ctn = val;
    sky_usize_t ctn_len = 0;

    const sky_bool_t raw = has_flag2(SKY_JSON_READ_NUMBER_AS_RAW);
    const sky_bool_t ext = has_flag2(SKY_JSON_READ_ALLOW_INF_AND_NAN);
    const sky_bool_t inv = has_flag2(SKY_JSON_READ_ALLOW_INVALID_UNICODE);

    sky_uchar_t *raw_end = null; /* raw end for null-terminator */
    sky_uchar_t **pre = raw ? &raw_end : null; /* previous raw end pointer */

    if (*cur++ == '{') {
        ctn->tag = SKY_JSON_TYPE_OBJ;
        ctn->ofs = 0;
        if (*cur == '\n') {
            ++cur;
        }
        goto obj_key_begin;
    } else {
        ctn->tag = SKY_JSON_TYPE_ARR;
        ctn->ofs = 0;
        if (*cur == '\n') {
            ++cur;
        }
        goto arr_val_begin;
    }

    arr_begin:
    /* save current container */
    ctn->tag = (((sky_u64_t) ctn_len + 1) << SKY_JSON_TAG_BIT) | (ctn->tag & SKY_JSON_TAG_MASK);

    /* create a new array value, save parent container offset */
    val_incr();
    val->tag = SKY_JSON_TYPE_ARR;
    val->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);

    /* push the new array value as current container */
    ctn = val;
    ctn_len = 0;

    arr_val_begin:
    for (;;) {
        repeat16({
                     if (sky_str2_cmp(cur, ' ', ' ')) {
                         cur += 2;
                     } else {
                         break;
                     }
                 });
    }
    if (*cur == '{') {
        ++cur;
        goto obj_begin;
    }
    if (*cur == '[') {
        ++cur;
        goto arr_begin;
    }
    if (char_is_number(*cur)) {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_number(&cur, pre, val, ext))) {
            goto arr_val_end;
        }
        goto fail_number;
    }
    if (*cur == '"') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_string(&cur, end, val, inv))) {
            goto arr_val_end;
        }
        goto fail_string;
    }
    if (*cur == 't') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_true(&cur, val))) {
            goto arr_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'f') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_false(&cur, val))) {
            goto arr_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'n') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_null(&cur, val))) {
            goto arr_val_end;
        }
        if (sky_likely(ext)) {
            if (read_nan(&cur, pre, val, false)) {
                goto arr_val_end;
            }
        }
        goto fail_literal;
    }
    if (*cur == ']') {
        ++cur;
        if (sky_likely(ctn_len == 0)) {
            goto arr_end;
        }
        if (has_flag(SKY_JSON_READ_ALLOW_TRAILING_COMMAS)) {
            goto arr_end;
        }
        goto fail_trailing_comma;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto arr_val_begin;
    }
    if (sky_likely(ext) && (*cur == 'i' || *cur == 'I' || *cur == 'N')) {
        val_incr();
        ++ctn_len;
        if (read_inf_or_nan(&cur, pre, val, false)) {
            goto arr_val_end;
        }
        goto fail_character;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto arr_val_begin;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    arr_val_end:

    if (sky_str2_cmp(cur, ',', '\n')) {
        cur += 2;
        goto arr_val_begin;
    }
    if (*cur == ',') {
        ++cur;
        goto arr_val_begin;
    }
    if (*cur == ']') {
        ++cur;
        goto arr_end;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto arr_val_end;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto arr_val_end;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    arr_end:

    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->ofs);
    ctn->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
    ctn->tag = (ctn_len << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_ARR;
    if (sky_unlikely(ctn == ctn_parent)) {
        goto doc_end;
    }

    /* pop parent as current container */
    ctn = ctn_parent;
    ctn_len = (sky_usize_t) (ctn->tag >> SKY_JSON_TAG_BIT);
    if (*cur == '\n') {
        ++cur;
    }
    if ((ctn->tag & SKY_JSON_TYPE_MASK) == SKY_JSON_TYPE_OBJ) {
        goto obj_val_end;
    } else {
        goto arr_val_end;
    }

    obj_begin:
    /* push container */
    ctn->tag = (((sky_u64_t) ctn_len + 1) << SKY_JSON_TAG_BIT) |
               (ctn->tag & SKY_JSON_TAG_MASK);
    val_incr();
    val->tag = SKY_JSON_TYPE_OBJ;
    /* offset to the parent */
    val->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);
    ctn = val;
    ctn_len = 0;
    if (*cur == '\n') {
        ++cur;
    }

    obj_key_begin:

    for (;;) {
        repeat16({
                     if (sky_str2_cmp(cur, ' ', ' ')) {
                         cur += 2;
                     } else {
                         break;
                     }
                 });
    }
    if (sky_likely(*cur == '"')) {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_string(&cur, end, val, inv))) {
            goto obj_key_end;
        }
        goto fail_string;
    }
    if (sky_likely(*cur == '}')) {
        ++cur;
        if (sky_likely(!ctn_len)) {
            goto obj_end;
        }
        if (has_flag(SKY_JSON_READ_ALLOW_TRAILING_COMMAS)) {
            goto obj_end;
        }
        goto fail_trailing_comma;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_key_begin;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_key_begin;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_key_end:

    if (sky_str2_cmp(cur, ':', ' ')) {
        cur += 2;
        goto obj_val_begin;
    }
    if (*cur == ':') {
        ++cur;
        goto obj_val_begin;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_key_end;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_key_end;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_val_begin:

    if (*cur == '"') {
        ++val;
        ctn_len++;
        if (sky_likely(read_string(&cur, end, val, inv))) {
            goto obj_val_end;
        }
        goto fail_string;
    }
    if (char_is_number(*cur)) {
        ++val;
        ++ctn_len;
        if (sky_likely(read_number(&cur, pre, val, ext))) {
            goto obj_val_end;
        }
        goto fail_number;
    }
    if (*cur == '{') {
        ++cur;
        goto obj_begin;
    }
    if (*cur == '[') {
        ++cur;
        goto arr_begin;
    }
    if (*cur == 't') {
        ++val;
        ++ctn_len;
        if (sky_likely(read_true(&cur, val))) {
            goto obj_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'f') {
        ++val;
        ++ctn_len;
        if (sky_likely(read_false(&cur, val))) {
            goto obj_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'n') {
        ++val;
        ++ctn_len;
        if (sky_likely(read_null(&cur, val))) {
            goto obj_val_end;
        }
        if (sky_unlikely(ext)) {
            if (read_nan(&cur, pre, val, false)) {
                goto obj_val_end;
            }
        }
        goto fail_literal;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_val_begin;
    }
    if (sky_unlikely(ext) && (*cur == 'i' || *cur == 'I' || *cur == 'N')) {
        ++val;
        ++ctn_len;
        if (read_inf_or_nan(&cur, pre, val, false)) {
            goto obj_val_end;
        }
        goto fail_character;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_val_begin;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_val_end:
    if (sky_str2_cmp(cur, ',', '\n')) {
        cur += 2;
        goto obj_key_begin;
    }
    if (sky_likely(*cur == ',')) {
        ++cur;
        goto obj_key_begin;
    }
    if (sky_likely(*cur == '}')) {
        ++cur;
        goto obj_end;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_val_end;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_val_end;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_end:

    /* pop container */
    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->ofs);
    /* point to the next value */
    ctn->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
    ctn->tag = (ctn_len << (SKY_JSON_TAG_BIT - 1)) | SKY_JSON_TYPE_OBJ;
    if (sky_unlikely(ctn == ctn_parent)) {
        goto doc_end;
    }
    ctn = ctn_parent;
    ctn_len = (sky_usize_t) (ctn->tag >> SKY_JSON_TAG_BIT);
    if (*cur == '\n') cur++;
    if ((ctn->tag & SKY_JSON_TYPE_MASK) == SKY_JSON_TYPE_OBJ) {
        goto obj_val_end;
    } else {
        goto arr_val_end;
    }

    doc_end:
    /* check invalid contents after json document */
    if (sky_unlikely(cur < end) && !has_flag(SKY_JSON_READ_STOP_WHEN_DONE)) {
        if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
            skip_spaces_and_comments(&cur);
        } else {
            while (char_is_space(*cur)) {
                ++cur;
            }
        }
        if (sky_unlikely(cur < end)) {
            goto fail_garbage;
        }
    }

    if (pre && *pre) {
        **pre = '\0';
    }
    sky_json_doc_t *doc = (sky_json_doc_t *) val_hdr;
    doc->root = val_hdr + hdr_len;
    doc->read_n = (sky_usize_t) (cur - hdr);
    doc->val_read_n = (sky_usize_t) ((val - val_hdr)) - hdr_len + 1;
    doc->str_pool = has_flag(SKY_JSON_READ_IN_SITU) ? null : hdr;
    return doc;

    fail_string:
    sky_free(val_hdr);
    return null;
    fail_number:
    sky_free(val_hdr);
    return null;

    fail_alloc:
    sky_log_error("memory allocation failed");
    return null;

    fail_trailing_comma:
    sky_free(val_hdr);
    sky_log_error("trailing comma is not allowed");
    return null;

    fail_literal:
    sky_free(val_hdr);
    sky_log_error("invalid literal");
    return null;
    fail_comment:
    sky_free(val_hdr);
    sky_log_error("unclosed multiline comment");
    return null;
    fail_character:
    sky_free(val_hdr);
    sky_log_error("unexpected character");
    return null;
    fail_garbage:
    sky_free(val_hdr);
    sky_log_error("unexpected content after document");
    return null;

#undef val_incr
#undef has_flag2
#undef has_flag
}

static sky_json_doc_t *
read_root_minify(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts) {
#define has_flag(_flag) sky_unlikely((opts & (_flag)) != 0)
#define has_flag2(_flag) ((opts & (_flag)) != 0)

#define val_incr() do { \
    ++val; \
    if (sky_unlikely(val >= val_end)) { \
        alc_len += alc_len >> 1; \
        if (sky_unlikely(alc_len >= alc_max)) goto fail_alloc; \
        val_tmp = (sky_json_val_t *) sky_realloc(val_hdr, alc_len * sizeof(sky_json_val_t)); \
        if (sky_unlikely(!val_tmp)) goto fail_alloc; \
        val = val_tmp + (sky_usize_t)(val - val_hdr); \
        ctn = val_tmp + (sky_usize_t)(ctn - val_hdr); \
        val_hdr = val_tmp; \
        val_end = val_tmp + (alc_len - 2); \
    } \
} while (false)

    const sky_usize_t dat_len = has_flag(SKY_JSON_READ_STOP_WHEN_DONE) ? 256 : (sky_usize_t) (end - cur);
    const sky_usize_t alc_max = SKY_USIZE_MAX / sizeof(sky_json_val_t);

    sky_usize_t hdr_len = sizeof(sky_json_doc_t) / sizeof(sky_json_val_t);
    hdr_len += (sizeof(sky_json_doc_t) % sizeof(sky_json_val_t)) > 0;

    sky_usize_t alc_len = hdr_len + (dat_len / READER_ESTIMATED_PRETTY_RATIO) + 4;
    alc_len = sky_min(alc_len, alc_max);

    sky_json_val_t *val_hdr = sky_malloc(alc_len * sizeof(sky_json_val_t));
    if (sky_unlikely(!val_hdr)) {
        goto fail_alloc;
    }
    sky_json_val_t *val_end = val_hdr + (alc_len - 2); /* padding for key-value pair reading */
    sky_json_val_t *val = val_hdr + hdr_len;

    sky_json_val_t *val_tmp, *ctn_parent;
    sky_json_val_t *ctn = val;
    sky_usize_t ctn_len = 0;

    const sky_bool_t raw = has_flag2(SKY_JSON_READ_NUMBER_AS_RAW);
    const sky_bool_t ext = has_flag2(SKY_JSON_READ_ALLOW_INF_AND_NAN);
    const sky_bool_t inv = has_flag2(SKY_JSON_READ_ALLOW_INVALID_UNICODE);

    sky_uchar_t *raw_end = null; /* raw end for null-terminator */
    sky_uchar_t **pre = raw ? &raw_end : null; /* previous raw end pointer */

    if (*cur++ == '{') {
        ctn->tag = SKY_JSON_TYPE_OBJ;
        ctn->ofs = 0;
        goto obj_key_begin;
    } else {
        ctn->tag = SKY_JSON_TYPE_ARR;
        ctn->ofs = 0;
        goto arr_val_begin;
    }


    arr_begin:
    /* save current container */
    ctn->tag = (((sky_u64_t) ctn_len + 1) << SKY_JSON_TAG_BIT) | (ctn->tag & SKY_JSON_TAG_MASK);

    /* create a new array value, save parent container offset */
    val_incr();
    val->tag = SKY_JSON_TYPE_ARR;
    val->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);

    /* push the new array value as current container */
    ctn = val;
    ctn_len = 0;

    arr_val_begin:

    if (*cur == '{') {
        ++cur;
        goto obj_begin;
    }
    if (*cur == '[') {
        ++cur;
        goto arr_begin;
    }
    if (char_is_number(*cur)) {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_number(&cur, pre, val, ext))) {
            goto arr_val_end;
        }
        goto fail_number;
    }
    if (*cur == '"') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_string(&cur, end, val, inv))) {
            goto arr_val_end;
        }
        goto fail_string;
    }
    if (*cur == 't') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_true(&cur, val))) {
            goto arr_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'f') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_false(&cur, val))) {
            goto arr_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'n') {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_null(&cur, val))) {
            goto arr_val_end;
        }
        if (sky_likely(ext)) {
            if (read_nan(&cur, pre, val, false)) {
                goto arr_val_end;
            }
        }
        goto fail_literal;
    }
    if (*cur == ']') {
        ++cur;
        if (sky_likely(ctn_len == 0)) {
            goto arr_end;
        }
        if (has_flag(SKY_JSON_READ_ALLOW_TRAILING_COMMAS)) {
            goto arr_end;
        }
        goto fail_trailing_comma;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto arr_val_begin;
    }
    if (sky_likely(ext) && (*cur == 'i' || *cur == 'I' || *cur == 'N')) {
        val_incr();
        ++ctn_len;
        if (read_inf_or_nan(&cur, pre, val, false)) {
            goto arr_val_end;
        }
        goto fail_character;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto arr_val_begin;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    arr_val_end:

    if (*cur == ',') {
        ++cur;
        goto arr_val_begin;
    }
    if (*cur == ']') {
        ++cur;
        goto arr_end;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto arr_val_end;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto arr_val_end;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    arr_end:

    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->ofs);
    ctn->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
    ctn->tag = (ctn_len << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_ARR;
    if (sky_unlikely(ctn == ctn_parent)) {
        goto doc_end;
    }

    /* pop parent as current container */
    ctn = ctn_parent;
    ctn_len = (sky_usize_t) (ctn->tag >> SKY_JSON_TAG_BIT);
    if ((ctn->tag & SKY_JSON_TYPE_MASK) == SKY_JSON_TYPE_OBJ) {
        goto obj_val_end;
    } else {
        goto arr_val_end;
    }

    obj_begin:
    /* push container */
    ctn->tag = (((sky_u64_t) ctn_len + 1) << SKY_JSON_TAG_BIT) |
               (ctn->tag & SKY_JSON_TAG_MASK);
    val_incr();
    val->tag = SKY_JSON_TYPE_OBJ;
    /* offset to the parent */
    val->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);
    ctn = val;
    ctn_len = 0;

    obj_key_begin:

    if (sky_likely(*cur == '"')) {
        val_incr();
        ++ctn_len;
        if (sky_likely(read_string(&cur, end, val, inv))) {
            goto obj_key_end;
        }
        goto fail_string;
    }
    if (sky_likely(*cur == '}')) {
        ++cur;
        if (sky_likely(!ctn_len)) {
            goto obj_end;
        }
        if (has_flag(SKY_JSON_READ_ALLOW_TRAILING_COMMAS)) {
            goto obj_end;
        }
        goto fail_trailing_comma;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_key_begin;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_key_begin;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_key_end:

    if (*cur == ':') {
        ++cur;
        goto obj_val_begin;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_key_end;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_key_end;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_val_begin:

    if (*cur == '"') {
        ++val;
        ctn_len++;
        if (sky_likely(read_string(&cur, end, val, inv))) {
            goto obj_val_end;
        }
        goto fail_string;
    }
    if (char_is_number(*cur)) {
        ++val;
        ++ctn_len;
        if (sky_likely(read_number(&cur, pre, val, ext))) {
            goto obj_val_end;
        }
        goto fail_number;
    }
    if (*cur == '{') {
        ++cur;
        goto obj_begin;
    }
    if (*cur == '[') {
        ++cur;
        goto arr_begin;
    }
    if (*cur == 't') {
        ++val;
        ++ctn_len;
        if (sky_likely(read_true(&cur, val))) {
            goto obj_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'f') {
        ++val;
        ++ctn_len;
        if (sky_likely(read_false(&cur, val))) {
            goto obj_val_end;
        }
        goto fail_literal;
    }
    if (*cur == 'n') {
        ++val;
        ++ctn_len;
        if (sky_likely(read_null(&cur, val))) {
            goto obj_val_end;
        }
        if (sky_unlikely(ext)) {
            if (read_nan(&cur, pre, val, false)) {
                goto obj_val_end;
            }
        }
        goto fail_literal;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_val_begin;
    }
    if (sky_unlikely(ext) && (*cur == 'i' || *cur == 'I' || *cur == 'N')) {
        ++val;
        ++ctn_len;
        if (read_inf_or_nan(&cur, pre, val, false)) {
            goto obj_val_end;
        }
        goto fail_character;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_val_begin;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_val_end:

    if (sky_likely(*cur == ',')) {
        ++cur;
        goto obj_key_begin;
    }
    if (sky_likely(*cur == '}')) {
        ++cur;
        goto obj_end;
    }
    if (char_is_space(*cur)) {
        while (char_is_space(*++cur));
        goto obj_val_end;
    }
    if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
        if (skip_spaces_and_comments(&cur)) {
            goto obj_val_end;
        }
        if (sky_str2_cmp(cur, '/', '*')) {
            goto fail_comment;
        }
    }
    goto fail_character;

    obj_end:

    /* pop container */
    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->ofs);
    /* point to the next value */
    ctn->ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
    ctn->tag = (ctn_len << (SKY_JSON_TAG_BIT - 1)) | SKY_JSON_TYPE_OBJ;
    if (sky_unlikely(ctn == ctn_parent)) {
        goto doc_end;
    }
    ctn = ctn_parent;
    ctn_len = (sky_usize_t) (ctn->tag >> SKY_JSON_TAG_BIT);
    if ((ctn->tag & SKY_JSON_TYPE_MASK) == SKY_JSON_TYPE_OBJ) {
        goto obj_val_end;
    } else {
        goto arr_val_end;
    }

    doc_end:
    /* check invalid contents after json document */
    if (sky_unlikely(cur < end) && !has_flag(SKY_JSON_READ_STOP_WHEN_DONE)) {
        if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
            skip_spaces_and_comments(&cur);
        } else {
            while (char_is_space(*cur)) {
                ++cur;
            }
        }
        if (sky_unlikely(cur < end)) {
            goto fail_garbage;
        }
    }

    if (pre && *pre) {
        **pre = '\0';
    }
    sky_json_doc_t *doc = (sky_json_doc_t *) val_hdr;
    doc->root = val_hdr + hdr_len;
    doc->read_n = (sky_usize_t) (cur - hdr);
    doc->val_read_n = (sky_usize_t) ((val - val_hdr)) - hdr_len + 1;
    doc->str_pool = has_flag(SKY_JSON_READ_IN_SITU) ? null : hdr;
    return doc;

    fail_string:
    sky_free(val_hdr);
    return null;
    fail_number:
    sky_free(val_hdr);
    return null;

    fail_alloc:
    sky_log_error("memory allocation failed");
    return null;

    fail_trailing_comma:
    sky_free(val_hdr);
    sky_log_error("trailing comma is not allowed");
    return null;

    fail_literal:
    sky_free(val_hdr);
    sky_log_error("invalid literal");
    return null;
    fail_comment:
    sky_free(val_hdr);
    sky_log_error("unclosed multiline comment");
    return null;
    fail_character:
    sky_free(val_hdr);
    sky_log_error("unexpected character");
    return null;
    fail_garbage:
    sky_free(val_hdr);
    sky_log_error("unexpected content after document");
    return null;

#undef val_incr
#undef has_flag2
#undef has_flag
}

static sky_json_doc_t *
read_root_single(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts) {
#define has_flag(_flag) sky_unlikely((opts & (_flag)) != 0)
#define has_flag2(_flag) ((opts & (_flag)) != 0)


    sky_usize_t hdr_len = sizeof(sky_json_doc_t) / sizeof(sky_json_val_t);
    hdr_len += (sizeof(sky_json_doc_t) % sizeof(sky_json_val_t)) > 0;

    sky_usize_t alc_num = hdr_len + 1;

    sky_json_val_t *val_hdr = sky_malloc(alc_num * sizeof(sky_json_val_t));
    if (sky_unlikely(!val_hdr)) {
        goto fail_alloc;
    }
    sky_json_val_t *val = val_hdr + hdr_len;

    const sky_bool_t raw = has_flag2(SKY_JSON_READ_NUMBER_AS_RAW);
    const sky_bool_t ext = has_flag2(SKY_JSON_READ_ALLOW_INF_AND_NAN);
    const sky_bool_t inv = has_flag2(SKY_JSON_READ_ALLOW_INVALID_UNICODE);

    sky_uchar_t *raw_end = null; /* raw end for null-terminator */
    sky_uchar_t **pre = raw ? &raw_end : null; /* previous raw end pointer */


    if (char_is_number(*cur)) {
        if (sky_likely(read_number(&cur, pre, val, ext))) goto doc_end;
        goto fail_number;
    }
    if (*cur == '"') {
        if (sky_likely(read_string(&cur, end, val, inv))) goto doc_end;
        goto fail_string;
    }
    if (*cur == 't') {
        if (sky_likely(read_true(&cur, val))) goto doc_end;
        goto fail_literal;
    }
    if (*cur == 'f') {
        if (sky_likely(read_false(&cur, val))) goto doc_end;
        goto fail_literal;
    }
    if (*cur == 'n') {
        if (sky_likely(read_null(&cur, val))) goto doc_end;
        if (sky_unlikely(ext)) {
            if (read_nan(&cur, pre, val, false)) goto doc_end;
        }
        goto fail_literal;
    }
    if (sky_unlikely(ext)) {
        if (read_inf_or_nan(&cur, pre, val, false)) goto doc_end;
    }
    goto fail_character;

    doc_end:
    /* check invalid contents after json document */
    if (sky_unlikely(cur < end) && !has_flag(SKY_JSON_READ_STOP_WHEN_DONE)) {
        if (has_flag(SKY_JSON_READ_ALLOW_COMMENTS)) {
            if (!skip_spaces_and_comments(&cur)) {
                if (sky_str2_cmp(cur, '/', '*')) {
                    goto fail_comment;
                }
            }
        } else {
            while (char_is_space(*cur)) {
                ++cur;
            }
        }
        if (sky_unlikely(cur < end)) {
            goto fail_garbage;
        }
    }

    if (pre && *pre) {
        **pre = '\0';
    }
    sky_json_doc_t *doc = (sky_json_doc_t *) val_hdr;
    doc->root = val_hdr + hdr_len;
    doc->read_n = (sky_usize_t) (cur - hdr);
    doc->val_read_n = 1;
    doc->str_pool = has_flag(SKY_JSON_READ_IN_SITU) ? null : hdr;

    return doc;

    fail_string:
    sky_free(val_hdr);
    return null;

    fail_number:
    sky_free(val_hdr);
    return null;

    fail_alloc:
    sky_free(val_hdr);
    sky_log_error("memory allocation failed");
    return null;

    fail_literal:
    sky_free(val_hdr);
    sky_log_error("invalid literal");
    return null;

    fail_comment:
    sky_free(val_hdr);
    sky_log_error("unclosed multiline comment");
    return null;

    fail_character:
    sky_free(val_hdr);
    sky_log_error("unexpected character");
    return null;

    fail_garbage:
    sky_free(val_hdr);
    sky_log_error("unexpected content after document");
    return null;


#undef has_flag2
#undef has_flag
}

/**
 * Read a JSON number.
    1. This function assume that the floating-point number is in IEEE-754 format.
    2. This function support uint64/int64/double number. If an integer number
       cannot fit in uint64/int64, it will returns as a double number. If a double
       number is infinite, the return value is based on flag.
    3. This function (with inline attribute) may generate a lot of instructions.
 * @param ptr ptr
 * @param pre pre
 * @param val val
 * @param ext ext
 * @return Whether success.
 */
static sky_bool_t
read_number(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t ext) {

    return false;
}

/**
 Read a JSON string.
 @param ptr The head pointer of string before '"' prefix (inout).
 @param lst JSON last position.
 @param val The string value to be written.
 @param inv Allow invalid unicode.
 @return Whether success.
 */
static sky_bool_t
read_string(sky_uchar_t **ptr, sky_uchar_t *lst, sky_json_val_t *val, sky_bool_t inv) {
    /*
 Each unicode code point is encoded as 1 to 4 bytes in UTF-8 encoding,
 we use 4-byte mask and pattern value to validate UTF-8 byte sequence,
 this requires the input data to have 4-byte zero padding.
 ---------------------------------------------------
 1 byte
 unicode range [U+0000, U+007F]
 unicode min   [.......0]
 unicode max   [.1111111]
 bit pattern   [0.......]
 ---------------------------------------------------
 2 byte
 unicode range [U+0080, U+07FF]
 unicode min   [......10 ..000000]
 unicode max   [...11111 ..111111]
 bit require   [...xxxx. ........] (1E 00)
 bit mask      [xxx..... xx......] (E0 C0)
 bit pattern   [110..... 10......] (C0 80)
 ---------------------------------------------------
 3 byte
 unicode range [U+0800, U+FFFF]
 unicode min   [........ ..100000 ..000000]
 unicode max   [....1111 ..111111 ..111111]
 bit require   [....xxxx ..x..... ........] (0F 20 00)
 bit mask      [xxxx.... xx...... xx......] (F0 C0 C0)
 bit pattern   [1110.... 10...... 10......] (E0 80 80)
 ---------------------------------------------------
 3 byte invalid (reserved for surrogate halves)
 unicode range [U+D800, U+DFFF]
 unicode min   [....1101 ..100000 ..000000]
 unicode max   [....1101 ..111111 ..111111]
 bit mask      [....xxxx ..x..... ........] (0F 20 00)
 bit pattern   [....1101 ..1..... ........] (0D 20 00)
 ---------------------------------------------------
 4 byte
 unicode range [U+10000, U+10FFFF]
 unicode min   [........ ...10000 ..000000 ..000000]
 unicode max   [.....100 ..001111 ..111111 ..111111]
 bit require   [.....xxx ..xx.... ........ ........] (07 30 00 00)
 bit mask      [xxxxx... xx...... xx...... xx......] (F8 C0 C0 C0)
 bit pattern   [11110... 10...... 10...... 10......] (F0 80 80 80)
 ---------------------------------------------------
 */

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define b1_mask SKY_U32(0x00000080)
#define b1_patt SKY_U32(0x00000000)
#define b2_mask SKY_U32(0x0000C0E0)
#define b2_patt SKY_U32(0x000080C0)
#define b2_requ SKY_U32(0x0000001E)
#define b3_mask SKY_U32(0x00C0C0F0)
#define b3_patt SKY_U32(0x008080E0)
#define b3_requ SKY_U32(0x0000200F)
#define b3_erro SKY_U32(0x0000200D)
#define b4_mask SKY_U32(0xC0C0C0F8)
#define b4_patt SKY_U32(0x808080F0)
#define b4_requ SKY_U32(0x00003007)
#define b4_err0 SKY_U32(0x00000004)
#define b4_err1 SKY_U32(0x00003003)
#else
#define b1_mask SKY_U32(0x80000000)
#define b1_patt SKY_U32(0x00000000)
#define b2_mask SKY_U32(0xE0C00000)
#define b2_patt SKY_U32(0xC0800000)
#define b2_requ SKY_U32(0x1E000000)
#define b3_mask SKY_U32(0xF0C0C000)
#define b3_patt SKY_U32(0xE0808000)
#define b3_requ SKY_U32(0x0F200000)
#define b3_erro SKY_U32(0x0D200000)
#define b4_mask SKY_U32(0xF8C0C0C0)
#define b4_patt SKY_U32(0xF0808080)
#define b4_requ SKY_U32(0x07300000)
#define b4_err0 SKY_U32(0x04000000)
#define b4_err1 SKY_U32(0x03300000)
#endif

#define is_valid_seq_1(_uni) (((_uni) & b1_mask) == b1_patt)

#define is_valid_seq_2(_uni) ( \
    (((_uni) & b2_mask) == b2_patt) && \
    (((_uni) & b2_requ)) \
)

#define is_valid_seq_3(_uni) ( \
    (((_uni) & b3_mask) == b3_patt) && \
    ((tmp = ((_uni) & b3_requ))) && \
    ((tmp != b3_erro)) \
)

#define is_valid_seq_4(_uni) ( \
    (((_uni) & b4_mask) == b4_patt) && \
    ((tmp = ((_uni) & b4_requ))) &&    \
    ((tmp & b4_err0) == 0 || (tmp & b4_err1) == 0) \
)

#define return_err(_end, _msg) do { \
    *end = _end; \
    return false; \
} while (false)

    sky_uchar_t *cur = *ptr;
    sky_uchar_t **end = ptr;
    sky_uchar_t *src = ++cur;

    sky_uchar_t *dst, *pos;
    sky_u16_t hi, lo;
    sky_u32_t uni, tmp;

    skip_ascii:
    /* Most strings have no escaped characters, so we can jump them quickly. */

    skip_ascii_begin:
    /*
     We want to make loop unrolling, as shown in the following code. Some
     compiler may not generate instructions as expected, so we rewrite it with
     explicit goto statements. We hope the compiler can generate instructions
     like this: https://godbolt.org/z/8vjsYq

         while (true) repeat16({
            if (likely(!(char_is_ascii_stop(*src)))) src++;
            else break;
         });
     */
#define expr_jump(i) \
    if (sky_unlikely(char_is_ascii_stop(src[i]))) { \
        goto skip_ascii_stop##i; \
    }

#define expr_stop(i) \
    skip_ascii_stop##i: \
    src += (i);      \
    goto skip_ascii_end;


    repeat16_incr(expr_jump);
    src += 16;
    goto skip_ascii_begin;
    repeat16_incr(expr_stop);

    skip_ascii_end:

    if (sky_likely(*src == '"')) {
        val->tag = ((sky_u64_t) (src - cur) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_STR;
        val->str = cur;
        *src = '\0';
        *end = src + 1;
        return true;
    }

#undef expr_jump
#undef expr_stop

    skip_utf8:
    if (*src & 0x80) { /* non-ASCII character */
        /*
         Non-ASCII character appears here, which means that the text is likely
         to be written in non-English or emoticons. According to some common
         data set statistics, byte sequences of the same length may appear
         consecutively. We process the byte sequences of the same length in each
         loop, which is more friendly to branch prediction.
         */
        pos = src;
        uni = *(sky_u32_t *) src;
        while (is_valid_seq_3(uni)) {
            src += 3;
            uni = *(sky_u32_t *) src;
        }
        if (is_valid_seq_1(uni)) {
            goto skip_ascii;
        }
        while (is_valid_seq_2(uni)) {
            src += 2;
            uni = *(sky_u32_t *) src;
        }
        while (is_valid_seq_4(uni)) {
            src += 4;
            uni = *(sky_u32_t *) src;
        }
        if (sky_unlikely(pos == src)) {
            if (!inv) {
                return_err(src, "invalid UTF-8 encoding in string");
            }
            ++src;
        }
        goto skip_ascii;
    }

    /* The escape character appears, we need to copy it. */
    dst = src;

    copy_escape:
    if (sky_likely(*src == '\\')) {
        switch (*++src) {
            case '"':
                *dst++ = '"';
                ++src;
                break;
            case '\\':
                *dst++ = '\\';
                ++src;
                break;
            case '/':
                *dst++ = '/';
                ++src;
                break;
            case 'b':
                *dst++ = '\b';
                ++src;
                break;
            case 'f':
                *dst++ = '\f';
                ++src;
                break;
            case 'n':
                *dst++ = '\n';
                ++src;
                break;
            case 'r':
                *dst++ = '\r';
                ++src;
                break;
            case 't':
                *dst++ = '\t';
                ++src;
                break;
            case 'u':
                if (sky_unlikely(!read_hex_u16(++src, &hi))) {
                    return_err(src - 2, "invalid escaped unicode in string");
                }
                src += 4;
                if (sky_unlikely((hi & 0xF800) != 0xD800)) {
                    /* a BMP character */
                    if (hi >= 0x800) {
                        *dst++ = (sky_uchar_t) (0xE0 | (hi >> 12));
                        *dst++ = (sky_uchar_t) (0x80 | ((hi >> 6) & 0x3F));
                        *dst++ = (sky_uchar_t) (0x80 | (hi & 0x3F));
                    } else if (hi >= 0x80) {
                        *dst++ = (sky_uchar_t) (0xC0 | (hi >> 6));
                        *dst++ = (sky_uchar_t) (0x80 | (hi & 0x3F));
                    } else {
                        *dst++ = (sky_uchar_t) hi;
                    }
                } else {
                    /* a non-BMP character, represented as a surrogate pair */
                    if (sky_unlikely((hi & 0xFC00) != 0xD800)) {
                        return_err(src - 6, "invalid high surrogate in string");
                    }
                    if (sky_unlikely(!sky_str2_cmp(src, '\\', 'u')) ||
                        sky_unlikely(!read_hex_u16(src + 2, &lo))) {
                        return_err(src, "no matched low surrogate in string");
                    }
                    if (sky_unlikely((lo & 0xFC00) != 0xDC00)) {
                        return_err(src, "invalid low surrogate in string");
                    }
                    uni = ((((sky_u32_t) hi - 0xD800) << 10) | ((sky_u32_t) lo - 0xDC00)) + 0x10000;
                    *dst++ = (sky_uchar_t) (0xF0 | (uni >> 18));
                    *dst++ = (sky_uchar_t) (0x80 | ((uni >> 12) & 0x3F));
                    *dst++ = (sky_uchar_t) (0x80 | ((uni >> 6) & 0x3F));
                    *dst++ = (sky_uchar_t) (0x80 | (uni & 0x3F));
                    src += 6;
                }
                break;
            default:
                return_err(src, "invalid escaped character in string");
        }
    } else if (sky_likely(*src == '"')) {
        val->tag = ((sky_u64_t) (dst - cur) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_STR;
        val->str = cur;
        *dst = '\0';
        *end = src + 1;
        return true;
    } else {
        if (!inv) return_err(src, "unexpected control character in string");
        if (src >= lst) return_err(src, "unclosed string");
        *dst++ = *src++;
    }

    copy_ascii:
    /*
     Copy continuous ASCII, loop unrolling, same as the following code:

         while (true) repeat16({
            if (unlikely(char_is_ascii_stop(*src))) break;
            *dst++ = *src++;
         });
     */
#define expr_jump(i) \
    if (sky_unlikely(char_is_ascii_stop(src[i]))) { \
        goto copy_ascii_stop_##i; \
    }
    repeat16_incr(expr_jump);
#undef expr_jump

    sky_memmove(dst, src, 16);
    src += 16;
    dst += 16;
    goto copy_ascii;

    copy_ascii_stop_0:

    goto copy_utf8;

    copy_ascii_stop_1:

    sky_memmove2(dst, src);
    src += 1;
    dst += 1;
    goto copy_utf8;

    copy_ascii_stop_2:
    sky_memmove2(dst, src);
    src += 2;
    dst += 2;
    goto copy_utf8;

    copy_ascii_stop_3:

    sky_memmove4(dst, src);
    src += 3;
    dst += 3;
    goto copy_utf8;

    copy_ascii_stop_4:

    sky_memmove4(dst, src);
    src += 4;
    dst += 4;
    goto copy_utf8;

    copy_ascii_stop_5:

    sky_memmove4(dst, src);
    sky_memmove2(dst + 4, src + 4);
    src += 5;
    dst += 5;
    goto copy_utf8;

    copy_ascii_stop_6:

    sky_memmove4(dst, src);
    sky_memmove2(dst + 4, src + 4);
    src += 6;
    dst += 6;
    goto copy_utf8;

    copy_ascii_stop_7:

    sky_memmove8(dst, src);
    src += 7;
    dst += 7;
    goto copy_utf8;

    copy_ascii_stop_8:

    sky_memmove8(dst, src);
    src += 8;
    dst += 8;
    goto copy_utf8;

    copy_ascii_stop_9:

    sky_memmove8(dst, src);
    sky_memmove2(dst + 8, src + 8);
    src += 9;
    dst += 9;
    goto copy_utf8;

    copy_ascii_stop_10:

    sky_memmove8(dst, src);
    sky_memmove2(dst + 8, src + 8);
    src += 10;
    dst += 10;
    goto copy_utf8;

    copy_ascii_stop_11:

    sky_memmove8(dst, src);
    sky_memmove4(dst + 8, src + 8);
    src += 11;
    dst += 11;
    goto copy_utf8;

    copy_ascii_stop_12:

    sky_memmove8(dst, src);
    sky_memmove4(dst + 8, src + 8);
    src += 12;
    dst += 12;
    goto copy_utf8;

    copy_ascii_stop_13:

    sky_memmove8(dst, src);
    sky_memmove4(dst + 8, src + 8);
    sky_memmove2(dst + 12, src + 12);
    src += 13;
    dst += 13;
    goto copy_utf8;

    copy_ascii_stop_14:

    sky_memmove8(dst, src);
    sky_memmove4(dst + 8, src + 8);
    sky_memmove2(dst + 12, src + 12);
    src += 14;
    dst += 14;
    goto copy_utf8;

    copy_ascii_stop_15:

    sky_memmove(dst, src, 16);
    src += 15;
    dst += 15;
    goto copy_utf8;

    copy_utf8:

    if (*src & 0x80) { /* non-ASCII character */
        pos = src;
        uni = *(sky_u32_t *) src;
        while (is_valid_seq_3(uni)) {
            sky_memmove4(dst, &uni);
            dst += 3;
            src += 3;
            uni = *(sky_u32_t *) src;
        }
        if (is_valid_seq_1(uni)) {
            goto copy_ascii;
        }
        while (is_valid_seq_2(uni)) {
            sky_memmove2(dst, &uni);
            dst += 2;
            src += 2;
            uni = *(sky_u32_t *) src;
        }
        while (is_valid_seq_4(uni)) {
            sky_memmove2(dst, &uni);
            dst += 4;
            src += 4;
            uni = *(sky_u32_t *) src;
        }
        if (sky_unlikely(pos == src)) {
            if (!inv) {
                return_err(src, "invalid UTF-8 encoding in string");
            }
            goto copy_ascii_stop_1;
        }
        goto copy_ascii;
    }
    goto copy_escape;

#undef b1_mask
#undef b1_patt
#undef b2_mask
#undef b2_patt
#undef b2_requ
#undef b3_mask
#undef b3_patt
#undef b3_requ
#undef b3_erro
#undef b4_mask
#undef b4_patt
#undef b4_requ
#undef b4_err0
#undef b4_err1

#undef return_err
#undef is_valid_seq_4
#undef is_valid_seq_3
#undef is_valid_seq_2
}

static sky_inline sky_bool_t
read_true(sky_uchar_t **ptr, sky_json_val_t *val) {
    const sky_uchar_t *cur = *ptr;

    if (sky_likely(sky_str4_cmp(cur, 't', 'r', 'u', 'e'))) {
        val->tag = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_TRUE;
        *ptr += 4;

        return true;
    }
    return false;
}

static sky_inline sky_bool_t
read_false(sky_uchar_t **ptr, sky_json_val_t *val) {
    const sky_uchar_t *cur = *ptr + 1;

    if (sky_likely(sky_str4_cmp(cur, 'a', 'l', 's', 'e'))) {
        val->tag = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_FALSE;
        *ptr += 5;

        return true;
    }
    return false;
}

static sky_inline sky_bool_t
read_null(sky_uchar_t **ptr, sky_json_val_t *val) {
    const sky_uchar_t *cur = *ptr;

    if (sky_likely(sky_str4_cmp(cur, 'n', 'u', 'l', 'l'))) {
        val->tag = SKY_JSON_TYPE_NULL;
        *ptr += 4;

        return true;
    }
    return false;
}

static sky_inline sky_bool_t
read_inf(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign) {
    sky_uchar_t *hdr = *ptr - sign;
    sky_uchar_t *cur = *ptr;
    sky_uchar_t **end = ptr;
    if ((cur[0] == 'I' || cur[0] == 'i') &&
        (cur[1] == 'N' || cur[1] == 'n') &&
        (cur[2] == 'F' || cur[2] == 'f')) {
        if ((cur[3] == 'I' || cur[3] == 'i') &&
            (cur[4] == 'N' || cur[4] == 'n') &&
            (cur[5] == 'I' || cur[5] == 'i') &&
            (cur[6] == 'T' || cur[6] == 't') &&
            (cur[7] == 'Y' || cur[7] == 'y')) {
            cur += 8;
        } else {
            cur += 3;
        }
        *end = cur;
        if (pre) {
            /* add null-terminator for previous raw string */
            if (*pre) {
                **pre = '\0';
            }
            *pre = cur;
            val->tag = ((sky_u64_t) (cur - hdr) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_RAW;
            val->str = hdr;
        } else {
            val->tag = SKY_JSON_TYPE_NUM | SKY_JSON_SUBTYPE_REAL;
            val->u64 = f64_raw_get_inf(sign);
        }
        return true;
    }

    return false;
}

static sky_inline sky_bool_t
read_nan(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign) {
    sky_uchar_t *hdr = *ptr - sign;
    sky_uchar_t *cur = *ptr;
    sky_uchar_t **end = ptr;
    if ((cur[0] == 'N' || cur[0] == 'n') &&
        (cur[1] == 'A' || cur[1] == 'a') &&
        (cur[2] == 'N' || cur[2] == 'n')) {
        cur += 3;
        *end = cur;
        if (pre) {
            /* add null-terminator for previous raw string */
            if (*pre) **pre = '\0';
            *pre = cur;
            val->tag = ((sky_u64_t) (cur - hdr) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_RAW;
            val->str = hdr;
        } else {
            val->tag = SKY_JSON_TYPE_NUM | SKY_JSON_SUBTYPE_REAL;
            val->u64 = f64_raw_get_nan(sign);
        }
        return true;
    }
    return false;
}

static sky_inline sky_bool_t
read_inf_or_nan(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign) {
    if (read_inf(ptr, pre, val, sign)) {
        return true;
    }
    if (read_nan(ptr, pre, val, sign)) {
        return true;
    }
    return false;
}

/**
 *  Scans an escaped character sequence as a UTF-16 code unit (branchless).
    e.g. "\\u005C" should pass "005C" as `cur`.

    This requires the string has 4-byte zero padding.
 * @param cur  str_ptr
 * @param val val
 * @return suceess
 */
static sky_inline sky_bool_t
read_hex_u16(const sky_uchar_t *cur, sky_u16_t *val) {
    /**
        This table is used to convert 4 hex character sequence to a number.
        A valid hex character [0-9A-Fa-f] will mapped to it's raw number [0x00, 0x0F],
        an invalid hex character will mapped to [0xF0].
        (generate with misc/make_tables.c)
 */
    static const sky_uchar_t hex_conv_table[256] = {
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
            0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0
    };

    const sky_u16_t c0 = hex_conv_table[cur[0]];
    const sky_u16_t c1 = hex_conv_table[cur[1]];
    const sky_u16_t c2 = hex_conv_table[cur[2]];
    const sky_u16_t c3 = hex_conv_table[cur[3]];
    const sky_u16_t t0 = (sky_u16_t) ((c0 << 8) | c2);
    const sky_u16_t t1 = (sky_u16_t) ((c1 << 8) | c3);

    *val = (sky_u16_t) ((t0 << 4) | t1);
    return ((t0 | t1) & (sky_u16_t) 0xF0F0) == 0;
}

// ================================ old version =======================================

#if defined(__AVX2__)

#include <immintrin.h>

#elif defined(__SSE4_2__)

#include <nmmintrin.h>

#elif defined(__SSE3__)

#include <tmmintrin.h>

#endif

#define NEXT_OBJECT_START   0x100
#define NEXT_ARRAY_START    0x80
#define NEXT_OBJECT_END     0x40
#define NEXT_ARRAY_END      0x20
#define NEXT_KEY            0x10
#define NEXT_KEY_VALUE      0x8
#define NEXT_OBJECT_VALUE   0x4
#define NEXT_ARRAY_VALUE    0x2
#define NEXT_NODE           0x1
#define NEXT_NONE           0

static void parse_whitespace(sky_uchar_t **ptr);

static sky_bool_t parse_string(sky_str_t *str, sky_uchar_t **ptr, sky_uchar_t *end);

static sky_bool_t parse_number(sky_json_t *json, sky_uchar_t **ptr);

static sky_bool_t backslash_parse(sky_uchar_t **ptr, sky_uchar_t **post);

static sky_json_t *parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end);

static sky_json_object_t *json_object_get(sky_json_t *json);

static sky_json_t *json_array_get(sky_json_t *json);

static void json_object_init(sky_json_t *json, sky_pool_t *pool);

static void json_array_init(sky_json_t *json, sky_pool_t *pool);

static void json_coding_str(sky_str_buf_t *buf, const sky_uchar_t *v, sky_usize_t v_len);


sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    sky_uchar_t *p = json->data;
    return parse_loop(pool, p, p + json->len);
}

sky_str_t *sky_json_tostring(sky_json_t *json) {
    static const sky_str_t BOOLEAN_TABLE[] = {
            sky_string("false"),
            sky_string("true")
    };

    sky_json_object_t *obj;
    sky_json_t *current, *tmp;

    if (json->type == json_object) {
        if (json->object == json->object->prev) {
            sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
            sky_str_set(str, "{}");
            return str;
        }
    } else if (json->type == json_array) {
        if (json->array == json->array->prev) {
            sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
            sky_str_set(str, "[]");
            return str;
        }
    } else {
        return null;
    }
    sky_str_buf_t buf;

    sky_str_buf_init2(&buf, json->pool, 32);

    current = json;

    for (;;) {
        switch (current->type) {
            case json_object: {
                if (current->object == current->object->prev) {
                    sky_str_buf_append_two_uchar(&buf, '{', '}');
                    break;
                }
                obj = current->object->next;
                current->current = obj;
                current = &obj->value;

                sky_str_buf_append_two_uchar(&buf, '{', '"');

                json_coding_str(&buf, obj->key.data, obj->key.len);

                sky_str_buf_append_two_uchar(&buf, '"', ':');
                continue;
            }
            case json_array: {
                if (current->array == current->array->prev) {
                    sky_str_buf_append_two_uchar(&buf, '[', ']');
                    break;
                }
                current->current = current->array->next;
                current = current->current;
                sky_str_buf_append_uchar(&buf, '[');
                continue;
            }
            case json_integer: {
                sky_str_buf_append_int64(&buf, current->integer);
                break;
            }
            case json_float: {
                break;
            }
            case json_string: {
                sky_str_buf_append_uchar(&buf, '"');
                json_coding_str(&buf, current->string.data, current->string.len);
                sky_str_buf_append_uchar(&buf, '"');
                break;
            }
            case json_boolean: {
                const sky_bool_t index = current->boolean != false;
                sky_str_buf_append_str(&buf, &BOOLEAN_TABLE[index]);
            }
                break;
            case json_null: {
                sky_str_buf_append_str_len(&buf, sky_str_line("null"));
                break;
            }
            default: {
                sky_str_buf_destroy(&buf);
                return null;
            }
        }
        tmp = current->parent;
        if (tmp->type == json_object) {
            tmp->current = ((sky_json_object_t *) tmp->current)->next;

            if (tmp->current != tmp->object) {
                obj = obj->next;
                current = &obj->value;

                sky_str_buf_append_two_uchar(&buf, ',', '"');
                json_coding_str(&buf, obj->key.data, obj->key.len);
                sky_str_buf_append_two_uchar(&buf, '"', ':');
                continue;
            }
            sky_str_buf_append_uchar(&buf, '}');
        } else {
            tmp->current = ((sky_json_array_t *) tmp->current)->next;
            if (tmp->current != tmp->array) {
                sky_str_buf_append_uchar(&buf, ',');

                current = &((sky_json_array_t *) current)->next->value;
                continue;
            }
            sky_str_buf_append_uchar(&buf, ']');
        }
        for (;;) {
            if (tmp == json) {
                sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
                sky_str_buf_build(&buf, str);

                return str;
            }

            tmp = tmp->parent;

            if (tmp->type == json_object) {
                tmp->current = ((sky_json_object_t *) tmp->current)->next;
                if (tmp->current == tmp->object) {
                    sky_str_buf_append_uchar(&buf, '}');
                    continue;
                }

                obj = (sky_json_object_t *) tmp->current;
                current = &obj->value;

                sky_str_buf_append_two_uchar(&buf, ',', '"');
                json_coding_str(&buf, obj->key.data, obj->key.len);
                sky_str_buf_append_two_uchar(&buf, '"', ':');
            } else {
                tmp->current = ((sky_json_array_t *) tmp->current)->next;
                if (tmp->current == tmp->array) {
                    sky_str_buf_append_uchar(&buf, ']');
                    continue;
                }
                sky_str_buf_append_uchar(&buf, ',');
                current = tmp->current;
            }
            break;
        }
    }
}


sky_json_t *
sky_json_find(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *object;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    object = json->object->next;
    while (object != json->object) {
        if (sky_str_equals2(&object->key, key, key_len)) {
            return &object->value;
        }
        object = object->next;
    }

    return null;
}

sky_json_t *
sky_json_put_object(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    json_object_init(&obj->value, json->pool);

    return &obj->value;
}

sky_json_t *
sky_json_put_array(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    json_array_init(&obj->value, json->pool);

    return &obj->value;
}

sky_json_t *
sky_json_put_boolean(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_bool_t value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_boolean;
    obj->value.boolean = value;

    return &obj->value;
}

sky_json_t *
sky_json_put_null(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_null;

    return &obj->value;
}

sky_json_t *
sky_json_put_integer(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_i64_t value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_integer;
    obj->value.integer = value;

    return &obj->value;
}

sky_json_t *
sky_json_put_double(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_f64_t value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_float;
    obj->value.dbl = value;

    return &obj->value;
}

sky_json_t *
sky_json_put_string(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_str_t *value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    if (!value) {
        obj->value.type = json_null;
    } else {
        obj->value.type = json_string;
        obj->value.string = *value;
    }

    return &obj->value;
}

sky_json_t *
sky_json_put_str_len(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_uchar_t *v, sky_u32_t v_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_string;
    obj->value.string.len = v_len;
    obj->value.string.data = v;

    return &obj->value;
}


sky_json_t *
sky_json_add_object(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    json_object_init(child, json->pool);

    return child;
}

sky_json_t *
sky_json_add_array(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    json_array_init(child, json->pool);

    return child;
}

sky_json_t *
sky_json_add_boolean(sky_json_t *json, sky_bool_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_boolean;
    child->boolean = value;

    return child;
}

sky_json_t *
sky_json_add_null(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_null;

    return child;
}

sky_json_t *
sky_json_add_integer(sky_json_t *json, sky_i64_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_integer;
    child->integer = value;

    return child;
}

sky_json_t *
sky_json_add_float(sky_json_t *json, sky_f64_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_float;
    child->dbl = value;

    return child;
}

sky_json_t *
sky_json_add_string(sky_json_t *json, sky_str_t *value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);

    if (!value) {
        child->type = json_null;
    } else {
        child->type = json_string;
        child->string = *value;
    }

    return child;
}

sky_json_t *
sky_json_add_str_len(sky_json_t *json, sky_uchar_t *v, sky_u32_t v_len) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_string;
    child->string.len = v_len;
    child->string.data = v;

    return child;
}


static sky_json_t *
parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end) {
    sky_u16_t next;

    sky_json_t *tmp, *current, *root;
    sky_json_object_t *object;


    parse_whitespace(&data);
    if (*data == '{') {
        root = current = sky_json_object_create(pool);
        next = NEXT_OBJECT_END | NEXT_KEY;
    } else if (*data == '[') {
        root = current = sky_json_array_create(pool);
        next = NEXT_ARRAY_END | NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
    } else {
        return null;
    }
    ++data;

    for (;;) {
        parse_whitespace(&data);
        switch (*data) {
            case '{': {
                if (sky_unlikely(!(next & NEXT_OBJECT_START))) {
                    return null;
                }
                ++data;

                tmp = current->type == json_object ? &object->value : json_array_get(current);
                json_object_init(tmp, pool);
                current = tmp;
                next = NEXT_OBJECT_END | NEXT_KEY;

                break;
            }
            case '[': {
                if (sky_unlikely(!(next & NEXT_ARRAY_START))) {
                    return null;
                }
                ++data;

                tmp = current->type == json_object ? &object->value : json_array_get(current);
                json_array_init(tmp, pool);
                current = tmp;
                next = NEXT_ARRAY_END | NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;

                break;
            }
            case '}': {
                if (sky_unlikely(!(next & NEXT_OBJECT_END))) {
                    return null;
                }

                ++data;
                current = current->parent;

                next = current != null ? (current->type == json_object ? (NEXT_NODE | NEXT_OBJECT_END)
                                                                       : (NEXT_NODE | NEXT_ARRAY_END))
                                       : NEXT_NONE;
                break;
            }
            case ']': {
                if (sky_unlikely(!(next & NEXT_ARRAY_END))) {
                    return null;
                }

                ++data;
                current = current->parent;
                next = current != null ? (current->type == json_object ? (NEXT_NODE | NEXT_OBJECT_END)
                                                                       : (NEXT_NODE | NEXT_ARRAY_END))
                                       : NEXT_NONE;
                break;
            }
            case ':': {
                if (sky_unlikely(!(next & NEXT_KEY_VALUE))) {
                    return null;
                }

                ++data;
                next = NEXT_OBJECT_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
                break;
            }
            case ',': {
                if (sky_unlikely(!(next & NEXT_NODE))) {
                    return null;
                }
                ++data;

                next = current->type == json_object ? NEXT_KEY :
                       (NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START);

                break;
            }
            case '"': {
                if (sky_unlikely(!(next & (NEXT_KEY | NEXT_ARRAY_VALUE | NEXT_OBJECT_VALUE)))) {
                    return null;
                }

                ++data;

                if ((next & NEXT_KEY) != 0) {
                    object = json_object_get(current);

                    if (sky_unlikely(!parse_string(&object->key, &data, end))) {
                        return null;
                    }
                    next = NEXT_KEY_VALUE;
                } else if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    tmp->type = json_string;
                    if (sky_unlikely(!parse_string(&tmp->string, &data, end))) {
                        return null;
                    }
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    tmp->type = json_string;

                    if (sky_unlikely(!parse_string(&tmp->string, &data, end))) {
                        return null;
                    }
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                break;
            }
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-': {
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE)))) {
                    return null;
                }
                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }

                if (sky_unlikely(!parse_number(tmp, &data))) {
                    return false;
                }

                break;
            }
            case 't': {
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE))
                                 || (end - data) < 4
                                 || !sky_str4_cmp(data, 't', 'r', 'u', 'e'))) {
                    return null;
                }
                data += 4;

                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_boolean;
                tmp->boolean = true;

                break;
            }
            case 'f': {
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE)))) {
                    return null;
                }
                ++data;

                if (sky_unlikely((end - data) < 4 || !sky_str4_cmp(data, 'a', 'l', 's', 'e'))) {
                    return null;
                }
                data += 4;

                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_boolean;
                tmp->boolean = false;
                break;
            }
            case 'n': {
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE))
                                 || (end - data) < 4
                                 || !sky_str4_cmp(data, 'n', 'u', 'l', 'l'))) {
                    return null;
                }
                data += 4;

                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_null;
                break;
            }
            case '\0': {
                if (sky_unlikely(next != NEXT_NONE)) {
                    return null;
                }
                return root;
            }
            default:
                return null;
        }
    }
}


static sky_inline void
parse_whitespace(sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    if (*p > ' ') {
        return;
    }
    if (*p == ' ' && *(p + 1) > ' ') {
        ++*ptr;
        return;
    }
    if (*p == '\r') {
        ++p;
    }

#if defined(__SSSE3__)
    const __m128i nrt_lut = _mm_set_epi8(-1, -1, 0, -1,
                                         -1, 0, 0, -1,
                                         -1, -1, -1, -1,
                                         -1, -1, -1, -1);

    for (;; p += 16) {

        const __m128i data = _mm_loadu_si128((const __m128i *) p);
        const __m128i dong = _mm_min_epu8(data, _mm_set1_epi8(0x0F));
        const __m128i not_an_nrt_mask = _mm_shuffle_epi8(nrt_lut, dong);
        const __m128i space_mask = _mm_cmpeq_epi8(data, _mm_set1_epi8(' '));
        const __m128i non_whitespace_mask = _mm_xor_si128(not_an_nrt_mask, space_mask);
        const sky_i32_t move_mask = _mm_movemask_epi8(non_whitespace_mask);
        if (__builtin_expect(move_mask, 1)) {
            *ptr = p + __builtin_ctz((sky_u32_t) move_mask);
            return;
        }
    }
#elif defined(__SSE4_2__)

    const __m128i w = _mm_setr_epi8('\n', '\r', '\t', ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    for (;; p += 16) {
        const __m128i s = _mm_loadu_si128((const __m128i *) p);
        const sky_i32_t r = _mm_cmpistri(w, s, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY
                                                 | _SIDD_LEAST_SIGNIFICANT | _SIDD_NEGATIVE_POLARITY);
        if (r != 16)    // some of characters is non-whitespace
        {
            *ptr = p + r;
            return;
        }
    }
#else
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    *ptr = p;
#endif
}


static sky_bool_t
parse_string(sky_str_t *str, sky_uchar_t **ptr, sky_uchar_t *end) {
    sky_uchar_t *p, *post;

    p = *ptr;
#if defined(__AVX2__)
    (void) end;

    sky_bool_t loop;

    for (;; p += 32) { // 转义符和引号在两块区域造成无法识别的问题
        const __m256i data = _mm256_loadu_si256((const __m256i *) p);
        const __m256i invalid_char_mask = _mm256_cmpeq_epi8(data, _mm256_min_epu8(data, _mm256_set1_epi8(0x1A)));
        const __m256i quote_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('"'));
        const __m256i backslash_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('\\'));

        const sky_u32_t quote_move_mask = (sky_u32_t) _mm256_movemask_epi8(quote_mask);
        const sky_u32_t backslash_move_mask = (sky_u32_t) _mm256_movemask_epi8(backslash_mask);

        if (sky_unlikely(_mm256_testz_si256(invalid_char_mask, invalid_char_mask) != 0)) {
            if (backslash_move_mask == 0) {
                if (quote_move_mask == 0) {
                    continue;
                }
                p += __builtin_ctz(quote_move_mask);
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_usize_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置
            if (quote_move_mask == 0) {
                p += backslash_len; // 未找到引号，数据还很长
                loop = true;
                break;
            }
            const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
            if (quote_len < backslash_len) {

                p += quote_len;
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_usize_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            p += backslash_len; // 找到引号，数据很快结束了
            loop = false;
            break;
        }
        if (sky_unlikely(quote_move_mask == 0)) { // 未找到引号
            return false;
        }
        const sky_u32_t invalid_char_move_mask = (sky_u32_t) _mm256_movemask_epi8(invalid_char_mask);
        const sky_i32_t invalid_char_len = __builtin_ctz(invalid_char_move_mask);
        const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
        if (sky_unlikely(invalid_char_len < quote_len)) {
            return false;
        }

        const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置

        if (quote_len < backslash_len) {
            p += quote_len;
            *p = '\0';
            str->data = *ptr;
            str->len = (sky_usize_t) (p - str->data);

            *ptr = p + 1;

            return true;
        }
        p += backslash_len; // 找到引号，数据很快结束了
        loop = false;
        break;
    }
    post = p;

    if (loop) {
        do {
            if (sky_unlikely(!backslash_parse(&p, &post))) {
                return false;
            }

            for (;; p += 32) { // 转义符和引号在两块区域造成无法识别的问题
                const __m256i data = _mm256_loadu_si256((const __m256i *) p);
                const __m256i invalid_char_mask = _mm256_cmpeq_epi8(data,
                                                                    _mm256_min_epu8(data, _mm256_set1_epi8(0x1A)));
                const __m256i quote_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('"'));
                const __m256i backslash_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('\\'));

                const sky_u32_t quote_move_mask = (sky_u32_t) _mm256_movemask_epi8(quote_mask);
                const sky_u32_t backslash_move_mask = (sky_u32_t) _mm256_movemask_epi8(backslash_mask);

                if (sky_unlikely(_mm256_testz_si256(invalid_char_mask, invalid_char_mask) != 0)) {
                    if (backslash_move_mask == 0) {
                        if (quote_move_mask == 0) {
                            _mm256_storeu_si256((__m256i *) post, data);
                            post += 32;
                            continue;
                        }
                        const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
                        sky_memmove(post, p, (sky_usize_t) quote_len);
                        post += quote_len;
                        p += quote_len;

                        *post = '\0';
                        str->data = *ptr;
                        str->len = (sky_usize_t) (post - str->data);
                        *ptr = p + 1;
                        return true;
                    }
                    const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置
                    if (quote_move_mask == 0) {
                        sky_memmove(post, p, (sky_usize_t) backslash_len);
                        post += backslash_len;
                        p += backslash_len; // 未找到引号，数据还很长
                        loop = true;
                        break;
                    }
                    const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
                    if (quote_len < backslash_len) {

                        sky_memmove(post, p, (sky_usize_t) quote_len);
                        post += quote_len;
                        p += quote_len;

                        *post = '\0';
                        str->data = *ptr;
                        str->len = (sky_usize_t) (post - str->data);
                        *ptr = p + 1;
                        return true;
                    }
                    sky_memmove(post, p, (sky_usize_t) backslash_len);
                    post += backslash_len;
                    p += backslash_len; // 找到引号，数据很快结束了
                    loop = false;
                    break;
                }
                if (sky_unlikely(quote_move_mask == 0)) { // 未找到引号
                    return false;
                }
                const sky_u32_t invalid_char_move_mask = (sky_u32_t) _mm256_movemask_epi8(invalid_char_mask);
                const sky_i32_t invalid_char_len = __builtin_ctz(invalid_char_move_mask);
                const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
                if (sky_unlikely(invalid_char_len < quote_len)) {
                    return false;
                }

                const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置

                if (quote_len < backslash_len) {
                    sky_memmove(post, p, (sky_usize_t) quote_len);
                    post += quote_len;
                    p += quote_len;

                    *post = '\0';
                    str->data = *ptr;
                    str->len = (sky_usize_t) (post - str->data);
                    *ptr = p + 1;
                    return true;
                }
                sky_memmove(post, p, (sky_usize_t) backslash_len);
                post += backslash_len;
                p += backslash_len; // 找到引号，数据很快结束了
                loop = false;
                break;
            }
        } while (loop);
    }
#elif defined(__SSE4_2__)
    static const sky_uchar_t sky_align(16) ranges[16] = "\0\037"
                                                      "\"\""
                                                      "\\\\";
    sky_bool_t loop = false;
    sky_usize_t size = (sky_usize_t) (end - p);
    if (sky_likely(size >= 16)) {
        sky_usize_t left = size & ~15U;

        __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);
        do {
            __m128i b16 = _mm_loadu_si128((const __m128i *) p);
            sky_i32_t r = _mm_cmpestri(
                    ranges16,
                    6,
                    b16,
                    16,
                    _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS
            );
            if (sky_unlikely(r != 16)) {
                p += r;

                if (sky_unlikely(*p < ' ')) {
                    return false;
                }
                if (*p == '"') {
                    *p = '\0';
                    str->data = *ptr;
                    str->len = (sky_usize_t) (p - str->data);

                    *ptr = p + 1;

                    return true;
                }
                loop = true;
                break;
            }
            p += 16;
            left -= 16;
        } while (sky_likely(left != 0));
    }
    if (!loop) {
        for (;;) {
            if (sky_unlikely(*p < ' ')) {
                return false;
            }
            if (*p == '\\') {
                break;
            }
            if (*p == '"') {
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_usize_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            ++p;
        }
        post = p;
    } else {
        post = p;
        do {
           if (sky_unlikely(!backslash_parse(&p, &post))) {
               return false;
           }

            loop = false;
            size = (sky_usize_t) (end - p);
            if (sky_likely(size >= 16)) {

                sky_usize_t left = size & ~15U;

                __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);
                do {
                    __m128i data = _mm_loadu_si128((const __m128i *) p);
                    sky_i32_t r = _mm_cmpestri(
                            ranges16,
                            6,
                            data,
                            16,
                            _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS
                    );
                    if (sky_unlikely(r != 16)) {
                        sky_memmove(post, p, (sky_usize_t) r);
                        p += r;
                        post += r;

                        if (sky_unlikely(*p < ' ')) {
                            return false;
                        }
                        if (*p == '"') {
                            *p = '\0';
                            str->data = *ptr;
                            str->len = (sky_usize_t) (p - str->data);

                            *ptr = p + 1;

                            return true;
                        }
                        loop = true;
                        break;
                    }
                    _mm_storeu_si128((__m128i *) post, data);
                    p += 16;
                    post += 16;
                    left -= 16;
                } while (sky_likely(left != 0));
            }
        } while (loop);
    }

#else
    (void) end;
    for (;;) {
        if (sky_unlikely(*p < ' ')) {
            return false;
        }
        if (*p == '\\') {
            break;
        }
        if (*p == '"') {
            *p = '\0';
            str->data = *ptr;
            str->len = (sky_usize_t) (p - str->data);

            *ptr = p + 1;

            return true;
        }
        ++p;
    }
    post = p;
#endif
    if (sky_unlikely(!backslash_parse(&p, &post))) {
        return false;
    }

    for (;;) {
        if (sky_unlikely(*p < ' ')) {
            return false;
        }
        if (*p == '\\') {
            if (sky_unlikely(!backslash_parse(&p, &post))) {
                return false;
            }
            continue;
        }
        if (*p == '"') {
            *post = '\0';
            str->data = *ptr;
            str->len = (sky_usize_t) (post - str->data);
            *ptr = p + 1;

            return true;
        }
        *post++ = *p++;
    }
}

static sky_bool_t
parse_number(sky_json_t *json, sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    ++p;

#if defined(__SSE4_2__)

    const __m128i w = _mm_setr_epi8('.', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0, 0, 0, 0, 0);

    for (;; p += 16) {
        const __m128i s = _mm_loadu_si128((const __m128i *) p);
        const sky_i32_t r = _mm_cmpistri(w, s, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY
                                               | _SIDD_LEAST_SIGNIFICANT | _SIDD_NEGATIVE_POLARITY);
        if (r != 16)    // some of characters is non-whitespace
        {
            p += r;

            const sky_str_t str = {
                    .len = ((sky_usize_t) (p - *ptr)),
                    .data = *ptr
            };
            json->type = json_integer;
            *ptr = p;

            return sky_str_to_i64(&str, &json->integer);
        }
    }
#else
    for (;;) {
        if (sky_unlikely(*p < '-' || *p > ':')) {
            const sky_str_t str = {
                    .len = ((sky_usize_t) (p - *ptr)),
                    .data = *ptr
            };

            json->type = json_integer;
            *ptr = p;

            return sky_str_to_i64(&str, &json->integer);
        }
        ++p;
    }
#endif
}

static sky_inline sky_bool_t
backslash_parse(sky_uchar_t **ptr, sky_uchar_t **post) {
    switch (*(++(*ptr))) {
        case '"':
        case '\\':
            *((*post)++) = **ptr;
            break;
        case 'b':
            *((*post)++) = '\b';
            break;
        case 'f':
            *((*post)++) = '\f';
            break;
        case 'n':
            *((*post)++) = '\n';
            break;
        case 'r':
            *((*post)++) = '\r';
            break;
        case 't':
            *((*post)++) = '\t';
            break;
        default:
            return false;
    }
    ++*ptr;

    return true;
}


static sky_inline sky_json_object_t *
json_object_get(sky_json_t *json) {
    sky_json_object_t *object = sky_palloc(json->pool, sizeof(sky_json_object_t));

    object->next = json->object;
    object->prev = object->next->prev;
    object->prev->next = object->next->prev = object;

    object->value.parent = json;

    return object;
}


static sky_inline sky_json_t *
json_array_get(sky_json_t *json) {
    sky_json_array_t *array = sky_palloc(json->pool, sizeof(sky_json_array_t));

    array->next = json->array;
    array->prev = array->next->prev;
    array->prev->next = array->next->prev = array;

    array->value.parent = json;

    return &array->value;
}


static sky_inline void
json_object_init(sky_json_t *json, sky_pool_t *pool) {
    sky_json_object_t *object = sky_palloc(pool, sizeof(sky_json_object_t));

    json->type = json_object;
    json->object = object;
    json->pool = pool;

    object->prev = object->next = object;
}


static sky_inline void
json_array_init(sky_json_t *json, sky_pool_t *pool) {
    sky_json_array_t *array = sky_palloc(pool, sizeof(sky_json_array_t));

    json->type = json_array;
    json->array = array;
    json->pool = pool;

    array->prev = array->next = array;
}

static sky_inline void
json_coding_str(sky_str_buf_t *buf, const sky_uchar_t *v, sky_usize_t v_len) {
    sky_str_buf_append_str_len(buf, v, v_len);
}