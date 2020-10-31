//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "log.h"

#if defined(__AVX2__)

#include <immintrin.h>

#elif defined(__SSE4_2__)

#include <nmmintrin.h>

#elif defined(__SSE3__)

#include <tmmintrin.h>

#endif

static void parse_whitespace(sky_uchar_t **ptr);

static sky_bool_t parse_string(sky_str_t *str, sky_uchar_t **ptr);

static sky_bool_t parse_number(sky_json_t *json, sky_uchar_t **ptr);

static sky_json_t *parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end);

static sky_json_t *json_object_create(sky_pool_t *pool);

static sky_json_t *json_array_create(sky_pool_t *pool);

static sky_json_object_t *json_object_get(sky_json_t *json);

static sky_json_t *json_array_get(sky_json_t *json);


sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    sky_uchar_t *p = json->data;
    return parse_loop(pool, p, p + json->len);
}

#define NEXT_OBJECT_START   0x100
#define NEXT_ARRAY_START    0x80
#define NEXT_OBJECT_END     0x40
#define NEXT_ARRAY_END      0x20
#define NEXT_KEY            0x10
#define NEXT_KEY_VALUE      0x8
#define NEXT_OBJECT_VALUE   0x4
#define NEXT_ARRAY_VALUE    0x2
#define NEXT_NODE           0x1

static sky_json_t *
parse_loop(sky_pool_t *pool, sky_uchar_t *data, sky_uchar_t *end) {
    sky_uint16_t next;

    sky_json_t *tmp, *current, *root;
    sky_json_object_t *object;


    parse_whitespace(&data);
    if (*data == '{') {
        root = current = json_object_create(pool);
        next = NEXT_OBJECT_END | NEXT_KEY;
    } else if (*data == '[') {
        root = current = json_array_create(pool);
        next = NEXT_ARRAY_END | NEXT_ARRAY_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
    } else {
        return null;
    }
    ++data;

    for (;;) {

        switch (*data) {
            case '{':
                if (sky_unlikely(!(next & NEXT_OBJECT_START))) {
                    return null;
                }

                ++data;

                tmp = json_object_create(pool);
                tmp->parent = current;

                current = tmp;
                next = NEXT_OBJECT_END | NEXT_KEY;

                break;
            case '[':
                if (sky_unlikely(!(next & NEXT_ARRAY_START))) {
                    return null;
                }

                ++data;
                tmp = json_array_create(pool);
                tmp->parent = current;

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

                if (!!(next & NEXT_KEY)) {
                    object = json_object_get(current);

                    if (sky_unlikely(!parse_string(&object->key, &data))) {
                        return null;
                    }
                    next = NEXT_KEY_VALUE;
                } else if (!!(next & NEXT_OBJECT_VALUE)) {
                    tmp = &object->value;
                    tmp->type = json_string;
                    tmp->parent = current;
                    if (sky_unlikely(!parse_string(&tmp->string, &data))) {
                        return null;
                    }
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    tmp->type = json_string;
                    tmp->parent = current;

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
                if (!!(next & NEXT_OBJECT_VALUE)) {
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
                                 || sky_str4_cmp(data, 't', 'u', 'r', 'e'))) {
                    return null;
                }
                data += 4;

                if (!!(next & NEXT_OBJECT_VALUE)) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_boolean;
                tmp->parent = current;
                tmp->boolean = true;

                break;
            case 'f':
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE)))) {
                    return null;
                }
                ++data;

                if (sky_unlikely((end - data) < 4 || sky_str4_cmp(data, 'a', 'l', 's', 'e'))) {
                    return null;
                }
                data += 4;

                if (!!(next & NEXT_OBJECT_VALUE)) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_boolean;
                tmp->parent = current;
                tmp->boolean = false;
                break;
            case 'n':
                if (sky_unlikely(!(next & (NEXT_OBJECT_VALUE | NEXT_ARRAY_VALUE))
                                 || (end - data) < 4
                                 || sky_str4_cmp(data, 'n', 'u', 'l', 'l'))) {
                    return null;
                }
                data += 4;

                if (!!(next & NEXT_OBJECT_VALUE)) {
                    tmp = &object->value;
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    tmp = json_array_get(current);
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                tmp->type = json_null;
                tmp->parent = current;
                break;
            case '\0':
                return root;
            default:
                return null;
        }

        parse_whitespace(&data);
    }
}

