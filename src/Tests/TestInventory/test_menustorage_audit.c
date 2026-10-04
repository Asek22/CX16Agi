#include "audit.h"
#include "../../logic.h"
#include "../../menu.h"
#include <string.h>

#define MENU_TEST_LOGIC 250
#define MENU_TEST_MESSAGES 19

extern int currentLog;
extern int numOfMenus;
extern LOGICFile logicFiles[];
extern char menuTextBuffer[];
extern char* nextMenuTextBufferAddr;
extern MENU the_menuChildren[];
extern byte bAMenuChildCount[];
extern byte bAMenuChildWidth[];
extern void testDisableInterrupts(void), testEnableInterrupts(void);
extern byte testStatus(void);

#pragma wrapped-call (push, trampoline, MENU_BANK)
extern void bAInitMenuState(void), bAMenuChildInit(void);
extern char* bAStoreMessageInBuffer(LOGICFile* currentLogicFile, byte messageNo);
extern void bASetMenu(byte messageNo);
extern void bASetMenuItem(int messageNo, int controllerNumber);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, 1)
static void menuChildPreservesMaskedIrq(void);
static byte* seedMessages(byte* data, byte bank);
static void messageLengths(LOGICFile* file);
static void fullAndOverflow(LOGICFile* file);
#pragma wrapped-call (pop)

#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")

static char* nextTextPointer(void)
{
    char* next;
    memCpyBanked((byte*)&next, (byte*)&nextMenuTextBufferAddr, MENU_BANK, sizeof(next));
    return next;
}

static void setNextTextPointer(char* next)
{
    memCpyBanked((byte*)&nextMenuTextBufferAddr, (byte*)&next, MENU_BANK, sizeof(next));
}

static void makeMessage(byte* dest, byte bank, unsigned int length, byte fill)
{
    byte terminator = 0;
    memsetBanked(dest, fill, length, bank);
    memCpyBanked(dest + length, &terminator, bank, sizeof(terminator));
}

static byte* seedMessages(byte* data, byte bank)
{
    unsigned int cursor = MENU_TEST_MESSAGES * sizeof(char*);
    unsigned int index, length;
    char* message;

    for (index = 0; index < MENU_TEST_MESSAGES; ++index) {
        if (index == 16) length = 255;
        else if (index == 17) length = 0;
        else if (index == 18) length = 19;
        else length = 31;
        message = (char*)(data + cursor);
        memCpyBanked(data + index * sizeof(message), (byte*)&message, bank, sizeof(message));
        makeMessage((byte*)message, bank, length, 'M');
        cursor += length + 1;
    }
    return data;
}

static void menuChildPreservesMaskedIrq(void)
{
	/* bASetMenu calls this while its own first-menu critical section is active. */
    byte interruptStatus;
    testDisableInterrupts();
    bAMenuChildInit();
    interruptStatus = testStatus();
    testEnableInterrupts();
    auditCheck(interruptStatus & 4, "MENU CHILD INIT CLEARED AN INCOMING IRQ MASK");
}

static void messageLengths(LOGICFile* file)
{
    char* result;
    char* base = menuTextBuffer;
    byte first, last, terminator;

    memsetBanked(menuTextBuffer, 0, MENU_TEXT_BUFFER_SIZE, MENU_BANK);
    setNextTextPointer(base);
    result = bAStoreMessageInBuffer(file, 1);
    auditCheck(result == base && nextTextPointer() == base + 32,
        "NORMAL MENU MESSAGE DID NOT RESERVE ITS TERMINATOR");
    memCpyBanked(&first, (byte*)result, MENU_BANK, 1);
    memCpyBanked(&last, (byte*)result + 30, MENU_BANK, 1);
    memCpyBanked(&terminator, (byte*)result + 31, MENU_BANK, 1);
    auditCheck(first == 'M' && last == 'M' && terminator == 0,
        "NORMAL MENU MESSAGE WAS NOT COPIED COMPLETELY");

    setNextTextPointer(base);
    result = bAStoreMessageInBuffer(file, 18);
    memCpyBanked(&terminator, (byte*)base, MENU_BANK, 1);
    auditCheck(result == base && nextTextPointer() == base + 1 && terminator == 0,
        "EMPTY MENU MESSAGE DID NOT RESERVE ONE TERMINATOR BYTE");

    setNextTextPointer(base);
    result = bAStoreMessageInBuffer(file, 17);
    memCpyBanked(&first, (byte*)result, MENU_BANK, 1);
    memCpyBanked(&last, (byte*)result + 254, MENU_BANK, 1);
    memCpyBanked(&terminator, (byte*)result + 255, MENU_BANK, 1);
    auditCheck(result == base && nextTextPointer() == base + 256,
        "255-CHARACTER MENU MESSAGE LENGTH WRAPPED");
    auditCheck(first == 'M' && last == 'M' && terminator == 0,
        "255-CHARACTER MENU MESSAGE WAS NOT COPIED COMPLETELY");
}

