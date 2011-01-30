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

#include "int.h"
#include "xeq.h"

/* Some utility routines to extract bits of long longs */

unsigned int int_base(void) {
	unsigned int b = state.int_base + 1;
	if (b < 2)
		return 10;
	return b;
}

enum arithmetic_modes int_mode(void) {
	unsigned int b = int_base();
	if (b == 10 || (b & (b-1)) == 0)
		return state.int_mode;
	return MODE_UNSIGNED;
}

int word_size(void) {
	if (state.int_len >= MAX_WORD_SIZE || state.int_len == 0)
		return MAX_WORD_SIZE;
	return state.int_len;
}

int get_carry(void) {
	return get_user_flag(CARRY_FLAG);
}

void set_carry(int c) {
	if (c)
		set_user_flag(CARRY_FLAG);
	else
		clr_user_flag(CARRY_FLAG);
}

int get_overflow(void) {
	return get_user_flag(OVERFLOW_FLAG);
}

void set_overflow(int o) {
	if (o)
		set_user_flag(OVERFLOW_FLAG);
	else
		clr_user_flag(OVERFLOW_FLAG);
}

/* Some utility routines for saving and restoring carry and overflow.
 * Some operations don't change these flags but their subcomponents might.
 */
static int save_flags(void) {
	return (get_overflow() << 1) | get_carry();
}

static void restore_flags(int co) {
	set_carry(co & 1);
	set_overflow(co & 2);
}

/* Utility routine for trimming a value to the current word size
 */
long long int mask_value(const long long int v) {
	const int ws = word_size();
    long long int mask;

	if (MAX_WORD_SIZE == 64 && ws == 64)
		return v;
	mask = (1LL << ws) - 1;
	return v & mask;
}

/* Ulility routine for returning a bit mask to get the topmost (sign)
 * bit from a number.
 */
static long long int topbit_mask(void) {
	const int ws = word_size();
	long long int bit = 1LL << (ws - 1);
	return bit;
}

/* Utility routine to convert a binary integer into separate sign and
 * value components.  The sign returned is 1 for negative and 0 for positive.
 */
unsigned long long int extract_value(const long long int val, int *const sign) {
	const enum arithmetic_modes mode = int_mode();
	long long int v = mask_value(val);

	if (mode == MODE_UNSIGNED) {
		*sign = 0;
		return v;
	}

    {const long long int tbm = topbit_mask();

	if (v & tbm) {
		*sign = 1;
		if (mode == MODE_2COMP)
			v = -v;
		else if (mode == MODE_1COMP)
			v = ~v;
		else // if (mode == MODE_SGNMANT)
			v ^= tbm;
	} else
		*sign = 0;
    return mask_value(v);}
}

/* Helper routine to construct a value from the magnitude and sign
 */
long long int build_value(const unsigned long long int x, const int sign) {
	const enum arithmetic_modes mode = int_mode();
	long long int v = mask_value(x);

	if (sign == 0 || mode == MODE_UNSIGNED)
		return v;

	if (mode == MODE_2COMP)
		return mask_value(-(signed long long int)v);
	if (mode == MODE_1COMP)
		return mask_value(~v);
	return v | topbit_mask();
}


/* Helper routine for addition and subtraction that detemines the proper
 * setting for the overflow bit.  This routine should only be called when
 * the signs of the operands are the same for addition and different
 * for subtraction.  Overflow isn't possible if the signs are opposite.
 * The arguments of the operator should be passed in after conversion
 * to positive unsigned quantities nominally in two's complement.
 */
static int calc_overflow(unsigned long long int xv,
		unsigned long long int yv, enum arithmetic_modes mode, int neg) {
	unsigned long long int tbm = topbit_mask();
	unsigned long long int u;
	int i;

	switch (mode) {
	case MODE_UNSIGNED:
		// C doesn't expose the processor's status bits to us so we
		// break the addition down so we don't lose the overflow.
		u = (yv & (tbm-1)) + (xv & (tbm-1));
		i = ((u & tbm)?1:0) + ((xv & tbm)?1:0) + ((yv & tbm)?1:0);
		if (i > 1)
			break;
		return 0;

	case MODE_2COMP:
		u = xv + yv;
		if (neg && u == tbm)
			return 0;
		if (tbm & u)
			break;
		if ((xv == tbm && yv !=0) || (yv == tbm && xv != 0))
			break;
		return 0;

	case MODE_SGNMANT:
	case MODE_1COMP:
		if (tbm & (xv + yv))
			break;
		return 0;
	}
	set_overflow(1);
	return 1;
}


