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
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/param.h>

#include <kvdb.h>

#include "internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct property_list_arg {
    void* cookie;
    void (*propfn)(const char* key, const char* value, void* cookie);
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void property_list_fn(const char* key, const void* value, size_t val_len, void* cookie)
{
    struct property_list_arg* list = (struct property_list_arg*)cookie;
    *((char*)value + val_len) = '\0';
    list->propfn(key, value, list->cookie);
}

static inline char nibble2ascii(unsigned char nibble)
{
    if (nibble < 10)
        return '0' + nibble;
    else
        return 'a' + nibble - 10;
}

static inline int ascii2nibble(char ascii)
{
    if (isdigit(ascii))
        return ascii - '0';
    else if (isxdigit(ascii))
        return tolower(ascii) - 'a' + 10;
    else
        return -ERANGE;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int kvdb_get_index(const char* key)
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

/****************************************************************************
 * Name: property_set_
 *
 * Description:
 *   Store Key-Values to database.
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

static int property_set_(const char* key, const char* value, bool oneway)
{
    if (!value)
        value = "";

    /* in environment variable? */
    if (getenv(key)) {
        int ret = setenv(key, value, 1);
        if (ret < 0)
            ret = -errno;
        return ret;
    }

    return property_set_binary(key, value, strlen(value) + 1, oneway);
}

/****************************************************************************
 * Name: property_set
 *
 * Description:
 *   Store Key-Values to database.
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

int property_set(const char* key, const char* value)
{
    return property_set_(key, value, false);
}

/****************************************************************************
 * Name: property_get
 *
 * Description:
 *   Retrieve Key-Values from database.
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
    /* in environment variable? */
    const char* env = getenv(key);
    if (env) {
        size_t len = strlen(env);
        if (len >= PROP_VALUE_MAX)
            return -E2BIG;

        if (value)
            memcpy(value, env, len + 1);

        return len;
    }

    ssize_t ret = property_get_binary(key, value, PROP_VALUE_MAX);
    if (ret <= 0) {
        if (!default_value)
            return -EINVAL;
        size_t len = strlen(default_value);
        if (value)
            memcpy(value, default_value, len + 1);
        return len;
    }
    *(value + ret) = '\0';
    return strlen(value);
}

/****************************************************************************
 * Name: property_list
 *
 * Description:
 *   List all KVs in every database and calls callback function.
 *
 * Input Parameters:
 *   property_callback propfn: callback function
 *   void* cookie: cookie data to pass to callback function
 *
 * Returned Value:
 *   Returns 0 on success, <0 if all databases failed to open.
 *
 ****************************************************************************/

int property_list(void (*propfn)(const char* key, const char* value, void* cookie), void* cookie)
{
    struct property_list_arg list;
    list.cookie = cookie;
    list.propfn = propfn;
    return property_list_binary(property_list_fn, &list);
}

int property_set_oneway(const char* key, const char* value)
{
    return property_set_(key, value, true);
}

/****************************************************************************
 * Name: property_set_bool
 *
 * Description:
 *   Saves a boolean to database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int8_t value: entry value
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

static int property_set_bool_(const char* key, int8_t value, bool oneway)
{
    return property_set_(key, value ? "true" : "false", oneway);
}

int property_set_bool(const char* key, int8_t value)
{
    return property_set_bool_(key, value, false);
}

int property_set_bool_oneway(const char* key, int8_t value)
{
    return property_set_bool_(key, value, true);
}

/****************************************************************************
 * Name: property_get_bool
 *
 * Description:
 *   Retrieve a Key-Value from backend and interpret the value as boolean.
 *   This is taken from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int8_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a boolean.
 *   On failure returns default_value.
 *
 ****************************************************************************/

int8_t property_get_bool(const char* key, int8_t default_value)
{
    char buf[PROP_VALUE_MAX];
    int len = property_get(key, buf, NULL);
    if (len == 1) {
        char ch = buf[0];
        if (ch == '0' || ch == 'n')
            return 0;
        else if (ch == '1' || ch == 'y')
            return 1;
    } else if (len > 1) {
        if (!strcmp(buf, "no") || !strcmp(buf, "false") || !strcmp(buf, "off"))
            return 0;
        else if (!strcmp(buf, "yes") || !strcmp(buf, "true") || !strcmp(buf, "on"))
            return 1;
    }

    return default_value;
}

