//
// Created by weijing on 17-11-24.
//

#include "hash.h"
#include "memory.h"
#include "cpuinfo.h"

void *
sky_hash_find(sky_hash_t *hash, sky_usize_t key, sky_uchar_t *name, sky_usize_t len) {
    sky_usize_t i;
    sky_hash_elt_t *elt;

    elt = hash->buckets[key % hash->size];

    if (!elt) {
        return null;
    }

    while (elt->value) {
        if (len != (sky_usize_t) elt->len) {
            goto next;
        }

        for (i = 0; i < len; i++) {
            if (name[i] != elt->name[i]) {
                goto next;
            }
        }

        return elt->value;

        next:
        elt = (sky_hash_elt_t *) sky_align_ptr(&elt->name[0] + elt->len, sizeof(void *));
    }

    return null;
}


void *
sky_hash_find_wc_head(sky_hash_wildcard_t *hwc, sky_uchar_t *name, sky_usize_t len) {
    void *value;
    sky_usize_t i, n, key;

    n = len;

    while (n) {
        if (name[n - 1] == '.') {
            break;
        }

        n--;
    }

    key = 0;

    for (i = n; i < len; i++) {
        key = sky_hash(key, name[i]);
    }

    value = sky_hash_find(&hwc->hash, key, &name[n], len - n);

    if (value) {

        /*
         * the 2 low bits of value have the special meaning:
         *     00 - value is data pointer for both "example.com"
         *          and "*.example.com";
         *     01 - value is data pointer for "*.example.com" only;
         *     10 - value is pointer to wildcard hash allowing
         *          both "example.com" and "*.example.com";
         *     11 - value is pointer to wildcard hash allowing
         *          "*.example.com" only.
         */

        if ((sky_usize_t) value & 2) {

            if (n == 0) {

                /* "example.com" */

                if ((sky_usize_t) value & 1) {
                    return null;
                }

                hwc = (sky_hash_wildcard_t *) ((sky_usize_t) value & (sky_usize_t) ~3);
                return hwc->value;
            }

            hwc = (sky_hash_wildcard_t *) ((sky_usize_t) value & (sky_usize_t) ~3);

            value = sky_hash_find_wc_head(hwc, name, n - 1);

            if (value) {
                return value;
            }

            return hwc->value;
        }

        if ((sky_usize_t) value & 1) {

            if (n == 0) {

                /* "example.com" */

                return null;
            }

            return (void *) ((sky_usize_t) value & (sky_usize_t) ~3);
        }

        return value;
    }

    return hwc->value;
}


void *
sky_hash_find_wc_tail(sky_hash_wildcard_t *hwc, sky_uchar_t *name, sky_usize_t len) {
    void *value;
    sky_usize_t i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        if (name[i] == '.') {
            break;
        }

        key = sky_hash(key, name[i]);
    }

    if (i == len) {
        return null;
    }

    value = sky_hash_find(&hwc->hash, key, name, i);

    if (value) {

        /*
         * the 2 low bits of value have the special meaning:
         *     00 - value is data pointer;
         *     11 - value is pointer to wildcard hash allowing "example.*".
         */

        if ((sky_usize_t) value & 2) {

            i++;

            hwc = (sky_hash_wildcard_t *) ((sky_usize_t) value & (sky_usize_t) ~3);

            value = sky_hash_find_wc_tail(hwc, &name[i], len - i);

            if (value) {
                return value;
            }

            return hwc->value;
        }

        return value;
    }

    return hwc->value;
}


void *
sky_hash_find_combined(sky_hash_combined_t *hash, sky_usize_t key, sky_uchar_t *name, sky_usize_t len) {
    void *value;

    if (hash->hash.buckets) {
        value = sky_hash_find(&hash->hash, key, name, len);

        if (value) {
            return value;
        }
    }

    if (len == 0) {
        return null;
    }

    if (hash->wc_head && hash->wc_head->hash.buckets) {
        value = sky_hash_find_wc_head(hash->wc_head, name, len);

        if (value) {
            return value;
        }
    }

    if (hash->wc_tail && hash->wc_tail->hash.buckets) {
        value = sky_hash_find_wc_tail(hash->wc_tail, name, len);

        if (value) {
            return value;
        }
    }

    return null;
}

