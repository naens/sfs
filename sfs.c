#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdint.h>

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

/****h* sfs/sfs
 * NAME
 *   sfs -- SFS implementation
 * DESCRIPTION
 *   Simple implementation of the SFS file system.
 ******
 */

/****s*	sfs/sfs_super
 * NAME
 *   struct sfs_super -- structure for the superblock of the file system
 * DESCRIPTION
 *   The structure *struct sfs_super* is read from the superblock of the
 *   file system and contains information about it.
 * FIELDS
 *   time_stamp - date/time when the volume was changed
 *   data_size - size of the Data Area in blocks
 *   index_size - size of the Index Area in bytes
 *   total_blocks - total size of the filesystem in blocks
 *   resvd_blocks - number of bytes used by the Superblock and by
 *                  the Reserved Area, indicates the first block of the
 *                  Data Area
 *   block_size - number of bytes in a block
 ******
 */
struct sfs_super {
    int64_t time_stamp;
    uint64_t data_size;
    uint64_t index_size;
    uint64_t total_blocks;
    uint32_t rsvd_blocks;
    uint8_t block_size;
};


/****s* sfs/block_list
 * NAME
 *   struct block_list -- structure to represent a list of block arrays
 * DESCRIPTION
 *   The struct block_list structure represents segments of one or more
 *   consecutive blocks in the data area.  The main use of the
 *   struct block_list is for the free list.  Other uses occur only during
 *   its construction.  There are two types of stuct block_list items:
 *   entries representing deleted files and entries representing free
 *   usable space.  Two items that represent free space cannot follow each
 *   other without a gap.  Which means that they can be stretched and merged
 *   when new free space is added.  The length cannot be zero: such items
 *   are either not inserted or deleted.
 *   When space used by a deleted file is used, the rest of the deleted file
 *   becomes normal free list items (without any file attached to it) and
 *   the entry from the Index Area entry list is deleted.
 * FIELDS
 *   start_block - the index of the first block
 *   length - the number of block represented by the structure
 *   delfile - the link to the deleted file entry if it corresponds to a delted
 *             entry
 *   next - pointer to the next item of the list
 */
struct block_list {
    uint64_t start_block;
    uint64_t length;
    struct sfs_entry *delfile;
    struct block_list *next;
};


/****s* sfs/sfs
 * NAME
 *   struct sfs -- the structure representing the current state of the
 *                 filesystem
 * DESCRIPTION
 *   When a filesystem is opened, this structure is created and must be
 *   accessible (passed as parameter) for every filesystem call.  This
 *   structure is known to the outside world as SFS and its fields are not
 *   accessed.  When the filesystem is closed the structure is freed and can
 *   no longer be used.
 * FIELDS
 *   file - the means to access filesystem data (is readable and writable)
 *   block_size - the size of the blocks to address data (also used for the
 *                filesystem
 *   super - pointer to the superblock structure
 *   volume - pointer to the volume entry
 *   entry_list - list of the entries in the index area
 *   free_list - list of the blocks that can be used to store data (each item
 *               of the list can represent several blocks)
 *   free_last - the last item of the free_list, is used to represent the free
 *               area, cannot be empty (in which case the filesystem should not
 *               be used)
 *   iter_curr - when reading the contents of a directory, the last found entry
 *               is stored here (no calls should be made between the first and
 *               the next calls to search a directory), it is reinitialized on
 *               each sfs_first call
 ******
 */
struct sfs {
    FILE *file;
    int block_size;
    struct sfs_super *super;
    struct sfs_entry *volume;
    struct sfs_entry *entry_list;
    struct block_list *free_list;
    struct block_list *free_last;
    struct sfs_entry *iter_curr;
};


/* The Volume ID Entry Data */
struct volume_data {
    int64_t time_stamp;
    char *name;
};


/* The Directory Entry Data */
struct dir_data {
    uint8_t num_cont;
    int64_t time_stamp;
    char *name;
};


/* The File Entry Data */
struct file_data {
    uint8_t num_cont;
    int64_t time_stamp;
    uint64_t start_block;
    uint64_t end_block;
    uint64_t file_len;
    char *name;
};


/* The Unusable Entry Data */
struct unusable_data {
    uint64_t start_block;
    uint64_t end_block;
};


struct sfs_entry {
    uint8_t type;
    long int offset;
    union {
        struct volume_data *volume_data;
        struct dir_data *dir_data;
        struct file_data *file_data;
        struct unusable_data *unusable_data;
    } data;
    struct sfs_entry *next;
};


static uint64_t timespec_to_time_stamp(struct timespec *timespec)
{
    time_t s = timespec->tv_sec;
    long n = timespec->tv_nsec;
    // timestamp = secs * 65536
    // (n / 1000000000 + s) * 65536
    // n*128/1953125 + 65536*s
    uint64_t timestamp = round((n << 7) / 1953125.0) + (s << 16);
    return timestamp;
}


static uint64_t make_time_stamp()
{
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    return timespec_to_time_stamp(&spec);
}


static void fill_timespec(uint64_t time_stamp, struct timespec *timespec)
{
    uint64_t sec = time_stamp >> 16;

    // 1/65536 of sec
    uint64_t rest = time_stamp & 0x10000;

    // convert 1/65536 to 1/1000000000
    uint64_t nsec = round((rest * 1953125) / 128.0);

    timespec->tv_sec = sec;
    timespec->tv_nsec = nsec;
}


static int check_crc(uint8_t *buf, int sz)
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


static struct sfs_super *read_super(struct sfs *sfs)
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


