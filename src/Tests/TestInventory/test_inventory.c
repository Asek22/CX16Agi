#include "../../commands.h"
#include "../../object.h"
#include "../../inventory.h"
#include "../../memoryManager.h"
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
extern void testResetVsyncMetrics(void);
extern void __fastcall__ testFeedKeys(const byte* keys);
extern unsigned testVisibleFrames;
extern unsigned testDetailVisibleFrames;
extern volatile unsigned testVsyncIrqs;
extern byte testStatus(void);
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
#pragma wrapped-call (push, trampoline, 0x08)
extern void viewaliasesAuditCases(void);
#pragma wrapped-call (pop)
extern void resource20AuditCases(void), render20AuditCases(void);
#pragma wrapped-call (push, trampoline, 1)
extern void auditIrq20(void);
#pragma wrapped-call (pop)
extern void interpreter20AuditCases(void);
extern void blit20AuditCases(void);
extern void upload20AuditCases(void);
extern void runtime20AuditCases(void);
extern void priority20AuditCases(void);
extern void textlayoutAuditCases(void);
extern void scriptstringsAuditCases(void);
extern void menustorageAuditCases(void);
extern void scriptstreamAuditCases(void);
extern void textoverlayAuditCases(void);
extern void readerdirAuditCases(void);
extern void readerwordsAuditCases(void);
extern void readerobjectAuditCases(void);
extern void readerdebugAuditCases(void);
extern void readerfilenameAuditCases(void);
extern void graphicsapiAuditCases(void);
#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
extern void backbufferAuditCases(void);
#pragma wrapped-call (pop)
static void makeObjects(void);
static unsigned long cycles(void);
static void checkVsyncChain(void);
#if defined(AUDIT_CASE_fullframe) || defined(AUDIT_CASE_integration)
#include "../../agifiles.h"
#include "../../controllers.h"
#include "../../logic.h"
#include "../../menu.h"
#include "../../paletteManager.h"
#include "../../parser.h"
#include "../../random.h"
#include "../../sound.h"
#include "../../timer.h"
#include "../../view.h"
#include "../../words.h"
#pragma wrapped-call (push, trampoline, MEKA_BANK)
extern void b6InitLogics(void), b6InitInterpreter(void), b6InitRandom(void);
extern void b6InitTimer(timer_proc timerProc), b6Timing_proc(void);
#pragma wrapped-call (pop)
extern byte horizon;
#pragma wrapped-call (push, trampoline, MENU_BANK)
extern void bAInitMenus(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_1)
extern void b9LoadViewFile(byte viewNum);
extern void b9SetView(byte viewNum, byte entryNum);
extern void b9SetLoop(ViewTable* table, byte entryNum, byte loopNum);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, SOUND_BANK)
extern void bBLoadSoundFile(int soundNum);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x02)
extern void writeRuntimeBytes(FILE* file, const byte* bytes, unsigned int length,
    const char* message);
extern void writeRuntimeDirectory(const char* name, const unsigned long* offsets,
    byte count);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x01)
extern void fullframeAuditCases(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 1)
extern void integrationAuditCases(void);
extern void integrationAuditPrepare(void);
#pragma wrapped-call (pop)

#pragma bss-name (push, "BSS")
static byte runtimeSoundCode[39];
static unsigned long runtimeLogicOffset[1], runtimePictureOffset[21];
static unsigned long runtimeViewOffset[1], runtimeSoundOffset[2];
static byte runtimeResourceHeader[5];
#pragma bss-name (pop)
static const byte runtimeViewCode[] = {
	0,0,3,0,0,11,0,25,0,47,0,
    1,3,0, 4,4,0,
    0x14,0, 0x14,0, 0x14,0, 0x14,0,
    1,3,0, 4,4,0x80,
	0x11,0x12,0x31,0, 0x11,0x12,0x31,0,
	0x11,0x12,0x31,0, 0x11,0x12,0x31,0,
    1,3,0, 36,8,0,
    0x3f,0x3f,0x36,0, 0x3f,0x3f,0x36,0,
    0x3f,0x3f,0x36,0, 0x3f,0x3f,0x36,0,
    0x3f,0x3f,0x36,0, 0x3f,0x3f,0x36,0,
    0x3f,0x3f,0x36,0, 0x3f,0x3f,0x36,0
};

