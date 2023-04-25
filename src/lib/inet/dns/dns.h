//
// Created by beliefsky on 2023/4/25.
//

#ifndef SKY_DNS_H
#define SKY_DNS_H

#include "../../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_dns_header_s sky_dns_header_t;

struct sky_dns_header_s {
    sky_u16_t id; // 事务id
    sky_u16_t flags;

//    qr: 1; // 操作类型：0 查询报文，1 响应报文
//    op_code: 4; // 查询类型：0 标准查询，1反省查询，2服务器状态查询，3~15保留
//    aa: 1; // 标识该域名解析服务器是授权回答该域的
//    tc: 1; // 表示报文被截断，UDP传输是，应答总长度超过512byte,只返回报文的前512个字节内容
//    rd: 1; // 客户端希望域名解析方式：0迭代解析，1递归解析
//    ra: 1; // 域名解析服务器解析的方式：0迭代解析，1递归解析
//    z: 3; // 全部为0，保留未用
//    r_code: 4; // 相应类型：0无出错，1查询格式错，2服务器无效，3域名不存在，4查询没被执行，4查询被拒绝，6-16保留
    sky_u16_t qd_count; // 请求段中问题记录数
    sky_u16_t an_count; // 回答段中回答记录数
    sky_u16_t ns_count; // 授权段中授权记录数
    sky_u16_t ar_count; // 附加段中附加记录数
    sky_u32_t _padding_; // 16字节对齐填充,禁止访问和赋值
};


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_DNS_H
