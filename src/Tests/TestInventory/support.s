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
.export _testObserveIrq, _testResetVsyncMetrics, _testVsyncMinGap
.export _testVisibleFrames, _testVsyncIrqs, _testDetailVisibleFrames, _testInspectVisibleFrames, _testVsyncMaxGap
.import _inventoryActive, _inventoryBuffer
.bss
savedIrq: .res 2
_testVisibleFrames: .res 2
_testVsyncIrqs: .res 2
_testDetailVisibleFrames: .res 2
_testInspectVisibleFrames: .res 2
feedActive: .res 1
feedDelay: .res 1
previousCycle: .res 4
sampleCycle: .res 4
serviceGap: .res 4
sampleValid: .res 1
_testVsyncMaxGap: .res 4
_testVsyncMinGap: .res 4
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
_testResetVsyncMetrics:
    php
    sei
    stz _testVisibleFrames
    stz _testVisibleFrames+1
    stz _testVsyncIrqs
    stz _testVsyncIrqs+1
    stz _testDetailVisibleFrames
    stz _testDetailVisibleFrames+1
    stz _testInspectVisibleFrames
    stz _testInspectVisibleFrames+1
    stz sampleValid
    stz _testVsyncMaxGap
    stz _testVsyncMaxGap+1
    stz _testVsyncMaxGap+2
    stz _testVsyncMaxGap+3
    lda #$ff
    sta _testVsyncMinGap
    sta _testVsyncMinGap+1
    sta _testVsyncMinGap+2
    sta _testVsyncMinGap+3
    plp
    rts
observeIrq:
    lda $9f27
    and #1
    bne @vsync
    jmp @chain
@vsync:
    jsr sampleVsyncGap
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
    lda $9f26
    pha
    stz $9f26
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
    lda #7
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
    ; Count actual VSYNCs where inventory's inspect slot is a published,
    ; enabled 64x64 sprite. The integration fixture uses several valid names.
    lda #$fe
    sta $9f20
    lda #$ff
    sta $9f21
    lda #7
    sta $9f22
    lda $9f23
    cmp #12
    bne @detailName
    lda #$ff
    sta $9f20
    lda $9f23
    cmp #$f0
    bne @detailName
    inc _testInspectVisibleFrames
    bne @detailName
    inc _testInspectVisibleFrames+1
@detailName:
    lda _inventoryBuffer
    cmp #$61
    beq :+
    cmp #$64
    beq :+
    cmp #$63
    beq :+
    cmp #$62
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
    beq @dagger
    cmp #$63
    beq @carrot
    cmp #$62
    beq @bucket
    bne @restore
@dagger:
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
@carrot:
    lda #$86
    sta $9f20
    lda $9f23
    cmp #$61
    bne @restore
    lda #$88
    sta $9f20
    lda $9f23
    cmp #$72
    bne @restore
    bra @detailVisible
@bucket:
    lda #$86
    sta $9f20
    lda $9f23
    cmp #$75
    bne @restore
    lda #$88
    sta $9f20
    lda $9f23
    cmp #$63
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
    sta $9f26
    pla
    sta $9f25
@chain:
    jmp (savedIrq)

; Capture cycle-counter entry times before the observer's existing work. The
; maximum interval includes hardware IRQ dispatch, any masked interval, and
; the production IRQ chain; the measurement code itself is test-only.
sampleVsyncGap:
    lda $9fb8
    sta sampleCycle
    lda $9fb9
    sta sampleCycle+1
    lda $9fba
    sta sampleCycle+2
    lda $9fbb
    sta sampleCycle+3
    lda sampleValid
    bne @subtract
    inc sampleValid
    jmp @save
@subtract:
    sec
    lda sampleCycle
    sbc previousCycle
    sta serviceGap
    lda sampleCycle+1
    sbc previousCycle+1
    sta serviceGap+1
    lda sampleCycle+2
    sbc previousCycle+2
    sta serviceGap+2
    lda sampleCycle+3
    sbc previousCycle+3
    sta serviceGap+3
    lda serviceGap+3
    cmp _testVsyncMaxGap+3
    bcc @minTest
    bne @newMaximum
    lda serviceGap+2
    cmp _testVsyncMaxGap+2
    bcc @minTest
    bne @newMaximum
    lda serviceGap+1
    cmp _testVsyncMaxGap+1
    bcc @minTest
    bne @newMaximum
    lda serviceGap
    cmp _testVsyncMaxGap
    bcc @minTest
    beq @minTest
@newMaximum:
    lda serviceGap
    sta _testVsyncMaxGap
    lda serviceGap+1
    sta _testVsyncMaxGap+1
    lda serviceGap+2
    sta _testVsyncMaxGap+2
    lda serviceGap+3
    sta _testVsyncMaxGap+3
@minTest:
    lda _testVsyncMinGap+3
    cmp serviceGap+3
    bcc @save
    bne @newMinimum
    lda _testVsyncMinGap+2
    cmp serviceGap+2
    bcc @save
    bne @newMinimum
    lda _testVsyncMinGap+1
    cmp serviceGap+1
    bcc @save
    bne @newMinimum
    lda _testVsyncMinGap
    cmp serviceGap
    bcc @save
    beq @save
@newMinimum:
    lda serviceGap
    sta _testVsyncMinGap
    lda serviceGap+1
    sta _testVsyncMinGap+1
    lda serviceGap+2
    sta _testVsyncMinGap+2
    lda serviceGap+3
    sta _testVsyncMinGap+3
@save:
    lda sampleCycle
    sta previousCycle
    lda sampleCycle+1
    sta previousCycle+1
    lda sampleCycle+2
    sta previousCycle+2
    lda sampleCycle+3
    sta previousCycle+3
    rts

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
