//
// Created by weijing on 2020/1/8.
//

#ifndef SKY_JSON_BUILDER_H
#define SKY_JSON_BUILDER_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "json.h"

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



/* Same as json_object_push, but doesn't call strlen() for you.
 */
sky_json_t *json_object_push_length(sky_json_t *object,
                                    unsigned int name_length, const sky_uchar_t *name,
                                    sky_json_t *);

/* Same as json_object_push_length, but doesn't copy the name buffer before
 * storing it in the value.  Use this micro-optimisation at your own risk.
 */
sky_json_t *json_object_push_nocopy(sky_json_t *object,
                                    unsigned int name_length, sky_uchar_t *name,
                                    sky_json_t *);

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

typedef struct json_serialize_opts {
    int mode;
    int opts;
    int indent_size;

} json_serialize_opts;


/* Returns a length in characters that is at least large enough to hold the
 * value in its serialized form, including a null terminator.
 */
size_t json_measure(sky_json_t *);

size_t json_measure_ex(sky_json_t *, json_serialize_opts);


/* Serializes a JSON value into the buffer given (which must already be
 * allocated with a length of at least json_measure(value, opts))
 */
void json_serialize(sky_uchar_t *buf, sky_json_t *);

void json_serialize_ex(sky_uchar_t *buf, sky_json_t *, json_serialize_opts);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_JSON_BUILDER_H
