//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "number.h"
#include "log.h"
#include "float.h"

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


/** Digit: '0'. */
#define DIGIT_TYPE_ZERO     (1 << 0)

/** Digit: [1-9]. */
#define DIGIT_TYPE_NONZERO  (1 << 1)

/** Plus sign (positive): '+'. */
#define DIGIT_TYPE_POS      (1 << 2)

/** Minus sign (negative): '-'. */
#define DIGIT_TYPE_NEG      (1 << 3)

/** Decimal point: '.' */
#define DIGIT_TYPE_DOT      (1 << 4)

/** Exponent sign: 'e, 'E'. */
#define DIGIT_TYPE_EXP      (1 << 5)

#define F64_RAW_INF SKY_U64(0x7FF0000000000000)
#define F64_RAW_NAN SKY_U64(0x7FF8000000000000)

/* Maximum number of digits for reading u64 safety. */
#define U64_SAFE_DIG    19

/* double number bits */
#define F64_BITS 64

/* double number exponent part bits */
#define F64_EXP_BITS 11

/* double number significand part bits */
#define F64_SIG_BITS 52

/* double number significand part bits (with 1 hidden bit) */
#define F64_SIG_FULL_BITS 53

/* double number significand bit mask */
#define F64_SIG_MASK SKY_U64(0x000FFFFFFFFFFFFF)

/* double number exponent bit mask */
#define F64_EXP_MASK SKY_U64(0x7FF0000000000000)

/* double number exponent bias */
#define F64_EXP_BIAS 1023

/* max significant digits count in decimal when reading double number */
#define F64_MAX_DEC_DIG 768

/* maximum decimal power of double number (1.7976931348623157e308) */
#define F64_MAX_DEC_EXP 308

/* minimum decimal power of double number (4.9406564584124654e-324) */
#define F64_MIN_DEC_EXP (-324)

/* maximum binary power of double number */
#define F64_MAX_BIN_EXP 1024

/* minimum binary power of double number */
#define F64_MIN_BIN_EXP (-1021)


/** Maximum exact pow10 exponent for double value. */
#define F64_POW10_EXP_MAX_EXACT 22

/** Minimum decimal exponent in pow10_sig_table. */
#define POW10_SIG_TABLE_MIN_EXP (-343)

/** Minimum exact decimal exponent in pow10_sig_table */
#define POW10_SIG_TABLE_MIN_EXACT_EXP 0

/** Maximum exact decimal exponent in pow10_sig_table */
#define POW10_SIG_TABLE_MAX_EXACT_EXP 55


#define CHAR_ENC_CPY_1  0 /* 1-byte UTF-8, copy. */
#define CHAR_ENC_ERR_1  1 /* 1-byte UTF-8, error. */
#define CHAR_ENC_ESC_A  2 /* 1-byte ASCII, escaped as '\x'. */
#define CHAR_ENC_ESC_1  3 /* 1-byte UTF-8, escaped as '\uXXXX'. */
#define CHAR_ENC_CPY_2  4 /* 2-byte UTF-8, copy. */
#define CHAR_ENC_ESC_2  5 /* 2-byte UTF-8, escaped as '\uXXXX'. */
#define CHAR_ENC_CPY_3  6 /* 3-byte UTF-8, copy. */
#define CHAR_ENC_ESC_3  7 /* 3-byte UTF-8, escaped as '\uXXXX'. */
#define CHAR_ENC_CPY_4  8 /* 4-byte UTF-8, copy. */
#define CHAR_ENC_ESC_4  9 /* 4-byte UTF-8, escaped as '\uXXXX\uXXXX'. */

#define repeat16(x) do { x x x x x x x x x x x x x x x x } while(0)

#define repeat4_incr(x)  { x(0) x(1) x(2) x(3) }
#define repeat16_incr(x) { x(0) x(1) x(2) x(3) x(4) x(5) x(6) x(7) \
                           x(8) x(9) x(10) x(11) x(12) x(13) x(14) x(15) }

#define repeat_in_1_18(x) { x(1) x(2) x(3) x(4) x(5) x(6) x(7) \
                            x(8) x(9) x(10) x(11) x(12) x(13) x(14) x(15) \
                            x(16) x(17) x(18) }

/** Maximum exponent of exact pow10 */
#define U64_POW10_MAX_EXP 19

/** Maximum numbers of chunks used by a bigint_t (58 is enough here). */
#define BIGINT_MAX_CHUNKS 64


typedef struct json_str_chunk_s json_str_chunk_t;
typedef struct json_val_chunk_s json_val_chunk_t;
typedef struct json_str_pool_s json_str_pool_t;
typedef struct json_val_pool_s json_val_pool_t;

/** "Do It Yourself Floating Point" struct. */
typedef struct {
    sky_u64_t sig; /* significand */
    sky_i32_t exp; /* exponent, base 2 */
    sky_i32_t pad; /* padding, useless */
} diy_fp_t;

/** Unsigned arbitrarily large integer */
typedef struct {
    sky_u32_t used; /* used chunks count, should not be 0 */
    sky_u64_t bits[BIGINT_MAX_CHUNKS]; /* chunks */
} bigint_t;

typedef struct {
    sky_usize_t tag;
} json_write_ctx_t;

typedef struct {
    sky_usize_t tag;
    sky_json_mut_val_t *ctn;
} json_mut_write_ctx_t;


/**
 A memory chunk in string memory pool.
 */
struct json_str_chunk_s {
    json_str_chunk_t *next;
    /* flexible array member here */
};

/**
 A memory chunk in value memory pool.
 */
struct json_val_chunk_s {
    json_val_chunk_t *next;
    /* flexible array member here */
};

struct json_str_pool_s {
    sky_uchar_t *cur; /* cursor inside current chunk */
    sky_uchar_t *end; /* the end of current chunk */
    sky_usize_t chunk_size; /* chunk size in bytes while creating new chunk */
    sky_usize_t chunk_size_max; /* maximum chunk size in bytes */
    json_str_chunk_t *chunks; /* a linked list of chunks, nullable */
};

struct json_val_pool_s {
    sky_json_mut_val_t *cur; /* cursor inside current chunk */
    sky_json_mut_val_t *end; /* the end of current chunk */
    sky_usize_t chunk_size; /* chunk size in bytes while creating new chunk */
    sky_usize_t chunk_size_max; /* maximum chunk size in bytes */
    json_val_chunk_t *chunks; /* a linked list of chunks, nullable */
};

struct sky_json_mut_doc_s {
    sky_json_mut_val_t *root; /**< root value of the JSON document, nullable */
    json_str_pool_t str_pool; /**< string memory pool */
    json_val_pool_t val_pool; /**< value memory pool */
};


static sky_bool_t char_is_type(sky_uchar_t c, sky_u8_t type);

static sky_bool_t char_is_space(sky_uchar_t c);

static sky_bool_t char_is_space_or_comment(sky_uchar_t c);

static sky_bool_t char_is_number(sky_uchar_t c);

static sky_bool_t char_is_container(sky_uchar_t c);

static sky_bool_t char_is_ascii_stop(sky_uchar_t c);

static sky_bool_t char_is_line_end(sky_uchar_t c);

static sky_bool_t skip_spaces_and_comments(sky_uchar_t **ptr);

static sky_bool_t digit_is_type(sky_uchar_t d, sky_u8_t type);

static sky_bool_t digit_is_sign(sky_uchar_t d);

static sky_bool_t digit_is_nonzero(sky_uchar_t d);

static sky_bool_t digit_is_digit(sky_uchar_t d);

static sky_bool_t digit_is_exp(sky_uchar_t d);

static sky_bool_t digit_is_fp(sky_uchar_t d);

static sky_bool_t digit_is_digit_or_fp(sky_uchar_t d);

static sky_u64_t f64_raw_get_inf(sky_bool_t sign);

static sky_u64_t f64_raw_get_nan(sky_bool_t sign);

static sky_json_doc_t *read_root_pretty(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts);

static sky_json_doc_t *read_root_minify(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts);

static sky_json_doc_t *read_root_single(sky_uchar_t *hdr, sky_uchar_t *cur, sky_uchar_t *end, sky_u32_t opts);

static sky_bool_t read_number(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t ext);

static sky_bool_t read_number_raw(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t ext);

static sky_f64_t normalized_u64_to_f64(sky_u64_t val);

static sky_f64_t f64_pow10_table(sky_i32_t i);

static void pow10_table_get_sig(sky_i32_t exp10, sky_u64_t *hi, sky_u64_t *lo);

static void pow10_table_get_exp(sky_i32_t exp10, sky_i32_t *exp2);

static void u128_mul(sky_u64_t a, sky_u64_t b, sky_u64_t *hi, sky_u64_t *lo);

static void u128_mul_add(sky_u64_t a, sky_u64_t b, sky_u64_t c, sky_u64_t *hi, sky_u64_t *lo);

static void bigint_add_u64(bigint_t *big, sky_u64_t val);

static void bigint_mul_u64(bigint_t *big, sky_u64_t val);

static void bigint_mul_pow2(bigint_t *big, sky_u32_t exp);

static void bigint_mul_pow10(bigint_t *big, sky_i32_t exp);

static sky_i32_t bigint_cmp(bigint_t *a, bigint_t *b);

static void bigint_set_u64(bigint_t *big, sky_u64_t val);

static void bigint_set_buf(
        bigint_t *big,
        sky_u64_t sig,
        sky_i32_t *exp,
        sky_uchar_t *sig_cut,
        const sky_uchar_t *sig_end,
        const sky_uchar_t *dot_pos
);

static diy_fp_t diy_fp_get_cached_pow10(sky_i32_t exp10);

static diy_fp_t diy_fp_mul(diy_fp_t fp, diy_fp_t fp2);

static sky_u64_t diy_fp_to_ieee_raw(diy_fp_t fp);

static sky_bool_t read_string(sky_uchar_t **ptr, const sky_uchar_t *lst, sky_json_val_t *val, sky_bool_t inv);

static sky_bool_t read_true(sky_uchar_t **ptr, sky_json_val_t *val);

static sky_bool_t read_false(sky_uchar_t **ptr, sky_json_val_t *val);

static sky_bool_t read_null(sky_uchar_t **ptr, sky_json_val_t *val);

static sky_bool_t read_inf(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign);

static sky_bool_t read_nan(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign);

static sky_bool_t read_inf_or_nan(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t sign);

static sky_bool_t read_hex_u16(const sky_uchar_t *cur, sky_u16_t *val);

static sky_str_t *json_write_single(const sky_json_val_t *val, sky_u32_t opts);

static sky_str_t *json_write_pretty(const sky_json_val_t *root, sky_u32_t opts);

static sky_str_t *json_write_minify(const sky_json_val_t *root, sky_u32_t opts);

static sky_str_t *json_mut_write_single(const sky_json_mut_val_t *val, sky_u32_t opts);

static sky_str_t *json_mut_write_pretty(const sky_json_mut_val_t *root, sky_u32_t opts);

static sky_str_t *json_mut_write_minify(const sky_json_mut_val_t *root, sky_u32_t opts);

static void json_write_ctx_set(json_write_ctx_t *ctx, sky_usize_t size, sky_bool_t is_obj);

static void json_write_ctx_get(const json_write_ctx_t *ctx, sky_usize_t *size, sky_bool_t *is_obj);

static void json_mut_write_ctx_set(
        json_mut_write_ctx_t *ctx,
        sky_json_mut_val_t *ctn,
        sky_usize_t size,
        sky_bool_t is_obj
);

static void json_mut_write_ctx_get(
        json_mut_write_ctx_t *ctx,
        sky_json_mut_val_t **ctn,
        sky_usize_t *size,
        sky_bool_t *is_obj
);

static const sky_u8_t *get_enc_table_with_flag(sky_u32_t opts);

static sky_uchar_t *write_raw(sky_uchar_t *cur, const sky_str_t *src);

static sky_uchar_t *write_null(sky_uchar_t *cur);

static sky_uchar_t *write_bool(sky_uchar_t *cur, sky_bool_t val);

static sky_uchar_t *write_indent(sky_uchar_t *cur, sky_usize_t level);

static sky_uchar_t *write_number(sky_uchar_t *cur, const sky_json_val_t *val, sky_u32_t opts);

static sky_uchar_t *write_string(
        sky_uchar_t *cur,
        const sky_str_t *str,
        const sky_u8_t *enc_table,
        sky_bool_t esc,
        sky_bool_t inv
);

static sky_bool_t size_add_is_overflow(sky_usize_t size, sky_usize_t add);

static sky_usize_t size_align_up(sky_usize_t size, sky_usize_t align);

static sky_bool_t json_str_pool_grow(json_str_pool_t *pool, sky_usize_t n);

static sky_bool_t json_val_pool_grow(json_val_pool_t *pool, sky_usize_t n);

static void json_str_pool_release(json_str_pool_t *pool);

static void json_val_pool_release(json_val_pool_t *pool);

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

sky_str_t *
sky_json_val_write_opts(const sky_json_val_t *val, sky_u32_t opts) {
    if (sky_unlikely(!val)) {
        return null;
    }
    if (!sky_json_unsafe_is_ctn(val) || sky_json_unsafe_get_len(val) == 0) {
        return json_write_single(val, opts);
    } else if (opts & SKY_JSON_WRITE_PRETTY) {
        return json_write_pretty(val, opts);
    } else {
        return json_write_minify(val, opts);
    }
}

sky_json_val_t *
sky_json_obj_get(sky_json_val_t *obj, const sky_uchar_t *key, sky_usize_t key_len) {
    const sky_u64_t tag = (((sky_u64_t) key_len) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_STR;
    if (sky_likely(sky_json_is_obj(obj) && key_len)) {
        sky_usize_t len = sky_json_unsafe_get_len(obj);

        sky_json_val_t *item = sky_json_unsafe_get_first(obj);
        while (len-- > 0) {
            if (item->tag == tag && sky_str_len_unsafe_equals(item->uni.str, key, key_len)) {
                return item + 1;
            }
            item = sky_json_unsafe_get_next(item + 1);
        }
    }
    return null;
}

sky_json_val_t *
sky_json_arr_get(sky_json_val_t *arr, sky_usize_t idx) {
    if (sky_likely(sky_json_is_arr(arr))) {
        if (sky_likely(sky_json_unsafe_get_len(arr) > idx)) {
            sky_json_val_t *val = sky_json_unsafe_get_first(arr);
            if (sky_json_unsafe_arr_is_flat(arr)) {
                return val + idx;
            } else {
                while (idx-- > 0) {
                    val = sky_json_unsafe_get_next(val);
                }
                return val;
            }
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


/* ========================================================================== */

sky_json_mut_val_t *
sky_json_mut_unsafe_val(sky_json_mut_doc_t *doc, sky_usize_t n) {
    sky_json_mut_val_t *val;
    json_val_pool_t *pool = &doc->val_pool;
    if (sky_unlikely((sky_usize_t) (pool->end - pool->cur) < n)) {
        if (sky_unlikely(!json_val_pool_grow(pool, n))) {
            return NULL;
        }
    }

    val = pool->cur;
    pool->cur += n;
    return val;
}

sky_uchar_t *
sky_json_mut_unsafe_str_cpy(sky_json_mut_doc_t *doc, const sky_uchar_t *data, sky_usize_t len) {
    sky_uchar_t *mem;
    json_str_pool_t *pool = &doc->str_pool;

    if (sky_unlikely((sky_usize_t) (pool->end - pool->cur) <= len)) {
        if (sky_unlikely(!json_str_pool_grow(pool, len + 1))) {
            return NULL;
        }
    }
    mem = pool->cur;
    pool->cur = mem + len + 1;
    sky_memcpy(mem, data, len);
    mem[len] = '\0';

    return mem;
}

sky_json_mut_doc_t *
sky_json_mut_doc_create() {
    sky_json_mut_doc_t *doc = sky_malloc(sizeof(sky_json_mut_doc_t));
    if (sky_unlikely(!doc)) {
        return null;
    }
    sky_memzero(doc, sizeof(sky_json_mut_doc_t));
    doc->str_pool.chunk_size = 0x100;
    doc->str_pool.chunk_size_max = 0x10000000;
    doc->val_pool.chunk_size = 0x10 * sizeof(sky_json_mut_val_t);
    doc->val_pool.chunk_size_max = 0x1000000 * sizeof(sky_json_mut_val_t);

    return doc;
}

void
sky_json_mut_doc_set_root(sky_json_mut_doc_t *doc, sky_json_mut_val_t *root) {
    if (sky_likely(doc)) {
        doc->root = root;
    }
}

sky_str_t *
sky_json_mut_val_write_opts(const sky_json_mut_val_t *val, sky_u32_t opts) {
    if (sky_unlikely(!val)) {
        return null;
    }
    if (!sky_json_unsafe_is_ctn(&val->val) || sky_json_unsafe_get_len(&val->val) == 0) {
        return json_mut_write_single(val, opts);
    } else if (opts & SKY_JSON_WRITE_PRETTY) {
        return json_mut_write_pretty(val, opts);
    } else {
        return json_mut_write_minify(val, opts);
    }
}

sky_str_t *
sky_json_mut_write_opts(const sky_json_mut_doc_t *doc, sky_u32_t opts) {
    sky_json_mut_val_t *root = sky_likely(doc) ? doc->root : null;
    return sky_json_mut_val_write_opts(root, opts);
}

void
sky_json_mut_doc_free(sky_json_mut_doc_t *doc) {
    if (sky_unlikely(!doc)) {
        return;
    }
    json_str_pool_release(&doc->str_pool);
    json_val_pool_release(&doc->val_pool);
    sky_free(doc);
}


static sky_inline
sky_bool_t char_is_type(sky_uchar_t c, sky_u8_t type) {
    static const sky_u8_t char_table[256] = {
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
 * Match a character with specified type.
 * @param d  value
 * @param type type
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_type(sky_uchar_t d, sky_u8_t type) {
    /** Digit type table (generate with misc/make_tables.c) */
    static const sky_u8_t digit_table[256] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x04, 0x00, 0x08, 0x10, 0x00,
            0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
            0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    return (digit_table[d] & type) != 0;
}

/**
 * Match a sign: '+', '-'
 * @param d  value
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_sign(sky_uchar_t d) {
    return digit_is_type(d, DIGIT_TYPE_POS | DIGIT_TYPE_NEG);
}

/**
 * Match a none zero digit: [1-9]
 * @param d  value
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_nonzero(sky_uchar_t d) {
    return digit_is_type(d, DIGIT_TYPE_NONZERO);
}

/**
 * Match a digit: [0-9]
 * @param d  value
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_digit(sky_uchar_t d) {
    return digit_is_type(d, DIGIT_TYPE_ZERO | DIGIT_TYPE_NONZERO);
}


/**
 * Match an exponent sign: 'e', 'E'.
 * @param d  value
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_exp(sky_uchar_t d) {
    return digit_is_type(d, DIGIT_TYPE_EXP);
}

/**
 * Match a floating point indicator: '.', 'e', 'E'.
 * @param d  value
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_fp(sky_uchar_t d) {
    return digit_is_type(d, DIGIT_TYPE_DOT | DIGIT_TYPE_EXP);
}

/**
 * Match a digit or floating point indicator: [0-9], '.', 'e', 'E'.
 * @param d  value
 * @return is type
 */
static sky_inline sky_bool_t
digit_is_digit_or_fp(sky_uchar_t d) {
    return digit_is_type(d, DIGIT_TYPE_ZERO | DIGIT_TYPE_NONZERO | DIGIT_TYPE_DOT | DIGIT_TYPE_EXP);
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
        ctn->uni.ofs = 0;
        if (*cur == '\n') {
            ++cur;
        }
        goto obj_key_begin;
    } else {
        ctn->tag = SKY_JSON_TYPE_ARR;
        ctn->uni.ofs = 0;
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
    val->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);

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

    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->uni.ofs);
    ctn->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
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
    val->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);
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
    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->uni.ofs);
    /* point to the next value */
    ctn->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
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
        ctn->uni.ofs = 0;
        goto obj_key_begin;
    } else {
        ctn->tag = SKY_JSON_TYPE_ARR;
        ctn->uni.ofs = 0;
        goto arr_val_begin;
    }


    arr_begin:
    /* save current container */
    ctn->tag = (((sky_u64_t) ctn_len + 1) << SKY_JSON_TAG_BIT) | (ctn->tag & SKY_JSON_TAG_MASK);

    /* create a new array value, save parent container offset */
    val_incr();
    val->tag = SKY_JSON_TYPE_ARR;
    val->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);

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

    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->uni.ofs);
    ctn->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
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
    val->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn);
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
    ctn_parent = (sky_json_val_t *) ((sky_uchar_t *) ctn - ctn->uni.ofs);
    /* point to the next value */
    ctn->uni.ofs = (sky_usize_t) ((sky_uchar_t *) val - (sky_uchar_t *) ctn) + sizeof(sky_json_val_t);
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

#define return_err(_pos, _msg) do { \
    *end = _pos;                    \
    return false;                   \
} while (false)

#define return_i64(_v) do { \
    val->tag = (sky_u64_t)(SKY_JSON_TYPE_NUM | ((sky_u8_t)sign << 3)); \
    val->uni.u64 = sign ? (~(_v) + 1) : (_v);                          \
    *end = cur;             \
    return true;            \
} while (false)

#define return_f64(_v) do { \
    val->tag = SKY_JSON_TYPE_NUM | SKY_JSON_SUBTYPE_REAL; \
    val->uni.f64 = sign ? -(_v) : (_v);                   \
    *end = cur;             \
    return true;            \
} while (false)

#define return_f64_raw(_v) do { \
    val->tag = SKY_JSON_TYPE_NUM | SKY_JSON_SUBTYPE_REAL; \
    val->uni.u64 = ((sky_u64_t)sign << 63) | (sky_u64_t)(_v); \
    *end = cur;                 \
    return true;                \
} while (false)

#define return_inf() do { \
    if (sky_unlikely(ext)) { \
        return_f64_raw(F64_RAW_INF); \
    } else {              \
        return_err(hdr, "number is infinity when parsed as double"); \
    }                     \
} while (false)

    sky_uchar_t *hdr = *ptr;
    sky_uchar_t *cur = *ptr;
    sky_uchar_t **end = ptr;

    /* read number as raw string if has flag */
    if (sky_unlikely(pre)) {
        return read_number_raw(ptr, pre, val, ext);
    }

    sky_uchar_t *sig_cut = null; /* significant part cutting position for long number */
    sky_uchar_t *sig_end = null; /* significant part ending position */
    sky_uchar_t *dot_pos = null; /* decimal point position */
    sky_u64_t sig = 0; /* significant part of the number */
    sky_i32_t exp = 0; /* exponent part of the number */
    sky_i64_t exp_sig = 0; /* temporary exponent number from significant part */
    sky_i64_t exp_lit = 0; /* temporary exponent number from exponent literal part */
    sky_u64_t num; /* temporary number for reading */
    sky_uchar_t *tmp; /* temporary cursor for reading */
    sky_bool_t exp_sign; /* temporary exponent sign from literal part */

    const sky_bool_t sign = (*hdr == '-');
    cur += sign;

    /* begin with a leading zero or non-digit */
    if (sky_unlikely(!digit_is_nonzero(*cur))) { /* 0 or non-digit char */
        if (sky_unlikely(*cur != '0')) { /* non-digit char */
            if (sky_unlikely(ext)) {
                if (read_inf_or_nan(&cur, pre, val, sign)) {
                    *end = cur;
                    return true;
                }
            }
            return_err(cur - 1, "no digit after minus sign");
        }
        /* begin with 0 */
        if (sky_likely(!digit_is_digit_or_fp(*++cur))) {
            return_i64(0);
        }
        if (sky_likely(*cur == '.')) {
            dot_pos = cur++;
            if (sky_unlikely(!digit_is_digit(*cur))) {
                return_err(cur - 1, "no digit after decimal point");
            }
            while (sky_unlikely(*cur == '0')) cur++;
            if (sky_likely(digit_is_digit(*cur))) {
                /* first non-zero digit after decimal point */
                sig = (sky_u64_t) (*cur - '0'); /* read first digit */
                cur--;
                goto digit_frac_1; /* continue read fraction part */
            }
        }
        if (sky_unlikely(digit_is_digit(*cur))) {
            return_err(cur - 1, "number with leading zero is not allowed");
        }
        if (sky_unlikely(digit_is_exp(*cur))) { /* 0 with any exponent is still 0 */
            cur += SKY_USIZE(1) + digit_is_sign(cur[1]);
            if (sky_unlikely(!digit_is_digit(*cur))) {
                return_err(cur - 1, "no digit after exponent sign");
            }
            while (digit_is_digit(*++cur));
        }
        return_f64_raw(0);
    }

    /* begin with non-zero digit */
    sig = (sky_u64_t) (*cur - '0');

    /*
     Read integral part, same as the following code.

         for (int i = 1; i <= 18; i++) {
            num = cur[i] - '0';
            if (num <= 9) sig = num + sig * 10;
            else goto digit_sepr_i;
         }
     */
