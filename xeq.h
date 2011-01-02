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

#ifndef __XEQ_H__
#define __XEQ_H__

/* Version number */
#define VERSION_STRING	"1.12"


/* Define the length of our extended precision numbers.
 * This must be greater than the length of the compressed reals we store
 * on the stack and in registers (currently 16 digits).
 *
 * To account for the nasty cancellations you get doing complex arithmetic
 * we really want this to be a bit more than double the standard precision.
 * i.e. >32.  There is a nice band from 34 - 39 digits which occupy 36 bytes.
 * 33 digits is the most that occupy 32 bytes but that doesn't leave enough
 * guard digits for my liking.
 */
//#define DECNUMDIGITS 24
#define DECNUMDIGITS 39		/* 32 bytes per real for 28 .. 33, 36 bytes for 34 .. 39 */

#include "decNumber/decNumber.h"
#include "decNumber/decContext.h"
#include "decNumber/decimal64.h"

enum rarg;
enum multiops;

/* Define some system flag to user flag mappings
 */
#define CARRY_FLAG	101	/* C = carry */
#define OVERFLOW_FLAG	100	/* B = excess/exceed */
#define NAN_FLAG	102	/* D = danger */


/* Define to include Reimann's Zeta function.
 * This adds circa 2kb to a thumb image and 3kb to a x86 image
 */
//#define INCLUDE_ZETA


#define NAME_LEN	6	/* Length of command names */

typedef unsigned int opcode;
typedef unsigned short int s_opcode;

/* Table of monadic functions */
struct monfunc {
#ifdef DEBUG
	unsigned short n;
#endif
	decNumber *(*mondreal)(decNumber *, const decNumber *, decContext *);
	void (*mondcmplx)(decNumber *, decNumber *, const decNumber *, const decNumber *, decContext *);
	long long int (*monint)(long long int);
	const char fname[NAME_LEN];
};
extern const struct monfunc monfuncs[];
extern const unsigned short num_monfuncs;

/* Table of dyadic functions */
struct dyfunc {
#ifdef DEBUG
	unsigned short n;
#endif
	decNumber *(*dydreal)(decNumber *, const decNumber *, const decNumber *, decContext *);
	void (*dydcmplx)(decNumber *, decNumber *, const decNumber *, const decNumber*,
				const decNumber *, const decNumber *, decContext *);
	long long int (*dydint)(long long int, long long int);
	const char fname[NAME_LEN];
};
extern const struct dyfunc dyfuncs[];
extern const unsigned short num_dyfuncs;

/* Table of triadic functions */
struct trifunc {
#ifdef DEBUG
	unsigned short n;
#endif
	decNumber *(*trireal)(decNumber *, const decNumber *, const decNumber *, const decNumber *, decContext *);
	long long int (*triint)(long long int, long long int, long long int);
	const char fname[NAME_LEN];
};
extern const struct trifunc trifuncs[];
extern const unsigned short num_trifuncs;


/* Table of niladic functions */
struct niladic {
#ifdef DEBUG
	unsigned short n;
#endif
	void (*niladicf)(decimal64 *, decimal64 *, decContext *);
	unsigned int numresults : 2;
	const char nname[NAME_LEN];
};
extern const struct niladic niladics[];
extern const unsigned short num_niladics;


/* Table of argument taking commands */
struct argcmd {
#ifdef DEBUG
	unsigned short n;
#endif
	void (*f)(unsigned int, enum rarg);
	unsigned char lim;
	unsigned int indirectokay:1;
	unsigned int notzero:1;
	unsigned int stckreg:1;
	unsigned int cmplx:1;
	const char cmd[NAME_LEN];
};
extern const struct argcmd argcmds[];
extern const unsigned short num_argcmds;

struct multicmd {
#ifdef DEBUG
	unsigned short n;
#endif
	void (*f)(opcode, enum multiops);
	const char cmd[NAME_LEN];
};
extern const struct multicmd multicmds[];
extern const unsigned short num_multicmds;



extern const char *disp_msg;

extern decContext *g_ctx, *g_ctx64;


/* Return the specified opcode in the position indicated in the current
 * catalogue.
 */
extern opcode current_catalogue(int);
extern int current_catalogue_max(void);


