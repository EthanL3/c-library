#include "disk.h"
#include <string.h>  // For memset, strcmp, strlen, strcpy
#include <stddef.h> 
#include <stdbool.h>
#include <stdlib.h> // For malloc

#define MAX_F_NAME 15
#define MAX_FILDES 32
#define MAX_FILES 64
#define MAX_FILE_SIZE 16

struct super_block {
    int fat_idx; // First block of the FAT
    int fat_len; // Length of FAT in blocks
    int dir_idx; // First block of directory
    int dir_len; // Length of directory in blocks
    int data_idx; // First block of file-data
};

struct dir_entry {
    int used; // Is this file-”slot” in use
    char name [MAX_F_NAME + 1]; // DOH!
    int size; // file size
    int head; // first data block of file
    int ref_cnt; // how many open file descriptors are there? ref_cnt > 0 -> cannot delete file
};

struct file_descriptor {
    int is_used; // fd in use
    int file; // the first block of the file (f) to which fd refers too
    int offset; // where in the file the fd is
};

struct super_block* fs;
struct file_descriptor fildes_array[MAX_FILDES]; // 32
int* FAT; //  Array of block_idx’es (or eof, or free)
struct dir_entry* DIR; // Will be populated with the directory data
int fs_mounted = 0;


int make_fs(char *disk_name) {
    fs = malloc(sizeof(struct super_block));
    FAT = malloc(DISK_BLOCKS * sizeof(int));
    memset(FAT, 0, DISK_BLOCKS * sizeof(int));
    DIR = malloc(MAX_FILES * sizeof(struct dir_entry));
    memset(DIR, 0, MAX_FILES * sizeof(struct dir_entry));

    if (make_disk(disk_name) < 0) {
        return -1; // Failed to create or open the disk
    }

    if (open_disk(disk_name) < 0) {
        return -1; // Failed to open the disk
    }

    fs->fat_idx = 1; // FAT starts at block 1
    fs->fat_len = 8; // FAT length in blocks
    fs->dir_idx = 9; // Directory starts after FAT
    fs->dir_len = 1; // Directory length in blocks
    fs->data_idx = 10; // Start at index 10

    int i;
    for(i = 0; i < DISK_BLOCKS; i++) {
        FAT[i] = 0;
    }

    for(i = 0; i < MAX_FILES; i++) {
        DIR[i].used = 0;
    }

    // write the superblock to disk
    if (block_write(0, (char *) fs) < 0) {
        return -1;
    }

    // write the FAT and directory to disk
    int entries_per_block = BLOCK_SIZE / sizeof(int);
    for (i = 0; i < fs->fat_len; i++) {
        if (block_write(fs->fat_idx + i, (char *) (FAT + i * entries_per_block)) < 0) {
            return -1;
        }
    }

    entries_per_block = BLOCK_SIZE / sizeof(struct dir_entry);
    for (i = 0; i < fs->dir_len; i++) {
    if (block_write(fs->dir_idx + i, (char *) (DIR + i * entries_per_block)) < 0) {
            return -1;
        }
    }
    if (close_disk() < 0) {
        return -1; // Failed to close the disk
    }
    return 0; // Success
}

int mount_fs(char *disk_name) {
    if (open_disk(disk_name) < 0) {
        return -1; // Failed to open the disk
    }
    if (block_read(0, (char *) fs) < 0) {
        return -1; // Failed to read the superblock
    }
    FAT = malloc(DISK_BLOCKS * sizeof(int));
    int entries_per_block = BLOCK_SIZE / sizeof(int);
    int i;
    for (i = 0; i < fs->fat_len; i++) {
        if (block_read(fs->fat_idx + i, (char *) (FAT + i * entries_per_block)) < 0) {
            return -1; 
        }
    }
    DIR = malloc(MAX_FILES * sizeof(struct dir_entry));
    entries_per_block = BLOCK_SIZE / sizeof(struct dir_entry);
    for(i = 0; i < fs->dir_len; i++) {
        if (block_read(fs->dir_idx + i, (char *) (DIR + i * entries_per_block)) < 0) {
            return -1;
        }
    }
    for(i = 0; i < MAX_FILDES; i++) {
        fildes_array[i].is_used = 0;
    }
    fs_mounted = 1;
    return 0;
}