#define expr_intg(i) \
    if (sky_likely((num = (cur[i] - '0')) <= 9)) { \
        sig = num + sig * 10;                      \
    } else {          \
        goto digit_sepr_##i;                       \
    }

    repeat_in_1_18(expr_intg);
#undef expr_intg

    cur += 19; /* skip continuous 19 digits */
    if (!digit_is_digit_or_fp(*cur)) {
        /* this number is an integer consisting of 19 digits */
        if (sign && (sig > (SKY_U64(1) << 63))) { /* overflow */
            return_f64(normalized_u64_to_f64(sig));
        }
        return_i64(sig);
    }
    goto digit_intg_more; /* read more digits in integral part */

    /* process first non-digit character */
#define expr_sepr(i) \
    digit_sepr_##i:   \
    if (sky_likely(!digit_is_fp(cur[i]))) { \
        cur += (i);  \
        return_i64(sig);                  \
    }                \
    dot_pos = cur + (i);                  \
    if (sky_likely(cur[i] == '.')) {      \
        goto digit_frac_##i;               \
    }                \
    cur += (i);      \
    sig_end = cur;   \
    goto digit_exp_more;


    repeat_in_1_18(expr_sepr)
#undef expr_sepr


    /* read fraction part */
#define expr_frac(i) \
    digit_frac_##i: \
    if (sky_likely((num = (cur[(i) + 1] - '0')) <= 9)) { \
        sig = num + sig * 10;                            \
    } else {         \
        goto digit_stop_##i;                             \
    }

    repeat_in_1_18(expr_frac)
#undef expr_frac

    cur += 20; /* skip 19 digits and 1 decimal point */
    if (!digit_is_digit(*cur)) {
        goto digit_frac_end; /* fraction part end */
    }
    goto digit_frac_more; /* read more digits in fraction part */

    /* significant part end */
#define expr_stop(i) \
    digit_stop_##i:  \
    cur += (i) + 1;  \
    goto digit_frac_end;

    repeat_in_1_18(expr_stop)
#undef expr_stop


    /* read more digits in integral part */
    digit_intg_more:
    if (digit_is_digit(*cur)) {
        if (!digit_is_digit_or_fp(cur[1])) {
            /* this number is an integer consisting of 20 digits */
            num = (sky_u64_t) (*cur - '0');
            if ((sig < (SKY_U64_MAX / 10)) ||
                (sig == (SKY_U64_MAX / 10) && num <= (SKY_U64_MAX % 10))) {
                sig = num + sig * 10;
                cur++;
                /* convert to double if overflow */
                if (sign) return_f64(normalized_u64_to_f64(sig));
                return_i64(sig);
            }
        }
    }

    if (digit_is_exp(*cur)) {
        dot_pos = cur;
        goto digit_exp_more;
    }

    if (*cur == '.') {
        dot_pos = cur++;
        if (!digit_is_digit(*cur)) {
            return_err(cur, "no digit after decimal point");
        }
    }


    /* read more digits in fraction part */
    digit_frac_more:
    sig_cut = cur; /* too large to fit in u64, excess digits need to be cut */
    sig += (*cur >= '5'); /* round */
    while (digit_is_digit(*++cur));
    if (!dot_pos) {
        dot_pos = cur;
        if (*cur == '.') {
            if (!digit_is_digit(*++cur)) {
                return_err(cur, "no digit after decimal point");
            }
            while (digit_is_digit(*cur)) cur++;
        }
    }
    exp_sig = (sky_i64_t) (dot_pos - sig_cut);
    exp_sig += (dot_pos < sig_cut);

    /* ignore trailing zeros */
    tmp = cur - 1;
    while (*tmp == '0' || *tmp == '.') tmp--;
    if (tmp < sig_cut) {
        sig_cut = NULL;
    } else {
        sig_end = cur;
    }

    if (digit_is_exp(*cur)) {
        goto digit_exp_more;
    }
    goto digit_exp_finish;


    /* fraction part end */
    digit_frac_end:
    if (sky_unlikely(dot_pos + 1 == cur)) {
        return_err(cur - 1, "no digit after decimal point");
    }
    sig_end = cur;
    exp_sig = -(sky_i64_t) ((sky_u64_t) (cur - dot_pos) - 1);
    if (sky_likely(!digit_is_exp(*cur))) {
        if (sky_unlikely(exp_sig < F64_MIN_DEC_EXP - 19)) {
            return_f64_raw(0); /* underflow */
        }
        exp = (sky_i32_t) exp_sig;
        goto digit_finish;
    } else {
        goto digit_exp_more;
    }


    /* read exponent part */
    digit_exp_more:
    exp_sign = (*++cur == '-');
    cur += digit_is_sign(*cur);
    if (sky_unlikely(!digit_is_digit(*cur))) {
        return_err(cur - 1, "no digit after exponent sign");
    }
    while (*cur == '0') cur++;

    /* read exponent literal */
    tmp = cur;
    while (digit_is_digit(*cur)) {
        exp_lit = (*cur++ - '0') + exp_lit * 10;
    }
    if (sky_unlikely(cur - tmp >= U64_SAFE_DIG)) {
        if (exp_sign) {
            return_f64_raw(0); /* underflow */
        } else {
            return_inf(); /* overflow */
        }
    }
    exp_sig += exp_sign ? -exp_lit : exp_lit;


    /* validate exponent value */
    digit_exp_finish:
    if (sky_unlikely(exp_sig < F64_MIN_DEC_EXP - 19)) {
        return_f64_raw(0); /* underflow */
    }
    if (sky_unlikely(exp_sig > F64_MAX_DEC_EXP)) {
        return_inf(); /* overflow */
    }
    exp = (sky_i32_t) exp_sig;


    /* all digit read finished */
    digit_finish:

    /*
     Fast path 1:

     1. The floating-point number calculation should be accurate, see the
        comments of macro `YYJSON_DOUBLE_MATH_CORRECT`.
     2. Correct rounding should be performed (fegetround() == FE_TONEAREST).
     3. The input of floating point number calculation does not lose precision,
        which means: 64 - leading_zero(input) - trailing_zero(input) < 53.

     We don't check all available inputs here, because that would make the code
     more complicated, and not friendly to branch predictor.
     */
    if (sig < (SKY_U64(1) << 53) &&
        exp >= -F64_POW10_EXP_MAX_EXACT &&
        exp <= +F64_POW10_EXP_MAX_EXACT) {
        sky_f64_t dbl = (sky_f64_t) sig;
        if (exp < 0) {
            dbl /= f64_pow10_table(-exp);
        } else {
            dbl *= f64_pow10_table(+exp);
        }
        return_f64(dbl);
    }

    /*
     Fast path 2:

     To keep it simple, we only accept normal number here,
     let the slow path to handle subnormal and infinity number.
     */
    if (sky_likely(!sig_cut && exp > -F64_MAX_DEC_EXP + 1 && exp < +F64_MAX_DEC_EXP - 20)) {
        /*
         The result value is exactly equal to (sig * 10^exp),
         the exponent part (10^exp) can be converted to (sig2 * 2^exp2).

         The sig2 can be an infinite length number, only the highest 128 bits
         is cached in the pow10_sig_table.

         Now we have these bits:
         sig1 (normalized 64bit)        : aaaaaaaa
         sig2 (higher 64bit)            : bbbbbbbb
         sig2_ext (lower 64bit)         : cccccccc
         sig2_cut (extra unknown bits)  : dddddddddddd....

         And the calculation process is:
         ----------------------------------------
                 aaaaaaaa *
                 bbbbbbbbccccccccdddddddddddd....
         ----------------------------------------
         abababababababab +
                 acacacacacacacac +
                         adadadadadadadadadad....
         ----------------------------------------
         [hi____][lo____] +
                 [hi2___][lo2___] +
                         [unknown___________....]
         ----------------------------------------

         The addition with carry may affect higher bits, but if there is a 0
         in higher bits, the bits higher than 0 will not be affected.

         `lo2` + `unknown` may get a carry bit and may affect `hi2`, the max
         value of `hi2` is 0xFFFFFFFFFFFFFFFE, so `hi2` will not overflow.

         `lo` + `hi2` may also get a carry bit and may affect `hi`, but only
         the highest significant 53 bits of `hi` is needed. If there is a 0
         in the lower bits of `hi`, then all the following bits can be dropped.

         To convert the result to IEEE-754 double number, we need to perform
         correct rounding:
         1. if bit 54 is 0, round down,
         2. if bit 54 is 1 and any bit beyond bit 54 is 1, round up,
         3. if bit 54 is 1 and all bits beyond bit 54 are 0, round to even,
            as the extra bits is unknown, this case will not be handled here.
         */

        sky_u64_t raw;
        sky_u64_t sig1, sig2, sig2_ext, hi, lo, hi2, lo2, add, bits;
        sky_i32_t exp2;
        sky_u32_t lz;
        sky_bool_t exact = false, carry, round_up;

        /* convert (10^exp) to (sig2 * 2^exp2) */
        pow10_table_get_sig(exp, &sig2, &sig2_ext);
        pow10_table_get_exp(exp, &exp2);

        /* normalize and multiply */
        lz = (sky_u32_t) sky_clz_u64(sig);
        sig1 = sig << lz;
        exp2 -= (sky_i32_t) lz;
        u128_mul(sig1, sig2, &hi, &lo);

        /*
         The `hi` is in range [0x4000000000000000FFFFFFFFFFFFFFFE],
         To get normalized value, `hi` should be shifted to the left by 0 or 1.

         The highest significant 53 bits is used by IEEE-754 double number,
         and the bit 54 is used to detect rounding direction.

         The lowest (64 - 54 - 1) bits is used to check whether it contains 0.
         */
        bits = hi & ((SKY_U64(1) << (64 - 54 - 1)) - 1);
        if (bits - 1 < ((SKY_U64(1) << (64 - 54 - 1)) - 2)) {
            /*
             (bits != 0 && bits != 0x1FF) => (bits - 1 < 0x1FF - 1)
             The `bits` is not zero, so we don't need to check `round to even`
             case. The `bits` contains bit `0`, so we can drop the extra bits
             after `0`.
             */
            exact = true;

        } else {
            /*
             (bits == 0 || bits == 0x1FF)
             The `bits` is filled with all `0` or all `1`, so we need to check
             lower bits with another 64-bit multiplication.
             */
            u128_mul(sig1, sig2_ext, &hi2, &lo2);

            add = lo + hi2;
            if (add + 1 > SKY_U64(1)) {
                /*
                 (add != 0 && add != U64_MAX) => (add + 1 > 1)
                 The `add` is not zero, so we don't need to check `round to
                 even` case. The `add` contains bit `0`, so we can drop the
                 extra bits after `0`. The `hi` cannot be U64_MAX, so it will
                 not overflow.
                 */
                carry = add < lo || add < hi2;
                hi += carry;
                exact = true;
            }
        }

        if (exact) {
            /* normalize */
            lz = hi < (SKY_U64(1) << 63);
            hi <<= lz;
            exp2 -= (sky_i32_t) lz;
            exp2 += 64;

            /* test the bit 54 and get rounding direction */
            round_up = (hi & (SKY_U64(1) << (64 - 54))) > SKY_U64(0);
            hi += round_up ? (SKY_U64(1) << (64 - 54)) : SKY_U64(0);

            /* test overflow */
            if (hi < (SKY_U64(1) << (64 - 54))) {
                hi = (SKY_U64(1) << 63);
                exp2 += 1;
            }

            /* This is a normal number, convert it to IEEE-754 format. */
            hi >>= F64_BITS - F64_SIG_FULL_BITS;
            exp2 += F64_BITS - F64_SIG_FULL_BITS + F64_SIG_BITS;
            exp2 += F64_EXP_BIAS;
            raw = ((sky_u64_t) exp2 << F64_SIG_BITS) | (hi & F64_SIG_MASK);
            return_f64_raw(raw);
        }
    }

    /*
     Slow path: read double number exactly with diyfp.
     1. Use cached diyfp to get an approximation value.
     2. Use bigcomp to check the approximation value if needed.

     This algorithm refers to google's double-conversion project:
     https://github.com/google/double-conversion
     */
    {
        const sky_i32_t ERR_ULP_LOG = 3;
        const sky_i32_t ERR_ULP = 1 << ERR_ULP_LOG;
        const sky_i32_t ERR_CACHED_POW = ERR_ULP >> 1;
        const sky_i32_t ERR_MUL_FIXED = ERR_ULP >> 1;
        const sky_i32_t DIY_SIG_BITS = 64;
        const sky_i32_t EXP_BIAS = F64_EXP_BIAS + F64_SIG_BITS;
        const sky_i32_t EXP_SUBNORMAL = -EXP_BIAS + 1;

        sky_u64_t fp_err;
        sky_u32_t bits;
        sky_i32_t order_of_magnitude;
        sky_i32_t effective_significand_size;
        sky_i32_t precision_digits_count;
        sky_u64_t precision_bits;
        sky_u64_t half_way;

        sky_u64_t raw;
        diy_fp_t fp, fp_upper;
        bigint_t big_full, big_comp;
        sky_i32_t cmp;

        fp.sig = sig;
        fp.exp = 0;
        fp_err = sig_cut ? (sky_u64_t) (ERR_ULP >> 1) : (sky_u64_t) 0;

        /* normalize */
        bits = (sky_u32_t) sky_clz_u64(fp.sig);
        fp.sig <<= bits;
        fp.exp -= (sky_i32_t) bits;
        fp_err <<= bits;

        /* multiply and add error */
        fp = diy_fp_mul(fp, diy_fp_get_cached_pow10(exp));
        fp_err += (sky_u64_t) ERR_CACHED_POW + (fp_err != 0) + (sky_u64_t) ERR_MUL_FIXED;

        /* normalize */
        bits = (sky_u32_t) sky_clz_u64(fp.sig);
        fp.sig <<= bits;
        fp.exp -= (sky_i32_t) bits;
        fp_err <<= bits;

        /* effective significand */
        order_of_magnitude = DIY_SIG_BITS + fp.exp;
        if (sky_likely(order_of_magnitude >= EXP_SUBNORMAL + F64_SIG_FULL_BITS)) {
            effective_significand_size = F64_SIG_FULL_BITS;
        } else if (order_of_magnitude <= EXP_SUBNORMAL) {
            effective_significand_size = 0;
        } else {
            effective_significand_size = order_of_magnitude - EXP_SUBNORMAL;
        }

        /* precision digits count */
        precision_digits_count = DIY_SIG_BITS - effective_significand_size;
        if (sky_unlikely(precision_digits_count + ERR_ULP_LOG >= DIY_SIG_BITS)) {
            const sky_i32_t shr = (precision_digits_count + ERR_ULP_LOG) - DIY_SIG_BITS + 1;
            fp.sig >>= shr;
            fp.exp += shr;
            fp_err = (fp_err >> shr) + 1 + (sky_u32_t) ERR_ULP;
            precision_digits_count -= shr;
        }

        /* half way */
        precision_bits = fp.sig & ((SKY_U64(1) << precision_digits_count) - 1);
        precision_bits *= (sky_u64_t) ERR_ULP;
        half_way = SKY_U64(1) << (precision_digits_count - 1);
        half_way *= (sky_u64_t) ERR_ULP;

        /* rounding */
        fp.sig >>= precision_digits_count;
        fp.sig += (precision_bits >= half_way + fp_err);
        fp.exp += precision_digits_count;

        /* get IEEE double raw value */
        raw = diy_fp_to_ieee_raw(fp);
        if (sky_unlikely(raw == F64_RAW_INF)) return_inf();
        if (sky_likely(precision_bits <= half_way - fp_err ||
                       precision_bits >= half_way + fp_err)) {
            return_f64_raw(raw); /* number is accurate */
        }
        /* now the number is the correct value, or the next lower value */

        /* upper boundary */
        if (raw & F64_EXP_MASK) {
            fp_upper.sig = (raw & F64_SIG_MASK) + (SKY_U64(1) << F64_SIG_BITS);
            fp_upper.exp = (sky_i32_t) ((raw & F64_EXP_MASK) >> F64_SIG_BITS);
        } else {
            fp_upper.sig = (raw & F64_SIG_MASK);
            fp_upper.exp = 1;
        }
        fp_upper.exp -= F64_EXP_BIAS + F64_SIG_BITS;
        fp_upper.sig <<= 1;
        fp_upper.exp -= 1;
        fp_upper.sig += 1; /* add half ulp */

        /* compare with bigint_t */
        bigint_set_buf(&big_full, sig, &exp, sig_cut, sig_end, dot_pos);
        bigint_set_u64(&big_comp, fp_upper.sig);
        if (exp >= 0) {
            bigint_mul_pow10(&big_full, +exp);
        } else {
            bigint_mul_pow10(&big_comp, -exp);
        }
        if (fp_upper.exp > 0) {
            bigint_mul_pow2(&big_comp, (sky_u32_t) fp_upper.exp);
        } else {
            bigint_mul_pow2(&big_full, (sky_u32_t) (-fp_upper.exp));
        }
        cmp = bigint_cmp(&big_full, &big_comp);
        if (sky_likely(cmp != 0)) {
            /* round down or round up */
            raw += (cmp > 0);
        } else {
            /* falls midway, round to even */
            raw += (raw & 1);
        }

        if (sky_unlikely(raw == F64_RAW_INF)) {
            return_inf();
        }
        return_f64_raw(raw);
    }

#undef has_flag
#undef return_err
#undef return_inf
#undef return_i64
#undef return_f64
#undef return_f64_raw

}

/**
 * Read a JSON number as raw string.
 * @param ptr ptr
 * @param pre pre
 * @param val val
 * @param ext ext
 * @return Whether success.
 */
static sky_bool_t
read_number_raw(sky_uchar_t **ptr, sky_uchar_t **pre, sky_json_val_t *val, sky_bool_t ext) {
#define return_err(_pos, _msg) do { \
    *end = _pos; \
    return false; \
} while (false)

#define return_raw() do { \
    val->tag = ((sky_u64_t)(cur - hdr) << SKY_JSON_TAG_BIT) | SKY_JSON_TYPE_RAW; \
    val->uni.str = hdr;   \
    *pre = cur;           \
    *end = cur;           \
    return true;          \
} while (false)

    sky_uchar_t *hdr = *ptr;
    sky_uchar_t *cur = *ptr;
    sky_uchar_t **end = ptr;

    /* add null-terminator for previous raw string */
    if (*pre) {
        **pre = '\0';
    }

    /* skip sign */
    cur += (*cur == '-');
    /* read first digit, check leading zero */
    if (sky_unlikely(!digit_is_digit(*cur))) {
        if (sky_unlikely(ext)) {
            if (read_inf_or_nan(&cur, pre, val, *hdr == '-')) {
                return_raw();
            }
        }
        return_err(cur - 1, "no digit after minus sign");
    }

    /* read integral part */
    if (*cur == '0') {
        ++cur;
        if (sky_unlikely(digit_is_digit(*cur))) {
            return_err(cur - 1, "number with leading zero is not allowed");
        }
        if (!digit_is_fp(*cur)) return_raw();
    } else {
        while (digit_is_digit(*cur)) {
            ++cur;
        }
        if (!digit_is_fp(*cur)) {
            return_raw();
        }
    }

    /* read fraction part */
    if (*cur == '.') {
        ++cur;
        if (!digit_is_digit(*cur++)) {
            return_err(cur - 1, "no digit after decimal point");
        }
        while (digit_is_digit(*cur)) {
            ++cur;
        }
    }

    /* read exponent part */
    if (digit_is_exp(*cur)) {
        cur += 1 + digit_is_sign(cur[1]);
        if (!digit_is_digit(*cur++)) {
            return_err(cur - 1, "no digit after exponent sign");
        }
        while (digit_is_digit(*cur)) {
            ++cur;
        }
    }

    return_raw();

#undef return_raw
#undef return_err
}


/**
 * Convert normalized u64 (highest bit is 1) to f64.

    Some compiler (such as Microsoft Visual C++ 6.0) do not support converting
    number from u64 to f64. This function will first convert u64 to i64 and then
    to f64, with `to nearest` rounding mode.
 * @param val value
 * @return convert value
 */
static sky_inline sky_f64_t
normalized_u64_to_f64(sky_u64_t val) {
    return (sky_f64_t) val;
}

static sky_inline sky_f64_t
f64_pow10_table(sky_i32_t i) {
    /** Cached pow10 table. */
    static const sky_f64_t table[] = {
            1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12,
            1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
    };

    return table[i];
}

static sky_inline sky_u64_t
u64_pow10_table(sky_i32_t i) {
    /** Table: [ 10^0, ..., 10^19 ] (generate with misc/make_tables.c) */
    static const sky_u64_t table[U64_POW10_MAX_EXP + 1] = {
            SKY_U64(0x0000000000000001), SKY_U64(0x000000000000000A),
            SKY_U64(0x0000000000000064), SKY_U64(0x00000000000003E8),
            SKY_U64(0x0000000000002710), SKY_U64(0x00000000000186A0),
            SKY_U64(0x00000000000F4240), SKY_U64(0x0000000000989680),
            SKY_U64(0x0000000005F5E100), SKY_U64(0x000000003B9ACA00),
            SKY_U64(0x00000002540BE400), SKY_U64(0x000000174876E800),
            SKY_U64(0x000000E8D4A51000), SKY_U64(0x000009184E72A000),
            SKY_U64(0x00005AF3107A4000), SKY_U64(0x00038D7EA4C68000),
            SKY_U64(0x002386F26FC10000), SKY_U64(0x016345785D8A0000),
            SKY_U64(0x0DE0B6B3A7640000), SKY_U64(0x8AC7230489E80000)
    };

    return table[i];
}


/**
 * Get the cached pow10 value from pow10_sig_table.
 * @param exp10 The exponent of pow(10, e). This value must in range
                POW10_SIG_TABLE_MIN_EXP to POW10_SIG_TABLE_MAX_EXP.
 * @param hi The highest 64 bits of pow(10, e).
 * @param lo The lower 64 bits after `hi`.
 */
