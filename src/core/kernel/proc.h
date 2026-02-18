#ifndef PROC_H
#define PROC_H

#include "queue.h"
#include "types.h"
#include "mutex.h"

#define	MAXCOMLEN	19		/* max command name remembered */

/*
 * Kernel runnable context (thread).
 * This is what is put to sleep and reactivated.
 * Thread context.  Processes may have multiple threads.
 */
struct thread {
    struct mtx	*volatile td_lock; /* replaces sched lock */
    struct proc	*td_proc;	/* (*) Associated process. */
    TAILQ_ENTRY(thread) td_plist;	/* (*) All threads in this proc. */
    TAILQ_ENTRY(thread) td_runq;	/* (t) Run queue. */
    TAILQ_ENTRY(thread) td_slpq;	/* (t) Sleep queue. */
    TAILQ_ENTRY(thread) td_lockq;	/* (t) Lock queue. */
    LIST_ENTRY(thread) td_hash;	/* (d) Hash chain. */
    struct cpuset	*td_cpuset;	/* (t) CPU affinity mask. */
    struct seltd	*td_sel;	/* Select queue/channel. */
    struct sleepqueue *td_sleepqueue; /* (k) Associated sleep queue. */
    struct turnstile *td_turnstile;	/* (k) Associated turnstile. */
    struct umtx_q   *td_umtxq;	/* (c?) Link for when we're blocked. */
    lwpid_t		td_tid;		/* (b) Thread ID. */
    //sigqueue_t	td_sigqueue;	/* (c) Sigs arrived, not delivered. */
#define	td_siglist	td_sigqueue.sq_signals
    u_char		td_lend_user_pri; /* (t) Lend user pri. */

/* Cleared during fork1() */
#define	td_startzero td_flags
    int		td_flags;	/* (t) TDF_* flags. */
    int		td_inhibitors;	/* (t) Why can not run. */
    int		td_pflags;	/* (k) Private thread (TDP_*) flags. */
    int		td_dupfd;	/* (k) Ret value from fdopen. XXX */
    int		td_sqqueue;	/* (t) Sleepqueue queue blocked on. */
    void		*td_wchan;	/* (t) Sleep address. */
    const char	*td_wmesg;	/* (t) Reason for sleep. */
    u_char		td_lastcpu;	/* (t) Last cpu we were on. */
    u_char		td_oncpu;	/* (t) Which cpu we are on. */
    volatile u_char td_owepreempt;  /* (k*) Preempt on last critical_exit */
    u_char		td_tsqueue;	/* (t) Turnstile queue blocked on. */
    short		td_locks;	/* (k) Count of non-spin locks. */
    short		td_rw_rlocks;	/* (k) Count of rwlock read locks. */
    short		td_lk_slocks;	/* (k) Count of lockmgr shared locks. */
    short		td_stopsched;	/* (k) Scheduler stopped. */
    struct turnstile *td_blocked;	/* (t) Lock thread is blocked on. */
    const char	*td_lockname;	/* (t) Name of lock blocked on. */
    LIST_HEAD(, turnstile) td_contested;	/* (q) Contested locks. */
    struct lock_list_entry *td_sleeplocks; /* (k) Held sleep locks. */
    int		td_intr_nesting_level; /* (k) Interrupt recursion. */
    int		td_pinned;	/* (k) Temporary cpu pin count. */
    struct ucred	*td_ucred;	/* (k) Reference to credentials. */
    u_int		td_estcpu;	/* (t) estimated cpu utilization */
    int		td_slptick;	/* (t) Time at sleep. */
    int		td_blktick;	/* (t) Time spent blocked. */
    int		td_swvoltick;	/* (t) Time at last SW_VOL switch. */
    u_int		td_cow;		/* (*) Number of copy-on-write faults */
    //struct rusage	td_ru;		/* (t) rusage information. */
    //struct rusage_ext td_rux;	/* (t) Internal rusage information. */
    uint64_t	td_incruntime;	/* (t) Cpu ticks to transfer to proc. */
    uint64_t	td_runtime;	/* (t) How many cpu ticks we've run. */
    u_int 		td_pticks;	/* (t) Statclock hits for profiling */
    u_int		td_sticks;	/* (t) Statclock hits in system mode. */
    u_int		td_iticks;	/* (t) Statclock hits in intr mode. */
    u_int		td_uticks;	/* (t) Statclock hits in user mode. */
    int		td_intrval;	/* (t) Return value for sleepq. */
    //sigset_t	td_oldsigmask;	/* (k) Saved mask from pre sigpause. */
    //sigset_t	td_sigmask;	/* (c) Current signal mask. */
    volatile u_int	td_generation;	/* (k) For detection of preemption */
    //stack_t		td_sigstk;	/* (k) Stack ptr and on-stack flag. */
    int		td_xsig;	/* (c) Signal for ptrace */
    u_long		td_profil_addr;	/* (k) Temporary addr until AST. */
    u_int		td_profil_ticks; /* (k) Temporary ticks until AST. */
    char		td_name[MAXCOMLEN + 1];	/* (*) Thread name. */
    struct file	*td_fpop;	/* (k) file referencing cdev under op */
    int		td_dbgflags;	/* (c) Userland debugger flags */
    //struct ksiginfo td_dbgksi;	/* (c) ksi reflected to debugger. */
    int		td_ng_outbound;	/* (k) Thread entered ng from above. */
    //struct osd	td_osd;		/* (k) Object specific data. */
    struct vm_map_entry *td_map_def_user; /* (k) Deferred entries. */
    //pid_t		td_dbg_forked;	/* (c) Child pid for debugger. */
#define	td_endzero td_rqindex

/* Copied during fork1() or thread_sched_upcall(). */
#define	td_startcopy td_endzero
    u_char		td_rqindex;	/* (t) Run queue index. */
    u_char		td_base_pri;	/* (t) Thread base kernel priority. */
    u_char		td_priority;	/* (t) Thread active priority. */
    u_char		td_pri_class;	/* (t) Scheduling class. */
    u_char		td_user_pri;	/* (t) User pri from estcpu and nice. */
    u_char		td_base_user_pri; /* (t) Base user pri */
#define	td_endcopy td_pcb

