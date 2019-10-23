#define _GNU_SOURCE
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int
tempfile()
{
	int fd;
	char filename[] = "XXXXXX";

	fd = mkstemp(filename);
	if (fd < 0)
		return -1;
	unlink(filename);
	fcntl(fd, F_SETFD,  FD_CLOEXEC);

	return fd;
}

int
alloc_tempfile(size_t sz)
{
	int fd;
	fd = tempfile();
	if (fd == -1)
		return fd;
	if (posix_fallocate(fd, 0, sz) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}
