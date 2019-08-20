#include <stdio.h>
#include <string.h>
#include <libudev.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>

#include <sys/select.h>
#include <sys/time.h>

#include "vector.h"
#include "udev.h"
#include "macro.h"

struct amcs_monitor {
	char *name;
	int status;
};

pvector *g_monitors;
void (*ext_update_status)(const char *name, int status);

pvector *
amcs_udev_get_monitors(void)
{
	const char *path, *status;
	struct udev *udev;
	struct udev_device *udev_dev;
	struct udev_enumerate *udev_enum;
	struct udev_list_entry *udev_list, *udev_list_entry;
	pvector *monitors;
	amcs_monitor *monitor;
	const char *sysname;

	if ((udev = udev_new()) == NULL)
		error(1, "Can't create udev object.");
	if ((udev_enum = udev_enumerate_new(udev)) == NULL )
		error(1, "Can't create unumerate object");

	udev_enumerate_add_match_subsystem(udev_enum, "drm");
	udev_enumerate_scan_devices(udev_enum);

	if ((udev_list = udev_enumerate_get_list_entry(udev_enum)) == NULL)
		error(1, "Can't get dev list");

	monitors = xmalloc(sizeof(*monitors));
	pvector_init(monitors, xrealloc);

	udev_list_entry_foreach(udev_list_entry, udev_list) {
		path = udev_list_entry_get_name(udev_list_entry);
		udev_dev = udev_device_new_from_syspath(udev, path);
		status = udev_device_get_sysattr_value(udev_dev, "status");

		if (status != NULL) {
			monitor = xmalloc(sizeof (amcs_monitor));
			sysname = udev_device_get_sysname(udev_dev);
			monitor->name = strdup(sysname);
			monitor->status = 0;
			if (STREQ(status, "connected"))
				monitor->status = 1;
			pvector_push(monitors, monitor);
		}
		udev_device_unref(udev_dev);
	}
	udev_enumerate_unref(udev_enum);
	udev_unref(udev);

	return monitors;
}

const char**
amcs_udev_get_cardnames(void)
{
	int cardcount = 0;
	const char *path;
	struct udev *udev;
	struct udev_device *udev_dev;
	struct udev_enumerate *udev_enum;
	struct udev_list_entry *udev_list, *udev_list_entry;
	const char *sysname;
	char **cards = NULL;


	if ((udev = udev_new()) == NULL)
		error(1, "Can't create udev object.");

	if ((udev_enum = udev_enumerate_new(udev)) == NULL )
		error(1, "Can't create unumerate object");

	udev_enumerate_add_match_property(udev_enum, "DEVTYPE", "drm_minor");
	udev_enumerate_add_match_sysname(udev_enum, "card[0-9]*");
	udev_enumerate_scan_devices(udev_enum);

	if ((udev_list = udev_enumerate_get_list_entry(udev_enum)) == NULL)
		error(1, "Can't get dev list");

	debug("Found cards:");
	udev_list_entry_foreach(udev_list_entry, udev_list) {
		path = udev_list_entry_get_name(udev_list_entry);
		udev_dev = udev_device_new_from_syspath(udev, path);
		sysname = udev_device_get_sysname(udev_dev);

		cards = xrealloc(cards, ++cardcount * sizeof (char*));
		cards[cardcount - 1] = xmalloc(strlen(sysname) + 1);
		strcpy(cards[cardcount - 1], sysname);

		debug("    %s", cards[cardcount - 1]);

		udev_device_unref(udev_dev);
	}

	cards = xrealloc(cards, ++cardcount * sizeof (char*));
	cards[cardcount - 1] = NULL;

	udev_enumerate_unref(udev_enum);
	udev_unref(udev);

	return (const char**)cards;
}

void
amcs_udev_free_cardnames(const char **cards)
{
	int i;

	for (i = 0; cards[i] != NULL; ++i)
		free((char*)cards[i]);

	free(cards);
}

static void
send_changes(void)
{
	int i, j;

	if (g_monitors == NULL) {
		g_monitors = xmalloc(sizeof(*g_monitors));
		pvector_init(g_monitors, xrealloc);
	}
	pvector *newmons = amcs_udev_get_monitors();
	for (i = 0; i < pvector_len(g_monitors); i++) {
		struct amcs_monitor *oldmon = pvector_get(g_monitors, i);
		for (j = 0; j < pvector_len(newmons); j++) {
			struct amcs_monitor *nmon = pvector_get(newmons, j);
			if (STRNEQ(oldmon->name, nmon->name))
				continue;
			if (oldmon->status != nmon->status) {
				oldmon->status = nmon->status;
				ext_update_status(oldmon->name, oldmon->status);
			}
			free(nmon->name);
			free(nmon);
			pvector_set(newmons, j, NULL);
		}
	}
	for (i = 0; i < pvector_len(newmons); i++) {
		struct amcs_monitor *nmon = pvector_get(newmons, i);
		if (nmon == NULL)
			continue;
		pvector_push(g_monitors, nmon);
		ext_update_status(nmon->name, nmon->status);
	}
	pvector_free(newmons);
	free(newmons);
}

static void *
monitor_tracking(void *args)
{
	int ret;
	int udev_fd;
	fd_set fds;
	struct udev *udev;
	struct udev_monitor *udev_mon;


	if ((udev = udev_new()) == NULL)
		error(1, "Can't create udev object.");

	udev_mon = udev_monitor_new_from_netlink(udev, "udev");

	udev_monitor_filter_add_match_subsystem_devtype(udev_mon, "drm", NULL);
	udev_monitor_enable_receiving(udev_mon);
	udev_fd = udev_monitor_get_fd(udev_mon);

	FD_ZERO(&fds);
	FD_SET(udev_fd, &fds);

	while (1) {
		ret = select(udev_fd + 1, &fds, NULL, NULL, NULL);

		if (ret <= 0) {
			perror("monitor_tracking()");
			warning("monitor tracking thread exited");
			break;
		}
		else if (!FD_ISSET(udev_fd, &fds))
			continue;

		send_changes();
	}
	udev_unref(udev);

	return NULL;
}

int
amcs_udev_monitor_tracking(void (*update_status)(const char *name, int status))
{
	pthread_t thread;
	pthread_attr_t attr;

	assert(update_status);
	ext_update_status = update_status;

	if (pthread_attr_init(&attr))
		goto thread_error;

	if (pthread_create(&thread, &attr, monitor_tracking, NULL))
		goto thread_error;

	return 0;

thread_error:
	warning("monitoring thread create error");

	return 1;
}
