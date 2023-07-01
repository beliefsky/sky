//
// Created by weijing on 17-12-5.
//

#ifndef SKY_BASE64_H
#define SKY_BASE64_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
这两个函数用于对str进行base64编码与解码，
调用前，需要保证dst中有足够的空间来存放结果，
如果不知道具体大小，
可先调用sky_base64_encoded_length与sky_base64_decoded_length来预估最大占用空间。
*/

//base64 编码／解码函数和宏
#define sky_base64_encoded_length(_len)  ((((_len) + 2) / 3) << 2)
#define sky_base64_decoded_length(_len)  ((((_len) + 3) >> 2) * 3)

//标准base64的编解码
sky_usize_t sky_base64_encode(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len);

sky_usize_t sky_base64_decode(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len);


static sky_inline sky_bool_t
sky_base64_decode_success(sky_usize_t len) {
    return len != SKY_USIZE_MAX;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_BASE64_H
