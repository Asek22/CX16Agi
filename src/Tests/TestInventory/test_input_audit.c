#include "audit.h"
#include "../../keyboard.h"
#include "../../memoryManager.h"
#include "../../controllers.h"
extern byte inputLineDisplayed;
extern volatile unsigned int vSyncCounter;
extern void testDisableInterrupts(void);
extern void testEnableInterrupts(void);
/* Same tile row as INPUT_STRING_ADDRESS in globalGraphics.s. */
#define INPUT_ADDRESS (MAPBASE + 25 * 128)
void inputAuditCases(void)
{
    unsigned int start;
    byte i;
    b1ResetControllers();
    auditCheck(!b1IsControllerSet(0) && !b1IsControllerSet(255), "CONTROLLER GETTER DID NOT RETURN RESET BITS");
    b1SetController(254); /* 255 is the unassociated-controller sentinel. */
    auditCheck(b1IsControllerSet(254) && !b1IsControllerSet(255), "CONTROLLER GETTER LOST ITS C CALLING CONVENTION");
    b1ResetControllers();
    testDisableInterrupts();
    inputLineDisplayed = FALSE;
    for (i=0; i<40; ++i) testPoke(65, INPUT_ADDRESS + (unsigned long)i*2);
    testPoke(66, INPUT_ADDRESS + 80UL);
    start = vSyncCounter;
    testEnableInterrupts();
    while (vSyncCounter == start) { }
    for (i=0; i<40; ++i)
        auditCheck(testPeek(INPUT_ADDRESS + (unsigned long)i*2) == 0x80, "hidden input line retained trailing characters after VSYNC");
    auditCheck(testPeek(INPUT_ADDRESS + 80UL) == 66, "input line clear overwrote adjacent tile");
    auditOutput("INPUT IRQ AUDIT PASS\n");
}
