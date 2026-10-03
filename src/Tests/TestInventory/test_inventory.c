#include "../../commands.h"
#include "../../object.h"
#include "../../inventory.h"
#include "../../spriteAllocator.h"
#include <cx16.h>
#include <string.h>
#include "../../keyboard.h"

extern void loadInitBankAndInitMemory(void);
extern byte __fastcall__ testPeek(unsigned long address);
extern void __fastcall__ testPoke(byte value, unsigned long address);
extern byte bDObjData[];
extern void testExit(void);
extern void testPutKey(byte key);
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
int main(void)
{
    byte i;
    objectType object;
    loadInitBankAndInitMemory();
    makeObjects();
    bDInitSpriteMemoryManager();
    bDResetSpriteMemoryManager();
    for (i=0; i<8; ++i) { attrs[i] = i == 6 ? 0 : i+16; testPoke(attrs[i], 0x1fff8UL+i); }
    memCpyBanked(baselineAllocations, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(baselineAllocations));
    for (i=0; i<8; ++i) { mouseAttrs[i] = i == 6 ? 0 : i+32; testPoke(mouseAttrs[i], SPRITE_ATTRIBUTES_START+i); }
    b6InitIrq();
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
    testPutKey(KEY_DOWN); testPutKey('v'); testPutKey('x'); testPutKey(KEY_ENTER);
    b4Status();
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
    output("INVENTORY PASS");
    testExit();
    return 0;
}
