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

#include <fnmatch.h>
#include <stdio.h>
#include <sys/param.h>

#include <netpacket/rpmsg.h>
#include <sys/epoll.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <kvdb.h>

#include "internal.h"

#if defined(CONFIG_NET_LOCAL) && defined(CONFIG_NET_RPMSG)
#define KVFD_LOCAL 0
#define KVFD_REMOTE 1
#define KVFD_COUNT 2
#else
#define KVFD_LOCAL 0
#define KVFD_REMOTE 0
#define KVFD_COUNT 1
#endif
#define KVFD_MAX 8

typedef struct kvdb_monitor {
    int fd;
    LIST_ENTRY(kvdb_monitor)
    entry;
    char key[0];
} kvdb_monitor;

typedef LIST_HEAD(kvdb_monitor_head, kvdb_monitor) kvdb_monitor_head;

typedef struct kvdb_server {
    struct kvdb* kvdb;
    int fd[KVFD_COUNT];
    int efd;
    kvdb_monitor_head head;
} kvdb_server;

/* Open a monitor channel, add the [key, fd] pair to the monitor list and
 * add the pollfd to the pollfd array.
 */
static int kvdb_monitor_open(kvdb_server* server, int fd, const char* key,
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
    int ret = epoll_ctl(server->efd, EPOLL_CTL_ADD, fd, &ev);
    if (ret < 0) {
        free(mon);
        return ret;
    }

    /* Add the [key, fd] pair to the monitor list */
    mon->fd = fd;
    strcpy(mon->key, key);
    LIST_INSERT_HEAD(&server->head, mon, entry);

    return 0;
}

/* Close the monitor fd, remove the fd from the monitor list and empty
 * corresponding pollfd.
 */
static void kvdb_monitor_close(kvdb_server* server, struct epoll_event* ev)
{
    kvdb_monitor* mon = (kvdb_monitor*)ev->data.ptr;

    /* Close the monitor fd and delete it from epoll */
    epoll_ctl(server->efd, EPOLL_CTL_DEL, mon->fd, NULL);
    close(mon->fd);

    /* Remove the element from the monitor list */
    LIST_REMOVE(mon, entry);
    free(mon);
}

/* Notify the client the value changed (updated or deleted) */
static void kvdb_monitor_notify(kvdb_server* server, const char* key, const void* value, size_t val_len)
{
    size_t key_len = strlen(key) + 1;

    /* value != NULL
      *---------------------------------*
      |   1   |   1   | key_len |val_len|
      |---------------------------------|
      |key_len|val_len|[key'\0']|[value]|
      *---------------------------------*
      * value == NULL
      *-------------------------*
      |   1   |   1   | key_len |
      |-------------------------|
      |key_len|   0   |[key'\0']|
      *-------------------------*/

    char cmd[2] = { key_len, val_len };
    struct iovec iov[3] = {
        { .iov_base = cmd, .iov_len = 2 },
        { .iov_base = (char*)key, .iov_len = key_len },
        { .iov_base = (char*)value, .iov_len = val_len },
    };

    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = value ? 3 : 2;

    kvdb_monitor* mon;
    kvdb_monitor* tmp;
    LIST_FOREACH_SAFE(mon, &server->head, entry, tmp)
    {
        if (fnmatch(mon->key, key, FNM_NOESCAPE) != 0)
            continue;

        if (sendmsg(mon->fd, &msg, 0) < 0) {
            /* Client close or some error happends, stop monitor */
            LIST_REMOVE(mon, entry);
            epoll_ctl(server->efd, EPOLL_CTL_DEL, mon->fd, NULL);
            close(mon->fd);
            free(mon);
        }
    }
}

static bool kvdb_is_comment(const char* line)
{
    size_t i = strspn(line, " \t\r\n");
    return line[i] == '\0' || line[i] == '#';
}

/****************************************************************************
 * Network Functions
 ****************************************************************************/

