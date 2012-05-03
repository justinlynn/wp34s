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
#if COMMANDS_PASS != 2
#include "xeq.h"
#include "xrom.h"
#include "decn.h"
#include "complex.h"
#include "stats.h"
#include "int.h"
#include "date.h"
#include "display.h"
#include "consts.h"
#include "alpha.h"
#include "lcd.h"
#include "storage.h"
#include "serial.h"
#ifdef INFRARED
#include "printer.h"
#endif
#include "matrix.h"
#ifdef INCLUDE_STOPWATCH
#include "stopwatch.h"
#endif
#endif

/*
 *  Macro to define pointers to XROM routines
 *  Usage: XPTR(WHO) instead of a function pointer
 */
#define XLBL(name) XROM_ ## name
#define XPTR(name) (xrom+XLBL(name)-XROM_START)

/* Utility macros to reduce the horizontal space of using XPTR directly
 * Usage is the same.
 */
#define XMR(name)	(FP_MONADIC_REAL) XPTR(name)
#define XDR(name)	(FP_DYADIC_REAL) XPTR(name)
#define XTR(name)	(FP_TRIADIC_REAL) XPTR(name)

#define XMC(name)	(FP_MONADIC_CMPLX) XPTR(name)
#define XDC(name)	(FP_DYADIC_CMPLX) XPTR(name)

#define XMI(name)	(FP_MONADIC_INT) XPTR(name)
#define XDI(name)	(FP_DYADIC_INT) XPTR(name)
#define XTI(name)	(FP_TRIADIC_INT) XPTR(name)

#define XNIL(name)	(FP_NILADIC) XPTR(name)
#define XARG(name)	(FP_RARG) XPTR(name)
#define XMULTI(name)	(FP_MULTI) XPTR(name)


/* Infrared command wrappers to maintain binary compatibility across images */
#ifdef INFRARED
#define IRN(x)		& (x)
#define IRA(x)		& (x)
#else
#define IRN(x)		(FP_NILADIC) NOFN
#define IRA(x)		NOFN
#endif


#ifdef SHORT_POINTERS
#ifndef COMMANDS_PASS
#define COMMANDS_PASS 1
#else
/*
 *  This is pass 2 of the compile.
 *
 *  Create a dummy segment and store the full blown tables there
 */
#define CMDTAB __attribute__((section(".cmdtab"),used))

/*
 *  Help the post-processor to find the data in the flash image
 */
extern const struct monfunc_cmdtab monfuncs_ct[];
extern const struct dyfunc_cmdtab dyfuncs_ct[];
extern const struct trifunc_cmdtab trifuncs_ct[];
extern const struct niladic_cmdtab niladics_ct[];
extern const struct argcmd_cmdtab argcmds_ct[];
extern const struct multicmd_cmdtab multicmds_ct[];

CMDTAB __attribute__((externally_visible))
const struct _command_info command_info = {
	NUM_MONADIC, monfuncs,  monfuncs_ct,
	NUM_DYADIC,  dyfuncs,   dyfuncs_ct,
	NUM_TRIADIC, trifuncs,  trifuncs_ct,
	NUM_NILADIC, niladics,  niladics_ct,
	NUM_RARG,    argcmds,   argcmds_ct,
	NUM_MULTI,   multicmds, multicmds_ct,
};

#endif
#endif

// Dummy definition for catalogue generation
#ifdef COMPILE_CATALOGUES
#undef NOFN 
#else
#define NOFN NULL
#endif

/* Define our table of monadic functions.
 * These must be in the same order as the monadic function enum but we'll
 * validate this only if debugging is enabled.
 */
#ifdef COMPILE_CATALOGUES
#undef NOFN
#define FUNC(name, d, c, i, fn) { #d, #c, #i, fn },
#elif DEBUG
#define FUNC(name, d, c, i, fn) { name, d, c, i, fn },
#elif COMMANDS_PASS == 1
#define FUNC(name, d, c, i, fn) { 0xaa55, 0x55aa, 0xa55a, fn },
#else
#define FUNC(name, d, c, i, fn) { d, c, i, fn },
#endif

