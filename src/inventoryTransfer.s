; Bounded VRAM transfers preserve the interrupted renderer's address and flags.
.segment "CODE"
.import _inventoryAddress, _inventoryBuffer, _inventoryLength, _inventoryWrite
.export _inventoryTransfer, _inventoryMouseButtons
_inventoryTransfer:
    php
    sei
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
    lda _inventoryAddress
    sta $9f20
    lda _inventoryAddress+1
    sta $9f21
    lda _inventoryAddress+2
    ora #$10
    sta $9f22
    ldx #0
    lda _inventoryWrite
    beq @read
@write:
    lda _inventoryBuffer,x
    sta $9f23
    inx
    cpx _inventoryLength
    bne @write
    bra @done
@read:
    lda $9f23
    sta _inventoryBuffer,x
    inx
    cpx _inventoryLength
    bne @read
@done:
    pla
    sta $9f22
    pla
    sta $9f21
    pla
    sta $9f20
    pla
    sta $9f25
    plp
    rts
_inventoryMouseButtons:
    ; KERNAL MOUSE_GET writes four bytes at X in zero page.
    lda $02
    pha
    lda $03
    pha
    lda $04
    pha
    lda $05
    pha
    ldx #$02
    jsr $ff6b
    tax
    pla
    sta $05
    pla
    sta $04
    pla
    sta $03
    pla
    sta $02
    txa
    ldx #0
    rts
.export _inventoryMouseEnable, _inventoryMouseDisable
_inventoryMouseEnable:
    lda #1
    ldx #40
    ldy #30
    jmp $ff68
_inventoryMouseDisable:
    lda #0
    ldx #0
    ldy #0
    jmp $ff68
