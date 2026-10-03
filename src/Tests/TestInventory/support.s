.export _testExit, _testPutKey
.code
_testExit:
    jmp $ffff
_testPutKey:
    jmp $fec3                  ; real KERNAL keyboard queue, consumed by GETIN
.import _vpeek, _vpoke
.export _testPeek, _testPoke
_testPeek:
    php
    sei
    jsr _vpeek
    plp
    rts
_testPoke:
    php
    sei
    jsr _vpoke
    plp
    rts

; Observe visibility at actual VSYNC, then chain the real game/KERNAL IRQ.
.export _testObserveIrq, _testVisibleFrames
.import _inventoryActive
.bss
savedIrq: .res 2
_testVisibleFrames: .res 2
.code
_testObserveIrq:
    php
    sei
    lda $0314
    sta savedIrq
    lda $0315
    sta savedIrq+1
    lda #<observeIrq
    sta $0314
    lda #>observeIrq
    sta $0315
    plp
    rts
observeIrq:
    lda $9f27
    and #1
    beq @chain
    lda _inventoryActive
    beq @chain
    lda $9f25
    pha
    and #$fe
    sta $9f25
    lda $9f20
    pha
    lda $9f21
    pha
    lda $9f22
    pha
    lda #$fe
    sta $9f20
    lda #$ff
    sta $9f21
    lda #1
    sta $9f22
    lda $9f23
    and #12
    cmp #12
    bne @restore
    inc _testVisibleFrames
    bne @restore
    inc _testVisibleFrames+1
@restore:
    pla
    sta $9f22
    pla
    sta $9f21
    pla
    sta $9f20
    pla
    sta $9f25
@chain:
    jmp (savedIrq)
