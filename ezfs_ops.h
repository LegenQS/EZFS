#ifndef __EZFS_OPS_H__
#define __EZFS_OPS_H__

struct inode *ezfs_get_inode(struct super_block *sb, 
		const struct inode *dir, unsigned long ino);

static struct buffer_head *ezfs_find_entry (struct inode *dir,
				const struct qstr *child,
				struct ezfs_dir_entry **res_dir);
#endif /* ifndef __EZFS_OPS_H__ */