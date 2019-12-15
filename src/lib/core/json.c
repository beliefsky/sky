#include "json.h"
#include "rbtree.h"
#include "memory.h"
#include "log.h"
#include "number.h"

#define SKY_JSON_TAG_NULL   0
#define SKY_JSON_TAG_VALUE  1
#define SKY_JSON_TAG_OBJECT 2
#define SKY_JSON_TAG_ARRAY  3

#define json_int32_to_str(_j, _in, _out)                            \
    if (sky_unlikely(((_j)->buf->end - (_j)->buf->last) < 12)) {    \
        sky_buf_set((_j)->buf, (_j)->pool, 1023);                   \
    }                                                               \
    (_out)->data = (_j)->buf->last;                                 \
    (_out)->len = sky_int32_to_str((_in), (_out)->data);            \
    (_j)->buf->last += (_out)->len + 1

#define json_uint32_to_str(_j, _in, _out)                           \
    if (sky_unlikely(((_j)->buf->end - (_j)->buf->last) < 11)) {    \
        sky_buf_set((_j)->buf, (_j)->pool, 1023);                   \
    }                                                               \
    (_out)->data = (_j)->buf->last;                                 \
    (_out)->len = sky_uint32_to_str((_in), (_out)->data);           \
    (_j)->buf->last += (_out)->len + 1

#define json_int64_to_str(_j, _in, _out)                            \
    if (sky_unlikely(((_j)->buf->end - (_j)->buf->last) < 21)) {    \
        sky_buf_set((_j)->buf, (_j)->pool, 1023);                   \
    }                                                               \
    (_out)->data = (_j)->buf->last;                                 \
    (_out)->len = sky_int64_to_str((_in), (_out)->data);            \
    (_j)->buf->last += (_out)->len + 1

#define json_uint64_to_str(_j, _in, _out)                           \
    if (sky_unlikely(((_j)->buf->end - (_j)->buf->last) < 21)) {    \
        sky_buf_set((_j)->buf, (_j)->pool, 1023);                   \
    }                                                               \
    (_out)->data = (_j)->buf->last;                                 \
    (_out)->len = sky_uint64_to_str((_in), (_out)->data);           \
    (_j)->buf->last += (_out)->len + 1


typedef struct sky_json_node_s      sky_json_node_t;
typedef struct sky_json_item_s      sky_json_item_t;

struct sky_json_item_s{
    sky_json_item_t *prev;
    sky_json_item_t *next;


    sky_uint8_t         tag:2;
    sky_bool_t          is_str:1;
    sky_bool_t          encode:1;
    union {
        struct {
            sky_str_t       *data;
            sky_uint32_t    len;
        } str;
        sky_str_t           num;
        sky_json_object_t   *obj;
        sky_json_array_t    *array;
    };
};

struct sky_json_node_s {
    sky_rbtree_node_t   node;
    sky_uint8_t         tag:2;
    sky_bool_t          is_str:1;
    sky_bool_t          encode:1;
    sky_str_t           key;
    union {
        struct {
            sky_str_t       *data;
            sky_uint32_t    len;
        } str;
        sky_str_t           num;
        sky_json_object_t   *obj;
        sky_json_array_t    *array;
    };
};

struct sky_json_object_s {
    sky_rbtree_t        root;
    sky_rbtree_node_t   sentinel;

    sky_bool_t          is_obj:1;
    sky_uint32_t        mem_size;
    sky_pool_t          *pool;
    sky_buf_t           *buf;
    void                *parent;
};

struct sky_json_array_s {
    sky_uint32_t    size;
    sky_json_item_t data;


    sky_bool_t      is_obj:1;
    sky_uint32_t    mem_size;
    sky_pool_t      *pool;
    sky_buf_t       *buf;
    void            *parent;
};


static void json_object_put_num(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len,
        sky_uchar_t *num, sky_uint32_t num_len);
void json_array_put_num(sky_json_array_t *json, sky_uchar_t *num, sky_uint32_t num_len);
static sky_uint32_t json_hash(sky_uchar_t *str);
static sky_bool_t json_object_decode(sky_json_object_t *json, sky_uchar_t **b);
static sky_bool_t json_array_decode(sky_json_array_t *json, sky_uchar_t **b);
static void json_object_encode(sky_json_object_t *obj, sky_uchar_t **b);
static void json_array_encode(sky_json_array_t *array, sky_uchar_t **b);
static void json_object_push_parent_size(sky_json_object_t *obj, sky_int64_t size);
static void json_array_push_parent_size(sky_json_array_t *array, sky_int64_t size);


sky_inline sky_json_object_t *
sky_json_object_create(sky_pool_t *pool, sky_buf_t *buf) {
    sky_json_object_t   *object;

    object = sky_palloc(pool, sizeof(sky_json_object_t));
    object->mem_size = 2;
    object->buf = buf;
    object->pool = pool;
    object->parent = null;
    sky_rbtree_init(&object->root, &object->sentinel, sky_rbtree_insert_value);

    return object;
}


sky_inline sky_json_array_t *
sky_json_array_create(sky_pool_t *pool, sky_buf_t *buf) {
    sky_json_array_t    *array;

    array = sky_palloc(pool, sizeof(sky_json_array_t));
    array->buf = buf;
    array->pool = pool;
    array->size = 0;
    array->mem_size = 2;
    array->parent = null;
    array->data.prev = array->data.next = &array->data;

    return array;
}


sky_inline void
sky_json_object_put_str(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_str_t *value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_uchar_t         *p;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 2;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = true;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len;
                    } else {
                        size = node->num.len - 2;
                        node->is_str = true;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size - 2;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = true;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size - 2;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = true;
                    break;
                default:
                    return;
            }
            node->encode = true;
            node->str.data = value;
            node->str.len = value->len;
            for(p = value->data; *p; ++p) {
                switch (*p) {
                    case '"':
                    case '\\':
                    case '\b':
                    case '\f':
                    case '\n':
                    case '\r':
                    case '\t':
                        node->encode = false;
                        ++node->str.len;
                        break;
                    default:
                        break;
                }
            }
            size = node->str.len - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = true;
    node->encode = true;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    node->str.len = value->len;
    node->str.data = value;

    for(p = value->data; *p; ++p) {
        switch (*p) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                node->encode = false;
                ++node->str.len;
                break;
            default:
                break;
        }
    }
    size = node->key.len + node->str.len + 6;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


