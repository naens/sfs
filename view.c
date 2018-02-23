#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "sfs.h"

void print_super(struct S_SFS_SUPER *super) {
    printf("super:\n");
    printf("    time_stamp: %" PRIx64 "\n", super->time_stamp);
    printf("    data_size: %" PRIx64 "\n", super->data_size);
    printf("    index_size: %" PRIx64 "\n", super->index_size);
    printf("    magic:0x");
    for (int i = 0; i < 3; i++)
        printf("%02x", super->magic[i]);
    putchar('\n');
    printf("    version: %02x\n", super->version);
    printf("    total_blocks: %" PRIx64 "\n", super->total_blocks);
    printf("    rsvd_blocks: %" PRIx32 "\n", super->rsvd_blocks);
    printf("    block_size: %02x\n", super->block_size);
    printf("    crc: %02x\n", super->crc);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <image file name>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (f == NULL) {
        fprintf(stderr, "file error\n");
        return 2;
    }
    

    struct S_SFS_SUPER *super = get_super(f);
    print_super(super);
    /* TODO */
    free(super);
    return 0;
}
