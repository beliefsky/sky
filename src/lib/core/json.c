//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "number.h"
#include "memory.h"
#include "log.h"

#if defined(__SSE4_2__)

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

static void parse_whitespace(sky_uchar_t **ptr);

static sky_bool_t parse_string(sky_str_t *str, sky_uchar_t **ptr);

static sky_bool_t parse_number(sky_json_t *json, sky_uchar_t **ptr);

static sky_json_t *parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end);

static sky_json_object_t *json_object_get(sky_json_t *json);

static sky_json_t *json_array_get(sky_json_t *json);

static void json_object_init(sky_json_t *json);

static void json_array_init(sky_json_t *json);


sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    sky_uchar_t *p = json->data;
    return parse_loop(pool, p, p + json->len);
}

sky_str_t *sky_json_tostring(sky_json_t *json) {
    static const sky_char_t *BOOLEAN_TABLE[] = {"false", "true"};

    sky_uchar_t *start, *p;
    sky_json_object_t *obj;
    sky_json_t *current, *tmp;

    if (json->type == json_object) {
        if (json->object.length == 0) {
            sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
            sky_str_set(str, "{}");
            return str;
        }
    } else if (json->type == json_array) {
        if (json->array.length == 0) {
            sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
            sky_str_set(str, "[]");
            return str;
        }
    } else {
        return null;
    }

    p = start = sky_pnalloc(json->pool, 2048);
    current = json;

    for (;;) {
        switch (current->type) {
            case json_object:
                *p++ = '{';
                current->index = 0;
                if (current->object.length != 0) {
                    obj = current->object.values;
                    current = &obj->value;

                    *p++ = '"';
                    sky_memcpy(p, obj->key.data, obj->key.len);
                    p += obj->key.len;
                    *p++ = '"';
                    *p++ = ':';
                    continue;
                }
                *p++ = '}';
                break;
            case json_array:
                *p++ = '[';
                current->index = 0;
                if (current->array.length != 0) {
                    current = current->array.values;
                    continue;
                }
                *p++ = '}';
                break;
            case json_integer:
                p += sky_int64_to_str(current->integer, p);
                break;
            case json_double:
                break;
            case json_string:
                *p++ = '"';
                sky_memcpy(p, current->string.data, current->string.len);
                p += current->string.len;
                *p++ = '"';
                break;
            case json_boolean:
                sky_memcpy(p, BOOLEAN_TABLE[current->boolean != false], 4 + (current->boolean == false));
                p += 4 + (current->boolean == false);
                break;
            case json_null:
                sky_memcpy(p, "null", 4);
                p += 4;
                break;
            default:
                return null;
        }

        tmp = current->parent;
        if (tmp->type == json_object) {
            if (++tmp->index != tmp->object.length) {
                *p++ = ',';

                ++obj;
                current = &obj->value;

                *p++ = '"';
                sky_memcpy(p, obj->key.data, obj->key.len);
                p += obj->key.len;
                *p++ = '"';
                *p++ = ':';
                continue;
            }
            *p++ = '}';
        } else {
            if (++tmp->index != tmp->array.length) {
                *p++ = ',';

                ++current;
                continue;
            }
            *p++ = ']';
        }

        for (;;) {
            if (tmp == json) {
                *p = '\0';
                sky_str_t *str = sky_palloc(json->pool, sizeof(sky_str_t));
                str->data = start;
                str->len = (sky_size_t) (p - start);

                return str;
            }
            tmp = tmp->parent;

            if (tmp->type == json_object) {
                if (++tmp->index == tmp->object.length) {
                    *p++ = '}';
                    continue;
                }
                *p++ = ',';

                obj = tmp->object.values + tmp->index;
                current = &obj->value;

                *p++ = '"';
                sky_memcpy(p, obj->key.data, obj->key.len);
                p += obj->key.len;
                *p++ = '"';
                *p++ = ':';
            } else {
                if (++tmp->index == tmp->array.length) {
                    *p++ = ']';
                    continue;
                }
                current = tmp->array.values + tmp->index;
            }
            break;
        }
    }
}


