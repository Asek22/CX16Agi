#include "audit.h"
#include "../../helpers.h"
#include "../../object.h"

extern byte bDObjData[];

static const char objectName[] = "valid object fixture";

static void loadFixture(const byte* data, unsigned int size)
{
    FILE* file = fopen("object", "wb");
    byte written;
    int closed;

    auditCheck(file != 0, "OBJECT fixture creation failed");
    written = fwrite(data, 1, size, file) == size;
    closed = fclose(file);
    auditCheck(written && closed == 0, "OBJECT fixture write failed");
    bDLoadObjectFile();
    auditCheck(remove("object") == 0, "OBJECT fixture cleanup failed");
}

static int objectCount(void)
{
    int count;
    memCpyBanked((byte*)&count, (byte*)&bFNumObjects, OBJECT_BANK, sizeof(count));
    return count;
}

static void checkValidObject(byte* data)
{
    objectType object;
    byte name[sizeof(objectName)];

    loadFixture(data, 6 + sizeof(objectName));
    auditCheck(objectCount() == 1, "complete OBJECT record was rejected");
    bDGetObject(0, &object);
    auditCheck(object.roomNum == 65, "complete OBJECT room was changed");
    memCpyBanked(name, (byte*)object.name, OBJECT_BANK, sizeof(name));
    auditCheck(memcmp(name, objectName, sizeof(name)) == 0, "complete OBJECT name was changed");
}

static void checkRejectedObject(byte* data, unsigned int size, const char* message)
{
    byte oldTail, canary = 0xa5, readTail;
    objectType object;

    memCpyBanked(&oldTail, bDObjData + size, OBJECT_BANK, 1);
    memCpyBanked(bDObjData + size, &canary, OBJECT_BANK, 1);
    loadFixture(data, size);
    memCpyBanked(&readTail, bDObjData + size, OBJECT_BANK, 1);
    memCpyBanked(bDObjData + size, &oldTail, OBJECT_BANK, 1);

    auditCheck(readTail == canary, "OBJECT loader wrote beyond bytes read");
    auditCheck(objectCount() == 0, message);
    bDGetObject(0, &object);
    auditCheck(!object.name && !object.roomNum, "rejected OBJECT file left a published entry");
}

void readerobjectAuditCases(void)
{
    byte valid[6 + sizeof(objectName)];
    byte encrypted[6 + sizeof(objectName)];
    byte truncatedTable[23] = {21, 0, 0};
    byte shortHeader[3] = {3, 0, 0};
    byte invalidNameOffset[30] = {3, 0, 0, 28, 0, 65};
    byte unterminatedName[30] = {3, 0, 0, 3, 0, 65};
    unsigned int i;

    auditOutput("OBJECT FILE BOUNDS CASES; ");
    memset(valid, 0, sizeof(valid));
    valid[0] = 3;
    valid[3] = 3;
    valid[5] = 65;
    memcpy(valid + 6, objectName, sizeof(objectName));
    checkValidObject(valid);

    for (i = 0; i < sizeof(valid); ++i)
        encrypted[i] = valid[i] ^ avisDurgan[i % 11];
    checkValidObject(encrypted);

    checkRejectedObject(truncatedTable, sizeof(truncatedTable), "truncated OBJECT record table was accepted");
    checkRejectedObject(shortHeader, sizeof(shortHeader), "OBJECT header without its record table was accepted");
    checkRejectedObject(invalidNameOffset, sizeof(invalidNameOffset), "out-of-file OBJECT name offset was accepted");
    memset(unterminatedName + 6, 'a', sizeof(unterminatedName) - 6);
    checkRejectedObject(unterminatedName, sizeof(unterminatedName), "unterminated OBJECT name was accepted");
}
