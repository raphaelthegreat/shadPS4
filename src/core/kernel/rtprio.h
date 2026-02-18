#ifndef RTPRIO_H
#define RTPRIO_H

/* priority range */
#define RTP_PRIO_MIN		0	/* Highest priority */
#define RTP_PRIO_MAX		31	/* Lowest priority */

#define	PRI_MIN			(0)		/* Highest priority. */
#define	PRI_MAX			(255)		/* Lowest priority. */

#define	PRI_MIN_REALTIME	(48)
#define	PRI_MAX_REALTIME	(PRI_MIN_KERN - 1)

#define	PRI_MIN_KERN		(80)
#define	PRI_MAX_KERN		(PRI_MIN_TIMESHARE - 1)

#endif // RTPRIO_H