void
sky_json_object_put_str2(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_char_t *value, sky_uint32_t len) {
    sky_str_t *v = sky_palloc(json->pool, sizeof(sky_str_t));
    v->data  = (sky_uchar_t *) value;
    v->len = len;
    sky_json_object_put_str(json, key, key_len, v);
}

sky_str_t *
sky_json_object_get_str(sky_json_object_t *json, sky_char_t *key) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_VALUE) {
                if (node->is_str) {
                    return node->str.data;
                } else {
                    return &node->num;
                }
            }
            return null;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }

    return null;
}


void
sky_json_object_put_null(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                    } else {
                        size = node->num.len;
                    }
                    node->tag = SKY_JSON_TAG_NULL;
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_NULL;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_NULL;
                    break;
                default:
                    return;
            }
            size = 4 - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_NULL;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    size = node->key.len + node->num.len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


void
sky_json_object_put_boolean(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_bool_t value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                        node->is_str = false;
                    } else {
                        size = node->num.len;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                default:
                    return;
            }
            size = 4 - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            if (value) {
                sky_str_set(&node->num, "true");
            } else {
                sky_str_set(&node->num, "false");
            }
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = false;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    if (value) {
        sky_str_set(&node->num, "true");
    } else {
        sky_str_set(&node->num, "false");
    }
    size = node->key.len + node->num.len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_bool_t
sky_json_object_get_boolean(sky_json_object_t *json, sky_char_t *key) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_VALUE) {
                if (node->is_str) {
                    if (node->str.len == 4 && sky_str4_cmp(node->str.data->data, 't', 'r', 'u', 'e')) {
                        return true;
                    }
                } else {
                    if (node->num.len == 4 && sky_str4_cmp(node->num.data, 't', 'r', 'u', 'e')) {
                        return true;
                    }
                }
            }
            return false;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }

    return false;
}


void
sky_json_object_put_int32(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_int32_t value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                        node->is_str = false;
                    } else {
                        size = node->num.len;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                default:
                    return;
            }
            json_int32_to_str(json, value, &node->num);
            size = node->num.len - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = false;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    json_int32_to_str(json, value, &node->num);
    size = node->key.len + node->num.len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_bool_t
sky_json_object_get_int32(sky_json_object_t *json, sky_char_t *key, sky_int32_t *out) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_VALUE) {
                if (node->is_str) {
                    return sky_str_to_int32(node->str.data, out);
                } else {
                    return sky_str_to_int32(&node->num, out);
                }
            }
            return false;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    return false;
}
void
sky_json_object_put_uint32(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_uint32_t value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                        node->is_str = false;
                    } else {
                        size = node->num.len;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                default:
                    return;
            }
            json_uint32_to_str(json, value, &node->num);
            size = node->num.len - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = false;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;

    json_uint32_to_str(json, value, &node->num);
    size = node->key.len + node->num.len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_bool_t
sky_json_object_get_uint32(sky_json_object_t *json, sky_char_t *key, sky_uint32_t *out) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_VALUE) {
                if (node->is_str) {
                    return sky_str_to_uint32(node->str.data, out);
                } else {
                    return sky_str_to_uint32(&node->num, out);
                }
            }
            return false;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    return false;
}


void
sky_json_object_put_int64(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_int64_t value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                        node->is_str = false;
                    } else {
                        size = node->num.len;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                default:
                    return;
            }
            json_int64_to_str(json, value, &node->num);
            size = node->num.len - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = false;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    json_int64_to_str(json, value, &node->num);
    size = node->key.len + node->num.len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_bool_t
sky_json_object_get_int64(sky_json_object_t *json, sky_char_t *key, sky_int64_t *out) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_VALUE) {
                if (node->is_str) {
                    return sky_str_to_int64(node->str.data, out);
                } else {
                    return sky_str_to_int64(&node->num, out);
                }
            }
            return false;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    return false;
}


void
sky_json_object_put_uint64(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_uint64_t value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                        node->is_str = false;
                    } else {
                        size = node->num.len;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                default:
                    return;
            }
            json_uint64_to_str(json, value, &node->num);
            size = node->num.len - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = false;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    json_uint64_to_str(json, value, &node->num);
    size = node->key.len + node->num.len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_bool_t
sky_json_object_get_uint64(sky_json_object_t *json, sky_char_t *key, sky_uint64_t *out) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_VALUE) {
                if (node->is_str) {
                    return sky_str_to_uint64(node->str.data, out);
                } else {
                    return sky_str_to_uint64(&node->num, out);
                }
            }
            return false;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    return false;
}


void sky_json_object_put_obj(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_json_object_t *value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_OBJECT;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                    } else {
                        size = node->num.len;
                    }
                    node->tag = SKY_JSON_TAG_OBJECT;
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_OBJECT;
                    break;
                default:
                    return;
            }
            node->obj = value;
            value->parent = json;
            value->is_obj = true;
            size = value->mem_size - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_OBJECT;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    node->obj = value;
    value->parent = json;
    value->is_obj = true;
    size = node->key.len + value->mem_size + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_json_object_t *
sky_json_object_get_obj(sky_json_object_t *json, sky_char_t *key) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_OBJECT) {
                return node->obj;
            }
            return null;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }

    return null;
}


void
sky_json_object_put_array(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_json_array_t *value) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_ARRAY;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                    } else {
                        size = node->num.len;
                    }
                    node->tag = SKY_JSON_TAG_ARRAY;
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_ARRAY;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    break;
                default:
                    return;
            }
            node->array = value;
            value->parent = json;
            value->is_obj = true;
            size = value->mem_size - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_ARRAY;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;
    node->array = value;
    value->parent = json;
    value->is_obj = true;
    size = node->key.len + value->mem_size + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


