// Crash handler, OSPanic on PC, the trace state it prints, and thread backtraces
// (docs/NATIVE_PORT_PHASE4_6.md, step 6.0).
//
// - SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP and SIGABRT go to one sigaction handler (on an
//   alternate stack for the thread that installed it, so a main-thread stack overflow still
//   reports). It prints the signal, the fault address (and a hint when it is below 4 GiB: on
//   macOS arm64 the 4 GiB __PAGEZERO makes every pointer truncated to 32 bits fault there), the
//   scene, game frame, retrace count and last resource, the registers, the image load address
//   (for atos), and a symbolised backtrace (backtrace_symbols_fd over the faulting pc, lr and the
//   frame-pointer chain of the interrupted context). Everything goes to stderr and to
//   <COS_RUN_DIR>/backtrace.txt; then exit 13. native/tools/run.sh adds atos file:line names.
// - On Linux x86-64, an integer divide by zero (and INT_MIN / -1) does not crash: the PowerPC's
//   divw does not trap and the Switch's sdiv gives 0, and game code relies on that (Gohma's first
//   turn in Dragon Roost divides by a scale of 0). The handler gives the idiv/div a quotient of 0
//   (INT_MIN for INT_MIN / -1, as sdiv) and remainder = dividend - quotient * divisor, steps over
//   the instruction and logs the site once.
// - pc_panic (called by OSPanic on PC) prints the same state and backtrace, then exits 12.
// - dumpAllThreads (the stall report) suspends every other thread and walks its frame chain.
// Memory is read with vm_read_overwrite, so a corrupt frame chain cannot fault the handler.
// On the Switch (phase 7) there are no signals: libnx's exception handler in switch/native/source
// writes the crash report and calls writeState through cos_switch_set_crash_state_writer; pc_panic
// and the frame walk read memory through svcQueryMemory, and frames are printed as offsets into
// switchwaker.elf for addr2line (there is no backtrace_symbols_fd).
// No Dusklight code: borealis::crash is not available there.
#include "pc_internal.h"

#include <dolphin/vi.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#if defined(__SWITCH__)
#include "cos_switch.h"
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif
#if defined(__linux__)
#include <link.h>
#include <sys/uio.h>
#endif

namespace pc {

namespace {

constexpr int kMaxFrames = 64;

// Trace state, written by the game threads, read by the crash handler.
std::atomic<int> sScene{-1};
char sLastRes[256];
std::atomic<int> sLastResEntry{-1};
std::atomic<unsigned int> sResSeq{0};

const char* sceneName(int procName) {
    // f_pc_name.h: the scene process names (fpcNm_*_SCENE_e) and the room scene.
    static const char* const kNames[] = {
        "OVERLAP0", "OVERLAP1", "OVERLAP6", "OVERLAP7", "OVERLAP8", "LOGO_SCENE", "MENU_SCENE",
        "PLAY_SCENE", "OPENING_SCENE", "OPENING2_SCENE", "TITLE_SCENE", "ENDING_SCENE",
        "NAME_SCENE", "NAMEEX_SCENE", "OPEN_SCENE", "OPEN2_SCENE", "OVERLAP2", "OVERLAP3",
        "OVERLAP4", "OVERLAP5", "ROOM_SCENE",
    };
    if (procName >= 0 && procName < (int)(sizeof(kNames) / sizeof(kNames[0]))) {
        return kNames[procName];
    }
    return procName < 0 ? "none" : "?";
}

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGTRAP: return "SIGTRAP";
    case SIGABRT: return "SIGABRT";
    default: return "signal";
    }
}

// Reads one pointer-sized word without faulting; false if the address is not readable.
bool readWord(uintptr_t addr, uintptr_t* out) {
    if (addr == 0 || (addr & (sizeof(uintptr_t) - 1)) != 0) {
        return false;
    }
#if defined(__APPLE__)
    vm_size_t got = 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)addr, sizeof(uintptr_t),
                                         (vm_address_t)out, &got);
    return kr == KERN_SUCCESS && got == sizeof(uintptr_t);
#elif defined(__SWITCH__)
    return cos_switch_read_word(addr, out) != 0;
