/*
 * Linux 2.6 and later 'parrot' sample device driver
 *
 * Copyright (c) 2011-2015, Pete Batard <pete@akeo.ie>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>

 
/* Module information */
MODULE_AUTHOR("Beley Maxim");
MODULE_DESCRIPTION("Driver for US-015 ultrasonic distance sensor");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

 
/* Device variables */
static struct class* parrot_class = NULL;
static struct device* parrot_device = NULL;
static int parrot_major;

static unsigned int trigPin = 5;
static unsigned int echoPin = 6;

/* Module parameters that can be provided on insmod */
module_param(trigPin, uint, S_IRUGO/* | S_IWUSR*/);
MODULE_PARM_DESC(trigPin, "Pin to make ultrasonic signal.");

module_param(echoPin, uint, S_IRUGO/* | S_IWUSR*/);
MODULE_PARM_DESC(echoPin, "Pin to check ultrasonic responce.");

/* The file_operation scructure tells the kernel which device operations are handled.
 * For a list of available file operations, see http://lwn.net/images/pdf/LDD3/ch03.pdf */

static int parrot_device_open(struct inode* inode, struct file* filp);
static int parrot_device_close(struct inode* inode, struct file* filp);
static ssize_t parrot_device_read(struct file* filp, char __user *buffer, size_t length, loff_t* offset);

static struct file_operations fops = {
 .read = parrot_device_read,
 .open = parrot_device_open,
 .release = parrot_device_close
};

 
static int parrot_device_open(struct inode* inode, struct file* filp)
{
 
 
}
 
static int parrot_device_close(struct inode* inode, struct file* filp)
{

 return 0;

}
 
static ssize_t parrot_device_read(struct file* filp, char __user *buffer, size_t length, loff_t* offset)
{

return 0;

}
 
/* Module initialization and release */
static int __init parrot_module_init(void)
{

}
 
static void __exit parrot_module_exit(void)
{

}
 
/* Let the kernel know the calls for module init and exit */
module_init(parrot_module_init);
module_exit(parrot_module_exit);
