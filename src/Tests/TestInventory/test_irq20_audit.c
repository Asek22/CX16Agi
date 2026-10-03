#include "audit.h"
#include "../../sound.h"
#include "../../controllers.h"
#include "../../textLayer.h"
#include "../../parser.h"
#include "../../menu.h"

extern boolean bBIsPlaying[4];
extern SoundFile* bBLoadedSoundsPointer[];
extern byte soundLoadCounter;
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

static void oversizedText(void)
{
    byte bank, value, originalGuard, sourceValue, guardValue;
    byte block[100];
    unsigned int offset;
    byte* source = b10BankedAlloc(2200, &bank);
    memset(block, 'A', sizeof(block));
    for (offset = 0; offset < 2100; offset += sizeof(block))
        memCpyBanked(source + offset, block, bank, sizeof(block));
    value = 0;
    memCpyBanked(source + 2100, &value, bank, 1);
    memCpyBanked(&originalGuard, (byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, TEXT_CODE_BANK, 1);
    value = 0x5a;
    memCpyBanked((byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, &value, TEXT_CODE_BANK, 1);
    b3DisplayMessageBox((char*)source, bank, 1, 0, 1, 0, FALSE);
    memCpyBanked(&sourceValue, source + TEXTBUFFER_SIZE * 2 - 1, bank, 1);
    memCpyBanked(&guardValue, (byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, TEXT_CODE_BANK, 1);
    memCpyBanked((byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, &originalGuard, TEXT_CODE_BANK, 1);
    b10BankedDealloc(source, bank);
    auditCheck(sourceValue == 'A', "long text display modified its banked source resource");
    auditCheck(guardValue == 0x5a, "long text display overflowed both text buffers");
}

static void activeSoundDiscard(void)
{
    byte bank, count = 1, i, playing[4], completion;
    byte note[] = {0xe8,3,0,4,30,255,255};
    unsigned int start;
    SoundFile sound, *entry = bBLoadedSounds;
    SoundFile* lookup;
    byte* retired;
    bBInitSound();
    /* Discarding an unloaded ID is also an ordinary idempotent operation. */
    bBDiscardSoundFile(255);
    sound.soundResource = b10BankedAlloc(100, &bank);
    sound.ch0 = sound.ch1 = sound.ch2 = sound.chNoise = sound.soundResource;
    sound.soundBank = bank;
    sound.isLogicZeroOrDependency = FALSE;
    retired = sound.soundResource;
    memCpyBanked(retired, note, bank, sizeof(note));
    memCpyBanked((byte*)entry, (byte*)&sound, SOUND_BANK, sizeof(sound));
    memCpyBanked((byte*)(bBLoadedSoundsPointer + 7), (byte*)&entry, SOUND_BANK, sizeof(entry));
    memCpyBanked(&soundLoadCounter, &count, SOUND_BANK, 1);
    flag[9] = TRUE;
    start = vSyncCounter;
    while (vSyncCounter == start) { }
    bBPlaySound(7, 43);
    start = vSyncCounter;
    while (vSyncCounter == start) { }
    auditCheck(testPeek(0x1f9c2UL) != 0, "seeded sound never reached the actual PSG IRQ path");
    bBDiscardSoundFile(7);
    testDisableInterrupts();
    memCpyBanked(playing, (byte*)bBIsPlaying, SOUND_BANK, sizeof(playing));
    memCpyBanked((byte*)&lookup, (byte*)(bBLoadedSoundsPointer + 7), SOUND_BANK, sizeof(lookup));
    completion = flag[43];
    testEnableInterrupts();
    for (i = 0; i < 4; ++i)
        auditCheck(!playing[i], "discard freed a resource while its IRQ channels were still playing");
    auditCheck(completion, "discard did not complete the active sound flag");
    auditCheck(lookup == NULL, "discard retained the loaded sound table pointer");
    sound.soundResource = b10BankedAlloc(100, &bank);
    auditCheck(sound.soundResource == retired, "discard did not release its banked sound allocation");
    memset(note, 0xff, sizeof(note));
    memCpyBanked(sound.soundResource, note, bank, sizeof(note));
    start = vSyncCounter;
    while (vSyncCounter == start) { }
    for (i = 0; i < 4; ++i)
        auditCheck(testPeek(0x1f9c2UL + (unsigned long)i * 4) == 0, "IRQ reactivated PSG after retired sound buffer reuse");
    b10BankedDealloc(sound.soundResource, bank);
    bBDiscardSoundFile(7);
    flag[9] = FALSE;
}

static void boxedTextBounds(void)
{
    byte bank, value, originalGuard, guard;
    byte block[100];
    unsigned int offset;
    byte* source = b10BankedAlloc(1000, &bank);
    memset(block, 'A', sizeof(block));
    for (offset = 0; offset < 1000; offset += sizeof(block))
        memCpyBanked(source + offset, block, bank, sizeof(block));
    value = 0;
    memCpyBanked(source + 999, &value, bank, 1);
    memCpyBanked(&originalGuard, (byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, TEXT_CODE_BANK, 1);
    value = 0x5a;
    memCpyBanked((byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, &value, TEXT_CODE_BANK, 1);
    b3DisplayMessageBox((char*)source, bank, 1, 0, 1, DEFAULT_BOX_WIDTH, FALSE);
    memCpyBanked(&guard, (byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, TEXT_CODE_BANK, 1);
    memCpyBanked((byte*)b3TextBuffer2 + TEXTBUFFER_SIZE, &originalGuard, TEXT_CODE_BANK, 1);
    b10BankedDealloc(source, bank);
    auditCheck(guard == 0x5a, "boxed text border expansion overflowed its destination");
}

void auditIrq20(void)
{
    stoppedSound();
    disabledSound();
    boundedInput();
    invalidControllerKey();
    explicitNewlines();
    oversizedText();
    activeSoundDiscard();
    boxedTextBounds();
    auditOutput("IRQ20 AUDIT PASS\n");
}
