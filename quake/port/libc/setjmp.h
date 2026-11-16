/* setjmp and longjmp for Quake on the RNS-E (setjmp.S): the registers a
 * function keeps for its caller, r8 to r15, PR, and fr12 to fr15 (each
 * half of dr12 and dr14). */
#ifndef RNSE_QUAKE_SETJMP_H
#define RNSE_QUAKE_SETJMP_H
typedef int jmp_buf[16];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int value) __attribute__((noreturn));
#endif
