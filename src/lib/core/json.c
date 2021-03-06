//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "number.h"
#include "memory.h"
#include "string_buf.h"


#if defined(__AVX2__)

#include <immintrin.h>

#elif defined(__SSE4_2__)

#include <nmmintrin.h>

#elif defined(__SSE3__)

#include <tmmintrin.h>

#endif

#define NEXT_OBJECT_START   0x100
#define NEXT_ARRAY_START    0x80
#define NEXT_OBJECT_END     0x40
#define NEXT_ARRAY_END      0x20
#define NEXT_KEY            0x10
#define NEXT_KEY_VALUE      0x8
#define NEXT_OBJECT_VALUE   0x4
#define NEXT_ARRAY_VALUE    0x2
#define NEXT_NODE           0x1
#define NEXT_NONE           0

static void parse_whitespace(sky_uchar_t **ptr);

static sky_bool_t parse_string(sky_str_t *str, sky_uchar_t **ptr, sky_uchar_t *end);

static sky_bool_t parse_number(sky_json_t *json, sky_uchar_t **ptr);

static sky_bool_t backslash_parse(sky_uchar_t **ptr, sky_uchar_t **post);

static sky_json_t *parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end);

static sky_json_object_t *json_object_get(sky_json_t *json);

static sky_json_t *json_array_get(sky_json_t *json);

static void json_object_init(sky_json_t *json, sky_pool_t *pool);

static void json_array_init(sky_json_t *json, sky_pool_t *pool);

static void json_coding_str(sky_str_buf_t *buf, const sky_uchar_t *v, sky_usize_t v_len);


sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    sky_uchar_t *p = json->data;
    return parse_loop(pool, p, p + json->len);
}

sky_str_t *sky_json_tostring(sky_json_t *json) {
    static const sky_str_t BOOLEAN_TABLE[] = {
            sky_string("false"),
            sky_string("true")
    };

    sky_json_object_t *obj;
    sky_json_t *current, *tmp;

    if (json->type == json_object) {
        if (json->object == json->object->prev) {
            sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
            sky_str_set(str, "{}");
            return str;
        }
    } else if (json->type == json_array) {
        if (json->array == json->array->prev) {
            sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
            sky_str_set(str, "[]");
            return str;
        }
    } else {
        return null;
    }
    sky_str_buf_t buf;

    sky_str_buf_init(&buf, json->pool, 8);

    current = json;

    for (;;) {
        switch (current->type) {
            case json_object:
                if (current->object == current->object->prev) {
                    sky_str_buf_append_two_uchar(&buf, '{', '}');
                    break;
                }
                obj = current->object->next;
                current->current = obj;
                current = &obj->value;

                sky_str_buf_append_two_uchar(&buf, '{', '"');

                json_coding_str(&buf, obj->key.data, obj->key.len);

                sky_str_buf_append_two_uchar(&buf, '"', ':');
                continue;
            case json_array:
                if (current->array == current->array->prev) {
                    sky_str_buf_append_two_uchar(&buf, '[', ']');
                    break;
                }
                current->current = current->array->next;
                current = current->current;
                sky_str_buf_append_uchar(&buf, '[');
                continue;
            case json_integer:
                sky_str_buf_append_int64(&buf, current->integer);
                break;
            case json_float:
                break;
            case json_string:
                sky_str_buf_append_uchar(&buf, '"');
                json_coding_str(&buf, current->string.data, current->string.len);
                sky_str_buf_append_uchar(&buf, '"');
                break;
            case json_boolean: {
                const sky_bool_t index = current->boolean != false;
                sky_str_buf_append_str(&buf, &BOOLEAN_TABLE[index]);
            }
                break;
            case json_null:
                sky_str_buf_append_str_len(&buf, sky_str_line("null"));
                break;
            default:
                sky_str_buf_destroy(&buf);
                return null;
        }
        tmp = current->parent;
        if (tmp->type == json_object) {
            tmp->current = ((sky_json_object_t *) tmp->current)->next;

            if (tmp->current != tmp->object) {
                obj = obj->next;
                current = &obj->value;

                sky_str_buf_append_two_uchar(&buf, ',', '"');
                json_coding_str(&buf, obj->key.data, obj->key.len);
                sky_str_buf_append_two_uchar(&buf, '"', ':');
                continue;
            }
            sky_str_buf_append_uchar(&buf, '}');
        } else {
            tmp->current = ((sky_json_array_t *) tmp->current)->next;
            if (tmp->current != tmp->array) {
                sky_str_buf_append_uchar(&buf, ',');

                current = &((sky_json_array_t *) current)->next->value;
                continue;
            }
            sky_str_buf_append_uchar(&buf, ']');
        }
        for (;;) {
            if (tmp == json) {
                return sky_str_buf_to_str(&buf);
            }

            tmp = tmp->parent;

            if (tmp->type == json_object) {
                tmp->current = ((sky_json_object_t *) tmp->current)->next;
                if (tmp->current == tmp->object) {
                    sky_str_buf_append_uchar(&buf, '}');
                    continue;
                }

                obj = (sky_json_object_t *) tmp->current;
                current = &obj->value;

                sky_str_buf_append_two_uchar(&buf, ',', '"');
                json_coding_str(&buf, obj->key.data, obj->key.len);
                sky_str_buf_append_two_uchar(&buf, '"', ':');
            } else {
                tmp->current = ((sky_json_array_t *) tmp->current)->next;
                if (tmp->current == tmp->array) {
                    sky_str_buf_append_uchar(&buf, ']');
                    continue;
                }
                sky_str_buf_append_uchar(&buf, ',');
                current = tmp->current;
            }
            break;
        }
    }
}


