#include "inventory.h"
#include "object.h"
#include "helpers.h"
#include "spriteAllocator.h"
#include "keyboard.h"
#include "irq.h"
#include <cbm.h>
#include <string.h>
#include "inventoryArt.h"

/* Slot 127 is outside the room renderer's 120 slots. The text panel and
 * bitmap are disposable overlays; neither uses the picture's memory layout. */
#define INSPECT_SLOT 0x1fff8UL
#define PANEL_ROWS 19
#define PANEL_BYTES 72
#define PANEL_START (MAPBASE + 5 * 128 + 2 * 2)
byte inventoryActive;
unsigned long inventoryAddress;
byte inventoryBuffer[80], inventoryLength, inventoryWrite;
extern void inventoryTransfer(void);
extern byte inventoryMouseButtons(void);
extern void inventoryMouseEnable(void);
extern void inventoryMouseDisable(void);
static byte savedMouse[8];
static byte carried[MAX_OBJECTS], carriedCount;
static byte savedAttributes[8], inspectOpen;
static VeraSpriteAddress inspectAddress;
static char objectName[40];

static void transfer(unsigned long address, byte length, byte write)
{
    inventoryAddress = address;
    inventoryLength = length;
    inventoryWrite = write;
    inventoryTransfer();
}
static byte fold(byte c)
{
    c &= 0x7f;
    return c >= 65 && c <= 90 ? c + 32 : c;
}
static boolean named(const char* text)
{
    byte i;
    for (i = 0; text[i]; ++i)
        if (fold(objectName[i]) != fold(text[i])) return FALSE;
    return objectName[i] == 0;
}
static boolean getName(byte id)
{
    int count;
    objectType object;
    byte i;
    memCpyBanked((byte*)&count, (byte*)&bFNumObjects, OBJECT_BANK, sizeof(count));
    objectName[0] = 0;
    if (id == 255 || count < 0 || id >= count) return FALSE;
    bDGetObject(id, &object);
    if (!object.name) return FALSE;
    /* Never dereference a banked OBJECT name while another bank is mapped. */
    if ((unsigned)object.name < BANK_MIN || (unsigned)object.name > BANK_MAX) return FALSE;
    for (i = 0; i < sizeof(objectName) - 1 && (unsigned)(object.name+i) <= BANK_MAX; ++i) {
        memCpyBanked((byte*)&objectName[i], (byte*)object.name+i, OBJECT_BANK, 1);
        if (!objectName[i]) return TRUE;
    }
    objectName[i] = 0;
    return TRUE;
}
boolean inventoryInspectOpen(byte id)
{
    byte art, y, x, packed;
    unsigned shifted;
    if (inspectOpen) return FALSE;
    if (!getName(id)) return FALSE;
    if (named("dagger")) art = 0;
    else if (named("carrot")) art = 1;
    else if (named("bucket")) art = 2;
    else return FALSE;
    inspectAddress = bDFindFreeVramBlock(SPR_SIZE_64, SPR_SIZE_64);
    if (!inspectAddress) return FALSE;
    transfer(INSPECT_SLOT, 8, FALSE);
    memcpy(savedAttributes, inventoryBuffer, 8);
    /* Upload completely before publishing the attribute record. */
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 16; ++x) {
            packed = inventoryArt[art][(unsigned)y*16+x];
            inventoryBuffer[x*2] = (packed & 0xf0) | (packed >> 4);
            inventoryBuffer[x*2+1] = (packed & 15)*17;
        }
        transfer(inspectAddress+(unsigned)y*64, 32, TRUE);
        transfer(inspectAddress+(unsigned)y*64+32, 32, TRUE);
    }
    shifted = (unsigned)(inspectAddress >> 5);
    inventoryBuffer[0] = shifted;
    inventoryBuffer[1] = shifted >> 8;
    inventoryBuffer[2] = 224;
    inventoryBuffer[3] = 0;
    inventoryBuffer[4] = 88;
    inventoryBuffer[5] = 0;
    inventoryBuffer[6] = 12;
    inventoryBuffer[7] = 0xf0;
    transfer(INSPECT_SLOT, 8, TRUE);
    inspectOpen = TRUE;
    return TRUE;
}
static void waitFrame(void)
{
    volatile unsigned* counter = &vSyncCounter;
    unsigned before = *counter;
    while (*counter == before) {}
}
void inventoryInspectClose(void)
{
    if (!inspectOpen) return;
    /* Restore the whole slot before freeing the bitmap: no stale IRQ pointer. */
    memcpy(inventoryBuffer, savedAttributes, 8);
    transfer(INSPECT_SLOT, 8, TRUE);
    /* Let VERA finish any scanline that fetched the previous attributes. */
    waitFrame();
    bDDeleteAllocation(inspectAddress, SPR_SIZE_64, SPR_SIZE_64);
    inspectAddress = 0;
    inspectOpen = FALSE;
}
static void line(byte row, const char* text, byte selected)
{
    byte x;
    for (x = 0; x < 36; ++x) {
        inventoryBuffer[x*2] = *text ? (*text++ & 0x7f) : 32;
        inventoryBuffer[x*2+1] = selected ? 0x20 : 0x10;
    }
    transfer(PANEL_START+(unsigned)row*128, PANEL_BYTES, TRUE);
}
static byte key(void)
{
    byte k;
    static byte buttons;
    byte next = inventoryMouseButtons();
    k = cbm_k_getin();
    if (next && !buttons) k = KEY_ESC;
    buttons = next;
    return k;
}
byte inventoryShow(boolean select)
{
    byte i, row, selected = 0, first, event, result = 255, bank;
    int count;
    objectType object;
    byte* saved;
    if (inventoryActive) return 255;
    saved = b10BankedAlloc(PANEL_ROWS*PANEL_BYTES, &bank);
    if (!saved) return 255;
    inventoryActive = TRUE;
    for (row = 0; row < PANEL_ROWS; ++row) {
        transfer(PANEL_START+(unsigned)row*128, PANEL_BYTES, FALSE);
        memCpyBanked(saved+(unsigned)row*PANEL_BYTES, inventoryBuffer, bank, PANEL_BYTES);
    }
    transfer(SPRITE_ATTRIBUTES_START, 8, FALSE);
    memcpy(savedMouse, inventoryBuffer, 8);
    inventoryMouseEnable();
    carriedCount = 0;
    memCpyBanked((byte*)&count, (byte*)&bFNumObjects, OBJECT_BANK, sizeof(count));
    if (count > MAX_OBJECTS) count = MAX_OBJECTS;
    for (i = 0; i < count; ++i) {
        bDGetObject(i, &object);
        if (object.roomNum == 255 && getName(i) && objectName[0]) carried[carriedCount++] = i;
    }
    for (;;) {
        first = (selected / 14)*14;
        line(0, "INVENTORY", FALSE);
        line(1, "UP/DOWN: SELECT    V: VIEW", FALSE);
        line(2, select ? "ENTER: CHOOSE    ESC: CLOSE" : "ENTER: VIEW    ESC: CLOSE", FALSE);
        for (row = 3; row < PANEL_ROWS; ++row) line(row, "", FALSE);
        if (!carriedCount) line(4, "YOU ARE CARRYING NOTHING.", FALSE);
        for (row = 0; row < 14 && first+row < carriedCount; ++row) {
            getName(carried[first+row]);
            /* Leave the inspect slot clear at the right of the panel. */
            objectName[25] = 0;
            line(row+4, objectName, first+row == selected);
        }
        do { event = key(); } while (!event);
        if (event == KEY_ESC) break;
        if (!carriedCount) continue;
        if (event == KEY_UP && selected) --selected;
        else if (event == KEY_DOWN && selected+1 < carriedCount) ++selected;
        else if (event == KEY_ENTER && select) { result = carried[selected]; break; }
        else if (event == 'v' || event == 'V' || event == KEY_ENTER) {
            if (inventoryInspectOpen(carried[selected])) {
                /* A queued close must still allow one complete visible frame. */
                waitFrame();
                waitFrame();
            }
            getName(carried[selected]);
            line(18, objectName, FALSE);
            do { event = key(); } while (!event);
            inventoryInspectClose();
        }
    }
    inventoryInspectClose();
    for (row = 0; row < PANEL_ROWS; ++row) {
        memCpyBanked(inventoryBuffer, saved+(unsigned)row*PANEL_BYTES, bank, PANEL_BYTES);
        transfer(PANEL_START+(unsigned)row*128, PANEL_BYTES, TRUE);
    }
    inventoryMouseDisable();
    memcpy(inventoryBuffer, savedMouse, 8);
    transfer(SPRITE_ATTRIBUTES_START, 8, TRUE);
    b10BankedDealloc(saved, bank);
    inventoryActive = FALSE;
    return result;
}
