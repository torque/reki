CC = gcc
CFLAGS = -ggdb

all: main

main: main.o http-parser/http_parser.o
	$(CC) -o $@ $^ -lev $(CFLAGS)

clean: 
	$(RM) *.o
	$(RM) *~
	$(RM) *#
