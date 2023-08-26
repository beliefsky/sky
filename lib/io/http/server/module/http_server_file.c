//
// Created by beliefsky on 2023/7/2.
//

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <io/http/http_server_file.h>
#include <core/memory.h>
#include <core/number.h>
#include <core/date.h>
#include <core/rbtree.h>
#include <core/crc32.h>

#define http_error_page(_r, _status, _msg)                              \
    (_r)->state = _status;                                              \
    sky_str_set(&(_r)->headers_out.content_type, "text/html");          \
    sky_http_response_str_len(r,sky_str_line("<html>\n<head><title>" \
    _msg                                                                \
    "</title></head>\n<body bgcolor=\"white\">\n<center><h1>"           \
    _msg                                                                \
    "</h1></center>\n<hr><center>sky</center>\n</body>\n</html>"),      \
    null,                                                               \
    null                                                                \
    )


typedef struct {
    sky_i64_t modified_time;
    sky_i64_t range_time;
    sky_i64_t left;
    sky_i64_t right;
    sky_bool_t modified: 1;
    sky_bool_t range: 1;
    sky_bool_t if_range: 1;
} http_file_t;

typedef struct {
    sky_str_t val;
    sky_bool_t binary: 1;
} http_mime_type_t;

typedef struct {
    sky_rb_tree_t cache_tree;
    sky_queue_t cache_queue;
    sky_timer_wheel_entry_t timer;
    http_mime_type_t default_mime_type;
    sky_str_t path;
    sky_str_t *prefix;
    sky_pool_t *pool;
    sky_event_loop_t *ev_loop;

    sky_bool_t (*pre_run)(sky_http_server_request_t *req, void *data);

    void *run_data;
    sky_u32_t cache_sec;
} http_module_file_t;


typedef struct {
    sky_rb_node_t node;
    sky_queue_t link;
    sky_str_t path;
    sky_i64_t modified_time;
    sky_i64_t file_size;
    sky_i64_t expire_at;
    http_module_file_t *module_file;
    sky_u32_t path_hash;
    sky_u32_t ref_count;
    sky_i32_t fd;
} file_cache_node_t;

static void http_run_handler(sky_http_server_request_t *r, void *data);

static void http_response_next(sky_http_server_request_t *r, void *data);

static file_cache_node_t *cache_node_file_get_ref(
        http_module_file_t *module_file,
        sky_pool_t *pool,
        const sky_str_t *uri_path
);

static void cache_node_file_unref(file_cache_node_t *node);

static void cache_node_free_timer(sky_timer_wheel_entry_t *timer);

static file_cache_node_t *rb_tree_get(
        sky_rb_tree_t *tree,
        const sky_str_t *path,
        sky_u32_t path_hash
);

static void rb_tree_insert(sky_rb_tree_t *tree, file_cache_node_t *node);

static sky_bool_t http_mime_type_get(const sky_str_t *exten, http_mime_type_t *type);

static sky_bool_t http_header_range(http_file_t *file, const sky_str_t *value);


sky_api sky_http_server_module_t *
sky_http_server_file_create(sky_event_loop_t *const ev_loop, const sky_http_server_file_conf_t *const conf) {
    sky_pool_t *const pool = sky_pool_create(2048);
    sky_http_server_module_t *const module = sky_palloc(pool, sizeof(sky_http_server_module_t));
    module->host.data = sky_palloc(pool, conf->host.len);
    module->host.len = conf->host.len;
    sky_memcpy(module->host.data, conf->host.data, conf->host.len);
    module->prefix.data = sky_palloc(pool, conf->prefix.len);
    module->prefix.len = conf->prefix.len;
    sky_memcpy(module->prefix.data, conf->prefix.data, conf->prefix.len);
    module->run = http_run_handler;

    http_module_file_t *const data = sky_palloc(pool, sizeof(http_module_file_t));
    sky_rb_tree_init(&data->cache_tree);
    sky_queue_init(&data->cache_queue);
    sky_timer_entry_init(&data->timer, cache_node_free_timer);
    sky_str_set(&data->default_mime_type.val, "application/octet-stream");
    data->default_mime_type.binary = true;
    data->path.data = sky_palloc(pool, conf->dir.len);
    data->path.len = conf->dir.len;
    sky_memcpy(data->path.data, conf->dir.data, conf->dir.len);
    data->prefix = &module->prefix;
    data->pool = pool;
    data->ev_loop = ev_loop;
    data->pre_run = conf->pre_run;
    data->run_data = conf->run_data;
    data->cache_sec = conf->cache_sec ?: 15;

    module->module_data = data;

    return module;
}

