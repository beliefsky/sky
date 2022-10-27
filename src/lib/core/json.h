//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H

#include "types.h"
#include "string.h"
#include "palloc.h"

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
    The input data must be padded by at least `YYJSON_PADDING_SIZE` bytes.
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

/** Read number as raw string (value with YYJSON_TYPE_RAW type),
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

struct sky_json_doc_s {
    sky_json_val_t *root;
    sky_usize_t read_n;
    sky_usize_t val_read_n;
    sky_uchar_t *str_pool;
};

struct sky_json_val_s {
    sky_u64_t tag;
    union {
        sky_u64_t u64;
        sky_i64_t i64;
        sky_usize_t ofs;
        sky_f64_t f64;
        sky_uchar_t *str;
        void *ptr;
    };
};


sky_json_doc_t *sky_json_read_opts(const sky_str_t *str, sky_u32_t opts);

sky_json_val_t *sky_json_doc_get_root(sky_json_doc_t *doc);

sky_json_val_t *sky_json_obj_get(sky_json_val_t *obj, const sky_uchar_t *key, sky_u32_t key_len);

void sky_json_doc_free(sky_json_doc_t *doc);


static sky_inline sky_u8_t
sky_json_unsafe_get_type(const sky_json_val_t *val) {
    return (sky_u8_t) val->tag & SKY_JSON_TYPE_MASK;
}


static sky_inline sky_u8_t
sky_json_unsafe_get_tag(const sky_json_val_t *val) {
    return (sky_u8_t) val->tag & SKY_JSON_TAG_MASK;
}


static sky_inline sky_bool_t
sky_json_unsafe_is_ctn(const sky_json_val_t *val) {
    const sky_u8_t mask = SKY_JSON_TYPE_ARR & SKY_JSON_TYPE_OBJ;

    return (sky_json_unsafe_get_tag(val) & mask) == mask;
};


static sky_inline sky_json_doc_t *
sky_json_read(const sky_str_t *str, sky_u32_t opts) {
    opts &= ~SKY_JSON_READ_IN_SITU; /* const string cannot be modified */

    return sky_json_read_opts(str, opts);
}

static sky_inline sky_u8_t
sky_json_get_type(const sky_json_val_t *val) {
    return sky_likely(val) ? sky_json_unsafe_get_type(val) : SKY_JSON_TYPE_NONE;
}

static sky_inline sky_bool_t
sky_json_is_obj(const sky_json_val_t *val) {
    return val && sky_json_unsafe_get_type(val) == SKY_JSON_TYPE_OBJ;
}


// ================================ old version =======================================





typedef struct sky_json_s sky_json_t;
typedef struct sky_json_object_s sky_json_object_t;
typedef struct sky_json_array_s sky_json_array_t;

struct sky_json_s {
    enum {
        json_object = 0,
        json_array,
        json_integer,
        json_float,
        json_string,
        json_boolean,
        json_null
    } type;
    union {
        sky_bool_t boolean;
        sky_i64_t integer;
        sky_f64_t dbl;
        sky_str_t string;

        struct {
            union {
                sky_json_object_t *object;
                sky_json_array_t *array;
            };
            void *current;
            sky_pool_t *pool;
        };
    };
    sky_json_t *parent;
};


struct sky_json_object_s {
    sky_json_t value;
    sky_str_t key;
    sky_json_object_t *prev;
    sky_json_object_t *next;
};

struct sky_json_array_s {
    sky_json_t value;
    sky_json_array_t *prev;
    sky_json_array_t *next;
};

sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json);

sky_str_t *sky_json_tostring(sky_json_t *json);

sky_json_t *sky_json_find(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_object(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_array(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_boolean(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_bool_t value);

sky_json_t *sky_json_put_null(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_integer(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_i64_t value);

sky_json_t *sky_json_put_double(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_f64_t value);

sky_json_t *sky_json_put_string(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_str_t *value);

sky_json_t *sky_json_put_str_len(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len,
                                 sky_uchar_t *v, sky_u32_t v_len);

sky_json_t *sky_json_add_object(sky_json_t *json);

sky_json_t *sky_json_add_array(sky_json_t *json);

sky_json_t *sky_json_add_boolean(sky_json_t *json, sky_bool_t value);

sky_json_t *sky_json_add_null(sky_json_t *json);

sky_json_t *sky_json_add_integer(sky_json_t *json, sky_i64_t value);

sky_json_t *sky_json_add_float(sky_json_t *json, sky_f64_t value);

sky_json_t *sky_json_add_string(sky_json_t *json, sky_str_t *value);

sky_json_t *sky_json_add_str_len(sky_json_t *json, sky_uchar_t *v, sky_u32_t v_len);

static sky_inline sky_json_t *
sky_json_object_create(sky_pool_t *pool) {
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t) + sizeof(sky_json_object_t));

    json->type = json_object;
    json->pool = pool;
    json->parent = null;

    json->object = (sky_json_object_t *) (json + 1);
    json->object->prev = json->object->next = json->object;

    return json;
}

static sky_inline sky_json_t *
sky_json_array_create(sky_pool_t *pool) {
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t) + sizeof(sky_json_array_t));

    json->type = json_array;
    json->pool = pool;
    json->parent = null;

    json->array = (sky_json_array_t *) (json + 1);
    json->array->prev = json->array->next = json->array;

    return json;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
