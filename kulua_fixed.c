/*
** kulua_fixed.c
** Q16.16 fixed-point support functions for Kulua.
** See Copyright Notice in lua.h
*/

#define kulua_fixed_c

#include "luaconf.h"

#if LUA_FLOAT_TYPE == LUA_FLOAT_FIXED

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"


/* ===================================================================
** String-to-number conversion: decimal
** =================================================================== */

/*
** Parse a decimal string to Q16.16 fixed-point.
** Handles: [+-]digits[.digits][e[+-]digits]
** Sets *endptr past the consumed characters.
** Returns the Q16.16 value.
*/
int32_t kulua_str2number (const char *s, char **endptr) {
  int neg = 0;
  int32_t ipart = 0;
  uint32_t frac = 0;
  const char *start = s;

  /* skip leading whitespace */
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'
         || *s == '\f' || *s == '\v')
    s++;

  /* sign */
  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') { s++; }

  /* must start with digit or '.' followed by digit */
  if (!isdigit((unsigned char)*s) && !(*s == '.' && isdigit((unsigned char)s[1]))) {
    if (endptr) *endptr = (char *)start;
    return 0;
  }

  /* integer part */
  while (isdigit((unsigned char)*s)) {
    ipart = ipart * 10 + (*s - '0');
    if (ipart > 32767) {
      /* overflow: consume rest of digits and return max */
      while (isdigit((unsigned char)*s)) s++;
      if (*s == '.') { s++; while (isdigit((unsigned char)*s)) s++; }
      if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') s++;
        while (isdigit((unsigned char)*s)) s++;
      }
      if (endptr) *endptr = (char *)s;
      return neg ? (int32_t)0x80000000 : (int32_t)0x7FFFFFFF;
    }
    s++;
  }

  /* fractional part */
  if (*s == '.') {
    s++;
    /* Parse up to 5 fractional digits for precision */
    uint32_t frac_num = 0;
    uint32_t frac_den = 1;
    int ndigits = 0;
    while (isdigit((unsigned char)*s) && ndigits < 9) {
      frac_num = frac_num * 10 + (uint32_t)(*s - '0');
      frac_den *= 10;
      ndigits++;
      s++;
    }
    /* skip remaining fractional digits */
    while (isdigit((unsigned char)*s)) s++;
    /* convert to Q16.16: frac_num / frac_den * 65536 */
    frac = (uint32_t)((uint64_t)frac_num * 65536 / frac_den);
  }

  /* combine integer and fractional parts */
  int32_t result = ((int32_t)ipart << 16) | (int32_t)frac;

  /* handle exponent */
  if (*s == 'e' || *s == 'E') {
    s++;
    int eneg = 0;
    int exp = 0;
    if (*s == '-') { eneg = 1; s++; }
    else if (*s == '+') { s++; }
    while (isdigit((unsigned char)*s)) {
      exp = exp * 10 + (*s - '0');
      s++;
    }
    /* apply exponent: multiply/divide by powers of 10 */
    if (eneg) {
      for (int j = 0; j < exp && result != 0; j++) {
        result = (int32_t)(((int64_t)result << 16) / ((int64_t)10 << 16));
      }
    } else {
      for (int j = 0; j < exp; j++) {
        int64_t tmp = (int64_t)result * 10;
        if (tmp > INT32_MAX || tmp < INT32_MIN) {
          result = neg ? (int32_t)0x80000000 : (int32_t)0x7FFFFFFF;
          break;
        }
        result = (int32_t)tmp;
      }
    }
  }

  if (neg) result = -result;
  if (endptr) *endptr = (char *)s;
  return result;
}


/* ===================================================================
** String-to-number conversion: hexadecimal
** =================================================================== */

