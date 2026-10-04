#include "audit.h"
#include "../../commands.h"
#include "../../logic.h"
#include "../../memoryManager.h"
#include "../../parser.h"
#include "../../words.h"
#include "../../inventory.h"
#include "../../keyboard.h"
#include <string.h>

#define TEST_LOGIC_NUMBER 250
#define OP_SET_STRING 114
#define OP_GET_STRING 115
#define OP_WORD_TO_STRING 116
#define OP_PARSE 117
#define OP_GET_NUM 118
#define OP_SET_GAME_ID 143
#define ALLOCATION_RECORD_SIZE (sizeof(byte*) + sizeof(byte))

extern LOGICEntry logics[];
extern LOGICFile logicFiles[];
extern char b7CurrentInputStr[41], strPos;

static LOGICEntry savedEntry;
static LOGICFile savedFile, testFile;
static byte* scriptData;
static byte scriptBank;
static byte* messageData;
static byte messageBank;

extern void testPutKey(byte key);
extern void __fastcall__ testFeedKeys(const byte* keys);

#pragma wrapped-call (push, trampoline, CONTROLLER_BANK)
extern void scriptstringsAuditBankedCases(void);
#pragma wrapped-call (pop)

/* Keep this audit's code and bytecode literals out of the low-memory test image. */
#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")

static void writeParserString(byte slot, const char* value)
{
    char buffer[40];
    byte sourceBank = RAM_BANK;
    size_t length = strlen(value);
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memcpy(buffer, value, length);
    buffer[length] = 0;
    memCpyBankedBetween((byte*)string[slot], PARSER_BANK, (byte*)buffer, sourceBank, sizeof(buffer));
}

static void writeParserWord(byte slot, const char* value)
{
    char buffer[80];
    byte sourceBank = RAM_BANK;
    size_t length = strlen(value);
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, value, length);
    memCpyBankedBetween((byte*)b7WordText[slot], PARSER_BANK,
        (byte*)buffer, sourceBank, sizeof(buffer));
}

static void initScriptFixture(void)
{
    LOGICEntry entry;

    b5GetLogicEntry(&savedEntry, TEST_LOGIC_NUMBER);
    b5GetLogicFile(&savedFile, TEST_LOGIC_NUMBER);
    scriptData = b10BankedAlloc(32, &scriptBank);
    messageData = b10BankedAlloc(80, &messageBank);
    memset(&testFile, 0, sizeof(testFile));
    testFile.logicCode = scriptData;
    testFile.codeBank = scriptBank;

    memset(&entry, 0, sizeof(entry));
    entry.loaded = TRUE;
    entry.data = &logicFiles[TEST_LOGIC_NUMBER];
    b5SetLogicEntry(&entry, TEST_LOGIC_NUMBER);
    b5SetLogicFile(&testFile, TEST_LOGIC_NUMBER);
}

static void writePromptMessage(const char* value)
{
    char* message = (char*)(messageData + sizeof(char*));
    char buffer[64];
    byte sourceBank = RAM_BANK;
    size_t length = strlen(value);

    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, value, length);
    memCpyBankedBetween(messageData, messageBank, (byte*)&message, sourceBank, sizeof(message));
    memCpyBankedBetween((byte*)message, messageBank, (byte*)buffer, sourceBank, sizeof(buffer));
    testFile.messages = (byte**)messageData;
    testFile.numMessages = 1;
    testFile.messageBank = messageBank;
    b5SetLogicFile(&testFile, TEST_LOGIC_NUMBER);
}

static void runScript(const byte* code, byte length)
{
    memCpyBankedBetween(scriptData, scriptBank, (byte*)code, RAM_BANK, length);
    testFile.codeSize = length;
    b5SetLogicFile(&testFile, TEST_LOGIC_NUMBER);
    executeLogic(&logics[TEST_LOGIC_NUMBER], TEST_LOGIC_NUMBER);
}

