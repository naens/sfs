#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "sfs.h"


#define DIR_NAME_LEN 53
#define FILE_NAME_LEN 29
#define ENTRY_SIZE 64

#define EXIT sfs_terminate(sfs); return 0;


/* Tests for file names using functions in sfs.h */

// buffer must be at least len+1 (for '\0')
void new_name(char *buf, int len)
{
    for (int i = 0; i < len; ++i) {
        int n = rand();
        buf[i] = 'a' + n % ('z' - 'a' + 1);
    }
    buf[len] = '\0';
}

// returns a length based on the number of entries
int get_length(int first_cont_len, int n)
{
    int r = rand();
    return (n - 1) * ENTRY_SIZE + r % first_cont_len;
}

// entry length is 64 bytes

int main(int argc, char **argv)
{
    srand(time(NULL));

// 0. initialize
    SFS *sfs = sfs_init("sfs_f.img");

// 1. create f1: 600 (10e)
    int f1namelen = get_length(FILE_NAME_LEN, 10);
    char f1name[f1namelen];
    new_name(f1name, f1namelen + 1);
    printf("f1namelen=%d\n", f1namelen);
    if (sfs_create(sfs, (const char*) f1name) != 0) {
        fprintf(stderr, "error test 1: create f1\n");
        return -1;
    }
    if (sfs_resize(sfs, (const char*) f1name, 100) != 0) {
        fprintf(stderr, "error test 1: resize f1\n");
        return -1;
    }
    EXIT

// 2. delete f1
//    if (sfs_delete(sfs, f1name) !=0) {
//        fprintf(stderr, "error test 2: delete f1\n");
//        return -1;
//    }

// 3. create f2: (5e)
    int f2namelen = get_length(FILE_NAME_LEN, 5);
    char f2name[f2namelen];
    new_name(f2name, f2namelen + 1);
    printf("f2namelen=%d\n", f2namelen);
    if (sfs_create(sfs, (const char*) f2name) != 0) {
        fprintf(stderr, "error test 3: create f2\n");
        return -1;
    }
    if (sfs_resize(sfs, (const char*) f2name, 100) != 0) {
        fprintf(stderr, "error test 3: resize f2\n");
        return -1;
    }

// 4. create f3: (3e)
    int f3namelen = get_length(FILE_NAME_LEN, 3);
    char f3name[f3namelen];
    new_name(f3name, f3namelen + 1);
    printf("f3namelen=%d\n", f3namelen);
    if (sfs_create(sfs, (const char*) f3name) != 0) {
        fprintf(stderr, "error test 4: create f3\n");
        return -1;
    }
    if (sfs_resize(sfs, (const char*) f3name, 100) != 0) {
        fprintf(stderr, "error test 4: resize f3\n");
        return -1;
    }

// 5. resize f2: (2e)
    int f2name2len = get_length(FILE_NAME_LEN, 2);
    char f2name2[f2name2len];
    new_name(f2name2, f2name2len + 1);
    if (sfs_rename(sfs, (const char*) f2name, (const char*) f2name2, 0) != 0) {
        fprintf(stderr, "error test 5: rename f2\n");
        return -1;
    }


// 6. resize f3: (1e)
    int f3name2len = get_length(FILE_NAME_LEN, 1);
    char f3name2[f3name2len];
    new_name(f3name2, f3name2len + 1);
    if (sfs_rename(sfs, f3name, f3name2, 0) != 0) {
        fprintf(stderr, "error test 6: rename f3\n");
        return -1;
    }

// 7. create f4: (2e)
    int f4namelen = get_length(FILE_NAME_LEN, 3);
    char f4name[f4namelen];
    new_name(f4name, f4namelen + 1);
    printf("f4namelen=%d\n", f4namelen);
    if (sfs_create(sfs, (const char*) f4name) != 0) {
        fprintf(stderr, "error test 7: create f4\n");
        return -1;
    }
    if (sfs_resize(sfs, (const char*) f4name, 100) != 0) {
        fprintf(stderr, "error test 7: resize f4\n");
        return -1;
    }

// 8. resize f2: (4e)
    int f2name3len = get_length(FILE_NAME_LEN, 4);
    char f2name3[f2name3len];
    new_name(f2name3, f2name3len + 1);
    if (sfs_rename(sfs, f2name2, f2name3, 0) != 0) {
        fprintf(stderr, "error test 8: rename f2\n");
        return -1;
    }

// 9. delete f4
    if (sfs_delete(sfs, f1name) !=0) {
        fprintf(stderr, "error test 9: delete f4\n");
        return -1;
    }

// 10. resize f2: (5e)
    int f2name4len = get_length(FILE_NAME_LEN, 5);
    char f2name4[f2name4len];
    new_name(f2name4, f2name4len + 1);
    if (sfs_rename(sfs, f2name3, f2name4, 0) != 0) {
        fprintf(stderr, "error test 10: rename f2\n");
        return -1;
    }

// 11. delete f2
    if (sfs_delete(sfs, f1name) !=0) {
        fprintf(stderr, "error test 11: delete f2\n");
        return -1;
    }

// 12. delete f3
    if (sfs_delete(sfs, f1name) !=0) {
        fprintf(stderr, "error test 12: delete f3\n");
        return -1;
    }

    EXIT
}