//计算sky_hash_elt_t结构大小，name为sky_hash_elt_t结构指针
#define SKY_HASH_ELT_SIZE(name) (sizeof(void *) + sky_align((name)->key.len + 2, sizeof(void *)))

// 第一个参数hinit是初始化的一些参数的一个集合。 names是初始化一个sky_hash_t所需要的所有<key,value>对的一个数组，而nelts是该数组的个数。
// 备注：我倒是觉得可以直接使用一个ngx_array_t*作为参数呢？
//
//初始化步骤
//1. 遍历待初始化的sky_hash_key_t数组, 保证占用空间最大的sky_hash_elt_t元素可以装进bucket_size大小空间
//2. 预估一个可以装入所有元素的hash表长度start, 判断是否可以将所有元素装进这个size大小的hash表
//3. 装不下, 增加size, 如果size达到max_size仍然不能创建这个hash表, 则失败. 否则确定了要构建的hash表长度(buckets个数)
//4. found:处开始,, 计算所有元素占用总空间, 为hash表的各个bucket分配各自的空间
//5. 将sky_hash_key_t数组元素分别放入对应的bucket中
//
//其中第2步中怎么计算初始的可能hash表的大小start?
//start = nelts / (bucket_size / (2 * sizeof(void *)));
//也即认为一个bucket最多放入的元素个数为bucket_size / (2 * sizeof(void *));
//64位机器上, sizeof(void *) 为8 Bytes,  sizeof(unsigned short)为2Bytes, sizeof(name)为1 Byte, sizeof(sky_hash_elt_t)为16Bytes, 正好与2 * sizeof(void *)相等.
sky_bool_t
sky_hash_init(sky_hash_init_t *hinit, sky_hash_key_t *names, sky_usize_t nelts) {
    sky_uchar_t *elts;
    sky_usize_t len;
    sky_u16_t *test;
    sky_usize_t i, n, key, size, start, bucket_size;
    sky_hash_elt_t *elt, **buckets;

    if (sky_unlikely(!hinit->max_size)) {
        return false;
    }
    if (sky_unlikely(hinit->bucket_size > 65536 - sky_cache_line_size)) {
        return false;
    }

    //检查names数组的每一个元素，判断桶的大小是否够存放key
    for (n = 0; n < nelts; n++) {
        if (sky_unlikely(hinit->bucket_size < SKY_HASH_ELT_SIZE(&names[n]) + sizeof(void *))) {
            //有任何一个元素，桶的大小不够为该元素分配空间，则退出
            return false;
        }
    }

    test = sky_malloc(hinit->max_size * sizeof(sky_u16_t));
    //分配 sizeof(sky_u16_t)*max_size 个字节的空间保存hash数据
    //(该内存分配操作不在sky的内存池中进行，因为test只是临时的)
    if (sky_unlikely(!test)) {
        return false;
    }

    bucket_size = hinit->bucket_size - sizeof(void *);

    start = nelts * bucket_size / (sizeof(void *) << 1);
    start = start ? start : 1;

    if (hinit->max_size > 10000 && nelts && hinit->max_size / nelts < 100) {
        start = hinit->max_size - 1000;
    }

    for (size = start; size <= hinit->max_size; size++) {

        sky_memzero(test, size * sizeof(sky_u16_t));
        //标记1：此块代码是检查bucket大小是否够分配hash数据
        for (n = 0; n < nelts; n++) {
            if (!names[n].key.data) {
                continue;
            }
            //计算key和names中所有name长度，并保存在test[key]中
            key = names[n].key_hash % size;     //若size=1，则key一直为0
            test[key] = (sky_u16_t) (test[key] + SKY_HASH_ELT_SIZE(&names[n]));
            //若超过了桶的大小，则到下一个桶重新计算
            if (test[key] > (sky_u16_t) bucket_size) {
                goto next;
            }
        }

        goto found;

        next:

        continue;
    }

    size = hinit->max_size;

    found:   //找到合适的bucket
    //将test数组前size个元素初始化为sizeof(void *)
    for (i = 0; i < size; i++) {
        test[i] = sizeof(void *);
    }
    /** 标记2：与标记1代码基本相同，但此块代码是再次计算所有hash数据的总长度(标记1的检查已通过)
        但此处的test[i]已被初始化为sizeof(void *)，即相当于后续的计算再加上一个void指针的大小。
     */
    for (n = 0; n < nelts; n++) {
        if (!names[n].key.data) {
            continue;
        }
        //计算key和names中所有name长度，并保存在test[key]中
        key = names[n].key_hash % size; //若size=1，则key一直为0
        len = test[key] + SKY_HASH_ELT_SIZE(&names[n]);
        if (len > 65536 - sky_cache_line_size) {
            sky_free(test);
            return false;
        }
        test[key] = (sky_u16_t) len;
    }
    //计算hash数据的总长度
    len = 0;

    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
            //若test[i]仍为初始化的值为sizeof(void *)，即没有变化，则继续
            continue;
        }
        //对test[i]按ngx_cacheline_size对齐(32位平台，ngx_cacheline_size=32)
        test[i] = (sky_u16_t) (sky_align(test[i], sky_cache_line_size));

        len += test[i];
    }

    if (!hinit->hash) {
        //在内存池中分配hash头及buckets数组(size个ngx_hash_elt_t*结构)
        hinit->hash = sky_pcalloc(hinit->pool, sizeof(sky_hash_wildcard_t) + size * sizeof(sky_hash_elt_t *));
        if (sky_unlikely(!hinit->hash)) {
            sky_free(test);
            return false;
        }

        //计算buckets的启示位置(在ngx_hash_wildcard_t结构之后)
        buckets = (sky_hash_elt_t **) ((sky_uchar_t *) hinit->hash + sizeof(sky_hash_wildcard_t));

    } else {
        //在内存池中分配buckets数组(size个ngx_hash_elt_t*结构)
        buckets = sky_pcalloc(hinit->pool, size * sizeof(sky_hash_elt_t *));
        if (sky_unlikely(!buckets)) {
            sky_free(test);
            return false;
        }
    }

    //接着分配elts，大小为len+ngx_cacheline_size，此处为什么+ngx_cacheline_size？——下面要按ngx_cacheline_size字节对齐
    elts = sky_palloc(hinit->pool, len + sky_cache_line_size);
    if (sky_unlikely(!elts)) {
        sky_free(test);
        return false;
    }

    // 对齐
    elts = sky_align_ptr(elts, sky_cache_line_size);

    //将buckets数组与相应elts对应起来
    for (i = 0; i < size; i++) {
        if (test[i] == sizeof(void *)) {
            continue;
        }

        buckets[i] = (sky_hash_elt_t *) elts;
        elts += test[i];
    }

    for (i = 0; i < size; i++) {
        test[i] = 0;
    }
    //将传进来的每一个hash数据存入hash表
    for (n = 0; n < nelts; n++) {
        if (!names[n].key.data) {
            continue;
        }
        //计算key，即将被hash的数据在第几个bucket，并计算其对应的elts位置
        key = names[n].key_hash % size;
        elt = (sky_hash_elt_t *) ((sky_uchar_t *) buckets[key] + test[key]);
        //对ngx_hash_elt_t结构赋值
        elt->value = names[n].value;
        elt->len = (sky_u16_t) names[n].key.len;

        sky_strlow(elt->name, names[n].key.data, names[n].key.len);
        //计算下一个要被hash的数据的长度偏移
        test[key] = (sky_u16_t) (test[key] + SKY_HASH_ELT_SIZE(&names[n]));
    }

    for (i = 0; i < size; i++) {
        if (!buckets[i]) {
            continue;
        }
        //test[i]相当于所有被hash的数据总长度
        elt = (sky_hash_elt_t *) ((sky_uchar_t *) buckets[i] + test[i]);
        //将每个bucket的最后一个指针大小区域置NULL
        elt->value = null;
    }

    sky_free(test);     //释放该临时空间

    hinit->hash->buckets = buckets;
    hinit->hash->size = size;


    return true;
}


