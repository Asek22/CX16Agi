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
