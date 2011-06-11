/* This file is part of 34S.
 * 
 * 34S is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * 34S is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with 34S.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xeq.h"
#include "decn.h"
#include "stats.h"
#include "consts.h"
#include "int.h"


#define sigmaXXY	(Regs[86])

#define sigmaX	(Regs[87])
#define sigmaXX	(Regs[88])
#define sigmaY	(Regs[89])
#define sigmaYY	(Regs[90])
#define sigmaXY	(Regs[91])
#define sigmaN	(Regs[92])

#define sigmalnX	(Regs[93])
#define sigmalnXlnX	(Regs[94])
#define sigmalnY	(Regs[95])
#define sigmalnYlnY	(Regs[96])
#define sigmalnXlnY	(Regs[97])
#define sigmaXlnY	(Regs[98])
#define sigmaYlnX	(Regs[99])

//#define DUMP1
#ifdef DUMP1
#include <stdio.h>
static FILE *debugf = NULL;

static void open_debug(void) {
	if (debugf == NULL) {
		debugf = fopen("/dev/ttys001", "w");
	}
}
static void dump1(const decNumber *a, const char *msg) {
	char buf[2000], *b = buf;

	open_debug();
	if (decNumberIsNaN(a)) b= "NaN";
	else if (decNumberIsInfinite(a)) b = decNumberIsNegative(a)?"-inf":"inf";
	else
		decNumberToString(a, b);
	fprintf(debugf, "%s: %s\n", msg ? msg : "???", b);
	fflush(debugf);
}
#endif


static void correlation(decNumber *, const enum sigma_modes);

static int check_number(const decNumber *r, int n) {
	decNumber s;

	if (dn_lt0(dn_compare(&s, r, small_int(n)))) {
		err(ERR_MORE_POINTS);
		return 1;
	}
	return 0;
}

static int check_data(int n) {
	decNumber r;

	decimal64ToNumber(&sigmaN, &r);
	return check_number(&r, n);
}


void stats_mode_expf(decimal64 *nul1, decimal64 *nul2) {
	State.sigma_mode = SIGMA_EXP;
}

void stats_mode_linf(decimal64 *nul1, decimal64 *nul2) {
	State.sigma_mode = SIGMA_LINEAR;
}

void stats_mode_logf(decimal64 *nul1, decimal64 *nul2) {
	State.sigma_mode = SIGMA_LOG;
}

void stats_mode_pwrf(decimal64 *nul1, decimal64 *nul2) {
	State.sigma_mode = SIGMA_POWER;
}

void stats_mode_best(decimal64 *nul1, decimal64 *nul2) {
	State.sigma_mode = SIGMA_BEST;
}

void sigma_clear(decimal64 *nul1, decimal64 *nul2) {
	sigmaN = CONSTANT_INT(OP_ZERO);
	sigmaX = CONSTANT_INT(OP_ZERO);
	sigmaY = CONSTANT_INT(OP_ZERO);
	sigmaXX = CONSTANT_INT(OP_ZERO);
	sigmaYY = CONSTANT_INT(OP_ZERO);
	sigmaXY = CONSTANT_INT(OP_ZERO);

	sigmalnX = CONSTANT_INT(OP_ZERO);
	sigmalnXlnX = CONSTANT_INT(OP_ZERO);
	sigmalnY = CONSTANT_INT(OP_ZERO);
	sigmalnYlnY = CONSTANT_INT(OP_ZERO);
	sigmalnXlnY = CONSTANT_INT(OP_ZERO);
	sigmaXlnY = CONSTANT_INT(OP_ZERO);
	sigmaYlnX = CONSTANT_INT(OP_ZERO);
}


/* Accumulate sigma data.
 */
static void sigop(decimal64 *r, const decNumber *a, decNumber *(*op)(decNumber *, const decNumber *, const decNumber *)) {
	decNumber t, u;

	decimal64ToNumber(r, &t);
	(*op)(&u, &t, a);
	packed_from_number(r, &u);
}


/* Multiply a pair of values and accumulate into the sigma data.
 */
static void mulop(decimal64 *r, const decNumber *a, const decNumber *b, decNumber *(*op)(decNumber *, const decNumber *, const decNumber *)) {
	decNumber t, u, v;

	dn_multiply(&t, a, b);
	decimal64ToNumber(r, &v);
	(*op)(&u, &v, &t);
	packed_from_number(r, &u);
}


/* Define a helper function to handle sigma+ and sigma-
 */
static void sigma_helper(decContext *ctx, decNumber *(*op)(decNumber *, const decNumber *, const decNumber *)) {
	decNumber x, y;
	decNumber lx, ly;

	getXY(&x, &y);

	sigop(&sigmaN, &const_1, op);
	sigop(&sigmaX, &x, op);
	sigop(&sigmaY, &y, op);
	mulop(&sigmaXX, &x, &x, op);
	mulop(&sigmaYY, &y, &y, op);
	mulop(&sigmaXY, &x, &y, op);

	decNumberSquare(&lx, &x);
	mulop(&sigmaXXY, &lx, &y, op);

//	if (State.sigma_mode == SIGMA_LINEAR)
//		return;

	dn_ln(&lx, &x);
	dn_ln(&ly, &y);

	sigop(&sigmalnX, &lx, op);
	sigop(&sigmalnY, &ly, op);
	mulop(&sigmalnXlnX, &lx, &lx, op);
	mulop(&sigmalnYlnY, &ly, &ly, op);
	mulop(&sigmalnXlnY, &lx, &ly, op);
	mulop(&sigmaXlnY, &x, &ly, op);
	mulop(&sigmaYlnX, &y, &lx, op);
}

void sigma_plus(decContext *ctx) {
	sigma_helper(ctx, &dn_add);
}

void sigma_minus(decContext *ctx) {
	sigma_helper(ctx, &dn_subtract);
}


/* Loop through the various modes and work out
 * which has the highest absolute correlation.
 */
static enum sigma_modes determine_best(const decNumber *n) {
	enum sigma_modes m = SIGMA_LINEAR, i;
	decNumber b, c, d;

	dn_compare(&b, &const_2, n);
	if (dn_lt0(&b)) {
		correlation(&c, SIGMA_LINEAR);
		dn_abs(&b, &c);
		for (i=SIGMA_LINEAR+1; i<SIGMA_BEST; i++) {
			correlation(&d, i);

			if (! decNumberIsNaN(&d)) {
				dn_abs(&c, &d);
				dn_compare(&d, &b, &c);
				if (dn_lt0(&d)) {
					decNumberCopy(&b, &c);
					m = i;
				}
			}
		}
	}
	return m;
}


/* Return the appropriate variables for the specified fit.
 * If the fit is best, call a routine to figure out which has the highest
 * absolute r.  This entails a recursive call back here.
 */
