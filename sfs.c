#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "sfs.h"

/* Index Data Area Entry Types */
#define SFS_ENTRY_VOL_ID 0x01
#define SFS_ENTRY_START 0x02
#define SFS_ENTRY_UNUSED 0x10
#define SFS_ENTRY_DIR 0x11
#define SFS_ENTRY_FILE 0x12
#define SFS_ENTRY_UNUSABLE 0x18
#define SFS_ENTRY_DIR_DEL 0x19
#define SFS_ENTRY_FILE_DEL 0x1A

#define SFS_VERSION 0x11

#define SFS_SUPER_START 0x18e
#define SFS_SUPER_SIZE 42
#define SFS_VOL_NAME_LEN 52
#define SFS_ENTRY_SIZE 64
#define SFS_DIR_NAME_LEN 53
#define SFS_FILE_NAME_LEN 29

struct sfs_super {
    int64_t time_stamp;
    uint64_t data_size;
    uint64_t index_size;
    uint64_t total_blocks;
    uint32_t rsvd_blocks;
    uint8_t block_size;
};

struct sfs_block_list {
    uint64_t start_block;
    uint64_t length;
    struct sfs_entry *delfile;
    struct sfs_block_list *next;
};

struct sfs {
    FILE *file;
    int block_size;
    int del_file_count;
    struct sfs_super *super;
    struct sfs_entry *volume;
    struct sfs_entry *entry_list;
    struct sfs_block_list *free_list;
    struct sfs_block_list *free_last;
    struct sfs_entry *iter_curr;
};

/* The Volume ID Entry Data */
struct sfs_vol {
    int64_t time_stamp;
    char *name;
};

/* The Directory Entry Data */
struct sfs_dir {
    uint8_t num_cont;
    int64_t time_stamp;
    char *name;
};

/* The File Entry Data */
struct sfs_file {
    uint8_t num_cont;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    char *name;
};

/* The Unusable Entry Data */
struct sfs_unusable {
    uint64_t start_block;
    uint64_t end_block;
};

struct sfs_entry {
    uint8_t type;
    long int offset;
    union {
        struct sfs_vol *volume_data;
        struct sfs_dir *dir_data;
        struct sfs_file *file_data;
        struct sfs_unusable *unusable_data;
    } data;
    struct sfs_entry *next;
};

/* TODO: old */
union entry {
    struct S_SFS_VOL_ID *volume;
    struct S_SFS_START *start;
    struct S_SFS_UNUSED *unused;
    struct S_SFS_DIR *dir;
    struct S_SFS_FILE *file;
    struct S_SFS_UNUSABLE *unusable;
    void *null;
};

/* TODO: old */
struct sfs_entry_list {
    uint8_t type;
    union entry entry;
    struct sfs_entry_list *next;
};

/* TODO: old */
struct sfs_del_file_list {
    struct S_SFS_FILE *file;
    struct sfs_del_file_list *next;
};

/* The Volume ID Entry */
struct sfs_volume {
    uint8_t type;
    uint8_t crc;
    uint16_t resvd;
    int64_t time_stamp;
    uint8_t name[SFS_VOL_NAME_LEN];
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
    uint8_t *name;
    int64_t time_stamp;
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

uint64_t sfs_make_time_stamp()
{
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    time_t s = spec.tv_sec;
    long n = spec.tv_nsec;
    // timestamp = secs * 65536
    // (n / 1000000000 + s) * 65536
    // n*128/1953125 + 65536*s
    uint64_t timestamp = round((n << 7) / 1953125.0) + (s << 16);
    return timestamp;
}

int check_crc(uint8_t *buf, int sz)
{
    uint8_t sum = 0;
    for (int i = 0; i < sz; i++)
        sum += buf[i];
    if (sum != 0) {
        fprintf(stderr, "crc error\n");
        return 0;
    }
    return 1;
}

struct sfs_super *sfs_read_super(struct sfs *sfs)
{
    sfs->super = malloc(sizeof(struct sfs_super));
    uint8_t buf[SFS_SUPER_SIZE];
    uint8_t *cbuf = buf;