    /*
     * Fields that must be manually set in fork1() or thread_sched_upcall()
     * or already have been set in the allocator, constructor, etc.
     */
    struct pcb	*td_pcb;	/* (k) Kernel VA of pcb and kstack. */
    enum {
        TDS_INACTIVE = 0x0,
        TDS_INHIBITED,
        TDS_CAN_RUN,
        TDS_RUNQ,
        TDS_RUNNING
    } td_state;			/* (t) thread state */
    //register_t	td_retval[2];	/* (k) Syscall aux returns. */
    //struct callout	td_slpcallout;	/* (h) Callout for sleep. */
    struct trapframe *td_frame;	/* (k) */
    struct vm_object *td_kstack_obj;/* (a) Kstack object. */
    //vm_offset_t	td_kstack;	/* (a) Kernel VA of kstack. */
    int		td_kstack_pages; /* (a) Size of the kstack. */
    volatile u_int	td_critnest;	/* (k*) Critical section nest level. */
    //struct mdthread td_md;		/* (k) Any machine-dependent fields. */
    struct td_sched	*td_sched;	/* (*) Scheduler-specific data. */
    struct kaudit_record	*td_ar;	/* (k) Active audit record, if any. */
    //struct lpohead	td_lprof[2];	/* (a) lock profiling objects. */
    struct kdtrace_thread	*td_dtrace; /* (*) DTrace-specific data. */
    int		td_errno;	/* Error returned by last syscall. */
    struct vnet	*td_vnet;	/* (k) Effective vnet. */
    const char	*td_vnet_lpush;	/* (k) Debugging vnet push / pop. */
    struct trapframe *td_intr_frame;/* (k) Frame of the current irq */
    struct proc *td_rfppwait_p;	/* (k) The vforked child */
};