static void get_sigmas(decNumber *N, decNumber *sx, decNumber *sy, decNumber *sxx, decNumber *syy,
			decNumber *sxy, enum sigma_modes mode) {
	decimal64 *xy;
	int lnx, lny;
	decNumber n;

	decimal64ToNumber(&sigmaN, &n);
	if (mode == SIGMA_BEST)
		mode = determine_best(&n);

	switch (mode) {
	default:			// Linear
		DispMsg = "Linear";
	case SIGMA_QUIET_LINEAR:
		xy = &sigmaXY;
		lnx = lny = 0;
		break;

	case SIGMA_LOG:
		DispMsg = "Log";
		xy = &sigmaYlnX;
		lnx = 1;
		lny = 0;
		break;

	case SIGMA_EXP:
		DispMsg = "Exp";
		xy = &sigmaXlnY;
		lnx = 0;
		lny = 1;
		break;

	case SIGMA_POWER:
		DispMsg = "Power";
	case SIGMA_QUIET_POWER:
		xy = &sigmalnXlnY;
		lnx = lny = 1;
		break;
	}

	if (N != NULL)
		decNumberCopy(N, &n);
	if (sx != NULL)
		decimal64ToNumber(lnx?&sigmalnX:&sigmaX, sx);
	if (sy != NULL)
		decimal64ToNumber(lny?&sigmalnY:&sigmaY, sy);
	if (sxx != NULL)
		decimal64ToNumber(lnx?&sigmalnXlnX:&sigmaXX, sxx);
	if (syy != NULL)
		decimal64ToNumber(lny?&sigmalnYlnY:&sigmaYY, syy);
	if (sxy != NULL)
		decimal64ToNumber(xy, sxy);
}


void sigma_N(decimal64 *x, decimal64 *y) {
	*x = sigmaN;
}

void sigma_X(decimal64 *x, decimal64 *y) {
	*x = sigmaX;
}

void sigma_Y(decimal64 *x, decimal64 *y) {
	*x = sigmaY;
}

void sigma_XX(decimal64 *x, decimal64 *y) {
	*x = sigmaXX;
}

void sigma_YY(decimal64 *x, decimal64 *y) {
	*x = sigmaYY;
}

void sigma_XY(decimal64 *x, decimal64 *y) {
	*x = sigmaXY;
}

void sigma_X2Y(decimal64 *x, decimal64 *y) {
	*x = sigmaXXY;
}

void sigma_lnX(decimal64 *x, decimal64 *y) {
	*x = sigmalnX;
}

void sigma_lnXlnX(decimal64 *x, decimal64 *y) {
	*x = sigmalnXlnX;
}

void sigma_lnY(decimal64 *x, decimal64 *y) {
	*x = sigmalnY;
}

void sigma_lnYlnY(decimal64 *x, decimal64 *y) {
	*x = sigmalnYlnY;
}

void sigma_lnXlnY(decimal64 *x, decimal64 *y) {
	*x = sigmalnXlnY;
}

void sigma_XlnY(decimal64 *x, decimal64 *y) {
	*x = sigmaXlnY;
}

void sigma_YlnX(decimal64 *x, decimal64 *y) {
	*x = sigmaYlnX;
}

void sigma_sum(decimal64 *x, decimal64 *y) {
	sigma_X(x, NULL);
	sigma_Y(y, NULL);
}


static void mean_common(decimal64 *res, const decNumber *x, const decNumber *n, int exp) {
	decNumber t, u, *p = &t;

	dn_divide(&t, x, n);
	if (exp)
		dn_exp(p=&u, &t);
	packed_from_number(res, p);
}

void stats_mean(decimal64 *x, decimal64 *y) {
	decNumber N;
	decNumber sx, sy;

	if (check_data(1))
		return;
	get_sigmas(&N, &sx, &sy, NULL, NULL, NULL, SIGMA_QUIET_LINEAR);

	mean_common(x, &sx, &N, 0);
	mean_common(y, &sy, &N, 0);
}


// weighted mean sigmaXY / sigmaY
void stats_wmean(decimal64 *x, decimal64 *nul) {
	decNumber xy, y;

	if (check_data(1))
		return;
	get_sigmas(NULL, NULL, &y, NULL, NULL, &xy, SIGMA_QUIET_LINEAR);

	mean_common(x, &xy, &y, 0);
}

// geometric mean e^(sigmaLnX / N)
void stats_gmean(decimal64 *x, decimal64 *y) {
	decNumber N;
	decNumber sx, sy;

	if (check_data(1))
		return;
	get_sigmas(&N, &sx, &sy, NULL, NULL, NULL, SIGMA_QUIET_POWER);

	mean_common(x, &sx, &N, 1);
	mean_common(y, &sy, &N, 1);
}

// Standard deviations and standard errors
static void do_s(decimal64 *s,
		const decNumber *sxx, const decNumber *sx,
		const decNumber *N, const decNumber *denom, 
		int rootn, int exp) {
	decNumber t, u, v, *p;

	decNumberSquare(&t, sx);
	dn_divide(&u, &t, N);
	dn_subtract(&t, sxx, &u);
	dn_divide(&u, &t, denom);
	dn_sqrt(p = &t, &u);

	if (rootn) {
		dn_sqrt(&u, N);
		dn_divide(p = &v, &t, &u);
	}
	if (exp) {
		dn_exp(&u, p);
		p = &u;
	}
	packed_from_number(s, p);
}

static void S(decimal64 *x, decimal64 *y, int sample, int rootn, int exp) {
	decNumber N, nm1, *n = &N;
	decNumber sx, sxx, sy, syy;

	if (check_data(2))
		return;
	get_sigmas(&N, &sx, &sy, &sxx, &syy, NULL, exp?SIGMA_QUIET_POWER:SIGMA_QUIET_LINEAR);
	if (sample)
		dn_subtract(n = &nm1, &N, &const_1);
	do_s(x, &sxx, &sx, &N, n, rootn, exp);
	do_s(y, &syy, &sy, &N, n, rootn, exp);
}

// sx = sqrt(sigmaX^2 - (sigmaX ^ 2 ) / (n-1))
void stats_s(decimal64 *x, decimal64 *y) {
	S(x, y, 1, 0, 0);
}

// [sigma]x = sqrt(sigmaX^2 - (sigmaX ^ 2 ) / n)
void stats_sigma(decimal64 *x, decimal64 *y) {
	S(x, y, 0, 0, 0);
}

// serr = sx / sqrt(n)
void stats_SErr(decimal64 *x, decimal64 *y) {
	S(x, y, 1, 1, 0);
}


// sx = sqrt(sigmaX^2 - (sigmaX ^ 2 ) / (n-1))
void stats_gs(decimal64 *x, decimal64 *y) {
	S(x, y, 1, 0, 1);
}

// [sigma]x = sqrt(sigmaX^2 - (sigmaX ^ 2 ) / n)
void stats_gsigma(decimal64 *x, decimal64 *y) {
	S(x, y, 0, 0, 1);
}

// serr = sx / sqrt(n)
void stats_gSErr(decimal64 *x, decimal64 *y) {
	S(x, y, 1, 1, 1);
}


// Weighted standard deviation
void WS(decimal64 *x, int sample, int rootn) {
	decNumber sxxy, sy, sxy, syy;
	decNumber t, u, v, w, *p;

	get_sigmas(NULL, NULL, &sy, NULL, &syy, &sxy, SIGMA_QUIET_LINEAR);
	if (check_number(&sy, 2))
		return;
	decimal64ToNumber(&sigmaXXY, &sxxy);

	dn_multiply(&t, &sy, &sxxy);
	decNumberSquare(&u, &sxy);
	dn_subtract(&v, &t, &u);
	decNumberSquare(p = &t, &sy);
	if (sample)
		dn_subtract(p = &u, &t, &syy);
	dn_divide(&w, &v, p);
	dn_sqrt(p = &u, &w);
	if (rootn) {
		dn_sqrt(&t, &sy);
		dn_divide(p = &v, &u, &t);
	}
	packed_from_number(x, p);
}

