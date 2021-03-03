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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netpacket/rpmsg.h>

#include "kvdb.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: property_connect
 *
 * Description:
 *   Initialize client socket and connect to server
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   On success return client socket fd.
 *   On error return error value (<0).
 *
 ****************************************************************************/

static int property_connect(void)
{
#ifdef CONFIG_KVDB_REMOTE_SERVER
    int fd = socket(AF_RPMSG, SOCK_STREAM, 0);
#else
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (fd < 0)
        return -errno;

#if CONFIG_KVDB_TIMEOUT_INTERVAL
    struct timeval timeout = {
        .tv_sec  = CONFIG_KVDB_TIMEOUT_INTERVAL,
        .tv_usec = 0,
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

#ifdef CONFIG_KVDB_REMOTE_SERVER
    struct sockaddr_rpmsg addr = {
        .rp_family = AF_RPMSG,
        .rp_name = PROP_SERVER_PATH,
        .rp_cpu = CONFIG_KVDB_RPMSG_SERVER_NAME,
    };
#else
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = PROP_SERVER_PATH,
    };
#endif

    int ret = connect(fd, (const struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        ret = -errno;
        close(fd);
        return ret;
    }

    return fd;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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
    if (!key)
        return -EINVAL;
    if (!value)
        value = "";

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_KEY_MAX)
        return -E2BIG;

    size_t val_len = strlen(value) + 1;
    if (val_len > PROP_VALUE_MAX)
        return -E2BIG;

    int fd = property_connect();
    if (fd < 0)
        return fd;

    /*-----------------------------------------*
     | 1 |   1   |   1   | key_len |  val_len  |
     |-----------------------------------------|
     |'S'|key_len|val_len|[key'\0']|[value'\0']|
     *-----------------------------------------*/

    char cmd[3] = {
        'S', key_len, val_len
    };

    struct iovec iov[3] = {
        {.iov_base = cmd         , .iov_len = 3      },
        {.iov_base = (char*)key  , .iov_len = key_len},
        {.iov_base = (char*)value, .iov_len = val_len},
    };

    struct msghdr msg = {0};
    msg.msg_iov    = iov;
    msg.msg_iovlen = 3;

    int ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        ret = -errno;
        goto out;
    }

    /*-----*
     |  4  |
     |-----|
     |error|
     *-----*/

    int32_t err;
    ret = recv(fd, &err, 4, 0);
    if (ret < 4) {
        ret = ret < 0 ? -errno : -EINVAL;
        goto out;
    }

    ret = err;

out:
    close(fd);
    return ret;
}

/****************************************************************************
 * Name: property_get
 *
 * Description:
 *   Retrieve Key-Values from database.
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   char* value: pointer to string buffer
 *   const char* default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns the length of the value which will never be greater
 *   than PROP_VALUE_MAX - 1 and will always be zero terminated.
 *   (the length does not include the terminating zero).
 *   On failure returns length of default_value.
 *
 ****************************************************************************/

int property_get(const char* key, char* value, const char* default_value)
{
    if (!key)
        return -EINVAL;

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_KEY_MAX)
        goto out;

    int fd = property_connect();
    if (fd < 0)
        goto out;

    /*---------------------*
     | 1 |   1   | key_len |
     | --------------------|
     |'G'|key_len|[key'\0']|
     *---------------------*/

    char cmd[2] = {
        'G', key_len
    };

    struct iovec iov[2] = {
        {.iov_base = cmd       , .iov_len = 2      },
        {.iov_base = (char*)key, .iov_len = key_len},
    };

    struct msghdr msg = {0};
    msg.msg_iov    = iov;
    msg.msg_iovlen = 2;

    if (sendmsg(fd, &msg, 0) < 0)
        goto out_fd;

    /*-----------*
     |  val_len  |
     |-----------|
     |[value'\0']|
     *-----------*/

    int val_len = recv(fd, value, PROP_VALUE_MAX, 0);
    if (val_len <= 0 || value[--val_len])
        goto out_fd;

    close(fd);
    return val_len;

out_fd:
    close(fd);
