CC=gcc
CFLAGS=-Werror -Wfatal-errors -g -O0 -Wall -lm
FUSEFLAGS=$(shell pkg-config fuse3 --cflags --libs)

all: sfs_fuse sfs_tool

#view: clean
#	gcc -Werror -Wfatal-errors -g -O0 -Wall view.c sfs.c dirset.c -o view

sfs_fuse: sfs_fuse.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(FUSEFLAGS)

sfs_tool: sfs_tool.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(FUSEFLAGS)

.PHONY: fuse
fuse: sfs_fuse
	./sfs_fuse -s -f test

.PHONY: clean
clean:
	rm -f *.o view sfs_tool sfs_fuse
