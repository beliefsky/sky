//
// Created by weijing on 2020/1/8.
//

#include "json_builder.h"
#include "number.h"
#include "memory.h"
#include "json.h"
#include "log.h"

#include <assert.h>
#include <stdio.h>

static const json_serialize_opts default_opts =
        {
                json_serialize_mode_single_line,
                0,
                3  /* indent_size */
        };

typedef struct json_builder_value {
    sky_json_t value;
    size_t length_iterated;
} json_builder_value;

sky_json_t *
sky_json_object_new(sky_pool_t *pool, sky_uint32_t length) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }
    value->type = json_object;
    value->obj = sky_array_create(pool, length, sizeof(sky_json_object_t));

    return value;
}


sky_json_t *
sky_json_array_new(sky_pool_t *pool, sky_uint32_t length) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_array;
    value->arr = sky_array_create(pool, length, sizeof(sky_json_t *));

    return value;
}

sky_json_t *
sky_json_integer_new(sky_pool_t *pool, json_int_t integer) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_integer;
    value->integer = integer;

    return value;
}

sky_json_t *
sky_json_double_new(sky_pool_t *pool, double dbl) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_double;
    value->dbl = dbl;

    return value;
}

sky_json_t *
sky_json_boolean_new(sky_pool_t *pool, sky_bool_t b) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_boolean;
    value->boolean = b;

    return value;
}

sky_json_t *
sky_json_null_new(sky_pool_t *pool) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_null;

    return value;
}


sky_json_t *
sky_json_string_new(sky_pool_t *pool, sky_str_t *value) {
    return sky_json_str_len_new(pool, value->data, value->len);
}


sky_json_t *
sky_json_str_len_new(sky_pool_t *pool, sky_uchar_t *str, sky_size_t str_len) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_string;
    value->string.len = str_len;
    value->string.data = str;

    return value;
}


void
sky_json_object_push(sky_json_t *object, sky_uchar_t *key, sky_size_t key_len, sky_json_t *value) {
    sky_json_object_t *entry;

    assert (object->type == json_object);
    value->parent = object;
    entry = sky_array_push(object->obj);
    entry->key.data = key;
    entry->key.len = key_len;
    entry->value = value;

    return;
}

void
sky_json_object_push2(sky_json_t *object, sky_str_t *key, sky_json_t *value) {
    sky_json_object_push(object, key->data, key->len, value);
}

void
sky_json_array_push(sky_json_t *array, sky_json_t *value) {
    assert(array->type == json_array);
    value->parent = array;
    *(sky_json_t **) sky_array_push(array->arr) = value;
}

/* These flags are set up from the opts before serializing to make the
 * serializer conditions simpler.
 */
const int f_spaces_around_brackets = (1 << 0);
const int f_spaces_after_commas = (1 << 1);
const int f_spaces_after_colons = (1 << 2);
const int f_tabs = (1 << 3);

static int get_serialize_flags(json_serialize_opts opts) {
    int flags = 0;

    if (opts.mode == json_serialize_mode_packed)
        return 0;

    if (opts.mode == json_serialize_mode_multiline) {
        if (opts.opts & json_serialize_opt_use_tabs)
            flags |= f_tabs;
    } else {
        if (!(opts.opts & json_serialize_opt_pack_brackets))
            flags |= f_spaces_around_brackets;

        if (!(opts.opts & json_serialize_opt_no_space_after_comma))
            flags |= f_spaces_after_commas;
    }

    if (!(opts.opts & json_serialize_opt_no_space_after_colon))
        flags |= f_spaces_after_colons;

    return flags;
}


static size_t measure_string(sky_size_t length,
                             const sky_uchar_t *str) {
    unsigned int i;
    size_t measured_length = 0;

    for (i = 0; i < length; ++i) {
        sky_uchar_t c = str[i];

        switch (c) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':

                measured_length += 2;
                break;

            default:

                ++measured_length;
                break;
        }
    }

    return measured_length;
}

#define PRINT_ESCAPED(c) do {  \
   *buf ++ = '\\';             \
   *buf ++ = (c);              \
} while(0)                     \


