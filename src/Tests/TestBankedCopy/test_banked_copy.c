#include "../../helpers.h"

extern byte testStatus(void);
extern void testExit(void);
extern void testIrqStart(void);
extern void testIrqStop(void);
extern unsigned int testIrqCount;
extern byte testIrqWrongBank;
extern unsigned long testIrqMaxGap;
extern void* testYieldCopy(byte* dest, byte* src, byte bank, size_t len);
static byte buffer[8194];
static char messageBuffer[120];

static void output(const char* text)
{
    while (*text) *(volatile byte*)0x9fbb = (byte)*text++ & 0x7f;
}
static const unsigned int lengths[] = {0, 1, 15, 16, 17, 31, 32, 33, 49, 50, 51, 191, 192, 193, 255, 256, 257, 511, 512, 513, 767, 768, 769, 1023, 8000, 8192};

/* Retain the original production C path as the repeatable timing baseline. */
static void* referenceCopy(byte* dest, byte* src, byte bank, size_t len)
{
    byte previous = RAM_BANK;
    void* result;
    RAM_BANK = bank;
    result = memcpy(dest, src, len);
    RAM_BANK = previous;
    return result;
}

static void check(byte ok, const char* message)
{
    if (!ok) {
        output("BANKED COPY FAIL: ");
        output(message);
        testExit();
    }
}

static byte pattern(unsigned int i)
{
    return (byte)(i ^ (i >> 8) ^ 0x5a);
}

static void parity(void)
{
    unsigned int i, j, n;
    byte bank;
    byte* window = (byte*)0xa000;
    for (bank = 2; bank < 48; bank += 15) {
        RAM_BANK = bank;
        for (i = 0; i < 8192; ++i) window[i] = pattern(i);
        for (j = 0; j < sizeof(lengths) / sizeof(lengths[0]); ++j) {
            n = lengths[j];
            memset(buffer, 0xcc, sizeof(buffer));
            RAM_BANK = 7;
            check(memCpyBanked(buffer + 1, window + 8192 - n, bank, n) == buffer + 1, "return pointer");
            check(RAM_BANK == 7, "bank restoration");
            check(buffer[0] == 0xcc && buffer[n + 1] == 0xcc, "copy bounds");
            for (i = 0; i < n; ++i)
                check(buffer[i + 1] == pattern(i + 8192 - n), "bank/page/length pixels");
        }
        for (i = 0; i < 8192; ++i) buffer[i] = pattern(i + 1);
        RAM_BANK = 7;
        memCpyBanked(window, buffer, bank, 8192);
        check(RAM_BANK == 7, "write bank restoration");
        RAM_BANK = bank;
        for (i = 0; i < 8192; ++i) check(window[i] == pattern(i + 1), "bank write pixels");
    }
    RAM_BANK = 2;
    for (i = 0; i < 8192; ++i) window[i] = pattern(i);
    RAM_BANK = 7;
    memCpyBankedBetween(window, 47, window, 2, 8000);
    check(RAM_BANK == 7, "between bank restoration");
    RAM_BANK = 47;
    for (i = 0; i < 8000; ++i) check(window[i] == pattern(i), "between bank pixels");
    /* An object crossing the 8 KiB window is split by its C owner. */
    RAM_BANK = 7;
    memCpyBanked(buffer, window + 8182, 2, 10);
    memCpyBanked(buffer + 10, window, 47, 257);
    check(RAM_BANK == 7, "split object bank restoration");
    for (i = 0; i < 10; ++i) check(buffer[i] == pattern(i + 8182), "split first bank");
    for (i = 0; i < 257; ++i) check(buffer[i + 10] == pattern(i), "split next bank");
    memCpyBanked(window + 1024, window, 47, 257);
    RAM_BANK = 47;
    for (i = 0; i < 257; ++i) check(window[1024 + i] == pattern(i), "same bank pixels");
    asm("sei");
    memCpyBanked(buffer, window, 47, 257);
    check(testStatus() & 4, "disabled IRQ state");
    asm("cli");
    memCpyBanked(buffer, window, 47, 257);
    check(!(testStatus() & 4), "enabled IRQ state");
}

