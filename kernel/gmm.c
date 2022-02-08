/* Rudimentary graphics memory mapper kernel driver
 *
 * Copyright (C) 2021 Aliaksei Katovich. All rights reserved.
 * Author: Aliaksei Katovich <aliaksei.katovich@gmail.com>
 *
 * Released under the GNU General Public License, version 2
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fdtable.h>

#include "gmm.h"

#define ee(...) printk(KERN_ERR "gmm: " __VA_ARGS__)
#define ii(...) printk(KERN_INFO "gmm: " __VA_ARGS__)
#define ww(...) printk(KERN_WARNING "gmm: " __VA_ARGS__)
#ifdef DEBUG
#define dd(...) printk(KERN_INFO "gmm: " __VA_ARGS__)
#else
#define dd(...) ;
#endif

struct gmm {
	const char *name;
	struct miscdevice dev;
};

static struct gmm gmm_;

static int gmm_match_fd(struct files_struct *files, struct file *file)
{
	struct fdtable *fdt;
	unsigned n = 0;
	int fd = -1;

	spin_lock(&files->file_lock);
	for (fdt = files_fdtable(files); n < fdt->max_fds; n++) {
		struct file *ptr;
		ptr = rcu_dereference_check_fdtable(files, fdt->fd[n]);
		if (ptr == file) {
			fd = n;
			break;
		}
	}
	spin_unlock(&files->file_lock);
	dd("match file %p fd %d\n", file, fd);
	return fd;
}

static int gmm_getfd(void __user *arg)
{
	int rc;
	struct file *file;
	struct vm_area_struct *vma;
	struct gmm_getfd_req req;

	if (!current->files)
		return -EFAULT;

	if ((rc = copy_from_user(&req, arg, sizeof(req)))) {
		ee("%d failed to copy request, rc=%d\n", current->pid, rc);
		return rc;
	}

	dd("pid %d files %p addr %#lx:\n", current->pid, current->files,
	  req.addr);
	file = NULL;

	for (vma = current->mm->mmap; vma; vma = vma->vm_next) {
		dd("\tfile %p start %#lx\n", vma->vm_file, vma->vm_start);
		if (vma->vm_start == req.addr) {
			file = vma->vm_file;
			break;
		}
	}

	dd("found file %p addr %#lx\n", file, req.addr);
	if (!file)
		return -ENOENT;

	req.fd = gmm_match_fd(current->files, file);
	if ((rc = copy_to_user(arg, &req, sizeof(req)))) {
		ee("%d failed to copy, rc=%d\n", current->pid, rc);
	}

	return rc;
}

static int gmm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int gmm_close(struct inode *inode, struct file *file)
{
	return 0;
}

static long gmm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	dd("cmd %u pid %d\n", cmd, current->pid);

	if (cmd == GMM_GETFD)
		return gmm_getfd((void __user *) arg);
	else
		return -EINVAL;
}

static struct file_operations gmm_fops = {
	.owner = THIS_MODULE,
	.open = gmm_open,
	.release = gmm_close,
	.unlocked_ioctl = gmm_ioctl,
};

static int __init gmm_init(void)
{
	int rc;

	gmm_.name = "gmm";
	gmm_.dev.name = gmm_.name;
	gmm_.dev.minor = MISC_DYNAMIC_MINOR;
	gmm_.dev.fops = &gmm_fops;
	rc = misc_register(&gmm_.dev);
	ii("init rc=%d\n", rc);

	return  rc;
}

static void __exit gmm_exit(void)
{
	misc_deregister(&gmm_.dev);
	ii("exit ok\n");
	return;
}

module_init(gmm_init);
module_exit(gmm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aliaksei Katovich <aliaksei.katovich@gmail.com>");
MODULE_DESCRIPTION("Rudimentary graphics memory mapper");
MODULE_VERSION("0.1");
