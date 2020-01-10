//
// Created by weijing on 2020/1/8.
//

#include "json_builder.h"
#include "number.h"
#include "memory.h"

#include <string.h>
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

    int is_builder_value;

    size_t additional_length_allocated;
    size_t length_iterated;

} json_builder_value;

static int builderize(sky_json_t *value) {
    if (((json_builder_value *) value)->is_builder_value)
        return 1;

    if (value->type == json_object) {
        unsigned int i;

        /* Values straight out of the parser have the names of object entries
         * allocated in the same allocation as the values array itself.  This is
         * not desirable when manipulating values because the names would be easy
         * to clobber.
         */
        for (i = 0; i < value->u.object.length; ++i) {
            sky_uchar_t *name_copy;
            json_object_s *entry = &value->u.object.values[i];

            if (!(name_copy = (sky_uchar_t *) malloc((entry->key.len + 1) * sizeof(sky_uchar_t))))
                return 0;

            memcpy(name_copy, entry->key.data, entry->key.len + 1);
            entry->key.data = name_copy;
        }
    }

    ((json_builder_value *) value)->is_builder_value = 1;

    return 1;
}

const size_t json_builder_extra = sizeof(json_builder_value) - sizeof(sky_json_t);

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

sky_json_t *json_array_new(size_t length) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_array;

    if (!(value->u.array.values = (sky_json_t **) malloc(length * sizeof(sky_json_t *)))) {
        free(value);
        return NULL;
    }

    ((json_builder_value *) value)->additional_length_allocated = length;

    return value;
}

sky_json_t *json_array_push(sky_json_t *array, sky_json_t *value) {
    assert (array->type == json_array);

    if (!builderize(array) || !builderize(value))
        return NULL;

    if (((json_builder_value *) array)->additional_length_allocated > 0) {
        --((json_builder_value *) array)->additional_length_allocated;
    } else {
        sky_json_t **values_new = (sky_json_t **) realloc
                (array->u.array.values, sizeof(sky_json_t *) * (array->u.array.length + 1));

        if (!values_new)
            return NULL;

        array->u.array.values = values_new;
    }

    array->u.array.values[array->u.array.length] = value;
    ++array->u.array.length;

    value->parent = array;

    return value;
}

sky_json_t *json_object_new(size_t length) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_object;

    if (!(value->u.object.values = (json_object_s *) calloc
            (length, sizeof(*value->u.object.values)))) {
        free(value);
        return NULL;
    }

    ((json_builder_value *) value)->additional_length_allocated = length;

    return value;
}

sky_json_t *json_object_push(sky_json_t *object,
                             const sky_uchar_t *name,
                             sky_json_t *value) {
    return json_object_push_length(object, strlen(name), name, value);
}

sky_json_t *json_object_push_length(sky_json_t *object,
                                    unsigned int name_length, const sky_uchar_t *name,
                                    sky_json_t *value) {
    sky_uchar_t *name_copy;

    assert (object->type == json_object);

    if (!(name_copy = (sky_uchar_t *) malloc((name_length + 1) * sizeof(sky_uchar_t))))
        return NULL;

    memcpy(name_copy, name, name_length * sizeof(sky_uchar_t));
    name_copy[name_length] = 0;

    if (!json_object_push_nocopy(object, name_length, name_copy, value)) {
        free(name_copy);
        return NULL;
    }

    return value;
}

sky_json_t *json_object_push_nocopy(sky_json_t *object,
                                    unsigned int name_length, sky_uchar_t *name,
                                    sky_json_t *value) {
    json_object_s *entry;

    assert (object->type == json_object);

    if (!builderize(object) || !builderize(value))
        return NULL;

    if (((json_builder_value *) object)->additional_length_allocated > 0) {
        --((json_builder_value *) object)->additional_length_allocated;
    } else {
        json_object_s *values_new = (json_object_s *)
                realloc(object->u.object.values, sizeof(*object->u.object.values)
                                                 * (object->u.object.length + 1));

        if (!values_new)
            return NULL;

        object->u.object.values = values_new;
    }

    entry = object->u.object.values + object->u.object.length;

    entry->key.len = name_length;
    entry->key.data = name;
    entry->value = value;

    ++object->u.object.length;

    value->parent = object;

    return value;
}

sky_json_t *json_string_new(const sky_uchar_t *buf) {
    return json_string_new_length(strlen(buf), buf);
}

