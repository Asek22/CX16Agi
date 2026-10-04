#include "audit.h"
#include <cbm.h>
#include <stdio.h>
#include <string.h>

#include "../../agifiles.h"

extern char b6SpriteDebugFileName[];

static const byte fixturePayload[] = {0x12, 0x34, 0xa5, 0x5a};

static void writeFixture(const char* fileName)
{
    FILE* file = fopen(fileName, "wb");
    auditCheck(file != 0, "reader filename short fixture creation");
    auditCheck(fwrite(fixturePayload, 1, sizeof(fixturePayload), file) == sizeof(fixturePayload),
        "reader filename short fixture write");
    auditCheck(fclose(file) == 0, "reader filename short fixture close");
}

static void checkFixtureReadable(const char* fileNameAndFlags, const char* openFailure)
{
    byte payload[sizeof(fixturePayload)];
    byte openStatus = cbm_open(SEQUENTIAL_LFN, FILE_DEVICE, FILE_OPEN_ADDRESS, fileNameAndFlags);
    int bytesRead;

    auditCheck(openStatus == 0, openFailure);
    if (openStatus != 0) return;
    bytesRead = cbm_read(SEQUENTIAL_LFN, payload, sizeof(payload));
    cbm_close(SEQUENTIAL_LFN);
    auditCheck(bytesRead == sizeof(fixturePayload), "reader filename raw KERNAL payload length changed");
    if (bytesRead == sizeof(fixturePayload))
        auditCheck(memcmp(payload, fixturePayload, sizeof(fixturePayload)) == 0,
            "reader filename raw KERNAL payload changed");
}

static void openAndCheckPayload(const char* fileName)
{
    struct {
        byte before;
        byte payload[sizeof(fixturePayload)];
        byte after;
    } guarded;
    byte channel = b6Cbm_openForSeeking(fileName);
    int bytesRead;

    guarded.before = 0x69;
    guarded.after = 0x96;
    bytesRead = cbm_read(channel, guarded.payload, sizeof(guarded.payload));
    cbm_close(channel);
    auditCheck(bytesRead == sizeof(fixturePayload), "reader filename production payload length changed");
    auditCheck(guarded.before == 0x69 && guarded.after == 0x96,
        "reader filename payload crossed its canaries");
    auditCheck(memcmp(guarded.payload, fixturePayload, sizeof(fixturePayload)) == 0,
        "reader filename production payload changed");
}

void readerfilenameAuditCases(void)
{
    static char inputStorage[255];
    char* boundaryName = inputStorage + 1;
    byte channel;
    byte boundaryOpenStatus;
    int bytesRead;
    int boundaryReadable;
    unsigned i;

    auditOutput("READER FILENAME VOL.8 CONTROL; ");
    writeFixture("vol.8");
    checkFixtureReadable("vol.8,S,R", "reader filename short fixture is not readable through KERNAL");
    openAndCheckPayload("vol.8");
    auditCheck(remove("vol.8") == 0, "vol.8 fixture cleanup");

    auditOutput("READER FILENAME BUILT-IN SPRITE DEBUG NAME; ");
    checkFixtureReadable("agi.cx16.spritedebug,S,R",
        "reader filename long fixture is not readable through KERNAL");
    /* Keep this marker visible if the production call corrupts its stack. */
    auditOutput("OPENING agi.cx16.spritedebug; ");
    openAndCheckPayload(b6SpriteDebugFileName);

    inputStorage[0] = 0x69;
    for (i = 0; i < 251; ++i) boundaryName[i] = 'A';
    boundaryName[251] = 0;
    auditOutput("READER FILENAME SETNAM 255-BYTE BOUNDARY; ");
    for (i = 0; i < 251; ++i) inputStorage[i] = 'A';
    inputStorage[251] = ',';
    inputStorage[252] = 'S';
    inputStorage[253] = ',';
    inputStorage[254] = 'R';
    /* Exercise SETNAM at its 8-bit maximum; this buffer is not NUL-terminated. */
    __asm__("lda #$ff");
    __asm__("ldx #<%v", inputStorage);
    __asm__("ldy #>%v", inputStorage);
    __asm__("jsr $ffbd");
    cbm_k_setlfs(SEQUENTIAL_LFN, FILE_DEVICE, FILE_OPEN_ADDRESS);
    boundaryOpenStatus = cbm_k_open();
    bytesRead = -1;
    boundaryReadable = 0;
    if (boundaryOpenStatus == 0) {
        byte rawPayload[sizeof(fixturePayload)];
        bytesRead = cbm_read(SEQUENTIAL_LFN, rawPayload, sizeof(rawPayload));
        cbm_close(SEQUENTIAL_LFN);
        boundaryReadable = bytesRead == sizeof(fixturePayload) &&
            memcmp(rawPayload, fixturePayload, sizeof(rawPayload)) == 0;
    }
    for (i = 0; i < 251; ++i) boundaryName[i] = 'A';
    boundaryName[251] = 0;
    inputStorage[0] = 0x69;
    inputStorage[253] = 0x96;
    if (!boundaryReadable) auditOutput("HOSTFS BOUNDARY PAYLOAD UNAVAILABLE; ");
    if (boundaryReadable) openAndCheckPayload(boundaryName);
    auditCheck(inputStorage[0] == 0x69 && inputStorage[253] == 0x96,
        "reader filename boundary input crossed its canaries");

    inputStorage[0] = 0x69;
    for (i = 0; i < 251; ++i) boundaryName[i] = 'A';
    boundaryName[251] = 'B';
    boundaryName[252] = 0;
    inputStorage[254] = 0x96;
    auditOutput("READER FILENAME OVERLONG INPUT; ");
    channel = b6Cbm_openForSeeking(boundaryName);
    auditCheck(channel == 0, "overlong reader filename did not return NULL");
    auditCheck(inputStorage[0] == 0x69 && inputStorage[254] == 0x96,
        "overlong reader filename input crossed its canaries");
}
