#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* These are the same on a 64-bit architecture */
#define timespec64 timespec

#include "ezfs.h"

void passert(int condition, char *message)
{
	printf("[%s] %s\n", condition ? " OK " : "FAIL", message);
	if (!condition)
		exit(1);
}

uint64_t get_length(char *path)
{
	FILE *fp;
	uint64_t size;

	fp = fopen(path, "r");
	if (fp == NULL) {
		printf("error when open file!\n");
		exit(1);
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	return size;
}

void inode_reset(struct ezfs_inode *inode)
{
	struct timespec current_time;

	/* In case inode is uninitialized/previously used */
	memset(inode, 0, sizeof(*inode));
	memset(&current_time, 0, sizeof(current_time));

	/* These sample files will be owned by the first user and group on the system */
	inode->uid = 1000;
	inode->gid = 1000;

	/* Current time UTC */
	clock_gettime(CLOCK_REALTIME, &current_time);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time;
}

void dentry_reset(struct ezfs_dir_entry *dentry)
{
	memset(dentry, 0, sizeof(*dentry));
}

int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret, len;
	struct ezfs_super_block sb;
	struct ezfs_inode inode;
	struct ezfs_dir_entry dentry;
	FILE *fp;

	char *name_contents = "Qishuo Wang and Ruochen Li\n";
	char *hello_contents = "Hello World!\n";
	uint64_t img_size, txt_size;
	char *img_path = "./big_files/big_img.jpeg";
	char *txt_path = "./big_files/big_txt.txt";
	char big_buf[409600];
	char buf[EZFS_BLOCK_SIZE];
	const char zeroes[EZFS_BLOCK_SIZE] = { 0 };

	if (argc != 2) {
		printf("Usage: ./format_disk_as_ezfs DEVICE_NAME.\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}
	memset(&sb, 0, sizeof(sb));

	sb.version = 1;
	sb.magic = EZFS_MAGIC_NUMBER;

	/* The first two inodes and datablocks are taken by the root and
	 * hello.txt file, respectively. Mark them as such.
	 */
	// set up inode for root, hello.txt, subdir, names.txt, big_img and big_txt
	for (int i = 0; i <= 5; i++)
		SETBIT(sb.free_inodes, i);

	SETBIT(sb.free_data_blocks, 0); // root
	SETBIT(sb.free_data_blocks, 1); // hello
	SETBIT(sb.free_data_blocks, 2); // subdir
	for (int i = 3; i <= 13; i++)
		SETBIT(sb.free_data_blocks, i);

	img_size = get_length(img_path);
	txt_size = get_length(txt_path);

	/* Write the superblock to the first block of the filesystem. */
	ret = write(fd, (char *)&sb, sizeof(sb));
	passert(ret == EZFS_BLOCK_SIZE, "Write superblock");

	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 3; // add 1 to 2 because add another directory
	inode.data_block_number = EZFS_ROOT_DATABLOCK_NUMBER;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	/* Write the root inode starting in the second block. */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write root inode");

	/* Write hello.txt inode */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.data_block_number = EZFS_ROOT_DATABLOCK_NUMBER + 1;
	inode.file_size = strlen(hello_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write hello.txt inode");
	/* Write subdir inode */
	inode_reset(&inode);
	inode.nlink = 2;
	inode.mode = S_IFDIR | 0777;
	inode.data_block_number = EZFS_ROOT_DATABLOCK_NUMBER + 2;
	inode.file_size = EZFS_BLOCK_SIZE;
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write subdir inode");

	/* Write names.txt inode */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.data_block_number = EZFS_ROOT_DATABLOCK_NUMBER + 3;
	inode.file_size = strlen(name_contents);
	inode.nblocks = 1;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write names.txt inode");

	/* Write big_img.jpeg inode */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.data_block_number = EZFS_ROOT_DATABLOCK_NUMBER + 4;
	inode.file_size = img_size;
	inode.nblocks = 8;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_img.jpeg inode");

	/* Write big_txt.txt inode */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.data_block_number = EZFS_ROOT_DATABLOCK_NUMBER + 12;
	inode.file_size = txt_size;
	inode.nblocks = 2;

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write big_txt.txt inode");

	/* lseek to the next data block */
	ret = lseek(fd, EZFS_BLOCK_SIZE - 6 * sizeof(struct ezfs_inode),
		SEEK_CUR);
	passert(ret >= 0, "Seek past inode table");
	// dentry for root
	/* dentry for hello.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "hello.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 1;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for hello.txt");

	/* dentry for subdir */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "subdir", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 2;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for subdir");
	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - 2 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of root dentries");
	/* write hello.txt content */
	len = strlen(hello_contents);
	strncpy(buf, hello_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write hello.txt contents");
	//
	ret = lseek(fd, EZFS_BLOCK_SIZE - len, SEEK_CUR);
	passert(ret >= 0, "Pad to end of hello.txt data block");
	// dentry for subdir
	/* dentry for names.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "names.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 3;
	//
	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for names.txt");
	/* dentry for big_img.jpeg */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_img.jpeg", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 4;
	//
	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for big_img.jpeg");
	/* dentry for big_txt.txt */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "big_txt.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = EZFS_ROOT_INODE_NUMBER + 5;
	//
	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for subdir");
	/* lseek to the next data block */
	len = EZFS_BLOCK_SIZE - 3 * sizeof(struct ezfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of subdir dentries");
	// write names.txt content
	len = strlen(name_contents);
	strncpy(buf, name_contents, len);
	ret = write(fd, buf, len);
	passert(ret == len, "Write names.txt contents");
	//
	ret = lseek(fd, EZFS_BLOCK_SIZE - len, SEEK_CUR);
	passert(ret >= 0, "Pad to end of names.txt data block");
	// write big_img.jpeg content
	fp = fopen(img_path, "r");
	fread(&big_buf, img_size, 1, fp);
	fclose(fp);
	len = img_size;
	ret = write(fd, big_buf, len);
	passert(ret == len, "Write big_img.jpeg contents");
	//
	ret = lseek(fd, EZFS_BLOCK_SIZE - len % EZFS_BLOCK_SIZE, SEEK_CUR);
	passert(ret >= 0, "Pad to end of big_img data block");
	// write big_txt.txt content
	fp = fopen(txt_path, "r");
	fread(&big_buf, txt_size, 1, fp);
	fclose(fp);
	len = txt_size;
	ret = write(fd, big_buf, len);
	passert(ret == len, "Write big_txt.txt contents");
	ret = lseek(fd, EZFS_BLOCK_SIZE - len % EZFS_BLOCK_SIZE, SEEK_CUR);
	passert(ret >= 0, "Pad to end of big_txt data block");
	ret = fsync(fd);
	passert(ret == 0, "Flush writes to disk");
	close(fd);
	printf("Device [%s] formatted successfully.\n", argv[1]);

	return 0;
}
