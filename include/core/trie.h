//
// Created by beliefsky on 2019/11/14.
//

#ifndef SKY_TRIE_H
#define SKY_TRIE_H

#include "./palloc.h"
#include "./string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_trie_s sky_trie_t;


sky_trie_t *sky_trie_create(sky_pool_t *pool);

void sky_trie_put(sky_trie_t *trie, const sky_str_t *key, void *value);

void *sky_trie_find(const sky_trie_t *trie, const sky_str_t *key);

void *sky_trie_contains(const sky_trie_t *trie, const sky_str_t *key);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TRIE_H
