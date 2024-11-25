/*
 * Copyright (C) 2023 Xiaomi Corporation
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvdb.h>

#include "internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: property_set_binary
 *
 * Description:
 *   Store Key-Values to unqlite backend or nvs backend.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   const void* value: entry value string
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_set_binary(const char* key, const void* value, size_t val_len, bool oneway)
{
    if (!key)
        return -EINVAL;

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_NAME_MAX)
        return -E2BIG;

    if (val_len == 0 || val_len >= PROP_VALUE_MAX)
        return -E2BIG;

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    ret = kvdb_set(client, key, key_len, value, val_len, false);
    kvdb_uninit(client);
    return ret;
}

/****************************************************************************
 * Name: property_get_binary
 *
 * Description:
 *   Retrieve Key-Values from database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   void* value: not NULL : pointer to string buffer
 *                NULL     : check whether this [key, value] exists
 *
 * Returned Value:
 *   On success returns the length of the value which will never be greater
 *   than PROP_NAME_MAX.
 *
 ****************************************************************************/

ssize_t property_get_binary(const char* key, void* value, size_t val_len)
{
    if (!key)
        return -E2BIG;

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_NAME_MAX)
        return -E2BIG;

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    ssize_t len = kvdb_get(client, key, key_len, value, val_len);
    kvdb_uninit(client);
    return len;
}

/****************************************************************************
 * Name: property_delete
 *
 * Description:
 *   Delete a KV pair by key
 *
 * Input Parameters:
 *   const char* key: entry key string
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_delete(const char* key)
{
    if (!key)
        return -EINVAL;

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_NAME_MAX)
        return -E2BIG;

    /* in environment variable? */
    if (getenv(key)) {
        int ret = unsetenv(key);
        if (ret < 0)
            ret = -errno;
        return ret;
    }

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    ret = kvdb_delete(client, key, key_len);
    kvdb_uninit(client);
    return ret;
}

/****************************************************************************
 * Name: property_list
 *
 * Description:
 *   List all KVs.
 *
 * Input Parameters:
 *   property_callback propfn: callback function
 *   void* cookie: cookie data to pass to callback function
 *
 * Returned Value:
 *   Returns 0 on success, <0 if all backend failed to open.
 *
 ****************************************************************************/

int property_list_binary(void (*propfn)(const char* key, const void* value, size_t val_len, void* cookie), void* cookie)
{
    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    ret = kvdb_list(client, propfn, cookie);
    kvdb_uninit(client);
    return ret;
}

/****************************************************************************
 * Name: property_commit
 *
 * Description:
 *   Actively commit all property changes
 *
 * Input Parameters:
 *   None
 *
 ****************************************************************************/

int property_commit(void)
{
    return 0;
}

/****************************************************************************
 * Name: property_reload
 *
 * Description:
 *   Reload default property value
 *
 * Input Parameters:
 *   None
 *
 ****************************************************************************/

int property_reload(void)
{
    return 0;
}
