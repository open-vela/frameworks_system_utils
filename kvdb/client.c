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
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netpacket/rpmsg.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <kvdb.h>

#include "internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static ssize_t recv_safe(int sockfd, char* buf, size_t offset, size_t len)
{
    while (offset < len) {
        ssize_t ret = recv(sockfd, buf + offset, len - offset, 0);
        if (ret < 0)
            return -errno;
        if (ret == 0)
            return -ENODATA;
        offset += ret;
    }

    return len;
}

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
#ifdef CONFIG_KVDB_SERVER
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
#else
    int fd = socket(AF_RPMSG, SOCK_STREAM, 0);
#endif
    if (fd < 0)
        return -errno;

#if CONFIG_KVDB_TIMEOUT_INTERVAL
    struct timeval timeout = {
        .tv_sec = CONFIG_KVDB_TIMEOUT_INTERVAL,
        .tv_usec = 0,
    };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

#ifdef CONFIG_KVDB_SERVER
    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
        .sun_path = PROP_SERVER_PATH,
    };
#else
    struct sockaddr_rpmsg addr = {
        .rp_family = AF_RPMSG,
        .rp_name = PROP_SERVER_PATH,
        .rp_cpu = CONFIG_KVDB_SERVER_CPUNAME,
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

int property_set_(const char* key, const char* value, bool oneway)
{
    int fd;

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

again:
    fd = property_connect();
    if (fd < 0) {
        KVERR("connect failed, fd=%d\n", fd);
        return fd;
    }

    /*-----------------------------------------*
     | 1 |   1   |   1   | key_len |  val_len  |
     |-----------------------------------------|
     |'S'|key_len|val_len|[key'\0']|[value'\0']|
     *-----------------------------------------*/

    char cmd[3] = {
        'S', key_len, val_len
    };

    struct iovec iov[3] = {
        { .iov_base = cmd, .iov_len = 3 },
        { .iov_base = (char*)key, .iov_len = key_len },
        { .iov_base = (char*)value, .iov_len = val_len },
    };

    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    int ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        /* handle server refused by backlog limitation */

        if (oneway && errno == ECONNRESET) {
            close(fd);
            goto again;
        }

        ret = -errno;
        KVERR("sendmsg failed, ret=%d\n", ret);
        goto out;
    }

    if (!oneway) {

        /*-----*
          |  4  |
          |-----|
          |error|
         *-----*/

        int32_t err;
        ret = recv(fd, &err, 4, 0);
        if (ret < 4) {
            KVERR("recv failed, ret=%d\n", ret);
            ret = ret < 0 ? -errno : -EINVAL;
            goto out;
        }

        ret = err;
    }

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

    int fd = property_connect();
    if (fd < 0) {
        KVERR("connect failed, fd=%d\n", fd);
        goto out;
    }

    /*---------------------*
     | 1 |   1   | key_len |
     | --------------------|
     |'G'|key_len|[key'\0']|
     *---------------------*/

    char cmd[2] = {
        'G', key_len
    };

    struct iovec iov[2] = {
        { .iov_base = cmd, .iov_len = 2 },
        { .iov_base = (char*)key, .iov_len = key_len },
    };

    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    int ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        KVERR("sendmsg failed, errno=%d\n", errno);
        goto out_fd;
    }

    /*-----------*
     |  val_len  |
     |-----------|
     |[value'\0']|
     *-----------*/

    int val_len;

    if (value) {
        /* value is not NULL, receive all the value */

        val_len = recv(fd, value, PROP_VALUE_MAX, 0);
        if (val_len <= 0 || value[--val_len]) {
            KVERR("recv failed, val_len=%d\n", val_len);
            goto out_fd;
        }
    } else {
        /* value is NULL, receive two chars to check whether this
         * [key, value] exists
         */

        char tmpvalue[2];
        val_len = recv(fd, tmpvalue, 2, 0);
        if (val_len <= 0) {
            KVERR("recv failed, val_len=%d\n", val_len);
            goto out_fd;
        }
    }

    close(fd);
    return val_len;

out_fd:
    close(fd);
