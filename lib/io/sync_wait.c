//
// Created by beliefsky on 2023/7/22.
//
#include <io/sync_wait.h>
#include <core/context.h>
#include <core/memory.h>
#include <core/log.h>

struct sky_sync_wait_s {
    sky_bool_t wait;
    sky_bool_t finish;
    sky_context_ref_t context;
    sky_context_ref_t parent;
    sky_sync_wait_pt call;
    void *data;
    void *att_data;
    sky_uchar_t stack[];
};

static void context_process(sky_context_from_t from);

sky_api sky_bool_t
sky_sync_wait_create(const sky_sync_wait_pt cb, void *const data) {
    return sky_sync_wait_create_with_stack(cb, data, 16384 - sizeof(sky_sync_wait_t));
}

sky_api sky_bool_t
sky_sync_wait_create_with_stack(
        const sky_sync_wait_pt cb,
        void *const data,
        const sky_usize_t stack_size
) {
    sky_sync_wait_t *const wait = sky_malloc(sizeof(sky_sync_wait_t) + stack_size);
    wait->wait = false;
    wait->finish = false;
    wait->context = sky_context_make(wait->stack, stack_size, context_process);
    wait->parent = null;
    wait->call = cb;
    wait->data = data;

    const sky_context_from_t from = sky_context_jump(wait->context, wait);
    wait->context = from.context;
    if (wait->finish) {
        sky_free(wait);
    }

    return true;
}

sky_api void
sky_sync_wait_resume(sky_sync_wait_t *const wait, void *const att_data) {
    wait->att_data = att_data;

    if (wait->wait) {
        wait->wait = false;
        return;
    }
    const sky_context_from_t from = sky_context_jump(wait->context, wait);
    wait->context = from.context;
    if (wait->finish) {
        sky_free(wait);
    }
}

sky_api void
sky_sync_wait_yield_before(sky_sync_wait_t *const wait) {
    wait->wait = true;
}


sky_api void *
sky_sync_wait_yield(sky_sync_wait_t *const wait) {
    if (!wait->wait) {
        return wait->att_data;
    }
    wait->wait = false;
    const sky_context_from_t from = sky_context_jump(wait->parent, null);
    wait->parent = from.context;

    return wait->att_data;
}


static void
context_process(sky_context_from_t from) {
    sky_sync_wait_t *const wait = from.data;
    wait->parent = from.context;
    wait->call(wait, wait->data);
    wait->finish = true;
    sky_context_jump(wait->parent, null);
}