static sky_inline void
pow10_table_get_sig(sky_i32_t exp10, sky_u64_t *hi, sky_u64_t *lo) {
    /** Normalized significant 128 bits of pow10, no rounded up (size: 10.4KB).
    This lookup table is used by both the double number reader and writer.
    (generate with misc/make_tables.c) */
    static const sky_u64_t pow10_sig_table[] = {
            SKY_U64(0xBF29DCABA82FDEAE), SKY_U64(0x7432EE873880FC33), /* ~= 10^-343 */
            SKY_U64(0xEEF453D6923BD65A), SKY_U64(0x113FAA2906A13B3F), /* ~= 10^-342 */
            SKY_U64(0x9558B4661B6565F8), SKY_U64(0x4AC7CA59A424C507), /* ~= 10^-341 */
            SKY_U64(0xBAAEE17FA23EBF76), SKY_U64(0x5D79BCF00D2DF649), /* ~= 10^-340 */
            SKY_U64(0xE95A99DF8ACE6F53), SKY_U64(0xF4D82C2C107973DC), /* ~= 10^-339 */
            SKY_U64(0x91D8A02BB6C10594), SKY_U64(0x79071B9B8A4BE869), /* ~= 10^-338 */
            SKY_U64(0xB64EC836A47146F9), SKY_U64(0x9748E2826CDEE284), /* ~= 10^-337 */
            SKY_U64(0xE3E27A444D8D98B7), SKY_U64(0xFD1B1B2308169B25), /* ~= 10^-336 */
            SKY_U64(0x8E6D8C6AB0787F72), SKY_U64(0xFE30F0F5E50E20F7), /* ~= 10^-335 */
            SKY_U64(0xB208EF855C969F4F), SKY_U64(0xBDBD2D335E51A935), /* ~= 10^-334 */
            SKY_U64(0xDE8B2B66B3BC4723), SKY_U64(0xAD2C788035E61382), /* ~= 10^-333 */
            SKY_U64(0x8B16FB203055AC76), SKY_U64(0x4C3BCB5021AFCC31), /* ~= 10^-332 */
            SKY_U64(0xADDCB9E83C6B1793), SKY_U64(0xDF4ABE242A1BBF3D), /* ~= 10^-331 */
            SKY_U64(0xD953E8624B85DD78), SKY_U64(0xD71D6DAD34A2AF0D), /* ~= 10^-330 */
            SKY_U64(0x87D4713D6F33AA6B), SKY_U64(0x8672648C40E5AD68), /* ~= 10^-329 */
            SKY_U64(0xA9C98D8CCB009506), SKY_U64(0x680EFDAF511F18C2), /* ~= 10^-328 */
            SKY_U64(0xD43BF0EFFDC0BA48), SKY_U64(0x0212BD1B2566DEF2), /* ~= 10^-327 */
            SKY_U64(0x84A57695FE98746D), SKY_U64(0x014BB630F7604B57), /* ~= 10^-326 */
            SKY_U64(0xA5CED43B7E3E9188), SKY_U64(0x419EA3BD35385E2D), /* ~= 10^-325 */
            SKY_U64(0xCF42894A5DCE35EA), SKY_U64(0x52064CAC828675B9), /* ~= 10^-324 */
            SKY_U64(0x818995CE7AA0E1B2), SKY_U64(0x7343EFEBD1940993), /* ~= 10^-323 */
            SKY_U64(0xA1EBFB4219491A1F), SKY_U64(0x1014EBE6C5F90BF8), /* ~= 10^-322 */
            SKY_U64(0xCA66FA129F9B60A6), SKY_U64(0xD41A26E077774EF6), /* ~= 10^-321 */
            SKY_U64(0xFD00B897478238D0), SKY_U64(0x8920B098955522B4), /* ~= 10^-320 */
            SKY_U64(0x9E20735E8CB16382), SKY_U64(0x55B46E5F5D5535B0), /* ~= 10^-319 */
            SKY_U64(0xC5A890362FDDBC62), SKY_U64(0xEB2189F734AA831D), /* ~= 10^-318 */
            SKY_U64(0xF712B443BBD52B7B), SKY_U64(0xA5E9EC7501D523E4), /* ~= 10^-317 */
            SKY_U64(0x9A6BB0AA55653B2D), SKY_U64(0x47B233C92125366E), /* ~= 10^-316 */
            SKY_U64(0xC1069CD4EABE89F8), SKY_U64(0x999EC0BB696E840A), /* ~= 10^-315 */
            SKY_U64(0xF148440A256E2C76), SKY_U64(0xC00670EA43CA250D), /* ~= 10^-314 */
            SKY_U64(0x96CD2A865764DBCA), SKY_U64(0x380406926A5E5728), /* ~= 10^-313 */
            SKY_U64(0xBC807527ED3E12BC), SKY_U64(0xC605083704F5ECF2), /* ~= 10^-312 */
            SKY_U64(0xEBA09271E88D976B), SKY_U64(0xF7864A44C633682E), /* ~= 10^-311 */
            SKY_U64(0x93445B8731587EA3), SKY_U64(0x7AB3EE6AFBE0211D), /* ~= 10^-310 */
            SKY_U64(0xB8157268FDAE9E4C), SKY_U64(0x5960EA05BAD82964), /* ~= 10^-309 */
            SKY_U64(0xE61ACF033D1A45DF), SKY_U64(0x6FB92487298E33BD), /* ~= 10^-308 */
            SKY_U64(0x8FD0C16206306BAB), SKY_U64(0xA5D3B6D479F8E056), /* ~= 10^-307 */
            SKY_U64(0xB3C4F1BA87BC8696), SKY_U64(0x8F48A4899877186C), /* ~= 10^-306 */
            SKY_U64(0xE0B62E2929ABA83C), SKY_U64(0x331ACDABFE94DE87), /* ~= 10^-305 */
            SKY_U64(0x8C71DCD9BA0B4925), SKY_U64(0x9FF0C08B7F1D0B14), /* ~= 10^-304 */
            SKY_U64(0xAF8E5410288E1B6F), SKY_U64(0x07ECF0AE5EE44DD9), /* ~= 10^-303 */
            SKY_U64(0xDB71E91432B1A24A), SKY_U64(0xC9E82CD9F69D6150), /* ~= 10^-302 */
            SKY_U64(0x892731AC9FAF056E), SKY_U64(0xBE311C083A225CD2), /* ~= 10^-301 */
            SKY_U64(0xAB70FE17C79AC6CA), SKY_U64(0x6DBD630A48AAF406), /* ~= 10^-300 */
            SKY_U64(0xD64D3D9DB981787D), SKY_U64(0x092CBBCCDAD5B108), /* ~= 10^-299 */
            SKY_U64(0x85F0468293F0EB4E), SKY_U64(0x25BBF56008C58EA5), /* ~= 10^-298 */
            SKY_U64(0xA76C582338ED2621), SKY_U64(0xAF2AF2B80AF6F24E), /* ~= 10^-297 */
            SKY_U64(0xD1476E2C07286FAA), SKY_U64(0x1AF5AF660DB4AEE1), /* ~= 10^-296 */
            SKY_U64(0x82CCA4DB847945CA), SKY_U64(0x50D98D9FC890ED4D), /* ~= 10^-295 */
            SKY_U64(0xA37FCE126597973C), SKY_U64(0xE50FF107BAB528A0), /* ~= 10^-294 */
            SKY_U64(0xCC5FC196FEFD7D0C), SKY_U64(0x1E53ED49A96272C8), /* ~= 10^-293 */
            SKY_U64(0xFF77B1FCBEBCDC4F), SKY_U64(0x25E8E89C13BB0F7A), /* ~= 10^-292 */
            SKY_U64(0x9FAACF3DF73609B1), SKY_U64(0x77B191618C54E9AC), /* ~= 10^-291 */
            SKY_U64(0xC795830D75038C1D), SKY_U64(0xD59DF5B9EF6A2417), /* ~= 10^-290 */
            SKY_U64(0xF97AE3D0D2446F25), SKY_U64(0x4B0573286B44AD1D), /* ~= 10^-289 */
            SKY_U64(0x9BECCE62836AC577), SKY_U64(0x4EE367F9430AEC32), /* ~= 10^-288 */
            SKY_U64(0xC2E801FB244576D5), SKY_U64(0x229C41F793CDA73F), /* ~= 10^-287 */
            SKY_U64(0xF3A20279ED56D48A), SKY_U64(0x6B43527578C1110F), /* ~= 10^-286 */
            SKY_U64(0x9845418C345644D6), SKY_U64(0x830A13896B78AAA9), /* ~= 10^-285 */
            SKY_U64(0xBE5691EF416BD60C), SKY_U64(0x23CC986BC656D553), /* ~= 10^-284 */
            SKY_U64(0xEDEC366B11C6CB8F), SKY_U64(0x2CBFBE86B7EC8AA8), /* ~= 10^-283 */
            SKY_U64(0x94B3A202EB1C3F39), SKY_U64(0x7BF7D71432F3D6A9), /* ~= 10^-282 */
            SKY_U64(0xB9E08A83A5E34F07), SKY_U64(0xDAF5CCD93FB0CC53), /* ~= 10^-281 */
            SKY_U64(0xE858AD248F5C22C9), SKY_U64(0xD1B3400F8F9CFF68), /* ~= 10^-280 */
            SKY_U64(0x91376C36D99995BE), SKY_U64(0x23100809B9C21FA1), /* ~= 10^-279 */
            SKY_U64(0xB58547448FFFFB2D), SKY_U64(0xABD40A0C2832A78A), /* ~= 10^-278 */
            SKY_U64(0xE2E69915B3FFF9F9), SKY_U64(0x16C90C8F323F516C), /* ~= 10^-277 */
            SKY_U64(0x8DD01FAD907FFC3B), SKY_U64(0xAE3DA7D97F6792E3), /* ~= 10^-276 */
            SKY_U64(0xB1442798F49FFB4A), SKY_U64(0x99CD11CFDF41779C), /* ~= 10^-275 */
            SKY_U64(0xDD95317F31C7FA1D), SKY_U64(0x40405643D711D583), /* ~= 10^-274 */
            SKY_U64(0x8A7D3EEF7F1CFC52), SKY_U64(0x482835EA666B2572), /* ~= 10^-273 */
            SKY_U64(0xAD1C8EAB5EE43B66), SKY_U64(0xDA3243650005EECF), /* ~= 10^-272 */
            SKY_U64(0xD863B256369D4A40), SKY_U64(0x90BED43E40076A82), /* ~= 10^-271 */
            SKY_U64(0x873E4F75E2224E68), SKY_U64(0x5A7744A6E804A291), /* ~= 10^-270 */
            SKY_U64(0xA90DE3535AAAE202), SKY_U64(0x711515D0A205CB36), /* ~= 10^-269 */
            SKY_U64(0xD3515C2831559A83), SKY_U64(0x0D5A5B44CA873E03), /* ~= 10^-268 */
            SKY_U64(0x8412D9991ED58091), SKY_U64(0xE858790AFE9486C2), /* ~= 10^-267 */
            SKY_U64(0xA5178FFF668AE0B6), SKY_U64(0x626E974DBE39A872), /* ~= 10^-266 */
            SKY_U64(0xCE5D73FF402D98E3), SKY_U64(0xFB0A3D212DC8128F), /* ~= 10^-265 */
            SKY_U64(0x80FA687F881C7F8E), SKY_U64(0x7CE66634BC9D0B99), /* ~= 10^-264 */
            SKY_U64(0xA139029F6A239F72), SKY_U64(0x1C1FFFC1EBC44E80), /* ~= 10^-263 */
            SKY_U64(0xC987434744AC874E), SKY_U64(0xA327FFB266B56220), /* ~= 10^-262 */
            SKY_U64(0xFBE9141915D7A922), SKY_U64(0x4BF1FF9F0062BAA8), /* ~= 10^-261 */
            SKY_U64(0x9D71AC8FADA6C9B5), SKY_U64(0x6F773FC3603DB4A9), /* ~= 10^-260 */
            SKY_U64(0xC4CE17B399107C22), SKY_U64(0xCB550FB4384D21D3), /* ~= 10^-259 */
            SKY_U64(0xF6019DA07F549B2B), SKY_U64(0x7E2A53A146606A48), /* ~= 10^-258 */
            SKY_U64(0x99C102844F94E0FB), SKY_U64(0x2EDA7444CBFC426D), /* ~= 10^-257 */
            SKY_U64(0xC0314325637A1939), SKY_U64(0xFA911155FEFB5308), /* ~= 10^-256 */
            SKY_U64(0xF03D93EEBC589F88), SKY_U64(0x793555AB7EBA27CA), /* ~= 10^-255 */
            SKY_U64(0x96267C7535B763B5), SKY_U64(0x4BC1558B2F3458DE), /* ~= 10^-254 */
            SKY_U64(0xBBB01B9283253CA2), SKY_U64(0x9EB1AAEDFB016F16), /* ~= 10^-253 */
            SKY_U64(0xEA9C227723EE8BCB), SKY_U64(0x465E15A979C1CADC), /* ~= 10^-252 */
            SKY_U64(0x92A1958A7675175F), SKY_U64(0x0BFACD89EC191EC9), /* ~= 10^-251 */
            SKY_U64(0xB749FAED14125D36), SKY_U64(0xCEF980EC671F667B), /* ~= 10^-250 */
            SKY_U64(0xE51C79A85916F484), SKY_U64(0x82B7E12780E7401A), /* ~= 10^-249 */
            SKY_U64(0x8F31CC0937AE58D2), SKY_U64(0xD1B2ECB8B0908810), /* ~= 10^-248 */
            SKY_U64(0xB2FE3F0B8599EF07), SKY_U64(0x861FA7E6DCB4AA15), /* ~= 10^-247 */
            SKY_U64(0xDFBDCECE67006AC9), SKY_U64(0x67A791E093E1D49A), /* ~= 10^-246 */
            SKY_U64(0x8BD6A141006042BD), SKY_U64(0xE0C8BB2C5C6D24E0), /* ~= 10^-245 */
            SKY_U64(0xAECC49914078536D), SKY_U64(0x58FAE9F773886E18), /* ~= 10^-244 */
            SKY_U64(0xDA7F5BF590966848), SKY_U64(0xAF39A475506A899E), /* ~= 10^-243 */
            SKY_U64(0x888F99797A5E012D), SKY_U64(0x6D8406C952429603), /* ~= 10^-242 */
            SKY_U64(0xAAB37FD7D8F58178), SKY_U64(0xC8E5087BA6D33B83), /* ~= 10^-241 */
            SKY_U64(0xD5605FCDCF32E1D6), SKY_U64(0xFB1E4A9A90880A64), /* ~= 10^-240 */
            SKY_U64(0x855C3BE0A17FCD26), SKY_U64(0x5CF2EEA09A55067F), /* ~= 10^-239 */
            SKY_U64(0xA6B34AD8C9DFC06F), SKY_U64(0xF42FAA48C0EA481E), /* ~= 10^-238 */
            SKY_U64(0xD0601D8EFC57B08B), SKY_U64(0xF13B94DAF124DA26), /* ~= 10^-237 */
            SKY_U64(0x823C12795DB6CE57), SKY_U64(0x76C53D08D6B70858), /* ~= 10^-236 */
            SKY_U64(0xA2CB1717B52481ED), SKY_U64(0x54768C4B0C64CA6E), /* ~= 10^-235 */
            SKY_U64(0xCB7DDCDDA26DA268), SKY_U64(0xA9942F5DCF7DFD09), /* ~= 10^-234 */
            SKY_U64(0xFE5D54150B090B02), SKY_U64(0xD3F93B35435D7C4C), /* ~= 10^-233 */
            SKY_U64(0x9EFA548D26E5A6E1), SKY_U64(0xC47BC5014A1A6DAF), /* ~= 10^-232 */
            SKY_U64(0xC6B8E9B0709F109A), SKY_U64(0x359AB6419CA1091B), /* ~= 10^-231 */
            SKY_U64(0xF867241C8CC6D4C0), SKY_U64(0xC30163D203C94B62), /* ~= 10^-230 */
            SKY_U64(0x9B407691D7FC44F8), SKY_U64(0x79E0DE63425DCF1D), /* ~= 10^-229 */
            SKY_U64(0xC21094364DFB5636), SKY_U64(0x985915FC12F542E4), /* ~= 10^-228 */
            SKY_U64(0xF294B943E17A2BC4), SKY_U64(0x3E6F5B7B17B2939D), /* ~= 10^-227 */
            SKY_U64(0x979CF3CA6CEC5B5A), SKY_U64(0xA705992CEECF9C42), /* ~= 10^-226 */
            SKY_U64(0xBD8430BD08277231), SKY_U64(0x50C6FF782A838353), /* ~= 10^-225 */
            SKY_U64(0xECE53CEC4A314EBD), SKY_U64(0xA4F8BF5635246428), /* ~= 10^-224 */
            SKY_U64(0x940F4613AE5ED136), SKY_U64(0x871B7795E136BE99), /* ~= 10^-223 */
            SKY_U64(0xB913179899F68584), SKY_U64(0x28E2557B59846E3F), /* ~= 10^-222 */
            SKY_U64(0xE757DD7EC07426E5), SKY_U64(0x331AEADA2FE589CF), /* ~= 10^-221 */
            SKY_U64(0x9096EA6F3848984F), SKY_U64(0x3FF0D2C85DEF7621), /* ~= 10^-220 */
            SKY_U64(0xB4BCA50B065ABE63), SKY_U64(0x0FED077A756B53A9), /* ~= 10^-219 */
            SKY_U64(0xE1EBCE4DC7F16DFB), SKY_U64(0xD3E8495912C62894), /* ~= 10^-218 */
            SKY_U64(0x8D3360F09CF6E4BD), SKY_U64(0x64712DD7ABBBD95C), /* ~= 10^-217 */
            SKY_U64(0xB080392CC4349DEC), SKY_U64(0xBD8D794D96AACFB3), /* ~= 10^-216 */
            SKY_U64(0xDCA04777F541C567), SKY_U64(0xECF0D7A0FC5583A0), /* ~= 10^-215 */
            SKY_U64(0x89E42CAAF9491B60), SKY_U64(0xF41686C49DB57244), /* ~= 10^-214 */
            SKY_U64(0xAC5D37D5B79B6239), SKY_U64(0x311C2875C522CED5), /* ~= 10^-213 */
            SKY_U64(0xD77485CB25823AC7), SKY_U64(0x7D633293366B828B), /* ~= 10^-212 */
            SKY_U64(0x86A8D39EF77164BC), SKY_U64(0xAE5DFF9C02033197), /* ~= 10^-211 */
            SKY_U64(0xA8530886B54DBDEB), SKY_U64(0xD9F57F830283FDFC), /* ~= 10^-210 */
            SKY_U64(0xD267CAA862A12D66), SKY_U64(0xD072DF63C324FD7B), /* ~= 10^-209 */
            SKY_U64(0x8380DEA93DA4BC60), SKY_U64(0x4247CB9E59F71E6D), /* ~= 10^-208 */
            SKY_U64(0xA46116538D0DEB78), SKY_U64(0x52D9BE85F074E608), /* ~= 10^-207 */
            SKY_U64(0xCD795BE870516656), SKY_U64(0x67902E276C921F8B), /* ~= 10^-206 */
            SKY_U64(0x806BD9714632DFF6), SKY_U64(0x00BA1CD8A3DB53B6), /* ~= 10^-205 */
            SKY_U64(0xA086CFCD97BF97F3), SKY_U64(0x80E8A40ECCD228A4), /* ~= 10^-204 */
            SKY_U64(0xC8A883C0FDAF7DF0), SKY_U64(0x6122CD128006B2CD), /* ~= 10^-203 */
            SKY_U64(0xFAD2A4B13D1B5D6C), SKY_U64(0x796B805720085F81), /* ~= 10^-202 */
            SKY_U64(0x9CC3A6EEC6311A63), SKY_U64(0xCBE3303674053BB0), /* ~= 10^-201 */
            SKY_U64(0xC3F490AA77BD60FC), SKY_U64(0xBEDBFC4411068A9C), /* ~= 10^-200 */
            SKY_U64(0xF4F1B4D515ACB93B), SKY_U64(0xEE92FB5515482D44), /* ~= 10^-199 */
            SKY_U64(0x991711052D8BF3C5), SKY_U64(0x751BDD152D4D1C4A), /* ~= 10^-198 */
            SKY_U64(0xBF5CD54678EEF0B6), SKY_U64(0xD262D45A78A0635D), /* ~= 10^-197 */
            SKY_U64(0xEF340A98172AACE4), SKY_U64(0x86FB897116C87C34), /* ~= 10^-196 */
            SKY_U64(0x9580869F0E7AAC0E), SKY_U64(0xD45D35E6AE3D4DA0), /* ~= 10^-195 */
            SKY_U64(0xBAE0A846D2195712), SKY_U64(0x8974836059CCA109), /* ~= 10^-194 */
            SKY_U64(0xE998D258869FACD7), SKY_U64(0x2BD1A438703FC94B), /* ~= 10^-193 */
            SKY_U64(0x91FF83775423CC06), SKY_U64(0x7B6306A34627DDCF), /* ~= 10^-192 */
            SKY_U64(0xB67F6455292CBF08), SKY_U64(0x1A3BC84C17B1D542), /* ~= 10^-191 */
            SKY_U64(0xE41F3D6A7377EECA), SKY_U64(0x20CABA5F1D9E4A93), /* ~= 10^-190 */
            SKY_U64(0x8E938662882AF53E), SKY_U64(0x547EB47B7282EE9C), /* ~= 10^-189 */
            SKY_U64(0xB23867FB2A35B28D), SKY_U64(0xE99E619A4F23AA43), /* ~= 10^-188 */
            SKY_U64(0xDEC681F9F4C31F31), SKY_U64(0x6405FA00E2EC94D4), /* ~= 10^-187 */
            SKY_U64(0x8B3C113C38F9F37E), SKY_U64(0xDE83BC408DD3DD04), /* ~= 10^-186 */
            SKY_U64(0xAE0B158B4738705E), SKY_U64(0x9624AB50B148D445), /* ~= 10^-185 */
            SKY_U64(0xD98DDAEE19068C76), SKY_U64(0x3BADD624DD9B0957), /* ~= 10^-184 */
            SKY_U64(0x87F8A8D4CFA417C9), SKY_U64(0xE54CA5D70A80E5D6), /* ~= 10^-183 */
            SKY_U64(0xA9F6D30A038D1DBC), SKY_U64(0x5E9FCF4CCD211F4C), /* ~= 10^-182 */
            SKY_U64(0xD47487CC8470652B), SKY_U64(0x7647C3200069671F), /* ~= 10^-181 */
            SKY_U64(0x84C8D4DFD2C63F3B), SKY_U64(0x29ECD9F40041E073), /* ~= 10^-180 */
            SKY_U64(0xA5FB0A17C777CF09), SKY_U64(0xF468107100525890), /* ~= 10^-179 */
            SKY_U64(0xCF79CC9DB955C2CC), SKY_U64(0x7182148D4066EEB4), /* ~= 10^-178 */
            SKY_U64(0x81AC1FE293D599BF), SKY_U64(0xC6F14CD848405530), /* ~= 10^-177 */
            SKY_U64(0xA21727DB38CB002F), SKY_U64(0xB8ADA00E5A506A7C), /* ~= 10^-176 */
            SKY_U64(0xCA9CF1D206FDC03B), SKY_U64(0xA6D90811F0E4851C), /* ~= 10^-175 */
            SKY_U64(0xFD442E4688BD304A), SKY_U64(0x908F4A166D1DA663), /* ~= 10^-174 */
            SKY_U64(0x9E4A9CEC15763E2E), SKY_U64(0x9A598E4E043287FE), /* ~= 10^-173 */
            SKY_U64(0xC5DD44271AD3CDBA), SKY_U64(0x40EFF1E1853F29FD), /* ~= 10^-172 */
            SKY_U64(0xF7549530E188C128), SKY_U64(0xD12BEE59E68EF47C), /* ~= 10^-171 */
            SKY_U64(0x9A94DD3E8CF578B9), SKY_U64(0x82BB74F8301958CE), /* ~= 10^-170 */
            SKY_U64(0xC13A148E3032D6E7), SKY_U64(0xE36A52363C1FAF01), /* ~= 10^-169 */
            SKY_U64(0xF18899B1BC3F8CA1), SKY_U64(0xDC44E6C3CB279AC1), /* ~= 10^-168 */
            SKY_U64(0x96F5600F15A7B7E5), SKY_U64(0x29AB103A5EF8C0B9), /* ~= 10^-167 */
            SKY_U64(0xBCB2B812DB11A5DE), SKY_U64(0x7415D448F6B6F0E7), /* ~= 10^-166 */
            SKY_U64(0xEBDF661791D60F56), SKY_U64(0x111B495B3464AD21), /* ~= 10^-165 */
            SKY_U64(0x936B9FCEBB25C995), SKY_U64(0xCAB10DD900BEEC34), /* ~= 10^-164 */
            SKY_U64(0xB84687C269EF3BFB), SKY_U64(0x3D5D514F40EEA742), /* ~= 10^-163 */
            SKY_U64(0xE65829B3046B0AFA), SKY_U64(0x0CB4A5A3112A5112), /* ~= 10^-162 */
            SKY_U64(0x8FF71A0FE2C2E6DC), SKY_U64(0x47F0E785EABA72AB), /* ~= 10^-161 */
            SKY_U64(0xB3F4E093DB73A093), SKY_U64(0x59ED216765690F56), /* ~= 10^-160 */
            SKY_U64(0xE0F218B8D25088B8), SKY_U64(0x306869C13EC3532C), /* ~= 10^-159 */
            SKY_U64(0x8C974F7383725573), SKY_U64(0x1E414218C73A13FB), /* ~= 10^-158 */
            SKY_U64(0xAFBD2350644EEACF), SKY_U64(0xE5D1929EF90898FA), /* ~= 10^-157 */
            SKY_U64(0xDBAC6C247D62A583), SKY_U64(0xDF45F746B74ABF39), /* ~= 10^-156 */
            SKY_U64(0x894BC396CE5DA772), SKY_U64(0x6B8BBA8C328EB783), /* ~= 10^-155 */
            SKY_U64(0xAB9EB47C81F5114F), SKY_U64(0x066EA92F3F326564), /* ~= 10^-154 */
            SKY_U64(0xD686619BA27255A2), SKY_U64(0xC80A537B0EFEFEBD), /* ~= 10^-153 */
            SKY_U64(0x8613FD0145877585), SKY_U64(0xBD06742CE95F5F36), /* ~= 10^-152 */
            SKY_U64(0xA798FC4196E952E7), SKY_U64(0x2C48113823B73704), /* ~= 10^-151 */
            SKY_U64(0xD17F3B51FCA3A7A0), SKY_U64(0xF75A15862CA504C5), /* ~= 10^-150 */
            SKY_U64(0x82EF85133DE648C4), SKY_U64(0x9A984D73DBE722FB), /* ~= 10^-149 */
            SKY_U64(0xA3AB66580D5FDAF5), SKY_U64(0xC13E60D0D2E0EBBA), /* ~= 10^-148 */
            SKY_U64(0xCC963FEE10B7D1B3), SKY_U64(0x318DF905079926A8), /* ~= 10^-147 */
            SKY_U64(0xFFBBCFE994E5C61F), SKY_U64(0xFDF17746497F7052), /* ~= 10^-146 */
            SKY_U64(0x9FD561F1FD0F9BD3), SKY_U64(0xFEB6EA8BEDEFA633), /* ~= 10^-145 */
            SKY_U64(0xC7CABA6E7C5382C8), SKY_U64(0xFE64A52EE96B8FC0), /* ~= 10^-144 */
            SKY_U64(0xF9BD690A1B68637B), SKY_U64(0x3DFDCE7AA3C673B0), /* ~= 10^-143 */
            SKY_U64(0x9C1661A651213E2D), SKY_U64(0x06BEA10CA65C084E), /* ~= 10^-142 */
            SKY_U64(0xC31BFA0FE5698DB8), SKY_U64(0x486E494FCFF30A62), /* ~= 10^-141 */
            SKY_U64(0xF3E2F893DEC3F126), SKY_U64(0x5A89DBA3C3EFCCFA), /* ~= 10^-140 */
            SKY_U64(0x986DDB5C6B3A76B7), SKY_U64(0xF89629465A75E01C), /* ~= 10^-139 */
            SKY_U64(0xBE89523386091465), SKY_U64(0xF6BBB397F1135823), /* ~= 10^-138 */
            SKY_U64(0xEE2BA6C0678B597F), SKY_U64(0x746AA07DED582E2C), /* ~= 10^-137 */
            SKY_U64(0x94DB483840B717EF), SKY_U64(0xA8C2A44EB4571CDC), /* ~= 10^-136 */
            SKY_U64(0xBA121A4650E4DDEB), SKY_U64(0x92F34D62616CE413), /* ~= 10^-135 */
            SKY_U64(0xE896A0D7E51E1566), SKY_U64(0x77B020BAF9C81D17), /* ~= 10^-134 */
            SKY_U64(0x915E2486EF32CD60), SKY_U64(0x0ACE1474DC1D122E), /* ~= 10^-133 */
            SKY_U64(0xB5B5ADA8AAFF80B8), SKY_U64(0x0D819992132456BA), /* ~= 10^-132 */
            SKY_U64(0xE3231912D5BF60E6), SKY_U64(0x10E1FFF697ED6C69), /* ~= 10^-131 */
            SKY_U64(0x8DF5EFABC5979C8F), SKY_U64(0xCA8D3FFA1EF463C1), /* ~= 10^-130 */
            SKY_U64(0xB1736B96B6FD83B3), SKY_U64(0xBD308FF8A6B17CB2), /* ~= 10^-129 */
            SKY_U64(0xDDD0467C64BCE4A0), SKY_U64(0xAC7CB3F6D05DDBDE), /* ~= 10^-128 */
            SKY_U64(0x8AA22C0DBEF60EE4), SKY_U64(0x6BCDF07A423AA96B), /* ~= 10^-127 */
            SKY_U64(0xAD4AB7112EB3929D), SKY_U64(0x86C16C98D2C953C6), /* ~= 10^-126 */
            SKY_U64(0xD89D64D57A607744), SKY_U64(0xE871C7BF077BA8B7), /* ~= 10^-125 */
            SKY_U64(0x87625F056C7C4A8B), SKY_U64(0x11471CD764AD4972), /* ~= 10^-124 */
            SKY_U64(0xA93AF6C6C79B5D2D), SKY_U64(0xD598E40D3DD89BCF), /* ~= 10^-123 */
            SKY_U64(0xD389B47879823479), SKY_U64(0x4AFF1D108D4EC2C3), /* ~= 10^-122 */
            SKY_U64(0x843610CB4BF160CB), SKY_U64(0xCEDF722A585139BA), /* ~= 10^-121 */
            SKY_U64(0xA54394FE1EEDB8FE), SKY_U64(0xC2974EB4EE658828), /* ~= 10^-120 */
            SKY_U64(0xCE947A3DA6A9273E), SKY_U64(0x733D226229FEEA32), /* ~= 10^-119 */
            SKY_U64(0x811CCC668829B887), SKY_U64(0x0806357D5A3F525F), /* ~= 10^-118 */
            SKY_U64(0xA163FF802A3426A8), SKY_U64(0xCA07C2DCB0CF26F7), /* ~= 10^-117 */
            SKY_U64(0xC9BCFF6034C13052), SKY_U64(0xFC89B393DD02F0B5), /* ~= 10^-116 */
            SKY_U64(0xFC2C3F3841F17C67), SKY_U64(0xBBAC2078D443ACE2), /* ~= 10^-115 */
            SKY_U64(0x9D9BA7832936EDC0), SKY_U64(0xD54B944B84AA4C0D), /* ~= 10^-114 */
            SKY_U64(0xC5029163F384A931), SKY_U64(0x0A9E795E65D4DF11), /* ~= 10^-113 */
            SKY_U64(0xF64335BCF065D37D), SKY_U64(0x4D4617B5FF4A16D5), /* ~= 10^-112 */
            SKY_U64(0x99EA0196163FA42E), SKY_U64(0x504BCED1BF8E4E45), /* ~= 10^-111 */
            SKY_U64(0xC06481FB9BCF8D39), SKY_U64(0xE45EC2862F71E1D6), /* ~= 10^-110 */
            SKY_U64(0xF07DA27A82C37088), SKY_U64(0x5D767327BB4E5A4C), /* ~= 10^-109 */
            SKY_U64(0x964E858C91BA2655), SKY_U64(0x3A6A07F8D510F86F), /* ~= 10^-108 */
            SKY_U64(0xBBE226EFB628AFEA), SKY_U64(0x890489F70A55368B), /* ~= 10^-107 */
            SKY_U64(0xEADAB0ABA3B2DBE5), SKY_U64(0x2B45AC74CCEA842E), /* ~= 10^-106 */
            SKY_U64(0x92C8AE6B464FC96F), SKY_U64(0x3B0B8BC90012929D), /* ~= 10^-105 */
            SKY_U64(0xB77ADA0617E3BBCB), SKY_U64(0x09CE6EBB40173744), /* ~= 10^-104 */
            SKY_U64(0xE55990879DDCAABD), SKY_U64(0xCC420A6A101D0515), /* ~= 10^-103 */
            SKY_U64(0x8F57FA54C2A9EAB6), SKY_U64(0x9FA946824A12232D), /* ~= 10^-102 */
            SKY_U64(0xB32DF8E9F3546564), SKY_U64(0x47939822DC96ABF9), /* ~= 10^-101 */
            SKY_U64(0xDFF9772470297EBD), SKY_U64(0x59787E2B93BC56F7), /* ~= 10^-100 */
            SKY_U64(0x8BFBEA76C619EF36), SKY_U64(0x57EB4EDB3C55B65A), /* ~= 10^-99 */
            SKY_U64(0xAEFAE51477A06B03), SKY_U64(0xEDE622920B6B23F1), /* ~= 10^-98 */
            SKY_U64(0xDAB99E59958885C4), SKY_U64(0xE95FAB368E45ECED), /* ~= 10^-97 */
            SKY_U64(0x88B402F7FD75539B), SKY_U64(0x11DBCB0218EBB414), /* ~= 10^-96 */
            SKY_U64(0xAAE103B5FCD2A881), SKY_U64(0xD652BDC29F26A119), /* ~= 10^-95 */
            SKY_U64(0xD59944A37C0752A2), SKY_U64(0x4BE76D3346F0495F), /* ~= 10^-94 */
            SKY_U64(0x857FCAE62D8493A5), SKY_U64(0x6F70A4400C562DDB), /* ~= 10^-93 */
            SKY_U64(0xA6DFBD9FB8E5B88E), SKY_U64(0xCB4CCD500F6BB952), /* ~= 10^-92 */
            SKY_U64(0xD097AD07A71F26B2), SKY_U64(0x7E2000A41346A7A7), /* ~= 10^-91 */
            SKY_U64(0x825ECC24C873782F), SKY_U64(0x8ED400668C0C28C8), /* ~= 10^-90 */
            SKY_U64(0xA2F67F2DFA90563B), SKY_U64(0x728900802F0F32FA), /* ~= 10^-89 */
            SKY_U64(0xCBB41EF979346BCA), SKY_U64(0x4F2B40A03AD2FFB9), /* ~= 10^-88 */
            SKY_U64(0xFEA126B7D78186BC), SKY_U64(0xE2F610C84987BFA8), /* ~= 10^-87 */
            SKY_U64(0x9F24B832E6B0F436), SKY_U64(0x0DD9CA7D2DF4D7C9), /* ~= 10^-86 */
            SKY_U64(0xC6EDE63FA05D3143), SKY_U64(0x91503D1C79720DBB), /* ~= 10^-85 */
            SKY_U64(0xF8A95FCF88747D94), SKY_U64(0x75A44C6397CE912A), /* ~= 10^-84 */
            SKY_U64(0x9B69DBE1B548CE7C), SKY_U64(0xC986AFBE3EE11ABA), /* ~= 10^-83 */
            SKY_U64(0xC24452DA229B021B), SKY_U64(0xFBE85BADCE996168), /* ~= 10^-82 */
            SKY_U64(0xF2D56790AB41C2A2), SKY_U64(0xFAE27299423FB9C3), /* ~= 10^-81 */
            SKY_U64(0x97C560BA6B0919A5), SKY_U64(0xDCCD879FC967D41A), /* ~= 10^-80 */
            SKY_U64(0xBDB6B8E905CB600F), SKY_U64(0x5400E987BBC1C920), /* ~= 10^-79 */
            SKY_U64(0xED246723473E3813), SKY_U64(0x290123E9AAB23B68), /* ~= 10^-78 */
            SKY_U64(0x9436C0760C86E30B), SKY_U64(0xF9A0B6720AAF6521), /* ~= 10^-77 */
            SKY_U64(0xB94470938FA89BCE), SKY_U64(0xF808E40E8D5B3E69), /* ~= 10^-76 */
            SKY_U64(0xE7958CB87392C2C2), SKY_U64(0xB60B1D1230B20E04), /* ~= 10^-75 */
            SKY_U64(0x90BD77F3483BB9B9), SKY_U64(0xB1C6F22B5E6F48C2), /* ~= 10^-74 */
            SKY_U64(0xB4ECD5F01A4AA828), SKY_U64(0x1E38AEB6360B1AF3), /* ~= 10^-73 */
            SKY_U64(0xE2280B6C20DD5232), SKY_U64(0x25C6DA63C38DE1B0), /* ~= 10^-72 */
            SKY_U64(0x8D590723948A535F), SKY_U64(0x579C487E5A38AD0E), /* ~= 10^-71 */
            SKY_U64(0xB0AF48EC79ACE837), SKY_U64(0x2D835A9DF0C6D851), /* ~= 10^-70 */
            SKY_U64(0xDCDB1B2798182244), SKY_U64(0xF8E431456CF88E65), /* ~= 10^-69 */
            SKY_U64(0x8A08F0F8BF0F156B), SKY_U64(0x1B8E9ECB641B58FF), /* ~= 10^-68 */
            SKY_U64(0xAC8B2D36EED2DAC5), SKY_U64(0xE272467E3D222F3F), /* ~= 10^-67 */
            SKY_U64(0xD7ADF884AA879177), SKY_U64(0x5B0ED81DCC6ABB0F), /* ~= 10^-66 */
            SKY_U64(0x86CCBB52EA94BAEA), SKY_U64(0x98E947129FC2B4E9), /* ~= 10^-65 */
            SKY_U64(0xA87FEA27A539E9A5), SKY_U64(0x3F2398D747B36224), /* ~= 10^-64 */
            SKY_U64(0xD29FE4B18E88640E), SKY_U64(0x8EEC7F0D19A03AAD), /* ~= 10^-63 */
            SKY_U64(0x83A3EEEEF9153E89), SKY_U64(0x1953CF68300424AC), /* ~= 10^-62 */
            SKY_U64(0xA48CEAAAB75A8E2B), SKY_U64(0x5FA8C3423C052DD7), /* ~= 10^-61 */
            SKY_U64(0xCDB02555653131B6), SKY_U64(0x3792F412CB06794D), /* ~= 10^-60 */
            SKY_U64(0x808E17555F3EBF11), SKY_U64(0xE2BBD88BBEE40BD0), /* ~= 10^-59 */
            SKY_U64(0xA0B19D2AB70E6ED6), SKY_U64(0x5B6ACEAEAE9D0EC4), /* ~= 10^-58 */
            SKY_U64(0xC8DE047564D20A8B), SKY_U64(0xF245825A5A445275), /* ~= 10^-57 */
            SKY_U64(0xFB158592BE068D2E), SKY_U64(0xEED6E2F0F0D56712), /* ~= 10^-56 */
            SKY_U64(0x9CED737BB6C4183D), SKY_U64(0x55464DD69685606B), /* ~= 10^-55 */
            SKY_U64(0xC428D05AA4751E4C), SKY_U64(0xAA97E14C3C26B886), /* ~= 10^-54 */
            SKY_U64(0xF53304714D9265DF), SKY_U64(0xD53DD99F4B3066A8), /* ~= 10^-53 */
            SKY_U64(0x993FE2C6D07B7FAB), SKY_U64(0xE546A8038EFE4029), /* ~= 10^-52 */
            SKY_U64(0xBF8FDB78849A5F96), SKY_U64(0xDE98520472BDD033), /* ~= 10^-51 */
            SKY_U64(0xEF73D256A5C0F77C), SKY_U64(0x963E66858F6D4440), /* ~= 10^-50 */
            SKY_U64(0x95A8637627989AAD), SKY_U64(0xDDE7001379A44AA8), /* ~= 10^-49 */
            SKY_U64(0xBB127C53B17EC159), SKY_U64(0x5560C018580D5D52), /* ~= 10^-48 */
            SKY_U64(0xE9D71B689DDE71AF), SKY_U64(0xAAB8F01E6E10B4A6), /* ~= 10^-47 */
            SKY_U64(0x9226712162AB070D), SKY_U64(0xCAB3961304CA70E8), /* ~= 10^-46 */
            SKY_U64(0xB6B00D69BB55C8D1), SKY_U64(0x3D607B97C5FD0D22), /* ~= 10^-45 */
            SKY_U64(0xE45C10C42A2B3B05), SKY_U64(0x8CB89A7DB77C506A), /* ~= 10^-44 */
            SKY_U64(0x8EB98A7A9A5B04E3), SKY_U64(0x77F3608E92ADB242), /* ~= 10^-43 */
            SKY_U64(0xB267ED1940F1C61C), SKY_U64(0x55F038B237591ED3), /* ~= 10^-42 */
            SKY_U64(0xDF01E85F912E37A3), SKY_U64(0x6B6C46DEC52F6688), /* ~= 10^-41 */
            SKY_U64(0x8B61313BBABCE2C6), SKY_U64(0x2323AC4B3B3DA015), /* ~= 10^-40 */
            SKY_U64(0xAE397D8AA96C1B77), SKY_U64(0xABEC975E0A0D081A), /* ~= 10^-39 */
            SKY_U64(0xD9C7DCED53C72255), SKY_U64(0x96E7BD358C904A21), /* ~= 10^-38 */
            SKY_U64(0x881CEA14545C7575), SKY_U64(0x7E50D64177DA2E54), /* ~= 10^-37 */
            SKY_U64(0xAA242499697392D2), SKY_U64(0xDDE50BD1D5D0B9E9), /* ~= 10^-36 */
            SKY_U64(0xD4AD2DBFC3D07787), SKY_U64(0x955E4EC64B44E864), /* ~= 10^-35 */
            SKY_U64(0x84EC3C97DA624AB4), SKY_U64(0xBD5AF13BEF0B113E), /* ~= 10^-34 */
            SKY_U64(0xA6274BBDD0FADD61), SKY_U64(0xECB1AD8AEACDD58E), /* ~= 10^-33 */
            SKY_U64(0xCFB11EAD453994BA), SKY_U64(0x67DE18EDA5814AF2), /* ~= 10^-32 */
            SKY_U64(0x81CEB32C4B43FCF4), SKY_U64(0x80EACF948770CED7), /* ~= 10^-31 */
            SKY_U64(0xA2425FF75E14FC31), SKY_U64(0xA1258379A94D028D), /* ~= 10^-30 */
            SKY_U64(0xCAD2F7F5359A3B3E), SKY_U64(0x096EE45813A04330), /* ~= 10^-29 */
            SKY_U64(0xFD87B5F28300CA0D), SKY_U64(0x8BCA9D6E188853FC), /* ~= 10^-28 */
            SKY_U64(0x9E74D1B791E07E48), SKY_U64(0x775EA264CF55347D), /* ~= 10^-27 */
            SKY_U64(0xC612062576589DDA), SKY_U64(0x95364AFE032A819D), /* ~= 10^-26 */
            SKY_U64(0xF79687AED3EEC551), SKY_U64(0x3A83DDBD83F52204), /* ~= 10^-25 */
            SKY_U64(0x9ABE14CD44753B52), SKY_U64(0xC4926A9672793542), /* ~= 10^-24 */
            SKY_U64(0xC16D9A0095928A27), SKY_U64(0x75B7053C0F178293), /* ~= 10^-23 */
            SKY_U64(0xF1C90080BAF72CB1), SKY_U64(0x5324C68B12DD6338), /* ~= 10^-22 */
            SKY_U64(0x971DA05074DA7BEE), SKY_U64(0xD3F6FC16EBCA5E03), /* ~= 10^-21 */
            SKY_U64(0xBCE5086492111AEA), SKY_U64(0x88F4BB1CA6BCF584), /* ~= 10^-20 */
            SKY_U64(0xEC1E4A7DB69561A5), SKY_U64(0x2B31E9E3D06C32E5), /* ~= 10^-19 */
            SKY_U64(0x9392EE8E921D5D07), SKY_U64(0x3AFF322E62439FCF), /* ~= 10^-18 */
            SKY_U64(0xB877AA3236A4B449), SKY_U64(0x09BEFEB9FAD487C2), /* ~= 10^-17 */
            SKY_U64(0xE69594BEC44DE15B), SKY_U64(0x4C2EBE687989A9B3), /* ~= 10^-16 */
            SKY_U64(0x901D7CF73AB0ACD9), SKY_U64(0x0F9D37014BF60A10), /* ~= 10^-15 */
            SKY_U64(0xB424DC35095CD80F), SKY_U64(0x538484C19EF38C94), /* ~= 10^-14 */
            SKY_U64(0xE12E13424BB40E13), SKY_U64(0x2865A5F206B06FB9), /* ~= 10^-13 */
            SKY_U64(0x8CBCCC096F5088CB), SKY_U64(0xF93F87B7442E45D3), /* ~= 10^-12 */
            SKY_U64(0xAFEBFF0BCB24AAFE), SKY_U64(0xF78F69A51539D748), /* ~= 10^-11 */
            SKY_U64(0xDBE6FECEBDEDD5BE), SKY_U64(0xB573440E5A884D1B), /* ~= 10^-10 */
            SKY_U64(0x89705F4136B4A597), SKY_U64(0x31680A88F8953030), /* ~= 10^-9 */
            SKY_U64(0xABCC77118461CEFC), SKY_U64(0xFDC20D2B36BA7C3D), /* ~= 10^-8 */
            SKY_U64(0xD6BF94D5E57A42BC), SKY_U64(0x3D32907604691B4C), /* ~= 10^-7 */
            SKY_U64(0x8637BD05AF6C69B5), SKY_U64(0xA63F9A49C2C1B10F), /* ~= 10^-6 */
            SKY_U64(0xA7C5AC471B478423), SKY_U64(0x0FCF80DC33721D53), /* ~= 10^-5 */
            SKY_U64(0xD1B71758E219652B), SKY_U64(0xD3C36113404EA4A8), /* ~= 10^-4 */
            SKY_U64(0x83126E978D4FDF3B), SKY_U64(0x645A1CAC083126E9), /* ~= 10^-3 */
            SKY_U64(0xA3D70A3D70A3D70A), SKY_U64(0x3D70A3D70A3D70A3), /* ~= 10^-2 */
            SKY_U64(0xCCCCCCCCCCCCCCCC), SKY_U64(0xCCCCCCCCCCCCCCCC), /* ~= 10^-1 */
            SKY_U64(0x8000000000000000), SKY_U64(0x0000000000000000), /* == 10^0 */
            SKY_U64(0xA000000000000000), SKY_U64(0x0000000000000000), /* == 10^1 */
            SKY_U64(0xC800000000000000), SKY_U64(0x0000000000000000), /* == 10^2 */
            SKY_U64(0xFA00000000000000), SKY_U64(0x0000000000000000), /* == 10^3 */
            SKY_U64(0x9C40000000000000), SKY_U64(0x0000000000000000), /* == 10^4 */
            SKY_U64(0xC350000000000000), SKY_U64(0x0000000000000000), /* == 10^5 */
            SKY_U64(0xF424000000000000), SKY_U64(0x0000000000000000), /* == 10^6 */
            SKY_U64(0x9896800000000000), SKY_U64(0x0000000000000000), /* == 10^7 */
            SKY_U64(0xBEBC200000000000), SKY_U64(0x0000000000000000), /* == 10^8 */
            SKY_U64(0xEE6B280000000000), SKY_U64(0x0000000000000000), /* == 10^9 */
            SKY_U64(0x9502F90000000000), SKY_U64(0x0000000000000000), /* == 10^10 */
            SKY_U64(0xBA43B74000000000), SKY_U64(0x0000000000000000), /* == 10^11 */
            SKY_U64(0xE8D4A51000000000), SKY_U64(0x0000000000000000), /* == 10^12 */
            SKY_U64(0x9184E72A00000000), SKY_U64(0x0000000000000000), /* == 10^13 */
            SKY_U64(0xB5E620F480000000), SKY_U64(0x0000000000000000), /* == 10^14 */
            SKY_U64(0xE35FA931A0000000), SKY_U64(0x0000000000000000), /* == 10^15 */
            SKY_U64(0x8E1BC9BF04000000), SKY_U64(0x0000000000000000), /* == 10^16 */
            SKY_U64(0xB1A2BC2EC5000000), SKY_U64(0x0000000000000000), /* == 10^17 */
            SKY_U64(0xDE0B6B3A76400000), SKY_U64(0x0000000000000000), /* == 10^18 */
            SKY_U64(0x8AC7230489E80000), SKY_U64(0x0000000000000000), /* == 10^19 */
            SKY_U64(0xAD78EBC5AC620000), SKY_U64(0x0000000000000000), /* == 10^20 */
            SKY_U64(0xD8D726B7177A8000), SKY_U64(0x0000000000000000), /* == 10^21 */
            SKY_U64(0x878678326EAC9000), SKY_U64(0x0000000000000000), /* == 10^22 */
            SKY_U64(0xA968163F0A57B400), SKY_U64(0x0000000000000000), /* == 10^23 */
            SKY_U64(0xD3C21BCECCEDA100), SKY_U64(0x0000000000000000), /* == 10^24 */
            SKY_U64(0x84595161401484A0), SKY_U64(0x0000000000000000), /* == 10^25 */
            SKY_U64(0xA56FA5B99019A5C8), SKY_U64(0x0000000000000000), /* == 10^26 */
            SKY_U64(0xCECB8F27F4200F3A), SKY_U64(0x0000000000000000), /* == 10^27 */
            SKY_U64(0x813F3978F8940984), SKY_U64(0x4000000000000000), /* == 10^28 */
            SKY_U64(0xA18F07D736B90BE5), SKY_U64(0x5000000000000000), /* == 10^29 */
            SKY_U64(0xC9F2C9CD04674EDE), SKY_U64(0xA400000000000000), /* == 10^30 */
            SKY_U64(0xFC6F7C4045812296), SKY_U64(0x4D00000000000000), /* == 10^31 */
            SKY_U64(0x9DC5ADA82B70B59D), SKY_U64(0xF020000000000000), /* == 10^32 */
            SKY_U64(0xC5371912364CE305), SKY_U64(0x6C28000000000000), /* == 10^33 */
            SKY_U64(0xF684DF56C3E01BC6), SKY_U64(0xC732000000000000), /* == 10^34 */
            SKY_U64(0x9A130B963A6C115C), SKY_U64(0x3C7F400000000000), /* == 10^35 */
            SKY_U64(0xC097CE7BC90715B3), SKY_U64(0x4B9F100000000000), /* == 10^36 */
            SKY_U64(0xF0BDC21ABB48DB20), SKY_U64(0x1E86D40000000000), /* == 10^37 */
            SKY_U64(0x96769950B50D88F4), SKY_U64(0x1314448000000000), /* == 10^38 */
            SKY_U64(0xBC143FA4E250EB31), SKY_U64(0x17D955A000000000), /* == 10^39 */
            SKY_U64(0xEB194F8E1AE525FD), SKY_U64(0x5DCFAB0800000000), /* == 10^40 */
            SKY_U64(0x92EFD1B8D0CF37BE), SKY_U64(0x5AA1CAE500000000), /* == 10^41 */
            SKY_U64(0xB7ABC627050305AD), SKY_U64(0xF14A3D9E40000000), /* == 10^42 */
            SKY_U64(0xE596B7B0C643C719), SKY_U64(0x6D9CCD05D0000000), /* == 10^43 */
            SKY_U64(0x8F7E32CE7BEA5C6F), SKY_U64(0xE4820023A2000000), /* == 10^44 */
            SKY_U64(0xB35DBF821AE4F38B), SKY_U64(0xDDA2802C8A800000), /* == 10^45 */
            SKY_U64(0xE0352F62A19E306E), SKY_U64(0xD50B2037AD200000), /* == 10^46 */
            SKY_U64(0x8C213D9DA502DE45), SKY_U64(0x4526F422CC340000), /* == 10^47 */
            SKY_U64(0xAF298D050E4395D6), SKY_U64(0x9670B12B7F410000), /* == 10^48 */
            SKY_U64(0xDAF3F04651D47B4C), SKY_U64(0x3C0CDD765F114000), /* == 10^49 */
            SKY_U64(0x88D8762BF324CD0F), SKY_U64(0xA5880A69FB6AC800), /* == 10^50 */
            SKY_U64(0xAB0E93B6EFEE0053), SKY_U64(0x8EEA0D047A457A00), /* == 10^51 */
            SKY_U64(0xD5D238A4ABE98068), SKY_U64(0x72A4904598D6D880), /* == 10^52 */
            SKY_U64(0x85A36366EB71F041), SKY_U64(0x47A6DA2B7F864750), /* == 10^53 */
            SKY_U64(0xA70C3C40A64E6C51), SKY_U64(0x999090B65F67D924), /* == 10^54 */
            SKY_U64(0xD0CF4B50CFE20765), SKY_U64(0xFFF4B4E3F741CF6D), /* == 10^55 */
            SKY_U64(0x82818F1281ED449F), SKY_U64(0xBFF8F10E7A8921A4), /* ~= 10^56 */
            SKY_U64(0xA321F2D7226895C7), SKY_U64(0xAFF72D52192B6A0D), /* ~= 10^57 */
            SKY_U64(0xCBEA6F8CEB02BB39), SKY_U64(0x9BF4F8A69F764490), /* ~= 10^58 */
            SKY_U64(0xFEE50B7025C36A08), SKY_U64(0x02F236D04753D5B4), /* ~= 10^59 */
            SKY_U64(0x9F4F2726179A2245), SKY_U64(0x01D762422C946590), /* ~= 10^60 */
            SKY_U64(0xC722F0EF9D80AAD6), SKY_U64(0x424D3AD2B7B97EF5), /* ~= 10^61 */
            SKY_U64(0xF8EBAD2B84E0D58B), SKY_U64(0xD2E0898765A7DEB2), /* ~= 10^62 */
            SKY_U64(0x9B934C3B330C8577), SKY_U64(0x63CC55F49F88EB2F), /* ~= 10^63 */
            SKY_U64(0xC2781F49FFCFA6D5), SKY_U64(0x3CBF6B71C76B25FB), /* ~= 10^64 */
            SKY_U64(0xF316271C7FC3908A), SKY_U64(0x8BEF464E3945EF7A), /* ~= 10^65 */
            SKY_U64(0x97EDD871CFDA3A56), SKY_U64(0x97758BF0E3CBB5AC), /* ~= 10^66 */
            SKY_U64(0xBDE94E8E43D0C8EC), SKY_U64(0x3D52EEED1CBEA317), /* ~= 10^67 */
            SKY_U64(0xED63A231D4C4FB27), SKY_U64(0x4CA7AAA863EE4BDD), /* ~= 10^68 */
            SKY_U64(0x945E455F24FB1CF8), SKY_U64(0x8FE8CAA93E74EF6A), /* ~= 10^69 */
            SKY_U64(0xB975D6B6EE39E436), SKY_U64(0xB3E2FD538E122B44), /* ~= 10^70 */
            SKY_U64(0xE7D34C64A9C85D44), SKY_U64(0x60DBBCA87196B616), /* ~= 10^71 */
            SKY_U64(0x90E40FBEEA1D3A4A), SKY_U64(0xBC8955E946FE31CD), /* ~= 10^72 */
            SKY_U64(0xB51D13AEA4A488DD), SKY_U64(0x6BABAB6398BDBE41), /* ~= 10^73 */
            SKY_U64(0xE264589A4DCDAB14), SKY_U64(0xC696963C7EED2DD1), /* ~= 10^74 */
            SKY_U64(0x8D7EB76070A08AEC), SKY_U64(0xFC1E1DE5CF543CA2), /* ~= 10^75 */
            SKY_U64(0xB0DE65388CC8ADA8), SKY_U64(0x3B25A55F43294BCB), /* ~= 10^76 */
            SKY_U64(0xDD15FE86AFFAD912), SKY_U64(0x49EF0EB713F39EBE), /* ~= 10^77 */
            SKY_U64(0x8A2DBF142DFCC7AB), SKY_U64(0x6E3569326C784337), /* ~= 10^78 */
            SKY_U64(0xACB92ED9397BF996), SKY_U64(0x49C2C37F07965404), /* ~= 10^79 */
            SKY_U64(0xD7E77A8F87DAF7FB), SKY_U64(0xDC33745EC97BE906), /* ~= 10^80 */
            SKY_U64(0x86F0AC99B4E8DAFD), SKY_U64(0x69A028BB3DED71A3), /* ~= 10^81 */
            SKY_U64(0xA8ACD7C0222311BC), SKY_U64(0xC40832EA0D68CE0C), /* ~= 10^82 */
            SKY_U64(0xD2D80DB02AABD62B), SKY_U64(0xF50A3FA490C30190), /* ~= 10^83 */
            SKY_U64(0x83C7088E1AAB65DB), SKY_U64(0x792667C6DA79E0FA), /* ~= 10^84 */
            SKY_U64(0xA4B8CAB1A1563F52), SKY_U64(0x577001B891185938), /* ~= 10^85 */
            SKY_U64(0xCDE6FD5E09ABCF26), SKY_U64(0xED4C0226B55E6F86), /* ~= 10^86 */
            SKY_U64(0x80B05E5AC60B6178), SKY_U64(0x544F8158315B05B4), /* ~= 10^87 */
            SKY_U64(0xA0DC75F1778E39D6), SKY_U64(0x696361AE3DB1C721), /* ~= 10^88 */
            SKY_U64(0xC913936DD571C84C), SKY_U64(0x03BC3A19CD1E38E9), /* ~= 10^89 */
            SKY_U64(0xFB5878494ACE3A5F), SKY_U64(0x04AB48A04065C723), /* ~= 10^90 */
            SKY_U64(0x9D174B2DCEC0E47B), SKY_U64(0x62EB0D64283F9C76), /* ~= 10^91 */
            SKY_U64(0xC45D1DF942711D9A), SKY_U64(0x3BA5D0BD324F8394), /* ~= 10^92 */
            SKY_U64(0xF5746577930D6500), SKY_U64(0xCA8F44EC7EE36479), /* ~= 10^93 */
            SKY_U64(0x9968BF6ABBE85F20), SKY_U64(0x7E998B13CF4E1ECB), /* ~= 10^94 */
            SKY_U64(0xBFC2EF456AE276E8), SKY_U64(0x9E3FEDD8C321A67E), /* ~= 10^95 */
            SKY_U64(0xEFB3AB16C59B14A2), SKY_U64(0xC5CFE94EF3EA101E), /* ~= 10^96 */
            SKY_U64(0x95D04AEE3B80ECE5), SKY_U64(0xBBA1F1D158724A12), /* ~= 10^97 */
            SKY_U64(0xBB445DA9CA61281F), SKY_U64(0x2A8A6E45AE8EDC97), /* ~= 10^98 */
            SKY_U64(0xEA1575143CF97226), SKY_U64(0xF52D09D71A3293BD), /* ~= 10^99 */
            SKY_U64(0x924D692CA61BE758), SKY_U64(0x593C2626705F9C56), /* ~= 10^100 */
            SKY_U64(0xB6E0C377CFA2E12E), SKY_U64(0x6F8B2FB00C77836C), /* ~= 10^101 */
            SKY_U64(0xE498F455C38B997A), SKY_U64(0x0B6DFB9C0F956447), /* ~= 10^102 */
            SKY_U64(0x8EDF98B59A373FEC), SKY_U64(0x4724BD4189BD5EAC), /* ~= 10^103 */
            SKY_U64(0xB2977EE300C50FE7), SKY_U64(0x58EDEC91EC2CB657), /* ~= 10^104 */
            SKY_U64(0xDF3D5E9BC0F653E1), SKY_U64(0x2F2967B66737E3ED), /* ~= 10^105 */
            SKY_U64(0x8B865B215899F46C), SKY_U64(0xBD79E0D20082EE74), /* ~= 10^106 */
            SKY_U64(0xAE67F1E9AEC07187), SKY_U64(0xECD8590680A3AA11), /* ~= 10^107 */
            SKY_U64(0xDA01EE641A708DE9), SKY_U64(0xE80E6F4820CC9495), /* ~= 10^108 */
            SKY_U64(0x884134FE908658B2), SKY_U64(0x3109058D147FDCDD), /* ~= 10^109 */
            SKY_U64(0xAA51823E34A7EEDE), SKY_U64(0xBD4B46F0599FD415), /* ~= 10^110 */
            SKY_U64(0xD4E5E2CDC1D1EA96), SKY_U64(0x6C9E18AC7007C91A), /* ~= 10^111 */
            SKY_U64(0x850FADC09923329E), SKY_U64(0x03E2CF6BC604DDB0), /* ~= 10^112 */
            SKY_U64(0xA6539930BF6BFF45), SKY_U64(0x84DB8346B786151C), /* ~= 10^113 */
            SKY_U64(0xCFE87F7CEF46FF16), SKY_U64(0xE612641865679A63), /* ~= 10^114 */
            SKY_U64(0x81F14FAE158C5F6E), SKY_U64(0x4FCB7E8F3F60C07E), /* ~= 10^115 */
            SKY_U64(0xA26DA3999AEF7749), SKY_U64(0xE3BE5E330F38F09D), /* ~= 10^116 */
            SKY_U64(0xCB090C8001AB551C), SKY_U64(0x5CADF5BFD3072CC5), /* ~= 10^117 */
            SKY_U64(0xFDCB4FA002162A63), SKY_U64(0x73D9732FC7C8F7F6), /* ~= 10^118 */
            SKY_U64(0x9E9F11C4014DDA7E), SKY_U64(0x2867E7FDDCDD9AFA), /* ~= 10^119 */
            SKY_U64(0xC646D63501A1511D), SKY_U64(0xB281E1FD541501B8), /* ~= 10^120 */
            SKY_U64(0xF7D88BC24209A565), SKY_U64(0x1F225A7CA91A4226), /* ~= 10^121 */
            SKY_U64(0x9AE757596946075F), SKY_U64(0x3375788DE9B06958), /* ~= 10^122 */
            SKY_U64(0xC1A12D2FC3978937), SKY_U64(0x0052D6B1641C83AE), /* ~= 10^123 */
            SKY_U64(0xF209787BB47D6B84), SKY_U64(0xC0678C5DBD23A49A), /* ~= 10^124 */
            SKY_U64(0x9745EB4D50CE6332), SKY_U64(0xF840B7BA963646E0), /* ~= 10^125 */
            SKY_U64(0xBD176620A501FBFF), SKY_U64(0xB650E5A93BC3D898), /* ~= 10^126 */
            SKY_U64(0xEC5D3FA8CE427AFF), SKY_U64(0xA3E51F138AB4CEBE), /* ~= 10^127 */
            SKY_U64(0x93BA47C980E98CDF), SKY_U64(0xC66F336C36B10137), /* ~= 10^128 */
            SKY_U64(0xB8A8D9BBE123F017), SKY_U64(0xB80B0047445D4184), /* ~= 10^129 */
            SKY_U64(0xE6D3102AD96CEC1D), SKY_U64(0xA60DC059157491E5), /* ~= 10^130 */
            SKY_U64(0x9043EA1AC7E41392), SKY_U64(0x87C89837AD68DB2F), /* ~= 10^131 */
            SKY_U64(0xB454E4A179DD1877), SKY_U64(0x29BABE4598C311FB), /* ~= 10^132 */
            SKY_U64(0xE16A1DC9D8545E94), SKY_U64(0xF4296DD6FEF3D67A), /* ~= 10^133 */
            SKY_U64(0x8CE2529E2734BB1D), SKY_U64(0x1899E4A65F58660C), /* ~= 10^134 */
            SKY_U64(0xB01AE745B101E9E4), SKY_U64(0x5EC05DCFF72E7F8F), /* ~= 10^135 */
            SKY_U64(0xDC21A1171D42645D), SKY_U64(0x76707543F4FA1F73), /* ~= 10^136 */
            SKY_U64(0x899504AE72497EBA), SKY_U64(0x6A06494A791C53A8), /* ~= 10^137 */
            SKY_U64(0xABFA45DA0EDBDE69), SKY_U64(0x0487DB9D17636892), /* ~= 10^138 */
            SKY_U64(0xD6F8D7509292D603), SKY_U64(0x45A9D2845D3C42B6), /* ~= 10^139 */
            SKY_U64(0x865B86925B9BC5C2), SKY_U64(0x0B8A2392BA45A9B2), /* ~= 10^140 */
            SKY_U64(0xA7F26836F282B732), SKY_U64(0x8E6CAC7768D7141E), /* ~= 10^141 */
            SKY_U64(0xD1EF0244AF2364FF), SKY_U64(0x3207D795430CD926), /* ~= 10^142 */
            SKY_U64(0x8335616AED761F1F), SKY_U64(0x7F44E6BD49E807B8), /* ~= 10^143 */
            SKY_U64(0xA402B9C5A8D3A6E7), SKY_U64(0x5F16206C9C6209A6), /* ~= 10^144 */
            SKY_U64(0xCD036837130890A1), SKY_U64(0x36DBA887C37A8C0F), /* ~= 10^145 */
            SKY_U64(0x802221226BE55A64), SKY_U64(0xC2494954DA2C9789), /* ~= 10^146 */
            SKY_U64(0xA02AA96B06DEB0FD), SKY_U64(0xF2DB9BAA10B7BD6C), /* ~= 10^147 */
            SKY_U64(0xC83553C5C8965D3D), SKY_U64(0x6F92829494E5ACC7), /* ~= 10^148 */
            SKY_U64(0xFA42A8B73ABBF48C), SKY_U64(0xCB772339BA1F17F9), /* ~= 10^149 */
            SKY_U64(0x9C69A97284B578D7), SKY_U64(0xFF2A760414536EFB), /* ~= 10^150 */
            SKY_U64(0xC38413CF25E2D70D), SKY_U64(0xFEF5138519684ABA), /* ~= 10^151 */
            SKY_U64(0xF46518C2EF5B8CD1), SKY_U64(0x7EB258665FC25D69), /* ~= 10^152 */
            SKY_U64(0x98BF2F79D5993802), SKY_U64(0xEF2F773FFBD97A61), /* ~= 10^153 */
            SKY_U64(0xBEEEFB584AFF8603), SKY_U64(0xAAFB550FFACFD8FA), /* ~= 10^154 */
            SKY_U64(0xEEAABA2E5DBF6784), SKY_U64(0x95BA2A53F983CF38), /* ~= 10^155 */
            SKY_U64(0x952AB45CFA97A0B2), SKY_U64(0xDD945A747BF26183), /* ~= 10^156 */
            SKY_U64(0xBA756174393D88DF), SKY_U64(0x94F971119AEEF9E4), /* ~= 10^157 */
            SKY_U64(0xE912B9D1478CEB17), SKY_U64(0x7A37CD5601AAB85D), /* ~= 10^158 */
            SKY_U64(0x91ABB422CCB812EE), SKY_U64(0xAC62E055C10AB33A), /* ~= 10^159 */
            SKY_U64(0xB616A12B7FE617AA), SKY_U64(0x577B986B314D6009), /* ~= 10^160 */
            SKY_U64(0xE39C49765FDF9D94), SKY_U64(0xED5A7E85FDA0B80B), /* ~= 10^161 */
            SKY_U64(0x8E41ADE9FBEBC27D), SKY_U64(0x14588F13BE847307), /* ~= 10^162 */
            SKY_U64(0xB1D219647AE6B31C), SKY_U64(0x596EB2D8AE258FC8), /* ~= 10^163 */
            SKY_U64(0xDE469FBD99A05FE3), SKY_U64(0x6FCA5F8ED9AEF3BB), /* ~= 10^164 */
            SKY_U64(0x8AEC23D680043BEE), SKY_U64(0x25DE7BB9480D5854), /* ~= 10^165 */
            SKY_U64(0xADA72CCC20054AE9), SKY_U64(0xAF561AA79A10AE6A), /* ~= 10^166 */
            SKY_U64(0xD910F7FF28069DA4), SKY_U64(0x1B2BA1518094DA04), /* ~= 10^167 */
            SKY_U64(0x87AA9AFF79042286), SKY_U64(0x90FB44D2F05D0842), /* ~= 10^168 */
            SKY_U64(0xA99541BF57452B28), SKY_U64(0x353A1607AC744A53), /* ~= 10^169 */
            SKY_U64(0xD3FA922F2D1675F2), SKY_U64(0x42889B8997915CE8), /* ~= 10^170 */
            SKY_U64(0x847C9B5D7C2E09B7), SKY_U64(0x69956135FEBADA11), /* ~= 10^171 */
            SKY_U64(0xA59BC234DB398C25), SKY_U64(0x43FAB9837E699095), /* ~= 10^172 */
            SKY_U64(0xCF02B2C21207EF2E), SKY_U64(0x94F967E45E03F4BB), /* ~= 10^173 */
            SKY_U64(0x8161AFB94B44F57D), SKY_U64(0x1D1BE0EEBAC278F5), /* ~= 10^174 */
            SKY_U64(0xA1BA1BA79E1632DC), SKY_U64(0x6462D92A69731732), /* ~= 10^175 */
            SKY_U64(0xCA28A291859BBF93), SKY_U64(0x7D7B8F7503CFDCFE), /* ~= 10^176 */
            SKY_U64(0xFCB2CB35E702AF78), SKY_U64(0x5CDA735244C3D43E), /* ~= 10^177 */
            SKY_U64(0x9DEFBF01B061ADAB), SKY_U64(0x3A0888136AFA64A7), /* ~= 10^178 */
            SKY_U64(0xC56BAEC21C7A1916), SKY_U64(0x088AAA1845B8FDD0), /* ~= 10^179 */
            SKY_U64(0xF6C69A72A3989F5B), SKY_U64(0x8AAD549E57273D45), /* ~= 10^180 */
            SKY_U64(0x9A3C2087A63F6399), SKY_U64(0x36AC54E2F678864B), /* ~= 10^181 */
            SKY_U64(0xC0CB28A98FCF3C7F), SKY_U64(0x84576A1BB416A7DD), /* ~= 10^182 */
            SKY_U64(0xF0FDF2D3F3C30B9F), SKY_U64(0x656D44A2A11C51D5), /* ~= 10^183 */
            SKY_U64(0x969EB7C47859E743), SKY_U64(0x9F644AE5A4B1B325), /* ~= 10^184 */
            SKY_U64(0xBC4665B596706114), SKY_U64(0x873D5D9F0DDE1FEE), /* ~= 10^185 */
            SKY_U64(0xEB57FF22FC0C7959), SKY_U64(0xA90CB506D155A7EA), /* ~= 10^186 */
            SKY_U64(0x9316FF75DD87CBD8), SKY_U64(0x09A7F12442D588F2), /* ~= 10^187 */
            SKY_U64(0xB7DCBF5354E9BECE), SKY_U64(0x0C11ED6D538AEB2F), /* ~= 10^188 */
            SKY_U64(0xE5D3EF282A242E81), SKY_U64(0x8F1668C8A86DA5FA), /* ~= 10^189 */
            SKY_U64(0x8FA475791A569D10), SKY_U64(0xF96E017D694487BC), /* ~= 10^190 */
            SKY_U64(0xB38D92D760EC4455), SKY_U64(0x37C981DCC395A9AC), /* ~= 10^191 */
            SKY_U64(0xE070F78D3927556A), SKY_U64(0x85BBE253F47B1417), /* ~= 10^192 */
            SKY_U64(0x8C469AB843B89562), SKY_U64(0x93956D7478CCEC8E), /* ~= 10^193 */
            SKY_U64(0xAF58416654A6BABB), SKY_U64(0x387AC8D1970027B2), /* ~= 10^194 */
            SKY_U64(0xDB2E51BFE9D0696A), SKY_U64(0x06997B05FCC0319E), /* ~= 10^195 */
            SKY_U64(0x88FCF317F22241E2), SKY_U64(0x441FECE3BDF81F03), /* ~= 10^196 */
            SKY_U64(0xAB3C2FDDEEAAD25A), SKY_U64(0xD527E81CAD7626C3), /* ~= 10^197 */
            SKY_U64(0xD60B3BD56A5586F1), SKY_U64(0x8A71E223D8D3B074), /* ~= 10^198 */
            SKY_U64(0x85C7056562757456), SKY_U64(0xF6872D5667844E49), /* ~= 10^199 */
            SKY_U64(0xA738C6BEBB12D16C), SKY_U64(0xB428F8AC016561DB), /* ~= 10^200 */
            SKY_U64(0xD106F86E69D785C7), SKY_U64(0xE13336D701BEBA52), /* ~= 10^201 */
            SKY_U64(0x82A45B450226B39C), SKY_U64(0xECC0024661173473), /* ~= 10^202 */
            SKY_U64(0xA34D721642B06084), SKY_U64(0x27F002D7F95D0190), /* ~= 10^203 */
            SKY_U64(0xCC20CE9BD35C78A5), SKY_U64(0x31EC038DF7B441F4), /* ~= 10^204 */
            SKY_U64(0xFF290242C83396CE), SKY_U64(0x7E67047175A15271), /* ~= 10^205 */
            SKY_U64(0x9F79A169BD203E41), SKY_U64(0x0F0062C6E984D386), /* ~= 10^206 */
            SKY_U64(0xC75809C42C684DD1), SKY_U64(0x52C07B78A3E60868), /* ~= 10^207 */
            SKY_U64(0xF92E0C3537826145), SKY_U64(0xA7709A56CCDF8A82), /* ~= 10^208 */
            SKY_U64(0x9BBCC7A142B17CCB), SKY_U64(0x88A66076400BB691), /* ~= 10^209 */
            SKY_U64(0xC2ABF989935DDBFE), SKY_U64(0x6ACFF893D00EA435), /* ~= 10^210 */
            SKY_U64(0xF356F7EBF83552FE), SKY_U64(0x0583F6B8C4124D43), /* ~= 10^211 */
            SKY_U64(0x98165AF37B2153DE), SKY_U64(0xC3727A337A8B704A), /* ~= 10^212 */
            SKY_U64(0xBE1BF1B059E9A8D6), SKY_U64(0x744F18C0592E4C5C), /* ~= 10^213 */
            SKY_U64(0xEDA2EE1C7064130C), SKY_U64(0x1162DEF06F79DF73), /* ~= 10^214 */
            SKY_U64(0x9485D4D1C63E8BE7), SKY_U64(0x8ADDCB5645AC2BA8), /* ~= 10^215 */
            SKY_U64(0xB9A74A0637CE2EE1), SKY_U64(0x6D953E2BD7173692), /* ~= 10^216 */
            SKY_U64(0xE8111C87C5C1BA99), SKY_U64(0xC8FA8DB6CCDD0437), /* ~= 10^217 */
            SKY_U64(0x910AB1D4DB9914A0), SKY_U64(0x1D9C9892400A22A2), /* ~= 10^218 */
            SKY_U64(0xB54D5E4A127F59C8), SKY_U64(0x2503BEB6D00CAB4B), /* ~= 10^219 */
            SKY_U64(0xE2A0B5DC971F303A), SKY_U64(0x2E44AE64840FD61D), /* ~= 10^220 */
            SKY_U64(0x8DA471A9DE737E24), SKY_U64(0x5CEAECFED289E5D2), /* ~= 10^221 */
            SKY_U64(0xB10D8E1456105DAD), SKY_U64(0x7425A83E872C5F47), /* ~= 10^222 */
            SKY_U64(0xDD50F1996B947518), SKY_U64(0xD12F124E28F77719), /* ~= 10^223 */
            SKY_U64(0x8A5296FFE33CC92F), SKY_U64(0x82BD6B70D99AAA6F), /* ~= 10^224 */
            SKY_U64(0xACE73CBFDC0BFB7B), SKY_U64(0x636CC64D1001550B), /* ~= 10^225 */
            SKY_U64(0xD8210BEFD30EFA5A), SKY_U64(0x3C47F7E05401AA4E), /* ~= 10^226 */
            SKY_U64(0x8714A775E3E95C78), SKY_U64(0x65ACFAEC34810A71), /* ~= 10^227 */
            SKY_U64(0xA8D9D1535CE3B396), SKY_U64(0x7F1839A741A14D0D), /* ~= 10^228 */
            SKY_U64(0xD31045A8341CA07C), SKY_U64(0x1EDE48111209A050), /* ~= 10^229 */
            SKY_U64(0x83EA2B892091E44D), SKY_U64(0x934AED0AAB460432), /* ~= 10^230 */
            SKY_U64(0xA4E4B66B68B65D60), SKY_U64(0xF81DA84D5617853F), /* ~= 10^231 */
            SKY_U64(0xCE1DE40642E3F4B9), SKY_U64(0x36251260AB9D668E), /* ~= 10^232 */
            SKY_U64(0x80D2AE83E9CE78F3), SKY_U64(0xC1D72B7C6B426019), /* ~= 10^233 */
            SKY_U64(0xA1075A24E4421730), SKY_U64(0xB24CF65B8612F81F), /* ~= 10^234 */
            SKY_U64(0xC94930AE1D529CFC), SKY_U64(0xDEE033F26797B627), /* ~= 10^235 */
            SKY_U64(0xFB9B7CD9A4A7443C), SKY_U64(0x169840EF017DA3B1), /* ~= 10^236 */
            SKY_U64(0x9D412E0806E88AA5), SKY_U64(0x8E1F289560EE864E), /* ~= 10^237 */
            SKY_U64(0xC491798A08A2AD4E), SKY_U64(0xF1A6F2BAB92A27E2), /* ~= 10^238 */
            SKY_U64(0xF5B5D7EC8ACB58A2), SKY_U64(0xAE10AF696774B1DB), /* ~= 10^239 */
            SKY_U64(0x9991A6F3D6BF1765), SKY_U64(0xACCA6DA1E0A8EF29), /* ~= 10^240 */
            SKY_U64(0xBFF610B0CC6EDD3F), SKY_U64(0x17FD090A58D32AF3), /* ~= 10^241 */
            SKY_U64(0xEFF394DCFF8A948E), SKY_U64(0xDDFC4B4CEF07F5B0), /* ~= 10^242 */
            SKY_U64(0x95F83D0A1FB69CD9), SKY_U64(0x4ABDAF101564F98E), /* ~= 10^243 */
            SKY_U64(0xBB764C4CA7A4440F), SKY_U64(0x9D6D1AD41ABE37F1), /* ~= 10^244 */
            SKY_U64(0xEA53DF5FD18D5513), SKY_U64(0x84C86189216DC5ED), /* ~= 10^245 */
            SKY_U64(0x92746B9BE2F8552C), SKY_U64(0x32FD3CF5B4E49BB4), /* ~= 10^246 */
            SKY_U64(0xB7118682DBB66A77), SKY_U64(0x3FBC8C33221DC2A1), /* ~= 10^247 */
            SKY_U64(0xE4D5E82392A40515), SKY_U64(0x0FABAF3FEAA5334A), /* ~= 10^248 */
            SKY_U64(0x8F05B1163BA6832D), SKY_U64(0x29CB4D87F2A7400E), /* ~= 10^249 */
            SKY_U64(0xB2C71D5BCA9023F8), SKY_U64(0x743E20E9EF511012), /* ~= 10^250 */
            SKY_U64(0xDF78E4B2BD342CF6), SKY_U64(0x914DA9246B255416), /* ~= 10^251 */
            SKY_U64(0x8BAB8EEFB6409C1A), SKY_U64(0x1AD089B6C2F7548E), /* ~= 10^252 */
            SKY_U64(0xAE9672ABA3D0C320), SKY_U64(0xA184AC2473B529B1), /* ~= 10^253 */
            SKY_U64(0xDA3C0F568CC4F3E8), SKY_U64(0xC9E5D72D90A2741E), /* ~= 10^254 */
            SKY_U64(0x8865899617FB1871), SKY_U64(0x7E2FA67C7A658892), /* ~= 10^255 */
            SKY_U64(0xAA7EEBFB9DF9DE8D), SKY_U64(0xDDBB901B98FEEAB7), /* ~= 10^256 */
            SKY_U64(0xD51EA6FA85785631), SKY_U64(0x552A74227F3EA565), /* ~= 10^257 */
            SKY_U64(0x8533285C936B35DE), SKY_U64(0xD53A88958F87275F), /* ~= 10^258 */
            SKY_U64(0xA67FF273B8460356), SKY_U64(0x8A892ABAF368F137), /* ~= 10^259 */
            SKY_U64(0xD01FEF10A657842C), SKY_U64(0x2D2B7569B0432D85), /* ~= 10^260 */
            SKY_U64(0x8213F56A67F6B29B), SKY_U64(0x9C3B29620E29FC73), /* ~= 10^261 */
            SKY_U64(0xA298F2C501F45F42), SKY_U64(0x8349F3BA91B47B8F), /* ~= 10^262 */
            SKY_U64(0xCB3F2F7642717713), SKY_U64(0x241C70A936219A73), /* ~= 10^263 */
            SKY_U64(0xFE0EFB53D30DD4D7), SKY_U64(0xED238CD383AA0110), /* ~= 10^264 */
            SKY_U64(0x9EC95D1463E8A506), SKY_U64(0xF4363804324A40AA), /* ~= 10^265 */
            SKY_U64(0xC67BB4597CE2CE48), SKY_U64(0xB143C6053EDCD0D5), /* ~= 10^266 */
            SKY_U64(0xF81AA16FDC1B81DA), SKY_U64(0xDD94B7868E94050A), /* ~= 10^267 */
            SKY_U64(0x9B10A4E5E9913128), SKY_U64(0xCA7CF2B4191C8326), /* ~= 10^268 */
            SKY_U64(0xC1D4CE1F63F57D72), SKY_U64(0xFD1C2F611F63A3F0), /* ~= 10^269 */
            SKY_U64(0xF24A01A73CF2DCCF), SKY_U64(0xBC633B39673C8CEC), /* ~= 10^270 */
            SKY_U64(0x976E41088617CA01), SKY_U64(0xD5BE0503E085D813), /* ~= 10^271 */
            SKY_U64(0xBD49D14AA79DBC82), SKY_U64(0x4B2D8644D8A74E18), /* ~= 10^272 */
            SKY_U64(0xEC9C459D51852BA2), SKY_U64(0xDDF8E7D60ED1219E), /* ~= 10^273 */
            SKY_U64(0x93E1AB8252F33B45), SKY_U64(0xCABB90E5C942B503), /* ~= 10^274 */
            SKY_U64(0xB8DA1662E7B00A17), SKY_U64(0x3D6A751F3B936243), /* ~= 10^275 */
            SKY_U64(0xE7109BFBA19C0C9D), SKY_U64(0x0CC512670A783AD4), /* ~= 10^276 */
            SKY_U64(0x906A617D450187E2), SKY_U64(0x27FB2B80668B24C5), /* ~= 10^277 */
            SKY_U64(0xB484F9DC9641E9DA), SKY_U64(0xB1F9F660802DEDF6), /* ~= 10^278 */
            SKY_U64(0xE1A63853BBD26451), SKY_U64(0x5E7873F8A0396973), /* ~= 10^279 */
            SKY_U64(0x8D07E33455637EB2), SKY_U64(0xDB0B487B6423E1E8), /* ~= 10^280 */
            SKY_U64(0xB049DC016ABC5E5F), SKY_U64(0x91CE1A9A3D2CDA62), /* ~= 10^281 */
            SKY_U64(0xDC5C5301C56B75F7), SKY_U64(0x7641A140CC7810FB), /* ~= 10^282 */
            SKY_U64(0x89B9B3E11B6329BA), SKY_U64(0xA9E904C87FCB0A9D), /* ~= 10^283 */
            SKY_U64(0xAC2820D9623BF429), SKY_U64(0x546345FA9FBDCD44), /* ~= 10^284 */
            SKY_U64(0xD732290FBACAF133), SKY_U64(0xA97C177947AD4095), /* ~= 10^285 */
            SKY_U64(0x867F59A9D4BED6C0), SKY_U64(0x49ED8EABCCCC485D), /* ~= 10^286 */
            SKY_U64(0xA81F301449EE8C70), SKY_U64(0x5C68F256BFFF5A74), /* ~= 10^287 */
            SKY_U64(0xD226FC195C6A2F8C), SKY_U64(0x73832EEC6FFF3111), /* ~= 10^288 */
            SKY_U64(0x83585D8FD9C25DB7), SKY_U64(0xC831FD53C5FF7EAB), /* ~= 10^289 */
            SKY_U64(0xA42E74F3D032F525), SKY_U64(0xBA3E7CA8B77F5E55), /* ~= 10^290 */
            SKY_U64(0xCD3A1230C43FB26F), SKY_U64(0x28CE1BD2E55F35EB), /* ~= 10^291 */
            SKY_U64(0x80444B5E7AA7CF85), SKY_U64(0x7980D163CF5B81B3), /* ~= 10^292 */
            SKY_U64(0xA0555E361951C366), SKY_U64(0xD7E105BCC332621F), /* ~= 10^293 */
            SKY_U64(0xC86AB5C39FA63440), SKY_U64(0x8DD9472BF3FEFAA7), /* ~= 10^294 */
            SKY_U64(0xFA856334878FC150), SKY_U64(0xB14F98F6F0FEB951), /* ~= 10^295 */
            SKY_U64(0x9C935E00D4B9D8D2), SKY_U64(0x6ED1BF9A569F33D3), /* ~= 10^296 */
            SKY_U64(0xC3B8358109E84F07), SKY_U64(0x0A862F80EC4700C8), /* ~= 10^297 */
            SKY_U64(0xF4A642E14C6262C8), SKY_U64(0xCD27BB612758C0FA), /* ~= 10^298 */
            SKY_U64(0x98E7E9CCCFBD7DBD), SKY_U64(0x8038D51CB897789C), /* ~= 10^299 */
            SKY_U64(0xBF21E44003ACDD2C), SKY_U64(0xE0470A63E6BD56C3), /* ~= 10^300 */
            SKY_U64(0xEEEA5D5004981478), SKY_U64(0x1858CCFCE06CAC74), /* ~= 10^301 */
            SKY_U64(0x95527A5202DF0CCB), SKY_U64(0x0F37801E0C43EBC8), /* ~= 10^302 */
            SKY_U64(0xBAA718E68396CFFD), SKY_U64(0xD30560258F54E6BA), /* ~= 10^303 */
            SKY_U64(0xE950DF20247C83FD), SKY_U64(0x47C6B82EF32A2069), /* ~= 10^304 */
            SKY_U64(0x91D28B7416CDD27E), SKY_U64(0x4CDC331D57FA5441), /* ~= 10^305 */
            SKY_U64(0xB6472E511C81471D), SKY_U64(0xE0133FE4ADF8E952), /* ~= 10^306 */
            SKY_U64(0xE3D8F9E563A198E5), SKY_U64(0x58180FDDD97723A6), /* ~= 10^307 */
            SKY_U64(0x8E679C2F5E44FF8F), SKY_U64(0x570F09EAA7EA7648), /* ~= 10^308 */
            SKY_U64(0xB201833B35D63F73), SKY_U64(0x2CD2CC6551E513DA), /* ~= 10^309 */
            SKY_U64(0xDE81E40A034BCF4F), SKY_U64(0xF8077F7EA65E58D1), /* ~= 10^310 */
            SKY_U64(0x8B112E86420F6191), SKY_U64(0xFB04AFAF27FAF782), /* ~= 10^311 */
            SKY_U64(0xADD57A27D29339F6), SKY_U64(0x79C5DB9AF1F9B563), /* ~= 10^312 */
            SKY_U64(0xD94AD8B1C7380874), SKY_U64(0x18375281AE7822BC), /* ~= 10^313 */
            SKY_U64(0x87CEC76F1C830548), SKY_U64(0x8F2293910D0B15B5), /* ~= 10^314 */
            SKY_U64(0xA9C2794AE3A3C69A), SKY_U64(0xB2EB3875504DDB22), /* ~= 10^315 */
            SKY_U64(0xD433179D9C8CB841), SKY_U64(0x5FA60692A46151EB), /* ~= 10^316 */
            SKY_U64(0x849FEEC281D7F328), SKY_U64(0xDBC7C41BA6BCD333), /* ~= 10^317 */
            SKY_U64(0xA5C7EA73224DEFF3), SKY_U64(0x12B9B522906C0800), /* ~= 10^318 */
            SKY_U64(0xCF39E50FEAE16BEF), SKY_U64(0xD768226B34870A00), /* ~= 10^319 */
            SKY_U64(0x81842F29F2CCE375), SKY_U64(0xE6A1158300D46640), /* ~= 10^320 */
            SKY_U64(0xA1E53AF46F801C53), SKY_U64(0x60495AE3C1097FD0), /* ~= 10^321 */
            SKY_U64(0xCA5E89B18B602368), SKY_U64(0x385BB19CB14BDFC4), /* ~= 10^322 */
            SKY_U64(0xFCF62C1DEE382C42), SKY_U64(0x46729E03DD9ED7B5), /* ~= 10^323 */
            SKY_U64(0x9E19DB92B4E31BA9), SKY_U64(0x6C07A2C26A8346D1)  /* ~= 10^324 */
    };


    const sky_i32_t idx = exp10 - (POW10_SIG_TABLE_MIN_EXP);
    *hi = pow10_sig_table[(idx << 1)];
    *lo = pow10_sig_table[(idx << 1) + 1];
}

