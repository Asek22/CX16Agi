
.ifndef SPLITVIEW_INC
; Set the value of include guard and define constants
SPLITVIEW_INC = 1
.include "global.s"

.import _b5RefreshBuffer
.import _memsetBanked
.import _b1DivAndCeil
.import _memCpyBankedBetween

.import _b10BankedAlloc
.import _b5Multiply
.import _b5Divide

.segment "BANKRAM0E"

SPLIT_CEL_WIDTH = ZP_TMP_2
SPLIT_CEL_HEIGHT = ZP_TMP_2 + 1
SPLIT_BANK = ZP_TMP_3
CEL_DATA_BANK = ZP_TMP_4
SPLIT_BUFFER_STATUS = ZP_TMP_5 ;Takes Up 6 as well
NO_BYTES_SIZE = ZP_TMP_7
SPLIT_BUFFER_POINTER = ZP_TMP_8
SPLIT_DATA = ZP_TMP_9
CEL_DATA = ZP_TMP_10
SEGMENT_SIZE = ZP_TMP_12
CEL_STRUCT_POINTER = ZP_TMP_13
SEGMENTS_ACROSS = ZP_TMP_22
NO_SEGMENTS = ZP_TMP_22 + 1
;14 is temp storage here
ACCUMULATED_SIZE = ZP_TMP_5 ; Reusing a ZP that is finished with when this one is used
STACK_ZP = ZP_TMP_6 ; Reusing a ZP that is finished with when this one is used
DBL_SEGMENTS = ZP_TMP_2 ; Reusing a ZP that is finished with when this one is used

SIZES = GOLDEN_RAM_WORK_AREA + 64 ;Stores the sizes of the various segments, the fifty is just arbitrary; all it has to be is a number that is big enough to avoid clashing with the addresses

.macro PARTITION_MEMORY
.local @endDivideSegments
.local @createSegmentPointersLoop

ldy #$0
ldx NO_SEGMENTS ;x is serving as our counter
@createSegmentPointersLoop: ;This is for partioning memory into segments. It assumes that the first address in already store in ZP_TMP_14
lda ZP_TMP_14
sta GOLDEN_RAM_WORK_AREA, y ; Store the address in the golden ram work area
pha ;Store the starting low byte for use later when we allocate the storage memory

iny
lda ZP_TMP_14 + 1
sta GOLDEN_RAM_WORK_AREA, y
pha ;Store the high byte for later

iny

dex
cpx #$0
beq @endDivideSegments

clc
lda SEGMENT_SIZE ;Work out the next address by adding segment_size to the previous address
adc ZP_TMP_14
sta ZP_TMP_14
lda ZP_TMP_14 + 1
adc SEGMENT_SIZE + 1
sta ZP_TMP_14 + 1
bra @createSegmentPointersLoop

@endDivideSegments:
.endmacro

MAX_SPLIT_COLUMNS = 8
MAX_SPLIT_ROWS = 4
POINTER_TO_SPLIT_DATA_SIZE = MAX_SPLIT_COLUMNS * MAX_SPLIT_ROWS * 2
.macro PREPARE_BUFFER_SPLIT_CEL
stz SPLIT_BUFFER_STATUS + 3

lda CEL_DATA
sta SPLIT_BUFFER_STATUS
lda CEL_DATA + 1
sta SPLIT_BUFFER_STATUS + 1

lda CEL_DATA_BANK
sta SPLIT_BUFFER_STATUS + 2

REFRESH_BUFFER SPLIT_BUFFER_POINTER, SPLIT_BUFFER_STATUS ;Uses the work area to buffer the run encoded data. Using the b5RefreshBuffer function from C

