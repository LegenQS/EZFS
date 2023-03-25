#include <linux/module.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/vfs.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mutex.h>
#include <linux/writeback.h>

#include "ezfs.h"
#include "ezfs_ops.h"

static void ezfs_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static void ezfs_free_inode(struct inode *inode)
{

}

struct ezfs_inode *find_inode_by_number(struct super_block *sb,
		unsigned long ino, struct buffer_head **p)
{
	int offset;

	if (ino < EZFS_ROOT_INODE_NUMBER || ino > EZFS_MAX_INODES) {
		return ERR_PTR(-EIO);
	}
	offset = ino - EZFS_ROOT_INODE_NUMBER;
	*p = sb_bread(sb, EZFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!*p) {
		return ERR_PTR(-EIO);
	}
	return (struct ezfs_inode *) (*p)->b_data + offset;
}

static void ezfs_evict_inode (struct inode *inode)
{
	unsigned long ino = inode->i_ino;
	struct ezfs_super_block *ezfs_sb;
	struct ezfs_sb_buffer_heads *sbh;
	struct ezfs_inode *ezfs_inode;
	struct buffer_head *bh;
	sbh = inode->i_sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *) sbh->sb_bh->b_data;
	ezfs_inode = inode->i_private;
	// clear inode and data block info.
	CLEARBIT(ezfs_sb->free_inodes, ino);
	CLEARBIT(ezfs_sb->free_data_blocks, ezfs_inode->data_block_number -
			EZFS_ROOT_DATABLOCK_NUMBER);

	// clear skeleton
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	ezfs_inode = find_inode_by_number(inode->i_sb, inode->i_ino, &bh);
	// clear bh and ezfs_inode
	mutex_lock(ezfs_sb->ezfs_lock);
	memset(ezfs_inode, 0, sizeof(struct ezfs_inode));
	mark_buffer_dirty(bh);
	brelse(bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	// release resource in ezfs
	mutex_destroy(ezfs_sb->ezfs_lock);
	kfree(ezfs_sb->ezfs_lock);
	brelse(sbh->sb_bh);
	brelse(sbh->i_store_bh);
	kfree(sbh);
}

static uint64_t get_next_block(struct ezfs_super_block *ezfs_sb)
{
	int i = EZFS_ROOT_DATABLOCK_NUMBER;
	while (i < EZFS_MAX_DATA_BLKS) {
		if (!IS_SET(ezfs_sb->free_data_blocks, i))
			return i;
	}
	return 0;
}

static int get_next_inode(struct ezfs_super_block *ezfs_sb)
{
	int i = EZFS_ROOT_INODE_NUMBER;
	while (i <= EZFS_MAX_INODES) {
		if (!IS_SET(ezfs_sb->free_inodes, i)) {
			SETBIT(ezfs_sb->free_inodes, i);
			return i;
		}
	}
	return -1;
}

static int ezfs_write_inode (struct inode *inode,
		struct writeback_control *wbc)
{
	struct ezfs_inode *di;
	struct buffer_head *bh;
	struct ezfs_sb_buffer_heads *sbh;
	struct ezfs_super_block *ezfs_sb;
	unsigned long ino = inode->i_ino;
	int err = 0;

	sbh = inode->i_sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *) sbh->sb_bh->b_data;

	di = find_inode_by_number(inode->i_sb, ino, &bh);
	if (IS_ERR(di))
		return PTR_ERR(di);

	mutex_lock(ezfs_sb->ezfs_lock);
	di->mode = inode->i_mode;
	di->uid = i_uid_read(inode);
	di->gid = i_gid_read(inode);
	di->nlink = inode->i_nlink;
	di->i_atime = inode->i_atime;
	di->i_mtime = inode->i_mtime;
	di->i_ctime = inode->i_ctime;

	mark_buffer_dirty(bh);
	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh))
			err = -EIO;
	}
	brelse(bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return err;
}

static int ezfs_create_inode(struct inode *inode)
{
	struct ezfs_inode *di;
	struct buffer_head *bh;
	struct ezfs_sb_buffer_heads *sbh;
	struct ezfs_super_block *ezfs_sb;
	unsigned long ino = inode->i_ino;

	sbh = inode->i_sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *) sbh->sb_bh->b_data;
	SETBIT(ezfs_sb->free_inodes, ino);
	di = find_inode_by_number(inode->i_sb, ino, &bh);
	if (IS_ERR(di))
		return PTR_ERR(di);

	if (ino == EZFS_ROOT_INODE_NUMBER) {
		di->data_block_number = EZFS_ROOT_DATABLOCK_NUMBER;
		di->mode = S_IFDIR | 0777;
		SETBIT(ezfs_sb->free_data_blocks, di->data_block_number);
	} else {
		uint64_t block_number = get_next_block(ezfs_sb);
		if (block_number == 0) {
			return PTR_ERR(di);
		}
		di->data_block_number = block_number;
		di->mode = S_IFREG | 0666;
		SETBIT(ezfs_sb->free_data_blocks, di->data_block_number);
	}

	di->uid = di->gid = 10000;
	di->i_atime = inode->i_atime;
	di->i_mtime = inode->i_mtime;
	di->i_ctime = inode->i_ctime;
	mark_buffer_dirty(bh);
	brelse(bh);
	mutex_unlock(ezfs_sb->ezfs_lock);
	return get_next_inode(ezfs_sb);
}