/**
 * Get the exponent (base 2) for highest 64 bits significand in pow10_sig_table.
 * @param exp10 exp10
 * @param exp2 exp2
 */
static sky_inline void
pow10_table_get_exp(sky_i32_t exp10, sky_i32_t *exp2) {
    /* e2 = floor(log2(pow(10, e))) - 64 + 1 */
    /*    = floor(e * log2(10) - 63)         */
    *exp2 = (exp10 * 217706 - 4128768) >> 16;
}

static sky_inline void
u128_mul(sky_u64_t a, sky_u64_t b, sky_u64_t *hi, sky_u64_t *lo) {
#ifdef HAVE_INT_128
    sky_u128_t m = (sky_u128_t) a * b;
    *hi = (sky_u64_t) (m >> 64);
    *lo = (sky_u64_t) (m);

#else
    const sky_u32_t a0 = (sky_u32_t)(a), a1 = (sky_u32_t)(a >> 32);
    const sky_u32_t b0 = (sky_u32_t)(b), b1 = (sky_u32_t)(b >> 32);
    const sky_u64_t p00 = (sky_u64_t)a0 * b0, p01 = (sky_u64_t)a0 * b1;
    const sky_u64_t p10 = (sky_u64_t)a1 * b0, p11 = (sky_u64_t)a1 * b1;
    const sky_u64_t m0 = p01 + (p00 >> 32);
    const sky_u32_t m00 = (sky_u32_t)(m0), m01 = (sky_u32_t)(m0 >> 32);
    const sky_u64_t m1 = p10 + m00;
    const sky_u32_t m10 = (sky_u32_t)(m1), m11 = (sky_u32_t)(m1 >> 32);
    *hi = p11 + m01 + m11;
    *lo = ((sky_u64_t)m10 << 32) | (sky_u32_t)p00;
#endif
}

