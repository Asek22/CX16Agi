#include "audit.h"
#include "../../picture.h"

static void clearBackgroundApiCase(void)
{
    unsigned long first = STARTING_BYTE;
    unsigned long after = STARTING_BYTE + (unsigned long)PICTURE_HEIGHT * BYTES_PER_ROW;
    unsigned long pixel = first + BYTES_PER_ROW + 3;

    testPoke(0x5a, pixel);
    testPoke(0x6b, first - 1);
    testPoke(0x7c, after);
    b8ClearBackground();

    auditCheck(testPeek(pixel) == 0xff, "C clear-background API did not clear the picture pixel");
    auditCheck(testPeek(first - 1) == 0x6b && testPeek(after) == 0x7c,
               "clear-background API overwrote adjacent VRAM rows");
}

void graphicsapiAuditCases(void)
{
    clearBackgroundApiCase();
    auditOutput("GRAPHICS API AUDIT PASS\n");
}