.endmacro
;void bESplitCel(Cel* cel);
_bESplitCel: ;The goal of this function is to PREPARE to split a cel into segments that can be drawn to the vera. The segments are stored in banked memory, and the pointers to the segments are stored in the cel struct
;There are two split cel methods, this one for preparation and bCSplitCel for splitting.
;;There are two main area of memory dealt with here. The buffer refers to the large temp. buffer on bank C used by the split function. The buffer is never cleared, only overwritten with new data, so there may be stale data on the end. 
;The code knows when to stop reading the buffer by the end addresses as calculated during the execution of bCSplitCel.
;The format of the buffer and allocated memory is: address of segment 0 (two bytes) . . . address of no segments - 1, data. 
;The addresses point to somewhere in the data.
; Note the buffer is never cleared so not all of the data is used
;Allocated memory refers to the block
; of dynamic memory allocated as the permanent storage location of the data.
;This method has several steps, in this order.
; 1. Read the cel struct to get the width, height, and cel data address and bank
; 2. Divide the memory by the number of segments
; 3. Now partition the bCSplitBuffer buffer into equal sized segments to the allocated memory. Note even though this buffer is larger then the allocated memory, any excess space will be discounted. This allows for the pointers to both buffers to point to the same data eg. the first pointer in the allocated memory will point to the first segment in the buffer will point the same data as the first pointer for the buffer and so on
; 4. Store the pointers to the divisions of bCSplitBuffer buffer in bCSplitBufferSegments
; 5. Call bCSplitCel
; 6. Determine how much dynamic memory needs to be allocated to permanently store the data, we can't just copy the data from the buffer because there are huge gaps. Then copy to the allocated memory

sta CEL_STRUCT_POINTER
stx CEL_STRUCT_POINTER + 1

;1. Read cel struct data

GET_STRUCT_16_STORED_OFFSET _offsetOfBmp, CEL_STRUCT_POINTER, CEL_DATA
GET_STRUCT_8_STORED_OFFSET _offsetOfCelWidth, CEL_STRUCT_POINTER, SPLIT_CEL_WIDTH


GET_STRUCT_8_STORED_OFFSET _offsetOfCelHeight, CEL_STRUCT_POINTER, SPLIT_CEL_HEIGHT
GET_STRUCT_8_STORED_OFFSET _offsetOfBmpBank, CEL_STRUCT_POINTER, CEL_DATA_BANK
lda SPLIT_CEL_WIDTH
beq @invalidDimensions
lda SPLIT_CEL_HEIGHT
bne @validDimensions
@invalidDimensions:
jmp bEAbortSplit
@validDimensions:

lda #< SPLIT_BUFFER_SIZE 
ldx #> SPLIT_BUFFER_SIZE 
sta NO_BYTES_SIZE
stx NO_BYTES_SIZE + 1


; 2. Divide the memory by the number of segments
lda SPLIT_CEL_WIDTH ;64 VERA pixels span32 AGI pixels. Preserve the carry.
clc
adc #31
ror
lsr
lsr
lsr
lsr

@loadWidth:
ldx #$0; High byte is always zero
sta SEGMENTS_ACROSS
stx SEGMENTS_ACROSS + 1
sta ZP_TMP_14
stx ZP_TMP_14 + 1

lda SPLIT_CEL_HEIGHT
cmp #64
bcc @heightLessThan64
clc
adc #63 ; Adding 63 to the height will ensure we round up when we divide by 64
ror ; Carry is the ninth bit of height+63; retain it in the quotient.
lsr
lsr
lsr
lsr
lsr 
bra @loadHeight

@heightLessThan64:
lda #$1 ;If height is less than 64 we only need one segment

@loadHeight:
ldx #$0 ;High byte is always zero

cmp #1
beq @storeSegmentSize ;Height is 1 we can skip the multiply as we would just be multiply width by 1. We already have width in ZP_TMP_14

ldy #$1
cpy SEGMENTS_ACROSS ;If width is 1 we can also skip the multiply, as we would just be multiplying height by 1. Note if both values were 1 we would never have gotten into the split function in the first place

bne @multiply
beq @storeMultiplyResult


@multiply:
jsr pushax ;We already have the height in a and x
lda SEGMENTS_ACROSS ; Get Width
ldx #$0
TRAMPOLINE #HELPERS_BANK, _b5Multiply

@storeMultiplyResult:
sta ZP_TMP_14
@storeSegmentSize: 
lda ZP_TMP_14
sta NO_SEGMENTS

@divideMemoryBySegments: ;Now we know width and height we can divide the memory by the number of segments to get the segment size
sec
lda NO_BYTES_SIZE ;We don't count the part of the memory reserved for the pointers in the segment division
sbc #<POINTER_TO_SPLIT_DATA_SIZE
tay ;Store the low byte in y as we will need this when we call divide, but we need a to do the high byte of the subtraction
lda NO_BYTES_SIZE + 1
sbc #>POINTER_TO_SPLIT_DATA_SIZE
tax ;The high byte must be in x prior to the call to the division function
tya ;The low byte must be in a prior to the call to the division function
jsr pushax

