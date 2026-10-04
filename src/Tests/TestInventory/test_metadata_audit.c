#include "audit.h"
#include "../../view.h"
#include <string.h>

#pragma wrapped-call (push, trampoline, SPRITE_METADATA_BANK)
extern void bEResetViewTableMetadata(void);
extern void bESwitchMetadata(ViewTable* table, View* view, byte viewNum, byte entryNum);
extern void bESetViewMetadata(View* view, ViewTable* table, byte viewNum, byte tableNum, byte slot);
extern void bEResetSpritePointers(void);
extern void bEResetSpritesUpdatedBuffer(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, PICTURE_DATA_BANK)
void metadataBankedCases(void);
#pragma wrapped-call (pop)
extern boolean agiBlit(byte entry, boolean disableInterrupts);
extern void setLoadedLoop(View* view, Loop* loop, byte number);
extern void setLoadedCel(Loop* loop, Cel* cel, byte number);
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern byte bESpritesUpdatedBuffer[];

extern byte allocationArray[];
static byte before[ALLOCATION_ARRAY_SIZE];
static boolean allocationsMatchSnapshot(void)
{
    byte current[ALLOCATION_ARRAY_SIZE];
    memCpyBanked(current, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(current));
    return !memcmp(before, current, sizeof(before));
}

static void wrongBankRelease(void)
{
    ViewTableMetadata owner, cached;
    byte *list, *payload, *unrelated;
    byte listBank, payloadBank, unrelatedBank;
    memset(&owner, 0, sizeof(owner));
    memset(&cached, 0, sizeof(cached));
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    list = b10BankedAlloc(10*sizeof(cached), &listBank);
    payload = b10BankedAlloc(1, &payloadBank);
    unrelated = b10BankedAlloc(4000, &unrelatedBank);
    auditCheck(listBank != payloadBank && payloadBank != unrelatedBank, "metadata fixture must use distinct banks");
    memsetBanked(list, 0, 10*sizeof(cached), listBank);
    cached.loopsVeraAddressesPointers = (VeraSpriteAddress**)payload;
    cached.viewTableMetadataBank = payloadBank;
    cached.inactiveBank = unrelatedBank;
    memCpyBanked(list, (byte*)&cached, listBank, sizeof(cached));
    owner.inactive = list;
    owner.inactiveBank = listBank;
    memCpyBanked((byte*)viewTableMetadata, (byte*)&owner, SPRITE_METADATA_BANK, sizeof(owner));
    bEResetViewTableMetadata();
    auditCheck(b10BankedDealloc(unrelated, unrelatedBank), "metadata cleanup freed unrelated bank allocation");
    auditCheck(allocationsMatchSnapshot(), "metadata cleanup leaked cached payload in its owning bank");
}
static void fullCacheSwitch(void)
{
    ViewTableMetadata owner, cached;
    View view;
    ViewTable table;
    byte *list, *payload;
    byte listBank, payloadBank, i;
    memset(&owner, 0, sizeof(owner));
    memset(&cached, 0, sizeof(cached));
    memset(&view, 0, sizeof(view));
    memset(&table, 0, sizeof(table));
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    list = b10BankedAlloc(10*sizeof(cached), &listBank);
    owner.inactive = list;
    owner.inactiveBank = listBank;
    owner.viewNum = 10;
    owner.loopsVeraAddressesPointers = (VeraSpriteAddress**)b10BankedAlloc(1, &owner.viewTableMetadataBank);
    for (i=0; i<10; ++i) {
        payload = b10BankedAlloc(1, &payloadBank);
        cached.loopsVeraAddressesPointers = (VeraSpriteAddress**)payload;
        cached.viewTableMetadataBank = payloadBank;
        cached.viewNum = i;
        memCpyBanked(list+i*sizeof(cached), (byte*)&cached, listBank, sizeof(cached));
    }
    memCpyBanked((byte*)viewTableMetadata, (byte*)&owner, SPRITE_METADATA_BANK, sizeof(owner));
    view.numberOfLoops = view.maxCels = view.maxVeraSlots = 1;
    auditOutput("FULL METADATA CACHE SWITCH MUST RETURN; ");
    bESwitchMetadata(&table, &view, 11, 0);
    memCpyBanked((byte*)&owner, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(owner));
    auditCheck(owner.viewNum == 11 && owner.inactive && owner.loopsVeraAddressesPointers, "full metadata cache switch lost active ownership");
    bEResetViewTableMetadata();
    auditCheck(allocationsMatchSnapshot(), "full metadata cache switch leaked allocations");
}