sky_json_array_t *
sky_json_object_get_array(sky_json_object_t *json, sky_char_t *key) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            if (node->tag == SKY_JSON_TAG_ARRAY) {
                return node->array;
            }
            return null;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }

    return null;
}


sky_inline void
sky_json_array_put_str(sky_json_array_t *json, sky_str_t *value) {
    sky_json_item_t *item;
    sky_uchar_t     *p;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = true;
    item->encode = true;
    item->str.data = value;
    item->str.len = value->len;
    for(p = value->data; *p; ++p) {
        switch (*p) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                item->encode = false;
                ++item->str.len;
                break;
            default:
                break;
        }
    }
    size = item->str.len + 3;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


void
sky_json_array_put_str2(sky_json_array_t *json, sky_char_t *value, sky_uint32_t len) {
    sky_str_t *v = sky_palloc(json->pool, sizeof(sky_str_t));
    v->data = (sky_uchar_t *) value;
    v->len = len;
    sky_json_array_put_str(json, v);
}

sky_str_t *sky_json_data_get_str(sky_json_data_t *data) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_VALUE) {
        if (item->is_str) {
            return item->str.data;
        } else {
            return &item->num;
        }
    }
    return null;
}


void
sky_json_array_put_null(sky_json_array_t *json) {
    sky_json_item_t *item;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_NULL;

    if (json->parent) {
        json_array_push_parent_size(json, 5);
    }
    json->mem_size += 5;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


void
sky_json_array_put_boolean(sky_json_array_t *json, sky_bool_t value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = false;

    if (value) {
        sky_str_set(&item->num, "true");
    } else {
        sky_str_set(&item->num, "false");
    }

    size = item->num.len + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_bool_t
sky_json_data_get_boolean(sky_json_data_t *data) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_VALUE) {
        if (item->is_str) {
            if (item->str.len == 4 && sky_str4_cmp(item->str.data->data, 't', 'r', 'u', 'e')) {
                return true;
            }
        } else {
            if (item->num.len == 4 && sky_str4_cmp(item->num.data, 't', 'r', 'u', 'e')) {
                return true;
            }
        }
    }
    return false;
}


void
sky_json_array_put_int32(sky_json_array_t *json, sky_int32_t value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = false;

    json_int32_to_str(json, value, &item->num);

    size = item->num.len + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_bool_t
sky_json_data_get_int32(sky_json_data_t *data, sky_int32_t *value) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_VALUE) {
        if (item->is_str) {
            return sky_str_to_int32(item->str.data, value);
        } else {
            return sky_str_to_int32(&item->num, value);
        }
    }
    return false;
}


void
sky_json_array_put_uint32(sky_json_array_t *json, sky_uint32_t value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = false;

    json_uint32_to_str(json, value, &item->num);

    size = item->num.len + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_bool_t sky_json_data_get_uint32(sky_json_data_t *data, sky_uint32_t *value) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_VALUE) {
        if (item->is_str) {
            return sky_str_to_uint32(item->str.data, value);
        } else {
            return sky_str_to_uint32(&item->num, value);
        }
    }
    return false;
}


void
sky_json_array_put_int64(sky_json_array_t *json, sky_int64_t value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = false;

    json_int64_to_str(json, value, &item->num);

    size = item->num.len + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_bool_t sky_json_data_get_int64(sky_json_data_t *data, sky_int64_t *value) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_VALUE) {
        if (item->is_str) {
            return sky_str_to_int64(item->str.data, value);
        } else {
            return sky_str_to_int64(&item->num, value);
        }
    }
    return false;
}


void
sky_json_array_put_uint64(sky_json_array_t *json, sky_uint64_t value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = false;

    json_uint64_to_str(json, value, &item->num);

    size = item->num.len + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_bool_t
sky_json_data_get_uint64(sky_json_data_t *data, sky_uint64_t *value) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_VALUE) {
        if (item->is_str) {
            return sky_str_to_uint64(item->str.data, value);
        } else {
            return sky_str_to_uint64(&item->num, value);
        }
    }
    return false;
}


void
sky_json_array_put_obj(sky_json_array_t *json, sky_json_object_t *value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_OBJECT;
    item->obj = value;
    value->parent = json;
    value->is_obj = false;
    size = value->mem_size + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_json_object_t *
sky_json_data_get_obj(sky_json_data_t *data) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_OBJECT) {
        return item->obj;
    }
    return null;
}


