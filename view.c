#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "sfs.h"

void print_super(struct S_SFS_SUPER *super) {
    printf("super:\n");
    printf("    time_stamp: %" PRIx64 "\n", super->time_stamp);
    printf("    data_size: %" PRIx64 "\n", super->data_size);
    printf("    index_size: %" PRIx64 "\n", super->index_size);
    printf("    magic: 0x%02x%02x%02x\n", super->magic[0],
        super->magic[1], super->magic[2]);
    printf("    version: %02x\n", super->version);
    printf("    total_blocks: %" PRIx64 "\n", super->total_blocks);
    printf("    rsvd_blocks: %" PRIx32 "\n", super->rsvd_blocks);
    printf("    block_size: %02x\n", super->block_size);
    printf("    crc: %02x\n", super->crc);
}

void print_volume(struct S_SFS_VOL_ID *volume) {
    printf("volume:\n");
    printf("    type: %02x\n", volume->type);
    printf("    crc: %02x\n", volume->crc);
    printf("    resvd: %04x\n", volume->resvd);
    printf("    time_stamp: %" PRIx64 "\n", volume->time_stamp);
    printf("    name: %s\n", volume->name);
}

void print_dir_entry(struct S_SFS_DIR *dir) {
    printf("dir:\n");
    printf("    type: %02x\n", dir->type);
    printf("    crc: %02x\n", dir->crc);
    printf("    num_cont: %d\n", dir->num_cont);
    printf("    time_stamp: %" PRIx64 "\n", dir->time_stamp);
    printf("    name: %s\n", dir->name);
}

void print_file_entry(struct S_SFS_FILE *file) {
    printf("file:\n");
    printf("    type: %02x\n", file->type);
    printf("    crc: %02x\n", file->crc);
    printf("    num_cont: %d\n", file->num_cont);
    printf("    time_stamp: %" PRIx64 "\n", file->time_stamp);
    printf("    start_block: %" PRIx64 "\n", file->start_block);
    printf("    end_block: %" PRIx64 "\n", file->end_block);
    printf("    file_len: %" PRIx64 "\n", file->file_len);
    printf("    name: %s\n", file->name);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <image file name>\n", argv[0]);
        return 1;
    }

    struct sfs *sfs = sfs_make(argv[1]);
    struct S_SFS_SUPER *super = sfs_get_super(sfs);
    print_super(super);

    struct S_SFS_VOL_ID *volume = sfs_get_volume(sfs);
    print_volume(volume);

    struct sfs_entry_list *list = sfs_get_entry_list(sfs);
    while (list != NULL) {
        if (list->type == SFS_ENTRY_FILE) {
            print_file_entry(list->entry.file);
        } else if (list->type == SFS_ENTRY_DIR) {
            print_dir_entry(list->entry.dir);
        } else {
//            printf("<entry of type 0x%02x>\n", list->type);
        }
        list = list->next;
    }
    /* TODO: enter prompt: list all, view file ... */
    sfs_terminate(sfs);
    return 0;
}
