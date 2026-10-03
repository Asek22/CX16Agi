#include "../../commands.h"
#include "../../object.h"
#include "../../inventory.h"
#include "../../spriteAllocator.h"
#include <cx16.h>
#include <string.h>
#include <stdlib.h>
#include "audit.h"
#include "../../keyboard.h"

extern void loadInitBankAndInitMemory(void);
extern byte __fastcall__ testPeek(unsigned long address);
extern void __fastcall__ testPoke(byte value, unsigned long address);
extern byte bDObjData[];
extern void testExit(void);
extern void testPutKey(byte key);
extern void testObserveIrq(void);
extern void __fastcall__ testFeedKeys(const byte* keys);
extern unsigned testVisibleFrames;
#pragma wrapped-call (push, trampoline, 6)
extern void b6InitIrq(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 4)
extern void b4Status(void);
#pragma wrapped-call (pop)

static void output(const char* text)
{
    while (*text) *(volatile byte*)0x9fbb = *text++ & 0x7f;
}
static void check(byte ok, const char* message)
{
    if (!ok) {
        output("INVENTORY FAIL: ");
        output(message);
        testExit();
    }
}
void auditCheck(byte ok, const char* message) { check(ok, message); }
void auditOutput(const char* text) { output(text); }
extern void soundAuditCases(void), metadataAuditCases(void), allocatorAuditCases(void);
extern void inputAuditCases(void), viewsAuditCases(void), showObjAuditCases(void);
extern void resource20AuditCases(void), render20AuditCases(void), auditIrq20(void);
static void makeObjects(void)
{
    objectType object;
    int count = 3;
    static byte names[] = {100,97,103,103,101,114,0,99,97,114,114,111,116,0,98,117,99,107,101,116,0};
    byte* text;
    /* Object names belong to the real OBJECT bank, as in the product loader. */
    text = bDObjData;
    memCpyBanked(text, names, OBJECT_BANK, sizeof(names));
    memCpyBanked((byte*)&bFNumObjects, (byte*)&count, OBJECT_BANK, sizeof(count));
    object.roomNum = 255;
    object.name = (char*)text;
    bDSetObject(0, &object);
    object.name = (char*)text + 7;
    bDSetObject(1, &object);
    object.name = (char*)text + 14;
    bDSetObject(2, &object);
}
extern byte bDSpriteAllocTable[];
extern byte allocationArray[];
static byte attrs[8], mouseAttrs[8], allocation[TOTAL_REAL_BLOCKS];
static byte baselineAllocations[ALLOCATION_ARRAY_SIZE], currentAllocations[ALLOCATION_ARRAY_SIZE];
static byte pattern(unsigned long address)
{
    return (byte)(address ^ (address >> 8));
}
static void seedBackground(void)
{
    unsigned long address;
    for (address = 0; address < 0x9600; ++address) testPoke(pattern(address), address);
    for (address = MAPBASE; address < MAPBASE+4096; ++address) testPoke(pattern(address), address);
}
static void checkBackground(void)
{
    unsigned long address;
    byte i;
    memCpyBanked(currentAllocations, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(currentAllocations));
    check(!memcmp(currentAllocations, baselineAllocations, sizeof(currentAllocations)), "inventory save-under allocation leaked");
    for (i=0; i<8; ++i) check(testPeek(SPRITE_ATTRIBUTES_START+i) == mouseAttrs[i], "mouse overlay not restored");
    for (address = 0; address < 0x9600; ++address)
        check(testPeek(address) == pattern(address), "inspect changed room pixels");
    for (address = MAPBASE+5*128+4; address < MAPBASE+24*128; ++address)
        if ((address-MAPBASE)%128 >= 4 && (address-MAPBASE)%128 < 76)
            check(testPeek(address) == pattern(address), "inventory did not restore every panel tile");
}
static void checkClosed(void)
{
    byte i;
    unsigned j;
    for (i=0; i<8; ++i) check(testPeek(0x1fff8UL+i) == attrs[i], "inspect left sprite attributes behind");
    memCpyBanked(allocation, bDSpriteAllocTable, OBJECT_BANK, sizeof(allocation));
    for (j=0; j<TOTAL_REAL_BLOCKS; ++j) check(allocation[j] == 0, "inspect bitmap not released");
}
static void checkArt(byte object, unsigned char expected, byte row, byte column)
{
    unsigned long address;
    unsigned x, y, painted = 0;
    byte pixel;
    check(inventoryInspectOpen(object), "built-in object artwork missing");
    check(testPeek(0x1fffeUL) == 12 && testPeek(0x1ffffUL) == 0xf0, "wrong sprite size or visibility");
    address = ((unsigned long)testPeek(0x1fff8UL) | ((unsigned long)testPeek(0x1fff9UL) << 8))*32;
    check(testPeek(address+(unsigned)row*64+column) == expected, "wrong built-in art pixel");
    for (y=0; y<32; ++y) for (x=0; x<32; ++x) {
        pixel = testPeek(address+y*64+x);
        check((pixel & 15) == (pixel >> 4), "horizontal art expansion differs");
        check(pixel == testPeek(address+y*64+32+x), "vertical art expansion differs");
        if (pixel) ++painted;
    }
    check(painted > 100, "big sprite is empty");
    check(!inventoryInspectOpen(object), "second inspect overwrote ownership");
    inventoryInspectClose();
    checkClosed();
}
static unsigned long cycles(void)
{
    byte a = *(volatile byte*)0x9fb8;
    byte b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba;
    byte d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) | ((unsigned)b << 8) | a;
}
static void number(unsigned long value)
{
    char digits[11];
    byte n = 0;
    do { digits[n++] = 48 + value % 10; value /= 10; } while (value);
    while (n) *(volatile byte*)0x9fbb = digits[--n];
}
static void fullInventoryPerformance(void)
{
    objectType object;
    int count = MAX_OBJECTS;
    unsigned i;
    unsigned long start, elapsed;
    makeObjects();
    bDGetObject(0, &object);
    for (i=0; i<MAX_OBJECTS; ++i) bDSetObject((byte)i, &object);
    memCpyBanked((byte*)&bFNumObjects, (byte*)&count, OBJECT_BANK, sizeof(count));
    start = cycles();
    for (i=0; i<8; ++i) {
        testPutKey(KEY_ESC);
        check(inventoryShow(FALSE) == 255, "full inventory cancel failed");
    }
    elapsed = (cycles()-start)/8;
    output("FULL INVENTORY OPEN/CLOSE CYCLES: "); number(elapsed); output("; ");
    /* Original per-character reads: 2,393,786 cycles per open/close.
     * Keep at least a 25% reduction, allowing normal IRQ timing variation. */
    check(elapsed < 1800000UL, "full inventory name reads regressed above CPU budget");
    checkBackground();
}
static void nameAndPagingCases(void)
{
    static byte keys[256];
    static byte mixed[] = {68,97,71,103,69,114,0};
    objectType object;
    byte old, one = 88;
    unsigned i;
    /* fullInventoryPerformance has created 255 carried objects. */
    for (i=0; i<254; ++i) keys[i] = KEY_DOWN;
    keys[254] = KEY_ENTER; keys[255] = 0;
    testFeedKeys(keys);
    check(inventoryShow(TRUE) == 254, "last inventory page lost object 254");
    keys[0]=KEY_UP; keys[1]=KEY_ENTER; keys[2]=0;
    testFeedKeys(keys);
    check(inventoryShow(TRUE) == 0, "up at first object wrapped selection");
    checkBackground();
    bDGetObject(0, &object);
    memCpyBanked((byte*)object.name, mixed, OBJECT_BANK, sizeof(mixed));
    checkArt(0, 0xff, 10, 15);
    object.name = (char*)BANK_MAX;
    memCpyBanked(&old, (byte*)BANK_MAX, OBJECT_BANK, 1);
    memCpyBanked((byte*)BANK_MAX, &one, OBJECT_BANK, 1);
    bDSetObject(0, &object);
    check(!inventoryInspectOpen(0), "unterminated bank-edge name produced art");
    memCpyBanked((byte*)BANK_MAX, &old, OBJECT_BANK, 1);
    object.name = (char*)(BANK_MAX+1); bDSetObject(0, &object);
    check(!inventoryInspectOpen(0), "name outside OBJECT bank accepted");
    object.name = 0; bDSetObject(0, &object);
    check(!inventoryInspectOpen(0), "null name accepted");
    makeObjects();
    checkClosed();
}
static void unexpectedExit(void)
{
    output("INVENTORY FAIL: exhausted save-under terminated the interpreter");
    testExit();
}
static void allocationFailureCases(void)
{
    static byte* pointers[ALLOCATION_ARRAY_SIZE];
    static byte banks[ALLOCATION_ARRAY_SIZE];
    static VeraSpriteAddress sprites[TOTAL_REAL_BLOCKS/64];
    byte count = 0, i;
    unsigned j;
    for (j=TINY_NO_SEGMENTS+EXTRA_SMALL_NO_SEGMENTS; j<ALLOCATION_ARRAY_SIZE; ++j)
        if (!baselineAllocations[j]) {
            pointers[count] = b10BankedAlloc(1368, &banks[count]);
            ++count;
        }
    /* The product must stay alive when the disposable panel cannot allocate. */
    testPutKey(KEY_ESC);
    check(inventoryShow(TRUE) == 255 && !inventoryActive, "exhausted save-under did not cancel safely");
    check(cbm_k_getin() == KEY_ESC, "failed inventory consumed queued input");
    for (i=0; i<count; ++i) check(b10BankedDealloc(pointers[i], banks[i]), "fixture RAM release failed");
    checkBackground();
    count = 0;
    while (count < TOTAL_REAL_BLOCKS/64) {
        sprites[count] = bDFindFreeVramBlock(SPR_SIZE_64, SPR_SIZE_64);
        check(sprites[count] != 0 && sprites[count]+2048 <= VRAM_START+VRAM_SIZE, "sprite allocator overflowed reserved VRAM");
        ++count;
    }
    check(!inventoryInspectOpen(0), "inspect allocated from an exhausted sprite pool");
    inventoryInspectClose();
    for (i=0; i<count; ++i) bDDeleteAllocation(sprites[i], SPR_SIZE_64, SPR_SIZE_64);
    checkClosed(); checkBackground();
    checkArt(0, 0xff, 10, 15);
}
extern unsigned long inventoryAddress;
extern byte inventoryBuffer[], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern void testDisableInterrupts(void);
extern void testEnableInterrupts(void);
extern byte testStatus(void);
static void transferStateCases(void)
{
    static byte lengths[] = {0,1,7,8,9,31,32,71,72,73,79,80};
    byte savedBank = RAM_BANK, i, n;
    unsigned repeat;
    unsigned long start, elapsed;
    testDisableInterrupts();
    RAM_BANK = 0x75;
    for (n=0; n<sizeof(lengths); ++n) {
        inventoryAddress = 0x14000;
        inventoryLength = lengths[n]; inventoryWrite = TRUE;
        for (i=0; i<inventoryLength; ++i) inventoryBuffer[i]=i^0x5a;
        /* Configure both VERA ports and DCSEL independently of the transfer. */
        *(volatile byte*)VERA_ctrl=4;
        *(volatile byte*)VERA_addr_low=0x32;
        *(volatile byte*)VERA_addr_high=0x54;
        *(volatile byte*)VERA_addr_bank=0x41;
        *(volatile byte*)VERA_ctrl=5;
        *(volatile byte*)VERA_addr_low=0x45;
        *(volatile byte*)VERA_addr_high=0x23;
        *(volatile byte*)VERA_addr_bank=0x21;
        inventoryTransfer();
        check((testStatus() & 4) && RAM_BANK == 0x75, "transfer changed IRQ state or RAM bank");
        check(*(volatile byte*)VERA_ctrl == 5, "transfer changed VERA control");
        check(*(volatile byte*)VERA_addr_low == 0x45 && *(volatile byte*)VERA_addr_high == 0x23 && *(volatile byte*)VERA_addr_bank == 0x21, "transfer changed VERA port 1");
        *(volatile byte*)VERA_ctrl=4;
        check(*(volatile byte*)VERA_addr_low == 0x32 && *(volatile byte*)VERA_addr_high == 0x54 && *(volatile byte*)VERA_addr_bank == 0x41, "transfer changed VERA port 0");
        memset(inventoryBuffer, 0, 80);
        inventoryWrite = FALSE; inventoryTransfer();
        for (i=0; i<inventoryLength; ++i) check(inventoryBuffer[i] == (i^0x5a), "transfer read/write mismatch");
        for (; i<80; ++i) check(inventoryBuffer[i] == 0, "transfer exceeded requested length");
    }
    inventoryLength = 72; inventoryWrite = TRUE;
    start=cycles();
    for (repeat=0; repeat<128; ++repeat) inventoryTransfer();
    elapsed=(cycles()-start)/128;
    RAM_BANK=savedBank;
    *(volatile byte*)VERA_ctrl=0;
    testEnableInterrupts();
    output("72-BYTE VRAM TRANSFER CYCLES: "); number(elapsed); output("; ");
    /* Original byte loop: 1,476 cycles for a 72-byte row. */
    check(elapsed < 1100, "VRAM row transfer exceeded CPU budget");
    inventoryTransfer();
    check(!(testStatus() & 4), "transfer left interrupts disabled");
    checkBackground();
}
static void kq1ObjectCases(void)
{
    static byte water[] = {119,97,116,101,114,32,98,117,99,107,101,116,0};
    static byte full[] = {102,117,108,108,32,98,117,99,107,101,116,0};
    objectType object;
    int count = 13;
    byte i;
    makeObjects();
    for (i=0; i<13; ++i) { object.roomNum=0; object.name=0; bDSetObject(i, &object); }
    object.roomNum=255; object.name=(char*)bDObjData; bDSetObject(1, &object);
    object.name=(char*)bDObjData+7; bDSetObject(3, &object);
    memCpyBanked(bDObjData+32, water, OBJECT_BANK, sizeof(water));
    object.name=(char*)bDObjData+32; bDSetObject(11, &object);
    memCpyBanked(bDObjData+48, full, OBJECT_BANK, sizeof(full));
    object.name=(char*)bDObjData+48; bDSetObject(12, &object);
    memCpyBanked((byte*)&bFNumObjects, (byte*)&count, OBJECT_BANK, sizeof(count));
    output("KQ1 OBJECT NAME CASES; ");
    checkArt(1, 0xff, 10, 15); checkArt(3, 0x66, 12, 15);
    checkArt(11, 0x77, 20, 15); checkArt(12, 0x77, 20, 15);
    testPutKey(KEY_DOWN); testPutKey(KEY_DOWN); testPutKey(KEY_DOWN); testPutKey(KEY_ENTER);
    check(inventoryShow(TRUE) == 12, "KQ1 full bucket selection lost object number");
    checkBackground();
    makeObjects();
}
int main(void)
{
    byte i;
    objectType object;
    atexit(unexpectedExit);
    loadInitBankAndInitMemory();
    makeObjects();
    bDInitSpriteMemoryManager();
    bDResetSpriteMemoryManager();
    for (i=0; i<8; ++i) { attrs[i] = i == 6 ? 0 : i+16; testPoke(attrs[i], 0x1fff8UL+i); }
    memCpyBanked(baselineAllocations, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(baselineAllocations));
    for (i=0; i<8; ++i) { mouseAttrs[i] = i == 6 ? 0 : i+32; testPoke(mouseAttrs[i], SPRITE_ATTRIBUTES_START+i); }
    b6InitIrq();
    testObserveIrq();
    seedBackground();
    flag[13] = TRUE;
    testPutKey(KEY_DOWN);
    testPutKey(KEY_ENTER);
    b4Status();
    check(var[25] == 1, "status must select the highlighted carried object");
    checkBackground();
    checkArt(0, 0xff, 10, 15);
    checkArt(1, 0x66, 12, 15);
    checkArt(2, 0x77, 20, 15);
    checkBackground();
    /* View consumes one close event and returns to the same highlighted item. */
    testVisibleFrames = 0;
    testPutKey(KEY_DOWN); testPutKey('v'); testPutKey('x'); testPutKey(KEY_ENTER);
    b4Status();
    check(testVisibleFrames != 0, "queued close erased artwork before any visible frame");
    check(var[25] == 1 && !inventoryActive, "view did not return to inventory selection");
    checkClosed(); checkBackground();
    bDGetObject(0, &object); object.name += 1; bDSetObject(0, &object);
    check(!inventoryInspectOpen(0), "unknown object received unrelated artwork");
    check(!inventoryInspectOpen(255) && !inventoryInspectOpen(3), "invalid object lookup accepted");
    inventoryInspectClose(); checkClosed();
    flag[13] = FALSE;
    testPutKey(KEY_ENTER); testPutKey(KEY_ESC); testPutKey(KEY_ESC);
    b4Status();
    check(var[25] == 255, "read-only inventory selected an object");
    for (i=0; i<3; ++i) { bDGetObject(i, &object); object.roomNum=0; bDSetObject(i, &object); }
    flag[13] = TRUE;
    testPutKey(KEY_ENTER); testPutKey('v'); testPutKey(KEY_ESC);
    b4Status();
    check(var[25] == 255 && !inventoryActive, "empty inventory selected an object");
    checkClosed(); checkBackground();
    fullInventoryPerformance();
    nameAndPagingCases();
    allocationFailureCases();
    transferStateCases();
    kq1ObjectCases();
#if defined(AUDIT_CASE_all) || defined(AUDIT_CASE_sound)
    soundAuditCases();
#endif
#if defined(AUDIT_CASE_all) || defined(AUDIT_CASE_metadata)
    metadataAuditCases();
#endif
#if defined(AUDIT_CASE_all) || defined(AUDIT_CASE_allocator)
    allocatorAuditCases();
#endif
#if defined(AUDIT_CASE_all) || defined(AUDIT_CASE_input)
    inputAuditCases();
#endif
#if defined(AUDIT_CASE_all) || defined(AUDIT_CASE_views)
    viewsAuditCases();
#endif
#if defined(AUDIT_CASE_showobj)
    showObjAuditCases();
#endif
#if defined(AUDIT_CASE_resource20)
    resource20AuditCases();
#endif
#if defined(AUDIT_CASE_render20)
    render20AuditCases();
#endif
#if defined(AUDIT_CASE_irq20)
    auditIrq20();
#endif
    output("INVENTORY PASS");
    testExit();
    return 0;
}
