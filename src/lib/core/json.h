//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H

#include "types.h"
#include "string.h"
#include "palloc.h"
#include "mem_pool.h"

#if defined(__cplusplus)
extern "C" {
#endif

/** Default option (RFC 8259 compliant):
- Read positive integer as uint64_t.
- Read negative integer as int64_t.
- Read floating-point number as double with round-to-nearest mode.
- Read integer which cannot fit in uint64_t or int64_t as double.
- Report error if real number is infinity.
- Report error if string contains invalid UTF-8 character or BOM.
- Report error on trailing commas, comments, inf and nan literals. */
#define SKY_JSON_READ_NO_FLAG SKY_U32(0)

/** Read the input data in-situ.
    This option allows the reader to modify and use input data to store string
    values, which can increase reading speed slightly.
    The caller should hold the input data before free the document.
    The input data must be padded by at least `SKY_JSON_PADDING_SIZE` bytes.
    For example: "[1,2]" should be "[1,2]\0\0\0\0", length should be 5. */
#define SKY_JSON_READ_IN_SITU (SKY_U32(1) << 0)

/** Stop when done instead of issuing an error if there's additional content
    after a JSON document. This option may be used to parse small pieces of JSON
    in larger data, such as `NDJSON`. */
#define SKY_JSON_READ_STOP_WHEN_DONE    (SKY_U32(1)  << 1)

/** Allow single trailing comma at the end of an object or array,
    such as [1,2,3,] {"a":1,"b":2,} (non-standard). */
#define SKY_JSON_READ_ALLOW_TRAILING_COMMAS (SKY_U32(1) << 2)

/** Allow C-style single line and multiple line comments (non-standard). */
#define SKY_JSON_READ_ALLOW_COMMENTS    (SKY_U32(1)  << 3)

/** Allow inf/nan number and literal, case-insensitive,
    such as 1e999, NaN, inf, -Infinity (non-standard). */
#define SKY_JSON_READ_ALLOW_INF_AND_NAN (SKY_U32(1)  << 4)

/** Read number as raw string (value with SKY_JSON_TYPE_RAW type),
    inf/nan literal is also read as raw with `ALLOW_INF_AND_NAN` flag. */
#define SKY_JSON_READ_NUMBER_AS_RAW (SKY_U32(1)  << 5)

/** Allow reading invalid unicode when parsing string values (non-standard).
    Invalid characters will be allowed to appear in the string values, but
    invalid escape sequences will still be reported as errors.
    This flag does not affect the performance of correctly encoded strings.

    @warning Strings in JSON values may contain incorrect encoding when this
    option is used, you need to handle these strings carefully to avoid security
    risks. */
#define SKY_JSON_READ_ALLOW_INVALID_UNICODE (SKY_U32(1)  << 6)


/** Default option:
    - Write JSON minify.
    - Report error on inf or nan number.
    - Report error on invalid UTF-8 string.
    - Do not escape unicode or slash. */
#define SKY_JSON_WRITE_NO_FLAG                  (SKY_U32(0) << 0)

/** Write JSON pretty with 4 space indent. */
#define SKY_JSON_WRITE_PRETTY                   (SKY_U32(1) << 0)

/** Escape unicode as `uXXXX`, make the output ASCII only. */
#define SKY_JSON_WRITE_ESCAPE_UNICODE           (SKY_U32(1) << 1)

/** Escape '/' as '\/'. */
#define SKY_JSON_WRITE_ESCAPE_SLASHES           (SKY_U32(1) << 2)

/** Write inf and nan number as 'Infinity' and 'NaN' literal (non-standard). */
#define SKY_JSON_WRITE_ALLOW_INF_AND_NAN        (SKY_U32(1) << 3)

/** Write inf and nan number as null literal.
    This flag will override `SKY_JSON_WRITE_ALLOW_INF_AND_NAN` flag. */
#define SKY_JSON_WRITE_INF_AND_NAN_AS_NULL      (SKY_U32(1) << 4)

/** Allow invalid unicode when encoding string values (non-standard).
    Invalid characters in string value will be copied byte by byte.
    If `SKY_JSON_WRITE_ESCAPE_UNICODE` flag is also set, invalid character will be
    escaped as `U+FFFD` (replacement character).
    This flag does not affect the performance of correctly encoded strings. */
#define SKY_JSON_WRITE_ALLOW_INVALID_UNICODE    (SKY_U32(1) << 5)


/** Type of JSON value (3 bit). */
#define SKY_JSON_TYPE_NONE        SKY_U8(0)        /* _____000 */
#define SKY_JSON_TYPE_RAW         SKY_U8(1)        /* _____001 */
#define SKY_JSON_TYPE_NULL        SKY_U8(2)        /* _____010 */
#define SKY_JSON_TYPE_BOOL        SKY_U8(3)        /* _____011 */
#define SKY_JSON_TYPE_NUM         SKY_U8(4)        /* _____100 */
#define SKY_JSON_TYPE_STR         SKY_U8(5)        /* _____101 */
#define SKY_JSON_TYPE_ARR         SKY_U8(6)        /* _____110 */
#define SKY_JSON_TYPE_OBJ         SKY_U8(7)        /* _____111 */

/** Subtype of JSON value (2 bit). */
#define SKY_JSON_SUBTYPE_NONE     (0 << 3) /* ___00___ */
#define SKY_JSON_SUBTYPE_FALSE    (0 << 3) /* ___00___ */
#define SKY_JSON_SUBTYPE_TRUE     (1 << 3) /* ___01___ */
#define SKY_JSON_SUBTYPE_UINT     (0 << 3) /* ___00___ */
#define SKY_JSON_SUBTYPE_INT      (1 << 3) /* ___01___ */
#define SKY_JSON_SUBTYPE_REAL     (2 << 3) /* ___10___ */

/** Mask and bits of JSON value. */
#define SKY_JSON_TYPE_MASK        SKY_U8(0x07)     /* _____111 */
#define SKY_JSON_TYPE_BIT         SKY_U8(3)
#define SKY_JSON_SUBTYPE_MASK     SKY_U8(0x18)     /* ___11___ */
#define SKY_JSON_SUBTYPE_BIT      SKY_U8(2)
#define SKY_JSON_RESERVED_MASK    SKY_U8(0xE0)     /* 111_____ */
#define SKY_JSON_RESERVED_BIT     SKY_U8(3)
#define SKY_JSON_TAG_MASK         SKY_U8(0xFF)     /* 11111111 */
#define SKY_JSON_TAG_BIT          SKY_U8(8)

typedef struct sky_json_doc_s sky_json_doc_t;
typedef struct sky_json_val_s sky_json_val_t;
typedef struct sky_json_mut_doc_s sky_json_mut_doc_t;
typedef struct sky_json_mut_val_s sky_json_mut_val_t;

typedef union {
    sky_u64_t u64;
    sky_i64_t i64;
    sky_usize_t ofs;
    sky_f64_t f64;
    sky_uchar_t *str;
    void *ptr;
} sky_json_val_uni_t;

struct sky_json_doc_s {
    sky_json_val_t *root;
    sky_usize_t read_n;
    sky_usize_t val_read_n;
    sky_uchar_t *str_pool;
};


struct sky_json_val_s {
    sky_u64_t tag;
    sky_json_val_uni_t uni;
};

struct sky_json_mut_doc_s {
    sky_json_mut_val_t *root; /**< root value of the JSON document, nullable */
    sky_mem_pool_t *str_pool; /**< string memory pool */
    sky_mem_pool_t *val_pool; /**< value memory pool */
};

/**
 Mutable JSON value, 24 bytes.
 The 'tag' and 'uni' field is same as immutable value.
 The 'next' field links all elements inside the container to be a cycle.
 */
struct sky_json_mut_val_s {
    uint64_t tag; /**< type, subtype and length */
    sky_json_val_uni_t uni; /**< payload */
    sky_json_mut_val_t *next; /**< the next value in circular linked list */
};

/* ============================= read unsafe api ========================= */

static sky_inline sky_u8_t
sky_json_unsafe_get_type(const sky_json_val_t *val) {
    return (sky_u8_t) val->tag & SKY_JSON_TYPE_MASK;
}

static sky_inline sky_u8_t
sky_json_unsafe_get_subtype(const sky_json_val_t *val) {
    return (sky_u8_t) val->tag & SKY_JSON_SUBTYPE_MASK;
}

static sky_inline sky_u8_t
sky_json_unsafe_get_tag(const sky_json_val_t *val) {
    return (sky_u8_t) val->tag & SKY_JSON_TAG_MASK;
}


static sky_inline sky_bool_t
sky_json_unsafe_is_raw(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_RAW;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_null(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_NULL;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_bool(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_BOOL;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_num(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_NUM;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_str(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_STR;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_arr(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_ARR;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_obj(const sky_json_val_t *val) {
    return sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_OBJ;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_ctn(const sky_json_val_t *val) {
    const sky_u8_t mask = SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ;
    return (sky_json_unsafe_get_tag(val) & mask) == mask;
};

static sky_inline sky_bool_t
sky_json_unsafe_is_uint(const sky_json_val_t *val) {
    const sky_u8_t patt = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_UINT;
    return sky_json_unsafe_get_tag(val) == patt;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_int(const sky_json_val_t *val) {
    const sky_u8_t patt = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_INT;
    return sky_json_unsafe_get_tag(val) == patt;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_real(const sky_json_val_t *val) {
    const sky_u8_t patt = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_REAL;
    return sky_json_unsafe_get_tag(val) == patt;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_true(const sky_json_val_t *val) {
    const sky_u8_t patt = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_TRUE;
    return sky_json_unsafe_get_tag(val) == patt;
}

static sky_inline sky_bool_t
sky_json_unsafe_is_false(const sky_json_val_t *val) {
    const sky_u8_t patt = SKY_JSON_TYPE_BOOL | SKY_JSON_SUBTYPE_FALSE;
    return sky_json_unsafe_get_tag(val) == patt;
}

static sky_inline sky_bool_t
sky_json_unsafe_arr_is_flat(const sky_json_val_t *val) {
    const sky_usize_t ofs = val->uni.ofs;
    const sky_usize_t len = (sky_usize_t) (val->tag >> SKY_JSON_TAG_BIT);
    return len * sizeof(sky_json_val_t) + sizeof(sky_json_val_t) == ofs;
}

static sky_inline sky_usize_t
sky_json_unsafe_get_len(const sky_json_val_t *val) {
    return (sky_usize_t) (val->tag >> SKY_JSON_TAG_BIT);
}

static sky_inline sky_json_val_t *
sky_json_unsafe_get_first(sky_json_val_t *ctn) {
    return ctn + 1;
}

static sky_inline sky_json_val_t *
sky_json_unsafe_get_next(const sky_json_val_t *val) {
    const sky_bool_t is_ctn = sky_json_unsafe_is_ctn(val);
    const sky_usize_t ctn_ofs = val->uni.ofs;
    const sky_usize_t ofs = (is_ctn ? ctn_ofs : sizeof(sky_json_val_t));
    return (sky_json_val_t *) ((uint8_t *) val + ofs);
}

static sky_inline sky_str_t
sky_json_unsafe_get_str(const sky_json_val_t *val) {
    sky_str_t str = {
            .len = sky_json_unsafe_get_len(val),
            .data = val->uni.str
    };

    return str;
}

static sky_inline sky_str_t
sky_json_unsafe_get_raw(const sky_json_val_t *val) {
    sky_str_t str = {
            .len = sky_json_unsafe_get_len(val),
            .data = val->uni.str
    };

    return str;
}

static sky_inline sky_bool_t
sky_json_unsafe_get_bool(const sky_json_val_t *val) {
    return (sky_bool_t) ((sky_json_unsafe_get_tag(val) & SKY_JSON_SUBTYPE_MASK) >> SKY_JSON_TYPE_BIT);
}

static sky_inline sky_u64_t
sky_json_unsafe_get_uint(const sky_json_val_t *val) {
    return val->uni.u64;
}

static sky_inline sky_i64_t
sky_json_unsafe_get_int(const sky_json_val_t *val) {
    return val->uni.i64;
}

static sky_inline sky_f64_t
sky_json_unsafe_get_real(const sky_json_val_t *val) {
    return val->uni.f64;
}

/* ============================= public read api ======================================== */

sky_json_doc_t *sky_json_read_opts(const sky_str_t *str, sky_u32_t opts);

sky_json_val_t *sky_json_obj_get(sky_json_val_t *obj, const sky_uchar_t *key, sky_u32_t key_len);

sky_json_val_t *sky_json_arr_get(sky_json_val_t *arr, sky_usize_t idx);

void sky_json_doc_free(sky_json_doc_t *doc);

static sky_inline sky_json_doc_t *
sky_json_read(const sky_str_t *str, sky_u32_t opts) {
    opts &= ~SKY_JSON_READ_IN_SITU; /* const string cannot be modified */

    return sky_json_read_opts(str, opts);
}

static sky_inline sky_json_val_t *
sky_json_doc_get_root(sky_json_doc_t *doc) {
    return sky_likely(doc) ? doc->root : null;
}


static sky_inline sky_u8_t
sky_json_get_type(const sky_json_val_t *val) {
    return sky_likely(val) ? sky_json_unsafe_get_type(val) : SKY_JSON_TYPE_NONE;
}

static sky_inline sky_u8_t
sky_json_get_subtype(const sky_json_val_t *val) {
    return sky_likely(val) ? sky_json_unsafe_get_subtype(val) : SKY_JSON_SUBTYPE_NONE;
}

static sky_inline sky_bool_t
sky_json_is_raw(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_raw(val);
}

static sky_inline sky_bool_t
sky_json_is_null(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_null(val);
}

static sky_inline sky_bool_t
sky_json_is_bool(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_bool(val);
}

static sky_inline sky_bool_t
sky_json_is_num(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_num(val);
}

static sky_inline sky_bool_t
sky_json_is_str(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_str(val);
}

static sky_inline sky_bool_t
sky_json_is_arr(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_arr(val);
}

static sky_inline sky_bool_t
sky_json_is_obj(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_obj(val);
}

static sky_inline sky_bool_t
sky_json_is_ctn(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_ctn(val);
}

static sky_inline sky_bool_t
sky_json_is_unit(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_uint(val);
}

static sky_inline sky_bool_t
sky_json_is_int(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_int(val);
}

static sky_inline sky_bool_t
sky_json_is_real(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_real(val);
}

static sky_inline sky_bool_t
sky_json_is_true(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_true(val);
}

static sky_inline sky_bool_t
sky_json_is_false(const sky_json_val_t *val) {
    return val && sky_json_unsafe_is_false(val);
}


static sky_inline sky_usize_t
sky_json_obj_size(const sky_json_val_t *obj) {
    return sky_likely(sky_json_unsafe_is_obj(obj)) ? sky_json_unsafe_get_len(obj) : 0;
}


static sky_inline sky_usize_t
sky_json_arr_size(const sky_json_val_t *arr) {
    return sky_likely(sky_json_unsafe_is_arr(arr)) ? sky_json_unsafe_get_len(arr) : 0;
}

static sky_inline sky_json_val_t *
sky_json_arr_get_first(sky_json_val_t *arr) {
    if (sky_likely(sky_json_is_arr((arr)))) {
        if (sky_likely(sky_json_unsafe_get_len(arr) > 0)) {
            return sky_json_unsafe_get_first(arr);
        }
    }
    return null;
}

static sky_inline sky_str_t
sky_json_get_str(const sky_json_val_t *val) {
    if (sky_likely(sky_json_is_str(val))) {
        return sky_json_unsafe_get_str(val);
    } else {
        sky_str_t str = sky_null_string;

        return str;
    }
}

static sky_inline sky_str_t
sky_json_get_raw(const sky_json_val_t *val) {
    if (sky_likely(sky_json_is_raw(val))) {
        return sky_json_unsafe_get_raw(val);
    } else {
        sky_str_t str = sky_null_string;

        return str;
    }
}

static sky_inline sky_bool_t
sky_json_get_bool(const sky_json_val_t *val) {
    return sky_likely(sky_json_is_bool(val)) ? sky_json_unsafe_get_bool(val) : false;
}

static sky_inline sky_u64_t
sky_json_get_uint(const sky_json_val_t *val) {
    return sky_likely(sky_json_is_unit(val)) ? sky_json_unsafe_get_uint(val) : 0;
}

static sky_inline sky_i64_t
sky_json_get_int(const sky_json_val_t *val) {
    return sky_likely(sky_json_is_int(val)) ? sky_json_unsafe_get_int(val) : 0;
}

static sky_inline sky_f64_t
sky_json_get_real(const sky_json_val_t *val) {
    return sky_likely(sky_json_is_raw(val)) ? sky_json_unsafe_get_real(val) : 0.0;
}

/**
 Macro for iterating over an object.
 It works like iterator, but with a more intuitive API.

 @par Example
 @code
    sky_usize idx, max;
    sky_json_val_t *key, *val;
    sky_json_obj_foreach(obj, idx, max, key, val) {
        your_func(key, val);
    }
 @endcode
 */
#define sky_json_obj_foreach(obj, idx, max, key, val) \
    for ((idx) = 0, \
        (max) = sky_json_obj_size(obj), \
        (key) = (obj) ? sky_json_unsafe_get_first(obj) : null, \
        (val) = (key) + 1; \
        (idx) < (max); \
        (idx)++, \
        (key) = sky_json_unsafe_get_next(val), \
        (val) = (key) + 1)


/**
 Macro for iterating over an array.
 It works like iterator, but with a more intuitive API.

 @par Example
 @code
    sky_usize idx, max;
    sky_json_val_t *val;
    sky_json_arr_foreach(arr, idx, max, val) {
        your_func(idx, val);
    }
 @endcode
 */
#define sky_json_arr_foreach(_arr, _idx, _max, _val) \
    for ((_idx) = 0,                                 \
        (_max) = sky_json_arr_size(_arr),            \
        (_val) = sky_json_arr_get_first(_arr);       \
        (_idx) < (_max);                             \
        (_idx)++,                                    \
        (_val) = sky_json_unsafe_get_next(_val))


/* ============================= unsafe write api ======================================== */

static sky_inline void
sky_json_unsafe_set_tag(sky_json_val_t *val, sky_u8_t tag) {
    val->tag = (val->tag & (~(sky_u64_t) SKY_JSON_TAG_MASK)) | (sky_u64_t) tag;
}

static sky_inline void
sky_json_unsafe_set_type(sky_json_val_t *val, sky_u8_t type, sky_u8_t subtype) {
    const sky_u8_t tag = (type | subtype);
    sky_json_unsafe_set_tag(val, tag);
}

static sky_inline void
sky_json_unsafe_set_len(sky_json_val_t *val, sky_usize_t len) {
    sky_u64_t tag = val->tag & SKY_JSON_TAG_MASK;
    tag |= (uint64_t) len << SKY_JSON_TAG_BIT;
    val->tag = tag;
}

static sky_inline void
sky_json_unsafe_set_raw_len(sky_json_val_t *val, sky_uchar_t *str_data, sky_usize_t str_len) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_RAW, SKY_JSON_SUBTYPE_NONE);
    sky_json_unsafe_set_len(val, str_len);
    val->uni.str = str_data;
}

static sky_inline void
sky_json_unsafe_set_raw(sky_json_val_t *val, sky_str_t *str) {
    sky_json_unsafe_set_raw_len(val, str->data, str->len);
}

static sky_inline void
sky_json_unsafe_set_str_len(sky_json_val_t *val, sky_uchar_t *str_data, sky_usize_t str_len) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_STR, SKY_JSON_SUBTYPE_NONE);
    sky_json_unsafe_set_len(val, str_len);
    val->uni.str = str_data;
}

static sky_inline void
sky_json_unsafe_set_str(sky_json_val_t *val, sky_str_t *str) {
    sky_json_unsafe_set_str_len(val, str->data, str->len);
}

static sky_inline void
sky_json_unsafe_set_null(sky_json_val_t *val) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_NULL, SKY_JSON_SUBTYPE_NONE);
    sky_json_unsafe_set_len(val, 0);
}

static sky_inline void
sky_json_unsafe_set_bool(sky_json_val_t *val, sky_bool_t v) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_BOOL, v ? SKY_JSON_SUBTYPE_TRUE : SKY_JSON_SUBTYPE_FALSE);
    sky_json_unsafe_set_len(val, 0);
}

static sky_inline void
sky_json_unsafe_set_int(sky_json_val_t *val, sky_i64_t v) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_NUM, SKY_JSON_SUBTYPE_INT);
    sky_json_unsafe_set_len(val, 0);
    val->uni.i64 = v;
}

static sky_inline void
sky_json_unsafe_set_uint(sky_json_val_t *val, sky_u64_t v) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_NUM, SKY_JSON_SUBTYPE_UINT);
    sky_json_unsafe_set_len(val, 0);
    val->uni.u64 = v;
}

