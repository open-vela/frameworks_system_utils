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

#include <stdio.h>

#include "kvdb.h"

void callback(const char* name, const char* value, void* cookie)
{
    printf("%s: %s\n", name, value);
}

int main(int argc, char* argv[])
{
    char buf[PROP_VALUE_MAX];
    if (argc == 1) {
        property_list(callback, NULL);
    } else if (argc == 2) {
        int ret = property_get(argv[1], buf, "");
        if (ret < 0)
            printf("Returned %d\n", ret);
        else
            printf("%s\n", buf);
    } else
        printf("\nUsage: %s [key]\n", argv[0]);
    return 0;
}
