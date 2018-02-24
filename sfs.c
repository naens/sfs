#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfs.h"

struct sfs *sfs_make(char *filename) {
    struct sfs *sfs = malloc(sizeof(struct sfs));
    sfs->super = NULL;
    sfs->entry_list = NULL;
    sfs->file = fopen(filename, "r");
    if (sfs->file == NULL) {
        fprintf(stderr, "file error\n");
        return NULL;
    }
    return sfs;
}

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

struct S_SFS_SUPER *sfs_get_super(struct sfs *sfs) {
    if (sfs->super != NULL)
        return sfs->super;

    sfs->super = malloc(sizeof(struct S_SFS_SUPER));
    uint8_t buf[42];
    uint8_t *cbuf = buf;

    if (fseek(sfs->file, 0x18e, SEEK_SET) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    fread(buf, 42, 1, sfs->file);
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
    memcpy(&sfs->super->crc, cbuf, sizeof(sfs->super->crc));

    if (!check_crc(buf + m, 42 - m)) {
        return NULL;
    }    
    return sfs->super;
}


struct S_SFS_VOL_ID *sfs_get_volume(struct sfs *sfs) {
    struct sfs_entry_list *list= sfs_get_entry_list(sfs);
    while (list != NULL && list->type != SFS_ENTRY_VOL_ID)
        list = list->next;
    if (list != NULL)
        return list->entry.volume;
    else
        return NULL;
}

struct S_SFS_VOL_ID *make_volume_entry(uint8_t *buf) {
    struct S_SFS_VOL_ID *volume = malloc(sizeof(struct S_SFS_VOL_ID));
    uint8_t *cbuf = buf;
    memcpy(&volume->type, cbuf, sizeof(volume->type));
    cbuf += sizeof(volume->type);
    memcpy(&volume->crc, cbuf, sizeof(volume->crc));
    cbuf += sizeof(volume->crc);
    memcpy(&volume->resvd, cbuf, sizeof(volume->resvd));
    cbuf += sizeof(volume->resvd);
    memcpy(&volume->time_stamp, cbuf, sizeof(volume->time_stamp));
    cbuf += sizeof(volume->time_stamp);
    memcpy(volume->name, cbuf, sizeof(volume->name));
    if (!check_crc(buf, 64)) {
        return NULL;
    }
    return volume;    
}

struct S_SFS_START *make_start_entry(uint8_t *buf) {
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

struct S_SFS_UNUSED *make_unused_entry(uint8_t *buf) {
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

uint8_t *rw_cont_name(int init_len, uint8_t *init_buf, FILE *f, int n_cont) {
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

struct S_SFS_DIR *make_dir_entry(uint8_t *buf, FILE *f) {
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
    dir->name = rw_cont_name(DIR_NAME_LEN, cbuf, f, dir->num_cont);
    if (!check_crc(buf, 64 * (1 + dir->num_cont))) {
        return NULL;
    }    
    return dir;    
}

struct S_SFS_FILE *make_file_entry(uint8_t *buf, FILE *f) {
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
    f_entry->name = rw_cont_name(FILE_NAME_LEN, cbuf, f, f_entry->num_cont);
    if (!check_crc(buf, 64 * (1 + f_entry->num_cont))) {
        return NULL;
    }    
    return f_entry;    
}

struct S_SFS_UNUSABLE *make_unusable_entry(uint8_t *buf) {
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

struct S_SFS_DIR_DEL *make_dir_del_entry(uint8_t *buf, FILE *f) {
    struct S_SFS_DIR_DEL *dir = malloc(sizeof(struct S_SFS_DIR_DEL));
    uint8_t *cbuf = buf;
    memcpy(&dir->type, cbuf, sizeof(dir->type));
    cbuf += sizeof(dir->type);
    memcpy(&dir->crc, cbuf, sizeof(dir->crc));
    cbuf += sizeof(dir->crc);
    memcpy(&dir->num_cont, cbuf, sizeof(dir->num_cont));
    cbuf += sizeof(dir->num_cont);
    memcpy(&dir->time_stamp, cbuf, sizeof(dir->time_stamp));
    cbuf += sizeof(dir->time_stamp);
    dir->name = rw_cont_name(DIR_NAME_LEN, cbuf, f, dir->num_cont);
    if (!check_crc(buf, 64 * (1 + dir->num_cont))) {
        return NULL;
    }    
    return dir;    
}

struct S_SFS_FILE_DEL *make_file_del_entry(uint8_t *buf, FILE *f) {
    struct S_SFS_FILE_DEL *f_entry = malloc(sizeof(struct S_SFS_FILE_DEL));
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
    f_entry->name = rw_cont_name(FILE_NAME_LEN, cbuf, f, f_entry->num_cont);
    if (!check_crc(buf, 64 * (1 + f_entry->num_cont))) {
        return NULL;
    }    
    return f_entry;    
}

/* file must be positioned on valid entry */
union entry make_entry(FILE *f) {
    union entry entry;
    uint8_t buf[64];
    fread(buf, 64, 1, f);
    switch (buf[0]) {
    case SFS_ENTRY_VOL_ID:
        entry.volume = make_volume_entry(buf);
        break;
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
        entry.file = make_file_entry(buf, f);
        break;
    case SFS_ENTRY_UNUSABLE:
        entry.unusable = make_unusable_entry(buf);
        break;
    case SFS_ENTRY_DIR_DEL:
        entry.dir_del = make_dir_del_entry(buf, f);
        break;
    case SFS_ENTRY_FILE_DEL:
        entry.file_del = make_file_del_entry(buf, f);
        break;
    default:
        fprintf(stderr, "bad entry type 0x%02x\n", buf[0]);
        entry.null = NULL;
    }
    return entry;
}

struct sfs_entry_list *sfs_get_entry_list(struct sfs *sfs) {
    if (sfs->entry_list != NULL)
        return sfs->entry_list;

    /* find location of entries */
    int n_entries = sfs->super->index_size / 64;
    if (fseek(sfs->file, -(64 * n_entries), SEEK_END) != 0) {
        fprintf(stderr, "fseek error\n");
        return NULL;
    }

    union entry entry = make_entry(sfs->file);
    while (entry.null != NULL) {
        struct sfs_entry_list *list = malloc(sizeof(struct sfs_entry_list));
        list->entry = entry;
        list->type = ((uint8_t*)entry.null)[0];
        list->next = sfs->entry_list;
        sfs->entry_list = list;
        if (list->type == SFS_ENTRY_VOL_ID)
            break;
        if (feof(sfs->file))
            break;
        entry = make_entry(sfs->file);
    }
    return sfs->entry_list;
}

void sfs_terminate(struct sfs *sfs) {
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
                    free(entry.dir_del->name);
                    break;
                case SFS_ENTRY_FILE_DEL:
                    free(entry.file_del->name);
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
}
