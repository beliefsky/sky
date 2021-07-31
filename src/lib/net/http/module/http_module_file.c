//
// Created by weijing on 2019/11/15.
//

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "http_module_file.h"
#include "../http_response.h"
#include "../../../core/date.h"
#include "../../../core/memory.h"

#define http_error_page(_r, _status, _msg)                              \
    (_r)->state = _status;                                              \
    sky_str_set(&(_r)->headers_out.content_type, "text/html");          \
    sky_http_response_static_len(r,sky_str_line("<html>\n<head><title>" \
    _msg                                                                \
    "</title></head>\n<body bgcolor=\"white\">\n<center><h1>"           \
    _msg                                                                \
    "</h1></center>\n<hr><center>sky</center>\n</body>\n</html>"))


typedef struct {
    time_t modified_time;
    time_t range_time;
    sky_i64_t left;
    sky_i64_t right;
    sky_defer_t *file_defer;

    sky_bool_t modified: 1;
    sky_bool_t range: 1;
    sky_bool_t if_range: 1;
} http_file_t;

typedef struct {
    sky_str_t val;
    sky_bool_t binary: 1;
} http_mime_type_t;

typedef struct {
    http_mime_type_t default_mime_type;
    sky_str_t path;
    sky_pool_t *pool;

    sky_bool_t (*pre_run)(sky_http_request_t *req, void *data);

    void *run_data;
} http_module_file_t;

static void http_run_handler(sky_http_request_t *r, http_module_file_t *data);

static sky_bool_t http_mime_type_get(const sky_str_t *exten, http_mime_type_t *type);

static sky_bool_t http_header_range(http_file_t *file, sky_str_t *value);

void
sky_http_module_file_init(sky_pool_t *pool, const sky_http_file_conf_t *conf) {
    http_module_file_t *data = sky_palloc(pool, sizeof(http_module_file_t));
    data->pool = pool;
    data->path = conf->dir;

    sky_str_set(&data->default_mime_type.val, "application/octet-stream");
    data->default_mime_type.binary = true;

    data->pre_run = conf->pre_run;

    sky_http_module_t *module = conf->module;
    module->prefix = conf->prefix;
    module->run = (sky_module_run_pt) http_run_handler;

    module->module_data = data;
}

static void
http_run_handler(sky_http_request_t *r, http_module_file_t *data) {
    struct stat stat_buf;
    sky_i64_t mtime;
    http_file_t *file;
    http_mime_type_t mime_type;
    sky_http_header_t *header;
    sky_char_t *path;
    sky_i32_t fd;

    if (sky_unlikely(!r->uri.len)) {
        http_error_page(r, 404, "404 Not Found");
        return;
    }
    if (r->uri.len == 1 && *r->uri.data == '/') {
        sky_str_set(&r->uri, "/index.html");
        sky_str_set(&r->exten, ".html");
    }

    if (data->pre_run) {
        if (!data->pre_run(r, data->run_data)) {
            return;
        }
    }

    if (!r->exten.len) {
        mime_type = data->default_mime_type;
    } else {
        const sky_str_t lower = {
                .len = r->exten.len,
                .data = sky_palloc(r->pool, sizeof(r->exten.len) + 1)
        };
        sky_str_lower(r->exten.data, lower.data, lower.len);
        lower.data[lower.len] = '\0';

        if (!http_mime_type_get(&lower, &mime_type)) {
            mime_type = data->default_mime_type;
        }
    }


    file = sky_pcalloc(r->pool, sizeof(http_file_t));

    if (r->headers_in.if_modified_since) {
        file->modified = true;
        sky_rfc_str_to_date(r->headers_in.if_modified_since, &file->modified_time);
    }
    if (r->headers_in.range) {
        file->range = true;
        http_header_range(file, r->headers_in.range);

        if (r->headers_in.if_range) {
            file->if_range = true;
            sky_rfc_str_to_date(r->headers_in.if_range, &file->range_time);
        }
    }
    path = sky_palloc(r->pool, data->path.len + r->uri.len + 1);
    sky_memcpy(path, data->path.data, data->path.len);
    sky_memcpy(path + data->path.len, r->uri.data, r->uri.len + 1);
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        http_error_page(r, 404, "404 Not Found");
        return;
    }

    fstat(fd, &stat_buf);
    if (sky_unlikely(S_ISDIR(stat_buf.st_mode))) {
        close(fd);
        http_error_page(r, 404, "404 Not Found");
        return;
    }

#if defined(__APPLE__)
    mtime = stat_buf.st_mtimespec.tv_sec;
#else
    mtime = stat_buf.st_mtim.tv_sec;