#if COMMANDS_PASS == 2
CMDTAB const struct monfunc_cmdtab monfuncs_ct[ NUM_MONADIC ] = {
#else
const struct monfunc monfuncs[ NUM_MONADIC ] = {
#endif
	FUNC(OP_FRAC,	&decNumberFrac,		XMC(cpx_FRAC),	&intFP,		"FP")
	FUNC(OP_FLOOR,	&decNumberFloor,	NOFN,		&intIP,		"FLOOR")
	FUNC(OP_CEIL,	&decNumberCeil,		NOFN,		&intIP,		"CEIL")
	FUNC(OP_ROUND,	&decNumberRound,	NOFN,		&intIP,		"ROUNDI")
	FUNC(OP_TRUNC,	&decNumberTrunc,	XMC(cpx_TRUNC),	&intIP,		"IP")
	FUNC(OP_ABS,	&dn_abs,		&cmplxAbs,	&intAbs,	"ABS")
	FUNC(OP_RND,	&decNumberRnd,		XMC(cpx_ROUND),	&intIP,		"ROUND")
	FUNC(OP_SIGN,	XMR(SIGN),		XMC(cpx_SIGN),	&intSign,	"SIGN")
	FUNC(OP_LN,	&dn_ln,			&cmplxLn,	&intMonadic,	"LN")
	FUNC(OP_EXP,	&dn_exp,		&cmplxExp,	&intMonadic,	"e\234")
	FUNC(OP_SQRT,	&dn_sqrt,		&cmplxSqrt,	&intSqrt,	"\003")
	FUNC(OP_RECIP,	&decNumberRecip,	&cmplxRecip,	NOFN,		"1/x")
	FUNC(OP__1POW,	&decNumberPow_1,	&cmplx_1x,	&int_1pow,	"(-1)\234")
	FUNC(OP_LOG,	&dn_log10,		XMC(cpx_LOG10),	&intLog10,	"LOG\271\270")
	FUNC(OP_LG2,	&dn_log2,		XMC(cpx_LOG2),	&intLog2,	"LOG\272")
	FUNC(OP_2POWX,	&decNumberPow2,		XMC(cpx_POW2),	&int2pow,	"2\234")
	FUNC(OP_10POWX,	&decNumberPow10,	XMC(cpx_POW10),	&int10pow,	"10\234")
	FUNC(OP_LN1P,	&decNumberLn1p,		XMC(cpx_LN1P),	NOFN,		"LN1+x")
	FUNC(OP_EXPM1,	&decNumberExpm1,	XMC(cpx_EXPM1),	NOFN,		"e\234-1")
	FUNC(OP_LAMW,	XMR(W0),		XMC(CPX_W0),	NOFN,		"W\276")
	FUNC(OP_LAMW1,	XMR(W1),		NOFN,		NOFN,		"W\033")
	FUNC(OP_INVW,	XMR(W_INVERSE),		XMC(CPX_W_INVERSE),NOFN,	"W\235")
	FUNC(OP_SQR,	&decNumberSquare,	XMC(cpx_x2),	&intSqr,	"x\232")
	FUNC(OP_CUBE,	&decNumberCube,		XMC(cpx_x3),	&intCube,	"x\200")
	FUNC(OP_CUBERT,	&decNumberCubeRoot,	&cmplxCubeRoot,	&intMonadic,	"\200\003")
	FUNC(OP_FIB,	XMR(FIB),		XMC(CPX_FIB),	&intFib,	"FIB")
	FUNC(OP_2DEG,	&decNumberDRG,		NOFN,		NOFN,		"\015DEG")
	FUNC(OP_2RAD,	&decNumberDRG,		NOFN,		NOFN,		"\015RAD")
	FUNC(OP_2GRAD,	&decNumberDRG,		NOFN,		NOFN,		"\015GRAD")
	FUNC(OP_DEG2,	&decNumberDRG,		NOFN,		NOFN,		"DEG\015")
	FUNC(OP_RAD2,	&decNumberDRG,		NOFN,		NOFN,		"RAD\015")
	FUNC(OP_GRAD2,	&decNumberDRG,		NOFN,		NOFN,		"GRAD\015")
	FUNC(OP_SIN,	&decNumberSin,		&cmplxSin,	NOFN,		"SIN")
	FUNC(OP_COS,	&decNumberCos,		&cmplxCos,	NOFN,		"COS")
	FUNC(OP_TAN,	&decNumberTan,		&cmplxTan,	NOFN,		"TAN")
	FUNC(OP_ASIN,	&decNumberArcSin,	XMC(cpx_ASIN),	NOFN,		"ASIN")
	FUNC(OP_ACOS,	&decNumberArcCos,	XMC(cpx_ACOS),	NOFN,		"ACOS")
	FUNC(OP_ATAN,	&decNumberArcTan,	XMC(cpx_ATAN),	NOFN,		"ATAN")
	FUNC(OP_SINC,	&decNumberSinc,		&cmplxSinc,	NOFN,		"SINC")
	FUNC(OP_SINH,	&decNumberSinh,		&cmplxSinh,	NOFN,		"SINH")
	FUNC(OP_COSH,	&decNumberCosh,		&cmplxCosh,	NOFN,		"COSH")
	FUNC(OP_TANH,	&decNumberTanh,		&cmplxTanh,	NOFN,		"TANH")
	FUNC(OP_ASINH,	&decNumberArcSinh,	XMC(cpx_ASINH),	NOFN,		"ASINH")
	FUNC(OP_ACOSH,	&decNumberArcCosh,	XMC(cpx_ACOSH),	NOFN,		"ACOSH")
	FUNC(OP_ATANH,	&decNumberArcTanh,	XMC(cpx_ATANH),	NOFN,		"ATANH")
#ifdef INCLUDE_GUDERMANNIAN
	FUNC(OP_GUDER,	XMR(gd),		XMC(cpx_gd),	NOFN,		"g\206")
	FUNC(OP_INVGUD,	XMR(inv_gd),		XMC(cpx_inv_gd),NOFN,		"g\206\235")
#endif

	FUNC(OP_FACT,	&decNumberFactorial,	XMC(cpx_FACT),	&intMonadic,	"x!")
	FUNC(OP_GAMMA,	&decNumberGamma,	&cmplxGamma,	&intMonadic,	"\202")
	FUNC(OP_LNGAMMA,&decNumberLnGamma,	&cmplxLnGamma,	NOFN,		"LN\202")
	FUNC(OP_DEG2RAD,&decNumberD2R,		NOFN,		NOFN,		"\005\015rad")
	FUNC(OP_RAD2DEG,&decNumberR2D,		NOFN,		NOFN,		"rad\015\005")
	FUNC(OP_DEG2GRD,&decNumberD2G,		NOFN,		NOFN,		"\005\015G")
	FUNC(OP_GRD2DEG,&decNumberG2D,		NOFN,		NOFN,		"G\015\005")
	FUNC(OP_RAD2GRD,&decNumberR2G,		NOFN,		NOFN,		"rad\015G")
	FUNC(OP_GRD2RAD,&decNumberG2R,		NOFN,		NOFN,		"G\015rad")
	FUNC(OP_CCHS,	NOFN,			&cmplxMinus,	NOFN,		"\024+/-")
	FUNC(OP_CCONJ,	NOFN,			XMC(cpx_CONJ),	NOFN,		"CONJ")
	FUNC(OP_ERF,	XMR(ERF),		NOFN,		NOFN,		"erf")
	FUNC(OP_ERFC,	XMR(ERFC),		NOFN,		NOFN,		"erfc")
	FUNC(OP_pdf_Q,	XMR(PDF_Q), 		NOFN,		NOFN,		"\264(x)")
	FUNC(OP_cdf_Q,	XMR(CDF_Q), 		NOFN,		NOFN,		"\224(x)")
	FUNC(OP_qf_Q,	XMR(QF_Q),  		NOFN,		NOFN,		"\224\235(p)")
	FUNC(OP_pdf_chi2, XMR(PDF_CHI2),	NOFN,		NOFN,		"\265\232\276")
	FUNC(OP_cdf_chi2, XMR(CDF_CHI2),	NOFN,		NOFN,		"\265\232")
	FUNC(OP_qf_chi2,  XMR(QF_CHI2),		NOFN,		NOFN,		"\265\232INV")
	FUNC(OP_pdf_T,	XMR(PDF_T),		NOFN,		NOFN,		"t\276(x)")
	FUNC(OP_cdf_T,	XMR(CDF_T),		NOFN,		NOFN,		"t(x)")
	FUNC(OP_qf_T,	XMR(QF_T),		NOFN,		NOFN,		"t\235(p)")
	FUNC(OP_pdf_F,	XMR(PDF_F),		NOFN,		NOFN,		"F\276(x)")
	FUNC(OP_cdf_F,	XMR(CDF_F),		NOFN,		NOFN,		"F(x)")
	FUNC(OP_qf_F,	XMR(QF_F),		NOFN,		NOFN,		"F\235(p)")
	FUNC(OP_pdf_WB,	XMR(PDF_WEIB),		NOFN,		NOFN,		"Weibl\276")
	FUNC(OP_cdf_WB,	XMR(CDF_WEIB),		NOFN,		NOFN,		"Weibl")
	FUNC(OP_qf_WB,	XMR(QF_WEIB),		NOFN,		NOFN,		"Weibl\235")
	FUNC(OP_pdf_EXP,XMR(PDF_EXPON),		NOFN,		NOFN,		"Expon\276")
	FUNC(OP_cdf_EXP,XMR(CDF_EXPON),		NOFN,		NOFN,		"Expon")
	FUNC(OP_qf_EXP,	XMR(QF_EXPON),		NOFN,		NOFN,		"Expon\235")
	FUNC(OP_pdf_B,	XMR(PDF_BINOMIAL),	NOFN,		NOFN,		"Binom\276")
	FUNC(OP_cdf_B,	XMR(CDF_BINOMIAL),	NOFN,		NOFN,		"Binom")
	FUNC(OP_qf_B,	XMR(QF_BINOMIAL),	NOFN,		NOFN,		"Binom\235")
	FUNC(OP_pdf_Plam, XMR(PDF_POISSON),	NOFN,		NOFN,		"Pois\252\276")
	FUNC(OP_cdf_Plam, XMR(CDF_POISSON),	NOFN,		NOFN,		"Pois\252")
	FUNC(OP_qf_Plam,  XMR(QF_POISSON),	NOFN,		NOFN,		"Pois\252\235")
	FUNC(OP_pdf_P,	XMR(PDF_POIS2),		NOFN,		NOFN,		"Poiss\276")
	FUNC(OP_cdf_P,	XMR(CDF_POIS2),		NOFN,		NOFN,		"Poiss")
	FUNC(OP_qf_P,	XMR(QF_POIS2),		NOFN,		NOFN,		"Poiss\235")
	FUNC(OP_pdf_G,	XMR(PDF_GEOM),		NOFN,		NOFN,		"Geom\276")
	FUNC(OP_cdf_G,	XMR(CDF_GEOM),		NOFN,		NOFN,		"Geom")
	FUNC(OP_qf_G,	XMR(QF_GEOM),		NOFN,		NOFN,		"Geom\235")
	FUNC(OP_pdf_N,	XMR(PDF_NORMAL),	NOFN,		NOFN,		"Norml\276")
	FUNC(OP_cdf_N,	XMR(CDF_NORMAL),	NOFN,		NOFN,		"Norml")
	FUNC(OP_qf_N,	XMR(QF_NORMAL),		NOFN,		NOFN,		"Norml\235")
	FUNC(OP_pdf_LN,	XMR(PDF_LOGNORMAL),	NOFN,		NOFN,		"LgNrm\276")
	FUNC(OP_cdf_LN,	XMR(CDF_LOGNORMAL),	NOFN,		NOFN,		"LgNrm")
	FUNC(OP_qf_LN,	XMR(QF_LOGNORMAL),	NOFN,		NOFN,		"LgNrm\235")
	FUNC(OP_pdf_LG,	XMR(PDF_LOGIT),		NOFN,		NOFN,		"Logis\276")
	FUNC(OP_cdf_LG,	XMR(CDF_LOGIT),		NOFN,		NOFN,		"Logis")
	FUNC(OP_qf_LG,	XMR(QF_LOGIT),		NOFN,		NOFN,		"Logis\235")
	FUNC(OP_pdf_C,	XMR(PDF_CAUCHY),	NOFN,		NOFN,		"Cauch\276")
	FUNC(OP_cdf_C,	XMR(CDF_CAUCHY),	NOFN,		NOFN,		"Cauch")
	FUNC(OP_qf_C,	XMR(QF_CAUCHY),		NOFN,		NOFN,		"Cauch\235")
#ifdef INCLUDE_CDFU
	FUNC(OP_cdfu_Q,	XMR(CDFU_Q),		NOFN,		NOFN,		"\224\277(x)")
	FUNC(OP_cdfu_T,	XMR(CDFU_T),		NOFN,		NOFN,		"t\277(x)")
	FUNC(OP_cdfu_WB, XMR(CDFU_WEIB),	NOFN,		NOFN,		"Weibl\277")
	FUNC(OP_cdfu_EXP, XMR(CDFU_EXPON),	NOFN,		NOFN,		"Expon\277")
	FUNC(OP_cdfu_G,	XMR(CDFU_GEOM),		NOFN,		NOFN,		"Geom\277")
	FUNC(OP_cdfu_N,	XMR(CDFU_NORMAL),	NOFN,		NOFN,		"Norml\277")
	FUNC(OP_cdfu_LN, XMR(CDFU_LOGNORMAL),	NOFN,		NOFN,		"LgNrm\277")
	FUNC(OP_cdfu_LG, XMR(CDFU_LOGIT),	NOFN,		NOFN,		"Logis\277")
	FUNC(OP_cdfu_C,	XMR(CDFU_CAUCHY),	NOFN,		NOFN,		"Cauch\277")
#endif
	FUNC(OP_xhat,	&stats_xhat,		NOFN,		NOFN,		"\031")
	FUNC(OP_yhat,	&stats_yhat,		NOFN,		NOFN,		"\032")
	FUNC(OP_sigper,	&stats_sigper,		NOFN,		NOFN,		"%\221")
	FUNC(OP_PERCNT,	XMR(PERCENT),		NOFN,		NOFN,		"%")
	FUNC(OP_PERCHG,	XMR(PERCHG),		NOFN,		NOFN,		"\203%")
	FUNC(OP_PERTOT,	XMR(PERTOT),		NOFN,		NOFN,		"%T")
	FUNC(OP_HMS2,	&decNumberHMS2HR,	NOFN,		NOFN,		"\015HR")
	FUNC(OP_2HMS,	&decNumberHR2HMS,	NOFN,		NOFN,		"\015H.MS")
	FUNC(OP_NOT,	&decNumberNot,		NOFN,		&intNot,	"NOT")
	FUNC(OP_BITCNT,	NOFN,			NOFN,		&intNumBits,	"nBITS")
	FUNC(OP_MIRROR,	NOFN,			NOFN,		&intMirror,	"MIRROR")
	FUNC(OP_DOWK,	&dateDayOfWeek,		NOFN,		NOFN,		"WDAY")
	FUNC(OP_D2J,	&dateToJ,		NOFN,		NOFN,		"D\015J")
	FUNC(OP_J2D,	&dateFromJ,		NOFN,		NOFN,		"J\015D")
	FUNC(OP_DEGC_F,	&convC2F,		NOFN,		NOFN,		"\005C\015\005F")
	FUNC(OP_DEGF_C,	&convF2C,		NOFN,		NOFN,		"\005F\015\005C")
	FUNC(OP_DB_AR,	&convDB2AR,		NOFN,		NOFN,		"dB\015ar.")
	FUNC(OP_AR_DB,	&convAR2DB,		NOFN,		NOFN,		"ar.\015dB")
	FUNC(OP_DB_PR,	&convDB2PR,		NOFN,		NOFN,		"dB\015pr.")
	FUNC(OP_PR_DB,	&convPR2DB,		NOFN,		NOFN,		"pr.\015dB")
	FUNC(OP_ZETA,	XMR(ZETA),		NOFN,		NOFN,		"\245")
	FUNC(OP_Bn,	XMR(Bn),		NOFN,		NOFN,		"B\275")
	FUNC(OP_BnS,	XMR(Bn_star),		NOFN,		NOFN,		"B\275\220")

#ifdef INCLUDE_EASTER
	FUNC(OP_EASTER,	&dateEaster,		NOFN,		NOFN,		"EASTER")
#endif
#ifdef INCLUDE_FACTOR
	FUNC(OP_FACTOR,	&decFactor,		NOFN,		&intFactor,	"FACTOR")
#endif
	FUNC(OP_DATE_YEAR, &dateExtraction,	NOFN,		NOFN,		"YEAR")
	FUNC(OP_DATE_MONTH, &dateExtraction,	NOFN,		NOFN,		"MONTH")
	FUNC(OP_DATE_DAY, &dateExtraction,	NOFN,		NOFN,		"DAY")
#ifdef INCLUDE_USER_IO
	FUNC(OP_RECV1,	&decRecv,		NOFN,		&intRecv,	"RECV1")
#endif
#ifdef INCLUDE_MANTISSA
	FUNC(OP_MANTISSA, &decNumberMantissa,	NOFN,		NOFN,		"MANT")
	FUNC(OP_EXPONENT, &decNumberExponent,	NOFN,		NOFN,		"EXPT")
	FUNC(OP_ULP,	  &decNumberULP,	NOFN,		XMI(int_ULP),	"ULP")
#endif
	FUNC(OP_MAT_ALL, &matrix_all,		NOFN,		NOFN,		"M-ALL")
	FUNC(OP_MAT_DIAG, &matrix_diag,		NOFN,		NOFN,		"M-DIAG")
	FUNC(OP_MAT_TRN, &matrix_transpose,	NOFN,		NOFN,		"TRANSP")
	FUNC(OP_MAT_RQ,	&matrix_rowq,		NOFN,		NOFN,		"nROW")
	FUNC(OP_MAT_CQ,	&matrix_colq,		NOFN,		NOFN,		"nCOL")
	FUNC(OP_MAT_IJ,	&matrix_getrc,		NOFN,		NOFN,		"M.IJ")
	FUNC(OP_MAT_DET, &matrix_determinant,	NOFN,		NOFN,		"DET")
#ifdef MATRIX_LU_DECOMP
	FUNC(OP_MAT_LU, &matrix_lu_decomp,	NOFN,		NOFN,		"M.LU")
#endif
#ifdef INCLUDE_XROM_DIGAMMA
	FUNC(OP_DIGAMMA,XMR(DIGAMMA),		XMC(CPX_DIGAMMA),	NOFN,	"\226")
#endif
#undef FUNC
};


/* Define our table of dyadic functions.
 * These must be in the same order as the dyadic function enum but we'll
 * validate this only if debugging is enabled.
 */
#ifdef COMPILE_CATALOGUES
#define FUNC(name, d, c, i, fn) { #d, #c, #i, fn },
#elif DEBUG
#define FUNC(name, d, c, i, fn) { name, d, c, i, fn },
#elif COMMANDS_PASS == 1
#define FUNC(name, d, c, i, fn) { 0xaa55, 0x55aa, 0xa55a, fn },
#else
#define FUNC(name, d, c, i, fn) { d, c, i, fn },
#endif

#if COMMANDS_PASS == 2
CMDTAB const struct dyfunc_cmdtab dyfuncs_ct[ NUM_DYADIC ] = {
#else
const struct dyfunc dyfuncs[ NUM_DYADIC ] = {
#endif
	FUNC(OP_POW,	&dn_power,		&cmplxPower,	&intPower,	"y\234")
	FUNC(OP_ADD,	&dn_add,		&cmplxAdd,	&intAdd,	"+")
	FUNC(OP_SUB,	&dn_subtract,		&cmplxSubtract,	&intSubtract,	"-")
	FUNC(OP_MUL,	&dn_multiply,		&cmplxMultiply,	&intMultiply,	"\034")
	FUNC(OP_DIV,	&dn_divide,		&cmplxDivide,	&intDivide,	"/")
	FUNC(OP_MOD,	&decNumberBigMod,	NOFN,		&intMod,	"RMDR")
	FUNC(OP_LOGXY,	&decNumberLogxy,	XDC(cpx_LOGXY),	&intDyadic,	"LOG\213")
	FUNC(OP_MIN,	&dn_min,		NOFN,		&intMin,	"MIN")
	FUNC(OP_MAX,	&dn_max,		NOFN,		&intMax,	"MAX")
	FUNC(OP_ATAN2,	&decNumberArcTan2,	NOFN,		NOFN,		"ANGLE")
	FUNC(OP_BETA,	XDR(beta),		XDC(cpx_beta),	NOFN,		"\241")
	FUNC(OP_LNBETA,	&decNumberLnBeta,	XDC(cpx_lnbeta),NOFN,		"LN\241")
	FUNC(OP_GAMMAP,	&decNumberGammap,	NOFN,		NOFN,		"I\202")
#ifdef INCLUDE_ELLIPTIC
	FUNC(OP_SN,	&decNumberSN,		&cmplxSN,	NOFN,		"SN")
	FUNC(OP_CN,	&decNumberCN,		&cmplxCN,	NOFN,		"CN")
	FUNC(OP_DN,	&decNumberDN,		&cmplxDN,	NOFN,		"DN")
#endif
	FUNC(OP_COMB,	&decNumberComb,		XDC(CPX_COMB),	&intDyadic,	"COMB")
	FUNC(OP_PERM,	&decNumberPerm,		XDC(CPX_PERM),	&intDyadic,	"PERM")
	FUNC(OP_PERMG,	XDR(PERMARGIN),		NOFN,		NOFN,		"%+MG")
	FUNC(OP_MARGIN,	XDR(MARGIN),		NOFN,		NOFN,		"%MG")
	FUNC(OP_PARAL,	XDR(PARL),		XDC(CPX_PARL),	XDI(PARL),	"||")
	FUNC(OP_AGM,	XDR(AGM),		XDC(CPX_AGM),	NOFN,		"AGM")
	FUNC(OP_HMSADD,	&decNumberHMSAdd,	NOFN,		NOFN,		"H.MS+")
	FUNC(OP_HMSSUB,	&decNumberHMSSub,	NOFN,		NOFN,		"H.MS-")
	FUNC(OP_GCD,	&decNumberGCD,		NOFN,		&intGCD,	"GCD")
	FUNC(OP_LCM,	&decNumberLCM,		NOFN,		&intLCM,	"LCM")

	FUNC(OP_LAND,	&decNumberBooleanOp,	NOFN,		&intBooleanOp,	"AND")
	FUNC(OP_LOR,	&decNumberBooleanOp,	NOFN,		&intBooleanOp,	"OR")
	FUNC(OP_LXOR,	&decNumberBooleanOp,	NOFN,		&intBooleanOp,	"XOR")
	FUNC(OP_LNAND,	&decNumberBooleanOp,	NOFN,		&intBooleanOp,	"NAND")
	FUNC(OP_LNOR,	&decNumberBooleanOp,	NOFN,		&intBooleanOp,	"NOR")
	FUNC(OP_LXNOR,	&decNumberBooleanOp,	NOFN,		&intBooleanOp,	"XNOR")

	FUNC(OP_DTADD,	XDR(DATE_ADD),		NOFN,		NOFN,		"DAYS+")
	FUNC(OP_DTDIF,	XDR(DATE_DELTA),	NOFN,		NOFN,		"\203DAYS")

	FUNC(OP_LEGENDRE_PN,	XDR(LegendrePn),	NOFN,	NOFN,		"P\275")
	FUNC(OP_CHEBYCHEV_TN,	XDR(ChebychevTn),	NOFN,	NOFN,		"T\275")
	FUNC(OP_CHEBYCHEV_UN,	XDR(ChebychevUn),	NOFN,	NOFN,		"U\275")
	FUNC(OP_LAGUERRE,	XDR(LaguerreLn),	NOFN,	NOFN,		"L\275")
	FUNC(OP_HERMITE_HE,	XDR(HermiteHe),		NOFN,	NOFN,		"H\275")
	FUNC(OP_HERMITE_H,	XDR(HermiteH),		NOFN,	NOFN,		"H\275\276")
#ifdef INCLUDE_XROOT
	FUNC(OP_XROOT,	&decNumberXRoot,	&cmplxXRoot,	&intDyadic,	"\234\003y")
#endif
	FUNC(OP_MAT_ROW, &matrix_row,		NOFN,		NOFN,		"M-ROW")
	FUNC(OP_MAT_COL, &matrix_col,		NOFN,		NOFN,		"M-COL")
	FUNC(OP_MAT_COPY, &matrix_copy,		NOFN,		NOFN,		"M.COPY")
#ifdef INCLUDE_MANTISSA
	FUNC(OP_NEIGHBOUR,&decNumberNeighbour,	NOFN,		NOFN,		"NEIGHB")
#endif
#ifdef INCLUDE_XROM_BESSEL
	FUNC(OP_BESJN,	XDR(BES_JN),		XDC(CPX_JN),	NOFN,		"Jn")
	FUNC(OP_BESIN,	XDR(BES_IN),		XDC(CPX_IN),	NOFN,		"In")
	FUNC(OP_BESYN,	XDR(BES_YN),		XDC(CPX_YN),	NOFN,		"Yn")
	FUNC(OP_BESKN,	XDR(BES_KN),		XDC(CPX_KN),	NOFN,		"Kn")
#endif
#undef FUNC
};

/* Define our table of triadic functions.
 * These must be in the same order as the triadic function enum but we'll
 * validate this only if debugging is enabled.
 */
#ifdef COMPILE_CATALOGUES
#define FUNC(name, d, i, fn) { #d, #i, fn },
#elif DEBUG
#define FUNC(name, d, i, fn) { name, d, i, fn },
#elif COMMANDS_PASS == 1
#define FUNC(name, d, i, fn) { 0xaa55, 0xa55a, fn },
#else
#define FUNC(name, d, i, fn) { d, i, fn },
#endif

#if COMMANDS_PASS == 2
CMDTAB const struct trifunc_cmdtab trifuncs_ct[ NUM_TRIADIC ] = {
#else
const struct trifunc trifuncs[ NUM_TRIADIC ] = {
#endif
	FUNC(OP_BETAI,		&betai,			(FP_TRIADIC_INT) NOFN,	"I\241")
	FUNC(OP_DBL_DIV, 	(FP_TRIADIC_REAL) NOFN,	&intDblDiv,		"DBL/")
	FUNC(OP_DBL_MOD, 	(FP_TRIADIC_REAL) NOFN,	&intDblRmdr,		"DBLR")
#ifdef INCLUDE_MULADD
	FUNC(OP_MULADD, 	&decNumberMAdd,		&intMAdd,		"\034+")
#endif
	FUNC(OP_PERMRR,		XTR(PERMMR),		(FP_TRIADIC_INT) NOFN,	"%MRR")
        FUNC(OP_GEN_LAGUERRE,   XTR(LaguerreLnA),	(FP_TRIADIC_INT) NOFN,	"L\275\240")

	FUNC(OP_MAT_MUL,	&matrix_multiply,	(FP_TRIADIC_INT) NOFN,	"M\034")
	FUNC(OP_MAT_GADD,	&matrix_genadd,		(FP_TRIADIC_INT) NOFN,	"M+\034")
	FUNC(OP_MAT_REG,	&matrix_getreg,		(FP_TRIADIC_INT) NOFN,	"M.REG")
	FUNC(OP_MAT_LIN_EQN,	&matrix_linear_eqn,	(FP_TRIADIC_INT) NOFN,	"LINEQS")
	FUNC(OP_TO_DATE,	&dateFromYMD,		(FP_TRIADIC_INT) NOFN,	"\015DATE")
#undef FUNC
};



#ifdef COMPILE_CATALOGUES
#define FUNC(name, d, fn, arg)		{ #d, arg, fn },
#elif DEBUG
#define FUNC(name, d, fn, arg)		{ name, d, arg, fn },
#elif COMMANDS_PASS == 1
#define FUNC(name, d, fn, arg)		{ 0xaa55, arg, fn },
#else
#define FUNC(name, d, fn, arg)		{ d, arg, fn },
#endif

#define FUNC0(name, d, fn)	FUNC(name, d, fn, 0)
#define FUNC1(name, d, fn)	FUNC(name, d, fn, 1)
#define FUNC2(name, d, fn)	FUNC(name, d, fn, 2)
#define FN_I0(name, d, fn)	FUNC(name, d, fn, NILADIC_NOINT | 0)
#define FN_I1(name, d, fn)	FUNC(name, d, fn, NILADIC_NOINT | 1)
#define FN_I2(name, d, fn)	FUNC(name, d, fn, NILADIC_NOINT | 2)

#if COMMANDS_PASS == 2
CMDTAB const struct niladic_cmdtab niladics_ct[ NUM_NILADIC ] = {
#else
const struct niladic niladics[ NUM_NILADIC ] = {
#endif
	FUNC0(OP_NOP,		(FP_NILADIC) NOFN,	"NOP")
	FUNC0(OP_VERSION,	&version,		"VERS")
	FUNC0(OP_OFF,		&cmd_off,		"OFF")
	FUNC1(OP_STKSIZE,	&get_stack_size,	"SSIZE?")
	FUNC0(OP_STK4,		XNIL(STACK_4_LEVEL),	"SSIZE4")
	FUNC0(OP_STK8,		XNIL(STACK_8_LEVEL),	"SSIZE8")
	FUNC1(OP_INTSIZE,	&get_word_size,		"WSIZE?")
	FUNC0(OP_RDOWN,		&roll_down,		"R\017")
	FUNC0(OP_RUP,		&roll_up,		"R\020")
	FUNC0(OP_CRDOWN,	&cpx_roll_down,		"\024R\017")
	FUNC0(OP_CRUP,		&cpx_roll_up,		"\024R\020")
	FUNC0(OP_CENTER,	&cpx_enter,		"\024ENTER")
	FUNC0(OP_FILL,		&fill,			"FILL")
	FUNC0(OP_CFILL,		&cpx_fill,		"\024FILL")
	FUNC0(OP_DROP,		&drop,			"DROP")
	FUNC0(OP_DROPXY,	&drop,			"\024DROP")
	FN_I1(OP_sigmaX2Y,	&sigma_val,		"\221x\232y")
	FN_I1(OP_sigmaX2,	&sigma_val,		"\221x\232")
	FN_I1(OP_sigmaY2,	&sigma_val,		"\221y\232")
	FN_I1(OP_sigmaXY,	&sigma_val,		"\221xy")
	FN_I1(OP_sigmaX,	&sigma_val,		"\221x")
	FN_I1(OP_sigmaY,	&sigma_val,		"\221y")
	FN_I1(OP_sigmalnX,	&sigma_val,		"\221lnx")
	FN_I1(OP_sigmalnXlnX,	&sigma_val,		"\221ln\232x")
	FN_I1(OP_sigmalnY,	&sigma_val,		"\221lny")
	FN_I1(OP_sigmalnYlnY,	&sigma_val,		"\221ln\232y")
	FN_I1(OP_sigmalnXlnY,	&sigma_val,		"\221lnxy")
	FN_I1(OP_sigmaXlnY,	&sigma_val,		"\221xlny")
	FN_I1(OP_sigmaYlnX,	&sigma_val,		"\221ylnx")
	FN_I1(OP_sigmaN,	&sigma_val,		"n\221")
	FN_I2(OP_statS,		&stats_deviations,	"s")
	FN_I2(OP_statSigma,	&stats_deviations,	"\261")
	FN_I2(OP_statGS,	&stats_deviations,	"\244")
	FN_I2(OP_statGSigma,	&stats_deviations,	"\244\276")
	FN_I1(OP_statWS,	&stats_wdeviations,	"sw")
	FN_I1(OP_statWSigma,	&stats_wdeviations,	"\261w")
	FN_I2(OP_statMEAN,	&stats_mean,		"\001")
	FN_I1(OP_statWMEAN,	&stats_wmean,		"\001w")
	FN_I2(OP_statGMEAN,	&stats_gmean,		"\001g")
	FN_I1(OP_statR,		&stats_correlation,	"CORR")
	FN_I2(OP_statLR,	&stats_LR,		"L.R.")
	FN_I2(OP_statSErr,	&stats_deviations,	"SERR")
	FN_I2(OP_statGSErr,	&stats_deviations,	"\244m")
	FN_I1(OP_statWSErr,	&stats_wdeviations,	"SERRw")
	FN_I1(OP_statCOV,	&stats_COV,		"COV")
	FN_I1(OP_statSxy,	&stats_COV,		"s\213\214")
	FUNC0(OP_LINF,		&stats_mode,		"LinF")
	FUNC0(OP_EXPF,		&stats_mode,		"ExpF")
	FUNC0(OP_PWRF,		&stats_mode,		"PowerF")
	FUNC0(OP_LOGF,		&stats_mode,		"LogF")
	FUNC0(OP_BEST,		&stats_mode,		"BestF")
	FUNC1(OP_RANDOM,	&stats_random,		"RAN#")
	FUNC0(OP_STORANDOM,	&stats_sto_random,	"SEED")
	FUNC0(OP_DEG,		XNIL(DEGREES),		"DEG")
	FUNC0(OP_RAD,		XNIL(RADIANS),		"RAD")
	FUNC0(OP_GRAD,		XNIL(GRADIANS),		"GRAD")
	FUNC0(OP_RTN,		&op_rtn,		"RTN")
	FUNC0(OP_RTNp1,		&op_rtn,		"RTN+1")
	FUNC0(OP_END,		&op_rtn,		"END")
	FUNC0(OP_RS,		&op_rs,			"STOP")
	FUNC0(OP_PROMPT,	&op_prompt,		"PROMPT")
	FUNC0(OP_SIGMACLEAR,	&sigma_clear,		"CL\221")
	FUNC0(OP_CLREG,		&clrreg,		"CLREGS")
	FUNC0(OP_rCLX,		&clrx,			"CLx")
	FUNC0(OP_CLSTK,		&clrstk,		"CLSTK")
	FUNC0(OP_CLALL,		NOFN,			"CLALL")
	FUNC0(OP_RESET,		NOFN,			"RESET")
	FUNC0(OP_CLPROG,	NOFN,			"CLPROG")
	FUNC0(OP_CLPALL,	NOFN,			"CLPALL")
	FUNC0(OP_CLFLAGS,	&clrflags,		"CFALL")
	FN_I0(OP_R2P,		&op_r2p,		"\015POL")
	FN_I0(OP_P2R,		&op_p2r,		"\015REC")
	FN_I0(OP_FRACDENOM,	&op_fracdenom,		"DENMAX")
	FN_I1(OP_2FRAC,		&op_2frac,		"DECOMP")
	FUNC0(OP_DENANY,	XNIL(F_DENANY),		"DENANY")
	FUNC0(OP_DENFIX,	XNIL(F_DENFIX),		"DENFIX")
	FUNC0(OP_DENFAC,	XNIL(F_DENFAC),		"DENFAC")
	FUNC0(OP_FRACIMPROPER,	&op_fract,		"IMPFRC")
	FUNC0(OP_FRACPROPER,	&op_fract,		"PROFRC")
	FUNC0(OP_RADDOT,	XNIL(RADIX_DOT),	"RDX.")
	FUNC0(OP_RADCOM,	XNIL(RADIX_COM),	"RDX,")
	FUNC0(OP_THOUS_ON,	XNIL(E3ON),		"E3ON")
	FUNC0(OP_THOUS_OFF,	XNIL(E3OFF),		"E3OFF")
	FUNC0(OP_INTSEP_ON,	XNIL(SEPON),		"SEPON")
	FUNC0(OP_INTSEP_OFF,	XNIL(SEPOFF),		"SEPOFF")
	FUNC0(OP_FIXSCI,	XNIL(FIXSCI),		"SCIOVR")
	FUNC0(OP_FIXENG,	XNIL(FIXENG),		"ENGOVR")
	FUNC0(OP_2COMP,		XNIL(ISGN_2C),		"2COMPL")
	FUNC0(OP_1COMP,		XNIL(ISGN_1C),		"1COMPL")
	FUNC0(OP_UNSIGNED,	XNIL(ISGN_UN),		"UNSIGN")
	FUNC0(OP_SIGNMANT,	XNIL(ISGN_SM),		"SIGNMT")
	FUNC0(OP_FLOAT,		&op_float,		"DECM")
	FUNC0(OP_HMS,		&op_float,		"H.MS")
	FUNC0(OP_FRACT,		&op_fract,		"FRACT")
	FUNC0(OP_LEAD0,		XNIL(IM_LZON),		"LZON")
	FUNC0(OP_TRIM0,		XNIL(IM_LZOFF),		"LZOFF")
	FUNC1(OP_LJ,		&int_justify,		"LJ")
	FUNC1(OP_RJ,		&int_justify,		"RJ")
	FUNC0(OP_DBL_MUL, 	&intDblMul,		"DBL\034")
	FN_I2(OP_RCLSIGMA,	&sigma_sum,		"SUM")
	FUNC0(OP_DATEDMY,	XNIL(D_DMY),		"D.MY")
	FUNC0(OP_DATEYMD,	XNIL(D_YMD),		"Y.MD")
	FUNC0(OP_DATEMDY,	XNIL(D_MDY),		"M.DY")
	FUNC0(OP_JG1752,	XNIL(JG1752),		"JG1752")
	FUNC0(OP_JG1582,	XNIL(JG1582),		"JG1582")
	FN_I0(OP_ISLEAP,	&date_isleap,		"LEAP?")
	FN_I0(OP_ALPHADAY,	&date_alphaday,		"\240DAY")
	FN_I0(OP_ALPHAMONTH,	&date_alphamonth,	"\240MONTH")
	FN_I0(OP_ALPHADATE,	&date_alphadate,	"\240DATE")
	FN_I0(OP_ALPHATIME,	&date_alphatime,	"\240TIME")
	FN_I1(OP_DATE,		&date_date,		"DATE")
	FN_I1(OP_TIME,		&date_time,		"TIME")
	FUNC0(OP_24HR,		XNIL(HR24),		"24H")
	FUNC0(OP_12HR,		XNIL(HR12),		"12H")
	FN_I0(OP_SETDATE,	&date_setdate,		"SETDAT")
	FN_I0(OP_SETTIME,	&date_settime,		"SETTIM")
	FUNC0(OP_CLRALPHA,	&clralpha,		"CL\240")
	FUNC0(OP_VIEWALPHA,	&alpha_view,		"VIEW\240")
	FUNC1(OP_ALPHALEN,	&alpha_length,		"\240LENG")
	FUNC1(OP_ALPHATOX,	&alpha_tox,		"\240\015x")
	FUNC0(OP_XTOALPHA,	&alpha_fromx,		"x\015\240")
	FUNC0(OP_ALPHAON,	&alpha_onoff,		"\240ON")
	FUNC0(OP_ALPHAOFF,	&alpha_onoff,		"\240OFF")
	FN_I0(OP_REGCOPY,	&op_regcopy,		"R-COPY")
	FN_I0(OP_REGSWAP,	&op_regswap,		"R-SWAP")
	FN_I0(OP_REGCLR,	&op_regclr,		"R-CLR")
	FN_I0(OP_REGSORT,	&op_regsort,		"R-SORT")

	FUNC0(OP_LOADA2D,	&store_a_to_d,		"\015A..D")
	FUNC0(OP_SAVEA2D,	&store_a_to_d,		"A..D\015")

	FUNC0(OP_GSBuser,	&do_usergsb,		"XEQUSR")
	FUNC0(OP_POPUSR,	&op_popusr,		"POPUSR")

	FN_I0(OP_XisInf,	&isInfinite,		"\237?")
	FN_I0(OP_XisNaN,	&isNan,			"NaN?")
	FN_I0(OP_XisSpecial,	&isSpecial,		"SPEC?")
	FUNC0(OP_XisPRIME,	&XisPrime,		"PRIME?")
	FUNC0(OP_XisINT,	&XisInt,		"INT?")
	FUNC0(OP_XisFRAC,	&XisInt,		"FP?")
	FUNC0(OP_XisEVEN,	&XisEvenOrOdd,		"EVEN?")
	FUNC0(OP_XisODD,	&XisEvenOrOdd,		"ODD?")
	FUNC0(OP_ENTRYP,	&op_entryp,		"ENTRY?")

	FUNC1(OP_TICKS,		&op_ticks,		"TICKS")
	FUNC1(OP_VOLTAGE,	&op_voltage,		"BATT")

	FUNC0(OP_QUAD,		XNIL(QUAD),		"SLVQ")
	FUNC0(OP_NEXTPRIME,	XNIL(NEXTPRIME),	"NEXTP")
	FUNC0(OP_SETEUR,	XNIL(SETEUR),		"SETEUR")
	FUNC0(OP_SETUK,		XNIL(SETUK),		"SETUK")
	FUNC0(OP_SETUSA,	XNIL(SETUSA),		"SETUSA")
	FUNC0(OP_SETIND,	XNIL(SETIND),		"SETIND")
	FUNC0(OP_SETCHN,	XNIL(SETCHN),		"SETCHN")
	FUNC0(OP_SETJPN,	XNIL(SETJAP),		"SETJPN")
	FUNC0(OP_WHO,		XNIL(WHO),		"WHO")	

	FUNC0(OP_XEQALPHA,	&op_gtoalpha,		"XEQ\240")
	FUNC0(OP_GTOALPHA,	&op_gtoalpha,		"GTO\240")

	FUNC1(OP_ROUNDING,	&op_roundingmode,	"RM?")
	FUNC0(OP_SLOW,		&op_setspeed,		"SLOW")
	FUNC0(OP_FAST,		&op_setspeed,		"FAST")

	FUNC0(OP_TOP,		&isTop,			"TOP?")
	FUNC1(OP_GETBASE,	&get_base,		"IBASE?")
	FUNC1(OP_GETSIGN,	&get_sign_mode,		"SMODE?")
	FUNC0(OP_ISINT,		&check_mode,		"INTM?")
	FUNC0(OP_ISFLOAT,	&check_mode,		"REALM?")

	FUNC0(OP_Xeq_pos0,	&check_zero,		"x=+0?")
	FUNC0(OP_Xeq_neg0,	&check_zero,		"x=-0?")

#ifdef MATRIX_ROWOPS
	FN_I0(OP_MAT_ROW_SWAP,	&matrix_rowops,		"MROW\027")
	FN_I0(OP_MAT_ROW_MUL,	&matrix_rowops,		"MROW\034")
	FN_I0(OP_MAT_ROW_GADD,	&matrix_rowops,		"MROW+\034")
#endif
	FN_I0(OP_MAT_CHECK_SQUARE, &matrix_is_square,	"M.SQR?")
	FN_I0(OP_MAT_INVERSE,	&matrix_inverse,	"M\235")
#ifdef SILLY_MATRIX_SUPPORT
	FN_I0(OP_MAT_ZERO,	&matrix_create,		"M.ZERO")
	FN_I0(OP_MAT_IDENT,	&matrix_create,		"M.IDEN")
#endif
	FUNC0(OP_POPLR,		&cmdlpop,		"PopLR")
	FUNC1(OP_MEMQ,		&get_mem,		"MEM?")
	FUNC1(OP_LOCRQ,		&get_mem,		"LocR?")
	FUNC1(OP_REGSQ,		&get_mem,		"REGS?")
	FUNC1(OP_FLASHQ,	&get_mem,		"FLASH?")

#ifdef INCLUDE_USER_IO
	FUNC0(OP_SEND1,		&send_byte,		"SEND1")
	FUNC0(OP_SERIAL_OPEN,	&serial_open,		"SOPEN")
	FUNC0(OP_SERIAL_CLOSE,	&serial_close,		"SCLOSE")
	FUNC0(OP_ALPHASEND,	&send_alpha,		"SEND\240")
	FUNC0(OP_ALPHARECV,	&recv_alpha,		"RECV\240")
#endif
	FUNC0(OP_SENDP,		&send_program,		"SENDP")
	FUNC0(OP_SENDR,		&send_registers,	"SENDR")
	FUNC0(OP_SENDsigma,	&send_sigma,		"SEND\221")
	FUNC0(OP_SENDA,		&send_all,		"SENDA")

	FUNC0(OP_RECV,		&recv_any,		"RECV")
	FUNC0(OP_SAVE,		&flash_backup,		"SAVE")
	FUNC0(OP_LOAD,		&flash_restore,		"LOAD")
	FUNC0(OP_LOADST,	&load_state,		"LOADSS")
	FUNC0(OP_LOADP,		&load_program,		"LOADP")
	FUNC0(OP_PRCL,		&recall_program,	"PRCL")
	FUNC0(OP_PSTO,		&store_program,		"PSTO")

	FUNC0(OP_LOADR,		&load_registers,	"LOADR")
	FUNC0(OP_LOADsigma,	&load_sigma,		"LOAD\221")

	FUNC0(OP_DBLON,		&op_double,		"DBLON")
	FUNC0(OP_DBLOFF,	&op_double,		"DBLOFF")
	FUNC0(OP_ISDBL,		&check_dblmode,		"DBL?")

	FN_I2(OP_cmplxI,	XNIL(CPX_I),		"\024i")

	FUNC0(OP_DATE_TO,	XNIL(DATE_TO),		"DATE\015")

	FUNC0(OP_DOTPROD,	XNIL(cpx_DOT),		"\024DOT")
	FUNC0(OP_CROSSPROD,	XNIL(cpx_CROSS),	"\024CROSS")

	/* INFRARED commands */
	FUNC0(OP_PRINT_PGM,	IRN(print_program),	"\222PROG")
	FUNC0(OP_PRINT_REGS,	IRN(print_registers),	"\222REGS")
	FUNC0(OP_PRINT_STACK,	IRN(print_registers),	"\222STK")
	FUNC0(OP_PRINT_SIGMA,	IRN(print_sigma),	"\222\221")
	FUNC0(OP_PRINT_ALPHA,	IRN(print_alpha),	"\222\240")
	FUNC0(OP_PRINT_ALPHA_NOADV, IRN(print_alpha),	"\222\240+")
	FUNC0(OP_PRINT_ALPHA_JUST,  IRN(print_alpha),	"\222+\240")
	FUNC0(OP_PRINT_ADV,	IRN(print_lf),		"\222ADV")
	/* end of INFRARED commands */

	FUNC0(OP_QUERY_XTAL,	&op_query_xtal,		"XTAL?")
	FUNC0(OP_QUERY_PRINT,	&op_query_print,	"\222?")

#ifdef INCLUDE_STOPWATCH
	FUNC0(OP_STOPWATCH,	&stopwatch,		"STOPW")
#endif
#ifdef _DEBUG
	FUNC0(OP_DEBUG,		XNIL(DBG),		"DBG")
#endif

#undef FUNC
#undef FUNC0
#undef FUNC1
#undef FUNC2
#undef FN_I0
#undef FN_I1
#undef FN_I2
};


#ifdef COMPILE_CATALOGUES
#define allCMD(name, func, limit, nm, ind, autoind, reg, stk, loc, cpx, lbl, flag)				\
	{ #func, (limit) - 1, ind, autoind, reg, stk, loc, cpx, lbl, flag, nm },
#elif DEBUG
#define allCMD(name, func, limit, nm, ind, autoind, reg, stk, loc, cpx, lbl, flag)			\
	{ name, func, (limit) - 1, ind, autoind, reg, stk, loc, cpx, lbl, flag, nm },
#elif COMMANDS_PASS == 1
#define allCMD(name, func, limit, nm, ind, autoind, reg, stk, loc, cpx, lbl, flag)				\
	{ 0xaa55, (limit) - 1, ind, autoind, reg, stk, loc, cpx, lbl, flag, nm },
#else
#define allCMD(name, func, limit, nm, ind, autoind, reg, stk, loc, cpx, lbl, flag)				\
	{ func, (limit) - 1, ind, autoind, reg, stk, loc, cpx, lbl, flag, nm },
#endif
                                                                       //  i ai  r  s  l  c  lb f
#define CMD(n, f, lim, nm)	allCMD(n, f, lim,                      nm, 1, 0, 0, 0, 0, 0, 0, 0)
#define CMDnoI(n, f, lim, nm)	allCMD(n, f, lim,                      nm, 0, 0, 0, 0, 0, 0, 0, 0)
#define CMDreg(n, f, nm)	allCMD(n, f, TOPREALREG,               nm, 1, 0, 1, 0, 1, 0, 0, 0)
#define CMDstk(n, f, nm)	allCMD(n, f, NUMREG+MAX_LOCAL,         nm, 1, 0, 1, 1, 1, 0, 0, 0)
#define CMDautoI(n, f, nm)	allCMD(n, f, NUMREG+MAX_LOCAL_DIRECT,  nm, 0, 1, 1, 1, 1, 0, 0, 0)
#define CMDcstk(n, f, nm)	allCMD(n, f, NUMREG+MAX_LOCAL-1,       nm, 1, 0, 1, 1, 1, 1, 0, 0)
#define CMDregnL(n, f, nm)	allCMD(n, f, TOPREALREG,               nm, 1, 0, 1, 0, 0, 0, 0, 0)
#define CMDstknL(n, f, nm)	allCMD(n, f, NUMREG,                   nm, 1, 0, 0, 1, 0, 0, 0, 0)
#define CMDcstknL(n, f, nm)	allCMD(n, f, NUMREG-1,                 nm, 1, 0, 0, 1, 0, 1, 0, 0)
#define CMDlbl(n, f, nm)	allCMD(n, f, NUMLBL,                   nm, 1, 0, 0, 0, 0, 0, 1, 0)
#define CMDlblnI(n, f, nm)	allCMD(n, f, NUMLBL,                   nm, 0, 0, 0, 0, 0, 0, 1, 0)
#define CMDflg(n, f, nm)	allCMD(n, f, NUMFLG+16,		       nm, 1, 0, 0, 1, 1, 0, 0, 1)

#if COMMANDS_PASS == 2
CMDTAB const struct argcmd_cmdtab argcmds_ct[ NUM_RARG ] = {
#else
const struct argcmd argcmds[ NUM_RARG ] = {
#endif
	CMDnoI(RARG_CONST,	&cmdconst,	NUM_CONSTS,		"#")
	CMDnoI(RARG_CONST_CMPLX,&cmdconst,	NUM_CONSTS,		"\024#")
	CMD(RARG_ERROR,		&cmderr,	MAX_ERROR,		"ERR")
	CMDstk(RARG_STO, 	&cmdsto,				"STO")
	CMDstk(RARG_STO_PL, 	&cmdsto,				"STO+")
	CMDstk(RARG_STO_MI, 	&cmdsto,				"STO-")
	CMDstk(RARG_STO_MU, 	&cmdsto,				"STO\034")
	CMDstk(RARG_STO_DV, 	&cmdsto,				"STO/")
	CMDstk(RARG_STO_MIN,	&cmdsto,				"STO\017")
	CMDstk(RARG_STO_MAX,	&cmdsto,				"STO\020")
	CMDstk(RARG_RCL, 	&cmdrcl,				"RCL")
	CMDstk(RARG_RCL_PL, 	&cmdrcl,				"RCL+")
	CMDstk(RARG_RCL_MI, 	&cmdrcl,				"RCL-")
	CMDstk(RARG_RCL_MU, 	&cmdrcl,				"RCL\034")
	CMDstk(RARG_RCL_DV, 	&cmdrcl,				"RCL/")
	CMDstk(RARG_RCL_MIN,	&cmdrcl,				"RCL\017")
	CMDstk(RARG_RCL_MAX,	&cmdrcl,				"RCL\020")
	CMDstk(RARG_SWAPX,	&cmdswap,				"x\027")
	CMDstk(RARG_SWAPY,	&cmdswap,				"y\027")
	CMDstk(RARG_SWAPZ,	&cmdswap,				"z\027")
	CMDstk(RARG_SWAPT,	&cmdswap,				"t\027")
	CMDcstk(RARG_CSTO, 	&cmdcsto,				"\024STO")
	CMDcstk(RARG_CSTO_PL, 	&cmdcsto,				"\024STO+")
	CMDcstk(RARG_CSTO_MI, 	&cmdcsto,				"\024STO-")
	CMDcstk(RARG_CSTO_MU, 	&cmdcsto,				"\024STO\034")
	CMDcstk(RARG_CSTO_DV, 	&cmdcsto,				"\024STO/")
	CMDcstk(RARG_CRCL, 	&cmdcrcl,				"\024RCL")
	CMDcstk(RARG_CRCL_PL, 	&cmdcrcl,				"\024RCL+")
	CMDcstk(RARG_CRCL_MI, 	&cmdcrcl,				"\024RCL-")
	CMDcstk(RARG_CRCL_MU, 	&cmdcrcl,				"\024RCL\034")
	CMDcstk(RARG_CRCL_DV, 	&cmdcrcl,				"\024RCL/")
	CMDcstk(RARG_CSWAPX,	&cmdswap,				"\024x\027")
	CMDcstk(RARG_CSWAPZ,	&cmdswap,				"\024z\027")
	CMDstk(RARG_VIEW,	&cmdview,				"VIEW")
	CMDstk(RARG_STOSTK,	&cmdstostk,				"STOS")
	CMDstk(RARG_RCLSTK,	&cmdrclstk,				"RCLS")
	CMDnoI(RARG_ALPHA,	&cmdalpha,	0,			"")
	CMDstk(RARG_AREG,	&alpha_reg,				"\240RC#")
	CMDstk(RARG_ASTO,	&alpha_sto,				"\240STO")
	CMDstk(RARG_ARCL,	&alpha_rcl,				"\240RCL")
	CMDstk(RARG_AIP,	&alpha_ip,				"\240IP")
	CMD(RARG_ALRL,		&alpha_shift_l,	NUMALPHA,		"\240RL")
	CMD(RARG_ALRR,		&alpha_rot_r,	NUMALPHA,		"\240RR")
	CMD(RARG_ALSL,		&alpha_shift_l,	NUMALPHA+1,		"\240SL")
	CMD(RARG_ALSR,		&alpha_shift_r,	NUMALPHA+1,		"\240SR")
	CMDstk(RARG_TEST_EQ,	&cmdtest,				"x=?")
	CMDstk(RARG_TEST_NE,	&cmdtest,				"x\013?")
	CMDstk(RARG_TEST_APX,	&cmdtest,				"x\035?")
	CMDstk(RARG_TEST_LT,	&cmdtest,				"x<?")
	CMDstk(RARG_TEST_LE,	&cmdtest,				"x\011?")
	CMDstk(RARG_TEST_GT,	&cmdtest,				"x>?")
	CMDstk(RARG_TEST_GE,	&cmdtest,				"x\012?")
	CMDcstk(RARG_TEST_ZEQ,	&cmdztest,				"\024x=?")
	CMDcstk(RARG_TEST_ZNE,	&cmdztest,				"\024x\013?")
	CMDnoI(RARG_SKIP,	&cmdskip,	256,			"SKIP")
	CMDnoI(RARG_BACK,	&cmdback,	256,			"BACK")
	CMDnoI(RARG_BSF,	&cmdskip,	256,			"BSRF")
	CMDnoI(RARG_BSB,	&cmdback,	256,			"BSRB")

	CMDstk(RARG_DSE,	&cmdloop,				"DSE")
	CMDstk(RARG_ISG,	&cmdloop,				"ISG")
	CMDstk(RARG_DSL,	&cmdloop,				"DSL")
	CMDstk(RARG_ISE,	&cmdloop,				"ISE")
	CMDstk(RARG_DSZ,	&cmdloopz,				"DSZ")
	CMDstk(RARG_ISZ,	&cmdloopz,				"ISZ")
	CMDstk(RARG_DEC,	&cmdlincdec,				"DEC")
	CMDstk(RARG_INC,	&cmdlincdec,				"INC")

	CMDlblnI(RARG_LBL,	NOFN,					"LBL")
	CMDlbl(RARG_LBLP,	&cmdlblp,				"LBL?")
	CMDlbl(RARG_XEQ,	&cmdgto,				"XEQ")
	CMDlbl(RARG_GTO,	&cmdgto,				"GTO")

	CMDlbl(RARG_SUM,	XARG(SIGMA),				"\221")
	CMDlbl(RARG_PROD,	XARG(PRODUCT),				"\217")
	CMDlbl(RARG_SOLVE,	XARG(SOLVE),				"SLV")
	CMDlbl(RARG_DERIV,	XARG(DERIV),				"f'(x)")
	CMDlbl(RARG_2DERIV,	XARG(2DERIV),				"f\"(x)")
	CMDlbl(RARG_INTG,	XARG(INTEGRATE),			"\004")

	CMD(RARG_STD,		&cmddisp,	DISPLAY_DIGITS,		"ALL")
	CMD(RARG_FIX,		&cmddisp,	DISPLAY_DIGITS,		"FIX")
	CMD(RARG_SCI,		&cmddisp,	DISPLAY_DIGITS,		"SCI")
	CMD(RARG_ENG,		&cmddisp,	DISPLAY_DIGITS,		"ENG")
	CMD(RARG_DISP,		&cmddisp,	DISPLAY_DIGITS,		"DISP")

	CMDflg(RARG_SF,		&cmdflag,				"SF")
	CMDflg(RARG_CF,		&cmdflag,				"CF")
	CMDflg(RARG_FF,		&cmdflag,				"FF")
	CMDflg(RARG_FS,		&cmdflag,				"FS?")
	CMDflg(RARG_FC,		&cmdflag,				"FC?")
	CMDflg(RARG_FSC,	&cmdflag,				"FS?C")
	CMDflg(RARG_FSS,	&cmdflag,				"FS?S")
	CMDflg(RARG_FSF,	&cmdflag,				"FS?F")
	CMDflg(RARG_FCC,	&cmdflag,				"FC?C")
	CMDflg(RARG_FCS,	&cmdflag,				"FC?S")
	CMDflg(RARG_FCF,	&cmdflag,				"FC?F")
	CMD(RARG_WS,		&intws,		MAX_WORD_SIZE+1,	"WSIZE")
	CMD(RARG_RL,		&introt,	MAX_WORD_SIZE,		"RL")
	CMD(RARG_RR,		&introt,	MAX_WORD_SIZE,		"RR")
	CMD(RARG_RLC,		&introt,	MAX_WORD_SIZE+1,	"RLC")
	CMD(RARG_RRC,		&introt,	MAX_WORD_SIZE+1,	"RRC")
	CMD(RARG_SL,		&introt,	MAX_WORD_SIZE+1,	"SL")
	CMD(RARG_SR,		&introt,	MAX_WORD_SIZE+1,	"SR")
	CMD(RARG_ASR,		&introt,	MAX_WORD_SIZE+1,	"ASR")
	CMD(RARG_SB,		&intbits,	MAX_WORD_SIZE,		"SB")
	CMD(RARG_CB,		&intbits,	MAX_WORD_SIZE,		"CB")
	CMD(RARG_FB,		&intbits,	MAX_WORD_SIZE,		"FB")
	CMD(RARG_BS,		&intbits,	MAX_WORD_SIZE,		"BS?")
	CMD(RARG_BC,		&intbits,	MAX_WORD_SIZE,		"BC?")
	CMD(RARG_MASKL,		&intmsks,	MAX_WORD_SIZE+1,	"MASKL")
	CMD(RARG_MASKR,		&intmsks,	MAX_WORD_SIZE+1,	"MASKR")
	CMD(RARG_BASE,		&set_int_base,	17,			"BASE")

	CMDnoI(RARG_CONV,	&cmdconv,	NUM_CONSTS_CONV*2,	"conv")

	CMD(RARG_PAUSE,		&cmdpause,	100,			"PSE")
	CMDstk(RARG_KEY,	&cmdkeyp,				"KEY?")

	CMDstk(RARG_ALPHAXEQ,	&cmdalphagto,				"\240XEQ")
	CMDstk(RARG_ALPHAGTO,	&cmdalphagto,				"\240GTO")

#ifdef INCLUDE_FLASH_RECALL
	CMDstknL(RARG_FLRCL, 	  &cmdflashrcl,				"RCF")
	CMDstknL(RARG_FLRCL_PL,   &cmdflashrcl,				"RCF+")
	CMDstknL(RARG_FLRCL_MI,   &cmdflashrcl,				"RCF-")
	CMDstknL(RARG_FLRCL_MU,   &cmdflashrcl,				"RCF\034")
	CMDstknL(RARG_FLRCL_DV,   &cmdflashrcl,				"RCF/")
	CMDstknL(RARG_FLRCL_MIN,  &cmdflashrcl,				"RCF\017")
	CMDstknL(RARG_FLRCL_MAX,  &cmdflashrcl,				"RCF\020")
	CMDcstknL(RARG_FLCRCL, 	  &cmdflashcrcl,			"\024RCF")
	CMDcstknL(RARG_FLCRCL_PL, &cmdflashcrcl,			"\024RCF+")
	CMDcstknL(RARG_FLCRCL_MI, &cmdflashcrcl,			"\024RCF-")
	CMDcstknL(RARG_FLCRCL_MU, &cmdflashcrcl,			"\024RCF\034")
	CMDcstknL(RARG_FLCRCL_DV, &cmdflashcrcl,			"\024RCF/")
#endif
	CMD(RARG_SLD,		&op_shift_digit, 256,			"SDL")
	CMD(RARG_SRD,		&op_shift_digit, 256,			"SDR")

	CMDstk(RARG_VIEW_REG,	&alpha_view_reg,			"VW\240+")
	CMD(RARG_ROUNDING,	&rarg_roundingmode, DEC_ROUND_MAX,	"RM")
	CMD(RARG_ROUND,		&rarg_round,	35,			"RSD")
	CMD(RARG_ROUND_DEC,	&rarg_round,	100,			"RDP")

#ifdef INCLUDE_USER_MODE
	CMDstk(RARG_STOM,	&cmdsavem,				"STOM")
	CMDstk(RARG_RCLM,	&cmdrestm,				"RCLM")
#endif
	CMDautoI(RARG_PUTKEY,	&cmdputkey,				"PUTK")
	CMDautoI(RARG_KEYTYPE,	&cmdkeytype,				"KTP?")

	CMD(RARG_MESSAGE,	&cmdmsg,	MAX_ERROR,		"MSG")
	CMD(RARG_LOCR,		&cmdlocr,	MAX_LOCAL + 1,		"LocR")
	CMD(RARG_REGS,		&cmdregs,	TOPREALREG + 1,		"REGS")

	CMDstk(RARG_iRCL,	&cmdircl,				"iRCL")
	CMDstk(RARG_sRCL,	&cmdrrcl,				"sRCL")
	CMDstk(RARG_dRCL,	&cmdrrcl,				"dRCL")
	CMD(RARG_MODE_SET,	&cmdmode,	64,			"xMSET")
	CMD(RARG_MODE_CLEAR,	&cmdmode,	64,			"xMCLR")

	CMDnoI(RARG_XROM_IN,	&cmdxin,	256,			"xIN")
	CMDnoI(RARG_XROM_OUT,	&cmdxout,	256,			"xOUT")
#ifdef XROM_RARG_COMMANDS
	CMDstk(RARG_XROM_ARG,	&cmdxarg,				"xARG")
#endif
	CMDnoI(RARG_CONVERGED,	&cmdconverged,	32,			"CNVG?")
	CMDnoI(RARG_SHUFFLE,	&cmdshuffle,	256,			"\027")

	CMDnoI(RARG_INTNUM,	  &cmdconst,	256,			"#")
	CMDnoI(RARG_INTNUM_CMPLX, &cmdconst,	256,			"\024#")
#ifdef INCLUDE_INDIRECT_CONSTS
	CMD(RARG_IND_CONST,	  &cmdconst,	NUM_CONSTS,		"CNST")
	CMD(RARG_IND_CONST_CMPLX, &cmdconst,	NUM_CONSTS,		"\024CNST")
#endif
	/* INFRARED commands */
	CMDstk(RARG_PRINT_REG,	IRA(cmdprintreg),			"\222r")
	CMD(RARG_PRINT_BYTE,	IRA(cmdprint),	256,			"\222#")
	CMD(RARG_PRINT_CHAR,	IRA(cmdprint),	256,			"\222CHR")
	CMD(RARG_PRINT_TAB,	IRA(cmdprint),	166,			"\222TAB")
	CMD(RARG_PMODE,		IRA(cmdprintmode),  4,			"\222MODE")
	CMD(RARG_PDELAY,	IRA(cmdprintmode),  32,			"\222DLAY")
	/* end of INFRARED commands */

	// Only the first of this group is used in XROM
	CMDautoI(RARG_CASE,	&cmdskip,				"CASE")
#ifdef INCLUDE_INDIRECT_BRANCHES
	CMD(RARG_iBACK,		&cmdback,				"iBACK")
	CMD(RARG_iBSF,		&cmdskip,				"iBSRF")
	CMD(RARG_iBSB,		&cmdback,				"iBSRB")
#endif

#undef CMDlbl
#undef CMDlblnI
#undef CMDnoI
#undef CMDstk
#undef CMD
#undef allCMD
};


#ifdef COMPILE_CATALOGUES
#define CMD(name, func, nm)			\
	{ #func, nm },
#elif DEBUG
#define CMD(name, func, nm)			\
	{ name, func, nm },
#elif COMMANDS_PASS == 1
#define CMD(name, func, nm)			\
	{ 0xaa55, nm },
#else
#define CMD(name, func, nm)			\
	{ func, nm },
#endif

#if COMMANDS_PASS == 2
CMDTAB const struct multicmd_cmdtab multicmds_ct[ NUM_MULTI ] = {
#else
const struct multicmd multicmds[ NUM_MULTI ] = {
#endif
	CMD(DBL_LBL,	NOFN,				"LBL")
	CMD(DBL_LBLP,	&cmdmultilblp,			"LBL?")
	CMD(DBL_XEQ,	&cmdmultigto,			"XEQ")
	CMD(DBL_GTO,	&cmdmultigto,			"GTO")
	CMD(DBL_SUM,	XMULTI(SIGMA),			"\221")
	CMD(DBL_PROD,	XMULTI(PRODUCT),		"\217")
	CMD(DBL_SOLVE,	XMULTI(SOLVE),			"SLV")
	CMD(DBL_DERIV,	XMULTI(DERIV),			"f'(x)")
	CMD(DBL_2DERIV,	XMULTI(2DERIV),			"f\"(x)")
	CMD(DBL_INTG,	XMULTI(INTEGRATE),		"\004")
	CMD(DBL_ALPHA,	&multialpha,			"\240")
#undef CMD
};


/*
 *  We need to include the same file a second time with updated #defines.
 *  This will create the structures in the CMDTAB segment with all pointers filled in.
 */
#if COMMANDS_PASS == 1
#undef COMMANDS_PASS
#define COMMANDS_PASS 2
#include "commands.c"
#undef COMMANDS_PASS
#endif
