#include <stdio.h>
#include <stdlib.h>

#include "sfs.h"

struct S_SFS_SUPER *get_super(FILE *f) {
    struct S_SFS_SUPER *super = malloc(sizeof(struct S_SFS_SUPER));

    if (fseek(f, 0x18e, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    fread(&super->time_stamp, sizeof(super->time_stamp), 1, f);
    fread(&super->data_size, sizeof(super->data_size), 1, f);
    fread(&super->index_size, sizeof(super->index_size), 1, f);
    fread(&super->magic, sizeof(super->magic), 1, f);
    fread(&super->version, sizeof(super->version), 1, f);
    fread(&super->total_blocks, sizeof(super->total_blocks), 1, f);
    fread(&super->rsvd_blocks, sizeof(super->rsvd_blocks), 1, f);
    fread(&super->block_size, sizeof(super->block_size), 1, f);
    fread(&super->crc, sizeof(super->crc), 1, f);

    uint8_t sum = 0;
    for (uint8_t *pbyte = &super->magic[0]; pbyte <= &super->crc; pbyte++)
        sum += *pbyte;
    
    if (sum) {
        fprintf(stderr, "crc error\n");
        return NULL;
    }
    return super;
}
