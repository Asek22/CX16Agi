#include "audit.h"
#include "../../view.h"
#include <string.h>
extern byte viewHeaderBuffer[], loopHeaderBuffer[];
#pragma wrapped-call (push, trampoline, VIEW_CODE_BANK_1)
extern void setViewData(byte number, AGIFile* file, View* view);
extern void setLoopData(AGIFile* file, View* view, Loop* loop, byte* header, byte number, byte loopNum);
#pragma wrapped-call (pop)
static void viewHeaderCase(byte loops)
{
    static byte bytes[515], copied[515];
    View local, stored;
    AGIFile file;
    byte bank, i, guard[16];
    unsigned index;
    memset(&local, 0, sizeof(local));
    memset(&file, 0, sizeof(file));
    memset(bytes, 0, sizeof(bytes));
    bytes[2] = loops;
    for (index=0; index<loops; ++index) {
        bytes[5+index*2] = (byte)(40+index);
        bytes[6+index*2] = (byte)(index>>4);
    }
    file.code = b10BankedAlloc(sizeof(bytes), &bank);
    file.codeBank = bank; file.codeSize = sizeof(bytes);
    memCpyBanked(file.code, bytes, bank, sizeof(bytes));
    memsetBanked(viewHeaderBuffer, 0xa5, 501, VIEW_CODE_BANK_1);
    memsetBanked(loopHeaderBuffer, 0xe7, 16, VIEW_CODE_BANK_1);
    setViewData(250, &file, &local);
    getLoadedView(&stored, 250);
    auditCheck(!memcmp(&stored, &local, sizeof(local)), "view loader cached stack pointer bytes instead of view metadata");
    memCpyBanked(copied, viewHeaderBuffer, VIEW_CODE_BANK_1, 5+(unsigned)loops*2);
    auditCheck(!memcmp(copied, bytes, 5+(unsigned)loops*2), "view header lost loop offsets after loop seven");
    memCpyBanked(guard, loopHeaderBuffer, VIEW_CODE_BANK_1, sizeof(guard));
    for (i=0; i<sizeof(guard); ++i) auditCheck(guard[i] == 0xe7, "view header overflowed neighboring loop buffer");
    b9DiscardView(250);
}
static void loopHeaderCase(void)
{
    static byte bytes[511];
    byte bank, before[16], after[16];
    View view;
    Loop loop;
    AGIFile file;
    memset(bytes, 0, sizeof(bytes)); bytes[0]=255;
    memset(&view,0,sizeof(view)); memset(&loop,0,sizeof(loop)); memset(&file,0,sizeof(file));
    view.loaded=TRUE; view.numberOfLoops=1;
    view.loops=(Loop*)b10BankedAlloc(sizeof(Loop), &view.loopsBank);
    memsetBanked(view.loops,0,sizeof(Loop),view.loopsBank);
    file.code=b10BankedAlloc(sizeof(bytes),&bank); file.codeBank=bank;
    memCpyBanked(file.code,bytes,bank,sizeof(bytes));
    memCpyBanked(before,(byte*)b9CelToVera,VIEW_CODE_BANK_1,sizeof(before));
    setLoopData(&file,&view,&loop,file.code,250,0);
    memCpyBanked(after,(byte*)b9CelToVera,VIEW_CODE_BANK_1,sizeof(after));
    auditCheck(!memcmp(before,after,sizeof(before)),"loop header overwrote neighboring sprite-transfer code");
    auditCheck(loop.numberOfCels==255,"loop header lost maximum cel count");
    auditCheck(b10BankedDealloc((byte*)loop.cels,loop.celsBank),"loop cel allocation cleanup");
    auditCheck(b10BankedDealloc((byte*)view.loops,view.loopsBank),"loop allocation cleanup");
    auditCheck(b10BankedDealloc(file.code,bank),"loop resource allocation cleanup");
}
void viewsAuditCases(void)
{
    auditOutput("VIEW HEADER CASES; ");
    viewHeaderCase(8);
    viewHeaderCase(14);
    viewHeaderCase(255);
    loopHeaderCase();
}