/* Allow the number of registers and the size of the stack to be changed
 * relatively easily.
 */
#define STACK_SIZE	8	/* Maximum depth of RPN stack */
#define EXTRA_REG	4
#define RET_STACK_SIZE	8	/* Depth of return stack */
#define NUMPROG		476	/* Number of program steps */
#define NUMLBL		103	/* Number of program labels */
#define NUMFLG		103	/* Number of flags */

#define NUMALPHA	31	/* Number of characters in alpha */
extern char alpha[NUMALPHA+1];


/* Macros to access program ROM */
#define XROM_MASK	(0x4000)
#define isXROM(pc)	((pc) & XROM_MASK)
#define addrXROM(pc)	((pc) | XROM_MASK)


/* Stack lives in the register set */
#define NUMREG		(TOPREALREG+STACK_SIZE+EXTRA_REG)/* Number of registers */
#define TOPREALREG	(100)				/* Non-stack last register */

extern decimal64 regs[NUMREG];

#define REGNAMES	"XYZTABCDLIJK"

#define regX_idx	(TOPREALREG)
#define regY_idx	(TOPREALREG+1)
#define regZ_idx	(TOPREALREG+2)
#define regT_idx	(TOPREALREG+3)
#define regA_idx	(TOPREALREG+4)
#define regB_idx	(TOPREALREG+5)
#define regC_idx	(TOPREALREG+6)
#define regD_idx	(TOPREALREG+7)
#define regL_idx	(TOPREALREG+8)
#define regI_idx	(TOPREALREG+9)
#define regJ_idx	(TOPREALREG+10)
#define regK_idx	(TOPREALREG+11)

#define regX	(regs[regX_idx])
#define regY	(regs[regY_idx])
#define regZ	(regs[regZ_idx])
#define regT	(regs[regT_idx])
#define regA	(regs[regA_idx])
#define regB	(regs[regB_idx])
#define regC	(regs[regC_idx])
#define regD	(regs[regD_idx])
#define regL	(regs[regL_idx])
#define regI	(regs[regI_idx])
#define regJ	(regs[regJ_idx])
#define regK	(regs[regK_idx])


/* Define the operation codes and various masks to simplify access to them all
 */
enum eKind {
	KIND_SPEC=0,
	KIND_NIL,
	KIND_MON,
	KIND_DYA,
	KIND_TRI,
	KIND_CMON,
	KIND_CDYA,
};

#define KIND_SHIFT	8
#define DBL_SHIFT	8

#define OP_SPEC	(KIND_SPEC << KIND_SHIFT)		/* Special operations */
#define OP_NIL	(KIND_NIL << KIND_SHIFT)		/* Niladic operations */
#define OP_MON	(KIND_MON << KIND_SHIFT)		/* Monadic operations */
#define OP_DYA	(KIND_DYA << KIND_SHIFT)		/* Dyadic operations */
#define OP_TRI	(KIND_TRI << KIND_SHIFT)		/* Dyadic operations */
#define OP_CMON	(KIND_CMON << KIND_SHIFT)		/* Complex Monadic operation */
#define OP_CDYA (KIND_CDYA << KIND_SHIFT)		/* Complex Dyadic operaion */

#define OP_DBL	0x1000			/* Double sized instructions */

#define OP_RARG	0x8000			/* All operations that have a register like argument */
#define RARG_OPSHFT	8
#define RARG_MASK	0x7f
#define RARG_IND	0x80

#define isRARG(op)	((op) & OP_RARG)

#define opKIND(op)	((enum eKind)((op) >> KIND_SHIFT))
#define argKIND(op)	((op) & ((1 << KIND_SHIFT)-1))
#define isDBL(op)	(((op) & 0xf000) == OP_DBL)
#define opDBL(op)	(((op) >> DBL_SHIFT) & 0xf)

enum tst_op {
	TST_EQ=0,	TST_NE=1,
	TST_LT=2,	TST_LE=3,
	TST_GT=4,	TST_GE=5
};
#define TST_NONE	7



// Monadic functions
enum {
	OP_FRAC = 0, OP_FLOOR, OP_CEIL, OP_ROUND, OP_TRUNC,
	OP_ABS, OP_RND, OP_SIGN,

