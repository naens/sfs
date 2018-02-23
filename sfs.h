#include <stdint.h>
#include <stdio.h>

/* The Super Block */
struct S_SFS_SUPER {
    int64_t time_stamp;
    uint64_t data_size;
    uint64_t index_size;
    uint8_t magic[3];
    uint8_t version;
    uint64_t total_blocks;
    uint32_t rsvd_blocks;
    uint8_t block_size;
    uint8_t crc;
};

struct S_SFS_SUPER *get_super(FILE *f);