lda NO_SEGMENTS
ldx #$0

SET_STRUCT_8_STORED_OFFSET_VALUE_IN_REG _offsetOfSplitSegments, CEL_STRUCT_POINTER

TRAMPOLINE #HELPERS_BANK, _b5Divide ;Divide the total amount of memory by the number of segments
sta SEGMENT_SIZE
stx SEGMENT_SIZE + 1

;3. Now partition the bCSplitBuffer buffer into equal sized segments to the allocated memory
lda #<bCSplitBuffer 
sta ZP_TMP_14
lda #>bCSplitBuffer 
sta ZP_TMP_14 + 1

PARTITION_MEMORY

;4. Store the pointers to the divisions of bCSplitBuffer buffer
lda #< bCSplitBufferSegments
ldx #> bCSplitBufferSegments
jsr pushax

lda #<GOLDEN_RAM_WORK_AREA
ldx #>GOLDEN_RAM_WORK_AREA
jsr pushax

lda #SPLIT_BUFFER_BANK
jsr pusha

lda NO_SEGMENTS
asl
ldx #$0
jsr _memCpyBanked

lda #<bCSplitBuffer 
sta ZP_TMP_14
lda #>bCSplitBuffer 
sta ZP_TMP_14 + 1

;5. Call bCSplitCel
PREPARE_BUFFER_SPLIT_CEL ;While in a perfect world the prep steps would live in bCSplitCel, it is here because there isn't much room left on C
;Clear out the portion of the buffer we will be using
lda #< bCSplitBuffer
ldx #> bCSplitBuffer
jsr pushax

lda #$0
ldx #$0
jsr pushax

lda NO_BYTES_SIZE
ldx NO_BYTES_SIZE + 1
jsr pushax

lda #SPLIT_BUFFER_BANK
ldx #$0

jsr _memsetBanked

TRAMPOLINE #SPLIT_BUFFER_BANK, _bCSplitCel
cmp #0
bne @splitReady
; Partition start pointers are still on the hardware stack. Retire all of them.
lda NO_SEGMENTS
asl
tax
@unwindFailedSplit:
pla
dex
bne @unwindFailedSplit
jmp bEAbortSplit
@splitReady:

;6 Determine how much dynamic memory needs to be allocated to permanently store the data, we can't just copy the data from the buffer because there are huge gaps. Then copy to the allocated memory
;Important to note while reading. 
;The start addresses of the segments of the buffer will have been placed on top of the stack with the high byte of the address of the
;last segment at the top down to the low byte of the first segment at the bottom. Therefore if you start with a pointer to the low of the first, you need to use the subtraction
;operator to get up to the other bytes.
lda NO_SEGMENTS
asl
sta DBL_SEGMENTS
stz DBL_SEGMENTS + 1 ;Double the number of segments because each address is two bytes

;6.1 Get the end addresses, from the buffer
;Get the addresses from the beginning of the split buffer. Note addresses won't be the start of the data, but the end (plus a byte for each which is where the splitter function would have put the next byte had there been one)
lda #< GOLDEN_RAM_WORK_AREA
ldx #> GOLDEN_RAM_WORK_AREA
jsr pushax

lda #< bCSplitBufferSegments
ldx #> bCSplitBufferSegments
jsr pushax

lda #SPLIT_BUFFER_BANK
jsr pusha

lda DBL_SEGMENTS
ldx DBL_SEGMENTS + 1
jsr _memCpyBanked

;6.2 Get the start addresses from the buffer, and and the end addresses to calculate how much dynamic memory we need to allocate
tsx ;The stack pointer - DBL_SEGMENTS will point to the start of the first segment, and two bytes after the next address and so on. These were put onto the stack in partition
txa
clc
adc DBL_SEGMENTS ; the stack plus the number of segments times 2 gets us the first original segment address
sta STACK_ZP
lda #$1 ;Stack always has 1 for a high byte
sta STACK_ZP + 1