/** Multiplies two 64-bit unsigned integers and add a value (a * b + c),
    returns the 128-bit result as 'hi' and 'lo'. */
static sky_inline void
u128_mul_add(sky_u64_t a, sky_u64_t b, sky_u64_t c, sky_u64_t *hi, sky_u64_t *lo) {
#ifdef HAVE_INT_128
    sky_u128_t m = (sky_u128_t) a * b + c;
    *hi = (sky_u64_t) (m >> 64);
    *lo = (sky_u64_t) (m);
#else
    sky_u64_t h, l, t;
    u128_mul(a, b, &h, &l);
    t = l + c;
    h += (sky_u64_t)(((t < l) | (t < c)));
    *hi = h;
    *lo = t;
#endif
}

/**
 Evaluate 'big += val'.
 @param big A big number (can be 0).
 @param val An unsigned integer (can be 0).
 */
static sky_inline void
bigint_add_u64(bigint_t *big, sky_u64_t val) {
    sky_u32_t idx, max;
    sky_u64_t num = big->bits[0];
    sky_u64_t add = num + val;
    big->bits[0] = add;
    if (sky_likely((add >= num) || (add >= val))) {
        return;
    }
    for ((void) (idx = 1), max = big->used; idx < max; idx++) {
        if (sky_likely(big->bits[idx] != SKY_U64_MAX)) {
            big->bits[idx] += 1;
            return;
        }
        big->bits[idx] = 0;
    }
    big->bits[big->used++] = 1;
}

