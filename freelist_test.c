#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "sfs.h"


#define DIR_NAME_LEN 53
#define FILE_NAME_LEN 29
#define ENTRY_SIZE 64
#define BLOCK_SIZE 512

#define EXIT sfs_terminate(sfs); exit(0);
#define ERROR fprintf(stderr, ">>>ERROR<<<\n"); sfs_terminate(sfs); exit(1);

int make_size(int count_blocks)
{
    if (count_blocks == 0) {
        return 0;
    }
    int x = abs(rand());
    return (count_blocks - 1) * BLOCK_SIZE + x % BLOCK_SIZE;
}

int curr_test;
void test_create(sfs, test_number, name)
    SFS *sfs;
    int test_number;
    const char *name;
{
    curr_test = test_number;
    printf("\n>>>%d. CREATE %s<<<\n", test_number, name);
    if (sfs_create(sfs, name) != 0) {
        ERROR
    }
}

void test_resize(sfs, test_number, name, num_blocks)
    SFS *sfs;
    int test_number;
    const char *name;
    int num_blocks;
{
    curr_test = test_number;
    printf("\n>>>%d. RESIZE %s<<<\n", test_number, name);
    int sz = make_size(num_blocks);
    if (sfs_resize(sfs, (const char*) name, sz) != 0) {
        ERROR
    }
}

void test_delete(sfs, test_number, name)
    SFS *sfs;
    int test_number;
    const char *name;
{
    curr_test = test_number;
    printf("\n>>>%d. DELETE %s<<<\n", test_number, name);
    if (sfs_delete(sfs, name) != 0) {
        ERROR
    }
}

int main(int argc, char **argv)
{
    srand(time(NULL));

    printf("\n>>>0. INITIALIZE<<<\n");
    SFS *sfs = sfs_init("sfs_f.img");

    test_create(sfs, 1, "File1");
    test_resize(sfs, 2, "File1", 2);
    test_create(sfs, 3, "File2");
    test_resize(sfs, 4, "File2", 1);
    test_delete(sfs, 5, "File1");
    test_create(sfs, 6, "File3");
    test_resize(sfs, 7, "File3", 3);
    test_resize(sfs, 8, "File2", 2);
    test_resize(sfs, 9, "File3", 5);
    test_resize(sfs, 10, "File2", 3);
    test_create(sfs, 11, "File4");
    test_resize(sfs, 12, "File4", 2);
    test_resize(sfs, 13, "File4", 1);
    test_resize(sfs, 14, "File3", 1);
    test_resize(sfs, 15, "File2", 4);
    test_resize(sfs, 16, "File3", 0);
    EXIT
    test_delete(sfs, 17, "File2");
    test_delete(sfs, 18, "File3");
    test_create(sfs, 19, "File5");
    test_resize(sfs, 20, "File5", 5);
    test_delete(sfs, 21, "File5");
    test_delete(sfs, 22, "File4");

    EXIT
}
