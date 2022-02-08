/* Rudimentary graphics memory mapper userspace library
 *
 * Author: Aliaksei Katovich <aliaksei.katovich@gmail.com>
 *
 */

#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <poll.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifndef GMM_SOCKET
#define GMM_SOCKET "/tmp/.gmm"
#endif
#ifndef GMM_SOCKET_TIMEOUT
#define GMM_SOCKET_TIMEOUT 32 /* ms */
#endif

enum {
	GMM_COLOR_R8G8B8,
	GMM_COLOR_B8G8R8A8_SRGB,
};

#define gmm_ee(...) {\
	int gmm_errno__ = errno;\
	fprintf(stderr, "(ee) gmm: " __VA_ARGS__);\
	fprintf(stderr, "(ee) gmm: ^^ %s:%d | %s\n", __func__, __LINE__,\
	 __FILE__);\
	if (gmm_errno__) {\
		fprintf(stderr, "(ee) gmm: %s (%d)\n", strerror(gmm_errno__),\
		 gmm_errno__);\
	}\
	errno = gmm_errno__;\
}

#define gmm_ii(...) printf("(ii) gmm: " __VA_ARGS__)

#ifdef __cplusplus
extern "C"
{
#endif

struct gmm_image {
	uint32_t format;
	uint16_t width;
	uint16_t height;
	uint32_t stride;
	uint16_t xpos;
	uint16_t ypos;
};

static inline int gmm_sendimg(int sd, struct gmm_image *img, int fd)
{
	ssize_t ret;
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	struct iovec iov;
	char buf[CMSG_SPACE(sizeof(int))];

	if (sd < 0 || !img || fd < 0) {
		errno = EINVAL;
		return -1;
	}

	iov.iov_base = img;
	iov.iov_len = sizeof(*img);

	cmsg = (struct cmsghdr *) buf;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsg;
	msg.msg_controllen = sizeof(buf);
	msg.msg_flags = 0;
	*((int *) CMSG_DATA(cmsg)) = fd;

	errno = 0;
	if ((ret = sendmsg(sd, &msg, 0)) < 0) {
		gmm_ee("sendmsg(%d) failed, ret=%zd\n", sd, ret);
		return -1;
	}

	return 0;
}

static inline int gmm_recvimg(int sd, struct gmm_image *img)
{
	ssize_t ret;
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	struct iovec iov;
	char buf[CMSG_SPACE(sizeof(int))] = {0};

	if (sd < 0 || !img) {
		errno = EINVAL;
		return -1;
	}

	iov.iov_base = img;
	iov.iov_len = sizeof(*img);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	msg.msg_flags = 0;

	errno = 0;
	if ((ret = recvmsg(sd, &msg, 0)) < 0) {
		gmm_ee("recvmsg(%d) failed\n", sd);
		return -1;
	} else if (errno) {
		return -1;
	} else if (ret == 0) {
		gmm_ii("sd %d connection reset by peer\n", sd);
		errno = ECONNRESET;
		return -1;
	}

	if (msg.msg_flags & MSG_CTRUNC) {
		gmm_ee("sd %d bad control data\n", sd);
		errno = EIO;
		return -1;
	}

	if (!(cmsg = CMSG_FIRSTHDR(&msg))) {
		gmm_ee("sd %d null control data\n", sd);
		errno = EIO;
		return -1;
	} else if (cmsg->cmsg_level != SOL_SOCKET ||
	  cmsg->cmsg_type != SCM_RIGHTS) {
		gmm_ee("sd %d unexpected message level %d type %d len %zu\n", sd,
		 cmsg->cmsg_level, cmsg->cmsg_type, cmsg->cmsg_len);
		errno = EIO;
		return -1;
	}

	return *((int *) CMSG_DATA(cmsg));
}

static inline int gmm_connect(void)
{
	int sd;
	struct sockaddr_un addr;

	if ((sd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		gmm_ee("failed to create socket\n");
		return -1;
	}

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, GMM_SOCKET, sizeof(addr.sun_path));

	if (connect(sd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		gmm_ee("failed to connect socket %d\n", sd);
		close(sd);
		return -1;
	}

	return sd;
}

static inline uint8_t gmm_poll(int fd, short flags)
{
	struct pollfd pfd = { .fd = fd, .events = flags, .revents = 0 };
	int ret;

try_again:
	errno = 0;
	ret = poll(&pfd, 1, GMM_SOCKET_TIMEOUT);

	if (errno != 0) {
		return 0;
	} else if (ret == 0) {
		errno = EAGAIN; /* timeout, no retrial needed */
		return 0;
	} else if (ret < 0) {
		if (errno == EINTR)
			goto try_again;
		else
			return 0; /* unrecoverable error occured */
	} else if (pfd.revents & flags) {
		return 1;
	}

	return 0;
}

static inline void gmm_close(int *sd)
{
	if (sd && *sd >= 0) {
		close(*sd);
		*sd = -1;
	}

	unlink(GMM_SOCKET);
}

static inline int gmm_listen(void)
{
	int sd;
	struct sockaddr_un addr;

	/* cleanup leftovers from previous session if any */
	unlink(GMM_SOCKET);
	errno = 0; /* ignore errors */

	if ((sd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		gmm_ee("failed to create socket\n");
		return -1;
	}

	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, GMM_SOCKET, sizeof(addr.sun_path));

	if (bind(sd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		gmm_ee("failed to bind socket %d\n", sd);
		gmm_close(&sd);
		return -1;
	}

	if (listen(sd, 1) < 0) {
		gmm_ee("failed to listen on socket %d\n", sd);
		gmm_close(&sd);
		return -1;
	}

	return sd;
}

#ifdef __cplusplus
}
#endif
