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

#ifndef REALBUILD
#if defined(WIN32) && !defined(QTGUI) && !defined(__GNUC__)
#include <stdlib.h>  // sleep
#include "win32.h"
#define sleep _sleep
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include <stdio.h>   // (s)printf
#endif // REALBUILD

#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 6
#define GNUC_POP_ERROR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#define XEQ_INTERNAL 1
#include "xeq.h"
#include "storage.h"
#include "decn.h"
#include "complex.h"
#include "stats.h"
#include "display.h"
#include "consts.h"
#include "int.h"
#include "date.h"
#include "lcd.h"
#include "xrom.h"
#include "alpha.h"

/* Define the number of program Ticks that must elapse between flashing the
 * RCL annunciator.
 */
#define TICKS_PER_FLASH	(5)

/*
 *  A program is running
 */
int Running;

#ifndef CONSOLE
/*
 *  A program has just stopped
 */
int JustStopped;
#endif

/*
 *  Stopwatch for a programmed pause
 */
volatile int Pause;

/*
 *  Some long running function has called busy();
 */
int Busy;

/*
 *  Error code
 */
int Error;

/*
 *  Indication of PC wrap around
 */
int PcWrapped;

/*
 *  Currently executed function
 */
s_opcode XeqOpCode;

/*
 *  Temporary display (not X)
 */
int ShowRegister;

/*
 *  User code being called from XROM
 */
unsigned short XromUserPc;
unsigned short UserLocalRegs;

/* We need various different math contexts.
 * More efficient to define these globally and reuse them as needed.
 */
decContext Ctx;

/*
 * A buffer for instruction display
 */
char TraceBuffer[25];

/*
 *  Total Size of the return stack
 */
int RetStkSize;

/*
 *  Number of remaining program steps
 */
int ProgFree;

/*
 * The actual top of the return stack
 */
unsigned short *RetStk;

/*
 *  Shift the return stack.
 *  The distance is in levels.
 *  If argument is negative, return stack will shrink.
 *  Returns 1 if unsuccessful (error is set)
 */
int move_retstk(int distance)
{
	if (RetStkSize + RetStkPtr + distance < 0) {
		err(ERR_RAM_FULL);
		return 1;
	}
	xcopy(RetStk + distance, RetStk, (-RetStkPtr) << 1);
	RetStk += distance;
	RetStkSize += distance;
	return 0;
}

/*
 *  How many stack levels with local data have we?
 */
int local_levels(void) {
	return LocalRegs < 0 ? LOCAL_LEVELS(RetStk[LocalRegs]) : 0;
}

/*
 *  How many local registers have we?
 */
int local_regs(void) {
	const int l = local_levels();
	const int n = l == 1 ? NUMXREGS : l >> 2;
#ifdef INCLUDE_DOUBLE_PRECISION
	if (is_dblmode())
		return n >> 1;
#endif
	return n;
}

#ifdef INCLUDE_DOUBLE_PRECISION
/*
 *  How many global registers have we?
 */
unsigned int global_regs(void) {
	if (is_dblmode())
		return NumRegs >> 1;
	return NumRegs;
}
#endif

#ifdef INCLUDE_DOUBLE_PRECISION
/*
 *  How many special registers have we?
 */
static int special_regs(void) {
	if (is_dblmode())
		return (STACK_SIZE + EXTRA_REG) >> 1;
	return STACK_SIZE + EXTRA_REG;
}
#else
#define special_regs(void) (STACK_SIZE + EXTRA_REG)
#endif

#ifdef CONSOLE
// Console screen only
unsigned int get_local_flags(void) {
	if (LocalRegs == 0)
		return 0;
	return RetStk[LocalRegs + 1];
}
#endif

void version(enum nilop op) {
	State2.version = 1;
	if (!State2.runmode)
		display();
}

void cmd_off(enum nilop op) {
	shutdown();
}

#ifndef state_pc
unsigned int state_pc(void) {
	return State.pc;	
}
#endif
static void raw_set_pc(unsigned int pc) {
	State.pc = pc;
	update_program_bounds(0);
}

/*
 *  Where do the program regions start?
 */
static const s_opcode *const RegionTab[] = {
	Prog,
	UserFlash.prog,
	BackupFlash._prog,
	xrom
};

/*
 *  Size of a program segment
 */
int sizeLIB(int region) {
	if (region == REGION_XROM)
		return xrom_size;
	else
		return (int)RegionTab[region][-1];
}


/*
 *  Get an opcode, check for double length codes
 */
static opcode get_opcode( const s_opcode *loc )
{
	opcode r = *loc;
	if ( isDBL(r) ) {
		r |= loc[1] << 16;
	}
	return r;
}


/* 
 * Return the program memory location specified.
 */
opcode getprog(unsigned int pc) {

	const int region = nLIB(pc);
	int offset = offsetLIB(pc);

	if (offset < 0 || offset >= sizeLIB(region))
		return OP_NIL | OP_END;
	return get_opcode(RegionTab[region] + offset);
}


/* 
 * Return the physical start-address of the current program
 */
const s_opcode *get_current_prog(void) {

	const int region = nLIB(ProgBegin);
	return RegionTab[region] + offsetLIB(ProgBegin);
}


/*
 *  Set PC with sanity check
 */
void set_pc(unsigned int pc) {
	if (isRAM(pc)) {
		if (pc > ProgSize)
			pc = ProgSize;
		if (pc > 1 && isDBL(Prog_1[pc - 1]))
			pc--;
	} else if (!isXROM(pc)) {
		const unsigned int n = startLIB(pc) + sizeLIB(nLIB(pc));
		if (pc > n - 1)
			pc = n - 1;
		if (pc > startLIB(pc) && isDBL(getprog(pc - 1)))
			--pc;
	}
	raw_set_pc(pc);
}



/* Locate the beginning and end of a section from a PC that points anywhere within
 */
static unsigned short int find_section_bounds(const unsigned int pc, const int endp, unsigned short int *const p_top) {
	unsigned short int top, bottom;

	if (endp && State2.runmode) { 
		// Use the current program as bounds
		top = ProgBegin;
		bottom = ProgEnd;
		if (top == 0)
			top = 1;
	}
	else if (isXROM(pc)) {
		top = addrXROM(1);
		bottom = addrXROM(xrom_size);
	} 
	else if (isLIB(pc)) {
		top = startLIB(pc);
		bottom = top + sizeLIB(nLIB(pc)) - 1;
	}
	else {
		top = State2.runmode;  // step 001 if not entering a program
		bottom = ProgSize;
	}
	*p_top = top;
	return bottom;
}


/* Increment the passed PC.  Account for wrap around but nothing else.
 * Return the updated PC.
 * Set PcWrapped on wrap around
 */
unsigned int do_inc(const unsigned int pc, int endp) {
	const unsigned short int npc = pc + 1 + isDBL(getprog(pc));
	unsigned short int top = 0;
	unsigned short int bottom = 0;

	PcWrapped = 0;
	bottom = find_section_bounds(pc, endp, &top);

	if (npc > bottom) {
		PcWrapped = 1;
		return top;
	}
	return npc;
}

/* Decrement the passed PC.  Account for wrap around but nothing else.
 * Return the updated PC.
 * Set PcWrapped on wrap around
 */
unsigned int do_dec(unsigned int pc, int endp) {
	unsigned short int top = 0;
	unsigned short int bottom = 0;

	PcWrapped = 0;
	bottom = find_section_bounds(pc, endp, &top);

	if (pc <= top) {
		PcWrapped = 1;
		pc = bottom;
	}
	else
		--pc;
	if (pc > top && isDBL(getprog(pc - 1)))
		--pc;
	return pc;
}

/* Increment the PC keeping account of wrapping around and stopping
 * programs on such.  Return non-zero if we wrapped.
 */
int incpc(void) {
	raw_set_pc(do_inc(state_pc(), 1));
	return PcWrapped;
}

void decpc(void) {
	raw_set_pc(do_dec(state_pc(), 1));
}

/*
 * Update the pointers to the current program delimited by END statements
 */
void update_program_bounds(const int force) {
	unsigned int pc = state_pc();
	if (pc == 0 && State2.runmode)
		State.pc = pc = 1;
	if (! force && pc >= ProgBegin && pc <= ProgEnd)
		return;
	for (PcWrapped = 0; !PcWrapped; pc = do_inc(pc, 0)) {
		ProgEnd = pc;
		if (getprog(pc) == (OP_NIL | OP_END)) {
			break;
		}
	}
	for (pc = state_pc();;) {
		const unsigned int opc = pc;
		pc = do_dec(opc, 0);
		if (PcWrapped || getprog(pc) == (OP_NIL | OP_END)) {
			ProgBegin = opc == 0 ? 1 : opc;
			break;
		}
	}
}

/* Determine where in program space the PC really is
 */
unsigned int user_pc(void) {
	unsigned int pc = state_pc();
	unsigned int n = 1;
	unsigned int base;

#ifndef REALBUILD
	if (pc == 0 || isXROM(pc))
		return offsetLIB(pc) + 1;
#else
	if (pc == 0)
		return 0;
#endif
	base = startLIB(pc);
	while (base < pc) {
		base = do_inc(base, 0);
		if (PcWrapped)
			return n;
		++n;
	}
	return n;
}

/* Given a target user PC, figure out the real matching PC
 */
unsigned int find_user_pc(unsigned int target) {
	unsigned int upc = state_pc();
	const int libp = isLIB(upc);
	unsigned int base = libp ? startLIB(upc) : 0;
	unsigned int n = libp ? 1 : 0;
#ifndef REALBUILD
	if (isXROM(upc))
		return addrXROM(target);
#endif
	while (n++ < target) {
		const unsigned int oldbase = base;
		base = do_inc(oldbase, 0);
		if (PcWrapped)
			return oldbase;
	}
	return base;
}


/* Set a flag to indicate that a complex operation has taken place
 * This only happens if we're not in a program.
 */
static void set_was_complex(void) {
	if (! Running)
		State2.wascomplex = 1;
}


/* Produce an error and stop
 */
int err(const enum errors e) {
	if (Error == ERR_NONE) {
		Error = e;
		error_message(e);
		return 1;
	}
	return e != ERR_NONE;
}


/* Display a warning
 */
int warn(const enum errors e) {
	if (Running) {
		return err(e);
	}
	error_message(e);
#ifndef CONSOLE
	State2.disp_freeze = 0;
	JustDisplayed = 1;
#endif
	return e != ERR_NONE;
}


/* Doing something in the wrong mode */
static void bad_mode_error(void) {
	err(ERR_BAD_MODE);
}


/* User command to produce an error */
void cmderr(unsigned int arg, enum rarg op) {
	err((enum errors) arg);
}


/* User command to display a warning */
void cmdmsg(unsigned int arg, enum rarg op) {
	error_message((enum errors) arg);
}


#if defined(DEBUG) && defined(CONSOLE)
#include <stdlib.h>
static void error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	exit(1);
}

#define illegal(op)	do { err(ERR_PROG_BAD); printf("illegal opcode 0x%08x\n", op); } while (0)
#else
#define illegal(op)	do { err(ERR_PROG_BAD); } while (0)
#endif

/* Real rounding mode access routine
 */
static enum rounding get_rounding_mode() {
	static const unsigned char modes[DEC_ROUND_MAX] = {
		DEC_ROUND_HALF_EVEN, DEC_ROUND_HALF_UP, DEC_ROUND_HALF_DOWN,
		DEC_ROUND_UP, DEC_ROUND_DOWN,
		DEC_ROUND_CEILING, DEC_ROUND_FLOOR
	};
	return (enum rounding) modes[UState.rounding_mode];
}

void op_roundingmode(enum nilop op) {
	setX_int_sgn(UState.rounding_mode, 0);
}

void rarg_roundingmode(unsigned int arg, enum rarg op) {
	UState.rounding_mode = arg;
}


/* Pack a number into our DPD register format
 */

void packed_from_number(decimal64 *r, const decNumber *x) {
	decContext ctx64;

	decContextDefault(&ctx64, DEC_INIT_DECIMAL64);
	ctx64.round = get_rounding_mode();
	decimal64FromNumber(r, x, &ctx64);
}

void packed128_from_number(decimal128 *r, const decNumber *x) {
	decContext ctx128;

	decContextDefault(&ctx128, DEC_INIT_DECIMAL128);
	ctx128.round = get_rounding_mode();
	decimal128FromNumber(r, x, &ctx128);
}

// Repack a decimal128 to decimal64
void packed_from_packed128(decimal64 *r, const decimal128 *s) {
	decNumber temp;
	packed_from_number(r, decimal128ToNumber(s, &temp));
}

#ifdef INCLUDE_DOUBLE_PRECISION
// Repack a decimal64 to decimal128
void packed128_from_packed(decimal128 *r, const decimal64 *s) {
	decNumber temp;
	packed128_from_number(r, decimal64ToNumber(s, &temp));
}
#endif

/*
 *  User command to round to a specific number of digits
 */
void rarg_round(unsigned int arg, enum rarg op) {
	decNumber res, x;

	setlastX();
	getX(&x);
	decNumberRoundDigits(&res, &x, arg, get_rounding_mode());
	setX(&res);
}

/* Check if a value is bogus and error out if so.
 */
static int check_special(const decNumber *x) {
	decNumber y;
	decimal64 z;
#ifdef INCLUDE_DOUBLE_PRECISION
	decimal128 d;
	if (is_dblmode()) {
		packed128_from_number(&d, x);
		decimal128ToNumber(&d, &y);
	}
	else 
#endif
	{
		packed_from_number(&z, x);
		decimal64ToNumber(&z, &y);
	}
	if (decNumberIsSpecial(&y)) {
		if (! get_user_flag(NAN_FLAG)) {
			if (decNumberIsNaN(&y))
				err(ERR_DOMAIN);
			else if (decNumberIsNegative(&y))
				err(ERR_MINFINITY);
			else
				err(ERR_INFINITY);
			return 1;
		}
	}
	return 0;
}


int stack_size(void) {
#ifdef INCLUDE_DOUBLE_PRECISION
	if (! UState.stack_depth || isXROM(state_pc()) || is_dblmode())
#else
	if (! UState.stack_depth || isXROM(state_pc()))
#endif
		return 4;
	return 8;
}

REGISTER *get_stack(int pos) {
	return get_reg_n(regX_idx + pos);
}

static REGISTER *get_stack_top(void) {
	return get_stack(stack_size()-1);
}

#ifdef INCLUDE_DOUBLE_PRECISION
void copyreg(REGISTER *d, const REGISTER *s) {
	xcopy(d, s, is_dblmode() ? sizeof(decimal128) : sizeof(decimal64));
}
#endif