	OP_LN, OP_EXP, OP_SQRT, OP_RECIP,
	OP_LOG, OP_LG2, OP_2POWX, OP_10POWX,
	OP_LN1P, OP_EXPM1,
	OP_LAMW, OP_INVW,
	OP_SQR,
#ifdef INCLUDE_CUBES
	OP_CUBE, OP_CUBERT,
#endif

	OP_FIB,

	OP_2DEG, OP_2RAD, OP_2GRAD,

	OP_SIN, OP_COS, OP_TAN,
	OP_ASIN, OP_ACOS, OP_ATAN,
	OP_SINC,

	OP_SINH, OP_COSH, OP_TANH,
	OP_ASINH, OP_ACOSH, OP_ATANH,

	OP_FACT, OP_GAMMA, OP_LNGAMMA,
#ifdef INCLUDE_DIGAMMA
	OP_PSI,
#endif
#ifdef INCLUDE_DBLFACT
	OP_DBLFACT,
#endif
#ifdef INCLUDE_SUBFACT
	OP_SUBFACT,
#endif
	OP_DEG2RAD, OP_RAD2DEG,
	OP_CCHS, OP_CCONJ,		// CHS and Conjugate
	OP_ERF,
	OP_cdf_Q, OP_qf_Q,
	OP_cdf_chi2, OP_qf_chi2,
	OP_cdf_T, OP_qf_T,
	OP_cdf_F, OP_qf_F,
	OP_cdf_WB, OP_qf_WB,
	OP_cdf_EXP, OP_qf_EXP,
	OP_cdf_B, OP_qf_B,
	OP_cdf_P, OP_qf_P,
	OP_cdf_G, OP_qf_G,
	OP_cdf_N, OP_qf_N,
	OP_xhat, OP_yhat,
	OP_sigper,
	OP_PERCNT, OP_PERCHG, OP_PERTOT,// % operations -- really dyadic but leave the Y register unchanged
	OP_HMS2, OP_2HMS,

	//OP_SEC, OP_COSEC, OP_COT,
	//OP_SECH, OP_COSECH, OP_COTH,

	OP_NOT,
	OP_BITCNT, OP_MIRROR,
	OP_DOWK, OP_D2J, OP_J2D,

	OP_DEGC_F, OP_DEGF_C,
#ifdef INCLUDE_ZETA
	OP_ZETA,
#endif
#ifdef INCLUDE_EASTER
	OP_EASTER,
#endif
	OP_stpsolve
};
	
// Dyadic functions
enum {
	OP_POW = 0,
	OP_ADD, OP_SUB, OP_MUL, OP_DIV,
	OP_MOD,
	OP_LOGXY,
	OP_MIN, OP_MAX,
	OP_ATAN2,
	OP_BETA, OP_LNBETA,
	OP_GAMMAP,
#ifdef INCLUDE_ELLIPTIC
	OP_SN, OP_CN, OP_DN,
#endif
#ifdef INCLUDE_BESSEL
	OP_BSJN, OP_BSIN, OP_BSYN, OP_BSKN,
#endif
	OP_COMB, OP_PERM,
	OP_PERAD, OP_PERSB, OP_PERMG, OP_MARGIN,
	OP_PARAL,
#ifdef INCLUDE_AGM
	OP_AGM,
#endif
	OP_HMSADD, OP_HMSSUB,
	OP_GCD, OP_LCM,
	//OP_HYPOT,
	OP_LAND, OP_LOR, OP_LXOR, OP_LNAND, OP_LNOR, OP_LXNOR,
	OP_DTADD, OP_DTDIF,
};

// Triadic functions
enum {
	OP_BETAI=0,
	OP_DBL_DIV, OP_DBL_MOD,
#ifdef INCLUDE_MULADD
	OP_MULADD,
#endif
	OP_PERMRR,
};	

