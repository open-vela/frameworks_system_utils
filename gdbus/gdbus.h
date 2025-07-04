/*
 * Copyright (c) 2024 Xiaomi Technologies Co., Ltd.
 * All rights reserved.
 *
 * This file is part of the Xiaomi project.
 *
 * This source code is licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
#define g_dbus_proxy_get_property_basic dbus_proxy_get_property_basic
#define g_dbus_proxy_get_property_iter_cb dbus_proxy_get_property_iter_cb
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

typedef void (*GDBusPropIterFunction)(DBusMessageIter* iter, void* value);

/**
 * @brief Set and connect to the specified DBus bus type
 *
 * @param type the type of DBus bus, which can be DBUS_BUS_SESSION or DBUS_BUS_SYSTEM
 * @param name The name of the DBus bus to connect to
 * @param error A DBusError structure used to store error information
 *
 * @return If successful, returns a pointer to DBusConnection; if failed, returns NULL
 */
DBusConnection* dbus_setup_bus(DBusBusType type, const char* name, DBusError* error);

/**
 * @brief Set up a private DBus connection
 *
 * This function is used to set up a private DBus connection. It accepts a DBusBusType
 * parameter that specifies the type of connection (session or system), a string parameter
 * that is the name of the DBus you want to connect to, and a DBusError pointer to store
 * any error information that may occur.
 * If the function succeeds, it returns a pointer to a DBusConnection, otherwise it returns NULL.
 *
 * @param type The type of DBus connection, which can be DBUS_BUS_SESSION or DBUS_BUS_SYSTEM
 * @param name The name of the DBus you want to connect to
 * @param error A DBusError structure used to store error information
 * @return Returns a DBusConnection pointer on success, or NULL on failure
 */
DBusConnection* dbus_setup_private(DBusBusType type, const char* name, DBusError* error);

/**
 * @brief Requests a specific D-Bus name on the specified DBus connection.
 *
 * @param connection Pointer to a DBusConnection object representing the connection for
 * which the name is to be requested.
 * @param name String of the D-Bus name to be requested.
 * @param error Pointer to a DBusError object for storing possible errors that may occur
 * during the name request.
 *
 * @return Returns a gboolean value of TRUE if the request succeeds or a gboolean value
 * of FALSE if the request fails.
 */
gboolean dbus_request_name(DBusConnection* connection, const char* name, DBusError* error);

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

/**
 * @brief Register an interface on the specified DBus connection
 *
 * This function is used to register an interface on the specified DBus connection.
 * It requires the following parameters:
 * - connection: DBus connection object
 * - path: path of DBus interface
 * - name: name of DBus interface
 * - methods: a GDBusMethodTable object containing methods of interface
 * - signals: a GDBusSignalTable object containing signals of interface
 * - properties: a GDBusPropertyTable object containing properties of interface
 * - user_data: user data, which can be passed to callback function
 * - destroy: a GDBusDestroyFunction, which is called when the interface is deregistered
 * to clean up user data
 *
 * @param connection DBus connection object
 * @param path path of DBus interface
 * @param name name of DBus interface
 * @param methods GDBusMethodTable object containing interface methods
 * @param signals GDBusSignalTable object containing interface signals
 * @param properties GDBusPropertyTable object containing interface properties
 * @param user_data user data
 * @param destroy GDBusDestroyFunction for cleaning up user data
 * @return gboolean Returns TRUE if registration is successful, and FALSE if fails
 */
gboolean dbus_register_interface(DBusConnection* connection,
    const char* path, const char* name,
    const GDBusMethodTable* methods,
    const GDBusSignalTable* signals,
    const GDBusPropertyTable* properties,
    void* user_data,
    GDBusDestroyFunction destroy);

/**
 * @brief Unregister the specified interface from the specified DBus connection
 *
 * @param connection Pointer to DBusConnection, indicating the DBus connection of the
 * interface to be unregistered
 * @param path Path of the interface
 * @param name Name of the interface
 * @return If the interface is successfully unregistered, return gboolean TRUE, otherwise
 * return FALSE
 */
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

