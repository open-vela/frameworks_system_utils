/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  D-Bus helper library
 *
 *  Copyright (C) 2004-2011  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 */

#ifndef __GDBUS_H
#define __GDBUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <dbus/dbus.h>

typedef void* gpointer;
typedef int gboolean;
typedef unsigned int guint;
typedef uint32_t guint32;

#define g_dbus_setup_bus dbus_setup_bus
#define g_dbus_setup_private dbus_setup_private
#define g_dbus_request_name dbus_request_name
#define g_dbus_set_disconnect_function dbus_set_disconnect_function
#define g_dbus_set_flags dbus_set_flags
#define g_dbus_get_flags dbus_get_flags
#define g_dbus_register_interface dbus_register_interface
#define g_dbus_unregister_interface dbus_unregister_interface
#define g_dbus_register_security dbus_register_security
#define g_dbus_unregister_security dbus_unregister_security
#define g_dbus_pending_success dbus_pending_success
#define g_dbus_pending_error dbus_pending_error
#define g_dbus_pending_error_valist dbus_pending_error_valist
#define g_dbus_create_error dbus_create_error
#define g_dbus_create_error_valist dbus_create_error_valist
#define g_dbus_create_reply dbus_create_reply
#define g_dbus_create_reply_valist dbus_create_reply_valist
#define g_dbus_send_message dbus_send_message
#define g_dbus_send_message_with_reply dbus_send_message_with_reply
#define g_dbus_send_error dbus_send_error
#define g_dbus_send_error_valist dbus_send_error_valist
#define g_dbus_send_reply dbus_send_reply
#define g_dbus_send_reply_valist dbus_send_reply_valist
#define g_dbus_emit_signal dbus_emit_signal
#define g_dbus_emit_signal_valist dbus_emit_signal_valist
#define g_dbus_add_service_watch dbus_add_service_watch
#define g_dbus_add_disconnect_watch dbus_add_disconnect_watch
#define g_dbus_add_signal_watch dbus_add_signal_watch
#define g_dbus_add_properties_watch dbus_add_properties_watch
#define g_dbus_remove_watch dbus_remove_watch
#define g_dbus_remove_all_watches dbus_remove_all_watches
#define g_dbus_pending_property_success dbus_pending_property_success
#define g_dbus_pending_property_error_valist dbus_pending_property_error_valist
#define g_dbus_pending_property_error dbus_pending_property_error
#define g_dbus_emit_property_changed dbus_emit_property_changed
#define g_dbus_emit_property_changed_full dbus_emit_property_changed_full
#define g_dbus_get_properties dbus_get_properties
#define g_dbus_attach_object_manager dbus_attach_object_manager
#define g_dbus_detach_object_manager dbus_detach_object_manager
#define g_dbus_proxy_new dbus_proxy_new
#define g_dbus_proxy_ref dbus_proxy_ref
#define g_dbus_proxy_unref dbus_proxy_unref
#define g_dbus_proxy_get_path dbus_proxy_get_path
#define g_dbus_proxy_get_interface dbus_proxy_get_interface
#define g_dbus_proxy_get_property dbus_proxy_get_property
#define g_dbus_proxy_lookup dbus_proxy_lookup
#define g_dbus_proxy_path_lookup dbus_proxy_path_lookup
#define g_dbus_proxy_refresh_property dbus_proxy_refresh_property
#define g_dbus_proxy_set_property_basic dbus_proxy_set_property_basic
#define g_dbus_proxy_set_property_array dbus_proxy_set_property_array
#define g_dbus_dict_append_entry dbus_dict_append_entry
#define g_dbus_dict_append_basic_array dbus_dict_append_basic_array
#define g_dbus_dict_append_array dbus_dict_append_array
#define g_dbus_proxy_method_call dbus_proxy_method_call
#define g_dbus_proxy_set_property_watch dbus_proxy_set_property_watch
#define g_dbus_proxy_remove_property_watch dbus_proxy_remove_property_watch
#define g_dbus_proxy_set_removed_watch dbus_proxy_set_removed_watch
#define g_dbus_client_new dbus_client_new
#define g_dbus_client_new_full dbus_client_new_full
#define g_dbus_client_ref dbus_client_ref
#define g_dbus_client_unref dbus_client_unref
#define g_dbus_client_set_connect_watch dbus_client_set_connect_watch
#define g_dbus_client_set_disconnect_watch dbus_client_set_disconnect_watch
#define g_dbus_client_set_signal_watch dbus_client_set_signal_watch
#define g_dbus_client_set_ready_watch dbus_client_set_ready_watch
#define g_dbus_client_set_proxy_handlers dbus_client_set_proxy_handlers
#define g_dbus_client_set_proxy_filter dbus_client_set_proxy_filter