void
sky_json_array_put_array(sky_json_array_t *json, sky_json_array_t *value) {
    sky_json_item_t *item;
    sky_uint32_t    size;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_ARRAY;
    item->array = value;
    value->parent = json;
    value->is_obj = false;
    size = value->mem_size + 1;
    if (json->parent) {
        json_array_push_parent_size(json, size);
    }
    json->mem_size += size;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


sky_json_array_t *
sky_json_data_get_array(sky_json_data_t *data) {
    sky_json_item_t *item;

    item = (sky_json_item_t *) data;
    if (item->tag == SKY_JSON_TAG_ARRAY) {
        return item->array;
    }
    return null;
}


sky_json_object_t *
sky_json_object_decode(sky_str_t *str, sky_pool_t *pool, sky_buf_t *buf) {
    sky_json_object_t   *obj;
    sky_uchar_t         *p;

    p = str->data;
    for(;;) {
        switch (*(p++)) {
            case '\t':
            case '\n':
            case '\r':
            case ' ':
                break;
            case '{':
                obj = sky_json_object_create(pool, buf);
                if (json_object_decode(obj, &p)) {
                    return obj;
                }
                return null;
            default:
                return null;
        }
    }
}


sky_json_array_t *
sky_json_array_decode(sky_str_t *str, sky_pool_t *pool, sky_buf_t *buf) {
    sky_json_array_t    *array;
    sky_uchar_t         *p;

    p = str->data;
    for(;;) {
        switch (*(p++)) {
            case '\t':
            case '\n':
            case '\r':
            case ' ':
                break;
            case '[':
                array = sky_json_array_create(pool, buf);
                if (json_array_decode(array, &p)) {
                    return array;
                }
                return null;
            default:
                return null;
        }
    }
}


sky_str_t *
sky_json_object_encode(sky_json_object_t *json) {
    sky_str_t           *result;
    sky_uchar_t         *p;

    result = sky_palloc(json->pool, sizeof(sky_str_t));
    result->data = p = sky_palloc(json->pool, json->mem_size + 1);
    json_object_encode(json, &p);
    *p = '\0';
    result->len = (sky_uint32_t) (p - result->data);

    return result;
}


void
sky_json_object_encode2(sky_json_object_t *json, sky_str_t *out) {
    sky_uchar_t         *p;

    out->data = p = sky_palloc(json->pool, json->mem_size + 1);
    json_object_encode(json, &p);
    *p = '\0';
    out->len = (sky_uint32_t) (p - out->data);
}
sky_str_t *
sky_json_array_encode(sky_json_array_t *json) {
    sky_str_t       *result;
    sky_uchar_t     *p;

    result = sky_palloc(json->pool, sizeof(sky_str_t));
    result->data = p = sky_palloc(json->pool, json->mem_size + 1);
    json_array_encode(json, &p);
    *p = '\0';
    result->len = (sky_uint32_t) (p - result->data);

    return result;
}


void sky_json_array_encode2(sky_json_array_t *json, sky_str_t *out) {
    sky_uchar_t     *p;

    out->data = p = sky_palloc(json->pool, json->mem_size + 1);
    json_array_encode(json, &p);
    *p = '\0';
    out->len = (sky_uint32_t) (p - out->data);
}


static void
json_object_put_num(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_uchar_t *num, sky_uint32_t num_len) {
    sky_json_node_t     *node;
    sky_uint32_t        hash;
    sky_int64_t         size;
    sky_rbtree_node_t   *tmp, *sentinel;

    hash = json_hash((sky_uchar_t *) key);
    tmp = json->root.root;
    sentinel = &json->sentinel;
    while (tmp != sentinel) {
        if (tmp->key == hash) {
            node = (sky_json_node_t *) tmp;
            switch (node->tag) {
                case SKY_JSON_TAG_NULL:
                    size = 4;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_VALUE:
                    if (node->is_str) {
                        size = node->str.len + 2;
                        node->is_str = false;
                    } else {
                        size = node->num.len;
                    }
                    break;
                case SKY_JSON_TAG_OBJECT:
                    size = node->obj->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                case SKY_JSON_TAG_ARRAY:
                    size = node->array->mem_size;
                    node->tag = SKY_JSON_TAG_VALUE;
                    node->is_str = false;
                    break;
                default:
                    return;
            }
            node->num.data = num;
            node->num.len = num_len;
            size = num_len - size;
            if (json->parent && size) {
                json_object_push_parent_size(json, size);
            }
            json->mem_size += size;
            return;
        }
        if (hash < tmp->key) {
            tmp = tmp->left;
        } else {
            tmp = tmp->right;
        }
    }
    node = sky_palloc(json->pool, sizeof(sky_json_node_t));
    node->tag = SKY_JSON_TAG_VALUE;
    node->is_str = false;
    node->key.data = (sky_uchar_t *) key;
    node->key.len = key_len;

    node->num.data = num;
    node->num.len = num_len;

    size = node->key.len + num_len + 4;
    if (json->parent) {
        json_object_push_parent_size(json, size);
    }
    json->mem_size += size;
    node->node.key = hash;

    sky_rbtree_insert(&json->root, &node->node);
}


void
json_array_put_num(sky_json_array_t *json, sky_uchar_t *num, sky_uint32_t num_len) {
    sky_json_item_t *item;

    item = sky_palloc(json->pool, sizeof(sky_json_item_t));

    ++json->size;
    item->tag = SKY_JSON_TAG_VALUE;
    item->is_str = false;
    item->num.data = num;
    item->num.len = num_len++;
    if (json->parent) {
        json_array_push_parent_size(json, num_len);
    }
    json->mem_size += num_len;

    item->next = &json->data;
    item->prev = item->next->prev;
    item->next->prev = item->prev->next = item;
}


static sky_bool_t
json_object_decode(sky_json_object_t *json, sky_uchar_t **b) {
    sky_uchar_t *p, *key, *value;

    enum {
        START = 0,
        KEY_BEFORE,
        KEY,
        KEY_AFTER,
        VALUE_BEFORE,
        VALUE_STRING,
        VALUE_NULL,
        VALUE_BOOL,
        VALUE_NUMBER,
        VALUE_AFTER
    } state;

    p = *b;
    key = value = null;
    state = START;
    for(;;) {
        switch (state) {
            case START:
                for(;;) {
                    switch (*(p++)) {
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            continue;
                        case '\"':
                            break;
                        case '}':
                            *b = p;
                            return true;
                        default:
                            return false;
                    }
                    key = p;
                    state = KEY;
                    break;
                }
                break;
            case KEY_BEFORE:
                for(;;) {
                    switch (*(p++)) {
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            continue;
                        case '\"':
                            break;
                        default:
                            return false;
                    }
                    key = p;
                    state = KEY;
                    break;
                }
                break;
            case KEY: {
                for (;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '\"':
                            *(p++) = '\0';
                            break;
                        case '\\': {
                            value = p++;
                            switch (*p) {
                                case '"':
                                case '\\':
                                    *(value++) = *p;
                                    break;
                                case 'b':
                                    *(value++) = '\b';
                                    break;
                                case 'f':
                                    *(value++) = '\f';
                                    break;
                                case 'n':
                                    *(value++) = '\n';
                                    break;
                                case 'r':
                                    *(value++) = '\r';
                                    break;
                                case 't':
                                    *(value++) = '\t';
                                    break;
                                default:
                                    return false;
                            }
                            ++p;
                            for(;;) {
                                switch (*p) {
                                    case '\0':
                                        return false;
                                    case '\"':
                                        ++p;
                                        *value = '\0';
                                        break;
                                    case '\\': {
                                        switch (*(++p)) {
                                            case '"':
                                            case '\\':
                                                *(value++) = *p;
                                                break;
                                            case 'b':
                                                *(value++) = '\b';
                                                break;
                                            case 'f':
                                                *(value++) = '\f';
                                                break;
                                            case 'n':
                                                *(value++) = '\n';
                                                break;
                                            case 'r':
                                                *(value++) = '\r';
                                                break;
                                            case 't':
                                                *(value++) = '\t';
                                                break;
                                            default:
                                                return false;
                                        }
                                        ++p;
                                    }
                                        continue;
                                    default:
                                        *(value++) = *(p++);
                                        continue;
                                }
                                break;
                            }
                        }
                        break;

                        default:
                            ++p;
                            continue;
                    }
                    state = KEY_AFTER;
                    break;
                }
            }
                break;
            case KEY_AFTER:
                for(;;) {
                    switch (*(p++)) {
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            continue;
                        case ':':
                            break;
                        default:
                            return false;
                    }
                    state = VALUE_BEFORE;
                    break;
                }
                break;
            case VALUE_BEFORE:
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            ++p;
                            continue;
                        case '\"':
                            value = ++p;
                            state = VALUE_STRING;
                            break;
                        case 'n':
                            value = p++;
                            state = VALUE_NULL;
                            break;
                        case 'f':
                        case 't':
                            value = p++;
                            state = VALUE_BOOL;
                            break;
                        case '{':
                            ++p;
                            {
                                sky_json_object_t *tmp = sky_json_object_create(json->pool, json->buf);
                                if (!json_object_decode(tmp, &p)) {
                                    return false;
                                }
                                sky_json_object_put_obj(json, (sky_char_t *) key, sky_strlen(key), tmp);
                            }
                            state = VALUE_AFTER;
                            break;
                        case '[':
                            ++p;
                            {
                                sky_json_array_t *tmp = sky_json_array_create(json->pool, json->buf);
                                if (!json_array_decode(tmp, &p)) {
                                    return false;
                                }
                                sky_json_object_put_array(json, (sky_char_t *) key, sky_strlen(key), tmp);
                            }
                            state = VALUE_AFTER;
                            break;
                        default:
                            value = p;
                            state = VALUE_NUMBER;
                            break;
                    }
                    break;
                }
                break;
            case VALUE_STRING: {
                sky_str_t *tmp = sky_palloc(json->pool, sizeof(sky_str_t));
                tmp->data = value;
                for (;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '\"':
                            tmp->len = (sky_uint32_t) (p - value);
                            *(p++) = '\0';
                            break;
                        case '\\': {
                            value = p++;
                            switch (*p) {
                                case '"':
                                case '\\':
                                    *(value++) = *p;
                                    break;
                                case 'b':
                                    *(value++) = '\b';
                                    break;
                                case 'f':
                                    *(value++) = '\f';
                                    break;
                                case 'n':
                                    *(value++) = '\n';
                                    break;
                                case 'r':
                                    *(value++) = '\r';
                                    break;
                                case 't':
                                    *(value++) = '\t';
                                    break;
                                default:
                                    return false;
                            }
                            ++p;
                            for(;;) {
                                switch (*p) {
                                    case '\0':
                                        return false;
                                    case '\"':
                                        ++p;
                                        *value = '\0';
                                        tmp->len = (sky_uint32_t) (value - tmp->data);
                                        break;
                                    case '\\': {
                                        switch (*(++p)) {
                                            case '"':
                                            case '\\':
                                                *(value++) = *p;
                                                break;
                                            case 'b':
                                                *(value++) = '\b';
                                                break;
                                            case 'f':
                                                *(value++) = '\f';
                                                break;
                                            case 'n':
                                                *(value++) = '\n';
                                                break;
                                            case 'r':
                                                *(value++) = '\r';
                                                break;
                                            case 't':
                                                *(value++) = '\t';
                                                break;
                                            default:
                                                return false;
                                        }
                                        ++p;
                                    }
                                        continue;
                                    default:
                                        *(value++) = *(p++);
                                        continue;
                                }
                                break;
                            }
                        }
                            break;
                        default:
                            ++p;
                            continue;
                    }
                    break;
                }
                sky_json_object_put_str(json, (sky_char_t *) key, sky_strlen(key), tmp);
            }
                state = VALUE_AFTER;
                break;
            case VALUE_NULL:
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case 'u':
                        case 'l':
                            ++p;
                            continue;
                        default:
                            break;
                    }
                    if (p - value != 4 || !sky_str4_cmp(value, 'n', 'u', 'l', 'l')) {
                        return false;
                    }
                    sky_json_object_put_null(json, (sky_char_t *) key, sky_strlen(key));
                    state = VALUE_AFTER;
                    break;
                }
                break;
            case VALUE_BOOL:
                for(;;) { // true false
                    switch (*(p++)) {
                        case '\0':
                            return false;
                        case 'a':
                        case 'l':
                        case 'r':
                        case 's':
                        case 'u':
                            continue;
                        case 'e':
                            break;
                        default:
                            return false;
                    }
                    if (*value == 't') {
                        if (p - value != 4 || !sky_str4_cmp(value, 't', 'r', 'u', 'e')) {
                            return false;
                        }
                        sky_json_object_put_boolean(json, (sky_char_t *) key, sky_strlen(key), true);
                    } else {
                        if (p - (value++) != 5 || !sky_str4_cmp(value, 'a', 'l', 's', 'e')) {
                            return false;
                        }
                        sky_json_object_put_boolean(json, (sky_char_t *) key, sky_strlen(key), false);
                    }
                    state = VALUE_AFTER;
                    break;
                }
                break;
            case VALUE_NUMBER:
                if (*p == '-') {
                    ++p;
                }
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '.':
                            ++p;
                            for (;;) {
                                if (*p == '\0') {
                                    return false;
                                }
                                if (*p < '0' || *p > '9') {
                                    break;
                                }
                                ++p;
                            }
                            break;
                        default:
                            if (*p < '0' || *p > '9') {
                                break;
                            }
                            ++p;
                            continue;
                    }
                    break;
                }
                switch (*p) {
                    case '\t':
                    case '\n':
                    case '\r':
                    case ' ':
                        json_object_put_num(json, (sky_char_t *) key, sky_strlen(key), value, (sky_uint32_t) (p - value));
                        *(p++) = '\0';
                        state = VALUE_AFTER;
                        break;
                    case ',':
                        json_object_put_num(json, (sky_char_t *) key, sky_strlen(key), value, (sky_uint32_t) (p - value));
                        *(p++) = '\0';
                        state = KEY_BEFORE;
                        break;
                    case '}':
                        json_object_put_num(json, (sky_char_t *) key, sky_strlen(key), value, (sky_uint32_t) (p - value));
                        *(p++) = '\0';
                        *b = p;
                        return true;
                    default:
                        return false;
                }
                break;
            case VALUE_AFTER:
                for(;;) {
                    switch (*(p++)) {
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            continue;
                        case ',':
                            break;
                        case '}':
                            *b = p;
                            return true;
                        default:
                            return false;
                    }
                    state = KEY_BEFORE;
                    break;
                }
                break;
        }
    }
}