#define	THREAD_LOCK_ASSERT(td, type)					\
do {									\
        struct mtx *__m = (td)->td_lock;				\
        if (__m != &blocked_lock)					\
        mtx_assert(__m, (type));				\
} while (0)

#define	THREAD_LOCKPTR_ASSERT(td, lock)

/*
 * Reasons that the current thread can not be run yet.
 * More than one may apply.
 */
#define	TDI_SUSPENDED	0x0001	/* On suspension queue. */
#define	TDI_SLEEPING	0x0002	/* Actually asleep! (tricky). */
#define	TDI_SWAPPED	0x0004	/* Stack not in mem.  Bad juju if run. */
#define	TDI_LOCK	0x0008	/* Stopped on a lock. */
#define	TDI_IWAIT	0x0010	/* Awaiting interrupt. */

#define	TD_IS_SLEEPING(td)	((td)->td_inhibitors & TDI_SLEEPING)
#define	TD_ON_SLEEPQ(td)	((td)->td_wchan != NULL)
#define	TD_IS_SUSPENDED(td)	((td)->td_inhibitors & TDI_SUSPENDED)
#define	TD_IS_SWAPPED(td)	((td)->td_inhibitors & TDI_SWAPPED)
#define	TD_IS_RUNNING(td)	((td)->td_state == TDS_RUNNING)

#define	TD_SET_INHIB(td, inhib) do {			\
(td)->td_state = TDS_INHIBITED;			\
    (td)->td_inhibitors |= (inhib);			\
} while (0)

#define	TD_CLR_INHIB(td, inhib) do {			\
if (((td)->td_inhibitors & (inhib)) &&		\
    (((td)->td_inhibitors &= ~(inhib)) == 0))	\
        (td)->td_state = TDS_CAN_RUN;		\
} while (0)

#define	TD_CLR_SLEEPING(td)	TD_CLR_INHIB((td), TDI_SLEEPING)
#define	TD_SET_SLEEPING(td)	TD_SET_INHIB((td), TDI_SLEEPING)

/*
 * Flags kept in td_flags:
 * To change these you MUST have the scheduler lock.
 */
#define	TDF_BORROWING	0x00000001 /* Thread is borrowing pri from another. */
#define	TDF_INPANIC	0x00000002 /* Caused a panic, let it drive crashdump. */
#define	TDF_INMEM	0x00000004 /* Thread's stack is in memory. */
#define	TDF_SINTR	0x00000008 /* Sleep is interruptible. */
#define	TDF_TIMEOUT	0x00000010 /* Timing out during sleep. */
#define	TDF_IDLETD	0x00000020 /* This is a per-CPU idle thread. */
#define	TDF_CANSWAP	0x00000040 /* Thread can be swapped. */
#define	TDF_SLEEPABORT	0x00000080 /* sleepq_abort was called. */
#define	TDF_KTH_SUSP	0x00000100 /* kthread is suspended */
#define	TDF_UNUSED09	0x00000200 /* --available-- */
#define	TDF_BOUNDARY	0x00000400 /* Thread suspended at user boundary */
#define	TDF_ASTPENDING	0x00000800 /* Thread has some asynchronous events. */
#define	TDF_TIMOFAIL	0x00001000 /* Timeout from sleep after we were awake. */
#define	TDF_SBDRY	0x00002000 /* Stop only on usermode boundary. */
#define	TDF_UPIBLOCKED	0x00004000 /* Thread blocked on user PI mutex. */
#define	TDF_NEEDSUSPCHK	0x00008000 /* Thread may need to suspend. */
#define	TDF_NEEDRESCHED	0x00010000 /* Thread needs to yield. */
#define	TDF_NEEDSIGCHK	0x00020000 /* Thread may need signal delivery. */
#define	TDF_NOLOAD	0x00040000 /* Ignore during load avg calculations. */
#define	TDF_UNUSED19	0x00080000 /* --available-- */
#define	TDF_THRWAKEUP	0x00100000 /* Libthr thread must not suspend itself. */
#define	TDF_UNUSED21	0x00200000 /* --available-- */
#define	TDF_SWAPINREQ	0x00400000 /* Swapin request due to wakeup. */
#define	TDF_UNUSED23	0x00800000 /* --available-- */
#define	TDF_SCHED0	0x01000000 /* Reserved for scheduler private use */
#define	TDF_SCHED1	0x02000000 /* Reserved for scheduler private use */
#define	TDF_SCHED2	0x04000000 /* Reserved for scheduler private use */
#define	TDF_SCHED3	0x08000000 /* Reserved for scheduler private use */
#define	TDF_ALRMPEND	0x10000000 /* Pending SIGVTALRM needs to be posted. */
#define	TDF_PROFPEND	0x20000000 /* Pending SIGPROF needs to be posted. */
#define	TDF_MACPEND	0x40000000 /* AST-based MAC event pending. */