/**
 * @brief Send a message to the specified DBus connection
 *
 * @param connection Pointer to the DBusConnection object, indicating the DBus connection
 * to send the message
 * @param message Pointer to the DBusMessage object, indicating the message to send
 *
 * @return If the sending is successful, return the gboolean type with a value of TRUE;
 * if the sending fails, return the gboolean type with a value of FALSE
 */
gboolean dbus_send_message(DBusConnection* connection, DBusMessage* message);

/**
 * @brief Send a message to the specified DBus connection and wait for a reply,
 * non-blocking call
 *
 * @param connection Pointer to the DBusConnection object, indicating the DBus connection
 * @param message Pointer to the DBusMessage object, indicating the message to be sent
 * @param call Pointer to the pointer to the DBusPendingCall object, used to store the
 * handle of the asynchronous call
 * @param timeout Timeout for waiting for a reply after sending a message, in milliseconds
 *
 * @return If the message is successfully sent and the DBusPendingCall object instance is
 * replied, the return value of the gboolean type is TRUE; otherwise, it returns FALSE
 */
gboolean dbus_send_message_with_reply(DBusConnection* connection, DBusMessage* message,
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

typedef struct GDBusWatch GDBusWatch;

/**
 * Adds a service watch to monitor the specified D-Bus service name
 * 
 * @param watcher The GDBusWatch instance to add the watch to
 * @param name The D-Bus service name to watch (e.g. "org.freedesktop.DBus")
 * @param connect Callback function when service connects
 * @param disconnect Callback function when service disconnects  
 * @param user_data User data passed to callbacks
 * @param destroy Destroy notification callback for user_data
 * @return Watch ID that can be used to remove the watch
 */
guint dbus_add_service_watch(GDBusWatch* watcher, const char* name,
    GDBusWatchFunction connect, GDBusWatchFunction disconnect,
    void* user_data, GDBusDestroyFunction destroy);

/**
 * Adds a disconnect watch for the specified D-Bus name
 *
 * @param watcher The GDBusWatch instance to add the watch to
 * @param name The D-Bus name to monitor for disconnection
 * @param function Callback function when name disconnects
 * @param user_data User data passed to callback  
 * @param destroy Destroy notification callback for user_data
 * @return Watch ID that can be used to remove the watch
 */
guint dbus_add_service_disconnect_watch(GDBusWatch* watcher, const char* name,
    GDBusWatchFunction function, void* user_data, GDBusDestroyFunction destroy);

/**
 * @brief Add a signal monitor to the DBus connection
 *
 * @param connection: Pointer to DBusConnection, indicating the DBus connection to which
 * the signal monitor is to be added
 * @param sender: The name of the DBus client that sends the signal. If it is NULL, the
 * signals of all clients are monitored
 * @param path: The path of the signal. If it is NULL, the signals of all paths are
 * monitored
 * @param interface: The interface of the signal. If it is NULL, the signals of all
 * interfaces are monitored
 * @param member: The member of the signal, that is, the specific name of the signal.
 * If it is NULL, the signals of all members are monitored
 * @param function: The function called when the signal is received receives the
 * following parameters:
 * - DBusConnection*: DBus connection
 * - const char*: The sender of the signal
 * - const char*: The path of the signal
 * - const char*: The interface of the signal
 * - const char*: The member of the signal
 * - void*: User data
 * @param user_data: User data, which will be passed to the function when calling the
 * function
 * @param destroy: The function called when the signal monitor is removed to destroy
 * user_data. If it is NULL, no action is performed.
 * @return: Returns an unsigned integer representing the ID of the newly created signal
 * monitor.
 */
guint dbus_add_signal_watch(GDBusWatch* watcher,
    const char* sender, const char* path,
    const char* interface, const char* member,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy);

guint dbus_add_properties_watch(GDBusWatch* watcher,
    const char* sender, const char* path,
    const char* interface,
    GDBusSignalFunction function, void* user_data,
    GDBusDestroyFunction destroy);

/**
 * @brief Remove a specific watch from the specified DBus connection
 *
 * @param connection Pointer to DBusConnection, indicating the connection from which the
 * watch is to be removed
 * @param tag Indicates the unique identifier of the watch to be removed
 *
 * @return gboolean The function returns TRUE if executed successfully, otherwise returns
 * FALSE
 */
gboolean dbus_remove_watch(GDBusWatch* watcher, guint tag);

void dbus_remove_all_watches(GDBusWatch* watcher);

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

/**
 * @brief Create a new D-Bus proxy object
 *
 * This function is used to create a new D-Bus proxy object.
 *
 * @param client Pointer to the GDBusClient object, which is an instance of the
 * D-Bus client.
 * @param path The path of the D-Bus proxy.
 * @param interface The name of the interface to be implemented by the D-Bus proxy.
 *
 * @return Returns a pointer to the GDBusProxy object if it was created successfully,
 * otherwise returns NULL.
 */
GDBusProxy* dbus_proxy_new(GDBusClient* client, const char* path,
    const char* interface);

/**
 * @brief Increase the reference count of a D-Bus proxy
 *
 * This function increases the reference count of a given D-Bus proxy.
 *
 * @param proxy Pointer to the D-Bus proxy whose reference count is to be increased.
 * @return Returns the pointer to the D-Bus proxy after the reference count is increased.
 */
GDBusProxy* dbus_proxy_ref(GDBusProxy* proxy);

/**
 * @brief Decrement the reference count of a D-Bus proxy
 *
 * This function decrements the reference count of a given D-Bus proxy. If the reference
 * count reaches 0, the proxy will be released.
 *
 * @param proxy Pointer to the D-Bus proxy whose reference count is to be decremented.
 */
void dbus_proxy_unref(GDBusProxy* proxy);

/**
 * @brief Get the path of the D-Bus proxy
 *
 * @param proxy Pointer to GDBusProxy
 * @return Return the path of the proxy
 */
const char* dbus_proxy_get_path(const GDBusProxy* proxy);

/**
 * @brief Get the interface of the D-Bus proxy
 *
 * @param proxy Pointer to GDBusProxy
 * @return Return the interface of the proxy
 */
const char* dbus_proxy_get_interface(GDBusProxy* proxy);

/**
 * @brief Gets a property value from a D-Bus proxy object, should run
 * in uv default loop.
 *
 * @param proxy The D-Bus proxy object to query
 * @param name The name of the property to get
 * @param iter Pointer to a DBusMessageIter to store the property value
 * @return gboolean TRUE if successful, FALSE otherwise
 *
 * This function synchronously gets a property value from a D-Bus proxy object
 * and stores it in the provided DBusMessageIter. The caller is responsible
 * for properly handling the DBusMessageIter contents.
 */
gboolean dbus_proxy_get_property(GDBusProxy* proxy, const char* name,
    DBusMessageIter* iter);

/**
 * @brief Gets a basic property value from a D-Bus proxy object (thread-safe)
 *
 * @param proxy The D-Bus proxy object to query
 * @param name The name of the property to get
 * @param value Pointer to store the property value (must match property type)
 * @return gboolean TRUE if successful, FALSE otherwise
 *
 * This function synchronously gets a basic type property value from a D-Bus
 * proxy object in a thread-safe manner. For complex struct properties,
 * please use dbus_proxy_get_property_iter_cb instead. The value parameter
 * must point to storage of the correct type for the property being retrieved.
 */
gboolean dbus_proxy_get_property_basic(GDBusProxy* proxy, const char* name,
    void* value);

/**
 * Asynchronously gets a D-Bus proxy property with thread safety
 *
 * @param proxy The D-Bus proxy object to query
 * @param name Name of the property to retrieve
 * @param value Pointer to store the property value
 * @param iter_cb Callback function to handle property iteration
 * @return TRUE if property was successfully retrieved, FALSE on error
 *
 * @note This function handles thread synchronization automatically.
 *       It can be called from any thread but will block if not in default loop.
 */
gboolean dbus_proxy_get_property_iter_cb(GDBusProxy* proxy, const char* name,
    void* value, GDBusPropIterFunction iter_cb);

GDBusProxy* dbus_proxy_lookup(void* list, int* index, const char* path,
    const char* interface);
char* dbus_proxy_path_lookup(void* list, int* index, const char* path);

gboolean dbus_proxy_refresh_property(GDBusProxy* proxy, const char* name);

typedef void (*GDBusResultFunction)(const DBusError* error, void* user_data);

/**
 * @brief This function is used to set the properties of a D-Bus proxy.
 *
 * @param proxy Pointer to GDBusProxy, indicating the proxy to set the properties.
 * @param name The name of the property, which should be a string type.
 * @param type The type of the property, which should be an integer, indicating the data
 * type of the property.
 * @param value Pointer to the value of the property, which should be a void type,
 * indicating the value of the property.
 * @param function Pointer to GDBusResultFunction, indicating the function to be executed
 * after setting the property.
 * @param user_data Pointer to user data, which will be passed when executing
 * the @param function function.
 * @param destroy Pointer to GDBusDestroyFunction, indicating how to destroy the data
 * pointed to by value when the set property is no longer needed.
 *
 * @return Returns a true value of type gboolean if the property is successfully set;
 * otherwise, returns a false value.
 */
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

/**
 * @brief Calls the specified @method method on the given D-Bus proxy
 *
 * @param proxy Pointer to GDBusProxy, indicating the D-Bus proxy to be called
 * @param method The name of the method to be called
 * @param setup A GDBusSetupFunction, used to set the parameters of the method call
 * @param function A GDBusReturnFunction, called after the method call succeeds,
 * returns the return value of the method
 * @param user_data User data, passed to GDBusReturnFunction
 * @param destroy A GDBusDestroyFunction, called after the method call is completed,
 * used to clean up resources
 *
 * @return Returns gboolean if the method call succeeds, otherwise returns gboolean
 */
gboolean dbus_proxy_method_call(GDBusProxy* proxy, const char* method,
    GDBusSetupFunction setup,
    GDBusReturnFunction function, void* user_data,
    GDBusDestroyFunction destroy);

typedef void (*GDBusClientFunction)(GDBusClient* client, void* user_data);
typedef void (*GDBusProxyFunction)(GDBusProxy* proxy, void* user_data);
typedef gboolean (*GDBusProxyPropertyFilterFunction)(GDBusProxy* proxy, void* user_data);
typedef void (*GDBusPropertyFunction)(GDBusProxy* proxy, const char* name,
    DBusMessageIter* iter, void* user_data);
typedef gboolean (*GDBusProxyFilterFunction)(const char* path,
    const char* interface, void* user_data);

/**
 * @brief Sets a property monitor for the D-Bus proxy.
 *
 * @param proxy the GDBusProxy object which the property monitor is to be set.
 * @param function Pointer to the callback function that handles property changes.
 * @param user_data User data, which will be passed to the callback function.
 * @return Returns TRUE if the monitor was successfully set, otherwise returns FALSE.
 */
gboolean dbus_proxy_set_property_watch(GDBusProxy* proxy,
    GDBusPropertyFunction function, void* user_data);

/**
 * @brief Removes a property monitor from a D-Bus proxy.
 *
 * @param proxy the GDBusProxy object whose property monitor is to be removed.
 * @param destroy Pointer to a destruction function that handles user data.
 * @return Returns TRUE if the monitor was successfully removed, otherwise returns FALSE.
 */
gboolean dbus_proxy_remove_property_watch(GDBusProxy* proxy,
    GDBusDestroyFunction destroy);

gboolean dbus_proxy_set_removed_watch(GDBusProxy* proxy,
    GDBusProxyFunction destroy, void* user_data);

/**
 * @brief Create a new DBus client instance
 *
 * This function is used to create a new DBus client instance.
 *
 * @param connection Pointer to DBus connection.
 * @param service Service name, used to identify DBus service.
 * @param path Path, used to identify the path of DBus object.
 *
 * @return Returns a pointer to the newly created GDBusClient instance.
 */
GDBusClient* dbus_client_new(DBusConnection* connection,
    const char* service, const char* path);

GDBusClient* dbus_client_new_full(DBusConnection* connection,
    const char* service,
    const char* path,
    const char* root_path);

/**
 * @brief Increase the reference count of the GDBusClient object.
 *
 * @param client The GDBusClient object whose reference count is to be increased.
 * @return The GDBusClient object after the reference count is increased.
 */
GDBusClient* dbus_client_ref(GDBusClient* client);

/**
 * @brief Decrement the reference count of a GDBusClient object.
 *
 * @param client The GDBusClient object whose reference count is to be decremented.
 */
void dbus_client_unref(GDBusClient* client);

/**
 * @brief Set the connection monitoring function of the D-Bus client.
 *
 * @param client Pointer to the GDBusClient instance.
 * @param function Pointer to the function of type GDBusWatchFunction, which will be
 * called when there is a new D-Bus connection.
 * @param user_data caller data, which will be passed to the caller when callback
 * @return If the setting is successful, return a true value of type gboolean; otherwise,
 * return a false value.
 */
gboolean dbus_client_set_connect_watch(GDBusClient* client, GDBusWatchFunction function,
    void* user_data);

/**
 * @brief Set the disconnection monitoring function of the D-Bus client.
 *
 * @param client Pointer to the GDBusClient instance.
 * @param function Pointer to the function of type GDBusWatchFunction, which will be
 * called when the D-Bus connection is disconnected.
 * @param user_data caller data will be passed to the caller when calling the function.
 * @return If the setting is successful, return a true value of type gboolean; otherwise,
 * return a false value.
 */
gboolean dbus_client_set_disconnect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data);

