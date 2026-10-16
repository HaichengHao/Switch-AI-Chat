// Switch startup entry from switchbrew/libnx v4.10.0 (ISC licensed).
// This revision advertises Homebrew ABI revision 1 through the LNY2 MOD0 extension.
.section .crt0, "ax", %progbits
.global _start
.align 2

_start:
    b 1f
    .word __nx_mod0 - _start
    .ascii "HOMEBREW"

.org _start+0x80; 1:
    cmp  x0, #0
    ccmn x1, #1, #4, ne
    beq  .Lcrt0_main_entry
    b    __libnx_exception_entry

.Lcrt0_main_entry:
    mov x25, x0
    mov x26, x1
    mov x27, x30
    mov x28, sp

    adr  x0, _start
    adr  x1, __nx_mod0
    bl   __nx_dynamic

    adrp x9, __stack_top
    str  x28, [x9, #:lo12:__stack_top]

    mov  x0, x25
    mov  x1, x26
    mov  x2, x27
    bl   __libnx_init

    adrp x0, __system_argc
    ldr  w0, [x0, #:lo12:__system_argc]
    adrp x1, __system_argv
    ldr  x1, [x1, #:lo12:__system_argv]
    adrp x30, :got:exit
    ldr  x30, [x30, #:got_lo12:exit]
    b    main

.global __nx_exit
.type   __nx_exit, %function
__nx_exit:
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    br   x1

.global __nx_mod0
__nx_mod0:
    .ascii "MOD0"
    .word  _DYNAMIC             - __nx_mod0
    .word  __bss_start__        - __nx_mod0
    .word  __bss_end__          - __nx_mod0
    .word  __eh_frame_hdr_start - __nx_mod0
    .word  __eh_frame_hdr_end   - __nx_mod0
    .word  0

    .ascii "LNY0"
    .word  __got_start__        - __nx_mod0
    .word  __got_end__          - __nx_mod0

    .ascii "LNY1"
    .word  __relro_start        - __nx_mod0
    .word  __data_start         - __nx_mod0

    .ascii "LNY2"
    .word  0x1
    .word  0x0

.section .bss.__stack_top, "aw", %nobits
.global __stack_top
.align 3

__stack_top:
    .space 8