typedef struct GDBusArgInfo GDBusArgInfo;
typedef struct GDBusMethodTable GDBusMethodTable;
typedef struct GDBusSignalTable GDBusSignalTable;
typedef struct GDBusPropertyTable GDBusPropertyTable;
typedef struct GDBusSecurityTable GDBusSecurityTable;

typedef void (*GDBusWatchFunction)(DBusConnection* connection,
    void* user_data);

typedef void (*GDBusMessageFunction)(DBusConnection* connection,
    DBusMessage* message, void* user_data);

typedef gboolean (*GDBusSignalFunction)(DBusConnection* connection,
    DBusMessage* message, void* user_data);

DBusConnection* dbus_setup_bus(DBusBusType type, const char* name,
    DBusError* error);

DBusConnection* dbus_setup_private(DBusBusType type, const char* name,
    DBusError* error);

gboolean dbus_request_name(DBusConnection* connection, const char* name,
    DBusError* error);

gboolean dbus_set_disconnect_function(DBusConnection* connection,
    GDBusWatchFunction function,
    void* user_data, DBusFreeFunction destroy);

typedef void (*GDBusDestroyFunction)(void* user_data);

typedef DBusMessage* (*GDBusMethodFunction)(DBusConnection* connection,
    DBusMessage* message, void* user_data);

typedef gboolean (*GDBusPropertyGetter)(const GDBusPropertyTable* property,
    DBusMessageIter* iter, void* data);

typedef guint32 GDBusPendingPropertySet;

typedef void (*GDBusPropertySetter)(const GDBusPropertyTable* property,
    DBusMessageIter* value, GDBusPendingPropertySet id,
    void* data);

typedef gboolean (*GDBusPropertyExists)(const GDBusPropertyTable* property,
    void* data);

typedef guint32 GDBusPendingReply;

typedef void (*GDBusSecurityFunction)(DBusConnection* connection,
    const char* action,
    gboolean interaction,
    GDBusPendingReply pending);

enum GDBusFlags {
    G_DBUS_FLAG_ENABLE_EXPERIMENTAL = (1 << 0),
};

enum GDBusMethodFlags {
    G_DBUS_METHOD_FLAG_DEPRECATED = (1 << 0),
    G_DBUS_METHOD_FLAG_NOREPLY = (1 << 1),
    G_DBUS_METHOD_FLAG_ASYNC = (1 << 2),
    G_DBUS_METHOD_FLAG_EXPERIMENTAL = (1 << 3),
};

enum GDBusSignalFlags {
    G_DBUS_SIGNAL_FLAG_DEPRECATED = (1 << 0),
    G_DBUS_SIGNAL_FLAG_EXPERIMENTAL = (1 << 1),
};

enum GDBusPropertyFlags {
    G_DBUS_PROPERTY_FLAG_DEPRECATED = (1 << 0),
    G_DBUS_PROPERTY_FLAG_EXPERIMENTAL = (1 << 1),
};

enum GDBusSecurityFlags {
    G_DBUS_SECURITY_FLAG_DEPRECATED = (1 << 0),
    G_DBUS_SECURITY_FLAG_BUILTIN = (1 << 1),
    G_DBUS_SECURITY_FLAG_ALLOW_INTERACTION = (1 << 2),
};

enum GDbusPropertyChangedFlags {
    G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH = (1 << 0),
};