/*
 * "Private" flags kept in td_pflags:
 * These are only written by curthread and thus need no locking.
 */
#define	TDP_OLDMASK	0x00000001 /* Need to restore mask after suspend. */
#define	TDP_INKTR	0x00000002 /* Thread is currently in KTR code. */
#define	TDP_INKTRACE	0x00000004 /* Thread is currently in KTRACE code. */
#define	TDP_BUFNEED	0x00000008 /* Do not recurse into the buf flush */
#define	TDP_COWINPROGRESS 0x00000010 /* Snapshot copy-on-write in progress. */
#define	TDP_ALTSTACK	0x00000020 /* Have alternate signal stack. */
#define	TDP_DEADLKTREAT	0x00000040 /* Lock aquisition - deadlock treatment. */
#define	TDP_NOFAULTING	0x00000080 /* Do not handle page faults. */
#define	TDP_NOSLEEPING	0x00000100 /* Thread is not allowed to sleep on a sq. */
#define	TDP_OWEUPC	0x00000200 /* Call addupc() at next AST. */
#define	TDP_ITHREAD	0x00000400 /* Thread is an interrupt thread. */
#define	TDP_SYNCIO	0x00000800 /* Local override, disable async i/o. */
#define	TDP_SCHED1	0x00001000 /* Reserved for scheduler private use */
#define	TDP_SCHED2	0x00002000 /* Reserved for scheduler private use */
#define	TDP_SCHED3	0x00004000 /* Reserved for scheduler private use */
#define	TDP_SCHED4	0x00008000 /* Reserved for scheduler private use */
#define	TDP_GEOM	0x00010000 /* Settle GEOM before finishing syscall */
#define	TDP_SOFTDEP	0x00020000 /* Stuck processing softdep worklist */
#define	TDP_NORUNNINGBUF 0x00040000 /* Ignore runningbufspace check */
#define	TDP_WAKEUP	0x00080000 /* Don't sleep in umtx cond_wait */
#define	TDP_INBDFLUSH	0x00100000 /* Already in BO_BDFLUSH, do not recurse */
#define	TDP_KTHREAD	0x00200000 /* This is an official kernel thread */
#define	TDP_CALLCHAIN	0x00400000 /* Capture thread's callchain */
#define	TDP_IGNSUSP	0x00800000 /* Permission to ignore the MNTK_SUSPEND* */
#define	TDP_AUDITREC	0x01000000 /* Audit record pending on thread */
#define	TDP_RFPPWAIT	0x02000000 /* Handle RFPPWAIT on syscall exit */
#define	TDP_RESETSPUR	0x04000000 /* Reset spurious page fault history. */
#define	TDP_NERRNO	0x08000000 /* Last errno is already in td_errno */


