//
// Created by weijing on 17-11-10.
//

#ifndef SKY_ARRAY_H
#define SKY_ARRAY_H

#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    // elts指向数组的首地址
    void *elts;
    // nelts是数组中已经使用的元素个数
    sky_uint32_t nelts;
    // 每个数组元素占用的内存大小
    sky_size_t size;
    // 当前数组中能够容纳元素个数的总大小
    sky_uint32_t nalloc;
    // 内存池对象
    sky_pool_t *pool;
} sky_array_t;

/*
从内存池中创建n个元素的数组，元素大小为size
创建一个新的数组对象，并返回这个对象。

p:	数组分配内存使用的内存池；
n:	数组的初始容量大小，即在不扩容的情况下最多可以容纳的元素个数。
size:	单个元素的大小，单位是字节。

注意事项: 由于使用ngx_palloc分配内存，数组在扩容时，旧的内存不会被释放，会造成内存的浪费。
因此，最好能提前规划好数组的容量，在创建或者初始化的时候一次搞定，避免多次扩容，造成内存浪费。
 */
sky_array_t *sky_array_create(sky_pool_t *p, sky_uint32_t n, sky_size_t size);

// 销毁该数组对象，并释放其分配的内存回内存池。
void sky_array_destroy(sky_array_t *a);

// 在数组a上新追加一个元素，并返回指向新元素的指针。
// 需要把返回的指针使用类型转换，转换为具体的类型，然后再给新元素本身或者是各字段（如果数组的元素是复杂类型）赋值。
// 如果数组已满，则重新分配两倍（nalloc*size)的内存空间，且nalloc更新为2*nalloc
void *sky_array_push(sky_array_t *a);

// 返回将要添加n个元素到数组中其首个元素的地址
void *sky_array_push_n(sky_array_t *a, sky_uint32_t n);

// 如果一个数组对象是被分配在堆上的，那么当调用ngx_array_destroy销毁以后，如果想再次使用，就可以调用此函数。
// 如果一个数组对象是被分配在栈上的，那么就需要调用此函数，进行初始化的工作以后，才可以使用。
static sky_inline sky_bool_t
sky_array_init(sky_array_t *array, sky_pool_t *pool, sky_uint32_t n, sky_size_t size) {
    /*
     * set "array->nelts" before "array->elts", otherwise MSVC thinks
     * that "array->nelts" may be used without having been initialized
     */
    array->nelts = 0x0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;
    array->elts = sky_palloc(pool, n * size);
    if (sky_unlikely(!array->elts)) {
        return false;
    }
    return true;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_ARRAY_H