void stats_ws(decimal64 *x, decimal64 *y) {
	WS(x, 1, 0);
}

void stats_wsigma(decimal64 *x, decimal64 *y) {
	WS(x, 0, 0);
}

void stats_wSErr(decimal64 *x, decimal64 *y) {
	WS(x, 1, 1);
}


decNumber *stats_sigper(decNumber *res, const decNumber *x) {
	decNumber sx, t;

	get_sigmas(NULL, &sx, NULL, NULL, NULL, NULL, SIGMA_QUIET_LINEAR);
	dn_divide(&t, x, &sx);
	return dn_multiply(res, &t, &const_100);
}

/* Calculate the correlation based on the stats data using the
 * specified model.
 */
static void correlation(decNumber *t, const enum sigma_modes m) {
	decNumber N, u, v, w;
	decNumber sx, sy, sxx, syy, sxy;

	get_sigmas(&N, &sx, &sy, &sxx, &syy, &sxy, m);

	dn_multiply(t, &N, &sxx);
	decNumberSquare(&u, &sx);
	dn_subtract(&v, t, &u);
	dn_multiply(t, &N, &syy);
	decNumberSquare(&u, &sy);
	dn_subtract(&w, t, &u);
	dn_multiply(t, &v, &w);
	dn_sqrt(&w, t);
	dn_multiply(t, &N, &sxy);
	dn_multiply(&u, &sx, &sy);
	dn_subtract(&v, t, &u);
	dn_divide(t, &v, &w);

	dn_compare(&u, &const_1, t);
	if (decNumberIsNegative(&u))
		decNumberCopy(t, &const_1);
	else {
		dn_compare(&u, t, &const__1);
		if (decNumberIsNegative(&u))
			decNumberCopy(t, &const__1);
	}
}


void stats_correlation(decimal64 *r, decimal64 *nul) {
	decNumber t;

	if (check_data(2))
		return;
	correlation(&t, State.sigma_mode);
	packed_from_number(r, &t);
}


static void covariance(decimal64 *r, int sample) {
	decNumber N, t, u, v;
	decNumber sx, sy, sxy;

	if (check_data(2))
		return;
	get_sigmas(&N, &sx, &sy, NULL, NULL, &sxy, State.sigma_mode);
	dn_multiply(&t, &sx, &sy);
	dn_divide(&u, &t, &N);
	dn_subtract(&t, &sxy, &u);
	if (sample) {
		dn_subtract(&v, &N, &const_1);
		dn_divide(&u, &t, &v);
	} else
		dn_divide(&u, &t, &N);
	packed_from_number(r, &u);
}

void stats_COV(decimal64 *r, decimal64 *nul) {
	covariance(r, 0);
}

void stats_Sxy(decimal64 *r, decimal64 *nul) {
	covariance(r, 1);
}

// y = B . x + A
static void do_LR(decNumber *B, decNumber *A) {
	decNumber N, u, v, denom;
	decNumber sx, sy, sxx, sxy;

	get_sigmas(&N, &sx, &sy, &sxx, NULL, &sxy, State.sigma_mode);

	dn_multiply(B, &N, &sxx);
	decNumberSquare(&u, &sx);
	dn_subtract(&denom, B, &u);

	dn_multiply(B, &N, &sxy);
	dn_multiply(&u, &sx, &sy);
	dn_subtract(&v, B, &u);
	dn_divide(A, &v, &denom);

	dn_multiply(B, &sxx, &sy);
	dn_multiply(&u, &sx, &sxy);
	dn_subtract(&v, B, &u);
	dn_divide(B, &v, &denom);
}


void stats_LR(decimal64 *bout, decimal64 *aout) {
	decNumber a, b;

	if (check_data(2))
		return;
	do_LR(&b, &a);
	packed_from_number(aout, &a);
	packed_from_number(bout, &b);
}


decNumber *stats_xhat(decNumber *res, const decNumber *y) {
	decNumber a, b, t;

	if (check_data(2))
		return NULL;
	do_LR(&b, &a);
	dn_subtract(&t, y, &b);
	dn_divide(res, &a, &t);
	return res;
}


decNumber *stats_yhat(decNumber *res, const decNumber *x) {
	decNumber a, b, t;

	if (check_data(2))
		return NULL;
	do_LR(&b, &a);
	dn_multiply(&t, x, &a);
	dn_add(res, &t, &b);
	return res;
}


/* rng/taus.c from the GNU Scientific Library.
 * The period of this generator is about 2^88.
 */
static unsigned long int taus_get(void) {
#define MASK 0xffffffffUL
#define TAUSWORTHE(s,a,b,c,d) (((s & c) << d) & MASK) ^ ((((s << a) & MASK) ^ s) >> b)

  RandS1 = TAUSWORTHE (RandS1, 13, 19, 4294967294UL, 12);
  RandS2 = TAUSWORTHE (RandS2,  2, 25, 4294967288UL, 4);
  RandS3 = TAUSWORTHE (RandS3,  3, 11, 4294967280UL, 17);

  return RandS1 ^ RandS2 ^ RandS3;
}

static void taus_seed(unsigned long int s) {
	int i;

	if (s == 0)
		s = 1;
#define LCG(n) ((69069 * n) & 0xffffffffUL)
	RandS1 = LCG (s);
	if (RandS1 < 2) RandS1 += 2UL;

	RandS2 = LCG (RandS1);
	if (RandS2 < 8) RandS2 += 8UL;

	RandS3 = LCG (RandS2);
	if (RandS3 < 16) RandS3 += 16UL;
#undef LCG

	for (i=0; i<6; i++)
		taus_get();
}

void stats_random(decimal64 *r, decimal64 *nul) {
	// Start by generating the next in sequence
	unsigned long int s;
	decNumber y, z;

	if (RandS1 == 0 && RandS2 == 0 && RandS3 == 0)
		taus_seed(0);
	s = taus_get();

	// Now build ourselves a number
	if (is_intmode())
		d64fromInt(r, build_value(s, 0));
	else {
		ullint_to_dn(&z, s);
		dn_multiply(&y, &z, &const_randfac);
		packed_from_number(r, &y);
	}
}


void stats_sto_random(decimal64 *nul1, decimal64 *nul2) {
	unsigned long int s;
	int z;
	decNumber x;

	if (is_intmode()) {
		 s = d64toInt(&regX) & 0xffffffff;
	} else {
		getX(&x);
		s = (unsigned long int) dn_to_ull(&x, &z);
	}
	taus_seed(s);
}


#ifndef TINY_BUILD
static void check_low(decNumber *d) {
	decNumber t, u;

	dn_abs(&t, d);
	dn_compare(&u, &t, &const_1e_32);
	if (decNumberIsNegative(&u))
		decNumberCopy(d, &const_1e_32);
}


static void ib_step(decNumber *d, decNumber *c, const decNumber *aa) {
	decNumber t, u;

	dn_multiply(&t, aa, d);
	dn_add(&u, &const_1, &t);	// d = 1+aa*d
	check_low(&u);
	decNumberRecip(d, &u);
	dn_divide(&t, aa, c);
	dn_add(c, &const_1, &t);	// c = 1+aa/c
	check_low(c);
}


