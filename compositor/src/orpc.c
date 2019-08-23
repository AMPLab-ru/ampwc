#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/major.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>

#include <wayland-server-core.h>

#include "tty.h"
#include "macro.h"
#include "orpc.h"
#include "wl-server.h"

#define CMD_KILL	'k'
#define CMD_FILE_OPEN	'o'
#define CMD_TTY_INIT	't'
#define SIG_DEACTIVATE	'd'
#define SIG_ACTIVATE	'a'

#define RESP_OK		't'
#define RESP_ERR	'f'

#ifndef DRM_MAJOR
#define DRM_MAJOR 226
#endif

int drm_fds[16];
int n_drm_fds;

/* handles commands like 'o OPEN_FLAG FPATH' */
static int
handle_file_open(struct amcs_orpc *ctx, char *buf, ssize_t bufsz)
{
	char iobuf[1] = {'\0'};
	struct iovec iov = {
		.iov_base = iobuf,
		.iov_len = sizeof(iobuf),
	};
	struct stat st;
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	int newfd, n, flags, pos, mode = 0;
	char *fpath;
	char cmd;
	int *fdptr;
	ssize_t sz;

	n = sscanf(buf, "%c %u %n", &cmd, &flags, &pos);
	if (n < 1)
		goto send_it;
	debug("recieved flags = %d, buf = %s", flags, buf + pos);
	if (flags | O_CREAT)
		mode = S_IRWXU;
	if ((flags & (O_ACCMODE | O_NONBLOCK | O_CREAT | O_CLOEXEC)) != flags) {
		warning("invalid open flags");
		goto send_it;
	}
	fpath = buf + pos;

	newfd = open(fpath, flags, mode);
	if (newfd == -1) {
		warning("open: %s", strerror(errno));
		goto send_it;
	}
	if (fstat(newfd, &st) != 0) {
		warning("can't fstat file");
		close(newfd);
		goto send_it;
	}
	if (major(st.st_rdev) == INPUT_MAJOR) {
	} else if (major(st.st_rdev) == DRM_MAJOR) {
		if (n_drm_fds >= ARRSZ(drm_fds)) {
			close(newfd);
			goto send_it;
		}
		drm_fds[n_drm_fds++] = dup(newfd);
	} else {
		warning("invalid open request");
		close(newfd);
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
		return 1;
	}
	close(newfd);
	return 0;
}

static int
get_tty_id()
{
	int n;
	char *nm;
	char *tty_path = "/dev/tty";

	if (!isatty(STDIN_FILENO))
		return 0;
	nm = ttyname(STDIN_FILENO);
	if (strncmp(nm, tty_path, strlen(tty_path)) != 0)
		return 0;
	n = atoi(nm + strlen(tty_path));
	if (n < 0)
		return 0;
	return n;
}

static void
close_drm()
{
	int i;
	for (i = 0; i < n_drm_fds; i++) {
		drmDropMaster(drm_fds[i]);
		close(drm_fds[i]);
	}
	n_drm_fds = 0;
}

static void
tty_activate(void *opaq)
{
	struct amcs_orpc *ctx = opaq;
	char c = SIG_ACTIVATE;
	write(ctx->sigpipe[1], &c, sizeof(c));
}

static void
tty_deactivate(void *opaq)
{
	struct amcs_orpc *ctx = opaq;
	char c = SIG_DEACTIVATE;
	write(ctx->sigpipe[1], &c, sizeof(c));
	close_drm();
}

static void
handle_tty_initialize(struct amcs_orpc *ctx, char *buf, ssize_t bufsz)
{
	static bool firstrun = true;
	int ttyid = get_tty_id();
	if (!firstrun)
		return;
	firstrun = false;
	amcs_tty_open(ttyid);
	amcs_tty_sethand(tty_activate, tty_deactivate, ctx);
	if (ttyid != 0)
		tty_activate(ctx);
}

static void
orpc_run_stop()
{
	amcs_tty_restore_term();
	exit(1);
	//assert(0 && "HUIPIZDA, finalize it gracefully");
}

/*
 * Use root privileges for file access,
 * pass descriptors to parent
 */