/**
 * @brief Set the signal monitoring function of the D-Bus client
 *
 * This function is used to set the signal monitoring function of the D-Bus client.
 * When the client receives a D-Bus signal, the function will be called.
 *
 * @param client Pointer to the D-Bus client
 * @param function Points to the function that processes the D-Bus signal
 * @param user_data caller data will be passed to the caller when calling the function
 *
 * @return If the setting is successful, return gboolean as TRUE, otherwise return FALSE
 */
gboolean dbus_client_set_signal_watch(GDBusClient* client,
    GDBusMessageFunction function, void* user_data);

/**
 * @brief Set the preparation function and user data of the DBus client
 *
 * This function is used to set the preparation function and user data of the DBus client.
 * The preparation function will be called when the DBus client is ready, and the user
 * data can be passed to this function.
 *
 * @param client Pointer to the DBus client
 * @param ready Preparation function, which will be called when the DBus client is ready
 * @param user_data User data, which can be passed to the preparation function
 *
 * @return gboolean If the setting is successful, it returns TRUE, otherwise it returns FALSE
 */
gboolean dbus_client_set_ready_watch(GDBusClient* client,
    GDBusClientFunction ready, void* user_data);

/**
 * @brief Set the proxy handler function
 *
 * This function is used to set the proxy handler function of the D-Bus client.
 *
 * @param client Pointer to the GDBusClient instance.
 * @param proxy_added Callback function when the proxy is added.
 * @param proxy_removed Callback function when the proxy is removed.
 * @param proxy_property_filter Callback function for proxy property filtering.
 * @param property_changed Callback function when the property is changed.
 * @param user_data caller data, which will be used in the callback function.
 * @return If the setting is successful, the return gboolean is TRUE, otherwise
 * it returns FALSE.
 */