static void fullAndOverflow(LOGICFile* file)
{
    char* base = menuTextBuffer;
    char* result;
    char* canaryAddress;
    MENU child;
    byte canary = 0xa5, value, childCount, terminator, i;

    numOfMenus = 0;
    bAInitMenuState();
    memsetBanked(menuTextBuffer, 0, MENU_TEXT_BUFFER_SIZE, MENU_BANK);
    setNextTextPointer(base);
    bASetMenu(1);
    for (i = 0; i < 14; ++i) bASetMenuItem(i + 2, 0);
    auditCheck(nextTextPointer() == base + 480,
        "MENU TEXT POINTER DID NOT TRACK TITLE AND 14 ITEMS");

    canaryAddress = (char*)((unsigned int)base + MENU_TEXT_BUFFER_SIZE + 1);
    memCpyBanked((byte*)canaryAddress, &canary, MENU_BANK, sizeof(canary));
    bASetMenuItem(16, 0);
    memCpyBanked(&value, (byte*)canaryAddress, MENU_BANK, sizeof(value));
    memCpyBanked(&childCount, bAMenuChildCount, MENU_BANK, sizeof(childCount));
    auditCheck(value == canary, "MENU TEXT STORAGE OVERWROTE THE POST-BUFFER CANARY");
    auditCheck(nextTextPointer() == base + 480 && childCount == 14,
        "MENU PUBLISHED AN ITEM THAT DID NOT FIT");
    for (i = 0; i < childCount; ++i) {
        memCpyBanked((byte*)&child, (byte*)(the_menuChildren + i), MENU_BANK, sizeof(child));
        auditCheck(child.text >= base && child.text < base + 480,
            "MENU PUBLISHED A CHILD WITH A DANGLING TEXT POINTER");
    }

    numOfMenus = 0;
    bAInitMenuState();
    memsetBanked(menuTextBuffer, 0, MENU_TEXT_BUFFER_SIZE, MENU_BANK);
    setNextTextPointer(base);
    bASetMenu(1);
    for (i = 0; i < 14; ++i) bASetMenuItem(i + 2, 0);
    bASetMenuItem(19, 0);
    memCpyBanked((byte*)&child, (byte*)(the_menuChildren + 14), MENU_BANK, sizeof(child));
    memCpyBanked(&terminator, (byte*)base + MENU_TEXT_BUFFER_SIZE - 1, MENU_BANK, sizeof(terminator));
    auditCheck(nextTextPointer() == base + MENU_TEXT_BUFFER_SIZE,
        "MENU TEXT STORAGE DID NOT ACCEPT AN EXACT-FIT ENTRY");
    auditCheck(child.text == base + 480 && terminator == 0,
        "EXACT-FIT MENU ITEM WAS NOT PUBLISHED WITH A TERMINATOR");
    canaryAddress = (char*)((unsigned int)base + MENU_TEXT_BUFFER_SIZE);
    canary = 0x5a;
    memCpyBanked((byte*)canaryAddress, &canary, MENU_BANK, sizeof(canary));
    result = bAStoreMessageInBuffer(file, 18);
    memCpyBanked(&value, (byte*)canaryAddress, MENU_BANK, sizeof(value));
    memCpyBanked(&childCount, bAMenuChildCount, MENU_BANK, sizeof(childCount));
    auditCheck(result == NULL && nextTextPointer() == base + MENU_TEXT_BUFFER_SIZE,
        "FULL MENU TEXT BUFFER ACCEPTED AN EMPTY ENTRY");
    auditCheck(value == canary && childCount == 15,
        "FULL MENU TEXT BUFFER DAMAGED ADJACENT STATE OR VALID CHILDREN");

    numOfMenus = 0;
    bAInitMenuState();
    memsetBanked(menuTextBuffer, 0, MENU_TEXT_BUFFER_SIZE, MENU_BANK);
    setNextTextPointer(base + 300);
    bASetMenu(17);
    bASetMenuItem(2, 0);
    memCpyBanked(&childCount, bAMenuChildCount, MENU_BANK, sizeof(childCount));
    auditCheck(numOfMenus == 0 && nextTextPointer() == base + 300 && childCount == 0,
        "MENU ITEM WAS PUBLISHED AFTER ITS TITLE DID NOT FIT");
}

#pragma rodata-name (pop)
#pragma code-name (pop)

void menustorageAuditCases(void)
{
    LOGICEntry savedEntry, testEntry;
    LOGICFile savedFile, file;
    byte* messageData;
    byte messageBank;
    byte hasSavedFile;
    int savedCurrentLog = currentLog;
    int savedMenuCount = numOfMenus;

    auditOutput("MENU STORAGE AND IRQ CASES; ");
    menuChildPreservesMaskedIrq();

    b5GetLogicEntry(&savedEntry, MENU_TEST_LOGIC);
    hasSavedFile = savedEntry.data != NULL;
    if (hasSavedFile) b5GetLogicFile(&savedFile, MENU_TEST_LOGIC);
    messageData = b10BankedAlloc(1200, &messageBank);
    auditCheck(messageData != NULL, "MENU MESSAGE FIXTURE ALLOCATION FAILED");
    memset(&file, 0, sizeof(file));
    file.messages = (byte**)seedMessages(messageData, messageBank);
    file.messageBank = messageBank;
    file.numMessages = MENU_TEST_MESSAGES;
    memset(&testEntry, 0, sizeof(testEntry));
    testEntry.loaded = TRUE;
    testEntry.data = &logicFiles[MENU_TEST_LOGIC];
    b5SetLogicEntry(&testEntry, MENU_TEST_LOGIC);
    b5SetLogicFile(&file, MENU_TEST_LOGIC);
    currentLog = MENU_TEST_LOGIC;

    messageLengths(&file);
    fullAndOverflow(&file);

    numOfMenus = 0;
    bAInitMenuState();
    memsetBanked(menuTextBuffer, 0, MENU_TEXT_BUFFER_SIZE, MENU_BANK);
    setNextTextPointer(menuTextBuffer);
    currentLog = savedCurrentLog;
    if (hasSavedFile) b5SetLogicFile(&savedFile, MENU_TEST_LOGIC);
    b5SetLogicEntry(&savedEntry, MENU_TEST_LOGIC);
    numOfMenus = savedMenuCount;
    auditCheck(b10BankedDealloc(messageData, messageBank), "MENU MESSAGE FIXTURE LEAKED ITS BANKED BUFFER");
    auditOutput("MENU STORAGE AND IRQ PASS; ");
}