static int hexdigit (int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/*
** Parse a hexadecimal numeral to Q16.16 fixed-point.
** Format: 0x[hex][.hex][p[+-]exp]
*/
int32_t kulua_strx2number (const char *s, char **endptr) {
  int neg = 0;
  int32_t result = 0;
  const char *start = s;

  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'
         || *s == '\f' || *s == '\v')
    s++;

  if (*s == '-') { neg = 1; s++; }
  else if (*s == '+') { s++; }

  /* expect 0x / 0X */
  if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) {
    if (endptr) *endptr = (char *)start;
    return 0;
  }
  s += 2;

  int any = 0;

  /* integer part (goes into bits 16..31 of Q16.16) */
  int32_t ipart = 0;
  while (hexdigit(*s) >= 0) {
    ipart = (ipart << 4) | hexdigit(*s);
    any = 1;
    s++;
  }

  /* fractional part (goes into bits 0..15 of Q16.16) */
  uint32_t frac = 0;
  if (*s == '.') {
    s++;
    int shift = 12;  /* first hex digit fills bits 12-15 */
    while (hexdigit(*s) >= 0) {
      if (shift >= 0) {
        frac |= (uint32_t)hexdigit(*s) << shift;
        shift -= 4;
      }
      any = 1;
      s++;
    }
  }

  if (!any) {
    if (endptr) *endptr = (char *)start;
    return 0;
  }

  result = (ipart << 16) | (int32_t)frac;

  /* binary exponent p[+-]exp */
  if (*s == 'p' || *s == 'P') {
    s++;
    int eneg = 0;
    int exp = 0;
    if (*s == '-') { eneg = 1; s++; }
    else if (*s == '+') { s++; }
    while (isdigit((unsigned char)*s)) {
      exp = exp * 10 + (*s - '0');
      s++;
    }
    if (eneg)
      result >>= exp;
    else
      result <<= exp;
  }

  if (neg) result = -result;
  if (endptr) *endptr = (char *)s;
  return result;
}


/* ===================================================================
** Number-to-string: hexadecimal
** =================================================================== */

int kulua_number2strx (lua_State *L, char *buff, unsigned sz,
                       const char *fmt, int32_t n) {
  int upper = 0;
  int neg = 0;
  int len;
  (void)L;

  /* detect uppercase from format string */
  if (strchr(fmt, 'A') != NULL) upper = 1;

  if (n < 0) { neg = 1; n = -n; }
  if (n < 0) {  /* INT32_MIN edge case */
    len = snprintf(buff, sz, "%s0x8000.0000p0", neg ? "-" : "");
    if (upper) { for (int i = 0; buff[i]; i++) buff[i] = (char)toupper(buff[i]); }
    return len;
  }

  uint32_t ipart = (uint32_t)n >> 16;
  uint32_t fpart = (uint32_t)n & 0xFFFF;

  if (fpart == 0) {
    len = snprintf(buff, sz, "%s0x%xp0", neg ? "-" : "", ipart);
  } else {
    /* remove trailing zero nibbles from fpart */
    int fdigits = 4;
    uint32_t f = fpart;
    while (fdigits > 1 && (f & 0xF) == 0) { f >>= 4; fdigits--; }
    len = snprintf(buff, sz, "%s0x%x.%0*xp0", neg ? "-" : "",
                   ipart, fdigits, f);
  }

  if (upper) {
    for (int i = 0; buff[i]; i++)
      buff[i] = (char)toupper((unsigned char)buff[i]);
  }
  return len;
}


/* ===================================================================
** Fixed-point power
** =================================================================== */

/*
** Q16.16 exponentiation. Only integer exponents are meaningful.
** Non-integer exponents are truncated to integer.
*/
int32_t kulua_numpow (int32_t base, int32_t exp) {
  int32_t e = exp >> 16;  /* convert Q16.16 exponent to integer */
  int neg_exp = 0;
  int32_t result;

  if (e < 0) { neg_exp = 1; e = -e; }

  /* result starts at 1.0 in Q16.16 */
  result = 1 << 16;

  while (e > 0) {
    if (e & 1)
      result = (int32_t)(((int64_t)result * (int64_t)base) >> 16);
    base = (int32_t)(((int64_t)base * (int64_t)base) >> 16);
    e >>= 1;
  }

  if (neg_exp) {
    /* 1/result in Q16.16 */
    if (result == 0) return (int32_t)0x7FFFFFFF;  /* inf-ish */
    result = (int32_t)(((int64_t)1 << 32) / (int64_t)result);
  }

  return result;
}


/* ===================================================================
** Math library stubs
** All kulua_* functions needed by l_mathop() expansion.
** =================================================================== */

