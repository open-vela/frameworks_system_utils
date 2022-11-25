/*
 * Copyright (C) 2020 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <netpacket/rpmsg.h>

#include <kvdb.h>
#include <unqlite.h>

#define KVDB_MEM                0
#define KVDB_PERSIST            1
#define KVDB_COUNT              2

#define KVFD_LOCAL              0
#define KVFD_REMOTE             1
#define KVFD_COUNT              2

#define KVFD_MAX                8

#ifndef MIN
    #define MIN(n,m)   (((n) < (m)) ? (n) : (m))
#endif

/****************************************************************************
 * Database Types
 ****************************************************************************/

typedef int (*kvdb_consume)(const char* key, size_t key_len,
                            const char* value, size_t val_len,
                            void* cookie);

typedef struct kvdb_consume_data {
    kvdb_consume consume;
    void* cookie;
    unqlite_kv_cursor* cur;
    const char* key;
    size_t key_len;
} kvdb_consume_data;

typedef struct kvdb_monitor {
    int                      fd;
    LIST_ENTRY(kvdb_monitor) entry;
    char                     key[0];
} kvdb_monitor;

typedef LIST_HEAD(kvdb_monitor_head, kvdb_monitor) kvdb_monitor_head;

typedef struct kvdb {
    int               fd[KVFD_COUNT];
    unqlite*          db[KVDB_COUNT];
    int               efd;
    kvdb_monitor_head head;
} kvdb;

/****************************************************************************
 * Database Functions
 ****************************************************************************/

static bool kvdb_is_comment(const char* line)
{
    size_t i = strspn(line, " \t\r\n");
    return line[i] == '\0' || line[i] == '#';
}

static bool kvdb_is_readonly(const char* key)
{
    return strncmp(key, "ro.", 3) == 0;
}

static int kvdb_get_index(const char* key)
{
    if (strncmp(key, "persist.", 8) == 0)
        return KVDB_PERSIST;
    else
        return KVDB_MEM;
}

static int kvdb_set(unqlite* db[], const char* key, size_t key_len,
                    const char* value, size_t val_len, bool force)
{
    if (--key_len >= PROP_NAME_MAX)
        return -E2BIG;

    if (!key || key[key_len])
        return -EINVAL;

    if (--val_len >= PROP_VALUE_MAX)
        return -E2BIG;

    if (value[val_len])
        return -EINVAL;

    if(kvdb_is_readonly(key) && !force)
        return -EPERM;

    /* in environment variable? */
    if (getenv(key)) {
        int ret = setenv(key, value, 1);
        if (ret < 0)
            ret = -errno;
        return ret;
    }

    /* no, then try database  */
    int i = kvdb_get_index(key);
    if (i < 0)
        return i;

    return unqlite_kv_store(db[i], key, ++key_len, value, ++val_len);
}

static int kvdb_get(unqlite* db[], const char* key, size_t key_len, char* value)
{
    if (--key_len >= PROP_NAME_MAX)
        return -E2BIG;

    if (key[key_len])
        return -EINVAL;

    /* in environment variable? */
    const char* env = getenv(key);
    if (env) {
        size_t len = strlen(env) + 1;
        if (len > PROP_VALUE_MAX)
            return -E2BIG;

        if (value)
            memcpy(value, env, len);

        return len;
    }

    /* no, then try database  */
    int i = kvdb_get_index(key);
    if (i < 0)
        return i;

    unqlite_int64 val_len = value ? PROP_VALUE_MAX : 0;
    int ret = unqlite_kv_fetch(db[i], key, ++key_len, value, &val_len);
    if (ret < 0)
        return ret;

    if ((val_len <= 0) || (value && value[val_len - 1]))
        return -EINVAL;

    return val_len;
}