#elif defined(__linux__)
    // process_vm_readv on our own pid fails with EFAULT instead of faulting on an unmapped address.
    struct iovec local = {out, sizeof(uintptr_t)};
    struct iovec remote = {(void*)addr, sizeof(uintptr_t)};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)sizeof(uintptr_t);
#else
    *out = *(const uintptr_t*)addr;
    return true;
#endif
}

// pc, then (if it is not the first return address) lr, then the return addresses of the frame
// record chain starting at fp (arm64/x86-64: [fp] = caller's fp, [fp + 8] = return address).
int collectFrames(uintptr_t pc, uintptr_t lr, uintptr_t fp, void** frames, int max) {
    int n = 0;
    if (pc != 0 && n < max) {
        frames[n++] = (void*)pc;
    }
    uintptr_t firstRet = 0;
    readWord(fp + sizeof(uintptr_t), &firstRet);
    // lr is a caller only while the function has not yet saved it in a frame record (a leaf, or
    // a fault in the prologue); once it calls something, lr is a stale address in the function
    // itself. So lr is listed only when it is not the first chain entry and lies in another
    // function than pc.
    if (lr != 0 && lr != firstRet && n < max) {
#if defined(__SWITCH__)
        const bool samePlace = false; // no dladdr: lr is listed, possibly stale
#else
        Dl_info pcInfo = {};
        Dl_info lrInfo = {};
        bool samePlace = dladdr((void*)pc, &pcInfo) != 0 && dladdr((void*)lr, &lrInfo) != 0 &&
                         pcInfo.dli_saddr == lrInfo.dli_saddr;
#endif
        if (!samePlace) {
            frames[n++] = (void*)lr;
        }
    }
    for (int depth = 0; fp != 0 && n < max && depth < 256; depth++) {
        uintptr_t next = 0;
        uintptr_t ret = 0;
        if (!readWord(fp, &next) || !readWord(fp + sizeof(uintptr_t), &ret)) {
            break;
        }
        if (ret == 0) {
            break;
        }
#if defined(__aarch64__)
        ret &= 0x0000FFFFFFFFFFFFull; // strip a pointer authentication code, if any
#endif
        frames[n++] = (void*)ret;
        if (next <= fp) {
            break; // the chain must go up the stack
        }
        fp = next;
    }
    return n;
}

void writeImageBase(int fd) {
#if defined(__APPLE__)
    // Image 0 is the executable: atos -o switchwaker -l <load> <addresses> symbolises the frames.
    const struct mach_header* header = _dyld_get_image_header(0);
    writef(fd, "[cos] image %s load=0x%llx slide=0x%llx\n", _dyld_get_image_name(0),
           (unsigned long long)(uintptr_t)header, (unsigned long long)_dyld_get_image_vmaddr_slide(0));
#elif defined(__linux__)
    // The executable's load address (PIE): addr2line -f -C -i -e switchwaker <address - load>.
    struct Base {
        uintptr_t addr = 0;
        const char* name = nullptr;
    } base;
    dl_iterate_phdr(
        [](struct dl_phdr_info* phdr, size_t, void* data) -> int {
            auto* b = (Base*)data;
            b->addr = (uintptr_t)phdr->dlpi_addr;
            b->name = phdr->dlpi_name;
            return 1; // the first object is the executable
        },
        &base);
    writef(fd, "[cos] image %s load=0x%llx: addr2line -f -C -i -e switchwaker <address - load>\n",
           base.name != nullptr && base.name[0] != '\0' ? base.name : "(main)",
           (unsigned long long)base.addr);
#elif defined(__SWITCH__)
    // The frames below are printed as offsets into switchwaker.elf, the ELF next to the NRO.
    writef(fd, "[cos] image base=0x%llx: aarch64-none-elf-addr2line -f -C -i -e switchwaker.elf <offset>\n",
           (unsigned long long)cos_switch_image_base());
#else
    (void)fd;
#endif
}

