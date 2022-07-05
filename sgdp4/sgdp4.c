/* > sgdp4.c
 *
 * 1.00 around 1980 - Felix R. Hoots & Ronald L. Roehrich, from original
 *                    SDP4.FOR and SGP4.FOR
 *
 ************************************************************************
 *
 *     Made famous by the spacetrack report No.3:
 *     "Models for Propogation of NORAD Element Sets"
 *     Edited and subsequently distributed by Dr. T. S. Kelso.
 *
 ************************************************************************
 *
 *     This conversion by:
 *     Paul S. Crawford and Andrew R. Brooks
 *     Dundee University
 *
 *                          NOTE !
 *  This code is supplied "as is" and without warranty of any sort.
 *
 * (c) 1994-2004, Paul Crawford, Andrew Brooks
 *
 ************************************************************************
 *
 * 1.07 arb     Oct    1994 - Transcribed by arb Oct 1994 into 'C', then
 *                            modified to fit Dundee systems by psc.
 *
 * 1.08 psc Mon Nov  7 1994 - replaced original satpos.c with SGP4 model.
 *
 * 1.09 psc Wed Nov  9 1994 - Corrected a few minor translation errors after
 *                            testing with example two-line elements.
 *
 * 1.10 psc Mon Nov 21 1994 - A few optimising tweeks.
 *
 * 1.11 psc Wed Nov 30 1994 - No longer uses eloset() and minor error in the
 *                            SGP4 code corrected.
 *
 * 2.00 psc Tue Dec 13 1994 - arb discovered the archive.afit.af.mil FTP site
 *                            with the original FORTRAN code in machine form.
 *                            Tidied up and added support for the SDP4 model.
 *
 * 2.01 psc Fri Dec 23 1994 - Tested out the combined SGP4/SDP4 code against
 *                            the original FORTRAN versions.
 *
 * 2.02 psc Mon Jan 02 1995 - Few more tweeks and tidied up the
 *                            doccumentation for more general use.
 *
 * 3.00 psc Mon May 29 1995 - Cleaned up for general use & distrabution (to
 *                            remove Dundee specific features).
 *
 * 3.01 psc Mon Jan 12 2004 - Minor bug fix for day calculation.
 *
 * 3.02 psc Mon Jul 10 2006 - Added if(rk < SUIMM(1)) test for sub-orbital decay.
 *
 * 3.03 psc Sat Aug 05 2006 - Added trap for divide-by-zero when calculating xlcof.
 *
 */


#define SU_LOG_DOMAIN "sgdp4"

#include <sigutils/types.h>
#include <sigutils/log.h>

#include "sgdp4.h"

#define ECC_ZERO SUIMM(0)                    /* Zero eccentricity case ? */
#define ECC_ALL  SUIMM(1e-4)                  /* For all drag terms in GSFC case. */
#define ECC_EPS  SUIMM(1e-6)                  /* Too low for computing further drops. */
#define ECC_LIMIT_LOW (SUIMM(-1.0e-3))          /* Exit point for serious decaying of orbits. */
#define ECC_LIMIT_HIGH (SU_ASFLOAT(1.0 - ECC_EPS)) /* Too close to 1 */

#define EPS_COSIO (1.5e-12) /* Minimum divisor allowed for (...)/(1+cos(IO)) */

#define TOTHRD (2.0 / 3.0)

#if defined(SGDP4_SNGL) || 0
#define NR_EPS (SU_ASFLOAT(1.0e-6)) /* Minimum ~1e-6 min for float. */
#else
#define NR_EPS (SU_ASFLOAT(1.0e-12)) /* Minimum ~1e-14 for double. */
//#define NR_EPS  (SU_ASFLOAT(1.0e-14))    /* Minimum ~1e-14 for double. */
//#define NR_EPS  (SU_ASFLOAT(1.0e-8))    /* Minimum ~1e-14 for double. */
#endif

#define Q0  SUIMM(120)
#define S0  SUIMM(78)
#define XJ2 SUIMM(1.082616e-3)
#define XJ3 (SUIMM(-2.53881e-6))
#define XJ4 (SUIMM(-1.65597e-6))
#define XKMPER (6378.135) /* Km per earth radii */
#define XMNPDA (1440.0)    /* Minutes per day */
#define AE (1.0)          /* Earth radius in "chosen units". */