sky_json_t *
sky_json_find(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len) {
    sky_uint32_t i;
    sky_json_object_t *object;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    i = json->object.length;
    object = json->object.values;
    while (i--) {
        if (key_len == object->key.len && memcmp(key, object->key.data, key_len) == 0) {
            return &object->value;
        }
        ++object;
    }

    return null;
}

sky_json_t *
sky_json_put_object(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    json_object_init(&obj->value);

    return &obj->value;
}

sky_json_t *
sky_json_put_array(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    json_array_init(json);

    return &obj->value;
}

sky_json_t *
sky_json_put_boolean(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, sky_bool_t value) {
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

sky_json_t *
sky_json_put_null(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len) {
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

sky_json_t *
sky_json_put_integer(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, sky_int64_t value) {
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

sky_json_t *
sky_json_put_double(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, double value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_double;
    obj->value.dbl = value;

    return &obj->value;
}

sky_json_t *
sky_json_put_string(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, sky_str_t *value) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->type != json_object)) {
        return null;
    }
    obj = json_object_get(json);
    obj->key.len = key_len;
    obj->key.data = key;

    obj->value.type = json_string;
    obj->value.string = *value;

    return &obj->value;
}


sky_json_t *
sky_json_add_object(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    json_object_init(child);

    return child;
}

sky_json_t *
sky_json_add_array(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    json_array_init(child);

    return child;
}

sky_json_t *
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

sky_json_t *
sky_json_add_null(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_null;

    return child;
}

sky_json_t *
sky_json_add_integer(sky_json_t *json, sky_int64_t value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_integer;
    child->integer = value;

    return child;
}

sky_json_t *
sky_json_add_double(sky_json_t *json, double value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_double;
    child->dbl = value;

    return child;
}

sky_json_t *
sky_json_add_string(sky_json_t *json, sky_str_t *value) {
    sky_json_t *child;

    if (sky_unlikely(json->type != json_array)) {
        return null;
    }
    child = json_array_get(json);
    child->type = json_string;
    child->string = *value;

    return child;
}