sky_bool_t
sky_hash_wildcard_init(sky_hash_init_t *hinit, sky_hash_key_t *names,
                       sky_u32_t nelts) {
    sky_usize_t len, dot_len;
    sky_usize_t i, n, dot;
    sky_array_t curr_names, next_names;
    sky_hash_key_t *name, *next_name;
    sky_hash_init_t h;
    sky_hash_wildcard_t *wdc;

    if (!sky_array_init(&curr_names, hinit->temp_pool, nelts, sizeof(sky_hash_key_t))) {
        return false;
    }

    if (!sky_array_init(&next_names, hinit->temp_pool, nelts, sizeof(sky_hash_key_t))) {
        return false;
    }

    for (n = 0; n < nelts; n = i) {

        dot = 0;

        for (len = 0; len < names[n].key.len; len++) {
            if (names[n].key.data[len] == '.') {
                dot = 1;
                break;
            }
        }

        name = sky_array_push(&curr_names);
        if (!name) {
            return false;
        }

        name->key.len = len;
        name->key.data = names[n].key.data;
        name->key_hash = hinit->key(name->key.data, name->key.len);
        name->value = names[n].value;

        dot_len = len + 1;

        if (dot) {
            len++;
        }

        next_names.nelts = 0;

        if (names[n].key.len != len) {
            next_name = sky_array_push(&next_names);
            if (!next_name) {
                return false;
            }

            next_name->key.len = names[n].key.len - len;
            next_name->key.data = names[n].key.data + len;
            next_name->key_hash = 0;
            next_name->value = names[n].value;
        }

        for (i = n + 1; i < nelts; i++) {
            if (!sky_str_len_equals_unsafe(names[n].key.data, names[i].key.data, len)) {
                break;
            }

            if (!dot
                && names[i].key.len > len
                && names[i].key.data[len] != '.') {
                break;
            }

            next_name = sky_array_push(&next_names);
            if (!next_name) {
                return false;
            }

            next_name->key.len = names[i].key.len - dot_len;
            next_name->key.data = names[i].key.data + dot_len;
            next_name->key_hash = 0;
            next_name->value = names[i].value;
        }

        if (next_names.nelts) {

            h = *hinit;
            h.hash = null;

            if (!sky_hash_wildcard_init(&h, (sky_hash_key_t *) next_names.elts, next_names.nelts)) {
                return false;
            }

            wdc = (sky_hash_wildcard_t *) h.hash;

            if (names[n].key.len == len) {
                wdc->value = names[n].value;
            }

            name->value = (void *) ((sky_usize_t) wdc | (dot ? 3 : 2));

        } else if (dot) {
            name->value = (void *) ((sky_usize_t) name->value | 1);
        }
    }

    if (!sky_hash_init(hinit, (sky_hash_key_t *) curr_names.elts, curr_names.nelts)) {
        return false;
    }

    return true;
}


