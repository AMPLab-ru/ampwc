#ifndef _AWC_TTY_H
#define _AWC_TTY_H

void amcs_tty_open(struct amcs_orpc *ctx, unsigned int num);
void amcs_tty_restore_term();
void amcs_tty_sethand(void (*ext_acq) (void), void (*ext_rel) (void));
void amcs_tty_activate(void);

#endif // _AWC_TTY_H