    if (fseek(sfs->file, SFS_SUPER_START, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    fread(buf, SFS_SUPER_SIZE, 1, sfs->file);
    memcpy(&sfs->super->time_stamp, cbuf, sizeof(sfs->super->time_stamp));
    cbuf += sizeof(sfs->super->time_stamp);
    memcpy(&sfs->super->data_size, cbuf, sizeof(sfs->super->data_size));
    cbuf += sizeof(sfs->super->data_size);
    memcpy(&sfs->super->index_size, cbuf, sizeof(sfs->super->index_size));
    cbuf += sizeof(sfs->super->index_size);
    int m = cbuf - buf;

    char magic[3];
    memcpy(&magic, cbuf, 3);
    cbuf += 3;
    char version = *cbuf;
    cbuf += 1;
    if (strncmp(magic, "SFS", 3) != 0 || version != SFS_VERSION) {
        printf("!!!magic=%c%c%c, version=0x%x\n", magic[0], magic[1], magic[2], version);
        return NULL;
    }    

    memcpy(&sfs->super->total_blocks, cbuf, sizeof(sfs->super->total_blocks));
    cbuf += sizeof(sfs->super->total_blocks);
    memcpy(&sfs->super->rsvd_blocks, cbuf, sizeof(sfs->super->rsvd_blocks));
    cbuf += sizeof(sfs->super->rsvd_blocks);
    memcpy(&sfs->super->block_size, cbuf, sizeof(sfs->super->block_size));
    cbuf += sizeof(sfs->super->block_size);
    sfs->block_size = 1 << (sfs->super->block_size + 7);

    if (!check_crc(buf + m, SFS_SUPER_SIZE - m)) {
        return NULL;
    }    
    return sfs->super;
}

void sfs_write_super(FILE *file, struct sfs_super *super) {
    char buf[SFS_SUPER_SIZE];
    memcpy(&buf[0], &super->time_stamp, 8);
    memcpy(&buf[8], &super->data_size, 8);
    memcpy(&buf[16], &super->index_size, 8);
    memcpy(&buf[24], "SFS", 3);
    buf[27] = SFS_VERSION;
    memcpy(&buf[28], &super->total_blocks, 8);
    memcpy(&buf[36], &super->rsvd_blocks, 4);
    memcpy(&buf[40], &super->block_size, 1);
    int sum = 0;
    for (int i = 24; i < SFS_SUPER_SIZE - 1; ++i) {
        sum += buf[i];
    }
    buf[41] = 0x100 - (char)(sum % 0x100);
    fseek(file, SFS_SUPER_START, SEEK_SET);
    fwrite(buf, SFS_SUPER_SIZE, 1, file);
}

struct sfs_entry *sfs_read_volume_data(uint8_t *buf, struct sfs_entry *entry)
{
    struct sfs_vol *volume_data = malloc(sizeof(struct sfs_vol));
    uint8_t *cbuf = buf + 4; /* skip type, crc, and resvd */
    memcpy(&volume_data->time_stamp, cbuf, sizeof(volume_data->time_stamp));
    cbuf += sizeof(volume_data->time_stamp);
    volume_data->name = malloc(SFS_VOL_NAME_LEN);
    memcpy(volume_data->name, cbuf, SFS_VOL_NAME_LEN);
    entry->data.volume_data = volume_data;
    if (!check_crc(buf, SFS_ENTRY_SIZE)) {
        return NULL;
    }
    return entry;
}

struct sfs_entry *sfs_read_dir_data(uint8_t *buf, struct sfs_entry *entry, FILE *file)
{
    uint8_t *b = buf;
    struct sfs_dir *dir_data = malloc(sizeof(struct sfs_dir));

    memcpy(&dir_data->num_cont, &buf[2], 1);
    memcpy(&dir_data->time_stamp, &buf[3], 8);

    const int cont_len = dir_data->num_cont * SFS_ENTRY_SIZE;
    const int name_len = SFS_DIR_NAME_LEN + cont_len;
    printf("dir: name_len=%d ", name_len);

    dir_data->name = malloc(name_len);
    int bufsz = SFS_ENTRY_SIZE * (1 + dir_data->num_cont);
    uint8_t buf2[bufsz];
    if (dir_data->num_cont != 0) {
        b = buf2;
        memcpy(buf2, buf, SFS_ENTRY_SIZE);  /* copy from old buffer to new */
        fread(buf2 + SFS_ENTRY_SIZE, cont_len, 1, file); /* copy into new */
    }
    memcpy(dir_data->name, &b[11], name_len);
    if (!check_crc(b, bufsz)) {
        return NULL;
    }
    entry->data.dir_data = dir_data;
    printf(" name=%s\n", dir_data->name);
    return entry;   
}

struct sfs_entry *sfs_read_file_data(uint8_t *buf, struct sfs_entry *entry, FILE *file)
{
    uint8_t *b = buf;
    struct sfs_file *file_data = malloc(sizeof(struct sfs_file));

    memcpy(&file_data->num_cont, &buf[2], 1);
    memcpy(&file_data->time_stamp, &buf[3], 8);
    memcpy(&file_data->start_block, &buf[11], 8);
    memcpy(&file_data->end_block, &buf[19], 8);
    memcpy(&file_data->file_len, &buf[27], 8);

    const int cont_len = file_data->num_cont * SFS_ENTRY_SIZE;
    const int name_len = SFS_FILE_NAME_LEN + cont_len;
    printf("file: name_len=%d ", name_len);

    file_data->name = malloc(name_len);
    int bufsz = SFS_ENTRY_SIZE * (1 + file_data->num_cont);
    uint8_t buf2[bufsz];
    if (file_data->num_cont != 0) {
        b = buf2;
        memcpy(buf2, buf, SFS_ENTRY_SIZE);  /* copy from old buffer to new */
        fread(buf2 + SFS_ENTRY_SIZE, cont_len, 1, file); /* copy into new */
    }
    memcpy(file_data->name, &b[35], name_len);
    if (!check_crc(b, bufsz)) {
        return NULL;
    }
    entry->data.file_data = file_data;
    printf("name=%s\n", file_data->name);
    return entry;   
}

struct sfs_entry *sfs_read_unusable_data(uint8_t *buf, struct sfs_entry *entry)
{
    struct sfs_unusable *unusable_data = malloc(sizeof(struct sfs_unusable));
    memcpy(&unusable_data->start_block, &buf[10], 8);
    memcpy(&unusable_data->end_block, &buf[18], 8);
    entry->data.unusable_data = unusable_data;
    if (!check_crc(buf, SFS_ENTRY_SIZE)) {
        return NULL;
    }
    return entry;
}

/* read entry, at current location */
struct sfs_entry *sfs_read_entry(SFS *sfs)
{

    uint8_t buf[SFS_ENTRY_SIZE];

    struct sfs_entry *entry = malloc(sizeof(struct sfs_entry));
    entry->offset = ftell(sfs->file);
    if (entry->offset == -1) {
        return NULL;
    }
    fread(buf, SFS_ENTRY_SIZE, 1, sfs->file);
    entry->type = buf[0];
    entry->next = NULL;
    switch (entry->type) {
    case SFS_ENTRY_VOL_ID:
        return sfs_read_volume_data(buf, entry);
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        return sfs_read_dir_data(buf, entry, sfs->file);
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        return sfs_read_file_data(buf, entry, sfs->file);
    case SFS_ENTRY_UNUSABLE:
        return sfs_read_unusable_data(buf, entry);
    default:
        printf("entry->type=%x\n", entry->type);
        return entry;
    }
}

struct sfs_entry *sfs_read_volume(SFS *sfs)
{
    int vol_offset = sfs->block_size * sfs->super->total_blocks - SFS_ENTRY_SIZE;
    if (fseek(sfs->file, vol_offset, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    struct sfs_entry *volume = sfs_read_entry(sfs);
    if (volume->type != SFS_ENTRY_VOL_ID) {
        return NULL;
    }

    return volume;
}

struct sfs_entry *sfs_read_entries(SFS *sfs)
{
    int offset = sfs->block_size * sfs->super->total_blocks - sfs->super->index_size;
    printf("bs=0x%x, tt=0x%lxH, is=0x%lx, of=0x%x\n",
        sfs->block_size, sfs->super->total_blocks, sfs->super->index_size, offset);
    if (fseek(sfs->file, offset, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }
    struct sfs_entry *entry = sfs_read_entry(sfs);
    struct sfs_entry *head = entry;
    while (entry->type != SFS_ENTRY_VOL_ID) {
        struct sfs_entry *prev = entry;
        entry = sfs_read_entry(sfs);
        prev->next = entry;
    }
    sfs->volume = entry;
    return head;
}

struct sfs_block_list *block_list_from_entries(struct sfs_entry *entry_list)
{
    struct sfs_block_list *list = NULL;
    struct sfs_entry *entry = entry_list;
    while (entry != NULL) {
        if (entry->type == SFS_ENTRY_FILE
                || entry->type == SFS_ENTRY_FILE_DEL
                || entry->type == SFS_ENTRY_UNUSABLE) {
            struct sfs_block_list *item = malloc(sizeof(struct sfs_block_list));
            item->start_block = entry->data.unusable_data->start_block;
            item->next = list;
            list = item;
            switch (entry->type) {
            case SFS_ENTRY_FILE:
                item->start_block = entry->data.file_data->start_block;
                item->length = entry->data.file_data->end_block + 1 - item->start_block;
                item->delfile = NULL;
                break;
            case SFS_ENTRY_UNUSABLE:
                item->start_block = entry->data.unusable_data->start_block;
                item->length = entry->data.unusable_data->end_block + 1 - item->start_block;
                item->delfile = NULL;
                break;
            case SFS_ENTRY_FILE_DEL:
                item->start_block = entry->data.file_data->start_block;
                item->length = entry->data.file_data->end_block + 1 - item->start_block;
                item->delfile = entry;
                break;
            }
        }
        entry = entry->next;
    }
    return list;
}

struct sfs_block_list **conquer(struct sfs_block_list **p1, struct sfs_block_list **p2, int sz)
{
    int i1 = 0;
    int i2 = 0;
    while (i1 != sz || (i2 != sz && (*p2) != NULL)) {
        if ((i1 != sz) &&
                (*p2 == NULL
                 || i2 == sz
                 || (*p1)->start_block <= (*p2)->start_block)) {
            i1++;
            p1 = &(*p1)->next;
        } else {
            i2++;
            if (i1 != sz) {
                struct sfs_block_list *tmp = *p2;
                (*p2) = tmp->next;
                tmp->next = (*p1);
                *p1 = tmp;
                p1 = &(*p1)->next;
            } else {
                p2 = &(*p2)->next;
            }
        }
    }
    return p2;  // return the tail
}

void print_block_list(char *info, struct sfs_block_list *list)
{
    printf("%s", info);
    while (list != NULL) {
        char *d = list->delfile == NULL ? "" : "d";
        printf("(%s%03lx,%03lx) ", d, list->start_block, list->length);
        list = list->next;
    }
    printf("\n");
}

void sort_block_list(struct sfs_block_list **plist)
{
    int sz = 1;
    int n;
    do {
        n = 0;
        struct sfs_block_list **plist1 = plist;
        while (*plist1 != NULL) {
            n = n + 1;
            struct sfs_block_list **plist2 = plist1;
            int i = 0;
            plist2 = plist1;
            while (i < sz && *plist2 != NULL) {
                i++;
                plist2 = &(*plist2)->next;
            }
            plist1 = conquer(plist1, plist2, sz);
        }
        sz = sz * 2;
    } while (n > 1);
}

void block_list_to_free_list(plist, first_block, total_blocks, free_last)
    struct sfs_block_list **plist;
    uint64_t first_block;
    uint64_t total_blocks;
    struct sfs_block_list **free_last;
{
    if (*plist == NULL) {
        uint64_t gap = total_blocks - first_block;
        struct sfs_block_list *item = malloc(sizeof(struct sfs_block_list));
        item->start_block = first_block;
        item->length = gap;
        item->delfile = NULL;
        item->next = NULL;
        *plist = item;
        return;
    }

    /* if gap before the first block, insert one block structure */
    struct sfs_block_list **pprev;
    struct sfs_block_list *curr;
    if (first_block < (*plist)->start_block) {
        struct sfs_block_list *b0 = malloc(sizeof(struct sfs_block_list));
        b0->start_block = first_block;
        b0->length = (*plist)->start_block - first_block;
        b0->next =  (*plist);
        b0->delfile = NULL;
        *plist = b0;
        pprev = &(*plist)->next;
    } else {
        pprev = plist;
    }
    curr = (*pprev)->next;

    while ((*pprev) != NULL) {
        uint64_t prev_end = (*pprev)->start_block + (*pprev)->length;
        uint64_t gap = (curr != 0 ? curr->start_block : total_blocks) - prev_end;
        if ((*pprev)->delfile == NULL) {
            if (gap == 0) {
                struct sfs_block_list *tmp = (*pprev);
                *pprev = tmp->next;
                if ((*pprev)->next == NULL) {
                    *free_last = NULL;
                }
                free(tmp);
            } else {
                (*pprev)->start_block += (*pprev)->length;
                (*pprev)->length = gap;
                if ((*pprev)->next == NULL) {
                    *free_last = *pprev;
                }
                pprev = &(*pprev)->next;
            }
        } else {
            if (gap > 0) {
                struct sfs_block_list *item = malloc(sizeof(struct sfs_block_list));
                item->start_block = (*pprev)->start_block + (*pprev)->length;
                item->length = gap;
                item->next = curr;
                item->delfile = NULL;
                (*pprev)->next = item;
                pprev = &item->next;
            } else {
                pprev = &(*pprev)->next;
            }
            if ((*pprev)->next == NULL) {
                *free_last = *pprev;
            }
        }
        if (curr != NULL) {
            curr = curr->next;
        }
    }
}

struct sfs_block_list *make_free_list(sfs, entry_list, free_last)
    struct sfs *sfs;
    struct sfs_entry *entry_list;
    struct sfs_block_list **free_last;
{
    struct sfs_super *super = sfs->super;
    struct sfs_block_list *block_list = block_list_from_entries(entry_list);
    print_block_list("not sorted: ", block_list);

    sort_block_list(&block_list);
    print_block_list("sorted:     ", block_list);

/*
    uint64_t data_size;
    uint64_t total_blocks;
    uint32_t rsvd_blocks;
*/
    uint64_t first_block = super->rsvd_blocks;  // !! includes the superblock
    uint64_t data_blocks = super->total_blocks;
    block_list_to_free_list(&block_list, first_block, data_blocks, free_last);
    print_block_list("free:     ", block_list);

    return block_list;
}

SFS *sfs_init(const char *filename)
{
    SFS *sfs = malloc(sizeof(SFS));
    sfs->file = fopen(filename, "r+");
    if (sfs->file == NULL) {
        perror("sfs_init error");
        fprintf(stderr, "sfs_init: file error \"%s\"\n", filename);
        return NULL;
    }
    sfs->super = sfs_read_super(sfs);
    if (sfs->super == NULL) {
        fprintf(stderr, "sfs_init: error reading the superblock\n");
        exit(7);
    }
    sfs->entry_list = sfs_read_entries(sfs);
    sfs->free_last = NULL;
    sfs->free_list = make_free_list(sfs, sfs->entry_list, &sfs->free_last);
    if (sfs->free_last == NULL) {
        fprintf(stderr, "sfs_init: error: free_last is null\n");
        exit(7);
    }
    return sfs;
}

void free_entry(struct sfs_entry *entry)
{
    printf("freeing: %x\n", entry->type);
    switch (entry->type) {
    case SFS_ENTRY_VOL_ID:
        free(entry->data.volume_data->name);
        free(entry->data.volume_data);
        break;
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        free(entry->data.dir_data->name);
        free(entry->data.dir_data);
        break;
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        free(entry->data.file_data->name);
        free(entry->data.file_data);
        break;
    case SFS_ENTRY_UNUSABLE:
        free(entry->data.unusable_data);
        break;
    default:
        break;
    }
    free(entry);
}

void free_entry_list(struct sfs_entry *entry_list)
{
    struct sfs_entry *prev = entry_list;
    struct sfs_entry *curr = prev->next;
    while (curr != NULL) {
        free_entry(prev);
        prev = curr;
        curr = curr->next;
    }
    free_entry(prev);
}

void free_free_list(struct sfs_block_list *free_list)
{
    struct sfs_block_list *prev = free_list;
    struct sfs_block_list *curr = prev->next;
    while (curr != NULL) {
        free(prev);
        prev = curr;
        curr = curr->next;
    }
    free(prev);
}

int sfs_terminate(SFS *sfs)
{
    printf("free sfs\n");
    // free entry list
    free_entry_list(sfs->entry_list);
    free_free_list(sfs->free_list);
    free(sfs->super);
    fclose(sfs->file);
    free(sfs);
    return 0;
}

char *fix_name(const char *path)
{
    char *result = (char *)path;
    while (*result == '/')
        result++;
    return result;
}

uint64_t sfs_get_file_size(SFS *sfs, const char *path)
{
    char *fxpath = fix_name(path);
    struct sfs_entry *entry = sfs->entry_list;
    while (entry != NULL) {
        if (entry->type == SFS_ENTRY_FILE
                && strcmp(fxpath, entry->data.file_data->name) == 0) {
            return entry->data.file_data->file_len;
        }
        entry = entry->next;
    }
    return 0;
}

struct sfs_entry *get_dir_by_name(SFS *sfs, char *path) {
    struct sfs_entry *entry = sfs->entry_list;
    while (entry != NULL) {
        if (entry->type == SFS_ENTRY_DIR
                && strcmp(path, entry->data.dir_data->name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

struct sfs_entry *get_file_by_name(SFS *sfs, char *path) {
    struct sfs_entry *entry = sfs->entry_list;
    while (entry != NULL) {
        if (entry->type == SFS_ENTRY_FILE
                && strcmp(path, entry->data.file_data->name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

int sfs_is_dir(SFS *sfs, const char *path)
{
    char *fxpath = fix_name(path);
//    printf("@@@@\tsfs_is_dir: name=\"%s\"\n", fxpath);
    struct sfs_entry *entry = get_dir_by_name(sfs, fxpath);
    if (entry != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int sfs_is_file(SFS *sfs, const char *path)
{
    char *fxpath = fix_name(path);
//    printf("@@@@\tsfs_is_file: name=\"%s\"\n", fxpath);
    struct sfs_entry *entry = get_file_by_name(sfs, fxpath);
    if (entry != NULL) {
        return 1;
    } else {
        return 0;
    }
}

struct sfs_entry *find_entry_from(struct sfs_entry *entry, char *path)
{
    int len = strlen(path);
    while (entry != NULL) {
        if (entry->type == SFS_ENTRY_DIR) {
            char *name = entry->data.dir_data->name;
            int name_len = strlen(name);
            if ((len == 0 || (name_len > len + 1 && name[len] == '/'))
                    && strncmp(path, name, len) == 0 && strchr(&name[len+1], '/') == NULL) {
                return entry;
            }
        } else if (entry->type == SFS_ENTRY_FILE) {
            char *name = entry->data.file_data->name;
            int name_len = strlen(name);
            if ((len == 0 || (name_len > len + 1 && name[len] == '/'))
                    && strncmp(path, name, len) == 0 && strchr(&name[len+1], '/') == NULL) {
                return entry;
            }
        }
        entry = entry->next;
    }
    return NULL;
}

char *get_basename(char *full_name)
{
    char *p = full_name;
    int i = 0;
    int last_slash = -1;
    while (p[i] != 0) {
        if (p[i] == '/') {
            last_slash = i;
        }
        i = i + 1;
    }
    return &p[last_slash + 1];
}

char *get_entry_basename(struct sfs_entry *entry)
{
    switch (entry->type) {
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        return get_basename((char*)entry->data.dir_data->name);
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        return get_basename((char*)entry->data.file_data->name);
    default:
        return NULL;
    }
}

char *sfs_first(SFS *sfs, const char *path)
{
    char *fxpath = fix_name(path);
    struct sfs_entry *entry = find_entry_from(sfs->entry_list, fxpath);
    if (entry != NULL) {
        sfs->iter_curr = entry->next;
        return get_entry_basename(entry);
    }
    sfs->iter_curr = NULL;
    return NULL;
}

char *sfs_next(SFS *sfs, const char *path)
{
    char *fxpath = fix_name(path);
    struct sfs_entry *entry = find_entry_from(sfs->iter_curr, fxpath);
    if (entry != NULL) {
        sfs->iter_curr = entry->next;
        return get_entry_basename(entry);
    }
    sfs->iter_curr = NULL;
    return NULL;
}

int sfs_read(SFS *sfs, const char *path, char *buf, size_t size, off_t offset)
{
    char *fxpath = fix_name(path);
//    printf("@@@@\tsfs_read: path=\"%s\", size:0x%lx, offset:0x%lx\n", fxpath, size, offset);
    struct sfs_entry *entry = get_file_by_name(sfs, fxpath);
    if (entry != NULL) {
        uint64_t sz;		// number of bytes to be read
        uint64_t len = entry->data.file_data->file_len;
        if (offset > len) {
            return 0;
        }
        if (offset + size > len) {
            sz = len - offset;
        } else {
            sz = size;
        }
        uint64_t data_offset = sfs->block_size * entry->data.file_data->start_block;
        uint64_t read_from = data_offset + offset;
/*
        printf("\tstart:\t0x%06lx\n", data_offset);
        printf("\tend:\t0x%06lx\n", data_offset + len);
        printf("\tfilesz:\t0x%06lx\n", len);
        printf("\tfrom:\t0x%06lx\n", read_from);
        printf("\tto:\t0x%06lx\n", read_from + sz);
        printf("\treadsz:\t0x%06lx\n", sz);
*/
        fseek(sfs->file, read_from, SEEK_SET);
        fread(buf, sz, 1, sfs->file);
        return sz;
    } else {
        return -1;
    }
}

int get_num_cont(struct sfs_entry *entry)
{
    switch (entry->type) {
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        return entry->data.dir_data->num_cont;
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        return entry->data.file_data->num_cont;
    default:
        return 0;
    }
}

int get_entry_usable_space(struct sfs_entry *entry)
{
    switch (entry->type) {
    case SFS_ENTRY_DIR_DEL:
        return 1 + entry->data.dir_data->num_cont;
    case SFS_ENTRY_FILE_DEL:
        return 1 + entry->data.file_data->num_cont;
    case SFS_ENTRY_UNUSED:
        return 1;
    default:
        return 0;
    }
}

void write_volume_data(char *buf, struct sfs_vol *vol_data)
{
    memcpy(&buf[4], &vol_data->time_stamp, 8);
    strncpy(&buf[12], vol_data->name, SFS_VOL_NAME_LEN);
}

void write_dir_data(char *buf, struct sfs_dir *dir_data)
{
    memcpy(&buf[2], &dir_data->num_cont, 1);
    memcpy(&buf[3], &dir_data->time_stamp, 8);
    uint64_t max_len = SFS_DIR_NAME_LEN + SFS_ENTRY_SIZE * dir_data->num_cont;
    strncpy(&buf[11], dir_data->name, max_len);
}

void write_file_data(char *buf, struct sfs_file *file_data)
{
    memcpy(&buf[2], &file_data->num_cont, 1);
    memcpy(&buf[3], &file_data->time_stamp, 8);
    memcpy(&buf[11], &file_data->start_block, 8);
    memcpy(&buf[19], &file_data->end_block, 8);
    memcpy(&buf[27], &file_data->file_len, 8);
    uint64_t max_len = SFS_FILE_NAME_LEN + SFS_ENTRY_SIZE * file_data->num_cont;
    strncpy(&buf[35], file_data->name, max_len);
}

void write_unusable_data(char *buf, struct sfs_unusable *unusable_data)
{
    memcpy(&buf[10], &unusable_data->start_block, 8);
    memcpy(&buf[18], &unusable_data->end_block, 8);
}


/* Writes the entry (with its continuations) to the Index Area.
 * Returns 0 on success and -1 on error.
 */
int write_entry(SFS *sfs, struct sfs_entry *entry)
{
    printf("=== WRITING ENTRY ===\n");
    int num_cont = get_num_cont(entry);
    int size = (1 + num_cont) * SFS_ENTRY_SIZE;
    char buf[size];
    memset(buf, 0, size);
    buf[0] = entry->type;
    buf[1] = 0;
    switch (entry->type) {
    case SFS_ENTRY_VOL_ID:
        write_volume_data(buf, entry->data.volume_data);
        break;
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        write_dir_data(buf, entry->data.dir_data);
        break;
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        write_file_data(buf, entry->data.file_data);
        break;
    case SFS_ENTRY_UNUSABLE:
        write_unusable_data(buf, entry->data.unusable_data);
        break;
    case SFS_ENTRY_START:
    case SFS_ENTRY_UNUSED:
        break;
    default:
        fprintf(stderr, "write_entry error: unknown entry type: 0x%02x\n", entry->type);
        printf("=== WRITING ENTRY: ERROR ===\n");
        return -1;
    }
    int sum = 0;
    for (int i = 0; i < size; ++i) {
        sum += buf[i];
    }
    buf[1] = 0x100 - sum % 0x100;

    printf("writing %d bytes at 0x%06lx\n", size, entry->offset);
    if (fseek(sfs->file, entry->offset, SEEK_SET) == -1) {
        fprintf(stderr, "write_entry error: couldn't fseek to %06lx\n", entry->offset);
        printf("=== WRITING ENTRY: ERROR ===\n");
        return -1;
    }
    int tmp;
    if ((tmp=fwrite(buf, size, 1, sfs->file)) != 1) {
        fprintf(stderr, "write_entry error: written %d bytes instead of %d bytes\n", tmp, size);
        printf("=== WRITING ENTRY: ERROR ===\n");
        return -1;
    }
    printf("=== WRITING ENTRY: OK ===\n");
    return 0;
}

void delete_entries(struct sfs_entry *from, struct sfs_entry *to)
{
    while (from != to) {
        struct sfs_entry *tmp = from;
        from = from->next;
        free_entry(tmp);
    }
}

/* Insert n unused entries before tail and returns the head */
struct sfs_entry *insert_unused(sfs, offset, n, tail)
    struct sfs *sfs;
    uint64_t offset;
    int n;
    struct sfs_entry *tail;
{
    struct sfs_entry *entry;
    struct sfs_entry *next = tail;
    for (int i = 0; i < n; ++i) {
        entry = malloc(sizeof(struct sfs_entry));
        entry->offset = offset + SFS_ENTRY_SIZE * (n - i - 1);
        entry->type = SFS_ENTRY_UNUSED;
        entry->next = next;
        if (write_entry(sfs, entry) != 0) {
            return NULL;
        }
        next = entry;
    }
    return entry;
}

/* Finds space for the entry and inserts it.
 * Writes changes to the Index Area.
 * Return 0 on success, -1 on error
 *
 * k: amount of space needed
 * l: amount of consecutive space found so that (l >= k)
 * The entries taking l space are deleted.
 * The new entry is inserted.
 * The remaining (l - k) is filled with unused entries.
 * If the space could not be found, -1 is returned, otherwise 0 is returned.
 */
int insert_entry(struct sfs *sfs, struct sfs_entry *entry_list, struct sfs_entry *new_entry)
{
    printf("=== INSERT ENTRY ===\n");
    int space_needed = 1 + get_num_cont(new_entry); // in number of simple entries
    printf("\tneeded: %d\n", space_needed);
    int space_found = 0;
    struct sfs_entry *curr_entry = entry_list;
    struct sfs_entry *first_usable = NULL;
    while (curr_entry != NULL) {
//        printf("\ttype=0x%02x\n", curr_entry->type);
        int usable_space = get_entry_usable_space(curr_entry);
//        printf("\tusable: %d\n", usable_space);
        if (usable_space > 0) {
            if (first_usable == NULL) {
                first_usable = curr_entry;
                space_found += usable_space;
                printf("\tfound: %d\n", space_found);
            }
            if (space_found >= space_needed) {
                int start = first_usable->offset;
                int end = start + SFS_ENTRY_SIZE * space_needed;
                struct sfs_entry *next = curr_entry->next;
                delete_entries(first_usable, next);
                new_entry->offset = start;
                int l = space_found - space_needed;
                new_entry->next = insert_unused(sfs, end, l, next);
                printf("=== INSERT ENTRY: OK ===\n");
                return 0;
            }
        } else if (first_usable != NULL) {
            first_usable = NULL;
            space_found = 0;
        }
        curr_entry = curr_entry->next;
    }
    printf("insert_entry: couldn't find (%d * 64) bytes\n", space_needed);
    printf("=== INSERT ENTRY: ERROR ===\n");
    return -1;
}

/* Prepends the entry to the list of entries.
 * The entry is inserted after the start marker.
 * In the Index Area, the start marker is moved in the direction of the superblock.
 * The entry is written to the Index Area after the start marker.
 * On success 0 is returned, -1 on error.
 */
int prepend_entry(struct sfs *sfs, struct sfs_entry *entry)
{
    struct sfs_entry *start = sfs->entry_list;
    int entry_size = SFS_ENTRY_SIZE * (1 + get_num_cont(entry));
    int start_size = SFS_ENTRY_SIZE * (1 + get_num_cont(start));

    /* check available space in the free area and update free list */
    if (sfs->free_last == NULL) printf("free_last is NULL!!!\n");

    if (sfs->free_last != NULL  && sfs->free_last->length >= entry_size) {
        sfs->free_last -= entry_size;
        sfs->super->index_size += entry_size;
        sfs_write_super(sfs->file, sfs->super);
    } else {
        fprintf(stderr, "prepend_entry: free list error\n");
        return -1;
    }

    start->type = SFS_ENTRY_START;
    start->offset -= entry_size;
    entry->offset = start->offset + start_size;
    if (write_entry(sfs, entry) == -1) {
        fprintf(stderr, "prepend_entry: write new entry error\n");
        return -1;
    }
    if (write_entry(sfs, start) == -1) {
        fprintf(stderr, "prepend_entry: write start marker entry error\n");
        return -1;
    }
    // update pointers only if write successful
    entry->next = start->next;
    start->next = entry;
    return 0;
}

/* Puts a new entry into the entry list, updating the index area:
 * -> finds a place in the list with needed number of continuations
 * -> writes changes to the index area
 * -> updates free list if deleted files overwritten
 * -> on success returns 0
 *    otherwise returns -1
 */
int put_new_entry(struct sfs *sfs, struct sfs_entry *new_entry)
{
    if (insert_entry(sfs, sfs->entry_list, new_entry) != 0) {
        return prepend_entry(sfs, new_entry);
    }
    return 0;
}

// check if path valid and does not exist
int check_valid_new(struct sfs *sfs, char *path)
{
    printf("check valid as new \"%s\"s:", path);
    // if path exists, not valid as a new name
    struct sfs_entry *entry = get_dir_by_name(sfs, path);
    if (entry != NULL) {
        printf(" no (already exists)\n");
        return 0;
    }

    int path_len = strlen(path);
    char *basename = get_basename(path);
    int basename_len = strlen(basename);

    /* check if prent dir exists */
    if (path_len > basename_len) {
        char parent[path_len];
        memcpy(parent, path, path_len);
        parent[path_len - basename_len - 1] = '\0';
        struct sfs_entry *parent_entry = get_dir_by_name(sfs, parent);
        if (parent_entry == NULL) {
            printf(" no (parent \"%s\" does exists)\n", parent);
            return 0;
        }
        printf(" parent=\"%s\"", parent);
    }
    printf(" basename=\"%s\"\n", basename);

    return 1;
}

int sfs_mkdir(struct sfs *sfs, const char *path)
{
    char *fxpath = fix_name(path);
    int path_len = strlen(fxpath);
    printf("@@@\tsfs_mkdir: create new directory \"%s\"\n", fxpath);
    if (!check_valid_new(sfs, fxpath)) {
        return -1;
    }

    struct sfs_entry *dir_entry = malloc(sizeof(struct sfs_entry));
    dir_entry->type = SFS_ENTRY_DIR;
    int num_cont;
    if (path_len < SFS_DIR_NAME_LEN) {
        num_cont = 0;
    } else {
        int cont_str_len = path_len - SFS_DIR_NAME_LEN + 1;
        num_cont = (cont_str_len + SFS_ENTRY_SIZE - 1) / SFS_ENTRY_SIZE;
    }
    dir_entry->data.dir_data = malloc(sizeof(struct sfs_dir));
    dir_entry->data.dir_data->num_cont = num_cont;
    dir_entry->data.dir_data->time_stamp = sfs_make_time_stamp();
    dir_entry->data.dir_data->name = malloc(path_len + 1);
    memcpy(dir_entry->data.dir_data->name, fxpath, path_len + 1);

    if (put_new_entry(sfs, dir_entry) == -1) {
        printf("\tsfs_mkdir put new entry error\n");
        free_entry(dir_entry);
    }

    return 0;
}

int sfs_create(struct sfs *sfs, const char *path)
{
    char *fxpath = fix_name(path);
    int path_len = strlen(fxpath);
    printf("@@@\tsfs_create: create new empty file \"%s\"\n", fxpath);
    if (!check_valid_new(sfs, fxpath)) {
        return -1;
    }

    struct sfs_entry *file_entry = malloc(sizeof(struct sfs_entry));
    file_entry->type = SFS_ENTRY_FILE;
    int num_cont;
    if (path_len < SFS_FILE_NAME_LEN) {
        num_cont = 0;
    } else {
        int cont_str_len = path_len - SFS_FILE_NAME_LEN + 1;
        num_cont = (cont_str_len + SFS_ENTRY_SIZE - 1) / SFS_ENTRY_SIZE;
    }

    file_entry->data.file_data = malloc(sizeof(struct sfs_file));
    file_entry->data.file_data->num_cont = num_cont;
    file_entry->data.file_data->time_stamp = sfs_make_time_stamp();
    file_entry->data.file_data->start_block = sfs->super->rsvd_blocks;
    file_entry->data.file_data->end_block = sfs->super->rsvd_blocks - 1;
    file_entry->data.file_data->file_len = 0;
    file_entry->data.file_data->name = malloc(path_len + 1);
    memcpy(file_entry->data.file_data->name, fxpath, path_len + 1);

    if (put_new_entry(sfs, file_entry) == -1) {
        printf("\tsfs_file put new entry error\n");
        free_entry(file_entry);
    }

    return 0;
}

/* TODO: old */
struct S_SFS_UNUSED *make_unused_entry(uint8_t *buf)
{
    struct S_SFS_UNUSED *unused = malloc(sizeof(struct S_SFS_UNUSED));
    uint8_t *cbuf = buf;
    memcpy(&unused->type, cbuf, sizeof(unused->type));
    cbuf += sizeof(unused->type);
    memcpy(&unused->crc, cbuf, sizeof(unused->crc));
    cbuf += sizeof(unused->crc);
    memcpy(&unused->resvd, cbuf, sizeof(unused->resvd));
    if (!check_crc(buf, 64)) {
        return NULL;
    }    
    return unused;    
}

/* TODO: old */
uint8_t *rw_cont_name(int init_len, uint8_t *init_buf, FILE *f, int n_cont)
{
    uint8_t *name = malloc(init_len + 64 * n_cont);
    memcpy(name, init_buf, init_len);
    uint8_t *p = name;
    uint8_t buf[64];
    for (int i = 0; i < n_cont; i++) {
        fread(buf, 64, 1, f);
        memcpy(p, buf, 64);
        p += 64;
        i++;
    }
    return name;
}

/* TODO: old */
struct S_SFS_DIR *make_dir_entry(uint8_t *buf, FILE *f)
{
    struct S_SFS_DIR *dir = malloc(sizeof(struct S_SFS_DIR));
    uint8_t *cbuf = buf;
    memcpy(&dir->type, cbuf, sizeof(dir->type));
    cbuf += sizeof(dir->type);
    memcpy(&dir->crc, cbuf, sizeof(dir->crc));
    cbuf += sizeof(dir->crc);
    memcpy(&dir->num_cont, cbuf, sizeof(dir->num_cont));
    cbuf += sizeof(dir->num_cont);
    memcpy(&dir->time_stamp, cbuf, sizeof(dir->time_stamp));
    cbuf += sizeof(dir->time_stamp);
    dir->name = rw_cont_name(SFS_DIR_NAME_LEN, cbuf, f, dir->num_cont);
    if (!check_crc(buf, 64 * (1 + dir->num_cont))) {
        return NULL;
    }    
    return dir;    
}

/* TODO: old */
struct S_SFS_FILE *make_file_entry(struct sfs *sfs, uint8_t *buf, FILE *f)
{
    struct S_SFS_FILE *f_entry = malloc(sizeof(struct S_SFS_FILE));
    uint8_t *cbuf = buf;
    memcpy(&f_entry->type, cbuf, sizeof(f_entry->type));
    cbuf += sizeof(f_entry->type);
    memcpy(&f_entry->crc, cbuf, sizeof(f_entry->crc));
    cbuf += sizeof(f_entry->crc);
    memcpy(&f_entry->num_cont, cbuf, sizeof(f_entry->num_cont));
    cbuf += sizeof(f_entry->num_cont);
    memcpy(&f_entry->time_stamp, cbuf, sizeof(f_entry->time_stamp));
    cbuf += sizeof(f_entry->time_stamp);
    memcpy(&f_entry->start_block, cbuf, sizeof(f_entry->start_block));
    cbuf += sizeof(f_entry->start_block);
    memcpy(&f_entry->end_block, cbuf, sizeof(f_entry->end_block));
    cbuf += sizeof(f_entry->end_block);
    memcpy(&f_entry->file_len, cbuf, sizeof(f_entry->file_len));
    cbuf += sizeof(f_entry->file_len);
    f_entry->file = NULL;
    f_entry->file_buf = NULL;
    f_entry->sfs = sfs;
    f_entry->name = rw_cont_name(SFS_FILE_NAME_LEN, cbuf, f, f_entry->num_cont);
    if (!check_crc(buf, 64 * (1 + f_entry->num_cont))) {
        return NULL;
    }
    f_entry->del_number = 0;
    return f_entry;    
}

/* TODO: old */
struct S_SFS_UNUSABLE *make_unusable_entry(uint8_t *buf)
{
    struct S_SFS_UNUSABLE *unusable = malloc(sizeof(struct S_SFS_UNUSABLE));
    uint8_t *cbuf = buf;
    memcpy(&unusable->type, cbuf, sizeof(unusable->type));
    cbuf += sizeof(unusable->type);
    memcpy(&unusable->crc, cbuf, sizeof(unusable->crc));
    cbuf += sizeof(unusable->crc);
    memcpy(&unusable->resv0, cbuf, sizeof(unusable->resv0));
    cbuf += sizeof(unusable->resv0);
    memcpy(&unusable->start_block, cbuf, sizeof(unusable->start_block));
    cbuf += sizeof(unusable->start_block);
    memcpy(&unusable->end_block, cbuf, sizeof(unusable->end_block));
    cbuf += sizeof(unusable->end_block);
    memcpy(&unusable->resv1, cbuf, sizeof(unusable->resv1));
    if (!check_crc(buf, 64)) {
        return NULL;
    }    
    return unusable;    
}

/* TODO: old */
struct S_SFS_DIR *make_dir_del_entry(uint8_t *buf, FILE *f)
{
    struct S_SFS_DIR *dir = malloc(sizeof(struct S_SFS_DIR));
    uint8_t *cbuf = buf;
    memcpy(&dir->type, cbuf, sizeof(dir->type));
    cbuf += sizeof(dir->type);
    memcpy(&dir->crc, cbuf, sizeof(dir->crc));
    cbuf += sizeof(dir->crc);
    memcpy(&dir->num_cont, cbuf, sizeof(dir->num_cont));
    cbuf += sizeof(dir->num_cont);
    memcpy(&dir->time_stamp, cbuf, sizeof(dir->time_stamp));
    cbuf += sizeof(dir->time_stamp);
    dir->name = rw_cont_name(SFS_DIR_NAME_LEN, cbuf, f, dir->num_cont);
    if (!check_crc(buf, 64 * (1 + dir->num_cont))) {
        return NULL;
    }    
    return dir;    
}


/* TODO: old */
void sfs_del_file(struct S_SFS_FILE *sfs_file);
struct S_SFS_FILE *make_file_del_entry(struct sfs *sfs, uint8_t *buf, FILE *f)
{
    struct S_SFS_FILE *f_entry = malloc(sizeof(struct S_SFS_FILE));
    uint8_t *cbuf = buf;
    memcpy(&f_entry->type, cbuf, sizeof(f_entry->type));
    cbuf += sizeof(f_entry->type);
    memcpy(&f_entry->crc, cbuf, sizeof(f_entry->crc));
    cbuf += sizeof(f_entry->crc);
    memcpy(&f_entry->num_cont, cbuf, sizeof(f_entry->num_cont));
    cbuf += sizeof(f_entry->num_cont);
    memcpy(&f_entry->time_stamp, cbuf, sizeof(f_entry->time_stamp));
    cbuf += sizeof(f_entry->time_stamp);
    memcpy(&f_entry->start_block, cbuf, sizeof(f_entry->start_block));
    cbuf += sizeof(f_entry->start_block);
    memcpy(&f_entry->end_block, cbuf, sizeof(f_entry->end_block));
    cbuf += sizeof(f_entry->end_block);
    memcpy(&f_entry->file_len, cbuf, sizeof(f_entry->file_len));
    cbuf += sizeof(f_entry->file_len);
    f_entry->file = NULL;
    f_entry->file_buf = NULL;
    f_entry->sfs = sfs;
    f_entry->name = rw_cont_name(SFS_FILE_NAME_LEN, cbuf, f, f_entry->num_cont);
    if (!check_crc(buf, 64 * (1 + f_entry->num_cont))) {
        return NULL;
    }
//    sfs_del_file(f_entry); /* this file is deleted! */
    return f_entry;    
}

/* TODO: old */
int get_entry_start(struct sfs_entry_list *list)
{
    switch (list->type) {
/*
    case SFS_ENTRY_FILE:
        return list->entry.file->start_block;
    case SFS_ENTRY_UNUSABLE:
        return list->entry.unusable->start_block;
    case SFS_ENTRY_FILE_DEL:
        return list->entry.file->start_block;
*/
    default:
        return -1;
    }
}

/* TODO: old */
struct sfs_entry_list *sfs_get_entry_list(struct sfs *sfs)
{
/*
    if (sfs->entry_list != NULL)
        return sfs->entry_list;
*/
    /* find location of entries */

    int n_entries = sfs->super->index_size / 64;
    if (fseek(sfs->file, -(64 * n_entries), SEEK_END) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

/*
    union entry entry = make_entry(sfs, sfs->file);
    while (entry.null != NULL) {
        int type = ((uint8_t*)entry.null)[0];
        if (type == SFS_ENTRY_FILE || type == SFS_ENTRY_UNUSABLE
          || type == SFS_ENTRY_FILE_DEL)
        {
            struct sfs_entry_list *list = malloc(sizeof(struct sfs_entry_list));
            list->entry = entry;
            list->type = type;
            insert_entry(&sfs->entry_list, list);
        }
        if (type == SFS_ENTRY_VOL_ID)
            break;
        if (feof(sfs->file))
            break;
        entry = make_entry(sfs, sfs->file);
    }
*/
    /* TODO: fill gaps in entry list with free entries */
//    return sfs->entry_list;
    return NULL;
}

/* TODO: old */
void sfs_terminate_old(struct sfs *sfs)
{
/*
    if (sfs->entry_list != NULL) {
        struct sfs_entry_list *entry_list = sfs->entry_list;
        struct sfs_entry_list *old;
        while (entry_list != NULL) {
            union entry entry = entry_list->entry;
            switch (entry_list->type) {
                case SFS_ENTRY_DIR:
                    free(entry.dir->name);
                    break;
                case SFS_ENTRY_FILE:
                    free(entry.file->name);
                    break;
                case SFS_ENTRY_DIR_DEL:
                    free(entry.dir->name);
                    break;
                case SFS_ENTRY_FILE_DEL:
                    free(entry.file->name);
                    break;
            }
            free(entry_list->entry.null);
            old = entry_list;
            entry_list = entry_list->next;
            free(old);
        }
    }
    if (sfs->super != NULL) {
        free(sfs->super);
    }
    if (sfs->file != NULL) {
        fclose(sfs->file);
    }
    free(sfs);
*/
}

/* TODO: old */
FILE* sfs_open_file(struct S_SFS_FILE *sfs_file)
{
    FILE *f = sfs_file->sfs->file;
    int bs = sfs_file->sfs->block_size;

    sfs_file->file_buf = malloc(sfs_file->file_len);

    /* read file contents to file_buf */
    fseek(f, sfs_file->start_block * bs, SEEK_SET);
    fread(sfs_file->file_buf, 1, sfs_file->file_len, f);

    sfs_file->file = fmemopen(sfs_file->file_buf, sfs_file->file_len, "r");
    if (sfs_file->file == NULL) {
        fprintf(stderr, "\nERROR: open_memstream \"%s\"\n", strerror(errno));
        return NULL;
    }
    return sfs_file->file;
}

/* TODO: old */
void sfs_close_file(struct S_SFS_FILE *sfs_file)
{
    fclose(sfs_file->file);
    sfs_file->file = NULL;
    free(sfs_file->file_buf);
    sfs_file->file_buf = NULL;
}

/* TODO: old */
void sfs_write_entries(struct sfs *sfs)
{
    /* TODO */
}

/* TODO: old */
/* write buffer to new file */
struct S_SFS_FILE *sfs_write_file(struct sfs *sfs, char *name,
    char *buf, int *sz)
{
    /* TODO: find place to write */
    /* TODO: delete/resize entries */
    /* TODO: write file to blocks */

    /* write new entries */
    sfs_write_entries(sfs);
    return NULL;
}

/* TODO: old */
struct S_SFS_FILE *sfs_get_file_by_name(struct sfs *sfs, char *name)
{
/*
    struct sfs_entry_list* entries = sfs_get_entry_list(sfs);
    struct S_SFS_FILE *f_entry;
    while (entries != NULL) {
        if (entries->type == SFS_ENTRY_FILE) {
            f_entry = entries->entry.file;
            if (strcmp(name, (char*)f_entry->name) == 0)
                return f_entry;
        }
        entries = entries->next;
    }
*/
    return NULL;
}
