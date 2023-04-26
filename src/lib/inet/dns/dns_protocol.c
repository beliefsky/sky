//
// Created by beliefsky on 2023/4/26.
//

#include "dns_protocol.h"
#include "../../core/memory.h"
#include "../inet.h"

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


