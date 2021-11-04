include $(APPDIR)/Make.defs

ifneq ($(CONFIG_APP_FOCUS),)

CSRCS   += app_focus.c
CFLAGS += ${shell $(INCDIR) $(INCDIROPT) "$(CC)" $(APPDIR)/frameworks/utils/include}

endif # CONFIG_AAPP_FOCUS

include $(APPDIR)/Application.mk
