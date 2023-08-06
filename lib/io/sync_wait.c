//
// Created by beliefsky on 2023/7/22.
//
#include <io/sync_wait.h>
#include <core/coro.h>

struct sky_sync_wait_s {
    sky_sync_wait_pt call;
    sky_sync_wait_finish_pt finish;
    sky_coro_t *coro;
    void *data;
    void *att_data;
    sky_bool_t wait;
};

static sky_usize_t coro_process(sky_coro_t *coro, void *data);


sky_api sky_bool_t
sky_sync_wait_create(const sky_sync_wait_pt cb, const sky_sync_wait_finish_pt finish, void *const data) {
    sky_coro_t *const coro = sky_coro_new();
    if (sky_unlikely(!coro)) {
        return false;
    }

    sky_sync_wait_t *const wait = sky_coro_malloc(coro, sizeof(sky_sync_wait_t));
    wait->call = cb;
    wait->finish = finish;
    wait->coro = coro;
    wait->data = data;
    wait->wait = false;

    sky_coro_set(coro, coro_process, wait);
    sky_sync_wait_resume(wait, null);

    return true;
}

sky_api sky_bool_t
sky_sync_wait_create_with_stack(
        const sky_sync_wait_pt cb,
        const sky_sync_wait_finish_pt finish,
        void *const data,
        const sky_usize_t stack_size
) {
    sky_coro_t *const coro = sky_coro_new_with_stack(stack_size);
    if (sky_unlikely(!coro)) {
        return false;
    }

    sky_sync_wait_t *const wait = sky_coro_malloc(coro, sizeof(sky_sync_wait_t));
    wait->call = cb;
    wait->finish = finish;
    wait->coro = coro;
    wait->data = data;
    wait->wait = false;

    sky_coro_set(coro, coro_process, wait);
    sky_sync_wait_resume(wait, null);

    return true;
}

sky_api void
sky_sync_wait_resume(sky_sync_wait_t *const wait, void *const att_data) {
    wait->att_data = att_data;

    if (wait->wait) {
        wait->wait = false;
        return;
    }

    if (sky_coro_resume(wait->coro) != SKY_CORO_MAY_RESUME) {
        const sky_sync_wait_finish_pt finish = wait->finish;
        if (finish) {
            sky_coro_destroy(wait->coro);
            return;
        }
        void *const data = wait->data;
        sky_coro_destroy(wait->coro);
        finish(data);
    }
}

sky_api void
sky_sync_wait_yield_before(sky_sync_wait_t *const wait) {
    wait->wait = true;
}


sky_api void *
sky_sync_wait_yield(sky_sync_wait_t *const wait) {
    if (wait->wait) {
        wait->wait = false;
        sky_coro_yield(SKY_CORO_MAY_RESUME);
    }
    return wait->att_data;
}

static sky_usize_t
coro_process(sky_coro_t *const coro, void *const data) {
    (void) coro;

    sky_sync_wait_t *const wait = data;

    wait->call(wait, wait->data);

    return SKY_CORO_FINISHED;
}