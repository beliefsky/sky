#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <core/json.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void server_start();

static sky_json_object_t *json_config_read(sky_pool_t *pool);


int
main() {
    sky_int64_t cpu_num;
    sky_uint32_t i;

    cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    if ((--cpu_num) < 0) {
        cpu_num = 0;
    }

    i = (sky_uint32_t) cpu_num;
    if (!i) {
        server_start();
        return 0;
    }
    for (;;) {
        pid_t pid = fork();
        switch (pid) {
            case -1:
                return 0;
            case 0: {
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                sched_setaffinity(0, sizeof(cpu_set_t), &mask);

                server_start();
            }
                break;
            default:
                if (--i) {
                    continue;
                }
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                sched_setaffinity(0, sizeof(cpu_set_t), &mask);

                server_start();
                break;
        }
        break;
    }

    return 0;
}

static void
server_start() {
    sky_json_object_t *json;

    sky_pool_t *pool;

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);


    json = json_config_read(pool);
    if (!json) {
        sky_destroy_pool(pool);
        return;
    }
    sky_destroy_pool(pool);
}

static sky_json_object_t *
json_config_read(sky_pool_t *pool) {
    sky_int32_t fd;
    sky_uint32_t size;
    ssize_t n;
    struct stat stat_buf;
    sky_buf_t *buf;
    sky_str_t data;

    fd = open("conf/sky.conf", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {

        return null;
    }

    fstat(fd, &stat_buf);
    size = (sky_uint32_t) stat_buf.st_size;

    buf = sky_buf_create(pool, size);
    data.data = buf->pos;
    data.len = size;

    for (;;) {
        n = read(fd, buf->last, size);
        if (n < 1) {
            close(fd);
            return null;
        }
        buf->last += n;
        if (n < size) {
            size -= n;
            continue;
        }
        break;
    }
    close(fd);
    return sky_json_object_decode(&data, pool, buf);
}