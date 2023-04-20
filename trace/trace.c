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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <cutils/trace.h>
#include <nuttx/sched_note.h>

int atrace_marker_fd = -1;
uint64_t atrace_enabled_tags = ~0ull;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint64_t atrace_get_enabled_tags(void)
{
    return atrace_enabled_tags;
}

void atrace_init(void)
{
}

void atrace_setup(void)
{
}

void atrace_update_tags(void)
{
}

void atrace_set_debuggable(bool debuggable)
{
}

void atrace_set_tracing_enabled(bool enabled)
{
}

void atrace_begin_body(const char* name)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "B|%d|%s", gettid(), name);
}

void atrace_end_body(void)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "E|%d", gettid());
}

void atrace_async_begin_body(const char* name, int32_t cookie)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "S|%d|%s|%" PRId32, gettid(),
        name, cookie);
}

void atrace_async_end_body(const char* name, int32_t cookie)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "F|%d|%s|%" PRId32, gettid(),
        name, cookie);
}

void atrace_async_for_track_begin_body(const char* track_name,
    const char* name, int32_t cookie)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "G|%d|%s|%s|%" PRId32, gettid(),
        track_name, name, cookie);
}

void atrace_async_for_track_end_body(const char* track_name, const char *name, int32_t cookie)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "H|%d|%s|%s|%" PRId32, gettid(),
        track_name, name, cookie);
}

void atrace_instant_body(const char* name)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "I|%d|%s", gettid(), name);
}

void atrace_instant_for_track_body(const char* track_name, const char* name)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "N|%d|%s|%s", gettid(),
        track_name, name);
}

void atrace_int_body(const char* name, int32_t value)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "C|%d|%s|%" PRId32,
        gettid(), name, value);
}

void atrace_int64_body(const char* name, int64_t value)
{
    sched_note_printf(NOTE_TAG_ALWAYS, "C|%d|%s|%" PRId64, gettid(),
        name, value);
}
