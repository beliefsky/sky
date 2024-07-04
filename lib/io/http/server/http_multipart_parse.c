//
// Created by weijing on 2024/7/3.
//

#include "./http_server_common.h"
#include "../http_parse_common.h"

#include <core/memory.h>
#include <core/string_buf.h>

typedef enum {
    sw_start = 0,
    sw_start_p1,
    sw_start_p2,
    sw_boundary,
    sw_boundary_cr,
    sw_boundary_n2,
    sw_boundary_lf,
    sw_boundary_end_cr,
    sw_boundary_end_lf,
    sw_start_header_init,
    sw_start_header,
    sw_header_name,
    sw_header_value_first,
    sw_header_value,
    sw_header_lf,
    sw_header_complete_cr,
    sw_header_complete_lf,
    sw_data,
    sw_data_maybe_boundary,

    sw_error

} http_multipart_status_t;


sky_api sky_http_multipart_parser_t *
sky_http_multipart_parse_create(sky_pool_t *const pool, const sky_str_t *const boundary) {
    if (!boundary || !boundary->len) {
        return null;
    }
    const sky_usize_t boundary_len = boundary->len + 4; // \r\n--boundary

    sky_http_multipart_parser_t *const stream = sky_palloc(
            pool,
            sizeof(sky_http_multipart_parser_t) + sizeof(sky_str_buf_t) + boundary_len
    );
    stream->boundary_len = boundary_len;
    stream->boundary_offset = 0;
    stream->headers = null;
    stream->content_type = null;
    stream->content_disposition = null;
    stream->pool = pool;
    stream->state = sw_start;
    stream->str_buf = stream->boundary + boundary_len;

    sky_memcpy4(stream->boundary, "\r\n--");
    sky_memcpy(stream->boundary + 4, boundary->data, boundary->len);

    return stream;
}