static void parseUsesItsOperand(void)
{
    /* Seed the previous slot local with the opposite value to the PARSE operand. */
    static const byte parseOne[] = {OP_WORD_TO_STRING, 0, 0, OP_PARSE, 1, 0};
    static const byte parseZero[] = {OP_WORD_TO_STRING, 1, 0, OP_PARSE, 0, 0};
    int noWords = 0;
    size_t length;
    char copied[40];
    char* stored;

    writeParserString(0, "");
    writeParserString(1, "unlisted");
    writeParserWord(0, "");
    stored = b7GetInternalStringPtr(1, &length);
    memCpyBanked((byte*)copied, (byte*)stored, PARSER_BANK, sizeof(copied));
    auditCheck(length == 8 && !strcmp(copied, "unlisted"), "STRING SLOT 1 FIXTURE WAS NOT WRITTEN TO PARSER BANK");
    memCpyBanked((byte*)&numWords, (byte*)&noWords, WORD_BANK, sizeof(noWords));
    var[9] = 0;
    auditOutput("PARSE MUST USE EACH BYTECODE STRING OPERAND; ");
    runScript(parseOne, sizeof(parseOne));
    auditCheck(var[9] == 1, "PARSE DID NOT USE STRING SLOT 1");
    var[9] = 55;
    writeParserWord(0, "unlisted");
    runScript(parseZero, sizeof(parseZero));
    auditCheck(var[9] == 55, "PARSE DID NOT USE STRING SLOT 0");
}

static void getStringRejectsInvalidDestination(void)
{
    static const byte invalidDestination[] = {
        OP_GET_STRING, 12, 1, 1, 1, 1,
        3, 251, 77, 0
    };
    char guard = 'G';
    char observed;
    int keyCode;

    writePromptMessage("");
    memCpyBankedBetween((byte*)string + sizeof(string), PARSER_BANK,
        (byte*)&guard, RAM_BANK, sizeof(guard));
    var[251] = 0;
    testPutKey('Q');
    testPutKey(KEY_ENTER);
    auditOutput("GET.STRING MUST REJECT SLOT 12 WITHOUT WRITING PAST THE STRING TABLE; ");
    runScript(invalidDestination, sizeof(invalidDestination));
    memCpyBanked((byte*)&observed, (byte*)string + sizeof(string), PARSER_BANK, sizeof(observed));
    auditCheck(observed == guard, "GET.STRING SLOT 12 WROTE PAST THE INTERNAL STRING TABLE");
    auditCheck(var[251] == 77, "GET.STRING SLOT 12 DID NOT CONSUME ITS OPERANDS");
    GET_IN(keyCode);
    auditCheck((keyCode & 0xff) == 'Q', "GET.STRING INVALID DESTINATION CONSUMED INPUT");
    GET_IN(keyCode);
    auditCheck((keyCode & 0xff) == KEY_ENTER, "GET.STRING INVALID DESTINATION LOST ENTER");
}

static void resetStringInput(void)
{
    char empty[41];
    memset(empty, 0, sizeof(empty));
    strPos = 0;
    memCpyBankedBetween((byte*)b7CurrentInputStr, PARSER_BANK,
        (byte*)empty, RAM_BANK, sizeof(empty));
}

static void getStringWaitsForInput(void)
{
    static const byte script[] = {
        OP_GET_STRING, 0, 1, 1, 1, 10,
        3, 252, 66, 0
    };
    static byte keys[] = {'A', 'B', KEY_ENTER, 0};
    char observed[40];
    boolean wasInventoryActive = inventoryActive;

    writePromptMessage("");
    writeParserString(0, "");
    resetStringInput();
    var[252] = 0;
    inventoryActive = TRUE;
    testFeedKeys(keys);
    runScript(script, sizeof(script));
    inventoryActive = wasInventoryActive;
    memCpyBanked((byte*)observed, (byte*)string[0], PARSER_BANK, sizeof(observed));
    auditCheck(!strcmp(observed, "AB"), "GET.STRING RETURNED BEFORE THE KERNAL INPUT WAS COMPLETE");
    auditCheck(var[252] == 66, "GET.STRING DID NOT EXECUTE THE FOLLOWING OPCODE");
}

