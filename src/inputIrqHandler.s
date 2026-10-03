.ifndef  INPUTIRQ_INC
GLOBAL_INPUTIRQ_INC = 1
MAX_INPUT_STRING_LENGTH = 40 ;Includes terminator
.import _inputLineDisplayed
.import _b7CurrentInputStr

.include "globalGraphics.s"
.segment "BANKRAM07"
b7HandleInputLine:
SET_VERA_ADDRESS_IMMEDIATE INPUT_STRING_ADDRESS, #$0, #$2
@clearInputLine:
lda #TRANSPARENT_CHAR
ldx #$0 ; Sound handler's X is not an input-line column.

@clearLoop:
sta VERA_data0
inx
cpx #MAX_INPUT_STRING_LENGTH
bne @clearLoop

lda _inputLineDisplayed
beq @end
ldx #$0

SET_VERA_ADDRESS_IMMEDIATE INPUT_STRING_ADDRESS, #$0, #$2
@drawInput:
lda #INPUT_PROMPT_CHAR
sta VERA_data0
@drawInputLoop:
lda _b7CurrentInputStr,x
beq @exitInputLoop

sta VERA_data0

inx
cpx #MAX_INPUT_STRING_LENGTH - 1 ; Reserve the first tile for the prompt.
beq @exitInputLoop
bra @drawInputLoop
@exitInputLoop:

@drawCursor:
cpx #MAX_INPUT_STRING_LENGTH - 1 ; Cursor also fits within the 40-tile row.
beq @end
lda #CURSOR_CHAR
sta VERA_data0

@end:
rts
.endif