#pragma code-name (push, "BANKRAM02")
static void writeRuntimeBytesLocal(FILE* file, const byte* bytes, unsigned int length,
    const char* message)
{
    check(file && fwrite(bytes, 1, length, file) == length, message);
}

void writeRuntimeBytes(FILE* file, const byte* bytes, unsigned int length,
    const char* message)
{
    writeRuntimeBytesLocal(file, bytes, length, message);
}

void writeRuntimeDirectory(const char* name, const unsigned long* offsets,
    byte count)
{
    FILE* file = fopen(name, "wb");
    byte i, entry[3];
    check(file != NULL, "runtime directory fixture creation");
    for (i = 0; i < count; ++i) {
        entry[0] = offsets[i] == EMPTY ? 0x0f : (byte)(offsets[i] >> 16) & 0x0f;
        entry[1] = offsets[i] == EMPTY ? 0xff : (byte)(offsets[i] >> 8);
        entry[2] = offsets[i] == EMPTY ? 0xff : (byte)offsets[i];
        writeRuntimeBytesLocal(file, entry, sizeof(entry), "runtime directory fixture write");
    }
    fclose(file);
}
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
extern byte inventoryBuffer[];

static void writeRuntimeFixtureBytes(FILE* file, const byte* bytes,
    unsigned int length, const char* message)
{
	unsigned int offset = 0, amount;
	while (offset < length) {
		amount = length - offset;
		if (amount > 80) amount = 80;
		memcpy(inventoryBuffer, bytes + offset, amount);
		writeRuntimeBytes(file, inventoryBuffer, amount, message);
		offset += amount;
	}
}

