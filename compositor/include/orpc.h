#ifndef ORPC_7IW82KP4
#define ORPC_7IW82KP4

struct amcs_orpc {
	int fd;
	int bgpid;
};

int orpc_open(struct amcs_orpc *ctx, const char *file, int flags);
bool orpc_init(struct amcs_orpc *ctx);
void orpc_deinit(struct amcs_orpc *ctx);

#endif
