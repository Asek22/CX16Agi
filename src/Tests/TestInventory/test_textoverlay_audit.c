#include "audit.h"
#include "../../textLayer.h"
#include "../../keyboard.h"
#include <cbm.h>
#include <string.h>

extern byte __fastcall__ testPeek(unsigned long address);
extern void __fastcall__ testPoke(byte value, unsigned long address);
extern void testPutKey(byte key);
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern byte testStatus(void);
extern byte allocationArray[];

#pragma code-name (push, "BANKRAM01")
static byte allocationBaseline[ALLOCATION_ARRAY_SIZE];
static byte* heldAllocations[ALLOCATION_ARRAY_SIZE];
static byte heldBanks[ALLOCATION_ARRAY_SIZE];
#define TEST_MAP_FIRST_ROW 10
#define TEST_MAP_LAST_ROW 17

static unsigned long tileMapAddress(byte row, byte column)
{
	return MAPBASE + (unsigned long)row * TILE_LAYER_BYTES_PER_ROW + (unsigned long)column * BYTES_PER_CELL;
}

static byte persistentCharacter(byte row, byte column)
{
	return 'A' + (row * 7 + column) % 26;
}

static byte persistentPalette(byte row, byte column)
{
	return ((row + column) & 15) << 4 | (column & 15);
}

static void seedPersistentText(void)
{
	byte row, column;
	unsigned long address;

	for (row = TEST_MAP_FIRST_ROW; row <= TEST_MAP_LAST_ROW; ++row)
	{
		for (column = 0; column < TILE_LAYER_WIDTH; ++column)
		{
			address = tileMapAddress(row, column);
			testPoke(persistentCharacter(row, column), address);
			testPoke(persistentPalette(row, column), address + 1);
		}
	}
}

static void checkPersistentText(void)
{
	byte row, column;
	unsigned long address;

	for (row = TEST_MAP_FIRST_ROW; row <= TEST_MAP_LAST_ROW; ++row)
	{
		for (column = 0; column < TILE_LAYER_WIDTH; ++column)
		{
			address = tileMapAddress(row, column);
			auditCheck(testPeek(address) == persistentCharacter(row, column),
				"TEMPORARY MESSAGE BOX CLEARED PERSISTENT TEXT");
			auditCheck(testPeek(address + 1) == persistentPalette(row, column),
				"TEMPORARY MESSAGE BOX CHANGED PERSISTENT PALETTE");
		}
	}
}

static void showAndClose(byte* message, byte bank, byte closeKey)
{
	byte status = testStatus();
	byte ramBank = RAM_BANK;
	byte result;

	testPutKey('x');
	testPutKey(closeKey);
	result = b3DisplayManuallyWrappedMessageBox((char*)message, bank, AUTO_CALC_ROW,
		AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, 20, 6);
	auditCheck(result == closeKey,
		"TEMPORARY MESSAGE BOX DID NOT CONSUME ITS KERNAL CLOSE KEY");
	auditCheck(RAM_BANK == ramBank, "TEMPORARY MESSAGE BOX CHANGED THE CALLER RAM BANK");
	auditCheck(((testStatus() ^ status) & 4) == 0,
		"TEMPORARY MESSAGE BOX CHANGED THE CALLER IRQ MASK");
	checkPersistentText();
}

static void allocationExhaustion(byte* message, byte bank)
{
	byte count = 0, blockBank, i, previousBank;
	byte* block;

	while (count < ALLOCATION_ARRAY_SIZE)
	{
		block = b10TryBankedAlloc(6 * TILE_LAYER_BYTES_PER_ROW, &blockBank);
		if (!block) break;
		heldAllocations[count] = block;
		heldBanks[count++] = blockBank;
	}
	auditCheck(!b10TryBankedAlloc(6 * TILE_LAYER_BYTES_PER_ROW, &blockBank),
		"TEXT OVERLAY EXHAUSTION FIXTURE LEFT AN ELIGIBLE BANKED SEGMENT");

	testPutKey(KEY_ESC);
	previousBank = RAM_BANK;
	testDisableInterrupts();
	auditCheck(b3DisplayManuallyWrappedMessageBox((char*)message, bank,
		AUTO_CALC_ROW, AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, 20, 6) == 255,
		"EXHAUSTED TEMPORARY MESSAGE BOX DID NOT RETURN WITHOUT DISPLAYING");
	auditCheck(RAM_BANK == previousBank, "FAILED TEMPORARY MESSAGE BOX CHANGED THE CALLER RAM BANK");
	auditCheck(testStatus() & 4, "FAILED TEMPORARY MESSAGE BOX ENABLED INTERRUPTS");
	testEnableInterrupts();
	auditCheck(cbm_k_getin() == KEY_ESC,
		"EXHAUSTED TEMPORARY MESSAGE BOX CONSUMED THE CLOSE KEY");
	checkPersistentText();

	for (i = 0; i < count; ++i)
		auditCheck(b10BankedDealloc(heldAllocations[i], heldBanks[i]),
			"TEXT OVERLAY EXHAUSTION FIXTURE RELEASE FAILED");
}

void textoverlayAuditCases(void)
{
	static const char messageText[] = "PERSISTENT\nOVERLAY";
	byte bank, currentAllocations[ALLOCATION_ARRAY_SIZE];
	byte* message = b10BankedAlloc(sizeof(messageText), &bank);

	auditCheck(message != 0, "TEXT OVERLAY MESSAGE ALLOCATION FAILED");
	memCpyBanked(message, (byte*)messageText, bank, sizeof(messageText));
	seedPersistentText();
	memCpyBanked(allocationBaseline, allocationArray, MEMORY_MANAGEMENT_BANK,
		ALLOCATION_ARRAY_SIZE);

	showAndClose(message, bank, KEY_ENTER);
	showAndClose(message, bank, KEY_ESC);
	memCpyBanked(currentAllocations, allocationArray, MEMORY_MANAGEMENT_BANK,
		ALLOCATION_ARRAY_SIZE);
	auditCheck(!memcmp(allocationBaseline, currentAllocations, sizeof(currentAllocations)),
		"REPEATED TEMPORARY MESSAGE BOXES LEAKED BANKED RAM");

	allocationExhaustion(message, bank);
	memCpyBanked(currentAllocations, allocationArray, MEMORY_MANAGEMENT_BANK,
		ALLOCATION_ARRAY_SIZE);
	auditCheck(!memcmp(allocationBaseline, currentAllocations, sizeof(currentAllocations)),
		"TEMPORARY MESSAGE BOX EXHAUSTION LEAKED BANKED RAM");
	auditCheck(b10BankedDealloc(message, bank), "TEXT OVERLAY MESSAGE RELEASE FAILED");
	auditOutput("TEXT OVERLAY AUDIT PASS\n");
}
#pragma code-name (pop)