sky_api void
sky_http_server_file_destroy(sky_http_server_module_t *const server_file) {
    http_module_file_t *const data = server_file->module_data;
    sky_timer_wheel_unlink(&data->timer);

    sky_rb_node_t *item;
    file_cache_node_t *node;
    for (;;) {
        item = sky_rb_tree_first(&data->cache_tree);
        if (!item) {
            break;
        }
        sky_rb_tree_del(&data->cache_tree, item);

        node = sky_type_convert(item, file_cache_node_t, node);
        if (node->fd != -1) {
            close(node->fd);
        }
        sky_free(node);
    }
    sky_pool_destroy(data->pool);
}

static void
http_run_handler(sky_http_server_request_t *const r, void *const data) {
    http_module_file_t *const module_file = data;

    r->uri.data += module_file->prefix->len;
    r->uri.len -= module_file->prefix->len;
    if (sky_unlikely(!r->uri.len)) {
        http_error_page(r, 404, "404 Not Found");
        return;
    }
    if (r->uri.len == 1 && *r->uri.data == '/') {
        sky_str_set(&r->uri, "/index.html");
        sky_str_set(&r->exten, ".html");
    }

    if (module_file->pre_run) {
        if (!module_file->pre_run(r, module_file->run_data)) {
            http_error_page(r, 400, "400 Bad Request");
            return;
        }
    }

    http_mime_type_t mime_type;
    if (!r->exten.len) {
        mime_type = module_file->default_mime_type;
    } else {
        const sky_str_t lower = {
                .len = r->exten.len,
                .data = sky_palloc(r->pool, sizeof(r->exten.len) + 1)
        };
        sky_str_lower(lower.data, r->exten.data, lower.len);
        lower.data[lower.len] = '\0';

        if (!http_mime_type_get(&lower, &mime_type)) {
            mime_type = module_file->default_mime_type;
        }
    }


    http_file_t *const file = sky_pcalloc(r->pool, sizeof(http_file_t));

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

    file_cache_node_t *const node = cache_node_file_get_ref(module_file, r->pool, &r->uri);
    if (node->fd == -1) {
        http_error_page(r, 404, "404 Not Found");
        return;
    }

    r->headers_out.content_type = mime_type.val;
    sky_http_server_header_t *const header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Last-Modified");

    if (file->modified && file->modified_time == node->modified_time) {
        header->val = *r->headers_in.if_modified_since;
        r->state = 304;
        cache_node_file_unref(node);
        sky_http_response_nobody(r, null, null);
        return;
    }
    header->val.data = sky_palloc(r->pool, 30);
    header->val.len = sky_date_to_rfc_str(node->modified_time, header->val.data);

    if (file->range && (!file->if_range || file->range_time == node->modified_time)) {
        r->state = 206;
        if (file->right == 0 || file->right > node->file_size) {
            file->right = node->file_size - 1;
        }
    } else {
        file->left = 0;
        file->right = node->file_size - 1;
    }

    sky_http_response_file(
            r,
            node->fd,
            file->left,
            (sky_usize_t) (file->right - file->left + 1),
            (sky_usize_t) node->file_size,
            http_response_next,
            node
    );
}

static void
http_response_next(sky_http_server_request_t *const r, void *const data) {
    file_cache_node_t *const node = data;
    cache_node_file_unref(node);

    sky_http_server_req_finish(r);
}

