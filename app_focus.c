/****************************************************************************
 * frameworks/utils/app_focus.c
 *
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

#include <debug.h>
#include <errno.h>
#include <nuttx/list.h>
#include <pthread.h>
#include <stdlib.h>
#include <syslog.h>

#include "app_focus.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

// struct for single focus node

typedef struct app_focus_node {
    struct list_node node;
    struct app_focus_id focus_id;
} app_focus_node;

// struct for app focus stack

typedef struct app_focus_stack {
    struct list_node head_node;
    int max_size;
    int cur_size;
    struct app_focus_node* node_list;
    app_focus_change_callback focus_change_callback;
} app_focus_stack;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

// if such focus stack is full or not.
static int app_focus_stack_is_full(app_focus_stack* s)
{
    return s->cur_size == s->max_size;
}

// if app focus stack is empty or not
static int app_focus_stack_is_empty(app_focus_stack* s)
{
    return s->cur_size == 0;
}

// get current app focus stack size
static int app_focus_stack_size(app_focus_stack* s)
{
    return s->cur_size;
}

// listener function for dealing with focus state change
static void app_focus_stack_change_callback(app_focus_stack* s,
    struct app_focus_id* cur_focus_id,
    struct app_focus_id* request_app_focus_id, int callback_flag)
{
    s->focus_change_callback(cur_focus_id, request_app_focus_id, callback_flag);
}

// reset node info in fixed node list
static int app_focus_node_list_remove(app_focus_stack* s,
    int input_client_id)
{
    if ((s->node_list + input_client_id)->focus_id.client_id != -1) {
        (s->node_list + input_client_id)->focus_id.client_id = -1;
        (s->node_list + input_client_id)->focus_id.focus_level = 0;
        (s->node_list + input_client_id)->focus_id.thread_id = 0;
        (s->node_list + input_client_id)->focus_id.focus_state = APP_FOCUS_STATE_STACK_QUIT;
        (s->node_list + input_client_id)->focus_id.focus_callback = NULL;
        (s->node_list + input_client_id)->focus_id.callback_argv = NULL;
        return 0;
    }
    return -ENOENT;
}

// check if app focus id's thread is alive or not
static int app_focus_stack_thread_is_alive(app_focus_id* value)
{
    pthread_t thread = (pthread_t)value->thread_id;
    if (pthread_kill(thread, 0) == 0) {
        return 0;
    } else {
        return -ESRCH;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int app_focus_stack_top(void* x, app_focus_id* value)
{
    app_focus_stack* s = (app_focus_stack*)x;
    if (app_focus_stack_is_empty(s)) {
        return -ENOENT;
    }
    *value = list_next_type(&s->head_node,
        &s->head_node,
        struct app_focus_node, node)
                 ->focus_id;
    return 0;
}

int app_focus_stack_pop(void* x, app_focus_id* value, int callback_flag)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_head_node;
    struct app_focus_node* p_tmp_node;

    if (app_focus_stack_is_empty(s)) {
        return -ENOENT;
    }

    p_head_node = list_next_type(&s->head_node,
        &s->head_node,
        struct app_focus_node,
        node);
    p_head_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_QUIT;
    *value = p_head_node->focus_id;

    if (s->cur_size == 1) {
        list_delete(&p_head_node->node);
    } else {
        p_tmp_node = list_next_type(&p_head_node->node,
            &p_head_node->node,
            struct app_focus_node,
            node);
        p_tmp_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_TOP;

        // listener: move focus request from stack under to top
        app_focus_stack_change_callback(s, value,
            &p_tmp_node->focus_id, callback_flag);
        list_delete(&p_head_node->node);
    }
    s->cur_size -= 1;
    app_focus_node_list_remove(s, p_head_node->focus_id.client_id);
    return 0;
}

int app_focus_stack_push(void* x, app_focus_id* value, int callback_flag)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_new_node;
    int node_list_location = -1;

    if (app_focus_stack_is_full(s)) {
        return -ENOSPC;
    }

    // copy client id of value to the node list in valid location
    node_list_location = value->client_id;

    // if specific location can not found
    if (node_list_location == -1) {
        return -EINVAL;
    }

    // get app focus node location in node list
    p_new_node = (s->node_list + node_list_location);
    // apply value of pointer to node list
    p_new_node->focus_id = *value;
    list_initialize(&p_new_node->node);

    // casue the push operation already been checked outside, it should
    // be success with no doubt in different condition

    if (!app_focus_stack_is_empty(s)) {
        struct app_focus_node* p_head_node;
        p_head_node = list_next_type(&s->head_node,
            &s->head_node,
            struct app_focus_node,
            node);
        p_head_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_UNDER;

        // listener: move focus request from top to under
        app_focus_stack_change_callback(s, value,
            &p_head_node->focus_id, callback_flag);
    }
    list_add_head(&s->head_node, &p_new_node->node);
    s->cur_size += 1;
    p_new_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_TOP;
    return 0;
}

int app_focus_stack_insert(void* x, app_focus_id* value, int index)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_new_node;
    struct app_focus_node* p_tmp_node;
    int node_list_location = -1;

    if (app_focus_stack_is_full(s)) {
        return -ENOSPC;
    }

    if (app_focus_stack_is_empty(s)) {
        return app_focus_stack_push(s, value, index);
    } else {
        node_list_location = value->client_id;

        if (node_list_location == -1) {
            return -EINVAL;
        }
        p_new_node = (s->node_list + node_list_location);
        p_new_node->focus_id = *value;
        list_initialize(&p_new_node->node);

        int count = 0;
        list_for_every_entry(&s->head_node,
            p_tmp_node,
            struct app_focus_node,
            node)
        {
            if (count == index - 1) {
                p_new_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_UNDER;
                list_add_head(&p_tmp_node->node, &p_new_node->node);
                s->cur_size += 1;
                return 0;
            }
            count += 1;
        }
        app_focus_node_list_remove(s, node_list_location);
        return -ENOENT;
    }
}

int app_focus_stack_delete(void* x, app_focus_id* value, int callback_flag)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_tmp_node;
    struct app_focus_node* p_next_focus_node;

    if (app_focus_stack_is_empty(s)) {
        return -ENOENT;
    }
    p_tmp_node = list_next_type(&s->head_node, &s->head_node, struct app_focus_node, node);

    //  node need to be deleted at stack top
    if (p_tmp_node->focus_id.client_id == value->client_id) {
        return app_focus_stack_pop(s, value, callback_flag);
    }

    // node need to be deleted at stack under
    list_for_every_entry_safe(&s->head_node,
        p_tmp_node,
        p_next_focus_node,
        struct app_focus_node,
        node)
    {
        if (p_tmp_node->focus_id.client_id == value->client_id) {
            p_tmp_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_QUIT;
            list_delete(&p_tmp_node->node);
            app_focus_node_list_remove(s, p_tmp_node->focus_id.client_id);
            s->cur_size -= 1;
            return 0;
        }
    }
    return -ENOENT;
}

void app_focus_stack_useless_clear(void* x, int callback_flag)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_tmp_node;
    struct app_focus_node* tmp_next_focus_node;

    // interates app focus stack, delete every focus node one by one
    if (!app_focus_stack_is_empty(s)) {
        list_for_every_entry_safe(&s->head_node,
            p_tmp_node,
            tmp_next_focus_node,
            struct app_focus_node,
            node)
        {
            if (app_focus_stack_thread_is_alive(&p_tmp_node->focus_id) != 0) {

                // kick off useless focus id from the stack
                list_delete(&p_tmp_node->node);
                if (p_tmp_node->focus_id.focus_state == APP_FOCUS_STATE_STACK_TOP) {

                    // if only one stack exist, make top to null
                    if (app_focus_stack_size(s) == 1) {
                        list_initialize(&s->head_node);
                    } else {
                        tmp_next_focus_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_TOP;
                        if (app_focus_stack_thread_is_alive(&tmp_next_focus_node->focus_id) == 0) {

                            // listener: move focus request from stack under to top
                            app_focus_stack_change_callback(s,
                                &p_tmp_node->focus_id,
                                &tmp_next_focus_node->focus_id,
                                callback_flag);
                        }
                    }
                }
                p_tmp_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_QUIT;
                app_focus_node_list_remove(s, p_tmp_node->focus_id.client_id);
                s->cur_size -= 1;
            }
        }
    }
}

int app_focus_stack_top_change_broadcast(void* x, int callback_flag)
{
    app_focus_stack* s = (app_focus_stack*)x;
    if (app_focus_stack_size(s) < 2) {
        return -EPERM;
    }

    struct app_focus_node* p_tmp_node;
    struct app_focus_node* p_head_node;
    p_head_node = list_next_type(&s->head_node, &s->head_node, struct app_focus_node, node);

    list_for_every_entry(&s->head_node,
        p_tmp_node,
        struct app_focus_node,
        node)
    {
        if (p_tmp_node->focus_id.focus_state == APP_FOCUS_STATE_STACK_TOP) {
            continue;
        } else {
            // listener: notify all focus under with focus top information
            app_focus_stack_change_callback(s, &p_head_node->focus_id,
                &p_tmp_node->focus_id, callback_flag);
        }
    }
    return 0;
}

void app_focus_stack_clean(void* x, app_focus_id* value, int callback_flag)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_tmp_node;
    struct app_focus_node* p_next_focus_node;

    // interates app focus stack, delete every focus node one by one
    list_for_every_entry_safe(&s->head_node,
        p_tmp_node,
        p_next_focus_node,
        struct app_focus_node,
        node)
    {
        p_tmp_node->focus_id.focus_state = APP_FOCUS_STATE_STACK_QUIT;
        list_delete(&p_tmp_node->node);

        // listener: move focus request from top to quit
        app_focus_stack_change_callback(s, value, &p_tmp_node->focus_id,
            callback_flag);
        app_focus_node_list_remove(s, p_tmp_node->focus_id.client_id);
        s->cur_size -= 1;
    }
}

int app_focus_stack_search_client_id(void* x, app_focus_id* value)
{
    app_focus_stack* s = (app_focus_stack*)x;
    if (app_focus_stack_is_empty(s)) {
        return -ENOENT;
    } else {
        struct app_focus_node* p_tmp_node;
        int count = 0;
        list_for_every_entry(&s->head_node,
            p_tmp_node,
            struct app_focus_node,
            node)
        {
            if (p_tmp_node->focus_id.client_id == value->client_id) {
                return count;
            }
            count += 1;
        }
    }
    return -ENOENT;
}

int app_focus_stack_search_focus_level(void* x, app_focus_id* value)
{
    app_focus_stack* s = (app_focus_stack*)x;
    if (app_focus_stack_is_empty(s)) {
        return -ENOENT;
    } else {
        struct app_focus_node* p_tmp_node;
        int count = 0;
        list_for_every_entry(&s->head_node,
            p_tmp_node,
            struct app_focus_node,
            node)
        {
            if (p_tmp_node->focus_id.focus_level == value->focus_level) {
                return count;
            }
            count += 1;
        }
    }
    return -ENOENT;
}

int app_focus_stack_get_index(void* x, app_focus_id* value, int index)
{
    int count = 0;
    struct app_focus_node* p_tmp_node;
    app_focus_stack* s = (app_focus_stack*)x;

    list_for_every_entry(&s->head_node,
        p_tmp_node,
        struct app_focus_node,
        node)
    {
        if (count == index) {
            *value = p_tmp_node->focus_id;
            return 0;
        }
        count += 1;
    }
    return -ENOENT;
}

void app_focus_stack_display(void* x)
{
    app_focus_stack* s = (app_focus_stack*)x;
    struct app_focus_node* p_tmp_node;

    syslog(LOG_INFO, "current size of stack: %d\n", s->cur_size);
    list_for_every_entry(&s->head_node,
        p_tmp_node,
        struct app_focus_node,
        node)
    {
        syslog(LOG_INFO, "Request client id: %d, "
                         "focus level: %d, "
                         "thread id: %d, "
                         "focus state: %d, "
                         "focus callback: %p, "
                         "callback arg: %p\n",
            p_tmp_node->focus_id.client_id,
            p_tmp_node->focus_id.focus_level,
            p_tmp_node->focus_id.thread_id,
            p_tmp_node->focus_id.focus_state,
            p_tmp_node->focus_id.focus_callback,
            p_tmp_node->focus_id.callback_argv);
    }
}

void* app_focus_stack_init(size_t focus_stack_size,
    app_focus_change_callback focus_callback_method)
{
    // pointer to app focus stack setting
    app_focus_stack* s = malloc(sizeof(app_focus_stack));
    if (!s) {
        auderr("No such mem for focus stack\n");
        return NULL;
    }

    s->node_list = calloc(focus_stack_size, sizeof(app_focus_node));
    if (!s->node_list) {
        auderr("No such mem for stack node list\n");
        free(s);
        return NULL;
    }

    for (int i = 0; i < focus_stack_size; i++) {
        app_focus_node tmp_node = {
            LIST_INITIAL_VALUE(s->node_list->node),
            {
                .client_id = -1,
                .focus_level = 0,
                .thread_id = 0,
                .focus_state = APP_FOCUS_STATE_STACK_QUIT,
                .focus_callback = NULL,
                .callback_argv = NULL,
            }
        };
        *(s->node_list + i) = tmp_node;
    }

    list_initialize(&s->head_node);
    s->cur_size = 0;
    s->max_size = focus_stack_size;
    s->focus_change_callback = focus_callback_method;

    return s;
}

void app_focus_stack_destory(void* x)
{
    app_focus_stack* s = (app_focus_stack*)x;
    if (s != NULL) {
        free(s->node_list);
        free(s);
    }
}

int app_focus_free_client_id(void* x)
{
    app_focus_stack* s = (app_focus_stack*)x;
    if (app_focus_stack_is_full(s)) {
        return -ENODATA;
    }
    for (int i = 0; i < s->max_size; i++) {
        if ((s->node_list + i)->focus_id.client_id == -1) {
            return i;
        } else {
            continue;
        }
    }
    return -ENODATA;
}

// return current app focus stack for checking
int app_focus_stack_return(void* x, app_focus_id* focus_id_list, int num)
{
    int size_count = 0;
    struct app_focus_node* p_tmp_node;
    app_focus_stack* s = (app_focus_stack*)x;

    list_for_every_entry(&s->head_node,
        p_tmp_node,
        struct app_focus_node,
        node)
    {
        if (size_count < s->cur_size && size_count < num) {
            *(focus_id_list + size_count) = p_tmp_node->focus_id;
        }
        size_count += 1;
    }
    return num - size_count;
}
