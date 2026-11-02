MODULE := engines/tad

MODULE_OBJS = \
	commands.o \
	console.o \
	cpu.o \
	dosvm.o \
	graphics.o \
	metaengine.o \
	prims.o \
	resource.o \
	room.o \
	runner.o \
	script.o \
	script_bifi2_xp00.o \
	script_bifi2_xp01.o \
	script_schatten_xp00.o \
	script_schatten_xp01.o \
	tad.o

# This module can be built as a plugin
ifeq ($(ENABLE_TAD), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