out:
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

    int fd = property_connect();
    if (fd < 0) {
        KVERR("connect failed, fd=%d\n", fd);
        return fd;
    }

    /*---------------------*
     | 1 |   1   | key_len |
     | --------------------|
     |'D'|key_len|[key'\0']|
     *---------------------*/

    char cmd[2] = {
        'D', key_len
    };

    struct iovec iov[2] = {
        { .iov_base = cmd, .iov_len = 2 },
        { .iov_base = (char*)key, .iov_len = key_len },
    };

    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    int ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        ret = -errno;
        KVERR("sendmsg failed, ret=%d\n", ret);
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
        KVERR("recv failed, ret=%d\n", ret);
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

int property_list(void (*propfn)(const char* key, const char* value, void* cookie), void* cookie)
{
    int fd = property_connect();
    if (fd < 0) {
        KVERR("connect failed, fd=%d\n", fd);
        return fd;
    }

    /*---*
     | 1 |
     | --|
     |'L'|
     *---*/

    int ret = send(fd, "L", 1, 0);
    if (ret < 0) {
        ret = -errno;
        KVERR("send failed, ret=%d\n", ret);
        goto out;
    }

    while (1) {
        /*-------------------------------------*
         |   1   |   1   | key_len |  val_len  |
         |-------------------------------------|
         |key_len|val_len|[key'\0']|[value'\0']|
         *-------------------------------------*/

        char msg[PROP_MSG_MAX];
        ret = recv_safe(fd, msg, 0, 2);
        if (ret < 0) {
            ret = -errno;
            KVERR("recv_safe failed, ret=%d\n", ret);
            goto out;
        }

        if (msg[0] == 0 && msg[1] == 0) {
            /* end of list */
            ret = 0;
            break;
        }

        size_t key_len = (unsigned char)msg[0];
        if (key_len > PROP_NAME_MAX)
            continue;

        size_t val_len = (unsigned char)msg[1];
        if (val_len > PROP_VALUE_MAX)
            continue;

        size_t total = key_len + val_len + 2;
        ret = recv_safe(fd, msg, 2, total);
        if (ret < 0) {
            KVERR("recv_safe failed, ret=%d\n", ret);
            break;
        }

        const char* key = msg + 2;
        const char* value = key + key_len;
        if (key[key_len - 1] || value[val_len - 1])
            continue;

        propfn(key, value, cookie);
    }

out:
    close(fd);
    return ret;
}

/****************************************************************************
 * Name: property_wait
 *
 * Description:
 *   Wait the monitored key until its value updated or key deleted.
 *
 * Input Parameters:
 *   const char* key     : the monitored key string, support fnmatch pattern
 *   char*       newkey  : pointer to a string buffer to receive the key of
 *                         the updated/deleted value
 *   char*       newvalue: pointer to a string buffer to receive the updated
 *                         value or deleted value ('\0')
 *   int         timeout : the wait timeout time (in milliseconds)
 *
 * Returned Value:
 *   On success returns 0, -errno otherwise.
 *
 ****************************************************************************/

int property_wait(const char* key, char* newkey, char* newvalue, int timeout)
{
    if (key == NULL)
        return -EINVAL;

    int fd = property_monitor_open(key);
    if (fd < 0)
        return fd;

    struct pollfd fds = {
        .fd = fd,
        .events = POLLIN
    };

    int ret = poll(&fds, 1, timeout);
    if (ret < 0) {
        ret = -errno;
        KVERR("poll failed, ret=%d\n", ret);
        goto out;
    } else if (ret == 0 || (fds.revents & POLLIN) == 0) {
        ret = -ETIMEDOUT;
        goto out;
    }

    ret = property_monitor_read(fd, newkey, newvalue);

out:
    property_monitor_close(fd);
    return ret;
}

/****************************************************************************
 * Name: property_monitor_open
 *
 * Description:
 *   Open a key monitor channel
 *
 * Input Parameters:
 *   const char* key : the monitored key string, support fnmatch pattern
 *
 * Returned Value:
 *   On success returns a file descriptor, -errno otherwise.
 *
 ****************************************************************************/

