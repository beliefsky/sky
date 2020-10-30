//
// Created by weijing on 2020/1/1.
//

#include "json.h"
#include "log.h"
#include "memory.h"
#include "number.h"


#include <math.h>
#include <assert.h>

static void parse_whitespace(sky_uchar_t **ptr);

static sky_bool_t parse_string(sky_str_t *str, sky_uchar_t **ptr);

static sky_json_t *parse_loop(sky_pool_t *pool, sky_uchar_t *data);

static sky_json_t *json_object_create(sky_pool_t *pool);

static sky_json_t *json_array_create(sky_pool_t *pool);

static sky_json_object_t *json_object_get(sky_json_t *json);

static sky_json_t *json_array_get(sky_json_t *json);


sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json) {
    sky_uchar_t *p = json->data;
    return parse_loop(pool, p);
}

#define NEXT_OBJECT_START   0x80
#define NEXT_ARRAY_START    0x40
#define NEXT_OBJECT_END     0x20
#define NEXT_ARRAY_END      0x10
#define NEXT_KEY            0x8
#define NEXT_VALUE          0x4
#define NEXT_NODE           0x2
#define NEXT_KEY_VALUE      0X1

static sky_json_t *
parse_loop(sky_pool_t *pool, sky_uchar_t *data) {
    sky_uint8_t next;

    sky_json_t *tmp, *current, *root;
    sky_json_object_t *object;


    parse_whitespace(&data);
    if (*data == '{') {
        root = current = json_object_create(pool);
        next = NEXT_OBJECT_END | NEXT_KEY;
    } else if (*data == '[') {
        root = current = json_array_create(pool);
        next = NEXT_ARRAY_END | NEXT_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
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

                current->state = next;
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

                current->state = next;
                current = tmp;
                next = NEXT_ARRAY_END | NEXT_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;

                break;
            case '}':
                if (sky_unlikely(!(next & NEXT_OBJECT_END))) {
                    return null;
                }

                ++data;
                current = current->parent;
                break;
            case ']':
                if (sky_unlikely(!(next & NEXT_ARRAY_END))) {
                    return null;
                }

                ++data;
                current = current->parent;
                break;
            case ':':
                if (sky_unlikely(!(next & NEXT_KEY_VALUE))) {
                    return null;
                }

                ++data;
                next = NEXT_ARRAY_END | NEXT_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
                break;
            case ',':
                if (sky_unlikely(!(next & NEXT_NODE))) {
                    return null;
                }
                ++data;

                if (current->type == json_array) {
                    next = NEXT_ARRAY_END | NEXT_VALUE | NEXT_OBJECT_START | NEXT_ARRAY_START;
                } else {
                    next = NEXT_OBJECT_END | NEXT_KEY;
                }

                break;
            case '"':
                if (sky_unlikely(!(next & (NEXT_KEY | NEXT_VALUE)))) {
                    return null;
                }

                ++data;

                if ((next & NEXT_KEY)) {
                    object = json_object_get(current);

                    if (sky_unlikely(!parse_string(&object->key, &data))) {
                        return null;
                    }
                    next = NEXT_KEY_VALUE;
                } else if (current->parent->type == json_object) {
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
                if (sky_unlikely(!(next & NEXT_VALUE))) {
                    return null;
                }
                ++data;


                if (current->parent->type == json_object) {
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                break;
            case 't':
                if (sky_unlikely(!(next & NEXT_VALUE))) {
                    return null;
                }

                if (current->parent->type == json_object) {
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                break;
            case 'f':
                if (sky_unlikely(!(next & NEXT_VALUE))) {
                    return null;
                }

                if (current->parent->type == json_object) {
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
                break;
            case 'n':
                if (sky_unlikely(!(next & NEXT_VALUE))) {
                    return null;
                }

                if (current->parent->type == json_object) {
                    next = NEXT_NODE | NEXT_OBJECT_END;
                } else {
                    next = NEXT_NODE | NEXT_ARRAY_END;
                }
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


static void
parse_whitespace(sky_uchar_t **ptr) {
    sky_uchar_t *p = *ptr;

    if (*p > ' ') {
        return;
    }
    if ((*p == ' ' && *(p + 1) > ' ') || *p == '\r') {
        ++p;
    }


    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }
    *ptr = p;
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