typedef enum GDBusMethodFlags GDBusMethodFlags;
typedef enum GDBusSignalFlags GDBusSignalFlags;
typedef enum GDBusPropertyFlags GDBusPropertyFlags;
typedef enum GDBusSecurityFlags GDBusSecurityFlags;
typedef enum GDbusPropertyChangedFlags GDbusPropertyChangedFlags;

struct GDBusArgInfo {
    const char* name;
    const char* signature;
};

struct GDBusMethodTable {
    const char* name;
    GDBusMethodFunction function;
    GDBusMethodFlags flags;
    unsigned int privilege;
    const GDBusArgInfo* in_args;
    const GDBusArgInfo* out_args;
};

struct GDBusSignalTable {
    const char* name;
    GDBusSignalFlags flags;
    const GDBusArgInfo* args;
};

struct GDBusPropertyTable {
    const char* name;
    const char* type;
    GDBusPropertyGetter get;
    GDBusPropertySetter set;
    GDBusPropertyExists exists;
    GDBusPropertyFlags flags;
};

struct GDBusSecurityTable {
    unsigned int privilege;
    const char* action;
    GDBusSecurityFlags flags;
    GDBusSecurityFunction function;
};

#define GDBUS_ARGS(args...) \
    (const GDBusArgInfo[])  \
    {                       \
        args, { }           \
    }

#define GDBUS_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                          \
    .in_args = _in_args,                                    \
    .out_args = _out_args,                                  \
    .function = _function

#define GDBUS_ASYNC_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                                \
    .in_args = _in_args,                                          \
    .out_args = _out_args,                                        \
    .function = _function,                                        \
    .flags = G_DBUS_METHOD_FLAG_ASYNC

#define GDBUS_DEPRECATED_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                                     \
    .in_args = _in_args,                                               \
    .out_args = _out_args,                                             \
    .function = _function,                                             \
    .flags = G_DBUS_METHOD_FLAG_DEPRECATED

#define GDBUS_DEPRECATED_ASYNC_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                                           \
    .in_args = _in_args,                                                     \
    .out_args = _out_args,                                                   \
    .function = _function,                                                   \
    .flags = G_DBUS_METHOD_FLAG_ASYNC | G_DBUS_METHOD_FLAG_DEPRECATED

#define GDBUS_EXPERIMENTAL_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                                       \
    .in_args = _in_args,                                                 \
    .out_args = _out_args,                                               \
    .function = _function,                                               \
    .flags = G_DBUS_METHOD_FLAG_EXPERIMENTAL

#define GDBUS_EXPERIMENTAL_ASYNC_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                                             \
    .in_args = _in_args,                                                       \
    .out_args = _out_args,                                                     \
    .function = _function,                                                     \
    .flags = G_DBUS_METHOD_FLAG_ASYNC | G_DBUS_METHOD_FLAG_EXPERIMENTAL

#define GDBUS_NOREPLY_METHOD(_name, _in_args, _out_args, _function) \
    .name = _name,                                                  \
    .in_args = _in_args,                                            \
    .out_args = _out_args,                                          \
    .function = _function,                                          \
    .flags = G_DBUS_METHOD_FLAG_NOREPLY

#define GDBUS_SIGNAL(_name, _args) \
    .name = _name,                 \
    .args = _args

#define GDBUS_DEPRECATED_SIGNAL(_name, _args) \
    .name = _name,                            \
    .args = _args,                            \
    .flags = G_DBUS_SIGNAL_FLAG_DEPRECATED

#define GDBUS_EXPERIMENTAL_SIGNAL(_name, _args) \
    .name = _name,                              \
    .args = _args,                              \
    .flags = G_DBUS_SIGNAL_FLAG_EXPERIMENTAL

void dbus_set_flags(int flags);
int dbus_get_flags(void);

gboolean dbus_register_interface(DBusConnection* connection,
    const char* path, const char* name,
    const GDBusMethodTable* methods,
    const GDBusSignalTable* signals,
    const GDBusPropertyTable* properties,
    void* user_data,
    GDBusDestroyFunction destroy);