long long int intAdd(long long int y, long long int x) {
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	const enum arithmetic_modes mode = int_mode();
	long long int v;
	int overflow;

	set_overflow(0);
	if (sx == sy)
		overflow = calc_overflow(xv, yv, mode, sx);
	else
		overflow = 0;

	if (mode == MODE_SGNMANT) {
	

    const long long int tbm = topbit_mask();
	const long long int x2 = (x & tbm)?-(x ^ tbm):x;
	const long long int y2 = (y & tbm)?-(y ^ tbm):y;

    set_carry(overflow);

	v = y2 + x2;
	if (v & tbm)
		v = -v | tbm;
	} else {
		int carry;
		const unsigned long long int u = mask_value(y + x);

		if (u < (unsigned long long int)mask_value(y)) {
			set_carry(1);
			carry = 1;
		} else {
			set_carry(0);
			carry = 0;
		}

		v = y + x;
		if (carry && mode == MODE_1COMP)
			v++;
	}
	return mask_value(v);
}

long long int intSubtract(long long int y, long long int x) {
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	const enum arithmetic_modes mode = int_mode();
	long long int v;

	set_overflow(0);
	if (sx != sy)
		calc_overflow(xv, yv, mode, sy);

	if (mode == MODE_SGNMANT) {
		set_carry((sx == 0 && sy == 0 && xv > yv) ||
				(sx != 0 && sy != 0 && xv < yv));

        {const long long int tbm = topbit_mask();
		const long long int x2 = (x & tbm)?-(x ^ tbm):x;
		const long long int y2 = (y & tbm)?-(y ^ tbm):y;

		v = y2 - x2;
		if (v & tbm)
            v = -v | tbm;}
	} else {
		int borrow;

		if ((unsigned long long int)y < (unsigned long long int)x) {
			set_carry(1);
			if (mode == MODE_UNSIGNED)
				set_overflow(1);
			borrow = 1;
		} else {
			set_carry(0);
			borrow = 0;
		}

		v = y - x;
		if (borrow && mode == MODE_1COMP)
			v--;
	}
	return mask_value(v);
}

long long int intMultiply(long long int y, long long int x) {
	const enum arithmetic_modes mode = int_mode();
	unsigned long long int u;
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	long long int v;

	u = mask_value(xv * yv);
	set_overflow(u / yv != xv);

	if (mode == MODE_UNSIGNED)
		v = u;
	else {
		const long long int tbm = topbit_mask();
		if (u & tbm)
			set_overflow(1);
		if (sx ^ sy) {
			if (mode == MODE_2COMP)
				v = -u;
			else if (mode == MODE_1COMP)
				v = ~u;
			else // if (mode == MODE_SGNMANT)
				v = u ^ tbm;
		} else
			v = u;
	}
	return mask_value(v);
}

static void err_div0(unsigned long long int num, int sn, int sd) {
	if (num == 0)
		err(ERR_DOMAIN);
	else if (sn == sd)
		err(ERR_INFINITY);
	else
		err(ERR_MINFINITY);
}

long long int intDivide(long long int y, long long int x) {
	const enum arithmetic_modes mode = int_mode();
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	unsigned long long int r;
	long long int tbm;
	long long int v;

	if (xv == 0) {
		err_div0(yv, sy, sx);
		return 0;
	}
	set_overflow(0);
	r = mask_value(yv / xv);
	// Set carry if there is a remainder
	set_carry(r * xv != yv);

	if (mode == MODE_UNSIGNED)
		v = r;
	else {
		tbm = topbit_mask();
		if (r & tbm)
			set_carry(1);
		// Special case for 0x8000...00 / -1 in 2's complement
		if (mode == MODE_2COMP && sx && xv == 1 && y == tbm)
			set_overflow(1);
		if (sx ^ sy) {
			if (mode == MODE_2COMP)
				v = -r;
			else if (mode == MODE_1COMP)
				v = ~r;
			else // if (mode == MODE_SGNMANT)
				v = r ^ tbm;
		} else
			v = r;
	}
	return mask_value(v);
}

