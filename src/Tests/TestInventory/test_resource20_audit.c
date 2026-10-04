#include "audit.h"
#include "../../helpers.h"
#include "../../memoryManager.h"
#include "../../view.h"
#include "../../picture.h"
#include "../../agifiles.h"
#include <string.h>
#include <cbm.h>

#pragma wrapped-call (push, trampoline, FILE_LOADER_HELPERS)
extern byte* b6ReadFileContentsIntoBankedRam(int size, byte* bank);
#pragma wrapped-call (pop)
extern byte allocationArray[];

#pragma wrapped-call (push, trampoline, PALETTE_MANAGER_BANK)
static void truncatedResourceCase(void);
static void completeResourceCase(void);
static void pictureLoadReuseCase(void);
static void wideCelCase(byte width);
static void metadataStrideCase(void);
static void allocationOwnershipCase(void);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")

static void truncatedResourceCase(void)
{
    static byte before[ALLOCATION_ARRAY_SIZE], after[ALLOCATION_ARRAY_SIZE];
    FILE* output;
    byte bank = 0xe7, channel;
    byte* block;
    output = fopen("short.res", "wb");
    auditCheck(output != 0, "short resource fixture creation");
    fputs("partial", output); fclose(output);
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    memset(GOLDEN_RAM_WORK_AREA, 0xa5, LOCAL_WORK_AREA_SIZE);
    channel = b6Cbm_openForSeeking("short.res");
    block = b6ReadFileContentsIntoBankedRam(600, &bank);
    cbm_close(channel);
    auditCheck(!block && !bank, "truncated resource copied stale work-area bytes as live payload");
    memCpyBanked(after, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(after));
    auditCheck(!memcmp(before, after, sizeof(before)), "truncated resource leaked its allocation");
    auditCheck(remove("short.res") == 0, "short resource fixture cleanup");
}

static void completeResourceCase(void)
{
    byte source[32], copied[32];
    static byte logic[] = {0x12,0x34,0,6,0, 1,0,0,0,0,0};
    FILE* output;
    byte bank, channel;
    byte* block;
    unsigned i, offset, amount;
    AGIFile loaded;
    AGIFilePosType position;
    for (i=0; i<sizeof(source); ++i) source[i] = (byte)i;
    output = fopen("whole.res", "wb");
    auditCheck(output != 0, "complete resource fixture creation");
    for (offset=0; offset<537; offset+=amount) {
        amount = 537-offset < sizeof(source) ? 537-offset : sizeof(source);
        auditCheck(fwrite(source, 1, amount, output) == amount, "complete resource fixture write");
    }
    fclose(output);
    channel = b6Cbm_openForSeeking("whole.res");
    block = b6ReadFileContentsIntoBankedRam(537, &bank);
    cbm_close(channel);
    auditCheck(block != 0, "complete resource incorrectly rejected");
    for (offset=0; offset<537; offset+=amount) {
        amount = 537-offset < sizeof(source) ? 537-offset : sizeof(source);
        memCpyBanked(copied, block+offset, bank, amount);
        auditCheck(!memcmp(source, copied, amount), "complete multi-chunk resource lost payload bytes");
    }
    auditCheck(b10BankedDealloc(block, bank), "complete resource allocation cleanup");
    bank = 0xe7;
    auditCheck(!b6ReadFileContentsIntoBankedRam(0, &bank) && !bank, "empty resource was accepted");
    bank = 0xe7;
    auditCheck(!b6ReadFileContentsIntoBankedRam(8001, &bank) && !bank, "oversized resource was accepted");
    auditCheck(remove("whole.res") == 0, "complete resource fixture cleanup");
    output = fopen("vol.8", "wb");
    auditCheck(output != 0, "empty message logic fixture creation");
    auditCheck(fwrite(logic, 1, sizeof(logic), output) == sizeof(logic), "empty message logic fixture write");
    fclose(output);
    memset(&position, 0, sizeof(position)); position.fileNum = 8;
    memset(&loaded, 0xe7, sizeof(loaded));
    b6LoadAGIFile(LOGIC, &position, &loaded);
    auditCheck(loaded.code && loaded.codeSize == 1 && !loaded.noMessages && !loaded.messagePointers && !loaded.messageData && !loaded.messageBank, "valid logic with no message payload lost empty-section metadata");
    auditCheck(b10BankedDealloc(loaded.code, loaded.codeBank), "empty message logic allocation cleanup");
    auditCheck(remove("vol.8") == 0, "empty message logic fixture cleanup");
}

