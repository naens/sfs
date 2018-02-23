all: clean view

view:
	gcc -g -O0 -Wall view.c sfs.c -o view

clean:
	rm -f *.o view