void copyreg_n(int d, int s) {
	copyreg(get_reg_n(d), get_reg_n(s));
}

/* Lift the stack one level.
 */
void lift(void) {
	const int n = stack_size();
	int i;
	for (i=n-1; i>0; i--)
		copyreg(get_stack(i), get_stack(i-1));
}

static void lift_if_enabled(void) {
	if (State.state_lift)
		lift();
}

static void lift2_if_enabled(void) {
	lift_if_enabled();
	lift();
}

static void lower(void) {
	const int n = stack_size();
	int i;

	for (i=1; i<n; i++)
		copyreg(get_stack(i-1), get_stack(i));
}

static void lower2(void) {
	const int n = stack_size();
	int i;

	for (i=2; i<n; i++)
		copyreg(get_stack(i-2), get_stack(i));
}


void setlastX(void) {
	copyreg_n(regL_idx, regX_idx);
}

static void setlastXY(void) {
	setlastX();
	copyreg_n(regI_idx, regY_idx);
}


#ifdef INCLUDE_DOUBLE_PRECISION
decNumber *getRegister(decNumber *r, int index) {
	const REGISTER *const reg = get_reg_n(index);
	if (is_dblmode())
		decimal128ToNumber(&(reg->d), r);
	else
		decimal64ToNumber(&(reg->s), r);
	return r;
}

void setRegister(int index, const decNumber *x) {
	REGISTER *const reg = get_reg_n(index);
	decNumber dn;

	if (! check_special(x)) {
		decNumberNormalize(&dn, x, &Ctx);
		if (is_dblmode())
			packed128_from_number(&(reg->d), &dn);
		else
			packed_from_number(&(reg->s), &dn);
	}
}
#endif

decNumber *getX(decNumber *x) {
	return getRegister(x, regX_idx);
}

void setX(const decNumber *x) {
	setRegister(regX_idx, x);
}

void getY(decNumber *y) {
	getRegister(y, regY_idx);
}

void setY(const decNumber *y) {
	setRegister(regY_idx, y);
}

void setXY(const decNumber *x, const decNumber *y) {
	setX(x);
	setY(y);
}

static void getZ(decNumber *z) {
	getRegister(z, regZ_idx);
}

static void getT(decNumber *t) {
	getRegister(t, regT_idx);
}

void getXY(decNumber *x, decNumber *y) {
	getX(x);
	getY(y);
}

void getXYZ(decNumber *x, decNumber *y, decNumber *z) {
	getXY(x, y);
	getZ(z);
}

void getXYZT(decNumber *x, decNumber *y, decNumber *z, decNumber *t) {
	getXYZ(x, y, z);
	getT(t);
}

void getYZ(decNumber *y, decNumber *z) {
	getY(y);
	getZ(z);
}

void roll_down(enum nilop op) {
	REGISTER r;
	copyreg(&r, get_reg_n(regX_idx));
	lower();
	copyreg(get_stack_top(), &r);
}

void roll_up(enum nilop op) {
	REGISTER r;
	copyreg(&r, get_stack_top());
	lift();
	copyreg(get_reg_n(regX_idx), &r);
}

void cpx_roll_down(enum nilop op) {
	roll_down(OP_RDOWN);
	roll_down(OP_RDOWN);
}

void cpx_roll_up(enum nilop op) {
	roll_up(OP_RUP);
	roll_up(OP_RUP);
}

void cpx_enter(enum nilop op) {
	lift();
	lift();
	copyreg(get_reg_n(regY_idx), get_reg_n(regT_idx));
}

void cpx_fill(enum nilop op) {
	const int n = stack_size();
	const REGISTER *y = get_reg_n(regY_idx);
	int i;

	for (i=2; i<n; i++)
		copyreg(get_stack(i), (i & 1) ? y : get_reg_n(regX_idx));
}

void fill(enum nilop op) {
	const int n = stack_size();
	int i;

	for (i=1; i<n; i++)
		copyreg(get_stack(i), get_reg_n(regX_idx));
}

void drop(enum nilop op) {
	lower();
	if (op == OP_DROPXY)
		lower();
}


int is_intmode(void) {
	return UState.intm;
}

#ifdef INCLUDE_DOUBLE_PRECISION
int is_dblmode(void) {
	return ! UState.intm && State.mode_double;
}
#endif

void lead0(enum nilop op) {
	UState.leadzero = (op == OP_LEAD0) ? 1 : 0;
}


/* Convert a possibly signed string to an integer
 */
int s_to_i(const char *s) {
	int x = 0;
	int neg;

	if (*s == '-') {
		s++;
		neg = 1;
	} else {
		if (*s == '+')
			s++;
		neg = 0;
	}

	for (;;) {
		const char c = *s++;

		if (c < '0' || c > '9')
			break;
		x = 10 * x + (c - '0');
	}
	if (neg)
		return -x;
	return x;
}

/* Convert a string in the given base to an unsigned integer
 */
unsigned long long int s_to_ull(const char *s, unsigned int base) {
	unsigned long long int x = 0;

	for (;;) {
		unsigned int n;
		const char c = *s++;

		if (c >= '0' && c <= '9')
			n = c - '0';
		else if (c >= 'A' && c <= 'F')
			n = c - 'A' + 10;
		else
			break;
		if (n >= base)
			break;
		x = x * base + n;
	}
	return x;
}

const char *get_cmdline(void) {
	if (CmdLineLength) {
		Cmdline[CmdLineLength] = '\0';
		return Cmdline;
	}
	return NULL;
}


static int fract_convert_number(decNumber *x, const char *s) {
	if (*s == '\0') {
		err(ERR_DOMAIN);
		return 1;
	}
	decNumberFromString(x, s, &Ctx);
	return check_special(x);
}

/* Process the command line if any
 */
void process_cmdline(void) {
	decNumber a, b, x, t, z;

	if (CmdLineLength) {
		const unsigned int cmdlinedot = CmdLineDot;
		char cmdline[CMDLINELEN + 1];

		xcopy(cmdline, Cmdline, CMDLINELEN + 1);

		cmdline[CmdLineLength] = '\0';
		if (!is_intmode()) {
			if (cmdline[CmdLineLength-1] == 'E')
				cmdline[CmdLineLength-1] = '\0';
			else if (CmdLineLength > 1 && cmdline[CmdLineLength-2] == 'E' && cmdline[CmdLineLength-1] == '-')
				cmdline[CmdLineLength-2] = '\0';
		}
		CmdLineLength = 0;
		lift_if_enabled();
		State.state_lift = 1;
		CmdLineDot = 0;
		CmdLineEex = 0;
		if (is_intmode()) {
			const int sgn = (cmdline[0] == '-')?1:0;
			unsigned long long int x = s_to_ull(cmdline+sgn, int_base());
			setX_int_sgn(x, sgn);
		} else if (cmdlinedot == 2) {
			char *d0, *d1, *d2;
			int neg;

			UState.fract = 1;
			if (cmdline[0] == '-') {
				neg = 1;
				d0 = cmdline+1;
			} else {
				neg = 0;
				d0 = cmdline;
			}
			d1 = find_char(d0, '.');
			*d1++ = '\0';
			d2 = find_char(d1, '.');
			*d2++ = '\0';
			if (fract_convert_number(&b, d2))
				return;
			if (dn_eq0(&b)) {
				err(ERR_DOMAIN);
				return;
			}
			if (fract_convert_number(&z, d0))	return;
			if (fract_convert_number(&a, d1))	return;
			if (cmdlinedot == 2) {
				dn_divide(&t, &a, &b);
				dn_add(&x, &z, &t);
			} else {
				if (dn_eq0(&a)) {
					err(ERR_DOMAIN);
					return;
				}
				dn_divide(&x, &z, &a);
			}
			if (neg)
				dn_minus(&x, &x);
			setX(&x);
		} else {
			decNumberFromString(&x, cmdline, &Ctx);
			setX(&x);
		}
		set_entry();
	}
}

void process_cmdline_set_lift(void) {
	process_cmdline();
	State.state_lift = 1;
}


/*
 *  Return a pointer to a numbered register.
 *  If locals are enabled and a non existent local register
 *  is accessed, the respective global register is returned.
 *  Error checking must be done outside this routine.
 *  We force the beginning of the local registers on an even stack position.
 *  This ensures 32 bit alignment of the decima64 object.
 */
#ifdef INCLUDE_DOUBLE_PRECISION
/*
 *  If working in double precision, register numbers must be remapped
 */
static decimal64 *reg_address(int n, decimal64 *const regs, decimal64 *const named_regs) {
	const int dbl = is_dblmode();

	if (n < regX_idx)
		return regs + n + (dbl ? n : 0);

	n -= regX_idx;
	// Lettered register
	if (dbl) {
		// Mapping of double precision registers: 
		// Y->Z/T, Z->A/B, T->C/D, L->L/I, I->J/K
		// J & K are needed for some functions and are
		// therefore mapped to the last two numeric registers
		// configured.
		// In XROM, the last two local registers are used instead
		static const signed char remap[] = 
			{ 0,  2,  4,  6, 
			  8, 10, -4, -2,
			  8, 10, -4, -2 };

		n = remap[n];
		if (n < 0 && isXROM(state_pc()))
			// Registers J/K in volatile RAM
			return XromJK + 4 + n;
	}
	return named_regs + n;
}
#else
#define reg_address(n,regs,named_regs) ((n < regX_idx ? regs : named_regs - regX_idx) + n);
#endif

REGISTER *get_reg_n(int n) {
	const int dbl = is_dblmode();

	if (n >= CONST_REG_BASE) {
		n -= CONST_REG_BASE;
		if (dbl) 
			return (REGISTER *) (cnsts_dbl + n);
		else
			return (REGISTER *) (cnsts_int + n);
	}
	if (n >= FLASH_REG_BASE)
		return get_flash_reg_n(n - FLASH_REG_BASE);

	if (n >= LOCAL_REG_BASE && LocalRegs < 0) {
		n -= LOCAL_REG_BASE;
#ifdef INCLUDE_DOUBLE_PRECISION
		if (is_dblmode())
			n <<= 1;
#endif
		if (local_levels() == 1) {
			// Local XROM register in volatile RAM
			return (REGISTER *) (XromRegs + n);
		} else {
			// local register on the return stack
			return (REGISTER *) ((decimal64 *) (RetStk + (short)((LocalRegs + 2) & 0xfffe)) + n);
		}
	}
	return (REGISTER *) reg_address(n, Regs + TOPREALREG - NumRegs, Regs + regX_idx);
}


REGISTER *get_flash_reg_n(int n) {
	return (REGISTER *) reg_address(n, BackupFlash._regs + TOPREALREG - BackupFlash._numregs,
					   BackupFlash._regs + regX_idx);
}


/*  Some conversion routines to take decimals and produce integers
 *  This is for opaque storage, no conversion takes place.
 */
long long int get_reg_n_int(int index) {
	long long int ll;
	xcopy(&ll, get_reg_n(index), sizeof(ll));
	return ll;
}

void set_reg_n_int(int index, long long int ll) {
	xcopy(get_reg_n(index), &ll, sizeof(ll));
}

/* Get an integer from a register
 */
unsigned long long int get_reg_n_int_sgn(int index, int *sgn) {
	if (is_intmode()) {
		return extract_value(get_reg_n_int(index), sgn);
	} else {
		decNumber n;

		getRegister(&n, index);
		return dn_to_ull(&n, sgn);
	}
}

/* Put an integer into a register
 */
void set_reg_n_int_sgn(int index, unsigned long long int val, int sgn) {
	if (is_intmode()) {
		set_reg_n_int(index, build_value(val, sgn));
	} else {
		decNumber t;

		ullint_to_dn(&t, val);
		if (sgn)
			dn_minus(&t, &t);
		setRegister(index, &t);
	}
}

/* Put an integer into X
 */
void setX_int(long long int val) {
	set_reg_n_int(regX_idx, val);
}

void setX_int_sgn(unsigned long long int val, int sgn) {
	set_reg_n_int_sgn(regX_idx, val, sgn);
}



/*
 *  Set the register value explicitely
 */
void zero_regs(REGISTER *dest, int n) {
	int i;

	if (is_intmode())
		xset(dest, 0, n << 3);
	else {
#ifdef INCLUDE_DOUBLE_PRECISION
		const int dbl = is_dblmode();
#endif
		for (i=0; i<n; i++)
#ifdef INCLUDE_DOUBLE_PRECISION
			if (dbl)
				(&(dest->d))[i] = CONSTANT_DBL(OP_ZERO);
			else
#endif
				(&(dest->s))[i] = CONSTANT_INT(OP_ZERO);
	}
}

void move_regs(REGISTER *dest, REGISTER *src, int n) {
#ifdef INCLUDE_DOUBLE_PRECISION
	if (is_dblmode())
		n <<= 1;
#endif
	xcopy(dest, src, n << 3);
}


/* Zero a register
 */
static void set_zero(REGISTER *x) {
	zero_regs(x, 1);
}

void zero_X(void) {
	set_zero(get_reg_n(regX_idx));
}

void zero_Y(void) {
	set_zero(get_reg_n(regY_idx));
}

void clrx(enum nilop op) {
	zero_X();
}

/* Zero out the stack
 */
void clrstk(enum nilop op) {
	zero_regs(get_reg_n(regX_idx), stack_size());
	CmdLineLength = 0;
	State.state_lift = 1;
}


/* Zero out all registers excluding the stack and lastx
 */	
void clrreg(enum nilop op) {
	process_cmdline_set_lift();

	// erase register memory
	zero_regs(get_reg_n(0), global_regs() + special_regs());

	// erase local registers but keep them allocated
	if (LocalRegs < 0) {
		zero_regs(get_reg_n(LOCAL_REG_BASE), local_regs());
	}
}


/* Clear the subroutine return stack
 */
void clrretstk(void) {
	RetStkPtr = LocalRegs = 0;
}

void clrretstk_pc(void) {
	clrretstk();
	raw_set_pc(0);
	update_program_bounds(1);
}



#ifdef INCLUDE_DOUBLE_PRECISION
/*
 *  PI in high precision
 */
void op_pi(enum nilop op)
{
	setRegister(regX_idx, &const_PI);
	if (op == OP_cmplxPI)
		set_zero(get_reg_n(regY_idx));
}
#endif

/* Commands to allow access to constants
 */
