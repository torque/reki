CC = cc
LIBS = -lev -lhiredis
CFLAGS = -std=c99 -ggdb -Wall

production: CFLAGS += -DPRODUCTION -DNDEBUG

all: reki

production: reki

reki: reki.o http-parser/http_parser.o dynamic_string.o
	$(CC) -o $@ $^ $(LIBS) $(CFLAGS)

clean:
	$(RM) *.o
	$(RM) *~
	$(RM) *#
