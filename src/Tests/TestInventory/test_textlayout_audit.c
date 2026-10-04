#include "audit.h"
#include "../../textLayer.h"

#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
void textlayoutAuditBankedCases(void);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM01")
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
	unsigned long lastTextCell = firstRow + 2UL * TILE_LAYER_BYTES_PER_ROW + 38UL * BYTES_PER_CELL;
	unsigned long rightSentinel = firstRow + 2UL * TILE_LAYER_BYTES_PER_ROW + 40UL * BYTES_PER_CELL;
	unsigned long spillSentinel = firstRow + 8UL * TILE_LAYER_BYTES_PER_ROW;

	auditCheck(message != 0, "text layout fixture allocation failed");
	for (i = 0; i < 60; ++i) text[i] = 'A';
	text[60] = '\0';
	memCpyBanked(message, (byte*)text, bank, sizeof(text));
	for (i = 0; i < MAX_CHAR_ACROSS; ++i)
		testPoke(SPACE, firstRow + (unsigned long)i * BYTES_PER_CELL);
	testPoke('X', rightSentinel);
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
	auditCheck(testPeek(lastTextCell) == 'A' &&
		testPeek(lastTextCell + BYTES_PER_CELL) == RIGHT_BORDER,
		"full-width box clipped its last visible character");
	auditCheck(testPeek(rightSentinel) == 'X', "full-width box wrote beyond column 39");
	auditCheck(testPeek(spillSentinel) == 'X',
		"oversized unbroken box overwrote an unrelated tile row");
	auditCheck(b10BankedDealloc(message, bank), "text layout fixture release failed");
}

static void smallBoxAndNoBorder(void)
{
	byte bank, i, startLine;
	byte* message = b10BankedAlloc(100, &bank);
	char splitText[41];
	char minimumWidthText[36];
	char plainText[] = "plain";
	unsigned long plainAddress = tileAddress(FIRST_ROW + 1, 5);
	unsigned long minimumAddress = tileAddress(FIRST_ROW + 2, 0);

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
	for (i = 0; i < 11; ++i)
	{
		minimumWidthText[3 * i] = 'A';
		minimumWidthText[3 * i + 1] = 'B';
		minimumWidthText[3 * i + 2] = ' ';
	}
	minimumWidthText[33] = 'A';
	minimumWidthText[34] = 'B';
	minimumWidthText[35] = '\0';
	memCpyBanked(message, (byte*)minimumWidthText, bank, sizeof(minimumWidthText));
	testPoke('X', minimumAddress + 4 * BYTES_PER_CELL);
	b3DisplayMessageBox((char*)message, bank, 1, 0, TEXTBOX_PALETTE_NUMBER, 4, TRUE);
	auditCheck(testPeek(minimumAddress + BYTES_PER_CELL) == 'A' &&
		testPeek(minimumAddress + 2 * BYTES_PER_CELL) == 'B' &&
		testPeek(minimumAddress + 3 * BYTES_PER_CELL) == RIGHT_BORDER,
		"minimum width box did not render its two-character capacity");
	auditCheck(testPeek(minimumAddress + 4 * BYTES_PER_CELL) == 'X',
		"minimum width box rendered beyond its right border");

	memCpyBanked(message, (byte*)plainText, bank, sizeof(plainText));
	testPoke('X', plainAddress - BYTES_PER_CELL);
	b3DisplayMessageBox((char*)message, bank, 2, 5, TEXTBOX_PALETTE_NUMBER, 0, TRUE);
	auditCheck(testPeek(plainAddress) == 'p' && testPeek(plainAddress + 8) == 'n',
		"no-border text was not displayed at its requested position");
	auditCheck(testPeek(plainAddress - BYTES_PER_CELL) == 'X',
		"no-border text unexpectedly drew a box");
	auditCheck(b10BankedDealloc(message, bank), "text layout boundary fixture release failed");
}

