//
// Created by beliefsky on 2023/4/26.
//

#include "dns_protocol.h"
#include "../../core/memory.h"
#include "../../core/string_buf.h"
#include "../inet.h"
#include "../../core/log.h"


typedef struct {
    sky_pool_t *pool;
    const sky_uchar_t *start;
    const sky_uchar_t **buf_ref;
    sky_u32_t *size_ref;
    sky_u32_t size;
} buf_ref_t;

typedef struct {
    sky_pool_t *pool;
    const sky_uchar_t *start;
    const sky_uchar_t *buf;
    sky_u32_t size;
    sky_u32_t start_size;
} buf_position_t;

static sky_bool_t decode_question(
        sky_dns_question_t *q,
        buf_ref_t *ref,
        sky_u16_t qn
);

static sky_bool_t decode_answer(
        sky_dns_answer_t *a,
        buf_ref_t *ref,
        sky_u16_t an
);

static sky_bool_t decode_name(sky_str_buf_t *str_buf, buf_ref_t *ref);

static sky_bool_t decode_name_offset(sky_str_buf_t *str_buf, buf_position_t *p);

static sky_bool_t dns_name_is_ptr(sky_u8_t v);

static sky_u16_t dns_name_ptr_offset(sky_u16_t v);

sky_u32_t sky_dns_encode_size(const sky_dns_packet_t *packet) {
    sky_u16_t i;
    sky_u32_t n = 12;
    {
        const sky_dns_question_t *question = packet->questions;
        for (i = packet->header.qd_count; i > 0; --i, ++question) {
            n += question->name_len;
            n += 4;
        }
    }


    return n;
}

sky_i32_t
sky_dns_encode(const sky_dns_packet_t *packet, sky_uchar_t *buf, sky_u32_t size) {
    sky_u32_t n = 12;
    sky_u16_t i;

    if (sky_unlikely(size < 12)) {
        return -1;
    }
    {
        const sky_dns_header_t *header = &packet->header;
        *(sky_u16_t *) buf = sky_htons(header->id);
        buf += 2;
        *(sky_u16_t *) buf = sky_htons(header->flags);
        buf += 2;
        *(sky_u16_t *) buf = sky_htons(header->qd_count);
        buf += 2;
        *(sky_u16_t *) buf = sky_htons(header->an_count);
        buf += 2;
        *(sky_u16_t *) buf = sky_htons(header->ns_count);
        buf += 2;
        *(sky_u16_t *) buf = sky_htons(header->ar_count);
        buf += 2;

    }


    {
        const sky_dns_question_t *question = packet->questions;
        for (i = packet->header.qd_count; i > 0; --i, ++question) {
            sky_memcpy(buf, question->name, question->name_len);
            buf += question->name_len;
            n += question->name_len;

            *(sky_u16_t *) buf = sky_htons(question->type);
            buf += 2;
            *(sky_u16_t *) buf = sky_htons(question->clazz);
            buf += 2;


            n += 4;
        }
    }


    return (sky_i32_t) n;
}

sky_bool_t
sky_dns_decode_header(sky_dns_packet_t *packet, const sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 12)) {
        return false;
    }
    sky_dns_header_t *header = &packet->header;

    header->id = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    header->flags = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    header->qd_count = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    header->an_count = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    header->ns_count = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    header->ar_count = sky_htons(*(sky_u16_t *) buf);

    return true;
}

