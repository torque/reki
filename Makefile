TARGET  := reki
OBJDIR  := build
UNAME   := $(shell uname -s)

CC      := cc
CFLAGS  := -Wall -std=c99 -I"$(OBJDIR)/include"
LDFLAGS :=

# libuv requires pthreads on Linux, and probably BSD, but not OSX.
ifneq ($(UNAME), Darwin)
LDFLAGS += -pthread
endif
# http://man7.org/linux/man-pages/man7/feature_test_macros.7.html
DEFS    := -D_XOPEN_SOURCE=600

SRCDIRS := src
DEPS    := $(OBJDIR)/lib/libhiredis.a $(OBJDIR)/lib/libuv.a
SOURCES := $(foreach dir, $(SRCDIRS), $(wildcard $(dir)/*.c)) http-parser/http_parser.c
OBJECTS := $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))

.PHONY: all debug hiredis libuv clean-all clean clean-deps

all: debug

production: DEFS += -DPRODUCTION -DNDEBUG -DCLIENTTIMEINFO
production: CFLAGS += -Os -flto
production: LDFLAGS += -flto
production: $(TARGET)

debug: DEFS += -DCLIENTTIMEINFO
debug: CFLAGS += -g -O0 -Wno-unused-function -fsanitize=address -fno-omit-frame-pointer
debug: LDFLAGS += -g -fsanitize=address
debug: $(TARGET)

$(TARGET): $(OBJECTS)
	@printf "\e[1;32m LINK\e[m $@\n"
	@$(CC) $^ $(DEPS) $(LDFLAGS) -o $@

$(OBJECTS): $(DEPS) | $(OBJDIR)/src/ $(OBJDIR)/http-parser/

$(OBJDIR)/%.o: %.c
	@printf "\e[1;34m   CC\e[m $<\n"
	@$(CC) $(DEFS) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@printf "\e[1;33mMKDIR\e[m $@\n"
	@mkdir -p $@

$(OBJDIR)/%/: | $(OBJDIR)
	@printf "\e[1;33mMKDIR\e[m $@\n"
	@mkdir -p $@

$(OBJDIR)/lib/libhiredis.a: deps/hiredis/Makefile
	@printf "\e[1;35m MAKE\e[m $@\n"
	@$(MAKE) -sC deps/hiredis -e PREFIX=$(realpath $(OBJDIR)) install >/dev/null

$(OBJDIR)/lib/libuv.a: deps/libuv/configure
	@printf "\e[1;35m MAKE\e[m $@\n"
	@cd deps/libuv && ./configure --prefix=$(realpath $(OBJDIR)) >/dev/null 2>&1
	@$(MAKE) -sC deps/libuv install >/dev/null 2>&1

deps/libuv/configure: deps/libuv/autogen.sh
	@printf "\e[1;36m  GEN\e[m $@\n"
	@deps/libuv/autogen.sh >/dev/null 2>&1

deps/libuv/autogen.sh: $(OBJDIR)/submodules
deps/hiredis/Makefile: $(OBJDIR)/submodules

$(OBJDIR)/submodules: | $(OBJDIR)
	@printf "\e[1;36mFETCH\e[m submodules\n"
	@git submodule update --init --recursive >/dev/null 2>&1
# a cheesy marker so this recipe doesn't run every single time.
	@touch $(OBJDIR)/submodules

clean-all: clean clean-deps
	@printf "\e[1;31m   RM\e[m $(OBJDIR)\n"
	@rm -rf $(OBJDIR)

clean:
	@printf "\e[1;31m   RM\e[m $(OBJDIR)/src\n"
	@rm -rf $(OBJDIR)/src
	@printf "\e[1;31m   RM\e[m $(OBJDIR)/http-parser\n"
	@rm -rf $(OBJDIR)/http-parser
	@printf "\e[1;31m   RM\e[m $(TARGET)\n"
	@rm -f $(TARGET)

clean-deps:
	@printf "\e[1;31mCLEAN\e[m deps/libuv\n"
	@cd deps/libuv && git clean -fdx >/dev/null
	@printf "\e[1;31mCLEAN\e[m deps/hiredis\n"
	@cd deps/hiredis && git clean -fdx >/dev/null