static file_cache_node_t *
cache_node_file_get_ref(http_module_file_t *module_file, sky_pool_t *pool, const sky_str_t *uri_path) {
    sky_u32_t path_hash = sky_crc32_init();
    path_hash = sky_crc32c_update(path_hash, uri_path->data, uri_path->len);
    path_hash = sky_crc32_final(path_hash);

    file_cache_node_t *node = rb_tree_get(&module_file->cache_tree, uri_path, path_hash);
    if (node) {
        if (sky_queue_linked(&node->link)) {
            sky_queue_remove(&node->link);
        }
        ++node->ref_count;
        return node;
    }
    sky_uchar_t *ptr = sky_malloc(sizeof(file_cache_node_t) + uri_path->len);
    node = (file_cache_node_t *) ptr;
    ptr += sizeof(file_cache_node_t);

    sky_queue_init_node(&node->link);
    node->module_file = module_file;
    node->path_hash = path_hash;
    node->ref_count = 1;
    node->fd = -1;
    node->path.data = ptr;
    node->path.len = uri_path->len;
    sky_memcpy(node->path.data, uri_path->data, uri_path->len);
    rb_tree_insert(&module_file->cache_tree, node);


    sky_char_t *const path = sky_palloc(pool, module_file->path.len + uri_path->len + 1);
    sky_memcpy(path, module_file->path.data, module_file->path.len);
    sky_memcpy(path + module_file->path.len, uri_path->data, uri_path->len + 1);

    const sky_i32_t fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return node;
    }
    struct stat stat_buf;
    fstat(fd, &stat_buf);
    if (sky_unlikely(S_ISDIR(stat_buf.st_mode))) {
        close(fd);
        return node;
    }
    node->fd = fd;
#if defined(__APPLE__)
    node->modified_time = stat_buf.st_mtimespec.tv_sec;
#else
    node->modified_time = stat_buf.st_mtim.tv_sec;
#endif
    node->file_size = stat_buf.st_size;

    return node;
}

static void
cache_node_file_unref(file_cache_node_t *const node) {
    if ((--node->ref_count) != 0) {
        return;
    }
    http_module_file_t *const module_file = node->module_file;
    node->expire_at = sky_event_now(module_file->ev_loop) + module_file->cache_sec;
    sky_queue_insert_prev(&module_file->cache_queue, &node->link);
    if (!sky_timer_linked(&module_file->timer)) {
        sky_event_timeout_set(module_file->ev_loop, &module_file->timer, module_file->cache_sec);
    }
}

static void
cache_node_free_timer(sky_timer_wheel_entry_t *const timer) {
    http_module_file_t *const module_file = sky_type_convert(timer, http_module_file_t, timer);
    sky_queue_t *item;
    file_cache_node_t *node;

    const sky_i64_t now_time = sky_event_now(module_file->ev_loop);
    while (!sky_queue_empty(&module_file->cache_queue)) {
        item = sky_queue_next(&module_file->cache_queue);
        node = sky_type_convert(item, file_cache_node_t, link);
        if (node->expire_at > now_time) {
            sky_event_timeout_set(module_file->ev_loop, &module_file->timer, (sky_u32_t) (node->expire_at - now_time));
            return;
        }
        sky_queue_remove(item);
        sky_rb_tree_del(&node->module_file->cache_tree, &node->node);
        if (node->fd != -1) {
            close(node->fd);
        }
        sky_free(node);
    }
}


static file_cache_node_t *
rb_tree_get(
        sky_rb_tree_t *const tree,
        const sky_str_t *const path,
        const sky_u32_t path_hash
) {
    sky_rb_node_t *node = tree->root;
    file_cache_node_t *tmp;
    sky_i32_t r;

    while (node != &tree->sentinel) {
        tmp = sky_type_convert(node, file_cache_node_t, node);
        if (tmp->path_hash == path_hash) {
            r = sky_str_cmp(&tmp->path, path);
            if (!r) {
                return tmp;
            }
            node = r > 0 ? node->left : node->right;
        } else {
            node = tmp->path_hash > path_hash ? node->left : node->right;
        }
    }

    return null;
}

static void
rb_tree_insert(sky_rb_tree_t *const tree, file_cache_node_t *const node) {
    if (sky_rb_tree_is_empty(tree)) {
        sky_rb_tree_link(tree, &node->node, null);
        return;
    }
    sky_rb_node_t **p, *temp = tree->root;
    file_cache_node_t *other;

    for (;;) {
        other = sky_type_convert(temp, file_cache_node_t, node);
        if (node->path_hash == other->path_hash) {
            p = sky_str_cmp(&node->path, &other->path) < 0 ? &temp->left : &temp->right;
        } else {
            p = node->path_hash < other->path_hash ? &temp->left : &temp->right;
        }

        if (*p == &tree->sentinel) {
            *p = &node->node;
            sky_rb_tree_link(tree, &node->node, temp);
            return;
        }
        temp = *p;
    }
}