static void saveAllocationMap(byte* destination, byte bank)
{
    memCpyBankedBetween(destination, bank, allocationArray, MEMORY_MANAGEMENT_BANK, ALLOCATION_ARRAY_SIZE);
}

static byte allocationMapMatches(byte* expected, byte bank)
{
    unsigned i;
    byte expectedByte;
    memCpyBanked(GOLDEN_RAM_WORK_AREA, allocationArray, MEMORY_MANAGEMENT_BANK, ALLOCATION_ARRAY_SIZE);
    for (i=0; i<ALLOCATION_ARRAY_SIZE; ++i) {
        memCpyBanked(&expectedByte, expected+i, bank, 1);
        if (GOLDEN_RAM_WORK_AREA[i] != expectedByte) return FALSE;
    }
    return TRUE;
}

static void pictureLoadReuseCase(void)
{
    static byte resource[] = {0x12,0x34,0,3,0,0xf0,5,0xff};
    AGIFilePosType position, oldPosition;
    PictureFile first, second, discarded;
    FILE* output;
    byte snapshotBank, firstLoaded, samePicture, sameAllocations, cleaned, freedFirst;
    byte* snapshots;
    output = fopen("vol.8", "wb");
    auditCheck(output != 0, "picture reload fixture creation");
    auditCheck(fwrite(resource, 1, sizeof(resource), output) == sizeof(resource), "picture reload fixture write");
    fclose(output);
    b6DiscardPictureFile(250);
    memCpyBanked((byte*)&oldPosition, (byte*)&picdir[250], DIRECTORY_BANK, sizeof(oldPosition));
    memset(&position, 0, sizeof(position));
    position.fileNum = 8;
    memCpyBanked((byte*)&picdir[250], (byte*)&position, DIRECTORY_BANK, sizeof(position));
    snapshots = b10BankedAlloc(2 * ALLOCATION_ARRAY_SIZE, &snapshotBank);
    auditCheck(snapshots != 0, "picture reload snapshot allocation");
    saveAllocationMap(snapshots, snapshotBank);
    b6LoadPictureFile(250);
    b0CGetLoadedPicture(&first, 250);
    firstLoaded = first.loaded && first.data;
    saveAllocationMap(snapshots + ALLOCATION_ARRAY_SIZE, snapshotBank);
    b6LoadPictureFile(250);
    b0CGetLoadedPicture(&second, 250);
    samePicture = second.data == first.data && second.bank == first.bank;
    sameAllocations = allocationMapMatches(snapshots + ALLOCATION_ARRAY_SIZE, snapshotBank);
    b6DiscardPictureFile(250);
    b0CGetLoadedPicture(&discarded, 250);
    if (samePicture)
        freedFirst = !b10BankedDealloc(first.data, first.bank);
    else
        freedFirst = b10BankedDealloc(first.data, first.bank);
    cleaned = !discarded.loaded && freedFirst && allocationMapMatches(snapshots, snapshotBank);
    memCpyBanked((byte*)&picdir[250], (byte*)&oldPosition, DIRECTORY_BANK, sizeof(oldPosition));
    auditCheck(remove("vol.8") == 0, "picture reload fixture cleanup");
    auditCheck(b10BankedDealloc(snapshots, snapshotBank), "picture reload snapshot cleanup");
    auditCheck(firstLoaded, "picture fixture did not load");
    auditCheck(samePicture, "PICTURE RELOAD REPLACED THE OWNED RESOURCE");
    auditCheck(sameAllocations, "PICTURE RELOAD ALLOCATED A SECOND RESOURCE");
    auditCheck(cleaned, "picture reload fixture allocation cleanup");
}
#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
extern void bEResetViewTableMetadata(void);
#pragma wrapped-call (pop)