static void getStringStartsFreshForEachPrompt(void)
{
    static const byte script[] = {
        OP_GET_STRING, 0, 1, 1, 1, 10,
        OP_GET_STRING, 1, 1, 1, 1, 10,
        3, 252, 68, 0
    };
    static byte keys[5];
    char observed[40];
    boolean wasInventoryActive = inventoryActive;

    keys[0] = 'A'; keys[1] = KEY_ENTER; keys[2] = 'B';
    keys[3] = KEY_ENTER; keys[4] = 0;
    writePromptMessage("");
    writeParserString(0, "");
    writeParserString(1, "");
    resetStringInput();
    var[252] = 0;
    inventoryActive = TRUE;
    testFeedKeys(keys);
    runScript(script, sizeof(script));
    inventoryActive = wasInventoryActive;
    memCpyBanked((byte*)observed, (byte*)string[0], PARSER_BANK, sizeof(observed));
    auditCheck(!strcmp(observed, "A"), "FIRST GET.STRING PROMPT LOST ITS INPUT");
    memCpyBanked((byte*)observed, (byte*)string[1], PARSER_BANK, sizeof(observed));
    auditCheck(!strcmp(observed, "B"), "SECOND GET.STRING PROMPT KEPT STALE INPUT");
    auditCheck(var[252] == 68, "REPEATED GET.STRING DID NOT EXECUTE THE FOLLOWING OPCODE");
}

static void getStringClampsToInternalRow(void)
{
    static const byte script[] = {
        OP_GET_STRING, 0, 1, 1, 1, 255,
        3, 253, 67, 0
    };
    static byte keys[43];
    char observed[40];
    char expected[40];
    size_t length;
    byte i;
    boolean wasInventoryActive = inventoryActive;

    for (i = 0; i < 41; ++i) keys[i] = 'A';
    keys[41] = KEY_ENTER;
    keys[42] = 0;
    writePromptMessage("");
    writeParserString(0, "");
    writeParserString(1, "sentinel");
    resetStringInput();
    var[253] = 0;
    inventoryActive = TRUE;
    testFeedKeys(keys);
    auditOutput("GET.STRING LENGTH 255 MUST FIT THE 40-BYTE STRING ROW; ");
    runScript(script, sizeof(script));
    inventoryActive = wasInventoryActive;

    memset(expected, 'A', sizeof(expected) - 1);
    expected[sizeof(expected) - 1] = 0;
    {
        char* value = b7GetInternalStringPtr(0, &length);
        memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    }
    auditCheck(length == sizeof(expected) - 1 && !strcmp(observed, expected),
        "GET.STRING ACCEPTED INPUT PAST ITS 40-BYTE STRING ROW");
    {
        char* value = b7GetInternalStringPtr(1, &length);
        memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    }
    auditCheck(!strcmp(observed, "sentinel"), "GET.STRING OVERWROTE THE FOLLOWING STRING ROW");
    auditCheck(var[253] == 67, "GET.STRING DID NOT EXECUTE THE FOLLOWING OPCODE");
}

static void wordToStringCopiesWord(void)
{
    static const byte script[] = {OP_WORD_TO_STRING, 1, 0, 0};
    char observed[40];
    size_t length;
    char* value;

    writeParserWord(0, "Echo");
    writeParserString(1, "stale");
    auditOutput("WORD.TO.STRING MUST COPY WORD 0 INTO STRING 1; ");
    runScript(script, sizeof(script));
    value = b7GetInternalStringPtr(1, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == 4 && !strcmp(observed, "Echo"),
        "WORD.TO.STRING DID NOT COPY THE REQUESTED WORD INTO ITS STRING");
}