static void betacf(decNumber *r, const decNumber *a, const decNumber *b, const decNumber *x) {
	decNumber aa, c, d, apb, am1, ap1, m, m2, oldr;
	int i;
	decNumber t, u, v, w;

	dn_add(&ap1, a, &const_1);		// ap1 = 1+a
	dn_subtract(&am1, a, &const_1);	// am1 = a-1
	dn_add(&apb, a, b);			// apb = a+b
	decNumberCopy(&c, &const_1);			// c = 1
	dn_divide(&t, x, &ap1);
	dn_multiply(&u, &t, &apb);
	dn_subtract(&t, &const_1, &u);	// t = 1-apb*x/ap1
	check_low(&t);
	decNumberRecip(&d, &t);			// d = 1/t
	decNumberCopy(r, &d);				// res = d
	decNumberZero(&m);
	for (i=0; i<500; i++) {
		decNumberCopy(&oldr, r);
		dn_inc(&m);			// m = i+1
		dn_multiply(&m2, &m, &const_2);
		dn_subtract(&t, b, &m);
		dn_multiply(&u, &t, &m);
		dn_multiply(&t, &u, x);	// t = m*(b-m)*x
		dn_add(&u, &am1, &m2);
		dn_add(&v, a, &m2);
		dn_multiply(&w, &u, &v);	// w = (am1+m2)*(a+m2)
		dn_divide(&aa, &t, &w);	// aa = t/w
		ib_step(&d, &c, &aa);
		dn_multiply(&t, r, &d);
		dn_multiply(r, &t, &c);	// r = r*d*c
		dn_add(&t, a, &m);
		dn_add(&u, &apb, &m);
		dn_multiply(&w, &t, &u);
		dn_multiply(&t, &w, x);
		dn_minus(&w, &t);		// w = -(a+m)*(apb+m)*x
		dn_add(&t, a, &m2);
		dn_add(&u, &ap1, &m2);
		dn_multiply(&v, &t, &u);	// v = (a+m2)*(ap1+m2)
		dn_divide(&aa, &w, &v);	// aa = w/v
		ib_step(&d, &c, &aa);
		dn_multiply(&v, &d, &c);
		dn_multiply(r, r, &v);	// r *= d*c
		dn_compare(&u, &oldr, r);
		if (decNumberIsZero(&u))
			break;
	}
}
#endif

/* Regularised incomplete beta function Ix(a, b)
 */
decNumber *betai(decNumber *r, const decNumber *a, const decNumber *b, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber t, u, v, w, y;
	int limit = 0;

	dn_compare(&t, &const_1, x);
	if (decNumberIsNegative(x) || decNumberIsNegative(&t)) {
		return set_NaN(r);
	}
	if (decNumberIsZero(x) || decNumberIsZero(&t))
		limit = 1;
	else {
		decNumberLnBeta(&u, a, b);
		dn_ln(&v, x);		// v = ln(x)
		dn_multiply(&t, a, &v);
		dn_subtract(&v, &t, &u);	// v = lng(...)+a.ln(x)
		dn_subtract(&y, &const_1, x);// y = 1-x
		dn_ln(&u, &y);		// u = ln(1-x)
		dn_multiply(&t, &u, b);
		dn_add(&u, &t, &v);		// u = lng(...)+a.ln(x)+b.ln(1-x)
		dn_exp(&w, &u);
	}
	dn_add(&v, a, b);
	dn_add(&u, &v, &const_2);		// u = a+b+2
	dn_add(&t, a, &const_1);		// t = a+1
	dn_divide(&v, &t, &u);		// u = (a+1)/(a+b+2)
	dn_compare(&t, x, &v);
	if (decNumberIsNegative(&t)) {
		if (limit)
			return decNumberZero(r);
		betacf(&t, a, b, x);
		dn_divide(&u, &t, a);
		return dn_multiply(r, &w, &u);
	} else {
		if (limit)
			return decNumberCopy(r, &const_1);
		betacf(&t, b, a, &y);
		dn_divide(&u, &t, b);
		dn_multiply(&t, &w, &u);
		return dn_subtract(r, &const_1, &t);
	}
#else
	return NULL;
#endif
}


#ifndef TINY_BUILD
static int check_probability(decNumber *r, const decNumber *x, int min_zero) {
	decNumber t;

	/* Range check the probability input */
	if (decNumberIsZero(x)) {
	    if (min_zero)
		decNumberCopy(r, &const_0);
	    else
		set_neginf(r);
	    return 1;
	}
	dn_compare(&t, &const_1, x);
	if (decNumberIsZero(&t)) {
	    set_inf(r);
	    return 1;
	}
	if (decNumberIsNegative(&t) || decNumberIsNegative(x) || decNumberIsSpecial(x)) {
	    set_NaN(r);
	    return 1;
	}
	return 0;
}


/* Get parameters for a distribution */
static void dist_one_param(decNumber *a) {
	get_reg_n_as_dn(regJ_idx, a);
}

static void dist_two_param(decNumber *a, decNumber *b) {
	get_reg_n_as_dn(regJ_idx, a);
	get_reg_n_as_dn(regK_idx, b);
}

static int param_verify(decNumber *r, const decNumber *n, int zero, int intg) {
	if (decNumberIsSpecial(n) ||
			dn_le0(n) ||
			(!zero && decNumberIsZero(n)) ||
			(intg && !is_int(n))) {
		decNumberZero(r);
		Error = ERR_BAD_PARAM;
		return 1;
	}
	return 0;
}
#define param_positive(r, n)		(param_verify(r, n, 0, 0))
#define param_positive_int(r, n)	(param_verify(r, n, 0, 1))
#define param_nonnegative(r, n)		(param_verify(r, n, 1, 0))
#define param_nonnegative_int(r, n)	(param_verify(r, n, 1, 1))

static int param_range01(decNumber *r, const decNumber *p) {
	decNumber h;

	dn_compare(&h, &const_1, p);
	if (decNumberIsSpecial(p) || dn_lt0(p) || dn_lt0(&h)) {
		decNumberZero(r);
		err(ERR_BAD_PARAM);
		return 1;
	}
	return 0;
}


// Ln 1 + (cdf(x)-p)/p
static int qf_eval(decNumber *fx, const decNumber *x, const decNumber *p,
		decNumber *(*f)(decNumber *, const decNumber *;)) {
	decNumber a, b;

	busy();
	(*f)(&a, x);
	dn_subtract(&b, &a, p);
	if (decNumberIsZero(&b))
		return 0;
	dn_divide(&a, &b, p);
	decNumberLn1p(fx, &a);
	return 1;
}
#endif