long long int intMod(long long int y, long long int x) {
	const enum arithmetic_modes mode = int_mode();
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	unsigned long long int r;
	long long int v;

	if (xv == 0) {
		err_div0(yv, sy, sx);
		return 0;
	}
	r = yv % xv;

	if (sy) {
		switch (mode) {
		default:
		case MODE_2COMP:    v = -r;                 break;
		case MODE_1COMP:    v = ~r;                 break;
		case MODE_UNSIGNED: v = r;                  break;
		case MODE_SGNMANT:  v = r ^ topbit_mask();  break;
		}
	} else
		v = r;
	return mask_value(v);
}


long long int intMin(long long int y, long long int x) {
	int sx, sy;
	const unsigned long long int xv = extract_value(x, &sx);
	const unsigned long long int yv = extract_value(y, &sy);

	if (sx != sy) {			// different signs
		if (sx)
			return x;
	} else if (sx) {		// both negative
		if (xv > yv)
			return x;
	} else {			// both positive
		if (xv < yv)
			return x;
	}
	return y;
}

long long int intMax(long long int y, long long int x) {
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);

	if (sx != sy) {			// different signs
		if (sx)
			return y;
	} else if (sx) {		// both negative
		if (xv > yv)
			return y;
	} else {			// both positive
		if (xv < yv)
			return y;
	}
	return x;
}


#ifdef INCLUDE_MULADD
long long int intMAdd(long long int z, long long int y, long long int x) {
	long long int t = intMultiply(x, y);
	const int of = get_overflow();

	t = intAdd(t, z);
	if (of)
		set_overflow(1);
	return t;
}
#endif


static unsigned long long int int_gcd(unsigned long long int a, unsigned long long int b) {
	while (b != 0) {
		const unsigned long long int t = b;
		b = a % b;
		a = t;
	}
	return a;
}

long long int intGCD(long long int y, long long int x) {
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	unsigned long long int v;
	const int sign = sx != sy;

	if (xv == 0)
		v = yv;
	else if (yv == 0)
		v = xv;
	else
		v = int_gcd(xv, yv);
	return build_value(v, sign);
}

long long int intLCM(long long int y, long long int x) {
	int sx, sy;
	unsigned long long int xv = extract_value(x, &sx);
	unsigned long long int yv = extract_value(y, &sy);
	unsigned long long int gcd;
	const int sign = sx != sy;

	if (xv == 0 || yv == 0)
		return build_value(0, sign);
	gcd = int_gcd(xv, yv);
	return intMultiply(mask_value(xv / gcd), build_value(yv, sign));
}

long long int intSqr(long long int x) {
	return intMultiply(x, x);
}

#ifdef INCLUDE_CUBES
long long int intCube(long long int x) {
	long long int y = intMultiply(x, x);
	int overflow = get_overflow();

	y = intMultiply(x, y);
	if (overflow)
		set_overflow(1);
	return y;
}
#endif


long long int intChs(long long int x) {
	long long int y;

	set_overflow(0);
	switch (int_mode()) {
	case MODE_UNSIGNED:
		set_overflow(1);
	default:
	case MODE_2COMP:
		if (x == topbit_mask())
			set_overflow(1);
		y = -x;
		break;

	case MODE_1COMP:
		y = ~x;
		break;

	case MODE_SGNMANT:
		y = x ^ topbit_mask();
		break;
	}
	return mask_value(y);
}

long long int intAbs(long long int x) {
	set_overflow(0);
	switch (int_mode()) {
	case MODE_UNSIGNED:
		break;

	case MODE_2COMP:
		if (x > 0)
			break;
		if (x == topbit_mask())
			set_overflow(1);
		x = -x;
		break;

	case MODE_1COMP:
		if (x & topbit_mask())
			x = ~x;
		break;

	case MODE_SGNMANT:
		if (x & topbit_mask())
			x = x ^ topbit_mask();
		break;
	}
	return mask_value(x);
}

static void breakup(unsigned long long int x, unsigned short xv[4]) {
	xv[0] = x & 0xffff;
	xv[1] = (x >> 16) & 0xffff;
	xv[2] = (x >> 32) & 0xffff;
	xv[3] = (x >> 48) & 0xffff;
}

static unsigned long long int packup(unsigned short int x[4]) {
	return (((unsigned long long int)x[3]) << 48) |
			(((unsigned long long int)x[2]) << 32) |
			(((unsigned long int)x[1]) << 16) |
			x[0];
}