static void wordToStringRejectsOutOfRangeSlots(void)
{
    static const byte invalidString[] = {
        OP_WORD_TO_STRING, 12, 0,
        3, 254, 78, 0
    };
    static const byte invalidWord[] = {
        OP_WORD_TO_STRING, 1, 10,
        3, 254, 79, 0
    };
    char observed[40];
    size_t length;
    char* value;

    writeParserWord(0, "Echo");
    resetStringInput();
    var[254] = 0;
    runScript(invalidString, sizeof(invalidString));
    memCpyBanked((byte*)observed, (byte*)b7CurrentInputStr, PARSER_BANK, sizeof(observed));
    auditCheck(observed[0] == 0, "WORD.TO.STRING SLOT 12 WROTE PAST THE STRING TABLE");
    auditCheck(var[254] == 78, "WORD.TO.STRING SLOT 12 DID NOT CONSUME ITS OPERANDS");

    writeParserString(0, "bad");
    writeParserString(1, "keep");
    auditOutput("WORD.TO.STRING MUST REJECT WORD SLOT 10; ");
    runScript(invalidWord, sizeof(invalidWord));
    value = b7GetInternalStringPtr(0, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == 3 && !strcmp(observed, "bad"),
        "WORD.TO.STRING SLOT 10 READ PAST THE PARSED WORD TABLE");
    value = b7GetInternalStringPtr(1, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == 4 && !strcmp(observed, "keep"),
        "WORD.TO.STRING SLOT 10 CHANGED ITS DESTINATION");
    auditCheck(var[254] == 79, "WORD.TO.STRING SLOT 10 DID NOT CONSUME ITS OPERANDS");
}

static void setStringCopiesMessage(void)
{
    static const byte script[] = {
        OP_SET_STRING, 0, 1,
        OP_SET_STRING, 11, 1,
        3, 254, 88, 0
    };
    char observed[40];
    size_t length;
    char* value;

    writePromptMessage("Set text");
    writeParserString(0, "old");
    writeParserString(11, "old");
    var[254] = 0;
    auditOutput("SET.STRING MUST COPY MESSAGE 1 INTO STRING SLOTS 0 AND 11; ");
    runScript(script, sizeof(script));
    value = b7GetInternalStringPtr(0, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == 8 && !strcmp(observed, "Set text"),
        "SET.STRING DID NOT COPY ITS MESSAGE INTO STRING SLOT 0");
    value = b7GetInternalStringPtr(11, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == 8 && !strcmp(observed, "Set text"),
        "SET.STRING DID NOT COPY ITS MESSAGE INTO STRING SLOT 11");
    auditCheck(var[254] == 88, "SET.STRING DID NOT EXECUTE THE FOLLOWING OPCODE");
}