static decNumber *qf_search(decNumber *r,
				const decNumber *x, int min_zero,
				const decNumber *samp_low, const decNumber *samp_high,
		decNumber *(*f)(decNumber *, const decNumber *)) {
#ifndef TINY_BUILD
	decNumber t, u, v, tv, uv, vv, oldv;
	unsigned int flags = 0;

	if (check_probability(r, x, min_zero))
	    return r;

	// Evaluate the first two points which are given to us.
	decNumberCopy(&t, samp_low);
	if (qf_eval(&tv, &t, x, f) == 0)
		return decNumberCopy(r, &t);
	if (Error == ERR_BAD_PARAM) {
		decNumberZero(r);
		return r;
	}

	decNumberCopy(&u, samp_high);
	if (qf_eval(&uv, &u, x, f) == 0)
		return decNumberCopy(r, &u);

	solver_init(&v, &t, &u, &tv, &uv, &flags);
	set_NaN(&oldv);
	do {
		// If we got below the minimum, do a bisection step instead
		if (min_zero && dn_le0(&v)) {
			dn_min(&v, &t, &u);
			dn_multiply(&v, &v, &const_0_5);
		}
		if (qf_eval(&vv, &v, x, f) == 0 || decNumberIsNaN(&vv))
			break;
		if (relative_error(&v, &oldv, &const_1e_24))
			break;
		decNumberCopy(&oldv, &v);
	} while (solver_step(&t, &u, &v, &tv, &uv, &vv, &flags, &relative_error) == 0);

	return decNumberCopy(r, &v);
#else
	return NULL;
#endif
}


/* Evaluate Ln(1 - x) accurately
 */
decNumber *dn_ln1m(decNumber *r, const decNumber *x) {
	decNumber a;
	dn_minus(&a, x);
	return decNumberLn1p(r, &a);
}


// Normal(0,1) PDF
// 1/sqrt(2 PI) . exp(-x^2/2)
decNumber *pdf_Q(decNumber *q, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber r, t;

	decNumberSquare(&t, x);
	dn_multiply(&r, &t, &const_0_5);
	dn_minus(&t, &r);
	dn_exp(&r, &t);
	return dn_multiply(q, &r, &const_recipsqrt2PI);
#else
	return NULL;
#endif
}

// Normal(0,1) CDF function
#ifndef TINY_BUILD
decNumber *cdf_Q_helper(decNumber *q, decNumber *pdf, const decNumber *x) {
	decNumber t, u, v, a, x2, d, absx, n;
	int i;

	pdf_Q(pdf, x);
	dn_abs(&absx, x);
	dn_compare(&u, &const_PI, &absx);	// We need a number about 3.2 and this is close enough
	if (decNumberIsNegative(&u)) {
		dn_minus(&x2, &absx);
		//n = ceil(5 + k / (|x| - 1))
		dn_subtract(&v, &absx, &const_1);
		dn_divide(&t, &const_256, &v);
		dn_add(&u, &t, &const_4);
		decNumberCeil(&n, &u);
		decNumberZero(&t);
		do {
			dn_add(&u, x, &t);
			dn_divide(&t, &n, &u);
			dn_dec(&n);
		} while (! decNumberIsZero(&n));

		dn_add(&u, &t, x);
		dn_divide(q, pdf, &u);
		if (! decNumberIsNegative(q))
			dn_subtract(q, &const_1, q);
		if (decNumberIsNegative(x))
			dn_minus(q, q);
		return q;
	} else {
		decNumberSquare(&x2, &absx);
		decNumberCopy(&t, &absx);
		decNumberCopy(&a, &absx);
		decNumberCopy(&d, &const_3);
		for (i=0;i<500; i++) {
			dn_multiply(&u, &t, &x2);
			dn_divide(&t, &u, &d);
			dn_add(&u, &a, &t);
			dn_compare(&v, &u, &a);
			if (decNumberIsZero(&v))
				break;
			decNumberCopy(&a, &u);
			dn_add(&d, &d, &const_2);
		}
		dn_multiply(&v, &a, pdf);
		if (decNumberIsNegative(x))
			return dn_subtract(q, &const_0_5, &v);
		return dn_add(q, &const_0_5, &v);
	}
}
#endif

decNumber *cdf_Q(decNumber *q, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber t;
	return cdf_Q_helper(q, &t, x);
#else
	return NULL;
#endif
}


#ifndef TINY_BUILD
static void qf_Q_est(decNumber *est, const decNumber *x, const decNumber *x05) {
	const int invert = decNumberIsNegative(x05);
	decNumber a, b, u, xc;

	if (invert) {
		dn_subtract(&xc, &const_1, x);
		x = &xc;
	}

	dn_compare(&a, x, &const_0_2);
	if (decNumberIsNegative(&a)) {
		dn_ln(&a, x);
		dn_multiply(&u, &a, &const__2);
		dn_subtract(&a, &u, &const_1);
		dn_sqrt(&b, &a);
		dn_multiply(&a, &b, &const_sqrt2PI);
		dn_multiply(&b, &a, x);
		dn_ln(&a, &b);
		dn_multiply(&b, &a, &const__2);
		dn_sqrt(&a, &b);
		dn_divide(&b, &const_0_2, &u);
		dn_add(est, &a, &b);
		if (!invert)
			dn_minus(est, est);
	} else {
		dn_multiply(&a, &const_sqrt2PI, x05);
		decNumberCube(&b, &a);
		dn_divide(&u, &b, &const_6);
		dn_add(est, &u, &a);
		dn_minus(est, est);
	}
}
#endif

decNumber *qf_Q(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, t, cdf, pdf;
	int i;


	if (check_probability(r, x, 0))
		return r;
	dn_subtract(&b, &const_0_5, x);
	if (decNumberIsZero(&b)) {
		decNumberZero(r);
		return r;
	}

	qf_Q_est(r, x, &b);
	for (i=0; i<10; i++) {
		cdf_Q_helper(&cdf, &pdf, r);
		dn_subtract(&a, &cdf, x);
		dn_divide(&t, &a, &pdf);
		dn_multiply(&a, &t, r);
		dn_multiply(&b, &a, &const_0_5);
		dn_subtract(&a, &b, &const_1);
		dn_divide(&b, &t, &a);
		dn_add(&a, &b, r);
		if (relative_error(&a, r, &const_1e_32))
			break;
		decNumberCopy(r, &a);
	}
	return decNumberCopy(r, &a);
#else
	return NULL;
#endif
}


// Pv(x) = (x/2)^(v/2) . exp(-x/2) / Gamma(v/2+1) . (1 + sum(x^k/(v+2)(v+4)..(v+2k))
decNumber *cdf_chi2(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, v;

	dist_one_param(&v);
	if (param_positive_int(r, &v))
		return r;
	if (decNumberIsNaN(x)) {
		return set_NaN(r);
	}
	if (dn_le0(x))
		return decNumberZero(r);
	if (decNumberIsInfinite(x))
		return decNumberCopy(r, &const_1);

	dn_multiply(&a, &v, &const_0_5);
	dn_multiply(&b, x, &const_0_5);
	return decNumberGammap(r, &a, &b);
#else
	return NULL;
#endif
}

decNumber *qf_chi2(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, c, q, v;

	dist_one_param(&v);
	if (param_positive_int(r, &v))
		return r;
	if (decNumberIsNaN(x)) {
		return set_NaN(r);
	}
	dn_multiply(&a, &v, &const_2);
	dn_subtract(&b, &a, &const_1);
	dn_sqrt(&a, &b);
	qf_Q(&q, x);
	dn_add(&c, &q, &a);
	decNumberSquare(&b, &c);
	dn_multiply(&a, &b, &const_0_25);	// lower estimate
	dn_multiply(&b, &a, &const_e);
	return qf_search(r, x, 1, &a, &b, &cdf_chi2);
#else
	return NULL;
#endif
}

