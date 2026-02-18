/*-
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)time.h	8.5 (Berkeley) 5/4/95
 * $FreeBSD$
 */

#ifndef _SYS_TIME_H_
#define _SYS_TIME_H_

#include "types.h"

#ifndef _SUSECONDS_T_DECLARED
typedef	__suseconds_t	suseconds_t;
#define	_SUSECONDS_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef	__time_t	time_t;
#define	_TIME_T_DECLARED
#endif

/*
 * New in POSIX 1003.1b-1993.
 */
#ifndef _CLOCKID_T_DECLARED
typedef	__clockid_t	clockid_t;
#define	_CLOCKID_T_DECLARED
#endif

#define	TIMEVAL_TO_TIMESPEC(tv, ts)					\
do {								\
        (ts)->tv_sec = (tv)->tv_sec;				\
        (ts)->tv_nsec = (tv)->tv_usec * 1000;			\
} while (0)
#define	TIMESPEC_TO_TIMEVAL(tv, ts)					\
    do {								\
        (tv)->tv_sec = (ts)->tv_sec;				\
        (tv)->tv_usec = (ts)->tv_nsec / 1000;			\
} while (0)


/*
 * Structure returned by gettimeofday(2) system call, and used in other calls.
 */
struct timeval {
    time_t		tv_sec;		/* seconds */
    suseconds_t	tv_usec;	/* and microseconds */
};

struct timespec {
    time_t	tv_sec;		/* seconds */
    long	tv_nsec;	/* and nanoseconds */
};
struct timezone {
    int	tz_minuteswest;	/* minutes west of Greenwich */
    int	tz_dsttime;	/* type of dst correction */
};
#define	DST_NONE	0	/* not on dst */
#define	DST_USA		1	/* USA style dst */
#define	DST_AUST	2	/* Australian style dst */
#define	DST_WET		3	/* Western European dst */
#define	DST_MET		4	/* Middle European dst */
#define	DST_EET		5	/* Eastern European dst */
#define	DST_CAN		6	/* Canada */

/* Operations on timespecs */
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#define	timespeccmp(tvp, uvp, cmp)					\
(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
     ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
     ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define timespecadd(vvp, uvp)						\
    do {								\
        (vvp)->tv_sec += (uvp)->tv_sec;				\
        (vvp)->tv_nsec += (uvp)->tv_nsec;			\
        if ((vvp)->tv_nsec >= 1000000000) {			\
            (vvp)->tv_sec++;				\
            (vvp)->tv_nsec -= 1000000000;			\
    }							\
} while (0)
#define timespecsub(vvp, uvp)						\
    do {								\
            (vvp)->tv_sec -= (uvp)->tv_sec;				\
            (vvp)->tv_nsec -= (uvp)->tv_nsec;			\
            if ((vvp)->tv_nsec < 0) {				\
                (vvp)->tv_sec--;				\
                (vvp)->tv_nsec += 1000000000;			\
        }							\
    } while (0)

/* Operations on timevals. */

#define	timevalclear(tvp)		((tvp)->tv_sec = (tvp)->tv_usec = 0)
#define	timevalisset(tvp)		((tvp)->tv_sec || (tvp)->tv_usec)
#define	timevalcmp(tvp, uvp, cmp)					\
        (((tvp)->tv_sec == (uvp)->tv_sec) ?				\
             ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
             ((tvp)->tv_sec cmp (uvp)->tv_sec))

/* timevaladd and timevalsub are not inlined */

/*
 * Names of the interval timers, and structure
 * defining a timer setting.
 */
#define	ITIMER_REAL	0
#define	ITIMER_VIRTUAL	1
#define	ITIMER_PROF	2

        struct itimerval {
        struct	timeval it_interval;	/* timer interval */
        struct	timeval it_value;	/* current value */
    };

/*
 * Getkerninfo clock information structure
 */
struct clockinfo {
    int	hz;		/* clock frequency */
    int	tick;		/* micro-seconds per hz tick */
    int	spare;
    int	stathz;		/* statistics clock frequency */
    int	profhz;		/* profiling clock frequency */
};

/* These macros are also in time.h. */
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME	0
#define CLOCK_VIRTUAL	1
#define CLOCK_PROF	2
#define CLOCK_MONOTONIC	4
#define CLOCK_UPTIME	5		/* FreeBSD-specific. */
#define CLOCK_UPTIME_PRECISE	7	/* FreeBSD-specific. */
#define CLOCK_UPTIME_FAST	8	/* FreeBSD-specific. */
#define CLOCK_REALTIME_PRECISE	9	/* FreeBSD-specific. */
#define CLOCK_REALTIME_FAST	10	/* FreeBSD-specific. */
#define CLOCK_MONOTONIC_PRECISE	11	/* FreeBSD-specific. */
#define CLOCK_MONOTONIC_FAST	12	/* FreeBSD-specific. */
#define CLOCK_SECOND	13		/* FreeBSD-specific. */
#define CLOCK_THREAD_CPUTIME_ID	14
#endif

#ifndef TIMER_ABSTIME
#define TIMER_RELTIME	0x0	/* relative timer */
#define TIMER_ABSTIME	0x1	/* absolute timer */
#endif

/*
 * Kernel to clock driver interface.
 */
void	inittodr(time_t base);
void	resettodr(void);

extern time_t	time_second;
extern time_t	time_uptime;
extern struct bintime boottimebin;
extern struct timeval boottime;

/*
 * Functions for looking at our clock: [get]{bin,nano,micro}[up]time()
 *
 * Functions without the "get" prefix returns the best timestamp
 * we can produce in the given format.
 *
 * "bin"   == struct bintime  == seconds + 64 bit fraction of seconds.
 * "nano"  == struct timespec == seconds + nanoseconds.
 * "micro" == struct timeval  == seconds + microseconds.
 *
 * Functions containing "up" returns time relative to boot and
 * should be used for calculating time intervals.
 *
 * Functions without "up" returns GMT time.
 *
 * Functions with the "get" prefix returns a less precise result
 * much faster than the functions without "get" prefix and should
 * be used where a precision of 1/hz seconds is acceptable or where
 * performance is priority. (NB: "precision", _not_ "resolution" !)
 *
 */

void	binuptime(struct bintime *bt);
void	nanouptime(struct timespec *tsp);
void	microuptime(struct timeval *tvp);

void	bintime(struct bintime *bt);
void	nanotime(struct timespec *tsp);
void	microtime(struct timeval *tvp);

void	getbinuptime(struct bintime *bt);
void	getnanouptime(struct timespec *tsp);
void	getmicrouptime(struct timeval *tvp);

void	getbintime(struct bintime *bt);
void	getnanotime(struct timespec *tsp);
void	getmicrotime(struct timeval *tvp);

/* Other functions */
int	itimerdecr(struct itimerval *itp, int usec);
int	itimerfix(struct timeval *tv);
int	ppsratecheck(struct timeval *, int *, int);
int	ratecheck(struct timeval *, const struct timeval *);
void	timevaladd(struct timeval *t1, const struct timeval *t2);
void	timevalsub(struct timeval *t1, const struct timeval *t2);
int	tvtohz(struct timeval *tv);

#endif /* !_SYS_TIME_H_ */
