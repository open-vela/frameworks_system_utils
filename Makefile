#
# Copyright (C) 2021 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include $(APPDIR)/Make.defs

CSRCS += trace/trace.c

ifneq ($(CONFIG_ANDROID_LIBBASE),)
CSRCS += log/log_write.c log/set_abort_message.c
endif

ifneq ($(CONFIG_ATRACE),)
MAINSRC  += trace/atrace.c
PROGNAME += atrace
endif

ifneq ($(CONFIG_GDBUS),)
CSRCS  += $(wildcard gdbus/*.c)
CFLAGS += -DDBUS_COMPILATION -DVERSION="1.15.1"
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/dbus/dbus
endif # CONFIG_LIB_DBUS

ifneq ($(CONFIG_KVDB),)
ifneq ($(CONFIG_KVDB_DIRECT),)
CSRCS += kvdb/direct.c
else
CSRCS += kvdb/client.c
MAINSRC += kvdb/exitprop.c
PROGNAME += exitprop
endif # CONFIG_KVDB_DIRECT

CSRCS += kvdb/common.c kvdb/system_properties.c kvdb/backend.c
MAINSRC  += kvdb/setprop.c kvdb/getprop.c
PROGNAME += setprop getprop

ifneq ($(CONFIG_KVDB_SERVER),)
MAINSRC  += kvdb/server.c
PROGNAME += kvdbd
endif # CONFIG_KVDB_SERVER

ifneq ($(CONFIG_KVDB_NVS),)
CSRCS += kvdb/nvs.c
else ifneq ($(CONFIG_KVDB_UNQLITE),)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/external/unqlite/unqlite
CSRCS += kvdb/unqlite.c
endif # CONFIG_KVDB_NVS

ifneq ($(CONFIG_KVDB_FILE)$(CONFIG_KVDB_TEMPORARY_STORAGE),)
CSRCS += kvdb/file.c
endif # CONFIG_KVDB_FILE or CONFIG_KVDB_TEMPORARY_STORAGE

ifneq ($(CONFIG_KVDB_QEMU_PROPERTIES),)
MAINSRC  += kvdb/qemu_properties.c
PROGNAME += qemuprop
endif # CONFIG_KVDB_QEMU_PROPERTIES

PRIORITY  = $(CONFIG_KVDB_PRIORITY)
STACKSIZE = $(CONFIG_KVDB_STACKSIZE)
MODULE    = $(CONFIG_KVDB)
endif # CONFIG_KVDB

EXPORT_FILES := gdbus/gdbus.h include

include $(APPDIR)/Application.mk
