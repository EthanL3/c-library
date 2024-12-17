# Project 5: File System:
For this file system, I used the FAT, DIR, and fildes_array data structures to organize my files. The FAT is an int array where each entry i is the next block of the file. If i = 0, then the block is "free", and if i = -1, then the block is "end of file". The directory structure is an array of dir_entry, which contains info regarding each file. Lastly, the fildes_array is an array of file_descriptors, which contain info on each file descriptor (max 32), including the offset (seek pointer), first block, and if the file descriptor is in use. Unlike the FAT and DIR data structures, the fildes_array is not written to disk and therefore does not persist beyond the current execution of the system.  
The hardest problem I had when doing this project was implementing the fs_read and fs_write functions. In these functions, a void* buf is inputted where the file contents are either read from or written to that pointer. Initially, I tried calling block_read and block_read, and directly inputting the void* buf to those functions. However, I realized this is wrong because block_read and block_write operate at a block level, reading and writing an entire block at a time. The void* buf might not perfectly align with block boundaries, and therefore a temporary buffer to handle the block-level functions before using buf. This allowed me to correctly execute partial reads and writes, and prevent overwrites or overrreads. 