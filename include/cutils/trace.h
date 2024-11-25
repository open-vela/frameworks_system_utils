/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef _LIBS_CUTILS_TRACE_H
#define _LIBS_CUTILS_TRACE_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* clang-format off */

/**
 * Define ATRACE_TAG before including this header to filter tracing by one of the tags below.
 * Use ATRACE_TAG_ALWAYS for always-on tracing (debug only, as it incurs performance cost).
 * ATRACE_TAG_NEVER or undefined disables tracing.
 *
 * For hardware modules, combine ATRACE_TAG_HAL with relevant tags, e.g., for camera:
 * #define ATRACE_TAG  (ATRACE_TAG_CAMERA | ATRACE_TAG_HAL)
 *
 * Sync with frameworks/base/core/java/android/os/Trace.java.
 */

#define ATRACE_TAG_NEVER             0
#define ATRACE_TAG_ALWAYS            (1<<0)
#define ATRACE_TAG_GRAPHICS          (1<<1)
#define ATRACE_TAG_INPUT             (1<<2)
#define ATRACE_TAG_VIEW              (1<<3)
#define ATRACE_TAG_WEBVIEW           (1<<4)
#define ATRACE_TAG_WINDOW_MANAGER    (1<<5)
#define ATRACE_TAG_ACTIVITY_MANAGER  (1<<6)
#define ATRACE_TAG_SYNC_MANAGER      (1<<7)
#define ATRACE_TAG_AUDIO             (1<<8)
#define ATRACE_TAG_VIDEO             (1<<9)
#define ATRACE_TAG_CAMERA            (1<<10)
#define ATRACE_TAG_HAL               (1<<11)
#define ATRACE_TAG_APP               (1<<12)
#define ATRACE_TAG_RESOURCES         (1<<13)
#define ATRACE_TAG_DALVIK            (1<<14)
#define ATRACE_TAG_RS                (1<<15)
#define ATRACE_TAG_BIONIC            (1<<16)
#define ATRACE_TAG_POWER             (1<<17)
#define ATRACE_TAG_PACKAGE_MANAGER   (1<<18)
#define ATRACE_TAG_SYSTEM_SERVER     (1<<19)
#define ATRACE_TAG_DATABASE          (1<<20)
#define ATRACE_TAG_NETWORK           (1<<21)
#define ATRACE_TAG_ADB               (1<<22)
#define ATRACE_TAG_VIBRATOR          (1<<23)
#define ATRACE_TAG_AIDL              (1<<24)
#define ATRACE_TAG_NNAPI             (1<<25)
#define ATRACE_TAG_RRO               (1<<26)
#define ATRACE_TAG_THERMAL           (1 << 27)
#define ATRACE_TAG_LAST              ATRACE_TAG_THERMAL

/* Reserved for initialization. */

#define ATRACE_TAG_NOT_READY         (1ULL<<63)

#define ATRACE_TAG_VALID_MASK        ((ATRACE_TAG_LAST - 1) | ATRACE_TAG_LAST)

#ifndef ATRACE_TAG
  #define ATRACE_TAG ATRACE_TAG_NEVER
#elif ATRACE_TAG_VALID_MASK < ATRACE_TAG
  #error ATRACE_TAG must be defined to be one of the tags defined in cutils/trace.h
#endif

/**
 * Opens trace file and reads initial tags from the system property.
 * This is automatically called when the first trace function is used.
 */
void atrace_setup(void);

/**
 * Updates `atrace_enabled_tags` from the system property `debug.atrace.tags.enableflags`.
 */
void atrace_update_tags(void);

/**
 * Enables or disables tracing for the current process (to prevent tracing in Zygote).
 */
void atrace_set_tracing_enabled(bool enabled);

/**
 * Set of enabled trace tags, initialized to ATRACE_TAG_NOT_READY. Zero indicates failure.
 */
extern uint64_t atrace_enabled_tags;

/**
 * Kernel trace buffer handle, initialized to -1. A valid fd indicates setup success.
 */
extern int atrace_marker_fd;

/**
 * Initializes tracing by opening the trace_marker file (optional, runs automatically).
 */
#define ATRACE_INIT() atrace_init()
#define ATRACE_GET_ENABLED_TAGS() atrace_get_enabled_tags()

void atrace_init(void);
uint64_t atrace_get_enabled_tags(void);

/**
 * Checks if a given trace tag is enabled, useful for expensive trace calculations.
 * Returns nonzero if enabled, zero otherwise.
 */
#define ATRACE_ENABLED() atrace_is_tag_enabled(ATRACE_TAG)
static inline uint64_t atrace_is_tag_enabled(uint64_t tag)
{
    return atrace_get_enabled_tags() & tag;
}

/**
 * Starts a tracing context, typically used for function timing.
 * @param name: Context name.
 */
#define ATRACE_BEGIN(name) atrace_begin(ATRACE_TAG, name)
static inline void atrace_begin(uint64_t tag, const char* name)
{
    if (atrace_is_tag_enabled(tag)) {
        atrace_begin_body(name);
    }
}

/**
 * Ends a tracing context that was started by ATRACE_BEGIN.
 */
#define ATRACE_END() atrace_end(ATRACE_TAG)
static inline void atrace_end(uint64_t tag)
{
    if (atrace_is_tag_enabled(tag)) {
        atrace_end_body();
    }
}

