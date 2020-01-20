//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "log.h"
#include "memory.h"
#include "number.h"


#include <math.h>
#include <assert.h>


#define JSON_INT_MAX INTPTR_MAX


typedef sky_uint32_t json_uchar;

typedef struct {
    sky_json_t value;
    sky_uint32_t additional_length_allocated;
    sky_uint32_t length_iterated;
} json_builder_value;

static sky_size_t measure_string(sky_size_t length, sky_uchar_t *str);

static sky_size_t serialize_string(sky_uchar_t *buf, sky_size_t length, sky_uchar_t *str);

static const sky_json_serialize_opts default_opts = {
        json_serialize_mode_single_line,
        0,
        3  /* indent_size */
};

sky_json_t *
sky_json_object_new(sky_pool_t *pool, sky_uint32_t length) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }
    value->type = json_object;
    value->object.values = sky_pnalloc(pool, sizeof(sky_json_object_t) * length);
    ((json_builder_value *) value)->additional_length_allocated = length;

    return value;
}


sky_json_t *
sky_json_array_new(sky_pool_t *pool, sky_uint32_t length) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(json_builder_value));

    if (sky_unlikely(!value)) {
        return null;
    }
    value->type = json_array;
    value->array.values = sky_pnalloc(pool, sizeof(sky_json_t *) * length);
    ((json_builder_value *) value)->additional_length_allocated = length;

    return value;
}

sky_json_t *
sky_json_integer_new(sky_pool_t *pool, json_int_t integer) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(sky_json_t));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_integer;
    value->integer = integer;

    return value;
}

sky_json_t *
sky_json_double_new(sky_pool_t *pool, double dbl) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(sky_json_t));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_double;
    value->dbl = dbl;

    return value;
}

sky_json_t *
sky_json_boolean_new(sky_pool_t *pool, sky_bool_t b) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(sky_json_t));

    if (sky_unlikely(!value)) {
        return null;
    }

    value->type = json_boolean;
    value->boolean = b;

    return value;
}

sky_json_t *
sky_json_null_new(sky_pool_t *pool) {
    sky_json_t *value = sky_pcalloc(pool, sizeof(sky_json_t));

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
    sky_json_t *value = sky_pcalloc(pool, sizeof(sky_json_t));

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
    if (sky_unlikely(((json_builder_value *) object)->additional_length_allocated <= object->object.length)) {
        return;
    }
    value->parent = object;
    entry = &object->object.values[object->object.length++];
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
    if (sky_unlikely(((json_builder_value *) array)->additional_length_allocated <= array->array.length)) {
        return;
    }
    value->parent = array;
    array->array.values[array->array.length++] = value;
}


static sky_uchar_t hex_value(sky_uchar_t c) {
    switch (c) {
        case '0':
            return 0;
        case '1':
            return 1;
        case '2':
            return 2;
        case '3':
            return 3;
        case '4':
            return 4;
        case '5':
            return 5;
        case '6':
            return 6;
        case '7':
            return 7;
        case '8':
            return 8;
        case '9':
            return 9;
        case 'a':
        case 'A':
            return 0x0A;
        case 'b':
        case 'B':
            return 0x0B;
        case 'c':
        case 'C':
            return 0x0C;
        case 'd':
        case 'D':
            return 0x0D;
        case 'e':
        case 'E':
            return 0x0E;
        case 'f':
        case 'F':
            return 0x0F;
        default:
            return 0xFF;
    }
}

static int would_overflow(json_int_t value, sky_uchar_t b) {
    return ((JSON_INT_MAX
             - (b - '0')) / 10) < value;
}

typedef struct {
    sky_uint32_t uint_max;

    sky_int32_t first_pass;

    sky_uchar_t *ptr;
    sky_uint32_t cur_line, cur_col;

    sky_pool_t *pool;
    sky_bool_t enable_comments;

} json_state;