sky_json_t*
sky_json_find(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *object;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    object = json->object->next;
    while (object != json->object) {
        if (key_len == object->key.len && memcmp(key, object->key.data, key_len) == 0) {
            return &object->value;
        }
        object = object->next;
    }

    return null;
}

sky_json_t*
sky_json_put_object(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    json_object_init(&obj->value, json->pool);

    return &obj->value;
}

sky_json_t*
sky_json_put_array(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    json_array_init(&obj->value, json->pool);

    return &obj->value;
}

sky_json_t*
sky_json_put_boolean(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_bool_t value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_boolean;
    obj->value.boolean = value;

    return &obj->value;
}

sky_json_t*
sky_json_put_null(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_null;

    return &obj->value;
}

sky_json_t*
sky_json_put_integer(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_i64_t value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_integer;
    obj->value.integer = value;

    return &obj->value;
}

sky_json_t*
sky_json_put_double(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_f64_t value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_float;
    obj->value.dbl = value;

    return &obj->value;
}

sky_json_t*
sky_json_put_string(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_str_t *value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    if (!value) {
        obj->value.type = json_null;
    } else {
        obj->value.type = json_string;
        obj->value.string = *value;
    }

    return &obj->value;
}

sky_json_t*
sky_json_put_str_len(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_uchar_t *v, sky_u32_t v_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_string;
    obj->value.string.len = v_len;
    obj->value.string.data = v;

    return &obj->value;
}


sky_json_t*
sky_json_add_object(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    json_object_init(child, json->pool);

    return child;
}

sky_json_t*
sky_json_add_array(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    json_array_init(child, json->pool);

    return child;
}

sky_json_t*
sky_json_add_boolean(sky_json_t *json, sky_bool_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_boolean;
    child->boolean = value;

    return child;
}

sky_json_t*
sky_json_add_null(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_null;

    return child;
}

sky_json_t*
sky_json_add_integer(sky_json_t *json, sky_i64_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_integer;
    child->integer = value;

    return child;
}

sky_json_t*
sky_json_add_float(sky_json_t *json, sky_f64_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_float;
    child->dbl = value;

    return child;
}

sky_json_t*
sky_json_add_string(sky_json_t *json, sky_str_t *value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);

    if (!value) {
        child->type = json_null;
    } else {
        child->type = json_string;
        child->string = *value;
    }

    return child;
}

sky_json_t*
sky_json_add_str_len(sky_json_t *json, sky_uchar_t *v, sky_u32_t v_len) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_string;
    child->string.len = v_len;
    child->string.data = v;

    return child;
}


