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

#include <sys/param.h>

#include <kvdb.h>

#include "internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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

static bool kvdb_is_comment(const char* line)
{
    size_t i = strspn(line, " \t\r\n");
    return line[i] == '\0' || line[i] == '#';
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int kvdb_get_index(const char* key)
{
    if (strncmp(key, PERSIST_LABEL, PERSIST_LABEL_LEN) == 0) {
        return KVDB_PERSIST;
    } else {
        return KVDB_MEM;
    }
}

int kvdb_load(struct kvdb* kvdb, const char* src, bool force)
{
    char* tmpb;
    const char* path;
    const char* sep;

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

        FILE* f = fopen(path, "re");
        if (!f)
            continue;

        while (fgets(buf, PROP_MSG_MAX, f)) {
            if (kvdb_is_comment(buf))
                continue;

            char* tmp;
            char* key = strtok_r(buf, "=", &tmp);
            char* value = strtok_r(NULL, "\n", &tmp);
            if (!key || !value)
                continue;

            size_t key_len = strlen(key) + 1;
            if (!force && kvdb_get(kvdb, key, key_len, NULL) >= 0)
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

int property_set(const char* key, const char* value)
{
    return property_set_(key, value, false);
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
    if (errno || *end)
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
    return property_set_int64_(key, value, false);
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
    if (errno || *end)
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