void cmdconst(unsigned int arg, enum rarg op) {
	REGISTER *x = get_reg_n(regX_idx);
	if (is_intmode()) {
		bad_mode_error();
		return;
	}
	if (op == RARG_CONST_CMPLX)
		lift2_if_enabled();
	else
		lift_if_enabled();

	x->s = op == RARG_CONST_INT ? CONSTANT_INT(arg) : CONSTANT(arg);
#ifdef INCLUDE_DOUBLE_PRECISION
	if (is_dblmode())
		packed128_from_packed(&(x->d), &(x->s));
#endif
	if (op == RARG_CONST_CMPLX)
		setY(&const_0);
}


/* Store/recall code here.
 * These two are pretty much the same so we define some utility routines first.
 */

/* Do a basic STO/RCL arithmetic operation.
 */
static int storcl_op(unsigned short opr, int index, decNumber *r, int rev) {
	decNumber a, b, *x = &a, *y = &b;

	getX(x);
	getRegister(y, index);
	if (rev) {
		x = y;
		y = &a;
	}

	switch (opr) {
	case 1:
		dn_add(r, y, x);
		break;
	case 2:
		dn_subtract(r, y, x);
		break;
	case 3:
		dn_multiply(r, y, x);
		break;
	case 4:
		dn_divide(r, y, x);
		break;
	case 5:
		dn_min(r, y, x);
		break;
	case 6:
		dn_max(r, y, x);
		break;
	default:
		return 1;
	}
	return 0;
}

static int storcl_intop(unsigned short opr, int index, long long int *r, int rev) {
	long long int x, y;

	x = get_reg_n_int(regX_idx);
	y = get_reg_n_int(index);

	if (rev) {
		const long long int t = x;
		x = y;
		y = t;
	}

	switch (opr) {
	case 1:
		*r = intAdd(y, x);
		break;
	case 2:
		*r = intSubtract(y, x);
		break;
	case 3:
		*r = intMultiply(y, x);
		break;
	case 4:
		*r = intDivide(y, x);
		break;
	case 5:
		*r = intMin(y, x);
		break;
	case 6:
		*r = intMax(y, x);
		break;
	default:
		return 1;
	}
	return 0;
}

/* We've got a STO operation to do.
 */
void cmdsto(unsigned int arg, enum rarg op) {
	if (op == RARG_STO) {
		copyreg_n(arg, regX_idx);
	} else {
		if (is_intmode()) {
			long long int r;

			if (storcl_intop(op - RARG_STO, arg, &r, 0))
				illegal(op);
			set_reg_n_int(arg, r);
		} else {
			decNumber r;

			if (storcl_op(op - RARG_STO, arg, &r, 0))
				illegal(op);
			setRegister(arg, &r);
		}
	}
}

/* We've got a RCL operation to do.
 */
static void do_rcl(int index, enum rarg op) {
	if (op == RARG_RCL) {
		REGISTER temp;
		copyreg(&temp, get_reg_n(index));
		lift_if_enabled();
		copyreg(get_reg_n(regX_idx), &temp);
	} else {
		if (is_intmode()) {
			long long int r;

			if (storcl_intop(op - RARG_RCL, index, &r, 1))
				illegal(op);
			setlastX();
			setX_int(r);
		} else {
			decNumber r;

			if (storcl_op(op - RARG_RCL, index, &r, 1))
				illegal(op);
			setlastX();
			setX(&r);
		}
	}
}

void cmdrcl(unsigned int arg, enum rarg op) {
	do_rcl(arg, op);
}

#ifdef INCLUDE_FLASH_RECALL
void cmdflashrcl(unsigned int arg, enum rarg op) {
	do_rcl(FLASH_REG_BASE + arg, op - RARG_FLRCL + RARG_RCL);
}
#endif

/* And the complex equivalents for the above.
 * We pair registers arg & arg+1 to provide a complex number
 */
static int storcl_cop(unsigned short opr, int index, decNumber *r1, decNumber *r2, int rev) {
	decNumber a[2], b[2], *x = a, *y = b;

	getXY(x + 0, x + 1);
	getRegister(y + 0, index);
	getRegister(y + 1, index + 1);

	if (rev) {
		x = y;
		y = a;
	}

	switch (opr) {
	case 1:
		cmplxAdd(r1, r2, y + 0, y + 1, x + 0, x + 1);
		break;
	case 2:
		cmplxSubtract(r1, r2, y + 0, y + 1, x + 0, x + 1);
		break;
	case 3:
		cmplxMultiply(r1, r2, y + 0, y + 1, x + 0, x + 1);
		break;
	case 4:
		cmplxDivide(r1, r2, y + 0, y + 1, x + 0, x + 1);
		break;
	default:
		return 1;
	}
	return 0;
}


void cmdcsto(unsigned int arg, enum rarg op) {
	decNumber r1, r2;
	REGISTER *t1, *t2;

	t1 = get_reg_n(arg);
	t2 = get_reg_n(arg + 1);

	if (op == RARG_CSTO) {
		copyreg(t1, get_reg_n(regX_idx));
		copyreg(t2, get_reg_n(regY_idx));
	} else {
		if (is_intmode())
			bad_mode_error();
		else if (storcl_cop(op - RARG_CSTO, arg, &r1, &r2, 0))
			illegal(op);
		else {
			setRegister(arg, &r1);
			setRegister(arg + 1, &r2);
		}
	}
	set_was_complex();
}

static void do_crcl(int index, enum rarg op) {
	decNumber r1, r2;

	if (op == RARG_CRCL) {
		REGISTER x, y;
		copyreg(&x, get_reg_n(index));
		copyreg(&y, get_reg_n(index + 1));
		lift2_if_enabled();
		copyreg(get_reg_n(regX_idx), &x);
		copyreg(get_reg_n(regY_idx), &y);
	} else {
		if (is_intmode())
			bad_mode_error();
		else if (storcl_cop(op - RARG_CRCL, index, &r1, &r2, 1))
			illegal(op);
		else {
			setlastXY();
			setXY(&r1, &r2);
		}
	}
	set_was_complex();
}

void cmdcrcl(unsigned int arg, enum rarg op) {
	do_crcl(arg, op);
}

#ifdef INCLUDE_FLASH_RECALL
void cmdflashcrcl(unsigned int arg, enum rarg op) {
	do_crcl(FLASH_REG_BASE + arg, op - RARG_FLCRCL + RARG_CRCL);
}
#endif

/* SWAP x with the specified register
 */
void swap_reg(REGISTER *a, REGISTER *b) {
	REGISTER t;

	copyreg(&t, a);
	copyreg(a, b);
	copyreg(b, &t);
}

void cmdswap(unsigned int arg, enum rarg op) {
	int idx;

	if (op == RARG_CSWAPX)
		idx = regX_idx;
	else if (op == RARG_CSWAPZ)
		idx = regZ_idx;
	else
		idx = regX_idx + (int)(op - RARG_SWAPX);

	swap_reg(get_reg_n(idx), get_reg_n(arg));

	if (op >= RARG_CSWAPX) {
		swap_reg(get_reg_n(idx + 1), get_reg_n(arg + 1));
		set_was_complex();
	}
}


/* View a specified register
 */
void cmdview(unsigned int arg, enum rarg op) {
	ShowRegister = arg;
	State2.disp_freeze = 0;
	display();
	State2.disp_freeze = Running || arg != regX_idx;
}


#ifdef INCLUDE_USER_MODE
/* Save and restore user state.
 */
void cmdsavem(unsigned int arg, enum rarg op) {
	xcopy( get_reg_n(arg), &UState, sizeof(unsigned long long int) );
}

void cmdrestm(unsigned int arg, enum rarg op) {
	xcopy( &UState, get_reg_n(arg), sizeof(unsigned long long int) );
	if ( UState.contrast == 0 )
		UState.contrast = 7;
}
#endif

/* Set the stack size */
void set_stack_size(enum nilop op) {
	UState.stack_depth = (op == OP_STK4) ? 0 : 1;
}

/* Get the stack size */
void get_stack_size(enum nilop op) {
	setX_int_sgn(stack_size(), 0);
}

void get_word_size(enum nilop op) {
	setX_int_sgn((int)word_size(), 0);
}

void get_sign_mode(enum nilop op) {
	static const unsigned char modes[4] = {
		0x02,		// 2's complement
		0x01,		// 1's complement
		0x00,		// unsigned
		0x81		// sign and mantissa
	};
	const unsigned char v = modes[(int)int_mode()];
	setX_int_sgn(v & 3, v & 0x80);
}

void get_base(enum nilop op) {
	setX_int_sgn((int)int_base(), 0);
}

/* Get the current ticker value */
void op_ticks(enum nilop op) {
#ifndef CONSOLE
    setX_int_sgn(Ticker, 0);
#else
    struct timeval tv;
    long long int t;
    gettimeofday(&tv, NULL);
    t = tv.tv_sec * 10 + tv.tv_usec / 100000;
    setX_int_sgn(t, 0);
#endif
}

/* Display the battery voltage */
void op_voltage(enum nilop op) {
	decNumber t, u;
#ifdef REALBUILD
	unsigned long long int v = 19 + Voltage;
#else
	unsigned long long int v = 32;
#endif

	if (is_intmode()) {
		setX_int_sgn(v, 0);
	} else {
		ullint_to_dn(&t, v);
		dn_mulpow10(&u, &t, -1);
		setX(&u);
	}
}

/*
 *  Commands to determine free memory
 */
int free_mem(void) {
	return RetStkSize + RetStkPtr;
}

int free_flash(void) {
	return NUMPROG_FLASH_MAX - UserFlash.size;
}

void get_mem(enum nilop op) {
	setX_int_sgn( op == OP_MEMQ ? free_mem() :
		 op == OP_LOCRQ ? local_regs() :
		 op == OP_FLASHQ ? free_flash() :
		 global_regs(),
		 0);
}


/* Check if a keystroke is pending in the buffer, if so return it to the specified
 * register, if not skip the next step.
 */
void op_keyp(unsigned int arg, enum rarg op) {
	int cond = LastKey == 0;
	if (!cond) {
		int k = LastKey - 1;
		LastKey = 0;
		set_reg_n_int_sgn(arg, keycode_to_row_column(k), 0);
	}
	fin_tst(cond);
}

/*
 *  Get a key code from a register and translate it from row/colum to internal
 *  Check for valid arguments
 */
static int get_keycode_from_reg(unsigned int n)
{
	int sgn;
	const int c = row_column_to_keycode((int) get_reg_n_int_sgn((int) n, &sgn));
	if ( c < 0 )
		err(ERR_RANGE);
	return c;
}

/*
 *  Take a row/column key code and feed it to the keyboard buffer
 *  This stops program execution first to make sure, the key is not
 *  read in by KEY? again.
 */
void op_putkey(unsigned int arg, enum rarg op)
{
	const int c = get_keycode_from_reg(arg);

	if (c >= 0) {
		set_running_off();
		put_key(c);
	}
}

/*
 *  Return the type of the keycode in register n
 *  returns 0-9 for digits, 10 for ., +/-, EEX, 11 for f,g,h, 12 for all other keys.
 *  Invalid codes produce an error.
 */
void op_keytype(unsigned int arg, enum rarg op)
{
	const int c = get_keycode_from_reg(arg);
	if ( c >= 0 ) {
		const char types[] = {
			12, 12, 12, 12, 12, 12,
			12, 12, 12, 11, 11, 11,
			12, 12, 10, 10, 12, 12,
			12,  7,  8,  9, 12, 12,
			12,  4,  5,  6, 12, 12,
			12,  1,  2,  3, 12, 12,
			12,  0, 10, 12, 12 };
		lift_if_enabled();
		setX_int_sgn(types[c], 0);
	}
}


/* Check which operating mode we're in -- integer or real -- they both
 * vector through this routine.
 */
void check_mode(enum nilop op) {
	const int intmode = is_intmode() ? 1 : 0;
	const int desired = (op == OP_ISINT) ? 1 : 0;

	fin_tst(intmode == desired);
}


#ifdef INCLUDE_DOUBLE_PRECISION
/* Check if DBLON is active
 */
void check_dblmode(enum nilop op) {
	fin_tst(is_dblmode());
}
#endif


/* Save and restore the entire stack to sequential registers */
static int check_stack_overlap(unsigned int arg) {
	const int n = stack_size();

	if (arg + n <= global_regs() || arg >= NUMREG) {
		return n;
	}
	err(ERR_STK_CLASH);
	return 0;
}

void cmdstostk(unsigned int arg, enum rarg op) {
	int i, n = check_stack_overlap(arg);

	for (i=0; i<n; i++)
		*get_reg_n(arg+i) = *get_stack(i);
}

void cmdrclstk(unsigned int arg, enum rarg op) {
	int i, n = check_stack_overlap(arg);

	for (i=0; i<n; i++)
		*get_stack(i) = *get_reg_n(arg+i);
}


/*
 *  Move up the return stack, skipping any local variables
 */
static void retstk_up(void)
{
	if (RetStkPtr < 0) {
		int sp = RetStkPtr++;
		unsigned int s = RetStk[sp++];
		if (isLOCAL(s)) {
			sp += LOCAL_LEVELS(s);
			RetStkPtr = sp;
			// Re-adjust the LocalRegs pointer
			LocalRegs = 0;
			while (sp < 0) {
				if (isLOCAL(RetStk[sp])) {
					LocalRegs = sp;
					break;
				}
				++sp;
			}
		}
	}
}


/* Search from the given position for the specified numeric label.
 */
unsigned int find_opcode_from(unsigned int pc, const opcode l, const int flags) {
	unsigned short int top;
	int count;
	const int endp = flags & FIND_OP_ENDS;
	const int errp = flags & FIND_OP_ERROR;

	count = 1 + find_section_bounds(pc, endp, &top);
	count -= top;
	while (count--) {
		// Wrap around doesn't hurt, we just limit the search to the number of possible steps
		// If we don't find the label, we may search a little too far if many double word
		// instructions are in the code, but this doesn't do any harm.
		if (getprog(pc) == l)
			return pc;
		pc = do_inc(pc, endp);
	}
	if (errp)
		err(ERR_NO_LBL);
	return 0;
}


unsigned int find_label_from(unsigned int pc, unsigned int arg, int flags) {
	return find_opcode_from(pc, RARG(RARG_LBL, arg), flags);
}



/* Handle a GTO/GSB instruction
 */
static void gsbgto(unsigned int pc, int gsb, unsigned int oldpc) {
	raw_set_pc(pc);
	if (gsb) {
		if (!Running) {
			// XEQ or hot key from keyboard
			clrretstk();
			set_running_on();
		}
		if (-RetStkPtr >= RetStkSize) {
			// Stack is full
			err(ERR_RAM_FULL);
			// clrretstk();
		}
		else {
			// Push PC on return stack
			RetStk[--RetStkPtr] = oldpc;
		}
	}
}