static const struct super_operations ezfs_sops = {
	//.alloc_inode	= bfs_alloc_inode,
	//.free_inode	= ezfs_free_inode,
	.write_inode	= ezfs_write_inode,
	//.evict_inode	= ezfs_evict_inode,
	.drop_inode	= generic_delete_inode,
	// .put_super	= bfs_put_super,
	.statfs		= simple_statfs,
};

static int ezfs_readdir(struct file *f, struct dir_context *ctx)
{
	struct inode *inode;
	struct buffer_head *bh;
	struct ezfs_inode *ez_inode;
	struct ezfs_dir_entry *de;
	struct super_block *sb;
	int block, pos, i;
	mode_t mode;

	inode = file_inode(f);
	if (inode == NULL) {
		return -1;
	}
	ez_inode = inode->i_private;
	mode = inode->i_mode;
	sb = inode->i_sb;
	block = ez_inode->data_block_number;
	bh = sb_bread(sb, block);
	if (!bh)
		return -EINVAL;

	if (!dir_emit_dots(f, ctx)) {
		return 0;
	}

	pos = ctx->pos - 2;
	for (i = pos; i < EZFS_MAX_CHILDREN; i++, ctx->pos++) {
		de = (struct ezfs_dir_entry *) bh->b_data + i;
		if (!de->active)
			continue;
		if (!dir_emit(ctx, de->filename, strnlen(de->filename,
			EZFS_FILENAME_BUF_SIZE), de->inode_no, S_DT(mode)))
			break;
	}
	brelse(bh);
	return 0;
}

// do string match for filename
static inline int ezfs_namecmp(int len, const unsigned char *name,
							const char *buffer)
{
	if ((len < EZFS_MAX_FILENAME_LENGTH) && buffer[len])
		return 0;
	return !memcmp(name, buffer, len);
}

static struct buffer_head *ezfs_find_entry(struct inode *dir,
			const struct qstr *child,
			struct ezfs_dir_entry **res_dir)
{
	struct buffer_head *bh = NULL;
	struct ezfs_dir_entry *de;
	struct ezfs_inode *ezfs_ino;
	const unsigned char *name = child->name;
	int namelen = child->len;
	int block, end_block;
	int offset = 0;

	*res_dir = NULL;
	if (namelen > EZFS_MAX_FILENAME_LENGTH)
		return NULL;
	ezfs_ino = dir->i_private;
	block = ezfs_ino->data_block_number;
	end_block = block + ezfs_ino->nblocks;

	// read each block of the dir
	while (block < end_block) {
		bh = sb_bread(dir->i_sb, block);
		if (!bh) {
			block++;
			continue;
		}
		de = (struct ezfs_dir_entry *) (bh->b_data + offset);
		offset += sizeof(struct ezfs_dir_entry);
		if (ezfs_namecmp(namelen, name, de->filename)) {
			*res_dir = de;
			//brelse(bh);
			return bh;
		}
		// read each dentry within the block
		if (offset < bh->b_size)
			continue;
		brelse(bh);
		bh = NULL;
		offset = 0;
		block++;
	}
	brelse(bh);
	return NULL;
}

static struct dentry *ezfs_lookup (struct inode *dir, struct dentry *dentry,
					unsigned int flags)
{
	struct inode *inode = NULL;
	struct buffer_head *bh;
	struct ezfs_dir_entry *de;
	struct ezfs_super_block *ezfs_sb;
	struct ezfs_sb_buffer_heads *sbh;

	if (dentry->d_name.len > EZFS_MAX_FILENAME_LENGTH)
		return ERR_PTR(-ENAMETOOLONG);

	//look up the dentry name in the dir and do string match
	sbh = dir->i_sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *) sbh->sb_bh->b_data;

	mutex_lock(ezfs_sb->ezfs_lock);
	bh = ezfs_find_entry(dir, &dentry->d_name, &de);

	// take the inode number to get the inode
	if (bh) {
		brelse(bh);
		inode = ezfs_get_inode(dir->i_sb, dir, de->inode_no);
	}
	mutex_unlock(ezfs_sb->ezfs_lock);

	return d_splice_alias(inode, dentry); //associate the inode with dentry
}