void intDblMul(decimal64 *nul1, decimal64 *nul2, decContext *ctx64) {
	unsigned long long int xv, yv;
	int s;	
	unsigned short int xa[4], ya[4];
	unsigned int t[8];
	unsigned short int r[8];
	int i, j;

	{
		long long int xr, yr;
		int sx, sy;

		xr = d64toInt(&regX);
		yr = d64toInt(&regY);

		xv = extract_value(xr, &sx);
		yv = extract_value(yr, &sy);

		s = sx != sy;
	}

	/* Do the multiplication by breaking the values into unsigned shorts
	 * multiplying them all out and accumulating into unsigned ints.
	 * Then perform a second pass over the ints to propogate carry.
	 * Finally, repack into unsigned long long ints.
	 *
	 * This isn't terribly efficient especially for shorter word
	 * sizes but it works.  Special cases for WS <= 16 and/or WS <= 32
	 * might be worthwhile since the CPU supports these multiplications
	 * natively.
	 */
	breakup(xv, xa);
	breakup(yv, ya);

	for (i=0; i<8; i++)
		t[i] = 0;

	for (i=0; i<4; i++)
		for (j=0; j<4; j++)
			t[i+j] += xa[i] * ya[j];

	for (i=0; i<8; i++) {
		if (t[i] >= 65536)
			t[i+1] += t[i] >> 16;
		r[i] = t[i];
	}

	yv = packup(r);
	xv = packup(r+4);

	i = word_size();
	if (i != 64)
		xv = (xv << (64-i)) | (yv >> i);

	setlastX();
	d64fromInt(&regY, mask_value(yv));
	d64fromInt(&regX, build_value(xv, s));
	set_overflow(0);
}


static int nlz(unsigned short int x) {
   int n;

   if (x == 0)
	   return 16;
   n = 0;
   if (x <= 0x00ff) {n = n + 8; x = x << 8;}
   if (x <= 0x0fff) {n = n + 4; x = x << 4;}
   if (x <= 0x3fff) {n = n + 2; x = x << 2;}
   if (x <= 0x7fff) {n = n + 1;}
   return n;
}

/* q[0], r[0], u[0], and v[0] contain the LEAST significant halfwords.
(The sequence is in little-endian order).

This first version is a fairly precise implementation of Knuth's
Algorithm D, for a binary computer with base b = 2**16.  The caller
supplies
   1. Space q for the quotient, m - n + 1 halfwords (at least one).
   2. Space r for the remainder (optional), n halfwords.
   3. The dividend u, m halfwords, m >= 1.
   4. The divisor v, n halfwords, n >= 2.
The most significant digit of the divisor, v[n-1], must be nonzero.  The
dividend u may have leading zeros; this just makes the algorithm take
longer and makes the quotient contain more leading zeros.  A value of
NULL may be given for the address of the remainder to signify that the
caller does not want the remainder.
   The program does not alter the input parameters u and v.
   The quotient and remainder returned may have leading zeros.
   For now, we must have m >= n.  Knuth's Algorithm D also requires
that the dividend be at least as long as the divisor.  (In his terms,
m >= 0 (unstated).  Therefore m+n >= n.) */

static void divmnu(unsigned short q[], unsigned short r[],
		const unsigned short u[], const unsigned short v[],
		const int m, const int n) {
	const unsigned int b = 65536;			// Number base (16 bits).
	unsigned qhat;            			// Estimated quotient digit.
	unsigned rhat;            			// A remainder.
	unsigned p;               			// Product of two digits.
	int s, i, j, t, k;
	unsigned short vn[8];				// Normalised denominator
	unsigned short un[18];				// Normalised numerator

	if (n == 1) {                        		// Take care of
		k = 0;                            	// the case of a
		for (j = m - 1; j >= 0; j--) {		// single-digit
			q[j] = (k*b + u[j])/v[0];	// divisor here.
			k = (k*b + u[j]) - q[j]*v[0];
		}
		r[0] = k;
		return;
	}

	// Normalize by shifting v left just enough so that
	// its high-order bit is on, and shift u left the
	// same amount.  We may have to append a high-order
	// digit on the dividend; we do that unconditionally.

	s = nlz(v[n-1]);       				 // 0 <= s <= 16.
	for (i = n - 1; i > 0; i--)
		vn[i] = (v[i] << s) | (v[i-1] >> (16-s));
	vn[0] = v[0] << s;

	un[m] = u[m-1] >> (16-s);
	for (i = m - 1; i > 0; i--)
		un[i] = (u[i] << s) | (u[i-1] >> (16-s));
	un[0] = u[0] << s;

	for (j = m - n; j >= 0; j--) {       		// Main loop.
	// Compute estimate qhat of q[j].
	qhat = (un[j+n]*b + un[j+n-1])/vn[n-1];
	rhat = (un[j+n]*b + un[j+n-1]) - qhat*vn[n-1];
	again:
	if (qhat >= b || qhat*vn[n-2] > b*rhat + un[j+n-2]) {
		qhat = qhat - 1;
		rhat = rhat + vn[n-1];
		if (rhat < b) goto again;
	}

	// Multiply and subtract.
	k = 0;
	for (i = 0; i < n; i++) {
		p = qhat*vn[i];
		t = un[i+j] - k - (p & 0xFFFF);
		un[i+j] = t;
		k = (p >> 16) - (t >> 16);
	}
	t = un[j+n] - k;
	un[j+n] = t;

	q[j] = qhat;            			// Store quotient digit.
	if (t < 0) {              			// If we subtracted too
		q[j] = q[j] - 1;       			// much, add back.
		k = 0;
		for (i = 0; i < n; i++) {
			t = un[i+j] + vn[i] + k;
			un[i+j] = t;
			k = t >> 16;
		}
		un[j+n] = un[j+n] + k;
		}
	} // End j.
	// Unnormalize remainder
	for (i = 0; i < n; i++)
		r[i] = (un[i] >> s) | (un[i+1] << (16-s));
}