/* --- Trig LUT (256 entries per quadrant, sine values) --- */

/*
** Quarter-wave sine table: 257 entries for indices 0..256.
** kulua_sin_lut[i] = round(sin(i * pi/512) * 65536)
** Values range from 0 to 65536 (= 1.0 in Q16.16).
*/
static const int32_t kulua_sin_lut[257] = {
      0,  402,  804, 1206, 1608, 2010, 2412, 2814,
   3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023,
   6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218,
   9616,10014,10411,10808,11204,11600,11996,12391,
  12785,13180,13573,13966,14359,14751,15143,15534,
  15924,16314,16703,17091,17479,17867,18253,18639,
  19024,19409,19792,20175,20557,20939,21320,21699,
  22078,22457,22834,23210,23586,23961,24335,24708,
  25080,25451,25821,26190,26558,26925,27291,27656,
  28020,28383,28745,29106,29466,29824,30182,30538,
  30893,31248,31600,31952,32303,32652,33000,33347,
  33692,34037,34380,34721,35062,35401,35738,36075,
  36410,36744,37076,37407,37736,38064,38391,38716,
  39040,39362,39683,40002,40320,40636,40951,41264,
  41576,41886,42194,42501,42806,43110,43412,43713,
  44011,44308,44604,44898,45190,45480,45769,46056,
  46341,46624,46906,47186,47464,47741,48015,48288,
  48559,48828,49095,49361,49624,49886,50146,50404,
  50660,50914,51166,51417,51665,51911,52156,52398,
  52639,52878,53114,53349,53581,53812,54040,54267,
  54491,54714,54934,55152,55368,55582,55794,56004,
  56212,56418,56621,56823,57022,57219,57414,57607,
  57798,57986,58172,58356,58538,58718,58896,59071,
  59244,59415,59583,59750,59914,60075,60235,60392,
  60547,60700,60851,60999,61145,61288,61429,61568,
  61705,61839,61971,62101,62228,62353,62476,62596,
  62714,62830,62943,63054,63162,63268,63372,63473,
  63572,63668,63763,63854,63944,64031,64115,64197,
  64277,64354,64429,64501,64571,64639,64704,64766,
  64827,64884,64940,64993,65043,65091,65137,65180,
  65220,65259,65294,65328,65358,65387,65413,65436,
  65457,65476,65492,65505,65516,65525,65531,65535,
  65536
};

/*
** Q16.16 representation of 2*pi = 411775 (6.28318... * 65536).
*/
#define KULUA_TWOPI     411775
#define KULUA_PI        205887
#define KULUA_HALFPI    102944

/*
** Fixed-point sine: input in Q16.16 radians.
** Uses quarter-wave LUT with linear interpolation.
*/
int32_t kulua_sin (int32_t x) {
  int neg = 0;
  int32_t idx, frac, a, b;

  /* normalize to [0, 2pi) */
  x = x % KULUA_TWOPI;
  if (x < 0) x += KULUA_TWOPI;

  /* map to quadrant */
  if (x >= KULUA_PI) { x -= KULUA_PI; neg = 1; }
  if (x > KULUA_HALFPI) x = KULUA_PI - x;

  /* x is now in [0, pi/2] in Q16.16.
  ** Map to LUT index [0, 256]: idx = x * 256 / (pi/2)
  ** = x * 512 / pi ≈ x * 512 / 205887
  ** Precompute: 512 * 65536 / 205887 ≈ 162.97 ~ 163 (in integer)
  ** Better: (x * 163) >> 16 gives LUT index.
  */
  int32_t scaled = (int32_t)(((int64_t)x * 163) >> 16);
  if (scaled > 256) scaled = 256;

  idx = scaled;
  /* fractional part for interpolation */
  /* reverse: expected x for this idx = idx * KULUA_HALFPI / 256 */
  int32_t x_expected = (int32_t)(((int64_t)idx * KULUA_HALFPI) >> 8);
  frac = x - x_expected;  /* remaining distance in Q16.16 */

  a = kulua_sin_lut[idx];
  b = (idx < 256) ? kulua_sin_lut[idx + 1] : kulua_sin_lut[256];

  /* linear interpolation: a + (b - a) * frac / step_size */
  /* step_size = KULUA_HALFPI / 256 = ~402 in Q16.16 */
  int32_t step = (int32_t)((int64_t)KULUA_HALFPI >> 8);
  int32_t result;
  if (step > 0)
    result = a + (int32_t)(((int64_t)(b - a) * frac) / step);
  else
    result = a;

  return neg ? -result : result;
}


