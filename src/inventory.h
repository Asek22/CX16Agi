#ifndef INVENTORY_H
#define INVENTORY_H
#include "general.h"
extern byte inventoryActive;
byte inventoryShow(boolean select);
/* Open/close own slot 127 and its disposable bitmap; unknown objects use name only. */
boolean inventoryInspectOpen(byte object);
void inventoryInspectClose(void);
#endif
