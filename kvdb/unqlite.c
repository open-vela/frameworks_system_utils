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
    unqlite* db[KVDB_COUNT];
};

/****************************************************************************
 * Database Functions
 ****************************************************************************/

static bool kvdb_is_readonly(const char* key)
{
    return strncmp(key, "ro.", 3) == 0;
}

static bool unqlite_kv_is_exist(unqlite* db, const char* key, size_t key_len)
{
    unqlite_int64 value_len;

    return unqlite_kv_fetch(db, key, key_len, NULL, &value_len) >= 0;
}

int kvdb_set(struct kvdb* kvdb, const char* key, size_t key_len, const void* value, size_t val_len, bool force)
{
    if (--key_len >= PROP_NAME_MAX)
        return -E2BIG;

    if (!key || key[key_len])
        return -EINVAL;

    if (val_len >= PROP_VALUE_MAX)
        return -E2BIG;

    /* no, then try database  */
    int i = kvdb_get_index(key);
    if (i < 0)
        return i;

    if (!force && kvdb_is_readonly(key) && unqlite_kv_is_exist(kvdb->db[i], key, key_len + 1))
        return -EPERM;

    return unqlite_kv_store(kvdb->db[i], key, ++key_len, value, val_len);
}

ssize_t kvdb_get(struct kvdb* kvdb, const char* key, size_t key_len, void* value, size_t val_len)
{
    if (--key_len >= PROP_NAME_MAX)
        return -E2BIG;

    if (key[key_len])
        return -EINVAL;

    /* no, then try database  */
    int i = kvdb_get_index(key);
    if (i < 0)
        return i;

    unqlite_int64 val_size = val_len;
    int ret = unqlite_kv_fetch(kvdb->db[i], key, ++key_len, value, &val_size);
    if (ret < 0)
        return ret;

    if (val_size <= 0)
        return -EINVAL;

    return val_size;
}

int kvdb_delete(struct kvdb* kvdb, const char* key, size_t key_len)
{
    if (--key_len >= PROP_NAME_MAX)
        return -E2BIG;

    if (key[key_len])
        return -EINVAL;

    if (kvdb_is_readonly(key))
        return -EPERM;

    /* no, then try database  */
    int i = kvdb_get_index(key);
    if (i < 0)
        return i;

    return unqlite_kv_delete(kvdb->db[i], key, ++key_len);
}

static int kvdb_list_value(const void* value, unsigned int len, void* arg)
{
    kvdb_consume_data* data = arg;
    data->consume(data->key, value, len, data->cookie);
    return 0;
}

static int kvdb_list_key(const void* value, unsigned int len, void* arg)
{
    kvdb_consume_data* data = arg;
    data->key = value;
    data->key_len = len;
    return unqlite_kv_cursor_data_callback(data->cur, kvdb_list_value, data);
}

int kvdb_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie)
{
    for (int i = 0; i < KVDB_COUNT; i++) {
        unqlite_kv_cursor* cur = NULL;
        unqlite_kv_cursor_init(kvdb->db[i], &cur);

        kvdb_consume_data data = {
            .consume = consume,
            .cookie = cookie,
            .cur = cur,
        };

        unqlite_kv_cursor_first_entry(cur);
        while (unqlite_kv_cursor_valid_entry(cur)) {
            int ret = unqlite_kv_cursor_key_callback(cur, kvdb_list_key, &data);
            if (ret < 0) { /* exit loop demanded by consume */
                unqlite_kv_cursor_release(kvdb->db[i], cur);
                return ret;
            }
            unqlite_kv_cursor_next_entry(cur);
        }
        unqlite_kv_cursor_release(kvdb->db[i], cur);
    }

    return 0;
}

int kvdb_commit(struct kvdb* kvdb)
{
    int ret = 0;

    for (int i = 0; i < KVDB_COUNT; i++) {
        int r = unqlite_commit(kvdb->db[i]);
        if (r < 0) {
            KVERR("commit db:%d error %d!\n", i, r);
            if (ret == 0) {
                ret = r;
            }
        }
    }

    return ret;
}

void kvdb_uninit(struct kvdb* kvdb)
{
    if (kvdb != NULL) {
        for (int i = 0; i < KVDB_COUNT; i++) {
            if (kvdb->db[i]) {
                unqlite_close(kvdb->db[i]);
                kvdb->db[i] = NULL;
            }
        }

        free(kvdb);
        kvdb = NULL;
    }
}

int kvdb_init(struct kvdb** kvdb)
{
    static const char* path[KVDB_COUNT] = {
        [KVDB_PERSIST] = CONFIG_KVDB_PERSIST_PATH,
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
        [KVDB_MEM] = CONFIG_KVDB_TEMPORARY_PATH,
#endif
    };

    int ret = 0;

    *kvdb = calloc(1, sizeof(struct kvdb));
    if (*kvdb == NULL)
        return -ENOMEM;

    /* open database */
    for (int i = 0; i < KVDB_COUNT; i++) {
        if (path[i][0])
            ret = unqlite_open(&(*kvdb)->db[i], path[i], UNQLITE_OPEN_CREATE | UNQLITE_OPEN_OMIT_JOURNALING);
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
        else
            ret = unqlite_open(&(*kvdb)->db[i], NULL, UNQLITE_OPEN_IN_MEMORY);
#endif

        if (ret < 0)
            goto out;
    }

    return ret;

out:
    kvdb_uninit(*kvdb);
    return ret;
}
