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
#include "keys.h"
#include "display.h"
#include "lcd.h"
#include "int.h"
#include "consts.h"
#include "storage.h"
#include "catalogues.h"


#define STATE_UNFINISHED	(OP_SPEC | OP_UNFINISHED)
#define STATE_BACKSPACE		(OP_SPEC | OP_BACKSPACE)
#define STATE_SST		(OP_SPEC | OP_SST)
#define STATE_BST		(OP_SPEC | OP_BST)
#define STATE_RUNNING		(OP_SPEC | OP_RUNNING)
#define STATE_IGNORE		(OP_SPEC | OP_IGNORE)

/* Define this if the key codes map rows sequentially */

#define TEST_EQ		0
#define TEST_NE		1
#define TEST_LT		2
#define TEST_LE		3
#define TEST_GT		4
#define TEST_GE		5

static void advance_to_next_label(unsigned int pc);


/* Short blink of PRN annunciator with every key */
int BusyBlink;

enum confirmations {
	confirm_none=0, confirm_clall, confirm_reset, confirm_clprog
};

enum shifts cur_shift(void) {
	return (enum shifts) State2.shifts;
}

/*
 * Mapping from the key code to a linear index
 * The trick is to move the shifts and the holes in the map out of the way
 */
static unsigned char keycode_to_linear(const keycode c)
{
	static const unsigned char linear_key_map[ 7 * 6 ] = {
		 0,  1,  2,  3,  4,  5,   // K00 - K05
		 6,  7,  8,  0,  0,  0,   // K10 - K15
		 9, 10, 11, 12, 13,  0,   // K20 - K24
		14, 15, 16, 17, 18,  0,   // K30 - K34
		19, 20, 21, 22, 23,  0,   // K40 - K44
		24, 25, 26, 27, 28,  0,   // K50 - K54
		29, 30, 31, 32, 33,  0    // K60 - K64
	};
	return linear_key_map[c];
}

/*
 *  Mapping from a key code to a digit from 0 to 9 or to a register address
 */
static signed char keycode_to_digit_or_register(const keycode c)
{
	static const signed char map[] = {
		regA_idx, regB_idx, regC_idx, regD_idx,       -1, -1,
		      -1,       -1, regI_idx,
		      -1, regJ_idx, regK_idx, regL_idx,       -1,
		      -1,        7,        8,        9,       -1,
		      -1,        4,        5,        6, regT_idx,
		      -1,        1,        2,        3,       -1,
		      -1,        0, regX_idx, regY_idx, regZ_idx
	};

	return map[keycode_to_linear(c)];
}

/*
 *  Mapping of a keycode and shift state to a catalogue number
 */
static enum catalogues keycode_to_cat(const keycode c, const enum shifts s)
{
	if (! State2.alphas) {
		/*
		 *  Normal processing
		 */
		if (s == SHIFT_F && c == K60)
			return CATALOGUE_REGISTERS;

		if (s != SHIFT_H)
			return CATALOGUE_NONE;

		switch (c) {

		case K_ARROW:
			if (UState.intm)
				return CATALOGUE_NONE;
			return CATALOGUE_CONV;

		case K05:
#ifdef INCLUDE_INTERNAL_CATALOGUE
			if (State2.cmplx)
				return CATALOGUE_INTERNAL;
#endif
			return CATALOGUE_MODE;

		case K10:	
			return CATALOGUE_LABELS;

		case K20:
			if (UState.intm)
				return CATALOGUE_NONE;
			if (State2.cmplx)
				return CATALOGUE_COMPLEX_CONST;
			return CATALOGUE_CONST;

		case K41:
			if (UState.intm)
				return CATALOGUE_NONE;
			return CATALOGUE_PROB;

		case K42:
			if (UState.intm)
				return CATALOGUE_NONE;
			return CATALOGUE_STATS;

		case K44:
			return CATALOGUE_STATUS;

		case K50:
			if (UState.intm)
				return CATALOGUE_INT;
			if (State2.cmplx)
				return CATALOGUE_COMPLEX;
			if (! State2.runmode)
				return CATALOGUE_PROGXFCN;
			return CATALOGUE_NORMAL;

		case K51:
			return CATALOGUE_TEST;

		case K52:
			return CATALOGUE_PROG;

		default:
			break;
		}
	}
	else {
		/*
		 *  All the alpha catalogues go here
		 */
		if (s != SHIFT_F && s != SHIFT_H)
			return 0;

		switch (c) {

		case K12:
			if (s == SHIFT_F)
				return CATALOGUE_ALPHA_SUBSCRIPTS;
			else
				return CATALOGUE_ALPHA_SUPERSCRIPTS;
			break;

		case K_ARROW:	// Alpha comparison characters
			if (s == SHIFT_F)
				return CATALOGUE_ALPHA_ARROWS;
			break;

		case K_CMPLX:	// Complex character menu
			if (s == SHIFT_F) {
				if (State2.alphashift)
					return CATALOGUE_ALPHA_LETTERS_LOWER;
				else
					return CATALOGUE_ALPHA_LETTERS_UPPER;
			}
			break;

		case K50:
			if (s == SHIFT_H) {	// Alpha command catalogue
				if (! State2.runmode)
					return CATALOGUE_PROGXFCN;
				else
					return CATALOGUE_ALPHA;
			}
			break;

		case K51:	// Alpha comparison characters
			if (s == SHIFT_H)
				return CATALOGUE_ALPHA_COMPARES;
			break;

		case K62:	// Alpha maths symbol characters
			if (s == SHIFT_H)
				return CATALOGUE_ALPHA_SYMBOLS;
			break;

		default:
			break;
		}
	}
	return CATALOGUE_NONE;
}


/*
 * Mapping from key position to alpha in the four key planes plus
 * the two lower case planes.
 */
static unsigned char keycode_to_alpha(const keycode c, unsigned int s)
{
	static const unsigned char alphamap[][6] = {
		/*upper f-sft g-sft h-sft lower g-shf lower */
		{ 'A',  0221, 0200, 0000, 'a',  0240,  },  // K00
		{ 'B',  'B',  0201, 0000, 'b',  0241,  },  // K01
		{ 'C',  'C',  0202, 0000, 'c',  0242,  },  // K02
		{ 'D',  0003, 0203, 0000, 'd',  0243,  },  // K03
		{ 'E',  0015, 0204, 0000, 'e',  0244,  },  // K04
		{ 'F',  0024, 0224, 0000, 'f',  0264,  },  // K05

		{ 'G',  0000, 0202, 0020, 'g',  0242,  },  // K10
		{ 'H',  0000, 0225, 0016, 'h',  0265,  },  // K11
		{ 'I',  0000, 0210, 0000, 'i',  0250,  },  // K12

		{ 0000, 0000, 0206, 0000, 0000, 0246,  },  // K20
		{ 'J',  0000, 0000, 0027, 'j',  0000,  },  // K21
		{ 'K',  0010, 0211, '\\', 'k',  0251,  },  // K22
		{ 'L',  0246, 0212, 0257, 'l',  0252,  },  // K23
		{ 0, 0, 0, 0, 0, 0 },

		{ 0000, 0000, 0000, 0000, 0000, 0000,  },  // K30
		{ 'M',  '7',  0213, '&',  'm',  0253,  },  // K31
		{ 'N',  '8',  0214, '|',  'n',  0254,  },  // K32
		{ 'O',  '9',  0227, 0013, 'o',  0267,  },  // K33
		{ 'P',  '/',  0217, 0235, 'p',  0257,  },  // K34

		{ 0020, 0000, 0000, '!',  0020, 0000,  },  // K40
		{ 'Q',  '4',  0000, '?',  'q',  0000,  },  // K41
		{ 'R',  '5',  0220, 0000, 'r',  0260,  },  // K42
		{ 'S',  '6',  0221, '$',  's',  0261,  },  // K43
		{ 'T',  0034, 0222, 0217, 't',  0262,  },  // K44

		{ 0017, '(',  ')',  0000, 0000, ')',   },  // K50
		{ '1',  '1',  0207, 0000, '1',  0247,  },  // K51
		{ 'U',  '2',  0000, 0014, 'u',  0000,  },  // K52
		{ 'V',  '3',  0000, 0036, 'v',  0000,  },  // K53
		{ 'W',  '-',  0000, '%',  'w',  0000,  },  // K54

		{ 0000, 0000, 0000, 0000, 0000, 0000,  },  // K60
		{ '0',  '0',  0226, ' ',  '0',  0266,  },  // K61
		{ 'X',  '.',  0215, 0000, 'x',  0255,  },  // K62
		{ 'Y',  0000, 0223, 0037, 'y',  0263,  },  // K63
		{ 'Z',  '+',  0205, 0000, 'z',  0245,  },  // K64
	};
	if (State2.alphashift) {
		if (s == SHIFT_N)
			s = SHIFT_LC_N;
		else if (s == SHIFT_G)
			s = SHIFT_LC_G;
	}
	return alphamap[keycode_to_linear(c)][s];
}

