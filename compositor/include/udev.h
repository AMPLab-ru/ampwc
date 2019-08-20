#ifndef _UDEV_H
#define _UDEV_H

typedef struct amcs_monitor amcs_monitor;

pvector *amcs_udev_get_monitors(void);
const char **amcs_udev_get_cardnames(void);
void amcs_udev_free_cardnames(const char **cards);
int amcs_udev_monitor_tracking(void (*update_status)(const char *name, int status));

#endif // _UDEV_H
