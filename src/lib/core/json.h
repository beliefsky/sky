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


#ifndef json_int_t
#define json_int_t sky_int64_t
#endif

/*** Serializing
***/
#define json_serialize_mode_multiline     0
#define json_serialize_mode_single_line   1
#define json_serialize_mode_packed        2

#define json_serialize_opt_CRLF                    (1 << 1)
#define json_serialize_opt_pack_brackets           (1 << 2)
#define json_serialize_opt_no_space_after_comma    (1 << 3)
#define json_serialize_opt_no_space_after_colon    (1 << 4)
#define json_serialize_opt_use_tabs                (1 << 5)

typedef struct sky_json_s sky_json_t;
typedef struct sky_json_object_s sky_json_object_t;


typedef enum {
    json_object = 0,
    json_array,
    json_integer,
    json_double,
    json_string,
    json_boolean,
    json_null

} json_type;

struct sky_json_object_s {
    sky_str_t key;
    sky_json_t *value;
};

struct sky_json_s {
    sky_json_t *parent;

    json_type type;

    union {
        sky_bool_t boolean;
        json_int_t integer;
        double dbl;

        sky_str_t string;

        struct {
            sky_uint32_t length;
            sky_json_object_t *values;
        } object;

        struct {
            sky_uint32_t length;
            sky_json_t **values;
        } array;

    };

    union {
        sky_json_t *next_alloc;
        void *object_mem;

    } _reserved;
};

typedef struct {
    sky_int32_t mode;
    sky_int32_t opts;
    sky_uint32_t indent_size;

} sky_json_serialize_opts;

sky_json_t *sky_json_object_new(sky_pool_t *pool, sky_uint32_t length);

sky_json_t *sky_json_array_new(sky_pool_t *pool, sky_uint32_t length);

sky_json_t *sky_json_integer_new(sky_pool_t *pool, json_int_t value);

sky_json_t *sky_json_double_new(sky_pool_t *pool, double value);

sky_json_t *sky_json_boolean_new(sky_pool_t *pool, sky_bool_t value);

sky_json_t *sky_json_null_new(sky_pool_t *pool);

sky_json_t *sky_json_string_new(sky_pool_t *pool, sky_str_t *value);

sky_json_t *sky_json_str_len_new(sky_pool_t *pool, sky_uchar_t *str, sky_size_t str_len);

void sky_json_object_push(sky_json_t *object, sky_uchar_t *key, sky_size_t key_len, sky_json_t *value);

void sky_json_object_push2(sky_json_t *object, sky_str_t *key, sky_json_t *value);

void sky_json_array_push(sky_json_t *array, sky_json_t *value);

sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json);

sky_json_t *sky_json_parse_ex(sky_pool_t *pool, sky_uchar_t *json, sky_size_t length, sky_bool_t enable_comments);

/* Returns a length in characters that is at least large enough to hold the
 * value in its serialized form, including a null terminator.
 */
sky_size_t sky_json_measure(sky_json_t *);

sky_size_t sky_json_measure_ex(sky_json_t *, sky_json_serialize_opts);


/* Serializes a JSON value into the buffer given (which must already be
 * allocated with a length of at least json_measure(value, opts))
 */
void sky_json_serialize(sky_uchar_t *buf, sky_json_t *);

void sky_json_serialize_ex(sky_uchar_t *buf, sky_json_t *, sky_json_serialize_opts);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