void set_shift(enum shifts s) {
	State2.shifts = s;
	State2.alpha_pos = 0;
}

static void init_arg(const enum rarg base) {
	State.base = base;
	State2.ind = 0;
	State2.digval = 0;
	State2.numdigit = 0;
	State2.rarg = 1;
	State2.dot = 0;
}

static void init_cat(enum catalogues cat) {
	if (cat == CATALOGUE_NONE && State2.catalogue != CATALOGUE_NONE) {
		// Save last catalogue for a later restore
		State.last_cat = State2.catalogue;
		State.last_catpos = State2.digval;
		CmdLineLength = 0;
	}
	process_cmdline();
	if (cat == CATALOGUE_LABELS) {
		// Label browser
		State2.registerlist = 0;
		State2.status = 0;
		State2.labellist = 1;
		advance_to_next_label(0);
	}
	else if (cat == CATALOGUE_REGISTERS) {
		// Register browser
		State2.status = 0;
		State2.labellist = 0;
		State2.registerlist = 1;
		State2.digval = regX_idx;
		State2.digval2 = 0;
	}
	else if (cat == CATALOGUE_STATUS) {
		// Register browser
		State2.registerlist = 0;
		State2.labellist = 0;
		State2.status = 1;
	}
	else {
		// Normal catalogue
		State2.catalogue = cat;
		State2.cmplx = (cat == CATALOGUE_COMPLEX || cat == CATALOGUE_COMPLEX_CONST)?1:0;
		if (cat != CATALOGUE_NONE && State.last_cat == cat) {
			// Same catalogue again, restore position
			State2.digval = State.last_catpos;
		}
		else {
			State2.digval = 0;
		}
	}
	set_shift(SHIFT_N);
}

void init_state(void) {
	unsigned int a = state_pc();
	unsigned int b = State.entryp;

	xset(&State, 0, sizeof(struct _state));
	State.state_lift = 1;
	State.entryp = b;
	set_pc(a);

#ifndef REALBUILD
	a = State2.flags;
	b = State2.trace;
#endif
	xset(&State2, 0, sizeof(State2));
	//State2.shifts = SHIFT_N;
	State2.test = TST_NONE;
	State2.runmode = 1;
#ifndef REALBUILD
	State2.trace = b;
	State2.flags = a;
#endif
	ShowRegister = regX_idx;
}

void soft_init_state(void) {
	int soft;
	unsigned int runmode;

	if (CmdLineLength) {
		CmdLineLength = 0;
		CmdLineEex = 0;
		CmdLineDot = 0;
		return;
	}
	soft = State2.multi || State2.rarg || State2.hyp || State2.gtodot || State2.labellist ||
			State2.cmplx || State2.arrow || State2.test != TST_NONE || State2.status;
	if (soft) {
		runmode = State2.runmode;
	}
	init_state();
	if (soft) {
		State2.runmode = runmode;
	}
}

static void init_confirm(enum confirmations n) {
	State2.confirm = n;
}

static void set_smode(const enum single_disp d) {
	State2.smode = (State2.smode == d)?SDISP_NORMAL:d;
}

static int check_f_key(int n, const int dflt) {
	const int code = 100 + n;
	unsigned int pc = state_pc();

	if(isXROM(pc))
		pc = 1;
	if (find_label_from(pc, code, 1))
		return RARG(RARG_XEQ, code);
	return dflt;
}

/* Return non-zero if the current mode is integer and we accept letters
 * as digits.
 */
static int intltr(int d) {
	return (UState.intm && (int) int_base() > d);
}

/*
 *  Process a key code in the unshifted mode.
 */
static int process_normal(const keycode c)
{
	static const unsigned short int op_map[] = {
		// Row 1
		OP_SPEC | OP_SIGMAPLUS, // A to D
		OP_MON  | OP_RECIP,
		OP_DYA  | OP_POW,
		OP_MON  | OP_SQRT,
		STATE_UNFINISHED,	// ->
		STATE_UNFINISHED,	// CPX
		// Row 2
		RARG_STO,
		RARG_RCL,
		OP_NIL  | OP_RDOWN,
		// Row 3
		OP_SPEC | OP_ENTER,
		OP_NIL  | OP_SWAP,	// x<>y
		OP_SPEC | OP_CHS,	// CHS
		OP_SPEC | OP_EEX,	// EEX
		OP_SPEC | OP_CLX,	// <-
		// Row 4
		RARG_XEQ,
		OP_SPEC | OP_7,
		OP_SPEC | OP_8,
		OP_SPEC | OP_9,
		OP_DYA  | OP_DIV,
		// Row 5
		STATE_BST,
		OP_SPEC | OP_4,
		OP_SPEC | OP_5,
		OP_SPEC | OP_6,
		OP_DYA  | OP_MUL,
		// Row 6
		STATE_SST,		// SST
		OP_SPEC | OP_1,
		OP_SPEC | OP_2,
		OP_SPEC | OP_3,
		OP_DYA  | OP_SUB,
		// Row 7
		STATE_UNFINISHED,	// ON/C
		OP_SPEC | OP_0,
		OP_SPEC | OP_DOT,
		OP_NIL  | OP_RS,	// R/S
		OP_DYA  | OP_ADD
	};
	int lc = keycode_to_linear(c);
	int op = op_map[lc];

	// The switch handles all the special cases
	switch (c) {
	case K00:
	case K01:
	case K02:
	case K03:
	case K_ARROW:
	case K_CMPLX:
		if (intltr(lc + 10))
			return ( OP_SPEC | OP_A ) + lc;

		if ( c == K_ARROW ) {
			State2.arrow = 1;
			break;
		}
		else if ( c == K_CMPLX ) {
			if (!UState.intm)
				State2.cmplx = 1;
			break;
		}
		return check_f_key(lc, op);

	case K24:				// <-
		if (State2.runmode)
			return op;
		return STATE_BACKSPACE;

	case K10:				// STO
	case K11:				// RCL
	case K30:				// XEQ
		init_arg((enum rarg)op);
		break;

	default:
		return op;			// Keys handled by table
	}
	return STATE_UNFINISHED;
}


static int process_f_shifted(const keycode c) {
	static const unsigned short int op_map[] = {
		// Row 1
		STATE_UNFINISHED,	// HYP
		OP_MON | OP_SIN,
		OP_MON | OP_COS,
		OP_MON | OP_TAN,
		OP_NIL | OP_P2R,
		OP_NIL | OP_FRACPROPER,
		// Row 2
		OP_NIL | OP_HMS,
		OP_NIL | OP_FLOAT,
		OP_NIL | OP_RANDOM,
		// Row 3
		STATE_UNFINISHED,	// Alpha
		OP_NIL | OP_ALPHATOX,
		RARG(RARG_BASE, 2),
		RARG(RARG_BASE, 10),
		OP_NIL | OP_CLRALPHA,
		// Row 4
		OP_MON | OP_EXP,
		OP_MON | OP_10POWX,
		OP_MON | OP_2POWX,
		OP_DYA | OP_POW,
		OP_MON | OP_RECIP,
		// Row 5
		OP_DYA | OP_COMB,
		OP_MON | OP_cdf_Q,
		OP_NIL | OP_statMEAN,
		OP_MON | OP_yhat,
		OP_MON | OP_SQRT,
		// Row 6
		STATE_UNFINISHED,	// Window left
		STATE_UNFINISHED,	// Test X=
		RARG_SOLVE,
		RARG_PROD,
		OP_MON | OP_PERCNT,
		// Row 7
		STATE_UNFINISHED,	// Register browser
		OP_MON | OP_ABS,
		OP_MON | OP_TRUNC,
		RARG_LBL,
		RARG_DSE
	};
	static const unsigned short int op_map2[] = {
		OP_SPEC | OP_SIGMAPLUS,
		OP_MON  | OP_RECIP,
		OP_DYA  | OP_POW,
		OP_MON  | OP_SQRT
	};
	int lc = keycode_to_linear(c);
	int op = op_map[lc];
	set_shift(SHIFT_N);

	// The switch handles all the special cases
	switch (c) {
	case K00:
	case K01:
	case K02:
	case K03:
		if (UState.intm)
			return check_f_key(lc, op_map2[lc]);

		if ( c == K00 ) {
			State2.hyp = 1;
			State2.dot = 1;
			State2.cmplx = 0;
		}
		return op;			// unfinished in case of HYP (K00)

	case K_ARROW:
		if (UState.intm) {
			State2.arrow = 1;
		}
		return op;

	case K20:				// ALpha
		State2.alphas = 1;
		process_cmdline_set_lift();
		break;

	case K50:				// Window left
		if (UState.intm && UState.int_maxw > State2.int_window)
			State2.int_window++;
		break;

	case K51:				// Test X=
		State2.test = TST_EQ;
		break;

	case K52:
	case K53:
	case K63:
	case K64:
		init_arg((enum rarg)op);
		break;

	default:
		return (unsigned short) op;	// Keys handled by table
	}
	return STATE_UNFINISHED;
}

