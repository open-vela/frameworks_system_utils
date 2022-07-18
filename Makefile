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

ifneq ($(CONFIG_APP_FOCUS),)

ifneq ($(CONFIG_ARCH_BOARD_CUSTOM_NAME),)
  BIN := $(TOPDIR)/$(CONFIG_ARCH_BOARD_CUSTOM_DIR)/libs/$(CONFIG_ARCH_BOARD_CUSTOM_NAME)/libframework.a
endif

CSRCS   += app_focus.c
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/frameworks/utils/include}

endif # CONFIG_APP_FOCUS

distclean::
	rm -rf $(TOPDIR)/$(CONFIG_ARCH_BOARD_CUSTOM_DIR)/libs/$(CONFIG_ARCH_BOARD_CUSTOM_NAME)

include $(APPDIR)/Application.mk
