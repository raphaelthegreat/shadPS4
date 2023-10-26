#include <cmath>
#include <cstdlib>
#include <cstring>

#include "common/debug.h"
#include "core/hle/error_codes.h"
#include "core/hle/libraries/libc/libc.h"
#include "core/hle/libraries/libc/libc_cxa.h"
#include "core/hle/libraries/libs.h"

namespace Core::Libraries::LibC {

static u32 g_need_sceLibc = 1;

static PS4_SYSV_ABI void init_env()  // every game/demo should probably
{
    // dummy no need atm
}

static PS4_SYSV_ABI void catchReturnFromMain(int status) {
    // dummy
}

static PS4_SYSV_ABI void _Assert() { BREAKPOINT(); }

PS4_SYSV_ABI int puts(const char* s) {
    std::puts(s);
    return SCE_OK;
}

PS4_SYSV_ABI int rand() { return std::rand(); }

PS4_SYSV_ABI void _ZdlPv(void* ptr) { std::free(ptr); }
PS4_SYSV_ABI void _ZSt11_Xbad_allocv() { BREAKPOINT(); }
PS4_SYSV_ABI void _ZSt14_Xlength_errorPKc() { BREAKPOINT(); }
PS4_SYSV_ABI void* _Znwm(u64 count) {
    if (count == 0) {
        BREAKPOINT();
    }
    void* ptr = std::malloc(count);
    return ptr;
}

float PS4_SYSV_ABI _Fsin(float arg) { return std::sinf(arg); }

typedef int(PS4_SYSV_ABI* pfunc_QsortCmp)(const void*, const void*);
thread_local static pfunc_QsortCmp compair_ps4;

int qsort_compair(const void* arg1, const void* arg2) { return compair_ps4(arg1, arg2); }

void PS4_SYSV_ABI qsort(void* ptr, size_t count,size_t size, int(PS4_SYSV_ABI* comp)(const void*, const void*)) {
    compair_ps4 = comp;
    std::qsort(ptr, count, size, qsort_compair);
}

PS4_SYSV_ABI int printf(VA_ARGS) {
    VA_CTX(ctx);
    return printf_ctx(&ctx);
}

int PS4_SYSV_ABI vsnprintf(char* s, size_t n, const char* format, VaList* arg) { return vsnprintf_ctx(s, n, format, arg); }

PS4_SYSV_ABI void exit(int code) { std::exit(code); }

PS4_SYSV_ABI int atexit(void (*func)()) {
    int rt = std::atexit(func);
    if (rt != 0) {
        BREAKPOINT();
    }
    return rt;
}

int PS4_SYSV_ABI memcmp(const void* s1, const void* s2, size_t n) { return std::memcmp(s1, s2, n); }

void* PS4_SYSV_ABI memcpy(void* dest, const void* src, size_t n) { return std::memcpy(dest, src, n); }

void* PS4_SYSV_ABI memset(void* s, int c, size_t n) { return std::memset(s, c, n); }

void* PS4_SYSV_ABI malloc(size_t size) { return std::malloc(size); }

void PS4_SYSV_ABI free(void* ptr) { std::free(ptr); }

int PS4_SYSV_ABI strcmp(const char* str1, const char* str2) { return std::strcmp(str1, str2); }

size_t PS4_SYSV_ABI strlen(const char* str) { return std::strlen(str); }

char* PS4_SYSV_ABI strncpy(char* dest, const char* src, size_t count) { return std::strncpy(dest, src, count); }

void* PS4_SYSV_ABI memmove(void* dest, const void* src, std::size_t count) { return std::memmove(dest, src, count); }

char* PS4_SYSV_ABI strcpy(char* dest, const char* src) { return std::strcpy(dest, src); }

char* PS4_SYSV_ABI strcat(char* dest, const char* src) { return std::strcat(dest, src); }

// math
float PS4_SYSV_ABI atan2f(float y, float x) { return std::atan2f(y, x); }

float PS4_SYSV_ABI acosf(float num) { return std::acosf(num); }

float PS4_SYSV_ABI tanf(float num) { return std::tanf(num); }

float PS4_SYSV_ABI asinf(float num) { return std::asinf(num); }

double PS4_SYSV_ABI pow(double base, double exponent) { return std::pow(base, exponent); }

double PS4_SYSV_ABI _Sin(double x) { return std::sin(x); }

void libC_Register(Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("bzQExy189ZI", "libc", 1, "libc", 1, 1, init_env);
    LIB_FUNCTION("3GPpjQdAMTw", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::__cxa_guard_acquire);
    LIB_FUNCTION("9rAeANT2tyE", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::__cxa_guard_release);
    LIB_FUNCTION("2emaaluWzUw", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::__cxa_guard_abort);
    LIB_FUNCTION("DfivPArhucg", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::memcmp);
    LIB_FUNCTION("Q3VBxCXhUHs", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::memcpy);
    LIB_FUNCTION("8zTFvBIAIN8", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::memset);
    LIB_FUNCTION("XKRegsFpEpk", "libc", 1, "libc", 1, 1, catchReturnFromMain);
    LIB_FUNCTION("uMei1W9uyNo", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::exit);
    LIB_FUNCTION("8G2LB+A3rzg", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::atexit);
    LIB_FUNCTION("-QgqOT5u2Vk", "libc", 1, "libc", 1, 1, _Assert);
    LIB_FUNCTION("hcuQgD53UxM", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::printf);
    LIB_FUNCTION("Q2V+iqvjgC0", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::vsnprintf);
    LIB_FUNCTION("YQ0navp+YIc", "libc", 1, "libc", 1, 1, puts);
    LIB_FUNCTION("cpCOXWMgha0", "libc", 1, "libc", 1, 1, rand);
    LIB_FUNCTION("ZtjspkJQ+vw", "libc", 1, "libc", 1, 1, _Fsin);
    LIB_FUNCTION("AEJdIVZTEmo", "libc", 1, "libc", 1, 1, qsort);
    LIB_FUNCTION("Ovb2dSJOAuE", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::strcmp);
    LIB_FUNCTION("gQX+4GDQjpM", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::malloc);
    LIB_FUNCTION("tIhsqj0qsFE", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::free);
    LIB_FUNCTION("j4ViWNHEgww", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::strlen);
    LIB_FUNCTION("6sJWiWSRuqk", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::strncpy);
    LIB_FUNCTION("+P6FRGH4LfA", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::memmove);
    LIB_FUNCTION("kiZSXIWd9vg", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::strcpy);
    LIB_FUNCTION("Ls4tzzhimqQ", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::strcat);
    LIB_FUNCTION("EH-x713A99c", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::atan2f);
    LIB_FUNCTION("QI-x0SL8jhw", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::acosf);
    LIB_FUNCTION("ZE6RNL+eLbk", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::tanf);
    LIB_FUNCTION("GZWjF-YIFFk", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::asinf);
    LIB_FUNCTION("9LCjpWyQ5Zc", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::pow);
    LIB_FUNCTION("cCXjU72Z0Ow", "libc", 1, "libc", 1, 1, Core::Libraries::LibC::_Sin);

    LIB_OBJ("P330P3dFF68", "libc", 1, "libc", 1, 1, &g_need_sceLibc);

    LIB_FUNCTION("z+P+xCnWLBk", "libc", 1, "libc", 1, 1, _ZdlPv);
    LIB_FUNCTION("eT2UsmTewbU", "libc", 1, "libc", 1, 1, _ZSt11_Xbad_allocv);
    LIB_FUNCTION("tQIo+GIPklo", "libc", 1, "libc", 1, 1, _ZSt14_Xlength_errorPKc);
    LIB_FUNCTION("fJnpuVVBbKk", "libc", 1, "libc", 1, 1, _Znwm);
}

};  // namespace Core::Libraries
