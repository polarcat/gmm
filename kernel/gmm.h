/* Rudimentary graphics memory mapper kernel driver
 *
 * Author: Aliaksei Katovich <aliaksei.katovich@gmail.com>
 *
 */

#ifndef GMM_H__
#define GMM_H__

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

#ifndef GMM_DEV
#define GMM_DEV "/dev/gmm"
#endif

struct gmm_getfd_req {
	unsigned long addr;
	int fd;
};

#define GMM_GETFD _IOWR('a', 'e', struct gmm_getfd_req *)

#endif /* GMM_H__ */
