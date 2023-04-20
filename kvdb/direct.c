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
 * Private Functions
 ****************************************************************************/

static int kvdb_list_consume(const char* key, size_t key_len, const char* value, size_t val_len, void* cookie)
{
    UNUSED(key_len);
    UNUSED(val_len);

    printf("%s: %s\n", key, value);
    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: property_set
 *
 * Description:
 *   Store Key-Values to unqlite backend or nvs backend.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   const char* value: entry value string
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

int property_set_(const char* key, const char* value, bool oneway)
{
    if (!key)
        return -EINVAL;
    if (!value)
        value = "";

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_NAME_MAX)
        return -E2BIG;

    size_t val_len = strlen(value) + 1;
    if (val_len > PROP_VALUE_MAX)
        return -E2BIG;

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    ret = kvdb_set(client, key, key_len, value, val_len, false);
    if (ret < 0)
        goto out;

    kvdb_uninit(client);
    return ret;

out:
    kvdb_uninit(client);
    return ret;
}

/****************************************************************************
 * Name: property_get
 *
 * Description:
 *   Retrieve Key-Values from unqlite backend or nvs backend.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   char* value: not NULL : pointer to string buffer
 *                NULL     : check whether this [key, value] exists
 *   const char* default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns the length of the value which will never be greater
 *   than PROP_NAME_MAX - 1 and will always be zero terminated.
 *   (the length does not include the terminating zero).
 *   On failure returns length of default_value.
 *
 ****************************************************************************/

int property_get(const char* key, char* value, const char* default_value)
{
    if (!key)
        goto out;

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_NAME_MAX)
        goto out;

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    int val_len = kvdb_get(client, key, key_len, value);
    if (val_len < 0)
        goto out;

    kvdb_uninit(client);
    return val_len;

out:
    kvdb_uninit(client);
    if (!value || !default_value)
        return -EINVAL;
    strcpy(value, default_value);
    return strlen(default_value);
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

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    ret = kvdb_delete(client, key, key_len);
    if (ret < 0)
        goto out;

    kvdb_uninit(client);
    return ret;

out:
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

int property_list(void (*propfn)(const char* key, const char* value, void* cookie), void* cookie)
{
    UNUSED(propfn);

    struct kvdb* client;
    int ret = kvdb_init(&client);
    if (ret < 0)
        return ret;

    kvdb_consume consume = kvdb_list_consume;

    ret = kvdb_list(client, consume, cookie);
    if (ret < 0)
        goto out;

    kvdb_uninit(client);
    return ret;

out:
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