sky_bool_t
sky_dns_decode_body(sky_dns_packet_t *packet, sky_pool_t *pool, const sky_uchar_t *buf, sky_u32_t size) {
    buf_ref_t ref = {
            .pool = pool,
            .start = buf,
            .buf_ref = &buf,
            .size_ref = &size,
            .size = size
    };

    buf += 12;
    size -= 12;


    if (packet->header.qd_count > 0) {
        packet->questions = sky_pnalloc(pool, sizeof(sky_dns_answer_t) * packet->header.qd_count);
        if (sky_unlikely(!decode_question(packet->questions, &ref, packet->header.qd_count))) {
            sky_log_warn("decode question error");
            return false;
        }
    }
    if (packet->header.an_count > 0) {
        packet->answers = sky_pnalloc(pool, sizeof(sky_dns_answer_t) * packet->header.an_count);
        if (sky_unlikely(!decode_answer(packet->answers, &ref, packet->header.an_count))) {
            sky_log_warn("decode answers error");
            return false;
        }
    }
    if (packet->header.ns_count > 0) {
        packet->authorities = sky_pnalloc(pool, sizeof(sky_dns_answer_t) * packet->header.ns_count);
        if (sky_unlikely(!decode_answer(packet->authorities, &ref, packet->header.ns_count))) {
            sky_log_warn("decode authorities error");
            return false;
        }
    }
    if (packet->header.ar_count > 0) {
        packet->authorities = sky_pnalloc(pool, sizeof(sky_dns_answer_t) * packet->header.ar_count);
        if (sky_unlikely(!decode_answer(packet->additional, &ref, packet->header.ar_count))) {
            sky_log_warn("decode additional error");
            return false;
        }
    }

    return size == 0;
}


static sky_bool_t
decode_question(
        sky_dns_question_t *q,
        buf_ref_t *ref,
        sky_u16_t qn
) {
    sky_u32_t size = *ref->size_ref;
    const sky_uchar_t *buf = *ref->buf_ref;


    buf_ref_t tmp = {
            .pool = ref->pool,
            .start = ref->start,
            .buf_ref = &buf,
            .size_ref = &size,
            .size = ref->size
    };

    do {
        if (sky_unlikely(size < 4)) {
            return false;
        }

        sky_str_t name;
        sky_str_buf_t str_buf;
        sky_str_buf_init2(&str_buf, ref->pool, 64);
        if (sky_unlikely(!decode_name(&str_buf, &tmp))) {
            return false;
        }
        sky_str_buf_build(&str_buf, &name);
        if (name.len > 0) {
            ++name.data;
            --name.len;
        }
        q->name = name.data;
        q->name_len = (sky_u32_t) name.len;

        if (sky_unlikely(size < 4)) {
            return false;
        }

        q->type = sky_htons(*(sky_u16_t *) buf);
        buf += 2;
        q->clazz = sky_htons(*(sky_u16_t *) buf);
        buf += 2;

        size -= 4;

        ++q;
        --qn;
    } while (qn > 0);

    *ref->buf_ref = buf;
    *ref->size_ref = size;

    return true;
}

static sky_bool_t
decode_answer(
        sky_dns_answer_t *a,
        buf_ref_t *ref,
        sky_u16_t an
) {
    sky_u32_t size = *ref->size_ref;
    const sky_uchar_t *buf = *ref->buf_ref;


    buf_ref_t tmp = {
            .pool = ref->pool,
            .start = ref->start,
            .buf_ref = &buf,
            .size_ref = &size,
            .size = ref->size
    };

    do {
        if (sky_unlikely(size < 10)) {
            return false;
        }

        sky_str_t name;
        sky_str_buf_t str_buf;
        sky_str_buf_init2(&str_buf, ref->pool, 64);
        if (sky_unlikely(!decode_name(&str_buf, &tmp))) {
            return false;
        }
        sky_str_buf_build(&str_buf, &name);
        if (name.len > 0) {
            ++name.data;
            --name.len;
        }
        a->name = name.data;
        a->name_len = (sky_u32_t) name.len;

        if (sky_unlikely(size < 10)) {
            return false;
        }
        a->type = sky_htons(*(sky_u16_t *) buf);
        buf += 2;
        a->clazz = sky_htons(*(sky_u16_t *) buf);
        buf += 2;
        a->ttl = sky_htonl(*(sky_u32_t *) buf);
        buf += 4;

        a->resource_len = sky_htons(*(sky_u16_t *) buf);
        buf += 2;
        size -= 10;

        if (sky_unlikely(size < a->resource_len)) {
            return false;
        }
        sky_u32_t resource_len = a->resource_len;
        const sky_uchar_t *resource = buf;

        buf += resource_len;
        size -= resource_len;

        switch (a->type) {
            case SKY_DNS_TYPE_A:
                if (sky_unlikely(resource_len != 4)) {
                    return false;
                }
                sky_memcpy4(&a->resource.ipv4, resource);
                break;
            case SKY_DNS_TYPE_CNAME: {
                a->resource.name = sky_palloc(ref->pool, resource_len);
                sky_memcpy(a->resource.name, resource, resource_len);
                break;
            }
            case SKY_DNS_TYPE_AAAA: {
                if (sky_unlikely(resource_len != 16)) {
                    return false;
                }
                a->resource.ipv6 = sky_palloc(ref->pool, 16);
                sky_memcpy8(a->resource.ipv6, resource);
                sky_memcpy8(a->resource.ipv6 + 8, resource + 8);
                break;
            }
            default: {
                break;
            }
        }
        ++a;
        --an;
    } while (an > 0);

    *ref->buf_ref = buf;
    *ref->size_ref = size;

    return true;
}

