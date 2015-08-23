TARGET  := reki
CC      := /Applications/Xcode-beta.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang
CFLAGS  := -Wall -std=c99
LDFLAGS :=
# http://man7.org/linux/man-pages/man7/feature_test_macros.7.html
DEFS    := -D_XOPEN_SOURCE=600

SRCDIRS := src
OBJDIR  := build
DEPS    := $(OBJDIR)/lib/libhiredis.a $(OBJDIR)/lib/libuv.a
SOURCES := $(foreach dir, $(SRCDIRS), $(wildcard $(dir)/*.c)) http-parser/http_parser.c
OBJECTS := $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))

.PHONY: all debug hiredis libuv clean

all: debug

production: DEFS += -DPRODUCTION -DNDEBUG
production: CFLAGS += -O2
production: $(TARGET)

debug: CFLAGS += -O0 -g -Wno-unused-function -fsanitize=address -fno-omit-frame-pointer
debug: LDFLAGS += -fsanitize=address -g
debug: $(TARGET)

$(TARGET): $(OBJECTS) $(DEPS)
	@printf "\e[1;32m LINK\e[m $@\n"
	@$(CC) $^ $(LDFLAGS) -o $@

$(OBJECTS): | $(OBJDIR)/src/ $(OBJDIR)/http-parser/

$(OBJDIR)/%.o: %.c
	@printf "\e[1;34m   CC\e[m $<\n"
	@$(CC) $(DEFS) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	@printf "\e[1;33mMKDIR\e[m $@\n"
	@mkdir -p $@

$(OBJDIR)/%/: $(OBJDIR)
	@printf "\e[1;33mMKDIR\e[m $@\n"
	@mkdir -p $@

$(OBJDIR)/lib/libhiredis.a: | $(OBJDIR)
	@printf "\e[1;35m MAKE\e[m $@\n"
	@make -sC deps/hiredis -e PREFIX=$(realpath $(OBJDIR)) install >/dev/null

$(OBJDIR)/lib/libuv.a: deps/libuv/configure | $(OBJDIR)
	@printf "\e[1;35m MAKE\e[m $@\n"
	@cd deps/libuv && ./configure --prefix=$(realpath $(OBJDIR)) >/dev/null 2>&1
	@make -sC deps/libuv install >/dev/null 2>&1

deps/libuv/configure:
	@printf "\e[1;36m  GEN\e[m $@\n"
	@deps/libuv/autogen.sh >/dev/null 2>&1

clean:
	@printf "\e[1;31m   RM\e[m $(OBJDIR)\n"
	@rm -rf $(OBJDIR)
	@printf "\e[1;31mCLEAN\e[m deps/libuv\n"
	@cd deps/libuv && git clean -fdx >/dev/null
	@printf "\e[1;31mCLEAN\e[m deps/hiredis\n"
	@cd deps/hiredis && git clean -fdx >/dev/null
	@printf "\e[1;31m   RM\e[m $(TARGET)\n"
	@rm -f $(TARGET)
