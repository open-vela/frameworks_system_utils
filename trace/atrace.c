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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/trace.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct atrace_category {
    uint64_t tag;
    const char* name;
    const char* help;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct atrace_category category_list[] = {
    { ATRACE_TAG_NEVER, "never", "never output trace" },
    { ATRACE_TAG_ALWAYS, "always", "always output trace" },
    { ATRACE_TAG_GRAPHICS, "gfx", "graphics" },
    { ATRACE_TAG_INPUT, "input", "input" },
    { ATRACE_TAG_VIEW, "view", "view" },
    { ATRACE_TAG_WEBVIEW, "webview", "webview" },
    { ATRACE_TAG_WINDOW_MANAGER, "wm", "window_manager" },
    { ATRACE_TAG_ACTIVITY_MANAGER, "am", "activity_manager" },
    { ATRACE_TAG_SYNC_MANAGER, "sm", "sync_manager" },
    { ATRACE_TAG_AUDIO, "audio", "audio" },
    { ATRACE_TAG_VIDEO, "video", "video" },
    { ATRACE_TAG_CAMERA, "camera", "camera" },
    { ATRACE_TAG_HAL, "hal", "hal" },
    { ATRACE_TAG_APP, "app", "app" },
    { ATRACE_TAG_RESOURCES, "res", "resources" },
    { ATRACE_TAG_DALVIK, "dalvik", "dalvik" },
    { ATRACE_TAG_RS, "rs", "rs" },
    { ATRACE_TAG_BIONIC, "bionic", "bionic" },
    { ATRACE_TAG_POWER, "power", "power" },
    { ATRACE_TAG_PACKAGE_MANAGER, "pm", "package_manager" },
    { ATRACE_TAG_SYSTEM_SERVER, "ss", "system_server" },
    { ATRACE_TAG_DATABASE, "db", "database" },
    { ATRACE_TAG_NETWORK, "net", "network" },
    { ATRACE_TAG_ADB, "adb", "adb" },
    { ATRACE_TAG_VIBRATOR, "vibrator", "vibrator" },
    { ATRACE_TAG_AIDL, "aidl", "aidl" },
    { ATRACE_TAG_NNAPI, "nnapi", "nnapi" },
    { ATRACE_TAG_RRO, "rro", "rro" },
    { ATRACE_TAG_THERMAL, "thermal", "thermal" },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * atrace_help
 ****************************************************************************/

static int atrace_help(const char* progname)
{
    printf("Usage: %s [option] [categories...]\n"
           "options include:\n"
           "  --list_categories\n"
           "                  list the available tracing categories\n",
        progname);

    return 0;
}

/****************************************************************************
 * atrace_list
 *
 * Description:
 *   List all available categories
 *
 ****************************************************************************/

static int atrace_list(void)
{
    int i;
    printf("Available categories:\n");
    for (i = 0; i < sizeof(category_list) / sizeof(category_list[0]); i++) {
        printf("\t%s: %s\n", category_list[i].name, category_list[i].help);
    }

    return 0;
}

/****************************************************************************
 * atrace_category
 *
 * Description:
 *  Update tracing tag
 *
 ****************************************************************************/

static int atrace_category(int argc, char** argv)
{
    int i;
    int j;
    uint64_t tag = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "always") == 0) {
            tag = UINT64_MAX;
        } else if (strcmp(argv[i], "never") == 0) {
            tag = 0ull;
        } else {
            for (j = 2; j < sizeof(category_list) / sizeof(category_list[0]); j++) {
                if (strcmp(argv[i], category_list[j].name) == 0) {
                    tag |= category_list[j].tag;
                    break;
                }
            }
        }

        if (j == sizeof(category_list) / sizeof(category_list[0])) {
            printf("Unknown category: %s", argv[i]);
            return -EINVAL;
        }
    }

    atrace_enabled_tags = tag;
    printf("Android tracing enabled with tag: %#" PRIx64 "\n", tag);
    return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   This is the main entry point for the atrace command.
 *
 ****************************************************************************/

int main(int argc, char** argv)
{
    int ret;
    if (argc < 2) {
        atrace_help(argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "--list_categories") == 0) {
        ret = atrace_list();
    } else {
        ret = atrace_category(argc, argv);
    }

    return ret;
}