// Handle a RTN
static void do_rtn(int plus1) {
	if (Running) {
		if (RetStkPtr < 0) {
			// Pop any LOCALS off the stack
			retstk_up();
		}
		if (RetStkPtr <= 0) {
			// Normal RTN within program
			unsigned short pc = RetStk[RetStkPtr - 1];
			raw_set_pc(pc);
			// If RTN+1 inc PC if not at END or a POPUSR command would be skipped
			fin_tst(! plus1 || getprog(pc) == (OP_NIL | OP_POPUSR));
		}
		else {
			// program was started without a valid return address on the stack
			clrretstk_pc();
		}
		if (RetStkPtr == 0) {
			// RTN with empty stack stops
			set_running_off();
		}
	} else {
		// Manual return goes to step 0 and clears the return stack
		clrretstk_pc();
	}
}

// RTN and RTN+1
void op_rtn(enum nilop op) {
	do_rtn(op == OP_RTNp1 ? 1 : 0);
}


// Called by XEQ, GTO and CAT browser
void cmdgtocommon(int gsb, unsigned int pc) {
	if (pc == 0)
		set_running_off();
	else
		gsbgto(pc, gsb, state_pc());
}

void cmdlblp(unsigned int arg, enum rarg op) {
	fin_tst(find_label_from(state_pc(), arg, FIND_OP_ENDS) != 0);
}

void cmdgto(unsigned int arg, enum rarg op) {
	cmdgtocommon(op != RARG_GTO, find_label_from(state_pc(), arg, FIND_OP_ERROR | FIND_OP_ENDS));
}

static unsigned int findmultilbl(const opcode o, int flags) {
	const opcode dest = (o & 0xfffff0ff) + (DBL_LBL << DBL_SHIFT);
	unsigned int lbl;

	lbl = find_opcode_from(0, dest, 0);					// RAM
	if (lbl == 0)
		lbl = find_opcode_from(addrLIB(0, REGION_LIBRARY), dest, 0);	// Library
	if (lbl == 0)
		lbl = find_opcode_from(addrLIB(0, REGION_BACKUP), dest, 0);	// Backup
#if 0
	// Disable searching for global labels in xrom
	if (lbl == 0)
		lbl = find_opcode_from(addrXROM(0), dest, 0);			// XROM
#endif
	if (lbl == 0 && (flags & FIND_OP_ERROR) != 0)
		err(ERR_NO_LBL);
	return lbl;
}

void cmdmultilblp(const opcode o, enum multiops mopr) {
	fin_tst(findmultilbl(o, 0) != 0);
}

static void do_multigto(int is_gsb, unsigned int lbl) {
	if (!Running && isXROM(lbl) && ! is_gsb) {
		lbl = 0;
		err(ERR_RANGE);
	}

	cmdgtocommon(is_gsb, lbl);
}

void cmdmultigto(const opcode o, enum multiops mopr) {
	unsigned int lbl = findmultilbl(o, FIND_OP_ERROR);
	int is_gsb = mopr != DBL_GTO;

	do_multigto(is_gsb, lbl);
}


static void branchtoalpha(int is_gsb, char buf[]) {
	unsigned int op, lbl;

	op = OP_DBL + (DBL_LBL << DBL_SHIFT);
	op |= buf[0] & 0xff;
	op |= (buf[1] & 0xff) << 16;
	op |= (buf[2] & 0xff) << 24;
	lbl = findmultilbl(op, FIND_OP_ERROR);

	do_multigto(is_gsb, lbl);
}

void cmdalphagto(unsigned int arg, enum rarg op) {
	char buf[12];

	xset(buf, '\0', sizeof(buf));
	branchtoalpha(op != RARG_ALPHAGTO, alpha_rcl_s(arg, buf));
}

static void do_branchalpha(int is_gsb) {
	char buf[4];

	xcopy(buf, Alpha, 3);
	buf[3] = '\0';
	branchtoalpha(is_gsb, buf);
}

void op_gtoalpha(enum nilop op) {
	do_branchalpha((op == OP_GTOALPHA) ? 0 : 1);
}


void cmddisp(unsigned int arg, enum rarg op) {
	UState.dispdigs = arg;
	if (op != RARG_DISP)
		UState.dispmode = (op - RARG_STD) + MODE_STD;
	op_float(OP_FLOAT);
}


/* Metric / Imperial conversion code */
decNumber *convC2F(decNumber *r, const decNumber *x) {
	decNumber s;

	dn_multiply(&s, x, &const_9on5);
	return dn_add(r, &s, &const_32);
}

decNumber *convF2C(decNumber *r, const decNumber *x) {
	decNumber s;

	dn_subtract(&s, x, &const_32);
	return dn_divide(r, &s, &const_9on5);
}

decNumber *convDB2AR(decNumber *r, const decNumber *x) {
	decNumber t;
	dn_multiply(&t, x, &const_0_05);
	return decNumberPow10(r, &t);
}

decNumber *convAR2DB(decNumber *r, const decNumber *x) {
	decNumber t;
	dn_log10(&t, x);
	return dn_multiply(r, &t, &const_20);
}

decNumber *convDB2PR(decNumber *r, const decNumber *x) {
	decNumber t;
	dn_mulpow10(&t, x, -1);
	return decNumberPow10(r, &t);
}

decNumber *convPR2DB(decNumber *r, const decNumber *x) {
	decNumber t;
	dn_log10(&t, x);
	return dn_mulpow10(r, &t, 1);
}

/* Scale conversions */
void do_conv(decNumber *r, unsigned int arg, const decNumber *x) {
	decNumber m;
	const unsigned int conv = arg / 2;
	const unsigned int dirn = arg & 1;

	if (conv > NUM_CONSTS_CONV) {
		decNumberCopy(r, x);
		return;
	}

	decimal64ToNumber(&CONSTANT_CONV(conv), &m);

	if (dirn == 0)		// metric to imperial
		dn_divide(r, x, &m);
	else			// imperial to metric
		dn_multiply(r, x, &m);
}

void cmdconv(unsigned int arg, enum rarg op) {
	decNumber x, r;

	if (is_intmode())
		return;

	getX(&x);
	do_conv(&r, arg, &x);
	setlastX();
	setX(&r);
}

/*  Finish up a test -- if the value is non-zero, the test passes.
 *  If it is zero, the test fails.
 */
void fin_tst(const int a) {
	if (Running) {
		if (! a && incpc())
			decpc();
	}
	else
		DispMsg = a ? "true" : "false";
}


/* Skip a number of instructions forwards */
void cmdskip(unsigned int arg, enum rarg op) {
	const unsigned int origpc = state_pc();
	unsigned int pc;

	if (isXROM(origpc))
		pc = origpc + arg;
	else {
		while (arg-- && !incpc());
		if (PcWrapped) {
			err(ERR_RANGE);
		}
		pc = state_pc();
	}
	gsbgto(pc, op == RARG_BSF, origpc);
}

/* Skip backwards */
void cmdback(unsigned int arg, enum rarg op) {
	const unsigned int origpc = state_pc();
	unsigned int pc = origpc;

	if (isXROM(origpc))
		pc -= arg + 1;
        else if (arg) {
		if ( Running ) {
			// Handles the case properly that we are on last step
			pc = do_dec(pc, 1);
		}
		do {
			pc = do_dec(pc, 1);
		} while (--arg && !PcWrapped);
		if (PcWrapped) {
			err(ERR_RANGE);
			return;
		}
	}
	gsbgto(pc, op == RARG_BSB, origpc);
}


/* We've encountered a CHS while entering the command line.
 */
static void cmdlinechs(void) {
	if (CmdLineEex) {
		const unsigned int pos = CmdLineEex + 1;
		if (CmdLineLength < pos) {
			if (CmdLineLength < CMDLINELEN)
				Cmdline[CmdLineLength++] = '-';
		} else if (Cmdline[pos] == '-') {
			if (CmdLineLength != pos)
				xcopy(Cmdline + pos, Cmdline + pos + 1, CmdLineLength-pos);
			CmdLineLength--;
		} else if (CmdLineLength < CMDLINELEN) {
			xcopy(Cmdline+pos+1, Cmdline+pos, CmdLineLength-pos);
			Cmdline[pos] = '-';
			CmdLineLength++;
		}
	} else {
		if (Cmdline[0] == '-') {
			if (CmdLineLength > 1)
				xcopy(Cmdline, Cmdline+1, CmdLineLength);
			CmdLineLength--;
		} else if (CmdLineLength < CMDLINELEN) {
			xcopy(Cmdline+1, Cmdline, CmdLineLength);
			Cmdline[0] = '-';
			CmdLineLength++;
		}
	}
}

/* Execute a tests command
 */
static void do_tst(int cmp, const enum tst_op op) {
	int a = 0;
	int iszero, isneg;

	process_cmdline_set_lift();

	if (is_intmode()) {
		unsigned long long int xv, yv;
		int xs, ys;

		xv = extract_value(get_reg_n_int(regX_idx), &xs);
		if (cmp >= CONST_REG_BASE) {
			yv = cmp - CONST_REG_BASE;
			ys = 1;
		} else
			yv = extract_value(get_reg_n_int(cmp), &ys);

		if (xv == 0 && yv == 0)
			iszero = 1;
		else
			iszero = (xv == yv) && (xs == ys);

		if (xs == ys) {		// same sign
			if (xs)		// both negative
				isneg = xv > yv;
			else		// both positive
				isneg = xv < yv;
		} else
			isneg = xs;	// opposite signs
	} else {
		decNumber t, x, r;

		getX(&x);
		if (decNumberIsNaN(&x))
			goto flse;

		getRegister(&t, cmp);
		if (decNumberIsNaN(&t))
			goto flse;

		if (op == TST_APX) {
			decNumberRnd(&x, &x);
			if (cmp < CONST_REG_BASE)
				decNumberRnd(&t, &t);
		}
		dn_compare(&r, &x, &t);
		iszero = dn_eq0(&r);
		isneg = decNumberIsNegative(&r);
	}

	switch (op) {
	case TST_APX:
	case TST_EQ:	a = iszero;		break;
	case TST_NE:	a = !iszero;		break;
	case TST_LT:	a = isneg && !iszero;	break;
	case TST_LE:	a = isneg || iszero;	break;
	case TST_GT:	a = !isneg && !iszero;	break;
	case TST_GE:	a = !isneg || iszero;	break;
	default:	a = 0;			break;
	}

flse:	fin_tst(a);
}

void check_zero(enum nilop op) {
	int neg;
	int zero;

	if (is_intmode()) {
		const unsigned long long int xv = extract_value(get_reg_n_int(regX_idx), &neg);
		zero = (xv == 0);
	} else {
		decNumber x;
		getX(&x);
		neg = decNumberIsNegative(&x);
		zero = dn_eq0(&x);
	}
	if (op == OP_Xeq_pos0)
		fin_tst(zero && !neg);
	else /* if (op == OP_Xeq_neg0) */
		fin_tst(zero && neg);
}

void cmdtest(unsigned int arg, enum rarg op) {
	do_tst(arg, (enum tst_op)(op - RARG_TEST_EQ));
}

static void do_ztst(const decNumber *re, const decNumber *im, enum tst_op op) {
	int c = 0;
	decNumber x, y, t;
	int eq = 1;

	process_cmdline_set_lift();

	if (is_intmode()) {
		bad_mode_error();
		return;
	}
	getXY(&x, &y);
	if (decNumberIsNaN(&x) || decNumberIsNaN(&y) || decNumberIsNaN(re) || decNumberIsNaN(im))
		goto flse;
	dn_compare(&t, &x, re);
	if (!dn_eq0(&t))
		eq = 0;
	else {
		dn_compare(&t, &y, im);
		if (!dn_eq0(&t))
			eq = 0;
	}
	if (op != TST_NE)
		c = eq;
	else
		c = !eq;
flse:	fin_tst(c);
}

void cmdztest(unsigned int arg, enum rarg op) {
	decNumber re, im;
	getRegister(&re, arg);
	getRegister(&im, arg + 1);
	do_ztst(&re, &im, (enum tst_op)(op - RARG_TEST_ZEQ));
}

static int incdec(unsigned int arg, int inc) {
	if (is_intmode()) {
		long long int x = get_reg_n_int(arg);
		int xs;
		unsigned long long int xv;

		if (inc)
			x = intAdd(x, 1LL);
		else
			x = intSubtract(x, 1LL);
		set_reg_n_int(arg, x);

		xv = extract_value(x, &xs);
		return xv != 0;
	} else {
		decNumber x, y;

		getRegister(&x, arg);
		if (inc)
			dn_inc(&x);
		else
			dn_dec(&x);
		setRegister(arg, &x);
		decNumberTrunc(&y, &x);
		return ! dn_eq0(&y);
	}
}

void cmdlincdec(unsigned int arg, enum rarg op) {
	incdec(arg, op == RARG_INC);
}

void cmdloopz(unsigned int arg, enum rarg op) {
	fin_tst(incdec(arg, op == RARG_ISZ));
}

void cmdloop(unsigned int arg, enum rarg op) {
	if (is_intmode()) {
		long long int x = get_reg_n_int(arg);
		int xs;
		unsigned long long int xv;

		if (op == RARG_ISG || op == RARG_ISE)
			x = intAdd(x, 1LL);
		else
			x = intSubtract(x, 1LL);
		set_reg_n_int(arg, x);

		xv = extract_value(x, &xs);
		if (op == RARG_ISG)
			fin_tst(! (xs == 0 && xv > 0));		// > 0
		else if (op == RARG_DSE)
			fin_tst(! (xs != 0 || xv == 0));	// <= 0
		else if (op == RARG_ISE)
			fin_tst(! (xs == 0 || xv == 0));	// >= 0
		else // if (op == RARG_DSL)
			fin_tst(! (xs != 0 && xv > 0));		// < 0
		return;
	} else {
		decNumber x, i, f, n, u;

		getRegister(&x, arg);

		// Break the number into the important bits
		// nnnnn.fffii
		dn_abs(&f, &x);
		decNumberTrunc(&n, &f);			// n = nnnnn
		dn_subtract(&u, &f, &n);		// u = .fffii
		if (decNumberIsNegative(&x))
			dn_minus(&n, &n);
		dn_mulpow10(&i, &u, 3);			// i = fff.ii
		decNumberTrunc(&f, &i);			// f = fff
		dn_subtract(&i, &i, &f);		// i = .ii
		dn_mul100(&x, &i);
		decNumberTrunc(&i, &x);			// i = ii
		if (dn_eq0(&i))
			dn_1(&i);

		if (op == RARG_ISG || op == RARG_ISE) {
			dn_add(&n, &n, &i);
			dn_compare(&x, &f, &n);
			if (op == RARG_ISE)
				fin_tst(dn_gt0(&x));
			else
				fin_tst(! dn_lt0(&x));
		} else {
			dn_subtract(&n, &n, &i);
			dn_compare(&x, &f, &n);
			if (op == RARG_DSL)
				fin_tst(dn_le0(&x));
			else
				fin_tst(dn_lt0(&x));
		}

		// Finally rebuild the result
		if (decNumberIsNegative(&n)) {
			dn_subtract(&x, &n, &u);
		} else
			dn_add(&x, &n, &u);
		setRegister(arg, &x);
	}
}


