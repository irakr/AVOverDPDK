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
LIBS := libdpdk alsa
PC_FILE := $(shell $(PKGCONF) --path $(LIBS) 2>/dev/null)
INCLUDE_DIRS = -I$(PROJECT_ROOT)/include/
CFLAGS += $(INCLUDE_DIRS)
CFLAGS += -O3 $(shell $(PKGCONF) --cflags $(LIBS))
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs $(LIBS))
LDFLAGS_STATIC = $(shell $(PKGCONF) --static --libs $(LIBS))

ifeq ($(MAKECMDGOALS),static)
# check for broken pkg-config
ifeq ($(shell echo $(LDFLAGS_STATIC) | grep 'whole-archive.*l:lib.*no-whole-archive'),)
$(warning "pkg-config output list does not contain drivers between 'whole-archive'/'no-whole-archive' flags.")
$(error "Cannot generate statically-linked binaries with this version of pkg-config")
endif
endif

CFLAGS += -DALLOW_EXPERIMENTAL_API

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true