ldy #$0
stz ACCUMULATED_SIZE ;These variable will keep track of how much size we have counted
stz ACCUMULATED_SIZE + 1

@sizeAddLoop: ;Loop number of segment times calculating the size of each segment and the size of all segments in total
sec
lda GOLDEN_RAM_WORK_AREA,y ;Get the buffer ending address and subtract it from the buffer starting address to calcuate the size
sbc (STACK_ZP)
dec STACK_ZP
sta SIZES,y
lda GOLDEN_RAM_WORK_AREA + 1,y
sbc (STACK_ZP)
sta SIZES + 1, y
dec STACK_ZP

clc ;Keep cumulative track of the size  
lda SIZES,y
adc ACCUMULATED_SIZE
sta ACCUMULATED_SIZE
lda SIZES + 1,y
adc ACCUMULATED_SIZE + 1
sta ACCUMULATED_SIZE + 1

iny ;Each address is two bytes
iny

@checkSizeAddLoop:
cpy DBL_SEGMENTS
bne @sizeAddLoop

;6.3 Add DBL_SEGMENTS to the total size to allow space for pointers at the beginning
clc
lda ACCUMULATED_SIZE
adc DBL_SEGMENTS
sta ACCUMULATED_SIZE
lda ACCUMULATED_SIZE + 1
adc DBL_SEGMENTS + 1
sta ACCUMULATED_SIZE + 1

;6.4 Call banked alloc to actually allocate the memory
lda ACCUMULATED_SIZE
ldx ACCUMULATED_SIZE + 1
jsr pushax

lda #< SPLIT_BANK
ldx #$0
TRAMPOLINE #BANKED_ALLOC_BANK, _b10BankedAlloc
sta SPLIT_DATA
stx SPLIT_DATA + 1
SET_STRUCT_16_STORED_OFFSET_VALUE_IN_REG _offsetOfSplitCelPointers, CEL_STRUCT_POINTER

lda SPLIT_BANK
SET_STRUCT_8_STORED_OFFSET_VALUE_IN_REG _offsetOfSplitCelBank, CEL_STRUCT_POINTER


; 6.5 Copy the data from bCSplitBuffer to the allocated memory
clc ;Calculate the end address + 1 of the allocated memory
lda SPLIT_DATA
adc ACCUMULATED_SIZE
sta ZP_TMP_14
lda SPLIT_DATA + 1
adc ACCUMULATED_SIZE + 1
sta ZP_TMP_14 + 1

@storeFinalResultLoop: ;Iterate no of segments times backwards to copy the segments 1 at a time into the allocated banked memory. We start at 'no segments' (1 out of range) go backwards by two immediately to get back into range
@checkFinalResultStoreLoop:
ldy DBL_SEGMENTS ;Can't just keep this in y permanently as the jsr calls destroy it 
dey
dey
sty DBL_SEGMENTS

bmi @endStoreFinalResultLoop ;When DBL_SEGMENTS reaches 0xFF it is negative 

sec
lda ZP_TMP_14 ;Minus the indexed size will get you to the start of that size
sbc SIZES,y
sta ZP_TMP_14
lda ZP_TMP_14 + 1
sbc SIZES + 1,y
sta ZP_TMP_14 + 1

tax ;Keep the high byte in x
lda ZP_TMP_14 ;Get the low byte again from a
jsr pushax

lda SPLIT_BANK
jsr pusha

pla ;The address to copy from is stored on the stack. It is the start of the segments
tax
pla
jsr pushax

lda #SPLIT_BUFFER_BANK
jsr pusha

ldy DBL_SEGMENTS
lda SIZES,y
ldx SIZES + 1,y

jsr _memCpyBankedBetween

; 6.6 add data pointers to the beginning of the segment. It will be the start of the split data plus the segment we are up to x2
clc 
lda DBL_SEGMENTS
adc SPLIT_DATA
tay
lda #$0
adc SPLIT_DATA + 1
tax
tya

jsr pushax

lda #ZP_TMP_14 ; Copy the address pointer to the beginning of the data
ldx #$0
jsr pushax

lda SPLIT_BANK
jsr pusha

lda #$2
ldx #$0

jsr _memCpyBanked