static int kvdb_delete(unqlite* db[], const char* key, size_t key_len)
{
    if (--key_len >= PROP_NAME_MAX)
        return -E2BIG;

    if (key[key_len])
        return -EINVAL;

    if(kvdb_is_readonly(key))
        return -EPERM;

    /* in environment variable? */
    if (getenv(key)) {
        int ret = unsetenv(key);
        if (ret < 0)
            ret = -errno;
        return ret;
    }

    /* no, then try database  */
    int i = kvdb_get_index(key);
    if (i < 0)
        return i;

    return unqlite_kv_delete(db[i], key, ++key_len);
}

static int kvdb_list_value(const void* value, unsigned int len, void* arg)
{
    kvdb_consume_data* data = arg;
    return data->consume(data->key, data->key_len, value, len, data->cookie);
}

static int kvdb_list_key(const void* value, unsigned int len, void* arg)
{
    kvdb_consume_data* data = arg;
    data->key = value;
    data->key_len = len;
    return unqlite_kv_cursor_data_callback(data->cur, kvdb_list_value, data);
}

static int kvdb_list(unqlite* db[], kvdb_consume consume, void* cookie)
{
    for (int i = 0; i < KVDB_COUNT; i++) {
        unqlite_kv_cursor* cur = NULL;
        unqlite_kv_cursor_init(db[i], &cur);

        kvdb_consume_data data = {
            .consume = consume,
            .cookie  = cookie,
            .cur     = cur,
        };

        unqlite_kv_cursor_first_entry(cur);
        while (unqlite_kv_cursor_valid_entry(cur)) {
            int ret = unqlite_kv_cursor_key_callback(cur, kvdb_list_key, &data);
            if (ret < 0) { /* exit loop demanded by consume */
                unqlite_kv_cursor_release(db[i], cur);
                return ret;
            }
            unqlite_kv_cursor_next_entry(cur);
        }
        unqlite_kv_cursor_release(db[i], cur);
    }

    return 0;
}

static int kvdb_commit(unqlite* db[])
{
    int ret = 0;

    for (int i = 0; i < KVDB_COUNT; i++) {
        int r = unqlite_commit(db[i]);
        if (r < 0 && ret == 0)
            ret = r;
    }

    return ret;
}

static void kvdb_uninit(unqlite* db[])
{
    for (int i = 0; i < KVDB_COUNT; i++) {
        if (db[i]) {
            unqlite_close(db[i]);
            db[i] = NULL;
        }
    }
}

static int kvdb_load(unqlite* db[], bool force)
{
    const char *src = CONFIG_KVDB_SOURCE_PATH;
    char tmpb[PATH_MAX];
    const char *path = tmpb;
    const char *sep;

    while (*src) {
        sep = strchr(src, ';');
        if (sep) {
            strlcpy(tmpb, src, MIN(PATH_MAX, sep - src + 1));
            src = sep + 1;
        } else {
            path = src;
            src += strlen(src);
        }

        FILE* f = fopen(path, "r");
        if (!f)
            continue;

        char buf[PROP_MSG_MAX];
        while (fgets(buf, PROP_MSG_MAX, f)) {
            if (kvdb_is_comment(buf))
                continue;

            char* tmp;
            char* key = strtok_r(buf, "=", &tmp);
            char* value = strtok_r(NULL, "\n", &tmp);
            if (!key || !value)
                continue;

            int i = kvdb_get_index(key);
            if (i < 0)
                continue;

            size_t key_len = strlen(key) + 1;
            if(!force && kvdb_get(db, key, key_len, NULL) >= 0)
                continue;

            kvdb_set(db, key, key_len, value, strlen(value) + 1, true);
        }

        fclose(f);
    }
    return 0;
}

/* Open a monitor channel, add the [key, fd] pair to the monitor list and
 * add the pollfd to the pollfd array.
 */