static void write_super(FILE *file, struct sfs_super *super) {
    char buf[SFS_SUPER_SIZE];
    super->time_stamp = make_time_stamp();
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


static struct sfs_entry *read_volume_data(uint8_t *buf, struct sfs_entry *entry)
{
    struct volume_data *volume_data = malloc(sizeof(struct volume_data));
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


static struct sfs_entry *read_dir_data(uint8_t *buf, struct sfs_entry *entry, FILE *file)
{
    uint8_t *b = buf;
    struct dir_data *dir_data = malloc(sizeof(struct dir_data));

    memcpy(&dir_data->num_cont, &buf[2], 1);
    memcpy(&dir_data->time_stamp, &buf[3], 8);

    const int cont_len = dir_data->num_cont * SFS_ENTRY_SIZE;
    const int name_len = SFS_DIR_NAME_LEN + cont_len;

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
    return entry;   
}


static struct sfs_entry *read_file_data(uint8_t *buf, struct sfs_entry *entry, FILE *file)
{
    uint8_t *b = buf;
    struct file_data *file_data = malloc(sizeof(struct file_data));

    memcpy(&file_data->num_cont, &buf[2], 1);
    memcpy(&file_data->time_stamp, &buf[3], 8);
    memcpy(&file_data->start_block, &buf[11], 8);
    memcpy(&file_data->end_block, &buf[19], 8);
    memcpy(&file_data->file_len, &buf[27], 8);

    const int cont_len = file_data->num_cont * SFS_ENTRY_SIZE;
    const int name_len = SFS_FILE_NAME_LEN + cont_len;

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
    return entry;   
}


static struct sfs_entry *read_unusable_data(uint8_t *buf, struct sfs_entry *entry)
{
    struct unusable_data *unusable_data = malloc(sizeof(struct unusable_data));
    memcpy(&unusable_data->start_block, &buf[10], 8);
    memcpy(&unusable_data->end_block, &buf[18], 8);
    entry->data.unusable_data = unusable_data;
    if (!check_crc(buf, SFS_ENTRY_SIZE)) {
        return NULL;
    }
    return entry;
}


static int get_num_cont(struct sfs_entry *entry)
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


/* read entry, at current location */
static struct sfs_entry *read_entry(SFS *sfs)
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
        return read_volume_data(buf, entry);
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        return read_dir_data(buf, entry, sfs->file);
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        return read_file_data(buf, entry, sfs->file);
    case SFS_ENTRY_UNUSABLE:
        return read_unusable_data(buf, entry);
    default:
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

    struct sfs_entry *volume = read_entry(sfs);
    if (volume->type != SFS_ENTRY_VOL_ID) {
        return NULL;
    }

    return volume;
}


void print_entry(struct sfs *sfs, struct sfs_entry *entry)
{
    printf("ENTRY Type: 0x%02x\n", entry->type);
    printf("\tOffset: 0x%06lx\n", entry->offset);
    int num_cont = get_num_cont(entry);
    printf("\tContinuations: %d\n", num_cont);
    printf("\tLength: 0x%06x (bytes)\n", (1 + num_cont) * SFS_ENTRY_SIZE);
    switch (entry->type) {
    case SFS_ENTRY_DIR:
    case SFS_ENTRY_DIR_DEL:
        printf("\tDirectory Name: %s\n", entry->data.dir_data->name);
        break;
    case SFS_ENTRY_FILE:
    case SFS_ENTRY_FILE_DEL:
        printf("\tFile Name: %s\n", entry->data.file_data->name);
        printf("\tFile Start: 0x%06lx\n", entry->data.file_data->start_block * sfs->block_size);
        printf("\tFile Length: 0x%06lx\n", entry->data.file_data->file_len);
        break;
    case SFS_ENTRY_UNUSABLE:
        break;
    default:
        break;
    }

}


static struct sfs_entry *read_entries(SFS *sfs)
{
    int offset = sfs->block_size * sfs->super->total_blocks - sfs->super->index_size;
    printf("bs=0x%x, tt=0x%lxH, is=0x%lx, of=0x%x\n",
        sfs->block_size, sfs->super->total_blocks, sfs->super->index_size, offset);
    if (fseek(sfs->file, offset, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }
    struct sfs_entry *entry = read_entry(sfs);
    print_entry(sfs, entry);
    struct sfs_entry *head = entry;
    while (entry->type != SFS_ENTRY_VOL_ID) {
        struct sfs_entry *prev = entry;
        entry = read_entry(sfs);
        print_entry(sfs, entry);
        prev->next = entry;
    }
    sfs->volume = entry;
    return head;
}


struct block_list *block_list_from_entries(struct sfs_entry *entry_list)
{
    struct block_list *list = NULL;
    struct sfs_entry *entry = entry_list;
    while (entry != NULL) {
        if ((entry->type == SFS_ENTRY_FILE && entry->data.file_data->file_len != 0)
                || entry->type == SFS_ENTRY_FILE_DEL
                || entry->type == SFS_ENTRY_UNUSABLE) {
            struct block_list *item = malloc(sizeof(struct block_list));
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


struct block_list **conquer(struct block_list **p1, struct block_list **p2, int sz)
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
                struct block_list *tmp = *p2;
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


static void print_block_list(struct sfs *sfs, char *info, struct block_list *list)
{
    printf("%s\n", info);
    while (list != NULL) {
        printf("\tstart:  0x%06lx", list->start_block * sfs->block_size);
        printf("\tlength: 0x%06lx\t", list->length * sfs->block_size);
        if (list->delfile != NULL) {
            printf("\tdelfile: %s", list->delfile->data.file_data->name);
        }
        list = list->next;
        printf("\n");
    }
    printf("\n");
}


static void sort_block_list(struct block_list **plist)
{
    int sz = 1;
    int n;
    do {
        n = 0;
        struct block_list **plist1 = plist;
        while (*plist1 != NULL) {
            n = n + 1;
            struct block_list **plist2 = plist1;
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


static void block_list_to_free_list(plist, first_block, total_blocks, free_last)
    struct block_list **plist;
    uint64_t first_block;
    uint64_t total_blocks;
    struct block_list **free_last;
{
    if (*plist == NULL) {
        uint64_t gap = total_blocks - first_block;
        struct block_list *item = malloc(sizeof(struct block_list));
        item->start_block = first_block;
        item->length = gap;
        item->delfile = NULL;
        item->next = NULL;
        *plist = item;
        return;
    }

    /* if gap before the first block, insert one block structure */
    struct block_list **pprev;
    struct block_list *curr;
    if (first_block < (*plist)->start_block) {
        struct block_list *b0 = malloc(sizeof(struct block_list));
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
                struct block_list *tmp = (*pprev);
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
                struct block_list *item = malloc(sizeof(struct block_list));
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


struct block_list *make_free_list(sfs, entry_list, free_last)
    struct sfs *sfs;
    struct sfs_entry *entry_list;
    struct block_list **free_last;
{
    struct sfs_super *super = sfs->super;
    struct block_list *block_list = block_list_from_entries(entry_list);
    print_block_list(sfs, "not sorted:", block_list);

    sort_block_list(&block_list);
    print_block_list(sfs, "sorted:", block_list);

    uint64_t first_block = super->rsvd_blocks;  // !! includes the superblock
    uint64_t iblocks = (sfs->super->index_size + sfs->block_size - 1) / sfs->block_size;
    uint64_t data_blocks = super->total_blocks - iblocks; // without index blocks !!
    block_list_to_free_list(&block_list, first_block, data_blocks, free_last);
    print_block_list(sfs, "free:", block_list);

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
    sfs->super = read_super(sfs);
    if (sfs->super == NULL) {
        fprintf(stderr, "sfs_init: error reading the superblock\n");
        exit(7);
    }
    sfs->entry_list = read_entries(sfs);
    sfs->free_last = NULL;
    sfs->free_list = make_free_list(sfs, sfs->entry_list, &sfs->free_last);
    if (sfs->free_last == NULL) {
        fprintf(stderr, "sfs_init: error: free_last is null\n");
        exit(7);
    }
    return sfs;
}


static void free_entry(struct sfs_entry *entry)
{
//    printf("freeing: %x\n", entry->type);
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


static void free_entry_list(struct sfs_entry *entry_list)
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


static void free_free_list(struct block_list *free_list)
{
    struct block_list *prev = free_list;
    struct block_list *curr = prev->next;
    while (curr != NULL) {
        free(prev);
        prev = curr;
        curr = curr->next;
    }
    free(prev);
}

int sfs_terminate(SFS *sfs)
{
    free_entry_list(sfs->entry_list);
    free_free_list(sfs->free_list);
    free(sfs->super);
    fclose(sfs->file);
    free(sfs);
    return 0;
}


uint64_t sfs_get_file_size(SFS *sfs, const char *path)
{
    struct sfs_entry *entry = sfs->entry_list;
    while (entry != NULL) {
        if (entry->type == SFS_ENTRY_FILE
                && strcmp(path, entry->data.file_data->name) == 0) {
            return entry->data.file_data->file_len;
        }
        entry = entry->next;
    }
    return 0;
}


// do not return deleted files and directories
struct sfs_entry *get_entry_by_name(SFS *sfs, const char *path) {
    struct sfs_entry *entry = sfs->entry_list;
    while (entry != NULL) {
        switch (entry->type) {
        case SFS_ENTRY_DIR:
            if (strcmp(path, entry->data.dir_data->name) == 0) {
                return entry;
            }
            break;
        case SFS_ENTRY_FILE:
            if (strcmp(path, entry->data.file_data->name) == 0) {
                return entry;
            }
            break;
        }
        entry = entry->next;
    }
    return NULL;
}


struct sfs_entry *get_dir_by_name(SFS *sfs, const char *path) {
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


struct sfs_entry *get_file_by_name(SFS *sfs, const char *path) {
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
//    printf("@@@@\tsfs_is_dir: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_dir_by_name(sfs, path);
    if (entry != NULL) {
        return 1;
    } else {
        return 0;
    }
}


int sfs_is_file(SFS *sfs, const char *path)
{
//    printf("@@@@\tsfs_is_file: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_file_by_name(sfs, path);
    if (entry != NULL) {
        return 1;
    } else {
        return 0;
    }
}


struct sfs_entry *find_entry_from(struct sfs_entry *entry, const char *path)
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


static const char *get_basename(const char *full_name)
{
    const char *p = full_name;
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


static const char *get_entry_basename(struct sfs_entry *entry)
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


const char *sfs_first(SFS *sfs, const char *path)
{
    struct sfs_entry *entry = find_entry_from(sfs->entry_list, path);
    if (entry != NULL) {
        sfs->iter_curr = entry->next;
        return get_entry_basename(entry);
    }
    sfs->iter_curr = NULL;
    return NULL;
}


const char *sfs_next(SFS *sfs, const char *path)
{
    struct sfs_entry *entry = find_entry_from(sfs->iter_curr, path);
    if (entry != NULL) {
        sfs->iter_curr = entry->next;
        return get_entry_basename(entry);
    }
    sfs->iter_curr = NULL;
    return NULL;
}


int sfs_read(SFS *sfs, const char *path, char *buf, size_t size, off_t offset)
{
//    printf("@@@@\tsfs_read: path=\"%s\", size:0x%lx, offset:0x%lx\n", path, size, offset);
    struct sfs_entry *entry = get_file_by_name(sfs, path);
    if (entry != NULL) {
        uint64_t sz;		// number of bytes to be read
        uint64_t len = entry->data.file_data->file_len;
        if ((uint64_t)offset > len) {
            return 0;
        }
        if (offset + size > len) {
            sz = len - offset;
        } else {
            sz = size;
        }
        uint64_t data_offset = sfs->block_size * entry->data.file_data->start_block;
        uint64_t read_from = data_offset + offset;
        fseek(sfs->file, read_from, SEEK_SET);
        fread(buf, sz, 1, sfs->file);
        return sz;
    } else {
        return -1;
    }
}


static int get_entry_usable_space(struct sfs_entry *entry)
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


static void write_volume_data(char *buf, struct volume_data *vol_data)
{
    memcpy(&buf[4], &vol_data->time_stamp, 8);
    strncpy(&buf[12], vol_data->name, SFS_VOL_NAME_LEN);
}


static void write_dir_data(char *buf, struct dir_data *dir_data)
{
    memcpy(&buf[2], &dir_data->num_cont, 1);
    memcpy(&buf[3], &dir_data->time_stamp, 8);
    uint64_t max_len = SFS_DIR_NAME_LEN + SFS_ENTRY_SIZE * dir_data->num_cont;
    strncpy(&buf[11], dir_data->name, max_len);
}


static void write_file_data(char *buf, struct file_data *file_data)
{
    memcpy(&buf[2], &file_data->num_cont, 1);
    memcpy(&buf[3], &file_data->time_stamp, 8);
    memcpy(&buf[11], &file_data->start_block, 8);
    memcpy(&buf[19], &file_data->end_block, 8);
    memcpy(&buf[27], &file_data->file_len, 8);
    uint64_t max_len = SFS_FILE_NAME_LEN + SFS_ENTRY_SIZE * file_data->num_cont;
    strncpy(&buf[35], file_data->name, max_len);
}


static void write_unusable_data(char *buf, struct unusable_data *unusable_data)
{
    memcpy(&buf[10], &unusable_data->start_block, 8);
    memcpy(&buf[18], &unusable_data->end_block, 8);
}


/* Writes the entry (with its continuations) to the Index Area.
 * Returns 0 on success and -1 on error.
 */
static int write_entry(SFS *sfs, struct sfs_entry *entry)
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


/****f* sfs/delfile_to_normal
 * NAME
 *   delfile_to_normal -- find item in free list by entry and make it normal
 * DESCRIPTION
 *   Find a delfile item in the free list and make it normal free space.  The
 *   item can be merged with previous or next item if they are before/after
 *   without a gap.
 * PARAMETERS
 *   SFS - the SFS structure variable
 *   entry - the delfile entry to convert to normal
 * RETURN VALUE
 *   No return value (void funcion)
 ******
 * Pseudocode
 *   find delfile
 *     curr: item contiaining delfile
 *     prev: previous item or null (if curr is first)
 *     next: next item or null (if curr is last)
 *   if prev <> null and prev:delfile = null
 *                   and prev:(start+length) = curr:start then
 *     prev:length += curr:length
 *     prev:next := curr:next
 *     tmp := curr
 *     curr := prev
 *     free(tmp)
 *   else
 *     curr:delfile = null
 *   end if
 *   if next <> null and next:delfile = null
 *                   and curr:(start+length) = next:start then
 *     curr:length += next:length
 *     curr:next := next:next
 *     free(next)
 *   end if
 */
void delfile_to_normal(struct sfs *sfs, struct sfs_entry *delfile)
{
    // find delfile
    struct block_list *curr = sfs->free_list;
    struct block_list *prev = NULL;
    struct block_list *next;
    while (curr != NULL && curr->delfile != delfile) {
        prev = curr;
        curr = curr->next;
    }
    if (curr == NULL) {
        return;
    }
    next = curr->next;

    // check before
    if (prev != NULL && prev->delfile == NULL
            && prev->start_block + prev->length == curr->start_block) {
        prev->length += curr->length;
        prev->next = curr->next;
        struct block_list *tmp = curr;
        curr = prev;
        free(tmp);
    } else {
        curr->delfile = NULL;
    }

    // check after
    if (next != NULL && next->delfile != NULL
            && curr->start_block + curr->length == next->start_block) {
        curr->length += next->length;
        curr->next = next->next;
        free(next);
    }
}


static void delete_entries(sfs, from, to)
    struct sfs *sfs;
    struct sfs_entry *from;
    struct sfs_entry *to;
{
    struct sfs_entry *entry = from;
    while (entry != to) {
        if (entry->type == SFS_ENTRY_FILE_DEL) {
            delfile_to_normal(sfs, entry);
        }
        struct sfs_entry *tmp = entry;
        entry = entry->next;
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
    return next;
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
static int insert_entry(struct sfs *sfs, struct sfs_entry *new_entry)
{
    printf("=== INSERT ENTRY ===\n");
    int space_needed = 1 + get_num_cont(new_entry); // in number of simple entries
    printf("\tneeded: %d\n", space_needed);
    int space_found = 0;
    struct sfs_entry **p_entry = &sfs->entry_list;
    struct sfs_entry **pfirst_usable = NULL;
    while (*p_entry != NULL) {
//        printf("\ttype=0x%02x\n", curr_entry->type);
        int usable_space = get_entry_usable_space(*p_entry);
//        printf("\tusable: %d\n", usable_space);
        if (usable_space > 0) {
            if (pfirst_usable == NULL) {
                pfirst_usable = p_entry;
                printf("\tfound: %d\n", space_found);
            }
            space_found += usable_space;
            if (space_found >= space_needed) {
                int start = (*pfirst_usable)->offset;
                int end = start + SFS_ENTRY_SIZE * space_needed;
                struct sfs_entry *next = (*p_entry)->next;
                delete_entries(sfs, *pfirst_usable, next);
                new_entry->offset = start;
                int l = space_found - space_needed;
                new_entry->next = insert_unused(sfs, end, l, next);
                *pfirst_usable = new_entry;
                printf("=== INSERT ENTRY: OK ===\n");
                if (write_entry(sfs, new_entry) != 0) {
                    return -1;
                } else {
                    return 0;
                }
            }
        } else if (pfirst_usable != NULL) {
            pfirst_usable = NULL;
            space_found = 0;
        }
        p_entry = &(*p_entry)->next;
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
static int prepend_entry(struct sfs *sfs, struct sfs_entry *entry)
{
    printf("=== PREPEND ENTRY ===\n");
    struct sfs_entry *start = sfs->entry_list;
    uint64_t entry_size = SFS_ENTRY_SIZE * (1 + get_num_cont(entry));
    uint64_t start_size = SFS_ENTRY_SIZE * (1 + get_num_cont(start));

    /* check available space in the free area and update free list */
    if (sfs->free_last == NULL) {
        printf("free_last is NULL!!!\n");
        return -1;
    }

    printf("\tfree_last length: 0x%06lx (bytes)\n", sfs->free_last->length * sfs->block_size);
    printf("\tentry size: 0x%06lx\n", entry_size);
    if (sfs->free_last != NULL  && sfs->free_last->length * sfs->block_size >= entry_size) {
        uint64_t new_isz = sfs->super->index_size + entry_size;
        uint64_t iblocks = (sfs->super->index_size + sfs->block_size - 1) / sfs->block_size;
        uint64_t ibt = iblocks * sfs->block_size;                // index with rest in bytes
        uint64_t fbt = sfs->free_last->length * sfs->block_size; // free blocks in bytes
        uint64_t index_start = sfs->super->total_blocks * sfs->block_size - sfs->super->index_size;
        printf("\tblock size: 0x%06x\n", sfs->block_size);
        printf("\toriginal index size: 0x%06lx\n", sfs->super->index_size);
        printf("\toriginal index start: 0x%06lx\n", index_start);
        printf("\toriginal free blocks: 0x%06lx\n", sfs->free_last->length);
        printf("\toriginal index blocks (bytes): 0x%06lx\n", ibt);
        printf("\toriginal free blocks (bytes): 0x%06lx\n", fbt);
        printf("\tnew entry size: 0x%06lx\n", entry_size);
        printf("\tnew index size: 0x%06lx\n", new_isz);
        printf("\tnew index start: 0x%06lx\n", index_start - entry_size);
        if (new_isz > ibt) {
            // update free list
            if (new_isz - ibt > fbt) {
                fprintf(stderr, "Error: could not prepend entry: no more free space\n");
                return -1;
            }
            sfs->free_last->length -= (new_isz - ibt + sfs->block_size - 1) / sfs->block_size;
            printf("\tupdate free_last: 0x%06lx\n", sfs->free_last->length);
            printf("\tnew free blocks (bytes): 0x%06lx\n", sfs->free_last->length * sfs->block_size);
        }
        sfs->super->index_size = new_isz;
        printf("\tupdate index size: 0x%06lx\n", new_isz);
        write_super(sfs->file, sfs->super);
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
    printf("=== PREPEND: OK! ===\n");
    return 0;
}


/* Puts a new entry into the entry list, updating the index area:
 * -> finds a place in the list with needed number of continuations
 * -> writes changes to the index area
 * -> updates free list if deleted files overwritten
 * -> on success returns 0
 *    otherwise returns -1
 */
static int put_new_entry(struct sfs *sfs, struct sfs_entry *new_entry)
{
    if (insert_entry(sfs, new_entry) != 0) {
        return prepend_entry(sfs, new_entry);
    }
    return 0;
}


// check if path valid and does not exist
static int check_valid_new(struct sfs *sfs, const char *path)
{
    printf("check valid as new \"%s\"s:", path);
    // if path exists, not valid as a new name
    struct sfs_entry *entry = get_dir_by_name(sfs, path);
    if (entry != NULL) {
        printf(" no (already exists)\n");
        return 0;
    }

    int path_len = strlen(path);
    const char *basename = get_basename(path);
    int basename_len = strlen(basename);
    if (basename_len == 0) {
        printf(" empty basename\n");
        return 0;
    }

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


static int num_cont_from_name(int entry_type, int name_len)
{
    int first_len;
    switch (entry_type) {
    case SFS_ENTRY_DIR:
        first_len = SFS_DIR_NAME_LEN;
        break;
    case SFS_ENTRY_FILE:
        first_len = SFS_FILE_NAME_LEN;
        break;
    default:
        break;
    }
    int num_cont;
    if (name_len < first_len) {
        num_cont = 0;
    } else {
        int cont_str_len = name_len - first_len + 1;
        num_cont = (cont_str_len + SFS_ENTRY_SIZE - 1) / SFS_ENTRY_SIZE;
    }
    return num_cont;
}


int sfs_mkdir(struct sfs *sfs, const char *path)
{
    int path_len = strlen(path);
    printf("@@@\tsfs_mkdir: create new directory \"%s\"\n", path);
    if (!check_valid_new(sfs, path)) {
        return -1;
    }

    struct sfs_entry *dir_entry = malloc(sizeof(struct sfs_entry));
    dir_entry->type = SFS_ENTRY_DIR;
    int num_cont = num_cont_from_name(SFS_ENTRY_DIR, path_len);
    dir_entry->data.dir_data = malloc(sizeof(struct dir_data));
    dir_entry->data.dir_data->num_cont = num_cont;
    dir_entry->data.dir_data->time_stamp = make_time_stamp();
    dir_entry->data.dir_data->name = strdup(path);

    if (put_new_entry(sfs, dir_entry) == -1) {
        printf("\tsfs_mkdir put new entry error\n");
        free_entry(dir_entry);
    }

    return 0;
}


int sfs_create(struct sfs *sfs, const char *path)
{
    int path_len = strlen(path);
    printf("@@@\tsfs_create: create new empty file \"%s\"\n", path);
    if (!check_valid_new(sfs, path)) {
        return -1;
    }

    struct sfs_entry *file_entry = malloc(sizeof(struct sfs_entry));
    file_entry->type = SFS_ENTRY_FILE;
    int num_cont = num_cont_from_name(SFS_ENTRY_FILE, path_len);
    printf("\tpath_len=%d=>num_cont=%d\n", path_len, num_cont);
    file_entry->data.file_data = malloc(sizeof(struct file_data));
    file_entry->data.file_data->num_cont = num_cont;
    file_entry->data.file_data->time_stamp = make_time_stamp();
    file_entry->data.file_data->start_block = sfs->super->rsvd_blocks;
    file_entry->data.file_data->end_block = sfs->super->rsvd_blocks - 1;
    file_entry->data.file_data->file_len = 0;
    file_entry->data.file_data->name = strdup(path);

    if (put_new_entry(sfs, file_entry) == -1) {
        printf("\tsfs_file put new entry error\n");
        free_entry(file_entry);
    }

    return 0;
}


static int is_dir_empty(struct sfs *sfs, const char *path) {
    struct sfs_entry *entry = sfs->entry_list;
    int path_len = strlen(path);
    while (entry != NULL) {
        if ((entry->type == SFS_ENTRY_DIR
                && strncmp(path, entry->data.dir_data->name, path_len) == 0
                && entry->data.dir_data->name[path_len] == '/')
        || (entry->type == SFS_ENTRY_FILE
                && strncmp(path, entry->data.file_data->name, path_len) == 0
                && entry->data.file_data->name[path_len] == '/')) {
            return 0;
        }
        entry = entry->next;
    }
    return 1;
}


/****f* sfs/sfs_rmdir
 * NAME
 *   sfs_rmdir -- delete an empty directory
 * DESCRIPTION
 *   Delete an empty directory located at the specified path.  If it is not
 *   a directory or if it's not empty, nothing happens and -1 is returned.
 * PARAMETERS
 *   SFS - the SFS structure variable
 *   path - absolute path of the directory to delete
 * RETURN VALUE
 *   On success returns 0.  On error returns -1.
 ******
 */
int sfs_rmdir(struct sfs *sfs, const char *path)
{
    printf("@@@@\tsfs_rmdir: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_dir_by_name(sfs, path);
    if (entry == NULL) {
        fprintf(stderr, "no directory \"%s\" exists\n", path);
        return -1;
    }
    if (!is_dir_empty(sfs, path)) {
        fprintf(stderr, "directory \"%s\" is not empty\n", path);
        return -1;
    }
    /* Deleted children: do not remove, unreachable
     * If the parent is deleted, the children cannot be restored
     * => on restore: check that the parent exists
     */
    entry->type = SFS_ENTRY_DIR_DEL;
    if (write_entry(sfs, entry) == 0) {
        printf("\trmdir(%s): ok\n", path);
        return 0;
    } else {
        return -1;
    }
}


/* Insert a deleted file into the free list */
static void free_list_insert(struct sfs *sfs, struct sfs_entry *delfile)
{
    struct block_list **p = &sfs->free_list;
    while ((*p) != NULL) {
        if ((*p)->start_block > delfile->data.file_data->start_block) {
            // insert before *p
            struct block_list *item = malloc(sizeof(struct block_list));
            item->start_block = delfile->data.file_data->start_block;
            item->length = (delfile->data.file_data->file_len + sfs->block_size - 1) / sfs->block_size;
            item->delfile = delfile;
            item->next = (*p);
            *p = item;
            break;
        }
        p = &(*p)->next;
    }
}


/****f* sfs/delete_entry
 * NAME
 *   delete_entry -- delete entry from the entry list and free it
 * DESCRIPTION
 *   The function delete_entry deletes the entry given as parameter and frees
 *   the entry.  It is assmued that the entry is in the entry list and can be
 *   freed (for example if it's a file entry, its name field contains a valid
 *   null-terminated string.
 * PARAMETERS
 *   SFS - the SFS structure variable
 *   entry - the entry to be deleted
 * RETURN VALUE
 *   This is a void function and does not return anything.
 ******
 * Pseudocode:
 *   entry_length - the number of SFS_ENTRY_SIZE-byte segments to be deleted
 *
 *   for each entry do:
 *     if entry found (pointers are equal) then
 *       insert unused before entry->next entry_length times
 *       point the pointer to the entry to its next
 *       free entry
 *     end if
 * end pseudocode
 */
static void delete_entry(struct sfs *sfs, struct sfs_entry *entry)
{
    struct sfs_entry **p_entry = &sfs->entry_list;
    int entry_length = 1 + get_num_cont(entry);
    struct sfs_entry *tail = entry->next;
    while (*p_entry != NULL) {
        if (*p_entry == entry) {
            *p_entry = insert_unused(sfs, entry->offset, entry_length, tail);
            free_entry(entry);
            break;
        }
        p_entry = &(*p_entry)->next;
    }
}


/****f* sfs/sfs_delete
 * NAME
 *   sfs_delete -- delete a file form the file system
 * DESCRIPTION
 *   Delete a file from the file system.  Can be used only for files, for
 *   directories sfs_rmdir can be used.
 * PARAMETERS
 *   SFS - the SFS structure variable
 *   path - the absolute path of the file that should be deleted
 * RETURN VALUE
 *   Returns 0 on success and -1 on error.
 ******
 */
// deleted empty files: do not allow in the free list (are never deleted)
int sfs_delete(struct sfs *sfs, const char *path)
{
    printf("@@@@\tsfs_delete: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_file_by_name(sfs, path);
    if (entry == NULL) {
        fprintf(stderr, "file \"%s\" does not exists\n", path);
        return -1;
    }

    // do not insert empty files into the free list
    if (entry->data.file_data->file_len == 0) {
        delete_entry(sfs, entry);
        return 0;
    }

    entry->type = SFS_ENTRY_FILE_DEL;
    free_list_insert(sfs, entry);
    if (write_entry(sfs, entry) == 0) {
        printf("\tdelete(%s): ok\n", path);
        return 0;
    } else {
        return -1;
    }
}


int sfs_get_sfs_time(SFS *sfs, struct timespec *timespec)
{
    fill_timespec(sfs->super->time_stamp, timespec);
    return 0;
}


int sfs_get_dir_time(SFS *sfs, const char *path, struct timespec *timespec)
{
    printf("@@@@\tsfs_get_dir_time: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_dir_by_name(sfs, path);
    if (entry == NULL) {
        fprintf(stderr, "directory \"%s\" does not exists\n", path);
        return -1;
    }
    fill_timespec(entry->data.dir_data->time_stamp, timespec);
    return 0;
}


int sfs_get_file_time(SFS *sfs, const char *path, struct timespec *timespec)
{
    printf("@@@@\tsfs_get_file_time: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_file_by_name(sfs, path);
    if (entry == NULL) {
        fprintf(stderr, "file \"%s\" does not exists\n", path);
        return -1;
    }
    fill_timespec(entry->data.file_data->time_stamp, timespec);
    return 0;
}


int sfs_set_time(SFS *sfs, const char *path, struct timespec *timespec)
{
    printf("@@@@\tsfs_set_time: name=\"%s\"\n", path);
    struct sfs_entry *entry = get_entry_by_name(sfs, path);
    if (entry == NULL) {
        fprintf(stderr, "File or directory\"%s\" does not exists\n", path);
        return -1;
    }
    uint64_t time_stamp = timespec_to_time_stamp(timespec);
    switch (entry->type) {
    case SFS_ENTRY_DIR:
        entry->data.dir_data->time_stamp = time_stamp;
        break;
    case SFS_ENTRY_FILE:
        entry->data.file_data->time_stamp = time_stamp;
        break;
    }
    return write_entry(sfs, entry);
}


/****f* sfs/rename_entry
 * NAME
 *   rename_entry -- rename the entry by deleting the old and inserting the new
 * DESCRIPTION
 *   Creates a new entry with all data the same as the entry given, but with a
 *   new name.  The old entry is deleted from the entry list and the new is
 *   inserted.  The changes made to the entry list are also written to the
 *   Index Area.
 * RETURN VALUE
 *   On success returns 0, on error returns -1.
 ******
 */
static int rename_entry(SFS *sfs, struct sfs_entry *entry, const char *name)
{
    struct sfs_entry *new_entry = malloc(sizeof(struct sfs_entry));
    int path_len = strlen(name);
    int num_cont = num_cont_from_name(entry->type, path_len);
    new_entry->type = entry->type;
    printf("\tpath_len=%d=>num_cont=%d\n", path_len, num_cont);
    char *buf = calloc(SFS_ENTRY_SIZE * (1 + num_cont), 1);
    strcpy(buf, name);
    switch (entry->type) {
    case SFS_ENTRY_DIR:
        new_entry->data.dir_data = malloc(sizeof(struct dir_data));
        new_entry->data.dir_data->num_cont = num_cont;
        new_entry->data.dir_data->time_stamp = entry->data.dir_data->time_stamp;
        new_entry->data.dir_data->name = buf;
        break;
    case SFS_ENTRY_FILE:
        new_entry->data.file_data = malloc(sizeof(struct file_data));
        new_entry->data.file_data->num_cont = num_cont;
        new_entry->data.file_data->time_stamp = entry->data.file_data->time_stamp;
        new_entry->data.file_data->start_block = entry->data.file_data->start_block;
        new_entry->data.file_data->end_block = entry->data.file_data->end_block;
        new_entry->data.file_data->file_len = entry->data.file_data->file_len;
        new_entry->data.file_data->name = buf;
        break;
    default:
        break;
    }
    delete_entry(sfs, entry);
    return put_new_entry(sfs, new_entry);
}


/* Assume: there is no entry with name dest_path.
 * Rename entry <source_path> to <dest_path>.
 * Replace in every file and directory entry that starts with <source_path>/
 * <source_path> with <dest_path>.  Write everything to index area.
 * Return 0 on success and -1 on error.
 */
static int move_dir(sfs, source_path, dest_path)
    struct sfs *sfs;
    const char *source_path;
    const char *dest_path;
{
    struct sfs_entry *entry = sfs->entry_list;
    int src_len = strlen(source_path);
    int dest_len = strlen(dest_path);
    while (entry != NULL) {
        char *name = NULL;
        switch (entry->type) {
        case SFS_ENTRY_DIR:
            name = entry->data.dir_data->name;
            break;
        case SFS_ENTRY_FILE:
            name = entry->data.file_data->name;
            break;
        }
        if (name != NULL) {
            int name_len = strlen(name);
            if (name_len >= src_len && (name_len == src_len || name[src_len] == '/')
                    && strncmp(source_path, name, src_len) == 0) {
                char new_name[dest_len + name_len - src_len + 1];
                strncpy(new_name, dest_path, dest_len);
                strncpy(&new_name[dest_len], &name[src_len], name_len - src_len + 1);
                if (rename_entry(sfs, entry, new_name) == -1) {
                    return -1;
                }
                // TODO: !!! move several entries => !!!
            }
        }
        entry = entry->next;
    }
    return 0;
}


/****f* sfs/sfs_rename
 * NAME
 *   sfs_rename -- rename the file or directory (or move)
 * DESCRIPTION
 *   Move a file or directory from one path to another.  It can replace
 *   existing files if the replace parameter is *true*.
 * PARAMETER
 *   SFS - the SFS structure variable
 *   source_path - the name of the file or directory to rename or move
 *   dest_path - the new name for the file or directory
 *   replace - boolean parameter indicating whether the file or directory
 *             should be replaced
 * RETURN VALUE
 *   Returns 0 on success and -1 on error.
 ******
 */
int sfs_rename(sfs, source_path, dest_path, replace)
    SFS *sfs;
    const char *source_path;
    const char *dest_path;
    int replace;
{
    printf("@@@@\tsfs_rename: \"%s\"->\"%s\"\n", source_path, dest_path);
    if (strcmp(source_path, dest_path) == 0) {
        return 0;
    }
    struct sfs_entry *entry = get_entry_by_name(sfs, source_path);
    if (entry == NULL) {
        fprintf(stderr, "Source \"%s\" does not exist\n", source_path);
        return -1;
    }
    if (!check_valid_new(sfs, dest_path)) {
        fprintf(stderr, "Destination name not valid\n");
        return -1;
    }
    struct sfs_entry *dest_entry = get_entry_by_name(sfs, dest_path);
    if (dest_entry != NULL) {
        if (replace == 0) {
            fprintf(stderr, "Cannot replace existing file \"%s\"\n", dest_path);
            return -1;
        } else {
            if (entry->type != dest_entry->type) {
                fprintf(stderr, "Source and destination of different types\n");
                return -1;
            }
            if (dest_entry->type == SFS_ENTRY_DIR) {
                // check if directory is empty
                if (!is_dir_empty(sfs, dest_path)) {
                    fprintf(stderr, "directory \"%s\" is not empty\n", dest_path);
                    return -1;
                }
            } 
            // delete entry from entry list
            delete_entry(sfs, dest_entry);
        }
    }
    if (entry->type == SFS_ENTRY_DIR) {
        if (move_dir(sfs, source_path, dest_path) != 0) {
            return -1;
        }
    } else if (entry->type == SFS_ENTRY_FILE) {
        if (rename_entry(sfs, entry, dest_path) == -1) {
            return -1;
        }
    }
    return 0;
}


/****f* sfs/sfs_write
 * NAME
 *   sfs_write -- write to file from buffer at an offset
 * DESCRIPTION
 *   Write a specified amount of bytes from buffer to file at a specified offset.
 *   If there is not enough space, the file size is not increased (sfs_resize
 *   must be called in such case).  The offset must not be further than the end
 *   of the file.  If the sum of size and the offset is greater than the tota
 *   size of the file, bytes are written only until the end of the file.
 * PARAMETERS
 *   SFS - the SFS structure variable
 *   path - path of the file in the file system
 *   buf - buffer containing bytes to write
 *   size - number of bytes to read from buffer and to write to file
 *   offset - offset in the file where the bytes should be written
 * RETURN VALUE
 *   On error -1 is returned, on success returns the number of bytes written.
 *****
 */
int sfs_write(SFS *sfs, const char *path, const char *buf, size_t size, off_t offset)
{
    printf("@@@@\tsfs_write: path=\"%s\", size:0x%lx, offset:0x%lx\n", path, size, offset);
    struct sfs_entry *entry = get_file_by_name(sfs, path);
    if (entry != NULL) {
        uint64_t sz;		// number of bytes to write
        uint64_t len = entry->data.file_data->file_len;
        printf("\toffset=0x%06lx\n", offset);
        printf("\tlen=0x%06lx\n", len);
        if ((uint64_t)offset > len) {
            return 0;
        }
        if (offset + size > len) {
            sz = len - offset;
        } else {
            sz = size;
        }
        uint64_t data_offset = sfs->block_size * entry->data.file_data->start_block;
        uint64_t write_start = data_offset + offset;
        printf("\tdata_offset=0x%06lx\n", data_offset);
        printf("\twrite_start=0x%06lx\n", write_start);
        if (fseek(sfs->file, write_start, SEEK_SET) != 0) {
            fprintf(stderr, "f!! seek error\n");
            return -1;
        }
        if (fwrite(buf, sz, 1, sfs->file) != 1) {
            fprintf(stderr, "!! fwrite error\n");
            return -1;
        }
        return sz;
    } else {
        fprintf(stderr, "!! no file error\n");
        return -1;
    }
}


/****f* sfs/free_list_find
 *  NAME
 *    free_list_find -- find consecutive free block in the free list
 *  DESCRIPTION
 *    Finds consecutive block entries starting from *start_block*.
 *  PARAMETERS
 *    SFS - the SFS structure variable
 *    start_block - the minimum block number for the consecutive blocks
 *    length - minimum length of the consecutive blocks
 *  RETURN VALUE
 *    Returns a pointer to pointer to the found block or NULL if could not find.
 ******
 * Pseudocode:
 *   p: pointer to pointer to the current item, initialized to the free list
 *   pfirst: pointer to pointer to the first item of the consecutive blocks
 *           initialized to the free list
 *   tot: the number of blocks found so far
 *   next: the expected start of the next entry if there are no gaps
 *
 *   do while not at end and not enough blocks in tot:
 *      if next <> current start block then	// means there is a gap
 *        *pfirst = p	// set the first block found to current
 *        tot = 0       // what was found was not enough, starting again
 *      end if
 *      if start block not yet reached then
 *        tot += (*p)->length
 *        next = (*p)->start_block + (*p)->length
 *       end if
 *      set p to next
 *   continue
 *   if tot >= length then return pfirst        // found => return
 *   return NULL      // not found
 * end pseudocode
 */
struct block_list **free_list_find(sfs, start_block, length)
    SFS *sfs;
    uint64_t start_block;
    uint64_t length;
{
    struct block_list **p = &sfs->free_list;
    struct block_list **pfirst = p;
    uint64_t tot = 0;
    uint64_t next = 0;
    while (*p != NULL && tot < length) {
        if (next != (*p)->start_block) {
            pfirst = p;
            tot = 0;
        }
        if (start_block <= (*p)->start_block) {
            tot += (*p)->length;
            next = (*p)->start_block + (*p)->length;
        }
        p = &(*p)->next;
    }
    if (tot >= length) {
        return pfirst;
    }
    return NULL;
}


/****f* sfs/free_list_add
 *  NAME
 *    free_list_add -- add free block to the free list
 *  DESCRIPTION
 *    Adds new blocks into the free list.  If the blocks are before and/or
 *    after existing free list entries, the entries are merged.
 *    Blocks can only be merged with blocks which have delfile NULL.
 *  PARAMETERS
 *    SFS - the SFS structure variable
 *    start - the block where the new free area starts
 *    length - the number of blocks in the new free area
 *  RETURN VALUE
 *    Returns 0 on success and -1 on error.
 ******
 * Pseudocode:
 *   item: current block list item
 *   prev: previous item
 *
 *   while item != NULL    // return 0 when found
 *     if cannot merge with current then
 *       if start + length = item->start then
 *         item->start -= length
 *         return 0
 *       else if start + length < item->start
 *         insert new item bewteen prev and item
 *         return 0
 *       end if
 *     else if prev->start + prev->next = start then
 *       if start + length < item->start
 *       || (start + length = item->start && item->delfile != NULL)
 *         prev->length += length
 *         return 0
 *       else if start + length = item->start
 *         prev->length += length + item->length
 *         prev->next = item->next
 *         free prev
 *         return 0
 *       end if
 *       prev = item
 *       item = item->next
 *   end while
 *   return -1
 *
 */
static int free_list_add(SFS *sfs, uint64_t start, uint64_t length)
{
    struct block_list *prev = NULL;
    struct block_list *item = sfs->free_list;

    if (length == 0) {
        return 0;
    }

    printf("[[free_list_add: start=0x%06lx length=0x%06lx]]\n", start, length);
    while (item != NULL) {
        if (prev == NULL || prev->start_block + prev->length < start
                || (prev->start_block + prev->length == start
                    && prev->delfile != NULL)) {
            if (start + length == item->start_block) {
                item->start_block -= length;
                return 0;
            } else if (start + length < item->start_block) {
                struct block_list *new_item = malloc(sizeof(struct block_list));
                new_item->start_block = start;
                new_item->length = length;
                new_item->delfile = NULL;
                new_item->next = item;
                if (prev != NULL) {
                    prev->next = new_item;
                } else {
                    sfs->free_list = new_item;
                }
              return 0;
            }
        } else if (prev->start_block + prev->length == start) {
            if (start + length < item->start_block
                    || (start + length == item->start_block
                        && item->delfile != NULL)) {
                prev->length += length;
                return 0;
            } else if (start + length == item->start_block) {
                prev->length += length + item->length;
                prev->next = item->next;
                free(item);
                return 0;
            }
        }
        prev = item;
        item = item->next;
    }
    return -1;
}


/****f* sfs/free_list_del
 *  NAME
 *    free_list_del -- delete free blocks from free list
 *  DESCRIPTION
 *    Deletes free blocks from the free list, so that they can be used.
 *    Several free list entries can be deleted and at most one can be resized,
 *    so that the beginning is moved forward and the length becomes smaller.
 *    Deleted block entries are freed and if they reference a deleted file, it's
 *    deleted from the entry list.
 *  PARAMETERS
 *    SFS - the SFS structure variable
 *    p_from - pointer to pointer to the block list entry where the search begins
 *    length - the number of blocks needed
 *  RETURN VALUE
 *    Returns 0 on success and -1 on error.
 ******
 * Pseudocode:
 *   rest: remaining blocks to delete, initialized to length
 *   p: pointer to pointer to current block, initialized to p_from
 *
 *   do while (not at end) and (rest <= length):
 *     rest := rest - current length
 *     if delfile in *p then delete it
 *     free current item
 *   continue
 *   // here = at-end or rest < length
 *   if at-end then error (no more space left) and return
 *   last element:
 *     length -= rest
 *     start block: advance rest times
 * end pseidocode
 */
static int free_list_del(SFS *sfs, struct block_list **p_from, uint64_t length)
{
    uint64_t rest = length;
    struct block_list **p = p_from;
    while (*p != NULL && (*p)->length <= rest) {
        struct block_list *tmp = (*p);
        rest -= (*p)->length;
        *p = (*p)->next;
        if (tmp->delfile != NULL) {
            delete_entries(&sfs->free_list, tmp->delfile, tmp->delfile->next);
        }
        free(tmp);
    }
    if (*p == NULL) {
        return -1;
    }
    (*p)->length -= rest;
    (*p)->delfile = NULL;
    (*p)->start_block += rest;
    return 0;
}


/****f* sfs/sfs_resize
 * NAME
 *   sfs_resize -- resize a file
 * DESCRIPTION
 *   Resize a file *path* to size *len*.  Truncate the file if its size
 *   is less than *len*.  Otherwise, fills with null characters.
 * PARAMETERS
 *   SFS - the SFS structure variable
 *   path - the absolute path of the file
 *   len - the new size for the file
 * RETURN VALUE
 *   Returns 0 on success and -1 on error.
 ******
 * Pseudocode:
 *   l0 - initial file size
 *   l1 - final file size
 *   b0 - initial number of blocks used by the file
 *   b1 - number of block needed for its new size
 *   s0 - the first block of the file
 *   file_entry - file entry in the Index Area
 *
 *   if b1 > b0 then    // file is to small
 *     p_next = free_list_find(sfs, s0 + l0, b1 - b0)
 *     if next <> null then     // enough space right after the file
 *       free_list_del(sfs, p_next, b1 - b0)
 *     else                     // not enough space: find some blocks
 *       delete the file (add it to free list)
 *       p_blocks = free_list_find(sfs, 0, b1)
 *       delete from free list
 *       copy the file contents: b0*bs bytes from s0 to start of p_blocks
 *     end if
 *     set file_entry start: s0
 *   else if b0 > b1    // file is to big => free b0-b1 blocks after the file
 *     free_list_add(sfs, s0 + b0, b0 - b1)
 *   end if
 *   if l1 > l0 then
 *     fill l1 - l0 bytes after the file contents with '\0'
 *   end if
 *   write_entry(sfs, file_entry)
 */
int sfs_resize(SFS *sfs, const char *path, off_t len)
{
    const uint64_t bs = sfs->block_size;
    printf("@@@@\tsfs_resize: name=\"%s\" length=%ld\n", path, len);
    struct sfs_entry *file_entry = get_file_by_name(sfs, path);
    if (file_entry == NULL) {
        fprintf(stderr, "file \"%s\" does not exists\n", path);
        return -1;
    }
    if (file_entry->type != SFS_ENTRY_FILE) {
        fprintf(stderr, "\"%s\" is not a file\n", path);
        return -1;
    }
    const uint64_t l0 = file_entry->data.file_data->file_len;
    const uint64_t l1 = (uint64_t)len;
    const uint64_t b0 = (l0 + bs - 1) / bs;
    const uint64_t b1 = (len + bs - 1) / bs;
    const uint64_t s0 = file_entry->data.file_data->start_block;
    uint64_t s1;
    if (b1 > b0) {
        struct block_list **p_next = free_list_find(sfs, s0 + b0, b1 - b0);
        if (*p_next != NULL && (*p_next)->start_block == s0 + b0) {
            if (l0 == 0) {
                s1 = (*p_next)->start_block;
                file_entry->data.file_data->start_block = s0;
            } else {
                s1 = s0;
            }
            free_list_del(sfs, p_next, b1 - b0);
        } else {
            if (free_list_add(sfs, s0, b0) != 0) {
                return -1;
            }
            struct block_list **p_blocks = free_list_find(sfs, 0, b1);
            if (*p_blocks == NULL) {
                return -1;
            }
            s1 = (*p_blocks)->start_block;
            if (free_list_del(sfs, p_blocks, b1) != 0) {
                return -1;
            }
            for (uint64_t i = 0; i < b0; ++i) {
                char buf[bs];
                fseek(sfs->file, (s0 + i) * bs, SEEK_SET);
                fread(buf, bs, 1, sfs->file);
                fseek(sfs->file, (s1 + i) * bs, SEEK_SET);
                fwrite(buf, bs, 1, sfs->file);
            }
            file_entry->data.file_data->start_block = s1;
        }
    } else if (b0 > b1) {
        if (free_list_add(sfs, s0 + b1, b0 - b1)) {
            return -1;
        }
        s1 = s0;
    }
    if (l1 > l0) {
        char c[l1-l0];
        memset(c, 0, l1-l0);
        fseek(sfs->file, s1 * bs + l0, SEEK_SET);
        fwrite(&c, l1 - l0, 1, sfs->file);
    }
    file_entry->data.file_data->file_len = l1;
    file_entry->data.file_data->end_block = s1 + (l1 + sfs->block_size - 1) / sfs->block_size - 1;
    if (write_entry(sfs, file_entry) != 0) {
        return -1;
    }
    return 0;
}
