#include "audit.h"
#include "../../sound.h"
#include "../../helpers.h"
#include <stdio.h>
#include <string.h>

#define SOUND_AUDIT_BANK 0x0f
#define SOUND_POOL_BANK 0x01

#pragma wrapped-call (push, trampoline, SOUND_AUDIT_BANK)
extern void soundAuditBankCases(void);
#pragma wrapped-call (pop)
#pragma wrapped-call (push, trampoline, SOUND_POOL_BANK)
extern void soundPoolBankCase(void);
#pragma wrapped-call (pop)

#pragma wrapped-call (push, trampoline, SOUND_BANK)
extern boolean bBFillNoteBuffer(byte* buffer, byte** cursor, BufferStatus* status, byte* note);
extern void bBWriteNextNG(byte** block, byte* buffer, byte** cursor, byte value, BufferStatus* status, unsigned int* size, unsigned int newSize, byte* bank, unsigned int bufferSize);
extern void bBPreComputePeriodicSound(SoundFile* sound, unsigned int* offsets);
extern void bBSetChannelOffsets(byte* code, SoundFile* sound, unsigned int* offsets);
extern void bBPrecomputeValues(SoundFile* sound, unsigned int* offsets);
#pragma wrapped-call (pop)
extern unsigned long totalSoundSize, totalBeats;
extern SoundFile* bBLoadedSoundsPointer[];
extern byte soundLoadCounter, bBChannelsPlaying;
extern byte allocationArray[];

#define CHANNEL_BUFFER_SIZE ((LOCAL_WORK_AREA_SIZE - 2) / 3)

static const byte soundAuditStringText[]={98,97,110,107,101,100,0};

#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")

static void stringReturnCase(void)
{
    byte bank, previous;
    byte* block;
    char first[12], second[12];
    block=b10BankedAlloc(sizeof(soundAuditStringText),&bank);
    memCpyBanked(block,(byte*)soundAuditStringText,bank,sizeof(soundAuditStringText));
    previous=RAM_BANK;
    auditCheck(strcpyBanked(first,(char*)block,bank)==first,"banked string copy returned wrong first destination");
    auditCheck(!memcmp(first,soundAuditStringText,sizeof(soundAuditStringText)) && RAM_BANK==previous,"banked string copy changed contents or caller bank");
    auditCheck(strcpyBanked(second,(char*)block,bank)==second,"banked string copy returned wrong second destination");
    auditCheck(!memcmp(second,soundAuditStringText,sizeof(soundAuditStringText)) && RAM_BANK==previous,"banked string copy changed second contents or caller bank");
    auditCheck(b10BankedDealloc(block,bank),"banked string allocation cleanup");
}

