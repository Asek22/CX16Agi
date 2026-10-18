#include "paletteManager.h"
#include "helpers.h"

#pragma wrapped-call (push, trampoline, HELPERS_BANK)
extern void memsetBanked(void* _Dst, int _Val, size_t _Size, byte bank);
#pragma wrapped-call (pop)

#pragma bss-name (push, "BANKRAM0F")
int allocatedPaletteOwners[NO_MANAGED_PALETTES];
byte palettesAllocated;
#pragma bss-name (pop)

//#define VERBOSE_PALETTE_MANAGER

#pragma code-name (push, "CODE")
byte bFInitPaletteManager()
{
	memsetBanked((byte*)allocatedPaletteOwners, 0, sizeof(allocatedPaletteOwners), PALETTE_MANAGER_BANK);
	memsetBanked((byte*)&palettesAllocated, 0, sizeof(palettesAllocated), PALETTE_MANAGER_BANK);
	return 0;
}
#pragma code-name (pop)

#pragma code-name (push, "BANKRAM0F")
byte bFGetPalette(int id, PaletteGetResult* result)
{
	byte i;
	byte allocatedPalette;

#ifdef VERBOSE_PALETTE_MANAGER
	printf("gp1. the id is %d\n", id);
#endif // VERBOSE_PALETTE_MANAGER

#ifdef VERBOSE_PALETTE_MANAGER
	printf("gp2. the number of available palettes are %d\n", NO_MANAGED_PALETTES - palettesAllocated);
#endif // VERBOSE_PALETTE_MANAGER


	for (i = 0; i < palettesAllocated; i++)
	{
		if (allocatedPaletteOwners[i] == id)
		{
#ifdef VERBOSE_PALETTE_MANAGER
			printf("gp3. we are now returning %d\n",i);
#endif // VERBOSE_PALETTE_MANAGER

			*result = AlreadyAllocated;
			return i + BASE_MANAGED_PALETTE;
		}
	}
	if (palettesAllocated == NO_MANAGED_PALETTES)
	{
		*result = FailToAllocate;
		return 0;
	}
	
	*result = Allocated;

#ifdef VERBOSE_PALETTE_MANAGER
	printf("gp4. allocating %d\n", palettesAllocated);
#endif

	allocatedPalette = palettesAllocated++;
	allocatedPaletteOwners[i] = id;

	return allocatedPalette + BASE_MANAGED_PALETTE;
}
#pragma code-name (pop)