static int
orpc_run(struct amcs_orpc *ctx)
{
	struct pollfd fds[] = {
		{ctx->fd,         POLLIN, 0},
		{ctx->sigpipe[0], POLLIN, 0},
	};
	char buf[16 + PATH_MAX] = {0};
	ssize_t sz;
	int rc;

	while (1) {
		rc = poll(fds, ARRSZ(fds), -1);
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			warning("poll: %s", strerror(errno));
			return 1;
		}
		if ((fds[0].revents | fds[1].revents) & POLLHUP) {
			warning("pollhup");
			return 1;
		}
		if ((fds[0].revents | fds[1].revents) & (POLLNVAL | POLLERR)) {
			warning("descriptor error");
			return 1;
		}
		if (fds[1].revents & POLLIN) {
			sz = read(ctx->sigpipe[0], buf, sizeof(buf) - 1);
			if (sz < 1) {
				warning("recv: %s", strerror(errno));
				return 1;
			}
			//resend signal info to client
			send(ctx->fd, buf, 1, 0);
		}
		if (fds[0].revents & POLLIN) {
			sz = recv(ctx->fd, buf, sizeof(buf) - 1, 0);
			if (sz == -1) {
				warning("recv: %s", strerror(errno));
				return 1;
			}
			if (sz == 0)
				return 0;
			debug("processing command %c", buf[0]);
			switch(buf[0]) {
			case CMD_KILL:
				orpc_run_stop();
			case CMD_FILE_OPEN:
				rc = handle_file_open(ctx, buf, sizeof(buf));
				if (rc != 0)
					orpc_run_stop();
				break;
			case CMD_TTY_INIT:
				handle_tty_initialize(ctx, buf, sizeof(buf));
				break;
			default:
				warning("orpc, wrong command %c", buf[0]);
				orpc_run_stop();
			}
		}
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

	n = snprintf(buf, sizeof(buf), "%c %d %s", CMD_FILE_OPEN, flags, file);
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
		if (cmsg->cmsg_level == SOL_SOCKET &&
		    cmsg->cmsg_type == SCM_RIGHTS)
		fdptr = (int *) CMSG_DATA(cmsg);
		newfd = *fdptr;
	}
	return newfd;
}

struct tty_handlers {
	orpc_handler_t start;
	orpc_handler_t stop;
};

static int
notify_tty_status(int fd, uint32_t mask, void *data)
{
	struct tty_handlers *h = data;
	char buf[1024];
	ssize_t sz;

	debug("");
	sz = read(fd, buf, sizeof(buf));
	if (sz < 1)
		error(1, "can't read event info");
	switch (buf[0]) {
	case SIG_ACTIVATE:
		h->start();
		break;
	case SIG_DEACTIVATE:
		h->stop();
		break;
	default:
		error(1, "should not reach");
	}
	return 0;
}

bool orpc_tty_init(struct amcs_orpc *ctx, orpc_handler_t start, orpc_handler_t stop)
{
	struct amcs_compositor *comp = &compositor_ctx;
	static struct tty_handlers h;
	static bool firstrun = true;
	ssize_t sz;
	char cmd[] = {CMD_TTY_INIT, '\0'};

	if (!firstrun)
		return 0;
	firstrun = false;
	h.start = start;
	h.stop = stop;
	sz = send(ctx->fd, cmd, sizeof(cmd), 0);
	if (sz <= 0) {
		debug("can't send %ld", sz);
		return false;
	}
	wl_event_loop_add_fd(comp->evloop, ctx->fd, WL_EVENT_READABLE,
				notify_tty_status, &h);

	return false;
}

bool
orpc_init(struct amcs_orpc *ctx)
{
	pid_t pid;
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0)
		error(1, "can't create socketpair");

	debug("uid %d euid %d", getuid(), geteuid());

	pid = fork();
	if (pid == -1) {
		return false;
	} else if (pid == 0) {
		// child. use additional privileges for open files
		if (pipe2(ctx->sigpipe, O_CLOEXEC) != 0)
			error(1, "can't create pipe");
		close(sv[0]);
		ctx->fd = sv[1];
		exit(orpc_run(ctx));
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
	char cmd[] = {CMD_KILL, '\0'};
	ssize_t sz;

	assert(ctx);
	sz = send(ctx->fd, cmd, sizeof(cmd), 0);
	if (sz > 0) {
		char buf[1024];
		recv(ctx->fd, buf, sizeof(buf), 0);
	}
	close(ctx->fd);
	kill(ctx->bgpid, SIGTERM);
	waitpid(ctx->bgpid, NULL, 0);
}