static sky_bool_t
json_array_decode(sky_json_array_t *json, sky_uchar_t **b) {
    sky_uchar_t *p, *value;

    enum {
        START = 0,
        VALUE_BEFORE,
        VALUE_STRING,
        VALUE_NUMBER,
        VALUE_BOOL,
        VALUE_NULL,
        VALUE_AFTER
    } state;

    p = *b;
    value = null;
    state = START;
    for(;;) {
        switch (state) {
            case START:
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            ++p;
                            continue;
                        case '\"':
                            value = ++p;
                            state = VALUE_STRING;
                            break;
                        case 'n':
                            value = p++;
                            state = VALUE_NULL;
                            break;
                        case 'f':
                        case 't':
                            value = p++;
                            state = VALUE_BOOL;
                            break;
                        case '{':
                            ++p;
                            {
                                sky_json_object_t *tmp = sky_json_object_create(json->pool, json->buf);
                                if (!json_object_decode(tmp, &p)) {
                                    return false;
                                }
                                sky_json_array_put_obj(json, tmp);
                            }
                            state = VALUE_AFTER;
                            break;
                        case '[':
                            ++p;
                            {
                                sky_json_array_t *tmp = sky_json_array_create(json->pool, json->buf);
                                if (!json_array_decode(tmp, &p)) {
                                    return false;
                                }
                                sky_json_array_put_array(json, tmp);
                            }
                            state = VALUE_AFTER;
                            break;
                        case ']':
                            *b = ++p;
                            return true;
                        default:
                            value = p;
                            state = VALUE_NUMBER;
                            break;
                    }
                    break;
                }
                break;
            case VALUE_BEFORE:
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            ++p;
                            continue;
                        case '\"':
                            value = ++p;
                            state = VALUE_STRING;
                            break;
                        case 'n':
                            value = p++;
                            state = VALUE_NULL;
                            break;
                        case 'f':
                        case 't':
                            value = p++;
                            state = VALUE_BOOL;
                            break;
                        case '{':
                            ++p;
                            {
                                sky_json_object_t *tmp = sky_json_object_create(json->pool, json->buf);
                                if (!json_object_decode(tmp, &p)) {
                                    return false;
                                }
                                sky_json_array_put_obj(json, tmp);
                            }
                            state = VALUE_AFTER;
                            break;
                        case '[':
                            ++p;
                            {
                                sky_json_array_t *tmp = sky_json_array_create(json->pool, json->buf);
                                if (!json_array_decode(tmp, &p)) {
                                    return false;
                                }
                                sky_json_array_put_array(json, tmp);
                            }
                            state = VALUE_AFTER;
                            break;
                        default:
                            value = p;
                            state = VALUE_NUMBER;
                            break;
                    }
                    break;
                }
                break;
            case VALUE_STRING: {
                sky_str_t *tmp = sky_palloc(json->pool, sizeof(sky_str_t));
                tmp->data = value;
                for (;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '\"':
                            tmp->len = (sky_uint32_t) (p - value);
                            *(p++) = '\0';
                            break;
                        case '\\': {
                            value = p++;
                            switch (*p) {
                                case '"':
                                case '\\':
                                    *(value++) = *p;
                                    break;
                                case 'b':
                                    *(value++) = '\b';
                                    break;
                                case 'f':
                                    *(value++) = '\f';
                                    break;
                                case 'n':
                                    *(value++) = '\n';
                                    break;
                                case 'r':
                                    *(value++) = '\r';
                                    break;
                                case 't':
                                    *(value++) = '\t';
                                    break;
                                default:
                                    return false;
                            }
                            ++p;
                            for (;;) {
                                switch (*p) {
                                    case '\0':
                                        return false;
                                    case '\"':
                                        ++p;
                                        *value = '\0';
                                        tmp->len = (sky_uint32_t) (value - tmp->data);
                                        break;
                                    case '\\': {
                                        switch (*(++p)) {
                                            case '"':
                                            case '\\':
                                                *(value++) = *p;
                                                break;
                                            case 'b':
                                                *(value++) = '\b';
                                                break;
                                            case 'f':
                                                *(value++) = '\f';
                                                break;
                                            case 'n':
                                                *(value++) = '\n';
                                                break;
                                            case 'r':
                                                *(value++) = '\r';
                                                break;
                                            case 't':
                                                *(value++) = '\t';
                                                break;
                                            default:
                                                return false;
                                        }
                                        ++p;
                                    }
                                        continue;
                                    default:
                                        *(value++) = *(p++);
                                        continue;
                                }
                                break;
                            }
                        }
                            break;
                        default:
                            ++p;
                            continue;
                    }
                    break;
                }
                sky_json_array_put_str(json, tmp);
            }
                state = VALUE_AFTER;
                break;
            case VALUE_NUMBER:
                if (*p == '-') {
                    ++p;
                }
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case '.':
                            ++p;
                            for (;;) {
                                if (*p == '\0') {
                                    return false;
                                }
                                if (*p < '0' || *p > '9') {
                                    break;
                                }
                                ++p;
                            }
                            break;
                        default:
                            if (*p < '0' || *p > '9') {
                                break;
                            }
                            ++p;
                            continue;
                    }
                    break;
                }
                switch (*p) {
                    case '\t':
                    case '\n':
                    case '\r':
                    case ' ':
                        json_array_put_num(json, value, (sky_uint32_t) (p - value));
                        *(p++) = '\0';
                        state = VALUE_AFTER;
                        break;
                    case ',':
                        json_array_put_num(json, value, (sky_uint32_t) (p - value));
                        *(p++) = '\0';
                        state = VALUE_BEFORE;
                        break;
                    case ']':
                        json_array_put_num(json, value, (sky_uint32_t) (p - value));
                        *(p++) = '\0';
                        *b = p;
                        return true;
                    default:
                        return false;
                }
                break;
            case VALUE_BOOL:
                for(;;) { // true false
                    switch (*(p++)) {
                        case '\0':
                            return false;
                        case 'a':
                        case 'l':
                        case 'r':
                        case 's':
                        case 'u':
                            continue;
                        case 'e':
                            break;
                        default:
                            return false;
                    }
                    if (*value == 't') {
                        if (p - value != 4 || !sky_str4_cmp(value, 't', 'r', 'u', 'e')) {
                            return false;
                        }
                        sky_json_array_put_boolean(json, true);
                    } else {
                        if (p - (value++) != 5 || !sky_str4_cmp(value, 'a', 'l', 's', 'e')) {
                            return false;
                        }
                        sky_json_array_put_boolean(json, false);
                    }
                    state = VALUE_AFTER;
                    break;
                }
                break;
            case VALUE_NULL:
                for(;;) {
                    switch (*p) {
                        case '\0':
                            return false;
                        case 'u':
                        case 'l':
                            ++p;
                            continue;
                        default:
                            break;
                    }
                    if (p - value != 4 || !sky_str4_cmp(value, 'n', 'u', 'l', 'l')) {
                        return false;
                    }
                    sky_json_array_put_null(json);
                    state = VALUE_AFTER;
                    break;
                }
                break;
            case VALUE_AFTER:
                for(;;) {
                    switch (*(p++)) {
                        case '\t':
                        case '\n':
                        case '\r':
                        case ' ':
                            continue;
                        case ',':
                            break;
                        case ']':
                            *b = p;
                            return true;
                        default:
                            return false;
                    }
                    state = VALUE_BEFORE;
                    break;
                }
                break;
        }
    }
}

