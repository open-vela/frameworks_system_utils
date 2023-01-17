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

#ifndef _INCLUDE_SYS_SYSTEM_PROPERTIES_H
#define _INCLUDE_SYS_SYSTEM_PROPERTIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct prop_info prop_info;

#if defined(__cplusplus)
extern "C"
{
#endif

/*
 * Sets system property `name` to `value`, creating the system property if it doesn't already exist.
 */
int __system_property_set(const char* __name, const char* __value);

/*
 * Returns a `prop_info` corresponding system property `name`, or nullptr if it doesn't exist.
 * Use __system_property_read_callback to query the current value.
 *
 * Property lookup is expensive, so it can be useful to cache the result of this function.
 */
const prop_info* __system_property_find(const char* __name);

/*
 * Calls `callback` with a consistent trio of name, value, and serial number for property `pi`.
 */
void __system_property_read_callback(const prop_info* __pi,
    void (*__callback)(void* __cookie, const char* __name, const char* __value, uint32_t __serial),
    void* __cookie);

/*
 * Passes a `prop_info` for each system property to the provided
 * callback.  Use __system_property_read_callback() to read the value.
 *
 * This method is for inspecting and debugging the property system, and not generally useful.
 */
int __system_property_foreach(void (*__callback)(const prop_info* __pi, void* __cookie), void* __cookie);

/*
 * Waits for the specific system property identified by `pi` to be updated
 * past `old_serial`. Waits no longer than `relative_timeout`, or forever
 * if `relaive_timeout` is null.
 *
 * If `pi` is null, waits for the global serial number instead.
 *
 * If you don't know the current serial, use 0.
 *
 * Returns true and updates `*new_serial_ptr` on success, or false if the call
 * timed out.
 */
bool __system_property_wait(const prop_info* __pi, uint32_t __old_serial, uint32_t* __new_serial_ptr,
                            const struct timespec* __relative_timeout);

#if defined(__cplusplus)
}
#endif

#endif
