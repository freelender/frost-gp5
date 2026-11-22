/* Inside naked reset_handler, after MSP is set and before any C call/push.
 * The exact locked linker map reserves [0x20005c00,0x20006c00) for this stack. */
                   "ldr r0, =0x20005C00\n"
                   "ldr r1, =0x20006C00\n"
                   "ldr r2, =0xA55AA55A\n"
                   "1:\n"
                   "str r2, [r0], #4\n"
                   "cmp r0, r1\n"
                   "blo 1b\n"