static sky_json_t*
parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end) {
    sky_u16_t next;

    sky_json_t *tmp, *current, *root;
    sky_json_object_t *object;


    parse_whitespace(&data);
    if (*data == '{') {
        root = current = sky_json_object_create(pool);
        next = NEXT_OBJECT_END | NEXT_KEY;
    } else if (*data == '[') {
        root = current = sky_json_array_create(pool);
        next = NEXT_ARRAY_END | NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
    } else {
        return null;
    }
    ++data;

    for (;;) {
        parse_whitespace(&data);
        switch (*data) {
            case '{':
                if (sky_unlikely(!(next & NEXT_OBJECT_START))) {
                    return null;
                }
                ++data;

                tmp = current->type == json_object ? &object->value : json_array_get(current);
                json_object_init(tmp, pool);
                current = tmp;
                next = NEXT_OBJECT_END | NEXT_KEY;

                break;
            case '[':
                if (sky_unlikely(!(next & NEXT_ARRAY_START))) {
                    return null;
                }
                ++data;

                tmp = current->type == json_object ? &object->value : json_array_get(current);
                json_array_init(tmp, pool);
                current = tmp;
                next = NEXT_ARRAY_END | NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;

                break;
            case '}':
                if (sky_unlikely(!(next & NEXT_OBJECT_END))) {
                    return null;
                }

                ++data;
                current = current->parent;

                next = current != null ? (current->type == json_object ? (NEXT_NODE | NEXT_OBJECT_END)
                                                                       : (NEXT_NODE | NEXT_ARRAY_END))
                                       : NEXT_NONE;
                break;
            case ']':
                if (sky_unlikely(!(next & NEXT_ARRAY_END))) {
                    return null;
                }

                ++data;
                current = current->parent;
                next = current != null ? (current->type == json_object ? (NEXT_NODE | NEXT_OBJECT_END)
                                                                       : (NEXT_NODE | NEXT_ARRAY_END))
                                       : NEXT_NONE;
                break;
            case ':':
                if (sky_unlikely(!(next & NEXT_KEY_VALUE))) {
                    return null;
                }

                ++data;
                next = NEXT_OBJECT_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
                break;
            case ',':
                if (sky_unlikely(!(next & NEXT_NODE))) {
                    return null;
                }
                ++data;

                next = current->type == json_object ? NEXT_KEY :
                       (NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START);

                break;
            case '"':
                if (sky_unlikely(!(next & (NEXT_KEY | NEXT_ARRAY_VALUE | NEXT_OBJECT_VALUE)))) {
                    return null;
                }

                ++data;

                if ((next & NEXT_KEY) != 0) {
                    object = json_object_get(current);

                    if (sky_unlikely(!parse_string(&object->key, &data, end))) {
                        return null;
                    }
                    next = NEXT_KEY_VALUE;
                } else if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    tmp->type = json_string;
                    if (sky_unlikely(!parse_string(&tmp->string, &data, end))) {
                        return null;
                    }
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    tmp->type = json_string;

                    if (sky_unlikely(!parse_string(&tmp->string, &data, end))) {
                        return null;
                    }
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                break;
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '-':
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE)))) {
                    return null;
                }
                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }

                if (sky_unlikely(!parse_number(tmp, &data))) {
                    return false;
                }

                break;
            case 't':
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE))
                                 || (end - data) < 4
                                 || !sky_str4_cmp(data, 't', 'r', 'u', 'e'))) {
                    return null;
                }
                data += 4;

                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_boolean;
                tmp->boolean = true;

                break;
            case 'f':
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE)))) {
                    return null;
                }
                ++data;

                if (sky_unlikely((end - data) < 4 || !sky_str4_cmp(data, 'a', 'l', 's', 'e'))) {
                    return null;
                }
                data += 4;

                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_boolean;
                tmp->boolean = false;
                break;
            case 'n':
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE))
                                 || (end - data) < 4
                                 || !sky_str4_cmp(data, 'n', 'u', 'l', 'l'))) {
                    return null;
                }
                data += 4;

                if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_null;
                break;
            case '\0':
                if (sky_unlikely(next != NEXT_NONE)) {
                    return null;
                }
                return root;
            default:
                return null;
        }
    }
}


