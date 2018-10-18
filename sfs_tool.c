#include <stdio.h>

#include "sfs.h"

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage %s <file>\n", argv[0]);
        return 1;
    }

    char *filename = argv[1];
    SFS *sfs = sfs_init(filename);

    printf("######\n");

    sfs_terminate(sfs);
    return 0;
}