//计算hash值
sky_usize_t
sky_hash_key(sky_uchar_t *data, sky_usize_t len) {
    sky_usize_t i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = sky_hash(key, data[i]);
    }

    return key;
}

//计算hash值
sky_usize_t
sky_hash_key_lc(sky_uchar_t *data, sky_usize_t len) {
    sky_usize_t i, key;

    key = 0;

    for (i = 0; i < len; i++) {
        key = sky_hash(key, sky_tolower(data[i]));
    }

    return key;
}


//小写化的同时计算出hash值
sky_usize_t
sky_hash_strlow(sky_uchar_t *dst, sky_uchar_t *src, sky_usize_t n) {
    sky_usize_t key;

    key = 0;

    while (n--) {
        *dst = sky_tolower(*src);
        key = sky_hash(key, *dst);
        dst++;
        src++;
    }

    return key;
}


sky_bool_t
sky_hash_keys_array_init(sky_hash_keys_arrays_t *ha, sky_usize_t type) {
    sky_u32_t asize;

    if (type == SKY_HASH_SMALL) {
        asize = 4;
        ha->hsize = 107;

    } else {
        asize = SKY_HASH_LARGE_ASIZE;
        ha->hsize = SKY_HASH_LARGE_HSIZE;
    }

    if (!sky_array_init(&ha->keys, ha->temp_pool, asize, sizeof(sky_hash_key_t))) {
        return false;
    }

    if (!sky_array_init(&ha->dns_wc_head, ha->temp_pool, asize, sizeof(sky_hash_key_t))) {
        return false;
    }

    if (!sky_array_init(&ha->dns_wc_tail, ha->temp_pool, asize, sizeof(sky_hash_key_t))) {
        return false;
    }

    ha->keys_hash = sky_pcalloc(ha->temp_pool, sizeof(sky_array_t) * ha->hsize);
    if (!ha->keys_hash) {
        return false;
    }

    ha->dns_wc_head_hash = sky_pcalloc(ha->temp_pool, sizeof(sky_array_t) * ha->hsize);
    if (!ha->dns_wc_head_hash) {
        return false;
    }

    ha->dns_wc_tail_hash = sky_pcalloc(ha->temp_pool, sizeof(sky_array_t) * ha->hsize);
    if (!ha->dns_wc_tail_hash) {
        return false;
    }

    return true;
}