static sky_inline void
parse_whitespace(sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    if (*p > ' ') {
        return;
    }
    if (*p == ' ' && *(p + 1) > ' ') {
        ++*ptr;
        return;
    }
    if (*p == '\r') {
        ++p;
    }

#if defined(__SSSE3__)
    const __m128i nrt_lut = _mm_set_epi8(-1, -1, 0, -1,
                                         -1, 0, 0, -1,
                                         -1, -1, -1, -1,
                                         -1, -1, -1, -1);

    for (;; p += 16) {

        const __m128i data = _mm_loadu_si128((const __m128i *) p);
        const __m128i dong = _mm_min_epu8(data, _mm_set1_epi8(0x0F));
        const __m128i not_an_nrt_mask = _mm_shuffle_epi8(nrt_lut, dong);
        const __m128i space_mask = _mm_cmpeq_epi8(data, _mm_set1_epi8(' '));
        const __m128i non_whitespace_mask = _mm_xor_si128(not_an_nrt_mask, space_mask);
        const sky_i32_t move_mask = _mm_movemask_epi8(non_whitespace_mask);
        if (__builtin_expect(move_mask, 1)) {
            *ptr = p + __builtin_ctz((sky_u32_t) move_mask);
            return;
        }
    }
#elif defined(__SSE4_2__)

    const __m128i w = _mm_setr_epi8('\n', '\r', '\t', ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    for (;; p += 16) {
        const __m128i s = _mm_loadu_si128((const __m128i *) p);
        const sky_i32_t r = _mm_cmpistri(w, s, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY
                                                 | _SIDD_LEAST_SIGNIFICANT | _SIDD_NEGATIVE_POLARITY);
        if (r != 16)    // some of characters is non-whitespace
        {
            *ptr = p + r;
            return;
        }
    }
#else
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    *ptr = p;
#endif
}


static sky_bool_t
parse_string(sky_str_t *str, sky_uchar_t **ptr, sky_uchar_t *end) {
    sky_uchar_t *p, *post;

    p = *ptr;
#if defined(__AVX2__)
    (void) end;

    sky_bool_t loop;

    for (;; p += 32) { // 转义符和引号在两块区域造成无法识别的问题
        const __m256i data = _mm256_loadu_si256((const __m256i *) p);
        const __m256i invalid_char_mask = _mm256_cmpeq_epi8(data, _mm256_min_epu8(data, _mm256_set1_epi8(0x1A)));
        const __m256i quote_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('"'));
        const __m256i backslash_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('\\'));

        const sky_u32_t quote_move_mask = (sky_u32_t) _mm256_movemask_epi8(quote_mask);
        const sky_u32_t backslash_move_mask = (sky_u32_t) _mm256_movemask_epi8(backslash_mask);

        if (sky_unlikely(_mm256_testz_si256(invalid_char_mask, invalid_char_mask) != 0)) {
            if (backslash_move_mask == 0) {
                if (quote_move_mask == 0) {
                    continue;
                }
                p += __builtin_ctz(quote_move_mask);
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_usize_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置
            if (quote_move_mask == 0) {
                p += backslash_len; // 未找到引号，数据还很长
                loop = true;
                break;
            }
            const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
            if (quote_len < backslash_len) {

                p += quote_len;
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_usize_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            p += backslash_len; // 找到引号，数据很快结束了
            loop = false;
            break;
        }
        if (sky_unlikely(quote_move_mask == 0)) { // 未找到引号
            return false;
        }
        const sky_u32_t invalid_char_move_mask = (sky_u32_t) _mm256_movemask_epi8(invalid_char_mask);
        const sky_i32_t invalid_char_len = __builtin_ctz(invalid_char_move_mask);
        const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
        if (sky_unlikely(invalid_char_len < quote_len)) {
            return false;
        }

        const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置

        if (quote_len < backslash_len) {
            p += quote_len;
            *p = '\0';
            str->data = *ptr;
            str->len = (sky_usize_t) (p - str->data);

            *ptr = p + 1;

            return true;
        }
        p += backslash_len; // 找到引号，数据很快结束了
        loop = false;
        break;
    }
    post = p;

    if (loop) {
        do {
            if (sky_unlikely(!backslash_parse(&p, &post))) {
                return false;
            }

            for (;; p += 32) { // 转义符和引号在两块区域造成无法识别的问题
                const __m256i data = _mm256_loadu_si256((const __m256i *) p);
                const __m256i invalid_char_mask = _mm256_cmpeq_epi8(data,
                                                                    _mm256_min_epu8(data, _mm256_set1_epi8(0x1A)));
                const __m256i quote_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('"'));
                const __m256i backslash_mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8('\\'));

                const sky_u32_t quote_move_mask = (sky_u32_t) _mm256_movemask_epi8(quote_mask);
                const sky_u32_t backslash_move_mask = (sky_u32_t) _mm256_movemask_epi8(backslash_mask);

                if (sky_unlikely(_mm256_testz_si256(invalid_char_mask, invalid_char_mask) != 0)) {
                    if (backslash_move_mask == 0) {
                        if (quote_move_mask == 0) {
                            _mm256_storeu_si256((__m256i *) post, data);
                            post += 32;
                            continue;
                        }
                        const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
                        sky_memmove(post, p, (sky_usize_t) quote_len);
                        post += quote_len;
                        p += quote_len;

                        *post = '\0';
                        str->data = *ptr;
                        str->len = (sky_usize_t) (post - str->data);
                        *ptr = p + 1;
                        return true;
                    }
                    const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置
                    if (quote_move_mask == 0) {
                        sky_memmove(post, p, (sky_usize_t) backslash_len);
                        post += backslash_len;
                        p += backslash_len; // 未找到引号，数据还很长
                        loop = true;
                        break;
                    }
                    const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
                    if (quote_len < backslash_len) {

                        sky_memmove(post, p, (sky_usize_t) quote_len);
                        post += quote_len;
                        p += quote_len;

                        *post = '\0';
                        str->data = *ptr;
                        str->len = (sky_usize_t) (post - str->data);
                        *ptr = p + 1;
                        return true;
                    }
                    sky_memmove(post, p, (sky_usize_t) backslash_len);
                    post += backslash_len;
                    p += backslash_len; // 找到引号，数据很快结束了
                    loop = false;
                    break;
                }
                if (sky_unlikely(quote_move_mask == 0)) { // 未找到引号
                    return false;
                }
                const sky_u32_t invalid_char_move_mask = (sky_u32_t) _mm256_movemask_epi8(invalid_char_mask);
                const sky_i32_t invalid_char_len = __builtin_ctz(invalid_char_move_mask);
                const sky_i32_t quote_len = __builtin_ctz(quote_move_mask); // 引号位置
                if (sky_unlikely(invalid_char_len < quote_len)) {
                    return false;
                }

                const sky_i32_t backslash_len = __builtin_ctz(backslash_move_mask); // 转义符位置

                if (quote_len < backslash_len) {
                    sky_memmove(post, p, (sky_usize_t) quote_len);
                    post += quote_len;
                    p += quote_len;

                    *post = '\0';
                    str->data = *ptr;
                    str->len = (sky_usize_t) (post - str->data);
                    *ptr = p + 1;
                    return true;
                }
                sky_memmove(post, p, (sky_usize_t) backslash_len);
                post += backslash_len;
                p += backslash_len; // 找到引号，数据很快结束了
                loop = false;
                break;
            }
        } while (loop);
    }