static sky_bool_t
http_header_range(http_file_t *const file, const sky_str_t *const value) {
    sky_isize_t index;
    sky_usize_t size;
    sky_uchar_t *start;

    index = sky_str_index_char(value, '=');
    if (sky_unlikely(index == -1)) {
        return false;
    }
    ++index;
    size = value->len - (sky_usize_t) index;
    start = value->data + index;

    index = sky_str_len_index_char(start, size, '-');
    if (sky_unlikely(index == -1)) {
        return false;
    } else if (index > 0) {
        if (sky_unlikely(!sky_str_len_to_i64(start, (sky_usize_t) index, &file->left))) {
            return false;
        }
    }

    ++index;
    size -= (sky_usize_t) index;
    start += index;
    if (size > 0) {
        if (sky_unlikely(!sky_str_len_to_i64(start, size, &file->right))) {
            return false;
        }
    }


    return true;
}


static sky_inline sky_bool_t
http_mime_type_get(const sky_str_t *const exten, http_mime_type_t *const type) {
#define mine_set(_type, _b)         \
    sky_str_set(&type->val, _type);  \
    type->binary = (_b)

    const sky_uchar_t *p = exten->data;
    switch (exten->len) {
        case 3: {
            switch (sky_str4_switch(p)) {
                case sky_str4_num('.', 'p', 's', '\0'):
                case sky_str4_num('.', 'a', 'i', '\0'):
                mine_set("application/postscript", true);
                    return true;
                case sky_str4_num('.', 'j', 's', '\0'):
                mine_set("application/javascript", false);
                    return true;
                case sky_str4_num('.', '7', 'z', '\0'):
                mine_set("application/x-7z-compressed", true);
                    return true;
                case sky_str4_num('.', 'g', 'z', '\0'):
                mine_set("application/x-gzip", true);
                    return true;
                case sky_str4_num('.', 'r', 'a', '\0'):
                mine_set("audio/x-realaudio", true);
                    return true;
                case sky_str4_num('.', 't', 's', '\0'):
                mine_set("video/mp2t", true);
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
                case sky_str4_num('.', 'm', 'm', 'l'):
                mine_set("text/mathml", false);
                    return true;
                case sky_str4_num('.', 't', 'x', 't'):
                mine_set("text/plain", false);
                    return true;
                case sky_str4_num('.', 'j', 'a', 'd'):
                mine_set("text/vnd.sun.j2me.app-descriptor", false);
                    return true;
                case sky_str4_num('.', 'w', 'm', 'l'):
                mine_set("text/vnd.wap.wml", false);
                    return true;
                case sky_str4_num('.', 'h', 't', 'c'):
                mine_set("text/x-component", false);
                    return true;
                case sky_str4_num('.', 'x', 'm', 'l'):
                mine_set("text/xml", false);
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
                case sky_str4_num('.', 't', 'i', 'f'):
                mine_set("image/tiff ", true);
                    return true;
                case sky_str4_num('.', 'i', 'c', 'o'):
                mine_set("image/x-icon", true);
                    return true;
                case sky_str4_num('.', 'j', 'n', 'g'):
                mine_set("image/x-jng", true);
                    return true;
                case sky_str4_num('.', 'b', 'm', 'p'):
                mine_set("image/x-ms-bmp", true);
                    return true;
                case sky_str4_num('.', 'r', 's', 's'):
                mine_set("application/rss+xml", false);
                    return true;
                case sky_str4_num('.', 'p', 'd', 'f'):
                mine_set("application/pdf", true);
                    return true;
                case sky_str4_num('.', 'j', 'a', 'r'):
                case sky_str4_num('.', 'w', 'a', 'r'):
                case sky_str4_num('.', 'e', 'a', 'r'):
                mine_set("application/java-archive", true);
                    return true;
                case sky_str4_num('.', 'h', 'q', 'x'):
                mine_set("application/mac-binhex40", true);
                    return true;
                case sky_str4_num('.', 'd', 'o', 'c'):
                mine_set("application/msword", true);
                    return true;
                case sky_str4_num('.', 'e', 's', 'p'):
                mine_set("application/postscript", true);
                    return true;
                case sky_str4_num('.', 'r', 't', 'f'):
                mine_set("application/rtf", false);
                    return true;
                case sky_str4_num('.', 'k', 'm', 'l'):
                mine_set("application/vnd.google-earth.kml+xml", false);
                    return true;
                case sky_str4_num('.', 'k', 'm', 'z'):
                mine_set("application/vnd.google-earth.kmz", true);
                    return true;
                case sky_str4_num('.', 'x', 'l', 's'):
                mine_set("application/vnd.ms-excel", true);
                    return true;
                case sky_str4_num('.', 'e', 'o', 't'):
                mine_set("application/vnd.ms-fontobject", true);
                    return true;
                case sky_str4_num('.', 'p', 'p', 't'):
                mine_set("application/vnd.ms-powerpoint", true);
                    return true;
                case sky_str4_num('.', 'o', 'd', 'g'):
                mine_set("application/vnd.oasis.opendocument.graphics", true);
                    return true;
                case sky_str4_num('.', 'o', 'd', 'd'):
                mine_set("application/vnd.oasis.opendocument.presentation", true);
                    return true;
                case sky_str4_num('.', 'o', 'd', 's'):
                mine_set("application/vnd.oasis.opendocument.spreadsheet", true);
                    return true;
                case sky_str4_num('.', 'o', 'd', 't'):
                mine_set("application/vnd.oasis.opendocument.text", false);
                    return true;
                case sky_str4_num('.', 'r', 'u', 'n'):
                mine_set("application/x-makeself", true);
                    return true;
                case sky_str4_num('.', 'r', 'a', 'r'):
                mine_set("application/x-rar-compressed", true);
                    return true;
                case sky_str4_num('.', 'r', 'p', 'm'):
                mine_set(" application/x-redhat-package-manager", true);
                    return true;
                case sky_str4_num('.', 'd', 'e', 'r'):
                case sky_str4_num('.', 'p', 'e', 'm'):
                case sky_str4_num('.', 'c', 'r', 't'):
                mine_set("application/x-x509-ca-cert", false);
                    return true;
                case sky_str4_num('.', 'x', 'p', 'i'):
                mine_set("application/x-xpinstall", true);
                    return true;
                case sky_str4_num('.', 'z', 'i', 'p'):
                mine_set("application/zip", true);
                    return true;
                case sky_str4_num('.', 't', 'a', 'r'):
                mine_set("application/x-tar", true);
                    return true;
                case sky_str4_num('.', 'b', 'i', 'n'):
                case sky_str4_num('.', 'e', 'x', 'e'):
                case sky_str4_num('.', 'd', 'l', 'l'):
                case sky_str4_num('.', 'd', 'e', 'b'):
                case sky_str4_num('.', 'd', 'm', 'g'):
                case sky_str4_num('.', 'i', 's', 'o'):
                case sky_str4_num('.', 'i', 'm', 'g'):
                case sky_str4_num('.', 's', 'm', 'i'):
                case sky_str4_num('.', 's', 'm', 'p'):
                case sky_str4_num('.', 's', 'm', 'm'):
                mine_set("application/octet-stream ", true);
                    return true;
                case sky_str4_num('.', 'k', 'a', 'r'):
                case sky_str4_num('.', 'm', 'i', 'd'):
                mine_set("audio/midi", true);
                    return true;
                case sky_str4_num('.', 'm', '4', 'a'):
                mine_set("audio/x-m4a", true);
                    return true;
                case sky_str4_num('.', 'm', 'p', '3'):
                mine_set("audio/mp3", true);
                    return true;
                case sky_str4_num('.', 'o', 'g', 'g'):
                mine_set("audio/ogg", true);
                    return true;
                case sky_str4_num('.', '3', 'g', 'p'):
                mine_set("video/3gpp", true);
                    return true;
                case sky_str4_num('.', 'm', 'p', '4'):
                mine_set("video/mp4", true);
                    return true;
                case sky_str4_num('.', 'm', 'p', 'g'):
                mine_set("video/mpeg", true);
                    return true;
                case sky_str4_num('.', 'm', 'o', 'v'):
                mine_set("video/quicktime", true);
                    return true;
                case sky_str4_num('.', 'f', 'l', 'v'):
                mine_set("video/x-flv", true);
                    return true;
                case sky_str4_num('.', 'm', '4', 'v'):
                mine_set("video/x-m4v", true);
                    return true;
                case sky_str4_num('.', 'm', 'n', 'g'):
                mine_set("video/x-mng", true);
                    return true;
                case sky_str4_num('.', 'a', 's', 'x'):
                case sky_str4_num('.', 'a', 's', 'f'):
                mine_set("video/x-ms-asf", true);
                    return true;
                case sky_str4_num('.', 'w', 'm', 'v'):
                mine_set("video/x-ms-wmv", true);
                    return true;
                case sky_str4_num('.', 'a', 'v', 'i'):
                mine_set("video/x-msvideo", true);
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
                case sky_str4_num('a', 't', 'o', 'm'):
                mine_set("application/atom+xml", false);
                    return true;
                case sky_str4_num('j', 's', 'o', 'n'):
                mine_set("application/json", false);
                    return true;
                case sky_str4_num('m', '3', 'u', '8'):
                mine_set("application/vnd.apple.mpegurl", false);
                    return true;
                case sky_str4_num('p', 'p', 't', 'x'):
                mine_set("application/vnd.openxmlformats-officedocument.presentationml.presentation", true);
                    return true;
                case sky_str4_num('x', 'l', 's', 'x'):
                mine_set("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", true);
                    return true;
                case sky_str4_num('d', 'o', 'c', 'x'):
                mine_set("application/vnd.openxmlformats-officedocument.wordprocessingml.document", true);
                    return true;
                case sky_str4_num('w', 'm', 'l', 'c'):
                mine_set("application/vnd.wap.wmlc", false);
                    return true;
                case sky_str4_num('w', 'a', 's', 'n'):
                mine_set("application/wasm", true);
                    return true;
                case sky_str4_num('x', 's', 'p', 'f'):
                mine_set("application/xspf+xml", false);
                    return true;
                case sky_str4_num('j', 'p', 'e', 'g'):
                mine_set("image/jpeg", true);
                    return true;
                case sky_str4_num('s', 'v', 'g', 'z'):
                mine_set("image/svg+xml", false);
                    return true;
                case sky_str4_num('t', 'i', 'f', 'f'):
                mine_set("image/tiff ", true);
                    return true;
                case sky_str4_num('w', 'b', 'm', 'p'):
                mine_set("image/vnd.wap.wbmp", true);
                    return true;
                case sky_str4_num('w', 'e', 'b', 'p'):
                mine_set("image/webp", true);
                    return true;
                case sky_str4_num('w', 'o', 'f', 'f'):
                mine_set("font/woff", true);
                    return true;
                case sky_str4_num('m', 'i', 'd', 'i'):
                mine_set("audio/midi", true);
                    return true;
                case sky_str4_num('3', 'g', 'p', 'p'):
                mine_set("video/3gpp", true);
                    return true;
                case sky_str4_num('m', 'p', 'e', 'g'):
                mine_set("video/mpeg", true);
                    return true;
                case sky_str4_num('w', 'e', 'b', 'm'):
                mine_set("video/webm", true);
                    return true;
                default:
                    break;
            }
            break;
        }
        case 6: {
            ++p;
            switch (sky_str4_switch(p)) {
                case sky_str4_num('w', 'o', 'f', 'f'):
                    if (sky_likely(p[4] == '2')) {
                        mine_set("font/woff2", true);
                        return true;
                    }
                    break;
                case sky_str4_num('x', 'h', 't', 'm'):
                    if (sky_likely(p[4] == 'l')) {
                        mine_set("application/x-xpinstall", false);
                        return true;
                    }
                    break;
                default:
                    break;
            }
        }
        default:
            break;
    }

    return false;

#undef mine_set
}