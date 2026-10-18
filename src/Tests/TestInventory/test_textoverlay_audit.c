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
extern byte inventoryActive;
extern byte allocationArray[];
extern byte b3PaletteRows;

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "RODATA")
static const char messageText[] = "PERSISTENT\012OVERLAY";
static const char paletteText[] = "A\012B\012C\012D";
#pragma rodata-name (pop)

static byte allocationBaseline[ALLOCATION_ARRAY_SIZE];
static byte paletteTextCopy[sizeof(paletteText)];
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

	for (row = TEST_MAP_FIRST_ROW - 1; row <= TEST_MAP_LAST_ROW + 1; ++row)
	{
		for (column = 0; column < TILE_LAYER_WIDTH; ++column)
		{
			address = tileMapAddress(row, column);
			testPoke(persistentCharacter(row, column), address);
			testPoke(persistentPalette(row, column), address + 1);
		}
	}
}

static byte persistentTextMatches(void)
{
	byte row, column;
	unsigned long address;

	for (row = TEST_MAP_FIRST_ROW - 1; row <= TEST_MAP_LAST_ROW + 1; ++row)
	{
		for (column = 0; column < TILE_LAYER_WIDTH; ++column)
		{
			address = tileMapAddress(row, column);
			if (testPeek(address) != persistentCharacter(row, column) ||
				testPeek(address + 1) != persistentPalette(row, column)) return FALSE;
		}
	}
	return TRUE;
}

static void checkPersistentText(void)
{
	auditCheck(persistentTextMatches(), "TEMPORARY MESSAGE BOX CHANGED PERSISTENT TEXT OR PALETTE");
}

static void unboxedPaletteSaveUnder(byte* message, byte bank)
{
	byte* savedText = 0;
	byte savedBank = 0, firstRow = 0, rows = 0;
	byte displayed, restored, previousActive = inventoryActive, paletteRows = 0;
	byte lastPalette, nextPalette;

	seedPersistentText();
	inventoryActive = TRUE;
	displayed = b3DisplayMessageBoxWithSaveUnder((char*)message, bank, AUTO_CALC_ROW,
		AUTO_CALC_COLUMN, TEXTBOX_PALETTE_NUMBER, 0, TRUE, &savedText, &savedBank,
		&firstRow, &rows);
	memCpyBanked(&paletteRows, (byte*)&b3PaletteRows, TEXT_CODE_BANK, 1);
	memCpyBanked(paletteTextCopy, (byte*)b3TextBuffer1, TEXT_CODE_BANK, sizeof(paletteTextCopy));
	lastPalette = testPeek(tileMapAddress(TEST_MAP_LAST_ROW, TILE_LAYER_WIDTH - 1) + 1);
	nextPalette = testPeek(tileMapAddress(TEST_MAP_LAST_ROW + 1, 0) + 1);
	auditCheck(!memcmp(paletteTextCopy, paletteText, sizeof(paletteTextCopy)),
		"FOUR-LINE SAVE-UNDER TEXT DID NOT REACH THE PRODUCTION TEXT BUFFER");
	auditCheck(lastPalette == (byte)(TEXTBOX_PALETTE_NUMBER << 4),
		"FOUR-LINE PALETTE WRITE DID NOT REACH THE LAST SAVED ROW");
	auditCheck(nextPalette == persistentPalette(TEST_MAP_LAST_ROW + 1, 0),
		"FOUR-LINE PALETTE WRITE CROSSED ITS EXTRA-ROW BOUNDARY");
	if (displayed && savedText) b3CopyTextRows(savedText, savedBank, firstRow, rows, TRUE);
	if (savedText) auditCheck(b10BankedDealloc(savedText, savedBank),
		"UNBOXED PALETTE SAVE-UNDER ALLOCATION RELEASE FAILED");
	inventoryActive = previousActive;
	restored = persistentTextMatches();
	if (!restored) seedPersistentText();
	auditCheck(displayed, "UNBOXED FOUR-LINE MESSAGE WAS REJECTED");
	auditCheck(paletteRows == 4, "UNBOXED FOUR-LINE PALETTE ROW COUNT WAS LOST");
	auditCheck(firstRow == TEST_MAP_FIRST_ROW, "UNBOXED FOUR-LINE MESSAGE USED THE WRONG SAVE-UNDER ROW");
	auditCheck(rows == 8, "UNBOXED FOUR-LINE PALETTE WRITE DID NOT RESERVE EIGHT ROWS");
	auditCheck(restored, "UNBOXED FOUR-LINE PALETTE WRITE ESCAPED ITS SAVE-UNDER");
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
	byte bank, paletteBank, currentAllocations[ALLOCATION_ARRAY_SIZE];
	byte* message = b10BankedAlloc(sizeof(messageText), &bank);
	byte* paletteMessage = b10BankedAlloc(sizeof(paletteText), &paletteBank);

	auditCheck(message != 0, "TEXT OVERLAY MESSAGE ALLOCATION FAILED");
	auditCheck(paletteMessage != 0, "PALETTE TEXT OVERLAY MESSAGE ALLOCATION FAILED");
	auditCheck(paletteText[1] == NEW_LINE && paletteText[3] == NEW_LINE &&
		paletteText[5] == NEW_LINE && paletteText[7] == '\0',
		"FOUR-LINE SAVE-UNDER FIXTURE DOES NOT CONTAIN THREE NEWLINES");
	memCpyBankedBetween(message, bank, (byte*)messageText, 0x01, sizeof(messageText));
	memCpyBankedBetween(paletteMessage, paletteBank, (byte*)paletteText, 0x01, sizeof(paletteText));
	memCpyBanked(paletteTextCopy, paletteMessage, paletteBank, sizeof(paletteTextCopy));
	auditCheck(!memcmp(paletteTextCopy, paletteText, sizeof(paletteTextCopy)),
		"FOUR-LINE SAVE-UNDER FIXTURE COPY IS INVALID");
	seedPersistentText();
	memCpyBanked(allocationBaseline, allocationArray, MEMORY_MANAGEMENT_BANK,
		ALLOCATION_ARRAY_SIZE);

	showAndClose(message, bank, KEY_ENTER);
	showAndClose(message, bank, KEY_ESC);
	unboxedPaletteSaveUnder(paletteMessage, paletteBank);
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
	auditCheck(b10BankedDealloc(paletteMessage, paletteBank), "PALETTE TEXT OVERLAY MESSAGE RELEASE FAILED");
	auditOutput("TEXT OVERLAY AUDIT PASS\n");
}
#pragma code-name (pop)
