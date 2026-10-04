#include <stdio.h>

static const unsigned char payload[] = {0x12, 0x34, 0xa5, 0x5a};

static int writeFixture(const char* fileName)
{
    FILE* file = fopen(fileName, "wb");
    if (!file) {
        perror(fileName);
        return 1;
    }
    if (fwrite(payload, 1, sizeof(payload), file) != sizeof(payload)) {
        perror(fileName);
        fclose(file);
        return 1;
    }
    if (fclose(file) != 0) {
        perror(fileName);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (writeFixture("agi.cx16.spritedebug") != 0) return 1;
    /* HostFS resolves filenames case-insensitively, like the X16 SD filesystem.
     * Replace the linker's same-named sprite-debug bank image with test data. */
    if (writeFixture("agi.cx16.SPRITEDEBUG") != 0) return 1;
    return 0;
}
