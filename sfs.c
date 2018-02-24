#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfs.h"

struct S_SFS_SUPER *super = NULL;
struct S_SFS_VOL_ID *volume = NULL;

int check_crc(uint8_t *buf, int sz) {
    uint8_t sum = 0;
    for (int i = 0; i < sz; i++)
        sum += buf[i];
    if (sum != 0) {
        fprintf(stderr, "crc error\n");
        return 0;
    }
    return 1;
}

struct S_SFS_SUPER *get_super(FILE *f) {
    if (super != NULL)
        return super;

    super = malloc(sizeof(struct S_SFS_SUPER));
    uint8_t buf[42];
    uint8_t *cbuf = buf;

    if (fseek(f, 0x18e, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    fread(buf, 42, 1, f);
    memcpy(&super->time_stamp, cbuf, sizeof(super->time_stamp));
    cbuf += sizeof(super->time_stamp);
    memcpy(&super->data_size, cbuf, sizeof(super->data_size));
    cbuf += sizeof(super->data_size);
    memcpy(&super->index_size, cbuf, sizeof(super->index_size));
    cbuf += sizeof(super->index_size);
    int m = cbuf - buf;
    memcpy(&super->magic, cbuf, sizeof(super->magic));
    cbuf += sizeof(super->magic);
    memcpy(&super->version, cbuf, sizeof(super->version));
    cbuf += sizeof(super->version);
    memcpy(&super->total_blocks, cbuf, sizeof(super->total_blocks));
    cbuf += sizeof(super->total_blocks);
    memcpy(&super->rsvd_blocks, cbuf, sizeof(super->rsvd_blocks));
    cbuf += sizeof(super->rsvd_blocks);
    memcpy(&super->block_size, cbuf, sizeof(super->block_size));
    cbuf += sizeof(super->block_size);
    memcpy(&super->crc, cbuf, sizeof(super->crc));

    printf("m=%d\n", m);
    if (!check_crc(buf + m, 42 - m)) {
        return NULL;
    }    
    return super;
}

struct S_SFS_VOL_ID *get_volume(FILE *f) {
    if (volume != NULL)
        return volume;

    volume = malloc(sizeof(struct S_SFS_VOL_ID));
    uint8_t buf[64];
    uint8_t *cbuf = buf;

    if (fseek(f, -64, SEEK_END) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    fread(buf, 64, 1, f);
    if (!check_crc(buf, 64)) {
        return NULL;
    }    
    memcpy(&volume->type, cbuf, sizeof(volume->type));
    cbuf += sizeof(volume->type);
    memcpy(&volume->crc, cbuf, sizeof(volume->crc));
    cbuf += sizeof(volume->crc);
    memcpy(&volume->resvd, cbuf, sizeof(volume->resvd));
    cbuf += sizeof(volume->resvd);
    memcpy(&volume->time_stamp, cbuf, sizeof(volume->time_stamp));
    cbuf += sizeof(volume->time_stamp);
    memcpy(volume->name, cbuf, 52);
    return volume;    
}