static int process_g_shifted(const keycode c) {
	static const unsigned short int op_map[] = {
		// Row 1
		STATE_UNFINISHED,	// HYP-1
		OP_MON | OP_ASIN,
		OP_MON | OP_ACOS,
		OP_MON | OP_ATAN,
		OP_NIL | OP_R2P,
		OP_NIL | OP_FRACIMPROPER,
		// Row 2
		OP_NIL | OP_DEG,
		OP_NIL | OP_RAD,
		OP_NIL | OP_GRAD,
		// Row 3
		OP_NIL | OP_FILL,
		OP_NIL | OP_XTOALPHA,
		RARG(RARG_BASE, 8),
		RARG(RARG_BASE, 16),
		OP_NIL | OP_SIGMACLEAR,
		// Row 4
		OP_MON | OP_LN,
		OP_MON | OP_LOG,
		OP_MON | OP_LG2,
		OP_DYA | OP_LOGXY,
		OP_DYA | OP_PARAL,
		// Row 5
		OP_DYA | OP_PERM,
		OP_MON | OP_qf_Q,
		OP_NIL | OP_statS,
		OP_NIL | OP_statR,
		OP_MON | OP_SQR,
		// Row 6
		STATE_UNFINISHED,	// Window right
		STATE_UNFINISHED,	// Test X!=
		RARG_INTG,
		RARG_SUM,
		OP_MON | OP_PERCHG,
		// Row 7
		OP_NIL | OP_OFF,
		OP_MON | OP_RND,
		OP_MON | OP_FRAC,
		OP_NIL | OP_RTN,
		RARG_ISG
	};

	int lc = keycode_to_linear(c);
	int op = op_map[lc];
	set_shift(SHIFT_N);

	// The switch handles all the special cases
	switch (c) {
	case K00:
		State2.hyp = 1;
		State2.dot = 0;
		State2.cmplx = 0;
		break;

	case K50:	
		if (UState.intm && UState.int_maxw > 0 && State2.int_window > 0)
			State2.int_window--;
		break;

	case K51:
		State2.test = TST_NE;
		break;

	case K52:
	case K53:
	case K64:
		init_arg((enum rarg) op);
		break;

	default:
		return (unsigned short) op;	// Keys handled by table
	}
	return STATE_UNFINISHED;
}

static int process_h_shifted(const keycode c) {
#define _RARG 0x8000	// Must not interfere with existing opcode markers
	static const unsigned short int op_map[] = {
		// Row 1
		_RARG   | RARG_STD,
		_RARG   | RARG_FIX,
		_RARG   | RARG_SCI,
		_RARG   | RARG_ENG,
		STATE_UNFINISHED,	// CONV
		STATE_UNFINISHED,	// MODE
		// Row 2
		STATE_UNFINISHED,	// CAT
		_RARG   | RARG_VIEW,
		OP_NIL  | OP_RUP,
		// Row 3
		STATE_UNFINISHED,	// CONST
		_RARG   | RARG_SWAP,
		OP_MON  | OP_NOT,
		STATE_UNFINISHED,	// CLP
		OP_NIL  | OP_rCLX,
		// Row 4
		_RARG   | RARG_GTO,
		OP_DYA  | OP_LAND,
		OP_DYA  | OP_LOR,
		OP_DYA  | OP_LXOR,
		OP_DYA  | OP_MOD,
		// Row 5
		OP_MON  | OP_FACT,
		STATE_UNFINISHED,	// PROB
		STATE_UNFINISHED,	// STAT
		OP_NIL  | OP_statLR,
		STATE_UNFINISHED,	// STATUS
		// Row 6
		STATE_UNFINISHED,	// X.FCN
		STATE_UNFINISHED,	// TEST
		STATE_UNFINISHED,	// P.FCN
		CONST(OP_PI),
		OP_SPEC | OP_SIGMAMINUS,
		// Row 7
		STATE_UNFINISHED,	// SHOW
		_RARG   | RARG_PAUSE,
		OP_NIL  | OP_RADCOM,
		STATE_UNFINISHED,	// P/R
		OP_SPEC | OP_SIGMAPLUS
	};

	int lc = keycode_to_linear(c);
	int op = op_map[lc];
	set_shift(SHIFT_N);

	// The switch handles all the special cases
	switch (c) {

	case K23:
		if (State2.runmode)
			clrretstk();
		else
			init_confirm(confirm_clprog);
		break;

	case K60:
		set_smode(SDISP_SHOW);
		break;

	case K62:
		if (UState.fraccomma)
			op = OP_NIL | OP_RADDOT;
		break;

	case K63:					// Program<->Run mode
		State2.runmode = 1 - State2.runmode;
		process_cmdline_set_lift();
		break;

	default:
		break;
	}

	if ( op != STATE_UNFINISHED ) {
		if ( op & _RARG ) {
			init_arg( (enum rarg) (op & ~_RARG) );
			op = STATE_UNFINISHED;
		}
	}
	return op;
#undef _RARG
}


static int process_normal_cmplx(const keycode c) {
	State2.cmplx = 0;
	switch (c) {
	case K00:	return check_f_key(0, STATE_UNFINISHED);
	case K01:	return check_f_key(1, OP_CMON | OP_RECIP);
	case K02:	return check_f_key(2, OP_CDYA | OP_POW);
	case K03:	return check_f_key(3, OP_CMON | OP_SQRT);

	case K10:	init_arg(RARG_CSTO);	break;	// complex STO
	case K11:	init_arg(RARG_CRCL);	break;	// complex RCL
	case K12:	return OP_NIL | OP_CRDOWN;

	case K20:	return OP_NIL | OP_CENTER;
	case K21:	return OP_NIL | OP_CSWAP;
	case K22:	return OP_CMON | OP_CCHS;		// CHS

	case K34:	return OP_CDYA | OP_DIV;
	case K44:	return OP_CDYA | OP_MUL;
	case K54:	return OP_CDYA | OP_SUB;
	case K64:	return OP_CDYA | OP_ADD;
	default:	break;
	}
	return STATE_UNFINISHED;
}

static int process_f_shifted_cmplx(const keycode c) {
	set_shift(SHIFT_N);
	State2.cmplx = 0;
	switch (c) {
	case K00:
		State2.hyp = 1;
		State2.dot = 1;
		State2.cmplx = 1;
		break;
	case K01:	return OP_CMON | OP_SIN;
	case K02:	return OP_CMON | OP_COS;
	case K03:	return OP_CMON | OP_TAN;
	case K04:	return OP_NIL | OP_P2R;
	case K_CMPLX:	set_shift(SHIFT_F);	break;

	case K30:	return OP_CMON | OP_EXP;
	case K31:	return OP_CMON | OP_10POWX;
	case K32:	return OP_CMON | OP_2POWX;
	case K33:	return OP_CDYA | OP_POW;
	case K34:	return OP_CMON | OP_RECIP;

	case K40:	return OP_CDYA | OP_COMB;
	case K44:	return OP_CMON | OP_SQRT;

	case K51:
		State2.cmplx = 1;
		State2.test = TST_EQ;
		break;

	case K60:
		init_state();
		break;

	case K61:	return OP_CMON | OP_ABS;
	case K62:	return OP_CMON | OP_TRUNC;

	default:
		break;
	}
	return STATE_UNFINISHED;
}

