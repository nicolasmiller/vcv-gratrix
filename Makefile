SLUG = Gratrix
VERSION = 0.6.0dev

FLAGS += -I../../src/core

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res

RACK_DIR ?= ../..
include $(RACK_DIR)/plugin.mk
