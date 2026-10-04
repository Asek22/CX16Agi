#include "audit.h"
#include "../../textLayer.h"

static unsigned long tileAddress(byte row, byte column)
{
	return MAPBASE + (unsigned long)row * TILE_LAYER_BYTES_PER_ROW + (unsigned long)column * BYTES_PER_CELL;
}

static void oversizedUnbrokenBox(void)
{
	byte bank, i, first = MAX_CHAR_ACROSS, last = 0, count = 0;
	byte* message = b10BankedAlloc(100, &bank);
	char text[61];
	byte boxRow = TEXT_ROWS / 2 - (1 + 2) / 2;
	unsigned long firstRow = tileAddress(FIRST_ROW + boxRow - 1, 0);
	unsigned long spillSentinel = firstRow + 8UL * TILE_LAYER_BYTES_PER_ROW;

	auditCheck(message != 0, "text layout fixture allocation failed");
	for (i = 0; i < 60; ++i) text[i] = 'A';
	text[60] = '\0';
	memCpyBanked(message, (byte*)text, bank, sizeof(text));
	for (i = 0; i < MAX_CHAR_ACROSS; ++i)
		testPoke(SPACE, firstRow + (unsigned long)i * BYTES_PER_CELL);
	testPoke('X', spillSentinel);

	b3DisplayMessageBox((char*)message, bank, AUTO_CALC_ROW, AUTO_CALC_COLUMN,
		TEXTBOX_PALETTE_NUMBER, DEFAULT_BOX_WIDTH, TRUE);

	for (i = 0; i < MAX_CHAR_ACROSS; ++i)
	{
		if (testPeek(firstRow + (unsigned long)i * BYTES_PER_CELL) == TOP_BORDER)
		{
			if (first == MAX_CHAR_ACROSS) first = i;
			last = i;
			count++;
		}
	}
	auditCheck(count >= MAX_CHAR_ACROSS - 2 && count <= MAX_CHAR_ACROSS,
		"oversized unbroken box did not render its top border visibly");
	auditCheck(last - first + 1 == count && first <= 1 && MAX_CHAR_ACROSS - 1 - last <= 1,
		"oversized unbroken box top border was not centered");
	auditCheck(testPeek(spillSentinel) == 'X',
		"oversized unbroken box overwrote an unrelated tile row");
	auditCheck(b10BankedDealloc(message, bank), "text layout fixture release failed");
}

static void smallBoxAndNoBorder(void)
{
	byte bank, i, startLine;
	byte* message = b10BankedAlloc(100, &bank);
	char splitText[41];
	char plainText[] = "plain";
	unsigned long plainAddress = tileAddress(FIRST_ROW + 1, 5);

	auditCheck(message != 0, "text layout boundary fixture allocation failed");
	for (i = 0; i < 16; ++i) splitText[i] = 'A';
	splitText[16] = ' ';
	for (i = 17; i < 40; ++i) splitText[i] = 'B';
	splitText[40] = '\0';
	memCpyBanked(message, (byte*)splitText, bank, sizeof(splitText));
	b3DisplayMessageBox((char*)message, bank, AUTO_CALC_ROW, AUTO_CALC_COLUMN,
		TEXTBOX_PALETTE_NUMBER, 3, TRUE);
	memCpyBanked(&startLine, &b3LastBoxStartLine, TEXT_CODE_BANK, 1);
	auditCheck(startLine == TEXT_ROWS / 2 - (2 + 2) / 2,
		"small nonzero box width underflowed its text wrap width");

	memCpyBanked(message, (byte*)plainText, bank, sizeof(plainText));
	testPoke('X', plainAddress - BYTES_PER_CELL);
	b3DisplayMessageBox((char*)message, bank, 2, 5, TEXTBOX_PALETTE_NUMBER, 0, TRUE);
	auditCheck(testPeek(plainAddress) == 'p' && testPeek(plainAddress + 8) == 'n',
		"no-border text was not displayed at its requested position");
	auditCheck(testPeek(plainAddress - BYTES_PER_CELL) == 'X',
		"no-border text unexpectedly drew a box");
	auditCheck(b10BankedDealloc(message, bank), "text layout boundary fixture release failed");
}

void textlayoutAuditCases(void)
{
	oversizedUnbrokenBox();
	smallBoxAndNoBorder();
	auditOutput("TEXTLAYOUT AUDIT PASS\n");
}