/* Shift a real number by 10 to the specified power
 */
void op_shift_digit(unsigned int n, enum rarg op) {
	decNumber x;
	int adjust = n;

	if (is_intmode()) {
		bad_mode_error();
		return;
	}
	getX(&x);
	setlastX();
	if (decNumberIsSpecial(&x) || dn_eq0(&x))
		return;
	if (op == RARG_SRD)
		adjust = -adjust;
	x.exponent += adjust;
	setX(&x);
}


/* Return a pointer to the byte with the indicated flag in it.
 * also return a byte with the relevant bit mask set up.
 * Also, handle local flags.
 */
static unsigned short int *flag_word(int n, unsigned short int *mask) {
	unsigned short int *p = UserFlags;
	if (n >= LOCAL_FLAG_BASE) {
		const int l = local_levels();
		n -= LOCAL_FLAG_BASE;
		if (l == 1) {
			// XROM special
			p = &XromFlags;
		}
		else if (LocalRegs & 1) {
			// Odd frame: flags are at end of frame
			p = RetStk + LocalRegs + l - 1;
		}
		else {
			// Even frame: Flags are at beginning of frame
			p = RetStk + LocalRegs + 1;
		}
	}
	if (mask != NULL)
		*mask = 1 << (n & 15);
	return p + (n >> 4);
}

int get_user_flag(int n) {
	unsigned short mask;
	const unsigned short *const f = flag_word(n, &mask);

	return (*f & mask)? 1 : 0;
}

void put_user_flag(int n, int f) {
	if (f)	set_user_flag(n);
	else	clr_user_flag(n);
}

#ifndef set_user_flag
void set_user_flag(int n) {
	unsigned short mask;
	unsigned short *const f = flag_word(n, &mask);

	*f |= mask;
}

void clr_user_flag(int n) {
	unsigned short mask;
	unsigned short *const f = flag_word(n, &mask);

	*f &= ~mask;
}
#endif

void cmdflag(unsigned int arg, enum rarg op) {
	unsigned short mask;
	unsigned short *const f = flag_word(arg, &mask);
	int flg = *f & mask;

	switch (op) {
	case RARG_SF:	flg = 1;			   break;
	case RARG_CF:	flg = 0;			   break;
	case RARG_FF:	flg = flg? 0 : 1;		   break;

	case RARG_FS:	fin_tst(flg);			   return;
	case RARG_FC:	fin_tst(! flg);			   return;

	case RARG_FSC:	fin_tst(flg); flg = 0;		   break;
	case RARG_FSS:	fin_tst(flg); flg = 1;		   break;
	case RARG_FSF:	fin_tst(flg); flg = flg ? 0 : 1;   break;

	case RARG_FCC:	fin_tst(! flg);	flg = 0;	   break;
	case RARG_FCS:	fin_tst(! flg);	flg = 1;	   break;
	case RARG_FCF:	fin_tst(! flg);	flg = flg ? 0 : 1; break;

	default:
		return;
	}

	// And write the value back
	if (flg)
		*f |= mask;
	else
		*f &= ~mask;

	if ( arg == A_FLAG ) {
		dot( BIG_EQ, flg );
		finish_display();
	}
}

/* Reset all flags to off/false
 */
void clrflags(enum nilop op) {
	xset(UserFlags, 0, sizeof(UserFlags));
	if (LocalRegs < 0) {
		* flag_word(LOCAL_REG_BASE, NULL) = 0;
	}
}


/* Integer word size
 */
void intws(unsigned int arg, enum rarg op) {
	if (is_intmode()) {
		int i, ss = stack_size();
		unsigned int oldlen = UState.int_len;
		long long int v;

		for (i=0; i<ss; i++) {
			v = get_reg_n_int(regX_idx + i);
			UState.int_len = arg;
			set_reg_n_int(regX_idx + i, mask_value(v));
			UState.int_len = oldlen;
		}
		v = get_reg_n_int(regL_idx);
		UState.int_len = arg;
		set_reg_n_int(regL_idx, mask_value(v));
	} else
	    UState.int_len = arg;
}


/* Convert from a real to a fraction
 */

void get_maxdenom(decNumber *d) {
	const unsigned int dm = UState.denom_max;
	int_to_dn(d, dm==0?9999:dm);
}

void op_2frac(enum nilop op) {
	decNumber z, n, d, t;

	if (UState.intm) {
		setX_int(1);
		return;
	}

	getY(&z);			// Stack has been lifted already
	decNumber2Fraction(&n, &d, &z);
	setXY(&d, &n);			// Set numerator and denominator
	if (State2.runmode) {
		dn_divide(&t, &n, &d);
		dn_compare(&n, &t, &z);
		if (dn_eq0(&n))
			DispMsg = "y/x =";
		else if (decNumberIsNegative(&n))
			DispMsg = "y/x \017";
		else
			DispMsg = "y/x \020";
	}
}

void op_fracdenom(enum nilop op) {
	int s;
	unsigned long long int i;

	i = get_reg_n_int_sgn(regX_idx, &s);
	if (i > 9999)
		UState.denom_max = 0;
	else if (i != 1)
		UState.denom_max = (unsigned int) i;
	else {
		setlastX();
		setX_int_sgn(UState.denom_max, 0);
	}
}

void op_denom(enum nilop op) {
	UState.denom_mode = DENOM_ANY + (op - OP_DENANY);
}


/* Switching from an integer mode to real mode requires us
 * to make an effort at converting x and y into a real numbers
 * x' = y . 2^x
 */
#ifdef HP16C_MODE_CHANGE
static void int2dn(decNumber *x, REGISTER *a) {
	int s;
	unsigned long long int v = extract_value(get_reg_n_int(a), &s);

	ullint_to_dn(x, v);
	if (s)
		dn_minus(x, x);
}
#else
static void float_mode_convert(int index, decimal64 *i) {
	decNumber x;
	int s;
	unsigned long long int ll;
	unsigned long long int v;
	
	xcopy(&ll, i, sizeof(ll));
	v = extract_value(ll, &s);

	ullint_to_dn(&x, v);
	if (s)
		dn_minus(&x, &x);
	setRegister(index, &x);
}
#endif

void op_float(enum nilop op) {
#ifdef HP16C_MODE_CHANGE
	decNumber x, y, z;
#else
	int i;
#endif

	if (is_intmode()) {
		UState.intm = 0;
		// UState.int_len = 0;
#ifdef HP16C_MODE_CHANGE
		int2dn(&x, get_reg_n(regX_idx));
		int2dn(&y, get_reg_n(regY_idx));
		clrstk(NULL, NULL, NULL);
		decNumberPower(&z, &const_2, &x);
		dn_multiply(&x, &z, &y);
		set_overflow(decNumberIsInfinite(&x));
		packed_from_number(get_reg_n(regX_idx), &x);
#else
		float_mode_convert(regL_idx, Regs + regL_idx);
		for (i = regX_idx + stack_size() - 1; i >= regX_idx; --i)
			float_mode_convert(i, Regs + i);
#endif
	}
	UState.fract = 0;
        State2.hms = (op == OP_HMS) ? 1 : 0;
}

void op_fract(enum nilop op) {
	op_float(OP_FLOAT);
	UState.fract = 1;
	if (op == OP_FRACIMPROPER)
		UState.improperfrac = 1;
	else if (op == OP_FRACPROPER)
		UState.improperfrac = 0;
}

static int is_digit(const char c) {
	if (c >= '0' && c <= '9')
		return 1;
	return 0;
}

static int is_xdigit(const char c) {
	if (is_digit(c) || (c >= 'A' && c <= 'F'))
		return 1;
	return 0;
}

/* Process a single digit.
 */
static void digit(unsigned int c) {
	const int intm = is_intmode();
	int i, j;
	int lim = 12;

	if (CmdLineLength >= CMDLINELEN) {
		warn(ERR_TOO_LONG);
		return;
	}
	if (intm) {
		if (c >= int_base()) {
			warn(ERR_DIGIT);
			return;
		}
		for (i=j=0; i<(int)CmdLineLength; i++)
			j += is_xdigit(Cmdline[i]);
		if (j == lim) {
			warn(ERR_TOO_LONG);
			return;
		}
		if (c >= 10) {
			Cmdline[CmdLineLength++] = c - 10 + 'A';
			return;
		}
	} else {
		if (c >= 10) {
			warn(ERR_DIGIT);
			return;
		}
		for (i=j=0; i<(int)CmdLineLength; i++)
			if (Cmdline[i] == 'E') {
				lim++;
				break;
			} else
				j += is_digit(Cmdline[i]);
		if (j == lim) {
			warn(ERR_TOO_LONG);
			return;
		}
	}

	Cmdline[CmdLineLength++] = c + '0';
	Cmdline[CmdLineLength] = '\0';

	if (! intm && CmdLineEex) {
		char *p = &Cmdline[CmdLineEex + 1];
		int emax = 384;
		int n;

		/* Figure out the range limit for the exponent */
		if (*p == '-') {
			p++;
			emax = 383;
		}

		/* Now, check if the current exponent exceeds the range.
		 * If so, shift it back a digit and validate a second time
		 * in case the first digit is too large.
		 */
		for (n=0; n<2; n++) {
			if (s_to_i(p) > emax) {
				int i;

				for (i=0; p[i] != '\0'; i++)
					p[i] = p[i+1];
				CmdLineLength--;
				Cmdline[CmdLineLength] = '\0';
			} else
				break;
		}
	}
}


void set_entry() {
	State.entryp = 1;
}


/* Decode and process the specials.  These are niladic functions and
 * commands with non-standard stack operation.
 */
static void specials(const opcode op) {
	int opm = argKIND(op);

	switch (opm) {
	case OP_0:	case OP_1:	case OP_2:
	case OP_3:	case OP_4:	case OP_5:
	case OP_6:	case OP_7:	case OP_8:
	case OP_9:	case OP_A:	case OP_B:
	case OP_C:	case OP_D:	case OP_E:
	case OP_F:
		digit(opm - OP_0);
		break;

	case OP_DOT:
		if (is_intmode())
			break;
		if (CmdLineDot < 2 && !CmdLineEex && CmdLineLength < CMDLINELEN) {
			if (CmdLineLength == 0 || Cmdline[CmdLineLength-1] == '.')
				digit(0);
			CmdLineDot++;
			Cmdline[CmdLineLength++] = '.';
		}
		break;

	case OP_EEX:
		if (is_intmode() || UState.fract || CmdLineDot == 2)
			break;
		if (!CmdLineEex && CmdLineLength < CMDLINELEN) {
			if (CmdLineLength == 0)
				digit(1);
			CmdLineEex = CmdLineLength;
			Cmdline[CmdLineLength++] = 'E';
		}
		break;

	case OP_CHS:
		if (CmdLineLength)
			cmdlinechs();
		else if (is_intmode()) {
			setX_int(intChs(get_reg_n_int(regX_idx)));
			State.state_lift = 1;
		} else {
			decNumber x, r;

			getX(&x);
			dn_minus(&r, &x);
			setX(&r);
			State.state_lift = 1;
		}
		break;

	case OP_CLX:
		if (Running)
			illegal(op);
		else if (CmdLineLength) {
			CmdLineLength--;
			if (Cmdline[CmdLineLength] == 'E')
				CmdLineEex = 0;
			else if (Cmdline[CmdLineLength] == '.')
				CmdLineDot--;
		} else
			clrx(OP_rCLX);
		break;

	case OP_ENTER:
		process_cmdline();
		lift();
		State.state_lift = 0;
		break;

	case OP_SIGMAPLUS:
	case OP_SIGMAMINUS:
		if (is_intmode()) {
			bad_mode_error();
			break;
		}
		process_cmdline();
		State.state_lift = 0;
		setlastX();
		if (opm == OP_SIGMAPLUS)
			sigma_plus();
		else
			sigma_minus();
		sigma_val(OP_sigmaN);
		break;

	// Conditional tests vs registers....
	case OP_Xeq0:	case OP_Xlt0:	case OP_Xgt0:
	case OP_Xne0:	case OP_Xle0:	case OP_Xge0:
	case OP_Xapx0:
		do_tst(CONST_REG_BASE + OP_ZERO, (enum tst_op)(opm - OP_Xeq0));
		break;
	case OP_Zeq0: case OP_Zne0:
		do_ztst(&const_0, &const_0, (enum tst_op)(opm - OP_Zeq0));
		break;

	case OP_Xeq1:	case OP_Xlt1:	case OP_Xgt1:
	case OP_Xne1:	case OP_Xle1:	case OP_Xge1:
	case OP_Xapx1:
		do_tst(CONST_REG_BASE + OP_ONE, (enum tst_op)(opm - OP_Xeq1));
		break;
	case OP_Zeq1:	case OP_Zne1:
		do_ztst(&const_1, &const_0, (enum tst_op)(opm - OP_Zeq1));
		break;

	default:
		illegal(op);
	}
}

enum trig_modes get_trig_mode(void) {
	if (State2.cmplx)
		return TRIG_RAD;
	//if (State2.hyp)	return TRIG_RAD;
	return (enum trig_modes) UState.trigmode;
}

/*
static void set_trig_mode(enum trig_modes m) {
	UState.trigmode = m;
}

void op_trigmode(enum nilop op) {
	set_trig_mode((enum trig_modes)(TRIG_DEG + (op - OP_DEG)));
}
*/


void op_separator(enum nilop op) {
	int x = (op - OP_THOUS_ON);
	int state = x & 1;
	if ((x&2) != 0)
		UState.nointseparator = state;
	else
		UState.nothousands = state;
}

void op_fixscieng(enum nilop op) {
	UState.fixeng = (op == OP_FIXSCI) ? 0 : 1;
	UState.fract = 0;
}

