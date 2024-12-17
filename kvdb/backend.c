/*
 * Copyright (C) 2024 Xiaomi Corporation
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <string.h>

#include <kvdb.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int kvdb_get_index(const char* key)
{
    if (strncmp(key, PERSIST_LABEL, PERSIST_LABEL_LEN) == 0) {
        return KVDB_PERSIST;
    } else {
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
        return KVDB_MEM;
#else
        return -EINVAL;
#endif
    }
}

static bool kvdb_kv_is_exist(struct kvdb* kvdb, const char* key,
    size_t key_len, size_t val_len)
{
    return kvdb_get(kvdb, key, key_len, NULL, val_len) >= 0;
}

static bool kvdb_is_readonly(const char* key)
{
    return strncmp(key, "ro.", 3) == 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: kvdb_init
 *
 * Description:
 *   init resource of nvs .
 *
 * Input Parameters:
 *   kvdb    - Pointer to save filekv instance.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_init(struct kvdb** kvdb)
{
    int ret = kvdb_persist_init(kvdb);
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    if (ret >= 0) {
        ret = mkdir(CONFIG_KVDB_TEMPORARY_PATH, 0666);
        if (ret < 0) {
            ret = -errno;

            /* Existing tmp dir is acceptable. */

            if (ret == -EEXIST)
                ret = 0;
        }
    }
#endif
    return ret;
}

/****************************************************************************
 * Name: kvdb_uninit
 *
 * Description:
 *   init resource of filekv .
 *
 * Input Parameters:
 *   kvdb    - Pointer to save filekv instance.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

void kvdb_uninit(struct kvdb* kvdb)
{
    kvdb_persist_uninit(kvdb);
}

/****************************************************************************
 * Name: kvdb_set
 *
 * Description:
 *   key-value set.
 *
 * Input Parameters:
 *   kvdb    - Pointer to save filekv instance.
 *   key     - Pointer to key to set.
 *   key_len - the length of the key
 *   value   - Pointer to data to be saved
 *   val_len - the length of the value
 *   force   - unused parameter
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_set(struct kvdb* kvdb, const char* key, size_t key_len,
    const void* value, size_t val_len, bool force)
{
    int ret;

    if (!force && kvdb_is_readonly(key) && kvdb_kv_is_exist(kvdb, key, key_len, val_len))
        return -EPERM;

    ret = kvdb_get_index(key);
    switch (ret) {
    case KVDB_PERSIST:
        ret = kvdb_persist_set(kvdb, key, key_len, value, val_len, force);
        break;
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    case KVDB_MEM:
        ret = kvdb_file_set(CONFIG_KVDB_TEMPORARY_PATH, key, value, val_len);
        break;
#endif
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

/****************************************************************************
 * Name: kvdb_get
 *
 * Description:
 *   key-value get.
 *
 * Input Parameters:
 *   kvdb    - Pointer to save filekv instance.
 *   key     - Pointer to key to get.
 *   key_len - the length of the key
 *   value   - Pointer to data to be get
 *   val_len - the length of the value
 *
 * Returned Value:
 *   the length of value, > 0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

ssize_t kvdb_get(struct kvdb* kvdb, const char* key, size_t key_len,
    void* value, size_t val_len)
{
    ssize_t ret = kvdb_get_index(key);
    switch (ret) {
    case KVDB_PERSIST:
        ret = kvdb_persist_get(kvdb, key, key_len, value, val_len);
        break;
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    case KVDB_MEM:
        ret = kvdb_file_get(CONFIG_KVDB_TEMPORARY_PATH, key, value, val_len);
        break;
#endif
    default:
        ret = -EINVAL;
        break;
    }
    return ret;
}

/****************************************************************************
 * Name: kvdb_delete
 *
 * Description:
 *   key-value delete.
 *
 * Input Parameters:
 *   kvdb    - Pointer to save filekv instance.
 *   key     - Pointer to key to delete.
 *   key_len - the length of the key
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_delete(struct kvdb* kvdb, const char* key, size_t key_len)
{
    int ret;

    if (kvdb_is_readonly(key))
        return -EPERM;

    ret = kvdb_get_index(key);
    switch (ret) {
    case KVDB_PERSIST:
        ret = kvdb_persist_delete(kvdb, key, key_len);
        break;
#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    case KVDB_MEM:
        ret = kvdb_file_delete(CONFIG_KVDB_TEMPORARY_PATH, key);
        break;
#endif
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

/****************************************************************************
 * Name: kvdb_list
 *
 *   key-value list.
 *
 * Input Parameters:
 *   kvdb      - Pointer to save filekv instance.
 *   consume   - callback when key-value fetch success.
 *   cookie    - private data for consume callback.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie)
{
    int ret = kvdb_persist_list(kvdb, consume, cookie);
    if (ret < 0)
        return ret;

#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    ret = kvdb_file_list(CONFIG_KVDB_TEMPORARY_PATH, consume, cookie);
#endif
    return ret;
}

/****************************************************************************
 * Name: kvdb_commit
 *
 *   key-value commit.
 *
 * Input Parameters:
 *   kvdb      - Pointer to save filekv instance.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_commit(struct kvdb* kvdb)
{
    /* Filekv over tmpfs always no need for commit */

    return kvdb_persist_commit(kvdb);
}
