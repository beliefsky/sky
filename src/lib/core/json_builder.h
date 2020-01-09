//
// Created by weijing on 2020/1/8.
//

#ifndef SKY_JSON_BUILDER_H
#define SKY_JSON_BUILDER_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "json.h"

/* IMPORTANT NOTE:  If you want to use json-builder functions with values
* allocated by json-parser as part of the parsing process, you must pass
* json_builder_extra as the value_extra setting in json_settings when
* parsing.  Otherwise there will not be room for the extra state and
* json-builder WILL invoke undefined behaviour.
*
* Also note that unlike json-parser, json-builder does not currently support
* custom allocators (for no particular reason other than that it doesn't have
* any settings or global state.)
*/
extern const size_t json_builder_extra;


/*** Arrays
 ***
 * Note that all of these length arguments are just a hint to allow for
 * pre-allocation - passing 0 is fine.
 */
sky_json_t * json_array_new (size_t length);
sky_json_t * json_array_push (sky_json_t * array, sky_json_t *);


/*** Objects
 ***/
sky_json_t * json_object_new (size_t length);

sky_json_t * json_object_push (sky_json_t * object,
                               const sky_uchar_t * name,
                               sky_json_t *);

/* Same as json_object_push, but doesn't call strlen() for you.
 */
sky_json_t * json_object_push_length (sky_json_t * object,
                                      unsigned int name_length, const sky_uchar_t * name,
                                      sky_json_t *);

/* Same as json_object_push_length, but doesn't copy the name buffer before
 * storing it in the value.  Use this micro-optimisation at your own risk.
 */
sky_json_t * json_object_push_nocopy (sky_json_t * object,
                                      unsigned int name_length, sky_uchar_t * name,
                                      sky_json_t *);

/* Merges all entries from objectB into objectA and destroys objectB.
 */
sky_json_t * json_object_merge (sky_json_t * objectA, sky_json_t * objectB);

/* Sort the entries of an object based on the order in a prototype object.
 * Helpful when reading JSON and writing it again to preserve user order.
 */
void json_object_sort (sky_json_t * object, sky_json_t * proto);



/*** Strings
 ***/
sky_json_t * json_string_new (const sky_uchar_t *);
sky_json_t * json_string_new_length (unsigned int length, const sky_uchar_t *);
sky_json_t * json_string_new_nocopy (unsigned int length, sky_uchar_t *);


/*** Everything else
 ***/
sky_json_t * json_integer_new (json_int_t);
sky_json_t * json_double_new (double);
sky_json_t * json_boolean_new (int);
sky_json_t * json_null_new (void);


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

typedef struct json_serialize_opts
{
    int mode;
    int opts;
    int indent_size;

} json_serialize_opts;


/* Returns a length in characters that is at least large enough to hold the
 * value in its serialized form, including a null terminator.
 */
size_t json_measure (sky_json_t *);
size_t json_measure_ex (sky_json_t *, json_serialize_opts);


/* Serializes a JSON value into the buffer given (which must already be
 * allocated with a length of at least json_measure(value, opts))
 */
void json_serialize (sky_uchar_t * buf, sky_json_t *);
void json_serialize_ex (sky_uchar_t * buf, sky_json_t *, json_serialize_opts);


/*** Cleaning up
 ***/
void json_builder_free (sky_json_t *);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_JSON_BUILDER_H