int property_monitor_open(const char* key)
{
    if (key == NULL)
        return -EINVAL;

    size_t key_len = strlen(key) + 1;
    if (key_len > PROP_NAME_MAX)
        return -E2BIG;

    int fd = property_connect();
    if (fd < 0) {
        KVERR("connect failed, fd=%d\n", fd);
        return fd;
    }

    /*------------------------*
    |   1   |   1   | key_len |
    |-------|-----------------|
    |  'M'  |key_len|[key'\0']|
    *-------------------------*/

    char cmd[2] = { 'M', key_len };

    struct iovec iov[2] = {
        { .iov_base = cmd, .iov_len = 2 },
        { .iov_base = (char*)key, .iov_len = key_len },
    };

    struct msghdr msg = { 0 };
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    int ret = sendmsg(fd, &msg, 0);
    if (ret < 0) {
        KVERR("sendmsg failed, ret=%d\n", ret);
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
        KVERR("recv failed, ret=%d\n", ret);
        ret = ret < 0 ? -errno : -EINVAL;
        goto out;
    }

    if (err < 0) {
        ret = err;
        goto out;
    }

    return fd;

out:
    close(fd);
    return ret;
}

/****************************************************************************
 * Name: property_monitor_read
 *
 * Description:
 *   Wait the monitored key until its value updated or key deleted.
 *
 * Input Parameters:
 *   int   fd      : file descriptor returned by property_monitor_open()
 *   char* newkey  : pointer to a strint buffer to receive the key of the
 *                   updated/deleted value
 *   char* newvalue: pointer to a string buffer to receive the updated
 *                   value or deleted value ('\0')
 *
 * Returned Value:
 *   On success returns 0, -errno otherwise.
 *
 ****************************************************************************/

int property_monitor_read(int fd, char* newkey, char* newvalue)
{
    char msg[PROP_MSG_MAX];
    ssize_t ret = recv(fd, msg, 2, 0);
    if (ret < 2) {
        KVERR("recv failed, ret=%d, errno=%d\n", ret, errno);
        return ret < 0 ? -errno : -ENODATA;
    }

    size_t key_len = (unsigned char)msg[0];
    if (key_len > PROP_NAME_MAX)
        return -E2BIG;

    size_t val_len = (unsigned char)msg[1];
    if (val_len > PROP_VALUE_MAX)
        return -E2BIG;

    size_t total = key_len + val_len + 2;
    ret = recv_safe(fd, msg, ret, total);
    if (ret < 0) {
        KVERR("recv_safe failed, ret=%d\n", ret);
        return ret;
    }

    const char* key = &msg[2];
    if (newkey != NULL)
        strlcpy(newkey, key, PROP_NAME_MAX);

    if (newvalue != NULL) {
        if (val_len == 0) {
            /*-------------------------*
            |   1   |   1   | key_len |
            |-------------------------|
            |key_len|   0   |[key'\0']|
            *-------------------------*/

            newvalue[0] = '\0';
        } else {
            /*-------------------------------------*
            |   1   |   1   | key_len |  val_len  |
            |-------------------------------------|
            |key_len|val_len|[key'\0']|[value'\0']|
            *-------------------------------------*/

            const char* value = &msg[2 + key_len];
            strlcpy(newvalue, value, PROP_VALUE_MAX);
        }
    }

    return 0;
}

/****************************************************************************
 * Name: property_monitor_close
 *
 * Description:
 *   Close a key monitor channel
 *
 * Input Parameters:
 *   int   fd      : file descriptor returned by property_monitor_open()
 *
 * Returned Value:
 *   On success returns 0, -errno otherwise.
 *
 ****************************************************************************/

int property_monitor_close(int fd)
{
    int ret = close(fd);
    if (ret < 0)
        ret = -errno;
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
    int ret;
    if (fd < 0)
        return fd;

    ret = send(fd, "C", 1, 0) > 0 ? 0 : -errno;

    close(fd);
    return ret;
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
 * Returned Value:
 *   On success returns 0.
 *   On failure returns -errno.
 *
 ****************************************************************************/

int property_reload(void)
{
    int fd = property_connect();
    int ret;
    if (fd < 0)
        return fd;

    ret = send(fd, "R", 1, 0) > 0 ? 0 : -errno;

    close(fd);
    return ret;
}