static unsigned long long int divmod(const long long int z, const long long int y,
		const long long int x, int *sx, int *sy, unsigned long long *rem) {
	unsigned long long int d, h, l;
	const int ws = word_size();
	unsigned short denom[4];
	unsigned short numer[8];
	unsigned short quot[5];
	unsigned short rmdr[4];
	int num_denom;
	int num_numer;

	l = (unsigned long long int)z;		// Numerator low
	h = extract_value(y, sy);		// Numerator high
	d = extract_value(x, sx);		// Demonimator
	if (d == 0) {
		err_div0(h|l, *sx, *sy);
		return 0;
	}

	if (ws != 64) {
		l |= h >> (64 - ws);
		h >>= (64 - ws);
	}

	if (d == 0) {					// divide by zero
		set_overflow(1);
		*rem = 0;
		return 0;
	} else
		set_overflow(0);
	if (h == 0 && l == 0) {				// zero over
		*rem = 0;
		return 0;
	}

	xset(quot, 0, sizeof(quot));
	xset(rmdr, 0, sizeof(rmdr));

	breakup(d, denom);
	breakup(l, numer);
	breakup(h, numer+4);

	for (num_denom = 4; num_denom > 1 && denom[num_denom-1] == 0; num_denom--);
	for (num_numer = 8; num_numer > num_denom && numer[num_numer-1] == 0; num_numer--);

	divmnu(quot, rmdr, numer, denom, num_numer, num_denom);

	*rem = packup(rmdr);
	return packup(quot);
}

long long int intDblDiv(long long int z, long long int y, long long int x) {
	unsigned long long int q, r;
	int sx, sy;

	q = divmod(z, y, x, &sx, &sy, &r);
	set_carry(r != 0);
	return build_value(q, sx != sy);
}

long long int intDblRmdr(long long int z, long long int y, long long int x) {
	unsigned long long int r;
	int sx, sy;

	divmod(z, y, x, &sx, &sy, &r);
	return build_value(r, sy);
}


long long int intNot(long long int x) {
	return mask_value(~x);
}

long long int intAnd(long long int y, long long int x) {
	return mask_value(y & x);
}

long long int intOr(long long int y, long long int x) {
	return mask_value(y | x);
}

long long int intXor(long long int y, long long int x) {
	return mask_value(y ^ x);
}

long long int intNand(long long int y, long long int x) {
	return mask_value(~(y & x));
}

long long int intNor(long long int y, long long int x) {
	return mask_value(~(y | x));
}

long long int intEquiv(long long int y, long long int x) {
	return mask_value(~(y ^ x));
}

/* Fraction and integer parts are very easy for integers.
 */
long long int intFP(long long int x) {
	return 0;
}

long long int intIP(long long int x) {
	return mask_value(x);
}


long long int intSign(long long int x) {
	int sgn;
	unsigned long long int v = extract_value(x, &sgn);

	if (v == 0)
		sgn = 0;
	else
		v = 1;
	return build_value(v, sgn);
}


/* Single bit shifts are special internal version.
 * The multi-bit shifts vector through these.
 */