/**
 * Starts an asynchronous tracing event. Unlike ATRACE_BEGIN/END, async events don't require nesting.
 * @param name: Event name
 * @param cookie: Unique identifier for the event.
 */
#define ATRACE_ASYNC_BEGIN(name, cookie) atrace_async_begin(ATRACE_TAG, name, cookie)
static inline void atrace_async_begin(uint64_t tag, const char* name, int32_t cookie)
{
    if (atrace_is_tag_enabled(tag)) {
        atrace_async_begin_body(name, cookie);
    }
}

/**
 * Ends an asynchronous tracing event, matching a previous ATRACE_ASYNC_BEGIN.
 * @param name: Event name
 * @param cookie: Unique identifier for the event.
 */
#define ATRACE_ASYNC_END(name, cookie) atrace_async_end(ATRACE_TAG, name, cookie)
static inline void atrace_async_end(uint64_t tag, const char* name, int32_t cookie)
{
    if (atrace_is_tag_enabled(tag)) {
        atrace_async_end_body(name, cookie);
    }
}

/**
 * Starts an asynchronous event with a trace track.
 * @param track_name: Track name where this async event is recorded.
 * @param name: Event name.
 * @param cookie: Unique event identifier.
 */

#define ATRACE_ASYNC_FOR_TRACK_BEGIN(track_name, name, cookie) \
        atrace_async_for_track_begin(ATRACE_TAG, track_name, name, cookie)

static inline void atrace_async_for_track_begin(uint64_t tag, const char* track_name,
                                                const char* name, int32_t cookie) {
    if (atrace_is_tag_enabled(tag)) {
        void atrace_async_for_track_begin_body(const char*, const char*, int32_t);
        atrace_async_for_track_begin_body(track_name, name, cookie);
    }
}

/**
 * @brief To indicate the beginning of async tracing action with trace info
 *
 * Trace the end of an asynchronous event.
 * This should correspond to a previous ATRACE_ASYNC_FOR_TRACK_BEGIN.
 * @param[in] trace_name the track name is the name of the row where this
 *                       async event should be recorded. The track name,
 *                       name, and cookie used to begin an event must be
 *                       used to end it.
 * @param[in] name       the name to describes the event
 * @param[in] cookie     using to provides a unique identifier for distinguishing
 *                       simultaneous events. The name and cookie used to begin an
 *                       event must be used to end it.
 */
#define ATRACE_ASYNC_FOR_TRACK_END(track_name, name, cookie) \
    atrace_async_for_track_end(ATRACE_TAG, track_name, name, cookie)
static inline void atrace_async_for_track_end(uint64_t tag, const char* track_name,
                                              const char* name, int32_t cookie) {
    if (atrace_is_tag_enabled(tag)) {
        void atrace_async_for_track_end_body(const char*, const char*, int32_t);
        atrace_async_for_track_end_body(track_name, name, cookie);
    }
}

/**
 * @brief To indicate trace an instantaneous context
 * An "instant" is an event with no defined duration. Visually is displayed like a single marker
 * in the timeline (rather than a span, in the case of begin/end events).
 *
 * By default, instant events are added into a dedicated track that has the same name of the event.
 * Use atrace_instant_for_track to put different instant events into the same timeline track/row.
 *
 * @param[in] name the value that using to identify the context
 */
#define ATRACE_INSTANT(name) atrace_instant(ATRACE_TAG, name)
static inline void atrace_instant(uint64_t tag, const char* name) {
    if (atrace_is_tag_enabled(tag)) {
        void atrace_instant_body(const char*);
        atrace_instant_body(name);
    }
}

/**
 * @brief To indicate trace an instantaneous context with track specified
 *
 * An "instant" is an event with no defined duration. Visually is displayed like a single marker
 * in the timeline (rather than a span, in the case of begin/end events).
 *
 * @param[in] name       the name used to identify the context.
 * @param[in] track_name is the name of the row where the event should be recorded.
 */
#define ATRACE_INSTANT_FOR_TRACK(trackName, name) \
    atrace_instant_for_track(ATRACE_TAG, trackName, name)
static inline void atrace_instant_for_track(uint64_t tag, const char* track_name,
                                            const char* name) {
    if (atrace_is_tag_enabled(tag)) {
        void atrace_instant_for_track_body(const char*, const char*);
        atrace_instant_for_track_body(track_name, name);
    }
}

/**
 * @brief To indicate trace an integer counter value
 *
 * @param[in] name  is used to identify the counter.
 * @param[in] velue this can be used to track how a value changes over time.
 */
#define ATRACE_INT(name, value) atrace_int(ATRACE_TAG, name, value)
static inline void atrace_int(uint64_t tag, const char* name, int32_t value)
{
    if (atrace_is_tag_enabled(tag)) {
        void atrace_int_body(const char*, int32_t);
        atrace_int_body(name, value);
    }
}

/**
 * @brief To indicate trace an 64-bit integer counter value
 *
 * @param[in] name  is used to identify the counter.
 * @param[in] value This can be used to track how a value changes over time.
 */
#define ATRACE_INT64(name, value) atrace_int64(ATRACE_TAG, name, value)
static inline void atrace_int64(uint64_t tag, const char* name, int64_t value)
{
    if (atrace_is_tag_enabled(tag)) {
        void atrace_int64_body(const char*, int64_t);
        atrace_int64_body(name, value);
    }
}

/* clang-format on */
#ifdef __cplusplus
}
#endif

#endif // _LIBS_CUTILS_TRACE_H
