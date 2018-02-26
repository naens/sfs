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

struct sfs {
    FILE *file;
    int block_size;
    struct S_SFS_SUPER *super;
    struct sfs_entry_list *entry_list;
};

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

union entry {
    struct S_SFS_VOL_ID *volume;
    struct S_SFS_START *start;
    struct S_SFS_UNUSED *unused;
    struct S_SFS_DIR *dir;
    struct S_SFS_FILE *file;
    struct S_SFS_UNUSABLE *unusable;
    struct S_SFS_DIR_DEL *dir_del;
    struct S_SFS_FILE_DEL *file_del;
    void *null;
};

struct sfs_entry_list {
    uint8_t type;
    union entry entry;
    struct sfs_entry_list *next;
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
    uint8_t num_cont;
    int64_t time_stamp;
    uint8_t *name;
};

/* The File Entry */
struct S_SFS_FILE {
    uint8_t type;
    uint8_t crc;
    uint8_t num_cont;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    uint8_t *name;
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
    uint8_t num_cont;
    int64_t time_stamp;
    uint8_t *name;
};

/* The Deleted File Entry */
struct S_SFS_FILE_DEL {
    uint8_t type;
    uint8_t crc;
    uint8_t num_cont;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    uint8_t *name;
};

struct sfs *sfs_make(char *filename);

struct S_SFS_SUPER *sfs_get_super(struct sfs *sfs);

struct S_SFS_VOL_ID *sfs_get_volume(struct sfs *sfs);

struct sfs_entry_list *sfs_get_entry_list(struct sfs *sfs);

void sfs_terminate(struct sfs *sfs);