static void irqParity(void)
{
    byte iteration;
    unsigned int i;
    RAM_BANK = 2;
    for (i = 0; i < 8192; ++i) ((byte*)0xa000)[i] = pattern(i);
    RAM_BANK = 7;
    testIrqStart();
    for (iteration = 0; iteration < 16; ++iteration)
        testYieldCopy(buffer, (byte*)0xa000, 2, 8192);
    testIrqStop();
    check(testIrqCount != 0, "IRQ stress did not deliver interrupts");
    check(!testIrqWrongBank, "IRQ saw a mapped transfer bank");
    sprintf(messageBuffer, "IRQ maximum service gap: %lu cycles; ", testIrqMaxGap);
    output(messageBuffer);
    check(testIrqMaxGap < 10000UL, "IRQ blocked longer than 1.25 ms");
    check(RAM_BANK == 7, "IRQ bank restoration");
    for (i = 0; i < 8192; ++i) check(buffer[i] == pattern(i), "IRQ copy pixels");
}

static unsigned long cycles(void)
{
    unsigned long value;
    byte a = *(volatile byte*)0x9fb8;
    byte b = *(volatile byte*)0x9fb9;
    byte c = *(volatile byte*)0x9fba;
    byte d = *(volatile byte*)0x9fbb;
    value = d;
    value = (value << 8) | c;
    value = (value << 8) | b;
    return (value << 8) | a;
}

static void resourceCases(void)
{
    BufferStatus status;
    unsigned int i, oldSize = 100;
    byte oldBank, bank, reusedBank;
    byte *oldPointer, *pointer, *reused;
    RAM_BANK = 2;
    for (i = 0; i < 1000; ++i) ((byte*)0xa000)[i] = pattern(i);
    status.bankedData = (byte*)0xa000;
    status.bank = 2;
    status.bufferCounter = 0;
    RAM_BANK = 7;
    b5RefreshBufferNonGolden(&status, buffer, 500);
    check(status.bufferCounter == 1 && RAM_BANK == 7, "refill bank/counter");
    for (i = 0; i < 500; ++i) check(buffer[i] == pattern(i), "first refill bytes");
    b5RefreshBufferNonGolden(&status, buffer, 500);
    for (i = 0; i < 500; ++i) check(buffer[i] == pattern(i + 500), "second refill bytes");
    RAM_BANK = MEMORY_MANAGEMENT_BANK;
    b10InitDynamicMemory();
    RAM_BANK = 7;
    oldPointer = b10BankedAlloc(100, &oldBank);
    bank = oldBank;
    pointer = oldPointer;
    RAM_BANK = oldBank;
    for (i = 0; i < 100; ++i) pointer[i] = pattern(i);
    RAM_BANK = 7;
    b5ReallocateBiggerMemoryBlock(&pointer, 160, &oldSize, &bank);
    check(oldSize == 160 && RAM_BANK == 7, "retirement bank/size");
    RAM_BANK = bank;
    for (i = 0; i < 100; ++i) check(pointer[i] == pattern(i), "retirement copy completed");
    RAM_BANK = 7;
    reused = b10BankedAlloc(100, &reusedBank);
    check(reused == oldPointer && reusedBank == oldBank, "old block released after copy");
    check(b10BankedDealloc(reused, reusedBank), "old block cleanup");
    check(b10BankedDealloc(pointer, bank), "new block cleanup");
}

/* Retain the original 50-byte C orchestration as the bank-to-bank baseline. */
static void referenceBetween(byte* dest, byte destBank, byte* src, byte srcBank, size_t len)
{
    int i;
    int amount = 0;
    byte scratch[50];
    for (i = 0; i < len; i += 50) {
        amount = i + 50 <= len ? 50 : len - i;
        referenceCopy(scratch, src + i, srcBank, amount);
        referenceCopy(dest + i, scratch, destBank, amount);
    }
}

