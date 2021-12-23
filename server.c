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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netpacket/rpmsg.h>

#include "kvdb.h"
#include "unqlite.h"

#define KVDB_MEM                0
#define KVDB_PERSIST            1
#define KVDB_COUNT              2

#define KVFD_LOCAL              0
#define KVFD_REMOTE             1
#define KVFD_COUNT              2

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

    if (key[key_len])
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
    char *source_path, *token_path, *saveptr;

    for (source_path = CONFIG_KVDB_SOURCE_PATH; ; source_path = NULL) {
        token_path = strtok_r(source_path, ";", &saveptr);
        if (token_path == NULL)
            break;

        FILE* f = fopen(token_path, "r");
        if (!f)
            return 0; /* optional */

        char buf[PROP_MSG_MAX];
        while (fgets(buf, PROP_MSG_MAX, f)) {
            if (kvdb_is_comment(buf))
                continue;

            char* tmp;
            char* key = strtok_r(buf, "=", &tmp);
            char* value = strtok_r(NULL, "\n", &tmp);
            if (!value)
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

static int kvdb_init(unqlite* db[])
{
    static const char* path[KVDB_COUNT] = {
        [KVDB_MEM]     = "",
        [KVDB_PERSIST] = CONFIG_KVDB_PERSIST_PATH,
    };

    int ret = 0;

    /* open database */
    memset(db, 0, sizeof(db[0]) * KVDB_COUNT);
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
 * Network Types
 ****************************************************************************/

typedef struct kvdb_list_data {
    int fd;
    unqlite** db;
} kvdb_list_data;

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

    memset(fd, 0, sizeof(int) * KVFD_COUNT);

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

static void* kvdb_list_thread(void* arg)
{
    kvdb_list_data* data = arg;
    void* cookie = (void*)data->fd;
    kvdb_list(data->db, kvdb_list_consume, cookie);
    send(data->fd, "", 1, 0); /* terminator */
    close(data->fd);
    free(data);
    return NULL;
}

static bool kvdb_client(int fd, unqlite* db[])
{
    bool dirty = false;

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
    recv(fd, msg, PROP_MSG_MAX, 0);

    switch (msg[0]) {
        case 'D': {
            size_t key_len = (unsigned char)msg[1];
            const char* key = msg + 2;
            int32_t err = kvdb_delete(db, key, key_len);
            if (err >= 0)
                dirty = true;
            send(fd, &err, 4, 0);
            break;
        }
        case 'G': {
            size_t key_len = (unsigned char)msg[1];
            const char* key = msg + 2;
            char value[PROP_VALUE_MAX];
            int len = kvdb_get(db, key, key_len, value);
            if (len > 0)
                send(fd, value, len, 0);
            break;
        }
        case 'S': {
            size_t key_len = (unsigned char)msg[1];
            size_t val_len = (unsigned char)msg[2];
            const char* key = msg + 3;
            const char* value = key + key_len;
            int32_t err = kvdb_set(db, key, key_len, value, val_len, false);
            if (err >= 0)
                dirty = true;
            send(fd, &err, 4, 0);
            break;
        }
        case 'L': {
            kvdb_list_data* data = malloc(sizeof(*data));
            if (!data)
                break;

            data->fd = fd;
            data->db = db;

            /* dispatch to new thread to allow the recursion */
            pthread_t t;
            if (pthread_create(&t, NULL, kvdb_list_thread, data) > 0) {
                free(data);
                break;
            }

            pthread_detach(t);
            goto out; /* skip close fd, done in the thread */
        }
        case 'C': {
            kvdb_commit(db);
            break;
        }
        case 'R': {
            kvdb_load(db, true);
            break;
        }
    }

    close(fd); /* done, close client socket */

out:
    return dirty;
}

static void kvdb_server(int fd[], unqlite* db[])
{
    struct pollfd pfd[KVFD_COUNT];
    int pfd_count = 0;
    for (int i = 0; i < KVFD_COUNT; i++) {
        if (fd[i] > 0) {
            pfd[pfd_count].fd = fd[i];
            pfd[pfd_count].events = POLLIN;
            pfd_count++;
        }
    }

    time_t next = 0;

    while (1) {
        int timeout = -1;

        /* commit the change after timeout */
        if (next) {
            timeout = next - time(NULL);
            if (timeout <= 0) {
                kvdb_commit(db);
                timeout = -1;
                next = 0;
            } else
                timeout *= 1000;
        }

        int nfds = poll(pfd, pfd_count, timeout);

        for (int i = 0; nfds > 0; i++) {
            if ((pfd[i].revents & POLLIN) == 0)
                continue;

            nfds--;
            int newfd = accept(pfd[i].fd, NULL, NULL);
            if (newfd < 0)
                continue;

            /* is database changed? */
            if (kvdb_client(newfd, db) && next == 0) {
                next = time(NULL) + CONFIG_KVDB_COMMIT_INTERVAL;
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
    int fd[KVFD_COUNT];
    int ret = kvdb_bind(fd);
    if (ret < 0)
        goto out;

    unqlite* db[KVDB_COUNT];
    ret = kvdb_init(db);
    if (ret < 0)
        goto out;

    kvdb_server(fd, db);
    kvdb_uninit(db);

out:
    kvdb_unbind(fd);
    return -ret;
}
