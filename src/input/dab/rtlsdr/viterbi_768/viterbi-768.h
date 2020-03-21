#
#ifndef	__VITERBI_768__
#define	__VITERBI_768__
/*
 * 	Viterbi.h according to the SPIRAL project
 */
 #include 	"tvheadend.h"

#include	"../../dab_constants.h"

//	For our particular viterbi decoder, we have
#define	RATE	4
#define NUMSTATES 64
#define DECISIONTYPE uint8_t
#define DECISIONTYPE_BITSIZE 8
#define COMPUTETYPE uint32_t

//decision_t is a BIT vector
typedef union {
	DECISIONTYPE t[NUMSTATES/DECISIONTYPE_BITSIZE];
	uint32_t w[NUMSTATES/32];
	uint16_t s[NUMSTATES/16];
	uint8_t c[NUMSTATES/8];
} decision_t __attribute__ ((aligned (16)));

typedef union {
	COMPUTETYPE t[NUMSTATES];
} metric_t __attribute__ ((aligned (16)));

/* State info for instance of Viterbi decoder
 */

struct v {
/* path metric buffer 1 */
	__attribute__ ((aligned (16))) metric_t metrics1;
/* path metric buffer 2 */
	__attribute__ ((aligned (16))) metric_t metrics2;
/* Pointers to path metrics, swapped on every bit */
	metric_t *old_metrics,*new_metrics;
	decision_t *decisions;   /* decisions */
	
	uint8_t *data;
	COMPUTETYPE *symbols;
	int16_t	frameBits;
	int spiral;
};

void	initConstViterbi768(void);
void	initViterbi768	(struct v *vp, int16_t, int spiral);
void	destroyViterbi768	(struct v *vp);
void	deconvolve	(struct v * vp, int16_t *, uint8_t *);

#endif

