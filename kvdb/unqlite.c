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

#include <stdio.h>
#include <stdlib.h>

#include <kvdb.h>
#include <unqlite.h>

#include "internal.h"

typedef struct kvdb_consume_data {
    kvdb_consume consume;
    void* cookie;
    unqlite_kv_cursor* cur;
    const char* key;
    size_t key_len;
} kvdb_consume_data;

struct kvdb {
    unqlite* db;
};

/****************************************************************************
 * Database Functions
 ****************************************************************************/

int kvdb_persist_set(struct kvdb* kvdb, const char* key, size_t key_len, const void* value, size_t val_len, bool force)
{
    int ret = unqlite_kv_store(kvdb->db, key, key_len, value, val_len);
    if (ret < 0) {
        KVERR("kv_store key:%s key_len:%d error %d!\n", key, (int)key_len, ret);
        return ret;
    }

    return ret;
}

ssize_t kvdb_persist_get(struct kvdb* kvdb, const char* key, size_t key_len, void* value, size_t val_len)
{
    unqlite_int64 val_size = val_len;
    ssize_t ret = unqlite_kv_fetch(kvdb->db, key, key_len, value, &val_size);
    if (ret < 0) {
        if (ret != UNQLITE_NOTFOUND)
            KVERR("kv_fetch key:%s key_len:%d error %d!\n", key, (int)key_len, ret);
        return ret;
    }

    if (val_size <= 0)
        return -EINVAL;

    return val_size;
}

int kvdb_persist_delete(struct kvdb* kvdb, const char* key, size_t key_len)
{
    int ret = unqlite_kv_delete(kvdb->db, key, key_len);
    if (ret < 0) {
        KVERR("kv_delete key:%s key_len:%d error %d!\n", key, (int)key_len, ret);
        return ret;
    }

    return ret;
}

static int kvdb_list_value(const void* value, unsigned int len, void* arg)
{
    kvdb_consume_data* data = arg;
    data->consume(data->key, value, len, data->cookie);
    return 0;
}

static int kvdb_list_key(const void* value, unsigned int len, void* arg)
{
    int ret;
    kvdb_consume_data* data = arg;
    data->key = value;
    data->key_len = len;
    ret = unqlite_kv_cursor_data_callback(data->cur, kvdb_list_value, data);
    if (ret < 0) {
        KVERR("kv_cursor_data_callback failed error %d!\n", ret);
        return ret;
    }

    return ret;
}

int kvdb_persist_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie)
{
    unqlite_kv_cursor* cur = NULL;
    unqlite_kv_cursor_init(kvdb->db, &cur);

    kvdb_consume_data data = {
        .consume = consume,
        .cookie = cookie,
        .cur = cur,
    };

    unqlite_kv_cursor_first_entry(cur);
    while (unqlite_kv_cursor_valid_entry(cur)) {
        int ret = unqlite_kv_cursor_key_callback(cur, kvdb_list_key, &data);
        if (ret < 0) { /* exit loop demanded by consume */
            KVERR("kv_cursor_key_callback failed error %d!\n", ret);
            unqlite_kv_cursor_release(kvdb->db, cur);
            return ret;
        }
        unqlite_kv_cursor_next_entry(cur);
    }
    unqlite_kv_cursor_release(kvdb->db, cur);

    return 0;
}

int kvdb_persist_commit(struct kvdb* kvdb)
{
    int ret = unqlite_commit(kvdb->db);
    if (ret < 0) {
        KVERR("commit db:%p error %d!\n", kvdb->db, ret);
    }

    return ret;
}

void kvdb_persist_uninit(struct kvdb* kvdb)
{
    if (kvdb != NULL) {
        unqlite_close(kvdb->db);
        free(kvdb);
    }
}

int kvdb_persist_init(struct kvdb** kvdb)
{
    int ret;

    *kvdb = zalloc(sizeof(struct kvdb));
    if (*kvdb == NULL)
        return -ENOMEM;

    /* open database */

    ret = unqlite_open(&(*kvdb)->db, CONFIG_KVDB_PERSIST_PATH, UNQLITE_OPEN_CREATE | UNQLITE_OPEN_OMIT_JOURNALING);

    if (ret < 0) {
        KVERR("unqlite_open failed error %d!\n", ret);
        free(*kvdb);
    }

    return ret;
}