static sky_inline void
sky_json_unsafe_set_arr(sky_json_val_t *val, sky_usize_t n) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_ARR, SKY_JSON_SUBTYPE_REAL);
    sky_json_unsafe_set_len(val, n);
}

static sky_inline void
sky_json_unsafe_set_obj(sky_json_val_t *val, sky_usize_t n) {
    sky_json_unsafe_set_type(val, SKY_JSON_TYPE_OBJ, SKY_JSON_SUBTYPE_REAL);
    sky_json_unsafe_set_len(val, n);
}


/* ============================= public write api ======================================== */


sky_json_mut_doc_t *sky_json_mut_doc_create();

void sky_json_mut_doc_free(sky_json_mut_doc_t *doc);

static sky_inline void
sky_json_mut_set_root(sky_json_mut_doc_t *doc, sky_json_mut_val_t *root) {
    if (sky_likely(doc)) {
        doc->root = root;
    }
}

static sky_inline sky_json_mut_val_t *
sky_json_mut_obj(sky_json_mut_doc_t *doc) {
    if (sky_likely(doc)) {
        sky_json_mut_val_t *val = sky_mem_pool_get(doc->val_pool);
        if (sky_likely(val)) {
            val->tag = SKY_JSON_TYPE_OBJ | SKY_JSON_SUBTYPE_NONE;
            return val;
        }
    }
    return null;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
