/****************************************************************************
 * frameworks/utils/kvdb/qemu_properties.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <kvdb.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_TRIES 5
#define BUFF_SIZE (PROPERTY_KEY_MAX + PROPERTY_VALUE_MAX + 2)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int
qemu_pipe_open_ns(const char* ns, const char* pipe_name, int flags)
{
    char buf[PATH_MAX];
    int buf_len;

    int fd = open("/dev/goldfish_pipe", flags);
    if (fd < 0) {
        return -errno;
    }

    if (ns) {
        buf_len = snprintf(buf, sizeof(buf), "pipe:%s:%s", ns, pipe_name);
    } else {
        buf_len = snprintf(buf, sizeof(buf), "pipe:%s", pipe_name);
    }

    if (write(fd, buf, buf_len + 1) < 0) {
        fprintf(stderr, "%s:%d: Could not connect to the '%s' service: %s\n",
            __func__, __LINE__, buf, strerror(errno));
        close(fd);
        return -errno;
    }

    return fd;
}

static int
qemud_channel_send(int pipe, const void* msg, int size)
{
    char header[5];

    if (size < 0) {
        size = strlen(msg);
    }

    if (size == 0) {
        return 0;
    }

    snprintf(header, sizeof(header), "%04x", size);
    if (write(pipe, header, 4) < 0) {
        return -errno;
    }

    if (write(pipe, msg, size) < 0) {
        return -errno;
    }

    return size;
}

static int
qemud_channel_recv(int pipe, void* msg, int maxsize)
{
    char header[5];
    int size;

    if (read(pipe, header, 4) < 0) {
        return -errno;
    }

    header[4] = 0;

    if (sscanf(header, "%04x", &size) != 1) {
        return -errno;
    }

    if (size > maxsize) {
        return -errno;
    }

    if (read(pipe, msg, size) < 0) {
        return -errno;
    }

    return size;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(void)
{
    int qemud_fd;

    /* try to connect to the qemud service */

    int tries = MAX_TRIES;

    while (true) {
        qemud_fd = qemu_pipe_open_ns("qemud", "boot-properties", O_RDWR);
        if (qemud_fd >= 0)
            break;

        if (--tries <= 0) {
            fprintf(stderr,
                "Could not connect after too many tries. Aborting\n");
            return EXIT_FAILURE;
        }

        fprintf(stderr, "waiting 1s to wait for qemud.\n");
        sleep(1);
    }

    fprintf(stderr, "connected to \"boot-properties\" qemud service.\n");

    /* send the 'list' command to the service */

    if (qemud_channel_send(qemud_fd, "list", -1) < 0) {
        fprintf(stderr,
            "could not send command to \"boot-properties\" service\n");
        return EXIT_FAILURE;
    }

    /* read each system property as a single line from the service,
     * until exhaustion.
     */

    while (true) {
        char* prop_key;
        char* prop_value;
        char temp[BUFF_SIZE];
        int len = qemud_channel_recv(qemud_fd, temp, sizeof(temp) - 1);

        /* lone NUL-byte signals end of properties */

        if (len < 0 || len > (BUFF_SIZE - 1) || !temp[0]) {
            break;
        }

        temp[len] = '\0'; /* zero-terminate string */

        /* separate propery name from value */

        prop_key = temp;
        prop_value = strchr(temp, '=');
        if (!prop_value) {
            continue;
        }

        *prop_value = '\0';
        ++prop_value;

        fprintf(stderr, "key = %s | value = %s\n", prop_key, prop_value);
        if (property_set(prop_key, prop_value) < 0) {
            continue;
        }
    }

    close(qemud_fd);

    return EXIT_SUCCESS;
}