// frames[0, exact) are program counters, the rest return addresses. A return address is printed
// as its call site (address - 1): after a call to a noreturn function the return address is
// already the next function's first instruction, and atos/file:line then name the wrong line.
void writeFrames(int fd, void** frames, int n, int exact) {
    void* sites[kMaxFrames];
    if (n > kMaxFrames) {
        n = kMaxFrames;
    }
    writef(fd, "[cos] frames (pc, then call sites):");
    for (int i = 0; i < n; i++) {
        uintptr_t a = (uintptr_t)frames[i];
        if (i >= exact && a != 0) {
            a -= 1;
        }
        sites[i] = (void*)a;
        writef(fd, " 0x%llx", (unsigned long long)a);
    }
    writef(fd, "\n");
#if defined(__SWITCH__)
    const uintptr_t base = cos_switch_image_base();
    for (int i = 0; i < n; i++) {
        const uintptr_t a = (uintptr_t)sites[i];
        if (a >= base && a - base < (1ull << 30)) {
            writef(fd, "[cos]   #%d switchwaker.elf+0x%llx\n", i, (unsigned long long)(a - base));
        } else {
            writef(fd, "[cos]   #%d 0x%llx\n", i, (unsigned long long)a);
        }
    }
#else
    backtrace_symbols_fd(sites, n, fd);
#endif
}

void writeThreadName(int fd) {
#if defined(__SWITCH__)
    writef(fd, "[cos] thread %llu\n", (unsigned long long)cos_switch_thread_id());
#else
    char name[64] = "";
    pthread_getname_np(pthread_self(), name, sizeof(name));
    uint64_t tid = 0;
#if defined(__APPLE__)
    pthread_threadid_np(nullptr, &tid);
#elif defined(__linux__)
    tid = (uint64_t)gettid();
#endif
    writef(fd, "[cos] thread %llu \"%s\"\n", (unsigned long long)tid, name);
#endif
}

#if !defined(__SWITCH__)
void crashReport(int fd, int sig, siginfo_t* info, ucontext_t* uc) {
    uintptr_t addr = (uintptr_t)info->si_addr;
    writef(fd, "[cos] CRASH %s (%d) code=%d addr=0x%llx\n", signalName(sig), sig, info->si_code,
           (unsigned long long)addr);
    if ((sig == SIGSEGV || sig == SIGBUS) && addr != 0 && addr < 0x100000000ull) {
        writef(fd, "[cos] hint: fault address below 4 GiB (__PAGEZERO): a pointer truncated to "
                   "32 bits, or a 32-bit offset used as a pointer\n");
    }
    writef(fd, "[cos] state: ");
    writeState(fd);
    writeThreadName(fd);
    writeImageBase(fd);

    void* frames[kMaxFrames];
    int n = 0;
#if defined(__APPLE__) && defined(__aarch64__)
    if (uc != nullptr && uc->uc_mcontext != nullptr) {
        const auto& ss = uc->uc_mcontext->__ss;
        uintptr_t pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(ss);
        uintptr_t lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(ss);
        uintptr_t fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(ss);
        uintptr_t sp = (uintptr_t)__darwin_arm_thread_state64_get_sp(ss);
        writef(fd, "[cos] pc=0x%llx lr=0x%llx fp=0x%llx sp=0x%llx\n", (unsigned long long)pc,
               (unsigned long long)lr, (unsigned long long)fp, (unsigned long long)sp);
        for (int r = 0; r < 29; r++) {
            writef(fd, "%sx%d=0x%llx%s", (r % 4) == 0 ? "[cos]   " : " ", r,
                   (unsigned long long)ss.__x[r], (r % 4) == 3 || r == 28 ? "\n" : "");
        }
        n = collectFrames(pc, lr, fp, frames, kMaxFrames);
    }
#elif defined(__linux__) && defined(__aarch64__)
    if (uc != nullptr) {
        const auto& mc = uc->uc_mcontext;
        const uintptr_t pc = (uintptr_t)mc.pc;
        const uintptr_t lr = (uintptr_t)mc.regs[30];
        const uintptr_t fp = (uintptr_t)mc.regs[29];
        writef(fd, "[cos] pc=0x%llx lr=0x%llx fp=0x%llx sp=0x%llx\n", (unsigned long long)pc,
               (unsigned long long)lr, (unsigned long long)fp, (unsigned long long)mc.sp);
        for (int r = 0; r < 29; r++) {
            writef(fd, "%sx%d=0x%llx%s", (r % 4) == 0 ? "[cos]   " : " ", r,
                   (unsigned long long)mc.regs[r], (r % 4) == 3 || r == 28 ? "\n" : "");
        }
        n = collectFrames(pc, lr, fp, frames, kMaxFrames);
    }
#elif defined(__linux__) && defined(__x86_64__)
    if (uc != nullptr) {
        const greg_t* g = uc->uc_mcontext.gregs;
        const uintptr_t pc = (uintptr_t)g[REG_RIP];
        const uintptr_t fp = (uintptr_t)g[REG_RBP];
        writef(fd, "[cos] rip=0x%llx rbp=0x%llx rsp=0x%llx\n", (unsigned long long)pc,
               (unsigned long long)fp, (unsigned long long)g[REG_RSP]);
        writef(fd, "[cos]   rax=0x%llx rbx=0x%llx rcx=0x%llx rdx=0x%llx rsi=0x%llx rdi=0x%llx\n",
               (unsigned long long)g[REG_RAX], (unsigned long long)g[REG_RBX],
               (unsigned long long)g[REG_RCX], (unsigned long long)g[REG_RDX],
               (unsigned long long)g[REG_RSI], (unsigned long long)g[REG_RDI]);
        // No link register: the return address is the first frame record's.
        n = collectFrames(pc, 0, fp, frames, kMaxFrames);
    }
#else
    (void)uc;
#endif
    int exact = 1;
    if (n == 0) {
        n = backtrace(frames, kMaxFrames);
        exact = 0;
    }
    writeFrames(fd, frames, n, exact);
}

