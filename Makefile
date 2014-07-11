TARGET  := reki
CC      := clang
CFLAGS  := -Wall -std=c99
LDFLAGS := -luv -lhiredis
# http://man7.org/linux/man-pages/man7/feature_test_macros.7.html
DEFS    := -D_XOPEN_SOURCE=600

SOURCE_DIRS := src
SOURCES     := $(foreach dir, $(SOURCE_DIRS), $(wildcard $(dir)/*.c)) http-parser/http_parser.c
OBJECTS     := $(SOURCES:.c=.o)

.PHONY: all debug clean

all: debug

production: DEFS += -DPRODUCTION -DNDEBUG
production: CFLAGS += -O2
production: $(TARGET)

debug: CFLAGS += -O0 -ggdb
debug: $(TARGET)

$(TARGET): $(OBJECTS)
	@echo LINK $@
	@$(CXX) $^ $(LDFLAGS) -o $@

%.o: %.c
	@echo CC $@
	@$(CC) $(DEFS) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Cleanup complete!"
