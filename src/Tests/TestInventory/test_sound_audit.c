#include "audit.h"
#include "../../sound.h"
#include "../../helpers.h"
#include <string.h>

#pragma wrapped-call (push, trampoline, SOUND_BANK)
extern boolean bBFillNoteBuffer(byte* buffer, byte** cursor, BufferStatus* status, byte* note);
extern void bBWriteNextNG(byte** block, byte* buffer, byte** cursor, byte value, BufferStatus* status, unsigned int* size, unsigned int newSize, byte* bank, unsigned int bufferSize);
extern void bBPreComputePeriodicSound(SoundFile* sound, unsigned int* offsets);
#pragma wrapped-call (pop)
extern unsigned long totalSoundSize, totalBeats;

#define CHANNEL_BUFFER_SIZE ((LOCAL_WORK_AREA_SIZE - 2) / 3)
static byte channel[CHANNEL_BUFFER_SIZE + 2];

static void resizeReturnCase(void)
{
    unsigned size=100;
    byte bank, oldBank, reuseBank, *block, *oldBlock, *reuse, *returned;
    block=b10BankedAlloc(size,&bank);
    oldBlock=block; oldBank=bank;
    memsetBanked(block,0x57,size,bank);
    returned=b5ReallocateBiggerMemoryBlock(&block,1000,&size,&bank);
    auditCheck(returned==block,"banked resize returned a pointer other than its owned allocation");
    memCpyBanked(channel,block,bank,100);
    auditCheck(channel[0]==0x57 && channel[99]==0x57,"banked resize lost copied contents");
    reuse=b10BankedAlloc(100,&reuseBank);
    auditCheck(reuse==oldBlock && reuseBank==oldBank,"banked resize retained retired allocation");
    auditCheck(b10BankedDealloc(reuse,reuseBank),"banked resize reused allocation cleanup");
    auditCheck(b10BankedDealloc(block,bank),"banked resize allocation cleanup");
}

static void refillCase(void)
{
    BufferStatus status;
    byte bank, previous, j, note[5], *block, *cursor;
    unsigned i;
    block = b10BankedAlloc(CHANNEL_BUFFER_SIZE*2, &bank);
    previous=RAM_BANK; RAM_BANK=bank;
    for (i = 0; i < CHANNEL_BUFFER_SIZE*2; ++i) block[i] = (byte)(i % 127 + 1);
    RAM_BANK=previous;
    channel[0] = channel[sizeof(channel)-1] = 0xa7;
    status.bankedData = block;
    status.bank = bank;
    status.bufferCounter = 0;
    b5RefreshBufferNonGolden(&status, channel+1, CHANNEL_BUFFER_SIZE);
    cursor = channel+1;
    for (i=0; i<35; ++i) {
        auditCheck(bBFillNoteBuffer(channel+1, &cursor, &status, note), "sound notes ended before channel refill");
        for(j=0;j<5;++j) auditCheck(note[j]==(byte)((i*5+j)%127+1), "sound channel refill consumed stale or wrong-bank notes");
    }
    auditCheck(channel[0]==0xa7 && channel[sizeof(channel)-1]==0xa7, "sound channel refill overwrote adjacent buffer");
    auditCheck(status.bufferCounter==2, "sound channel refill counter");
    auditCheck(b10BankedDealloc(block, bank), "sound refill allocation cleanup");
}

static void growthCase(void)
{
    BufferStatus status;
    byte bank, oldBank, reuseBank, *block, *oldBlock, *reuse, *cursor;
    unsigned size=100, i;
    block = b10BankedAlloc(size, &bank);
    oldBlock=block; oldBank=bank;
    memsetBanked(block, 0x43, size, bank);
    status.bankedData=block+20;
    status.bank=bank;
    status.bufferCounter=0;
    cursor=channel+1;
    for (i=0; i<200; ++i) {
        bBWriteNextNG(&block, channel+1, &cursor, (byte)i, &status, &size, 1000, &bank, CHANNEL_BUFFER_SIZE);
        if (i==80) auditCheck(size>=101, "sound growth ignored noise-channel resource offset");
    }
    auditCheck(status.bankedData==block+20 && status.bank==bank, "sound growth retained freed buffer metadata");
    reuse=b10BankedAlloc(100, &reuseBank);
    auditCheck(reuse==oldBlock && reuseBank==oldBank, "sound growth did not release original block");
    memsetBanked(reuse, 0xa9, 100, reuseBank);
    b5FlushBufferNonGolden(&status, channel+1, CHANNEL_BUFFER_SIZE, cursor-(channel+1));
    memCpyBanked(channel, block, bank, 160);
    for(i=0;i<20;++i) auditCheck(channel[i]==0x43,"sound growth changed earlier channel bytes");
    for(i=0;i<140;++i) auditCheck(channel[i+20]==(byte)i,"sound growth lost generated note bytes");
    memCpyBanked(channel, block+160, bank, 60);
    for(i=0;i<60;++i) auditCheck(channel[i]==(byte)(i+140),"sound growth lost flushed note bytes");
    memCpyBanked(channel, reuse, reuseBank, 100);
    for(i=0;i<100;++i) auditCheck(channel[i]==0xa9,"sound flush wrote retired allocation");
    auditCheck(b10BankedDealloc(reuse,reuseBank),"sound reused allocation cleanup");
    auditCheck(b10BankedDealloc(block,bank),"sound grown allocation cleanup");
}