// Niladic functions
enum {
	OP_NOP=0, OP_VERSION, OP_STKSIZE, OP_STK4, OP_STK8, OP_INTSIZE,
	OP_LASTX, OP_LASTXY,
	OP_SWAP, OP_CSWAP, OP_RDOWN, OP_RUP, OP_CRDOWN, OP_CRUP,
	OP_CENTER, OP_FILL, OP_CFILL, OP_DROP, OP_DROPY, OP_DROPXY,
	OP_sigmaX, OP_sigmaY, OP_sigmaX2, OP_sigmaY2, OP_sigma_XY,
	OP_sigmaN,
	OP_sigmalnX, OP_sigmalnXlnX, OP_sigmalnY, OP_sigmalnYlnY,
		OP_sigmalnXlnY, OP_sigmaXlnY, OP_sigmaYlnX,
	OP_statS, OP_statSigma, OP_statMEAN, OP_statWMEAN,
		OP_statR, OP_statLR, OP_statSErr,
	OP_EXPF, OP_LINF, OP_LOGF, OP_PWRF, OP_BEST,
	OP_RANDOM, OP_STORANDOM,
	OP_DEG, OP_RAD, OP_GRAD,
	OP_ALL, OP_RTN, OP_RTNp1,
	OP_RS, OP_PROMPT,
	OP_SIGMACLEAR, OP_CLREG, OP_CLSTK, OP_CLALL, OP_RESET, OP_CLFLAGS,
	OP_R2P, OP_P2R,
	OP_FRACDENOM, OP_2FRAC, OP_DENFIX, OP_DENFAC, OP_DENANY,
	OP_FRACIMPROPER, OP_FRACPROPER,
	OP_RADDOT, OP_RADCOM, OP_THOUS_ON, OP_THOUS_OFF,
	OP_PAUSE,
	OP_2COMP, OP_1COMP, OP_UNSIGNED, OP_SIGNMANT,
	OP_FLOAT, OP_HMS, OP_FRACT,
	OP_LJ, OP_RJ,
	OP_DBL_MUL,
	OP_RCLSIGMA,
	OP_DATEYMD, OP_DATEDMY, OP_DATEMDY, OP_ISLEAP,
	OP_ALPHADAY, OP_ALPHAMONTH, OP_ALPHADATE, OP_ALPHATIME,
	OP_DATE, OP_TIME, OP_24HR, OP_12HR,
	OP_SETDATE, OP_SETTIME,
	OP_CLRALPHA, OP_VIEWALPHA, OP_ALPHALEN,
	OP_ALPHATOX, OP_XTOALPHA, OP_ALPHAON, OP_ALPHAOFF,
	OP_REGCOPY, OP_REGSWAP, OP_REGCLR, OP_REGSORT,
	OP_RCLFLAG, OP_STOFLAG,
	OP_GSBuser,
	OP_XisInf, OP_XisNaN, OP_XisSpecial, OP_XisPRIME,
	OP_inisolve,
#ifdef INCLUDE_MODULAR
	OP_MPLUS, OP_MMINUS, OP_MMULTIPLY, OP_MSQ,
#endif
};


/* Command that can take an argument */
enum rarg {
	RARG_CONST,		// user visible constants
	RARG_CONST_CMPLX,
	RARG_CONST_INT,		// like the above but for intenal constants
	RARG_ERROR,
	/* STO and RCL must be in operator order */
	RARG_STO, RARG_STO_PL, RARG_STO_MI, RARG_STO_MU, RARG_STO_DV,
			RARG_STO_MIN, RARG_STO_MAX,
	RARG_RCL, RARG_RCL_PL, RARG_RCL_MI, RARG_RCL_MU, RARG_RCL_DV,
			RARG_RCL_MIN, RARG_RCL_MAX,
	RARG_SWAP,
	RARG_CSTO, RARG_CSTO_PL, RARG_CSTO_MI, RARG_CSTO_MU, RARG_CSTO_DV,
	RARG_CRCL, RARG_CRCL_PL, RARG_CRCL_MI, RARG_CRCL_MU, RARG_CRCL_DV,
	RARG_CSWAP,
	RARG_VIEW,
	RARG_STOSTK, RARG_RCLSTK,

	RARG_ALPHA, RARG_AREG, RARG_ASTO, RARG_ARCL,
	RARG_AIP, RARG_ALRL, RARG_ALRR, RARG_ALSL, RARG_ALSR,