/**
 Evaluate 'big *= val'.
 @param big A big number (can be 0).
 @param val An unsigned integer (cannot be 0).
 */
static sky_inline void
bigint_mul_u64(bigint_t *big, sky_u64_t val) {
    sky_u32_t idx = 0, max = big->used;
    sky_u64_t hi, lo, carry = 0;
    for (; idx < max; idx++) {
        if (big->bits[idx]) {
            break;
        }
    }
    for (; idx < max; idx++) {
        u128_mul_add(big->bits[idx], val, carry, &hi, &lo);
        big->bits[idx] = lo;
        carry = hi;
    }
    if (carry) big->bits[big->used++] = carry;
}

/**
 Evaluate 'big *= 2^exp'.
 @param big A big number (can be 0).
 @param exp An exponent integer (can be 0).
 */
static sky_inline void
bigint_mul_pow2(bigint_t *big, sky_u32_t exp) {
    sky_u32_t shift = exp & 63;
    sky_u32_t move = exp >> 6;
    sky_u32_t idx = big->used;
    if (sky_unlikely(shift == 0)) {
        for (; idx > 0; idx--) {
            big->bits[idx + move - 1] = big->bits[idx - 1];
        }
        big->used += move;
        while (move) big->bits[--move] = 0;
    } else {
        big->bits[idx] = 0;
        for (; idx > 0; idx--) {
            sky_u64_t num = big->bits[idx] << shift;
            num |= big->bits[idx - 1] >> (64 - shift);
            big->bits[idx + move] = num;
        }
        big->bits[move] = big->bits[0] << shift;
        big->used += move + (big->bits[big->used + move] > 0);
        while (move) {
            big->bits[--move] = 0;
        }
    }
}

/**
 Evaluate 'big *= 10^exp'.
 @param big A big number (can be 0).
 @param exp An exponent integer (cannot be 0).
 */
static sky_inline void
bigint_mul_pow10(bigint_t *big, sky_i32_t exp) {
    for (; exp >= U64_POW10_MAX_EXP; exp -= U64_POW10_MAX_EXP) {
        bigint_mul_u64(big, u64_pow10_table(U64_POW10_MAX_EXP));
    }
    if (exp) {
        bigint_mul_u64(big, u64_pow10_table(exp));
    }
}

/**
 Compare two bigint.
 @return -1 if 'a < b', +1 if 'a > b', 0 if 'a == b'.
 */
static sky_inline sky_i32_t
bigint_cmp(bigint_t *a, bigint_t *b) {
    sky_u32_t idx = a->used;
    if (a->used < b->used) {
        return -1;
    } else if (a->used > b->used) {
        return +1;
    }
    while (idx-- > 0) {
        const sky_u64_t av = a->bits[idx];
        const sky_u64_t bv = b->bits[idx];
        if (av < bv) {
            return -1;
        } else if (av > bv) {
            return +1;
        }
    }
    return 0;
}

/**
 Evaluate 'big = val'.
 @param big A big number (can be 0).
 @param val An unsigned integer (can be 0).
 */
static sky_inline
void bigint_set_u64(bigint_t *big, sky_u64_t val) {
    big->used = 1;
    big->bits[0] = val;
}

/** Set a bigint_t with floating point number string. */
static sky_inline
void bigint_set_buf(
        bigint_t *big,
        sky_u64_t sig,
        sky_i32_t *exp,
        sky_uchar_t *sig_cut,
        const sky_uchar_t *sig_end,
        const sky_uchar_t *dot_pos
) {

    if (sky_unlikely(!sig_cut)) {
        /* no digit cut, set significant part only */
        bigint_set_u64(big, sig);
        return;
    } else {
        /* some digits were cut, read them from 'sig_cut' to 'sig_end' */
        sky_uchar_t *hdr = sig_cut;
        sky_uchar_t *cur = hdr;
        sky_u32_t len = 0;
        sky_u64_t val = 0;
        sky_bool_t dig_big_cut = false;
        sky_bool_t has_dot = (hdr < dot_pos) & (dot_pos < sig_end);
        sky_u32_t dig_len_total = U64_SAFE_DIG + (sky_u32_t) (sig_end - hdr) - has_dot;

        sig -= (*sig_cut >= '5'); /* sig was rounded before */
        if (dig_len_total > F64_MAX_DEC_DIG) {
            dig_big_cut = true;
            sig_end -= dig_len_total - (F64_MAX_DEC_DIG + 1);
            sig_end -= (dot_pos + 1 == sig_end);
            dig_len_total = (F64_MAX_DEC_DIG + 1);
        }
        *exp -= (sky_i32_t) dig_len_total - U64_SAFE_DIG;

        big->used = 1;
        big->bits[0] = sig;
        while (cur < sig_end) {
            if (sky_likely(cur != dot_pos)) {
                val = val * 10 + (sky_uchar_t) (*cur++ - '0');
                len++;
                if (sky_unlikely(cur == sig_end && dig_big_cut)) {
                    /* The last digit must be non-zero,    */
                    /* set it to '1' for correct rounding. */
                    val = val - (val % 10) + 1;
                }
                if (len == U64_SAFE_DIG || cur == sig_end) {
                    bigint_mul_pow10(big, (sky_i32_t) len);
                    bigint_add_u64(big, val);
                    val = 0;
                    len = 0;
                }
            } else {
                cur++;
            }
        }
    }
}

/**
 * Get cached rounded diy_fp_t with pow(10, e) The input value must in range
    [POW10_SIG_TABLE_MIN_EXP, POW10_SIG_TABLE_MAX_EXP].
 * @param exp10 exp10
 * @return diy_fp
 */
static sky_inline diy_fp_t
diy_fp_get_cached_pow10(sky_i32_t exp10) {
    diy_fp_t fp;
    sky_u64_t sig_ext;
    pow10_table_get_sig(exp10, &fp.sig, &sig_ext);
    pow10_table_get_exp(exp10, &fp.exp);
    fp.sig += (sig_ext >> 63);
    return fp;
}

/** Returns fp * fp2. */
static sky_inline diy_fp_t
diy_fp_mul(diy_fp_t fp, diy_fp_t fp2) {
    sky_u64_t hi, lo;
    u128_mul(fp.sig, fp2.sig, &hi, &lo);
    fp.sig = hi + (lo >> 63);
    fp.exp += fp2.exp + 64;
    return fp;
}

/** Convert diy_fp_t to IEEE-754 raw value. */
static sky_inline sky_u64_t
diy_fp_to_ieee_raw(diy_fp_t fp) {
    sky_u64_t sig = fp.sig;
    sky_i32_t exp = fp.exp;
    sky_u32_t lz_bits;
    if (sky_unlikely(fp.sig == 0)) {
        return 0;
    }

    lz_bits = (sky_u32_t) sky_clz_u64(sig);
    sig <<= lz_bits;
    sig >>= F64_BITS - F64_SIG_FULL_BITS;
    exp -= (sky_i32_t) lz_bits;
    exp += F64_BITS - F64_SIG_FULL_BITS;
    exp += F64_SIG_BITS;

    if (sky_unlikely(exp >= F64_MAX_BIN_EXP)) {
        /* overflow */
        return F64_RAW_INF;
    } else if (sky_likely(exp >= F64_MIN_BIN_EXP - 1)) {
        /* normal */
        exp += F64_EXP_BIAS;
        return ((sky_u64_t) exp << F64_SIG_BITS) | (sig & F64_SIG_MASK);
    } else if (sky_likely(exp >= F64_MIN_BIN_EXP - F64_SIG_FULL_BITS)) {
        /* subnormal */
        return sig >> (F64_MIN_BIN_EXP - exp - 1);
    } else {
        /* underflow */
        return 0;
    }
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
read_string(sky_uchar_t **ptr, const sky_uchar_t *lst, sky_json_val_t *val, sky_bool_t inv) {
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
        val->uni.str = cur;
        *src = '\0';
        *end = src + 1;
        return true;
    }

#undef expr_jump
#undef expr_stop

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
        val->uni.str = cur;
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
#undef is_valid_seq_1
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
            val->uni.str = hdr;
        } else {
            val->tag = SKY_JSON_TYPE_NUM | SKY_JSON_SUBTYPE_REAL;
            val->uni.u64 = f64_raw_get_inf(sign);
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
            val->uni.str = hdr;
        } else {
            val->tag = SKY_JSON_TYPE_NUM | SKY_JSON_SUBTYPE_REAL;
            val->uni.u64 = f64_raw_get_nan(sign);
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
        A valid hex character [0-9A-Fa-f] will mapped to it's raw number [0x000F],
        an invalid hex character will mapped to [0xF0].
        (generate with misc/make_tables.c)
 */
    static const sky_u8_t hex_conv_table[256] = {
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

static sky_str_t *
json_write_single(const sky_json_val_t *val, sky_u32_t opts) {

#define return_err(_msg) do { \
    sky_free(result);         \
    sky_log_error(_msg);      \
    return null;              \
} while (false)

#define incr_len(_len) do { \
    hdr = sky_malloc(_len); \
    if (sky_unlikely(!hdr)) { \
        goto fail_alloc;    \
    }                       \
    result = (sky_str_t *)hdr;\
    hdr += sizeof(sky_str_t); \
    cur = hdr;              \
} while (false)

#define check_str_len(_len) do { \
    if ((SKY_USIZE_MAX < SKY_U64_MAX) && ((_len) >= (SKY_USIZE_MAX - 16) / 6)) \
        goto fail_alloc;         \
} while (false)

    const sky_u8_t *enc_table = get_enc_table_with_flag(opts);
    const sky_bool_t esc = (opts & SKY_JSON_WRITE_ESCAPE_UNICODE) != 0;
    const sky_bool_t inv = (opts & SKY_JSON_WRITE_ALLOW_INVALID_UNICODE) != 0;

    sky_uchar_t *hdr, *cur;
    sky_str_t *result;
    sky_str_t str;

    switch (sky_json_unsafe_get_type(val)) {
        case SKY_JSON_TYPE_RAW:
            str = sky_json_unsafe_get_str(val);
            check_str_len(str.len);
            incr_len(str.len + 1);
            cur = write_raw(cur, &str);

            break;
        case SKY_JSON_TYPE_STR:
            str = sky_json_unsafe_get_str(val);
            check_str_len(str.len);
            incr_len(str.len * 6 + 4);
            cur = write_string(cur, &str, enc_table, esc, inv);
            if (sky_unlikely(!cur)) {
                goto fail_str;
            }
            break;
        case SKY_JSON_TYPE_NUM:
            incr_len(32);
            cur = write_number(cur, val, opts);
            if (sky_unlikely(!cur)) {
                goto fail_num;
            }
            break;
        case SKY_JSON_TYPE_BOOL:
            incr_len(8);
            cur = write_bool(cur, sky_json_unsafe_get_bool(val));
            break;
        case SKY_JSON_TYPE_NULL:
            incr_len(8);
            cur = write_null(cur);
            break;
        case SKY_JSON_TYPE_ARR:
            incr_len(4);
            sky_memcpy2(cur, "[]");
            cur += 2;
            break;
        case SKY_JSON_TYPE_OBJ:
            incr_len(4);
            sky_memcpy2(cur, "{}");
            cur += 2;
            break;
        default:
            goto fail_type;
    }

    *cur = '\0';
    result->len = (sky_usize_t) (cur - hdr);
    result->data = hdr;

    return result;

    fail_alloc:
    sky_log_error("memory allocation failed");
    return null;
    fail_type:
    sky_log_error("invalid JSON value type");
    return null;
    fail_num:
    return_err("nan or inf number is not allowed");
    fail_str:
    return_err("invalid utf-8 encoding in string");

