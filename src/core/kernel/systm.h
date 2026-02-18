#pragma once

#include "types.h"

/*
 * Macro to test if we're using a specific version of gcc or later.
 */
#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
#define	__GNUC_PREREQ__(ma, mi)	\
(__GNUC__ > (ma) || __GNUC__ == (ma) && __GNUC_MINOR__ >= (mi))
#else
#define	__GNUC_PREREQ__(ma, mi)	0
#endif

#if __GNUC_PREREQ__(3, 3)
#define __nonnull(x)	__attribute__((__nonnull__(x)))
#else
#define __nonnull(x)
#endif

#if __GNUC_PREREQ__(2, 96)
#define	__malloc_like	__attribute__((__malloc__))
#define	__pure		__attribute__((__pure__))
#else
#define	__malloc_like
#define	__pure
#endif

#if __GNUC_PREREQ__(2, 96)
#define __predict_true(exp)     __builtin_expect((exp), 1)
#define __predict_false(exp)    __builtin_expect((exp), 0)
#else
#define __predict_true(exp)     (exp)
#define __predict_false(exp)    (exp)
#endif

#define	__aligned(x)	__attribute__((__aligned__(x)))

#define	__dead2		__attribute__((__noreturn__))
#define	__printflike(fmtarg, firstvararg) __attribute__((__format__ (__printf__, fmtarg, firstvararg)))

/*
 * If we have already panic'd and this is the thread that called
 * panic(), then don't block on any mutexes but silently succeed.
 * Otherwise, the kernel will deadlock since the scheduler isn't
 * going to run the thread that holds any lock we need.
 */
#define	SCHEDULER_STOPPED() __predict_false(curthread->td_stopsched)


/*
 * Assert that a pointer can be loaded from memory atomically.
 *
 * This assertion enforces stronger alignment than necessary.  For example,
 * on some architectures, atomicity for unaligned loads will depend on
 * whether or not the load spans multiple cache lines.
 */
#define	ASSERT_ATOMIC_LOAD_PTR(var, msg)				\
KASSERT(sizeof(var) == sizeof(void *) &&			\
                                              ((uintptr_t)&(var) & (sizeof(void *) - 1)) == 0, msg)

#define MAXCPU		64

int	copystr(const void * __restrict kfaddr, void * __restrict kdaddr,
            size_t len, size_t * __restrict lencopied)
    __nonnull(1) __nonnull(2);
int	copyinstr(const void * __restrict udaddr, void * __restrict kaddr,
              size_t len, size_t * __restrict lencopied)
    __nonnull(1) __nonnull(2);
int	copyin(const void * __restrict udaddr, void * __restrict kaddr,
           size_t len) __nonnull(1) __nonnull(2);
int	copyin_nofault(const void * __restrict udaddr, void * __restrict kaddr,
                   size_t len) __nonnull(1) __nonnull(2);
int	copyout(const void * __restrict kaddr, void * __restrict udaddr,
            size_t len) __nonnull(1) __nonnull(2);
int	copyout_nofault(const void * __restrict kaddr, void * __restrict udaddr,
                    size_t len) __nonnull(1) __nonnull(2);

int	fubyte(const void *base);
long	fuword(const void *base);
int	fuword16(void *base);
int32_t	fuword32(const void *base);
int64_t	fuword64(const void *base);
int	subyte(void *base, int byte);
int	suword(void *base, long word);
int	suword16(void *base, int word);
int	suword32(void *base, int32_t word);
int	suword64(void *base, int64_t word);
uint32_t casuword32(volatile uint32_t *base, uint32_t oldval, uint32_t newval);
u_long	 casuword(volatile u_long *p, u_long oldval, u_long newval);

void	bcopy(const void *from, void *to, size_t len) __nonnull(1) __nonnull(2);
void	bzero(void *buf, size_t len) __nonnull(1);

void	critical_enter(void);
void	critical_exit(void);

void	panic(const char *, ...) __dead2 __printflike(1, 2);

/* XXX: Should be void nanodelay(u_int nsec); */
void	DELAY(int usec);

extern int cold;		/* nonzero if we are doing a cold boot */
extern const char *panicstr;	/* panic message */

/*
 * Common `proc' functions are declared here so that proc.h can be included
 * less often.
 */
int	_sleep(void *chan, struct lock_object *lock, int pri, const char *wmesg,
           int timo) __nonnull(1);
#define	msleep(chan, mtx, pri, wmesg, timo)				\
_sleep((chan), &(mtx)->lock_object, (pri), (wmesg), (timo))
int	msleep_spin(void *chan, struct mtx *mtx, const char *wmesg, int timo)
    __nonnull(1);
int	pause(const char *wmesg, int timo);
#define	tsleep(chan, pri, wmesg, timo)					\
_sleep((chan), NULL, (pri), (wmesg), (timo))
    void	wakeup(void *chan) __nonnull(1);
void	wakeup_one(void *chan) __nonnull(1);

#define	cpu_spinwait()			__asm __volatile("pause")