	RARG_TEST_EQ, RARG_TEST_NE, RARG_TEST_LT,	/* Must be in the same order as enuim test_op */
			RARG_TEST_LE, RARG_TEST_GT, RARG_TEST_GE,
	RARG_TEST_ZEQ, RARG_TEST_ZNE,
	RARG_DSE, RARG_ISG,
	RARG_DSZ, RARG_ISZ,

	/* These 5 must be sequential and in the same order as the DBL_ commands */
	RARG_LBL, RARG_XEQ, RARG_GTO,
	RARG_SUM, RARG_PROD, RARG_SOLVE, RARG_INTG,

	RARG_FIX, RARG_SCI, RARG_ENG, RARG_DISP,

	RARG_SF, RARG_CF, RARG_FF, RARG_FS, RARG_FC,
	RARG_FSC, RARG_FSS, RARG_FSF, RARG_FCC, RARG_FCS, RARG_FCF,

	RARG_WS,

	RARG_RL, RARG_RR, RARG_RLC, RARG_RRC,
	RARG_SL, RARG_SR, RARG_ASR,
	RARG_SB, RARG_CB, RARG_FB, RARG_BS, RARG_BC,
	RARG_MASKL, RARG_MASKR,

	RARG_BASE,

	RARG_CONV,
#ifdef REALBUILD
	RARG_CONTRAST
#endif
};
#define RARG(op, n)	(OP_RARG | ((op) << RARG_OPSHFT) | (n))


// Special functions
enum specials {
	OP_ENTER=0,
	OP_CLX, OP_EEX, OP_CHS, OP_DOT,
	OP_0, OP_1, OP_2, OP_3, OP_4, OP_5, OP_6, OP_7, OP_8, OP_9,
			OP_A, OP_B, OP_C, OP_D, OP_E, OP_F,
	OP_SIGMAPLUS, OP_SIGMAMINUS,
	OP_Xeq0, OP_Xne0, OP_Xlt0, OP_Xgt0, OP_Xle0, OP_Xge0,
	OP_Xeq1, OP_Xne1, OP_Xlt1, OP_Xgt1, OP_Xle1, OP_Xge1,
	OP_Zeq0, OP_Zne0,
	OP_Zeq1, OP_Zne1,
	SPECIAL_MAX
};

// Double sized instructions
enum multiops {
	DBL_LBL=0, DBL_XEQ, DBL_GTO,
	DBL_SUM, DBL_PROD, DBL_SOLVE, DBL_INTG,
#ifdef MULTI_ALPHA
	DBL_ALPHA,
#endif
	//DBL_NUMBER,
};



// Error codes
enum errors {
	ERR_NONE = 0,
	ERR_DOMAIN,	ERR_BAD_DATE,	ERR_PROG_BAD,
	ERR_INFINITY,	ERR_MINFINITY,	ERR_NO_LBL,
	ERR_XROM_NEST,	ERR_RANGE,	ERR_DIGIT,
	ERR_TOO_LONG,	ERR_XEQ_NEST,	ERR_STK_CLASH,
	ERR_BAD_MODE,	ERR_INT_SIZE,
#ifndef REALBUILD
	ERR_UNSETTABLE,
#endif
	MAX_ERROR
};


// Integer modes
enum arithmetic_modes {
	MODE_2COMP=0,	MODE_1COMP,	MODE_UNSIGNED,	MODE_SGNMANT
};

enum integer_bases {
	MODE_DEC=0,	MODE_BIN,	MODE_OCT,	MODE_HEX
};

// Display modes
enum display_modes {
	MODE_STD=0,	MODE_FIX,	MODE_SCI,	MODE_ENG
};

// Single action display modes
enum single_disp {
	SDISP_NORMAL=0,
	SDISP_SHOW,
	SDISP_BIN,	SDISP_OCT,	SDISP_DEC,	SDISP_HEX,
};

// Trig modes
enum trig_modes {
	TRIG_DEG=0,	TRIG_RAD,	TRIG_GRAD
};
extern enum trig_modes get_trig_mode(void);

// Date modes
enum date_modes {
	DATE_YMD=0,	DATE_DMY,	DATE_MDY
};
	
// Fraction denominator modes
enum denom_modes {
	DENOM_ANY=0,	DENOM_FIXED,	DENOM_FACTOR
};

