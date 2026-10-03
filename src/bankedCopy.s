; Shared synchronous banked lookup/copy. No display layout or allocator policy.
; void* memCpyBanked(byte* dest, byte* src, byte bank, size_t len)
; cc65 fastcall: len in A/X, bank/src/dest on the software stack.
; Both pointers must fit their mapped window; callers choose the next bank.
; Run in low RAM while RAM_BANK is changed. IRQs cannot consume our zero-page
; scratch or the source bank within each <=512-byte critical section. Between
; sections, restore the original bank and preserve all scratch on the CPU stack
; while IRQs run. Preserve incoming IRQ state; ownership stays with the caller.
.export _memCpyBanked
.import popa, popax, popptr1
.importzp ptr1, ptr2, ptr3, ptr4
.segment "CODE"
.proc _memCpyBanked
    php
    sei
    sta ptr3
    stx ptr3+1
    lda $00
    pha                         ; previous RAM bank
    jsr popa
    pha                         ; requested RAM bank
    jsr popptr1                 ; source
    jsr popax                   ; destination, also the return value
    sta ptr2
    stx ptr2+1
    sta ptr4
    stx ptr4+1
    pla
    sta $00
    ldy #0
    ldx ptr3+1
    bne page
    jmp tail
page:
    .repeat 16
        lda (ptr1),y
        sta (ptr2),y
        iny
    .endrepeat
    bne page
    inc ptr1+1
    inc ptr2+1
    dex
    stx ptr3+1
    txa
    and #1
    bne resumePages
    ; At most 512 bytes with IRQ masked. A caller already inside a critical
    ; section keeps that state. The original P and bank are on our CPU stack.
    tsx
    lda $0102,x
    and #4
    bne resumePages
    .repeat 2, i
        lda ptr1+i
        pha
    .endrepeat
    .repeat 2, i
        lda ptr2+i
        pha
    .endrepeat
    .repeat 2, i
        lda ptr3+i
        pha
    .endrepeat
    .repeat 2, i
        lda ptr4+i
        pha
    .endrepeat
    lda $00
    pha
    tsx
    lda $010a,x                 ; caller's bank, below nine saved bytes
    sta $00
    lda $010b,x                 ; caller's P
    pha
    plp
    nop                        ; allow a pending IRQ to run before remapping
    sei
    pla
    sta $00
    .repeat 2, i
        pla
        sta ptr4+1-i
    .endrepeat
    .repeat 2, i
        pla
        sta ptr3+1-i
    .endrepeat
    .repeat 2, i
        pla
        sta ptr2+1-i
    .endrepeat
    .repeat 2, i
        pla
        sta ptr1+1-i
    .endrepeat
    ldy #0
resumePages:
    ldx ptr3+1
    beq tail
    jmp page
 tail:
    ldx ptr3
    beq done
    cpx #16
    bcc byte
 tailBlock:
    .repeat 16
        lda (ptr1),y
        sta (ptr2),y
        iny
    .endrepeat
    txa
    sec
    sbc #16
    tax
    cpx #16
    bcs tailBlock
    cpx #0
    beq done
byte:
    lda (ptr1),y
    sta (ptr2),y
    iny
    dex
    bne byte
 done:
    pla
    sta $00                     ; unmap before any pending IRQ can run
    lda ptr4
    ldx ptr4+1
    plp
    rts
.endproc