sky_i8_t
sky_hash_add_key(sky_hash_keys_arrays_t *ha, sky_str_t *key, void *value, sky_usize_t flags) {
    sky_usize_t len;
    sky_uchar_t *p;
    sky_str_t *name;
    sky_usize_t i, k, n, skip, last;
    sky_array_t *keys, *hwc;
    sky_hash_key_t *hk;

    last = key->len;

    if (flags & SKY_HASH_WILDCARD_KEY) {

        /*
         * supported wildcards:
         *     "*.example.com", ".example.com", and "www.example.*"
         */

        n = 0;

        for (i = 0; i < key->len; i++) {

            if (key->data[i] == '*') {
                if (++n > 1) {
                    return -1;
                }
            }

            if (key->data[i] == '.' && key->data[i + 1] == '.') {
                return -1;
            }

            if (key->data[i] == '\0') {
                return -1;
            }
        }

        if (key->len > 1 && key->data[0] == '.') {
            skip = 1;
            goto wildcard;
        }

        if (key->len > 2) {

            if (key->data[0] == '*' && key->data[1] == '.') {
                skip = 2;
                goto wildcard;
            }

            if (key->data[i - 2] == '.' && key->data[i - 1] == '*') {
                skip = 0;
                last -= 2;
                goto wildcard;
            }
        }

        if (n) {
            return -1;
        }
    }

    /* exact hash */

    k = 0;

    for (i = 0; i < last; i++) {
        if (!(flags & SKY_HASH_READONLY_KEY)) {
            key->data[i] = sky_tolower(key->data[i]);
        }
        k = sky_hash(k, key->data[i]);
    }

    k %= ha->hsize;

    /* check conflicts in exact hash */

    name = ha->keys_hash[k].elts;

    if (name) {
        for (i = 0; i < ha->keys_hash[k].nelts; i++) {
            if (last != name[i].len) {
                continue;
            }

            if (sky_str_len_equals_unsafe(key->data, name[i].data, last)) {
                return false;
            }
        }

    } else {
        if (!sky_array_init(&ha->keys_hash[k], ha->temp_pool, 4, sizeof(sky_str_t))) {
            return false;
        }
    }

    name = sky_array_push(&ha->keys_hash[k]);
    if (!name) {
        return false;
    }

    *name = *key;

    hk = sky_array_push(&ha->keys);
    if (!hk) {
        return false;
    }

    hk->key = *key;
    hk->key_hash = sky_hash_key(key->data, last);
    hk->value = value;

    return true;


    wildcard:

    /* wildcard hash */

    k = sky_hash_strlow(&key->data[skip], &key->data[skip], last - skip);

    k %= ha->hsize;

    if (skip == 1) {

        /* check conflicts in exact hash for ".example.com" */

        name = ha->keys_hash[k].elts;

        if (name) {
            len = last - skip;

            for (i = 0; i < ha->keys_hash[k].nelts; i++) {
                if (len != name[i].len) {
                    continue;
                }

                if (sky_str_len_equals_unsafe(&key->data[1], name[i].data, len)) {
                    return false;
                }
            }

        } else {
            if (!sky_array_init(&ha->keys_hash[k], ha->temp_pool, 4, sizeof(sky_str_t))) {
                return false;
            }
        }

        name = sky_array_push(&ha->keys_hash[k]);
        if (!name) {
            return false;
        }

        name->len = last - 1;
        name->data = sky_pnalloc(ha->temp_pool, name->len);
        if (!name->data) {
            return false;
        }

        sky_memcpy(name->data, &key->data[1], name->len);
    }


    if (skip) {

        /*
         * convert "*.example.com" to "com.example.\0"
         *      and ".example.com" to "com.example\0"
         */

        p = sky_pnalloc(ha->temp_pool, last);
        if (!p) {
            return false;
        }

        len = 0;
        n = 0;

        for (i = last - 1; i; i--) {
            if (key->data[i] == '.') {
                sky_memcpy(&p[n], &key->data[i + 1], len);
                n += len;
                p[n++] = '.';
                len = 0;
                continue;
            }

            len++;
        }

        if (len) {
            sky_memcpy(&p[n], &key->data[1], len);
            n += len;
        }

        p[n] = '\0';

        hwc = &ha->dns_wc_head;
        keys = &ha->dns_wc_head_hash[k];

    } else {

        /* convert "www.example.*" to "www.example\0" */

        last++;

        p = sky_pnalloc(ha->temp_pool, last);
        if (!p) {
            return false;
        }

        sky_memcpy(p, key->data, last + 1);

        hwc = &ha->dns_wc_tail;
        keys = &ha->dns_wc_tail_hash[k];
    }


    /* check conflicts in wildcard hash */

    name = keys->elts;

    if (name) {
        len = last - skip;

        for (i = 0; i < keys->nelts; i++) {
            if (len != name[i].len) {
                continue;
            }

            if (sky_str_len_equals_unsafe(key->data + skip, name[i].data, len)) {
                return false;
            }
        }

    } else {
        if (!sky_array_init(keys, ha->temp_pool, 4, sizeof(sky_str_t))) {
            return false;
        }
    }

    name = sky_array_push(keys);
    if (!name) {
        return false;
    }

    name->len = last - skip;
    name->data = sky_pnalloc(ha->temp_pool, name->len);
    if (!name->data) {
        return false;
    }

    sky_memcpy(name->data, key->data + skip, name->len);


    /* add to wildcard hash */

    hk = sky_array_push(hwc);
    if (!hk) {
        return false;
    }

    hk->key.len = last - 1;
    hk->key.data = p;
    hk->key_hash = 0;
    hk->value = value;

    return true;
}