static void wrappedLineWidthAndBlankRows(void)
{
	static const char text[] = {
		'1','2','3','4','5','6','7','8','9','0','1','2','3','4','5','6','7',10,10,
		'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q',10,
		'q','r','s','t','u','v','w','x','y','z','A','B','C','D','E','F','G',0
	};
	byte bank;
	byte* message = b10BankedAlloc(sizeof(text), &bank);
	unsigned long lastGlyph = tileAddress(FIRST_ROW + 2, 17);
	unsigned long afterBorder = tileAddress(FIRST_ROW + 2, 19);
	unsigned long blankRow = tileAddress(FIRST_ROW + 3, 0);
	unsigned long nextText = tileAddress(FIRST_ROW + 4, 1);

	auditCheck(message != 0, "wrapped text fixture allocation failed");
	memCpyBanked(message, (byte*)text, bank, sizeof(text));
	testPoke('X', lastGlyph);
	testPoke('X', afterBorder);
	testPoke('X', blankRow + BYTES_PER_CELL);
	testPoke('X', nextText);
	b3DisplayMessageBox((char*)message, bank, 1, 0, TEXTBOX_PALETTE_NUMBER,
		DEFAULT_BOX_WIDTH, TRUE);

	auditCheck(testPeek(lastGlyph) == '7', "boxed wrap clipped the widest line's final character");
	auditCheck(testPeek(tileAddress(FIRST_ROW + 2, 18)) == RIGHT_BORDER,
		"boxed wrap misplaced the widest line's right border");
	auditCheck(testPeek(afterBorder) == 'X', "boxed wrap wrote beyond its right border");
	auditCheck(testPeek(blankRow) == LEFT_BORDER && testPeek(blankRow + BYTES_PER_CELL) == SPACE,
		"boxed wrap dropped an explicit blank line");
	auditCheck(testPeek(nextText) == 'A', "boxed wrap shifted text after a blank line incorrectly");
	auditCheck(b10BankedDealloc(message, bank), "wrapped text fixture release failed");
}

static void leadingAndTrailingBlankRows(void)
{
	static const char text[] = {10,'e','d','g','e',10,0};
	byte bank;
	byte* message = b10BankedAlloc(sizeof(text), &bank);
	unsigned long leadingBlank = tileAddress(FIRST_ROW + 2, 1);
	unsigned long textRow = tileAddress(FIRST_ROW + 3, 1);
	unsigned long trailingBlank = tileAddress(FIRST_ROW + 4, 1);

	auditCheck(message != 0, "blank span fixture allocation failed");
	memCpyBanked(message, (byte*)text, bank, sizeof(text));
	testPoke('X', leadingBlank);
	testPoke('X', textRow);
	testPoke('X', trailingBlank);
	b3DisplayMessageBox((char*)message, bank, 1, 0, TEXTBOX_PALETTE_NUMBER,
		DEFAULT_BOX_WIDTH, FALSE);

	auditCheck(testPeek(leadingBlank) == SPACE, "boxed text dropped a leading blank line");
	auditCheck(testPeek(textRow) == 'e', "boxed text shifted text after a leading blank line");
	auditCheck(testPeek(trailingBlank) == SPACE, "boxed text dropped a trailing blank line");
	auditCheck(b10BankedDealloc(message, bank), "blank span fixture release failed");
}

static void shortUnwrappedMultilineCentering(void)
{
	static const char text[] = {'t','o','p',10,'b','o','t','t','o','m',0};
	byte bank;
	byte* message = b10BankedAlloc(sizeof(text), &bank);
	byte column = MAX_CHAR_ACROSS / 2 - (sizeof(text) + 2) / 2;
	unsigned long expectedTop = tileAddress(FIRST_ROW + 7, column);

	auditCheck(message != 0, "multiline centering fixture allocation failed");
	memCpyBanked(message, (byte*)text, bank, sizeof(text));
	testPoke('X', expectedTop);
	b3DisplayMessageBox((char*)message, bank, AUTO_CALC_ROW, AUTO_CALC_COLUMN,
		TEXTBOX_PALETTE_NUMBER, DEFAULT_BOX_WIDTH, FALSE);

	auditCheck(testPeek(expectedTop) == TOP_BORDER,
		"short unwrapped multiline box was centered as a single line");
	auditCheck(b10BankedDealloc(message, bank), "multiline centering fixture release failed");
}

void textlayoutAuditBankedCases(void)
{
	oversizedUnbrokenBox();
	smallBoxAndNoBorder();
	wrappedLineWidthAndBlankRows();
	leadingAndTrailingBlankRows();
	shortUnwrappedMultilineCentering();
	auditOutput("TEXTLAYOUT AUDIT PASS\n");
}
#pragma code-name (pop)

void textlayoutAuditCases(void)
{
	textlayoutAuditBankedCases();
}
