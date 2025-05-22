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
#include <stdio.h>
#include <unistd.h>

#include "internal.h"
#include "kvdb.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * kvdb_file_genpath
 ****************************************************************************/

static void kvdb_file_genpath(const char* path, const char* key, char* filepath)
{
    snprintf(filepath, PATH_MAX, "%s/%s", path, key);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * kvdb_file_set
 ****************************************************************************/

int kvdb_file_set(const char* path, const char* key,
    const void* value, size_t val_len)
{
    char filepath[PATH_MAX];
    size_t nbyteswrite = 0;
    ssize_t result;
    int fd;

    kvdb_file_genpath(path, key, filepath);
    fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        KVERR("open %s error with %d", filepath, errno);
        return -errno;
    }

    while (nbyteswrite < val_len) {
        result = write(fd, value, val_len);
        if (result < 0) {
            if (result == -EINTR) {
                continue;
            }

            KVERR("write %s error with %d", filepath, errno);
            close(fd);
            return -errno;
        }

        nbyteswrite += result;
    }

    close(fd);
    return 0;
}

/****************************************************************************
 * kvdb_file_get
 ****************************************************************************/

ssize_t kvdb_file_get(const char* path, const char* key,
    void* value, size_t val_len)
{
    char filepath[PATH_MAX];
    size_t nbytesread = 0;
    ssize_t result;
    ssize_t ret;
    int fd;

    kvdb_file_genpath(path, key, filepath);

    if (value == NULL) {

        /* Readonly property only check if exist, no read required. */

        if (access(filepath, O_RDONLY) == 0)
            return val_len;
        else
            return -ENOENT;
    }

    fd = open(filepath, O_RDONLY | O_CLOEXEC, 0666);
    if (fd < 0) {
        KVERR("open %s error with %d", filepath, errno);
        return -errno;
    }

    do {
        result = read(fd, value + nbytesread, val_len - nbytesread);
        if (result < 0) {
            result = -errno;
            if (result == -EINTR) {
                continue;
            }

            KVERR("read %s error with %d", filepath, errno);
            ret = -errno;
            close(fd);
            return ret;
        }

        nbytesread += result;
    } while (result > 0 && nbytesread < val_len);

    ret = nbytesread;
    close(fd);
    return ret;
}

/****************************************************************************
 * kvdb_file_list
 ****************************************************************************/

int kvdb_file_list(const char* path, kvdb_consume consume, void* cookie)
{
    char value[PROP_VALUE_MAX];
    struct dirent* entry;
    int ret = 0;
    DIR* dir;

    dir = opendir(path);
    if (!dir) {
        KVERR("opendir %s error with %d", path, errno);
        return -errno;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) {
            continue;
        }

        ret = kvdb_file_get(path, entry->d_name, value, PROP_VALUE_MAX);
        if (ret < 0) {
            ret = -errno;
            break;
        }

        consume(entry->d_name, value, ret, cookie);
        ret = 0;
    }

    closedir(dir);
    return ret;
}

/****************************************************************************
 * kvdb_file_delete
 ****************************************************************************/

int kvdb_file_delete(const char* path, const char* key)
{
    char filepath[PATH_MAX];

    kvdb_file_genpath(path, key, filepath);
    return unlink(filepath);
}

#ifdef CONFIG_KVDB_FILE
/****************************************************************************
 * Name: kvdb_persist_init
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

int kvdb_persist_init(struct kvdb** kvdb)
{
    return 0;
}

/****************************************************************************
 * Name: kvdb_persist_uninit
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

void kvdb_persist_uninit(struct kvdb* kvdb)
{
}

/****************************************************************************
 * Name: kvdb_persist_set
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

int kvdb_persist_set(struct kvdb* kvdb, const char* key, size_t key_len,
    const void* value, size_t val_len, bool force)
{
    return kvdb_file_set(CONFIG_KVDB_PERSIST_PATH, key, value, val_len);
}

/****************************************************************************
 * Name: kvdb_persist_get
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

ssize_t kvdb_persist_get(struct kvdb* kvdb, const char* key, size_t key_len,
    void* value, size_t val_len)
{
    return kvdb_file_get(CONFIG_KVDB_PERSIST_PATH, key, value, val_len);
}

/****************************************************************************
 * Name: kvdb_persist_delete
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

int kvdb_persist_delete(struct kvdb* kvdb, const char* key, size_t key_len)
{
    return kvdb_file_delete(CONFIG_KVDB_PERSIST_PATH, key);
}

/****************************************************************************
 * Name: kvdb_persist_list
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

int kvdb_persist_list(struct kvdb* kvdb, kvdb_consume consume, void* cookie)
{
    return kvdb_file_list(CONFIG_KVDB_PERSIST_PATH, consume, cookie);
}

/****************************************************************************
 * Name: kvdb_persist_commit
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

int kvdb_persist_commit(struct kvdb* kvdb)
{
    return 0;
}
#endif