static int ezfs_move_block(unsigned long from, unsigned long to,
			struct super_block *sb)
{
	struct buffer_head *bh, *new;

	bh = sb_bread(sb, from);
	if (!bh)
		return -EIO;
	new = sb_getblk(sb, to);
	memcpy(new->b_data, bh->b_data, bh->b_size);
	mark_buffer_dirty(new);
	bforget(bh);
	brelse(new);
	return 0;
}

static int ezfs_move_blocks(struct super_block *sb, unsigned long start,
				unsigned long end, unsigned long where)
{
	unsigned long i;

	for (i = start; i < end; i++) {
		if (ezfs_move_block(i, where + i, sb)) {
			printk("failed to move blocks\n");
			return -EIO;
		}
	}
	return 0;
}

static int ezfs_get_block(struct inode *inode, sector_t block,
		struct buffer_head *bh_result, int create)
{
	// block is the data block number of the file requested
	struct super_block *sb = inode->i_sb;
	struct ezfs_super_block *ezfs_sb;
	struct ezfs_sb_buffer_heads *sbh = sb->s_fs_info;
	struct ezfs_inode *ezfs_inode = inode->i_private;
	uint64_t nblocks, block_num;
	unsigned long phys;
	int err;

	ezfs_sb = (struct ezfs_super_block *) sbh->sb_bh->b_data;
	nblocks = ezfs_inode->nblocks;
	block_num = ezfs_inode->data_block_number;
	phys = block_num + block;

	if (!create) {
		if (phys < block_num + nblocks) {
			printk("b=%08lx granted\n", (unsigned long) block);
			map_bh(bh_result, sb, phys);
		}
		return 0;
	}

	if (block_num > 0 && phys < block_num + nblocks) {
		map_bh(bh_result, sb, phys);
		return 0;
	}
	if (phys >= EZFS_MAX_DATA_BLKS)
		return -ENOSPC;

	mutex_lock(ezfs_sb->ezfs_lock);
	phys = get_next_block(ezfs_sb);
	if (phys + block >= EZFS_MAX_DATA_BLKS) {
		err = -ENOSPC;
		goto out;
	}
	if (ezfs_inode->data_block_number != 0) {
		err = ezfs_move_blocks(inode->i_sb, block_num,
				nblocks + block_num, phys);
		if (err) {
			printk("failed to move \n");
			goto out;
		}
	} else {
		err = 0;
	}
	ezfs_inode->data_block_number = phys;
	phys += block;
	mark_inode_dirty(inode);
	map_bh(bh_result, sb, phys);

out:	mutex_unlock(ezfs_sb->ezfs_lock);
	return err;

}

static int ezfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page, ezfs_get_block);
}

static int ezfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ezfs_get_block, wbc);
}

static void ezfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size)
		truncate_pagecache(inode, inode->i_size);
}

static int ezfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, flags, pagep,
			ezfs_get_block);
	if (unlikely(ret))
		ezfs_write_failed(mapping, pos + len);
	return ret;
}

const struct inode_operations ezfs_dir_inode_ops = {
	//.create = ezfs_create,
	.lookup	= ezfs_lookup,
	.link	= simple_link,
	.unlink = simple_unlink,
	.rename = simple_rename,
};

const struct inode_operations ezfs_file_inode_ops = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
};

const struct file_operations ezfs_file_ops = {
	.read_iter  	= generic_file_read_iter,
	.write_iter 	= generic_file_write_iter,
	.llseek    	= generic_file_llseek,
	.mmap	    	= generic_file_mmap,
	.splice_read	= generic_file_splice_read,
};

const struct file_operations ezfs_dir_file_ops = {
	.read		= generic_read_dir,
	.read_iter	= generic_file_read_iter,
	.iterate_shared	= ezfs_readdir,
	.fsync		= generic_file_fsync,
	.llseek		= generic_file_llseek,
};

static sector_t ezfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ezfs_get_block);
}

static const struct address_space_operations ezfs_aops = {
	.readpage 	= ezfs_readpage,
	.writepage	= ezfs_writepage,
	.write_begin	= ezfs_write_begin,
	.write_end	= generic_write_end,
	.bmap		= ezfs_bmap,
};

