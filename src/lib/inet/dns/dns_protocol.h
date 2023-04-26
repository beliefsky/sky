//
// Created by beliefsky on 2023/4/26.
//

#ifndef SKY_DNS_PROTOCOL_H
#define SKY_DNS_PROTOCOL_H

#include "../../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * IPV4地址
 */
#define SKY_DNS_TYPE_A SKY_U16(1)
/**
 * 名字服务器
 */
#define SKY_DNS_TYPE_NS SKY_U16(2)
/**
 * 规范名称定义主机的正式名称的别名
 */
#define SKY_DNS_TYPE_CNAME SKY_U16(5)
/**
 * 开始授权标记一个区的开始
 */
#define SKY_DNS_TYPE_SOA SKY_U16(6)
/**
 * 熟知服务定义主机提供的网络服务
 */
#define SKY_DNS_TYPE_WKS SKY_U16(11)
/**
 * 指针把IP地址转化为域名
 */
#define SKY_DNS_TYPE_PTR SKY_U16(12)
/**
 * 主机信息给出主机使用的硬件和操作系统的表述
 */
#define SKY_DNS_TYPE_HINFO SKY_U16(13)
/**
 * 邮件交换把邮件改变路由送到邮件服务器
 */
#define SKY_DNS_TYPE_MX SKY_U16(15)
/**
 * IPV6 地址
 */
#define SKY_DNS_TYPE_AAAA SKY_U16(28)
/**
 * 传送整个区的请求
 */
#define SKY_DNS_TYPE_AXFR SKY_U16(252)
/**
 * 对所有记录的请求
 */
#define SKY_DNS_TYPE_ANY SKY_U16(255)

/**
 * Internet数据
 */
#define SKY_DNS_CLAZZ_IN SKY_U16(1)


typedef struct sky_dns_packet_s sky_dns_packet_t;
typedef struct sky_dns_header_s sky_dns_header_t;
typedef struct sky_dns_question_s sky_dns_question_t;
typedef struct sky_dns_answer_s sky_dns_answer_t;
typedef struct sky_dns_authority_s sky_dns_authority_t;
typedef struct sky_dns_additional_s sky_dns_additional_t;


struct sky_dns_header_s {
    sky_u16_t id; // 事务id

    /*
     * |-- 1bit --|------ 4bit ------|-- 1bit --|-- 1bit --|-- 1bit --|
     * |--- QR ---|----- OPCODE -----|--- AA ---|--- TC ---|--- RD ---|
     * |-- 1bit --|----- 3bit -----|------------- 4bit ---------------|
     * |--- RA ---|------- Z ------|------------- RCODE --------------|
     *
     * QR: 操作类型：0 查询报文，1 响应报文
     * OPCODE: 查询类型：0 标准查询，1反省查询，2服务器状态查询，3~15保留
     * AA: 标识该域名解析服务器是授权回答该域的
     * TC: 表示报文被截断，UDP传输是，应答总长度超过512byte,只返回报文的前512个字节内容
     * RD: 客户端希望域名解析方式：0迭代解析，1递归解析
     * RA: 域名解析服务器解析的方式：0迭代解析，1递归解析
     * Z: 全部为0，保留未用
     * RCODE: 相应类型：0无出错，1查询格式错，2服务器无效，3域名不存在，4查询没被执行，4查询被拒绝，6-16保留
     */
    sky_u16_t flags;

    sky_u16_t qd_count; // 请求段中问题记录数
    sky_u16_t an_count; // 回答段中回答记录数
    sky_u16_t ns_count; // 授权段中授权记录数
    sky_u16_t ar_count; // 附加段中附加记录数
};

struct sky_dns_question_s {
    sky_u16_t type;
    sky_u16_t clazz;
    sky_u32_t name_len;
    sky_uchar_t *name;
};

struct sky_dns_answer_s {
};

struct sky_dns_authority_s {
};

struct sky_dns_additional_s {
};

struct sky_dns_packet_s {
    sky_dns_header_t header;
    sky_dns_question_t *questions;
    sky_dns_answer_t *answers;
    sky_dns_authority_t *authorities;
    sky_dns_additional_t *additional;
};

sky_u32_t sky_dns_encode_size(const sky_dns_packet_t *packet);

sky_u32_t sky_dns_encode(const sky_dns_packet_t *packet, sky_uchar_t *buf);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_DNS_PROTOCOL_H
