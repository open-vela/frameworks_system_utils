/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _INCLUDE_SYS_SYSTEM_PROPERTIES_H
#define _INCLUDE_SYS_SYSTEM_PROPERTIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct prop_info prop_info;

#define PROP_VALUE_MAX  255

/*
 * Sets system property `name` to `value`, creating it if it doesn't exist.
 */
int __system_property_set(const char* __name,
                          const char* __value);

/*
 * Returns a `prop_info` for system property `name`, or NULL if not found.
 * Consider caching the result due to the expensive lookup.
 */
const prop_info* __system_property_find(const char* __name);

/*
 * Calls `callback` with name, value, and serial number for property `pi`.
 */
void __system_property_read_callback(const prop_info* __pi,
    void (*__callback)(void* __cookie, const char* __name, const char* __value, uint32_t __serial),
    void* __cookie);

/*
 * Iterates through all system properties and calls the provided callback for each.
 * Primarily for debugging and inspection.
 */
int __system_property_foreach(void (*__callback)(const prop_info* __pi, void* __cookie), void* __cookie);

/*
 * Waits for the system property `pi` to be updated past `old_serial`, with an optional timeout.
 * If `pi` is NULL, waits for the global serial number.
 */
struct timespec;
bool __system_property_wait(const prop_info* __pi, uint32_t __old_serial, uint32_t* __new_serial_ptr, const struct timespec* __relative_timeout);

/* Deprecated: Property name length limit. */
#define PROP_NAME_MAX   127

/* Deprecated: Use __system_property_read_callback instead. */
int __system_property_read(const prop_info* __pi, char* __name, char* __value);

/* Deprecated: Use __system_property_read_callback instead. */
int __system_property_get(const char* __name, char* __value);

#if defined(__cplusplus)
}
#endif

#endif
