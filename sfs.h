#include <stdint.h>
#include <stdio.h>

/* Index Data Area Entry Types */
#define SFS_ENTRY_VOL_ID 0x01
#define SFS_ENTRY_START 0x02
#define SFS_ENTRY_UNUSED 0x10
#define SFS_ENTRY_DIR 0x11
#define SFS_ENTRY_FILE 0x12
#define SFS_ENTRY_UNUSABLE 0x18
#define SFS_ENTRY_DIR_DEL 0x19
#define SFS_ENTRY_FILE_DEL 0x1A

#define DIR_NAME_LEN 53
#define FILE_NAME_LEN 29

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

/* The Volume ID Entry */
struct S_SFS_VOL_ID {
    uint8_t type;
    uint8_t crc;
    uint16_t resvd;
    int64_t time_stamp;
    uint8_t name[52];
};

/* The Start Marker Entry */
struct S_SFS_START {
    uint8_t type;
    uint8_t crc;
    uint8_t resvd[62];
};

/* The Unused Entry */
struct S_SFS_UNUSED {
    uint8_t type;
    uint8_t crc;
    uint8_t resvd[62];
};

/* The Directory Entry */
struct S_SFS_DIR {
    uint8_t type;
    uint8_t crc;
    uint8_t num_count;
    int64_t time_stamp;
    uint8_t name[DIR_NAME_LEN];
};

/* The File Entry */
struct S_SFS_FILE {
    uint8_t type;
    uint8_t crc;
    uint8_t num_count;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    uint8_t name[FILE_NAME_LEN];
};

/* The Unusable Entry */
struct S_SFS_UNUSABLE {
    uint8_t type;
    uint8_t crc;
    uint8_t resv0[8];
    uint64_t start_block;
    uint64_t end_block;
    uint8_t resv1[38];
};

/* The Deleted Directory Entry */
struct S_SFS_DIR_DEL {
    uint8_t type;
    uint8_t crc;
    uint8_t num_count;
    int64_t time_stamp;
    uint8_t name[DIR_NAME_LEN];
};

/* The Deleted File Entry */
struct S_SFS_FILE_DEL {
    uint8_t type;
    uint8_t crc;
    uint8_t num_count;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    uint8_t name[FILE_NAME_LEN];
};

struct S_SFS_SUPER *get_super(FILE *f);
struct S_SFS_VOL_ID *get_volume(FILE *f);
