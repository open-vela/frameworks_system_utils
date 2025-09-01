/*
 * Copyright (C) 2025 Xiaomi Corporation
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
#include <stdlib.h>

#include <dbus/dbus.h>

#include "gdbus-internal.h"

#define POLICY_KIT_DBUS_NAME "org.freedesktop.PolicyKit1"
#define POLICY_KIT_INTERFACE "org.freedesktop.PolicyKit1.Authority"
#define POLICY_KIT_PATH "/org/freedesktop/PolicyKit1/Authority"
#define POLICY_KIT_ACTION "org.freedesktop.policykit.exec"

typedef enum polkit_interaction_flag {
    POLKIT_FLAG_NONE = 0x00000000,
    POLKIT_FLAG_ALLOW = 0x00000001
} polkit_interaction_flag;

typedef struct authorization_context {
    void (*callback)(dbus_bool_t authorized, void* user_data);
    void* user_data;
} authorization_context;

typedef struct dict_entry_builder {
    DBusMessageIter iter;
    const char* key;
    const char* value;
} dict_entry_builder;

typedef enum contained_sig_type {
    SIG_TYPE_EMPTY,
    SIG_TYPE_VARIANT,
} contained_sig_type;

static const char* init_contained_signature_type(contained_sig_type type)
{
    switch (type) {
    case SIG_TYPE_EMPTY:
        return DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_STRING_AS_STRING
                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING;
    case SIG_TYPE_VARIANT:
        return DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_VARIANT_AS_STRING
                    DBUS_DICT_ENTRY_END_CHAR_AS_STRING;
    default:
        return NULL;
    }
}

static void build_dict_entry(dict_entry_builder* builder)
{
    DBusMessageIter dict, entry, variant;

    dbus_message_iter_open_container(&builder->iter, DBUS_TYPE_ARRAY,
        init_contained_signature_type(SIG_TYPE_VARIANT), &dict);

    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &builder->key);

    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
        DBUS_TYPE_STRING_AS_STRING, &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &builder->value);

    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&builder->iter, &dict);
}

static void build_empty_dict(DBusMessageIter* iter)
{
    DBusMessageIter dict;
    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
        init_contained_signature_type(SIG_TYPE_EMPTY), &dict);
    dbus_message_iter_close_container(iter, &dict);
}

static void build_authorization_arguments(DBusConnection* conn, DBusMessageIter* iter,
    const char* action, polkit_interaction_flag flags)
{
    const char* bus_name = dbus_bus_get_unique_name(conn);
    const char* subject_kind = "system-bus-name";
    const char* cancellation_id = "";
    DBusMessageIter subject;

    // build main subject
    dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &subject);
    dbus_message_iter_append_basic(&subject, DBUS_TYPE_STRING, &subject_kind);

    dict_entry_builder builder = {
        .iter = subject,
        .key = "name",
        .value = bus_name
    };
    build_dict_entry(&builder);
    dbus_message_iter_close_container(iter, &subject);

    // add less parameters
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING,
        action ? action : POLICY_KIT_ACTION);
    build_empty_dict(iter);
    dbus_message_iter_append_basic(iter, DBUS_TYPE_UINT32, &flags);
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &cancellation_id);
}

static dbus_bool_t parse_authorization_result(DBusMessageIter* iter)
{
    DBusMessageIter recurse_iter;
    dbus_bool_t auth = FALSE;

    if (!iter)
        return FALSE;

    dbus_message_iter_recurse(iter, &recurse_iter);
    dbus_message_iter_get_basic(&recurse_iter, &auth);

    return auth;
}

static void handle_authorization_reply(DBusPendingCall* call, void* user_data)
{
    authorization_context* context = user_data;
    DBusMessage* reply = NULL;
    DBusMessageIter iter;
    dbus_bool_t authorized = FALSE;

    if (!call || !context)
        goto cleanup;

    reply = dbus_pending_call_steal_reply(call);
    if (!reply)
        goto cleanup;

    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        error("Authorization error: %s\n", dbus_message_get_error_name(reply));
        goto cleanup;
    }

    if (!dbus_message_has_signature(reply, "(bba{ss})")) {
        error("Invalid reply signature\n");
        goto cleanup;
    }

    if (!dbus_message_iter_init(reply, &iter))
        goto cleanup;

    authorized = parse_authorization_result(&iter);

cleanup:
    if (context != NULL && context->callback) {
        context->callback(authorized, context->user_data);
    }

    if (reply)
        dbus_message_unref(reply);

    dbus_pending_call_unref(call);
}

int dbus_polkit_check_authorization(DBusConnection* conn,
    const char* action, gboolean allow_interaction,
    void (*callback)(dbus_bool_t, void*),
    void* user_data, int timeout_ms)
{
    DBusMessage* msg;
    DBusMessageIter iter;
    DBusPendingCall* pending_call = NULL;
    polkit_interaction_flag flags;

    if (!conn)
        return -EINVAL;

    authorization_context* context = calloc(1, sizeof(authorization_context));
    if (!context)
        return -ENOMEM;

    msg = dbus_message_new_method_call(POLICY_KIT_DBUS_NAME, POLICY_KIT_PATH,
        POLICY_KIT_INTERFACE, "CheckAuthorization");
    if (!msg) {
        free(context);
        return -ENOMEM;
    }

    flags = allow_interaction ? POLKIT_FLAG_ALLOW : POLKIT_FLAG_NONE;

    dbus_message_iter_init_append(msg, &iter);
    build_authorization_arguments(conn, &iter, action, flags);

    if (!dbus_connection_send_with_reply(conn, msg, &pending_call, timeout_ms)) {
        dbus_message_unref(msg);
        free(context);
        return -EIO;
    }

    if (!pending_call) {
        dbus_message_unref(msg);
        free(context);
        return -EIO;
    }

    context->callback = callback;
    context->user_data = user_data;

    dbus_pending_call_set_notify(pending_call, handle_authorization_reply,
        context, free);
    dbus_message_unref(msg);

    return 0;
}