#if 0
/* Original code constants. */
#define XKE (0.743669161e-1)
#define CK2 (SUIMM(5.413080e-4))      /* (0.5 * XJ2 * AE * AE) */
#define CK4 (SUIMM(0.62098875e-6))    /* (-0.375 * XJ4 * AE * AE * AE * AE) */
#define QOMS2T (SUIMM(1.88027916e-9)) /* (SU_POWX((Q0 - S0)*AE/XKMPER, 4.0)) */
#define KS (SUIMM(1)1222928)        /* (AE * (1.0 + S0/XKMPER)) */
#else
/* GSFC improved coeficient resolution. */
#define XKE (SUIMM(7.43669161331734132e-2))
#define CK2 (SU_ASFLOAT(0.5 * XJ2 * AE * AE))
#define CK4 (SU_ASFLOAT(-0.375 * XJ4 * AE * AE * AE * AE))
#define QOMS2T (SUIMM(1.880279159015270643865e-9)) /* (SU_POWX((Q0 - S0)*AE/XKMPER, 4.0)) */
#define KS (SU_ASFLOAT(AE * (1.0 + S0 / XKMPER)))
#endif
static const SUFLOAT a3ovk2 = SU_ASFLOAT(-XJ3 / CK2 * (AE * AE * AE));

/* =======================================================================
   The init_sgdp4() function passes all of the required orbital elements to
   the sgdp4() function together with the pre-calculated constants. There is
   some basic error traps and the detemination of the orbital model is made.
   For near-earth satellites (xnodp < 225 minutes according to the NORAD
   classification) the SGP4 model is used, with truncated terms for low
   perigee heights when the drag terms are high. For deep-space satellites
   the SDP4 model is used and the deep-space terms initialised (a slow
   process). For orbits with an eccentricity of less than ECC_EPS the model
   reverts to a very basic circular model. This is not physically meaningfull
   but such a circluar orbit is not either! It is fast though.
   Callinr arguments:

   orb      : Input, structure with the orbital elements from NORAD 2-line
              element data in radian form.

   The return value indicates the orbital model used.
   ======================================================================= */

