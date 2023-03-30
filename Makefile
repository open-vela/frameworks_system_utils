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

CSRCS += log/log_write.c log/set_abort_message.c trace/trace.c

ifneq ($(CONFIG_LIB_DBUS),)
CSRCS  += $(wildcard gdbus/*.c)
CFLAGS += -DDBUS_COMPILATION -DVERSION="1.15.1"
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/dbus/dbus}
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/glib/glib/glib}
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/glib/glib/}
endif # CONFIG_LIB_DBUS

ifneq ($(CONFIG_KVDB),)
CSRCS     += kvdb/client.c kvdb/system_properties.c
MAINSRC   += kvdb/setprop.c kvdb/getprop.c
PROGNAME  += setprop getprop
endif # CONFIG_KVDB

ifneq ($(CONFIG_KVDB_SERVER),)
CFLAGS    += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/external/unqlite/unqlite}
CSRCS     += kvdb/unqlite.c
MAINSRC   += kvdb/server.c
PROGNAME  += kvdbd
endif # CONFIG_KVDB_SERVER

PRIORITY  = $(CONFIG_KVDB_PRIORITY)
STACKSIZE = $(CONFIG_KVDB_STACKSIZE)
MODULE    = $(CONFIG_KVDB)

ASRCS := $(wildcard $(ASRCS))
CSRCS := $(wildcard $(CSRCS))
CXXSRCS := $(wildcard $(CXXSRCS))
MAINSRC := $(wildcard $(MAINSRC))
NOEXPORTSRCS = $(ASRCS)$(CSRCS)$(CXXSRCS)$(MAINSRC)

ifneq ($(NOEXPORTSRCS),)
BIN := $(APPDIR)/staging/libframework.a
endif

EXPORT_FILES := gdbus/gdbus.h include

include $(APPDIR)/Application.mk
