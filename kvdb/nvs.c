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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <nuttx/mtd/configdata.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "internal.h"
#include "kvdb.h"

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct kvdb {
    int fd[KVDB_COUNT];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const char* kvdb_skip_prefix(const char* key, int index)
{
    return index == KVDB_PERSIST ? key + PERSIST_LABEL_LEN : key;
}

static void kvdb_add_prefix(char* out, size_t outlen,
    int index, const char* in)
{
    if (index == KVDB_PERSIST) {
        strlcpy(out, PERSIST_LABEL, outlen);
        strlcat(out, in, outlen);
    } else {
        strlcpy(out, in, outlen);
    }
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
 *   kvdb    - Pointer to save nvs kvdb instance.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_init(struct kvdb** kvdb)
{
    struct kvdb* handle;
    int ret;

    handle = (struct kvdb*)zalloc(sizeof(struct kvdb));
    if (handle == NULL) {
        KVERR("kvdb init error !\n");
        return -ENOMEM;
    }

    ret = open(CONFIG_KVDB_PERSIST_PATH, O_RDWR | O_CLOEXEC);
    if (ret < 0) {
        ret = -errno;
        KVERR("open %s error with %d",
            CONFIG_KVDB_PERSIST_PATH, ret);
        goto err;
    }

    handle->fd[KVDB_PERSIST] = ret;

#ifdef CONFIG_KVDB_TEMPORARY_STORAGE
    ret = open(CONFIG_KVDB_TEMPORARY_PATH, O_RDWR | O_CLOEXEC);
    if (ret < 0) {
        ret = -errno;
        KVERR("open %s error with %d",
            CONFIG_KVDB_TEMPORARY_PATH, ret);
        goto err;
    }

    handle->fd[KVDB_MEM] = ret;
#endif
    *kvdb = handle;

    return 0;

err:
    for (int i = 0; i < KVDB_COUNT; i++) {
        if (handle->fd[i])
            close(handle->fd[i]);
    }

    free(handle);
    return ret;
}

/****************************************************************************
 * Name: kvdb_uninit
 *
 * Description:
 *   init resource of nvs .
 *
 * Input Parameters:
 *   kvdb    - Pointer to save nvs kvdb instance.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

void kvdb_uninit(struct kvdb* kvdb)
{
    int i;

    for (i = 0; i < KVDB_COUNT; i++) {
        close(kvdb->fd[i]);
    }

    free(kvdb);
}

/****************************************************************************
 * Name: kvdb_set
 *
 * Description:
 *   key-value set.
 *
 * Input Parameters:
 *   kvdb    - Pointer to save nvs kvdb instance.
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
    struct config_data_s data;
    int index;
    int ret;

    if (key == NULL || key_len == 0)
        return -EINVAL;

    index = kvdb_get_index(key);
    if (index < 0)
        return index;

    key = kvdb_skip_prefix(key, index);

    if (strlen(key) >= sizeof(data.name))
        return -EINVAL;

    strlcpy(data.name, key, sizeof(data.name));

    data.len = val_len;
    data.configdata = (uint8_t*)value;

    ret = ioctl(kvdb->fd[index], CFGDIOC_SETCONFIG, &data);
    if (ret < 0) {
        ret = -errno;
        KVERR("IOCTL_SETCONFIG ERROR %d", ret);
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
 *   kvdb    - Pointer to save nvs kvdb instance.
 *   key     - Pointer to key to get.
 *   key_len - the length of the key
 *   value   - Pointer to data to be get
 *   val_len - the length of the value
 *
 * Returned Value:
 *   the length of value, > 0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

ssize_t kvdb_get(struct kvdb* kvdb, const char* key, size_t key_len, void* value, size_t val_len)
{
    struct config_data_s data;
    int index;
    int ret;

    if (key == NULL || key_len == 0)
        return -EINVAL;

    index = kvdb_get_index(key);
    if (index < 0)
        return index;

    key = kvdb_skip_prefix(key, index);

    if (strlen(key) >= sizeof(data.name))
        return -EINVAL;

    strlcpy(data.name, key, sizeof(data.name));
    data.configdata = (uint8_t*)value;
    data.len = val_len;

    ret = ioctl(kvdb->fd[index], CFGDIOC_GETCONFIG, &data);
    if (ret < 0) {
        ret = -errno;
        KVERR("CFGDIOC_GETCONFIG ERROR: %d", ret);
        return ret;
    }

    return data.len;
}

/****************************************************************************
 * Name: kvdb_delete
 *
 * Description:
 *   key-value delete.
 *
 * Input Parameters:
 *   kvdb    - Pointer to save nvs kvdb instance.
 *   key     - Pointer to key to delete.
 *   key_len - the length of the key
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_delete(struct kvdb* kvdb, const char* key, size_t key_len)
{
    struct config_data_s data;
    int index;
    int ret;

    if (key == NULL || key_len == 0)
        return -EINVAL;

    index = kvdb_get_index(key);
    if (index < 0)
        return index;

    key = kvdb_skip_prefix(key, index);

    if (strlen(key) >= sizeof(data.name))
        return -EINVAL;

    strlcpy(data.name, key, sizeof(data.name));

    ret = ioctl(kvdb->fd[index], CFGDIOC_DELCONFIG, &data);
    if (ret < 0) {
        ret = -errno;
        KVERR("CFGDIOC_DELCONFIG ERROR: %d", ret);
    }

    return ret;
}

/****************************************************************************
 * Name: kvdb_list
 *
 * Description:
 *   key-value list.
 *
 * Input Parameters:
 *   kvdb      - Pointer to save nvs kvdb instance.
 *   consume   - callback when key-value fetch success.
 *   cookie    - private data for consume callback.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie)
{
    char key[CONFIG_NAME_MAX + PERSIST_LABEL_LEN];
    uint8_t buf[PROP_VALUE_MAX];
    struct config_data_s data;
    int i;

    if (!consume)
        return 0;

    for (i = 0; i < KVDB_COUNT; i++) {
        data.configdata = buf;
        data.len = PROP_VALUE_MAX;
        int ret = ioctl(kvdb->fd[i], CFGDIOC_FIRSTCONFIG, &data);
        if (ret < 0)
            continue;

        kvdb_add_prefix(key, sizeof(key), i, data.name);

        consume(key, data.configdata, data.len, cookie);

        while (1) {
            data.configdata = buf;
            data.len = PROP_VALUE_MAX;
            ret = ioctl(kvdb->fd[i], CFGDIOC_NEXTCONFIG, &data);
            if (ret < 0)
                break;

            kvdb_add_prefix(key, sizeof(key), i, data.name);

            consume(key, data.configdata, data.len, cookie);
        }
    }

    return 0;
}

/****************************************************************************
 * Name: kvdb_commit
 *
 * Description:
 *   key-value commit, unused
 *
 * Input Parameters:
 *   kvdb    - Pointer to save nvs kvdb instance.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

int kvdb_commit(struct kvdb* kvdb)
{
    return 0;
}