static int process_g_shifted_cmplx(const keycode c) {
	set_shift(SHIFT_N);
	State2.cmplx = 0;
	switch (c) {
	case K00:
		State2.hyp = 1;
		State2.dot = 0;
		State2.cmplx = 1;
		break;
	case K01:	return OP_CMON | OP_ASIN;
	case K02:	return OP_CMON | OP_ACOS;
	case K03:	return OP_CMON | OP_ATAN;
	case K04:	return OP_NIL | OP_R2P;
	case K_CMPLX:	set_shift(SHIFT_G);	break;

	case K20:	return OP_NIL | OP_CFILL;

	case K30:	return OP_CMON | OP_LN;
	case K31:	return OP_CMON | OP_LOG;
	case K32:	return OP_CMON | OP_LG2;
	case K33:	return OP_CDYA | OP_LOGXY;
	case K34:	return OP_CDYA | OP_PARAL;

	case K40:	return OP_CDYA | OP_PERM;
	case K44:	return OP_CMON | OP_SQR;

	case K51:
		State2.cmplx = 1;
		State2.test = TST_NE;
		break;

	case K60:
		init_state();
		break;

	case K61:	return OP_CMON | OP_RND;
	case K62:	return OP_CMON | OP_FRAC;

	default:
		break;
	}
	return STATE_UNFINISHED;
}

static int process_h_shifted_cmplx(const keycode c) {
	set_shift(SHIFT_N);
	State2.cmplx = 0;
	switch (c) {
	case K12:	return OP_NIL | OP_CRUP;

	case K21:	init_arg(RARG_CSWAP);	break;	// x<>
	case K22:	return OP_CMON | OP_CCONJ;

	case K40:	return OP_CMON | OP_FACT;	// z!

	case K53:	return CONST_CMPLX(OP_PI);

	default:
		break;
	}
	return STATE_UNFINISHED;
}


/* Fairly simple routine for dealing with the HYP prefix.
 * This setting can only be followed by 4, 5, or 6 to specify
 * the function.  The inverse routines use the code too, the State2.dot
 * is 1 for normal and 0 for inverse hyperbolic.  We also have to
 * deal with the complex versions and the handling of that key and
 * the ON key are dealt with by our caller.
 */
static int process_hyp(const keycode c) {
	const int cmplx = State2.cmplx;
	const int dot = State2.dot;
	const opcode x = cmplx?OP_CMON:OP_MON;

	State2.hyp = 0;
	State2.cmplx = 0;
	State2.dot = 0;

	switch (c) {
	case K01:	return x + (dot?OP_SINH:OP_ASINH);
	case K02:	return x + (dot?OP_COSH:OP_ACOSH);
	case K03:	return x + (dot?OP_TANH:OP_ATANH);

	case K60:
	case K24:
		break;
	default:
		State2.hyp = 1;
		State2.cmplx = cmplx;
		State2.dot = dot;
		break;
	}
	return STATE_UNFINISHED;
}


static int process_arrow(const keycode c) {
	const enum shifts oldstate = cur_shift();

	set_shift(SHIFT_N);
	State2.arrow = 0;
	switch (c) {
	case K10:
		if (oldstate == SHIFT_N || oldstate == SHIFT_G)
			return OP_MON | OP_2DEG;
		return OP_MON | OP_2HMS;
	case K11:
		if (oldstate == SHIFT_N || oldstate == SHIFT_G)
			return OP_MON | OP_2RAD;
		return OP_MON | OP_HMS2;
	case K12:	return OP_MON | OP_2GRAD;

	case K20:
		if (oldstate == SHIFT_N || oldstate == SHIFT_F)
			State2.arrow_alpha = 1;
		break;

	case K22:
		set_smode((oldstate != SHIFT_F)?SDISP_OCT:SDISP_BIN);
		process_cmdline_set_lift();
		break;
	case K23:
		set_smode((oldstate != SHIFT_F)?SDISP_HEX:SDISP_DEC);
		process_cmdline_set_lift();
		break;

	case K04:
		switch (oldstate) {
		case SHIFT_F:	return OP_NIL | OP_P2R;
		case SHIFT_G:	return OP_NIL | OP_R2P;
		default:	break;
		}
		break;

//	case K05:
//		return OP_NIL | OP_2FRAC;

	default:
		break;
	}
	return STATE_UNFINISHED;
}


/* Process a GTO . sequence
 */
static int gtodot_digit(const int n) {
	unsigned int dv = State2.digval;
	const unsigned int val = dv * 10 + n;

	State2.numdigit++;
	if (State2.numdigit == 2) {		// two digits starting large
		if (val > NUMPROG / 10)
			return val;
	}
	if (State2.numdigit == 3)
		return val;
	State2.digval = val;
	return -1;
}

static int gtodot_fkey(int n) {
	const int code = 100 + n;
	unsigned int pc = state_pc();

	if(isXROM(pc))
		pc = 1;
	pc = find_label_from(pc, code, 0);
	if (pc > 0)
		return pc;
	return state_pc();
}

static int process_gtodot(const keycode c) {
	int pc = -1;
	unsigned int rawpc;

	switch (c) {
	case K31:
	case K32:
	case K33:
	case K41:
	case K42:
	case K43:
	case K51:
	case K52:
	case K53:
	case K61:
		pc = gtodot_digit(keycode_to_digit_or_register(c));
		break;

	case K00:
	case K01:
	case K02:
	case K03:
		rawpc = gtodot_fkey(c - K00);
		goto fin;

#ifdef ALLOW_MORE_LABELS
	case K05:	rawpc = gtodot_fkey(4);	goto fin;		// F
	case K10:	case K11:	case K12:			// G H & I
		rawpc = gtodot_fkey(c - K10 + 5);
		goto fin;
	case K21:	case K22:	case K23:			// J K L
		rawpc = gtodot_fkey(c - K21 + 8);
		goto fin;
	case K63:	case K64:					// Y & Z
		rawpc = gtodot_fkey(c - K63 + 14);
		goto fin;
	case K34:	rawpc = gtodot_fkey(11);	goto fin;	// P
	case K44:	rawpc = gtodot_fkey(12);	goto fin;	// T
	case K54:	rawpc = gtodot_fkey(13);	goto fin;	// W
#endif
	case K62:		// .
		if (State2.numdigit == 0) {
			rawpc = 0;
			goto fin;
		}
		break;

	case K20:		// ENTER - short circuit processing
		pc = State2.digval;
		break;

	case K24:		// backspace
		if (State2.numdigit == 0) {
			pc = state_pc();
		} else {
			State2.numdigit--;
			State2.digval /= 10;
		}
	default:
		return STATE_UNFINISHED;
	}
	if (pc >= 0) {
		rawpc = find_user_pc(pc);
fin:		set_pc(rawpc);
		State2.gtodot = 0;
		State2.digval = 0;
		State2.numdigit = 0;
	}
	return STATE_UNFINISHED;
}


/* Process a keystroke in alpha mode
 */
static int process_alpha(const keycode c) {
	const enum shifts oldstate = cur_shift();
	unsigned char ch;
	unsigned int alpha_pos = State2.alpha_pos, n;
	State2.alpha_pos = 0;
        int t;

	set_shift(SHIFT_N);

	switch (c) {
	case K00:
	case K01:
	case K02:
	case K03:
		if (oldstate != SHIFT_F)
			break;
		ch = keycode_to_alpha(c, oldstate);
                t = check_f_key(c - K00, RARG(RARG_ALPHA, ch));
                if (t == RARG(RARG_ALPHA, '\0'))
			return STATE_UNFINISHED;
		return t;

	case K10:	// STO
		if (oldstate == SHIFT_F)
			init_arg(RARG_ASTO);
		else
			break;
		return STATE_UNFINISHED;

	case K11:	// RCL - maybe view
		if (oldstate == SHIFT_F) {
			init_arg(RARG_ARCL);
			return STATE_UNFINISHED;
		} else if (oldstate == SHIFT_H) {
			init_arg(RARG_VIEW_REG);
			return STATE_UNFINISHED;
		}
		break;

	case K20:	// Enter - maybe exit alpha mode
		if (oldstate == SHIFT_G)
			break;
		State2.alphas = 0;
		State2.alphashift = 0;
		return STATE_UNFINISHED;

	case K21:
		if (oldstate == SHIFT_F)
			return OP_NIL | OP_ALPHATOX;
		else if (oldstate == SHIFT_G)
			return OP_NIL | OP_XTOALPHA;
		break;

	case K24:	// Clx - backspace, clear Alpha
		if (oldstate == SHIFT_F)
			return OP_NIL | OP_CLRALPHA;
		if (oldstate == SHIFT_N)
			return STATE_BACKSPACE;
		break;

	case K30:
		if (oldstate == SHIFT_N)
			init_arg(RARG_XEQ);
		else if (oldstate == SHIFT_H)
			init_arg(RARG_GTO);
		else
			break;
		return STATE_UNFINISHED;

	case K40:
		if (oldstate == SHIFT_N) {	// Alpha scroll left
			n = alpha_pos + 1;
			State2.alpha_pos = ( n < ( alen() + 5 ) / 6 ) ? n : alpha_pos;
			return STATE_UNFINISHED;
		}
		break;

	case K44:
		if (oldstate == SHIFT_H) {
			State2.status = 1;
			return STATE_UNFINISHED;
		}
		break;

	case K50:
		if (oldstate == SHIFT_N) {	// Alpha scroll right
			if (alpha_pos > 0)
				State2.alpha_pos = alpha_pos-1;
			return STATE_UNFINISHED;
		}
		break;

	case K60:	// EXIT/ON maybe case switch, otherwise exit alpha
		if (oldstate == SHIFT_F)
			State2.alphashift = 1 - State2.alphashift;
		else if (oldstate == SHIFT_G)
			return OP_NIL | OP_OFF;
		else
			init_state();
		return STATE_UNFINISHED;

	case K63:
		if (oldstate == SHIFT_F)
			return OP_NIL | OP_RS;		// R/S
		break;

	default:
		break;
	}

	/* Look up the character and return an alpha code if okay */
	ch = keycode_to_alpha(c, oldstate);
	if (ch == 0)
		return STATE_UNFINISHED;
	return RARG(RARG_ALPHA, ch & 0xff);
}