static sky_size_t
serialize_string(sky_uchar_t *buf, sky_size_t length, sky_uchar_t *str) {
    sky_uchar_t *orig_buf = buf, c;
    sky_uint32_t i;

    for (i = 0; i < length; ++i) {
        c = str[i];
        switch (c) {
            case '"':
                PRINT_ESCAPED ('\"');
                continue;
            case '\\':
                PRINT_ESCAPED ('\\');
                continue;
            case '\b':
                PRINT_ESCAPED ('b');
                continue;
            case '\f':
                PRINT_ESCAPED ('f');
                continue;
            case '\n':
                PRINT_ESCAPED ('n');
                continue;
            case '\r':
                PRINT_ESCAPED ('r');
                continue;
            case '\t':
                PRINT_ESCAPED ('t');
                continue;

            default:

                *buf++ = c;
                break;
        };
    };

    return buf - orig_buf;
}

size_t json_measure(sky_json_t *value) {
    return json_measure_ex(value, default_opts);
}

#define MEASURE_NEWLINE() do {                     \
   ++ newlines;                                    \
   indents += depth;                               \
} while(0)                                         \


size_t json_measure_ex(sky_json_t *value, json_serialize_opts opts) {
    sky_size_t total = 1;  /* null terminator */
    sky_size_t newlines = 0;
    sky_size_t depth = 0;
    sky_size_t indents = 0;
    sky_int32_t flags;
    sky_int32_t bracket_size, comma_size, colon_size;

    flags = get_serialize_flags(opts);

    /* to reduce branching
     */
    bracket_size = flags & f_spaces_around_brackets ? 2 : 1;
    comma_size = flags & f_spaces_after_commas ? 2 : 1;
    colon_size = flags & f_spaces_after_colons ? 2 : 1;

    while (value) {
        json_int_t integer;
        sky_json_object_t *entry;

        switch (value->type) {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->arr->nelts == 0) {
                        total += 2;  /* `[]` */
                        break;
                    }

                    total += bracket_size;  /* `[` */

                    ++depth;
                    MEASURE_NEWLINE(); /* \n after [ */
                }

                if (((json_builder_value *) value)->length_iterated == value->arr->nelts) {
                    --depth;
                    MEASURE_NEWLINE();
                    total += bracket_size;  /* `]` */

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0) {
                    total += comma_size;  /* `, ` */

                    MEASURE_NEWLINE();
                }

                ((json_builder_value *) value)->length_iterated++;
                value = ((sky_json_t **) value->arr->elts)[((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->obj->nelts == 0) {
                        total += 2;  /* `{}` */
                        break;
                    }

                    total += bracket_size;  /* `{` */

                    ++depth;
                    MEASURE_NEWLINE(); /* \n after { */
                }

                if (((json_builder_value *) value)->length_iterated == value->obj->nelts) {
                    --depth;
                    MEASURE_NEWLINE();
                    total += bracket_size;  /* `}` */

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0) {
                    total += comma_size;  /* `, ` */
                    MEASURE_NEWLINE();
                }
                entry = &((sky_json_object_t *) value->obj->elts)[((json_builder_value *) value)->length_iterated++];

                total += 2 + colon_size;  /* `"": ` */
                total += measure_string(entry->key.len, entry->key.data);

                value = entry->value;
                continue;

            case json_string:

                total += 2;  /* `""` */
                total += measure_string(value->string.len, value->string.data);
                break;

            case json_integer:

                integer = value->integer;

                if (integer < 0) {
                    total += 1;  /* `-` */
                    integer = -integer;
                }

                ++total;  /* first digit */

                while (integer >= 10) {
                    ++total;  /* another digit */
                    integer /= 10;
                }

                break;

            case json_double:

                total += snprintf(NULL, 0, "%g", value->dbl);

                /* Because sometimes we need to add ".0" if sprintf does not do it
                 * for us. Downside is that we allocate more bytes than strictly
                 * needed for serialization.
                 */
                total += 2;

                break;

            case json_boolean:

                total += value->boolean ?
                         4 :  /* `true` */
                         5;  /* `false` */

                break;

            case json_null:

                total += 4;  /* `null` */
                break;

            default:
                break;
        }

        value = value->parent;
    }

    if (opts.mode == json_serialize_mode_multiline) {
        total += newlines * (((opts.opts & json_serialize_opt_CRLF) ? 2 : 1) + opts.indent_size);
        total += indents * opts.indent_size;
    }

    return total;
}