static long long int intLSL(long long int x) {
	set_carry(topbit_mask() & x);
	return mask_value((x << 1) & ~1);
}

static long long int intLSR(long long int x) {
	set_carry(x & 1);
	return mask_value((x >> 1) & ~topbit_mask());
}

static long long int intASR(long long int x) {

	const long long int tbm = topbit_mask();
    	set_carry(x & 1);
	if (x & tbm) {
		return mask_value((x >> 1) | tbm);
	} else {
		return mask_value((x >> 1) & ~tbm);
	}
}

static long long int intRL(long long int x) {
	const int cry = (topbit_mask() & x)?1:0;

	set_carry(cry);
	return mask_value(intLSL(x) | cry);
}

static long long int intRR(long long int x) {
	const int cry = x & 1;

	set_carry(cry);
	x = intLSR(x);
	if (cry)
		x |= topbit_mask();
	return mask_value(x);
}

static long long int intRLC(long long int x) {
	const int cin = get_carry();
	set_carry((topbit_mask() & x)?1:0);
	return mask_value(intLSL(x) | cin);
}

static long long int intRRC(long long int x) {
	const int cin = get_carry();

	set_carry(x&1);
	x = intLSR(x);
	if (cin)
		x |= topbit_mask();
	return mask_value(x);
}


/* Like the above but taking the count argument from the opcode.
 * Also possibly register indirect but that is dealt with elsewhere.
 */
void introt(unsigned arg, enum rarg op) {
	if (!is_intmode()) {
		err(ERR_BAD_MODE);
		return;
	}
    {long long int (*f)(long long int);
	int mod;
	const int ws = word_size();
	long long int x = d64toInt(&regX);
	unsigned int i;

	if (arg != 0) {
		switch (op) {
		case RARG_RL:	f = &intRL;	mod = ws;	break;
		case RARG_RR:	f = &intRR;	mod = ws;	break;
		case RARG_RLC:	f = &intRLC;	mod = ws + 1;	break;
		case RARG_RRC:	f = &intRRC;	mod = ws + 1;	break;
		case RARG_SL:	f = &intLSL;	mod = 0;	break;
		case RARG_SR:	f = &intLSR;	mod = 0;	break;
		case RARG_ASR:	f = &intASR;	mod = 0;	break;
		default:
			return;
		}
		if (arg > ws) {
			if (mod)
				arg = arg % mod;
			else
				arg = ws;
		}
		for (i=0; i<arg; i++)
			x = (*f)(x);
	}
    d64fromInt(&regX, mask_value(x));}
}


/* Some code to count bits.  We start with a routine to count bits in a single
 * 32 bit word and call this twice.
 */