enum sigma_modes {
	SIGMA_LINEAR=0,		SIGMA_EXP,
	SIGMA_POWER,		SIGMA_LOG,
	SIGMA_BEST
};

enum catalogues {
	CATALOGUE_NONE=0,
	CATALOGUE_NORMAL,		CATALOGUE_COMPLEX,
	CATALOGUE_STATS,		CATALOGUE_PROB,
	CATALOGUE_INT,
	CATALOGUE_PROG,			CATALOGUE_TEST,
	CATALOGUE_MODE,
	CATALOGUE_ALPHA,
	CATALOGUE_ALPHA_SYMBOLS,	CATALOGUE_ALPHA_COMPARES,
	CATALOGUE_ALPHA_ARROWS,		CATALOGUE_ALPHA_STATS,
	CATALOGUE_ALPHA_LETTERS_UPPER,	CATALOGUE_ALPHA_LETTERS_LOWER,
	CATALOGUE_CONST,		CATALOGUE_COMPLEX_CONST,
	CATALOGUE_CONV
};
	
enum shifts {
	SHIFT_N = 0,
	SHIFT_F, SHIFT_G, SHIFT_H,
	SHIFT_LC_N, SHIFT_LC_G		// Two lower case planes
};

#define MagicMarker	0x1357fdb9

extern struct state {
	unsigned long int magic;	// Magic marker to detect failed RAM

// User noticable state
#define SB(f, p)	unsigned int f : p
#include "statebits.h"
#undef SB
	
	unsigned int base : 8;		// Base value for a command with an argument

	unsigned int error : 4;		// Did an error occur, if so what code?
	unsigned int status : 4;	// display status screen line

	unsigned char digval2 : 8;
	unsigned int digval : 10;
	unsigned int numdigit : 4;
	unsigned int shifts : 2;

	unsigned int denom_mode : 2;	// Fractions denominator mode
	unsigned int denom_max : 14;	// Maximum denominator

	unsigned int last_prog : 9;	// Position of the last program statement
	unsigned int int_len : 6;	// Length of Integers
	unsigned int intm : 1;		// In integer mode

	unsigned int state_pc : 15;	// XEQ internal - don't use
	unsigned int state_lift : 1;	// XEQ internal - don't use

	unsigned int retstk_ptr : 4;	// XEQ internal - don't use
	unsigned int usrpc : 9;		// XEQ internal - don't use
	unsigned int smode : 3;		// Single short display mode

	unsigned int catalogue : 5;	// In catalogue mode
	unsigned int int_base : 4;	// Integer input/output base
	//unsigned int int_mode : 2;	// Integer sign mode
	unsigned int test : 3;		// Waiting for a test command entry
	unsigned int int_window : 3;	// Which window to display 0=rightmost
	unsigned int gtodot : 1;	// GTO . sequence met

	unsigned int eol : 5;		// XEQ internal - don't use
	unsigned int cmdlineeex : 5;	// XEQ internal - don't use
	unsigned int cmdlinedot : 2;	// XEQ internal - don't use
	unsigned int state_running : 1;	// XEQ internal - don't use

	unsigned int alpha : 1;		// Alpha shift key pressed
	unsigned int cmplx : 1;		// Complex prefix pressed
	unsigned int wascomplex : 1;	// Previous operation was complex
	unsigned int arrow : 1;		// Conversion in progress
	unsigned int multi : 1;		// Multi-word instruction being entered
	unsigned int alphashift : 1;	// Alpha shifted to lower case
	unsigned int version : 1;	// Version display mode
	unsigned int implicit_rtn : 1;	// End of program is an implicit return
	unsigned int hyp : 1;		// Entering a HYP or HYP-1 operation
	unsigned int confirm : 2;	// Confirmation of operation required
	unsigned int dot : 1;		// misc use
	unsigned int improperfrac : 1;	// proper or improper fraction display
	unsigned int fraccomma : 1;	// . or , for decimal
	unsigned int nothousands : 1;	// , or nothing for thousands separator

	unsigned int ind : 1;		// Indirection STO or RCL
	unsigned int arrow_alpha : 1;	// display alpha conversion
	unsigned int rarg : 1;		// In argument accept mode
	unsigned int runmode : 1;	// Program mode or run mode
	unsigned int flags : 1;		// Display state flags
#ifdef REALBUILD
	unsigned int testmode : 1;
	unsigned int off : 1;
	unsigned int contrast : 4;	// Display contrast
	unsigned int LowPower : 1;	// low power detected
	unsigned int LowPowerCount : 16;
#else
	unsigned int trace : 1;
#endif
	unsigned int disp_small : 1;	// Display the status message in small font
	unsigned int int_winl : 1;	// Is there a window left
	unsigned int int_winr : 1;	// Is there a window right

	unsigned int hms : 1;		// H.MS mode
	unsigned int fract : 1;		// Fractions mode
} state;