static void precomputeCase(void)
{
    SoundFile sound;
    unsigned offsets[4] = {8,10,12,19};
    byte note[5] = {1,0,0,4,15}, sustained[5] = {255,127,0,4,15};
    unsigned long size=1000, beats=35;
    unsigned i;
    sound.soundResource=b10BankedAlloc(1000,&sound.soundBank);
    sound.ch0=sound.soundResource+8;
    sound.ch1=sound.soundResource+10;
    sound.ch2=sound.soundResource+12;
    sound.chNoise=sound.soundResource+19;
    memsetBanked(sound.soundResource,255,1000,sound.soundBank);
    memCpyBanked(sound.ch2,sustained,sound.soundBank,5);
    for(i=0;i<35;++i) memCpyBanked(sound.chNoise+i*5,note,sound.soundBank,5);
    memCpyBanked((byte*)&totalSoundSize,(byte*)&size,SOUND_BANK,sizeof(size));
    memCpyBanked((byte*)&totalBeats,(byte*)&beats,SOUND_BANK,sizeof(beats));
    memset(GOLDEN_RAM_WORK_AREA,255,LOCAL_WORK_AREA_SIZE);
    bBPreComputePeriodicSound(&sound,offsets);
    auditCheck(sound.chNoise==sound.soundResource+19,"precomputed sound channel pointer not relocated");
    for(i=0;i<35;++i) {
        memCpyBanked(channel,sound.chNoise+i*5,sound.soundBank,5);
        auditCheck(!memcmp(channel,note,5),"periodic sound caller moved channel refill boundary");
    }
    memCpyBanked(channel,sound.chNoise+175,sound.soundBank,2);
    auditCheck(channel[0]==255 && channel[1]==255,"periodic sound lost channel terminator");
    auditCheck(b10BankedDealloc(sound.soundResource,sound.soundBank),"precomputed sound allocation cleanup");
}

static void whiteNoiseDurationCase(void)
{
    SoundFile sound;
    unsigned offsets[4]={8,10,12,364}, i;
    byte note[5]={2,0,0,4,15}, savedR0[2];
    unsigned long size=600, beats=70;
    sound.soundResource=b10BankedAlloc(600,&sound.soundBank);
    sound.ch0=sound.soundResource+8;
    sound.ch1=sound.soundResource+10;
    sound.ch2=sound.soundResource+12;
    sound.chNoise=sound.soundResource+364;
    memsetBanked(sound.soundResource,255,600,sound.soundBank);
    for(i=0;i<70;++i) memCpyBanked(sound.ch2+i*5,note,sound.soundBank,5);
    for(i=0;i<35;++i) memCpyBanked(sound.chNoise+i*5,note,sound.soundBank,5);
    memCpyBanked((byte*)&totalSoundSize,(byte*)&size,SOUND_BANK,sizeof(size));
    memCpyBanked((byte*)&totalBeats,(byte*)&beats,SOUND_BANK,sizeof(beats));
    /* The broken cast reads KERNAL r0 at address 2 instead of this note.
     * Keep unrelated KERNAL IRQ work from changing that fixture register. */
    asm("sei");
    savedR0[0]=*(volatile byte*)2; savedR0[1]=*(volatile byte*)3;
    *(volatile byte*)2=0; *(volatile byte*)3=0;
    bBPreComputePeriodicSound(&sound,offsets);
    *(volatile byte*)2=savedR0[0]; *(volatile byte*)3=savedR0[1];
    asm("cli");
    auditCheck(GOLDEN_RAM_WORK_AREA[0]==0,"white noise used a zero-page word instead of note duration");
    auditCheck(b10BankedDealloc(sound.soundResource,sound.soundBank),"white-noise duration allocation cleanup");
}

void soundAuditCases(void)
{
    resizeReturnCase();
    refillCase();
    growthCase();
    auditOutput("sound precompute channel-boundary case; ");
    precomputeCase();
    whiteNoiseDurationCase();
    auditOutput("sound audit cases passed; ");
}
