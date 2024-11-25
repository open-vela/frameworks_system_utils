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

#include <errno.h>
#include <kvdb.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/system_properties.h>

struct system_property_foreach_cookie {
    void* __cookie;
    void (*__callback)(const prop_info* __pi, void* __cookie);
};

static uint32_t __system_property_serial_num = 0;

/*
 * Sets system property `name` to `value`, creating the system property if it doesn't already exist.
 */
int __system_property_set(const char* __name, const char* __value)
{
    int ret = property_set(__name, __value);
    if (ret >= 0)
        __system_property_serial_num++;

    return ret;
}

/*
 * Returns a `prop_info` corresponding system property `name`, or nullptr if it doesn't exist.
 * Use __system_property_read_callback to query the current value.
 *
 * Property lookup is expensive, so it can be useful to cache the result of this function.
 */
const prop_info* __system_property_find(const char* __name)
{
    int ret = property_get(__name, NULL, NULL);
    if (ret < 0)
        return NULL;

    return (const prop_info*)__name;
}

/*
 * Calls `callback` with a consistent trio of name, value, and serial number for property `pi`.
 */
void __system_property_read_callback(const prop_info* __pi,
    void (*__callback)(void* __cookie, const char* __name, const char* __value, uint32_t __serial),
    void* __cookie)
{
    char value[PROP_VALUE_MAX];

    if (__callback == NULL)
        return;

    int ret = property_get((const char*)__pi, value, NULL);
    if (ret < 0)
        return;

    __callback(__cookie, (const char*)__pi, value, __system_property_serial_num);
}

/*
 * used by __system_property_foreach() to pass to property_list.
 */
static void __system_property_foreach_callback(const char* key, const char* value, void* cookie)
{
    struct system_property_foreach_cookie* pcookie = cookie;
    pcookie->__callback((const prop_info*)key, pcookie->__cookie);
}

/*
 * Iterates over each system property and invokes the provided callback.
 * Use __system_property_read_callback() to read property values.
 *
 * This function is mainly for inspecting and debugging the property system.
 */

int __system_property_foreach(void (*__callback)(const prop_info* __pi, void* __cookie), void* __cookie)
{
    struct system_property_foreach_cookie cookie = {
         .__cookie = __cookie,
         .__callback = __callback};

    return property_list(__system_property_foreach_callback, &cookie);
}

/*
 * Waits for the system property `pi` to be updated past `old_serial`, with an optional timeout.
 * If `pi` is NULL, it waits for the global serial number.
 * If the serial is unknown, pass 0.
 *
 * Returns true if updated within the timeout, false if the call times out.
 */

bool __system_property_wait(const prop_info* __pi, uint32_t __old_serial, uint32_t* __new_serial_ptr, const struct timespec* __relative_timeout)
{
    int timems = __relative_timeout->tv_sec * 1000 + __relative_timeout->tv_nsec / 1000000;
    int ret = property_wait(__pi ? (const char*)__pi : "*", NULL, NULL, 0, timems);
    if (ret >= 0) {
        if (__new_serial_ptr)
            *__new_serial_ptr = __system_property_serial_num;
        return true;
    }

    return false;
}

int __system_property_read(const prop_info* __pi, char* __name, char* __value)
{
    strlcpy(__name, (const char*)__pi, PROP_NAME_MAX);
    return property_get(__name, __value, NULL);
}

int __system_property_get(const char* __name, char* __value)
{
    return property_get(__name, __value, NULL);
}