#ifdef INCLUDE_DOUBLE_PRECISION
void op_double(enum nilop op) {
	static const unsigned char reglist[] = {
		regX_idx, regY_idx, regZ_idx, regT_idx, regL_idx, regI_idx, regJ_idx, regK_idx
	};
	const int dbl = (op == OP_DBLON);
	const int xrom = (isXROM(state_pc()));
	int i;
	if (dbl == State.mode_double) {
		if (xrom)
			// make J & K accessible to XROM code
			xcopy(XromJK, Regs + TOPREALREG - 4, 2 * sizeof(decimal128));
		return;
	}
	if (dbl) {
		if (NumRegs < 4) {
			// Need space for J & K
			cmdregs(1, RARG_REGS);
			if (Error) {
				return;
			}
		}
		if (xrom)
			xcopy(XromAtoD, get_reg_n(regA_idx), sizeof(XromAtoD));

		State.mode_double = 1;
		// Convert X, Y, Z, T, L, I, J & K to double precision
		// J & K go to highest numbered global registers
		// I goes to J/K
		// L goes to L/I
		// A to D are lost
		// T goes to C/D
		// Z goes to A/B
		// Y goes to Z/T
		// X goes to X/Y
		for (i = sizeof(reglist) - 1; i >= 0; --i) {
			const int j = reglist[ i ];
			packed128_from_packed(&(get_reg_n(j)->d), Regs + j);
		}
	}
	else {
		// Convert X/Y, Z/T, A/B, C/D and L/I to single precision
		// X comes from X/Y
		// Y comes from Z/T
		// Z comes from A/B
		// T comes from C/D
		// L comes from L/I
		// I comes from J/K
		// J & K come from highest numbered global registers
		// clear A to D
		for (i = 0; i < sizeof(reglist); ++i) {
			const int j = reglist[ i ];
			packed_from_packed128(Regs + j, &(get_reg_n(j)->d));
		}
		State.mode_double = 0;

		if (xrom)
			xcopy(get_reg_n(regA_idx), XromAtoD, sizeof(XromAtoD));
		else
			zero_regs(get_reg_n(regA_idx), 4);

		if (NumRegs > TOPREALREG)
			cmdregs(TOPREALREG - 1, RARG_REGS);
	}
}
#endif

void op_pause(unsigned int arg, enum rarg op) {
	display();
#ifndef CONSOLE
	// decremented in the low level heartbeat
	Pause = arg;
	GoFast = (arg == 0);
#else
#if defined(WIN32) && !defined(__GNUC__)
#pragma warning(disable:4996)
	sleep(arg/10);
#else
	usleep(arg * 100000);
#endif
#endif
}

void op_intsign(enum nilop op) {
	UState.int_mode = (op - OP_2COMP) + MODE_2COMP;
}


/* Switch to integer mode.
 * If we're coming from real mode we do funny stuff with the stack,
 * if we're already in int mode we leave alone.
 *
 * We take the real X register and put it into the x and y registers
 * such that the mantissa is in y and the exponent is in x.  There
 * is also an additional condition that 2^31 <= |y| < 2^32.
 *
 * Since the word size gets reset when we enter real mode, there is
 * plenty of space to do this and overflow isn't possible -- we have
 * to account for zero, infinities and NaNs.
 */
#ifndef HP16C_MODE_CHANGE
void int_mode_convert(int index) {
	decNumber x;
	int s;
	unsigned long long int v;

	getRegister(&x, index);
        decNumberTrunc(&x, &x);
	v = dn_to_ull(&x, &s);
	set_reg_n_int_sgn(index, v, s);
}
#endif

static void check_int_switch(void) {
	if (!is_intmode()) {
#ifdef HP16C_MODE_CHANGE
		decNumber x, y, z;
		int ex;			/* exponent |ex| < 1000 */
		unsigned long int m;	/* Mantissa 32 bits */
		int sgn, i;

		getX(&x);
		lift();
		if (decNumberIsSpecial(&x)) {
			/* Specials all have 0 mantissa and a coded exponent
			 * We cannot use +/- a number for the infinities since
			 * we might be in unsigned mode so we code them as 1 & 2.
			 * NaN's get 3.
			 */
			set_reg_n_int(regY_idx, 0);
			if (decNumberIsNaN(&x))
				setX_int(3);
			else if (decNumberIsNegative(&x))
				setX_int(2);
			else
				setX_int(1);

		} else if (dn_eq0(&x)) {
			/* 0 exponent, 0 mantissa -- although this can be negative zero */
			set_reg_n_int_sgn(regY_idx, 0, decNumberIsNegative(&x) ? 1 : 0);
			setX_int(0);
		} else {
			/* Deal with the sign */
			if (decNumberIsNegative(&x)) {
				dn_minus(&x, &x);
				sgn = 1;
			} else
				sgn = 0;
			/* Figure the exponent */
			decNumberLog2(&y, &x);
			decNumberTrunc(&z, &y);
			ex = dn_to_int(&z);
			/* On to the mantissa */
			decNumberPow2(&y, &z);
			dn_divide(&z, &x, &y);
			m = 1;
			decNumberFrac(&y, &z);
			for (i=0; i<31; i++) {
				dn_mul2(&z, &y);
				decNumberTrunc(&y, &z);
				m += m;
				if (! dn_eq0(&y))
					m++;
				decNumberFrac(&y, &z);
			}
			ex -= 31;
			/* Finally, round up if required */
			dn_mul2(&z, &y);
			decNumberTrunc(&y, &z);
			if (! dn_eq0(&y)) {
				m++;
				if (m == 0) {
					ex++;
					m = 0x80000000;
				}
			}
			/* The mantissa */
			set_reg_n_int_sgn(regY_idx, m, sgn);
			/* The exponent */
			if (ex < 0) {
				ex = -ex;
				sgn = 1;
			} else
				sgn = 0;
			setX_int_sgn(ex, sgn);
		}
#else
		int i;
		const int j = stack_size() + regX_idx;
		for (i = regX_idx; i < j; ++i)
			int_mode_convert(i);
		int_mode_convert(i);
#endif
		UState.intm = 1;
	}
}

static void set_base(unsigned int b) {
	UState.int_base = b - 1;
	check_int_switch();
}

void set_int_base(unsigned int arg, enum rarg op) {
	if (arg < 2) {
		if (arg == 0)
			op_float(OP_FLOAT);
		else
			op_fract(OP_FRACT);
	} else
		set_base(arg);
}

void op_datemode(enum nilop op) {
	UState.date_mode = (op - OP_DATEDMY) + DATE_DMY;
}

void op_setspeed(enum nilop op) {
	UState.slow_speed = (op == OP_SLOW) ? 1 : 0;
	update_speed(1);
}

void op_rs(enum nilop op) {
	if (Running)
		set_running_off();
	else {
		set_running_on();
		if (RetStkPtr == 0)
			RetStk[--RetStkPtr] = state_pc();
	}
}

void op_prompt(enum nilop op) {
	set_running_off();
	alpha_view_common(regX_idx);
}

// XEQUSR
// Command pushes 4 values on stack, needs to be followed by POPUSR
void do_usergsb(enum nilop op) {
	const unsigned int pc = state_pc();
	if (isXROM(pc) && XromUserPc != 0) {
		gsbgto(pc, 1, XromUserPc);    // push address of callee
		gsbgto(pc, 1, LocalRegs);     // push my local registers
		gsbgto(pc, 1, UserLocalRegs); // push former local registers
		gsbgto(XromUserPc, 1, pc);    // push return address, transfer control
		XromUserPc = 0;
		LocalRegs = UserLocalRegs;    // reestablish user environment
	}
}

// POPUSR
void op_popusr(enum nilop op) {
	if (isXROM(state_pc())) {
		UserLocalRegs = RetStk[RetStkPtr++]; // previous local registers
		LocalRegs =     RetStk[RetStkPtr++]; // my local registers
		XromUserPc =    RetStk[RetStkPtr++]; // adress of callee
	}
}

/* Tests if the user program is at the top level */
void isTop(enum nilop op) {
	int top = 0;

	if (Running) {
		top = RetStkPtr >= -1 - local_levels();
	}
	fin_tst(top);
}

/* Test if a number is an integer or fractional */
/* Special numbers are neither */
void XisInt(enum nilop op) {
	decNumber x;
	int result, op_int = (op == OP_XisINT);
	if ( is_intmode() )
		result = op_int;
	else if (decNumberIsSpecial(getX(&x)))
		result = 0;
	else
	        result = (is_int(&x) == op_int);
	fin_tst(result);
}

/* Test if a number is an even or odd integer */
/* fractional or special values are neither even nor odd */
void XisEvenOrOdd(enum nilop op) {
	decNumber x;
	int odd = (op == OP_XisODD);

	if (is_intmode()) {
		fin_tst((get_reg_n_int(regX_idx) & 1) == odd);
	} else {
		fin_tst(is_even(getX(&x)) == !odd);
	}
}


/* Test if a number is prime */
void XisPrime(enum nilop op) {
	int sgn;

	fin_tst(isPrime(get_reg_n_int_sgn(regX_idx, &sgn)) && sgn == 0);
}

/* Test is a number is infinite.
 */
void isInfinite(enum nilop op) {
	decNumber x;

	getX(&x);
	fin_tst(!is_intmode() && decNumberIsInfinite(&x));
}

/* Test for NaN.
 * this could be done by testing x != x, but having a special command
 * for it reads easier.
 */
void isNan(enum nilop op) {
	decNumber x;

	getX(&x);
	fin_tst(!is_intmode() && decNumberIsNaN(&x));
}

void isSpecial(enum nilop op) {
	decNumber x;

	getX(&x);
	fin_tst(!is_intmode() && decNumberIsSpecial(&x));
}

void op_entryp(enum nilop op) {
	fin_tst(State.entryp);
}

/* Bulk register operations */
static int reg_decode(int *s, int *n, int *d, int flash) {
	decNumber x, y;
	int rsrc, num, rdest, q, mx_src, mx_dest;

	if (is_intmode()) {
		bad_mode_error();
		return 1;
	}
	getX(&x);			// sss.nnddd~
	dn_mulpow10(&y, &x, 2 + 3);	// sssnnddd.~
	decNumberTrunc(&x, &y);		// sssnnddd.0
	rsrc = dn_to_int(&x);		// sssnnddd

	if (rsrc < 0) {
		if (!flash)
			goto range_error;
		rsrc = -rsrc;
	}
	else
		flash = 0;

	rdest = rsrc % 1000;		// ddd
	rsrc /= 1000;			// sssnn
	num = rsrc % 100;		// nn
	rsrc /= 100;			// sss

	mx_src = flash ? BackupFlash._numregs :
		 rsrc >= LOCAL_REG_BASE ? local_regs() : global_regs();
	if (rsrc >= mx_src)
		goto range_error;

	if (d != NULL) {
		mx_dest = rdest >= LOCAL_REG_BASE ? local_regs() : global_regs();

		if (num == 0) {
			/* Calculate the maximum non-overlapping size */
			if (flash || (rsrc >= LOCAL_REG_BASE) != (rdest >= LOCAL_REG_BASE))
				// source & destination in different memory areas
				num = mx_dest < mx_src ? mx_dest : mx_src;
			else {
				if (rsrc > rdest) {
					num = mx_src - rsrc;
					q = rsrc - rdest;
				}
				else {
					num = mx_dest - rdest;
					q = rdest - rsrc;
				}
				if (num > q)
					num = q;
			}
		}
		if (rdest >= LOCAL_REG_BASE)
			mx_dest += LOCAL_REG_BASE;
		if (rdest + num > mx_dest)
			goto range_error;
		// Set pointer
		*d = rdest;
	}
	else {
		if (num == 0) {
			num = mx_src - rsrc;
		}
	}
	if (rsrc >= LOCAL_REG_BASE)
		mx_src += LOCAL_REG_BASE;
	if (rsrc + num > mx_src)
		goto range_error;

	// Now point to the correct source register
	*s = flash ? FLASH_REG_BASE + rsrc : rsrc;
	*n = num;

	return 0;

range_error:
	err(ERR_RANGE);
	return 1;
}

void op_regcopy(enum nilop op) {
	int s, n, d;

	if (reg_decode(&s, &n, &d, 1))
		return;
	move_regs(get_reg_n(d), get_reg_n(s), n);
}

void op_regswap(enum nilop op) {
	int s, n, d, i;

	if (reg_decode(&s, &n, &d, 0) || s == d)
		return;
	else {
		if ((s > d && d + n > s) || (d > s && s + n > d))
			err(ERR_RANGE);
		else {
			for (i = 0; i < n; i++)
				swap_reg(get_reg_n(s + i), get_reg_n(d + i));
		}
	}
}

void op_regclr(enum nilop op) {
	int s, n;

	if (reg_decode(&s, &n, NULL, 0))
		return;
	zero_regs(get_reg_n(s), n);
}

void op_regsort(enum nilop op) {
	int s, n;
	decNumber pivot, a, t;
	int beg[10], end[10], i;

	if (reg_decode(&s, &n, NULL, 0) || n == 1)
		return;

	/* Non-recursive quicksort */
	beg[0] = 0;
	end[0] = n;
	i = 0;
	while (i>=0) {
		int L = beg[i];
		int R = end[i] - 1;
		if (L<R) {
			const int pvt = s + L;
			getRegister(&pivot, pvt);
			while (L<R) {
				while (L<R) {
					getRegister(&a, s + R);
					if (dn_lt0(dn_compare(&t, &a, &pivot)))
						break;
					R--;
				}
				if (L<R)
					copyreg_n(s + L++, s + R);
				while (L<R) {
					getRegister(&a, s + L);
					if (dn_lt0(dn_compare(&t, &pivot, &a)))
						break;
					L++;
				}
				if (L<R)
					copyreg_n(s + R--, s + L);
			}
			setRegister(s + L, &pivot);
			if (L - beg[i] < end[i] - (L+1)) {
				beg[i+1] = beg[i];
				end[i+1] = L;
				beg[i] = L+1;
			} else {
				beg[i+1] = L+1;
				end[i+1] = end[i];
				end[i] = L;
			}
			i++;
		} else
			i--;
	}
}



/* Print a single program step nicely.
 */
static void print_step(const opcode op) {
	char buf[16];
	const unsigned int pc = state_pc();
	char *p = TraceBuffer;

	if (isXROM(pc)) {
		*p++ = 'x';
	} else if (isLIB(pc)) {
		p = num_arg_0(p, nLIB(pc), 1);
		*p++ = ' ';
	}
	if (pc == 0)
		scopy(p, "000:");
	else {
		p = num_arg_0(p, user_pc(), 3);
		*p++ = ':';
		scopy_char(p, prt(op, buf), '\0');
		if (*p == '?')
			*p = '\0';
	}
	State2.disp_small = 1;
	DispMsg = TraceBuffer;
}


