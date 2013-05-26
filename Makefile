all: main

main: main.o
	$(CC) -o $@ $^ -lev

clean: 
	$(RM) *.o
	$(RM) *~
	$(RM) *#
