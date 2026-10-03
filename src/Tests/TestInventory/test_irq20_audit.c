#include "audit.h"
#include "../../sound.h"
#include "../../controllers.h"
#include "../../textLayer.h"
#include "../../parser.h"
#include "../../menu.h"

extern boolean bBIsPlaying[4];
extern byte bBEndSoundFlag, bBChannelsPlaying;
extern char b7CurrentInputStr[];
extern void testDisableInterrupts(void), testEnableInterrupts(void);

static void stoppedSound(void)
{
    byte playing[4] = {1,1,1,1}, endFlag = 40, count = 4;
    memCpyBanked((byte*)bBIsPlaying, playing, SOUND_BANK, 4);
    memCpyBanked(&bBEndSoundFlag, &endFlag, SOUND_BANK, 1);
    memCpyBanked(&bBChannelsPlaying, &count, SOUND_BANK, 1);
    soundEndFlag = 41;
    flag[40] = flag[41] = FALSE;
    bBStopSound();
    auditCheck(flag[40], "explicit stop did not set the active sound completion flag");
    auditCheck(!flag[41], "explicit stop set an unrelated sound completion flag");
    memCpyBanked(&count, &bBChannelsPlaying, SOUND_BANK, 1);
    auditCheck(count == 0, "explicit stop retained a playing channel count");
}

static byte interruptStatus;
static void disabledSound(void)
{
    byte previousBank = RAM_BANK;
    flag[9] = FALSE;
    flag[42] = FALSE;
    testEnableInterrupts();
    bBPlaySound(255, 42);
    asm("php");
    asm("pla");
    asm("sta %v", interruptStatus);
    testEnableInterrupts();
    auditCheck(!(interruptStatus & 4), "disabled sound left IRQs masked on early return");
    auditCheck(flag[42], "disabled sound did not complete its requested flag");
    auditCheck(RAM_BANK == previousBank, "disabled sound changed the caller bank");
}

#define INPUT_ADDRESS (MAPBASE + 25UL * 128)
static void boundedInput(void)
{
    char line[41];
    unsigned int start;
    byte i;
    memset(line, 'A', 40);
    line[40] = 0;
    testDisableInterrupts();
    inputLineDisplayed = TRUE;
    memCpyBanked((byte*)b7CurrentInputStr, (byte*)line, PARSER_BANK, sizeof(line));
    testPoke(66, INPUT_ADDRESS + 80UL);
    testPoke(67, INPUT_ADDRESS + 82UL);
    start = vSyncCounter;
    testEnableInterrupts();
    while (vSyncCounter == start) { }
    auditCheck(testPeek(INPUT_ADDRESS + 80UL) == 66, "maximum input string spilled beyond the visible input row");
    auditCheck(testPeek(INPUT_ADDRESS + 82UL) == 67, "maximum input cursor spilled beyond the visible input row");
    for (i = 1; i < 40; ++i)
        auditCheck(testPeek(INPUT_ADDRESS + (unsigned long)i * 2) == 'A', "bounded input lost an in-range character");
    inputLineDisplayed = FALSE;
}

extern byte b1PetciiToControllers[];
extern byte b1ControllerBits[];
static void invalidControllerKey(void)
{
    byte original, fakeController = 17, bits;
    boolean accepted;
    b1InitControllers();
    /* A high PETSCII key must not consult the neighboring bank data. */
    memCpyBanked(&original, b1PetciiToControllers + LARGEST_PETSCII + 1, CONTROLLER_BANK, 1);
    memCpyBanked(b1PetciiToControllers + LARGEST_PETSCII + 1, &fakeController, CONTROLLER_BANK, 1);
    accepted = b1SetControllerByPetscii(LARGEST_PETSCII + 1);
    memCpyBanked(b1PetciiToControllers + LARGEST_PETSCII + 1, &original, CONTROLLER_BANK, 1);
    auditCheck(!accepted, "out-of-range PETSCII key read neighboring controller data");
    memCpyBanked(&bits, b1ControllerBits + 2, CONTROLLER_BANK, 1);
    auditCheck(!(bits & 2), "out-of-range PETSCII key activated an unrelated controller");
}

#pragma wrapped-call (push, trampoline, TEXT_CODE_BANK)
extern byte b3WrapText(char* text, int width, byte* maxWidth);
#pragma wrapped-call (pop)
static void explicitNewlines(void)
{
    static const char expected[] = {111,110,101,32,120,10,116,119,111,32,120,10,116,104,114,101,101,0};
    static char text[sizeof(expected)];
    byte width, lines;
    memcpy(text, expected, sizeof(expected));
    lines = b3WrapText(text, 5, &width);
    auditCheck(lines == 3, "explicit newline caused a spurious text wrap");
    auditCheck(!strcmp(text, expected), "explicit newline altered an already fitting line");
    auditCheck(width == 5, "text wrap ignored final or explicit line width");
}

void auditIrq20(void)
{
    stoppedSound();
    disabledSound();
    boundedInput();
    invalidControllerKey();
    explicitNewlines();
    auditOutput("IRQ20 AUDIT PASS\n");
}