static int kvdb_monitor_open(kvdb* kv, int fd, const char* key,
                             size_t key_len)
{
    /* Malloc monitor element to store [key, fd] pair */
    kvdb_monitor* mon = zalloc(sizeof(kvdb_monitor) + key_len);
    if (mon == NULL) {
        return -ENOMEM;
    }

    /* Add the monitor fd to the epoll */
    struct epoll_event ev = {
        .data.ptr = &mon->fd,
        .events = EPOLLIN
    };
    int ret = epoll_ctl(kv->efd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        free(mon);
        return ret;
    }

    /* Add the [key, fd] pair to the monitor list */
    mon->fd = fd;
    strcpy(mon->key, key);
    LIST_INSERT_HEAD(&kv->head, mon, entry);

    return 0;
}

/* Close the monitor fd, remove the fd from the monitor list and empty
 * corresponding pollfd.
 */
static void kvdb_monitor_close(kvdb* kv, struct epoll_event* ev)
{
    kvdb_monitor* mon = (kvdb_monitor*)ev->data.ptr;

    /* Close the monitor fd and delete it from epoll */
    epoll_ctl(kv->efd, EPOLL_CTL_DEL, mon->fd, NULL);
    close(mon->fd);

    /* Remove the element from the monitor list */
    LIST_REMOVE(mon, entry);
    free(mon);
}

/* Notify the client the value changed (updated or deleted) */
static void kvdb_monitor_notify(kvdb* kv, const char* key, const char* value)
{
    size_t key_len = strlen(key) + 1;

    /* value != NULL
      *-------------------------------------*
      |   1   |   1   | key_len |  val_len  |
      |-------------------------------------|
      |key_len|val_len|[key'\0']|[value'\0']|
      *-------------------------------------*
      * value == NULL
      *-------------------------*
      |   1   |   1   | key_len |
      |-------------------------|
      |key_len|   0   |[key'\0']|
      *-------------------------*/

    size_t val_len = value ? strlen(value) + 1 : 0;
    char cmd[2] = {key_len, val_len};
    struct iovec iov[3] = {
        {.iov_base = cmd          , .iov_len = 2      },
        {.iov_base = (char *)key  , .iov_len = key_len},
        {.iov_base = (char *)value, .iov_len = val_len},
    };

    struct msghdr msg = {0};
    msg.msg_iov = iov;
    msg.msg_iovlen = value ? 3 : 2;

    kvdb_monitor* mon;
    kvdb_monitor* tmp;
    LIST_FOREACH_SAFE(mon, &kv->head, entry, tmp) {
        if (fnmatch(mon->key, key, FNM_NOESCAPE) != 0)
            continue;

        if (sendmsg(mon->fd, &msg, 0) < 0) {
            /* Client close or some error happends, stop monitor */
            LIST_REMOVE(mon, entry);
            close(mon->fd);
            free(mon);
        }
    }
}

static int kvdb_init(unqlite* db[])
{
    static const char* path[KVDB_COUNT] = {
        [KVDB_MEM]     = "",
        [KVDB_PERSIST] = CONFIG_KVDB_PERSIST_PATH,
    };

    int ret = 0;

    /* open database */
    memset(db, 0, sizeof(*db) * KVDB_COUNT);
    for (int i = 0; i < KVDB_COUNT; i++) {
        if (path[i][0])
            ret = unqlite_open(&db[i], path[i], UNQLITE_OPEN_CREATE | UNQLITE_OPEN_OMIT_JOURNALING);
        else
            ret = unqlite_open(&db[i], NULL, UNQLITE_OPEN_IN_MEMORY);

        if (ret < 0)
            goto out;
    }

    /* load initial value from text file */
    kvdb_load(db, false);
    kvdb_commit(db);
    return 0;

out:
    kvdb_uninit(db);
    return ret;
}

/****************************************************************************
 * Network Functions
 ****************************************************************************/