static sky_bool_t
new_value(json_state *state, sky_json_t **top, sky_json_t **root, sky_json_t **alloc, json_type type) {
    sky_json_t *value;
    sky_size_t values_size;

    if (!state->first_pass) {
        value = *top = *alloc;
        *alloc = (*alloc)->_reserved.next_alloc;

        if (!*root) {
            *root = value;
        }
        switch (value->type) {
            case json_array:
                if (value->array.length == 0) { break; }
                if (sky_unlikely(!(value->array.values = sky_pcalloc(state->pool,
                                                                     value->array.length * sizeof(sky_json_t *))))) {
                    return false;
                }
                value->array.length = 0;
                break;
            case json_object:
                if (value->object.length == 0) { break; }
                values_size = sizeof(*value->object.values) * value->object.length;
                if (sky_unlikely(!(value->object.values = sky_pcalloc(state->pool, values_size +
                                                                                   ((sky_uintptr_t) value->object.values))))) {
                    return false;
                }
                value->_reserved.object_mem = (*(sky_uchar_t **) &value->object.values) + values_size;
                value->object.length = 0;
                break;
            case json_string:
                if (sky_unlikely(!(value->string.data = (sky_uchar_t *) sky_pcalloc
                        (state->pool, (value->string.len + 1) * sizeof(sky_uchar_t))))) {
                    return false;
                }
                value->string.len = 0;
                break;
            default:
                break;
        }

        return true;
    }

    if (sky_unlikely(!(value = sky_palloc(state->pool, sizeof(sky_json_t))))) {
        return false;
    }

    if (!*root) { *root = value; }
    value->type = type;
    value->parent = *top;

    if (*alloc) { (*alloc)->_reserved.next_alloc = value; }
    *alloc = *top = value;

    return true;
}

#define whitespace \
   case '\n': ++ state.cur_line;  state.cur_col = 0; \
   case ' ': case '\t': case '\r'

#define string_add(b)  \
   do { if (!state.first_pass) string [string_length] = b;  ++ string_length; } while (0)

#define line_and_col \
   state.cur_line, state.cur_col

sky_json_t *
sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    return sky_json_parse_ex(pool, json->data, json->len, false);
}


