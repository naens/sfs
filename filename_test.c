#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "sfs.h"


#define DIR_NAME_LEN 53
#define FILE_NAME_LEN 29
#define ENTRY_SIZE 64

#define EXIT sfs_terminate(sfs); return 0;
#define ERROR sfs_terminate(sfs); return -1;


/* Tests for file names using functions in sfs.h */

// buffer must be at least len+1 (for '\0')
void new_name(const char *prefix, char *buf, int len)
{
    int prefix_len = strlen(prefix);
    strncpy(buf, prefix, len);
    buf[prefix_len] = '_';
    for (int i = prefix_len + 1; i < len; ++i) {
        int n = rand();
        buf[i] = 'a' + n % ('z' - 'a' + 1);
    }
    buf[len] = '\0';
}

// returns a length based on the number of entries
// returned length does not include terminating zero
int get_length(int first_cont_len, int n)
{
    int r = abs(rand());
    const int min_len = 3;
    return (n - 1) * ENTRY_SIZE + min_len + r % (first_cont_len - min_len - 1);
}

// entry length is 64 bytes

int main(int argc, char **argv)
{
    srand(time(NULL));

// 0. initialize
    printf("\n>>>0. INITIALIZE<<<\n");
    SFS *sfs = sfs_init("sfs_f.img");

// 1. create f1: 600 (10e)
    printf("\n>>>1. CREATE F1.1(10e)<<<\n");
    int f1namelen = get_length(FILE_NAME_LEN, 10);
    char f1name[f1namelen];
    new_name("F1.1", f1name, f1namelen + 1);
    printf("f1namelen=%d\n", f1namelen);
    if (sfs_create(sfs, (const char*) f1name) != 0) {
        fprintf(stderr, "error test 1: create f1\n");
        ERROR
    }
    if (sfs_resize(sfs, (const char*) f1name, 100) != 0) {
        fprintf(stderr, "error test 1: resize f1\n");
        ERROR
    }

// 2. delete f1
    printf("\n>>>2. DELETE F1.1(10e)<<<\n");
    if (sfs_delete(sfs, f1name) !=0) {
        fprintf(stderr, "error test 2: delete f1\n");
        ERROR
    }

// 3. create f2: (5e)
    printf("\n>>>3. CREATE F2.1(5e)<<<\n");
    int f2namelen = get_length(FILE_NAME_LEN, 5);
    char f2name[f2namelen];
    new_name("F2.1", f2name, f2namelen + 1);
    printf("f2namelen=%d\n", f2namelen);
    if (sfs_create(sfs, (const char*) f2name) != 0) {
        fprintf(stderr, "error test 3: create f2\n");
        ERROR
    }
    if (sfs_resize(sfs, (const char*) f2name, 100) != 0) {
        fprintf(stderr, "error test 3: resize f2\n");
        ERROR
    }

// 4. create f3: (3e)
    printf("\n>>>4. CREATE F3.1(3e)<<<\n");
    int f3namelen = get_length(FILE_NAME_LEN, 3);
    char f3name[f3namelen];
    new_name("F3.1", f3name, f3namelen + 1);
    printf("f3namelen=%d\n", f3namelen);
    if (sfs_create(sfs, (const char*) f3name) != 0) {
        fprintf(stderr, "error test 4: create f3\n");
        ERROR
    }
    if (sfs_resize(sfs, (const char*) f3name, 100) != 0) {
        fprintf(stderr, "error test 4: resize f3\n");
        ERROR
    }

// 5. resize f2: (2e)
    printf("\n>>>5. RENAME F2.1(5e) TO F2.2(2e)<<<\n");
    int f2name2len = get_length(FILE_NAME_LEN, 2);
    char f2name2[f2name2len];
    new_name("F2.2", f2name2, f2name2len + 1);
    if (sfs_rename(sfs, (const char*) f2name, (const char*) f2name2, 0) != 0) {
        fprintf(stderr, "error test 5: rename f2\n");
        ERROR
    }


// 6. resize f3: (1e)
    printf("\n>>>6. RENAME F3.1(3e) TO F3.2(1e)<<<\n");
    int f3name2len = get_length(FILE_NAME_LEN, 1);
    char f3name2[f3name2len];
    new_name("F3.2", f3name2, f3name2len + 1);
    if (sfs_rename(sfs, f3name, f3name2, 0) != 0) {
        fprintf(stderr, "error test 6: rename f3\n");
        ERROR
    }

// 7. create f4: (2e)
    printf("\n>>>7. CREATE F4.1 (2e)<<<\n");
    int f4namelen = get_length(FILE_NAME_LEN, 2);
    char f4name[f4namelen];
    new_name("F4.1", f4name, f4namelen + 1);
    printf("f4namelen=%d\n", f4namelen);
    if (sfs_create(sfs, (const char*) f4name) != 0) {
        fprintf(stderr, "error test 7: create f4\n");
        ERROR
    }
    if (sfs_resize(sfs, (const char*) f4name, 100) != 0) {
        fprintf(stderr, "error test 7: resize f4\n");
        ERROR
    }

// 8. resize f2: (4e)
    printf("\n>>>8. RENAME F2.2(2e) TO F2.3(4e)<<<\n");
    int f2name3len = get_length(FILE_NAME_LEN, 4);
    char f2name3[f2name3len];
    new_name("F2.3", f2name3, f2name3len + 1);
    if (sfs_rename(sfs, f2name2, f2name3, 0) != 0) {
        fprintf(stderr, "error test 8: rename f2\n");
        ERROR
    }

// 9. delete f4
    printf("\n>>>9. DELETE F4.1(2e)<<<\n");
    if (sfs_delete(sfs, f4name) !=0) {
        fprintf(stderr, "error test 9: delete f4\n");
        ERROR
    }

// 10. resize f3: (4e)
    printf("\n>>>10. RENAME F3.2(1e) TO F3.3(4e)<<<\n");
    int f3name3len = get_length(FILE_NAME_LEN, 4);
    char f3name3[f3name3len];
    new_name("F3.3", f3name3, f3name3len + 1);
    if (sfs_rename(sfs, f3name2, f3name3, 0) != 0) {
        fprintf(stderr, "error test 10: rename f3\n");
        ERROR
    }
//    EXIT

// 11. delete f2
    printf("\n>>>11. DELETE F2.4(5e)<<<\n");
    if (sfs_delete(sfs, f2name3) !=0) {
        fprintf(stderr, "error test 11: delete f2\n");
        ERROR
    }

// 12. delete f3
    printf("\n>>>12. DELETE F3.2(1e)<<<\n");
    if (sfs_delete(sfs, f3name3) !=0) {
        fprintf(stderr, "error test 12: delete f3\n");
        ERROR
    }

    EXIT
}