sky_api sky_usize_t
sky_http_multipart_parse_exec(
        sky_http_multipart_parser_t *const m,
        const sky_uchar_t *buf,
        sky_usize_t size
) {
    if (sky_unlikely(!size)) {
        m->result = MULTIPART_PENDING;
        return 0;
    }

    union {
        sky_usize_t usize;
        sky_isize_t isize;
    } tmp;

    sky_usize_t read_n = 0;
    sky_u32_t state = m->state;

    switch (state) {
        case sw_start_p2: {
            if (sky_unlikely(*buf != '-')) {
                goto error;
            }
            ++read_n;
            state = sw_boundary;
            if (size == 1) {
                m->result = MULTIPART_PENDING;
                break;
            }
            ++buf;
            --size;

            goto sw_boundary;
        }
        case sw_boundary_n2: {
            if (sky_unlikely(*buf != '-')) {
                goto error;
            }
            ++read_n;
            state = sw_boundary_end_cr;
            if (size == 1) {
                m->result = MULTIPART_PENDING;
                break;
            }
            ++buf;
            --size;
        }
        case sw_boundary_end_cr: {
            sw_boundary_end_cr:

            if (size == 1) {
                if (sky_likely(*buf == '\r')) {
                    ++read_n;
                    state = sw_boundary_end_lf;
                    m->result = MULTIPART_PENDING;
                } else {
                    goto error;
                }
                break;
            }
            if (sky_likely(sky_str2_cmp(buf, '\r', '\n'))) {
                read_n += 2;
                state = sw_start;
                m->result = MULTIPART_END;
            } else {
                goto error;
            }
            break;
        }
        case sw_boundary_end_lf: {
            if (sky_likely(*buf == '\n')) {
                ++read_n;
                state = sw_start;
                m->result = MULTIPART_END;
            } else {
                goto error;
            }
            break;
        }
        case sw_boundary_lf: {
            if (sky_likely(*buf == '\n')) {
                ++read_n;
                state = sw_start_header_init;

                if (size == 1) {
                    m->result = MULTIPART_PENDING;
                } else {
                    goto sw_start_header_init;
                }

            } else {
                goto error;
            }
            break;
        }
        case sw_start:
        case sw_start_p1: {
            if (size == 1) {
                if (sky_likely(*buf == '-')) {
                    ++read_n;
                    state = sw_start_p2;
                    m->result = MULTIPART_PENDING;
                } else {
                    goto error;
                }
                break;
            }
            if (sky_unlikely(!sky_str2_cmp(buf, '-', '-'))) {
                goto error;
            }

            read_n += 2;
            state = sw_boundary;

            m->boundary_offset = 4;
            if (size == 2) { // 刚好读完的可能性
                m->result = MULTIPART_PENDING;
                break;
            }
            buf += 2;
            size -= 2;
        }
        case sw_boundary: {
            sw_boundary:

            tmp.usize = m->boundary_len - m->boundary_offset;
            if (size <= tmp.usize) {
                if (sky_likely(sky_str_len_unsafe_equals(buf, m->boundary + m->boundary_offset, size))) {
                    read_n += size;
                    if (size == tmp.usize) {
                        m->boundary_offset = 0;
                        state = sw_boundary_cr;
                    } else {
                        m->boundary_offset += size;
                    }
                    m->result = MULTIPART_PENDING;
                } else {
                    goto error;
                }
                break;
            }

            if (sky_unlikely(!sky_str_len_unsafe_equals(
                    buf,
                    m->boundary + m->boundary_offset,
                    tmp.usize
            ))) {
                goto error;
            }
            m->boundary_offset = 0;

            read_n += tmp.usize;

            buf += tmp.usize;
            size -= tmp.usize;
        }
        case sw_boundary_cr: {
            sw_boundary_cr:

            if (size == 1) {
                if (*buf == '\r') {
                    ++read_n;
                    state = sw_boundary_lf;
                    m->result = MULTIPART_PENDING;
                } else if (*buf == '-') {
                    ++read_n;
                    state = sw_boundary_n2;
                    m->result = MULTIPART_PENDING;
                } else {
                    goto error;
                }
                break;
            }
            if (sky_str2_cmp(buf, '\r', '\n')) {
                state = sw_start_header_init;
            } else if (sky_str2_cmp(buf, '-', '-')) {
                state = sw_boundary_end_cr;
                read_n += 2;
                if (size == 2) {
                    m->result = MULTIPART_PENDING;
                    break;
                }
                buf += 2;
                size -= 2;
                goto sw_boundary_end_cr;
            } else {
                goto error;
            }
            read_n += 2;
            if (size == 2) {
                m->result = MULTIPART_PENDING;
                break;
            }
            buf += 2;
            size -= 2;
        }
        case sw_start_header_init: {
            sw_start_header_init:

            m->headers = sky_list_create(m->pool, 4, sizeof(sky_http_header_t));
            m->content_type = null;
            m->content_disposition = null;
        }
        case sw_start_header: {
            sw_start_header:

            sky_str_buf_init2(m->str_buf, m->pool, 32);
            state = sw_header_name;
        }

        case sw_header_name: {
            tmp.isize = parse_token((sky_uchar_t *) buf, buf + size, ':');
            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(tmp.isize == -2)) {
                    goto error;
                }
                read_n += size;
                sky_str_buf_append_str_len(m->str_buf, buf, size);
                m->result = MULTIPART_PENDING;
                break;
            }
            state = sw_header_value_first;

            tmp.usize = (sky_usize_t) tmp.isize;
            sky_str_buf_append_str_len(m->str_buf, buf, tmp.usize);
            sky_str_buf_build(m->str_buf, &m->tmp_header_name);

            ++tmp.usize;
            read_n += tmp.usize;
            if (tmp.usize == size) {
                m->result = MULTIPART_PENDING;
                break;
            }

            buf += tmp.usize;
            size -= tmp.usize;
        }
        case sw_header_value_first: {
            if (*buf == ' ') {
                do {
                    ++read_n;
                    if (size == 1) {
                        m->result = MULTIPART_PENDING;
                        break;
                    }
                    ++buf;
                    --size;

                } while (*buf == ' ');
            }
            state = sw_header_value;

            sky_str_buf_init2(m->str_buf, m->pool, 32);
        }
        case sw_header_value: {
            tmp.isize = find_header_line((sky_uchar_t *) buf, buf + size);
            if (sky_unlikely(tmp.isize < 0)) {
                if (sky_unlikely(tmp.isize == -2)) {
                    goto error;
                }
                read_n += size;
                sky_str_buf_append_str_len(m->str_buf, buf, size);

                m->result = MULTIPART_PENDING;
                break;
            }
            tmp.usize = (sky_usize_t) tmp.isize;
            if (sky_unlikely(*(buf + tmp.usize) != '\r')) {
                goto error;
            }
            sky_str_buf_append_str_len(m->str_buf, buf, tmp.usize);

            ++tmp.usize;
            buf += tmp.usize;
            read_n += tmp.usize;
            if (tmp.usize == size) {
                state = sw_header_lf;
                m->result = MULTIPART_PENDING;
                break;
            }
            size -= tmp.usize;
        }
        case sw_header_lf: {
            if (sky_unlikely(*buf != '\n')) {
                state = sw_error;
                m->result = MULTIPART_ERROR;
                break;
            }
            ++read_n;
            state = sw_header_complete_cr;

            sky_http_header_t *const h = sky_list_push(m->headers);
            h->key = m->tmp_header_name;
            sky_str_buf_build(m->str_buf, &h->val);

            if (size == 1) {
                m->result = MULTIPART_PENDING;
                break;
            }
            ++buf;
            --size;
        }
        case sw_header_complete_cr: {
            if (*buf != '\r') {
                goto sw_start_header;
            }
            ++read_n;
            state = sw_header_complete_lf;
            if (size == 1) {
                m->result = MULTIPART_PENDING;
                break;
            }
            ++buf;
            --size;
        }
        case sw_header_complete_lf: {
            if (sky_unlikely(*buf != '\n')) {
                goto error;
            }
            ++read_n;
            m->result = MULTIPART_HEADERS;
            state = sw_data;
            break;
        }
        case sw_data: {
            m->data = buf;
            m->data_size = 0;

            sw_data:

            tmp.isize = sky_str_len_index_char(buf, size, '\r');
            if (tmp.isize == -1) {
                read_n += size;
                m->boundary_offset = 0;
                m->data_size += size;
                m->result = MULTIPART_DATA;
                break;
            }

            tmp.usize = (sky_usize_t) tmp.isize;
            ++tmp.usize;
            read_n += tmp.usize;

            m->data_size += tmp.usize - 1;
            m->boundary_offset = 1;

            if (tmp.usize == size) {
                state = sw_data_maybe_boundary;
                m->result = m->data_size ? MULTIPART_DATA : MULTIPART_PENDING;
                break;
            }
            buf += tmp.usize;
            size -= tmp.usize;
            // 此处必定 size >= 1
            sky_uchar_t *const p = m->boundary + 1;
            const sky_usize_t len = m->boundary_len - 1;

            if (size < len) {
                for (tmp.usize = 0; tmp.usize < size; ++tmp.usize) {
                    if (buf[tmp.usize] != p[tmp.usize]) { //不匹配，也应考虑是 \r的可能
                        read_n += tmp.usize;
                        buf += tmp.usize;
                        size -= tmp.usize;

                        m->data_size += tmp.usize + 1;

                        goto sw_data;
                    }
                }
                read_n += size;
                m->boundary_offset += size;

                state = sw_data_maybe_boundary;
            } else {
                for (tmp.usize = 0; tmp.usize < len; ++tmp.usize) {
                    if (buf[tmp.usize] != p[tmp.usize]) { //不匹配，也应考虑是 \r的可能
                        read_n += tmp.usize;
                        buf += tmp.usize;
                        size -= tmp.usize;

                        m->data_size += tmp.usize + 1;

                        goto sw_data;
                    }
                }
                read_n += len;
                m->boundary_offset = 0;
                state = sw_boundary_cr;
            }
            m->result = m->data_size ? MULTIPART_DATA : MULTIPART_PENDING;

            break;
        }
        case sw_data_maybe_boundary: {
            sky_uchar_t *const p = m->boundary + m->boundary_offset;
            const sky_usize_t len = m->boundary_len - m->boundary_offset;
            if (size < len) {
                for (tmp.usize = 0; tmp.usize < size; ++tmp.usize) {
                    if (buf[tmp.usize] != p[tmp.usize]) {
                        read_n += tmp.usize;

                        m->data = m->boundary;
                        m->data_size = m->boundary_offset + tmp.usize;

                        state = sw_data;
                        m->result = MULTIPART_DATA;

                        goto down;
                    }
                }
                read_n += size;

                m->boundary_offset += size;
                m->result = MULTIPART_PENDING;
                break;
            }

            for (tmp.usize = 0; tmp.usize < len; ++tmp.usize) {
                if (buf[tmp.usize] != p[tmp.usize]) { //不匹配，也应考虑是 \r的可能
                    read_n += tmp.usize;

                    m->data = m->boundary;
                    m->data_size = m->boundary_offset + tmp.usize;

                    state = sw_data;
                    m->result = MULTIPART_DATA;

                    goto down;
                }
            }
            read_n += len;
            m->boundary_offset = 0;
            state = sw_boundary_cr;
            if (size == len) {
                break;
            }
            buf += len;
            size -= len;

            goto sw_boundary_cr;
        }
        default:
            m->result = MULTIPART_ERROR;
            break;
    }

    down:
    m->state = state;
    return read_n;

    error:
    m->state = sw_error;
    m->result = MULTIPART_ERROR;
    return 0;
}

sky_api sky_http_multipart_parser_t *
sky_http_req_body_parse_multipart(sky_http_request_t *const r) {
    sky_str_t *const content_type = sky_http_req_content_type(r);
    if (!content_type || !sky_str_starts_with(content_type, sky_str_line("multipart/form-data;"))) {
        return null;
    }
    sky_usize_t boundary_len = content_type->len - (sizeof("multipart/form-data;") - 1);
    if (sky_unlikely(boundary_len < (sizeof("boundary=") - 1))) {
        return null;
    }
    sky_uchar_t *boundary = content_type->data + (sizeof("multipart/form-data;") - 1);
    if (*boundary == ' ') {
        ++boundary;
        --boundary_len;
        if (*boundary == ' ') {
            do {
                ++boundary;
                --boundary_len;
            } while (*boundary == ' ');
        }
    }
    if (sky_unlikely(!sky_str_len_starts_with(boundary, boundary_len, sky_str_line("boundary=")))) {
        return null;
    }
    boundary += sizeof("boundary=") - 1;
    boundary_len -= sizeof("boundary=") - 1;

    const sky_str_t result = {
            .data = boundary,
            .len= boundary_len
    };

    return sky_http_multipart_parse_create(sky_http_req_pool(r), &result);
}
