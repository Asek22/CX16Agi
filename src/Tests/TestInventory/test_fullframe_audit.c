#include "audit.h"
#include "../../commands.h"
#include "../../logic.h"
#include "../../sound.h"
#include "../../view.h"
#include <stdio.h>
#include <string.h>

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")

#pragma wrapped-call (push, trampoline, 0x0f)
extern void auditRuntimeFixture(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x07)
extern void auditRuntimeCheckRoomPixels(byte room);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, 0x07)
extern void auditRuntimeCheckSpritePixels(void);
#pragma wrapped-call (pop)
extern void testResetVsyncMetrics(void);
extern volatile unsigned int testVsyncIrqs;
extern volatile unsigned long testVsyncMaxGap, testVsyncMinGap;
extern byte mainLoopCounter;
extern unsigned int vSyncCounter;
extern unsigned int bBCh1Ticks;

extern void b6RunInterpreterTick(void);
static unsigned long fullInterpreterTick(void);
#pragma wrapped-call (push, trampoline, SOUND_BANK)
extern void bBPlaySound(byte soundNum, byte endSoundFlag);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_1)
extern void b9AnimateObjects(void);
#pragma wrapped-call (pop)

#define FULLFRAME_SAMPLES 12
#define FRAME_WAIT_LIMIT 1000000UL
#define IDLE_IRQ_SAMPLES 12
#define FULLFRAME_TICK_BASELINE 24807573UL
#define FULLFRAME_TICK_LIMIT 30000000UL
#define IRQ_GAP_BASELINE_LIMIT 1200000UL

#pragma code-name (push, "CODE")
static unsigned long cycles(void)
{
    byte a = *(volatile byte*)0x9fb8;
    byte b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba;
    byte d = *(volatile byte*)0x9fbb;
    return ((unsigned long)d << 24) | ((unsigned long)c << 16) |
        ((unsigned)b << 8) | a;
}
#pragma code-name (pop)

static void snapshotVsyncMetrics(unsigned int* irqs, unsigned long* minimum,
    unsigned long* maximum)
{
    asm("php");
    asm("sei");
    *irqs = testVsyncIrqs;
    *minimum = testVsyncMinGap;
    *maximum = testVsyncMaxGap;
    asm("plp");
}

#pragma code-name (push, "CODE")
static unsigned long fullInterpreterTick(void)
{
	byte previousBank = RAM_BANK;
	unsigned long start, elapsed;
	RAM_BANK = MEKA_BANK;
	start = cycles();
    b6RunInterpreterTick();
	elapsed = cycles() - start;
	RAM_BANK = previousBank;
	return elapsed;
}
#pragma code-name (pop)

static void waitForFrames(unsigned int count)
{
    unsigned int previous;
    unsigned long start;
    while (count--) {
        previous = vSyncCounter;
        start = cycles();
        while (vSyncCounter == previous) {
            if (cycles() - start > FRAME_WAIT_LIMIT)
                auditCheck(FALSE, "bounded VSYNC wait timed out");
        }
    }
}

static unsigned int soundTicks(void)
{
    unsigned int ticks;
    memCpyBanked((byte*)&ticks, (byte*)&bBCh1Ticks, SOUND_BANK, sizeof(ticks));
    return ticks;
}

static void checkFixtureShapes(void)
{
    View view;
    Loop loop;
    Cel cel;

    getLoadedView(&view, 0);
    auditCheck(view.loaded && view.numberOfLoops == 3,
        "real VIEW fixture did not load three loops");
    getLoadedLoop(&view, &loop, 1);
    getLoadedCel(&loop, &cel, 0);
    auditCheck(cel.flipped, "mirrored VIEW cel did not load as mirrored");
    getLoadedLoop(&view, &loop, 2);
    getLoadedCel(&loop, &cel, 0);
    auditCheck(cel.width == 36 && cel.splitSegments >= 2,
        "wide VIEW cel did not load as a split sprite");
}