#if defined(__linux__) && defined(__x86_64__)
uintptr_t sImageBase = 0;

// The general register numbered as in ModRM/SIB (0 rax ... 15 r15).
uint64_t gpr(const greg_t* g, int n) {
    static const int kRegs[16] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP,
                                  REG_RSI, REG_RDI, REG_R8,  REG_R9,  REG_R10, REG_R11,
                                  REG_R12, REG_R13, REG_R14, REG_R15};
    return (uint64_t)g[kRegs[n]];
}

// Decodes the div/idiv at pc: its operand size in bytes (1, 2, 4 or 8), length and divisor
// (sign-extended for idiv), or 0 if the bytes at pc are not one.
int decodeDivide(const greg_t* g, const uint8_t* pc, int* length, int64_t* divisor) {
    const uint8_t* p = pc;
    bool opSize16 = false;
    for (int i = 0; i < 4; i++) {
        const uint8_t b = *p;
        if (b == 0x66) {
            opSize16 = true;
        } else if (b != 0x67 && b != 0xF0 && b != 0xF2 && b != 0xF3 && b != 0x2E && b != 0x36 &&
                   b != 0x3E && b != 0x26 && b != 0x64 && b != 0x65) {
            break;
        }
        p++;
    }
    uint8_t rex = 0;
    if ((*p & 0xF0) == 0x40) {
        rex = *p++;
    }
    const uint8_t opcode = *p++;
    if (opcode != 0xF6 && opcode != 0xF7) {
        return 0;
    }
    const uint8_t modrm = *p++;
    const int mod = modrm >> 6, reg = (modrm >> 3) & 7, rm = modrm & 7;
    if (reg != 6 && reg != 7) {
        return 0; // not div / idiv
    }
    const int size = opcode == 0xF6 ? 1 : (rex & 0x08) ? 8 : opSize16 ? 2 : 4;
    uint64_t value;
    if (mod == 3) {
        const int n = rm | ((rex & 0x01) << 3);
        if (size == 1 && rex == 0 && rm >= 4) {
            value = gpr(g, rm - 4) >> 8; // ah, ch, dh, bh
        } else {
            value = gpr(g, n);
        }
    } else {
        uint64_t addr = 0;
        bool ripRelative = false;
        if (rm == 4) {
            const uint8_t sib = *p++;
            const int scale = sib >> 6, index = ((sib >> 3) & 7) | ((rex & 0x02) << 2);
            const int base = (sib & 7) | ((rex & 0x01) << 3);
            if (index != 4) {
                addr += gpr(g, index) << scale;
            }
            if (mod == 0 && (sib & 7) == 5) {
                addr += (uint64_t)(int64_t) * (const int32_t*)p;
                p += 4;
            } else {
                addr += gpr(g, base);
            }
        } else if (mod == 0 && rm == 5) {
            addr = (uint64_t)(int64_t) * (const int32_t*)p;
            p += 4;
            ripRelative = true;
        } else {
            addr = gpr(g, rm | ((rex & 0x01) << 3));
        }
        if (mod == 1) {
            addr += (uint64_t)(int64_t) * (const int8_t*)p;
            p += 1;
        } else if (mod == 2) {
            addr += (uint64_t)(int64_t) * (const int32_t*)p;
            p += 4;
        }
        if (ripRelative) {
            addr += (uint64_t)(uintptr_t)p; // relative to the next instruction (no immediate)
        }
        // The divide faulted on its result, not on reading this operand, so it is readable.
        const uint8_t* m = (const uint8_t*)(uintptr_t)addr;
        value = size == 1 ? *m : size == 2 ? *(const uint16_t*)m : size == 4 ? *(const uint32_t*)m
                                                                           : *(const uint64_t*)m;
    }
    if (size < 8) {
        value &= (1ull << (size * 8)) - 1;
    }
    if (reg == 7 && size < 8 && (value >> (size * 8 - 1)) != 0) {
        value |= ~0ull << (size * 8); // idiv: sign-extend
    }
    *length = (int)(p - pc);
    *divisor = (int64_t)value;
    return size;
}