int umount_fs(char *disk_name) {
    if (block_write(0, (char *) fs) < 0) {
        return -1; // Failed to write the superblock
    }
    int entries_per_block = BLOCK_SIZE / sizeof(int);
    int i;
    for (i = 0; i < fs->fat_len; i++) {
        if (block_write(fs->fat_idx + i, (char *) (FAT + i * entries_per_block)) < 0) {
            return -1; // Failed to write FAT
        }
    }
    entries_per_block = BLOCK_SIZE / sizeof(struct dir_entry);
    for (i = 0; i < fs->dir_len; i++) {
        if (block_write(fs->dir_idx + i, (char *) (DIR + i * entries_per_block)) < 0) {
            return -1; // Failed to write directory
        }
    }
    if (close_disk() < 0) {
        return -1; // Failed to close the disk
    }
    fs_mounted = 0;
    return 0;
}

int fs_open(char *name) {
    int dir_index = -1;
    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (DIR[i].used && strcmp(DIR[i].name, name) == 0) {
            dir_index = i;
            break;
        }
    }

    if (dir_index == -1) {
        return -1; // File not found
    }

    for (i = 0; i < MAX_FILDES; i++) {
        if (fildes_array[i].is_used == 0) {
            fildes_array[i].is_used = 1;
            fildes_array[i].file = dir_index;
            fildes_array[i].offset = 0;
            DIR[dir_index].ref_cnt++;
            return i; // Return the file descriptor
        }
    }
    return -1; // No free file descriptors
}

int fs_close(int fildes) {
    if (fildes < 0 || fildes >= MAX_FILDES || fildes_array[fildes].is_used == 0) {
        return -1; // Invalid or closed file descriptor
    }
    int file_head = fildes_array[fildes].file;
    fildes_array[fildes].is_used = 0;
    fildes_array[fildes].file = 0;
    fildes_array[fildes].offset = 0;

    int i;
    for (i = 0; i < MAX_FILES; i++) {
        if (DIR[i].used && DIR[i].head == file_head) {
            DIR[i].ref_cnt--;
            break;
        }
    }
    return 0; // Success
}

int fs_create(char *name) {
    if (strlen(name) > MAX_F_NAME || strlen(name) == 0) {
        return -1; // File name too long
    }
    int i;
    for(i = 0; i < MAX_FILES; i++) {
        if (strcmp(DIR[i].name, name) == 0) {
            return -1; // File already exists
        }
    }

    int dir_index = -1;
    for(i = 0; i < MAX_FILES; i++) {
        if (DIR[i].used == 0) {
            dir_index = i;
            break;
        }
    }
    if (dir_index == -1) {
        return -1; // No free directory slots
    }
    
    DIR[dir_index].used = 1;
    strcpy(DIR[dir_index].name, name);
    DIR[dir_index].size = 0;
    DIR[dir_index].head = -1;
    DIR[dir_index].ref_cnt = 0;
    
    return 0; // Success
}

