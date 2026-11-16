/* setjmp and longjmp (setjmp.h): r8-r15, PR, fr12-fr15 into env (r4),
 * in that order.  fmov.s moves 32 bits whatever FPSCR.PR says (SZ is
 * 0 throughout the game). */
        .text
        .align  2
        .global setjmp
        .type   setjmp, @function
setjmp:
        add     #52, r4
        fmov.s  fr15, @-r4
        fmov.s  fr14, @-r4
        fmov.s  fr13, @-r4
        fmov.s  fr12, @-r4
        sts.l   pr, @-r4
        mov.l   r15, @-r4
        mov.l   r14, @-r4
        mov.l   r13, @-r4
        mov.l   r12, @-r4
        mov.l   r11, @-r4
        mov.l   r10, @-r4
        mov.l   r9, @-r4
        mov.l   r8, @-r4
        rts
        mov     #0, r0

        .global longjmp
        .type   longjmp, @function
longjmp:
        mov.l   @r4+, r8
        mov.l   @r4+, r9
        mov.l   @r4+, r10
        mov.l   @r4+, r11
        mov.l   @r4+, r12
        mov.l   @r4+, r13
        mov.l   @r4+, r14
        mov.l   @r4+, r15
        lds.l   @r4+, pr
        fmov.s  @r4+, fr12
        fmov.s  @r4+, fr13
        fmov.s  @r4+, fr14
        fmov.s  @r4+, fr15
        mov     r5, r0
        tst     r0, r0
        bf      1f
        mov     #1, r0
1:      rts
        nop

        .section .note.GNU-stack,"",@progbits