// Finishes an integer divide that trapped as the PowerPC and the Switch would have; false if the
// instruction is not a div/idiv (then it is a crash).
bool recoverIntDivide(int sig, siginfo_t* info, ucontext_t* uc) {
    // Linux reports both a zero divisor and INT_MIN / -1 as FPE_INTDIV (the CPU raises #DE for
    // both); the divisor tells them apart.
    if (sig != SIGFPE || uc == nullptr || (info->si_code != FPE_INTDIV && info->si_code != FPE_INTOVF)) {
        return false;
    }
    greg_t* g = uc->uc_mcontext.gregs;
    const uint8_t* pc = (const uint8_t*)g[REG_RIP];
    int length = 0;
    int64_t divisor = 0;
    const int size = decodeDivide(g, pc, &length, &divisor);
    if (size == 0) {
        return false;
    }
    // Divide by zero: quotient 0, remainder the dividend. Overflow (INT_MIN / -1): quotient
    // INT_MIN, remainder 0. Both are the dividend's low part in one of the two.
    const bool byZero = divisor == 0;
    if (!byZero && divisor != -1) {
        return false; // a quotient overflow C code does not make
    }
    const uint64_t rax = (uint64_t)g[REG_RAX];
    const uint64_t rdx = (uint64_t)g[REG_RDX];
    switch (size) {
    case 1: {
        const uint64_t al = rax & 0xFF;
        const uint64_t quot = byZero ? 0 : al, rem = byZero ? al : 0;
        g[REG_RAX] = (greg_t)((rax & ~0xFFFFull) | (rem << 8) | quot);
        break;
    }
    case 2: {
        const uint64_t ax = rax & 0xFFFF;
        g[REG_RAX] = (greg_t)((rax & ~0xFFFFull) | (byZero ? 0 : ax));
        g[REG_RDX] = (greg_t)((rdx & ~0xFFFFull) | (byZero ? ax : 0));
        break;
    }
    case 4: {
        const uint64_t eax = rax & 0xFFFFFFFFull;
        g[REG_RAX] = (greg_t)(byZero ? 0 : eax);
        g[REG_RDX] = (greg_t)(byZero ? eax : 0);
        break;
    }
    default:
        g[REG_RAX] = (greg_t)(byZero ? 0 : rax);
        g[REG_RDX] = (greg_t)(byZero ? rax : 0);
        break;
    }
    g[REG_RIP] = (greg_t)(pc + length);

    // One line per site (the first 32), so a divide that runs every frame does not fill the log.
    static std::atomic<uintptr_t> sSites[32];
    const uintptr_t site = (uintptr_t)pc;
    for (auto& slot : sSites) {
        uintptr_t seen = slot.load();
        if (seen == site) {
            return true;
        }
        if (seen == 0 && slot.compare_exchange_strong(seen, site)) {
            writef(STDERR_FILENO,
                   "[cos] %s at switchwaker+0x%llx: quotient %s, as on the GameCube and the Switch "
                   "(scene %d)\n",
                   byZero ? "integer divide by zero" : "integer divide overflow",
                   (unsigned long long)(site - sImageBase), byZero ? "0" : "the dividend",
                   sScene.load());
            return true;
        }
        if (slot.load() == site) {
            return true;
        }
    }
    return true;
}
#endif

