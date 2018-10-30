CC=gcc
CFLAGS=-g -O0 -Wextra -Wall -Wfatal-errors -Wno-unused-parameter $(shell pkg-config fuse3 --cflags)
LDFLAGS=-lm $(shell pkg-config fuse3 --libs)

all: sfs_fuse sfs_tool filename_test freelist_test

sfs_fuse: sfs_fuse.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

sfs_tool: sfs_tool.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

filename_test: filename_test.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

freelist_test: freelist_test.c sfs.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

.PHONY: fuse
fuse: sfs_fuse
	./sfs_fuse -s -f test

.PHONY: docs
docs: sfs.c sfs_tool.c
	robodoc --src ./ --doc ./docs --html --multidoc \ #--rc robodoc.rc \
			--nodesc --sections --tell --toc --index
	mv docs/masterindex.html docs/index.html

.PHONY: troff
troff: sfs.c sfs_tool.c
	robodoc --src ./ --doc ./troff --troff --multidoc --nodesc \
			--sections --tell

.PHONY: clean
clean:
	rm -f *.o view sfs_tool sfs_fuse filename_test freelist_test