#undef return_err
#undef check_str_len
#undef incr_len
}

static sky_str_t *
json_write_pretty(const sky_json_val_t *root, sky_u32_t opts) {
#define return_err(_msg) do { \
    sky_free(result);         \
    sky_log_error(_msg);      \
    return null;              \
} while (false)

#define incr_len(_len) do { \
    ext_len = (sky_usize_t)(_len); \
    if (sky_unlikely((sky_uchar_t *)(cur + ext_len) >= (sky_uchar_t *)ctx)) { \
        alc_inc = sky_max(alc_len >> 1, ext_len);                             \
        alc_inc = size_align_up(alc_inc, sizeof(json_write_ctx_t));           \
        if (size_add_is_overflow(alc_len, alc_inc)) {                         \
            goto fail_alloc;\
        }                   \
        alc_len += alc_inc; \
        tmp = sky_realloc(result, alc_len + sizeof(sky_str_t));               \
        if (sky_unlikely(!tmp)) {  \
            goto fail_alloc;\
        }                   \
        result = (sky_str_t *)tmp; \
        tmp += sizeof(sky_str_t);  \
        ctx_len = (sky_usize_t)(end - (sky_uchar_t *)ctx);                    \
        ctx_tmp = (json_write_ctx_t *)(tmp + (alc_len - ctx_len));            \
        sky_memmove(ctx_tmp, tmp + ((sky_uchar_t *)ctx - hdr), ctx_len);      \
        ctx = ctx_tmp;      \
        cur = tmp + (cur - hdr); \
        end = tmp + alc_len; \
        hdr = tmp; \
    } \
} while (false)

#define check_str_len(_len) do { \
    if ((SKY_USIZE_MAX < SKY_U64_MAX) && ((_len) >= (SKY_USIZE_MAX - 16) / 6)) \
        goto fail_alloc;         \
} while (false)


    const sky_u8_t *enc_table = get_enc_table_with_flag(opts);
    const sky_bool_t esc = (opts & SKY_JSON_WRITE_ESCAPE_UNICODE) != 0;
    const sky_bool_t inv = (opts & SKY_JSON_WRITE_ALLOW_INVALID_UNICODE) != 0;


    sky_str_t *result = null;
    sky_usize_t alc_len = root->uni.ofs / sizeof(sky_json_val_t);
    alc_len = alc_len * WRITER_ESTIMATED_PRETTY_RATIO + 64;
    alc_len = size_align_up(alc_len, sizeof(json_write_ctx_t));
    sky_uchar_t *hdr = sky_malloc(alc_len + sizeof(sky_str_t));
    if (sky_unlikely(!hdr)) {
        goto fail_alloc;
    }
    result = (sky_str_t *) hdr;
    hdr += sizeof(sky_str_t);

    sky_uchar_t *cur = hdr;
    sky_uchar_t *end = hdr + alc_len;
    json_write_ctx_t *ctx = (json_write_ctx_t *) end;

    sky_str_t str;
    sky_uchar_t *tmp;
    json_write_ctx_t *ctx_tmp;
    sky_json_val_t *val;
    sky_usize_t alc_inc, ctx_len, ctn_len, ctn_len_tmp, ext_len, level;
    sky_u8_t val_type;
    sky_bool_t ctn_obj, ctn_obj_tmp, is_key, no_indent;


    val = (sky_json_val_t *) root;
    val_type = sky_json_unsafe_get_type(val);
    ctn_obj = (val_type == SKY_JSON_TYPE_OBJ);
    ctn_len = sky_json_unsafe_get_len(val) << (sky_u8_t) ctn_obj;
    *cur++ = (sky_u8_t) ('[' | ((sky_u8_t) ctn_obj << 5));
    *cur++ = '\n';
    ++val;
    level = 1;

    val_begin:
    val_type = sky_json_unsafe_get_type(val);
    if (val_type == SKY_JSON_TYPE_STR) {
        is_key = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ~ctn_len);
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        str = sky_json_unsafe_get_str(val);
        check_str_len(str.len);
        incr_len(str.len * 6 + 16 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_string(cur, &str, enc_table, esc, inv);
        if (sky_unlikely(!cur)) {
            goto fail_str;
        }
        *cur++ = is_key ? ':' : ',';
        *cur++ = is_key ? ' ' : '\n';
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NUM) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        incr_len(32 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_number(cur, val, opts);
        if (sky_unlikely(!cur)) {
            goto fail_num;
        }
        *cur++ = ',';
        *cur++ = '\n';
        goto val_end;
    }
    if ((val_type & (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) == (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        ctn_len_tmp = sky_json_unsafe_get_len(val);
        ctn_obj_tmp = (val_type == SKY_JSON_TYPE_OBJ);
        if (sky_unlikely(ctn_len_tmp == 0)) {
            /* write empty container */
            incr_len(16 + (no_indent ? 0 : level * 4));
            cur = write_indent(cur, no_indent ? 0 : level);
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = ',';
            *cur++ = '\n';
            goto val_end;
        } else {
            /* push context, setup new container */
            incr_len(32 + (no_indent ? 0 : level * 4));
            json_write_ctx_set(--ctx, ctn_len, ctn_obj);
            ctn_len = ctn_len_tmp << (sky_u8_t) ctn_obj_tmp;
            ctn_obj = ctn_obj_tmp;
            cur = write_indent(cur, no_indent ? 0 : level);
            ++level;
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj << 5));
            *cur++ = '\n';
            ++val;
            goto val_begin;
        }
    }
    if (val_type == SKY_JSON_TYPE_BOOL) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        incr_len(16 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_bool(cur, sky_json_unsafe_get_bool(val));
        cur += 2;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NULL) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        incr_len(16 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_null(cur);
        cur += 2;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_RAW) {
        str = sky_json_unsafe_get_str(val);
        check_str_len(str.len);
        incr_len(str.len + 3);
        cur = write_raw(cur, &str);
        *cur++ = ',';
        *cur++ = '\n';
        goto val_end;
    }
    goto fail_type;

    val_end:
    ++val;
    --ctn_len;
    if (sky_unlikely(ctn_len == 0)) {
        goto ctn_end;
    }
    goto val_begin;

    ctn_end:
    cur -= 2;
    *cur++ = '\n';
    incr_len(level * 4);
    cur = write_indent(cur, --level);
    *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj << 5));
    if (sky_unlikely((sky_uchar_t *) ctx >= end)) {
        goto doc_end;
    }
    json_write_ctx_get(ctx++, &ctn_len, &ctn_obj);
    ctn_len--;
    *cur++ = ',';
    *cur++ = '\n';
    if (sky_likely(ctn_len > 0)) {
        goto val_begin;
    } else {
        goto ctn_end;
    }

    doc_end:
    *cur = '\0';

    result->len = (sky_usize_t) (cur - hdr);
    result->data = hdr;

    return result;


    fail_alloc:
    return_err("invalid JSON value type");
    return null;
    fail_type:
    return_err("invalid JSON value type");
    fail_num:
    return_err("nan or inf number is not allowed");
    fail_str:
    return_err("invalid utf-8 encoding in string");

#undef return_err
#undef incr_len
#undef check_str_len
}

static sky_str_t *
json_write_minify(const sky_json_val_t *root, sky_u32_t opts) {
#define return_err(_msg) do { \
    sky_free(result);         \
    sky_log_error(_msg);      \
    return null;              \
} while (false)

#define incr_len(_len) do { \
    ext_len = (sky_usize_t)(_len); \
    if (sky_unlikely((sky_uchar_t *)(cur + ext_len) >= (sky_uchar_t *)ctx)) { \
        alc_inc = sky_max(alc_len >> 1, ext_len);                             \
        alc_inc = size_align_up(alc_inc, sizeof(json_write_ctx_t));           \
        if (size_add_is_overflow(alc_len, alc_inc)) {                         \
            goto fail_alloc;\
        }                   \
        alc_len += alc_inc; \
        tmp = sky_realloc(result, alc_len + sizeof(sky_str_t));               \
        if (sky_unlikely(!tmp)) {  \
            goto fail_alloc;\
        }                   \
        result = (sky_str_t *)tmp; \
        tmp += sizeof(sky_str_t);  \
        ctx_len = (sky_usize_t)(end - (sky_uchar_t *)ctx);                    \
        ctx_tmp = (json_write_ctx_t *)(tmp + (alc_len - ctx_len));            \
        sky_memmove(ctx_tmp, tmp + ((sky_uchar_t *)ctx - hdr), ctx_len);      \
        ctx = ctx_tmp;      \
        cur = tmp + (cur - hdr); \
        end = tmp + alc_len; \
        hdr = tmp; \
    } \
} while (false)

#define check_str_len(_len) do { \
    if ((SKY_USIZE_MAX < SKY_U64_MAX) && ((_len) >= (SKY_USIZE_MAX - 16) / 6)) \
        goto fail_alloc;         \
} while (false)


    const sky_u8_t *enc_table = get_enc_table_with_flag(opts);
    const sky_bool_t esc = (opts & SKY_JSON_WRITE_ESCAPE_UNICODE) != 0;
    const sky_bool_t inv = (opts & SKY_JSON_WRITE_ALLOW_INVALID_UNICODE) != 0;


    sky_str_t *result = null;
    sky_usize_t alc_len = root->uni.ofs / sizeof(sky_json_val_t);
    alc_len = alc_len * WRITER_ESTIMATED_PRETTY_RATIO + 64;
    alc_len = size_align_up(alc_len, sizeof(json_write_ctx_t));
    sky_uchar_t *hdr = sky_malloc(alc_len + sizeof(sky_str_t));
    if (sky_unlikely(!hdr)) {
        goto fail_alloc;
    }
    result = (sky_str_t *) hdr;
    hdr += sizeof(sky_str_t);

    sky_uchar_t *cur = hdr;
    sky_uchar_t *end = hdr + alc_len;
    json_write_ctx_t *ctx = (json_write_ctx_t *) end;

    sky_str_t str;
    sky_uchar_t *tmp;
    json_write_ctx_t *ctx_tmp;
    sky_json_val_t *val;
    sky_usize_t alc_inc, ctx_len, ctn_len, ctn_len_tmp, ext_len;
    sky_u8_t val_type;
    sky_bool_t ctn_obj, ctn_obj_tmp, is_key;


    val = (sky_json_val_t *) root;
    val_type = sky_json_unsafe_get_type(val);
    ctn_obj = (val_type == SKY_JSON_TYPE_OBJ);
    ctn_len = sky_json_unsafe_get_len(val) << (sky_u8_t) ctn_obj;
    *cur++ = (sky_u8_t) ('[' | ((sky_u8_t) ctn_obj << 5));
    ++val;

    val_begin:
    val_type = sky_json_unsafe_get_type(val);
    if (val_type == SKY_JSON_TYPE_STR) {
        is_key = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ~ctn_len);
        str = sky_json_unsafe_get_str(val);
        check_str_len(str.len);
        incr_len(str.len * 6 + 16);
        cur = write_string(cur, &str, enc_table, esc, inv);
        if (sky_unlikely(!cur)) {
            goto fail_str;
        }
        *cur++ = is_key ? ':' : ',';
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NUM) {
        incr_len(32);
        cur = write_number(cur, val, opts);
        if (sky_unlikely(!cur)) {
            goto fail_num;
        }
        *cur++ = ',';
        goto val_end;
    }
    if ((val_type & (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) == (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) {
        ctn_len_tmp = sky_json_unsafe_get_len(val);
        ctn_obj_tmp = (val_type == SKY_JSON_TYPE_OBJ);

        incr_len(16);
        if (sky_unlikely(ctn_len_tmp == 0)) {
            /* write empty container */
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = ',';
            goto val_end;
        } else {
            /* push context, setup new container */
            json_write_ctx_set(--ctx, ctn_len, ctn_obj);
            ctn_len = ctn_len_tmp << (sky_u8_t) ctn_obj_tmp;
            ctn_obj = ctn_obj_tmp;
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj << 5));
            ++val;
            goto val_begin;
        }
    }
    if (val_type == SKY_JSON_TYPE_BOOL) {
        incr_len(16);
        cur = write_bool(cur, sky_json_unsafe_get_bool(val));
        ++cur;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NULL) {
        incr_len(16);
        cur = write_null(cur);
        ++cur;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_RAW) {
        str = sky_json_unsafe_get_str(val);
        check_str_len(str.len);
        incr_len(str.len + 2);
        cur = write_raw(cur, &str);
        *cur++ = ',';
        goto val_end;
    }
    goto fail_type;

    val_end:
    ++val;
    --ctn_len;
    if (sky_unlikely(ctn_len == 0)) {
        goto ctn_end;
    }
    goto val_begin;

    ctn_end:
    --cur;
    *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj << 5));
    *cur++ = ',';
    if (sky_unlikely((sky_uchar_t *) ctx >= end)) {
        goto doc_end;
    }
    json_write_ctx_get(ctx++, &ctn_len, &ctn_obj);
    --ctn_len;
    if (sky_likely(ctn_len > 0)) {
        goto val_begin;
    } else {
        goto ctn_end;
    }

    doc_end:
    *--cur = '\0';

    result->len = (sky_usize_t) (cur - hdr);
    result->data = hdr;

    return result;


    fail_alloc:
    return_err("invalid JSON value type");
    return null;
    fail_type:
    return_err("invalid JSON value type");
    fail_num:
    return_err("nan or inf number is not allowed");
    fail_str:
    return_err("invalid utf-8 encoding in string");

#undef return_err
#undef incr_len
#undef check_str_len
}


static sky_str_t *
json_mut_write_single(const sky_json_mut_val_t *val, sky_u32_t opts) {
    return json_write_single(&val->val, opts);
}

static sky_str_t *
json_mut_write_pretty(const sky_json_mut_val_t *root, sky_u32_t opts) {
#define return_err(_msg) do { \
    sky_free(result);         \
    sky_log_error(_msg);      \
    return null;              \
} while (false)

#define incr_len(_len) do { \
    ext_len = (sky_usize_t)(_len); \
    if (sky_unlikely((sky_uchar_t *)(cur + ext_len) >= (sky_uchar_t *)ctx)) { \
        alc_inc = sky_max(alc_len >> 1, ext_len);                             \
        alc_inc = size_align_up(alc_inc, sizeof(json_mut_write_ctx_t));       \
        if (size_add_is_overflow(alc_len, alc_inc)) {                         \
            goto fail_alloc;\
        }                   \
        alc_len += alc_inc; \
        tmp = sky_realloc(result, alc_len + sizeof(sky_str_t));               \
        if (sky_unlikely(!tmp)) {  \
            goto fail_alloc;\
        }                   \
        result = (sky_str_t *)tmp; \
        tmp += sizeof(sky_str_t);  \
        ctx_len = (sky_usize_t)(end - (sky_uchar_t *)ctx);                    \
        ctx_tmp = (json_mut_write_ctx_t *)(tmp + (alc_len - ctx_len));        \
        sky_memmove(ctx_tmp, tmp + ((sky_uchar_t *)ctx - hdr), ctx_len);      \
        ctx = ctx_tmp;      \
        cur = tmp + (cur - hdr); \
        end = tmp + alc_len; \
        hdr = tmp; \
    } \
} while (false)

#define check_str_len(_len) do { \
    if ((SKY_USIZE_MAX < SKY_U64_MAX) && ((_len) >= (SKY_USIZE_MAX - 16) / 6)) \
        goto fail_alloc;         \
} while (false)


    const sky_u8_t *enc_table = get_enc_table_with_flag(opts);
    const sky_bool_t esc = (opts & SKY_JSON_WRITE_ESCAPE_UNICODE) != 0;
    const sky_bool_t inv = (opts & SKY_JSON_WRITE_ALLOW_INVALID_UNICODE) != 0;


    sky_str_t *result = null;
    sky_usize_t alc_len = 0 * WRITER_ESTIMATED_PRETTY_RATIO + 64;
    alc_len = size_align_up(alc_len, sizeof(json_mut_write_ctx_t));
    sky_uchar_t *hdr = sky_malloc(alc_len + sizeof(sky_str_t));
    if (sky_unlikely(!hdr)) {
        goto fail_alloc;
    }
    result = (sky_str_t *) hdr;
    hdr += sizeof(sky_str_t);

    sky_uchar_t *cur = hdr;
    sky_uchar_t *end = hdr + alc_len;
    json_mut_write_ctx_t *ctx = (json_mut_write_ctx_t *) end;

    sky_str_t str;
    sky_uchar_t *tmp;
    json_mut_write_ctx_t *ctx_tmp;
    sky_json_mut_val_t *val, *ctn;
    sky_usize_t alc_inc, ctx_len, ctn_len, ctn_len_tmp, ext_len, level;
    sky_u8_t val_type;
    sky_bool_t ctn_obj, ctn_obj_tmp, is_key, no_indent;


    val = (sky_json_mut_val_t *) root;
    val_type = sky_json_unsafe_get_type(&val->val);
    ctn_obj = (val_type == SKY_JSON_TYPE_OBJ);
    ctn_len = sky_json_unsafe_get_len(&val->val) << (sky_u8_t) ctn_obj;
    *cur++ = (sky_u8_t) ('[' | ((sky_u8_t) ctn_obj << 5));
    *cur++ = '\n';
    ctn = val;
    val = val->val.uni.ptr; /* tail */
    val = ctn_obj ? val->next->next : val->next;
    level = 1;

    val_begin:
    val_type = sky_json_unsafe_get_type(&val->val);
    if (val_type == SKY_JSON_TYPE_STR) {
        is_key = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ~ctn_len);
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        str = sky_json_unsafe_get_str(&val->val);
        check_str_len(str.len);
        incr_len(str.len * 6 + 16 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_string(cur, &str, enc_table, esc, inv);
        if (sky_unlikely(!cur)) {
            goto fail_str;
        }
        *cur++ = is_key ? ':' : ',';
        *cur++ = is_key ? ' ' : '\n';
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NUM) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        incr_len(32 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_number(cur, &val->val, opts);
        if (sky_unlikely(!cur)) {
            goto fail_num;
        }
        *cur++ = ',';
        *cur++ = '\n';
        goto val_end;
    }
    if ((val_type & (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) == (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        ctn_len_tmp = sky_json_unsafe_get_len(&val->val);
        ctn_obj_tmp = (val_type == SKY_JSON_TYPE_OBJ);
        if (sky_unlikely(ctn_len_tmp == 0)) {
            /* write empty container */
            incr_len(16 + (no_indent ? 0 : level * 4));
            cur = write_indent(cur, no_indent ? 0 : level);
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = ',';
            *cur++ = '\n';
            goto val_end;
        } else {
            /* push context, setup new container */
            incr_len(32 + (no_indent ? 0 : level * 4));
            json_mut_write_ctx_set(--ctx,ctn, ctn_len, ctn_obj);
            ctn_len = ctn_len_tmp << (sky_u8_t) ctn_obj_tmp;
            ctn_obj = ctn_obj_tmp;
            cur = write_indent(cur, no_indent ? 0 : level);
            ++level;
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj << 5));
            *cur++ = '\n';
            ctn = val;
            val = ctn->val.uni.ptr; /* tail */
            val = ctn_obj ? val->next->next : val->next;
            goto val_begin;
        }
    }
    if (val_type == SKY_JSON_TYPE_BOOL) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        incr_len(16 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_bool(cur, sky_json_unsafe_get_bool(&val->val));
        cur += 2;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NULL) {
        no_indent = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ctn_len);
        incr_len(16 + (no_indent ? 0 : level * 4));
        cur = write_indent(cur, no_indent ? 0 : level);
        cur = write_null(cur);
        cur += 2;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_RAW) {
        str = sky_json_unsafe_get_str(&val->val);
        check_str_len(str.len);
        incr_len(str.len + 3);
        cur = write_raw(cur, &str);
        *cur++ = ',';
        *cur++ = '\n';
        goto val_end;
    }
    goto fail_type;

    val_end:
    --ctn_len;
    if (sky_unlikely(ctn_len == 0)) {
        goto ctn_end;
    }
    val = val->next;
    goto val_begin;

    ctn_end:
    cur -= 2;
    *cur++ = '\n';
    incr_len(level * 4);
    cur = write_indent(cur, --level);
    *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj << 5));
    if (sky_unlikely((sky_uchar_t *) ctx >= end)) {
        goto doc_end;
    }
    val = ctn->next;
    json_mut_write_ctx_get(ctx++, &ctn, &ctn_len, &ctn_obj);
    ctn_len--;
    *cur++ = ',';
    *cur++ = '\n';
    if (sky_likely(ctn_len > 0)) {
        goto val_begin;
    } else {
        goto ctn_end;
    }

    doc_end:
    *cur = '\0';

    result->len = (sky_usize_t) (cur - hdr);
    result->data = hdr;

    return result;


    fail_alloc:
    return_err("invalid JSON value type");
    return null;
    fail_type:
    return_err("invalid JSON value type");
    fail_num:
    return_err("nan or inf number is not allowed");
    fail_str:
    return_err("invalid utf-8 encoding in string");

#undef return_err
#undef incr_len
#undef check_str_len
}

static sky_str_t *
json_mut_write_minify(const sky_json_mut_val_t *root, sky_u32_t opts) {
#define return_err(_msg) do { \
    sky_free(result);         \
    sky_log_error(_msg);      \
    return null;              \
} while (false)

#define incr_len(_len) do { \
    ext_len = (sky_usize_t)(_len); \
    if (sky_unlikely((sky_uchar_t *)(cur + ext_len) >= (sky_uchar_t *)ctx)) { \
        alc_inc = sky_max(alc_len >> 1, ext_len);                             \
        alc_inc = size_align_up(alc_inc, sizeof(json_mut_write_ctx_t));       \
        if (size_add_is_overflow(alc_len, alc_inc)) {                         \
            goto fail_alloc;\
        }                   \
        alc_len += alc_inc; \
        tmp = sky_realloc(result, alc_len + sizeof(sky_str_t));               \
        if (sky_unlikely(!tmp)) {  \
            goto fail_alloc;\
        }                   \
        result = (sky_str_t *)tmp; \
        tmp += sizeof(sky_str_t);  \
        ctx_len = (sky_usize_t)(end - (sky_uchar_t *)ctx);                    \
        ctx_tmp = (json_mut_write_ctx_t *)(tmp + (alc_len - ctx_len));            \
        sky_memmove(ctx_tmp, tmp + ((sky_uchar_t *)ctx - hdr), ctx_len);      \
        ctx = ctx_tmp;      \
        cur = tmp + (cur - hdr); \
        end = tmp + alc_len; \
        hdr = tmp; \
    } \
} while (false)

