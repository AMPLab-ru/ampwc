#ifndef ORPC_7IW82KP4
#define ORPC_7IW82KP4

struct amcs_orpc {
	int fd;
	int bgpid;

	/* internal */
	int sigpipe[2];
};

bool orpc_init(struct amcs_orpc *ctx);
void orpc_deinit(struct amcs_orpc *ctx);

typedef void (*orpc_handler_t)(void);
int orpc_open(struct amcs_orpc *ctx, const char *file, int flags);
bool orpc_tty_init(struct amcs_orpc *ctx, orpc_handler_t start, orpc_handler_t stop);

#endif