static void
json_object_encode(sky_json_object_t *obj, sky_uchar_t **b) {
    sky_json_node_t     *node;
    sky_uchar_t         *p;

    p = *b;

    *(p++) = '{';
    node = (sky_json_node_t *) sky_rbtree_min(obj->root.root, &obj->sentinel);
    if (!node) {
        *(p++) = '}';

        *b = p;
        return;
    }
    *(p++) = '"';
    sky_memcpy(p, node->key.data, node->key.len);
    p += node->key.len;
    *(p++) = '"';
    *(p++) = ':';
    switch (node->tag) {
        case SKY_JSON_TAG_NULL:
            sky_memcpy(p, "null", 4);
            p += 4;
            break;
        case SKY_JSON_TAG_VALUE:
            if (node->is_str) {
                *(p++) = '"';
                if (node->encode) {
                    sky_memcpy(p, node->str.data->data, node->str.len);
                    p += node->str.len;
                } else {
                    for (sky_uchar_t *c = node->str.data->data; *c; ++c) {
                        switch (*c) {
                            case '"':
                                *(p++) = '\\';
                                *(p++) = '"';
                                break;
                            case '\\':
                                *(p++) = '\\';
                                *(p++) = '\\';
                                break;
                            case '\b':
                                *(p++) = '\\';
                                *(p++) = 'b';
                                break;
                            case '\f':
                                *(p++) = '\\';
                                *(p++) = 'f';
                                break;
                            case '\n':
                                *(p++) = '\\';
                                *(p++) = 'n';
                                break;
                            case '\r':
                                *(p++) = '\\';
                                *(p++) = 'r';
                                break;
                            case '\t':
                                *(p++) = '\\';
                                *(p++) = 't';
                                break;
                            default:
                                *(p++) = *c;
                                break;
                        }
                    }
                }
                *(p++) = '"';
            } else {
                sky_memcpy(p, node->num.data, node->num.len);
                p += node->num.len;
            }
            break;
        case SKY_JSON_TAG_OBJECT:
            json_object_encode(node->obj, &p);
            break;
        case SKY_JSON_TAG_ARRAY:
            json_array_encode(node->array, &p);
            break;
        default:
            break;
    }

    while ((node = (sky_json_node_t *) sky_rbtree_next(&obj->root, &node->node))) {
        *(p++) = ',';
        *(p++) = '"';
        sky_memcpy(p, node->key.data, node->key.len);
        p += node->key.len;
        *(p++) = '"';
        *(p++) = ':';
        switch (node->tag) {
            case SKY_JSON_TAG_NULL:
                sky_memcpy(p, "null", 4);
                p += 4;
                break;
            case SKY_JSON_TAG_VALUE:
                if (node->is_str) {
                    *(p++) = '"';
                    if (node->encode) {
                        sky_memcpy(p, node->str.data->data, node->str.len);
                        p += node->str.len;
                    } else {
                        for (sky_uchar_t *c = node->str.data->data; *c; ++c) {
                            switch (*c) {
                                case '"':
                                    *(p++) = '\\';
                                    *(p++) = '"';
                                    break;
                                case '\\':
                                    *(p++) = '\\';
                                    *(p++) = '\\';
                                    break;
                                case '\b':
                                    *(p++) = '\\';
                                    *(p++) = 'b';
                                    break;
                                case '\f':
                                    *(p++) = '\\';
                                    *(p++) = 'f';
                                    break;
                                case '\n':
                                    *(p++) = '\\';
                                    *(p++) = 'n';
                                    break;
                                case '\r':
                                    *(p++) = '\\';
                                    *(p++) = 'r';
                                    break;
                                case '\t':
                                    *(p++) = '\\';
                                    *(p++) = 't';
                                    break;
                                default:
                                    *(p++) = *c;
                                    break;
                            }
                        }
                    }
                    *(p++) = '"';
                } else {
                    sky_memcpy(p, node->num.data, node->num.len);
                    p += node->num.len;
                }
                break;
            case SKY_JSON_TAG_OBJECT:
                json_object_encode(node->obj, &p);
                break;
            case SKY_JSON_TAG_ARRAY:
                json_array_encode(node->array, &p);
                break;
            default:
                break;
        }
    }
    *(p++) = '}';
    *b = p;
}

