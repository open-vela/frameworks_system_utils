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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/streams.h>

#include <debug.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include <android/log.h>
#include <android/set_abort_message.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LOG_BUF_SIZE 256

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const int g_logprimap[ANDROID_LOG_SILENT + 1] = {
    LOG_DEBUG, /* ANDROID_LOG_UNKNOWN */
    LOG_DEBUG, /* ANDROID_LOG_DEFAULT */
    LOG_DEBUG, /* ANDROID_LOG_VERBOSE */
    LOG_DEBUG, /* ANDROID_LOG_DEBUG */
    LOG_INFO, /* ANDROID_LOG_INFO */
    LOG_WARNING, /* ANDROID_LOG_WARN */
    LOG_ERR, /* ANDROID_LOG_ERROR */
    LOG_CRIT, /* ANDROID_LOG_FATAL */
    LOG_DEBUG, /* ANDROID_LOG_SILENT */
};

static __android_logger_function g_logger_function = __android_log_logd_logger;
static __android_aborter_function g_aborter_function = __android_log_default_aborter;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * Call get the default tag, if not set the default tag before, return "nullptr".
 */
static const char* __android_log_get_default_tag(void)
{
    char* envtag = getenv("ANDROID_LOG_DEFAULT_TAG");
    if (envtag == NULL) {
        return getprogname();
    }

    return envtag;
}

/**
 * Call __android_log_write_log_message() with a large buffer to support pass the message to user set
 * logger
 */
static void __android_log_write_log_message_buf(int bufID, int prio, const char* tag, const char* fmt, va_list ap)
{
    char buf[LOG_BUF_SIZE];

    vsnprintf(buf, LOG_BUF_SIZE, fmt, ap);

    struct __android_log_message log_message = {
        sizeof(struct __android_log_message), bufID, prio, tag, NULL, 0, buf
    };
    __android_log_write_log_message(&log_message);
}

/**
 * Writes a formatted string to log buffer `id`,
 * with priority `prio` and tag `tag`.
 *
 * 1 is returned on success.  On failure, a negated errno value is
 * returned.
 */
static int __android_log_buf_vprint(int bufID, int prio, const char* tag, const char* fmt, va_list ap)
{
    if (tag == NULL) {
        tag = __android_log_get_default_tag();
    }

    if (!__android_log_is_loggable(prio, tag, ANDROID_LOG_VERBOSE)) {
        return -EPERM;
    }

    if (g_logger_function == __android_log_logd_logger) {
        struct va_format vaf;

#ifdef va_copy
        va_list copy;
        va_copy(copy, ap);
        vaf.fmt = fmt;
        vaf.va = &copy;
#else
        vaf.fmt = fmt;
        vaf.va = &ap;
#endif

        syslog(g_logprimap[prio], "[%s] %pV\n", tag, &vaf);

#ifdef va_copy
        va_end(copy);
#endif
    } else {
        __android_log_write_log_message_buf(bufID, prio, tag, fmt, ap);
    }

    return 1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * Writes the constant string `text` to the log, with priority `prio` and tag
 * `tag`.
 */
int __android_log_write(int prio, const char* tag, const char* text)
{
    return __android_log_buf_write(LOG_ID_MAIN, prio, tag, text);
}

/**
 * Writes a formatted string to the log, with priority `prio` and tag `tag`.
 * The details of formatting are the same as for
 * [printf(3)](http://man7.org/linux/man-pages/man3/printf.3.html).
 */
int __android_log_print(int prio, const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = __android_log_vprint(prio, tag, fmt, ap);
    va_end(ap);
    return ret;
}

/**
 * Equivalent to `__android_log_print`, but taking a `va_list`.
 * (If `__android_log_print` is like `printf`, this is like `vprintf`.)
 */
int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap)
{
    return __android_log_buf_vprint(LOG_ID_MAIN, prio, tag, fmt, ap);
}

/**
 * Writes an assertion failure to the log (as `ANDROID_LOG_FATAL`) and to
 * stderr, before calling
 * [abort(3)](http://man7.org/linux/man-pages/man3/abort.3.html).
 *
 * If `fmt` is non-null, `cond` is unused. If `fmt` is null, the string
 * `Assertion failed: %s` is used with `cond` as the string argument.
 * If both `fmt` and `cond` are null, a default string is provided.
 *
 * Most callers should use
 * [assert(3)](http://man7.org/linux/man-pages/man3/assert.3.html) from
 * `&lt;assert.h&gt;` instead, or the `__assert` and `__assert2` functions
 * provided by bionic if more control is needed. They support automatically
 * including the source filename and line number more conveniently than this
 * function.
 */
void __android_log_assert(const char* cond, const char* tag, const char* fmt, ...)
{
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        __android_log_vprint(ANDROID_LOG_FATAL, tag, fmt, ap);
        va_end(ap);
    } else {
        /* Msg not provided, log condition.  N.B. Do not use cond directly as
         * format string as it could contain spurious '%' syntax (e.g.
         * "%d" in "blocks%devs == 0").
         */
        if (cond) {
            __android_log_print(ANDROID_LOG_FATAL, tag, "Assertion failed: %s\n", cond);
        } else {
            __android_log_print(ANDROID_LOG_FATAL, tag, "Unspecified assertion failed\n");
        }
    }

    __android_log_call_aborter(tag);

    /* noreturn function */
    while (1)
        ;
}

