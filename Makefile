CC = cc
CFLAGS = -std=c99 -ggdb -Wall
#  -Wextra -Werror
all: reki

reki: reki.o http-parser/http_parser.o dynamic_string.o
	$(CC) -o $@ $^ -lev -lhiredis $(CFLAGS)

clean:
	$(RM) *.o
	$(RM) *~
	$(RM) *#
