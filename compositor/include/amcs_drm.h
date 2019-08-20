#ifndef _AMCS_DRM_H
#define _AMCS_DRM_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <stdint.h>

typedef struct amcs_drm_card amcs_drm_card;
typedef struct amcs_drm_dev amcs_drm_dev_list;
typedef struct amcs_drm_dev amcs_drm_dev;

struct amcs_drm_dev {
	uint8_t *buf;
	uint32_t conn_id, enc_id, crtc_id, fb_id;
	uint32_t w, h;
	uint32_t pitch, size, handle;
	drmModeModeInfo mode;

	amcs_drm_dev_list *next;
};

struct amcs_drm_card {
	const char *path;
	int fd;

	amcs_drm_dev_list *list;
};

amcs_drm_card *amcs_drm_init(struct amcs_orpc *orpc, const char *path);
void amcs_drm_free(amcs_drm_card *card);

#endif // _AMCS_DRM_H
