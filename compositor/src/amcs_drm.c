#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include "macro.h"
#include "orpc.h"
#include "amcs_drm.h"


#define DEPTH 24
#define BPP 32

static int
dumb_is_supported(int fd)
{
	uint64_t dumb;
	int flags;


	if ((flags = fcntl(fd, F_GETFD)) < 0 ||
	    fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
		return 0;

	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &dumb) < 0 || dumb == 0)
		return 0;

	return 1;
}

static int
drm_setFB(int fd, amcs_drm_dev *dev)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;

	// get settings, and FB id
	memset(&creq, 0, sizeof (struct drm_mode_create_dumb));
	creq.width = dev->w;
	creq.height = dev->h;
	creq.bpp = BPP;

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		warning("drmIoctl error");
		return 1;
	}

	dev->pitch = creq.pitch;
	dev->size = creq.size;
	dev->handle = creq.handle;

	if (drmModeAddFB(fd, dev->w, dev->h, DEPTH, BPP, dev->pitch,
			 dev->handle, &dev->fb_id)) {
		warning("error adding frame buffer");
		return 1;
	}


	// get settings, and map buffer
	memset(&mreq, 0, sizeof (struct drm_mode_map_dumb));
	mreq.handle = dev->handle;

	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
		warning("error getting map dumb");
		return 1;
	}

	dev->buf = mmap(NULL, dev->size, PROT_READ | PROT_WRITE, MAP_SHARED,
			fd, mreq.offset);
	if (dev->buf == MAP_FAILED) {
		warning("error mapping buffer");
		return 1;
	}

	if (drmModeSetCrtc(fd, dev->crtc_id, dev->fb_id, 0, 0,
			   &dev->conn_id, 1, &dev->mode)) {
		warning("error setting crtc");
		return 1;
	}
	return 0;
}

amcs_drm_card*
amcs_drm_init(struct amcs_orpc *orpc, const char *path)
{
	int i;
	int fd;

	amcs_drm_card *card;
	amcs_drm_dev *dev_list;
	amcs_drm_dev *dev;
	amcs_drm_dev *drmdev;

	drmModeRes *res;
	drmModeConnector *conn;
	drmModeEncoder *enc;

	assert(path);
	debug("Init dev: %s", path);

	if ((fd = orpc_open(orpc, path, O_RDWR | O_CLOEXEC)) < 0) {
		perror("amcs_drm_init()");
		return NULL;
	}

	if (!dumb_is_supported(fd)) {
		debug("dumb buffer is not supported for device: %s", path);
		close(fd);
		return NULL;
	}

	if ((res = drmModeGetResources(fd)) == NULL) {
		debug("error getting drm resources for device: %s", path);
		close(fd);
		return NULL;
	}

	card = xmalloc(sizeof (amcs_drm_card));
	card->path = path;
	card->fd = fd;

	debug("Count connectors: %d", res->count_connectors);

	dev_list = NULL;
	for (i = 0; i < res->count_connectors; ++i) {
		conn = drmModeGetConnector(fd, res->connectors[i]);

		if (conn == NULL)
			continue;

		if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes <= 0)
			goto free_connector;

		enc = drmModeGetEncoder(fd, conn->encoder_id);
		if (enc == NULL)
			goto free_connector;

		drmdev = xmalloc(sizeof (amcs_drm_dev));
		drmdev->next = dev_list;
		dev_list = card->list = drmdev;

		dev_list->conn_id = conn->connector_id;
		dev_list->enc_id = conn->encoder_id;
		dev_list->crtc_id = enc->crtc_id;

		dev_list->mode = conn->modes[0];
		dev_list->w = conn->modes[0].hdisplay;
		dev_list->h = conn->modes[0].vdisplay;

		if (drm_setFB(fd, dev_list) != 0) {
			dev_list = drmdev->next;
			free(drmdev);
			drmModeFreeEncoder(enc);
			goto cleanup;
		}

		debug("    conn id: %d", dev_list->conn_id);
		debug("    enc id: %d", dev_list->enc_id);
		debug("    crtc id: %d", dev_list->crtc_id);
		debug("    mode: %dx%d", dev_list->w, dev_list->h);
		debug("    pitch: %d, size: %d, handle: %d",
		      dev_list->pitch, dev_list->size, dev_list->handle);

		drmModeFreeEncoder(enc);
free_connector:
		drmModeFreeConnector(conn);
		debug("    ____________________");
	}

	drmModeFreeResources(res);

	return card;

cleanup:
	drmModeFreeResources(res);
	for (dev_list = card->list; dev_list != NULL;) {
		munmap(dev_list->buf, dev_list->size);
		dev = dev_list;
		dev_list = dev_list->next;
		free(dev);
	}
	close(card->fd);
	free(card);
	return NULL;
}

void
amcs_drm_free(amcs_drm_card *card)
{
	amcs_drm_dev *dev, *dev_list;
	struct drm_mode_destroy_dumb dreq;

	assert(card);
	for (dev_list = card->list; dev_list != NULL;) {
		munmap(dev_list->buf, dev_list->size);
		drmModeRmFB(card->fd, dev_list->fb_id);
		dreq.handle = dev_list->handle;
		drmIoctl(card->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

		dev = dev_list;
		dev_list = dev_list->next;
		free(dev);
	}

	close(card->fd);
	free(card);
}