static unsigned int count32bits(unsigned long int v) {
	v = v - ((v >> 1) & 0x55555555);
	v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
	return (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
}

static unsigned int count64bits(long long int x) {
	return count32bits(x & 0xffffffff) + count32bits((x >> 32) & 0xffffffff);
}

long long int intNumBits(long long int x) {
	return mask_value(count64bits(x));
}


/* Integer floor(sqrt())
 */
long long int intSqrt(long long int x) {
	int sx;
	unsigned long long int v = extract_value(x, &sx);

	if (sx) {
		err(ERR_DOMAIN);
		return 0;
	}
	if (v == 0) {
		set_carry(0);
		return 0;
	}
    {unsigned long long int n0 = v / 2 + 1;
	unsigned long long int n1 = v / n0 + n0 / 2;
	while (n1 < n0) {
		n0 = n1;
		n1 = (n0 + v / n0) / 2;
	}
	set_carry((n1 * n1 != v)?1:0);
    return build_value(n1, sx);}
}

#ifdef INCLUDE_CUBES
/* Integer cube root
 */
long long int intCubeRoot(long long int v) {
	int sx;
	unsigned long long int w = extract_value(v, &sx);
	unsigned long long int x, y, b, bs, y2;
	int s;

	if (w == 0) {
		set_carry(0);
		return 0;
	}
	x = w;
	y2 = 0;
	y = 0;
	for (s=63; s>=0; s -= 3) {
		y2 = 4*y2;
		y = 2*y;
		b = 3*(y2 + y) + 1;
		bs = b << s;
		if (x >= bs && b == (bs >> s)) {
			x -= bs;
			y2 += 2*y + 1;
			y++;
		}
	}
	set_carry((y*y*y != w)?1:0);
	return build_value(y, sx);
}
#endif

/* Integer power y^x
 */
long long int intPower(long long int y, long long int x) {
	int sx, sy, sr;
	unsigned long long int vx = extract_value(x, &sx);
	unsigned long long int vy = extract_value(y, &sy);
	unsigned long long int r = 1;
	int ws, i;

	if (vx == 0 && vy == 0) {
		err(ERR_DOMAIN);
		return 0;
	}
	set_carry(0);
	set_overflow(0);

	if (x == 0) {
		if (y == 0) {
			set_overflow(1);
			return 0;
		}
		return 1;
	} else if (y == 0)
		return 0;

	if (sx) {
		set_carry(1);
		return 0;
	}

	sr = (sy && (vx & 1))?1:0;	// Determine the sign of the result

	ws = word_size();
	for (i=0; i<ws && vx != 0; i++) {
		if (vx & 1)
			r *= vy;
		vx >>= 1;
		vy *= vy;
	}
	return build_value(r, sr);
}


/* Integer floor(log2())
 */
long long int intLog2(long long int x) {
	int sx;
	unsigned long long int v = extract_value(x, &sx);
	unsigned int r = 0;

	if (v == 0 || sx) {
		err(ERR_DOMAIN);
		return 0;
	}
	set_carry((v & (v-1))?1:0);
	if (v != 0)
		while (v >>= 1)
			r++;
	return build_value(r, sx);
}


/* 2^x
 */
long long int int2pow(long long int x) {
	int sx;
	unsigned long long int v = extract_value(x, &sx);

	set_carry(0);
	set_overflow(0);
	if (sx)
		return 0;

    {unsigned int ws = word_size();
	if (int_mode() != MODE_UNSIGNED)
		ws--;
	if (v >= ws) {
		set_carry(v == ws);
		set_overflow(1);
		return 0;
	}

    return 1LL << (unsigned int)(v & 0xff);}
}


/* Integer floor(log10())
 */
long long int intLog10(long long int x) {
	int sx;
	unsigned long long int v = extract_value(x, &sx);
	int r = 0;
	int c = 0;

	if (v == 0 || sx) {
		err(ERR_DOMAIN);
		return 0;
	}
	while (v >= 10) {
		r++;
		if (v % 10)
			c = 1;
		v /= 10;
	}
	set_carry(c || v != 1);
	return build_value(r, sx);
}


/* 10^x
 */
long long int int10pow(long long int x) {
	const long long int r = intPower(10, x);

	set_overflow(intLog10(r) != x);

	return r;
}


/* Mirror - reverse the bits in the word
 */
long long int intMirror(long long int x) {
	long long int r = 0;
	int n = word_size();
	int i;

	if (x == 0)
		return 0;

	for (i=0; i<n; i++)
		if (x & (1LL << i))
			r |= 1LL << (n-i-1);
	return r;
}


/* Justify to the end of the register
 */
static void justify(decimal64 *ct,
			long long int (*shift)(long long int),
			const long long int mask) {
	unsigned int c = 0;
	long long int v = d64toInt(&regY);

	if (v != 0) {
		const int flags = save_flags();
		while ((v & mask) == 0) {
			v = (*shift)(v);
			c++;
		}
		restore_flags(flags);
		d64fromInt(&regY, v);
	}
	d64fromInt(ct, (long long int)c);
}

void intLJ(decimal64 *x, decimal64 *nul, decContext *ctx) {
	justify(x, &intLSL, topbit_mask());
}

void intRJ(decimal64 *x, decimal64 *nul, decContext *ctx) {
	justify(x, &intLSR, 1LL);
}


/* Create n bit masks at either end of the word.
 * If the n is negative, the mask is created at the other end of the
 * word.
 */
void intmsks(unsigned arg, enum rarg op) {
	long long int mask;
	long long int x;
	unsigned int i;
	long long int (*f)(long long int);

	lift();

	if (op == RARG_MASKL) {
		mask = topbit_mask();
		f = &intLSR;
	} else {
		mask = 1LL;
		f = &intLSL;
	}
	if (arg >= word_size()) {
		x = mask_value(-1);
	} else {
		x = 0;
		for (i=0; i<arg; i++) {
			x |= mask;
			mask = (*f)(mask);
		}
	}
	d64fromInt(&regX, x);
}


/* Set, clear, flip and test bits */
void intbits(unsigned arg, enum rarg op) {
	if (!is_intmode()) {
		err(ERR_BAD_MODE);
		return;
	}
    {const long long int m =  (arg >= word_size())?0:(1LL << arg);
	long long int x = d64toInt(&regX);

	switch (op) {
	case RARG_SB:	x |= m;		break;
	case RARG_CB:	x &= ~m;	break;
	case RARG_FB:	x ^= m;		break;
	case RARG_BS:	fin_tst((x&m)?1:0);			break;
	case RARG_BC:	fin_tst((m != 0 && (x&m) != 0)?0:1);	break;
	default:
		return;
	}

    d64fromInt(&regX, x);}
}

long long int intFib(long long int x) {
	int sx, s;
	unsigned long long int v = extract_value(x, &sx);
	const enum arithmetic_modes mode = int_mode();
	unsigned long long int a0, a1;
	unsigned int n, i;
	long long int tbm;

	/* Limit things so we don't loop for too long.
	 * The actual largest non-overflowing values for 64 bit integers
	 * are Fib(92) for signed quantities and Fib(93) for unsigned.
	 * We allow a bit more and maintain the low order digits.
	 */
	if (v >= 100) {
		set_overflow(1);
		return 0;
	}
	set_overflow(0);
	n = v & 0xff;
	if (n <= 1)
		return build_value(n, 0);

	/* Negative integers produce the same values as positive
	 * except the sign for negative evens is negative.
	 */
	s = (sx && (n & 1) == 0)?1:0;

	/* Mask to check for overflow */
	tbm = topbit_mask();
	if (mode == MODE_UNSIGNED)
		tbm <<= 1;

	/* Down to the computation.
	 */
	a0 = 0;
	a1 = 1;
	for (i=1; i<n; i++) {
		const unsigned long long int anew = a0 + a1;
		if ((anew & tbm) || anew < a1)
			set_overflow(1);
		a0 = a1;
		a1 = anew;
	}
	return build_value(a1, s);
}


/* Calculate (a . b) mod c taking care to avoid overflow */
static unsigned long long mulmod(const unsigned long long int a, unsigned long long int b, const unsigned long long int c) {
	unsigned long long int x=0, y=a%c;
	while (b > 0) {
		if ((b & 1))
			x = (x+y)%c;
		y = (y+y)%c;
		b /= 2;
	}
	return x % c;
}

/* Calculate (a ^ b) mod c */
static int modulo(const unsigned long long int a, unsigned long long int b, const unsigned long long int c) {
	unsigned long long int x=1, y=a;
	while (b > 0) {
		if ((b & 1))
			x = mulmod(x, y, c);
		y = mulmod(y, y, c);
		b /= 2;
	}
	return x % c;
}

/* Test if a number is prime or not using a Miller-Rabin test */
int isPrime(unsigned long long int p) {
	int i;
	unsigned long long int s;
	int a, step;
	static const unsigned char primes[] = {
		3, 5, 7, 11, 13, 17, 19, 23, 29, 31,
		37, 41, 43, 47, 53, 59, 61, 67, 71, 73,
		79, 83, 89, 97, 101, 103, 107, 109, 113, 127,
		131, 137, 139, 149, 151, 157, 163, 167, 173, 179,
		181, 191, 193, 197, 199, 211, 223, 227, 229, 233,
		239, 241, 251, /* 257 */
	};
#define N_PRIMES	(sizeof(primes) / sizeof(unsigned char))
#define QUICK_CHECK	(257*257-1)
#define PRIME_ITERATION	40

	/* Quick check for p <= 2 and evens */
	if (p < 2)	return 0;
	if (p == 2)	return 1;
	if ((p&1) == 0)	return 0;

	/* Quick check for divisibility by small primes */
	for (i=0; i<N_PRIMES; i++)
		if (p == primes[i])
			return 1;
		else if ((p % primes[i]) == 0)
			return 0;
	if (p < QUICK_CHECK)
		return 1;

	a = 4;
	step = ((QUICK_CHECK - a) / PRIME_ITERATION);
	if ((step & 1) == 0)
		step--;

	s = p - 1;
	while ((s&1) == 0)
		s /= 2;

	for(i=0; i<PRIME_ITERATION; i++) {
		unsigned long long int temp = s;
		unsigned long long int mod = modulo(a, temp, p);
		while (temp != p-1 && mod != 1 && mod != p-1) {
			mod = mulmod(mod, mod, p);
			temp += temp;
		}
		if(mod!=p-1 && temp%2==0)
			return 0;
		a += step;
	}
	return 1;
}