void crashHandler(int sig, siginfo_t* info, void* context) {
#if defined(__linux__) && defined(__x86_64__)
    if (recoverIntDivide(sig, info, (ucontext_t*)context)) {
        return;
    }
#endif
    // Only one thread reports; pc_exit makes any later caller wait.
    static std::atomic<int> sInHandler{0};
    int expected = 0;
    if (!sInHandler.compare_exchange_strong(expected, 1)) {
        for (;;) {
            pause();
        }
    }
    ucontext_t* uc = (ucontext_t*)context;
    crashReport(STDERR_FILENO, sig, info, uc);
    int fd = openRunFile("backtrace.txt");
    if (fd >= 0) {
        crashReport(fd, sig, info, uc);
        close(fd);
    }
    pc_exit(PC_EXIT_SIGNAL);
}
#endif // !__SWITCH__

} // namespace

void writef(int fd, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0) {
        return;
    }
    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    const char* p = buf;
    while (len > 0) {
        ssize_t w = write(fd, p, (size_t)len);
        if (w <= 0) {
            break;
        }
        p += w;
        len -= (int)w;
    }
}

int openRunFile(const char* name) {
    if (gConfig.runDir == nullptr) {
        return -1;
    }
    char path[1024];
    int len = snprintf(path, sizeof(path), "%s/%s", gConfig.runDir, name);
    if (len < 0 || len >= (int)sizeof(path)) {
        return -1;
    }
    return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

void writeState(int fd) {
    int scene = sScene.load(std::memory_order_relaxed);
    char res[sizeof(sLastRes)];
    // A torn copy is acceptable here: it is diagnostics, and the last byte stays NUL.
    memcpy(res, sLastRes, sizeof(res));
    res[sizeof(res) - 1] = '\0';
    writef(fd, "scene=%s(%d) frame=%u retrace=%u ms=%llu last_res=%s entry=%d res_count=%u\n",
           sceneName(scene), scene, pc_frame_count(), (unsigned int)VIGetRetraceCount(),
           (unsigned long long)elapsedMs(), res[0] != '\0' ? res : "-",
           sLastResEntry.load(std::memory_order_relaxed), sResSeq.load(std::memory_order_relaxed));
}

int traceScene() {
    return sScene.load(std::memory_order_relaxed);
}

const char* traceSceneName(int procName) {
    return sceneName(procName);
}

unsigned int traceResourceCount() {
    return sResSeq.load(std::memory_order_relaxed);
}

void traceLastResource(char* out, size_t size) {
    if (size == 0) {
        return;
    }
    const size_t n = size < sizeof(sLastRes) ? size : sizeof(sLastRes);
    memcpy(out, sLastRes, n);
    out[n - 1] = '\0';
}

void installCrashHandler() {
#if defined(__SWITCH__)
    // libnx's exception handler (switch/native/source/cos_switch.cpp) reports crashes; it adds
    // this state line.
    cos_switch_set_crash_state_writer(writeState);
#else
    // Alternate signal stack for this (the main) thread, so a stack overflow still reports.
    static char sAltStack[256 * 1024];
    stack_t ss = {};
    ss.ss_sp = sAltStack;
    ss.ss_size = sizeof(sAltStack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

#if defined(__linux__) && defined(__x86_64__)
    dl_iterate_phdr(
        [](struct dl_phdr_info* phdr, size_t, void*) -> int {
            sImageBase = (uintptr_t)phdr->dlpi_addr;
            return 1; // the first object is the executable
        },
        nullptr);
#endif
    struct sigaction sa = {};
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};
    for (int sig : signals) {
        sigaction(sig, &sa, nullptr);
    }
#endif
}

void dumpAllThreads(int fd) {
#if defined(__APPLE__)
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) {
        writef(fd, "[cos] task_threads failed\n");
        return;
    }
    mach_port_t self = mach_thread_self();
    writeImageBase(fd);
    for (mach_msg_type_number_t i = 0; i < count; i++) {
        thread_act_t t = threads[i];
        if (t == self) {
            continue;
        }
        thread_suspend(t);
        char name[64] = "";
        pthread_t pt = pthread_from_mach_thread_np(t);
        if (pt != nullptr) {
            pthread_getname_np(pt, name, sizeof(name));
        }
        thread_identifier_info_data_t idInfo = {};
        mach_msg_type_number_t idCount = THREAD_IDENTIFIER_INFO_COUNT;
        thread_info(t, THREAD_IDENTIFIER_INFO, (thread_info_t)&idInfo, &idCount);
        writef(fd, "[cos] thread %llu \"%s\"\n", (unsigned long long)idInfo.thread_id, name);
#if defined(__aarch64__)
        arm_thread_state64_t state = {};
        mach_msg_type_number_t stateCount = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(t, ARM_THREAD_STATE64, (thread_state_t)&state, &stateCount) == KERN_SUCCESS) {
            void* frames[kMaxFrames];
            int n = collectFrames((uintptr_t)__darwin_arm_thread_state64_get_pc(state),
                                  (uintptr_t)__darwin_arm_thread_state64_get_lr(state),
                                  (uintptr_t)__darwin_arm_thread_state64_get_fp(state), frames,
                                  kMaxFrames);
            writeFrames(fd, frames, n, 1);
        }
#endif
        // Left suspended: the process exits right after the report.
    }
    for (mach_msg_type_number_t i = 0; i < count; i++) {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    mach_port_deallocate(mach_task_self(), self);
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(thread_act_t));
#else
    writef(fd, "[cos] thread dump not implemented on this host\n");
