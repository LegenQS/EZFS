# ezfs: A Simple File System Implementation

ezfs is a simple file system implementation designed for educational purposes. It is a single-file system that supports basic file system operations, such as file creation, deletion, reading, and writing.

## File System Overview

ezfs is a simple file system that uses a block-based storage system. The file system is stored in a single file that contains a super block, an inode store, and data blocks.

The super block stores information about the file system, such as the number of inodes, the number of data blocks, and the size of the inode store. The inode store is used to store information about files and directories, such as their permissions, ownership, timestamps, and data block numbers. The data blocks are used to store the actual file and directory data.

## Code Overview

The ezfs code is organized into several files. The header files are:
- `ezfs.h`: This header file contains the main data structures and macros used throughout the implementation. It defines the following structures:
  - `ezfs_inode`: a structure that contains metadata about the file, such as permissions, size, and access times.
  - `ezfs_dir_entry`: a structure that represents a directory entry, which maps file names to inode numbers.
  - `ezfs_super_block`: a structure that represents the superblock, which contains information about the file system, such as the version, magic number, and free inodes and data blocks.  
  The header file also defines various constants, including the block size, maximum number of inodes, and root inode number. Additionally, it includes macros for setting, testing, and clearing bit arrays of integers.
- `ezfs_ops.h`: This header file contains function declarations related to operating on EZFS filesystem inodes and directory entries. Specifically, it defines the `ezfs_get_inode` function which returns an inode struct given a superblock and inode number, and the `ezfs_find_entry` function which searches for a directory entry given a parent directory inode and child name.
- `myez.c`: This file implements all the functionalities for the file system to mount/umount, make modifications to files etc..

## Code Explanation

The code for the ezfs file system is written in C and uses the Linux kernel data structures and functions. The file system operations are implemented using a set of functions that interact with the ezfs data structures and the underlying storage device.

- `find_inode_by_number` is used to find the inode for a given inode number. It takes a pointer to the super block, the inode number, and a pointer to a buffer head. It first checks if the inode number is within the valid range, and then calculates the offset of the inode within the inode store. It reads the inode store block using the sb_bread function, and returns a pointer to the inode.

- `ezfs_evict_inode` is called when an inode is being evicted from the inode cache. It first retrieves the super block and the inode from the inode private data. It then clears the inode and data block information from the super block. It also clears the inode skeleton, truncates the inode pages, and releases the buffer head and the ezfs inode. Finally, it releases the resources used by the ezfs file system.

- `get_next_block` is used to find the next available data block in the file system. It loops through the data blocks starting from the root data block, and checks if the data block is free by using the IS_SET macro. If a free data block is found, it returns its number. If no free data blocks are found, it returns 0.

- `get_next_inode` is used to find the next available inode in the file system. It loops through the inodes starting from the root inode, and checks if the inode is free by using the IS_SET macro. If a free inode is found, it sets the inode as used by using the SETBIT macro, and returns its number. If no free inodes are found, it returns -1.

- `ezfs_write_inode` is called when an inode is being written to disk. It first retrieves the ezfs inode and the buffer head for the inode. It then updates the ezfs inode with the inode metadata, marks the buffer head as dirty, and syncs the buffer head to disk if necessary. Finally, it releases the buffer head and the ezfs lock.

- `ezfs_get_inode` retrieves an inode for a given inode number and directory. It is used when a file or directory needs to be accessed.

- `ezfs_find_entry` searches a directory for a given filename and returns a pointer to the corresponding directory entry. It is used when a file or directory needs to be looked up.

- `ezfs_create_inode` creates a new inode for a file or directory. It sets the inode's attributes and updates the superblock to reflect the new inode. It is used when a new file or directory is created.

- `ezfs_readdir` reads the contents of a directory and returns them as directory entries. It is used when the contents of a directory need to be listed.

- `ezfs_lookup` is used to search for a directory entry by name in a given directory inode. If the entry exists, it returns the associated inode. If it does not exist, it returns an error.

`ezfs_move_block` is used to move a block of data from one location to another. It is used to move data blocks when a file is resized or relocated on disk.

- `ezfs_move_blocks` is used to move multiple blocks of data from one location to another. It is called by ezfs_get_block to move blocks when a file is resized or relocated on disk.

- `ezfs_get_block` is called by the file system when it needs to map a logical block number to a physical block number on disk. It also handles the allocation of new blocks on disk if necessary.

- `ezfs_readpage` is used to read data from disk into a page cache page.

- `ezfs_writepage` is used to write data from a page cache page to disk.

- `ezfs_write_failed` is called when a write operation fails. It truncates the page cache for the affected file.

- `ezfs_write_begin` is called before a write operation begins. It performs various checks and prepares the page cache for writing.

- `ezfs_get_inode` creates a new inode, associates it with a buffer head, initializes some of its parameters, and returns it.

- `ezfs_fill_super` is called when the file system is mounted, reads the superblock and inode store, initializes some parameters, and creates the root inode.

- `ezfs_get_tree` calls get_tree_bdev with the fill_super function to get the file system tree.

- `ezfs_init_fs_context` allocates memory for and initializes the file system context.

- `ezfs_kill_sb` is called when the file system is unmounted and cleans up any remaining resources. It destroys the mutex, frees memory, and releases buffer heads.

## Instruction on EZFS
create a disk image and assign it to a loop device
```
$ dd bs=4096 count=400 if=/dev/zero of=~/ez_disk.img
# losetup --find --show ~/ez_disk.img
```
compile and run the `format_disk_as_ezfs.c` code
```
# ./format_disk_as_ezfs /dev/loop
```
use `insmod` command to load the kernel module
```
# mkdir /mnt/ez
# insmod ezfs-ARCH.ko
# mount -t ezfs /dev/loop /mnt/ez
```
After this, you can use `ls`, `cd`, `cat`, `dd`, `echo`, `stat`, `touch` and etc. commands for this file system.  
Still working on functions like dir create/delete, file deletion and rename etc..
