#ifndef RESOURCE_AUDIT_H
#define RESOURCE_AUDIT_H
#include "../../general.h"
void auditCheck(byte ok, const char* message);
void auditOutput(const char* text);
byte __fastcall__ testPeek(unsigned long address);
void __fastcall__ testPoke(byte value, unsigned long address);
#endif
