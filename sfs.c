#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
    uint8_t magic[3];
    uint8_t version;
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
    struct sfs_entry *iter_curr;
};

/* The Volume ID Entry Data */
struct sfs_vol {
    int64_t time_stamp;
    uint8_t *name;
};

/* The Directory Entry Data */
struct sfs_dir {
    uint8_t num_cont;
    int64_t time_stamp;
    uint8_t *name;
};

/* The File Entry Data */
struct sfs_file {
    uint8_t num_cont;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    uint8_t *name;
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
    memcpy(&sfs->super->magic, cbuf, sizeof(sfs->super->magic));
    cbuf += sizeof(sfs->super->magic);
    memcpy(&sfs->super->version, cbuf, sizeof(sfs->super->version));
    cbuf += sizeof(sfs->super->version);
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
        if ( entry->type == SFS_ENTRY_FILE
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
        printf("(%03lx,%03lx) ", list->start_block, list->length);
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

void block_list_to_free_list(struct sfs_block_list **plist)
{
}

struct sfs_block_list *make_free_list(struct sfs_entry *entry_list)
{
    struct sfs_block_list *block_list = block_list_from_entries(entry_list);

    print_block_list("not sorted: ", block_list);
    sort_block_list(&block_list);
    print_block_list("sorted:     ", block_list);

    block_list_to_free_list(&block_list);

    return block_list;
}

SFS *sfs_init(const char *filename)
{
    SFS *sfs = malloc(sizeof(SFS));
    sfs->file = fopen(filename, "r");
    if (sfs->file == NULL) {
        perror("sfs_init error");
        fprintf(stderr, "sfs_init: file error \"%s\"\n", filename);
        return NULL;
    }
    sfs->super = sfs_read_super(sfs);
    sfs->entry_list = sfs_read_entries(sfs);
    sfs->free_list = make_free_list(sfs->entry_list);
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

int sfs_terminate(SFS *sfs)
{
    printf("free sfs\n");
    // free entry list
    struct sfs_entry *prev = sfs->entry_list;
    struct sfs_entry *curr = prev->next;
    while (curr != NULL) {
        free_entry(prev);
        prev = curr;
        curr = curr->next;
    }
    free_entry(prev);

    /* TODO: free block list */

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
                && strcmp(fxpath, (char *)entry->data.file_data->name) == 0) {
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
                && strcmp(path, (char *)entry->data.dir_data->name) == 0) {
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
                && strcmp(path, (char *)entry->data.file_data->name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

int sfs_is_dir(SFS *sfs, const char *path)
{
    char *fxpath = fix_name(path);
    printf("@@@@\tsfs_is_dir: name=\"%s\"\n", fxpath);
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
    printf("@@@@\tsfs_is_file: name=\"%s\"\n", fxpath);
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
            char *name = (char *)entry->data.dir_data->name;
            int name_len = strlen(name);
            if ((len == 0 || (name_len > len + 1 && name[len] == '/'))
                    && strncmp(path, name, len) == 0 && strchr(&name[len+1], '/') == NULL) {
                return entry;
            }
        } else if (entry->type == SFS_ENTRY_FILE) {
            char *name = (char *)entry->data.file_data->name;
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
    printf("@@@@\tsfs_read: path=\"%s\", size:0x%lx, offset:0x%lx\n", fxpath, size, offset);
    struct sfs_entry *entry = get_file_by_name(sfs, fxpath);
    if (entry != NULL) {
        int sz;		// number of bytes to be read
        uint64_t len = entry->data.file_data->file_len;
        if (offset > len) {
            return 0;
        }
        if (offset + size > len) {
            sz = offset + size - len;
        } else {
            sz = size;
        }
        uint64_t data_offset = sfs->block_size * entry->data.file_data->start_block;
        fseek(sfs->file, data_offset, SEEK_SET);
        fread(buf, sz, 1, sfs->file);
        return sz;
    } else {
        return -1;
    }
}

/* TODO: old */
struct sfs_entry_list *sfs_get_entry_list(struct sfs *sfs);
struct S_SFS_VOL_ID *sfs_get_volume(struct sfs *sfs)
{
/*
    struct sfs_entry_list *list= sfs_get_entry_list(sfs);
    while (list != NULL && list->type != SFS_ENTRY_VOL_ID)
        list = list->next;
    if (list != NULL)
        return list->entry.volume;
    else
*/
        return NULL;

}

/* TODO: old */
struct S_SFS_START *make_start_entry(uint8_t *buf)
{
    struct S_SFS_START *start = malloc(sizeof(struct S_SFS_START));
    uint8_t *cbuf = buf;
    memcpy(&start->type, cbuf, sizeof(start->type));
    cbuf += sizeof(start->type);
    memcpy(&start->crc, cbuf, sizeof(start->crc));
    cbuf += sizeof(start->crc);
    memcpy(&start->resvd, cbuf, sizeof(start->resvd));
    if (!check_crc(buf, 64)) {
        return NULL;
    }    
    return start;    
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
    sfs_del_file(f_entry); /* this file is deleted! */
    return f_entry;    
}

/* TODO: old */
/* file must be positioned on valid entry */
union entry make_entry(struct sfs *sfs, FILE *f)
{
    union entry entry;
    uint8_t buf[64];
    fread(buf, 64, 1, f);
    switch (buf[0]) {
//    case SFS_ENTRY_VOL_ID:
//        entry.volume = make_volume_entry(buf);
//        break;
    case SFS_ENTRY_START:
        entry.start = make_start_entry(buf);
        break;
    case SFS_ENTRY_UNUSED:
        entry.unused = make_unused_entry(buf);
        break;
    case SFS_ENTRY_DIR:
        entry.dir = make_dir_entry(buf, f);
        break;
    case SFS_ENTRY_FILE:
        entry.file = make_file_entry(sfs, buf, f);
        break;
    case SFS_ENTRY_UNUSABLE:
        entry.unusable = make_unusable_entry(buf);
        break;
    case SFS_ENTRY_DIR_DEL:
        entry.dir = make_dir_del_entry(buf, f);
        break;
    case SFS_ENTRY_FILE_DEL:
        entry.file = make_file_del_entry(sfs, buf, f);
        break;
    default:
        fprintf(stderr, "bad entry type 0x%02x\n", buf[0]);
        entry.null = NULL;
    }
    return entry;
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
void insert_entry(struct sfs_entry_list **list, struct sfs_entry_list *item)
{
    while (*list && get_entry_start(*list) < get_entry_start(item))
        list = &(*list)->next;
    item->next = (*list);
    *list = item;
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
void sfs_del_file(struct S_SFS_FILE *sfs_file)
{
/*
    struct sfs *sfs = sfs_file->sfs;
    struct sfs_del_file_list *new_list = malloc(sizeof(new_list));

    sfs_file->type = SFS_ENTRY_FILE_DEL;
    new_list->next = sfs->del_file_list;
    sfs->del_file_list = new_list;
    sfs->del_file_count++;
    sfs_file->del_number = sfs->del_file_count;
    sfs_write_entries(sfs);
*/
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