static sky_json_t *
parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end) {
    sky_uint16_t next;

    sky_json_t *tmp, *current, *root;
    sky_json_object_t *object;


    parse_whitespace(&data);
    if (*data == '{') {
        root = current = sky_palloc(pool, sizeof(sky_json_t));
        sky_json_object_init(current, pool);
        next = NEXT_OBJECT_END | NEXT_KEY;
    } else if (*data == '[') {
        root = current = sky_palloc(pool, sizeof(sky_json_t));
        sky_json_object_init(current, pool);
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
                json_object_init(tmp);
                current = tmp;
                next = NEXT_OBJECT_END | NEXT_KEY;

                break;
            case '[':
                if (sky_unlikely(!(next & NEXT_ARRAY_START))) {
                    return null;
                }
                ++data;

                tmp = current->type == json_object ? &object->value : json_array_get(current);
                json_array_init(tmp);
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
                                       : 0;
                break;
            case ']':
                if (sky_unlikely(!(next & NEXT_ARRAY_END))) {
                    return null;
                }

                ++data;
                current = current->parent;
                next = current != null ? (current->type == json_object ? (NEXT_NODE | NEXT_OBJECT_END)
                                                                       : (NEXT_NODE | NEXT_ARRAY_END))
                                       : 0;
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

                    if (sky_unlikely(!parse_string(&object->key, &data))) {
                        return null;
                    }
                    next = NEXT_KEY_VALUE;
                } else if ((next & NEXT_OBJECT_VALUE) != 0) {
                    tmp = &object->value;
                    tmp->type = json_string;
                    if (sky_unlikely(!parse_string(&tmp->string, &data))) {
                        return null;
                    }
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    tmp->type = json_string;

                    if (sky_unlikely(!parse_string(&tmp->string, &data))) {
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
                tmp->parent = current;

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

    for (;;) {

        const __m128i data = _mm_loadu_si128((const __m128i *) p);
        const __m128i dong = _mm_min_epu8(data, _mm_set1_epi8(0x0F));
        const __m128i not_an_nrt_mask = _mm_shuffle_epi8(nrt_lut, dong);
        const __m128i space_mask = _mm_cmpeq_epi8(data, _mm_set1_epi8(' '));
        const __m128i non_whitespace_mask = _mm_xor_si128(not_an_nrt_mask, space_mask);
        const sky_int32_t move_mask = _mm_movemask_epi8(non_whitespace_mask);
        if (__builtin_expect(move_mask, 1)) {
            *ptr = p + __builtin_ctz((sky_uint32_t) move_mask);
            return;
        } else {
            p += 16;
        }
    }
#elif defined(__SSE4_2__)

    const __m128i w = _mm_setr_epi8('\n', '\r', '\t', ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    for (;; p += 16) {
        const __m128i s = _mm_loadu_si128((const __m128i *) p);
        const sky_int32_t r = _mm_cmpistri(w, s, _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY
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
parse_string(sky_str_t *str, sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    for (;;) {
        if (sky_unlikely(*p < ' ')) {
            return false;
        }

        if (*p == '"') {
            if (sky_likely(*(p - 1) != '\\')) {
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_size_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            *(p - 1) = *p;
            sky_uchar_t *post = p++;

            for (;;) {
                if (sky_unlikely(*p < ' ')) {
                    return false;
                }

                if (*p == '"') {
                    if (sky_likely(*(p - 1) != '\\')) {
                        *post = '\0';
                        str->data = *ptr;
                        str->len = (sky_size_t) (post - str->data);
                        *ptr = p + 1;

                        return true;
                    }
                    *(post - 1) = *p++;
                    continue;
                }

                *post++ = *p++;
            }
        }
        ++p;
    }
}

static sky_bool_t
parse_number(sky_json_t *json, sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    ++p;

    for (;;) {
        if (sky_unlikely(*p < '-' || *p > ':' || *p == '/')) {
            const sky_str_t str = {
                    .len = ((sky_size_t) (p - *ptr)),
                    .data = *ptr
            };

            if (sky_likely(sky_str_to_int64(&str, &json->integer))) {
                json->type = json_integer;
                *ptr = p;
                return true;
            }
            return false;
        }
        ++p;
    }
}


static sky_inline sky_json_object_t *
json_object_get(sky_json_t *json) {
    sky_json_object_t *obj;

    if (sky_unlikely(json->object.length == json->object.alloc)) {
        const sky_size_t size = sizeof(sky_json_object_t) * json->array.alloc;

        if ((sky_uchar_t *) (json->object.values + json->object.alloc) == json->pool->d.last
            && json->pool->d.last + size <= json->pool->d.end) {
            json->pool->d.last += size;
        } else {
            sky_json_object_t *new = sky_palloc(json->pool, size << 1);
            sky_memcpy(new, json->object.values, size);
            json->object.values = new;
        }
        json->array.alloc <<= 1;
    }

    obj = json->object.values + (json->object.length++);
    obj->value.parent = json;
    obj->value.pool = json->pool;

    return obj;
}


static sky_inline sky_json_t *
json_array_get(sky_json_t *json) {
    sky_json_t *child;

    if (sky_unlikely(json->array.length == json->array.alloc)) {
        const sky_size_t size = sizeof(sky_json_t) * json->array.alloc;

        if ((sky_uchar_t *) (json->array.values + json->array.alloc) == json->pool->d.last
            && json->pool->d.last + size <= json->pool->d.end) {
            json->pool->d.last += size;
        } else {
            sky_json_t *new = sky_palloc(json->pool, size << 1);
            sky_memcpy(new, json->array.values, size);
            json->array.values = new;
        }
        json->array.alloc <<= 1;
    }
    child = json->array.values + (json->array.length++);
    child->parent = json;
    child->pool = json->pool;

    return child;
}


static sky_inline void
json_object_init(sky_json_t *json) {
    json->type = json_object;
    json->object.length = 0;
    json->object.alloc = 16;
    json->object.values = sky_pnalloc(json->pool, sizeof(sky_json_object_t) << 4);
}


static sky_inline void
json_array_init(sky_json_t *json) {
    json->type = json_array;
    json->array.length = 0;
    json->array.alloc = 16;
    json->array.values = sky_pnalloc(json->pool, sizeof(sky_json_t) << 4);
}