all: view

view: clean
	gcc -Werror -Wfatal-errors -g -O0 -Wall view.c sfs.c -o view

clean:
	rm -f *.o view
