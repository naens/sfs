#include <stdint.h>
#include <stdio.h>

struct sfs {
    FILE *file;
    int block_size;
    int del_file_count;
    struct S_SFS_SUPER *super;
    struct sfs_entry_list *entry_list;
    struct sfs_del_file_list *del_file_list;
    struct sfs_del_file_list *del_file_tail;
    struct sfs_dirset_list **dirset;
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
    void *null;
};

struct sfs_entry_list {
    uint8_t type;
    union entry entry;
    struct sfs_entry_list *next;
};

struct sfs_del_file_list {
    struct S_SFS_FILE *file;
    struct sfs_del_file_list *next;
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
    long f_offset;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    FILE *file;
    char *file_buf;
    uint8_t *name;
    int del_number;
    struct sfs *sfs;
};

/* The Unusable Entry */
struct S_SFS_UNUSABLE {
    uint8_t type;
    uint8_t crc;
    long f_offset;
    uint8_t resv0[8];
    uint64_t start_block;
    uint64_t end_block;
    uint8_t resv1[38];
};

struct sfs *sfs_make(char *filename);

struct S_SFS_SUPER *sfs_get_super(struct sfs *sfs);

struct S_SFS_VOL_ID *sfs_get_volume(struct sfs *sfs);

struct sfs_entry_list *sfs_get_entry_list(struct sfs *sfs);

FILE* sfs_open_file(struct S_SFS_FILE *sfs_file);

void sfs_close_file(struct S_SFS_FILE *sfs_file);

void sfs_del_file(struct S_SFS_FILE *sfs_file);

struct S_SFS_FILE *sfs_write_file(struct sfs *sfs, char *name,
    char *buf, int *sz);

void sfs_terminate(struct sfs *sfs);

struct S_SFS_FILE *sfs_get_file_by_name(struct sfs *sfs, char *filename);
