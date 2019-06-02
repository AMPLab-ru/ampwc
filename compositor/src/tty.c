#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <pthread.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <linux/kd.h>

#include "tty.h"
#include "macro.h"


typedef struct tty_dev {
	int fd;
	int num;
	char *path;
	int state;
} tty_dev;


static void (*extern_acquisition) (void);
static void (*extern_release) (void);
static tty_dev *dev;
static pthread_mutex_t signal_mux;


static char*
uitoa(unsigned int n)
{
	int i, j;
	unsigned int num, tmp;
	char *s;

	num = tmp = n;

	for (i = 0; num > 0; ++i)
		num /= 10;

	if (tmp == 0)
		i = 1;

	s = xmalloc(i + 1);
	memset(s, '\0', i + 1);

	for (j = 0; j < i; ++j) {
		s[j] = tmp / (int) pow(10, i - j - 1) + '0';
		tmp = tmp % (int) pow(10, i - j - 1);
	}

	return s;
}

static void
tty_acquisition(int sig)
{
	if (pthread_mutex_trylock(&signal_mux))
		return;

	if (dev->state == 0) {
		dev->state = 1;
		ioctl(dev->fd, VT_RELDISP, VT_ACKACQ);
		extern_acquisition();
	}

	pthread_mutex_unlock(&signal_mux);
}

static void
tty_release(int sig)
{
	if (pthread_mutex_trylock(&signal_mux))
		return;

	if (dev->state == 1) {
		dev->state = 0;
		extern_release();
		ioctl(dev->fd, VT_RELDISP, 1);
	}

	pthread_mutex_unlock(&signal_mux);
}

void
amcs_tty_open(unsigned int num)
{
	int fd;
	size_t size;
	char *path;
	char *str;


	size = sizeof (tty_dev);
	dev = xmalloc(size);
	dev = memset(dev, 0, size);

	if (pthread_mutex_init(&signal_mux, NULL) != 0) {
		perror("amcs_tty_open()");
		exit(1);
	}

	if (num == 0) {
		if ((fd = open("/dev/tty1", O_RDWR | O_NOCTTY)) < 0) {
			perror("open('/dev/tty1', O_RDWR | O_NOCTTY)");
			exit(1);
		}

		if (ioctl(fd, VT_OPENQRY, &num) || (num == -1)) {
			perror("ioctl(fd, VT_OPENQRY, &num)");
			exit(1);
		}

		if (close(fd) < 0) {
			perror("close(fd)");
			exit(1);
		}

	}

	dev->num = num;
	str = uitoa(dev->num);

	size = sizeof ("/dev/tty") + strlen(str);
	path = xmalloc(size);

	strcpy(path, "/dev/tty");
	dev->path = path = strcat(path, str);

	free(str);

	debug("Open terminal: %s", path);
	if ((dev->fd = open(path, O_RDWR | O_CLOEXEC)) < 0) {
		perror("open(path, O_RDWR | O_CLOEXEC)");
		exit(1);
	}
}

void
amcs_tty_sethand(void (*ext_acq) (void), void (*ext_rel) (void))
{
	struct vt_mode mode;


	assert(ext_acq && ext_rel);

	extern_acquisition = ext_acq;
	extern_release = ext_rel;

	if (signal(SIGUSR1, tty_acquisition) == SIG_ERR) {
		perror("signal(SIGUSR1, tty_acquisition)");
		exit(1);
	}

	if (signal(SIGUSR2, tty_release) == SIG_ERR) {
		perror("signal(SIGUSR2, tty_release)");
		exit(1);
	}


	if (ioctl(dev->fd, VT_GETMODE, &mode)) {
		perror("ioctl(dev->fd, VT_GETMODE, &mode)");
		exit(1);
	}

	mode.mode = VT_PROCESS;
	mode.acqsig = SIGUSR1;
	mode.relsig = SIGUSR2;

	if (ioctl(dev->fd, VT_SETMODE, &mode)) {
		perror("ioctl(fd, VT_SETMODE, &mode)");
		exit(1);
	}
}

void
amcs_tty_activate(void)
{
	debug("Activating terminal");

	if (ioctl(dev->fd, VT_ACTIVATE, dev->num)) {
		perror("ioctl(dev->fd, VT_ACTIVATE, dev->num)");
		exit(1);
	}

	if (ioctl(dev->fd, VT_WAITACTIVE, dev->num)) {
		perror("ioctl(dev->fd, VT_WAITACTIVE, dev->num)");
		exit(1);
	}
}
