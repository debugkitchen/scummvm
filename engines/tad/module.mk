MODULE := engines/tad

MODULE_OBJS = \
	console.o \
	cpu.o \
	dosvm.o \
	graphics.o \
	metaengine.o \
	resource.o \
	room.o \
	runner.o \
	tad.o

# This module can be built as a plugin
ifeq ($(ENABLE_TAD), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
