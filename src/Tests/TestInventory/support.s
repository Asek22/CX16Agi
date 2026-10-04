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
.export _testObserveIrq, _testVisibleFrames, _testVsyncIrqs, _testDetailVisibleFrames
.import _inventoryActive, _inventoryBuffer
.bss
savedIrq: .res 2
_testVisibleFrames: .res 2
_testVsyncIrqs: .res 2
_testDetailVisibleFrames: .res 2
feedActive: .res 1
feedDelay: .res 1
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
    bne @vsync
    jmp @chain
@vsync:
    inc _testVsyncIrqs
    bne :+
    inc _testVsyncIrqs+1
:
    lda _inventoryActive
    beq :+
    jsr feedKey
:
    lda $9f25
    pha
    stz $9f25
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
    bne @detail
    inc _testVisibleFrames
    bne @detail
    inc _testVisibleFrames+1
@detail:
    lda _inventoryActive
    bne :+
    jmp @restore
:
    lda _inventoryBuffer
    cmp #$61
    beq :+
    cmp #$64
    beq :+
    jmp @restore
:
    ; Row 18 starts at MAPBASE + 23 * 128 + 4. Read the tile characters
    ; directly from VERA at VSYNC, skipping each cell's palette byte.
    lda #$84
    sta $9f20
    lda #$e5
    sta $9f21
    stz $9f22
    lda $9f23
    cmp #$61
    beq @unknownName
    cmp #$64
    bne @restore
    lda #$86
    sta $9f20
    lda $9f23
    cmp #$61
    bne @restore
    lda #$88
    sta $9f20
    lda $9f23
    cmp #$67
    bne @restore
    bra @detailVisible
@unknownName:
    lda #$86
    sta $9f20
    lda $9f23
    cmp #$67
    bne @restore
    lda #$88
    sta $9f20
    lda $9f23
    cmp #$67
    bne @restore
@detailVisible:
    inc _testDetailVisibleFrames
    bne @restore
    inc _testDetailVisibleFrames+1
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

; Pace real KERNAL queue input so a long navigation script cannot overflow it.
.export _testFeedKeys
_testFeedKeys:
    php
    sei
    sta feedLoad+1
    stx feedLoad+2
    lda #1
    sta feedActive
    stz feedDelay
    plp
    rts
feedKey:
    lda feedActive
    beq feedKeyDone
    lda feedDelay
    beq feedKeyNext
    dec feedDelay
    rts
feedKeyNext:
feedLoad:
    lda $ffff
    beq feedKeyEnd
    jsr $fec3
    inc feedLoad+1
    bne :+
    inc feedLoad+2
:
    lda #7
    sta feedDelay
    rts
feedKeyEnd:
    stz feedActive
feedKeyDone:
    rts
.export _testDisableInterrupts, _testEnableInterrupts, _testStatus
_testDisableInterrupts:
    sei
    rts
_testEnableInterrupts:
    cli
    rts
_testStatus:
    php
    pla
    ldx #0
    rts