out:
    if (!default_value)
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
    if (key_len > PROP_KEY_MAX)
        return -E2BIG;

    int fd = property_connect();
    if (fd < 0)
        return fd;

    /*---------------------*
     | 1 |   1   | key_len |
     | --------------------|
     |'D'|key_len|[key'\0']|
     *---------------------*/

    char cmd[2] = {
        'D', key_len
    };

    struct iovec iov[2] = {
        {.iov_base = cmd       , .iov_len = 2      },
        {.iov_base = (char*)key, .iov_len = key_len},
    };

    struct msghdr msg = {0};
    msg.msg_iov    = iov;
    msg.msg_iovlen = 2;

    int ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        ret = -errno;
        goto out;
    }

    /*-----*
     |  4  |
     |-----|
     |error|
     *-----*/

    int32_t err;
    ret = recv(fd, &err, 4, 0);
    if (ret < 4) {
        ret = ret < 0 ? -errno : -EINVAL;
        goto out;
    }

    ret = err;

out:
    close(fd);
    return ret;
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

int property_list(property_callback propfn, void* cookie)
{
    int fd = property_connect();
    if (fd < 0)
        return fd;

    /*---*
     | 1 |
     | --|
     |'L'|
     *---*/

    int ret = send(fd, "L", 1, 0);
    if (ret < 0) {
        ret = -errno;
        goto out;
    }

    while (1) {
        /*-------------------------------------*
         |   1   |   1   | key_len |  val_len  |
         |-------------------------------------|
         |key_len|val_len|[key'\0']|[value'\0']|
         *-------------------------------------*/

        char msg[PROP_MSG_MAX];
        ret = recv(fd, msg, 2, 0);
        if (ret < 2) {
            if (!--ret && !msg[0])
                break; /* end the list */
            ret = ret < -1 ? -errno : -EINVAL;
            goto out;
        }

        size_t key_len = msg[0];
        if (--key_len >= PROP_KEY_MAX)
            continue;

        size_t val_len = msg[1];
        if (--val_len >= PROP_VALUE_MAX)
            continue;

        ret = recv(fd, msg + 2, msg[0] + msg[1], 0);
        if (ret != msg[0] + msg[1])
            continue;

        const char* key   = msg + 2;
        const char* value = key + key_len + 1;
        if (key[key_len] || value[val_len])
            continue;

        propfn(key, value, cookie);
    }

out:
    close(fd);
    return ret;
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

int property_set_bool(const char* key, int8_t value)
{
    return property_set(key, value ? "true" : "false");
}

/****************************************************************************
 * Name: property_get_bool
 *
 * Description:
 *   Retrieve a Key-Value from database and interpret the value as boolean.
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
 *   Saves an 32-bit integer to database.
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

int property_set_int32(const char* key, int32_t value)
{
    char buf[32];
    snprintf(buf, 32, "%" PRId32, value);
    return property_set(key, buf);
}

/****************************************************************************
 * Name: property_get_int32
 *
 * Description:
 *   Retrieve a Key-Value from database and interpret the value as int32_t.
 *   This is modified from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int32_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a boolean.
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
    if (errno ||  *end)
        return default_value;

    return ret;
}

/****************************************************************************
 * Name: property_set_int64
 *
 * Description:
 *   Saves an 64-bit integer to database.
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

int property_set_int64(const char* key, int64_t value)
{
    char buf[32];
    snprintf(buf, 32, "%" PRId64, value);
    return property_set(key, buf);
}

/****************************************************************************
 * Name: property_get_int64
 *
 * Description:
 *   Retrieve a Key-Value from database and interpret the value as int64_t.
 *   This is modified from Android libcutils:
 *   https://android.googlesource.com/platform/system/core/+/master/libcutils/
 *   properties.cpp
 *
 * Input Parameters:
 *   const char* key: entry key string
 *   int64_t default_value: the value to return on failure
 *
 * Returned Value:
 *   On success returns a boolean.
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
 * Name: property_commit
 *
 * Description:
 *   Actively commit all property changes
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   On success returns 0.
 *   On failure returns -errno.
 *
 ****************************************************************************/

int property_commit(void)
{
    int fd = property_connect();
    if (fd < 0)
        return fd;

    return send(fd, "C", 1, 0) > 0 ? 0 : -errno;
}