sky_json_t *json_string_new_length(unsigned int length, const sky_uchar_t *buf) {
    sky_json_t *value;
    sky_uchar_t *copy = (sky_uchar_t *) malloc((length + 1) * sizeof(sky_uchar_t));

    if (!copy)
        return NULL;

    memcpy(copy, buf, length * sizeof(sky_uchar_t));
    copy[length] = 0;

    if (!(value = json_string_new_nocopy(length, copy))) {
        free(copy);
        return NULL;
    }

    return value;
}

sky_json_t *json_string_new_nocopy(unsigned int length, sky_uchar_t *buf) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_string;
    value->u.string.len = length;
    value->u.string.data = buf;

    return value;
}

sky_json_t *json_integer_new(json_int_t integer) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_integer;
    value->u.integer = integer;

    return value;
}

sky_json_t *json_double_new(double dbl) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_double;
    value->u.dbl = dbl;

    return value;
}

sky_json_t *json_boolean_new(int b) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_boolean;
    value->u.boolean = b;

    return value;
}

sky_json_t *json_null_new(void) {
    sky_json_t *value = (sky_json_t *) calloc(1, sizeof(json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_null;

    return value;
}

void json_object_sort(sky_json_t *object, sky_json_t *proto) {
    unsigned int i, out_index = 0;

    if (!builderize(object))
        return;  /* TODO error */

    assert (object->type == json_object);
    assert (proto->type == json_object);

    for (i = 0; i < proto->u.object.length; ++i) {
        unsigned int j;
        json_object_s proto_entry = proto->u.object.values[i];

        for (j = 0; j < object->u.object.length; ++j) {
            json_object_s entry = object->u.object.values[j];

            if (entry.key.len != proto_entry.key.len)
                continue;

            if (memcmp(entry.key.data, proto_entry.
                    key.data, entry.key.len) != 0)
                continue;

            object->u.object.values[j] = object->u.object.values[out_index];
            object->u.object.values[out_index] = entry;

            ++out_index;
        }
    }
}

sky_json_t *json_object_merge(sky_json_t *objectA, sky_json_t *objectB) {
    unsigned int i;

    assert (objectA->type == json_object);
    assert (objectB->type == json_object);
    assert (objectA != objectB);

    if (!builderize(objectA) || !builderize(objectB))
        return NULL;

    if (objectB->u.object.length <=
        ((json_builder_value *) objectA)->additional_length_allocated) {
        ((json_builder_value *) objectA)->additional_length_allocated
                -= objectB->u.object.length;
    } else {
        json_object_s *values_new;

        unsigned int alloc =
                objectA->u.object.length
                + ((json_builder_value *) objectA)->additional_length_allocated
                + objectB->u.object.length;

        if (!(values_new = (json_object_s *)
                realloc(objectA->u.object.values, sizeof(json_object_s) * alloc))) {
            return NULL;
        }

        objectA->u.object.values = values_new;
    }

    for (i = 0; i < objectB->u.object.length; ++i) {
        json_object_s *entry = &objectA->u.object.values[objectA->u.object.length + i];

        *entry = objectB->u.object.values[i];
        entry->value->parent = objectA;
    }

    objectA->u.object.length += objectB->u.object.length;

    free(objectB->u.object.values);
    free(objectB);

    return objectA;
}

static size_t measure_string(unsigned int length,
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
        };
    };

    return measured_length;
}

#define PRINT_ESCAPED(c) do {  \
   *buf ++ = '\\';             \
   *buf ++ = (c);              \
} while(0);                    \