gboolean dbus_client_set_proxy_handlers(GDBusClient* client,
    GDBusProxyFunction proxy_added,
    GDBusProxyFunction proxy_removed,
    GDBusProxyPropertyFilterFunction proxy_property_filter,
    GDBusPropertyFunction property_changed,
    void* user_data);

/**
 * @brief Set the proxy filter function
 *
 * This function is used to set the proxy filter function of the D-Bus client.
 *
 * @param client Pointer to the GDBusClient instance.
 * @param proxy_filter Callback function of the proxy filter.
 * @param user_data caller data, which will be used in the callback function.
 * @return If the setting is successful, the return gboolean is TRUE, otherwise
 * it returns FALSE.
 */
gboolean dbus_client_set_proxy_filter(GDBusClient* client,
    GDBusProxyFilterFunction proxy_filter,
    void* user_data);

/**
 * Adds a service watch for the specified name.
 * @param client The DBus client instance
 * @param name The service name to watch
 * @param connect Callback when service connects
 * @param disconnect Callback when service disconnects
 * @param user_data User data passed to callbacks
 * @param destroy Destroy notification callback
 * @return Watch ID
 */
guint dbus_client_add_service_watch(GDBusClient* client, const char* name,
    GDBusWatchFunction connect, GDBusWatchFunction disconnect,
    void* user_data, GDBusDestroyFunction destroy);

