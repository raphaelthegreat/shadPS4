/* SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project */
/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2003 Peter Wemm */
/* SPDX-FileCopyrightText: 1993 The Regents of the University of California */
/* SPDX-License-Identifier: BSD-2-Clause */

.text

#define CNAME(csym) csym

#define ALIGN_TEXT .p2align 4,0x90

#define ENTRY(name) ALIGN_TEXT; .globl CNAME(name); .type CNAME(name),@function; CNAME(name):

#define	END(name) .size name, . - name

* casuword.  Compare and set user word.  Returns -1 or the current value.
*        dst = %rdi, old = %rsi, new = %rdx
*/
ENTRY(casuword)
       movq	PCPU(CURPCB),%rcx
       movq	$fusufault,PCB_ONFAULT(%rcx)

       movq	$VM_MAXUSER_ADDRESS-4,%rax
       cmpq	%rax,%rdi			/* verify address is valid */
       ja	fusufault

       movq	%rsi,%rax			/* old */
       lock
       cmpxchgq %rdx,(%rdi)			/* new = %rdx */

       /*
        * The old value is in %eax.  If the store succeeded it will be the
        * value we expected (old) from before the store, otherwise it will
        * be the current value.
        */

       movq	PCPU(CURPCB),%rcx
       movq	$fusufault,PCB_ONFAULT(%rcx)
       movq	$0,PCB_ONFAULT(%rcx)
       ret
END(casuword)