/****************************************************************************
 * Name: property_set_int32
 *
 * Description:
 *   Saves an 32-bit integer to unqlite backend or nvs backend.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int32_t value: entry value
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

static int property_set_int32_(const char* key, int32_t value, bool oneway)
{
    char buf[32];
    snprintf(buf, 32, "%" PRId32, value);
    return property_set_(key, buf, oneway);
}

int property_set_int32(const char* key, int32_t value)
{
    return property_set_int32_(key, value, false);
}

int property_set_int32_oneway(const char* key, int32_t value)
{
    return property_set_int32_(key, value, true);
}

/****************************************************************************
 * Name: property_get_int32
 *
 * Description:
 *   Retrieve a Key-Value from backend and interpret the value as int32_t.
 *   This is modified from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int32_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a int32_t.
 *   On failure returns default_value.
 *
 ****************************************************************************/

int32_t property_get_int32(const char* key, int32_t default_value)
{
    char value[PROP_VALUE_MAX];
    if (property_get(key, value, NULL) < 0)
        return default_value;

    errno = 0;
    char* end;
    int32_t ret = strtol(value, &end, 0);
    if (errno || *end || value == end)
        return default_value;

    return ret;
}

/****************************************************************************
 * Name: property_set_int64
 *
 * Description:
 *   Saves an 64-bit integer to unqlite backend or nvs backend.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int64_t value: entry value
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

static int property_set_int64_(const char* key, int64_t value, bool oneway)
{
    char buf[32];
    snprintf(buf, 32, "%" PRId64, value);
    return property_set_(key, buf, oneway);
}

int property_set_int64(const char* key, int64_t value)
{
    return property_set_int64_(key, value, false);
}

int property_set_int64_oneway(const char* key, int64_t value)
{
    return property_set_int64_(key, value, true);
}

/****************************************************************************
 * Name: property_get_int64
 *
 * Description:
 *   Retrieve a Key-Value from backend and interpret the value as int64_t.
 *   This is modified from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int64_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a int64_t.
 *   On failure returns default_value.
 *
 ****************************************************************************/

int64_t property_get_int64(const char* key, int64_t default_value)
{
    char value[PROP_VALUE_MAX];
    if (property_get(key, value, NULL) < 0)
        return default_value;

    errno = 0;
    char* end;
    int64_t ret = strtoll(value, &end, 0);
    if (errno || *end || value == end)
        return default_value;

    return ret;
}

/****************************************************************************
 * Name: property_set_buffer
 *
 * Description:
 *   Saves a binary buffer to unqlite backend or nvs backend.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   const void* value: buffer value
 *   size_t size: buffer size
 *
 * Returned Value:
 *         0: success
 *        <0: failure during execution
 *
 ****************************************************************************/

static int property_set_buffer_(const char* key, const void* value,
    size_t size, bool oneway)
{
    size_t buf_size = 2 * size;
    if (buf_size >= PROP_VALUE_MAX)
        return -E2BIG;

    const unsigned char* tmp = value;
    char buf[PROP_VALUE_MAX];
    size_t i = 0;

    while (i < buf_size) {
        buf[i++] = nibble2ascii(*tmp >> 4);
        buf[i++] = nibble2ascii(*tmp++ & 0x0f);
    }

    buf[i] = '\0';
    return property_set_(key, buf, oneway);
}

int property_set_buffer(const char* key, const void* value, size_t size)
{
    return property_set_buffer_(key, value, size, false);
}

int property_set_buffer_oneway(const char* key, const void* value, size_t size)
{
    return property_set_buffer_(key, value, size, false);
}

/****************************************************************************
 * Name: property_get_buffer
 *
 * Description:
 *   Retrieve a Key-Value from backend and interpret as binary buffer.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   void* value: buffer value
 *   size_t size: buffer size
 *
 * Returned Value:
 *   On success returns buffer length.
 *   On failure returns -errno.
 *
 ****************************************************************************/

ssize_t property_get_buffer(const char* key, void* value, size_t size)
{
    char buf[PROP_VALUE_MAX];
    size_t buf_size = 2 * size;
    int ret = property_get(key, buf, NULL);
    if (ret < 0)
        return ret;

    char* tmp = value;
    size_t i = 0;

    while (buf[i]) {
        if (i >= buf_size)
            return -E2BIG;

        ret = ascii2nibble(buf[i++]);
        if (ret < 0)
            return ret;

        *tmp = ret << 4;

        ret = ascii2nibble(buf[i++]);
        if (ret < 0)
            return ret;

        *tmp++ |= ret;
    }

    return i / 2;
}
