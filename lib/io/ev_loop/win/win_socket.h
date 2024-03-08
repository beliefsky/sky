//
// Created by weijing on 2024/3/6.
//

#ifndef SKY_WIN_SOCKET_H
#define SKY_WIN_SOCKET_H

#ifdef __WINNT__

#include <io/ev_loop.h>

#if sky_has_include(<ioapiset.h>)

#define EVENT_USE_IOCP

#include <core/memory.h>
#include <ioapiset.h>
#include <winsock2.h>

#define EV_REQ_TCP_CONNECT      SKY_U8(1)
#define EV_REQ_TCP_WRITE        SKY_U8(2)
#define EV_REQ_TCP_READ         SKY_U8(3)

typedef struct sky_ev_block_s sky_ev_block_t;

typedef void (*event_req_pt)(sky_ev_t *ev, sky_ev_req_t *req, sky_bool_t success);

typedef void (*sky_ev_connect_pt)(sky_ev_t *ev, sky_bool_t success);

typedef void (*sky_ev_rw_pt)(sky_ev_t *ev, sky_usize_t n);


struct sky_ev_loop_s {
    HANDLE iocp;
    sky_ev_block_t *current_block;
};


struct sky_ev_req_s {
    OVERLAPPED overlapped;
    union {
        sky_ev_connect_pt connect;
        sky_ev_rw_pt rw;
    } cb;
    sky_ev_block_t *block;
    sky_usize_t bytes;
    sky_u8_t type; // send, connect,
};

struct sky_ev_block_s {
    sky_u32_t count;
    sky_u32_t free_size;
    sky_uchar_t data[];
};


void event_on_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req, sky_bool_t success);

void event_on_tcp_write(sky_ev_t *ev, sky_ev_req_t *req, sky_bool_t success);

void event_on_tcp_read(sky_ev_t *ev, sky_ev_req_t *req, sky_bool_t success);


static sky_inline sky_bool_t
get_extension_function(sky_socket_t socket, GUID guid, void **target) {
    DWORD bytes;

    return WSAIoctl(
            socket,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guid,
            sizeof(GUID),
            target,
            sizeof(void *),
            &bytes,
            null,
            null
    ) != SOCKET_ERROR;
}

static sky_inline void *
event_req_get(sky_ev_loop_t *ev_loop, sky_u32_t size) {
    sky_ev_block_t *block = ev_loop->current_block;

    if (!block || block->free_size < size) {
        block = sky_malloc(16384);
        block->free_size = SKY_U32(16384) - size - sizeof(sky_ev_block_t);
        block->count = 1;
        ev_loop->current_block = block;
    } else {
        block->free_size -= size;
        ++block->count;
    }
    sky_ev_req_t *const result = (sky_ev_req_t *) (block->data + block->free_size);
    result->block = block;

    return result;
}

static sky_inline void
event_req_release(sky_ev_loop_t *ev_loop, sky_ev_req_t *req) {
    sky_ev_block_t *const block = req->block;

    req->block = null;
    if (!(--block->count) && block != ev_loop->current_block) {
        sky_free(block);
    }
}


#else

#error Unsupported platform.

#endif

#endif

#endif //SKY_WIN_SOCKET_H