static sky_inline sky_json_t *
json_object_create(sky_pool_t *pool) {
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t));
    json->type = json_object;
    json->object.length = 0;
    json->object.alloc = 16;
    json->object.values = sky_pnalloc(pool, sizeof(sky_json_object_t) << 4);

    return json;
}

static sky_inline sky_json_t *
json_array_create(sky_pool_t *pool) {
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t));
    json->type = json_array;
    json->array.length = 0;
    json->array.alloc = 16;
    json->array.values = sky_pnalloc(pool, sizeof(sky_json_t) << 4);

    return json;
}

static sky_inline sky_json_object_t *
json_object_get(sky_json_t *json) {
    if (json->object.length < json->object.alloc) {
        return json->object.values + (json->object.length++);
    }
    sky_log_error("null");

    return null;
}

static sky_inline sky_json_t *
json_array_get(sky_json_t *json) {
    if (json->array.length < json->array.alloc) {
        return json->array.values + (json->array.length++);
    }
    sky_log_error("null");

    return null;
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

#ifdef __AVX2__
    // common too: \n and spaces
    {
        const __m256i data = _mm256_loadu_si256((const __m256i *) p);
        const __m256i eol_spaces = _mm256_set_epi8(' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                                   ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                                                   ' ', ' ', ' ', '\n');

        const __m256i mask = _mm256_cmpeq_epi8(data, eol_spaces);
        const sky_uint32_t move_mask = (sky_uint32_t) (~_mm256_movemask_epi8(mask));
        if (move_mask) {
            const int32_t first_non_space = __builtin_ctz(move_mask);
            p += first_non_space;
        } else {
            p += 32;
        }
    }

    if (*p > ' ') {
        *ptr = p;
        return;
    }

    {
        // try skipping regular spaces first
        __m256i data = _mm256_loadu_si256((const __m256i *) p);
        __m256i mask = _mm256_cmpeq_epi8(data, _mm256_set1_epi8(' '));
        sky_uint32_t move_mask = (sky_uint32_t) (~_mm256_movemask_epi8(mask));
        if (move_mask) {

            const sky_int32_t first_non_space = __builtin_ctz(move_mask);
            p += first_non_space;
            if (*p > ' ') {
                *ptr = p;
                return;
            }
        } else {
            p += 32;
        }
    }

#endif

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
            *ptr = p + __builtin_ctz((sky_uint32_t) move_mask);;
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
    sky_uchar_t *p, *post;

    p = *ptr;
    while (*p) {
        if (sky_unlikely(*p < ' ')) {
            return false;
        }

        if (*p == '"') {
            if (*(p - 1) != '\\') {
                *p = '\0';
                str->data = *ptr;
                str->len = (sky_size_t) (p - str->data);

                *ptr = p + 1;

                return true;
            }
            *(p - 1) = *p;
            post = p++;

            while (*p) {
                if (sky_unlikely(*p < ' ')) {
                    return false;
                }

                if (*p == '"') {
                    if (*(p - 1) != '\\') {
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
            return false;
        }
        ++p;
    }


    return false;
}

static sky_bool_t
parse_number(sky_json_t *json, sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    json->string.data = p;
    ++p;

    for (;;) {
        if (sky_unlikely(*p < '-' || *p > ':' || *p == '/')) {
            json->type = json_integer;
            json->string.len = (sky_uint32_t) (p - json->string.data);
            *ptr = p;
            return true;
        }
        ++p;
    }
}