static void fullCacheActivePayloadAlias(void)
{
    ViewTableMetadata owner, cached;
    View view;
    ViewTable table;
    byte *list, *activePayload, *targetPayload, *cachedPayload;
    byte listBank, activeBank, targetBank, cachedBank, i, payloadValue, actualValue;
    memset(&owner, 0, sizeof(owner));
    memset(&cached, 0, sizeof(cached));
    memset(&view, 0, sizeof(view));
    memset(&table, 0, sizeof(table));
    bEResetViewTableMetadata();
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));

    list = b10BankedAlloc(10*sizeof(cached), &listBank);
    memsetBanked(list, 0, 10*sizeof(cached), listBank);
    bEResetSpritePointers();
    targetPayload = b10BankedAlloc(1, &targetBank);
    payloadValue = 0x5a;
    memCpyBanked(targetPayload, &payloadValue, targetBank, 1);
    for (i=0; i<9; ++i) {
        cachedPayload = i == 0 ? targetPayload : b10BankedAlloc(1, &cachedBank);
        if (i == 0) cachedBank = targetBank;
        cached.loopsVeraAddressesPointers = (VeraSpriteAddress**)cachedPayload;
        cached.viewTableMetadataBank = cachedBank;
        cached.viewNum = i;
        cached.inactive = list;
        cached.inactiveBank = listBank;
        memCpyBanked(list+i*sizeof(cached), (byte*)&cached, listBank, sizeof(cached));
    }

    activePayload = b10BankedAlloc(1, &activeBank);
    owner.loopsVeraAddressesPointers = (VeraSpriteAddress**)activePayload;
    owner.viewTableMetadataBank = activeBank;
    owner.viewNum = 10;
    owner.inactive = list;
    owner.inactiveBank = listBank;
    memCpyBanked((byte*)viewTableMetadata, (byte*)&owner, SPRITE_METADATA_BANK, sizeof(owner));
    view.numberOfLoops = view.maxCels = view.maxVeraSlots = 1;

    bESwitchMetadata(&table, &view, 0, 0);
    memCpyBanked((byte*)&owner, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(owner));
    auditCheck(owner.viewNum == 0 && owner.loopsVeraAddressesPointers == (VeraSpriteAddress**)targetPayload,
        "metadata cache restore did not alias the active cached payload");
    bESwitchMetadata(&table, &view, 11, 0);
    bESwitchMetadata(&table, &view, 0, 0);
    memCpyBanked((byte*)&owner, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(owner));
    memCpyBanked(&actualValue, targetPayload, targetBank, 1);
    auditCheck(owner.viewNum == 0 && owner.loopsVeraAddressesPointers == (VeraSpriteAddress**)targetPayload && actualValue == payloadValue,
        "full-cache eviction freed the active cached payload before it could be restored");

    bEResetViewTableMetadata();
    auditCheck(allocationsMatchSnapshot(), "active-alias cache cleanup leaked allocations");
}

#pragma code-name (push, "BANKRAM0C")
#pragma rodata-name (push, "BANKRAM0C")
static void makeBlitView(View* view, Loop* loop, Cel* cel, byte viewNum)
{
    byte row, run[2] = {0x18, 0};
    memset(view, 0, sizeof(*view));
    memset(loop, 0, sizeof(*loop));
    memset(cel, 0, sizeof(*cel));
    view->loaded = TRUE;
    view->numberOfLoops = view->maxCels = view->maxVeraSlots = 1;
    view->loops = (Loop*)b10BankedAlloc(sizeof(*loop), &view->loopsBank);
    view->codeBlock = b10BankedAlloc(16, &view->codeBlockBank);
    loop->numberOfCels = 1;
    loop->allocationWidth = loop->allocationHeight = SPR_ATTR_8;
    loop->cels = (Cel*)b10BankedAlloc(sizeof(*cel), &loop->celsBank);
    cel->width = cel->height = 8;
    cel->splitSegments = cel->veraSlotsWidth = cel->veraSlotsHeight = 1;
    cel->bmp = view->codeBlock;
    cel->bitmapBank = view->codeBlockBank;
    for (row=0; row<8; ++row) memCpyBanked(view->codeBlock+row*2, run, view->codeBlockBank, sizeof(run));
    setLoadedCel(loop, cel, 0);
    setLoadedLoop(view, loop, 0);
    setLoadedView(view, viewNum);
}