/* When stuff gets done, there are some bits of state that need
 * to be reset -- SHOW, ->base change the display mode until something
 * happens.  This should be called on that something.
 */
void reset_volatile_state(void) {
	State2.int_window = 0;
	UState.int_maxw = 0;
	State2.smode = SDISP_NORMAL;
}


/*
 *  Called by any long running function
 */
void busy(void)
{
	/*
	 *  Serve the hardware watch dog
	 */
	watchdog();

	/*
	 *  Increase the speed
	 */
	update_speed(1);

	/*
	 *  Indicate busy state to the user
	 */
	if ( !Busy && !Running ) {
		Busy = 1;
		message( "Wait...", NULL );
	}
}

/***************************************************************************
 * Function dispatchers.
 */

/*
 *  Check for a call into XROM space.
 *  Fix the pointer alignment on the go.
 */
static const s_opcode *check_for_xrom_address(void *fp)
{
	const s_opcode *xp = (const s_opcode *) ((unsigned int) fp & ~1);
	if (xp < xrom)
		return NULL;
#ifndef REALBUILD
	// On the device, XROM is at the end so this is not needed
	if (xp >= xrom + xrom_size)
		return NULL;
#endif
	return xp;
}

/*
 *  Check for a call into XROM space and dispatch it.
 */
static int dispatch_xrom(void *fp)
{
	const s_opcode *xp = check_for_xrom_address(fp);
	if (xp == NULL)
		return 0;

	UserLocalRegs = LocalRegs;
	gsbgto(addrXROM((xp - xrom) + 1), 1, state_pc());
	return 1;
}


/* Dispatch routine for niladic functions.
 */
static void niladic(const opcode op) {
	const unsigned int idx = argKIND(op);

	process_cmdline();
	if (idx < num_niladics) {
		if (is_intmode() && NILADIC_NOTINT(niladics[idx]))
			bad_mode_error();
		else if (! isNULL(niladics[idx].niladicf)) {
			FP_NILADIC fp = (FP_NILADIC) EXPAND_ADDRESS(niladics[idx].niladicf);
			if (dispatch_xrom(fp))
				return;
			else {
				switch (NILADIC_NUMRESULTS(niladics[idx])) {
				case 2:	lift_if_enabled();
				case 1:	lift_if_enabled();
				default:
					fp((enum nilop)idx);
					break;
				}
			}
		}
	} else
		illegal(op);
	if (idx != OP_rCLX)
		State.state_lift = 1;
}


/* Dispatch routine for monadic operations.
 * Since these functions take an argument from the X register, save it in
 * lastx and then replace it with their result, we can factor out the common
 * stack manipulatin code.
 */
static void monadic(const opcode op)
{
	unsigned int f;
	process_cmdline_set_lift();

	f = argKIND(op);
	if (f < num_monfuncs) {
		if (is_intmode()) {
			if (! isNULL(monfuncs[f].monint)) {
				FP_MONADIC_INT fp = (FP_MONADIC_INT) EXPAND_ADDRESS(monfuncs[f].monint);
				if (dispatch_xrom(fp))
					return;
				else {
					long long int x = get_reg_n_int(regX_idx);
					x = fp(x);
					setlastX();
					setX_int(x);
				}
			} else
				bad_mode_error();
		} else {
			if (! isNULL(monfuncs[f].mondreal)) {
				FP_MONADIC_REAL fp = (FP_MONADIC_REAL) EXPAND_ADDRESS(monfuncs[f].mondreal);
				if (dispatch_xrom(fp))
					return;
				else {
					decNumber x, r;
					getX(&x);
					if (NULL == fp(&r, &x))
						set_NaN(&r);
					setlastX();
					setX(&r);
				}
			} else
				bad_mode_error();
		}
	} else
		illegal(op);
}

static void monadic_cmplex(const opcode op) {
	decNumber x, y, rx, ry;
	unsigned int f;

	process_cmdline_set_lift();

	f = argKIND(op);

	if (f < num_monfuncs) {
		if (! isNULL(monfuncs[f].mondcmplx)) {
			FP_MONADIC_CMPLX fp = (FP_MONADIC_CMPLX) EXPAND_ADDRESS(monfuncs[f].mondcmplx);
			if (dispatch_xrom(fp))
				return;
			else {
				getXY(&x, &y);
				fp(&rx, &ry, &x, &y);
				setlastXY();
				setXY(&rx, &ry);
				set_was_complex();
			}
		} else
			bad_mode_error();
	} else
		illegal(op);
}

/***************************************************************************
 * Dyadic function handling.
 */

/* Dispatch routine for dyadic operations.
 * Again, these functions have a common argument decode and record and
 * common stack manipulation.
 */
static void dyadic(const opcode op) {

	unsigned int f;
	process_cmdline_set_lift();

	f = argKIND(op);
	if (f < num_dyfuncs) {
		if (is_intmode()) {
			if (! isNULL(dyfuncs[f].dydint)) {
				FP_DYADIC_INT fp = (FP_DYADIC_INT) EXPAND_ADDRESS(dyfuncs[f].dydint);
				if (dispatch_xrom(fp))
					return;
				else {
					long long int x = get_reg_n_int(regX_idx);
					long long int y = get_reg_n_int(regY_idx);
					x = fp(y, x);
					setlastX();
					lower();
					setX_int(x);
				}
			} else
				bad_mode_error();
		} else {
			if (! isNULL(dyfuncs[f].dydreal)) {
				FP_DYADIC_REAL fp = (FP_DYADIC_REAL) EXPAND_ADDRESS(dyfuncs[f].dydreal);
				if (dispatch_xrom(fp))
					return;
				else {
					decNumber x, y, r;
					getXY(&x, &y);
					if (NULL == fp(&r, &y, &x))
						set_NaN(&r);
					setlastX();
					lower();
					setX(&r);
				}
			} else
				bad_mode_error();
		}
	} else
		illegal(op);
}

static void dyadic_cmplex(const opcode op) {
	decNumber x1, y1, x2, y2, xr, yr;
	unsigned int f;

	process_cmdline_set_lift();

	f = argKIND(op);
	if (f < num_dyfuncs) {
		if (! isNULL(dyfuncs[f].dydcmplx)) {
			FP_DYADIC_CMPLX fp = (FP_DYADIC_CMPLX) EXPAND_ADDRESS(dyfuncs[f].dydcmplx);
			if (dispatch_xrom(fp))
				return;
			else {
				getXYZT(&x1, &y1, &x2, &y2);

				fp(&xr, &yr, &x2, &y2, &x1, &y1);

				setlastXY();
				lower2();
				setXY(&xr, &yr);
				set_was_complex();
			}
		} else
			bad_mode_error();
	} else
		illegal(op);
}

/* Dispatch routine for triadic operations.
 * Again, these functions have a common argument decode and record and
 * common stack manipulation.
 */
static void triadic(const opcode op) {
	unsigned int f;
	process_cmdline_set_lift();

	f = argKIND(op);
	if (f < num_trifuncs) {
		if (is_intmode()) {
			if (! isNULL(trifuncs[f].triint)) {
				FP_TRIADIC_INT fp = (FP_TRIADIC_INT) EXPAND_ADDRESS(trifuncs[f].triint);
				if (dispatch_xrom(fp))
					return;
				else {
					long long int x = get_reg_n_int(regX_idx);
					long long int y = get_reg_n_int(regY_idx);
					long long int z = get_reg_n_int(regZ_idx);
					x = fp(z, y, x);
					setlastX();
					lower();
					lower();
					setX_int(x);
				}
			} else
				bad_mode_error();
		} else {
			if (! isNULL(trifuncs[f].trireal)) {
				FP_TRIADIC_REAL fp = (FP_TRIADIC_REAL) EXPAND_ADDRESS(trifuncs[f].trireal);
				if (dispatch_xrom(fp))
					return;
				else {
					decNumber x, y, z, r;
					getXYZ(&x, &y, &z);
					if (NULL == fp(&r, &z, &y, &x))
						set_NaN(&r);
					setlastX();
					lower();
					lower();
					setX(&r);
				}
			} else
				bad_mode_error();
		}
	} else
		illegal(op);
}

/* Handle a command that takes an argument.  The argument is encoded
 * in the low order bits of the opcode.  We also have to take
 * account of the indirection flag and various limits -- we always work modulo
 * the limit.
 */
static void rargs(const opcode op) {
	unsigned int arg = op & RARG_MASK;
	int ind = op & RARG_IND;
	const unsigned int cmd = RARG_CMD(op);
	decNumber x;
	unsigned int lim = argcmds[cmd].lim;

	XeqOpCode = (s_opcode) cmd;
	if (lim == 0) lim = 256; // default

	process_cmdline();

	if (cmd >= num_argcmds) {
		illegal(op);
		return;
	}
	if (isNULL(argcmds[cmd].f)) {
		State.state_lift = 1;
		return;
	}

	if (ind && argcmds[cmd].indirectokay) {
		if (is_intmode()) {
			arg = (unsigned int) get_reg_n_int(arg);
		} else {
			getRegister(&x, arg);
			arg = dn_to_int(&x);
		}
	} else {
		if (lim > 128 && ind)		// put the top bit back in
			arg |= RARG_IND;
	}
	if (argcmds[cmd].reg && arg < TOPREALREG) {
		// Range checking for registers against variable boundary
		lim = global_regs();
		if (argcmds[cmd].cmplx)
			--lim;
	}
	else if (argcmds[cmd].flag) {
		if (LocalRegs == 0)
			lim -= 16;
		if ((int)arg < 0)
			arg = NUMFLG - (int)arg;
	}
	else if (argcmds[cmd].local) {
		// Range checking for local registers or flags
		lim = NUMREG + local_regs();
		if (argcmds[cmd].cmplx)
			--lim;
		else if (argcmds[cmd].stos)
			lim -= stack_size() - 1;
		if ((int)arg < 0)
			arg = NUMREG - (int)arg;
	}
	if (arg >= lim )
		err(ERR_RANGE);
	else if (argcmds[cmd].cmplx && arg >= TOPREALREG-1 && arg < NUMREG && (arg & 1))
		err(ERR_ILLEGAL);
	else {
		FP_RARG fp = (FP_RARG) EXPAND_ADDRESS(argcmds[cmd].f);
		if (NULL != check_for_xrom_address(fp)) {
			XromUserPc = find_label_from(state_pc(), arg, 0);
			if (XromUserPc != 0)
				dispatch_xrom(fp);
			return;
		}
		else
			fp(arg, (enum rarg)cmd);
		State.state_lift = 1;
	}
}

static void multi(const opcode op) {
	const int cmd = opDBL(op);
	XeqOpCode = (s_opcode) cmd;

	process_cmdline_set_lift();

	if (cmd >= num_multicmds) {
		illegal(op);
		return;
	}
	if (isNULL(multicmds[cmd].f))	// LBL does nothing
		return;
	else {
		FP_MULTI fp = (FP_MULTI) EXPAND_ADDRESS(multicmds[cmd].f);
		if (NULL != check_for_xrom_address(fp)) {
			XromUserPc = findmultilbl(op, FIND_OP_ERROR);
			if (XromUserPc != 0)
				dispatch_xrom(fp);
			return;
		}
		else
			fp(op, (enum multiops)cmd);
	}
}



/* Main dispatch routine that decodes the top level of the opcode and
 * goes to the appropriate lower level dispatch routine.
 */
void xeq(opcode op) 
{
#ifdef INCLUDE_DOUBLE_PRECISION
	decimal128 save[STACK_SIZE+2];
#else
	decimal64 save[STACK_SIZE+2];
#endif
	struct _ustate old = UState;
	unsigned short old_pc = state_pc();
	int old_cl = *((int *)&CommandLine);

#ifdef CONSOLE
	instruction_count++;
#endif
#ifndef REALBUILD
	if (State2.trace) {
		char buf[16];
		if (Running)
			print_step(op);
		else
			sprintf(TraceBuffer, "%04X:%s", op, prt(op, buf));
		DispMsg = TraceBuffer;
	}
#endif
	Busy = 0;
	xcopy(save, get_reg_n(regX_idx), sizeof(save));
	if (isDBL(op))
		multi(op);
	else if (isRARG(op))
		rargs(op);
	else {
		XeqOpCode = (s_opcode) op;
		switch (opKIND(op)) {
		case KIND_SPEC:	specials(op);	break;
		case KIND_NIL:	niladic(op);	break;
		case KIND_MON:	monadic(op);	break;
		case KIND_DYA:	dyadic(op);	break;
		case KIND_TRI:	triadic(op);	break;
		case KIND_CMON:	monadic_cmplex(op);	break;
		case KIND_CDYA:	dyadic_cmplex(op);	break;
		default:	illegal(op);
		}
	}

	if (Error != ERR_NONE) {
		// Repair stack and state
		// Clear return stack
		Error = ERR_NONE;
		xcopy(get_reg_n(regX_idx), save, sizeof(save));
		UState = old;
		raw_set_pc(old_pc);
		*((int *)&CommandLine) = old_cl;
		process_cmdline_set_lift();
		if (Running) {
#ifndef REALBUILD
			if (! State2.trace ) {
#endif
				unsigned short int pc = state_pc();
				while (isXROM(pc)) {
					// Leave XROM
					if (RetStkPtr != 0) {
						retstk_up();
						pc = RetStk[RetStkPtr - 1];
					}
					if (RetStkPtr == 0)
						++pc; // compensate for decpc below
				}
				raw_set_pc(pc);
#ifndef REALBUILD
			}
#endif
			decpc();	// Back to error instruction
			RetStkPtr = 0;  // clear return stack
			set_running_off();
		}
	} 
	reset_volatile_state();
}

/* Execute a single step and return.
 */
static void xeq_single(void) {
	const opcode op = getprog(state_pc());

	incpc();
	xeq(op);
}

/* Continue execution trough xrom code
 */
void xeq_xrom(void) {
	int is_running = Running;
#ifndef REALBUILD
	if (State2.trace)
		return;
#endif
	/* Now if we've stepped into the xROM area, keep going until
	 * we break free.
	 */
	Running = 1;	// otherwise RTN doesn't work
	while (!Pause && isXROM(state_pc()))
		xeq_single();
	Running = is_running;
}

/* Check to see if we're running a program and if so execute it
 * for a while.
 *
 */