int 
sgdp4_ctx_init(sgdp4_ctx_t *self, orbit_t *orb)
{
  SUFLOAT theta2, theta4, xhdot1, x1m5th;
  SUFLOAT s4, del1, del0;
  SUFLOAT betao, betao2, coef, coef1;
  SUFLOAT etasq, eeta, qoms24;
  SUFLOAT pinvsq, tsi, psisq, c1sq;
  SUDOUBLE a0, a1, epoch;
  SUFLOAT temp0, temp1, temp2, temp3;
  long iday, iyear;

  /* Copy over elements. */
  /* Convert year to Gregorian with century as 1994 or 94 type ? */

  iyear = (long)orb->ep_year;

  /* Assume 0 and 100 both refer to 2000AD */
  if (iyear < 1960)
    iyear += (iyear < 60 ? 2000 : 1900);

  if (iyear < 1901 || iyear > 2099) {
    SU_ERROR("init_sgdp4: Satellite ep_year error %ld", iyear);
    self->imode = SGDP4_ERROR;
    return self->imode;
  }

  self->Isat = orb->satno;

  /* Compute days from 1st Jan 1900 (works 1901 to 2099 only). */
  iday = ((iyear - 1901) * 1461L) / 4L + 364L + 1L;
  self->SGDP4_jd0 = JD1900 + iday + (orb->ep_day - 1.0); /* Julian day number. */
  epoch = (iyear - 1900) * 1.0e3 + orb->ep_day; /* YYDDD.DDDD as from 2-line. */

#ifdef DEBUG
  SU_WARNING("Epoch = %f SGDP4_jd0 = %f\n", epoch, SGDP4_jd0);
#endif

  self->eo     = SU_ASFLOAT(orb->ecc);
  self->xno    = (double)orb->rev * TWOPI / XMNPDA; /* Radian / unit time. */
  self->xincl  = SU_ASFLOAT(orb->eqinc);
  self->xnodeo = SU_ASFLOAT(orb->ascn);
  self->omegao = SU_ASFLOAT(orb->argp);
  self->xmo    = SU_ASFLOAT(orb->mnan);
  self->bstar  = SU_ASFLOAT(orb->bstar);

  /* A few simple error checks here. */

  if (self->eo < SUIMM(0) || self->eo > ECC_LIMIT_HIGH)  {
    SU_ERROR(
      "init_sgdp4: Eccentricity out of range for %ld (%le)", 
      self->Isat, 
      (double)self->eo);
    self->imode = SGDP4_ERROR;
    return self->imode;
  }

  if (self->xno < .035 * TWOPI / XMNPDA || self->xno > 18. * TWOPI / XMNPDA) {
    SU_ERROR(
      "init_sgdp4: Mean motion out of range %ld (%le)", self->Isat, self->xno);
    self->imode = SGDP4_ERROR;
    return self->imode;
  }

  if (self->xincl < SUIMM(0) || self->xincl > PI) {
    SU_ERROR(
      "init_sgdp4: Equatorial inclination out of range %ld (%le)", 
      self->Isat, 
      SU_RAD2DEG(self->xincl));
    self->imode = SGDP4_ERROR;
    return self->imode;
  }

  /* Start the initialisation. */
  if (self->eo < ECC_ZERO)
    self->imode = SGDP4_ZERO_ECC; /* Special mode for "ideal" circular orbit. */
  else
    self->imode = SGDP4_NOT_INIT;

  /*
    Recover original mean motion (xnodp) and semimajor axis (aodp)
    from input elements.
    */

  SU_SINCOS(self->xincl, &self->sinIO, &self->cosIO);

  theta2        = self->cosIO * self->cosIO;
  theta4        = theta2 * theta2;
  self->x3thm1  = SUIMM(3) * theta2 - SUIMM(1);
  self->x1mth2  = SUIMM(1) - theta2;
  self->x7thm1  = SUIMM(7) * theta2 - SUIMM(1);

  a1            = SU_POWX(XKE / self->xno, TOTHRD);
  betao2        = SUIMM(1) - self->eo * self->eo;
  betao         = SU_SQRTX(betao2);
  temp0         = SU_ASFLOAT(1.5 * CK2) * self->x3thm1 / (betao * betao2);
  del1          = temp0 / (a1 * a1);
  a0            = 
    a1 * (1.0 - del1 * (1.0 / 3.0 + del1 * (1.0 + del1 * 134.0 / 81.0)));
  del0          = temp0 / (a0 * a0);
  self->xnodp   = self->xno / (1.0 + del0);
  self->aodp    = SU_ASFLOAT(a0 / (1.0 - del0));
  self->perigee = (self->aodp * (1.0 - self->eo) - AE) * XKMPER;
  self->apogee  = (self->aodp * (1.0 + self->eo) - AE) * XKMPER;
  self->period  = (TWOPI * 1440.0 / XMNPDA) / self->xnodp;

  if (self->perigee <= 0.0)  {
    SU_WARNING(
      "Satellite %ld sub-orbital (apogee = %.1f km, perigee = %.1f km)\n",
      self->Isat, 
      self->apogee, 
      self->perigee);
  }

  if (self->imode == SGDP4_ZERO_ECC)
    return self->imode;

  if (self->period >= 225.0 && self->Set_LS_zero < 2)
    self->imode = SGDP4_DEEP_NORM; /* Deep-Space model(s). */
  else if (self->perigee < 220.0)
    /*
     * For perigee less than 220 km the imode flag is set so the
     * equations are truncated to linear variation in sqrt A and
     * quadratic variation in mean anomaly. Also the c3 term, the
     * delta omega term and the delta m term are dropped.
     */
    self->imode = SGDP4_NEAR_SIMP; /* Near-space, simplified equations. */
  else
    self->imode = SGDP4_NEAR_NORM; /* Near-space, normal equations. */

  /* For perigee below 156 km the values of S and QOMS2T are altered */
  if (self->perigee < 156.0) {
    s4 = SU_ASFLOAT(self->perigee - 78.0);
    if (s4 < SUIMM(20)) {
      SU_WARNING("Very low s4 constant for sat %ld (perigee = %.2f)\n", self->Isat, self->perigee);
      s4 = SUIMM(20);
    }  else {
      SU_WARNING("Changing s4 constant for sat %ld (perigee = %.2f)\n", self->Isat, self->perigee);
    }

    qoms24 = POW4(SU_ASFLOAT((120.0 - s4) * (AE / XKMPER)));
    s4 = SU_ASFLOAT(s4 / XKMPER + AE);
  }  else {
    s4 = KS;
    qoms24 = QOMS2T;
  }

  pinvsq    = SUIMM(1) / (self->aodp * self->aodp * betao2 * betao2);
  tsi       = SUIMM(1) / (self->aodp - s4);
  self->eta = self->aodp * self->eo * tsi;
  etasq     = self->eta * self->eta;
  eeta      = self->eo * self->eta;
  psisq     = FABS(SUIMM(1) - etasq);
  coef      = qoms24 * POW4(tsi);
  coef1     = coef / POW(psisq, 3.5);

  self->c2  = coef1 
		* (SUFLOAT)self->xnodp 
		* (self->aodp 
			* (SUIMM(1) 
				+ SUIMM(1.5) * etasq 
				+ eeta * (SUIMM(4) + etasq)) 
				+ SU_ASFLOAT(0.75 * CK2) 
					* tsi / psisq 
					* self->x3thm1 
					* (SUIMM(8) + SUIMM(3) * etasq * (SUIMM(8) + etasq)));
  self->c1  = self->bstar * self->c2;
  self->c4  = SUIMM(2) 
    * (SUFLOAT)self->xnodp 
    * coef1 
    * self->aodp 
    * betao2 
    * (self->eta * (
        SUIMM(2) 
        + SUIMM(.5) * etasq) 
        + self->eo * (SUIMM(.5)   + SUIMM(2) * etasq)
        - SU_ASFLOAT(2.0 * CK2) * tsi 
          / (self->aodp * psisq) * (
            SUIMM(-3.0) 
              * self->x3thm1 
              * (SUIMM(1) 
                - SUIMM(2) * eeta 
                + etasq * (SUIMM(1.5) - SUIMM(.5) * eeta)) 
                + SUIMM(0.75) * self->x1mth2 
                  * (SUIMM(2) * etasq - eeta * (SUIMM(1) + etasq)) 
                  * SU_COSX(SUIMM(2) * self->omegao)));

  self->c5 = self->c3 = self->omgcof = SUIMM(.0);

  /* BSTAR drag terms for normal near-space 'normal' model only. */
  if (self->imode == SGDP4_NEAR_NORM) {
    self->c5 = SUIMM(2) 
      * coef1 
      * self->aodp 
      * betao2 
      * (SUIMM(1) + SUIMM(2.75) * (etasq + eeta) + eeta * etasq);

    if (self->eo > ECC_ALL)
      self->c3 = coef 
        * tsi 
        * a3ovk2 
        * (SUFLOAT)self->xnodp * (SUFLOAT)AE * self->sinIO / self->eo;

    self->omgcof = self->bstar * self->c3 * SU_COSX(self->omegao);
  }

  temp1 = SU_ASFLOAT(3.0 * CK2) * pinvsq * (SUFLOAT)self->xnodp;
  temp2 = temp1 * CK2 * pinvsq;
  temp3 = SU_ASFLOAT(1.25 * CK4) * pinvsq * pinvsq * (SUFLOAT)self->xnodp;

  self->xmdot = self->xnodp + (
    SUIMM(.5) * temp1 * betao * self->x3thm1 
    + SUIMM(.0625) * temp2 * betao * (
        SUIMM(13) 
        - SUIMM(78) * theta2 + SUIMM(137) * theta4));

  x1m5th = SUIMM(1) - SUIMM(5) * theta2;

  self->omgdot = 
    SUIMM(-0.5) * temp1 * x1m5th 
    + SUIMM(.0625) * temp2 * (SUIMM(7) 
        - SUIMM(114) * theta2 
        + SUIMM(395) * theta4) 
    + temp3 * (SUIMM(3) 
      - SUIMM(36) * theta2 
      + SUIMM(49) * theta4);

  xhdot1 = -temp1 * self->cosIO;
  self->xnodot = xhdot1 
    + (SUIMM(.5) * temp2 * (SUIMM(4) - SUIMM(19) * theta2) 
    + SUIMM(2) 
      * temp3  
      * (SUIMM(3) - SUIMM(7) * theta2)) 
      *  self->cosIO;

  self->xmcof = SUIMM(.0);

  if (self->eo > ECC_ALL)
    self->xmcof = SU_ASFLOAT(-TOTHRD * AE) * coef * self->bstar / eeta;

  self->xnodcf = SUIMM(3.5) * betao2 * xhdot1 * self->c1;
  self->t2cof = SUIMM(1.5) * self->c1;

  /* Check for possible divide-by-zero for X/(1+cosIO) when calculating xlcof */
  temp0 = SUIMM(1) + self->cosIO;

  if (fabs(temp0) < EPS_COSIO)
    temp0 = (SUFLOAT)SIGN2(EPS_COSIO, temp0);

  self->xlcof = SUIMM(0.125) 
    * a3ovk2 
    * self->sinIO 
    * (SUIMM(3) + SUIMM(5) * self->cosIO) / temp0;

  self->aycof = SUIMM(.25) * a3ovk2 * self->sinIO;

  SU_SINCOS(self->xmo, &self->sinXMO, &self->cosXMO);
  self->delmo = CUBE(SUIMM(1) + self->eta * self->cosXMO);

  switch (self->imode) {
    case SGDP4_NEAR_NORM:
      c1sq        = self->c1 * self->c1;
      self->d2    = SUIMM(4) * self->aodp * tsi * c1sq;
      temp0       = self->d2 * tsi * self->c1 / SUIMM(3);
      self->d3    = (SUIMM(17) * self->aodp + s4) * temp0;
      self->d4    = SUIMM(.5) 
        * temp0 
        * self->aodp 
        * tsi 
        * (SUIMM(221) * self->aodp + SUIMM(31) * s4)
        * self->c1;

      self->t3cof = self->d2 + SUIMM(2) * c1sq;
      self->t4cof = SUIMM(.25) * (SUIMM(3) * self->d3 
        + self->c1 * (SUIMM(12) * self->d2  + SUIMM(10) * c1sq));
      self->t5cof = SUIMM(0.2) 
        * (SUIMM(3) * self->d4 
          + SUIMM(12) * self->c1 * self->d3 
          +  SUIMM(6) * self->d2 * self->d2 
          + SUIMM(15) * c1sq * (SUIMM(2) * self->d2 + c1sq));
      break;

#ifndef NO_DEEP_SPACE
    case SGDP4_DEEP_NORM:
      self->imode = sgdp4_ctx_init_deep(self, epoch);
      break;
#endif
    default:
      SU_ERROR("Unsupported mode %d\n", self->imode);
  }

  return self->imode;
}

