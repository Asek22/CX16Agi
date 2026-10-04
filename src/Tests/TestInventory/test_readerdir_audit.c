#include "audit.h"
#include "../../agifiles.h"
#include <stdio.h>

#pragma wrapped-call (push, trampoline, FILE_LOADER_HELPERS)
extern void b6LoadAGIDir(int dirNum, const char* fName, int* count);
#pragma wrapped-call (pop)

static void readDirectoryEntry(unsigned index, AGIFilePosType* entry)
{
    memCpyBanked((byte*)entry, (byte*)&logdir[index], DIRECTORY_BANK, sizeof(*entry));
}

static void seedDirectoryEntry(unsigned index, const AGIFilePosType* entry)
{
    memCpyBanked((byte*)&logdir[index], (byte*)entry, DIRECTORY_BANK, sizeof(*entry));
}

static void writeDirectory(const byte* bytes, unsigned size)
{
    FILE* output = fopen("logdir", "wb");
    auditCheck(output != 0, "directory fixture creation");
    auditCheck(fwrite(bytes, 1, size, output) == size, "directory fixture write");
    fclose(output);
}

static void checkPartialDirectory(const byte* bytes, unsigned size, const char* message)
{
    AGIFilePosType sentinel, actual;
    int count = 0;
    sentinel.fileNum = 0xe7;
    sentinel.filePos = 0x00abcdefUL;
    seedDirectoryEntry(0, &sentinel);
    writeDirectory(bytes, size);
    b6LoadAGIDir(0, "logdir", &count);
    readDirectoryEntry(0, &actual);
    auditCheck(count == 0, message);
    auditCheck(actual.fileNum == sentinel.fileNum && actual.filePos == sentinel.filePos,
        "incomplete directory record changed its destination entry");
    auditCheck(remove("logdir") == 0, "partial directory fixture cleanup");
}

void readerdirAuditCases(void)
{
    static const byte complete[] = {0x52, 0x34, 0x56};
    static const byte oneByte[] = {0x52};
    static const byte twoBytes[] = {0x52, 0x34};
    static const byte completeThenOne[] = {0x52, 0x34, 0x56, 0xa1};
    AGIFilePosType expected, sentinel, actual, extra;
    int count = 0;

    auditOutput("AGI DIRECTORY RECORD CASES; ");
    checkPartialDirectory(oneByte, sizeof(oneByte),
        "one-byte directory tail was counted as a record");
    checkPartialDirectory(twoBytes, sizeof(twoBytes),
        "two-byte directory tail was counted as a record");

    sentinel.fileNum = 0xe7;
    sentinel.filePos = 0x00abcdefUL;
    seedDirectoryEntry(0, &sentinel);
    seedDirectoryEntry(1, &sentinel);
    writeDirectory(complete, sizeof(complete));
    b6LoadAGIDir(0, "logdir", &count);
    readDirectoryEntry(0, &actual);
    readDirectoryEntry(1, &extra);
    expected.fileNum = 5;
    expected.filePos = 0x00023456UL;
    auditCheck(count == 1, "complete directory record was not counted");
    auditCheck(actual.fileNum == expected.fileNum && actual.filePos == expected.filePos,
        "complete directory record was not decoded");
    auditCheck(extra.fileNum == sentinel.fileNum && extra.filePos == sentinel.filePos,
        "complete directory read published an extra record");
    auditCheck(remove("logdir") == 0, "complete directory fixture cleanup");

    count = 0;
    seedDirectoryEntry(0, &sentinel);
    seedDirectoryEntry(1, &sentinel);
    writeDirectory(completeThenOne, sizeof(completeThenOne));
    b6LoadAGIDir(0, "logdir", &count);
    readDirectoryEntry(0, &actual);
    readDirectoryEntry(1, &extra);
    auditCheck(count == 1, "partial tail changed the count after a complete record");
    auditCheck(actual.fileNum == expected.fileNum && actual.filePos == expected.filePos,
        "partial tail changed the preceding complete record");
    auditCheck(extra.fileNum == sentinel.fileNum && extra.filePos == sentinel.filePos,
        "partial tail published a bogus directory offset");
    auditCheck(remove("logdir") == 0, "trailing partial directory fixture cleanup");
}