sky_json_t *
sky_json_parse_ex(sky_pool_t *pool, sky_uchar_t *json, sky_size_t length, sky_bool_t enable_comments) {
    enum {
        flag_next = 1 << 0,
        flag_reproc = 1 << 1,
        flag_need_comma = 1 << 2,
        flag_seek_value = 1 << 3,
        flag_escaped = 1 << 4,
        flag_string = 1 << 5,
        flag_need_colon = 1 << 6,
        flag_done = 1 << 7,
        flag_num_negative = 1 << 8,
        flag_num_zero = 1 << 9,
        flag_num_e = 1 << 10,
        flag_num_e_got_sign = 1 << 11,
        flag_num_e_negative = 1 << 12,
        flag_line_comment = 1 << 13,
        flag_block_comment = 1 << 14,
        flag_num_got_decimal = 1 << 15
    };

    sky_uchar_t *end;
    sky_json_t *top, *root = null, *alloc = null;
    json_state state = {0};
    long flags = 0;
    double num_digits = 0, num_e = 0;
    double num_fraction = 0;

    /* Skip UTF-8 BOM
     */
    if (length >= 3 && ((sky_uchar_t) json[0]) == 0xEF
        && ((sky_uchar_t) json[1]) == 0xBB
        && ((sky_uchar_t) json[2]) == 0xBF) {
        json += 3;
        length -= 3;
    }
    end = (json + length);

    state.pool = pool;
    state.enable_comments = enable_comments;
    memset(&state.uint_max, 0xFF, sizeof(state.uint_max));

    state.uint_max -= 8; /* limit of how much can be added before next check */

    for (state.first_pass = 1; state.first_pass >= 0; --state.first_pass) {
        json_uchar uchar;
        sky_uchar_t b, uc_b1, uc_b2, uc_b3, uc_b4;
        sky_uchar_t *string = 0;
        sky_uint32_t string_length = 0;

        top = root = 0;
        flags = flag_seek_value;

        state.cur_line = 1;

        for (state.ptr = json;; ++state.ptr) {
            b = (state.ptr == end ? 0 : *state.ptr);

            if (flags & flag_string) {
                if (!b) {
                    sky_log_error("Unexpected EOF in string (at %d:%d)", line_and_col);
                    goto e_failed;
                }

                if (sky_unlikely(string_length > state.uint_max)) {
                    goto e_overflow;
                }

                if (flags & flag_escaped) {
                    flags &= ~flag_escaped;

                    switch (b) {
                        case 'b':
                            string_add ('\b');
                            break;
                        case 'f':
                            string_add ('\f');
                            break;
                        case 'n':
                            string_add ('\n');
                            break;
                        case 'r':
                            string_add ('\r');
                            break;
                        case 't':
                            string_add ('\t');
                            break;
                        case 'u':

                            if (sky_unlikely(end - state.ptr <= 4 ||
                                             (uc_b1 = hex_value(*++state.ptr)) == 0xFF ||
                                             (uc_b2 = hex_value(*++state.ptr)) == 0xFF ||
                                             (uc_b3 = hex_value(*++state.ptr)) == 0xFF ||
                                             (uc_b4 = hex_value(*++state.ptr)) == 0xFF)) {
                                sky_log_error("Invalid character value `%c` (at %d:%d)", b, line_and_col);
                                goto e_failed;
                            }

                            uc_b1 = (sky_uchar_t) ((uc_b1 << 4) | uc_b2);
                            uc_b2 = (sky_uchar_t) ((uc_b3 << 4) | uc_b4);
                            uchar = (json_uchar) ((uc_b1 << 8) | uc_b2);

                            if ((uchar & 0xF800) == 0xD800) {
                                json_uchar uchar2;

                                if (sky_likely(
                                        end - state.ptr <= 6 || (*++state.ptr) != '\\' || (*++state.ptr) != 'u' ||
                                        (uc_b1 = hex_value(*++state.ptr)) == 0xFF ||
                                        (uc_b2 = hex_value(*++state.ptr)) == 0xFF ||
                                        (uc_b3 = hex_value(*++state.ptr)) == 0xFF ||
                                        (uc_b4 = hex_value(*++state.ptr)) == 0xFF)) {
                                    sky_log_error("Invalid character value `%c` (at %d:%d)", b, line_and_col);
                                    goto e_failed;
                                }

                                uc_b1 = (sky_uchar_t) ((uc_b1 << 4) | uc_b2);
                                uc_b2 = (sky_uchar_t) ((uc_b3 << 4) | uc_b4);
                                uchar2 = (json_uchar) ((uc_b1 << 8) | uc_b2);

                                uchar = 0x010000 | ((uchar & 0x3FF) << 10) | (uchar2 & 0x3FF);
                            }

                            if (uchar <= 0x7F) {
                                string_add ((sky_uchar_t) uchar);
                                break;
                            }

                            if (uchar <= 0x7FF) {
                                if (state.first_pass)
                                    string_length += 2;
                                else {
                                    string[string_length++] = (sky_uchar_t) (0xC0 | (uchar >> 6));
                                    string[string_length++] = 0x80 | (uchar & 0x3F);
                                }

                                break;
                            }

                            if (uchar <= 0xFFFF) {
                                if (state.first_pass)
                                    string_length += 3;
                                else {
                                    string[string_length++] = (sky_uchar_t) (0xE0 | (uchar >> 12));
                                    string[string_length++] = 0x80 | ((uchar >> 6) & 0x3F);
                                    string[string_length++] = 0x80 | (uchar & 0x3F);
                                }

                                break;
                            }

                            if (state.first_pass) {
                                string_length += 4;
                            } else {
                                string[string_length++] = (sky_uchar_t) (0xF0 | (uchar >> 18));
                                string[string_length++] = 0x80 | ((uchar >> 12) & 0x3F);
                                string[string_length++] = 0x80 | ((uchar >> 6) & 0x3F);
                                string[string_length++] = 0x80 | (uchar & 0x3F);
                            }

                            break;

                        default:
                            string_add (b);
                    }

                    continue;
                }

                if (b == '\\') {
                    flags |= flag_escaped;
                    continue;
                }

                if (b == '"') {
                    if (!state.first_pass)
                        string[string_length] = 0;

                    flags &= ~flag_string;
                    string = 0;

                    switch (top->type) {
                        case json_string:

                            top->string.len = string_length;
                            flags |= flag_next;

                            break;

                        case json_object:

                            if (state.first_pass)
                                (*(sky_uchar_t **) &top->object.values) += string_length + 1;
                            else {
                                top->object.values[top->object.length].key.data
                                        = (sky_uchar_t *) top->_reserved.object_mem;

                                top->object.values[top->object.length].key.len
                                        = string_length;

                                (*(sky_uchar_t **) &top->_reserved.object_mem) += string_length + 1;
                            }

                            flags |= flag_seek_value | flag_need_colon;
                            continue;

                        default:
                            break;
                    }
                } else {
                    string_add (b);
                    continue;
                }
            }

            if (state.enable_comments) {
                if (flags & (flag_line_comment | flag_block_comment)) {
                    if (flags & flag_line_comment) {
                        if (b == '\r' || b == '\n' || !b) {
                            flags &= ~flag_line_comment;
                            --state.ptr;  /* so null can be reproc'd */
                        }

                        continue;
                    }

                    if (flags & flag_block_comment) {
                        if (!b) {
                            sky_log_error("%d:%d: Unexpected EOF in block comment", line_and_col);
                            goto e_failed;
                        }

                        if (b == '*' && state.ptr < (end - 1) && state.ptr[1] == '/') {
                            flags &= ~flag_block_comment;
                            ++state.ptr;  /* skip closing sequence */
                        }

                        continue;
                    }
                } else if (b == '/') {
                    if (!(flags & (flag_seek_value | flag_done)) && top->type != json_object) {
                        sky_log_error("%d:%d: Comment not allowed here", line_and_col);
                        goto e_failed;
                    }

                    if (++state.ptr == end) {
                        sky_log_error("%d:%d: EOF unexpected", line_and_col);
                        goto e_failed;
                    }

                    switch (b = *state.ptr) {
                        case '/':
                            flags |= flag_line_comment;
                            continue;

                        case '*':
                            flags |= flag_block_comment;
                            continue;

                        default:
                            sky_log_error("%d:%d: Unexpected `%c` in comment opening sequence", line_and_col, b);
                            goto e_failed;
                    }
                }
            }

            if (flags & flag_done) {
                if (!b)
                    break;

                switch (b) {
                    whitespace:
                        continue;

                    default:

                        sky_log_error("%d:%d: Trailing garbage: `%c`",
                                      state.cur_line, state.cur_col, b);

                        goto e_failed;
                }
            }

            if (flags & flag_seek_value) {
                switch (b) {
                    whitespace:
                        continue;
                    case ']':

                        if (sky_likely(top && top->type == json_array)) {
                            flags = (flags & ~(flag_need_comma | flag_seek_value)) | flag_next;
                        } else {
                            sky_log_error("%d:%d: Unexpected ]", line_and_col);
                            goto e_failed;
                        }

                        break;

                    default:

                        if (sky_likely(flags & flag_need_comma)) {
                            if (b == ',') {
                                flags &= ~flag_need_comma;
                                continue;
                            } else {
                                sky_log_error("%d:%d: Expected , before %c",
                                              state.cur_line, state.cur_col, b);

                                goto e_failed;
                            }
                        }

                        if (sky_likely(flags & flag_need_colon)) {
                            if (b == ':') {
                                flags &= ~flag_need_colon;
                                continue;
                            } else {
                                sky_log_error("%d:%d: Expected : before %c",
                                              state.cur_line, state.cur_col, b);

                                goto e_failed;
                            }
                        }

                        flags &= ~flag_seek_value;

                        switch (b) {
                            case '{':

                                if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_object))) {
                                    goto e_alloc_failure;
                                }
                                continue;

                            case '[':

                                if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_array))) {
                                    goto e_alloc_failure;
                                }
                                flags |= flag_seek_value;
                                continue;

                            case '"':

                                if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_string))) {
                                    goto e_alloc_failure;
                                }

                                flags |= flag_string;

                                string = top->string.data;
                                string_length = 0;

                                continue;

                            case 't':
                                if (sky_unlikely((end - state.ptr) < 3
                                                 || !sky_str4_cmp(state.ptr, 't', 'r', 'u', 'e'))) {
                                    goto e_unknown_value;
                                }
                                state.ptr += 3;
                                if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_boolean))) {
                                    goto e_alloc_failure;
                                }
                                top->boolean = true;

                                flags |= flag_next;
                                break;

                            case 'f':
                                if (sky_unlikely((end - state.ptr) < 4
                                                 || !sky_str4_cmp(++state.ptr, 'a', 'l', 's', 'e'))) {
                                    goto e_unknown_value;
                                }
                                state.ptr += 3;
                                if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_boolean))) {
                                    goto e_alloc_failure;
                                }
                                flags |= flag_next;
                                break;

                            case 'n':
                                if (sky_unlikely((end - state.ptr) < 3
                                                 || !sky_str4_cmp(state.ptr, 'n', 'u', 'l', 'l'))) {
                                    goto e_unknown_value;
                                }
                                state.ptr += 3;
                                if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_null))) {
                                    goto e_alloc_failure;
                                }
                                flags |= flag_next;
                                break;

                            default:

                                if (sky_likely((b >= '0' && b <= '9') || b == '-')) {
                                    if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_integer))) {
                                        goto e_alloc_failure;
                                    }

                                    if (!state.first_pass) {
                                        while ((b >= '0' && b <= '9') || b == '+' || b == '-'
                                               || b == 'e' || b == 'E' || b == '.') {
                                            if ((++state.ptr) == end) {
                                                break;
                                            }

                                            b = *state.ptr;
                                        }

                                        flags |= flag_next | flag_reproc;
                                        break;
                                    }

                                    flags &= ~(flag_num_negative | flag_num_e |
                                               flag_num_e_got_sign | flag_num_e_negative |
                                               flag_num_zero);

                                    num_digits = 0;
                                    num_fraction = 0;
                                    num_e = 0;

                                    if (b != '-') {
                                        flags |= flag_reproc;
                                        break;
                                    }

                                    flags |= flag_num_negative;
                                    continue;
                                } else {
                                    sky_log_error("%d:%d: Unexpected %c when seeking value", line_and_col, b);
                                    goto e_failed;
                                }
                        }
                }
            } else {
                switch (top->type) {
                    case json_object:

                        switch (b) {
                            whitespace:
                                continue;
                            case '"':
                                if (sky_unlikely(flags & flag_need_comma)) {
                                    sky_log_error("%d:%d: Expected , before \"", line_and_col);
                                    goto e_failed;
                                }
                                flags |= flag_string;

                                string = (sky_uchar_t *) top->_reserved.object_mem;
                                string_length = 0;
                                break;
                            case '}':
                                flags = (flags & ~flag_need_comma) | flag_next;
                                break;
                            case ',':
                                if (flags & flag_need_comma) {
                                    flags &= ~flag_need_comma;
                                    break;
                                }
                            default:
                                sky_log_error("%d:%d: Unexpected `%c` in object", line_and_col, b);
                                goto e_failed;
                        }

                        break;

                    case json_integer:
                    case json_double:

                        if (b >= '0' && b <= '9') {
                            ++num_digits;

                            if (top->type == json_integer || flags & flag_num_e) {
                                if (!(flags & flag_num_e)) {
                                    if (sky_unlikely(flags & flag_num_zero)) {
                                        sky_log_error("%d:%d: Unexpected `0` before `%c`", line_and_col, b);
                                        goto e_failed;
                                    }
                                    if (num_digits == 1 && b == '0') {
                                        flags |= flag_num_zero;
                                    }
                                } else {
                                    flags |= flag_num_e_got_sign;
                                    num_e = (num_e * 10) + (b - '0');
                                    continue;
                                }

                                if (would_overflow(top->integer, b)) {
                                    --num_digits;
                                    --state.ptr;
                                    top->type = json_double;
                                    top->dbl = (double) top->integer;
                                    continue;
                                }

                                top->integer = (top->integer * 10) + (b - '0');
                                continue;
                            }

                            if (flags & flag_num_got_decimal) {
                                num_fraction = (num_fraction * 10) + (b - '0');
                            } else {
                                top->dbl = (top->dbl * 10) + (b - '0');
                            }
                            continue;
                        }

                        if (b == '+' || b == '-') {
                            if ((flags & flag_num_e) && !(flags & flag_num_e_got_sign)) {
                                flags |= flag_num_e_got_sign;

                                if (b == '-') {
                                    flags |= flag_num_e_negative;
                                }
                                continue;
                            }
                        } else if (b == '.' && top->type == json_integer) {
                            if (sky_unlikely(!num_digits)) {
                                sky_log_error("%d:%d: Expected digit before `.`", line_and_col);
                                goto e_failed;
                            }

                            top->type = json_double;
                            top->dbl = (double) top->integer;

                            flags |= flag_num_got_decimal;
                            num_digits = 0;
                            continue;
                        }

                        if (!(flags & flag_num_e)) {
                            if (top->type == json_double) {
                                if (sky_unlikely(!num_digits)) {
                                    sky_log_error("%d:%d: Expected digit after `.`", line_and_col);
                                    goto e_failed;
                                }

                                top->dbl += num_fraction / pow(10.0, num_digits);
                            }

                            if (b == 'e' || b == 'E') {
                                flags |= flag_num_e;

                                if (top->type == json_integer) {
                                    top->type = json_double;
                                    top->dbl = (double) top->integer;
                                }

                                num_digits = 0;
                                flags &= ~flag_num_zero;

                                continue;
                            }
                        } else {
                            if (sky_unlikely(!num_digits)) {
                                sky_log_error("%d:%d: Expected digit after `e`", line_and_col);
                                goto e_failed;
                            }

                            top->dbl *= pow(10.0, (flags & flag_num_e_negative ? -num_e : num_e));
                        }

                        if (flags & flag_num_negative) {
                            if (top->type == json_integer) {
                                top->integer = -top->integer;
                            } else {
                                top->dbl = -top->dbl;
                            }
                        }

                        flags |= flag_next | flag_reproc;
                        break;

                    default:
                        break;
                }
            }

            if (flags & flag_reproc) {
                flags &= ~flag_reproc;
                --state.ptr;
            }

            if (flags & flag_next) {
                flags = (flags & ~flag_next) | flag_need_comma;

                if (!top->parent) {
                    /* root value done */
                    flags |= flag_done;
                    continue;
                }

                if (top->parent->type == json_array) {
                    flags |= flag_seek_value;
                }

                if (!state.first_pass) {
                    sky_json_t *parent = top->parent;

                    switch (parent->type) {
                        case json_object:
                            parent->object.values[parent->object.length].value = top;
                            break;

                        case json_array:
                            parent->array.values[parent->array.length] = top;
                            break;
                        default:
                            break;
                    }
                }

                if ((++top->parent->array.length) > state.uint_max) {
                    goto e_overflow;
                }
                top = top->parent;

                continue;
            }
        }

        alloc = root;
    }

    return root;

    e_unknown_value:

    sky_log_error("%d:%d: Unknown value", line_and_col);
    goto e_failed;

    e_alloc_failure:

    sky_log_error("Memory allocation failure");
    goto e_failed;

    e_overflow:

    sky_log_error("%d:%d: Too long (caught overflow)", line_and_col);
    goto e_failed;

    e_failed:

    if (state.first_pass) {
        alloc = root;
    }

    while (alloc) {
        top = alloc->_reserved.next_alloc;
        alloc = top;
    }

    return 0;
}



