.export _trampoline, _testStatus, _testExit
.importzp tmp4
.import callptr4
.code
; Same bank-call ABI as helpersAsm.s; helper/allocator code is linked in low
; RAM in this suite, while data accesses still use the real selected banks.
_trampoline:
    sta trampolineArg
    lda $00
    pha
    lda tmp4
    sta $00
    lda trampolineArg
    jsr callptr4
    sta trampolineArg
    pla
    sta $00
    lda trampolineArg
    rts
.data
trampolineArg: .byte 0
.code
_testStatus:
    php
    pla
    ldx #0
    rts
_testExit:
    jmp $ffff

; Observe actual KERNAL IRQ entry. Deliberately switch to an unrelated bank;
; a pending interrupt must only see bank 7 after the transfer has unmapped.
.export _testIrqStart, _testIrqStop, _testIrqCount, _testIrqWrongBank
.bss
savedIrq: .res 2
_testIrqCount: .res 2
.export _testIrqMaxGap, _testYieldCopy
.import _memCpyBanked
.importzp ptr1, ptr2, ptr3, ptr4
_testIrqMaxGap: .res 4
lastTick: .res 4
elapsed: .res 4
tick: .res 4
copyActive: .res 1
_testIrqWrongBank: .res 1
.code
_testIrqStart:
    sei
    lda $0314
    sta savedIrq
    lda $0315
    sta savedIrq+1
    stz _testIrqCount
    stz _testIrqCount+1
    stz copyActive
    .repeat 4, i
        stz _testIrqMaxGap+i
        stz lastTick+i
    .endrepeat
    stz _testIrqWrongBank
    lda #<testIrq
    sta $0314
    lda #>testIrq
    sta $0315
    lda #$40
    sta $9f1b                 ; VIA2 timer 1 continuous mode
    lda #$e8
    sta $9f14
    lda #$03
    sta $9f15                 ; 1000-cycle IRQ source
    lda #$c0
    sta $9f1e
    cli
    rts
_testIrqStop:
    sei
    lda #$40
    sta $9f1e                 ; disable test timer IRQ
    lda savedIrq
    sta $0314
    lda savedIrq+1
    sta $0315
    cli
    rts
_testYieldCopy:
    pha
    lda #1
    sta copyActive
    pla
    jsr _memCpyBanked
    pha
    lda #0
    sta copyActive
    pla
    rts
 testIrq:
    lda $9f1d
    and #$40
    bne timerIrq
    jmp (savedIrq)
 timerIrq:
    lda $9f14                 ; acknowledge timer IRQ
    .repeat 4, i
        lda $9fb8+i
        sta tick+i
    .endrepeat
    sec
    .repeat 4, i
        lda tick+i
        sbc lastTick+i
        sta elapsed+i
    .endrepeat
    lda _testIrqCount
    ora _testIrqCount+1
    beq updateTick            ; first tick has no preceding service timestamp
    .repeat 4, i
        lda elapsed+3-i
        cmp _testIrqMaxGap+3-i
        bcc updateTick
        bne newMaximum
    .endrepeat
    bra updateTick
 newMaximum:
    .repeat 4, i
        lda elapsed+i
        sta _testIrqMaxGap+i
    .endrepeat
 updateTick:
    .repeat 4, i
        lda tick+i
        sta lastTick+i
    .endrepeat
    inc _testIrqCount
    bne countDone
    inc _testIrqCount+1
 countDone:
    lda $00
    pha
    cmp #7
    beq restoredBank
    inc _testIrqWrongBank
 restoredBank:
    lda #31
    sta $00
    pla
    sta $00
    lda copyActive
    beq returnTimer
    ; Reproduce the product IRQ's use of shared cc65 scratch during a yield.
    .repeat 2, i
        lda #$a5
        sta ptr1+i
        sta ptr2+i
        sta ptr3+i
        sta ptr4+i
    .endrepeat
 returnTimer:
    ply
    plx
    pla
    rti