/**
 * Writes the constant string `text` to the log buffer `id`,
 * with priority `prio` and tag `tag`.
 *
 * Apps should use __android_log_write() instead.
 */
int __android_log_buf_write(int bufID, int prio, const char* tag, const char* text)
{
    if (!__android_log_is_loggable(prio, tag, ANDROID_LOG_VERBOSE)) {
        return -EPERM;
    }

    struct __android_log_message log_message = {
        sizeof(struct __android_log_message), bufID, prio, tag, NULL, 0, text
    };
    __android_log_write_log_message(&log_message);
    return 1;
}

/**
 * Writes a formatted string to log buffer `id`,
 * with priority `prio` and tag `tag`.
 * The details of formatting are the same as for
 * [printf(3)](http://man7.org/linux/man-pages/man3/printf.3.html).
 *
 * Apps should use __android_log_print() instead.
 */
int __android_log_buf_print(int bufID, int prio, const char* tag, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = __android_log_buf_vprint(bufID, prio, tag, fmt, ap);
    va_end(ap);
    return ret;
}

/**
 * Writes the log message specified by log_message.  log_message includes additional file name and
 * line number information that a logger may use.  log_message is versioned for backwards
 * compatibility.
 * This assumes that loggability has already been checked through __android_log_is_loggable().
 * Higher level logging libraries, such as libbase, first check loggability, then format their
 * buffers, then pass the message to liblog via this function, and therefore we do not want to
 * duplicate the loggability check here.
 *
 * @param log_message the log message itself, see __android_log_message.
 *
 * Available since API level 30.
 */
void __android_log_write_log_message(struct __android_log_message* log_message)
{
    if (log_message->tag == NULL) {
        log_message->tag = __android_log_get_default_tag();
    }

    if (log_message->priority == ANDROID_LOG_FATAL) {
        android_set_abort_message(log_message->message);
    }

    g_logger_function(log_message);
}

/**
 * Sets a user defined logger function.  All log messages sent to liblog will be set to the
 * function pointer specified by logger for processing.  It is not expected that log messages are
 * already terminated with a new line.  This function should add new lines if required for line
 * separation.
 *
 * @param logger the new function that will handle log messages.
 *
 * Available since API level 30.
 */
void __android_log_set_logger(__android_logger_function logger)
{
    g_logger_function = logger;
}

/**
 * Writes the log message to logd.  This is an __android_logger_function and can be provided to
 * __android_log_set_logger().  It is the default logger when running liblog on a device.
 *
 * @param log_message the log message to write, see __android_log_message.
 *
 * Available since API level 30.
 */
void __android_log_logd_logger(const struct __android_log_message* log_message)
{
    int32_t priority = log_message->priority > ANDROID_LOG_SILENT ? ANDROID_LOG_FATAL : log_message->priority;

    if (log_message->file != NULL) {
        syslog(g_logprimap[priority], "[%s %s:%" PRIu32 "] %s\n", log_message->tag,
            log_message->file, log_message->line, log_message->message);
    } else {
        syslog(g_logprimap[priority], "[%s] %s\n", log_message->tag,
            log_message->message);
    }
}

/**
 * Writes the log message to stderr.  This is an __android_logger_function and can be provided to
 * __android_log_set_logger().  It is the default logger when running liblog on host.
 *
 * @param log_message the log message to write, see __android_log_message.
 *
 * Available since API level 30.
 */
void __android_log_stderr_logger(const struct __android_log_message* log_message)
{
    if (!__android_log_is_loggable(log_message->priority, NULL, ANDROID_LOG_VERBOSE)) {
        return;
    }

    struct tm now;
    time_t t = time(NULL);
    localtime_r(&t, &now);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%m-%d %H:%M:%S", &now);

    static const char log_characters[ANDROID_LOG_SILENT + 1] = "XXVDIWEF";
    int32_t priority = log_message->priority > ANDROID_LOG_SILENT ? ANDROID_LOG_FATAL : log_message->priority;
    if (log_message->file != NULL) {
        fprintf(stderr, "%s %c %s %5d %5d %s:%" PRIu32 "] %s\n",
            log_message->tag, log_characters[priority], timestamp, getpid(), gettid(),
            log_message->file, log_message->line, log_message->message);
    } else {
        fprintf(stderr, "%s %c %s %5d %5d] %s\n",
            log_message->tag, log_characters[priority], timestamp, getpid(), gettid(),
            log_message->message);
    }

    fflush(stderr);
}

/**
 * Sets a user defined aborter function that is called for __android_log_assert() failures.  This
 * user defined aborter function is highly recommended to abort and be noreturn, but is not strictly
 * required to.
 *
 * @param aborter the new aborter function, see __android_aborter_function.
 *
 * Available since API level 30.
 */
void __android_log_set_aborter(__android_aborter_function aborter)
{
    g_aborter_function = aborter;
}