int32_t kulua_cos (int32_t x) {
  return kulua_sin(x + KULUA_HALFPI);
}


int32_t kulua_tan (int32_t x) {
  int32_t c = kulua_cos(x);
  if (c == 0) return (kulua_sin(x) >= 0) ? (int32_t)0x7FFFFFFF
                                          : (int32_t)0x80000001;
  return (int32_t)(((int64_t)kulua_sin(x) << 16) / (int64_t)c);
}


/* atan2 using CORDIC algorithm */
static const int32_t cordic_angles[16] = {
  51472,  /* atan(2^0) = 45.0000 deg in Q16.16 radians */
  30386,  /* atan(2^-1) = 26.5651 deg */
  16055,  /* atan(2^-2) = 14.0362 deg */
   8150,  /* atan(2^-3) =  7.1250 deg */
   4091,  /* atan(2^-4) =  3.5763 deg */
   2047,  /* atan(2^-5) =  1.7899 deg */
   1024,  /* atan(2^-6) */
    512,  /* atan(2^-7) */
    256,  /* atan(2^-8) */
    128,  /* atan(2^-9) */
     64,  /* atan(2^-10) */
     32,  /* atan(2^-11) */
     16,  /* atan(2^-12) */
      8,  /* atan(2^-13) */
      4,  /* atan(2^-14) */
      2,  /* atan(2^-15) */
};


int32_t kulua_atan2 (int32_t y, int32_t x) {
  int32_t angle = 0;
  int32_t tx, ty;

  /* handle special cases */
  if (x == 0 && y == 0) return 0;

  /* CORDIC works best with positive x; handle quadrants */
  int negate_result = 0;
  if (x < 0) {
    x = -x;
    y = -y;
    negate_result = 1;
  }

  for (int i = 0; i < 16; i++) {
    if (y > 0) {
      tx = x + (y >> i);
      ty = y - (x >> i);
      angle += cordic_angles[i];
    } else {
      tx = x - (y >> i);
      ty = y + (x >> i);
      angle -= cordic_angles[i];
    }
    x = tx;
    y = ty;
  }

  if (negate_result) {
    angle = (angle >= 0) ? KULUA_PI - angle : -KULUA_PI - angle;
  }

  return angle;
}


int32_t kulua_asin (int32_t x) {
  /* asin(x) = atan2(x, sqrt(1 - x*x)) */
  int32_t one = 1 << 16;
  int32_t x2 = (int32_t)(((int64_t)x * x) >> 16);
  int32_t kulua_sqrt(int32_t);
  int32_t sq = kulua_sqrt(one - x2);
  return kulua_atan2(x, sq);
}


int32_t kulua_acos (int32_t x) {
  return KULUA_HALFPI - kulua_asin(x);
}


int32_t kulua_atan (int32_t x) {
  int32_t one = 1 << 16;
  return kulua_atan2(x, one);
}


/* ===================================================================
** sqrt: Newton-Raphson for Q16.16
** =================================================================== */

int32_t kulua_sqrt (int32_t x) {
  if (x <= 0) return 0;
  uint32_t v = (uint32_t)x;

  /* initial guess: shift-based approximation */
  int shift = 0;
  uint32_t tmp = v;
  while (tmp > 0) { tmp >>= 1; shift++; }
  /* guess ~= sqrt(v) in Q16.16: start around (1 << ((shift + 16) / 2)) */
  uint32_t guess = (uint32_t)1 << ((shift + 16) / 2);

  /* Newton-Raphson: g = (g + v_q16 / g) / 2
  ** where v_q16 = v << 16 to keep things in Q16.16 */
  uint64_t v_q16 = (uint64_t)v << 16;
  for (int i = 0; i < 6; i++) {
    if (guess == 0) break;
    guess = (uint32_t)((guess + v_q16 / guess) >> 1);
  }
  return (int32_t)guess;
}