void auditRuntimeFixture(void)
{
#if defined(AUDIT_CASE_integration)
    static const byte runtimeLogic[] = {
        0xff, 0xfd, 7, 5, 0xff, 2, 0, 19, 250,
        4, 251, 250, 24, 250, 25, 250, 26,
        41, 1, 0, 43, 1, 0, 33, 1, 54, 1, 15, 67, 1, 35, 1, 37, 1, 140, 150,
        41, 2, 0, 43, 2, 1, 33, 2, 54, 2, 15, 67, 2, 35, 2, 37, 2, 38, 98,
        41, 3, 0, 43, 3, 2, 33, 3, 54, 3, 15, 67, 3, 35, 3, 37, 3, 80, 106,
        41, 4, 0, 43, 4, 0, 33, 4, 54, 4, 15, 67, 4, 35, 4, 37, 4, 120, 114, 0
    };
#elif defined(AUDIT_CASE_fullframe)
	static const byte runtimeLogic[] = {
		3, 251, 77,
		41, 1, 0, 43, 1, 0, 37, 1, 10, 90, 33, 1, 35, 1,
		41, 2, 0, 43, 2, 1, 37, 2, 42, 98, 33, 2, 35, 2,
		41, 3, 0, 43, 3, 2, 37, 3, 80, 106, 33, 3, 35, 3,
		41, 4, 0, 43, 4, 0, 37, 4, 120, 114, 33, 4, 35, 4, 0
	};
#else
	static const byte runtimeLogic[] = {
		0xff, 0xfd, 2, 0, 250, 0xff, 2, 0,
		19, 250, 4, 251, 0, 24, 0, 25, 0, 26, 0, 0
	};
#endif
	static byte pictureCode[] = {0xf0, 5, 0xf6, 10, 80, 20, 80, 0xff};
    static const byte logicTrailer[] = {0, 0, 0};
    static const byte runtimeObjects[] = {
        9, 0, 0,
        9, 0, 255, 16, 0, 255, 23, 0, 255,
        'd', 'a', 'g', 'g', 'e', 'r', 0,
        'c', 'a', 'r', 'r', 'o', 't', 0,
        'b', 'u', 'c', 'k', 'e', 't', 0
    };
    static const byte emptyWords[] = {0, 2};
    FILE* volume;
    unsigned long position = 0;
	unsigned int viewLength, channel, i;
#if defined(AUDIT_CASE_integration)
	LOGICFile loadedLogic;
#endif

    check(RAM_BANK == 0x0f, "runtime fixture did not enter in its assigned bank");
	asm("php");
	asm("sei");
	bFInitPaletteManager();
	asm("plp");
    volume = fopen("vol.0", "wb");
    check(volume != NULL, "runtime VOL fixture creation");
    runtimeLogicOffset[0] = position;
    runtimeResourceHeader[0] = 0x12; runtimeResourceHeader[1] = 0x34; runtimeResourceHeader[2] = 0;
    runtimeResourceHeader[3] = sizeof(runtimeLogic) + sizeof(logicTrailer) + 2;
    runtimeResourceHeader[4] = 0;
    writeRuntimeFixtureBytes(volume, runtimeResourceHeader, sizeof(runtimeResourceHeader), "logic.0 fixture header write");
    runtimeResourceHeader[0] = sizeof(runtimeLogic); runtimeResourceHeader[1] = 0;
    writeRuntimeFixtureBytes(volume, runtimeResourceHeader, 2, "logic.0 code-size write");
    writeRuntimeFixtureBytes(volume, runtimeLogic, sizeof(runtimeLogic), "logic.0 fixture write");
    writeRuntimeFixtureBytes(volume, logicTrailer, sizeof(logicTrailer), "logic.0 message trailer write");
    position += sizeof(runtimeResourceHeader) + 2 + sizeof(runtimeLogic) + sizeof(logicTrailer);
	for (i = 0; i <= 20; ++i) {
		runtimePictureOffset[i] = position;
		pictureCode[1] = i ? (byte)(i % 15 + 1) : 5;
		runtimeResourceHeader[0] = 0x12; runtimeResourceHeader[1] = 0x34; runtimeResourceHeader[2] = 0;
		runtimeResourceHeader[3] = sizeof(pictureCode); runtimeResourceHeader[4] = 0;
		writeRuntimeFixtureBytes(volume, runtimeResourceHeader, sizeof(runtimeResourceHeader), "room picture fixture header write");
		writeRuntimeFixtureBytes(volume, pictureCode, sizeof(pictureCode), "room picture fixture write");
		position += sizeof(runtimeResourceHeader) + sizeof(pictureCode);
	}

    viewLength = sizeof(runtimeViewCode);
    runtimeViewOffset[0] = position;
    runtimeResourceHeader[0] = 0x12; runtimeResourceHeader[1] = 0x34; runtimeResourceHeader[2] = 0;
    runtimeResourceHeader[3] = (byte)viewLength; runtimeResourceHeader[4] = (byte)(viewLength >> 8);
    writeRuntimeFixtureBytes(volume, runtimeResourceHeader, sizeof(runtimeResourceHeader), "VIEW fixture header write");
    writeRuntimeFixtureBytes(volume, runtimeViewCode, viewLength, "VIEW fixture write");
    position += sizeof(runtimeResourceHeader) + viewLength;

    memset(runtimeSoundCode, 0xff, sizeof(runtimeSoundCode));
    for (channel = 0; channel < 4; ++channel) {
        unsigned int offset = 8 + channel * 8;
        runtimeSoundCode[channel * 2] = (byte)offset;
        runtimeSoundCode[channel * 2 + 1] = 0;
        runtimeSoundCode[offset] = 0x50; runtimeSoundCode[offset + 1] = 0xc3;
        runtimeSoundCode[offset + 2] = (byte)(0x30 + channel); runtimeSoundCode[offset + 3] = 5;
        runtimeSoundCode[offset + 4] = 0;
        runtimeSoundCode[offset + 5] = 0xff; runtimeSoundCode[offset + 6] = 0xff;
    }
    for (i = 0; i < 2; ++i) {
        runtimeSoundOffset[i] = position;
        runtimeResourceHeader[0] = 0x12; runtimeResourceHeader[1] = 0x34; runtimeResourceHeader[2] = 0;
        runtimeResourceHeader[3] = sizeof(runtimeSoundCode); runtimeResourceHeader[4] = 0;
        writeRuntimeFixtureBytes(volume, runtimeResourceHeader, sizeof(runtimeResourceHeader), "SOUND fixture header write");
        runtimeSoundCode[8] = (byte)(0x50 + i); /* Distinct notes, each longer than the measured sequence. */
        writeRuntimeFixtureBytes(volume, runtimeSoundCode, sizeof(runtimeSoundCode), "SOUND fixture write");
        position += sizeof(runtimeResourceHeader) + sizeof(runtimeSoundCode);
    }
    fclose(volume);

    writeRuntimeDirectory("logdir", runtimeLogicOffset, 1);
	writeRuntimeDirectory("picdir", runtimePictureOffset, 21);
    writeRuntimeDirectory("viewdir", runtimeViewOffset, 1);
    writeRuntimeDirectory("snddir", runtimeSoundOffset, 2);
    volume = fopen("object", "wb");
    writeRuntimeFixtureBytes(volume, runtimeObjects, sizeof(runtimeObjects), "OBJECT fixture write");
    fclose(volume);
    volume = fopen("words.tok", "wb");
    writeRuntimeFixtureBytes(volume, emptyWords, sizeof(emptyWords), "WORDS fixture write");
    fclose(volume);
    b6InitTimer(b6Timing_proc);
    b6InitFiles();
    bAInitViews();
    bAResetSpriteMemory(TRUE);
    b9LoadViewFile(0);

    check(RAM_BANK == 0x0f, "runtime VIEW load changed its caller RAM bank");
    b6InitRandom();
    bAInitMenus();
    memset(var, 0, 255);
    memset(flag, FALSE, 255);
    flag[5] = TRUE;
    flag[13] = TRUE;
    var[24] = 0x29;
    var[22] = 3;
    var[26] = 3;
    var[8] = 255;
    var[10] = 2;
    b6InitLogics();
	#if defined(AUDIT_CASE_integration)
	b5GetLogicFile(&loadedLogic, 0);
	check(loadedLogic.codeSize == sizeof(runtimeLogic) && loadedLogic.logicCode,
		"generated runtime LOGIC header did not load its complete bytecode");
	memCpyBanked(inventoryBuffer, loadedLogic.logicCode, loadedLogic.codeBank,
		sizeof(runtimeLogic));
	check(!memcmp(inventoryBuffer, runtimeLogic, sizeof(runtimeLogic)),
		"generated runtime LOGIC bytes changed during production loading");
	#endif
    bBInitSound();
    bAInitObjects();
    bDLoadObjectFile();
    b12LoadWords();
    b7InitEvents();
    b6InitInterpreter();
	*(volatile byte*)0x9f26 = 1;
	testResetVsyncMetrics();
    checkVsyncChain();
	check(RAM_BANK == 0x0f && !(testStatus() & 4),
		"runtime graphics setup lost its caller bank or enabled IRQ state");
    bDInitSpriteMemoryManager();
    horizon = 36;
    b1InitControllers();
    bBLoadSoundFile(0);
	check(RAM_BANK == 0x0f, "runtime first SOUND load changed its caller RAM bank");
    bBLoadSoundFile(1);
	check(RAM_BANK == 0x0f, "runtime second SOUND load changed its caller RAM bank");
    flag[9] = TRUE;
}
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM07")
void auditRuntimeCheckRoomPixels(byte room)
{
	byte color = room ? (byte)(room % 15 + 1) : 5;
	byte expected = (byte)(color << 4 | color);
	check(testPeek(b8GetVeraPictureAddress(10, 80)) == expected &&
		testPeek(b8GetVeraPictureAddress(20, 80)) == expected,
        "logic-driven room picture line pixels missing");
}
#pragma code-name (pop)