/* Code to handle all commands with arguments */
static int arg_eval(unsigned int dv) {
	const unsigned int base = State.base;
	const int r = RARG(base, (State2.ind?RARG_IND:0) + dv);
	init_arg(0);
	State2.rarg = 0;
#ifdef INCLUDE_MULTI_DELETE
	if (base == RARG_DELPROG) {
		del_till_label(dv);
		return STATE_UNFINISHED;
	}
#endif
	return r;
}

static int arg_digit(int n) {
	const unsigned int base = State.base;
	const int mx = State2.ind ? NUMREG : argcmds[base].lim;
	const unsigned int val = State2.digval * 10 + n;

	if (val == TOPREALREG-1 && argcmds[base].cmplx)
		return STATE_UNFINISHED;
	if (State2.numdigit == 0) {
		if (mx < 10) {
			if (n < mx)
				return arg_eval(n);
			return STATE_UNFINISHED;
		}
		if (n * 10 >= mx)
			return arg_eval(n);
	} else {
		if ((int) val >= mx)
			return STATE_UNFINISHED;
	}
	State2.digval = val;
	State2.numdigit++;
	if (State2.numdigit == 2)
		return arg_eval(val);
	return STATE_UNFINISHED;
}

static int arg_fkey(int n) {
	const unsigned int b = State.base;

#ifdef ALLOW_MORE_LABELS
	if (argcmds[b].label || (b >= RARG_SF && b <= RARG_FCF && n < 4))
#else
	if (argcmds[b].label || (b >= RARG_SF && b <= RARG_FCF))
#endif
	{
		if (State2.ind || State2.numdigit > 0)
			return STATE_UNFINISHED;
		if (argcmds[State.base].lim <= 100)
			return STATE_UNFINISHED;
		return arg_eval(n + 100);
	}
	return STATE_UNFINISHED;
}

static int arg_storcl_check(const unsigned int b, const int cmplx) {
	return (b == RARG_STO || b == RARG_RCL || b == RARG_FLRCL ||
			(cmplx && (b == RARG_CSTO || b == RARG_CRCL || b == RARG_FLCRCL)));
}

static int arg_storcl(const unsigned int n, int cmplx) {
	unsigned int b = State.base;

	if (arg_storcl_check(b, cmplx)) {
		State.base += n;
		return 1;
	}
	/* And we can turn off the operation too */
	if (b >= n) {
		b -= n;
		if (arg_storcl_check(b, cmplx)) {
			State.base = b;
			return 1;
		}
	}
	return 0;
}

static int process_arg_dot(const unsigned int base) {
	if (State2.dot || argcmds[base].stckreg || State2.ind)
		return arg_eval(regX_idx);

	if (base == RARG_GTO) {
		// Special GTO . sequence
		if (State2.numdigit == 0 && ! State2.ind) {
			State2.gtodot = 1;
			init_arg(0);
			State2.rarg = 0;
		}
	}
	return STATE_UNFINISHED;
}

static int process_arg(const keycode c) {
	unsigned int base = State.base;
	int n = keycode_to_digit_or_register(c);
#ifndef ALLOW_MORE_LABELS
	enum shifts old_shift = cur_shift();
	int shorthand = 0;
	set_shift(SHIFT_N);
#endif
	if (base >= num_argcmds) {
		init_arg(0);
		State2.rarg = 0;
		return STATE_UNFINISHED;
	}
#ifdef ALLOW_MORE_LABELS
	if ( n >= 0 && n <= 9 ) {
		return arg_digit(n);
	}
	if ( argcmds[base].label && ! State2.ind ) {
		int v;
		switch ( c ) {
		case K_CMPLX:
			v = arg_fkey(4);		// F
			break;
		case K10:
		case K11:
		case K12:
			v = arg_fkey(c - K10 + 5);	// G, H, I
			break;
		case K21:
		case K22:
		case K23:
			v = arg_fkey(c - K21 + 8);	// J, K, L
			break;
		case K34:
			v = arg_fkey(11);		// P
			break;
		case K44:
			v = arg_fkey(12);		// T
			break;
		case K54:
			v = arg_fkey(13);		// W
			break;
		case K63:
		case K64:
			v = arg_fkey(c - K63 + 14);	// Y, Z
			break;
		}
		if ( v != STATE_UNFINISHED )
			return v;
	}
#else
	shorthand = argcmds[base].label && ! State2.ind && ! State2.dot && 
		(old_shift == SHIFT_F || (c > K_ARROW && c != K20 && c != K24 && c != K60 && c != K62));

	if (n >= 0 && n <= 9 && (! shorthand || old_shift != SHIFT_F))
		return arg_digit(n);

	if (shorthand)
		// row column shorthand addressing
		return arg_eval(11 + ( c / 6 ) * 10 + c % 6);
#endif
	/*
	 *  So far, we've got the digits and some special label adressing keys
	 *  Handle the rest here.
	 */
	switch (c) {

	case K_ARROW:		// arrow
		if (!State2.dot && argcmds[base].indirectokay) {
			State2.ind = 1 - State2.ind;
			if (State2.ind == 0 && !argcmds[base].stckreg)
				State2.dot = 0;
		}
		break;

	case K_CMPLX:
#ifdef INCLUDE_USER_MODE
		if (State2.ind || State2.dot)
			break;
		if (base == RARG_STO)
			State.base = RARG_SAVEM;
		else if (base == RARG_RCL)
			State.base = RARG_RESTM;
		break;
#endif

	case K00:	// A
	case K02:	// C
	case K21:	// J
	case K23:	// L (lastX)
		// Real and complex mode allowed
		if (State2.dot || argcmds[base].stckreg || State2.ind)
			return arg_eval(n);
		goto fkey;

	case K01:	// B
	case K03:	// D
	case K12:	// I (lastY)
	case K22:	// K
	case K63:	// Y
		// Only real mode allowed
		if (State2.dot || argcmds[base].stckreg || State2.ind)
			if (!argcmds[base].cmplx)
				return arg_eval(n);
	fkey:
		if ( c <= K03 ) {
			return arg_fkey(c - K00);		// Labels A to D
		}
		break;

	case K62:	// X
		return process_arg_dot(base);

	/* STO and RCL can take an arithmetic argument */
	case K64:		// Z register
		if (State2.dot || ( ! arg_storcl(RARG_STO_PL - RARG_STO, 1) &&
					(argcmds[base].stckreg || State2.ind)))
			return arg_eval(n);
		break;

	case K54:
		if (base == RARG_VIEW || base == RARG_VIEW_REG) {
			init_arg(0);
			State2.rarg = 0;
			return OP_NIL | OP_VIEWALPHA;
		}
		arg_storcl(RARG_STO_MI - RARG_STO, 1);
		break;

	case K44:		// T register
		if (State2.dot || ( ! arg_storcl(RARG_STO_MU - RARG_STO, 1) &&
					(argcmds[base].stckreg || State2.ind)))
			if (!argcmds[base].cmplx)
				return arg_eval(n);
		break;

	case K34:
		arg_storcl(RARG_STO_DV - RARG_STO, 1);
		break;

	case K40:
		arg_storcl(RARG_STO_MAX - RARG_STO, 0);
		break;

	case K50:
		arg_storcl(RARG_STO_MIN - RARG_STO, 0);
		break;

	case K20:				// Enter is a short cut finisher but it also changes a few commands if it is first up
		if (State2.numdigit == 0 && !State2.ind && !State2.dot) {
			if (argcmds[base].label) {
#ifdef INCLUDE_MULTI_DELETE
				if (base == RARG_DELPROG)
					init_arg(DBL_DELPROG);
				else
#endif
					init_arg(base - RARG_LBL);
				State2.multi = 1;
				State2.rarg = 0;
			} else if (base == RARG_SCI) {
				init_arg(0);
				State2.rarg = 0;
				return OP_NIL | OP_FIXSCI;
			} else if (base == RARG_ENG) {
				init_arg(0);
				State2.rarg = 0;
				return OP_NIL | OP_FIXENG;
			} else if (base == RARG_VIEW || base == RARG_VIEW_REG) {
				init_arg(0);
				State2.rarg = 0;
				return OP_NIL | OP_VIEWALPHA;
			} else if (argcmds[base].stckreg)
				State2.dot = 1;
		} else if (State2.numdigit > 0)
			return arg_eval(State2.digval);
		else if (argcmds[base].stckreg || State2.ind)
			State2.dot = 1 - State2.dot;
		break;

	case K60:
	case K24:
		init_arg(0);
		State2.rarg = 0;
		break;

	default:
		break;
	}
	return STATE_UNFINISHED;
}