static void
json_array_encode(sky_json_array_t *array, sky_uchar_t **b) {
    sky_json_item_t *item;
    sky_uchar_t     *p;

    p = *b;

    *(p++) = '[';
    if ((item = array->data.next) == &array->data) {
        *(p++) = ']';
        *b = p;
        return;
    }
    switch (item->tag) {
        case SKY_JSON_TAG_NULL:
            sky_memcpy(p, "null", 4);
            p += 4;
            break;
        case SKY_JSON_TAG_VALUE:
            if (item->is_str) {
                *(p++) = '"';
                if (item->encode) {
                    sky_memcpy(p, item->str.data->data, item->str.len);
                    p += item->str.len;
                } else {
                    for (sky_uchar_t *c = item->str.data->data; *c; ++c) {
                        switch (*c) {
                            case '"':
                                *(p++) = '\\';
                                *(p++) = '"';
                                break;
                            case '\\':
                                *(p++) = '\\';
                                *(p++) = '\\';
                                break;
                            case '\b':
                                *(p++) = '\\';
                                *(p++) = 'b';
                                break;
                            case '\f':
                                *(p++) = '\\';
                                *(p++) = 'f';
                                break;
                            case '\n':
                                *(p++) = '\\';
                                *(p++) = 'n';
                                break;
                            case '\r':
                                *(p++) = '\\';
                                *(p++) = 'r';
                                break;
                            case '\t':
                                *(p++) = '\\';
                                *(p++) = 't';
                                break;
                            default:
                                *(p++) = *c;
                                break;
                        }
                    }
                }
                *(p++) = '"';
            } else {
                sky_memcpy(p, item->num.data, item->num.len);
                p += item->num.len;
            }
            break;
        case SKY_JSON_TAG_OBJECT:
            json_object_encode(item->obj, &p);
            break;
        case SKY_JSON_TAG_ARRAY:
            json_array_encode(item->array, &p);
            break;
        default:
            return;
    }
    while ((item = item->next) != &array->data) {
        *(p++) = ',';
        switch (item->tag) {
            case SKY_JSON_TAG_NULL:
                sky_memcpy(p, "null", 4);
                p += 4;
                break;
            case SKY_JSON_TAG_VALUE:
                if (item->is_str) {
                    *(p++) = '"';
                    if (item->encode) {
                        sky_memcpy(p, item->str.data->data, item->str.len);
                        p += item->str.len;
                    } else {
                        for (sky_uchar_t *c = item->str.data->data; *c; ++c) {
                            switch (*c) {
                                case '"':
                                    *(p++) = '\\';
                                    *(p++) = '"';
                                    break;
                                case '\\':
                                    *(p++) = '\\';
                                    *(p++) = '\\';
                                    break;
                                case '\b':
                                    *(p++) = '\\';
                                    *(p++) = 'b';
                                    break;
                                case '\f':
                                    *(p++) = '\\';
                                    *(p++) = 'f';
                                    break;
                                case '\n':
                                    *(p++) = '\\';
                                    *(p++) = 'n';
                                    break;
                                case '\r':
                                    *(p++) = '\\';
                                    *(p++) = 'r';
                                    break;
                                case '\t':
                                    *(p++) = '\\';
                                    *(p++) = 't';
                                    break;
                                default:
                                    *(p++) = *c;
                                    break;
                            }
                        }
                    }
                    *(p++) = '"';
                } else {
                    sky_memcpy(p, item->num.data, item->num.len);
                    p += item->num.len;
                }
                break;
            case SKY_JSON_TAG_OBJECT:
                json_object_encode(item->obj, &p);
                break;
            case SKY_JSON_TAG_ARRAY:
                json_array_encode(item->array, &p);
                break;
            default:
                return;
        }
    }
    *(p++) = ']';
    *b = p;
}


