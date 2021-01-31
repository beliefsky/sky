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
#include "../../../core/cpuinfo.h"
#include "../../../core/log.h"

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
    sky_int64_t left;
    sky_int64_t right;
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
    sky_hash_t mime_types;
    http_mime_type_t default_mime_type;
    sky_pool_t *pool;
    sky_pool_t *tmp_pool;
    sky_str_t path;
} http_module_file_t;

static void http_run_handler(sky_http_request_t *r, http_module_file_t *data);

static sky_bool_t http_header_range(http_file_t *file, sky_str_t *value);

static void http_mime_type_init(http_module_file_t *data);

void
sky_http_module_file_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix, sky_str_t *dir) {
    http_module_file_t *data;

    data = sky_palloc(pool, sizeof(http_module_file_t));
    data->pool = pool;
    data->path = *dir;
    data->tmp_pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);

    http_mime_type_init(data);

    sky_destroy_pool(data->tmp_pool);
    data->tmp_pool = null;

    module->prefix = *prefix;
    module->read_body = null;
    module->run = (sky_module_run_pt) http_run_handler;

    module->module_data = data;
}

static void
http_run_handler(sky_http_request_t *r, http_module_file_t *data) {
    struct stat stat_buf;
    sky_int64_t mtime;
    http_file_t *file;
    http_mime_type_t *mime_type;
    sky_table_elt_t *header;
    sky_char_t *path;
    sky_int32_t fd;

    if (sky_unlikely(!r->uri.len)) {
        http_error_page(r, 404, "404 Not Found");
    }
    if (r->uri.len == 1 && *r->uri.data == '/') {
        sky_str_set(&r->uri, "/index.html");
    }
    if (!r->exten.len) {
        sky_str_set(&r->exten, ".html");
    }
    mime_type = sky_hash_find(&data->mime_types, sky_hash_key_lc(r->exten.data, r->exten.len), r->exten.data,
                              r->exten.len);
    if (!mime_type) {
        mime_type = &data->default_mime_type;
    }

    file = sky_pcalloc(r->pool, sizeof(http_file_t));

    if (r->headers_in.if_modified_since) {
        file->modified = true;
        sky_rfc_str_to_date(&r->headers_in.if_modified_since->value, &file->modified_time);
    }
    if (r->headers_in.range) {
        file->range = true;
        http_header_range(file, &r->headers_in.range->value);

        if (r->headers_in.if_range) {
            file->if_range = true;
            sky_rfc_str_to_date(&r->headers_in.if_range->value, &file->range_time);
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

    r->headers_out.content_type = mime_type->val;
    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Last-Modified");

    if (file->modified && file->modified_time == mtime) {
        close(fd);
        header->value = r->headers_in.if_modified_since->value;

        r->state = 304;

        sky_http_response_nobody(r);
        return;
    }
    header->value.data = sky_palloc(r->pool, 30);
    header->value.len = sky_date_to_rfc_str(mtime, header->value.data);

    if (file->range && (!file->if_range || file->range_time == mtime)) {
        r->state = 206;
        if (file->right == 0 || file->right > stat_buf.st_size) {
            file->right = stat_buf.st_size - 1;
        }
    } else {
        file->left = 0;
        file->right = stat_buf.st_size - 1;
    }
    file->file_defer = sky_defer_add(r->conn->coro, (sky_defer_func_t) close, (void *) (sky_uintptr_t) fd);

    sky_http_sendfile(r, fd, (sky_size_t) file->left, (sky_size_t) (file->right - file->left + 1),
                      (sky_size_t) stat_buf.st_size);

    sky_defer_remove(r->conn->coro, file->file_defer);
}

static sky_bool_t
http_header_range(http_file_t *file, sky_str_t *value) {
    sky_uchar_t *start, *end, p;
    sky_int64_t tmp;

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
        }
    }
    file->right = tmp;
    return true;
}

static void
http_mime_type_init(http_module_file_t *data) {
    sky_hash_init_t hash;
    sky_array_t arrays;
    sky_hash_key_t *hk;
    http_mime_type_t *mime_type;

    sky_str_set(&data->default_mime_type.val, "application/octet-stream");
    data->default_mime_type.binary = true;
    sky_array_init(&arrays, data->tmp_pool, 64, sizeof(sky_hash_key_t));

#define http_mime_type_push(_exten, _type, _binary)                            \
    hk = sky_array_push(&arrays);                                               \
    sky_str_set(&hk->key, _exten);                                              \
    hk->key_hash = sky_hash_key_lc(hk->key.data, hk->key.len);                  \
    hk->value = mime_type = sky_palloc(data->pool, sizeof(http_mime_type_t));   \
    mime_type->binary = _binary;                                                \
    sky_str_set(&mime_type->val, _type)


    http_mime_type_push(".html", "text/html", false);
    http_mime_type_push(".htm", "text/html", false);
    http_mime_type_push(".css", "text/css", false);
    http_mime_type_push(".js", "application/x-javascript", false);
    http_mime_type_push(".txt", "text/plain", false);
    http_mime_type_push(".json", "application/json", false);
    http_mime_type_push(".xml", "text/xml", false);
    http_mime_type_push(".pdf", "application/pdf", true);

    http_mime_type_push(".jpg", "image/jpeg", true);
    http_mime_type_push(".jpeg", "image/jpeg", true);
    http_mime_type_push(".png", "image/png", true);
    http_mime_type_push(".icon", "image/x-icon", true);
    http_mime_type_push(".gif", "image/gif", true);
    http_mime_type_push(".svg", "image/svg+xml", false);

    http_mime_type_push(".woff", "application/font-woff", true);
    http_mime_type_push(".7z", "application/x-7z-compressed", true);
    http_mime_type_push(".rar", "application/x-rar-compressed", true);
    http_mime_type_push(".zip", "application/zip", true);
    http_mime_type_push(".gz", "application/x-gzip", true);
    http_mime_type_push(".tar", "application/x-tar", true);

    http_mime_type_push(".mp3", "audio/mp3", true);
    http_mime_type_push(".ogg", "audio/ogg", true);
    http_mime_type_push(".mp4", "video/mp4", true);
    http_mime_type_push(".webm", "video/webm", false);


#undef http_mime_type_push

    hash.hash = &data->mime_types;
    hash.key = sky_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = sky_align(64, sky_cache_line_size);
    hash.pool = data->pool;
    if (!sky_hash_init(&hash, arrays.elts, arrays.nelts)) {
        sky_log_error("文件类型初始化出错");
    }
}