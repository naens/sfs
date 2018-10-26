#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#include "sfs.h"

#define ENTRY_SIZE 64
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
int get_length(int n)
{
    int r = rand();
    return (n - 1) * ENTRY_SIZE + r % ENTRY_SIZE;
}

// entry length is 64 bytes

int main(int argc, char **argv)
{
    char name[1000];
    srand(time(NULL));

// 0. initialize
    SFS *sfs = sfs_init("sfs_f.img");

// 1. create f1: 600 (10e)
    int f1sz1 = get_length(10);
    new_name(name, f1sz1);
    printf("f1name=%s\n", name);
    if (sfs_create(sfs, (const char*) name) != 0) {
        fprintf(stderr, "error test 1: create f1\n");
        return -1;
    }
    goto end;

// 2. delete f1
// 3. create f2: 300 (5e)
// 4. create f3: 150 (3e)
// 5. resize f2: 80 (2e)
// 6. resize f3: 40 (1e)
// 7. create f4: 100 (2e)
// 8. resize f2: 200 (4e)
// 9. delete f4
// 10. resize f2: 315 (5e)
// 11. delete f2
// 12. delete f3


end:
    sfs_terminate(sfs);
    return 0; 
}