gboolean dbus_unregister_interface(DBusConnection* connection,
    const char* path, const char* name);

gboolean dbus_register_security(const GDBusSecurityTable* security);
gboolean dbus_unregister_security(const GDBusSecurityTable* security);

void dbus_pending_success(DBusConnection* connection,
    GDBusPendingReply pending);
void dbus_pending_error(DBusConnection* connection,
    GDBusPendingReply pending,
    const char* name, const char* format, ...)
    __attribute__((format(printf, 4, 5)));
void dbus_pending_error_valist(DBusConnection* connection,
    GDBusPendingReply pending, const char* name,
    const char* format, va_list args);

DBusMessage* dbus_create_error(DBusMessage* message, const char* name,
    const char* format, ...)
    __attribute__((format(printf, 3, 4)));
DBusMessage* dbus_create_error_valist(DBusMessage* message, const char* name,
    const char* format, va_list args);
DBusMessage* dbus_create_reply(DBusMessage* message, int type, ...);
DBusMessage* dbus_create_reply_valist(DBusMessage* message,
    int type, va_list args);

gboolean dbus_send_message(DBusConnection* connection, DBusMessage* message);
gboolean dbus_send_message_with_reply(DBusConnection* connection,
    DBusMessage* message,
    DBusPendingCall** call, int timeout);
gboolean dbus_send_error(DBusConnection* connection, DBusMessage* message,
    const char* name, const char* format, ...)
    __attribute__((format(printf, 4, 5)));
gboolean dbus_send_error_valist(DBusConnection* connection,
    DBusMessage* message, const char* name,
    const char* format, va_list args);
gboolean dbus_send_reply(DBusConnection* connection,
    DBusMessage* message, int type, ...);
gboolean dbus_send_reply_valist(DBusConnection* connection,
    DBusMessage* message, int type, va_list args);

gboolean dbus_emit_signal(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name, int type, ...);
gboolean dbus_emit_signal_valist(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name, int type, va_list args);

guint dbus_add_service_watch(DBusConnection* connection, const char* name,
    GDBusWatchFunction connect,
    GDBusWatchFunction disconnect,
    void* user_data, GDBusDestroyFunction destroy);
guint dbus_add_disconnect_watch(DBusConnection* connection, const char* name,
    GDBusWatchFunction function,
    void* user_data, GDBusDestroyFunction destroy);
guint dbus_add_signal_watch(DBusConnection* connection,
    const char* sender, const char* path,
    const char* interface, const char* member,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy);
guint dbus_add_properties_watch(DBusConnection* connection,
    const char* sender, const char* path,
    const char* interface,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy);
gboolean dbus_remove_watch(DBusConnection* connection, guint tag);
void dbus_remove_all_watches(DBusConnection* connection);

void dbus_pending_property_success(GDBusPendingPropertySet id);
void dbus_pending_property_error_valist(GDBusPendingReply id,
    const char* name, const char* format, va_list args);
void dbus_pending_property_error(GDBusPendingReply id, const char* name,
    const char* format, ...);

/*
 * Note that when multiple properties for a given object path are changed
 * in the same mainloop iteration, they will be grouped with the last
 * property changed. If this behaviour is undesired, use
 * dbus_emit_property_changed_full() with the
 * G_DBUS_PROPERTY_CHANGED_FLAG_FLUSH flag, causing the signal to ignore
 * any grouping.
 */
void dbus_emit_property_changed(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name);
void dbus_emit_property_changed_full(DBusConnection* connection,
    const char* path, const char* interface,
    const char* name,
    GDbusPropertyChangedFlags flags);
gboolean dbus_get_properties(DBusConnection* connection, const char* path,
    const char* interface, DBusMessageIter* iter);

gboolean dbus_attach_object_manager(DBusConnection* connection);
gboolean dbus_detach_object_manager(DBusConnection* connection);

typedef struct GDBusClient GDBusClient;
typedef struct GDBusProxy GDBusProxy;

GDBusProxy* dbus_proxy_new(GDBusClient* client, const char* path,
    const char* interface);

