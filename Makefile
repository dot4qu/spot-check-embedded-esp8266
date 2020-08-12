#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := tides

include $(IDF_PATH)/make/project.mk

SUBDIRS += esp-gdbstub
COMPONENTS_eagle.app.v6 += esp-gdbstub/libesp-gdbstub.a
CXXFLAGS	+= -mlongcalls
CFLAGS += -mlongcalls
C_CXX_FLAGS += -mlongcalls
LDFLAGS += -mlongcalls
COMPONENT_LDFLAGS += -mlongcalls

.PHONY: monitor
monitor:
	miniterm.py /dev/cu.usbserial-14140 9600