/* Multi (2) word instruction entry
 */
static int process_multi(const keycode c) {
	const enum shifts oldstate = cur_shift();
	unsigned char ch;
	unsigned int opcode;
	unsigned int base = State.base;

	set_shift(SHIFT_N);

	switch (c) {
	case K20:	// Enter - exit multi mode, maybe return a result
		if (oldstate == SHIFT_F)
			break;
		State2.multi = 0;
		if (State2.numdigit == 0) {
			return STATE_UNFINISHED;
		} else if (State2.numdigit == 1) {
			opcode = OP_DBL + (base << DBL_SHIFT) + State2.digval;
			goto fin;
		} else {
			opcode = OP_DBL + (base << DBL_SHIFT) + State2.digval +
				(State2.digval2 << 16);
			goto fin;
		}

	case K24:	// Clx - backspace, clear alpha
		if (oldstate == SHIFT_N || oldstate == SHIFT_F) {
			if (State2.numdigit == 0)
				State2.multi = 0;
			else
				State2.numdigit--;
			return STATE_UNFINISHED;
		}
		break;

	case K60:	// EXIT/ON maybe case switch, otherwise exit alpha
		if (oldstate == SHIFT_F)
			State2.alphashift = 1 - State2.alphashift;
		else
			init_state();
		return STATE_UNFINISHED;

	default:
		break;
	}

	/* Look up the character and return an alpha code if okay */
	ch = keycode_to_alpha(c, oldstate);
	if (ch == 0)
		return STATE_UNFINISHED;
	if (State2.numdigit == 0) {
		State2.digval = ch;
		State2.numdigit = 1;
		return STATE_UNFINISHED;
	} else if (State2.numdigit == 1) {
		State2.digval2 = ch;
		State2.numdigit = 2;
		return STATE_UNFINISHED;
	}
	State2.multi = 0;

	base = State.base;
	opcode = OP_DBL + (base << DBL_SHIFT) + State2.digval +
			(State2.digval2 << 16) + (ch << 24);
fin:
#ifdef INCLUDE_MULTI_DELETE
	if (base == DBL_DELPROG) {
		del_till_multi_label(opcode);
		return STATE_UNFINISHED;
	}
#endif
	return opcode;
}


static int process_test(const keycode c) {
	int r = State2.test;
	int cmpx = State2.cmplx;

	State2.test = TST_NONE;
	State2.cmplx = 0;
	switch (c) {
	case K_CMPLX:
	case K12:
		if (cmpx)
			break;
		return RARG(RARG_TEST_EQ+r, regI_idx);
	case K23:
		return RARG((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ)+r, regL_idx);
	case K44:
		if (cmpx)
			break;
		return RARG(RARG_TEST_EQ+r, regT_idx);
	case K63:
		if (cmpx)
			break;
		return RARG(RARG_TEST_EQ+r, regY_idx);
	case K62:
		return RARG((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ)+r, regX_idx);
	case K64:
		return RARG((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ)+r, regZ_idx);
	case K00:
		return RARG((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ)+r, regA_idx);
	case K01:
		if (cmpx)
			break;
		return RARG(RARG_TEST_EQ+r, regB_idx);
	case K02:
		return RARG((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ)+r, regC_idx);
	case K03:
		if (cmpx)
			break;
		return RARG(RARG_TEST_EQ+r, regD_idx);

	case K31:	case K32:	case K33:	// 7 8 9
	case K41:	case K42:	case K43:	// 4 5 6
			case K52:	case K53:	//   2 3
		init_arg((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ) + r);
		return process_arg(c);

	case K61:	return OP_SPEC + (cmpx?OP_Zeq0:OP_Xeq0) + r;	// tests vs 0
	case K51:	return OP_SPEC + (cmpx?OP_Zeq1:OP_Xeq1) + r;	// tests vs 1

	case K11:					// tests vs register
	case K20:
		init_arg((cmpx?RARG_TEST_ZEQ:RARG_TEST_EQ) + r);
		return STATE_UNFINISHED;

	case K60:
	case K24:
		return STATE_UNFINISHED;
	default:
		break;
	}
	State2.test = r;
	State2.cmplx = cmpx;
	return STATE_UNFINISHED;
}


/* Return the number of entries in the current catalogue.
 * These are all fixed size known at compile time so a table lookup will
 * likely be the most space efficient method.
 */
int current_catalogue_max(void) {
	// A quick table of catalogue sizes
	// NB: the order here MUST match that in `enum catalogues' 
	static const unsigned char catalogue_sizes[] = 
	{
		0, // NONE
		sizeof(catalogue) / sizeof(const s_opcode),
		sizeof(cplx_catalogue) / sizeof(const s_opcode),
		sizeof(stats_catalogue) / sizeof(const s_opcode),
		sizeof(prob_catalogue) / sizeof(const s_opcode),
		sizeof(int_catalogue) / sizeof(const s_opcode),
		sizeof(prog_catalogue) / sizeof(const s_opcode),
		sizeof(program_xfcn) / sizeof(const s_opcode),
		sizeof(test_catalogue) / sizeof(const s_opcode),
		sizeof(mode_catalogue) / sizeof(const s_opcode),
		sizeof(alpha_catalogue) / sizeof(const s_opcode),
		sizeof(alpha_symbols),
		sizeof(alpha_compares),
		sizeof(alpha_arrows),
		sizeof(alpha_letters_upper),
		sizeof(alpha_letters_lower),
		sizeof(alpha_superscripts),
		sizeof(alpha_subscripts),
		NUM_CONSTS,
		NUM_CONSTS,
		sizeof(conv_catalogue) / sizeof(const s_opcode),
#ifdef INCLUDE_INTERNAL_CATALOGUE
		sizeof(internal_catalogue) / sizeof(const s_opcode),
#endif
	};
	return catalogue_sizes[State2.catalogue];
}


/* Look up the character position in the given byte array and
 * build the alpha op-code for it.
 */
static opcode alpha_code(int n, const char tbl[]) {
	return RARG(RARG_ALPHA, tbl[n] & 0xff);
}


/* Return the opcode for entry n from the current catalogue
 */