struct inode *ezfs_get_inode(struct super_block *sb,
		const struct inode *dir, unsigned long ino)
{
	struct inode *inode;
	struct ezfs_inode *ezfs_inode;
	struct buffer_head *bh;
	int offset;
	umode_t mode;
	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	bh = sb_bread(inode->i_sb, EZFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!bh)
		return ERR_PTR(-EIO);
	offset = ino - EZFS_ROOT_INODE_NUMBER;
	ezfs_inode = (struct ezfs_inode *) bh->b_data + offset;
	mode = ezfs_inode->mode;

	if (inode) {
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &ezfs_aops;
		inode->i_atime = inode->i_mtime = inode->i_ctime =
			current_time(inode);
		inode->i_private = ezfs_inode;
		inode->i_size = ezfs_inode->file_size;
		if (mode == (S_IFDIR | 0777)) {
			inode->i_mode |= S_IFDIR;
			inode->i_op = &ezfs_dir_inode_ops;
			inode->i_fop = &ezfs_dir_file_ops;
			inc_nlink(inode);
		} else if (mode == (S_IFREG | 0666)) {
			inode->i_mode |= S_IFREG;
			inode->i_op = &ezfs_file_inode_ops;
			inode->i_fop = &ezfs_file_ops;
		}
		inode->i_private = ezfs_inode;
	}
	unlock_new_inode(inode);
	return inode;

}

static int ezfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	// create inode by iget_locked()
	// associated a dentry
	struct ezfs_sb_buffer_heads *sbh;
	struct ezfs_super_block *ezfs_sb;
	struct ezfs_inode *ez_ino;
	struct inode *inode;

	sbh = kzalloc(sizeof(*sbh), GFP_KERNEL);
	if (!sbh)
		return -ENOMEM;

	sbh->sb_bh = kzalloc(sizeof(struct buffer_head), GFP_KERNEL);
	sbh->i_store_bh = kzalloc(sizeof(struct buffer_head), GFP_KERNEL);
	if (!sbh->sb_bh)
		return -ENOMEM;
	if (!sbh->i_store_bh)
		return -ENOMEM;
	//read and populate the sb_bh
	sb_set_blocksize(sb, EZFS_BLOCK_SIZE);
	sb->s_fs_info = sbh;
	sbh->sb_bh = sb_bread(sb, EZFS_SUPERBLOCK_DATABLOCK_NUMBER);
	ezfs_sb = (struct ezfs_super_block *)sbh->sb_bh->b_data;
	ezfs_sb->ezfs_lock = kzalloc(sizeof(struct mutex *), GFP_KERNEL);
	if (!ezfs_sb->ezfs_lock) {
		return -ENOMEM;
	}
	mutex_init(ezfs_sb->ezfs_lock);
	// read and populate the i_store
	sbh->i_store_bh = sb_bread(sb, EZFS_INODE_STORE_DATABLOCK_NUMBER);
	ez_ino = (struct ezfs_inode *) sbh->i_store_bh->b_data;
	// fill out additional parameters
	sb->s_magic = EZFS_MAGIC_NUMBER;
	sb->s_op = &ezfs_sops;

	// create root inode
	inode = ezfs_get_inode(sb, NULL, EZFS_ROOT_INODE_NUMBER);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;

}

static int ezfs_get_tree (struct fs_context *fc)
{
	// call get_tree_bdev with fill_super func
	return get_tree_bdev(fc, ezfs_fill_super);
}

static const struct fs_context_operations ezfs_context_ops = {
	.free     = ezfs_free_fc,
	.get_tree = ezfs_get_tree,
};

// mount
int ezfs_init_fs_context (struct fs_context *fc)
{
	// allocate memory and generate basic info for fc
	struct ezfs_sb_buffer_heads *ezbh;
	ezbh = kzalloc(sizeof(*ezbh), GFP_KERNEL);
	if (!ezbh)
		return -ENOMEM;
	fc->s_fs_info = ezbh;
	fc->ops = &ezfs_context_ops;
	return 0;
}

// umount
static void ezfs_kill_sb (struct super_block *sb)
{
	struct ezfs_sb_buffer_heads *sbh;
	struct ezfs_super_block *ezfs_sb;
    sbh = sb->s_fs_info;
	ezfs_sb = (struct ezfs_super_block *) sbh->sb_bh->b_data;
	
	mutex_destroy(ezfs_sb->ezfs_lock);
	kfree(ezfs_sb->ezfs_lock);
	brelse(sbh->sb_bh);
	brelse(sbh->i_store_bh);
	kfree(sb->s_fs_info);
	kill_block_super(sb);
}

static struct file_system_type myezfs = {
	.owner 		 = THIS_MODULE,
	.name  		 = "myezfs",
	.init_fs_context = ezfs_init_fs_context,
	.kill_sb 	 = ezfs_kill_sb,
};

static int __init init_ezfs (void)
{
	return register_filesystem(&myezfs);
}

static void __exit exit_ezfs (void)
{
	unregister_filesystem(&myezfs);
}

module_init(init_ezfs);
module_exit(exit_ezfs);