#elif defined(__SSE4_2__)
#ifdef _MSC_VER
#define ALIGNED(_n) _declspec(align(_n))
#else
#define ALIGNED(_n) __attribute__((aligned(_n)))
#endif

    static const sky_uchar_t ALIGNED(16) ranges[16] = "\0\037"
                                                      "\"\""
                                                      "\\\\";
#undef ALIGNED
    sky_bool_t loop = false;
    sky_usize_t size = (sky_usize_t) (end - p);
    if (sky_likely(size >= 16)) {
        sky_usize_t left = size & ~15U;

        __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);
        do {
            __m128i b16 = _mm_loadu_si128((const __m128i *) p);
            sky_i32_t r = _mm_cmpestri(
                    ranges16,
                    6,
                    b16,
                    16,
                    _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS
            );
            if (sky_unlikely(r != 16)) {
                p += r;

                if (sky_unlikely(*p < ' ')) {
                    return false;
                }
                if (*p == '"') {
                    *p = '\0';
                    str->data = *ptr;
                    str->len = (sky_usize_t) (p - str->data);

                    *ptr = p + 1;

                    return true;
                }
                loop = true;
                break;
            }
            p += 16;
            left -= 16;
        } while (sky_likely(left != 0));
    }
    if (!loop) {
        for (;;) {
            if (sky_unlikely(*p < ' ')) {
                return false;
            }
            if (*p == '\\') {
                break;
            }
            if (*p == '"') {
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_usize_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            ++p;
        }
        post = p;
    } else {
        post = p;
        do {
           if (sky_unlikely(!backslash_parse(&p, &post))) {
               return false;
           }

            loop = false;
            size = (sky_usize_t) (end - p);
            if (sky_likely(size >= 16)) {

                sky_usize_t left = size & ~15U;

                __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);
                do {
                    __m128i data = _mm_loadu_si128((const __m128i *) p);
                    sky_i32_t r = _mm_cmpestri(
                            ranges16,
                            6,
                            data,
                            16,
                            _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS
                    );
                    if (sky_unlikely(r != 16)) {
                        sky_memmove(post, p, (sky_usize_t) r);
                        p += r;
                        post += r;

                        if (sky_unlikely(*p < ' ')) {
                            return false;
                        }
                        if (*p == '"') {
                            *p = '\0';
                            str->data = *ptr;
                            str->len = (sky_usize_t) (p - str->data);

                            *ptr = p + 1;

                            return true;
                        }
                        loop = true;
                        break;
                    }
                    _mm_storeu_si128((__m128i_u *) post, data);
                    p += 16;
                    post += 16;
                    left -= 16;
                } while (sky_likely(left != 0));
            }
        } while (loop);
    }