static void setStringTruncatesLongMessage(void)
{
    static const byte script[] = {OP_SET_STRING, 0, 1, 0};
    static const char longMessage[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHI";
    char observed[40];
    size_t length;
    char* value;

    writePromptMessage(longMessage);
    writeParserString(0, "");
    writeParserString(1, "sentinel");
    auditOutput("SET.STRING MUST TERMINATE A LONG MESSAGE IN ITS 40-BYTE ROW; ");
    runScript(script, sizeof(script));
    value = b7GetInternalStringPtr(0, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == sizeof(observed) - 1 && observed[sizeof(observed) - 1] == 0,
        "SET.STRING DID NOT TERMINATE ITS TRUNCATED MESSAGE");
    value = b7GetInternalStringPtr(1, &length);
    memCpyBanked((byte*)observed, (byte*)value, PARSER_BANK, sizeof(observed));
    auditCheck(length == 8 && !strcmp(observed, "sentinel"),
        "SET.STRING LONG MESSAGE OVERWROTE THE NEXT ROW");
}

static void setGameIdKeepsItsTerminator(void)
{
    static const byte script[] = {OP_SET_GAME_ID, 1, 3, 255, 89, 0};
    char savedGameId[GAMEID_MAX_LENGTH + 1];

    memcpy(savedGameId, gameId, sizeof(savedGameId));
    writePromptMessage("123456");
    var[255] = 0;
    auditOutput("SET.GAME.ID MUST KEEP ITS SIX-CHARACTER TERMINATOR; ");
    runScript(script, sizeof(script));
    auditCheck(!strcmp(gameId, "123456"), "SET.GAME.ID DID NOT STORE SIX CHARACTERS AND NUL");
    auditCheck(var[255] == 89, "SET.GAME.ID DID NOT EXECUTE THE FOLLOWING OPCODE");

    writePromptMessage("1234567");
    var[255] = 0;
    runScript(script, sizeof(script));
    auditCheck(gameId[GAMEID_MAX_LENGTH] == 0,
        "SET.GAME.ID SEVEN-CHARACTER MESSAGE OVERWROTE THE TERMINATOR");
    auditCheck(var[255] == 89, "SET.GAME.ID OVERLONG MESSAGE DID NOT EXECUTE THE FOLLOWING OPCODE");
    memcpy(gameId, savedGameId, sizeof(savedGameId));
}

static void storeHeldAllocation(byte* table, byte tableBank, unsigned int index, byte* pointer, byte bank)
{
    byte* record = table + index * ALLOCATION_RECORD_SIZE;
    memCpyBankedBetween(record, tableBank, (byte*)&pointer, RAM_BANK, sizeof(pointer));
    memCpyBankedBetween(record + sizeof(pointer), tableBank, &bank, RAM_BANK, sizeof(bank));
}

static void releaseHeldAllocations(byte* table, byte tableBank, unsigned int count)
{
    unsigned int i;
    byte* pointer;
    byte bank;
    byte* record;
    for (i = 0; i < count; ++i) {
        record = table + i * ALLOCATION_RECORD_SIZE;
        memCpyBanked((byte*)&pointer, record, tableBank, sizeof(pointer));
        memCpyBanked(&bank, record + sizeof(pointer), tableBank, sizeof(bank));
        auditCheck(b10BankedDealloc(pointer, bank), "GET.NUM EXHAUSTION FIXTURE LEAKED AN ALLOCATION");
    }
}

static void getNumSurvivesAllocationFailure(void)
{
    static const byte script[] = {
        OP_GET_NUM, 1, 252,
        3, 253, 88, 0
    };
    byte* heldAllocations;
    byte heldBank, bank;
    byte* pointer;
    unsigned int count = 0;

    heldAllocations = b10TryBankedAlloc(ALLOCATION_ARRAY_SIZE * ALLOCATION_RECORD_SIZE, &heldBank);
    auditCheck(heldAllocations != NULL, "GET.NUM EXHAUSTION RECORD ALLOCATION FAILED");
    memsetBanked(heldAllocations, 0, ALLOCATION_ARRAY_SIZE * ALLOCATION_RECORD_SIZE, heldBank);
    while (count < ALLOCATION_ARRAY_SIZE) {
        pointer = b10TryBankedAlloc(80, &bank);
        if (!pointer) break;
        storeHeldAllocation(heldAllocations, heldBank, count, pointer, bank);
        count++;
    }
    auditCheck(count > 0, "GET.NUM EXHAUSTION FIXTURE COULD NOT HOLD ANY ALLOCATIONS");

    var[253] = 0;
    runScript(script, sizeof(script));
    auditCheck(var[253] == 88, "GET.NUM ALLOCATION FAILURE DID NOT CONSUME OPERANDS");

    releaseHeldAllocations(heldAllocations, heldBank, count);
    auditCheck(b10BankedDealloc(heldAllocations, heldBank), "GET.NUM EXHAUSTION RECORD BUFFER LEAKED");
}

static void getNumCopiesInputFromItsBank(void)
{
    static const byte script[] = {
        OP_GET_NUM, 1, 252,
        3, 253, 89, 0
    };
    static byte keys[4];
    byte* heldAllocations;
    byte heldBank, bank;
    byte* pointer;
    unsigned int count = 0;
    unsigned int safeAddress = BANK_MIN + (unsigned int)_BANKRAM07_SIZE__;
    boolean wasInventoryActive = inventoryActive;

    heldAllocations = b10TryBankedAlloc(ALLOCATION_ARRAY_SIZE * ALLOCATION_RECORD_SIZE, &heldBank);
    auditCheck(heldAllocations != NULL, "GET.NUM INPUT RECORD ALLOCATION FAILED");
    memsetBanked(heldAllocations, 0, ALLOCATION_ARRAY_SIZE * ALLOCATION_RECORD_SIZE, heldBank);
    while (count < ALLOCATION_ARRAY_SIZE) {
        pointer = b10TryBankedAlloc(80, &bank);
        auditCheck(pointer != NULL, "GET.NUM INPUT FIXTURE RAN OUT OF ALLOCATIONS BEFORE A SAFE BUFFER");
        if ((unsigned int)pointer >= safeAddress) {
            auditCheck(b10BankedDealloc(pointer, bank), "GET.NUM SAFE INPUT BUFFER RELEASE FAILED");
            break;
        }
        storeHeldAllocation(heldAllocations, heldBank, count, pointer, bank);
        count++;
    }
    auditCheck(count < ALLOCATION_ARRAY_SIZE, "GET.NUM INPUT FIXTURE DID NOT REACH A SAFE BANK OFFSET");

    keys[0] = '4'; keys[1] = '2'; keys[2] = KEY_ENTER; keys[3] = 0;
    writePromptMessage("");
    resetStringInput();
    var[252] = var[253] = 0;
    inventoryActive = TRUE;
    testFeedKeys(keys);
    auditOutput("GET.NUM MUST READ ITS BANKED INPUT AND STORE 42; ");
    runScript(script, sizeof(script));
    inventoryActive = wasInventoryActive;
    auditCheck(var[252] == 42, "GET.NUM DID NOT READ THE INPUT BUFFER'S BANK");
    auditCheck(var[253] == 89, "GET.NUM DID NOT EXECUTE THE FOLLOWING OPCODE");

    releaseHeldAllocations(heldAllocations, heldBank, count);
    auditCheck(b10BankedDealloc(heldAllocations, heldBank), "GET.NUM INPUT RECORD BUFFER LEAKED");
}

void scriptstringsAuditBankedCases(void)
{
    initScriptFixture();
    parseUsesItsOperand();
    getStringRejectsInvalidDestination();
    getStringWaitsForInput();
    getStringStartsFreshForEachPrompt();
    getStringClampsToInternalRow();
    wordToStringCopiesWord();
    wordToStringRejectsOutOfRangeSlots();
    setStringCopiesMessage();
    setStringTruncatesLongMessage();
    setGameIdKeepsItsTerminator();
    getNumCopiesInputFromItsBank();
    getNumSurvivesAllocationFailure();
    b5SetLogicFile(&savedFile, TEST_LOGIC_NUMBER);
    b5SetLogicEntry(&savedEntry, TEST_LOGIC_NUMBER);
    auditCheck(b10BankedDealloc(messageData, messageBank), "SCRIPT STRING MESSAGE FIXTURE LEAKED");
    auditCheck(b10BankedDealloc(scriptData, scriptBank), "SCRIPT STRING CODE FIXTURE LEAKED");
    auditOutput("SCRIPT STRINGS AUDIT PASS\n");
}

#pragma rodata-name (pop)
#pragma code-name (pop)

void scriptstringsAuditCases(void)
{
    scriptstringsAuditBankedCases();
}
