# adapted from https://github.com/rikusalminen/makefile-for-c

CC=gcc
CFLAGS=-std=gnu99
CFLAGS+=-W -Wall -pedantic -Wextra -Wfatal-errors -Wformat
#CFLAGS+=-O3
CFLAGS+=-O0 -g -ggdb
CFLAGS+=-MMD  # generate dependency .d files
LDLIBS=
LDFLAGS=

SRCS=src/main.c
TARGETS=src/main

src/main: src/main.o

TEST_SUITE=src/foo-test

.DEFAULT_GOAL=all
.PHONY: all
all: $(TARGETS)

SRC_DIR ?= $(patsubst %/,%, $(dir $(abspath $(firstword $(MAKEFILE_LIST)))))
CFLAGS+=-I$(SRC_DIR)/include

.PHONY: clean
clean:
	$(RM) $(TARGETS)
	$(RM) $(OBJS)
	$(RM) $(DEPS)
ifneq ($(SRC_DIR), $(CURDIR))
	-@rmdir $(OBJDIRS)
endif

#SRCS=$(notdir $(wildcard $(SRC_DIR)src/*.c))
OBJS=$(SRCS:.c=.o)
DEPS=$(OBJS:.o=.d)

# Object file subdirectories
ifneq ($(SRC_DIR), $(CURDIR))
vpath %.c $(SRC_DIR)

OBJDIRS=$(filter-out $(CURDIR)/, $(sort $(dir $(abspath $(OBJS)))))
$(OBJDIRS): ; @mkdir $@
$(DEPS): | $(OBJDIRS)
$(OBJS): | $(OBJDIRS)
endif

-include $(DEPS)

# implicit rules for building archives not parallel safe (e.g. make -j 3)
%.a: ; ar rcs $@ $^
