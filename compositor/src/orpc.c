#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>

#include "macro.h"
#include "orpc.h"

/* Use root privileges for file access,
 * pass descriptors to parent
 */
static int
orpc_listen(struct amcs_orpc *ctx)
{
	int *fdptr;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	char buf[16 + PATH_MAX] = {0};
	char iobuf[1];
	struct iovec iov = {
		.iov_base = iobuf,
		.iov_len = sizeof(iobuf),
	};
	int n;
	ssize_t sz;

	while (1) {
		int newfd, flags, pos, mode = 0;

		memset(&msg, 0, sizeof(msg));
		sz = recv(ctx->fd, buf, sizeof(buf) - 1, 0);
		if (sz == -1) {
			warning("recv: %s", strerror(errno));
			return 1;
		}
		if (sz == 0)
			return 0;
		n = sscanf(buf, "%u %n", &flags, &pos);
		if (n < 1)
			goto send_it;
		debug("recieved flags = %d, buf = %s", flags, buf + pos);
		if (flags | O_CREAT)
			mode = S_IRWXU;
		newfd = open(buf + pos, flags, mode);
		if (newfd == -1) {
			warning("open: %s", strerror(errno));
			goto send_it;
		}
		/* prepare fd for sending */
		msg.msg_control = buf;
		msg.msg_controllen = CMSG_SPACE(sizeof(int) * 1);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 1);
		fdptr = (int *) CMSG_DATA(cmsg);
		fdptr[0] = newfd;
send_it:
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		sz = sendmsg(ctx->fd, &msg, 0);
		if (sz < 1) {
			warning("open: %s", strerror(errno));
			exit(3);
		}
		close(newfd);
	}
	return 0;
}

int
orpc_open(struct amcs_orpc *ctx, const char *file, int flags)
{
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	char buf[16 + PATH_MAX];
	char iobuf[1];
	struct iovec iov = {
		.iov_base = iobuf,
		.iov_len = sizeof(iobuf),
	};
	int n;
	ssize_t sz;

	n = snprintf(buf, sizeof(buf), "%d %s", flags, file);
	if (n <= 0)
		return -1;
	sz = send(ctx->fd, buf, n + 1, 0);
	if (sz <= 0) {
		debug("can't send %ld", sz);
		return -1;
	}

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_SPACE(sizeof(int) * 1);
	sz = recvmsg(ctx->fd, &msg, 0);
	if (sz == -1) {
		warning("recvmsg: %s", strerror(errno));
		return -1;
	}
	int newfd = -1;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		int *fdptr;
		debug("parent: next cmsg");
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS)
		debug("parent:pass FD!");
		fdptr = (int *) CMSG_DATA(cmsg);
		newfd = *fdptr;
	}
	return newfd;
}

bool
orpc_init(struct amcs_orpc *ctx)
{
	pid_t pid;
	int sv[2];

	socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);

	debug("uid %d euid %d", getuid(), geteuid());
	debug("sv = %d %d", sv[0], sv[1]);

	pid = fork();
	if (pid == -1) {
		return false;
	} else if (pid == 0) {
		// child. use additional privileges for open files
		close(sv[0]);
		ctx->fd = sv[1];
		exit(orpc_listen(ctx));
	} else {
		// parent. drop superuser privileges
		close(sv[1]);
		ctx->fd = sv[0];
		setgid(getgid());
		setuid(getuid());
		debug("uid %d euid %d", getuid(), geteuid());
	}
	return true;
}

void
orpc_deinit(struct amcs_orpc *ctx)
{
	assert(ctx);
	close(ctx->fd);
	kill(ctx->bgpid, SIGTERM);
	waitpid(ctx->bgpid, NULL, 0);
}

#if 0
int
test_file(struct amcs_orpc *ctx, char *fpath, int flags)
{
	char buf[1024];
	int fd;
	ssize_t sz;
	fd = orpc_open(ctx, fpath, flags);
	if (fd < 0) {
		debug("can't open %s via orpc", fpath);
		return 1;
	}
	debug("opened file %s", fpath);
	while ((sz = read(fd, buf, sizeof(buf))) > 0) {
		write(1, buf, sz);
	}
	debug("======");
	return 0;
}

int
main(int argc, const char *argv[])
{
	struct amcs_orpc ctx;

	orpc_init(&ctx);
	test_file(&ctx, "/tmp/shadow", O_RDONLY);
	test_file(&ctx, "/etc/shadow", O_RDONLY);
	test_file(&ctx, "/tmp/aaaaa", O_RDWR | O_CREAT);
	orpc_deinit(&ctx);
	return 0;
}
#endif