static int kvdb_bind(int fd[])
{
    const int family[] = {
        [KVFD_LOCAL]  = AF_UNIX,
        [KVFD_REMOTE] = AF_RPMSG,
    };

    const struct sockaddr_un addr0 = {
        .sun_family = AF_UNIX,
        .sun_path   = PROP_SERVER_PATH,
    };

    const struct sockaddr_rpmsg addr1 = {
        .rp_family = AF_RPMSG,
        .rp_cpu    = "",
        .rp_name   = PROP_SERVER_PATH,
    };

    const struct sockaddr* addr[] = {
        [KVFD_LOCAL]  = (const struct sockaddr*)&addr0,
        [KVFD_REMOTE] = (const struct sockaddr*)&addr1,
    };

    const socklen_t addrlen[] = {
        [KVFD_LOCAL]  = sizeof(struct sockaddr_un),
        [KVFD_REMOTE] = sizeof(struct sockaddr_rpmsg),
    };

    memset(fd, 0, sizeof(*fd) * KVFD_COUNT);

    for (int i = 0; i < KVFD_COUNT; i++) {
        fd[i] = socket(family[i], SOCK_STREAM, 0);
        if (fd[i] < 0)
            continue;

        int ret = bind(fd[i], addr[i], addrlen[i]);
        if (ret < 0)
            return ret;

        ret = listen(fd[i], SOMAXCONN);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void kvdb_unbind(int fd[])
{
    for (int i = 0; i < KVFD_COUNT; i++)
        if (fd[i] > 0)
            close(fd[i]);
}

static int kvdb_list_consume(const char* key, size_t key_len,
                             const char* value, size_t val_len,
                             void* cookie)
{
    char cmd[2] = {
        key_len, val_len
    };

    struct iovec iov[3] = {
        {.iov_base = cmd         , .iov_len = 2      },
        {.iov_base = (char*)key  , .iov_len = key_len},
        {.iov_base = (char*)value, .iov_len = val_len},
    };

    struct msghdr msg = {0};
    msg.msg_iov    = iov;
    msg.msg_iovlen = 3;

    int fd = (int)cookie;
    int ret = sendmsg(fd, &msg, 0);
    return ret > 0 ? 0 : ret;
}

static ssize_t kvdb_recv(int sockfd, char *buf, size_t offset, size_t len)
{
    while (offset < len) {
        ssize_t ret = recv(sockfd, buf + offset, len - offset, 0);
        if (ret < 0)
            return ret;
        if (ret == 0)
            return -ENODATA;
        offset += ret;
    }

    return len;
}

static bool kvdb_client(kvdb* kv, int fd)
{
    bool dirty = false;
    ssize_t len;

#if CONFIG_KVDB_TIMEOUT_INTERVAL
    struct timeval timeout = {
        .tv_sec  = CONFIG_KVDB_TIMEOUT_INTERVAL,
        .tv_usec = 0,
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    char msg[PROP_MSG_MAX];
    msg[0] = msg[1] = msg[2] = 0; /* zero the first key bytes */

    len = recv(fd, msg, PROP_MSG_MAX, 0);
    if (len <= 0)
        goto out;

    switch (msg[0]) {
        case 'D': {
            size_t key_len = (unsigned char)msg[1];
            size_t end_pos = key_len + 2;
            if (end_pos >= PROP_MSG_MAX)
                break;

            const char* key = msg + 2;
            len = kvdb_recv(fd, msg, len, end_pos);
            if (len > 0) {
                int32_t err = kvdb_delete(kv->db, key, key_len);
                if (err >= 0) {
                    dirty = true;
                    kvdb_monitor_notify(kv, key, NULL);
                }
                send(fd, &err, 4, 0);
            }
            break;
        }
        case 'G': {
            size_t key_len = (unsigned char)msg[1];
            size_t end_pos = key_len + 2;
            if (end_pos >= PROP_MSG_MAX)
                break;

            const char* key = msg + 2;
            char value[PROP_VALUE_MAX];
            len = kvdb_recv(fd, msg, len, end_pos);
            if (len > 0) {
                len = kvdb_get(kv->db, key, key_len, value);
                if (len > 0)
                    send(fd, value, len, 0);
            }
            break;
        }
        case 'S': {
            size_t key_len = (unsigned char)msg[1];
            size_t val_len = (unsigned char)msg[2];
            size_t end_pos = key_len + val_len + 3;
            if (end_pos >= PROP_MSG_MAX)
                break;

            const char* key = msg + 3;
            const char* value = key + key_len;
            len = kvdb_recv(fd, msg, len, end_pos);
            if (len > 0) {
                int32_t err = kvdb_set(kv->db, key, key_len, value, val_len, false);
                if (err >= 0) {
                    dirty = true;
                    kvdb_monitor_notify(kv, key, value);
                }
                send(fd, &err, 4, 0);
            }
            break;
        }
        case 'L': {
            kvdb_list(kv->db, kvdb_list_consume, (void *)(uintptr_t)fd);
            send(fd, "\0", 2, 0); /* terminator */
            break;
        }
        case 'C': {
            kvdb_commit(kv->db);
            break;
        }
        case 'R': {
            kvdb_load(kv->db, true);
            break;
        }
        case 'M': {
            /* Property monitor open operation */
            size_t key_len = (unsigned char)msg[1];
            size_t end_pos = key_len + 2;
            if (end_pos >= PROP_MSG_MAX)
                break;

            const char* key = msg + 2;
            len = kvdb_recv(fd, msg, len, end_pos);
            if (len < 0 || key[key_len - 1]) {
                break;
            }
            if (len > 0) {
                int32_t err = kvdb_monitor_open(kv, fd, key, key_len);
                send(fd, &err, 4, 0);
            }
            /* Direct return, not close the monitor fd */
            return false;
        }
    }

out:
    close(fd); /* done, close client socket */
    return dirty;
}

static void kvdb_server(kvdb* kv)
{
    struct epoll_event evs[KVFD_MAX];
    struct timespec ts;

    kv->efd = epoll_create(KVFD_MAX);
    if (kv->efd < 0)
        return;

    for (int i = 0; i < KVFD_COUNT; i++) {
        if (kv->fd[i] > 0) {
            evs[0].data.ptr = &kv->fd[i];
            evs[0].events = EPOLLIN;
            if (epoll_ctl(kv->efd, EPOLL_CTL_ADD, kv->fd[i], &evs[0]) < 0) {
                close(kv->efd);
                return;
            }
        }
    }

    time_t next = 0;

    while (1) {
        int timeout = -1;

        /* commit the change after timeout */
        if (next) {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            timeout = (int)(next - ts.tv_sec);
            if (timeout <= 0) {
                kvdb_commit(kv->db);
                timeout = -1;
                next = 0;
            } else
                timeout *= 1000;
        }

        int nfds = epoll_wait(kv->efd, evs, KVFD_MAX, timeout);
        for (int i = 0; i < nfds; i++) {
            int fd = *(int*)evs[i].data.ptr;
            if (fd != kv->fd[0] && fd != kv->fd[1]) {
                if ((evs[i].events & EPOLLHUP) != 0) {
                    kvdb_monitor_close(kv, &evs[i]);
                }
                continue;
            }

            if ((evs[i].events & EPOLLIN) == 0)
                continue;

            int newfd = accept(fd, NULL, NULL);
            if (newfd < 0)
                continue;

            /* is database changed? */
            if (kvdb_client(kv, newfd) && next == 0) {
                clock_gettime(CLOCK_MONOTONIC, &ts);
                next = ts.tv_sec + CONFIG_KVDB_COMMIT_INTERVAL;
                if (next == 0)
                    next++; /* ensure no zero */
            }
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   Main entry point. Listens for UNIX domain socket connection and perform
 *   corresponding database operations.
 *
 ****************************************************************************/

int main(int argc, char* argv[])
{
    UNUSED(argc);
    UNUSED(argv);
    kvdb kv = {
        .head = LIST_HEAD_INITIALIZER(),
    };
    int ret = kvdb_bind(kv.fd);
    if (ret < 0)
        goto out;

    ret = kvdb_init(kv.db);
    if (ret < 0)
        goto out;

    kvdb_server(&kv);
    kvdb_uninit(kv.db);

out:
    kvdb_unbind(kv.fd);
    return -ret;
}