/* =======================================================================
   The sgdp4() function computes the Keplarian elements that describe the
   position and velocity of the satellite. Depending on the initialisation
   (and the compile options) the deep-space perturbations are also included
   allowing sensible predictions for most satellites. These output elements
   can be transformed to Earth Centered Inertial coordinates (X-Y-Z) and/or
   to sub-satellite latitude and longitude as required. The terms for the
   velocity solution are often not required so the 'withvel' flag can be used
   to by-pass that step as required. This function is normally called through
   another since the input 'tsince' is the time from epoch.
   Calling arguments:

   tsince   : Input, time from epoch (minutes).

   withvel  : Input, non-zero if velocity terms required.

   kep      : Output, the Keplarian position / velocity of the satellite.

   The return value indicates the orbital mode used.

   ======================================================================= */

int 
sgdp4_ctx_compute(sgdp4_ctx_t *self, double tsince, int withvel, kep_t *kep)
{
  SUFLOAT rk, uk, xnodek, xinck, em, xinc;
  SUFLOAT xnode, delm, axn, ayn, omega;
  SUFLOAT capu, epw, elsq, invR, beta2, betal;
  SUFLOAT sinu, sin2u, cosu, cos2u;
  SUFLOAT a, e, r, u, pl;
  SUFLOAT sinEPW, cosEPW, sinOMG, cosOMG;
  SUDOUBLE xmp, xl, xlt;
	SUFLOAT theta2;
  const int MAXI = 10;
	SUDOUBLE nr, f, df;

#ifndef NO_DEEP_SPACE
  SUDOUBLE xn, xmam;
#endif /* !NO_DEEP_SPACE */

  SUFLOAT esinE, ecosE, maxnr;
  SUFLOAT temp0, temp1, temp2, temp3;
  SUFLOAT tempa, tempe, templ;
  int ii;

#ifdef SGDP4_SNGL
  SUFLOAT ts = (SUFLOAT)tsince;
#else
#define ts tsince
#endif /* ! SGDP4_SNGL */

  /* Update for secular gravity and atmospheric drag. */

  em = self->eo;
  xinc = self->xincl;

  xmp = (double)self->xmo + self->xmdot * tsince;
  xnode = self->xnodeo + ts * (self->xnodot + ts * self->xnodcf);
  omega = self->omegao + self->omgdot * ts;

  switch (self->imode) {
  	case SGDP4_ZERO_ECC:
			/* Not a "SUFLOAT" orbit but OK for fast computation searches. */
			kep->smjaxs   = kep->radius = (double)self->aodp * XKMPER / AE;
			kep->theta    = fmod(PI + self->xnodp * tsince, TWOPI) - PI;
			kep->eqinc    = (double)self->xincl;
			kep->ascn     = self->xnodeo;

			kep->argp     = 0;
			kep->ecc      = 0;
			kep->rfdotk   = 0;

			if (withvel)
				kep->rfdotk = 
					self->aodp * self->xnodp * (XKMPER / AE * XMNPDA / 86400.0); /* For km/sec */
			else
				kep->rfdotk = 0;

			return self->imode;

  case SGDP4_NEAR_SIMP:
    tempa = SUIMM(1) - ts * self->c1;
    tempe = self->bstar * ts * self->c4;
    templ = ts * ts * self->t2cof;
    a     = self->aodp * tempa * tempa;
    e     = em - tempe;
    xl    = xmp + omega + xnode + self->xnodp * templ;
    break;

  case SGDP4_NEAR_NORM:
    delm   = self->xmcof 
			* (CUBE(SUIMM(1) + self->eta * SU_COSX(xmp)) - self->delmo);
    temp0  = ts * self->omgcof + delm;
    xmp   += (double)temp0;
    omega -= temp0;
    tempa  = SUIMM(1) 
			- (ts * (self->c1 
				+ ts * (self->d2 
					+ ts * (self->d3 
						+ ts * self->d4))));

    tempe  = self->bstar 
			* (self->c4 * ts + self->c5 * (SU_SINX(xmp) - self->sinXMO));
			
    templ  = ts * ts 
			* (self->t2cof 
				+ ts * (self->t3cof 
					+ ts * (self->t4cof 
						+ ts * self->t5cof)));

    /* xmp   += (double)temp0; See above */

    a  = self->aodp * tempa * tempa;
    e  = em - tempe;
    xl = xmp + omega + xnode + self->xnodp * templ;
    break;

#ifndef NO_DEEP_SPACE
  case SGDP4_DEEP_NORM:
  case SGDP4_DEEP_RESN:
  case SGDP4_DEEP_SYNC:
    tempa = SUIMM(1) - ts * self->c1;
    tempe = self->bstar * ts * self->c4;
    templ = ts * ts * self->t2cof;
    xn    = self->xnodp;

    sgdp4_ctx_init_deep_secular(self, &xmp, &omega, &xnode, &em, &xinc, &xn, tsince);

    a     = POW(XKE / xn, TOTHRD) * tempa * tempa;
    e     = em - tempe;
    xmam  = xmp + self->xnodp * templ;

    sgdp4_ctx_init_deep_periodic(self, &e, &xinc, &omega, &xnode, &xmam, tsince);

    if (xinc < SUIMM(.0)) {
      xinc = (-xinc);
      xnode += (SUFLOAT)PI;
      omega -= (SUFLOAT)PI;
    }

    xl = xmam + omega + xnode;

    /* Re-compute the perturbed values. */
    SU_SINCOS(xinc, &self->sinIO, &self->cosIO);

		theta2 = self->cosIO * self->cosIO;

		self->x3thm1 = SUIMM(3) * theta2 - SUIMM(1);
		self->x1mth2 = SUIMM(1) - theta2;
		self->x7thm1 = SUIMM(7) * theta2 - SUIMM(1);

		/* Check for possible divide-by-zero for X/(1+cosIO) when calculating xlcof */
		temp0 = SUIMM(1) + self->cosIO;

		if (fabs(temp0) < EPS_COSIO)
			temp0 = (SUFLOAT)SIGN2(EPS_COSIO, temp0);

		self->xlcof = SUIMM(0.125) 
			* a3ovk2 
			* self->sinIO 
			*	(SUIMM(3) + SUIMM(5) * self->cosIO) / temp0;
		self->aycof = SUIMM(.25) * a3ovk2 * self->sinIO;
    break;

#endif /* ! NO_DEEP_SPACE */

  default:
    SU_ERROR("sgdp4: Orbit not initialised");
    return SGDP4_ERROR;
  }

  if (a < SUIMM(1)) {
    /* SU_WARNING("sgdp4: Satellite %05ld crashed at %.3f (a = %.3f Earth radii)\n", self->Isat, ts, a); */
    return SGDP4_ERROR;
  }

  if (e < ECC_LIMIT_LOW) {
    /* SU_WARNING("sgdp4: Satellite %05ld modified eccentricity too low (ts = %.3f, e = %e < %e)\n", self->Isat, ts, e, ECC_LIMIT_LOW); */
    return SGDP4_ERROR;
  }

  if (e < ECC_EPS)
    e = ECC_EPS;
  else if (e > ECC_LIMIT_HIGH)
    e = ECC_LIMIT_HIGH;

  beta2 = SUIMM(1) - e * e;

  /* Long period periodics */
  SU_SINCOS(omega, &sinOMG, &cosOMG);

  temp0 = SUIMM(1) / (a * beta2);
  axn   = e * cosOMG;
  ayn   = e * sinOMG + temp0 * self->aycof;
  xlt   = xl + temp0 * self->xlcof * axn;

  elsq = axn * axn + ayn * ayn;
  if (elsq >= SUIMM(1)) {
    /*SU_WARNING(
				"sgdp4: SQR(e) >= 1 (%.3f at tsince = %.3f for sat %05ld)\n", 
				elsq, 
				tsince, 
				self->Isat);*/
    return SGDP4_ERROR;
  }

  /* Sensibility check for N-R correction. */
  kep->ecc = sqrt(elsq);

  /*
   * Solve Kepler's equation using Newton-Raphson root solving. Here 'capu' is
   * almost the "Mean anomaly", initialise the "Eccentric Anomaly" term 'epw'.
   * The fmod() saves reduction of angle to +/-2pi in SU_SINCOS() and prevents
   * convergence problems.
   *
   * Later modified to support 2nd order NR method which saves roughly 1 iteration
   * for only a couple of arithmetic operations.
   */

  epw   = capu = fmod(xlt - xnode, TWOPI);
  maxnr = kep->ecc;

  for (ii = 0; ii < MAXI; ii++) {
    SU_SINCOS(epw, &sinEPW, &cosEPW);

    ecosE = axn * cosEPW + ayn * sinEPW;
    esinE = axn * sinEPW - ayn * cosEPW;
    f     = capu - epw + esinE;
    if (fabs(f) < NR_EPS)
      break;

    df = 1.0 - ecosE;

    /* 1st order Newton-Raphson correction. */
    nr = f / df;

    if (ii == 0 && FABS(nr) > 1.25 * maxnr)
      nr = SIGN2(maxnr, nr);
    else
      nr = f / (df + 0.5 * esinE * nr); /* f/(df - 0.5*d2f*f/df) */

    epw += nr; /* Newton-Raphson correction of -F/DF. */
    //if (fabs(nr) < NR_EPS) break;
  }

  /* Short period preliminary quantities */
  temp0 = SUIMM(1) - elsq;
  betal = SU_SQRTX(temp0);
  pl    = a * temp0;
  r     = a * (SUIMM(1) - ecosE);
  invR  = SUIMM(1) / r;
  temp2 = a * invR;
  temp3 = SUIMM(1) / (SUIMM(1) + betal);
  cosu  = temp2 * (cosEPW - axn + ayn * esinE * temp3);
  sinu  = temp2 * (sinEPW - ayn - axn * esinE * temp3);
  u     = ATAN2(sinu, cosu);
  sin2u = SUIMM(2) * sinu * cosu;
  cos2u = SUIMM(2) * cosu * cosu - SUIMM(1);
  temp0 = SUIMM(1) / pl;
  temp1 = CK2 * temp0;
  temp2 = temp1 * temp0;

  /* Update for short term periodics to position terms. */

  rk     = r * (SUIMM(1) - SUIMM(1.5) * temp2 * betal * self->x3thm1) 
		+ SUIMM(.5) * temp1 * self->x1mth2 * cos2u;
  uk     = u - SUIMM(.25) * temp2 * self->x7thm1 * sin2u;
  xnodek = xnode + SUIMM(1.5) * temp2 * self->cosIO * sin2u;
  xinck  = xinc + SUIMM(1.5) * temp2 * self->cosIO * self->sinIO * cos2u;

  if (rk < SUIMM(1)) {
    SU_WARNING(
			"sgdp4: Satellite %05ld crashed at %.3f (rk = %.3f Earth radii)\n", 
			self->Isat, 
			ts, 
			rk);
    return SGDP4_ERROR;
  }

  kep->radius = rk * XKMPER / AE; /* Into km */
  kep->theta  = uk;
  kep->eqinc  = xinck;
  kep->ascn   = xnodek;
  kep->argp   = omega;
  kep->smjaxs = a * XKMPER / AE;

  /* Short period velocity terms ?. */
  if (withvel) {
    /* xn = XKE / SU_POWX(a, 1.5); */
    temp0       = SU_SQRTX(a);
    temp2       = (SUFLOAT)XKE / (a * temp0);
    kep->rdotk  = ((SUFLOAT)XKE * temp0 * esinE * invR -
                  temp2 * temp1 * self->x1mth2 * sin2u) *
                 (XKMPER / AE * XMNPDA / 86400.0); /* Into km/sec */

    kep->rfdotk = (
			(SUFLOAT)XKE * SU_SQRTX(pl) * invR + temp2 * temp1 * (
						self->x1mth2 * cos2u + SUIMM(1.5) * self->x3thm1)) 
			* (XKMPER / AE * XMNPDA / 86400.0);
  } else {
    kep->rdotk = kep->rfdotk = 0;
  }

#ifndef SGDP4_SNGL
#undef ts
#endif

  return self->imode;
}