extern struct thread* curthread;
extern int curcpu;

/*
 * Process structure.
 */
struct proc {
    LIST_ENTRY(proc) p_list;	/* (d) List of all processes. */
    TAILQ_HEAD(, thread) p_threads;	/* (c) all threads. */
    struct mtx	p_slock;	/* process spin lock */
    struct ucred	*p_ucred;	/* (c) Process owner's identity. */
    struct filedesc	*p_fd;		/* (b) Open files. */
    struct filedesc_to_leader *p_fdtol; /* (b) Tracking node */
    struct pstats	*p_stats;	/* (b) Accounting/statistics (CPU). */
    struct plimit	*p_limit;	/* (c) Process limits. */
    //struct callout	p_limco;	/* (c) Limit callout handle */
    struct sigacts	*p_sigacts;	/* (x) Signal actions, state (CPU). */

    /*
     * The following don't make too much sense.
     * See the td_ or ke_ versions of the same flags.
     */
    int		p_flag;		/* (c) P_* flags. */
    enum {
        PRS_NEW = 0,		/* In creation */
        PRS_NORMAL,		/* threads can be run. */
        PRS_ZOMBIE
    } p_state;			/* (j/c) Process status. */
    pid_t		p_pid;		/* (b) Process identifier. */
    LIST_ENTRY(proc) p_hash;	/* (d) Hash chain. */
    LIST_ENTRY(proc) p_pglist;	/* (g + e) List of processes in pgrp. */
    struct proc	*p_pptr;	/* (c + e) Pointer to parent process. */
    LIST_ENTRY(proc) p_sibling;	/* (e) List of sibling processes. */
    LIST_HEAD(, proc) p_children;	/* (e) Pointer to list of children. */
    struct mtx	p_mtx;		/* (n) Lock for this struct. */
    struct ksiginfo *p_ksi;	/* Locked by parent proc lock */
    //sigqueue_t	p_sigqueue;	/* (c) Sigs not delivered to a td. */
#define p_siglist	p_sigqueue.sq_signals

/* The following fields are all zeroed upon creation in fork. */
#define	p_startzero	p_oppid
    pid_t		p_oppid;	/* (c + e) Save ppid in ptrace. XXX */
    int		p_pad_dbg_child;
    struct vmspace	*p_vmspace;	/* (b) Address space. */
    u_int		p_swtick;	/* (c) Tick when swapped in or out. */
    //struct itimerval p_realtimer;	/* (c) Alarm timer. */
    //struct rusage	p_ru;		/* (a) Exit information. */
    //struct rusage_ext p_rux;	/* (cj) Internal resource usage. */
    //struct rusage_ext p_crux;	/* (c) Internal child resource usage. */
    int		p_profthreads;	/* (c) Num threads in addupc_task. */
    volatile int	p_exitthreads;	/* (j) Number of threads exiting */
    int		p_traceflag;	/* (o) Kernel trace points. */
    struct vnode	*p_tracevp;	/* (c + o) Trace to vnode. */
    struct ucred	*p_tracecred;	/* (o) Credentials to trace with. */
    struct vnode	*p_textvp;	/* (b) Vnode of executable. */
    u_int		p_lock;		/* (c) Proclock (prevent swap) count. */
    //struct sigiolst	p_sigiolst;	/* (c) List of sigio sources. */
    int		p_sigparent;	/* (c) Signal to parent on exit. */
    int		p_sig;		/* (n) For core dump/debugger XXX. */
    u_long		p_code;		/* (n) For core dump/debugger XXX. */
    u_int		p_stops;	/* (c) Stop event bitmask. */
    u_int		p_stype;	/* (c) Stop event type. */
    char		p_step;		/* (c) Process is stopped. */
    u_char		p_pfsflags;	/* (c) Procfs flags. */
    struct nlminfo	*p_nlminfo;	/* (?) Only used by/for lockd. */
    struct kaioinfo	*p_aioinfo;	/* (y) ASYNC I/O info. */
    struct thread	*p_singlethread;/* (c + j) If single threading this is it */
    int		p_suspcount;	/* (j) Num threads in suspended mode. */
    struct thread	*p_xthread;	/* (c) Trap thread */
    int		p_boundary_count;/* (j) Num threads at user boundary */
    int		p_pendingcnt;	/* how many signals are pending */
    struct itimers	*p_itimers;	/* (c) POSIX interval timers. */
    struct procdesc	*p_procdesc;	/* (e) Process descriptor, if any. */
/* End area that is zeroed on creation. */
#define	p_endzero	p_magic

/* The following fields are all copied upon creation in fork. */
#define	p_startcopy	p_endzero
    u_int		p_magic;	/* (b) Magic number. */
    int		p_osrel;	/* (x) osreldate for the
                    binary (from ELF note, if any) */
    char		p_comm[MAXCOMLEN + 1];	/* (b) Process name. */
    struct pgrp	*p_pgrp;	/* (c + e) Pointer to process group. */
    struct sysentvec *p_sysent;	/* (b) Syscall dispatch info. */
    struct pargs	*p_args;	/* (c) Process arguments. */
    //rlim_t		p_cpulimit;	/* (c) Current CPU limit in seconds. */
    signed char	p_nice;		/* (c) Process "nice" value. */
    int		p_fibnum;	/* in this routing domain XXX MRT */
/* End area that is copied on creation. */
#define	p_endcopy	p_xstat