static void drawFixturePicture(void)
{
    PictureFile picture;

    b6LoadPictureFile(0);
    b0CGetLoadedPicture(&picture, 0);
    b11DrawPic(picture.data, picture.size, TRUE, 0);
    auditRuntimeCheckRoomPixels(0);
}

static void reportCycles(const char* phase, unsigned int samples,
    unsigned long total, unsigned long worst)
{
    char line[96];
    sprintf(line, "%s samples=%u average=%lu worst=%lu cycles\n",
        phase, samples, total / samples, worst);
    auditOutput(line);
}

void fullframeAuditCases(void)
{
    LOGICEntry entry;
    LOGICFile logicFile;
    ViewTable object;
    byte logicProbe[3];
    unsigned int i, beforeSound, afterSound, irqBefore, irqAfter;
    unsigned int idleIrqs;
    unsigned long elapsed, total = 0, worst = 0;
    unsigned long logicTotal = 0, logicWorst = 0;
    unsigned long animationTotal = 0, animationWorst = 0;
    unsigned long idleMinimumGap, idleMaximumGap, minimumGap, maximumGap;
    byte countBefore;

    auditRuntimeFixture();
    b5GetLogicFile(&logicFile, 0);
    b5GetLogicEntry(&entry, 0);
    memCpyBanked(logicProbe, logicFile.logicCode, logicFile.codeBank,
        sizeof(logicProbe));
    auditCheck(logicFile.codeSize >= sizeof(logicProbe) &&
        logicProbe[0] == 3 && logicProbe[1] == 251 &&
        entry.entryPoint == 0 && entry.currentPoint == 0,
        "runtime logic fixture did not load its command stream");
    countBefore = mainLoopCounter;
    testResetVsyncMetrics();
    checkFixtureShapes();
    drawFixturePicture();
    fullInterpreterTick();
    auditCheck(var[0] == 0 && var[251] == 77,
        "logic-driven frame script did not finish in the production tick");
    getViewTab(&object, 2);
    auditCheck(object.currentView == 0,
        "logic-driven animation bytecode did not select view zero for object two");
    auditCheck(object.currentLoop == 1,
        "logic-driven animation bytecode did not select the mirrored loop");
    auditCheck(object.xPos == 38,
        "logic-driven animation bytecode did not position the mirrored sprite");
    auditCheck((object.flags & (DRAWN | ANIMATED)) == (DRAWN | ANIMATED),
        "logic-driven animation bytecode did not display and animate the mirrored sprite");
    auditRuntimeCheckRoomPixels(0);
    waitForFrames(1);
    auditRuntimeCheckSpritePixels();

    testResetVsyncMetrics();
    waitForFrames(IDLE_IRQ_SAMPLES);
    snapshotVsyncMetrics(&idleIrqs, &idleMinimumGap, &idleMaximumGap);
    auditCheck(idleIrqs >= IDLE_IRQ_SAMPLES && idleMinimumGap >= 10000UL &&
        idleMaximumGap >= idleMinimumGap && idleMaximumGap <= IRQ_GAP_BASELINE_LIMIT,
        "idle VSYNC baseline exceeded its retained service-gap bounds");

    bBPlaySound(0, 60);
    waitForFrames(5);

    testResetVsyncMetrics();
    beforeSound = soundTicks();
    snapshotVsyncMetrics(&irqBefore, &minimumGap, &maximumGap);
    for (i = 0; i < FULLFRAME_SAMPLES; ++i) {
        waitForFrames(6);
        elapsed = fullInterpreterTick();
        total += elapsed;
        if (elapsed > worst) worst = elapsed;
    }
    waitForFrames(6);
    snapshotVsyncMetrics(&irqAfter, &minimumGap, &maximumGap);
    afterSound = soundTicks();

    auditOutput("ACTIVE SOUND CH1 TICKS before=");
    auditNumber(beforeSound);
    auditOutput(" after=");
    auditNumber(afterSound);
    auditOutput(" IRQs=");
    auditNumber(irqAfter - irqBefore);
    auditOutput("\n");

    auditCheck(mainLoopCounter == (byte)(countBefore + FULLFRAME_SAMPLES + 1),
        "full interpreter ticks did not advance the production loop counter");
    auditCheck(irqAfter - irqBefore >= FULLFRAME_SAMPLES * 6,
        "full-frame workload did not observe repeated real VSYNC IRQs");
    auditCheck(afterSound < beforeSound,
        "active SOUND note cursor did not advance through its IRQ path");
    auditCheck(beforeSound - afterSound == irqAfter - irqBefore,
        "active SOUND note cursor did not track the observed IRQ count");
    auditCheck(testPeek(0x1f9c2UL) != 0,
        "full-frame workload did not reach the real PSG sound path");

    for (i = 0; i < FULLFRAME_SAMPLES; ++i) {
        unsigned long phaseStart;
        b5GetLogicEntry(&entry, 0);
        phaseStart = cycles();
        executeLogic(&entry, 0);
        elapsed = cycles() - phaseStart;
        logicTotal += elapsed;
        if (elapsed > logicWorst) logicWorst = elapsed;

        phaseStart = cycles();
        b9AnimateObjects();
        elapsed = cycles() - phaseStart;
        animationTotal += elapsed;
        if (elapsed > animationWorst) animationWorst = elapsed;
        waitForFrames(1);
    }

    auditRuntimeCheckRoomPixels(0);
    auditRuntimeCheckSpritePixels();
    snapshotVsyncMetrics(&irqAfter, &minimumGap, &maximumGap);
    auditCheck(minimumGap >= 10000UL && maximumGap >= minimumGap &&
        maximumGap <= IRQ_GAP_BASELINE_LIMIT,
        "active workload exceeded the retained IRQ service-gap baseline limit");
    auditCheck(worst <= FULLFRAME_TICK_LIMIT,
        "full production tick exceeded its retained cycle ceiling");
    {
        char line[128];
        sprintf(line, "FULL PRODUCTION TICK BASELINE=%lu LIMIT=%lu cycles\n",
            (unsigned long)FULLFRAME_TICK_BASELINE,
            (unsigned long)FULLFRAME_TICK_LIMIT);
        auditOutput(line);
        sprintf(line, "IDLE VSYNC samples=%u minimum-entry-gap=%lu maximum-entry-gap=%lu cycles\n",
            idleIrqs, idleMinimumGap, idleMaximumGap);
        auditOutput(line);
    }
    reportCycles("FULL PRODUCTION TICK", FULLFRAME_SAMPLES, total, worst);
    reportCycles("LOGIC0 PHASE PROBE", FULLFRAME_SAMPLES, logicTotal, logicWorst);
    reportCycles("ANIMATION PHASE PROBE", FULLFRAME_SAMPLES,
        animationTotal, animationWorst);
    {
        const char* candidate = logicTotal / FULLFRAME_SAMPLES >= animationTotal / FULLFRAME_SAMPLES
            ? "interpreter/picture phase" : "sprite animation phase";
        char line[128];
        unsigned long excess = maximumGap - minimumGap;
        sprintf(line, "NEXT ASSEMBLY CANDIDATE: %s; interpreter=%lu animation=%lu average cycles\n",
            candidate, logicTotal / FULLFRAME_SAMPLES, animationTotal / FULLFRAME_SAMPLES);
        auditOutput(line);
        sprintf(line, "ACTIVE SOUND VSYNC samples=%u minimum-entry-gap=%lu maximum-entry-gap=%lu excess=%lu cycles\n",
            irqAfter, minimumGap, maximumGap, excess);
        auditOutput(line);
    }
    auditOutput("FULLFRAME AUDIT PASS\n");
}

#pragma rodata-name (pop)
#pragma code-name (pop)