#endif

    r->headers_out.content_type = mime_type.val;
    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Last-Modified");

    if (file->modified && file->modified_time == mtime) {
        close(fd);
        header->val = *r->headers_in.if_modified_since;

        r->state = 304;

        sky_http_response_nobody(r);
        return;
    }
    header->val.data = sky_palloc(r->pool, 30);
    header->val.len = sky_date_to_rfc_str(mtime, header->val.data);

    if (file->range && (!file->if_range || file->range_time == mtime)) {
        r->state = 206;
        if (file->right == 0 || file->right > stat_buf.st_size) {
            file->right = stat_buf.st_size - 1;
        }
    } else {
        file->left = 0;
        file->right = stat_buf.st_size - 1;
    }
    file->file_defer = sky_defer_add(r->conn->coro, (sky_defer_func_t) close, (void *) (sky_usize_t) fd);

    sky_http_sendfile(r, fd, (sky_usize_t) file->left, (sky_usize_t) (file->right - file->left + 1),
                      (sky_usize_t) stat_buf.st_size);

    sky_defer_remove(r->conn->coro, file->file_defer);
}

static sky_bool_t
http_header_range(http_file_t *file, sky_str_t *value) {
    sky_uchar_t *start, *end, p;
    sky_i64_t tmp;

    enum {
        rang_start = 0,
        rang_left,
        rang_right
    } state;

    start = value->data;
    end = start + value->len;
    state = rang_start;
    tmp = 0;


    for (; start != end; ++start) {
        p = *start;
        switch (state) {
            case rang_start:
                if (p == '=') {
                    tmp = 0;
                    state = rang_left;
                }
                break;
            case rang_left:
                if (p == '-') {
                    file->left = tmp;
                    tmp = 0;
                    state = rang_right;
                } else {
                    if (sky_unlikely(p < '0' || p > '9')) {
                        return false;
                    }
                    tmp = tmp * 10 + (p - '0');
                }
                break;
            case rang_right:
                if (sky_unlikely(p < '0' || p > '9')) {
                    return false;
                }
                tmp = tmp * 10 + (p - '0');
                break;
            default:
                return false;
        }
    }
    file->right = tmp;
    return true;
}


static sky_inline sky_bool_t
http_mime_type_get(const sky_str_t *exten, http_mime_type_t *type) {
#define mine_set(_type, _b)         \
    sky_str_set(&type->val, _type);  \
    type->binary = (_b)

    const sky_uchar_t *p = exten->data;
    switch (exten->len) {
        case 3: {
            switch (sky_str4_switch(p)) {
                case sky_str4_num('.', 'j', 's', '\0'):
                mine_set("application/javascript", false);
                    return true;
                case sky_str4_num('.', '7', 'z', '\0'):
                mine_set("application/x-7z-compressed", true);
                    return true;
                case sky_str4_num('.', 'g', 'z', '\0'):
                mine_set("application/x-gzip", true);
                    return true;
                default:
                    break;
            }
            break;
        }
        case 4: {
            switch (sky_str4_switch(p)) {
                case sky_str4_num('.', 'h', 't', 'm'):
                mine_set("text/html", false);
                    return true;
                case sky_str4_num('.', 'c', 's', 's'):
                mine_set("text/css", false);
                    return true;
                case sky_str4_num('.', 't', 'x', 't'):
                mine_set("text/plain", false);
                    return true;
                case sky_str4_num('.', 'x', 'm', 'l'):
                mine_set("text/xml", false);
                    return true;
                case sky_str4_num('.', 'p', 'd', 'f'):
                mine_set("application/pdf", true);
                    return true;
                case sky_str4_num('.', 'j', 'p', 'g'):
                mine_set("image/jpeg", true);
                    return true;
                case sky_str4_num('.', 'p', 'n', 'g'):
                mine_set("image/png", true);
                    return true;
                case sky_str4_num('.', 'g', 'i', 'f'):
                mine_set("image/gif", true);
                    return true;
                case sky_str4_num('.', 's', 'v', 'g'):
                mine_set("image/svg+xml", false);
                    return true;
                case sky_str4_num('.', 'r', 'a', 'r'):
                mine_set("application/x-rar-compressed", true);
                    return true;
                case sky_str4_num('.', 'z', 'i', 'p'):
                mine_set("application/zip", true);
                    return true;
                case sky_str4_num('.', 't', 'a', 'r'):
                mine_set("application/x-tar", true);
                    return true;
                case sky_str4_num('.', 'm', 'p', '3'):
                mine_set("audio/mp3", true);
                    return true;
                case sky_str4_num('.', 'o', 'g', 'g'):
                mine_set("audio/ogg", true);
                    return true;
                case sky_str4_num('.', 'm', 'p', '4'):
                mine_set("video/mp4", true);
                    return true;
                default:
                    break;
            }
            break;
        }
        case 5: {
            ++p;
            switch (sky_str4_switch(p)) {
                case sky_str4_num('h', 't', 'm', 'l'):
                mine_set("text/html", false);
                    return true;
                case sky_str4_num('j', 's', 'o', 'n'):
                mine_set("application/json", false);
                    return true;
                case sky_str4_num('j', 'p', 'e', 'g'):
                mine_set("image/jpeg", true);
                    return true;
                case sky_str4_num('i', 'c', 'o', 'n'):
                mine_set("image/x-icon", true);
                    return true;
                case sky_str4_num('w', 'o', 'f', 'f'):
                mine_set("application/font-woff", true);
                    return true;
                case sky_str4_num('w', 'w', 'b', 'm'):
                mine_set("video/webm", false);
                    return true;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    return false;

#undef mine_set
}