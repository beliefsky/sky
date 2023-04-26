//
// Created by beliefsky on 2023/4/26.
//

#include "dns_protocol.h"
#include "../../core/memory.h"
#include "../inet.h"
#include "../../core/log.h"

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
    sky_u8_t n;
    sky_u16_t i;

    buf += 12;
    size -= 12;

    {
        sky_dns_question_t *question = packet->questions;
        for (i = packet->header.qd_count; i > 0; --i, ++question) {
            question->name = buf;
            question->name_len = 0;

            for (;;) {
                n = *buf;
                if (n > 0) {
                    *buf++ = '.';
                    buf += n++;

                    question->name_len += n;
                    size -= n;
                    continue;
                }
                ++buf;
                ++question->name;
                --question->name_len;
                break;
            }
            question->type = sky_htons(*(sky_u16_t *) buf);
            buf += 2;
            question->clazz = sky_htons(*(sky_u16_t *) buf);
            buf += 2;
        }
    }

    return true;
}