static void resizeReturnCase(void)
{
    byte channel[CHANNEL_BUFFER_SIZE + 2];
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
    byte channel[CHANNEL_BUFFER_SIZE + 2];
    BufferStatus status;
    byte bank, j, note[5], value, *block, *cursor;
    unsigned i;
    block = b10BankedAlloc(CHANNEL_BUFFER_SIZE*2, &bank);
    for (i = 0; i < CHANNEL_BUFFER_SIZE*2; ++i) {
        value=(byte)(i % 127 + 1);
        memCpyBanked(block+i,&value,bank,1);
    }
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
    byte channel[CHANNEL_BUFFER_SIZE + 2];
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
    byte channel[CHANNEL_BUFFER_SIZE + 2];
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

static void lookaheadCase(void)
{
    SoundFile sound;
    unsigned offsets[4]={8,20,1022,1024}, i;
    byte markers[2]={0xff,0xff}, note[5]={1,0,1,0,15};
    byte canary[3]={0xa7,0xa7,0xa7}, actual[3], bank, previous;

    sound.soundResource=b10BankedAlloc(1029,&bank);
    sound.soundBank=bank;
    memsetBanked(sound.soundResource,0,1029,bank);
    memCpyBanked(sound.soundResource,(byte*)offsets,bank,sizeof(offsets));
    /* Channel 0's marker shifts channel 1 so both refill edges split its frequency pair. */
    memCpyBanked(sound.soundResource+8,note,bank,sizeof(note));
    memCpyBanked(sound.soundResource+13,note,bank,sizeof(note));
    memCpyBanked(sound.soundResource+18,markers,bank,sizeof(markers));
    for(i=0;i<200;++i) memCpyBanked(sound.soundResource+20+i*5,note,bank,sizeof(note));
    memCpyBanked(sound.soundResource+1020,markers,bank,sizeof(markers));
    memCpyBanked(sound.soundResource+1022,markers,bank,sizeof(markers));
    memCpyBanked(sound.soundResource+1024,markers,bank,sizeof(markers));
    memCpyBanked(sound.soundResource+1026,canary,bank,sizeof(canary));
    bBSetChannelOffsets(sound.soundResource,&sound,offsets);

    previous=RAM_BANK;
    bBPrecomputeValues(&sound,offsets);
    auditCheck(RAM_BANK==previous,"sound precompute changed caller bank");
    memCpyBanked(actual,sound.ch1+3,sound.soundBank,1);
    auditCheck(actual[0]==0x24,"sound precompute control frequency was not 0x24");
    memCpyBanked(actual,sound.ch0+500,sound.soundBank,1);
    auditCheck(actual[0]==0x24,"sound precompute missed first-refill split frequency high byte");
    memCpyBanked(actual,sound.ch0+1000,sound.soundBank,1);
    auditCheck(actual[0]==0x24,"sound precompute missed second-refill split frequency high byte");
    memCpyBanked(actual,sound.soundResource+1026,sound.soundBank,sizeof(canary));
    auditCheck(!memcmp(actual,canary,sizeof(canary)),"sound precompute overwrote resource end canary");
    auditCheck(b10BankedDealloc(sound.soundResource,sound.soundBank),"sound precompute allocation cleanup");
}

#define SOUND_POOL_FIXTURE_COUNT (MAX_LOADED_SOUNDS + 1)
#define SOUND_POOL_FIXTURE_FIRST 200
#define SOUND_FIXTURE_SIZE 512
#define SOUND_FIXTURE_RECORD_SIZE (SOUND_FIXTURE_SIZE + 5)

#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (push, "BANKRAM01")
#pragma rodata-name (push, "BANKRAM01")

static unsigned soundPoolAllocationCount(void)
{
    byte value;
    unsigned i, count=0;
    for(i=0;i<ALLOCATION_ARRAY_SIZE;++i) {
        memCpyBanked(&value,allocationArray+i,MEMORY_MANAGEMENT_BANK,1);
        count += value != 0;
    }
    return count;
}

void soundPoolBankCase(void)
{
    AGIFilePosType position;
    SoundFile* loadedPointers[SOUND_POOL_FIXTURE_COUNT];
    SoundFile loaded, lastBefore, lastAfter;
    byte resource[31], zeroPad[64];
    byte counter, active, duration, oldSoundEnabled, oldCompletion, fixtureBank, i;
    byte bankZero=0;
    byte* oldEntries, *lastResource;
    FILE* volume;
    unsigned j, allocations;

    memCpyBanked(&counter,(byte*)&soundLoadCounter,SOUND_BANK,1);
    auditCheck(counter==0,"sound pool test did not start empty");
    bBInitSound();
    memCpyBanked((byte*)&soundLoadCounter,&bankZero,SOUND_BANK,1);
    oldEntries=b10BankedAlloc(SOUND_POOL_FIXTURE_COUNT*sizeof(AGIFilePosType),&fixtureBank);
    auditCheck(oldEntries!=NULL,"sound pool directory snapshot allocation");
    memCpyBankedBetween(oldEntries,fixtureBank,(byte*)&snddir[SOUND_POOL_FIXTURE_FIRST],DIRECTORY_BANK,SOUND_POOL_FIXTURE_COUNT*sizeof(AGIFilePosType));

    memset(resource,0,sizeof(resource)); memset(zeroPad,0,sizeof(zeroPad));
    resource[0]=0x12; resource[1]=0x34; resource[2]=8;
    resource[3]=(byte)SOUND_FIXTURE_SIZE;
    resource[4]=(byte)(SOUND_FIXTURE_SIZE>>8);
    resource[5]=8; resource[7]=15; resource[9]=17; resource[11]=19;
    volume=fopen("vol.8","wb");
    auditCheck(volume!=NULL,"sound pool volume fixture creation");
    for(i=0;i<SOUND_POOL_FIXTURE_COUNT;++i) {
        memset(&position,0,sizeof(position));
        position.fileNum=8;
        position.filePos=(unsigned long)i*SOUND_FIXTURE_RECORD_SIZE;
        memCpyBanked((byte*)&snddir[SOUND_POOL_FIXTURE_FIRST+i],(byte*)&position,DIRECTORY_BANK,sizeof(position));
        resource[13]=i+1; resource[14]=0; resource[15]=1; resource[16]=0; resource[17]=15;
        for(j=18;j<26;++j) resource[j]=0xff;
        auditCheck(fwrite(resource,1,sizeof(resource),volume)==sizeof(resource),"sound resource fixture write");
        for(j=0;j<7;++j) auditCheck(fwrite(zeroPad,1,sizeof(zeroPad),volume)==sizeof(zeroPad),"sound resource padding write");
        auditCheck(fwrite(zeroPad,1,38,volume)==38,"sound resource tail write");
    }
    fclose(volume);

    for(i=0;i<MAX_LOADED_SOUNDS;++i) bBLoadSoundFile(SOUND_POOL_FIXTURE_FIRST+i);
    memCpyBanked((byte*)loadedPointers,(byte*)&bBLoadedSoundsPointer[SOUND_POOL_FIXTURE_FIRST],SOUND_BANK,MAX_LOADED_SOUNDS*sizeof(*loadedPointers));
    for(i=0;i<MAX_LOADED_SOUNDS;++i) {
        auditCheck(loadedPointers[i]==&bBLoadedSounds[i],"sound IDs did not occupy distinct pool slots");
        memCpyBanked((byte*)&loaded,(byte*)loadedPointers[i],SOUND_BANK,sizeof(loaded));
        memCpyBanked(&duration,loaded.soundResource+8,loaded.soundBank,1);
        auditCheck(duration==i+1,"SOUND ID loaded the wrong volume fixture");
    }

    allocations=soundPoolAllocationCount();
    bBLoadSoundFile(SOUND_POOL_FIXTURE_FIRST+5);
    memCpyBanked((byte*)loadedPointers,(byte*)&bBLoadedSoundsPointer[SOUND_POOL_FIXTURE_FIRST],SOUND_BANK,MAX_LOADED_SOUNDS*sizeof(*loadedPointers));
    auditCheck(loadedPointers[5]==&bBLoadedSounds[5],"loading an existing sound changed its owner");
    auditCheck(allocations==soundPoolAllocationCount(),"loading an existing sound allocated a duplicate");

    memCpyBanked((byte*)&lastBefore,(byte*)loadedPointers[MAX_LOADED_SOUNDS-1],SOUND_BANK,sizeof(lastBefore));
    lastResource=lastBefore.soundResource;
    allocations=soundPoolAllocationCount();
    bBLoadSoundFile(SOUND_POOL_FIXTURE_FIRST+MAX_LOADED_SOUNDS);
    memCpyBanked((byte*)&lastAfter,(byte*)loadedPointers[MAX_LOADED_SOUNDS-1],SOUND_BANK,sizeof(lastAfter));
    auditCheck(lastAfter.soundResource==lastResource,"31st SOUND load overwrote slot 29 owned by the prior ID");
    memCpyBanked((byte*)loadedPointers,(byte*)&bBLoadedSoundsPointer[SOUND_POOL_FIXTURE_FIRST],SOUND_BANK,sizeof(loadedPointers));
    auditCheck(loadedPointers[MAX_LOADED_SOUNDS]==NULL,"full sound pool published an unowned ID");
    memCpyBanked(&counter,(byte*)&soundLoadCounter,SOUND_BANK,1);
    auditCheck(counter==MAX_LOADED_SOUNDS,"sound pool count did not reach its capacity");
    auditCheck(allocations==soundPoolAllocationCount(),"full sound pool failure allocated a resource");

    bBDiscardSoundFile(SOUND_POOL_FIXTURE_FIRST+7);
    bBLoadSoundFile(SOUND_POOL_FIXTURE_FIRST+MAX_LOADED_SOUNDS);
    memCpyBanked((byte*)loadedPointers,(byte*)&bBLoadedSoundsPointer[SOUND_POOL_FIXTURE_FIRST],SOUND_BANK,sizeof(loadedPointers));
    auditCheck(loadedPointers[7]==NULL && loadedPointers[MAX_LOADED_SOUNDS]==&bBLoadedSounds[7],"sound loader did not reuse a discarded pool hole");
    memCpyBanked((byte*)&lastAfter,(byte*)loadedPointers[MAX_LOADED_SOUNDS],SOUND_BANK,sizeof(lastAfter));
    auditCheck(lastAfter.soundResource!=NULL,"reused sound slot was not published after loading");
    memCpyBanked(&duration,lastAfter.soundResource+8,lastAfter.soundBank,1);
    auditCheck(duration==SOUND_POOL_FIXTURE_COUNT,"hole reuse published the wrong SOUND fixture");
    bBLoadSoundFile(SOUND_POOL_FIXTURE_FIRST+MAX_LOADED_SOUNDS);
    memCpyBanked(&counter,(byte*)&soundLoadCounter,SOUND_BANK,1);
    auditCheck(counter==MAX_LOADED_SOUNDS,"sound pool high-water count changed after hole reuse");

    oldSoundEnabled=flag[9]; oldCompletion=flag[42];
    flag[9]=TRUE; flag[42]=FALSE;
    bBPlaySound(SOUND_POOL_FIXTURE_FIRST+SOUND_POOL_FIXTURE_COUNT,42);
    memCpyBanked(&active,(byte*)&bBChannelsPlaying,SOUND_BANK,1);
    auditCheck(flag[42] && !active,"playing a null sound did not finish safely");
    flag[9]=oldSoundEnabled; flag[42]=oldCompletion;

    for(i=0;i<SOUND_POOL_FIXTURE_COUNT;++i) bBDiscardSoundFile(SOUND_POOL_FIXTURE_FIRST+i);
    memCpyBanked(&counter,(byte*)&soundLoadCounter,SOUND_BANK,1);
    auditCheck(counter==0,"sound pool cleanup left occupied slots");
    memCpyBankedBetween((byte*)&snddir[SOUND_POOL_FIXTURE_FIRST],DIRECTORY_BANK,oldEntries,fixtureBank,SOUND_POOL_FIXTURE_COUNT*sizeof(AGIFilePosType));
    auditCheck(b10BankedDealloc(oldEntries,fixtureBank),"sound pool directory snapshot cleanup");
    auditCheck(remove("vol.8")==0,"sound pool volume fixture cleanup");
}

#pragma code-name (pop)
#pragma rodata-name (pop)
#pragma code-name (push, "BANKRAM0F")
#pragma rodata-name (push, "BANKRAM0F")

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

void soundAuditBankCases(void)
{
    stringReturnCase();
    resizeReturnCase();
    refillCase();
    growthCase();
    auditOutput("sound precompute channel-boundary case; ");
    precomputeCase();
    auditOutput("sound multi-refill lookahead case; ");
    lookaheadCase();
    auditOutput("sound bounded pool production-load case; ");
    soundPoolBankCase();
    whiteNoiseDurationCase();
    auditOutput("sound audit cases passed; ");
}

#pragma code-name (pop)
#pragma rodata-name (pop)

void soundAuditCases(void)
{
    soundAuditBankCases();
}
