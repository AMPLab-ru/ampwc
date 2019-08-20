#ifndef _AWC_TTY_H
#define _AWC_TTY_H

void amcs_tty_open(unsigned int num);
void amcs_tty_restore_term();
typedef void (*amcs_tty_handler_t)(void *);
void amcs_tty_sethand(amcs_tty_handler_t acq, amcs_tty_handler_t rel, void *opaq);
void amcs_tty_activate(void);

#endif // _AWC_TTY_H