static int t_param(decNumber *r, decNumber *v, const decNumber *x) {
	dist_one_param(v);
	if (param_positive(r, v))
		return 1;
	if (decNumberIsNaN(x)) {
		set_NaN(r);
		return 1;
	}
	return 0;
}


decNumber *cdf_T(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber t, v;
	int invert;

	if (t_param(r, &v, x))
		return r;
	if (decNumberIsInfinite(x)) {
		if (decNumberIsNegative(x))
			return decNumberZero(r);
		return decNumberCopy(r, &const_1);
	}
	if (decNumberIsInfinite(&v))			// Normal in the limit
		return cdf_Q(r, x);
	if (decNumberIsZero(x))
		return decNumberCopy(r, &const_0_5);
	invert = ! decNumberIsNegative(x);
	decNumberSquare(&t, x);
	dn_add(r, &t, &v);
	dn_divide(&t, &v, r);
	dn_multiply(r, &v, &const_0_5);
	betai(&v, r, &const_0_5, &t);
	dn_multiply(r, &const_0_5, &v);
	if (invert)
		dn_subtract(r, &const_1, r);
	return r;
#else
	return NULL;
#endif
}

#ifndef TINY_BUILD
static void qf_T_est(decNumber *r, const decNumber *df, const decNumber *p, const decNumber *p05) {
	const int invert = decNumberIsNegative(p05);
	int negate;
	decNumber a, b, u, pc, pc05, x, x2, x3;

	if (invert) {
		dn_subtract(&pc, &const_1, p);
		p = &pc;
		dn_minus(&pc05, p05);
		p05 = &pc05;
	}
	dn_ln(&a, p);
	dn_minus(&a, &a);
	dn_multiply(&b, df, &const_1_7);
	if (dn_lt0(dn_compare(&u, &a, &b))) {
		qf_Q_est(&x, p, p05);
		decNumberSquare(&x2, &x);
		dn_multiply(&x3, &x2, &x);
		dn_add(&a, &x, &x3);
		dn_multiply(&b, &a, &const_0_25);
		dn_divide(&a, &b, df);
		dn_add(r, &a, &x);

		dn_divide(&a, &x2, &const_3);
		dn_inc(&a);
		dn_multiply(&b, &a, &x3);
		dn_multiply(&a, &b, &const_0_25);
		decNumberSquare(&b, df);
		dn_divide(&u, &a, &b);
		dn_add(r, r, &u);
		negate = invert;
	} else {
		dn_multiply(&x2, df, &const_2);
		dn_subtract(&b, &x2, &const_1);
		dn_divide(&a, &const_PI, &b);
		dn_sqrt(&b, &a);
		dn_multiply(&a, &b, &x2);
		dn_multiply(&b, &a, p);
		decNumberRecip(&a, df);
		dn_power(&u, &b, &a);
		dn_sqrt(&a, df);
		dn_divide(r, &a, &u);
		negate = !invert;
	}
	if (negate)
		dn_minus(r, r);
}

static int qf_T_init(decNumber *r, decNumber *a, decNumber *b, const decNumber *x) {
	decNumber c, d, v;

	if (t_param(r, &v, x))
		return 1;
	dn_subtract(b, &const_0_5, x);
	if (decNumberIsZero(b)) {
		decNumberZero(r);
		return 1;
	}
	if (decNumberIsInfinite(&v)) {					// Normal in the limit
		qf_Q(r, x);
		return 1;
	}

	dn_compare(a, &v, &const_1);
	if (decNumberIsZero(a)) {					// special case v = 1
		dn_multiply(a, b, &const_PI);
		dn_sincos(a, &c, &d);
		dn_divide(a, &c, &d);			// lower = tan(pi (x - 1/2))
		dn_minus(r, a);
		return 1;
	}
	dn_compare(&d, &v, &const_2);			// special case v = 2
	if (decNumberIsZero(&d)) {
		dn_subtract(a, &const_1, x);
		dn_multiply(&c, a, x);
		dn_multiply(&d, &c, &const_4);		// alpha = 4p(1-p)

		dn_divide(&c, &const_2, &d);
		dn_sqrt(a, &c);
		dn_multiply(&c, a, b);
		dn_multiply(r, &c, &const__2);
	}

	// common case v >= 3
	qf_T_est(&c, &v, x, b);
	dn_divide(b, &c, &const_0_9);
	dn_multiply(a, &c, &const_0_9);
	return 0;
}
#endif

decNumber *qf_T(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b;

	if (qf_T_init(r, &a, &b, x))
		return r;
	return qf_search(r, x, 0, &a, &b, &cdf_T);
#else
	return NULL;
#endif
}
	
decNumber *cdf_F(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber t, u, w, v1, v2;

	dist_two_param(&v1, &v2);
	if (param_positive(r, &v1) || param_positive(r, &v2))
		return r;
	if (decNumberIsNaN(x)) {
		return set_NaN(r);
	}
	if (dn_le0(x))
		return decNumberZero(r);
	if (decNumberIsInfinite(x))
		return decNumberCopy(r, &const_1);

	dn_multiply(&t, &v1, x);
	dn_add(&u, &t, &v2);			// u = v1 * x + v2
	dn_divide(&w, &t, &u);		// w = (v1 * x) / (v1 * x + v2)
	dn_multiply(&t, &v1, &const_0_5);
	dn_multiply(&u, &v2, &const_0_5);
	return betai(r, &t, &u, &w);
#else
	return NULL;
#endif
}

decNumber *qf_F(decNumber *r, const decNumber *x) {
	if (decNumberIsZero(x))
		return decNumberZero(r);
	// MORE: provide reasonable initial estaimtes
	return qf_search(r, x, 1, &const_1e_10, &const_20, &cdf_F);
}


/* Weibull distribution cdf = 1 - exp(-(x/lambda)^k)
 */
#ifndef TINY_BUILD
static int weibull_param(decNumber *r, decNumber *k, decNumber *lam, const decNumber *x) {
	dist_two_param(k, lam);
	if (param_positive(r, k) || param_positive(r, lam))
		return 1;
	if (decNumberIsNaN(x)) {
		set_NaN(r);
		return 1;
	}
	return 0;
}
#endif

decNumber *pdf_WB(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber k, lam, t, u, v, q;

	if (weibull_param(r, &k, &lam, x))
		return r;
	if (dn_lt0(x)) {
		decNumberZero(r);
		return r;
	}
	dn_divide(&q, x, &lam);
	dn_power(&u, &q, &k);		// (x/lam)^k
	dn_divide(&t, &u, &q);		// (x/lam)^(k-1)
	dn_exp(&v, &u);
	dn_divide(&q, &t, &v);
	dn_divide(&t, &q, &lam);
	return dn_multiply(r, &t, &k);
#else
	return NULL;
#endif
}

decNumber *cdf_WB(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber k, lam, t;

	if (weibull_param(r, &k, &lam, x))
		return r;
	if (dn_le0(x))
		return decNumberZero(r);
	if (decNumberIsInfinite(x))
		return decNumberCopy(r, &const_1);

	dn_divide(&t, x, &lam);
	dn_power(&lam, &t, &k);
	dn_minus(&t, &lam);
	decNumberExpm1(&lam, &t);
	return dn_minus(r, &lam);
#else
	return NULL;
#endif
}