#pragma wrapped-call (push, trampoline, HELPERS_BANK)
static void referenceRefill(BufferStatus* status, byte* dest, int size);
#pragma wrapped-call (pop)
#pragma code-name (push, "BANKRAM05")
static void referenceRefill(BufferStatus* status, byte* dest, int size)
{
    BufferStatus before;
    before = *status;
    status->bufferCounter++;
    referenceCopy(dest, before.bankedData + before.bufferCounter * size, before.bank, size);
}
#pragma code-name (pop)

static void refillBenchmark(void)
{
    BufferStatus status;
    unsigned long start, baseline, actual;
    byte iteration;
    asm("sei");
    start = cycles();
    for (iteration = 0; iteration < 8; ++iteration)
        referenceBetween((byte*)0xa000, 47, (byte*)0xa000, 2, 500);
    baseline = cycles() - start;
    start = cycles();
    for (iteration = 0; iteration < 8; ++iteration)
        memCpyBankedBetween((byte*)0xa000, 47, (byte*)0xa000, 2, 500);
    actual = cycles() - start;
    asm("cli");
    sprintf(messageBuffer, "500-byte cross-bank x8: C %lu, production %lu cycles; ", baseline, actual);
    output(messageBuffer);
    check(actual < baseline - baseline / 20, "cross-bank refill slower than C");
    status.bankedData = (byte*)0xa000;
    status.bank = 2;
    status.bufferCounter = 0;
    asm("sei");
    start = cycles();
    for (iteration = 0; iteration < 8; ++iteration)
        referenceRefill(&status, buffer, 500);
    baseline = cycles() - start;
    status.bufferCounter = 0;
    start = cycles();
    for (iteration = 0; iteration < 8; ++iteration)
        b5RefreshBufferNonGolden(&status, buffer, 500);
    actual = cycles() - start;
    asm("cli");
    sprintf(messageBuffer, "500-byte production refill x8: C %lu, assembly %lu cycles; ", baseline, actual);
    output(messageBuffer);
    check(actual < baseline, "production refill slower than original C refill");
}

static void benchmark(void)
{
    unsigned long start, baseline, actual;
    unsigned int size;
    byte bank, workload, irqMode, veraIen, viaIer;
    static const unsigned int sizes[] = {8000, 2048, 500, 50, 6, 1};
    /* Measure incoming I=0 yields without IRQ-handler timing noise. Real IRQ
     * delivery, latency and scratch clobbering are tested separately above. */
    asm("sei");
    veraIen = *(volatile byte*)0x9f26;
    viaIer = *(volatile byte*)0x9f0e;
    *(volatile byte*)0x9f26 = 0;
    *(volatile byte*)0x9f0e = 0x7f;
    for (irqMode = 0; irqMode < 2; ++irqMode) {
        for (workload = 0; workload < sizeof(sizes) / sizeof(sizes[0]); ++workload) {
            size = sizes[workload];
            if (irqMode) { asm("cli"); } else { asm("sei"); }
            start = cycles();
            for (bank = 2; bank < 6; ++bank) referenceCopy(buffer, (byte*)0xa000, bank, size);
            baseline = cycles() - start;
            start = cycles();
            for (bank = 2; bank < 6; ++bank) memCpyBanked(buffer, (byte*)0xa000, bank, size);
            actual = cycles() - start;
            asm("cli");
            sprintf(messageBuffer, "%u-byte copy IRQ %u: C %lu, assembly %lu cycles; ", size * 4, irqMode, baseline, actual);
            output(messageBuffer);
            check(actual < baseline - baseline / 20, "copy must be at least 5 percent faster than C");
        }
    }
    asm("sei");
    *(volatile byte*)0x9f26 = veraIen;
    *(volatile byte*)0x9f0e = viaIer;
    asm("cli");
}

int main(void)
{
    check(*(volatile byte*)0x9fbe == '1' && *(volatile byte*)0x9fbf == '6', "X16 emulator required");
    parity();
    resourceCases();
    irqParity();
    benchmark();
    refillBenchmark();
    output("BANKED COPY PASS");
    testExit();
    return 0;
}
