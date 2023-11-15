# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2010-2014 Intel Corporation

# Project root
PROJECT_ROOT=$(PWD)

# binary name
APP = nspk-core

# all source are stored in SRCS-y
SRCS-y := $(shell find src -type f -name *.c 2>/dev/null)

PKGCONF ?= pkg-config

# Build using pkg-config variables if possible
ifneq ($(shell $(PKGCONF) --exists libdpdk && echo 0),0)
$(error "no installation of DPDK found")
endif

all: shared
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

CC=gcc

FFMPEG_LIBS = libavdevice   \
              libavformat   \
              libavfilter   \
              libavcodec    \
              libswresample \
              libswscale    \
              libavutil

LIBS := libdpdk alsa $(FFMPEG_LIBS)
PC_FILE := $(shell $(PKGCONF) --path $(LIBS) 2>/dev/null)

DEFINES = -DNETBE_DEBUG -DNETFE_DEBUG
INCLUDE_DIRS = -I$(PROJECT_ROOT)/include/ -I$(PROJECT_ROOT)/deps/tldk/${RTE_TARGET}/include
CFLAGS += $(DEFINES) $(INCLUDE_DIRS) -D_GNU_SOURCE -DALLOW_EXPERIMENTAL_API
CFLAGS += $(shell $(PKGCONF) --cflags $(LIBS))
CFLAGS += -g -O0

LDFLAGS_SHARED = -L$(PROJECT_ROOT)/deps/tldk/${RTE_TARGET}/lib -ltle_dring -ltle_timer -ltle_memtank -ltle_l4p
LDFLAGS_SHARED += $(shell $(PKGCONF) --libs $(LIBS))
LDFLAGS_STATIC = -L$(PROJECT_ROOT)/deps/tldk/${RTE_TARGET}/lib -l:libtle_dring.a -l:libtle_timer.a -l:libtle_memtank.a -l:libtle_l4p.a
LDFLAGS_STATIC += $(shell $(PKGCONF) --static --libs $(LIBS))

ifeq ($(MAKECMDGOALS),static)
$(error "Sorry!! Currently we don't support static builds. We will soon support this feature.")
# check for broken pkg-config
ifeq ($(shell echo $(LDFLAGS_STATIC) | grep 'whole-archive.*l:lib.*no-whole-archive'),)
$(warning "pkg-config output list does not contain drivers between 'whole-archive'/'no-whole-archive' flags.")
$(error "Cannot generate statically-linked binaries with this version of pkg-config")
endif
endif

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(SRCS-y) -o $@ $(LDFLAGS) $(CFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(SRCS-y) -o $@ $(LDFLAGS) $(CFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true
