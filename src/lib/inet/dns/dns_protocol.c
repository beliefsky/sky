//
// Created by beliefsky on 2023/4/26.
//

#include "dns_protocol.h"
#include "../../core/memory.h"
#include "../inet.h"
#include "../../core/log.h"

static sky_bool_t decode_question(
        sky_dns_question_t *q,
        sky_uchar_t **b_ptr,
        sky_uchar_t *start,
        sky_u32_t *s_ptr,
        sky_u32_t start_size,
        sky_u16_t qn
);

static sky_bool_t decode_answer(
        sky_dns_answer_t *a,
        sky_uchar_t **b_ptr,
        sky_uchar_t *start,
        sky_u32_t *s_ptr,
        sky_u32_t start_size,
        sky_u16_t an
);

static sky_bool_t decode_name(sky_uchar_t *start, sky_uchar_t **b_ptr, sky_u32_t *s_ptr, sky_u32_t start_size);

static sky_bool_t decode_name_offset(sky_uchar_t *start, sky_uchar_t *buf, sky_u32_t size, sky_u32_t start_size);

static sky_bool_t dns_name_is_ptr(sky_u8_t v);

static sky_u16_t dns_name_ptr_offset(sky_u16_t v);

static void print_name(const sky_uchar_t *buf, sky_u8_t n);

static void print_buf(const sky_uchar_t *buf, sky_u32_t n);


sky_u32_t
sky_dns_encode_size(const sky_dns_packet_t *packet) {
    sky_u32_t n = 12;

    sky_u16_t i;
    {
        const sky_dns_question_t *question = packet->questions;
        for (i = packet->header.qd_count; i > 0; --i, ++question) {
            n += 5;
            n += question->name_len;
        }
    }

    return n;
}

sky_u32_t
sky_dns_encode(const sky_dns_packet_t *packet, sky_uchar_t *buf) {
    sky_u32_t n = 12;
    sky_u16_t i;

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


    return n;
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
    const sky_u32_t buf_size = size;
    sky_uchar_t *start = buf;

    buf += 12;
    size -= 12;

    if (sky_unlikely(!decode_question(packet->questions, &buf, start, &size, buf_size, packet->header.qd_count))) {
        sky_log_warn("decode question error");
        return false;
    }
    if (sky_unlikely(!decode_answer(packet->answers, &buf, start, &size, buf_size, packet->header.an_count))) {
        sky_log_warn("decode answers error");
        return false;
    }
    if (sky_unlikely(!decode_answer(packet->authorities, &buf, start, &size, buf_size, packet->header.ns_count))) {
        sky_log_warn("decode authorities error");
        return false;
    }
    if (sky_unlikely(!decode_answer(packet->additional, &buf, start, &size, buf_size, packet->header.ar_count))) {
        sky_log_warn("decode additional error");
        return false;
    }

    return size == 0;
}


static sky_bool_t
decode_question(
        sky_dns_question_t *q,
        sky_uchar_t **b_ptr,
        sky_uchar_t *start,
        sky_u32_t *s_ptr,
        sky_u32_t start_size,
        sky_u16_t qn
) {
    sky_u32_t size = *s_ptr;
    sky_uchar_t *buf = *b_ptr;

    for (; qn > 0; --qn) {
        if (sky_unlikely(size < 4)) {
            return false;
        }
        if (sky_unlikely(!decode_name(start, &buf, &size, start_size))) {
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

    *b_ptr = buf;
    *s_ptr = size;

    return true;
}

static sky_bool_t
decode_answer(
        sky_dns_answer_t *a,
        sky_uchar_t **b_ptr,
        sky_uchar_t *start,
        sky_u32_t *s_ptr,
        sky_u32_t start_size,
        sky_u16_t an
) {
    sky_u32_t size = *s_ptr;
    sky_uchar_t *buf = *b_ptr;

    for (; an > 0; --an) {
        if (sky_unlikely(size < 10)) {
            return false;
        }
        if (sky_unlikely(!decode_name(start, &buf, &size, start_size))) {
            return false;
        }

        if (sky_unlikely(size < 10)) {
            return false;
        }
        sky_log_info("answer type: %d", sky_htons(*(sky_u16_t *) buf));
        buf += 2;
        sky_log_info("answer clazz: %d", sky_htons(*(sky_u16_t *) buf));
        buf += 2;
        sky_log_info("answer ttl: %u", sky_htonl(*(sky_u32_t *) buf));
        buf += 4;

        const sky_u16_t data_len = sky_htons(*(sky_u16_t *) buf);

        sky_log_info("answer data len: %d", data_len);
        buf += 2;

        size -= 10;

        print_buf(buf, data_len);
        buf += data_len;
        size -= data_len;
    }

    *b_ptr = buf;
    *s_ptr = size;

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