/**
 * Calls the stored aborter function.  This allows for other logging libraries to use the same
 * aborter function by calling this function in liblog.
 *
 * @param abort_message an additional message supplied when aborting, for example this is used to
 *                      call android_set_abort_message() in __android_log_default_aborter().
 *
 * Available since API level 30.
 */
void __android_log_call_aborter(const char* abort_message)
{
    g_aborter_function(abort_message);
}

/**
 * Sets android_set_abort_message() on device then aborts().  This is the default aborter.
 *
 * @param abort_message an additional message supplied when aborting.  This functions calls
 *                      android_set_abort_message() with its contents.
 *
 * Available since API level 30.
 */
void __android_log_default_aborter(const char* abort_message)
{
    android_set_abort_message(abort_message);
    abort();
}

/**
 * Use the per-tag properties "log.tag.<tagname>" along with the minimum priority from
 * __android_log_set_minimum_priority() to determine if a log message with a given prio and tag will
 * be printed.  A non-zero result indicates yes, zero indicates false.
 *
 * If both a priority for a tag and a minimum priority are set by
 * __android_log_set_minimum_priority(), then the lowest of the two values are to determine the
 * minimum priority needed to log.  If only one is set, then that value is used to determine the
 * minimum priority needed.  If none are set, then default_priority is used.
 *
 * @param prio         the priority to test, takes android_LogPriority values.
 * @param tag          the tag to test.
 * @param default_prio the default priority to use if no properties or minimum priority are set.
 * @return an integer where 1 indicates that the message is loggable and 0 indicates that it is not.
 *
 * Available since API level 30.
 */
int __android_log_is_loggable(int prio, const char* tag, int default_prio)
{
    int minimum_log_priority = __android_log_get_minimum_priority();

    if (minimum_log_priority != ANDROID_LOG_DEFAULT) {
        return prio >= minimum_log_priority;
    } else {
        return prio >= default_prio;
    }
}

/**
 * Use the per-tag properties "log.tag.<tagname>" along with the minimum priority from
 * __android_log_set_minimum_priority() to determine if a log message with a given prio and tag will
 * be printed.  A non-zero result indicates yes, zero indicates false.
 *
 * If both a priority for a tag and a minimum priority are set by
 * __android_log_set_minimum_priority(), then the lowest of the two values are to determine the
 * minimum priority needed to log.  If only one is set, then that value is used to determine the
 * minimum priority needed.  If none are set, then default_priority is used.
 *
 * @param prio         the priority to test, takes android_LogPriority values.
 * @param tag          the tag to test.
 * @param len          the length of the tag.
 * @param default_prio the default priority to use if no properties or minimum priority are set.
 * @return an integer where 1 indicates that the message is loggable and 0 indicates that it is not.
 *
 * Available since API level 30.
 */
int __android_log_is_loggable_len(int prio, const char* tag, size_t len, int default_prio)
{
    return __android_log_is_loggable(prio, tag, default_prio);
}

/**
 * Sets the minimum priority that will be logged for this process.
 *
 * @param priority the new minimum priority to set, takes android_LogPriority values.
 * @return the previous set minimum priority as android_LogPriority values, or
 *         ANDROID_LOG_DEFAULT if none was set.
 *
 * Available since API level 30.
 */
int32_t __android_log_set_minimum_priority(int32_t priority)
{
    char buffer[sizeof(int32_t) * 8 + 1];
    int32_t old_minimum_log_priority = __android_log_get_minimum_priority();

    priority = priority > ANDROID_LOG_SILENT ? ANDROID_LOG_FATAL : priority;
    setenv("ANDROID_LOG_MIN_PRIORITY", itoa(priority, buffer, 10), true);
    return old_minimum_log_priority;
}

/**
 * Gets the minimum priority that will be logged for this process.  If none has been set by a
 * previous __android_log_set_minimum_priority() call, this returns ANDROID_LOG_DEFAULT.
 *
 * @return the current minimum priority as android_LogPriority values, or
 *         ANDROID_LOG_DEFAULT if none is set.
 *
 * Available since API level 30.
 */
int32_t __android_log_get_minimum_priority(void)
{
    char* envprio = getenv("ANDROID_LOG_MIN_PRIORITY");
    if (envprio == NULL) {
        return ANDROID_LOG_DEFAULT;
    }

    return atoi(envprio);
}

/**
 * Sets the default tag if no tag is provided when writing a log message.  Defaults to
 * getprogname().  This truncates tag to the maximum log message size, though appropriate tags
 * should be much smaller.
 *
 * @param tag the new log tag.
 *
 * Available since API level 30.
 */
void __android_log_set_default_tag(const char* tag)
{
    if (tag != NULL) {
        setenv("ANDROID_LOG_DEFAULT_TAG", tag, true);
    }
}

int __android_log_error_write(int tag, const char* subTag, int32_t uid,
    const char* data, uint32_t dataLen)
{
    __android_log_print(ANDROID_LOG_ERROR, subTag,
        "tag: %d uid: %" PRId32 " data: %s\n",
        tag, uid, data);
    return 0;
}
