//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "types.h"


#ifndef json_int_t
#define json_int_t sky_int64_t
#endif

#include <stdlib.h>

typedef struct _json_value json_value;
typedef struct _json_object_entry json_object_entry;

typedef struct {
    unsigned long max_memory;
    int settings;

    /* Custom allocator support (leave null to use malloc/free)
     */

    void *(*mem_alloc)(size_t, int zero, void *user_data);

    void (*mem_free)(void *, void *user_data);

    void *user_data;  /* will be passed to mem_alloc and mem_free */

    size_t value_extra;  /* how much extra space to allocate for values? */

} json_settings;

#define json_enable_comments  0x01

typedef enum {
    json_none,
    json_object,
    json_array,
    json_integer,
    json_double,
    json_string,
    json_boolean,
    json_null

} json_type;

struct _json_object_entry {
    sky_uchar_t *name;
    unsigned int name_length;

    json_value *value;

};

struct _json_value {
    json_value *parent;

    json_type type;

    union {
        sky_bool_t boolean;
        json_int_t integer;
        double dbl;

        struct {
            unsigned int length;
            sky_uchar_t *ptr; /* null terminated */

        } string;

        struct {
            unsigned int length;

            json_object_entry *values;

        } object;

        struct {
            unsigned int length;
            json_value **values;
        } array;

    } u;

    union {
        json_value *next_alloc;
        void *object_mem;

    } _reserved;
};

json_value *json_parse(const sky_uchar_t *json,
                       size_t length);

#define json_error_max 128

json_value *json_parse_ex(json_settings *settings,
                          const sky_uchar_t *json,
                          size_t length,
                          char *error);

void json_value_free(json_value *);


/* Not usually necessary, unless you used a custom mem_alloc and now want to
 * use a custom mem_free.
 */
void json_value_free_ex(json_settings *settings,
                        json_value *);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