opcode current_catalogue(int n) {
	switch (State2.catalogue) {
	default:
		return OP_NIL | OP_NOP;

	case CATALOGUE_CONST:
		return CONST(n);

	case CATALOGUE_COMPLEX_CONST:
		return CONST_CMPLX(n);

	case CATALOGUE_INT:
		return int_catalogue[n];

	case CATALOGUE_ALPHA:
		return alpha_catalogue[n];

	/* Alpha character menus are all similar */
	case CATALOGUE_ALPHA_SYMBOLS:
		return alpha_code(n, alpha_symbols);
	case CATALOGUE_ALPHA_COMPARES:
		return alpha_code(n, alpha_compares);
	case CATALOGUE_ALPHA_ARROWS:
		return alpha_code(n, alpha_arrows);
	case CATALOGUE_ALPHA_LETTERS_UPPER:
		return alpha_code(n, alpha_letters_upper);
	case CATALOGUE_ALPHA_LETTERS_LOWER:
		return alpha_code(n, alpha_letters_lower);
	case CATALOGUE_ALPHA_SUPERSCRIPTS:
		return alpha_code(n, alpha_superscripts);
	case CATALOGUE_ALPHA_SUBSCRIPTS:
		return alpha_code(n, alpha_subscripts);

	case CATALOGUE_COMPLEX:
		return cplx_catalogue[n];

	case CATALOGUE_STATS:
		return stats_catalogue[n];

	case CATALOGUE_PROB:
		return prob_catalogue[n];

	case CATALOGUE_PROG:
		return prog_catalogue[n];

	case CATALOGUE_PROGXFCN:
		return program_xfcn[n];

	case CATALOGUE_MODE:
		return mode_catalogue[n];

#ifdef INCLUDE_INTERNAL_CATALOGUE
	case CATALOGUE_INTERNAL:
		return internal_catalogue[n];
#endif

	case CATALOGUE_TEST:
		return test_catalogue[n];

	case CATALOGUE_NORMAL:
		return catalogue[n];

	case CATALOGUE_CONV:
		return conv_catalogue[n];
	}
}


static int process_catalogue(const keycode c) {
	unsigned int dv = State2.digval;
	unsigned char ch;
	const int ctmax = current_catalogue_max();
	const enum shifts s = cur_shift();

	if (s == SHIFT_N) {
		switch (c) {
		case K30:			// XEQ accepts command
		case K20:			// Enter accepts command
			if ((int) dv < ctmax) {
				const opcode op = current_catalogue(dv);

				init_cat(CATALOGUE_NONE);

				if (isRARG(op)) {
					const unsigned int rarg = RARG_CMD(op);

					if (rarg == RARG_CONST || rarg == RARG_CONST_CMPLX || rarg == RARG_CONV || rarg == RARG_ALPHA)
						return op;
					if (rarg >= RARG_TEST_EQ && rarg <= RARG_TEST_GE) {
						State2.test = TST_EQ + (rarg - RARG_TEST_EQ);
						return STATE_UNFINISHED;
					}
					init_arg(rarg);
				} else {
					if (op == (OP_NIL | OP_CLALL))
						init_confirm(confirm_clall);
					else if (op == (OP_NIL | OP_RESET))
						init_confirm(confirm_reset);
					else
						return op;
				}
			} else
				init_cat(CATALOGUE_NONE);
			return STATE_UNFINISHED;

		case K24:			// backspace
			if (CmdLineLength > 0 && Keyticks < 30) {
				if (--CmdLineLength > 0)
					goto search;
				State2.digval = 0;
			} else
				init_cat(CATALOGUE_NONE);
			return STATE_UNFINISHED;

		case K60:
			init_cat(CATALOGUE_NONE);
			return STATE_UNFINISHED;

		case K40:
			if (State2.digval > 0)
				State2.digval--;
			else
				State2.digval = ctmax-1;
			CmdLineLength = 0;
			return STATE_UNFINISHED;

		case K50:
			if ((int) ++State2.digval >= ctmax)
				State2.digval = 0;
			CmdLineLength = 0;
			return STATE_UNFINISHED;

		default:
			break;
		}
	} else if (s == SHIFT_F && c == K01 && State2.catalogue == CATALOGUE_CONV) {
		/* A small table of commands in pairs containing inverse commands.
		 * This table could be unsigned characters only storing the monadic kind.
		 * this saves twelve bytes in the table at a cost of some bytes in the code below.
		 * Not worth it since the maximum saving will be less than twelve bytes.
		 */
		static const unsigned short int conv_mapping[] = {
			OP_MON | OP_AR_DB,	OP_MON | OP_DB_AR,
			OP_MON | OP_DB_PR,	OP_MON | OP_PR_DB,
			OP_MON | OP_DEGC_F,	OP_MON | OP_DEGF_C,
			OP_MON | OP_DEG2RAD,	OP_MON | OP_RAD2DEG,
			OP_MON | OP_DEG2GRD,	OP_MON | OP_GRD2DEG,
			OP_MON | OP_RAD2GRD,	OP_MON | OP_GRD2RAD,
		};
		const opcode op = current_catalogue(dv);
		int i;

		init_cat(CATALOGUE_NONE);
		if (isRARG(op))
			return op ^ 1;
		for (i=0; i<sizeof(conv_mapping) / sizeof(conv_mapping[0]); i++)
			if (op == conv_mapping[i])
				return conv_mapping[i^1];
		return STATE_UNFINISHED;		// Unreached
	}
	else if ( c == K60 && State2.alphas ) {
		// Handle alpha shift in alpha mode
		return process_alpha(c);
	}

	/* We've got a key press, map it to a character and try to
	 * jump to the appropriate catalogue entry.
	 */
	ch = remap_chars(keycode_to_alpha(c, s));
	set_shift(SHIFT_N);
	if (ch == '\0')
		return STATE_UNFINISHED;
	if (Keyticks >= 30 || (State2.alphas && State2.catalogue > CATALOGUE_ALPHA))
		CmdLineLength = 0;	// keyboard search timed out or single letter cat
	if (CmdLineLength < 10)
		Cmdline[CmdLineLength++] = ch;
	/* Search for the current buffer in the catalogue */

search:
	Cmdline[CmdLineLength] = '\0';
	for (dv = 0; dv < (unsigned int)ctmax; dv++) {
		char buf[16];
		const char *cmd = catcmd(current_catalogue(dv), buf);
		int i;

		if (*cmd == COMPLEX_PREFIX)
			cmd++;
		for (i=0; cmd[i] != '\0'; i++) {
			const unsigned char c = remap_chars(cmd[i]);
			const unsigned char cl = 0xff&Cmdline[i];
			if (c > cl) {
				State2.digval = dv;
				return STATE_UNFINISHED;
			} else if (c < cl)
				break;
		}
		if (Cmdline[i] == '\0') {
			State2.digval = dv;
			return STATE_UNFINISHED;
		}
	}
	State2.digval = ctmax-1;
	return STATE_UNFINISHED;
}


static int process_confirm(const keycode c) {
	switch (c) {
	case K63:			// Yes
		switch (State2.confirm) {
		case confirm_clall:	clrall(NULL, NULL, OP_CLALL);	break;
		case confirm_reset:	reset(NULL, NULL, OP_RESET);	break;
		case confirm_clprog:	clrprog();
		}
		State2.confirm = confirm_none;
		State2.digval = 0;
		break;

	//case K60:
	case K24:
	case K32:			// No
		State2.confirm = 0;
		State2.digval = 0;
		break;
	default:			// No state change
		break;
	}
	return STATE_UNFINISHED;
}

static int process_status(const keycode c) {
	int n = State2.status - 1;

	if ( c == K40 ) {
		if (--n < 0)
			n = 9;
	}
	else if ( c == K50 ) {
		if (++n > 9)
			n = 0;
	}
	else
		n = keycode_to_digit_or_register(c);

	State2.status = n + 1;
	return STATE_UNFINISHED;
}

static int is_label_at(unsigned int pc) {
	const unsigned int op = getprog(pc);

	return (isDBL(op) && opDBL(op) == DBL_LBL);
}

static unsigned int advance_to_next_code_segment(int n) {
	for (;;) {
		unsigned int pc;

		if (++n > NUMBER_OF_FLASH_REGIONS)
			return addrXROM(0);
		pc = addrLIB(0, n);
		if ( is_prog_region( n ) )
			return pc;
	}
}

static void advance_to_next_label(unsigned int pc) {
	for (;;) {
		for (;;) {
			pc = inc(pc);
			if (PcWrapped)
				break;
			if (is_label_at(pc)) {
				State2.digval = pc;
				return;
			}
		}
		if (isXROM(pc))
			pc = LastProg > 1 ? 1 : advance_to_next_code_segment(0);
		else
			pc = advance_to_next_code_segment(nLIB(pc));
		if (is_label_at(pc)) {
			State2.digval = pc;
			return;
		}
	}
}

static unsigned int advance_to_previous_code_segment(int n) {
	for (;;) {
		unsigned int pc;
		if (--n == 0)
			return LastProg > 1 ? 0 : addrXROM(0);
		pc = addrLIB(0, n);
		if ( is_prog_region( n ) )
			return pc;
	}
}

static void advance_to_previous_label(unsigned int pc) {
	for (;;) {
		for (;;) {
			pc = dec(pc);
			if (PcWrapped)
				break;
			if (is_label_at(pc)) {
				State2.digval = pc;
				return;
			}
		}
		if (isRAM(pc))
			pc = addrXROM(0);
		else if (isXROM(pc))
			pc = advance_to_previous_code_segment(NUMBER_OF_FLASH_REGIONS);
		else
			pc = advance_to_previous_code_segment(nLIB(pc));
		pc = dec(pc);
		if (is_label_at(pc)) {
			State2.digval = pc;
			return;
		}
	}
}