/* Weibull distribution quantile function:
 *	p = 1 - exp(-(x/lambda)^k)
 *	exp(-(x/lambda)^k) = 1 - p
 *	-(x/lambda)^k = ln(1-p)
 * Thus, the qf is:
 *	x = (-ln(1-p) ^ (1/k)) * lambda
 * So no searching is required.
 */
decNumber *qf_WB(decNumber *r, const decNumber *p) {
#ifndef TINY_BUILD
	decNumber t, u, k, lam;

	if (weibull_param(r, &k, &lam, p))
		return r;
	if (check_probability(r, p, 1))
	    return r;
	dn_subtract(&t, &const_1, p);
	if (decNumberIsNaN(p) || decNumberIsSpecial(&lam) || decNumberIsSpecial(&k) ||
			dn_le0(&k) || dn_le0(&lam)) {
		return set_NaN(r);
	}

	dn_ln(&u, &t);
	dn_minus(&t, &u);
	decNumberRecip(&u, &k);
	dn_power(&k, &t, &u);
	return dn_multiply(r, &lam, &k);
#else
	return NULL;
#endif
}


/* Exponential distribution cdf = 1 - exp(-lambda . x)
 */
#ifndef TINY_BUILD
static int exponential_xform(decNumber *r, decNumber *lam, const decNumber *x) {
	dist_one_param(lam);
	if (param_positive(r, lam))
		return 1;
	if (decNumberIsNaN(x)) {
		set_NaN(r);
		return 1;
	}
	return 0;
}
#endif

decNumber *pdf_EXP(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber lam, t, u;

	if (exponential_xform(r, &lam, x))
		return r;
	if (dn_lt0(x)) {
		set_NaN(r);
		return r;
	}
	dn_multiply(&t, &lam, x);
	dn_minus(&u, &t);
	dn_exp(&t, &u);
	return dn_multiply(r, &t, &lam);
#else
	return NULL;
#endif
}

decNumber *cdf_EXP(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber lam, t, u;

	if (exponential_xform(r, &lam, x))
		return r;
	if (dn_le0(x))
		return decNumberZero(r);
	if (decNumberIsInfinite(x))
		return decNumberCopy(r, &const_1);

	dn_multiply(&t, &lam, x);
	dn_minus(&u, &t);
	decNumberExpm1(&t, &u);
	return dn_minus(r, &t);
#else
	return NULL;
#endif
}


/* Exponential distribution quantile function:
 *	p = 1 - exp(-lambda . x)
 *	exp(-lambda . x) = 1 - p
 *	-lambda . x = ln(1 - p)
 * Thus, the quantile function is:
 *	x = ln(1-p)/-lambda
 */
decNumber *qf_EXP(decNumber *r, const decNumber *p) {
#ifndef TINY_BUILD
	decNumber t, u, lam;

	dist_one_param(&lam);
	if (param_positive(r, &lam))
		return r;
	if (check_probability(r, p, 1))
	    return r;
	if (decNumberIsNaN(p) || decNumberIsSpecial(&lam) || dn_le0(&lam)) {
		return set_NaN(r);
	}

//	dn_minus(&t, p);
//	decNumberLn1p(&u, &t);
	dn_ln1m(&u, p);
	dn_divide(&t, &u, &lam);
	return dn_minus(r, &t);
#else
	return NULL;
#endif
}

/* Binomial cdf f(k; n, p) = iBeta(n-floor(k), 1+floor(k); 1-p)
 */
#ifndef TINY_BUILD
static int binomial_param(decNumber *r, decNumber *p, decNumber *n, const decNumber *x) {
	dist_two_param(p, n);
	if (param_nonnegative_int(r, n) || param_range01(r, p))
		return 1;
	if (decNumberIsNaN(x)) {
		set_NaN(r);
		return 1;
	}
	return 0;
}
#endif

decNumber *cdf_B_helper(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber n, p, t, u, v;

	if (binomial_param(r, &p, &n, x))
		return r;
	if (dn_lt0(x))
		return decNumberZero(r);
	if (dn_lt0(dn_compare(&t, &n, x)))
		return decNumberCopy(r, &const_1);

	dn_add(&u, x, &const_1);
	dn_subtract(&v, &n, x);
	dn_subtract(&t, &const_1, &p);
	return betai(r, &v, &u, &t);
#else
	return NULL;
#endif
}

decNumber *pdf_B(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber n, p, t, u, v;

	if (binomial_param(r, &p, &n, x))
		return r;
	if (! is_int(x)) {
		decNumberZero(r);
		return r;
	}

	dn_subtract(&u, &n, x);
	if (dn_lt0(&u) || dn_lt0(x)) {
		decNumberZero(r);
		return r;
	}
	dn_subtract(&t, &const_1, &p);
	dn_power(&v, &t, &u);
	decNumberComb(&t, &n, x);
	dn_multiply(&u, &t, &v);
	dn_power(&t, &p, x);
	return dn_multiply(r, &t, &u);
#else
	return NULL;
#endif
}

decNumber *cdf_B(decNumber *r, const decNumber *x) {
	decNumber t;

	decNumberFloor(&t, x);
	return cdf_B_helper(r, &t);
}

decNumber *qf_B(decNumber *r, const decNumber *x) {
	decNumber p, n;

	if (binomial_param(r, &p, &n, x))
		return r;
	return qf_search(r, x, 1, &const_0, &n, &cdf_B_helper);
}


/* Poisson cdf f(k, lam) = 1 - iGamma(floor(k+1), lam) / floor(k)! k>=0
 */
#ifndef TINY_BUILD
static int poisson_param(decNumber *r, decNumber *lambda, const decNumber *x) {
	decNumber prob, count;

	dist_two_param(&prob, &count);
	if (param_range01(r, &prob) || param_nonnegative_int(r, &count))
		return 1;
	if (decNumberIsNaN(x)) {
		set_NaN(r);
		return 1;
	}
	dn_multiply(lambda, &prob, &count);
	return 0;
}
#endif

// Evaluate via: exp(x Ln(lambda) - lambda - sum(i=1, k, Ln(i)))
decNumber *cdf_P_helper(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber lambda, t, u;

	poisson_param(r, &lambda, x);
	if (dn_lt0(x))
		return decNumberZero(r);
	if (decNumberIsInfinite(x))
		return decNumberCopy(r, &const_1);

	dn_add(&u, x, &const_1);
	decNumberGammap(&t, &u, &lambda);
	return dn_subtract(r, &const_1, &t);
#else
	return NULL;
#endif
}

decNumber *pdf_P(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber lambda, t, u, v;

	if (poisson_param(r, &lambda, x))
		return r;
	if (! is_int(x) || dn_lt0(x)) {
		decNumberZero(r);
		return r;
	}
	dn_power(&t, &lambda, x);
	decNumberFactorial(&u, x);
	dn_divide(&v, &t, &u);
	dn_exp(&t, &lambda);
	return dn_divide(r, &v, &t);
#else
	return NULL;
#endif
}

decNumber *cdf_P(decNumber *r, const decNumber *x) {
	decNumber t;

	decNumberFloor(&t, x);
	return cdf_P_helper(r, &t);
}

decNumber *qf_P(decNumber *r, const decNumber *x) {
	// MORE: provide reasonable initial estaimtes
	return qf_search(r, x, 1, &const_0, &const_20, &cdf_P_helper);
}