bra @storeFinalResultLoop
@endStoreFinalResultLoop:
@end:
rts
bEAbortSplit:
lda #0
ldx #0
SET_STRUCT_16_STORED_OFFSET_VALUE_IN_REG _offsetOfSplitCelPointers, CEL_STRUCT_POINTER
SET_STRUCT_8_STORED_OFFSET_VALUE_IN_REG _offsetOfSplitSegments, CEL_STRUCT_POINTER
SET_STRUCT_8_STORED_OFFSET_VALUE_IN_REG _offsetOfSplitCelBank, CEL_STRUCT_POINTER
rts
.endif

.segment "BANKRAM0C"
SPLIT_BUFFER_SIZE = 4000
bCSplitBuffer: .res SPLIT_BUFFER_SIZE
bCSplitBufferSegments: .res POINTER_TO_SPLIT_DATA_SIZE
bCSplitBufferSegmentEnds: .res POINTER_TO_SPLIT_DATA_SIZE

;Still uses Uses ZP_TMP_14 as temp
WIDTH_SEG_COUNTER = ZP_TMP_16 ;Counter for which width segment we are currently using
HEIGHT_SEG_COUNTER = ZP_TMP_16 + 1 ;Counter for which height segment we are currently using
SEGMENT_POINTER = ZP_TMP_17 ;Stores a pointer to the current segment we are writing to
PIXELS_WIDTH_COUNTED_SO_FAR = ZP_TMP_18 ;Counter for the number of pixels we have counted so far. Note this resets when we reach the maximum of 32
SEGMENT_POINTER_COUNTER = ZP_TMP_18 + 1 ; Which segment pointer we are currently using
PIXEL_AMOUNT = ZP_TMP_19 ;How many pixels was the byte trying to draw
;Uses ZP_TMP_14 and ZP_TMP_20 as tmp
ROWS_SO_FAR = ZP_TMP_21 ;How many rows have been processed so far

SPLIT_WRITE_OFFSET = ZP_TMP_23
SPLIT_FAILED = ZP_TMP_23 + 1
.macro CHECK_SPLIT_WRITE
.local @fits
jsr bCCheckSegmentWrite
bcc @fits
lda #1
sta SPLIT_FAILED
jmp bCSplitFailure
@fits:
.endmacro
.macro INCREMENT_SEGMENT ;When we have finished with a segment we need to add to it the number of the times we wrote to it, as we will return to it the next line. Also we should pad it with one zero
CHECK_SPLIT_WRITE
iny ;We need to add a single zero on the end, and this is the easiest way to do it
tya
tax ;Where we are in the current segment in preserved in x
lda SEGMENT_POINTER_COUNTER ;Double the segment pointer counter, as there are two bytes per address
asl
tay ;Put the doubled segment pointer into y to be an index
txa ;Bring were we are in the current segment back into a

clc ;Add where we are in the current segment to the segment pointer
adc bCSplitBufferSegments,y
sta bCSplitBufferSegments,y
iny
lda #$0
adc bCSplitBufferSegments,y
sta bCSplitBufferSegments,y
.endmacro

.macro GO_TO_NEXT_SEGMENT
INCREMENT_SEGMENT ;Increment the current segment pointer, so when we write to it again we won't clobber what we have written

inc WIDTH_SEG_COUNTER ;Ready to do the next width segment
jsr _bCSetSegmentPointer ;Calculate the segment pointer for the next width segment
ldy #$0 ;Reset the counter for where we are in the next segment
.endmacro

;void bCSplitCel() ;Don't take any arguments, because all of the data is stored in the zero page
;This method will split the cel into segments and store the segments in the bCSplitBuffer buffer
_bCSplitCel: ;Must be called by bESplitCel, which does all of the prepartion, as this depends on the data in the zero page values set up by bESplitCel and the buffer prepare macro being called
.ifdef DEBUG_SPLIT
lda debugCounter
.endif

; Fixed exclusive ends survive changes to the current segment pointers.
stz SPLIT_FAILED
lda NO_SEGMENTS
asl
tax
@initializeEnds:
dex
dex
clc
lda bCSplitBufferSegments,x
adc SEGMENT_SIZE
sta bCSplitBufferSegmentEnds,x
lda bCSplitBufferSegments+1,x
adc SEGMENT_SIZE+1
sta bCSplitBufferSegmentEnds+1,x
cpx #0
bne @initializeEnds
;Initialize data
stz WIDTH_SEG_COUNTER 
stz HEIGHT_SEG_COUNTER
stz PIXELS_WIDTH_COUNTED_SO_FAR
stz SEGMENT_POINTER_COUNTER
stz ROWS_SO_FAR
jsr _bCSetSegmentPointer