#else
    (void) end;
    for (;;) {
        if (sky_unlikely(*p < ' ')) {
            return false;
        }
        if (*p == '\\') {
            break;
        }
        if (*p == '"') {
            *p = '\0';
            str->data = *ptr;
            str->len = (sky_usize_t) (p - str->data);

            *ptr = p + 1;

            return true;
        }
        ++p;
    }
    post = p;
#endif
    if (sky_unlikely(!backslash_parse(&p, &post))) {
        return false;
    }

    for (;;) {
        if (sky_unlikely(*p < ' ')) {
            return false;
        }
        if (*p == '\\') {
            if (sky_unlikely(!backslash_parse(&p, &post))) {
                return false;
            }
            continue;
        }
        if (*p == '"') {
            *post = '\0';
            str->data = *ptr;
            str->len = (sky_usize_t) (post - str->data);
            *ptr = p + 1;

            return true;
        }
        *post++ = *p++;
    }
}

static sky_bool_t
parse_number(sky_json_t *json, sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    ++p;

#if defined(__SSE4_2__)

    const __m128i w = _mm_setr_epi8('.', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0, 0, 0, 0, 0);

    for (;; p += 16) {
        const __m128i s = _mm_loadu_si128((const __m128i *) p);
        const sky_i32_t r = _mm_cmpistri(w, s, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY
                                                 | _SIDD_LEAST_SIGNIFICANT | _SIDD_NEGATIVE_POLARITY);
        if (r != 16)    // some of characters is non-whitespace
        {
            p += r;

            const sky_str_t str = {
                    .len = ((sky_usize_t) (p - *ptr)),
                    .data = *ptr
            };
            json->type = json_integer;
            *ptr = p;

            return sky_str_to_i64(&str, &json->integer);
        }
    }
#else
    for (;;) {
        if (sky_unlikely(*p < '-' || *p > ':')) {
            const sky_str_t str = {
                    .len = ((sky_usize_t) (p - *ptr)),
                    .data = *ptr
            };

            json->type = json_integer;
            *ptr = p;

            return sky_str_to_i64(&str, &json->integer);
        }
        ++p;
    }
#endif
}

static sky_inline sky_bool_t
backslash_parse(sky_uchar_t **ptr, sky_uchar_t **post) {
    switch (*(++(*ptr))) {
        case '"':
        case '\\':
            *((*post)++) = **ptr;
            break;
        case 'b':
            *((*post)++) = '\b';
            break;
        case 'f':
            *((*post)++) = '\f';
            break;
        case 'n':
            *((*post)++) = '\n';
            break;
        case 'r':
            *((*post)++) = '\r';
            break;
        case 't':
            *((*post)++) = '\t';
            break;
        default:
            return false;
    }
    ++*ptr;

    return true;
}


static sky_inline sky_json_object_t*
json_object_get(sky_json_t *json) {
    sky_json_object_t *object = sky_palloc(json->pool, sizeof(sky_json_object_t));

    object->next = json->object;
    object->prev = object->next->prev;
    object->prev->next = object->next->prev = object;

    object->value.parent = json;

    return object;
}


static sky_inline sky_json_t*
json_array_get(sky_json_t *json) {
    sky_json_array_t *array = sky_palloc(json->pool, sizeof(sky_json_array_t));

    array->next = json->array;
    array->prev = array->next->prev;
    array->prev->next = array->next->prev = array;

    array->value.parent = json;

    return &array->value;
}


static sky_inline void
json_object_init(sky_json_t *json, sky_pool_t *pool) {
    sky_json_object_t *object = sky_palloc(pool, sizeof(sky_json_object_t));

    json->type = json_object;
    json->object = object;
    json->pool = pool;

    object->prev = object->next = object;
}


static sky_inline void
json_array_init(sky_json_t *json, sky_pool_t *pool) {
    sky_json_array_t *array = sky_palloc(pool, sizeof(sky_json_array_t));

    json->type = json_array;
    json->array = array;
    json->pool = pool;

    array->prev = array->next = array;
}

static sky_inline void
json_coding_str(sky_str_buf_t *buf, const sky_uchar_t *v, sky_usize_t v_len) {
    sky_str_buf_append_str_len(buf, v, (sky_u32_t) v_len);
}