/* Geometric cdf
 */
#ifndef TINY_BUILD
static int geometric_param(decNumber *r, decNumber *p, const decNumber *x) {
        dist_one_param(p);
        if (param_range01(r, p))
                return 1;
        if (decNumberIsNaN(x)) {
                set_NaN(r);
                return 1;
        }
        return 0;
}
#endif

decNumber *pdf_G(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber p, t, u, v;

	if (geometric_param(r, &p, x))
		return r;
        if (! is_int(x) || dn_lt0(x)) {
		decNumberZero(r);
		return r;
        }
	dn_subtract(&t, &const_1, &p);
	dn_subtract(&u, x, &const_1);
	dn_power(&v, &t, &u);
	return dn_multiply(r, &v, &p);
#else
	return NULL;
#endif
}

decNumber *cdf_G(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
        decNumber p, t, u, ipx;

        if (geometric_param(r, &p, x))
                return r;
        if (! is_int(x)) {
		decNumberFloor(&ipx, x);
		x = &ipx;
        }
        if (dn_le0(x))
                return decNumberZero(r);
        if (decNumberIsInfinite(x))
                return decNumberCopy(r, &const_1);

//	dn_minus(&t, &p);
//	decNumberLn1p(&u, &t);
	dn_ln1m(&u, &p);
	dn_multiply(&t, &u, x);
	decNumberExpm1(&u, &t);
	return dn_minus(r, &u);
#else
	return NULL;
#endif
}

decNumber *qf_G(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
        decNumber p, t, v;

        if (geometric_param(r, &p, x))
                return r;
        if (check_probability(r, x, 1))
                return r;
//	dn_minus(&t, x);
//	decNumberLn1p(&v, &t);
//	dn_minus(&u, &p);
//	decNumberLn1p(&t, &u);
	dn_ln1m(&v, x);
	dn_ln1m(&t, &p);
        return dn_divide(r, &v, &t);
#else
	return NULL;
#endif
}

/* Normal with specified mean and variance */
#ifndef TINY_BUILD
static int normal_xform(decNumber *r, decNumber *q, const decNumber *x, decNumber *var) {
	decNumber a, mu;

	dist_two_param(&mu, var);
	if (param_positive(r, var))
		return 1;
	dn_subtract(&a, x, &mu);
	dn_divide(q, &a, var);
	return 0;
}
#endif

decNumber *pdf_normal(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber q, var, s;

	if (normal_xform(r, &q, x, &var))
		return r;
	pdf_Q(&s, &q);
	return dn_divide(r, &s, &var);
#else
	return NULL;
#endif
}

decNumber *cdf_normal(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber q, var;

	if (normal_xform(r, &q, x, &var))
		return r;
	return cdf_Q(r, &q);
#else
	return NULL;
#endif
}

decNumber *qf_normal(decNumber *r, const decNumber *p) {
#ifndef TINY_BUILD
	decNumber a, b, mu, var;

	dist_two_param(&mu, &var);
	if (param_positive(r, &var))
		return r;
	qf_Q(&a, p);
	dn_multiply(&b, &a, &var);
	return dn_add(r, &b, &mu);
#else
	return NULL;
#endif
}


/* Log normal with specified mean and variance */
decNumber *pdf_lognormal(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber t, lx;

	dn_ln(&lx, x);
	pdf_normal(&t, &lx);
	return dn_divide(r, &t, x);
#else
	return NULL;
#endif
}

decNumber *cdf_lognormal(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber lx;

	dn_ln(&lx, x);
	return cdf_normal(r, &lx);
#else
	return NULL;
#endif
}


decNumber *qf_lognormal(decNumber *r, const decNumber *p) {
#ifndef TINY_BUILD
	decNumber lr;

	qf_normal(&lr, p);
	return dn_exp(r, &lr);
#else
	return NULL;
#endif
}

/* Logistic with specified mean and spread */
#ifndef TINY_BUILD
static int logistic_xform(decNumber *r, decNumber *c, const decNumber *x, decNumber *s) {
	decNumber mu, a, b;
	
	dist_two_param(&mu, s);
	if (param_positive(r, s))
		return 1;
	dn_subtract(&a, x, &mu);
	dn_divide(&b, &a, s);
	dn_multiply(c, &b, &const_0_5);
	return 0;
}
#endif

decNumber *pdf_logistic(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, s;

	if (logistic_xform(r, &a, x, &s))
		return r;
	decNumberCosh(&b, &a);
	decNumberSquare(&a, &b);
	dn_multiply(&b, &a, &const_4);
	dn_multiply(&a, &b, &s);
	return decNumberRecip(r, &a);
#else
	return NULL;
#endif
}

decNumber *cdf_logistic(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, s;

	if (logistic_xform(r, &a, x, &s))
		return r;
	decNumberTanh(&b, &a);
	dn_multiply(&a, &b, &const_0_5);
	return dn_add(r, &a, &const_0_5);
#else
	return NULL;
#endif
}

decNumber *qf_logistic(decNumber *r, const decNumber *p) {
#ifndef TINY_BUILD
	decNumber a, b, mu, s;

	dist_two_param(&mu, &s);
	if (param_positive(r, &s))
		return r;
	if (check_probability(r, p, 0))
	    return r;
	dn_subtract(&a, p, &const_0_5);
	dn_multiply(&b, &a, &const_2);
	decNumberArcTanh(&a, &b);
	dn_multiply(&b, &a, &const_2);
	dn_multiply(&a, &b, &s);
	return dn_add(r, &a, &mu);
#else
	return NULL;
#endif
}

/* Cauchy distribution */
#ifndef TINY_BUILD
static int cauchy_xform(decNumber *r, decNumber *c, const decNumber *x, decNumber *gamma) {
	decNumber a, x0;

	dist_two_param(&x0, gamma);
	if (param_positive(r, gamma))
		return 1;
	dn_subtract(&a, x, &x0);
	dn_divide(c, &a, gamma);
	return 0;
}
#endif

decNumber *pdf_cauchy(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, gamma;

	if (cauchy_xform(r, &b, x, &gamma))
		return r;
	decNumberSquare(&a, &b);
	dn_add(&b, &a, &const_1);
	dn_multiply(&a, &b, &const_PI);
	dn_multiply(&b, &a, &gamma);
	return decNumberRecip(r, &b);
#else
	return NULL;
#endif
}

decNumber *cdf_cauchy(decNumber *r, const decNumber *x) {
#ifndef TINY_BUILD
	decNumber a, b, gamma;

	if (cauchy_xform(r, &b, x, &gamma))
		return r;
	do_atan(&a, &b);
	dn_divide(&b, &a, &const_PI);
	return dn_add(r, &b, &const_0_5);
#else
	return NULL;
#endif
}

decNumber *qf_cauchy(decNumber *r, const decNumber *p) {
#ifndef TINY_BUILD
	decNumber a, b, x0, gamma;

	dist_two_param(&x0, &gamma);
	if (param_positive(r, &gamma))
		return r;
	if (check_probability(r, p, 0))
	    return r;
	dn_subtract(&a, p, &const_0_5);
	dn_multiply(&b, &a, &const_PI);
	decNumberTan(&a, &b);
	dn_multiply(&b, &a, &gamma);
	return dn_add(r, &b, &x0);
#else
	return NULL;
#endif
}
