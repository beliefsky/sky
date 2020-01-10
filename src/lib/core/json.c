//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "log.h"

#include <ctype.h>
#include <math.h>


#define JSON_INT_MAX INTPTR_MAX


typedef sky_uint32_t json_uchar;

static sky_uchar_t hex_value(sky_uchar_t c) {
    if (isdigit(c))
        return c - '0';

    switch (c) {
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
    unsigned long used_memory;

    unsigned int uint_max;
    unsigned long ulong_max;

    int first_pass;

    const sky_uchar_t *ptr;
    unsigned int cur_line, cur_col;

    sky_pool_t *pool;
    sky_bool_t enable_comments;

} json_state;


static sky_inline void *
json_alloc(json_state *state, sky_size_t size, sky_bool_t zero) {
    if ((state->ulong_max - state->used_memory) < size) {
        return null;
    }

    return zero ? sky_pcalloc(state->pool, size) : sky_palloc(state->pool, size);
}

static int
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
                if (value->u.array.length == 0) { break; }
                if (sky_unlikely(!(value->u.array.values = json_alloc
                        (state, value->u.array.length * sizeof(sky_json_t *), 0)))) {
                    return 0;
                }
                value->u.array.length = 0;
                break;
            case json_object:
                if (value->u.object.length == 0) { break; }
                values_size = sizeof(*value->u.object.values) * value->u.object.length;
                if (sky_unlikely(!(value->u.object.values = json_alloc
                        (state, values_size + ((sky_uintptr_t) value->u.object.values), 0)))) {
                    return 0;
                }
                value->_reserved.object_mem = (*(sky_uchar_t **) &value->u.object.values) + values_size;
                value->u.object.length = 0;
                break;
            case json_string:
                if (sky_unlikely(!(value->u.string.ptr = (sky_uchar_t *) json_alloc
                        (state, (value->u.string.length + 1) * sizeof(sky_uchar_t), 0)))) {
                    return 0;
                }
                value->u.string.length = 0;
                break;
            default:
                break;
        }

        return 1;
    }

    if (sky_unlikely(!(value = json_alloc(state, sizeof(sky_json_t), 1)))) {
        return 0;
    }

    if (!*root) { *root = value; }
    value->type = type;
    value->parent = *top;

    if (*alloc) { (*alloc)->_reserved.next_alloc = value; }
    *alloc = *top = value;

    return 1;
}

#define whitespace \
   case '\n': ++ state.cur_line;  state.cur_col = 0; \
   case ' ': case '\t': case '\r'

#define string_add(b)  \
   do { if (!state.first_pass) string [string_length] = b;  ++ string_length; } while (0);

#define line_and_col \
   state.cur_line, state.cur_col


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
    sky_json_t *top, *root, *alloc = null;
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
    memset(&state.ulong_max, 0xFF, sizeof(state.ulong_max));

    state.uint_max -= 8; /* limit of how much can be added before next check */
    state.ulong_max -= 8;

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

                            if (sizeof(sky_uchar_t) >= sizeof(json_uchar) || (uchar <= 0x7F)) {
                                string_add ((sky_uchar_t) uchar);
                                break;
                            }

                            if (uchar <= 0x7FF) {
                                if (state.first_pass)
                                    string_length += 2;
                                else {
                                    string[string_length++] = 0xC0 | (uchar >> 6);
                                    string[string_length++] = 0x80 | (uchar & 0x3F);
                                }

                                break;
                            }

                            if (uchar <= 0xFFFF) {
                                if (state.first_pass)
                                    string_length += 3;
                                else {
                                    string[string_length++] = 0xE0 | (uchar >> 12);
                                    string[string_length++] = 0x80 | ((uchar >> 6) & 0x3F);
                                    string[string_length++] = 0x80 | (uchar & 0x3F);
                                }

                                break;
                            }

                            if (state.first_pass) {
                                string_length += 4;
                            } else {
                                string[string_length++] = 0xF0 | (uchar >> 18);
                                string[string_length++] = 0x80 | ((uchar >> 12) & 0x3F);
                                string[string_length++] = 0x80 | ((uchar >> 6) & 0x3F);
                                string[string_length++] = 0x80 | (uchar & 0x3F);
                            }

                            break;

                        default:
                            string_add (b);
                    };

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

                            top->u.string.length = string_length;
                            flags |= flag_next;

                            break;

                        case json_object:

                            if (state.first_pass)
                                (*(sky_uchar_t **) &top->u.object.values) += string_length + 1;
                            else {
                                top->u.object.values[top->u.object.length].key.data
                                        = (sky_uchar_t *) top->_reserved.object_mem;

                                top->u.object.values[top->u.object.length].key.len
                                        = string_length;

                                (*(sky_uchar_t **) &top->_reserved.object_mem) += string_length + 1;
                            }

                            flags |= flag_seek_value | flag_need_colon;
                            continue;

                        default:
                            break;
                    };
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
                    };
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
                };
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

                                string = top->u.string.ptr;
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
                                top->u.boolean = true;

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

                                if (sky_likely(isdigit(b) || b == '-')) {
                                    if (sky_unlikely(!new_value(&state, &top, &root, &alloc, json_integer))) {
                                        goto e_alloc_failure;
                                    }

                                    if (!state.first_pass) {
                                        while (isdigit (b) || b == '+' || b == '-'
                                               || b == 'e' || b == 'E' || b == '.') {
                                            if ((++state.ptr) == end) {
                                                b = 0;
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
                };
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
                        };

                        break;

                    case json_integer:
                    case json_double:

                        if (isdigit (b)) {
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

                                if (would_overflow(top->u.integer, b)) {
                                    --num_digits;
                                    --state.ptr;
                                    top->type = json_double;
                                    top->u.dbl = (double) top->u.integer;
                                    continue;
                                }

                                top->u.integer = (top->u.integer * 10) + (b - '0');
                                continue;
                            }

                            if (flags & flag_num_got_decimal) {
                                num_fraction = (num_fraction * 10) + (b - '0');
                            } else {
                                top->u.dbl = (top->u.dbl * 10) + (b - '0');
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
                            top->u.dbl = (double) top->u.integer;

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

                                top->u.dbl += num_fraction / pow(10.0, num_digits);
                            }

                            if (b == 'e' || b == 'E') {
                                flags |= flag_num_e;

                                if (top->type == json_integer) {
                                    top->type = json_double;
                                    top->u.dbl = (double) top->u.integer;
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

                            top->u.dbl *= pow(10.0, (flags & flag_num_e_negative ? -num_e : num_e));
                        }

                        if (flags & flag_num_negative) {
                            if (top->type == json_integer) {
                                top->u.integer = -top->u.integer;
                            } else {
                                top->u.dbl = -top->u.dbl;
                            }
                        }

                        flags |= flag_next | flag_reproc;
                        break;

                    default:
                        break;
                };
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
                            parent->u.object.values[parent->u.object.length].value = top;
                            break;

                        case json_array:
                            parent->u.array.values[parent->u.array.length] = top;
                            break;
                        default:
                            break;
                    }
                }

                if ((++top->parent->u.array.length) > state.uint_max) {
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

sky_json_t *
sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    return sky_json_parse_ex(pool, json->data, json->len, 0);
}

