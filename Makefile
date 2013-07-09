CC = cc
CFLAGS = -ggdb

all: main

main: reki.o http-parser/http_parser.o dynamic_string.o
	$(CC) -o $@ $^ -lev -lhiredis $(CFLAGS)

clean:
	$(RM) *.o
	$(RM) *~
	$(RM) *#
