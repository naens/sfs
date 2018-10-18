CC=gcc
CFLAGS=-Werror -Wfatal-errors -g -O0 -Wall
FUSEFLAGS=$(shell pkg-config fuse --cflags --libs)

#view: clean
#	gcc -Werror -Wfatal-errors -g -O0 -Wall view.c sfs.c dirset.c -o view

sfs_fuse: sfs_fuse.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(FUSEFLAGS)

clean:
	rm -f *.o view