/* Read one address through its owning bank before inspecting the published sprite. */
#pragma code-name (push, "CODE")
static unsigned long runtimeExpectedSpriteAddress(byte entry)
{
	ViewTable table;
	View view;
	ViewTableMetadata metadata;
	VeraSpriteAddress address;
	unsigned long addressOffset;
	byte metadataEntry;

	getViewTab(&table, entry);
	metadataEntry = table.entryNum;
	memCpyBanked((byte*)&metadata, (byte*)&viewTableMetadata[metadataEntry],
		SPRITE_METADATA_BANK, sizeof(metadata));
	getLoadedView(&view, table.currentView);
	addressOffset = ((unsigned long)table.currentLoop * view.maxCels +
		table.currentCel) * view.maxVeraSlots * sizeof(address);
	memCpyBanked((byte*)&address,
		(byte*)metadata.veraAddresses + addressOffset,
		metadata.viewTableMetadataBank, sizeof(address));
	return address;
}
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM07")
static unsigned long runtimeSpriteAddressAtX(word x, byte* spriteFlags,
	byte* addressLow, byte* addressHigh)
{
	unsigned int slot, address;
	unsigned long attributes;
	for (slot = 0; slot < 128; ++slot) {
		attributes = SPRITE_ATTRIBUTES_START + (unsigned long)slot * 8;
		if (!(testPeek(attributes + 6) & 12) ||
			(testPeek(attributes + 2) | (word)testPeek(attributes + 3) << 8) != x) continue;
		if (spriteFlags) *spriteFlags = testPeek(attributes + 6);
		address = testPeek(attributes) | (word)testPeek(attributes + 1) << 8;
		if (addressLow) *addressLow = (byte)address;
		if (addressHigh) *addressHigh = (byte)(address >> 8);
		return (unsigned long)(address & 0x0fff) << 5;
	}
	return 0;
}

