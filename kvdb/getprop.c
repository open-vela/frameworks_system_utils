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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <kvdb.h>

#ifdef CONFIG_KVDB_DUMPLIST
static void callback(const char* name, const void* value, size_t val_len, void* cookie)
{
    const char* temp = value;
    ssize_t i;

    for (i = 0; i < val_len; i++) {
        if (!isprint(temp[i]))
            break;
    }

    if (name != NULL)
        printf("%s: ", name);

    /* All previous characters can be printed and ending '\0' */
    if (i == val_len - 1 && temp[i] == '\0') {
        printf("%s\n", temp);
    } else {
        for (i = 0; i < val_len; i++)
            printf("%02x", temp[i]);
        printf("\n");
    }
}
#endif

int main(int argc, char* argv[])
{
    int ret = 0;

    if (argc == 2 && strncmp(argv[1], "-h", 3)) {
        char buf[PROP_VALUE_MAX];

        ssize_t len = property_get_binary(argv[1], buf, sizeof(buf));
        if (len < 0)
            return len;

        callback(NULL, buf, len, NULL);
    }
#ifdef CONFIG_KVDB_DUMPLIST
    else if (argc == 1)
        ret = -property_list_binary(callback, NULL);
#endif
    else
        printf("Usage: %s [key]\n", argv[0]);

    return ret;
}