static int process_labellist(const keycode c) {
	unsigned int pc = State2.digval;

	switch (c) {
	case K62:				// Jump to XROM
		advance_to_next_label(addrXROM(0));
		return STATE_UNFINISHED;

	// Digits take you to that segment
	case K31:
	case K32:
	case K33:
	case K41:
	case K42:
	case K43:
	case K51:
	case K52:
	case K53:
	case K61:
		pc = advance_to_next_code_segment(keycode_to_digit_or_register(c));
		if (! is_label_at(pc))
			advance_to_next_label(pc);
		else
			State2.digval = pc;
		return STATE_UNFINISHED;

	case K50:			// Find next label
		advance_to_next_label(pc);
		return STATE_UNFINISHED;

	case K40:			// Find previous label
		advance_to_previous_label(pc);
		return STATE_UNFINISHED;

	case K20:			// GTO
		State2.digval = 0;
		State2.labellist = 0;
		return (getprog(pc) & 0xfffff0ff) + (DBL_GTO << DBL_SHIFT);

	case K30:			// XEQ
	case K63:			// R/S
		State2.digval = 0;
		State2.labellist = 0;
		return (getprog(pc) & 0xfffff0ff) + (DBL_XEQ << DBL_SHIFT);

	case K24:			// Exit doing nothing
	case K60:
		break;
	default:
		return STATE_UNFINISHED;
	}
	State2.digval = 0;
	State2.labellist = 0;
	return STATE_UNFINISHED;
}


static int process_registerlist(const keycode c) {
	int n = keycode_to_digit_or_register(c);

	if ( n >= 0 && n <= 9 ) {
		if (State2.digval > NUMREG-1)
			State2.digval = 0;
		State2.digval = (State2.digval * 10 + n) % 100;
		return STATE_UNFINISHED;
	}

	if ( n > NUMREG - 1 ) {
		State2.digval = n;
		return STATE_UNFINISHED;
	}

	switch (c) {
	case K40:
		if (State2.digval > 0)
			State2.digval--;
		else
			State2.digval = NUMREG-1;
		return STATE_UNFINISHED;

	case K50:
		if (State2.digval < NUMREG-1)
			State2.digval++;
		else	State2.digval = 0;
		return STATE_UNFINISHED;

	case K04:
		State2.digval2 = !State2.digval2;
		return STATE_UNFINISHED;

	case K11:
	case K20:
		n = RARG(State2.digval2?RARG_FLRCL:RARG_RCL, State2.digval);
		State2.registerlist = 0;
		State2.digval = 0;
		State2.digval2 = 0;
		return n;

	case K24:			// Exit doing nothing
	case K60:
		break;
	default:
		return STATE_UNFINISHED;
	}
	State2.registerlist = 0;
	State2.digval = 0;
	State2.digval2 = 0;
	return STATE_UNFINISHED;
}

static int process(const int c) {
	const enum shifts s = cur_shift();
	enum catalogues cat;

	if (c == K_HEARTBEAT) {
		/*
		 *  Heartbeat processing goes here.
		 *  This is totally thread safe!
		 */

		/*
		 *  Toggle the RPN annunciator as a visual feedback
		 */
		dot(RPN, !BusyBlink);
		BusyBlink = 0;
		finish_display();

		/*
		 *  Serve the watchdog
		 */
		watchdog();

#if defined(REALBUILD) || defined(WINGUI)
		/*
		 *  If buffer is empty re-allow R/S to start a program
		 */
		if ( JustStopped && !is_key_pressed() ) {
			JustStopped = 0;
		}
#endif

		/*
		 *  Do nothing if not running a program
		 */
		if (!Running && !Pause)
			return STATE_IGNORE;
	}

	if (Running || Pause) {
		/*
		 *  Abort a running program with R/S or EXIT
		 */
		if (c == K60 || c == K63) {
			if (Pause && isXROM(state_pc()))
				set_pc(0);
			set_running_off();
			Pause = 0;
			DispMsg = "Stopped";
			return STATE_UNFINISHED;
		}
		if ( c != K_HEARTBEAT ) {
			LastKey = (char) (c + 1);	// Store for KEY?
			Pause = 0;			// leave PSE statement
		}
		// continue execution if really running, else ignore (PSE)
		return STATE_RUNNING;
	}

	/*
	 * Turn off the RPN annunciator as a visual feedback
	 */
	BusyBlink = 1;
	dot(RPN, 0);
	finish_display();

	/* Check for ON in the unshifted state -- this is a reset sequence
	 * common across all modes.  Shifted modes need to check this themselves
	 * if required.
	 */
	if (c == K60 && s == SHIFT_N && ! State2.catalogue) {
		soft_init_state();
		return STATE_UNFINISHED;
	}
#if defined(REALBUILD) || defined(WINGUI)
	if ( c == K63 && JustStopped ) {
		// Avoid an accidental restart with R/S
		JustStopped = 0;
		return STATE_IGNORE;
	}
#endif

	if (State2.confirm)
		return process_confirm((const keycode)c);

	if (State2.gtodot)
		return process_gtodot((const keycode)c);

	if (State2.hyp)
		return process_hyp((const keycode)c);

	if (State2.test != TST_NONE)
		return process_test((const keycode)c);

	if (State2.arrow)
		return process_arrow((const keycode)c);

	// Process shift keys directly
	if (c == K_F) {
		set_shift((s == SHIFT_F)?SHIFT_N:SHIFT_F);
		return STATE_UNFINISHED;
	}
	if (c == K_G) {
		set_shift((s == SHIFT_G)?SHIFT_N:SHIFT_G);
		return STATE_UNFINISHED;
	}
	if (c == K_H) {
		set_shift((s == SHIFT_H)?SHIFT_N:SHIFT_H);
		return STATE_UNFINISHED;
	}

	if (State2.multi)
		return process_multi((const keycode)c);

	if (State2.rarg)
		return process_arg((const keycode)c);

	// Here the keys are mapped to catalogues
	// The position of this code decides where catalog switching
	// works and were not
	cat = keycode_to_cat((keycode)c, s);
	if ( cat != CATALOGUE_NONE ) {
		init_cat( CATALOGUE_NONE );
		init_cat( cat );
		return STATE_UNFINISHED;
	}

	if (State2.status)
		return process_status((const keycode)c);

	if (State2.labellist)
		return process_labellist((const keycode)c);

	if (State2.registerlist)
		return process_registerlist((const keycode)c);

	if (State2.catalogue)
		return process_catalogue((const keycode)c);

	if (State2.alphas)
		return process_alpha((const keycode)c);

	if (State2.cmplx) {
		if (s == SHIFT_F)
			return process_f_shifted_cmplx((const keycode)c);
		if (s == SHIFT_G)
			return process_g_shifted_cmplx((const keycode)c);
		if (s == SHIFT_H)
			return process_h_shifted_cmplx((const keycode)c);
		return process_normal_cmplx((const keycode)c);
	} else {
		if (s == SHIFT_F)
			return process_f_shifted((const keycode)c);
		if (s == SHIFT_G)
			return process_g_shifted((const keycode)c);
		if (s == SHIFT_H)
			return process_h_shifted((const keycode)c);
		return process_normal((const keycode)c);
	}
}

void process_keycode(int c) {
	char tracebuf[25];

	xeq_init_contexts();
	c = process(c);
	switch (c) {
	case STATE_SST:
		xeq_sst(tracebuf);
		break;

	case STATE_BST:
		xeq_bst(tracebuf);
		break;

	case STATE_BACKSPACE:
		if (! State2.runmode)
			delprog();
		else if (State2.alphas) {
			char *p = find_char(Alpha, '\0');
			if (p > Alpha)
				*--p = '\0';
		}
		break;

	case STATE_RUNNING:
		xeqprog();  // continue execution
		break;

	case STATE_UNFINISHED:
	case STATE_IGNORE:
		break;

	default:
		if (State2.runmode) {
			xeq(c);
			xeqprog();
		} else
			stoprog(c);
	}
#if defined(REALBUILD) || defined(WINGUI)
	if (!Running && !Pause && !JustStopped && c != STATE_IGNORE) {
		display();
	}
#else
	if (!Running && !Pause && c != STATE_IGNORE && ! just_displayed) {
		display();
	}
        just_displayed = 0;
#endif
}



