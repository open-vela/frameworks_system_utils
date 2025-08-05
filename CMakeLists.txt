#
# Copyright (C) 2021 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.
#
if(CONFIG_SCHED_INSTRUMENTATION_DUMP
   OR CONFIG_ANDROID_LIBBASE
   OR CONFIG_ATRACE
   OR CONFIG_LIB_DBUS
   OR CONFIG_KVDB)

  set_property(
    TARGET nuttx
    APPEND
    PROPERTY NUTTX_INCLUDE_DIRECTORIES
             ${NUTTX_APPS_DIR}/frameworks/system/utils/include)

  set(CSRCS)
  set(CFLAGS)
  set(INCDIR)

  nuttx_add_library(framework_utils STATIC)

  list(APPEND CSRCS trace/trace.c)
  if(CONFIG_ANDROID_LIBBASE)
    list(APPEND CSRCS log/log_write.c log/set_abort_message.c)
  endif()

  if(CONFIG_ATRACE)
    nuttx_add_application(NAME atrace SRCS trace/atrace.c DEPENDS
                          framework_utils)
  endif()

  if(CONFIG_GDBUS)
    file(GLOB GDBUS_CSRCS gdbus/*.c)
    list(APPEND CSRCS ${GDBUS_CSRCS})
    list(APPEND CFLAGS -DDBUS_COMPILATION -DVERSION="1.15.1")
    list(APPEND INCDIR ${NUTTX_APPS_DIR}/external/dbus/dbus)
  endif()

  if(CONFIG_KVDB)
    if(CONFIG_KVDB_DIRECT)
      list(APPEND CSRCS kvdb/direct.c)
    else()
      list(APPEND CSRCS kvdb/client.c)
      nuttx_add_application(
        MODULE
        ${CONFIG_KVDB}
        NAME
        exitprop
        STACKSIZE
        ${CONFIG_KVDB_STACKSIZE}
        PRIORITY
        ${CONFIG_KVDB_PRIORITY}
        SRCS
        kvdb/exitprop.c
        INCLUDE_DIRECTORIES
        ${INCDIR}
        DEPENDS
        framework_utils)
    endif()
    list(APPEND CSRCS kvdb/common.c kvdb/system_properties.c kvdb/backend.c)

    if(CONFIG_KVDB_NVS)
      list(APPEND CSRCS kvdb/nvs.c)
    elseif(CONFIG_KVDB_UNQLITE)
      list(APPEND INCDIR ${NUTTX_APPS_DIR}/external/unqlite/unqlite)
      list(APPEND CSRCS kvdb/unqlite.c)
    endif()

    if(CONFIG_KVDB_FILE OR CONFIG_KVDB_TEMPORARY_STORAGE)
      list(APPEND CSRCS kvdb/file.c)
    endif()

    if(CONFIG_KVDB_SERVER)
      nuttx_add_application(
        MODULE
        ${CONFIG_KVDB}
        NAME
        kvdbd
        STACKSIZE
        ${CONFIG_KVDB_STACKSIZE}
        PRIORITY
        ${CONFIG_KVDB_PRIORITY}
        SRCS
        kvdb/server.c
        INCLUDE_DIRECTORIES
        ${INCDIR}
        DEPENDS
        framework_utils)
    endif()

    nuttx_add_application(
      MODULE
      ${CONFIG_KVDB}
      NAME
      setprop
      STACKSIZE
      ${CONFIG_KVDB_STACKSIZE}
      PRIORITY
      ${CONFIG_KVDB_PRIORITY}
      SRCS
      kvdb/setprop.c
      INCLUDE_DIRECTORIES
      ${INCDIR}
      DEPENDS
      framework_utils)

    nuttx_add_application(
      MODULE
      ${CONFIG_KVDB}
      NAME
      getprop
      STACKSIZE
      ${CONFIG_KVDB_STACKSIZE}
      PRIORITY
      ${CONFIG_KVDB_PRIORITY}
      SRCS
      kvdb/getprop.c
      INCLUDE_DIRECTORIES
      ${INCDIR}
      DEPENDS
      framework_utils)

    if(CONFIG_KVDB_QEMU_PROPERTIES)
      nuttx_add_application(
        MODULE
        ${CONFIG_KVDB}
        NAME
        qemuprop
        STACKSIZE
        ${CONFIG_KVDB_STACKSIZE}
        PRIORITY
        ${CONFIG_KVDB_PRIORITY}
        SRCS
        kvdb/qemu_properties.c
        INCLUDE_DIRECTORIES
        ${INCDIR}
        DEPENDS
        framework_utils)
    endif()

  endif()

  target_sources(framework_utils PRIVATE ${CSRCS})
  target_include_directories(framework_utils PRIVATE ${INCDIR})
  target_compile_options(framework_utils PRIVATE ${CFLAGS})

endif()