    u_short		p_xstat;	/* (c) Exit status; also stop sig. */
    //struct knlist	p_klist;	/* (c) Knotes attached to this proc. */
    int		p_numthreads;	/* (c) Number of threads. */
    //struct mdproc	p_md;		/* Any machine-dependent fields. */
    //struct callout	p_itcallout;	/* (h + c) Interval timer callout. */
    u_short		p_acflag;	/* (c) Accounting flags. */
    struct proc	*p_peers;	/* (r) */
    struct proc	*p_leader;	/* (b) */
    void		*p_emuldata;	/* (c) Emulator state data. */
    struct label	*p_label;	/* (*) Proc (not subject) MAC label. */
    struct p_sched	*p_sched;	/* (*) Scheduler-specific data. */
    STAILQ_HEAD(, ktr_request)	p_ktr;	/* (o) KTR event queue. */
    LIST_HEAD(, mqueue_notifier)	p_mqnotifier; /* (c) mqueue notifiers.*/
    struct kdtrace_proc	*p_dtrace; /* (*) DTrace-specific data. */
    //struct cv	p_pwait;	/* (*) wait cv for exit/exec. */
    //struct cv	p_dbgwait;	/* (*) wait cv for debugger attach
    //                after fork. */
    uint64_t	p_prev_runtime;	/* (c) Resource usage accounting. */
    struct racct	*p_racct;	/* (b) Resource accounting. */
    /*
     * An orphan is the child that has beed re-parented to the
     * debugger as a result of attaching to it.  Need to keep
     * track of them for parent to be able to collect the exit
     * status of what used to be children.
     */
    LIST_ENTRY(proc) p_orphan;	/* (e) List of orphan processes. */
    LIST_HEAD(, proc) p_orphans;	/* (e) Pointer to list of orphans. */
};

/* Lock and unlock a process. */
#define	PROC_LOCK(p)	mtx_lock(&(p)->p_mtx)
#define	PROC_TRYLOCK(p)	mtx_trylock(&(p)->p_mtx)
#define	PROC_UNLOCK(p)	mtx_unlock(&(p)->p_mtx)
#define	PROC_LOCKED(p)	mtx_owned(&(p)->p_mtx)
#define	PROC_LOCK_ASSERT(p, type)	mtx_assert(&(p)->p_mtx, (type))

#endif // PROC_H
