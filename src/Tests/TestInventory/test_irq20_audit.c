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

void auditIrq20(void)
{
    stoppedSound();
    auditOutput("IRQ20 AUDIT PASS\n");
}