#define f_spaces_around_brackets    (1 << 0)
#define f_spaces_after_commas       (1 << 1)
#define f_spaces_after_colons       (1 << 2)
#define f_tabs                      (1 << 3)

static sky_int32_t
get_serialize_flags(sky_json_serialize_opts opts) {
    int flags = 0;

    if (opts.mode == json_serialize_mode_packed) {
        return 0;
    }
    if (opts.mode == json_serialize_mode_multiline) {
        if (opts.opts & json_serialize_opt_use_tabs) {
            flags |= f_tabs;
        }
    } else {
        if (!(opts.opts & json_serialize_opt_pack_brackets)) {
            flags |= f_spaces_around_brackets;
        }
        if (!(opts.opts & json_serialize_opt_no_space_after_comma)) {
            flags |= f_spaces_after_commas;
        }
    }

    if (!(opts.opts & json_serialize_opt_no_space_after_colon)) {
        flags |= f_spaces_after_colons;
    }

    return flags;
}

sky_size_t
sky_json_measure(sky_json_t *value) {
    return sky_json_measure_ex(value, default_opts);
}

sky_size_t
sky_json_measure_ex(sky_json_t *value, sky_json_serialize_opts opts) {
#define MEASURE_NEWLINE() do {  \
   ++ newlines;                 \
   indents += depth;            \
} while(0)


    sky_size_t total = 1;  /* null terminator */
    sky_size_t newlines = 0;
    sky_size_t depth = 0;
    sky_size_t indents = 0;
    sky_int32_t flags;
    sky_uint32_t bracket_size, comma_size, colon_size;

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
                    if (value->array.length == 0) {
                        total += 2;  /* `[]` */
                        break;
                    }

                    total += bracket_size;  /* `[` */

                    ++depth;
                    MEASURE_NEWLINE(); /* \n after [ */
                }

                if (((json_builder_value *) value)->length_iterated == value->array.length) {
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
                value = value->array.values[((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->object.length == 0) {
                        total += 2;  /* `{}` */
                        break;
                    }

                    total += bracket_size;  /* `{` */

                    ++depth;
                    MEASURE_NEWLINE(); /* \n after { */
                }

                if (((json_builder_value *) value)->length_iterated == value->object.length) {
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
                entry = &value->object.values[((json_builder_value *) value)->length_iterated++];

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
                total += snprintf(null, 0, "%g", value->dbl);
                /* Because sometimes we need to add ".0" if sprintf does not do it
                 * for us. Downside is that we allocate more bytes than strictly
                 * needed for serialization.
                 */
                total += 2;
                break;
            case json_boolean:
                total += value->boolean ? 4 :  /* `true` */ 5;  /* `false` */
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


void
sky_json_serialize(sky_uchar_t *buf, sky_json_t *value) {
    sky_json_serialize_ex(buf, value, default_opts);
}

void sky_json_serialize_ex(sky_uchar_t *buf, sky_json_t *value, sky_json_serialize_opts opts) {

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

#define PRINT_NEWLINE() do {                          \
   if (opts.mode == json_serialize_mode_multiline) {  \
      if (opts.opts & json_serialize_opt_CRLF)        \
         *buf ++ = '\r';                              \
      *buf ++ = '\n';                                 \
      for(i = 0; i < indent; ++ i)                    \
         *buf ++ = indent_char;                       \
   }                                                  \
} while(0)

    sky_json_object_t *entry;
    sky_uchar_t *ptr, *dot, indent_char;
    sky_int32_t indent = 0, i, flags;

    flags = get_serialize_flags(opts);

    indent_char = flags & f_tabs ? '\t' : ' ';

    while (value) {
        switch (value->type) {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->array.length == 0) {
                        *buf++ = '[';
                        *buf++ = ']';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('[');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->array.length) {
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
                value = value->array.values[((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0) {
                    if (value->object.length == 0) {
                        *buf++ = '{';
                        *buf++ = '}';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('{');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->object.length) {
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
                entry = &value->object.values[((json_builder_value *) value)->length_iterated++];

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

                buf += sprintf((sky_char_t *) buf, "%g", value->dbl);

                if ((dot = (sky_uchar_t *) strchr((sky_char_t *) ptr, ','))) {
                    *dot = '.';
                } else if (!strchr((sky_char_t *) ptr, '.') && !strchr((sky_char_t *) ptr, 'e')) {
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

static sky_size_t
measure_string(sky_size_t length, sky_uchar_t *str) {
    sky_size_t i, measured_length = length;

    for (i = 0; i < length; ++i) {
        switch (str[i]) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                ++measured_length;
                break;
            default:
                break;
        }
    }

    return measured_length;
}

static sky_size_t
serialize_string(sky_uchar_t *buf, sky_size_t length, sky_uchar_t *str) {
#define PRINT_ESCAPED(c) do {   \
   *buf ++ = '\\';              \
   *buf ++ = (c);               \
   ++orig_len;                  \
} while(0)

    sky_uchar_t c;
    sky_uint32_t i;
    sky_size_t orig_len = length;

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
        }
    }
    return orig_len;
}
