all: main

main: main.o http-parser/http_parser.o
	$(CC) -o $@ $^ -lev

clean: 
	$(RM) *.o
	$(RM) *~
	$(RM) *#