GDBusProxy* dbus_proxy_ref(GDBusProxy* proxy);
void dbus_proxy_unref(GDBusProxy* proxy);

const char* dbus_proxy_get_path(const GDBusProxy* proxy);
const char* dbus_proxy_get_interface(GDBusProxy* proxy);

gboolean dbus_proxy_get_property(GDBusProxy* proxy, const char* name,
    DBusMessageIter* iter);

GDBusProxy* dbus_proxy_lookup(void* list, int* index, const char* path,
    const char* interface);
char* dbus_proxy_path_lookup(void* list, int* index, const char* path);

gboolean dbus_proxy_refresh_property(GDBusProxy* proxy, const char* name);

typedef void (*GDBusResultFunction)(const DBusError* error, void* user_data);

gboolean dbus_proxy_set_property_basic(GDBusProxy* proxy,
    const char* name, int type, const void* value,
    GDBusResultFunction function, void* user_data,
    GDBusDestroyFunction destroy);

gboolean dbus_proxy_set_property_array(GDBusProxy* proxy,
    const char* name, int type, const void* value,
    size_t size, GDBusResultFunction function,
    void* user_data, GDBusDestroyFunction destroy);

void dbus_dict_append_entry(DBusMessageIter* dict,
    const char* key, int type, void* val);
void dbus_dict_append_basic_array(DBusMessageIter* dict, int key_type,
    const void* key, int type, void* val,
    int n_elements);
void dbus_dict_append_array(DBusMessageIter* dict,
    const char* key, int type, void* val,
    int n_elements);

typedef void (*GDBusSetupFunction)(DBusMessageIter* iter, void* user_data);
typedef void (*GDBusReturnFunction)(DBusMessage* message, void* user_data);

gboolean dbus_proxy_method_call(GDBusProxy* proxy, const char* method,
    GDBusSetupFunction setup,
    GDBusReturnFunction function, void* user_data,
    GDBusDestroyFunction destroy);

typedef void (*GDBusClientFunction)(GDBusClient* client, void* user_data);
typedef void (*GDBusProxyFunction)(GDBusProxy* proxy, void* user_data);
typedef gboolean (*GDBusProxyPropertyFilterFunction)(GDBusProxy* proxy);
typedef void (*GDBusPropertyFunction)(GDBusProxy* proxy, const char* name,
    DBusMessageIter* iter, void* user_data);
typedef gboolean (*GDBusProxyFilterFunction)(GDBusClient* client, const char* path,
    const char* interface);

gboolean dbus_proxy_set_property_watch(GDBusProxy* proxy,
    GDBusPropertyFunction function, void* user_data);
gboolean dbus_proxy_remove_property_watch(GDBusProxy* proxy,
    GDBusDestroyFunction destroy);

gboolean dbus_proxy_set_removed_watch(GDBusProxy* proxy,
    GDBusProxyFunction destroy, void* user_data);

GDBusClient* dbus_client_new(DBusConnection* connection,
    const char* service, const char* path);
GDBusClient* dbus_client_new_full(DBusConnection* connection,
    const char* service,
    const char* path,
    const char* root_path);

GDBusClient* dbus_client_ref(GDBusClient* client);
void dbus_client_unref(GDBusClient* client);

gboolean dbus_client_set_connect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data);
gboolean dbus_client_set_disconnect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data);
gboolean dbus_client_set_signal_watch(GDBusClient* client,
    GDBusMessageFunction function, void* user_data);
gboolean dbus_client_set_ready_watch(GDBusClient* client,
    GDBusClientFunction ready, void* user_data);
gboolean dbus_client_set_proxy_handlers(GDBusClient* client,
    GDBusProxyFunction proxy_added,
    GDBusProxyFunction proxy_removed,
    GDBusProxyPropertyFilterFunction proxy_property_filter,
    GDBusPropertyFunction property_changed,
    void* user_data);
gboolean dbus_client_set_proxy_filter(GDBusClient* client,
    GDBusProxyFilterFunction proxy_filter,
    void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* __GDBUS_H */