static byte runtimeSpritePixel(unsigned long address, byte x)
{
	byte packed = testPeek(address + (x >> 1));
	return (x & 1) ? packed & 15 : packed >> 4;
}

void auditRuntimeCheckSpritePixels(void)
{
	ViewTable splitTable;
	byte mirrorFlags = 0;
	byte mirrorAddressLow = 0, mirrorAddressHigh = 0;
	unsigned long mirrored = runtimeSpriteAddressAtX(76, &mirrorFlags,
		&mirrorAddressLow, &mirrorAddressHigh);
	unsigned long splitFirst, splitSecond;
	unsigned long expectedAddress = runtimeExpectedSpriteAddress(2);
	unsigned int slot, visible = 0;
	char line[112];
	getViewTab(&splitTable, 3);
	splitFirst = runtimeSpriteAddressAtX((word)splitTable.xPos * 2,
		NULL, NULL, NULL);
	splitSecond = runtimeSpriteAddressAtX(((word)splitTable.xPos + 32) * 2,
		NULL, NULL, NULL);
	for (slot = 0; slot < 128; ++slot) {
		unsigned long attributes = SPRITE_ATTRIBUTES_START + (unsigned long)slot * 8;
		if (testPeek(attributes + 6) & 12)
			++visible;
	}
	sprintf(line, "VISIBLE SPRITES count=%u\n", visible);
	auditOutput(line);
	sprintf(line, "SPRITE PIXELS mirror=%lu alloc=%lu:%u,%u,%u attr=%u:%u,%u split=%lu:%u,%u/%lu:%u\n",
		mirrored, expectedAddress, runtimeSpritePixel(expectedAddress, 0),
		runtimeSpritePixel(expectedAddress, 2), runtimeSpritePixel(expectedAddress, 7),
		mirrorFlags, mirrorAddressHigh,
		mirrorAddressLow, splitFirst, runtimeSpritePixel(splitFirst, 0),
		runtimeSpritePixel(splitFirst, 63), splitSecond, runtimeSpritePixel(splitSecond, 0));
	auditOutput(line);
	auditCheck(visible >= 5, "full runtime workload did not publish five sprite segments");
	auditCheck(mirrored == expectedAddress,
		"published mirrored sprite address differs from its allocated VERA bitmap");
	auditCheck(mirrored && (mirrorFlags & 1) &&
		runtimeSpritePixel(mirrored, 0) == 1 && runtimeSpritePixel(mirrored, 5) == 1 &&
		runtimeSpritePixel(mirrored, 6) == 3 && runtimeSpritePixel(mirrored, 7) == 3,
		"published mirrored sprite lost its literal pixels or H-flip attribute");
	auditCheck(splitFirst && splitSecond && runtimeSpritePixel(splitFirst, 0) == 3 &&
		runtimeSpritePixel(splitFirst, 63) == 3 && runtimeSpritePixel(splitSecond, 0) == 3,
		"published split sprite bitmaps have the wrong literal pixels");
}
#pragma code-name (pop)
#endif

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
extern byte inventoryBuffer[];
static byte attrs[8], mouseAttrs[8];
byte baselineAllocations[ALLOCATION_ARRAY_SIZE], currentAllocations[ALLOCATION_ARRAY_SIZE];
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
    byte i, amount;
    unsigned j;
    for (i=0; i<8; ++i) check(testPeek(0x1fff8UL+i) == attrs[i], "inspect left sprite attributes behind");
    for (j=0; j<TOTAL_REAL_BLOCKS; j += 80) {
        amount = TOTAL_REAL_BLOCKS - j;
        if (amount > 80) amount = 80;
        memCpyBanked(inventoryBuffer, bDSpriteAllocTable+j, SPRITE_MEMORY_MANAGER_BANK, amount);
        for (i=0; i<amount; ++i) check(inventoryBuffer[i] == 0, "inspect bitmap not released");
    }
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
    checkVsyncChain();
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
void auditNumber(unsigned long value) { number(value); }
static void checkVsyncChain(void)
{
    unsigned int observerBefore = testVsyncIrqs;
    volatile unsigned int* counter = (volatile unsigned int*)&vSyncCounter;
    unsigned int gameBefore = *counter;
    unsigned long start = cycles();
    while (testVsyncIrqs == observerBefore && cycles() - start < 3000000UL) {}
    if (testVsyncIrqs != observerBefore && *counter != gameBefore &&
        (*(volatile byte*)0x9f26 & 1)) return;
    output("INVENTORY FAIL: real VSYNC chain did not advance observer and game counters; observer ");
    number(observerBefore); output("->"); number(testVsyncIrqs);
    output("; game "); number(gameBefore); output("->"); number(*counter);
    output("; CPU-I "); number((testStatus() & 4) != 0);
	output("; VERA-IEN "); number(*(volatile byte*)0x9f26);
    output("; VERA-ISR "); number(*(volatile byte*)0x9f27);
    output("; IRQ vector "); number(*(volatile byte*)0x0314);
    output("/"); number(*(volatile byte*)0x0315); output("; ");
    testExit();
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
    static VeraSpriteAddress sprites[TOTAL_REAL_BLOCKS/64];
    byte count = 0, i, area, segment;
    unsigned j, index = 0;
    static const unsigned segmentSizes[] = {TINY_SIZE, EXTRA_SMALL_SIZE, SMALL_SIZE, MEDIUM_SIZE, LARGE_SIZE};
    static const byte segmentCounts[] = {TINY_NO_SEGMENTS, EXTRA_SMALL_NO_SEGMENTS, SMALL_NO_SEGMENTS, MEDIUM_NO_SEGMENTS, LARGE_NO_SEGMENTS};
    static const byte firstBanks[] = {TINY_FIRST_BANK, EXTRA_SMALL_FIRST_BANK, SMALL_FIRST_BANK, MEDIUM_FIRST_BANK, LARGE_FIRST_BANK};
    unsigned long offset;
    for (j=TINY_NO_SEGMENTS+EXTRA_SMALL_NO_SEGMENTS; j<ALLOCATION_ARRAY_SIZE; ++j)
        if (!baselineAllocations[j]) {
            b10BankedAlloc(1368, &area); /* The allocation bitmap records the returned segment. */
        }
    /* The product must stay alive when the disposable panel cannot allocate. */
    testPutKey(KEY_ESC);
    check(inventoryShow(TRUE) == 255 && !inventoryActive, "exhausted save-under did not cancel safely");
    check(cbm_k_getin() == KEY_ESC, "failed inventory consumed queued input");
    memCpyBanked(currentAllocations, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(currentAllocations));
    for (area = 0; area < NO_SIZES; ++area) {
        for (segment = 0; segment < segmentCounts[area]; ++segment, ++index) {
            if (!baselineAllocations[index] && currentAllocations[index]) {
                offset = (unsigned long)segment * segmentSizes[area];
                check(b10BankedDealloc((byte*)(BANK_MIN + offset % MEMORY_MANAGER_BANK_SIZE),
                    firstBanks[area] + offset / MEMORY_MANAGER_BANK_SIZE), "fixture RAM release failed");
            }
        }
    }
    checkBackground();
    count = 0;
    while (count < TOTAL_REAL_BLOCKS/64) {
        sprites[count] = bDFindFreeVramBlock(SPR_SIZE_64, SPR_SIZE_64);
        check(sprites[count] != 0 && sprites[count]+2048 <= VRAM_START+VRAM_SIZE, "sprite allocator overflowed reserved VRAM");
        ++count;
    }
    check(!inventoryInspectOpen(0), "inspect allocated from an exhausted sprite pool");
    testDetailVisibleFrames = 0;
    testPutKey('v'); testPutKey('x'); testPutKey(KEY_ESC);
    check(inventoryShow(FALSE) == 255, "exhausted-VRAM detail view did not close");
    check(testDetailVisibleFrames != 0, "exhausted-VRAM name-only detail row erased before VSYNC");
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
    testVsyncIrqs = 0;
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
    testVisibleFrames = testDetailVisibleFrames = 0;
    testPutKey('v'); testPutKey('x'); testPutKey(KEY_ESC);
    check(inventoryShow(FALSE) == 255, "dagger detail view did not close");
    check(testVisibleFrames >= 2, "dagger art closed before a complete visible frame");
    checkClosed(); checkBackground();
    bDGetObject(0, &object); object.name += 1; bDSetObject(0, &object);
    check(!inventoryInspectOpen(0), "unknown object received unrelated artwork");
    check(!inventoryInspectOpen(255) && !inventoryInspectOpen(3), "invalid object lookup accepted");
    testDetailVisibleFrames = 0;
    testPutKey('v'); testPutKey('x'); testPutKey(KEY_ESC);
    check(inventoryShow(FALSE) == 255, "unknown name-only detail view did not close");
    check(testDetailVisibleFrames != 0, "unknown name-only detail row erased before VSYNC");
    checkClosed(); checkBackground();
    inventoryInspectClose(); checkClosed();
    flag[13] = FALSE;
    testPutKey(KEY_ENTER); testPutKey(KEY_ESC); testPutKey(KEY_ESC);
    b4Status();
    check(var[25] == 255, "read-only inventory selected an object");
    for (i=0; i<3; ++i) { bDGetObject(i, &object); object.roomNum=0; bDSetObject(i, &object); }
    flag[13] = TRUE;
    testPutKey(KEY_ENTER); testPutKey('v'); testPutKey(KEY_ESC);
    testDetailVisibleFrames = 0;
    b4Status();
    check(var[25] == 255 && !inventoryActive, "empty inventory selected an object");
    check(testDetailVisibleFrames == 0, "empty inventory displayed a detail row");
    checkClosed(); checkBackground();
    fullInventoryPerformance();
    nameAndPagingCases();
    allocationFailureCases();
    transferStateCases();
    kq1ObjectCases();
    output("INVENTORY PRELUDE DONE; ");
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
#if defined(AUDIT_CASE_viewaliases)
    viewaliasesAuditCases();
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
#if defined(AUDIT_CASE_interpreter20)
    interpreter20AuditCases();
#endif
#if defined(AUDIT_CASE_blit20)
    blit20AuditCases();
#endif
#if defined(AUDIT_CASE_upload20)
    upload20AuditCases();
#endif
#if defined(AUDIT_CASE_runtime20)
    runtime20AuditCases();
#endif
#if defined(AUDIT_CASE_priority20)
    priority20AuditCases();
#endif
#if defined(AUDIT_CASE_textlayout)
    textlayoutAuditCases();
#endif
#if defined(AUDIT_CASE_scriptstrings)
    scriptstringsAuditCases();
#endif
#if defined(AUDIT_CASE_menustorage)
    menustorageAuditCases();
#endif
#if defined(AUDIT_CASE_scriptstream)
    scriptstreamAuditCases();
#endif
#if defined(AUDIT_CASE_textoverlay)
    textoverlayAuditCases();
#endif
#if defined(AUDIT_CASE_readerdir)
    readerdirAuditCases();
#endif
#if defined(AUDIT_CASE_readerwords)
    readerwordsAuditCases();
#endif
#if defined(AUDIT_CASE_readerobject)
    readerobjectAuditCases();
#endif
#if defined(AUDIT_CASE_readerdebug)
    readerdebugAuditCases();
#endif
#if defined(AUDIT_CASE_readerfilename)
    readerfilenameAuditCases();
#endif
#if defined(AUDIT_CASE_graphicsapi)
    graphicsapiAuditCases();
#endif
#if defined(AUDIT_CASE_backbuffer)
    backbufferAuditCases();
#endif
#if defined(AUDIT_CASE_fullframe)
    fullframeAuditCases();
#endif
#if defined(AUDIT_CASE_integration)
    integrationAuditPrepare();
    integrationAuditCases();
#endif
    output("INVENTORY PASS");
    testExit();
    return 0;
}