int fs_delete(char *name) {
    int i;
    int dir_index = -1;
    int block_index = -1;
    for(i = 0; i < MAX_FILES; i++) {
        if (strcmp(DIR[i].name, name) == 0) {
            if (DIR[i].ref_cnt > 0) {
                return -1; // File is open
            }
            dir_index = i;
            block_index = DIR[i].head;
            break;
        }
    }
    if (dir_index == -1) {
        return -1; // File not found
    }

    char block_buf[BLOCK_SIZE];
    memset(block_buf, '\0', BLOCK_SIZE);
    while (block_index != -1) {
        int next = FAT[block_index];
        if (block_write(block_index, block_buf) < 0) {
            return -1; // Failed to write block
        }
        FAT[block_index] = 0; // Mark block as free
        block_index = next;
    }
    DIR[dir_index].used = 0;
    memset(DIR[dir_index].name, '\0', MAX_F_NAME + 1);
    DIR[dir_index].size = 0;
    DIR[dir_index].head = -1;
    DIR[dir_index].ref_cnt = 0;
    if (block_write(fs->dir_idx, (char *) DIR) < 0) {
        return -1; // Failed to write directory
    }
    return 0; // Success
}

int fs_read(int fildes, void *buf, size_t nbyte) {
    if (fildes < 0 || fildes >= MAX_FILDES || fildes_array[fildes].is_used == 0) {
        return -1; // Invalid or closed file descriptor
    }

    int dir_index = fildes_array[fildes].file;
    int offset = fildes_array[fildes].offset;
    int file_size = DIR[dir_index].size;
    int num_bytes_read = 0;

    // Check if we are at the end of the file
    if (offset >= file_size) {
        return 0; // EOF
    }

    int current_block = DIR[dir_index].head;
    int blocks_to_skip = offset / BLOCK_SIZE;
    int i;
    // Skip to the correct block
    for (i = 0; i < blocks_to_skip; i++) {
        if(current_block == -1 || current_block == 0) {
            return 0;
        }
        current_block = FAT[current_block];
    }

    int block_offset = offset % BLOCK_SIZE;
    char read_buf[BLOCK_SIZE];

    // Read the file
    while(num_bytes_read < nbyte && current_block != -1 && current_block != 0) {
        if (block_read(current_block, read_buf) < 0) {
            return -1;
        }
        int bytes_left = file_size - (offset + num_bytes_read);
        if (bytes_left <= 0) {
            break;
        }

        int bytes_to_read = BLOCK_SIZE - block_offset;
        if (bytes_to_read > bytes_left) {
            bytes_to_read = bytes_left;
        }
        if (bytes_to_read > nbyte - num_bytes_read) {
            bytes_to_read = nbyte - num_bytes_read;
        }

        memcpy((char *)buf + num_bytes_read, read_buf + block_offset, bytes_to_read);
        num_bytes_read += bytes_to_read;
        block_offset = 0;
        current_block = FAT[current_block];
    }
    fildes_array[fildes].offset += num_bytes_read;
    return num_bytes_read;
}

int get_next_block() {
    int i;
    for (i = fs->data_idx; i < DISK_BLOCKS; i++) {
        if (FAT[i] == 0) {
            FAT[i] = -1;
            return i;
        }
    }
    return -1;
}