static sky_bool_t
decode_name(sky_str_buf_t *str_buf, buf_ref_t *ref) {
    sky_u8_t len;
    sky_u32_t size = *ref->size_ref;
    const sky_uchar_t *buf = *ref->buf_ref;

    for (;;) {
        len = *buf;

        if (sky_unlikely(!len)) {
            sky_str_buf_append_u8(str_buf, len);
            ++buf;
            --size;
            break;
        }
        if (dns_name_is_ptr(len)) {
            const sky_u16_t offset = dns_name_ptr_offset(sky_htons(*(sky_u16_t *) buf));
            buf += 2;
            size -= 2;

            buf_position_t position = {
                    .pool = ref->pool,
                    .start = ref->start,
                    .buf = ref->start + offset,
                    .size = ref->size - offset,
                    .start_size = ref->size
            };

            if (sky_unlikely(offset >= ref->size || !decode_name_offset(str_buf, &position))) {
                return false;
            }

            break;
        }
        ++buf;
        --size;

        if (sky_likely(size > len)) {
            sky_str_buf_append_u8(str_buf, len);
            sky_str_buf_append_str_len(str_buf, buf, len);
            buf += len;
            size -= len;

            continue;
        }

        return false;
    }

    *ref->buf_ref = buf;
    *ref->size_ref = size;

    return true;
}

static sky_bool_t
decode_name_offset(sky_str_buf_t *str_buf, buf_position_t *p) {
    sky_u8_t len;

    for (;;) {
        len = *p->buf;
        if (sky_unlikely(!len)) {
            sky_str_buf_append_u8(str_buf, len);
            ++p->buf;
            --p->size;
            break;
        }
        if (dns_name_is_ptr(len)) {
            const sky_u16_t offset = dns_name_ptr_offset(sky_htons(*(sky_u16_t *) p->buf));
            if (sky_unlikely(offset > p->start_size)) {
                return false;
            }
            p->buf = p->start + offset;
            p->size = p->start_size - offset;
            continue;
        }
        ++p->buf;
        --p->size;

        if (sky_likely(p->size > len)) {
            sky_str_buf_append_u8(str_buf, len);
            sky_str_buf_append_str_len(str_buf, p->buf, len);
            p->buf += len;
            p->size -= len;

            continue;
        }

        return false;
    }

    return true;
}

static sky_inline sky_bool_t
dns_name_is_ptr(sky_u8_t v) {
    return (v & SKY_U8(0xC0)) == SKY_U8(0xC0);
}

static sky_inline sky_u16_t
dns_name_ptr_offset(sky_u16_t v) {
    return v & SKY_U16(0x3FFF);
}

