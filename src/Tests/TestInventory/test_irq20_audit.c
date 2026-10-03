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

void auditIrq20(void)
{
    stoppedSound();
    disabledSound();
    boundedInput();
    auditOutput("IRQ20 AUDIT PASS\n");
}