static int kvdb_load(struct kvdb* kvdb, const char* src, bool force)
{
    char* tmpb;
    const char* path;
    const char* sep;
    int retry = 20;

    char* buf = malloc(PROP_MSG_MAX);
    if (buf == NULL) {
        KVERR("malloc failed\n");
        return -ENOMEM;
    }

    tmpb = malloc(PATH_MAX);
    if (tmpb == NULL) {
        KVERR("malloc failed\n");
        free(buf);
        return -ENOMEM;
    }
    path = tmpb;

    while (*src) {
        sep = strchr(src, ';');
        if (sep) {
            strlcpy(tmpb, src, MIN(PATH_MAX, sep - src + 1));
            src = sep + 1;
        } else {
            path = src;
            src += strlen(src);
        }

        /* Wait filesystem mount success */
        while (access(path, 0) < 0 && retry-- > 0)
            usleep(1000);

        FILE* f = fopen(path, "re");
        if (!f) {
            KVERR("kvdb open:%s failed, errno:%d\n", path, errno);
            continue;
        }

        while (fgets(buf, PROP_MSG_MAX, f)) {
            if (kvdb_is_comment(buf))
                continue;

            char* tmp;
            char* key = strtok_r(buf, "=", &tmp);
            char* value = strtok_r(NULL, "\n", &tmp);
            if (!key || !value)
                continue;

            size_t key_len = strlen(key) + 1;
            if (!force && kvdb_get(kvdb, key, key_len, NULL, 0) >= 0)
                continue;

            kvdb_set(kvdb, key, key_len, value, strlen(value) + 1, true);
        }

        fclose(f);
    }

    kvdb_commit(kvdb);
    free(tmpb);
    free(buf);

    return 0;
}

static int kvdb_bind(int fd[])
{
    const int family[] = {
#ifdef CONFIG_NET_LOCAL
        [KVFD_LOCAL] = AF_UNIX,
#endif
#ifdef CONFIG_NET_RPMSG
        [KVFD_REMOTE] = AF_RPMSG,
#endif
    };

#ifdef CONFIG_NET_LOCAL
    const struct sockaddr_un addr0 = {
        .sun_family = AF_UNIX,
        .sun_path = PROP_SERVER_PATH,
    };
#endif

#ifdef CONFIG_NET_RPMSG
    const struct sockaddr_rpmsg addr1 = {
        .rp_family = AF_RPMSG,
        .rp_cpu = "",
        .rp_name = PROP_SERVER_PATH,
    };
#endif

    const struct sockaddr* addr[] = {
#ifdef CONFIG_NET_LOCAL
        [KVFD_LOCAL] = (const struct sockaddr*)&addr0,
#endif
#ifdef CONFIG_NET_RPMSG
        [KVFD_REMOTE] = (const struct sockaddr*)&addr1,
#endif
    };

    const socklen_t addrlen[] = {
#ifdef CONFIG_NET_LOCAL
        [KVFD_LOCAL] = sizeof(struct sockaddr_un),
#endif
#ifdef CONFIG_NET_RPMSG
        [KVFD_REMOTE] = sizeof(struct sockaddr_rpmsg),
#endif
    };

    memset(fd, 0, sizeof(*fd) * KVFD_COUNT);

    for (int i = 0; i < KVFD_COUNT; i++) {
        fd[i] = socket(family[i], SOCK_STREAM | SOCK_CLOEXEC, 0);
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

#ifdef CONFIG_KVDB_DUMPLIST
static void kvdb_list_consume(const char* key, const void* value, size_t val_len, void* cookie)
{
    size_t key_len = strlen(key) + 1;
    char cmd[2] = {
        key_len, val_len
    };

    struct iovec iov[3] = {
        { .iov_base = cmd, .iov_len = 2 },
        { .iov_base = (char*)key, .iov_len = key_len },
        { .iov_base = (char*)value, .iov_len = val_len },
    };

    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    int fd = (intptr_t)cookie;
    sendmsg(fd, &msg, 0);
}
#endif

static ssize_t kvdb_recv(int sockfd, char* buf, size_t offset, size_t len)
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

static bool kvdb_client(kvdb_server* server, int fd)
{
    bool dirty = false;
    ssize_t len;
    char* msg;

#if CONFIG_KVDB_TIMEOUT_INTERVAL
    struct timeval timeout = {
        .tv_sec = CONFIG_KVDB_TIMEOUT_INTERVAL,
        .tv_usec = 0,
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    msg = malloc(PROP_MSG_MAX);
    if (msg == NULL) {
        KVERR("malloc failed\n");
        goto out;
    }

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
            int32_t err = kvdb_delete(server->kvdb, key, key_len);
            if (err >= 0) {
                dirty = true;
                kvdb_monitor_notify(server, key, NULL, 0);
            }
            send(fd, &err, 4, 0);
        }
        break;
    }
    case 'G': {
        size_t key_len = (unsigned char)msg[1];
        size_t val_len = (unsigned char)msg[2];
        size_t end_pos = key_len + 3;
        if (end_pos >= PROP_MSG_MAX)
            break;

        const char* key = msg + 3;
        char value[PROP_VALUE_MAX];
        len = kvdb_recv(fd, msg, len, end_pos);
        if (len > 0) {
            len = kvdb_get(server->kvdb, key, key_len, value, val_len);
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
            int32_t err = kvdb_set(server->kvdb, key, key_len, value, val_len, false);
            if (err >= 0) {
                dirty = true;
                kvdb_monitor_notify(server, key, value, val_len);
            }
            send(fd, &err, 4, 0);
        }
        break;
    }
#ifdef CONFIG_KVDB_DUMPLIST
    case 'L': {
        kvdb_list(server->kvdb, kvdb_list_consume, (void*)(uintptr_t)fd);
        send(fd, "\0", 2, 0); /* terminator */
        break;
    }
#endif
    case 'C': {
        int ret = kvdb_commit(server->kvdb);
        send(fd, &ret, sizeof(ret), 0);
        break;
    }
    case 'R': {
        kvdb_load(server->kvdb, CONFIG_KVDB_SOURCE_PATH, true);
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
            int32_t err = kvdb_monitor_open(server, fd, key, key_len);
            send(fd, &err, 4, 0);
        }
        /* Direct return, not close the monitor fd */
        free(msg);
        return false;
    }
    }

