#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "sfs.h"

void print_super(struct S_SFS_SUPER *super) {
    printf("super:\n");
    printf("    time_stamp: %" PRIx64 "\n", super->time_stamp);
    printf("    data_size: %" PRIx64 "\n", super->data_size);
    printf("    index_size: %" PRIx64 "\n", super->index_size);
    printf("    magic: 0x%02x%02x%02x\n", super->magic[0],
        super->magic[1], super->magic[2]);
    printf("    version: %02x\n", super->version);
    printf("    total_blocks: %" PRIx64 "\n", super->total_blocks);
    printf("    rsvd_blocks: %" PRIx32 "\n", super->rsvd_blocks);
    printf("    block_size: %02x\n", super->block_size);
    printf("    crc: %02x\n", super->crc);
}

void print_volume(struct S_SFS_VOL_ID *volume) {
    printf("volume:\n");
    printf("    type: %02x\n", volume->type);
    printf("    crc: %02x\n", volume->crc);
    printf("    resvd: %04x\n", volume->resvd);
    printf("    time_stamp: %" PRIx64 "\n", volume->time_stamp);
    printf("    name: %s\n", volume->name);
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

    struct S_SFS_VOL_ID *volume = get_volume(f);
    print_volume(volume);

    /* TODO: enter prompt: list all, view file ... */
    free(super);
    free(volume);
    return 0;
}