@heightLoop:

ldy #$0
@widthLoop:
GET_NEXT SPLIT_BUFFER_POINTER, SPLIT_BUFFER_STATUS ;Get the next byte from the run encoded data. The byte will be in the format AX where A is the colour and X is the number of pixels
.ifdef DEBUG_SPLIT
ldx debugCounter
inc debugCounter
bne @compare
@debugHigh:
inc debugCounter + 1

@compare:
ldx debugCounter
cpx #$F4
bcc @continue
ldx debugCounter + 1
cpx #$1
bcc @continue
nop
ldx debugCounter
@continue:
.endif

cmp #$0 ;We have reached the end of the line if we see zero
bne @countPixels
jmp @checkHeightLoopCondition

@countPixels:
tax
and #$0F; The number of pixels is the lower 4 bits
sta PIXEL_AMOUNT
clc
adc PIXELS_WIDTH_COUNTED_SO_FAR ;We will go into the next width segment if we have counted 32 pixels
cmp #64 / 2 ;Divide by two because in Sierra AGI pixels are double width, so 32 agi pixels fit in 64
bne @notExactWidth
jmp @widthEqualToSegment
@notExactWidth:
bcs @widthOverflow
jmp @widthWithinSegment

@widthOverflow:
sec
sbc #64 / 2 ;Divide by two because in Sierra AGI pixels are double width, so 32 agi pixels fit in 64
sta ZP_TMP_14 ;Amount over stored in ZP_TMP_14

lda PIXEL_AMOUNT
sec
sbc ZP_TMP_14 ;Take away the amount over
sta ZP_TMP_20 ; Amount under stored in ZP_TMP_20

txa; Bring back the whole byte
and #$F0; The colour is the upper 4 bits
sta ZP_TMP_14 + 1 ;Store the colour in ZP_TMP_14 + 1

lda ZP_TMP_20 ; Bring the amount under back into a
clc
adc ZP_TMP_14 + 1 ;Add the colour to the amount under

CHECK_SPLIT_WRITE
sta (SEGMENT_POINTER), y ;Store amount under and colour in the segment
iny

GO_TO_NEXT_SEGMENT ;We have stored the portion of this byte that belongs in this segment, now the remainder will be stored in the next segment

cmp #$0 ;Check the return value of bCSetSegmentPointer to see if we are under the maximum number of segments
beq @gotoWidthLoop ;This is an edge case where there is nothing under, so we can just go to the next width loop

lda ZP_TMP_14 ; Bring the amount over back into a
sta PIXELS_WIDTH_COUNTED_SO_FAR ;Reset the amount counted so far

clc
adc ZP_TMP_14 + 1 ;Add the colour to the amount over

CHECK_SPLIT_WRITE
sta (SEGMENT_POINTER) ;y will always be zero here, so we can just store the amount over in the segment
iny

@gotoWidthLoop:
jmp @widthLoop

@widthWithinSegment: ;We still have room to store this value in the segment
sta PIXELS_WIDTH_COUNTED_SO_FAR ;Add the number of pixels to the number of pixels counted so far
txa ;Bring back the whole byte
CHECK_SPLIT_WRITE
sta (SEGMENT_POINTER), y ;Store it
iny ;Increment for the next byte
jmp @widthLoop

@widthEqualToSegment: ;We have exactly the right amount of pixels to fill the segment
lda #$0 
sta PIXELS_WIDTH_COUNTED_SO_FAR ;Reset the amount counted so far
txa ;Bring back the whole byte
CHECK_SPLIT_WRITE
sta (SEGMENT_POINTER), y ;Store the value
iny

GO_TO_NEXT_SEGMENT

jmp @widthLoop

@checkHeightLoopCondition: ;Getting ready for the next line
lda WIDTH_SEG_COUNTER
cmp SEGMENTS_ACROSS
bcs @finishRow
INCREMENT_SEGMENT ;Add a zero on the end of the line
@finishRow:
jsr bCPadUnusedSegmentsForLine ; Pad unused segments for this row with zeros
lda SPLIT_FAILED
beq @rowFits
jmp bCSplitFailure
@rowFits:

inc ROWS_SO_FAR ;Stop when we have processed all of the rows
lda ROWS_SO_FAR
cmp SPLIT_CEL_HEIGHT
beq @end

cmp #64 ;Checking to see if we have reached the end of the height segment
beq @increaseHeightSegment
cmp #64 * 2
beq @increaseHeightSegment
cmp #64 * 3
beq @increaseHeightSegment
bra @repeatForNextRow

@increaseHeightSegment:
inc HEIGHT_SEG_COUNTER

@repeatForNextRow:
lda #$0
sta WIDTH_SEG_COUNTER ;Go back to the first width segment, for a new row
jsr _bCSetSegmentPointer ;Set the segment pointer for this new row, and column 0
stz PIXELS_WIDTH_COUNTED_SO_FAR ;Reset the number of pixels counted so far
.ifdef DEBUG_SPLIT

lda debugCounter
.endif
jmp @heightLoop
@end:
lda #1
rts
bCSplitFailure:
lda #0
rts
.ifdef DEBUG_SPLIT
debugCounter: .word $0
.endif
;boolean bCSetSegmentPointer()
;Figures out which segment we should be in based upon width and height seg counters
;Returns 1 if we are under the maximum number of segments, 0 if we are over
_bCSetSegmentPointer:
.ifdef DEBUG_SPLIT
lda debugCounter
cmp #$E8
bne @checkIfHeightIs0
;stp
.endif

@checkIfHeightIs0:
; Row index is a segment count, never the pixel width. At most three adds.
lda #0
ldx HEIGHT_SEG_COUNTER
beq @addWidthCounter
@rowOffset:
clc
adc SEGMENTS_ACROSS
dex
bne @rowOffset

@addWidthCounter:
clc ;Add the width seg counter to the result of the multiplication, or just the height seg counter if height is 0 or 1
adc WIDTH_SEG_COUNTER

@getSegmentPointer:
sta SEGMENT_POINTER_COUNTER ;Store the result in the segment pointer counter

asl ;Now we need to retrieve the pointer to the segment we are going to write to. Two bytes per address so we multiply by 2
tay 

lda bCSplitBufferSegments, y ;Get the low byte of the address 
sta SEGMENT_POINTER
iny
lda bCSplitBufferSegments, y; Ando now get the high
sta SEGMENT_POINTER + 1

lda SEGMENT_POINTER_COUNTER ;Figure out if we are under the maximum number of segments
cmp NO_SEGMENTS
bcc @underMax

@overMax:
lda #$0 ;Return 0 for over
bra @end

@underMax:
lda #$1; Return 1 for under

@end:
ldx #$0; High byte of return value is always zero
rts

bCPadUnusedSegmentsForLine: ;If we reach a new line without having used all of the horizontal segments we need to pad the unused segments with zeros
@padUnusedSegmentLoop:
.ifdef DEBUG_SPLIT
lda debugCounter
cmp #$C2
bcc @continue
;stp
@continue:
.endif

inc WIDTH_SEG_COUNTER ;The segment we are currently on has definalty been used, so we can increment the counter
lda SEGMENTS_ACROSS ;Minus one from segments across because seg counter is zero indexed
dec
cmp WIDTH_SEG_COUNTER
bcc @end ;If the width seg counter is greater than or equal to the number of segments across we have used all of the segments for this row

jsr _bCSetSegmentPointer
ldy #0
INCREMENT_SEGMENT

bra @padUnusedSegmentLoop

@end:
rts

; Preserve the encoded byte and offset; carry reports an exclusive-end violation.
bCCheckSegmentWrite:
pha
phx
phy
sty SPLIT_WRITE_OFFSET
lda SEGMENT_POINTER_COUNTER
asl
tay
clc
lda SEGMENT_POINTER
adc SPLIT_WRITE_OFFSET
tax
lda SEGMENT_POINTER+1
adc #0
cmp bCSplitBufferSegmentEnds+1,y
bne @checked
 txa
cmp bCSplitBufferSegmentEnds,y
@checked:
ply
plx
pla
rts