void json_serialize(sky_uchar_t *buf, sky_json_t *value) {
    json_serialize_ex(buf, value, default_opts);
}

#define PRINT_NEWLINE() do {                          \
   if (opts.mode == json_serialize_mode_multiline) {  \
      if (opts.opts & json_serialize_opt_CRLF)        \
         *buf ++ = '\r';                              \
      *buf ++ = '\n';                                 \
      for(i = 0; i < indent; ++ i)                    \
         *buf ++ = indent_char;                       \
   }                                                  \
} while(0)

#define PRINT_OPENING_BRACKET(c) do {                 \
   *buf ++ = (c);                                     \
   if (flags & f_spaces_around_brackets)              \
      *buf ++ = ' ';                                  \
} while(0)

#define PRINT_CLOSING_BRACKET(c) do {                 \
   if (flags & f_spaces_around_brackets)              \
      *buf ++ = ' ';                                  \
   *buf ++ = (c);                                     \
} while(0)


void json_serialize_ex(sky_uchar_t *buf, sky_json_t *value, json_serialize_opts opts) {
    sky_json_object_t *entry;
    sky_uchar_t *ptr, *dot, indent_char;
    sky_int32_t indent = 0, i, flags;

    flags = get_serialize_flags(opts);

    indent_char = flags & f_tabs ? '\t' : ' ';

    while (value) {
        switch (value->type) {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->arr->nelts == 0) {
                        *buf++ = '[';
                        *buf++ = ']';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('[');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->arr->nelts) {
                    indent -= opts.indent_size;
                    PRINT_NEWLINE();
                    PRINT_CLOSING_BRACKET (']');

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0) {
                    *buf++ = ',';

                    if (flags & f_spaces_after_commas)
                        *buf++ = ' ';

                    PRINT_NEWLINE();
                }

                ((json_builder_value *) value)->length_iterated++;
                value = ((sky_json_t **) value->arr->elts)[((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->obj->nelts == 0) {
                        *buf++ = '{';
                        *buf++ = '}';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('{');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->obj->nelts) {
                    indent -= opts.indent_size;
                    PRINT_NEWLINE();
                    PRINT_CLOSING_BRACKET ('}');

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0) {
                    *buf++ = ',';

                    if (flags & f_spaces_after_commas)
                        *buf++ = ' ';

                    PRINT_NEWLINE();
                }

                entry = &((sky_json_object_t *) value->obj->elts)[((json_builder_value *) value)->length_iterated++];

                *buf++ = '\"';
                buf += serialize_string(buf, entry->key.len, entry->key.data);
                *buf++ = '\"';
                *buf++ = ':';

                if (flags & f_spaces_after_colons)
                    *buf++ = ' ';

                value = entry->value;
                continue;

            case json_string:
                *buf++ = '\"';
                buf += serialize_string(buf, value->string.len, value->string.data);
                *buf++ = '\"';
                break;

            case json_integer:
                buf += sky_int64_to_str(value->integer, buf);
                break;

            case json_double:

                ptr = buf;

                buf += sprintf(buf, "%g", value->dbl);

                if ((dot = strchr(ptr, ','))) {
                    *dot = '.';
                } else if (!strchr(ptr, '.') && !strchr(ptr, 'e')) {
                    *buf++ = '.';
                    *buf++ = '0';
                }

                break;

            case json_boolean:
                if (value->boolean) {
                    sky_memcpy(buf, "true", 4);
                    buf += 4;
                } else {
                    sky_memcpy(buf, "false", 5);
                    buf += 5;
                }
                break;
            case json_null:
                sky_memcpy(buf, "null", 4);
                buf += 4;
                break;

            default:
                break;
        }

        value = value->parent;
    }

    *buf = 0;
}

