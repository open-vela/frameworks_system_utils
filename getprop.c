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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <kvdb.h>

static void callback(const char* name, const char* value, void* cookie)
{
    printf("%s: %s\n", name, value);
}

int main(int argc, char* argv[])
{
    int ret = 0;

    if (argc == 2 && strncmp(argv[1], "-h", 3)) {
        char buf[PROP_VALUE_MAX];
        if (property_get(argv[1], buf, ""))
            printf("%s\n", buf);
        else
            ret = EINVAL;
    } else if (argc == 1)
        ret = -property_list(callback, NULL);
    else
        printf("Usage: %s [key]\n", argv[0]);

    return ret;
}