extern void err(const enum errors);
extern const char *pretty(unsigned char);

extern const char *get_cmdline(void);

extern int is_intmode(void);
extern enum shifts cur_shift(void);

extern void reset_volatile_state(void);
extern void xeq(opcode);
extern void xeqprog(void);
extern void xeqone(char *);
extern void xeq_init(void);
extern void xeq_init_contexts(void);
extern void init_34s(void);
extern void process_keycode(int);

extern unsigned int state_pc(void);
extern void set_pc(unsigned int);

extern void clrprog(void);
extern void clrall(decimal64 *a, decimal64 *b, decContext *nulc);
extern void reset(decimal64 *a, decimal64 *b, decContext *nulc);

extern opcode getprog(int n);
extern void stoprog(opcode);
extern void delprog(void);
extern unsigned int dec(unsigned int);
extern void incpc(void);
extern void decpc(void);
extern unsigned int find_label_from(unsigned short int, unsigned int, int);
extern void fin_tst(const int);
extern unsigned int checksum_code(void);

extern unsigned int get_bank_flags(void);
extern void set_bank_flags(unsigned int);

extern const char *prt(opcode, char *);
extern const char *catcmd(opcode, char *);

extern void getX(decNumber *x);
extern void getY(decNumber *y);

extern void getXY(decNumber *x, decNumber *y);
extern void getYZ(decNumber *x, decNumber *y);
extern void setXY(const decNumber *x, const decNumber *y);

extern void setlastX(void);

extern int stack_size(void);
extern void lift(void);
extern void process_cmdline_set_lift(void);

extern long long int d64toInt(const decimal64 *n);
extern void d64fromInt(decimal64 *n, const long long int z);

extern decimal64 *get_reg_n(int);
extern long long int get_reg_n_as_int(int);
extern void put_reg_n_from_int(int, const long long int);
extern void get_reg_n_as_dn(int, decNumber *);
extern void put_reg_n(int, const decNumber *);
extern void swap_reg(decimal64 *, decimal64 *);

extern void reg_put_int(int, unsigned long long int, int);
//extern unsigned long long int reg_get_int(int, int *);

extern void put_int(unsigned long long int, int, decimal64 *);
extern unsigned long long int get_int(const decimal64 *, int *);

extern void get_maxdenom(decNumber *);

extern int get_user_flag(int);
extern void set_user_flag(int);
extern void clr_user_flag(int);

extern void xcopy(void *, const void *, int);
extern void xset(void *, const char, int);
extern char *find_char(const char *, const char);
extern char *scopy(char *, const char *);		// copy string return pointer to *end*
extern const char *sncopy(char *, const char *, int);	// copy string return pointer to *start*
extern char *scopy_char(char *, const char *, const char);
extern char *scopy_spc(char *, const char *);
extern char *sncopy_char(char *, const char *, int, const char);
extern char *sncopy_spc(char *, const char *, int);
extern int slen(const char *);

extern char *num_arg(char *, unsigned int);		// number, no leading zeros
extern char *num_arg_0(char *, unsigned int, int);	// n digit number, leading zeros

extern int s_to_i(const char *);
unsigned long long int s_to_ull(const char *, int);

extern void do_conv(decNumber *, unsigned int, const decNumber *, decContext *);
extern decNumber *convC2F(decNumber *, const decNumber *, decContext *);
extern decNumber *convF2C(decNumber *, const decNumber *, decContext *);

#endif
