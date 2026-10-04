/***************************************************************************
** object.c
**
** Routines to load the OBJECT file. Firstly it needs to determine whether
** the file is encrypted or not. This is to accommodate some of the early
** AGIv2 games that didn't bother about having it encrypted.
**
** (c) 1997 Lance Ewing - Inital code (26 Aug 97)
***************************************************************************/

#include <stdio.h>
#include <cbm.h>

#include "general.h"
#include "object.h"

#pragma code-name (push, "BANKRAM0D")

#pragma rodata (push, "BANKRAM0D")
const char BD_OBJECT_FILE_NAME[] = "object";
const char BD_CANNOT_OPEN[] = "Cannot find file : object\n";
#pragma rodata (pop)

#pragma bss-name (push, "BANKRAM0D")
int bFNumObjects;
objectType bDObjects[MAX_OBJECTS];
byte bDObjData[OBJ_NAME_CACHE_SIZE];
#pragma bss-name (pop)

void bDGetObject(byte objNum, objectType* objectType)
{
    *objectType = bDObjects[objNum];
}

void bDSetObject(byte objNum, objectType* objectType)
{
    bDObjects[objNum] = *objectType;
}

/**************************************************************************
** isObjCrypt
**
** Purpose: Checks whether the OBJECT file is encrypted with Avis Durgan
** or not. I havn't fully tested this routine, but it seems to work with
** all the AGI games that I've tried it on. What it does is check the
** end of the OBJECT file which should be all text characters if it is
** not encrypted. On the other hand, if the OBJECT file is encrypted,
** there is usually a lot of characters less than 0x20.
**************************************************************************/
boolean bDIsObjCrypt(long fileLen, byte* objData)
{
    int i, checkLen;

    checkLen = (fileLen < 10) ? fileLen : ((fileLen < 20) ? 10 : 20);

    /* TODO: Needs a fix here for Mixed Up Mother Goose */
    // ->>>

    for (i = fileLen - 1; i > (fileLen - checkLen); i--) {
        if (((bDObjData[i] < 0x20) || (bDObjData[i] > 0x7F)) && (bDObjData[i] != 0))
            return TRUE;
    }

    return FALSE;
}

boolean bDLoadFile(int* fileLen, byte* buffer)
{
    byte lfn = b6Cbm_openForSeeking(BD_OBJECT_FILE_NAME);
    byte extraByte;
  
    if (lfn == NULL) {
        printf("Cannot find file : object\n");
        exit(1);
    }

    for (*fileLen = 0; *fileLen < OBJ_NAME_CACHE_SIZE; ++*fileLen) {
        if (!cbm_read(lfn, buffer + *fileLen, 1)) break;
    }

    if (*fileLen == OBJ_NAME_CACHE_SIZE && cbm_read(lfn, &extraByte, 1)) {
        cbm_close(lfn);
        printf(BD_CANNOT_OPEN);
        return FALSE;
    }

    cbm_close(lfn);

    return TRUE;
}

/**************************************************************************
** loadObjectFile
**
** Purpose: Load the names of the inventory items from the OBJECT file and
** their starting rooms.
**************************************************************************/
void bDLoadObjectFile()
{
    int avisPos = 0, objNum, i;
    int fileLen;
    byte* marker;
    word tableSize, nameOffset;
    unsigned int objectCount;
    unsigned long nameStart, pos;

    bFNumObjects = 0;
    for (objNum = 0; objNum < MAX_OBJECTS; objNum++) {
        bDObjects[objNum].name = NULL;
        bDObjects[objNum].roomNum = 0;
    }
    if (!bDLoadFile(&fileLen, bDObjData)) return;

    if (fileLen < 3) return;
    if (bDIsObjCrypt(fileLen, bDObjData))
    {
        for (i = 0; i < fileLen; i++)
        {
           bDObjData[i] ^= avisDurgan[avisPos++ % 11];
        }
    }

    tableSize = (word)bDObjData[0] | ((word)bDObjData[1] << 8);
    if (tableSize % 3 || (unsigned long)tableSize > (unsigned long)(fileLen - 3)) return;

    objectCount = tableSize / 3;
    if (objectCount > MAX_OBJECTS) return;

    marker = bDObjData + 3;
    for (objNum = 0; objNum < objectCount; objNum++, marker += 3) {
        nameOffset = (word)marker[0] | ((word)marker[1] << 8);
        nameStart = (unsigned long)nameOffset + 3UL;
        if (nameStart >= (unsigned long)fileLen) return;
        for (pos = nameStart; pos < (unsigned long)fileLen && bDObjData[pos]; pos++) {}
        if (pos == (unsigned long)fileLen) return;
    }

    marker = bDObjData + 3;
    for (objNum = 0; objNum < objectCount; objNum++, marker += 3) {
        nameOffset = (word)marker[0] | ((word)marker[1] << 8);
        bDObjects[objNum].name = (char*)(bDObjData + nameOffset + 3);
        bDObjects[objNum].roomNum = *(marker + 2);
    }
    bFNumObjects = objectCount;
}
#pragma code-name (pop)