static void restoredBackBufferState(void)
{
    View first, second;
    Loop firstLoop, secondLoop;
    Cel firstCel, secondCel;
    ViewTable table;
    ViewTableMetadata metadata;
    memset(&table, 0, sizeof(table));
    bEResetViewTableMetadata();
    bEResetSpritePointers();
    bEResetSpritesUpdatedBuffer();
    bDResetSpriteMemoryManager();
    memCpyBanked(before, allocationArray, MEMORY_MANAGEMENT_BANK, sizeof(before));
    makeBlitView(&first, &firstLoop, &firstCel, 244);
    makeBlitView(&second, &secondLoop, &secondCel, 245);
    table.currentView = 244;
    table.currentLoop = table.currentCel = 0;
    table.numberOfLoops = table.numberOfCels = 1;
    table.staleCounter = 1;
    table.xPos = 10;
    table.yPos = 80;
    table.priority = 15;
    setViewTab(&table, 0);
    bESetViewMetadata(&first, &table, 244, 0, 0);

    testDisableInterrupts();
    auditCheck(agiBlit(0, FALSE), "initial metadata fixture blit failed");
    memCpyBanked((byte*)&metadata, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(metadata));
    auditCheck(metadata.isOnBackBuffer, "initial production blit did not establish back-buffer state");

    table.staleCounter = 0;
    table.currentView = 245;
    setViewTab(&table, 0);
    auditCheck(agiBlit(0, FALSE), "second real view blit failed");
    table.currentView = 244;
    setViewTab(&table, 0);
    auditCheck(agiBlit(0, FALSE), "restored real view blit failed");
    memCpyBanked((byte*)&metadata, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(metadata));
    auditCheck(!metadata.isOnBackBuffer, "production blit did not update restored view buffer state");

    table.currentView = 245;
    setViewTab(&table, 0);
    auditCheck(agiBlit(0, FALSE), "switch away from changed view failed");
    table.currentView = 244;
    table.staleCounter = 1;
    setViewTab(&table, 0);
    bEResetSpritesUpdatedBuffer();
    auditCheck(agiBlit(0, FALSE), "restored back-buffer render failed");
    memCpyBanked((byte*)&metadata, (byte*)viewTableMetadata, SPRITE_METADATA_BANK, sizeof(metadata));
    auditCheck(metadata.isOnBackBuffer, "production metadata restore used stale back-buffer state after a blit mutation");

    {
        VeraSpriteAddress backBufferAddress, viewAddress;
        byte publishedAddress[2];
        memCpyBanked((byte*)&backBufferAddress, (byte*)metadata.backBuffers, metadata.viewTableMetadataBank, sizeof(backBufferAddress));
        memCpyBanked((byte*)&viewAddress, (byte*)metadata.veraAddresses, metadata.viewTableMetadataBank, sizeof(viewAddress));
        memCpyBanked(publishedAddress, bESpritesUpdatedBuffer, SPRITE_UPDATED_BANK, sizeof(publishedAddress));
        auditCheck(publishedAddress[0] == (byte)(backBufferAddress >> 5) &&
            publishedAddress[1] == (byte)(backBufferAddress >> 13) &&
            (publishedAddress[0] != (byte)(viewAddress >> 5) || publishedAddress[1] != (byte)(viewAddress >> 13)),
            "restored production blit queued the view buffer address instead of its back buffer address");
        auditCheck(testPeek(backBufferAddress) == 0x11,
            "restored production blit did not upload the cel pixel to its selected back buffer address");
    }
    testEnableInterrupts();

    bEResetViewTableMetadata();
    b9DiscardView(244);
    b9DiscardView(245);
    bDResetSpriteMemoryManager();
    auditCheck(allocationsMatchSnapshot(), "restored buffer-state fixture leaked allocations");
}

void metadataBankedCases(void)
{
    restoredBackBufferState();
}

#pragma rodata-name (pop)
#pragma code-name (pop)

void metadataAuditCases(void)
{
    auditOutput("METADATA LIFECYCLE CASES; ");
    wrongBankRelease();
    fullCacheSwitch();
    fullCacheActivePayloadAlias();
    metadataBankedCases();
}