static size_t serialize_string(sky_uchar_t *buf,
                               unsigned int length,
                               const sky_uchar_t *str) {
    sky_uchar_t *orig_buf = buf;
    unsigned int i;

    for (i = 0; i < length; ++i) {
        sky_uchar_t c = str[i];

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
} while(0);                                        \


size_t json_measure_ex(sky_json_t *value, json_serialize_opts opts) {
    size_t total = 1;  /* null terminator */
    size_t newlines = 0;
    size_t depth = 0;
    size_t indents = 0;
    int flags;
    int bracket_size, comma_size, colon_size;

    flags = get_serialize_flags(opts);

    /* to reduce branching
     */
    bracket_size = flags & f_spaces_around_brackets ? 2 : 1;
    comma_size = flags & f_spaces_after_commas ? 2 : 1;
    colon_size = flags & f_spaces_after_colons ? 2 : 1;

    while (value) {
        json_int_t integer;
        json_object_s *entry;

        switch (value->type) {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->u.array.length == 0) {
                        total += 2;  /* `[]` */
                        break;
                    }

                    total += bracket_size;  /* `[` */

                    ++depth;
                    MEASURE_NEWLINE(); /* \n after [ */
                }

                if (((json_builder_value *) value)->length_iterated == value->u.array.length) {
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
                value = value->u.array.values[((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->u.object.length == 0) {
                        total += 2;  /* `{}` */
                        break;
                    }

                    total += bracket_size;  /* `{` */

                    ++depth;
                    MEASURE_NEWLINE(); /* \n after { */
                }

                if (((json_builder_value *) value)->length_iterated == value->u.object.length) {
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

                entry = value->u.object.values + (((json_builder_value *) value)->length_iterated++);

                total += 2 + colon_size;  /* `"": ` */
                total += measure_string(entry->key.len, entry->key.data);

                value = entry->value;
                continue;

            case json_string:

                total += 2;  /* `""` */
                total += measure_string(value->u.string.len, value->u.string.data);
                break;

            case json_integer:

                integer = value->u.integer;

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

                total += snprintf(NULL, 0, "%g", value->u.dbl);

                /* Because sometimes we need to add ".0" if sprintf does not do it
                 * for us. Downside is that we allocate more bytes than strictly
                 * needed for serialization.
                 */
                total += 2;

                break;

            case json_boolean:

                total += value->u.boolean ?
                         4 :  /* `true` */
                         5;  /* `false` */

                break;

            case json_null:

                total += 4;  /* `null` */
                break;

            default:
                break;
        };

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
} while(0);                                           \

#define PRINT_OPENING_BRACKET(c) do {                 \
   *buf ++ = (c);                                     \
   if (flags & f_spaces_around_brackets)              \
      *buf ++ = ' ';                                  \
} while(0);                                           \

#define PRINT_CLOSING_BRACKET(c) do {                 \
   if (flags & f_spaces_around_brackets)              \
      *buf ++ = ' ';                                  \
   *buf ++ = (c);                                     \
} while(0);                                           \


void json_serialize_ex(sky_uchar_t *buf, sky_json_t *value, json_serialize_opts opts) {
    json_int_t integer, orig_integer;
    json_object_s *entry;
    sky_uchar_t *ptr, *dot;
    int indent = 0;
    char indent_char;
    int i;
    int flags;

    flags = get_serialize_flags(opts);

    indent_char = flags & f_tabs ? '\t' : ' ';

    while (value) {
        switch (value->type) {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->u.array.length == 0) {
                        *buf++ = '[';
                        *buf++ = ']';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('[');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->u.array.length) {
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
                value = value->u.array.values[((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->u.object.length == 0) {
                        *buf++ = '{';
                        *buf++ = '}';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('{');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->u.object.length) {
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

                entry = value->u.object.values + (((json_builder_value *) value)->length_iterated++);

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
                buf += serialize_string(buf, value->u.string.len, value->u.string.data);
                *buf++ = '\"';
                break;

            case json_integer:
                buf += sky_int64_to_str(value->u.integer, buf);
                break;

            case json_double:

                ptr = buf;

                buf += sprintf(buf, "%g", value->u.dbl);

                if ((dot = strchr(ptr, ','))) {
                    *dot = '.';
                } else if (!strchr(ptr, '.') && !strchr(ptr, 'e')) {
                    *buf++ = '.';
                    *buf++ = '0';
                }

                break;

            case json_boolean:
                if (value->u.boolean) {
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
        };

        value = value->parent;
    }

    *buf = 0;
}

void json_builder_free(sky_json_t *value) {
    sky_json_t *cur_value;

    if (!value)
        return;

    value->parent = 0;

    while (value) {
        switch (value->type) {
            case json_array:

                if (!value->u.array.length) {
                    free(value->u.array.values);
                    break;
                }

                value = value->u.array.values[--value->u.array.length];
                continue;

            case json_object:

                if (!value->u.object.length) {
                    free(value->u.object.values);
                    break;
                }

                --value->u.object.length;

                if (((json_builder_value *) value)->is_builder_value) {
                    /* Names are allocated separately for builder values.  In parser
                     * values, they are part of the same allocation as the values array
                     * itself.
                     */
                    free(value->u.object.values[value->u.object.length].key.data);
                }

                value = value->u.object.values[value->u.object.length].value;
                continue;

            case json_string:

                free(value->u.string.data);
                break;

            default:
                break;
        };

        cur_value = value;
        value = value->parent;
        free(cur_value);
    }
}