/* ===================================================================
** Simple math functions
** =================================================================== */

int32_t kulua_fabs (int32_t x) {
  return (x < 0) ? -x : x;
}

int32_t kulua_ceil (int32_t x) {
  if (x >= 0)
    return (x + 0xFFFF) & ~0xFFFF;
  else
    return x & ~0xFFFF;
}

int32_t kulua_fmod (int32_t a, int32_t b) {
  if (b == 0) return 0;
  /* Use Q16.16 division: (a << 16) / b gives Q16.16 quotient,
  ** truncate to get integer quotient, multiply back */
  int32_t q = (int32_t)(((int64_t)a << 16) / (int64_t)b);
  q = q & ~0xFFFF;  /* truncate to integer (toward zero) */
  int32_t m = a - (int32_t)(((int64_t)q * b) >> 16);
  return m;
}


/* exp/log: rough stub implementations */
int32_t kulua_exp (int32_t x) {
  /* e^x using iterative multiplication.
  ** Only works reasonably for small |x|.
  ** exp(x) ≈ 1 + x + x^2/2 + x^3/6 + x^4/24 + x^5/120 */
  int32_t one = 1 << 16;
  int32_t term = one;
  int32_t sum = one;
  for (int i = 1; i <= 8; i++) {
    term = (int32_t)(((int64_t)term * x) / ((int64_t)i << 16));
    sum += term;
    if (term == 0) break;
  }
  return sum;
}


int32_t kulua_log (int32_t x) {
  if (x <= 0) return (int32_t)0x80000000;  /* -inf-ish */
  /* ln(x) for Q16.16 using a simple series.
  ** Normalize x to [1,2) range, then use ln(1+u) series.
  */
  int32_t one = 1 << 16;
  int32_t two = 2 << 16;
  int32_t log2 = 0;
  int32_t ln2 = 45426;  /* ln(2) in Q16.16 = 0.6931... * 65536 */

  /* scale to [1, 2) */
  while (x >= two) { x >>= 1; log2++; }
  while (x < one) { x <<= 1; log2--; }

  /* x is now in [1,2). Compute ln(x) via ln(1+u) where u = x - 1 */
  int32_t u = x - one;
  int32_t result = 0;
  int32_t term = u;
  for (int i = 1; i <= 12; i++) {
    if (i & 1)
      result += term / i;
    else
      result -= term / i;
    term = (int32_t)(((int64_t)term * u) >> 16);
    if (term == 0) break;
  }

  return result + (int32_t)((int64_t)log2 * ln2);
}


int32_t kulua_log2 (int32_t x) {
  int32_t ln2 = 45426;  /* ln(2) in Q16.16 */
  int32_t lnx = kulua_log(x);
  if (ln2 == 0) return 0;
  return (int32_t)(((int64_t)lnx << 16) / (int64_t)ln2);
}


int32_t kulua_log10 (int32_t x) {
  int32_t ln10 = 150902;  /* ln(10) in Q16.16 = 2.3026... * 65536 */
  int32_t lnx = kulua_log(x);
  if (ln10 == 0) return 0;
  return (int32_t)(((int64_t)lnx << 16) / (int64_t)ln10);
}


/* ldexp: multiply by 2^e (shift the Q16.16 value) */
int32_t kulua_ldexp (int32_t x, int e) {
  if (e >= 0)
    return (e < 31) ? x << e : 0;
  else
    return (-e < 31) ? x >> (-e) : 0;
}


/* frexp: decompose into mantissa * 2^exp */
int32_t kulua_frexp (int32_t x, int *exp) {
  if (x == 0) { *exp = 0; return 0; }
  int neg = 0;
  if (x < 0) { neg = 1; x = -x; }

  /* find highest bit */
  int e = 0;
  int32_t v = x;
  while (v >= (2 << 16)) { v >>= 1; e++; }
  while (v < (1 << 16)) { v <<= 1; e--; }
  /* v is now in [1.0, 2.0) in Q16.16, scale to [0.5, 1.0) */
  v >>= 1;
  e++;

  *exp = e;
  return neg ? -v : v;
}


/* pow: already defined above as kulua_numpow */


#endif  /* LUA_FLOAT_FIXED */
