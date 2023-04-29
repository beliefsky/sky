//
// Created by beliefsky on 2023/4/26.
//

#include "dns_protocol.h"
#include "../../core/memory.h"
#include "../inet.h"
#include "../../core/log.h"


typedef struct {
    sky_uchar_t *start;
    sky_uchar_t **buf_ref;
    sky_u32_t *size_ref;
    sky_u32_t size;
} buf_ref_t;

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

static sky_bool_t decode_name(sky_uchar_t *start, sky_uchar_t **b_ptr, sky_u32_t *s_ptr, sky_u32_t start_size);

static sky_bool_t decode_name_offset(sky_uchar_t *start, sky_uchar_t *buf, sky_u32_t size, sky_u32_t start_size);

static sky_bool_t dns_name_is_ptr(sky_u8_t v);

static sky_u16_t dns_name_ptr_offset(sky_u16_t v);

static void print_name(const sky_uchar_t *buf, sky_u8_t n);

static void print_buf(const sky_uchar_t *buf, sky_u32_t n);


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
            *buf++ = '\0';

            *(sky_u16_t *) buf = sky_htons(question->type);
            buf += 2;
            *(sky_u16_t *) buf = sky_htons(question->clazz);
            buf += 2;


            n += 5;
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
sky_dns_decode_body(sky_dns_packet_t *packet, sky_uchar_t *buf, sky_u32_t size) {
    buf_ref_t ref = {
            .start = buf,
            .buf_ref = &buf,
            .size_ref = &size,
            .size = size
    };

    buf += 12;
    size -= 12;


    if (sky_unlikely(!decode_question(packet->questions, &ref, packet->header.qd_count))) {
        sky_log_warn("decode question error");
        return false;
    }
    if (sky_unlikely(!decode_answer(packet->answers, &ref, packet->header.an_count))) {
        sky_log_warn("decode answers error");
        return false;
    }
    if (sky_unlikely(!decode_answer(packet->authorities, &ref, packet->header.ns_count))) {
        sky_log_warn("decode authorities error");
        return false;
    }
    if (sky_unlikely(!decode_answer(packet->additional, &ref, packet->header.ar_count))) {
        sky_log_warn("decode additional error");
        return false;
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
    sky_uchar_t *buf = *ref->buf_ref;

    for (; qn > 0; --qn) {
        if (sky_unlikely(size < 4)) {
            return false;
        }
        if (sky_unlikely(!decode_name(ref->start, &buf, &size, ref->size))) {
            return false;
        }
        if (sky_unlikely(size < 4)) {
            return false;
        }

        sky_log_info("question type: %d", sky_htons(*(sky_u16_t *) buf));
        buf += 2;
        sky_log_info("question clazz: %d", sky_htons(*(sky_u16_t *) buf));
        buf += 2;

        size -= 4;
    }

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
    sky_uchar_t *buf = *ref->buf_ref;

    for (; an > 0; --an) {
        if (sky_unlikely(size < 10)) {
            return false;
        }

        if (sky_unlikely(!decode_name(ref->start, &buf, &size, ref->size))) {
            return false;
        }

        if (sky_unlikely(size < 10)) {
            return false;
        }
        a->type = sky_htons(*(sky_u16_t *) buf);
        sky_log_info("answer type: %d", a->type);
        buf += 2;
        a->clazz = sky_htons(*(sky_u16_t *) buf);
        sky_log_info("answer clazz: %d", a->clazz);
        buf += 2;
        a->ttl = sky_htonl(*(sky_u32_t *) buf);
        sky_log_info("answer ttl: %u", a->ttl);
        buf += 4;

        a->resource_len = sky_htons(*(sky_u16_t *) buf);
        sky_log_info("answer data len: %d", a->resource_len);
        buf += 2;
        size -= 10;

        if (sky_unlikely(size < a->resource_len)) {
            return false;
        }
        sky_u32_t resource_len = a->resource_len;
        sky_uchar_t *resource = buf;

        buf += resource_len;
        size -= resource_len;

        switch (a->type) {
            case SKY_DNS_TYPE_A:
                if (sky_unlikely(resource_len != 4)) {
                    return false;
                }
                sky_log_info("%d.%d.%d.%d", resource[0], resource[1], resource[2], resource[3]);
                break;
            case SKY_DNS_TYPE_CNAME: {
                if (sky_unlikely(!decode_name(ref->start, &resource, &resource_len, ref->size) || resource_len)) {
                    return false;
                }
                break;
            }
            case SKY_DNS_TYPE_AAAA: {
                if (sky_unlikely(resource_len != 16)) {
                    return false;
                }
                sky_u16_t *tm= (sky_u16_t *)resource;
                for (int i = 0; i < 8; ++i) {
                    printf("%x:", tm[i]);
                }
                printf("\n");
                break;
            }
            default: {
                print_buf(resource, resource_len);

                break;
            }
        }


        sky_log_info("===============================");
    }

    *ref->buf_ref = buf;
    *ref->size_ref = size;

    return true;
}

static sky_bool_t
decode_name(sky_uchar_t *start, sky_uchar_t **b_ptr, sky_u32_t *s_ptr, sky_u32_t start_size) {
    sky_u8_t len;
    sky_u32_t size = *s_ptr;
    sky_uchar_t *buf = *b_ptr;

    for (;;) {
        len = *buf;

        if (sky_unlikely(!len)) {
            ++buf;
            --size;
            printf("\n");
            break;
        }
        if (dns_name_is_ptr(len)) {
            const sky_u16_t offset = dns_name_ptr_offset(sky_htons(*(sky_u16_t *) buf));
            buf += 2;
            size -= 2;
            if (sky_unlikely(offset >= start_size ||
                             !decode_name_offset(start, start + offset, start_size - offset, start_size))) {
                return false;
            }

            break;
        }
        ++buf;
        --size;

        if (sky_likely(size > len)) {
            print_name(buf, len);
            buf += len;
            size -= len;

            continue;
        }

        return false;
    }

    *b_ptr = buf;
    *s_ptr = size;

    return true;
}

static sky_bool_t
decode_name_offset(sky_uchar_t *start, sky_uchar_t *buf, sky_u32_t size, sky_u32_t start_size) {
    sky_u8_t len;

    for (;;) {
        len = *buf;
        if (sky_unlikely(!len)) {
            ++buf;
            --size;
            printf("\n");
            break;
        }
        if (dns_name_is_ptr(len)) {
            const sky_u16_t offset = dns_name_ptr_offset(sky_htons(*(sky_u16_t *) buf));
            if (sky_unlikely(offset > start_size)) {
                return false;
            }
            buf = start + offset;
            size = start_size - offset;
            continue;
        }
        ++buf;
        --size;

        if (sky_likely(size > len)) {
            print_name(buf, len);
            buf += len;
            size -= len;

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

static void
print_name(const sky_uchar_t *buf, sky_u8_t n) {
    for (sky_u8_t i = 0; i < n; ++i) {
        printf("%c", buf[i]);
    }
    printf(" ");
}

static void
print_buf(const sky_uchar_t *buf, sky_u32_t n) {
    sky_log_warn("===== size: %u =======", n);
    for (sky_isize_t i = 0; i < n; ++i) {
        printf("%c\t", buf[i]);
    }
    printf("\n");

    for (sky_isize_t i = 0; i < n; ++i) {
        printf("%d\t", buf[i]);
    }
    printf("\n");
}