out:
    free(msg);
    close(fd); /* done, close client socket */
    return dirty;
}

static void kvdb_loop(kvdb_server* server)
{
    struct epoll_event evs[KVFD_MAX];
    struct timespec ts;

    server->efd = epoll_create(KVFD_MAX);
    if (server->efd < 0)
        return;

    for (int i = 0; i < KVFD_COUNT; i++) {
        if (server->fd[i] >= 0) {
            evs[0].data.ptr = &server->fd[i];
            evs[0].events = EPOLLIN;
            if (epoll_ctl(server->efd, EPOLL_CTL_ADD, server->fd[i], &evs[0]) < 0) {
                close(server->efd);
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
                kvdb_commit(server->kvdb);
                timeout = -1;
                next = 0;
            } else
                timeout *= 1000;
        }

        int nfds = epoll_wait(server->efd, evs, KVFD_MAX, timeout);
        for (int i = 0; i < nfds; i++) {
            int fd = *(int*)evs[i].data.ptr;
#ifdef CONFIG_NET_RPMSG
            if (fd != server->fd[0] && fd != server->fd[1]) {
#else
            if (fd != server->fd[0]) {
#endif
                if ((evs[i].events & EPOLLHUP) != 0) {
                    kvdb_monitor_close(server, &evs[i]);
                }
                continue;
            }

            if ((evs[i].events & EPOLLIN) == 0)
                continue;

            int newfd = accept(fd, NULL, NULL);
            if (newfd < 0)
                continue;

            /* is database changed? */
            if (kvdb_client(server, newfd) && next == 0) {
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
    kvdb_server server = {
        .head = LIST_HEAD_INITIALIZER(),
    };
    int ret = kvdb_bind(server.fd);
    if (ret < 0)
        goto out;

    ret = kvdb_init(&server.kvdb);
    if (ret < 0)
        goto out;

    kvdb_load(server.kvdb, CONFIG_KVDB_SOURCE_PATH, false);
    kvdb_loop(&server);
    kvdb_uninit(server.kvdb);

out:
    kvdb_unbind(server.fd);
    return -ret;
}
