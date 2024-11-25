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
#include <string.h>

#include <kvdb.h>

int main(int argc, char* argv[])
{
    int ret = 0;

    if (argc == 3)
        ret = -property_set(argv[1], argv[2]);
    else if (argc == 2 && strncmp(argv[1], "-h", 3))
        ret = -property_delete(argv[1]);
    else
        printf("Usage: %s <key> [value]\n", argv[0]);

    if (ret > 0)
        printf("Error: %s\n", strerror(ret));
    else {
        ret = -property_commit();
        if (ret > 0)
            printf("Error: commit %s\n", strerror(ret));
    }

    return ret;
}
