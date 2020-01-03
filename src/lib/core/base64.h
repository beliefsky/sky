//
// Created by weijing on 17-12-5.
//

#ifndef SKY_BASE64_H
#define SKY_BASE64_H
#if defined(__cplusplus)
extern "C" {
#endif
#include "string.h"

//base64 编码／解码函数和宏
#define sky_base64_encoded_length(len)  (((len + 0x2) / 0x3) << 0x2)
#define sky_base64_decoded_length(len)  (((len + 0x3) >> 0x2) * 0x3)

//标准base64的编解码
void sky_encode_base64(sky_str_t *dst, sky_str_t *src);
sky_bool_t sky_decode_base64(sky_str_t *dst, sky_str_t *src);
/*
这两个函数用于对str进行base64编码与解码，
调用前，需要保证dst中有足够的空间来存放结果，
如果不知道具体大小，
可先调用sky_base64_encoded_length与sky_base64_decoded_length来预估最大占用空间。
*/

void sky_encode_base64url(sky_str_t *dst, sky_str_t *src);
sky_bool_t sky_decode_base64url(sky_str_t *dst, sky_str_t *src);
#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_BASE64_H
