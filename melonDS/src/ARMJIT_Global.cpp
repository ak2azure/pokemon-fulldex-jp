#include "ARMJIT_Global.h"
#include "ARMJIT_Memory.h"

#if defined(__SWITCH__)
// ssrecomp: on the Switch the ARM64 compiler makes its own code memory (ARMJIT_Compiler.cpp).
#elif defined(__PROSPERO__) && defined(SSR_PS5_NATIVE)
// ssrecomp: the PS5 title (frontend/ps5_native.h). A title cannot make its own memory
// executable (mprotect with PROT_EXEC is refused), but direct memory mapped read-write can be
// given execute afterwards: the platform layer of Mihawk's payload SDK fork does that, within
// reach of the program's code, which the compiled code calls with 32-bit displacements.
extern "C" void* ps5_exec_allocate(size_t bytes, uintptr_t anchor);
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdint.h>

#include <mutex>

namespace melonDS
{

namespace ARMJIT_Global
{

std::mutex globalMutex;

#if defined(__APPLE__) && defined(__aarch64__)
#define APPLE_AARCH64
#endif

#if !defined(APPLE_AARCH64) && !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(__SWITCH__)
static constexpr size_t NumCodeMemSlices = 4;
static constexpr size_t CodeMemoryAlignedSize = NumCodeMemSlices * CodeMemorySliceSize;

u32 AvailableCodeMemSlices = (1 << NumCodeMemSlices) - 1;

#if defined(__PROSPERO__) && defined(SSR_PS5_NATIVE)
// (ssrecomp: the slices in one region of direct memory, asked for the first time the JIT
// starts: not 128 MB of the title's program memory, which is far smaller than direct memory.)
u8* CodeMemory = nullptr;

u8* GetAlignedCodeMemoryStart()
{
    return CodeMemory;
}
#else
// I haven't heard of pages larger than 16 KB
u8 CodeMemory[CodeMemoryAlignedSize + 16*1024];

u8* GetAlignedCodeMemoryStart()
{
    return reinterpret_cast<u8*>((reinterpret_cast<intptr_t>(CodeMemory) + (16*1024-1)) & ~static_cast<intptr_t>(16*1024-1));
}
#endif
#endif

int RefCounter = 0;

void* AllocateCodeMem()
{
    std::lock_guard guard(globalMutex);

#if !defined(APPLE_AARCH64) && !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(__SWITCH__)
#if defined(__PROSPERO__) && defined(SSR_PS5_NATIVE)
    if (!CodeMemory)
    {
        CodeMemory = static_cast<u8*>(ps5_exec_allocate(CodeMemoryAlignedSize, reinterpret_cast<uintptr_t>(&AllocateCodeMem)));
        if (!CodeMemory)
        {
            fprintf(stderr, "[ps5] JIT: no code memory\n");
            return nullptr;
        }
    }
#endif
    if (AvailableCodeMemSlices)
    {
        int slice = __builtin_ctz(AvailableCodeMemSlices);
        AvailableCodeMemSlices &= ~(1 << slice);
        //printf("allocating slice %d\n", slice);
        return &GetAlignedCodeMemoryStart()[slice * CodeMemorySliceSize];
    }
#endif

    // allocate
#if defined(__SWITCH__) || (defined(__PROSPERO__) && defined(SSR_PS5_NATIVE))
    return nullptr;
#elif defined(_WIN32)
    return VirtualAlloc(nullptr, CodeMemorySliceSize, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#elif defined(APPLE_AARCH64)
    return mmap(NULL, CodeMemorySliceSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT,-1, 0);
#elif defined(__NetBSD__)
    return mmap(nullptr, CodeMemorySliceSize, PROT_MPROTECT(PROT_READ | PROT_WRITE | PROT_EXEC), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#else
    //printf("mmaping...\n");
    return mmap(nullptr, CodeMemorySliceSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
}

void FreeCodeMem(void* codeMem)
{
    std::lock_guard guard(globalMutex);

#if !defined(APPLE_AARCH64) && !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(__SWITCH__)
    for (int i = 0; i < NumCodeMemSlices; i++)
    {
        if (codeMem == &GetAlignedCodeMemoryStart()[CodeMemorySliceSize * i])
        {
            //printf("freeing slice\n");
            AvailableCodeMemSlices |= 1 << i;
            return;
        }
    }
#endif

#if defined(__SWITCH__)
    (void)codeMem;
#elif defined(_WIN32)
    VirtualFree(codeMem, CodeMemorySliceSize, MEM_RELEASE|MEM_DECOMMIT);
#else
    munmap(codeMem, CodeMemorySliceSize);
#endif
}

void Init()
{
    std::lock_guard guard(globalMutex);

    RefCounter++;
    if (RefCounter == 1)
    {
        #if defined(__SWITCH__) || (defined(__PROSPERO__) && defined(SSR_PS5_NATIVE))
        #elif defined(_WIN32)
            DWORD dummy;
            VirtualProtect(GetAlignedCodeMemoryStart(), CodeMemoryAlignedSize, PAGE_EXECUTE_READWRITE, &dummy);
        #elif defined(APPLE_AARCH64) || defined(__NetBSD__) || defined(__OpenBSD__)
            // Apple aarch64 always uses dynamic allocation
        #else
            mprotect(GetAlignedCodeMemoryStart(), CodeMemoryAlignedSize, PROT_EXEC | PROT_READ | PROT_WRITE);
        #endif

        ARMJIT_Memory::RegisterFaultHandler();
    }
}

void DeInit()
{
    std::lock_guard guard(globalMutex);

    RefCounter--;
    if (RefCounter == 0)
    {
        ARMJIT_Memory::UnregisterFaultHandler();
    }
}

}

}