int fs_write(int fildes, void *buf, size_t nbyte) {
    if (fildes < 0 || fildes >= MAX_FILDES || fildes_array[fildes].is_used == 0) {
        return -1; // Invalid or closed file descriptor
    }

    // Check if disk is full
    int free_blocks = 0;
    int i;
    for(i = fs->data_idx; i < DISK_BLOCKS; i++) {
        if (FAT[i] == 0) {
            free_blocks++;
        }
    }
    if (free_blocks == 0) {
        return 0; // Disk is full
    }
    int free_bytes = free_blocks * BLOCK_SIZE;
    if (nbyte > free_bytes) {
        nbyte = free_bytes;
    }

    int dir_index = fildes_array[fildes].file;
    int offset = fildes_array[fildes].offset;
    int num_bytes_written = 0;

    // Check if we are at the end of the file
    if (DIR[dir_index].size + nbyte > BLOCK_SIZE * 4096) {
        nbyte = BLOCK_SIZE * 4096 - DIR[dir_index].size;
        if (nbyte <= 0) {
            return 0;
        }
    }

    int blocks_to_skip = offset / BLOCK_SIZE;
    int block_offset = offset % BLOCK_SIZE;
    int current_block = DIR[dir_index].head;

    if (current_block == -1) {
        int new_block = get_next_block();
        if (new_block == -1) {
            return 0;
        }
        DIR[dir_index].head = new_block;
        current_block = new_block;
    }

    // Skip to the correct block
    for(i = 0; i < blocks_to_skip; i++) {
        if (FAT[current_block] == -1) {
            int new_block = get_next_block();
            if (new_block == -1) {
                return 0;
            }
            FAT[current_block] = new_block;
            current_block = new_block;
        }
        else {
            current_block = FAT[current_block];
        }
    }

    char write_buf[BLOCK_SIZE];
    // Write the file
    while(num_bytes_written < nbyte) {
        if(block_read(current_block, write_buf) < 0) {
            return num_bytes_written;
        }

        int bytes_to_write = BLOCK_SIZE - block_offset;
        if (bytes_to_write > nbyte - num_bytes_written) {
            bytes_to_write = nbyte - num_bytes_written;
        }
        memcpy(write_buf + block_offset, (char *) buf + num_bytes_written, bytes_to_write);
        
        if (block_write(current_block, write_buf) < 0) {
            return num_bytes_written;
        }
        num_bytes_written += bytes_to_write;
        fildes_array[fildes].offset += bytes_to_write;
        if (fildes_array[fildes].offset > DIR[dir_index].size) {
            DIR[dir_index].size = fildes_array[fildes].offset;
        }
        block_offset = 0;

        if (FAT[current_block] == -1) {
            if (num_bytes_written < nbyte) {
                int new_block = get_next_block();
                if (new_block == -1) {
                    return num_bytes_written;
                }
                FAT[current_block] = new_block;
                current_block = new_block;
            }
        }
        else {
            current_block = FAT[current_block];
        }
        memset(write_buf, '\0', BLOCK_SIZE);
    }
    return num_bytes_written;
}

int fs_get_filesize(int fildes) {
    if (fildes < 0 || fildes >= MAX_FILDES || fildes_array[fildes].is_used == 0) {
        return -1; // Invalid or closed file descriptor
    }
    return DIR[fildes_array[fildes].file].size;
}

int fs_listfiles(char ***files) {
    char **file_list = malloc(MAX_FILES * sizeof(char *));
    int count = 0;
    int i;
    for(i = 0; i < MAX_FILES; i++) {
        if (DIR[i].used) {
            file_list[count] = malloc(MAX_F_NAME + 1);
            strcpy(file_list[count], DIR[i].name);
            count++;
        }
    }
    file_list[count] = NULL;
    *files = file_list;
    return 0;
}

int fs_lseek(int fildes, off_t offset) {
    if (fildes < 0 || fildes >= MAX_FILDES || fildes_array[fildes].is_used == 0) {
        return -1; // Invalid or closed file descriptor
    }
    if (offset < 0 || offset > DIR[fildes_array[fildes].file].size) {
        return -1; // Invalid offset
    }
    fildes_array[fildes].offset = offset;
    return 0;
}

int fs_truncate(int fildes, off_t length) {
    if (fildes < 0 || fildes >= MAX_FILDES || fildes_array[fildes].is_used == 0) {
        return -1; // Invalid or closed file descriptor
    }
    if (length < 0 || length > DIR[fildes_array[fildes].file].size) {
        return -1; // Invalid length
    }

    char block_buf[BLOCK_SIZE];
    int file_size = DIR[fildes_array[fildes].file].size;
    int i;
    int start_block = fildes_array[fildes].file;
    for(i = file_size; i >= length; i--) {
        block_read(start_block + i/BLOCK_SIZE, block_buf);
        memset(block_buf + (i % BLOCK_SIZE), '\0', 1);
        block_write(start_block + i/BLOCK_SIZE, block_buf);
    }
    fildes_array[fildes].offset = length;
    DIR[fildes_array[fildes].file].size = length;
    return 0;
}