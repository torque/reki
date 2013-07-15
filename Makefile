CC = cc
CFLAGS = -ggdb

all: reki

reki: reki.o http-parser/http_parser.o dynamic_string.o
	$(CC) -Wall -Wextra -Werror -o $@ $^ -lev -lhiredis $(CFLAGS)

clean:
	$(RM) *.o
	$(RM) *~
	$(RM) *#