static void wideCelCase(byte width)
{
    /* One-loop VIEW resource, with a 128 AGI pixel cel (256 display pixels). */
    static byte bytes[] = {0x12,0x34,0,14,0, 0,0,1,0,0,7,0, 1,3,0,128,1,0,0};
    AGIFilePosType position;
    FILE* output;
    View view;
    Loop loop;
    Cel cel;
    bytes[15] = width;
    output = fopen("vol.8", "wb");
    auditCheck(output != 0, "wide cel fixture creation");
    auditCheck(fwrite(bytes, 1, sizeof(bytes), output) == sizeof(bytes), "wide cel fixture write");
    fclose(output);
    memset(&position, 0, sizeof(position)); position.fileNum = 8;
    memCpyBanked((byte*)&viewdir[249], (byte*)&position, DIRECTORY_BANK, sizeof(position));
    b9LoadViewFile(249);
    getLoadedView(&view, 249);
    getLoadedLoop(&view, &loop, 0);
    getLoadedCel(&loop, &cel, 0);
    auditCheck(cel.width == width && cel.veraSlotsWidth == ((unsigned)width * 2 + 63) / 64, "wide cel display width wrapped to one sprite slot");
    b9DiscardView(249);
    auditCheck(remove("vol.8") == 0, "wide cel fixture cleanup");
}

static void metadataStrideCase(void)
{
    View view;
    ViewTable table;
    ViewTableMetadata metadata;
    VeraSpriteAddress* pointers[2];
    memset(&view, 0, sizeof(view));
    memset(&table, 0, sizeof(table));
    view.numberOfLoops = 2;
    view.maxVeraSlots = 4;
    view.maxCels = 64;
    bEResetViewTableMetadata();
    bESetViewMetadata(&view, &table, 250, 0, 0);
    memCpyBanked((byte*)&metadata, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(metadata));
    memCpyBanked((byte*)pointers, (byte*)metadata.loopsVeraAddressesPointers, metadata.viewTableMetadataBank, sizeof(pointers));
    auditCheck(pointers[1] - pointers[0] == 256, "split cel metadata stride wrapped at 256 addresses");
    auditCheck(metadata.backBuffers - metadata.veraAddresses == 512, "split cel metadata backbuffer overlaps cel addresses");
    bEResetViewTableMetadata();
}

static void allocationOwnershipCase(void)
{
    byte bank, sentinel = 0xe7;
    byte* block = b10BankedAlloc(100, &bank);
    auditCheck(!b10BankedDealloc(block + 1, bank), "interior pointer freed its owning allocation");
    auditCheck(b10BankedDealloc(block, bank), "interior free lost the original allocation");
    auditCheck(!b10BankedDealloc((byte*)0x9fff, bank), "low RAM pointer accepted as banked allocation");
    auditCheck(!b10BankedDealloc((byte*)0xbf40, bank), "unused bank tail accepted as allocation");
    auditCheck(!b10BankedDealloc((byte*)0xa000, LARGE_FIRST_BANK + LARGE_NO_BANKS), "unowned bank accepted as allocation");
    auditCheck(!b10TryBankedAlloc(0, &sentinel) && sentinel == 0xe7, "zero byte allocation consumed a segment");
    auditCheck(!b10TryBankedAlloc(-1, &sentinel) && sentinel == 0xe7, "negative allocation consumed a segment");
}
#pragma rodata-name (pop)
#pragma code-name (pop)

void resource20AuditCases(void)
{
    char formatted[40];
    int length;
    byte callerBank;
    auditOutput("RESOURCE OWNERSHIP CASES; ");
    allocationOwnershipCase();
    callerBank = RAM_BANK;
    length = sprintfBanked(formatted, TINY_FIRST_BANK, "%u:%s:%u:%02x", 0u, "bank", 321u, 0xafu);
    auditCheck(!strcmp(formatted, "0:bank:321:af"), "banked format ignored format string or variadic arguments");
    auditCheck(length == 13, "banked format returned wrong output length");
    auditCheck(RAM_BANK == callerBank, "banked format changed caller bank");
    metadataStrideCase();
    wideCelCase(127);
    wideCelCase(128);
    wideCelCase(160);
    wideCelCase(255);
    truncatedResourceCase();
    completeResourceCase();
    pictureLoadReuseCase();
}