static sky_inline sky_uint32_t
json_hash(sky_uchar_t *str) {
    sky_uint32_t seed = 131; // 31 131 1313 13131 131313 etc..
    sky_uint32_t hash = 0;


    while (*str)
    {
        hash = hash * seed + (*str++);
    }

    return (hash & 0x7FFFFFFF);
}


static sky_inline void
json_object_push_parent_size(sky_json_object_t *obj, sky_int64_t size) {
    void        *node;
    sky_uint32_t type;

    type = obj->is_obj;
    node = obj->parent;
    while (node) {
        if (type) {
            ((sky_json_object_t *) node)->mem_size += size;
            type = ((sky_json_object_t *) node)->is_obj;
            node = ((sky_json_object_t *) node)->parent;
        } else {
            ((sky_json_array_t *) node)->mem_size += size;
            type = ((sky_json_array_t *) node)->is_obj;
            node = ((sky_json_array_t *) node)->parent;
        }
    }
}

static sky_inline void
json_array_push_parent_size(sky_json_array_t *array, sky_int64_t size) {
    void        *node;
    sky_uint32_t type;

    type = array->is_obj;
    node = array->parent;
    while (node) {
        if (type) {
            ((sky_json_object_t *) node)->mem_size += size;
            type = ((sky_json_object_t *) node)->is_obj;
            node = ((sky_json_object_t *) node)->parent;
        } else {
            ((sky_json_array_t *) node)->mem_size += size;
            type = ((sky_json_array_t *) node)->is_obj;
            node = ((sky_json_array_t *) node)->parent;
        }
    }
}