/**
 * Adds a watch for service disconnection only.
 * @param client The DBus client instance
 * @param name The service name to watch
 * @param function Callback when service disconnects
 * @param user_data User data passed to callback
 * @param destroy Destroy notification callback
 * @return Watch ID
 */
guint dbus_client_add_service_disconnect_watch(GDBusClient* client, const char* name,
    GDBusWatchFunction function, void* user_data, GDBusDestroyFunction destroy);

/**
 * Adds a signal watch with detailed matching criteria.
 * @param client The DBus client instance
 * @param sender The sender name to match
 * @param path The object path to match
 * @param interface The interface name to match
 * @param member The member name to match
 * @param function Callback when signal is received
 * @param user_data User data passed to callback
 * @param destroy Destroy notification callback
 * @return Watch ID
 */
guint dbus_client_add_signal_watch(GDBusClient* client,const char* sender,
    const char* path, const char* interface, const char* member,
    GDBusSignalFunction function, void* user_data, GDBusDestroyFunction destroy);

/**
 * Adds a properties change watch.
 * @param client The DBus client instance
 * @param sender The sender name to match
 * @param path The object path to match
 * @param interface The interface name to match
 * @param function Callback when properties change
 * @param user_data User data passed to callback
 * @param destroy Destroy notification callback
 * @return Watch ID
 */
guint dbus_client_add_properties_watch(GDBusClient* client,
    const char* sender, const char* path, const char* interface,
    GDBusSignalFunction function, void* user_data, GDBusDestroyFunction destroy);

/**
 * Removes a previously added watch.
 * @param client The DBus client instance
 * @param tag The watch ID to remove
 * @return TRUE if watch was found and removed
 */
gboolean dbus_client_remove_watch(GDBusClient* client, guint tag);

/**
 * Removes all watches from the client.
 * @param client The DBus client instance
 */
void dbus_client_remove_all_watches(GDBusClient* client);

/**
 * Adds a watch for client disconnection.
 * @param client The DBus client instance
 * @param function Callback when client disconnects
 * @param user_data User data passed to callback
 * @param destroy Destroy notification callback
 * @return Watch ID
 */
gboolean dbus_client_add_disconnect_watch(GDBusClient* client,
    GDBusWatchFunction function, void* user_data, DBusFreeFunction destroy);


#ifdef __cplusplus
}
#endif

#endif /* __GDBUS_H */