void xeqprog(void) 
{
	int state = 0;

	if ( Running || Pause ) {
#ifndef CONSOLE
		long long last_ticker = Ticker;
		state = ((int) last_ticker % (2*TICKS_PER_FLASH) < TICKS_PER_FLASH);
#else
		state = 1;
#endif
		dot(RCL_annun, state);
		finish_display();

		while (!Pause && Running) {
			xeq_single();
			if (is_key_pressed()) {
				xeq_xrom();
				break;
			}
		}
	}
	if (!Running && !Pause) {
		// Program has terminated
		clr_dot(RCL_annun);
		display();
#ifndef CONSOLE
		// Avoid accidental restart with R/S or APD after program ends
		JustStopped = 1;
#endif
	}
}

/* Single step and back step routine
 */
void xeq_sst_bst(int kind) 
{
	opcode op;

	reset_volatile_state();
	if (kind == -1)
		decpc();

	if (State2.runmode) {
		// Display the step
		op = getprog(state_pc());
		print_step(op);
		if (kind == 1) {
			// Execute the step on key up
#ifndef REALBUILD
			unsigned int trace = State2.trace;
			State2.trace = 0;
#endif
			set_running_on_sst();
			incpc();
			xeq(op);
#ifndef REALBUILD
			State2.trace = trace;
#endif
			xeq_xrom();
			set_running_off_sst();
		}
	}
	else if (kind == 0) {
		// Key down in program mode
		incpc();
		OpCode = 0;
	}
}


/* 
 *  The following needs to be done each time before any user input is processed.
 *  On the hardware, RAM is volatile and these pointers and structures need valid values!
 */
void xeq_init_contexts(void) {
	/*
	 *  Compute the sizes of the various memory portions
	 */
	const short int s = ((TOPREALREG - NumRegs) << 2) - SizeStatRegs; // additional register space
	RetStk = RetStkBase + s;					  // Move RetStk up or down
	RetStkSize = s + RET_STACK_SIZE - ProgSize;
	ProgMax = s + RET_STACK_SIZE - MINIMUM_RET_STACK_SIZE;
	ProgFree = ProgMax - ProgSize + RetStkPtr;

	/*
	 *  Initialise our standard contexts.
	 *  We bump the digits for internal calculations.
	 */
	decContextDefault(&Ctx, DEC_INIT_BASE);
	Ctx.digits = DECNUMDIGITS;
	Ctx.emax=DEC_MAX_MATH;
	Ctx.emin=-DEC_MAX_MATH;
	Ctx.round = DEC_ROUND_HALF_EVEN;
}


/*
 *  We don't allow some commands from a running program
 */
int not_running(void) {
	if ( Running ) {
		err(ERR_ILLEGAL);
		return 0;
	}
	return 1;
}

/*
 *  Handle the Running Flag
 */
void set_running_off_sst() {
	Running = 0;
}

void set_running_on_sst() {
	Running = 1;
}

void set_running_off() {
	set_running_off_sst();
	State.entryp = 0;
	dot( RCL_annun, 0);
}

void set_running_on() {
	update_speed(0);
	GoFast = 1;
	set_running_on_sst();
	LastKey = 0;
	if (!isXROM(state_pc()))
		error_message(ERR_NONE);
	dot(BEG, 0);
	finish_display();
}

/*
 *  Command to support local variables.
 *  A stack frame is constructed:
 *	marker including size of frame,
 *	register + flag area.
 *  Registers must reside on even stack positions
 *  so the flag word is either at the top or at the bottom of the frame.
 */
void cmdlocr(unsigned int arg, enum rarg op) {
	short int sp = RetStkPtr;
#ifdef INCLUDE_DOUBLE_PRECISION
	int size = (++arg << (is_dblmode() ? 3 : 2)) + 2;
#else
	int size = (++arg << 2) + 2;
#endif
	const unsigned short marker = LOCAL_MASK | size;
	int old_size = 0;
	short unsigned int old_flags = 0;

	if (sp != 0 && sp == LocalRegs) {
		// resize required
		old_size = local_levels();
		sp += old_size;
		old_flags = *flag_word(LOCAL_FLAG_BASE, NULL);
	}
	// compute space needed
	sp -= size;
	if (-sp > RetStkSize) {
		err(ERR_RAM_FULL);
		return;
	}
	if ( old_size > 0 ) {
		// move previous contents to new destination
		int n;
		if (size > old_size) {
			n = old_size;
			size -= old_size;
		}
		else {
			n = size;
			size = 0;
		}
		xcopy(RetStk + sp, RetStk + LocalRegs, n + n);
	}
	// fill the rest with 0
	xset(RetStk + sp + old_size, 0, size + size);

	// set marker, pointers and flags
	RetStk[sp] = marker;
	RetStkPtr = LocalRegs = sp;
	*flag_word(LOCAL_FLAG_BASE, NULL) = old_flags;
}

/*
 *  Command to support a single set of local variables
 *  for non recursive non interruptible XROM routines.
 *  We need a single stack level for a special marker
 */
void cmdxlocal(enum nilop op) {
	if (isXROM(state_pc())) {
		if (RetStkPtr >= RetStkSize) {
			err(ERR_RAM_FULL);
			return;
		}
		RetStk[--RetStkPtr] = LOCAL_MASK | 1;
		// fill with 0
#ifdef INCLUDE_DOUBLE_PRECISION
		xset(XromRegs, 0, sizeof(XromRegs));
#else
		zero_regs(XromRegs, NUMXREGS);
#endif
		XromFlags = 0;
		LocalRegs = RetStkPtr;
	}
	else {
		cmdlocr(16, RARG_LOCR);
	}
}

#ifdef XROM_COMMANDS
void cmdxin(enum nilop op) {
	if (! isXROM(state_pc())) {
		err(ERR_ILLEGAL);
		return;
	}
	if (is_intmode()) {
		err(ERR_BAD_MODE);
		return;
	}

	cmdxlocal(OP_XLOCAL);
	if (is_dblmode())
		set_user_flag(LOCAL_FLAG_BASE + MAX_LOCAL_DIRECT - 1);
	op_double(OP_DBLON);
	cmdstostk(LOCAL_REG_BASE + MAX_LOCAL_DIRECT - 4, RARG_STOSTK);
}

void cmdxout(unsigned int arg, enum rarg op) {
	const int lastX = arg & 0x40;
	const int complex = arg & 0x80;
	const int pop = arg & 0007;
	const int push = (arg >> 3) & 0007;

	if (! isXROM(state_pc() || pop > 4 || push > 4)) {
		err(ERR_ILLEGAL);
		return;
	}
	if (is_intmode()) {
		err(ERR_BAD_MODE);
		return;
	}

	if (complex)
		set_was_complex();
/*
>>       restore stack
*/
	if (lastX) {
		if (complex) {
			setlastXY();
		} else {
			setlastX();
		}
	}
/*
>>       consume ccc items from stack
>>       push ppp items from internal stack to saved stack
*/
	if (get_user_flag(LOCAL_FLAG_BASE + MAX_LOCAL_DIRECT - 1))
		op_double(OP_DBLOFF);

	do_rtn(0);
}
#endif

/*
 *  Toggle UState mode bits from XROM
 */
void cmdmode(unsigned int arg, enum rarg cmd) {
	unsigned long long int bit = 1ll << arg;
	unsigned long long int *mode = (unsigned long long int *) & UState;

	if (cmd == RARG_MODE_SET)
		*mode |= bit;
	else
		*mode &= ~bit;
}

/*
 *  Undo the effect of LOCL by popping the current local frame.
 *  Needs to be executed from the same level that has established the frame.
 */
void cmdlpop(enum nilop op) {
	if (LocalRegs != RetStkPtr) {
		err(ERR_ILLEGAL);
		return;
	}
	RetStkPtr = LocalRegs;
	retstk_up();
	--RetStkPtr;
}

/*
 *  Reduce the number of global registers in favour of local data on the return stack
 */
void cmdregs(unsigned int arg, enum rarg op) {
	int distance;
	++arg;
#ifdef INCLUDE_DOUBLE_PRECISION
	if (is_dblmode()) {
		arg <<= 1;
		if( arg < 2 )
			arg = 2;	// Reserve space for J&K
	}
#endif
	distance = NumRegs - arg;
	// Move return stack, check for room
	if (move_retstk(distance << 2))
		return;
	// Move register contents, including the statistics registers
	xcopy((unsigned short *)(Regs + TOPREALREG - arg)     - SizeStatRegs,
	      (unsigned short *)(Regs + TOPREALREG - NumRegs) - SizeStatRegs,
	      (arg << 3) + (SizeStatRegs << 1));
	if (distance < 0)
#ifdef INCLUDE_DOUBLE_PRECISION
		xset(Regs + TOPREALREG + distance, 0, -distance << 3);
#else
		zero_regs((REGISTER *)(Regs + TOPREALREG + distance), -distance);
#endif
	NumRegs = arg;
}


/*
 *  Debugging output for the console version
 */
#if defined(DEBUG) && defined(CONSOLE) && !defined(WP34STEST)
extern unsigned char remap_chars(unsigned char ch);

static int compare(s_opcode a1, s_opcode a2, int cata) {
	char b1[16], b2[16];
	const unsigned char *s1, *s2;
	int i;

	xset(b1, 0, sizeof(b1));
	xset(b2, 0, sizeof(b2));
	s1 = (unsigned char *)catcmd(a1, b1);
	s2 = (unsigned char *)catcmd(a2, b2);
	if (*s1 == COMPLEX_PREFIX) s1++;
	if (*s2 == COMPLEX_PREFIX) s2++;

	for (i=0;;i++) {
		unsigned char c1 = *s1++;
		unsigned char c2 = *s2++;
		c1 = remap_chars(c1);
		c2 = remap_chars(c2);

		if (c1 != c2) {
			if (c1 > c2) {
				return 1;
			}
			return 0;
		} else if (c1 == '\0')
			break;
	}
	return 0;
}

static void check_cat(const enum catalogues cata, const char *name) {
	int i;
	char b1[16], b2[16];
	const int oldcata = State2.catalogue;
	int n;

	State2.catalogue = cata;
	n = current_catalogue_max();
	for (i=1; i<n; i++) {
		opcode cold = current_catalogue(i-1);
		opcode c = current_catalogue(i);
		if (compare(cold, c, cata))
			error("catalogue %s row %04x / %04x  %d / %d: %04o / %04o (%s / %s)", name, cold, c, i-1, i,
					0xff & cold, 0xff & c,
					catcmd(cold, b1), catcmd(c, b2));
	}
	State2.catalogue = oldcata;
}

static void check_const_cat(void) {
	int i;
	char b1[16], b2[16];
	char p1[64], p2[64];

	for (i=1; i<NUM_CONSTS; i++) {
		if (compare(CONST(i-1), CONST(i), 0)) {
			prettify(catcmd(CONST(i-1), b1), p1);
			prettify(catcmd(CONST(i), b2), p2);
			error("constants row %d / %d: %s / %s", i, i+1, p1, p2);
		}
	}
}

static void bad_table(const char *t, int row, const char *n, int nlen) {
	char buf[64], name[20];
	int i;

	for (i=0; i<nlen; i++)
		name[i] = n[i];
	name[nlen] = '\0';
	prettify(name, buf);
	error("%s table row %d: %6s", t, row, buf);
}

#endif

/* Main initialisation routine that sets things up for us.
 * Returns a nonzero result if it has cleared ram.
 */
int init_34s(void)
{
	int cleared = checksum_all();
	if ( cleared ) {
		reset();
	}
	init_state();
	xeq_init_contexts();
	ShowRPN = 1;

#if defined(CONSOLE) && !defined(WP34STEST) && defined(DEBUG)
	{
		int i;
	/* Sanity check the function table indices.
	 * These indicies must correspond exactly with the enum definition.
	 * This code validates that this is true and prints error messages
	 * if it isn't.
	 */
	for (i=0; i<num_monfuncs; i++)
		if (monfuncs[i].n != i)
			bad_table("monadic function", i, monfuncs[i].fname, NAME_LEN);
	for (i=0; i<num_dyfuncs; i++)
		if (dyfuncs[i].n != i)
			bad_table("dyadic function", i, dyfuncs[i].fname, NAME_LEN);
	for (i=0; i<num_trifuncs; i++)
		if (trifuncs[i].n != i)
			bad_table("triadic function", i, trifuncs[i].fname, NAME_LEN);
	for (i=0; i<num_niladics; i++)
		if (niladics[i].n != i)
			bad_table("niladic function", i, niladics[i].nname, NAME_LEN);
	for (i=0; i<num_argcmds; i++)
		if (argcmds[i].n != i)
			bad_table("argument command", i, argcmds[i].cmd, NAME_LEN);
	for (i=0; i<num_multicmds; i++)
		if (multicmds[i].n != i)
			bad_table("multi command", i, multicmds[i].cmd, NAME_LEN);
	check_const_cat();
	check_cat(CATALOGUE_COMPLEX, "complex");
	check_cat(CATALOGUE_STATS, "statistics");
	check_cat(CATALOGUE_SUMS, "summations");
	check_cat(CATALOGUE_PROB, "probability");
	check_cat(CATALOGUE_PROG, "programme");
	check_cat(CATALOGUE_MODE, "mode");
	check_cat(CATALOGUE_TEST, "tests");
	check_cat(CATALOGUE_INT, "int");
#ifdef MATRIX_SUPPORT
	check_cat(CATALOGUE_MATRIX, "matrix");
#endif
	/*
	check_cat(CATALOGUE_ALPHA, "alpha");
	check_cat(CATALOGUE_ALPHA_LETTERS, "alpha special upper case letters");
	// check_cat(CATALOGUE_ALPHA_LETTERS_LOWER, "alpha special lower letters");
	check_cat(CATALOGUE_ALPHA_SUBSCRIPTS, "alpha subscripts");
	check_cat(CATALOGUE_ALPHA_SYMBOLS, "alpha symbols");
	check_cat(CATALOGUE_ALPHA_COMPARES, "alpha compares");
	check_cat(CATALOGUE_ALPHA_ARROWS, "alpha arrows");
	*/
	check_cat(CATALOGUE_CONV, "conversion");
	check_cat(CATALOGUE_NORMAL, "float");
#ifdef INCLUDE_INTERNAL_CATALOGUE
	check_cat(CATALOGUE_INTERNAL, "internal");
#endif
        if (sizeof(unsigned long long int) != sizeof(UState))
            error("sizeof register (%u) != sizeof user state (%u)\n", sizeof(unsigned long long int), sizeof(UState));
	}
#endif
	return cleared;
}

#ifdef GNUC_POP_ERROR
#pragma GCC diagnostic pop
#endif