#define check_str_len(_len) do { \
    if ((SKY_USIZE_MAX < SKY_U64_MAX) && ((_len) >= (SKY_USIZE_MAX - 16) / 6)) \
        goto fail_alloc;         \
} while (false)


    const sky_u8_t *enc_table = get_enc_table_with_flag(opts);
    const sky_bool_t esc = (opts & SKY_JSON_WRITE_ESCAPE_UNICODE) != 0;
    const sky_bool_t inv = (opts & SKY_JSON_WRITE_ALLOW_INVALID_UNICODE) != 0;


    sky_str_t *result = null;
    sky_usize_t alc_len = 0 * WRITER_ESTIMATED_MINIFY_RATIO + 64;
    alc_len = size_align_up(alc_len, sizeof(json_mut_write_ctx_t));
    sky_uchar_t *hdr = sky_malloc(alc_len + sizeof(sky_str_t));
    if (sky_unlikely(!hdr)) {
        goto fail_alloc;
    }
    result = (sky_str_t *) hdr;
    hdr += sizeof(sky_str_t);

    sky_uchar_t *cur = hdr;
    sky_uchar_t *end = hdr + alc_len;
    json_mut_write_ctx_t *ctx = (json_mut_write_ctx_t *) end;

    sky_str_t str;
    sky_uchar_t *tmp;
    json_mut_write_ctx_t *ctx_tmp;
    sky_json_mut_val_t *val, *ctn;
    sky_usize_t alc_inc, ctx_len, ctn_len, ctn_len_tmp, ext_len;
    sky_u8_t val_type;
    sky_bool_t ctn_obj, ctn_obj_tmp, is_key;


    val = (sky_json_mut_val_t *) root;
    val_type = sky_json_unsafe_get_type(&val->val);
    ctn_obj = (val_type == SKY_JSON_TYPE_OBJ);
    ctn_len = sky_json_unsafe_get_len(&val->val) << (sky_u8_t) ctn_obj;
    *cur++ = (sky_u8_t) ('[' | ((sky_u8_t) ctn_obj << 5));
    ctn = val;
    val = val->val.uni.ptr; /* tail */
    val = ctn_obj ? val->next->next : val->next;

    val_begin:
    val_type = sky_json_unsafe_get_type(&val->val);
    if (val_type == SKY_JSON_TYPE_STR) {
        is_key = (sky_bool_t) ((sky_u8_t) ctn_obj & (sky_u8_t) ~ctn_len);
        str = sky_json_unsafe_get_str(&val->val);
        check_str_len(str.len);
        incr_len(str.len * 6 + 16);
        cur = write_string(cur, &str, enc_table, esc, inv);
        if (sky_unlikely(!cur)) {
            goto fail_str;
        }
        *cur++ = is_key ? ':' : ',';
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NUM) {
        incr_len(32);
        cur = write_number(cur, &val->val, opts);
        if (sky_unlikely(!cur)) {
            goto fail_num;
        }
        *cur++ = ',';
        goto val_end;
    }
    if ((val_type & (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) == (SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ)) {
        ctn_len_tmp = sky_json_unsafe_get_len(&val->val);
        ctn_obj_tmp = (val_type == SKY_JSON_TYPE_OBJ);

        incr_len(16);
        if (sky_unlikely(ctn_len_tmp == 0)) {
            /* write empty container */
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj_tmp << 5));
            *cur++ = ',';
            goto val_end;
        } else {
            /* push context, setup new container */
            json_mut_write_ctx_set(--ctx, ctn, ctn_len, ctn_obj);
            ctn_len = ctn_len_tmp << (sky_u8_t) ctn_obj_tmp;
            ctn_obj = ctn_obj_tmp;
            *cur++ = (sky_uchar_t) ('[' | ((sky_u8_t) ctn_obj << 5));
            ctn = val;
            val = ctn->val.uni.ptr; /* tail */
            val = ctn_obj ? val->next->next : val->next;
            goto val_begin;
        }
    }
    if (val_type == SKY_JSON_TYPE_BOOL) {
        incr_len(16);
        cur = write_bool(cur, sky_json_unsafe_get_bool(&val->val));
        ++cur;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_NULL) {
        incr_len(16);
        cur = write_null(cur);
        ++cur;
        goto val_end;
    }
    if (val_type == SKY_JSON_TYPE_RAW) {
        str = sky_json_unsafe_get_str(&val->val);
        check_str_len(str.len);
        incr_len(str.len + 2);
        cur = write_raw(cur, &str);
        *cur++ = ',';
        goto val_end;
    }
    goto fail_type;

    val_end:
    --ctn_len;
    if (sky_unlikely(ctn_len == 0)) {
        goto ctn_end;
    }
    val = val->next;
    goto val_begin;

    ctn_end:
    --cur;
    *cur++ = (sky_uchar_t) (']' | ((sky_u8_t) ctn_obj << 5));
    *cur++ = ',';
    if (sky_unlikely((sky_uchar_t *) ctx >= end)) {
        goto doc_end;
    }
    val = ctn->next;
    json_mut_write_ctx_get(ctx++, &ctn, &ctn_len, &ctn_obj);
    --ctn_len;
    if (sky_likely(ctn_len > 0)) {
        goto val_begin;
    } else {
        goto ctn_end;
    }

    doc_end:
    *--cur = '\0';

    result->len = (sky_usize_t) (cur - hdr);
    result->data = hdr;

    return result;


    fail_alloc:
    return_err("invalid JSON value type");
    return null;
    fail_type:
    return_err("invalid JSON value type");
    fail_num:
    return_err("nan or inf number is not allowed");
    fail_str:
    return_err("invalid utf-8 encoding in string");

#undef return_err
#undef incr_len
#undef check_str_len
}

static sky_inline void
json_write_ctx_set(json_write_ctx_t *ctx, sky_usize_t size, sky_bool_t is_obj) {
    ctx->tag = (size << 1) | (sky_usize_t) is_obj;
}

static sky_inline void
json_write_ctx_get(const json_write_ctx_t *ctx, sky_usize_t *size, sky_bool_t *is_obj) {
    sky_usize_t tag = ctx->tag;
    *size = tag >> 1;
    *is_obj = (sky_bool_t) (tag & 1);
}

static sky_inline void
json_mut_write_ctx_set(json_mut_write_ctx_t *ctx, sky_json_mut_val_t *ctn, sky_usize_t size, sky_bool_t is_obj) {
    ctx->tag = (size << 1) | (sky_usize_t) is_obj;
    ctx->ctn = ctn;
}

static sky_inline void
json_mut_write_ctx_get(json_mut_write_ctx_t *ctx, sky_json_mut_val_t **ctn, sky_usize_t *size, sky_bool_t *is_obj) {
    sky_usize_t tag = ctx->tag;
    *size = tag >> 1;
    *is_obj = (sky_bool_t) (tag & 1);
    *ctn = ctx->ctn;
}

static sky_inline const sky_u8_t *
get_enc_table_with_flag(sky_u32_t opts) {

    /** Character encode type table: escape unicode, escape '/'.
    (generate with misc/make_tables.c) */
    static const sky_u8_t enc_table_esc_slash[256] = {
            3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 3, 2, 2, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            9, 9, 9, 9, 9, 9, 9, 9, 1, 1, 1, 1, 1, 1, 1, 1
    };

    /** Character encode type table: escape unicode, don't escape '/'.
    (generate with misc/make_tables.c) */
    static const sky_u8_t enc_table_esc[256] = {
            3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 3, 2, 2, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
            9, 9, 9, 9, 9, 9, 9, 9, 1, 1, 1, 1, 1, 1, 1, 1
    };

    /** Character encode type table: don't escape unicode, escape '/'.
    (generate with misc/make_tables.c) */
    static const sky_u8_t enc_table_cpy_slash[256] = {
            3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 3, 2, 2, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
            8, 8, 8, 8, 8, 8, 8, 8, 1, 1, 1, 1, 1, 1, 1, 1
    };

    /** Character encode type table: don't escape unicode, don't escape '/'.
    (generate with misc/make_tables.c) */
    static const sky_u8_t enc_table_cpy[256] = {
            3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 3, 2, 2, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
            6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
            8, 8, 8, 8, 8, 8, 8, 8, 1, 1, 1, 1, 1, 1, 1, 1
    };

    if (sky_unlikely(opts & SKY_JSON_WRITE_ESCAPE_UNICODE)) {
        if (sky_unlikely(opts & SKY_JSON_WRITE_ESCAPE_SLASHES)) {
            return enc_table_esc_slash;
        } else {
            return enc_table_esc;
        }
    } else {
        if (sky_unlikely(opts & SKY_JSON_WRITE_ESCAPE_SLASHES)) {
            return enc_table_cpy_slash;
        } else {
            return enc_table_cpy;
        }
    }
}

static sky_inline sky_uchar_t *
write_raw(sky_uchar_t *cur, const sky_str_t *src) {
    sky_memcpy(cur, src->data, src->len);

    return (cur + src->len);
}

/** Write null (requires 8 bytes buffer). */
static sky_inline sky_uchar_t *
write_null(sky_uchar_t *cur) {
    sky_memcpy8(cur, "null,\n\0\0");
    return cur + 4;
}

/** Write bool (requires 8 bytes buffer). */
static sky_inline sky_uchar_t *
write_bool(sky_uchar_t *cur, sky_bool_t val) {
    if (val) {
        sky_memcpy8(cur, "true,\n\0\0");
    } else {
        sky_memcpy8(cur, "false,\n\0");
    }
    return cur + 5 - val;
}

/** Write indent (requires level * 4 bytes buffer). */
static sky_inline sky_uchar_t *
write_indent(sky_uchar_t *cur, sky_usize_t level) {
    while (level-- > 0) {
        sky_memcpy4(cur, "    ");
        cur += 4;
    }
    return cur;
}

static sky_inline sky_uchar_t *
write_number(sky_uchar_t *cur, const sky_json_val_t *val, sky_u32_t opts) {
    (void) opts;
    if (val->tag & SKY_JSON_SUBTYPE_REAL) {
        const sky_u8_t n = sky_f64_to_str(val->uni.f64, cur);
        if (sky_unlikely(!n)) {
            return null;
        }
        cur += n;
    } else {
        sky_u64_t pos = val->uni.u64;
        sky_usize_t sgn = ((val->tag & SKY_JSON_SUBTYPE_INT) > 0) & ((sky_i64_t) pos < 0);
        if (sgn) {
            *cur++ = '-';
            pos = ~pos + 1;
        }
        cur += sky_u64_to_str(pos, cur);
    }


    return cur;
}

static sky_uchar_t *
write_string(sky_uchar_t *cur, const sky_str_t *str, const sky_u8_t *enc_table, sky_bool_t esc, sky_bool_t inv) {

    /** Escaped hex character table: ["00" "01" "02" ... "FD" "FE" "FF"].
    (generate with misc/make_tables.c) */
    static const sky_uchar_t esc_hex_char_table[512] = {
            '0', '0', '0', '1', '0', '2', '0', '3',
            '0', '4', '0', '5', '0', '6', '0', '7',
            '0', '8', '0', '9', '0', 'A', '0', 'B',
            '0', 'C', '0', 'D', '0', 'E', '0', 'F',
            '1', '0', '1', '1', '1', '2', '1', '3',
            '1', '4', '1', '5', '1', '6', '1', '7',
            '1', '8', '1', '9', '1', 'A', '1', 'B',
            '1', 'C', '1', 'D', '1', 'E', '1', 'F',
            '2', '0', '2', '1', '2', '2', '2', '3',
            '2', '4', '2', '5', '2', '6', '2', '7',
            '2', '8', '2', '9', '2', 'A', '2', 'B',
            '2', 'C', '2', 'D', '2', 'E', '2', 'F',
            '3', '0', '3', '1', '3', '2', '3', '3',
            '3', '4', '3', '5', '3', '6', '3', '7',
            '3', '8', '3', '9', '3', 'A', '3', 'B',
            '3', 'C', '3', 'D', '3', 'E', '3', 'F',
            '4', '0', '4', '1', '4', '2', '4', '3',
            '4', '4', '4', '5', '4', '6', '4', '7',
            '4', '8', '4', '9', '4', 'A', '4', 'B',
            '4', 'C', '4', 'D', '4', 'E', '4', 'F',
            '5', '0', '5', '1', '5', '2', '5', '3',
            '5', '4', '5', '5', '5', '6', '5', '7',
            '5', '8', '5', '9', '5', 'A', '5', 'B',
            '5', 'C', '5', 'D', '5', 'E', '5', 'F',
            '6', '0', '6', '1', '6', '2', '6', '3',
            '6', '4', '6', '5', '6', '6', '6', '7',
            '6', '8', '6', '9', '6', 'A', '6', 'B',
            '6', 'C', '6', 'D', '6', 'E', '6', 'F',
            '7', '0', '7', '1', '7', '2', '7', '3',
            '7', '4', '7', '5', '7', '6', '7', '7',
            '7', '8', '7', '9', '7', 'A', '7', 'B',
            '7', 'C', '7', 'D', '7', 'E', '7', 'F',
            '8', '0', '8', '1', '8', '2', '8', '3',
            '8', '4', '8', '5', '8', '6', '8', '7',
            '8', '8', '8', '9', '8', 'A', '8', 'B',
            '8', 'C', '8', 'D', '8', 'E', '8', 'F',
            '9', '0', '9', '1', '9', '2', '9', '3',
            '9', '4', '9', '5', '9', '6', '9', '7',
            '9', '8', '9', '9', '9', 'A', '9', 'B',
            '9', 'C', '9', 'D', '9', 'E', '9', 'F',
            'A', '0', 'A', '1', 'A', '2', 'A', '3',
            'A', '4', 'A', '5', 'A', '6', 'A', '7',
            'A', '8', 'A', '9', 'A', 'A', 'A', 'B',
            'A', 'C', 'A', 'D', 'A', 'E', 'A', 'F',
            'B', '0', 'B', '1', 'B', '2', 'B', '3',
            'B', '4', 'B', '5', 'B', '6', 'B', '7',
            'B', '8', 'B', '9', 'B', 'A', 'B', 'B',
            'B', 'C', 'B', 'D', 'B', 'E', 'B', 'F',
            'C', '0', 'C', '1', 'C', '2', 'C', '3',
            'C', '4', 'C', '5', 'C', '6', 'C', '7',
            'C', '8', 'C', '9', 'C', 'A', 'C', 'B',
            'C', 'C', 'C', 'D', 'C', 'E', 'C', 'F',
            'D', '0', 'D', '1', 'D', '2', 'D', '3',
            'D', '4', 'D', '5', 'D', '6', 'D', '7',
            'D', '8', 'D', '9', 'D', 'A', 'D', 'B',
            'D', 'C', 'D', 'D', 'D', 'E', 'D', 'F',
            'E', '0', 'E', '1', 'E', '2', 'E', '3',
            'E', '4', 'E', '5', 'E', '6', 'E', '7',
            'E', '8', 'E', '9', 'E', 'A', 'E', 'B',
            'E', 'C', 'E', 'D', 'E', 'E', 'E', 'F',
            'F', '0', 'F', '1', 'F', '2', 'F', '3',
            'F', '4', 'F', '5', 'F', '6', 'F', '7',
            'F', '8', 'F', '9', 'F', 'A', 'F', 'B',
            'F', 'C', 'F', 'D', 'F', 'E', 'F', 'F'
    };


    /** Escaped single character table. (generate with misc/make_tables.c) */
    static const sky_uchar_t esc_single_char_table[512] = {
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            '\\', 'b', '\\', 't', '\\', 'n', ' ', ' ',
            '\\', 'f', '\\', 'r', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', '\\', '"', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', '\\', '/',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            '\\', '\\', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '
    };


#if __BYTE_ORDER == __LITTLE_ENDIAN
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
    /* The replacement character U+FFFD, used to indicate invalid character. */
    const sky_uchar_t rep[] = {'F', 'F', 'F', 'D'};
    const sky_uchar_t pre[] = {'\\', 'u', '0', '0'};

    const sky_uchar_t *src = str->data;
    const sky_uchar_t *end = str->data + str->len;
    *cur++ = '"';

    copy_ascii:
    /*
     Copy continuous ASCII, loop unrolling, same as the following code:

         while (end > src) (
            if (unlikely(enc_table[*src])) break;
            *cur++ = *src++;
         );
     */
#define expr_jump(i) \
    if (sky_unlikely(enc_table[src[i]])) goto stop_char_##i;

#define expr_stop(i) \
    stop_char_##i:   \
    sky_memcpy(cur, src, i); \
    cur += (i);      \
    src += (i);      \
    goto copy_utf8;

    while ((end - src) >= 16) {
        repeat16_incr(expr_jump);
        sky_memcpy(cur, src, 16);
        cur += 16;
        src += 16;
    }

    while ((end - src) >= 4) {
        repeat4_incr(expr_jump);
        sky_memcpy4(cur, src);
        cur += 4;
        src += 4;
    }

    while (end > src) {
        expr_jump(0);
        *cur++ = *src++;
    }
    *cur++ = '"';
    return cur;

    repeat16_incr(expr_stop);

#undef expr_jump
#undef expr_stop

    copy_utf8:
    if (sky_unlikely((src + 4) > end)) {
        if (end == src) {
            goto copy_end;
        }
        if ((end - src) < (enc_table[*src] >> 1)) {
            goto err_one;
        }
    }
    switch (enc_table[*src]) {
        case CHAR_ENC_CPY_1: {
            *cur++ = *src++;
            goto copy_ascii;
        }
        case CHAR_ENC_CPY_2: {
            sky_u16_t v;
            v = *(sky_u16_t *) src;
            if (sky_unlikely(!is_valid_seq_2(v))) {
                goto err_cpy;
            }
            sky_memcpy2(cur, src);
            cur += 2;
            src += 2;
            goto copy_utf8;
        }
        case CHAR_ENC_CPY_3: {
            sky_u32_t v, tmp;
            if (sky_likely(src + 4 <= end)) {
                v = *(sky_u32_t *) src;
                if (sky_unlikely(!is_valid_seq_3(v))) {
                    goto err_cpy;
                }
                sky_memcpy4(cur, src);
            } else {
                *((sky_u16_t *) &v) = *(sky_u16_t *) src;
                *((sky_u8_t *) &v + 2) = src[2];
                *((sky_u8_t *) &v + 3) = 0;

                if (sky_unlikely(!is_valid_seq_3(v))) {
                    goto err_cpy;
                }
                sky_memcpy4(cur, &v);
            }
            cur += 3;
            src += 3;
            goto copy_utf8;
        }
        case CHAR_ENC_CPY_4: {
            sky_u32_t v, tmp;
            v = *(sky_u32_t *) src;
            if (sky_unlikely(!is_valid_seq_4(v))) {
                goto err_cpy;
            }
            sky_memcpy4(cur, src);
            cur += 4;
            src += 4;
            goto copy_utf8;
        }
        case CHAR_ENC_ESC_A: {
            sky_memmove2(cur, &esc_single_char_table[*src * 2]);
            cur += 2;
            src += 1;
            goto copy_utf8;
        }
        case CHAR_ENC_ESC_1: {
            sky_memcpy4(cur + 0, &pre);
            sky_memcpy2(cur + 4, &esc_hex_char_table[*src * 2]);
            cur += 6;
            src += 1;
            goto copy_utf8;
        }
        case CHAR_ENC_ESC_2: {
            sky_u16_t u, v;
            v = *(sky_u16_t *) src;
            if (sky_unlikely(!is_valid_seq_2(v))) goto err_esc;

            u = (sky_u16_t) (((sky_u16_t) (src[0] & 0x1F) << 6) |
                             ((sky_u16_t) (src[1] & 0x3F) << 0));
            sky_memcpy2(cur + 0, &pre);
            sky_memcpy2(cur + 2, &esc_hex_char_table[(u >> 8) * 2]);
            sky_memcpy2(cur + 4, &esc_hex_char_table[(u & 0xFF) * 2]);
            cur += 6;
            src += 2;
            goto copy_utf8;
        }
        case CHAR_ENC_ESC_3: {
            sky_u16_t u;
            sky_u32_t v, tmp;

            *((sky_u16_t *) &v) = *(sky_u16_t *) src;
            *((sky_u8_t *) &v + 2) = src[2];
            *((sky_u8_t *) &v + 3) = 0;
            if (sky_unlikely(!is_valid_seq_3(v))) {
                goto err_esc;
            }

            u = (sky_u16_t) (((sky_u16_t) (src[0] & 0x0F) << 12) |
                             ((sky_u16_t) (src[1] & 0x3F) << 6) |
                             ((sky_u16_t) (src[2] & 0x3F) << 0));
            sky_memcpy2(cur + 0, &pre);
            sky_memcpy2(cur + 2, &esc_hex_char_table[(u >> 8) * 2]);
            sky_memcpy2(cur + 4, &esc_hex_char_table[(u & 0xFF) * 2]);
            cur += 6;
            src += 3;
            goto copy_utf8;
        }
        case CHAR_ENC_ESC_4: {
            sky_u32_t hi, lo, u, v, tmp;
            v = *(sky_u32_t *) src;
            if (sky_unlikely(!is_valid_seq_4(v))) {
                goto err_esc;
            }

            u = ((sky_u32_t) (src[0] & 0x07) << 18) |
                ((sky_u32_t) (src[1] & 0x3F) << 12) |
                ((sky_u32_t) (src[2] & 0x3F) << 6) |
                ((sky_u32_t) (src[3] & 0x3F) << 0);
            u -= 0x10000;
            hi = (u >> 10) + 0xD800;
            lo = (u & 0x3FF) + 0xDC00;
            sky_memcpy2(cur + 0, &pre);
            sky_memcpy2(cur + 2, &esc_hex_char_table[(hi >> 8) * 2]);
            sky_memcpy2(cur + 4, &esc_hex_char_table[(hi & 0xFF) * 2]);
            sky_memcpy2(cur + 6, &pre);
            sky_memcpy2(cur + 8, &esc_hex_char_table[(lo >> 8) * 2]);
            sky_memcpy2(cur + 10, &esc_hex_char_table[(lo & 0xFF) * 2]);
            cur += 12;
            src += 4;
            goto copy_utf8;
        }
        case CHAR_ENC_ERR_1: {
            goto err_one;
        }
        default:
            break;
    }

    copy_end:
    *cur++ = '"';
    return cur;

    err_one:
    if (esc) {
        goto err_esc;
    } else {
        goto err_cpy;
    }

    err_cpy:
    if (!inv) {
        return null;
    }
    *cur++ = *src++;
    goto copy_utf8;

    err_esc:
    if (!inv) {
        return null;
    }
    sky_memcpy2(cur + 0, &pre);
    sky_memcpy4(cur + 2, &rep);
    cur += 6;
    src += 1;
    goto copy_utf8;


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


/** Returns whether the size is overflow after increment. */
static sky_inline sky_bool_t
size_add_is_overflow(sky_usize_t size, sky_usize_t add) {
    sky_usize_t val = size + add;
    return (val < size) | (val < add);
}

/** Align size upwards (may overflow). */
static sky_inline sky_usize_t
size_align_up(sky_usize_t size, sky_usize_t align) {
    if (sky_is_2_power(align)) {
        return (size + (align - 1)) & ~(align - 1);
    } else {
        return size + align - (size + align - 1) % align - 1;
    }
}

static sky_bool_t
json_str_pool_grow(json_str_pool_t *pool, sky_usize_t n) {
    json_str_chunk_t *chunk;
    sky_usize_t size = n + sizeof(json_str_chunk_t);

    size = sky_max(pool->chunk_size, size);
    chunk = sky_malloc(size);
    if (sky_unlikely(!chunk)) {
        return false;
    }

    chunk->next = pool->chunks;
    pool->chunks = chunk;
    pool->cur = (sky_uchar_t *) chunk + sizeof(json_str_chunk_t);
    pool->end = (sky_uchar_t *) chunk + size;

    n = pool->chunk_size << 1;
    size = sky_min(n, pool->chunk_size_max);
    pool->chunk_size = size;

    return true;
}

static sky_bool_t
json_val_pool_grow(json_val_pool_t *pool, sky_usize_t n) {
    json_val_chunk_t *chunk;
    sky_usize_t size;

    if (n >= SKY_USIZE_MAX / sizeof(sky_json_mut_val_t) - 16) {
        return false;
    }
    size = (n + 1) * sizeof(sky_json_mut_val_t);
    size = sky_max(pool->chunk_size, size);
    chunk = sky_malloc(size);
    if (sky_unlikely(!chunk)) {
        return false;
    }

    chunk->next = pool->chunks;
    pool->chunks = chunk;
    pool->cur = (sky_json_mut_val_t *) ((sky_uchar_t *) chunk + sizeof(sky_json_mut_val_t));
    pool->end = (sky_json_mut_val_t *) ((sky_uchar_t *) chunk + size);

    n = pool->chunk_size << 1;
    size = sky_min(n, pool->chunk_size_max);
    pool->chunk_size = size;

    return true;
}

static void
json_str_pool_release(json_str_pool_t *pool) {
    json_str_chunk_t *chunk = pool->chunks, *next;
    while (chunk) {
        next = chunk->next;
        sky_free(chunk);
        chunk = next;
    }
}

static void
json_val_pool_release(json_val_pool_t *pool) {
    json_val_chunk_t *chunk = pool->chunks, *next;
    while (chunk) {
        next = chunk->next;
        sky_free(chunk);
        chunk = next;
    }
}