/* ====================================================================

   Transformation from "Kepler" type coordinates to cartesian XYZ form.
   Calling arguments:

   K    : Kepler structure as filled by sgdp4();

   pos  : XYZ structure for position.

   vel  : same for velocity.

   ==================================================================== */

void 
kep_get_pos_vel_teme(kep_t *K, xyz_t *pos, xyz_t *vel)
{
  SUFLOAT xmx, xmy;
  SUFLOAT ux, uy, uz, vx, vy, vz;
  SUFLOAT sinT, cosT, sinI, cosI, sinS, cosS;

  /* Orientation vectors for X-Y-Z format. */

  SU_SINCOS((SUFLOAT)K->theta, &sinT, &cosT);
  SU_SINCOS((SUFLOAT)K->eqinc, &sinI, &cosI);
  SU_SINCOS((SUFLOAT)K->ascn, &sinS, &cosS);

  xmx = -sinS * cosI;
  xmy = cosS * cosI;

  ux = xmx * sinT + cosS * cosT;
  uy = xmy * sinT + sinS * cosT;
  uz = sinI * sinT;

  /* Position and velocity */

  if (pos != NULL) {
    pos->x = K->radius * ux;
    pos->y = K->radius * uy;
    pos->z = K->radius * uz;
  }

  if (vel != NULL) {
    vx = xmx * cosT - cosS * sinT;
    vy = xmy * cosT - sinS * sinT;
    vz = sinI * cosT;

    vel->x = K->rdotk * ux + K->rfdotk * vx;
    vel->y = K->rdotk * uy + K->rfdotk * vy;
    vel->z = K->rdotk * uz + K->rfdotk * vz;
  }
}

/* ======================================================================
   Compute the satellite position and/or velocity for a given time (in the
   form of Julian day number.)
   Calling arguments are:

   jd   : Time as Julian day number.

   pos  : Pointer to posiition vector, km (NULL if not required).

   vel  : Pointer to velocity vector, km/sec (NULL if not required).

   ====================================================================== */

int 
sgdp4_ctx_get_pos_vel(sgdp4_ctx_t *self, double jd, xyz_t *pos, xyz_t *vel)
{
  kep_t K;
  int withvel, rv;
  double tsince;

  tsince = (jd - self->SGDP4_jd0) * XMNPDA;

#ifdef DEBUG
  SU_WARNING("Tsince = %f\n", tsince);
#endif

  if (vel != NULL)
    withvel = 1;
  else
    withvel = 0;

  rv = sgdp4_ctx_compute(self, tsince, withvel, &K);

  kep_get_pos_vel_teme(&K, pos, vel);

  return rv;
}

/* ==================== End of file sgdp4.c ========================== */