#endif
}

} // namespace pc

using namespace pc;

extern "C" {

int pc_trace_enabled(const char* channel) {
    const char* list = gConfig.trace;
    if (list == nullptr) {
        return 0;
    }
    size_t len = strlen(channel);
    for (const char* p = list; *p != '\0';) {
        const char* end = strchr(p, ',');
        size_t n = end != nullptr ? (size_t)(end - p) : strlen(p);
        if ((n == len && strncmp(p, channel, n) == 0) || (n == 3 && strncmp(p, "all", 3) == 0)) {
            return 1;
        }
        if (end == nullptr) {
            break;
        }
        p = end + 1;
    }
    return 0;
}

void pc_trace_scene(int procName) {
    sScene.store(procName, std::memory_order_relaxed);
    if (pc_trace_enabled("scene")) {
        writef(STDERR_FILENO, "[cos] trace scene %s (%d) frame=%u ms=%llu\n", sceneName(procName),
               procName, pc_frame_count(), (unsigned long long)elapsedMs());
    }
}

void pc_trace_resource(const char* path, int entryNum) {
    if (path == nullptr) {
        path = "(null)";
    }
    // Keep the last byte NUL, so a concurrent reader always sees a terminated string.
    strncpy(sLastRes, path, sizeof(sLastRes) - 1);
    sLastResEntry.store(entryNum, std::memory_order_relaxed);
    unsigned int seq = sResSeq.fetch_add(1, std::memory_order_relaxed) + 1;
    if (pc_trace_enabled("res")) {
        writef(STDERR_FILENO, "[cos] trace res #%u %s entry=%d frame=%u ms=%llu\n", seq, path,
               entryNum, pc_frame_count(), (unsigned long long)elapsedMs());
    }
}

void pc_panic(const char* file, int line) {
    void* frames[kMaxFrames];
#if defined(__SWITCH__)
    int n = collectFrames((uintptr_t)__builtin_return_address(0), 0,
                          (uintptr_t)__builtin_frame_address(0), frames, kMaxFrames);
#else
    int n = backtrace(frames, kMaxFrames);
#endif
    for (int pass = 0; pass < 2; pass++) {
        int fd = pass == 0 ? STDERR_FILENO : openRunFile("backtrace.txt");
        if (fd < 0) {
            continue;
        }
        writef(fd, "[cos] PANIC in \"%s\" on line %d\n", file != nullptr ? file : "?", line);
        writef(fd, "[cos] state: ");
        writeState(fd);
        writeThreadName(fd);
        writeImageBase(fd);
        writeFrames(fd, frames, n, 0);
        if (fd != STDERR_FILENO) {
            close(fd);
        }
    }
    pc_exit(PC_EXIT_PANIC);
}

} // extern "C"
