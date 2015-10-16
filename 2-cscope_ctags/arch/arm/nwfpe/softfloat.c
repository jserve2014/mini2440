/*
===============================================================================

This C source file is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/softfloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these three paragraphs for those parts of
this code that are retained.

===============================================================================
*/

#include <asm/div64.h>

#include "fpa11.h"
//#include "milieu.h"
//#include "softfloat.h"

/*
-------------------------------------------------------------------------------
Primitive arithmetic functions, including multi-word arithmetic, and
division and square root approximations.  (Can be specialized to target if
desired.)
-------------------------------------------------------------------------------
*/
#include "softfloat-macros"

/*
-------------------------------------------------------------------------------
Functions and definitions to determine:  (1) whether tininess for underflow
is detected before or after rounding by default, (2) what (if anything)
happens when exceptions are raised, (3) how signaling NaNs are distinguished
from quiet NaNs, (4) the default generated quiet NaNs, and (5) how NaNs
are propagated from function inputs to output.  These details are target-
specific.
-------------------------------------------------------------------------------
*/
#include "softfloat-specialize"

/*
-------------------------------------------------------------------------------
Takes a 64-bit fixed-point value `absZ' with binary point between bits 6
and 7, and returns the properly rounded 32-bit integer corresponding to the
input.  If `zSign' is nonzero, the input is negated before being converted
to an integer.  Bit 63 of `absZ' must be zero.  Ordinarily, the fixed-point
input is simply rounded to an integer, with the inexact exception raised if
the input cannot be represented exactly as an integer.  If the fixed-point
input is too large, however, the invalid exception is raised and the largest
positive or negative integer is returned.
-------------------------------------------------------------------------------
*/
static int32 roundAndPackInt32( struct roundingData *roundData, flag zSign, bits64 absZ )
{
    int8 roundingMode;
    flag roundNearestEven;
    int8 roundIncrement, roundBits;
    int32 z;

    roundingMode = roundData->mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    roundIncrement = 0x40;
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            roundIncrement = 0;
        }
        else {
            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_up ) roundIncrement = 0;
            }
            else {
                if ( roundingMode == float_round_down ) roundIncrement = 0;
            }
        }
    }
    roundBits = absZ & 0x7F;
    absZ = ( absZ + roundIncrement )>>7;
    absZ &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    z = absZ;
    if ( zSign ) z = - z;
    if ( ( absZ>>32 ) || ( z && ( ( z < 0 ) ^ zSign ) ) ) {
        roundData->exception |= float_flag_invalid;
        return zSign ? 0x80000000 : 0x7FFFFFFF;
    }
    if ( roundBits ) roundData->exception |= float_flag_inexact;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the fraction bits of the single-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE bits32 extractFloat32Frac( float32 a )
{

    return a & 0x007FFFFF;

}

/*
-------------------------------------------------------------------------------
Returns the exponent bits of the single-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE int16 extractFloat32Exp( float32 a )
{

    return ( a>>23 ) & 0xFF;

}

/*
-------------------------------------------------------------------------------
Returns the sign bit of the single-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
#if 0	/* in softfloat.h */
INLINE flag extractFloat32Sign( float32 a )
{

    return a>>31;

}
#endif

/*
-------------------------------------------------------------------------------
Normalizes the subnormal single-precision floating-point value represented
by the denormalized significand `aSig'.  The normalized exponent and
significand are stored at the locations pointed to by `zExpPtr' and
`zSigPtr', respectively.
-------------------------------------------------------------------------------
*/
static void
 normalizeFloat32Subnormal( bits32 aSig, int16 *zExpPtr, bits32 *zSigPtr )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros32( aSig ) - 8;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*
-------------------------------------------------------------------------------
Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
single-precision floating-point value, returning the result.  After being
shifted into the proper positions, the three fields are simply added
together to form the result.  This means that any integer portion of `zSig'
will be added into the exponent.  Since a properly normalized significand
will have an integer portion equal to 1, the `zExp' input should be 1 less
than the desired result exponent whenever `zSig' is a complete, normalized
significand.
-------------------------------------------------------------------------------
*/
INLINE float32 packFloat32( flag zSign, int16 zExp, bits32 zSig )
{
#if 0
   float32 f;
   __asm__("@ packFloat32				\n\
   	    mov %0, %1, asl #31				\n\
   	    orr %0, %2, asl #23				\n\
   	    orr %0, %3"
   	    : /* no outputs */
   	    : "g" (f), "g" (zSign), "g" (zExp), "g" (zSig)
   	    : "cc");
   return f;
#else
    return ( ( (bits32) zSign )<<31 ) + ( ( (bits32) zExp )<<23 ) + zSig;
#endif 
}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand `zSig', and returns the proper single-precision floating-
point value corresponding to the abstract input.  Ordinarily, the abstract
value is simply rounded and packed into the single-precision format, with
the inexact exception raised if the abstract input cannot be represented
exactly.  If the abstract value is too large, however, the overflow and
inexact exceptions are raised and an infinity or maximal finite value is
returned.  If the abstract value is too small, the input value is rounded to
a subnormal number, and the underflow and inexact exceptions are raised if
the abstract input cannot be represented exactly as a subnormal single-
precision floating-point number.
    The input significand `zSig' has its binary point between bits 30
and 29, which is 7 bits to the left of the usual location.  This shifted
significand must be normalized or smaller.  If `zSig' is not normalized,
`zExp' must be 0; in that case, the result returned is a subnormal number,
and it must not require rounding.  In the usual case that `zSig' is
normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
The handling of underflow and overflow follows the IEC/IEEE Standard for
Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static float32 roundAndPackFloat32( struct roundingData *roundData, flag zSign, int16 zExp, bits32 zSig )
{
    int8 roundingMode;
    flag roundNearestEven;
    int8 roundIncrement, roundBits;
    flag isTiny;

    roundingMode = roundData->mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    roundIncrement = 0x40;
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            roundIncrement = 0;
        }
        else {
            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_up ) roundIncrement = 0;
            }
            else {
                if ( roundingMode == float_round_down ) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig & 0x7F;
    if ( 0xFD <= (bits16) zExp ) {
        if (    ( 0xFD < zExp )
             || (    ( zExp == 0xFD )
                  && ( (sbits32) ( zSig + roundIncrement ) < 0 ) )
           ) {
            roundData->exception |= float_flag_overflow | float_flag_inexact;
            return packFloat32( zSign, 0xFF, 0 ) - ( roundIncrement == 0 );
        }
        if ( zExp < 0 ) {
            isTiny =
                   ( float_detect_tininess == float_tininess_before_rounding )
                || ( zExp < -1 )
                || ( zSig + roundIncrement < 0x80000000 );
            shift32RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x7F;
            if ( isTiny && roundBits ) roundData->exception |= float_flag_underflow;
        }
    }
    if ( roundBits ) roundData->exception |= float_flag_inexact;
    zSig = ( zSig + roundIncrement )>>7;
    zSig &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat32( zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand `zSig', and returns the proper single-precision floating-
point value corresponding to the abstract input.  This routine is just like
`roundAndPackFloat32' except that `zSig' does not have to be normalized in
any way.  In all cases, `zExp' must be 1 less than the ``true'' floating-
point exponent.
-------------------------------------------------------------------------------
*/
static float32
 normalizeRoundAndPackFloat32( struct roundingData *roundData, flag zSign, int16 zExp, bits32 zSig )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros32( zSig ) - 1;
    return roundAndPackFloat32( roundData, zSign, zExp - shiftCount, zSig<<shiftCount );

}

/*
-------------------------------------------------------------------------------
Returns the fraction bits of the double-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE bits64 extractFloat64Frac( float64 a )
{

    return a & LIT64( 0x000FFFFFFFFFFFFF );

}

/*
-------------------------------------------------------------------------------
Returns the exponent bits of the double-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
INLINE int16 extractFloat64Exp( float64 a )
{

    return ( a>>52 ) & 0x7FF;

}

/*
-------------------------------------------------------------------------------
Returns the sign bit of the double-precision floating-point value `a'.
-------------------------------------------------------------------------------
*/
#if 0	/* in softfloat.h */
INLINE flag extractFloat64Sign( float64 a )
{

    return a>>63;

}
#endif

/*
-------------------------------------------------------------------------------
Normalizes the subnormal double-precision floating-point value represented
by the denormalized significand `aSig'.  The normalized exponent and
significand are stored at the locations pointed to by `zExpPtr' and
`zSigPtr', respectively.
-------------------------------------------------------------------------------
*/
static void
 normalizeFloat64Subnormal( bits64 aSig, int16 *zExpPtr, bits64 *zSigPtr )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros64( aSig ) - 11;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*
-------------------------------------------------------------------------------
Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
double-precision floating-point value, returning the result.  After being
shifted into the proper positions, the three fields are simply added
together to form the result.  This means that any integer portion of `zSig'
will be added into the exponent.  Since a properly normalized significand
will have an integer portion equal to 1, the `zExp' input should be 1 less
than the desired result exponent whenever `zSig' is a complete, normalized
significand.
-------------------------------------------------------------------------------
*/
INLINE float64 packFloat64( flag zSign, int16 zExp, bits64 zSig )
{

    return ( ( (bits64) zSign )<<63 ) + ( ( (bits64) zExp )<<52 ) + zSig;

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand `zSig', and returns the proper double-precision floating-
point value corresponding to the abstract input.  Ordinarily, the abstract
value is simply rounded and packed into the double-precision format, with
the inexact exception raised if the abstract input cannot be represented
exactly.  If the abstract value is too large, however, the overflow and
inexact exceptions are raised and an infinity or maximal finite value is
returned.  If the abstract value is too small, the input value is rounded to
a subnormal number, and the underflow and inexact exceptions are raised if
the abstract input cannot be represented exactly as a subnormal double-
precision floating-point number.
    The input significand `zSig' has its binary point between bits 62
and 61, which is 10 bits to the left of the usual location.  This shifted
significand must be normalized or smaller.  If `zSig' is not normalized,
`zExp' must be 0; in that case, the result returned is a subnormal number,
and it must not require rounding.  In the usual case that `zSig' is
normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
The handling of underflow and overflow follows the IEC/IEEE Standard for
Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static float64 roundAndPackFloat64( struct roundingData *roundData, flag zSign, int16 zExp, bits64 zSig )
{
    int8 roundingMode;
    flag roundNearestEven;
    int16 roundIncrement, roundBits;
    flag isTiny;

    roundingMode = roundData->mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    roundIncrement = 0x200;
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            roundIncrement = 0;
        }
        else {
            roundIncrement = 0x3FF;
            if ( zSign ) {
                if ( roundingMode == float_round_up ) roundIncrement = 0;
            }
            else {
                if ( roundingMode == float_round_down ) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig & 0x3FF;
    if ( 0x7FD <= (bits16) zExp ) {
        if (    ( 0x7FD < zExp )
             || (    ( zExp == 0x7FD )
                  && ( (sbits64) ( zSig + roundIncrement ) < 0 ) )
           ) {
            //register int lr = __builtin_return_address(0);
            //printk("roundAndPackFloat64 called from 0x%08x\n",lr);
            roundData->exception |= float_flag_overflow | float_flag_inexact;
            return packFloat64( zSign, 0x7FF, 0 ) - ( roundIncrement == 0 );
        }
        if ( zExp < 0 ) {
            isTiny =
                   ( float_detect_tininess == float_tininess_before_rounding )
                || ( zExp < -1 )
                || ( zSig + roundIncrement < LIT64( 0x8000000000000000 ) );
            shift64RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x3FF;
            if ( isTiny && roundBits ) roundData->exception |= float_flag_underflow;
        }
    }
    if ( roundBits ) roundData->exception |= float_flag_inexact;
    zSig = ( zSig + roundIncrement )>>10;
    zSig &= ~ ( ( ( roundBits ^ 0x200 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat64( zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and significand `zSig', and returns the proper double-precision floating-
point value corresponding to the abstract input.  This routine is just like
`roundAndPackFloat64' except that `zSig' does not have to be normalized in
any way.  In all cases, `zExp' must be 1 less than the ``true'' floating-
point exponent.
-------------------------------------------------------------------------------
*/
static float64
 normalizeRoundAndPackFloat64( struct roundingData *roundData, flag zSign, int16 zExp, bits64 zSig )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros64( zSig ) - 1;
    return roundAndPackFloat64( roundData, zSign, zExp - shiftCount, zSig<<shiftCount );

}

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
Returns the fraction bits of the extended double-precision floating-point
value `a'.
-------------------------------------------------------------------------------
*/
INLINE bits64 extractFloatx80Frac( floatx80 a )
{

    return a.low;

}

/*
-------------------------------------------------------------------------------
Returns the exponent bits of the extended double-precision floating-point
value `a'.
-------------------------------------------------------------------------------
*/
INLINE int32 extractFloatx80Exp( floatx80 a )
{

    return a.high & 0x7FFF;

}

/*
-------------------------------------------------------------------------------
Returns the sign bit of the extended double-precision floating-point value
`a'.
-------------------------------------------------------------------------------
*/
INLINE flag extractFloatx80Sign( floatx80 a )
{

    return a.high>>15;

}

/*
-------------------------------------------------------------------------------
Normalizes the subnormal extended double-precision floating-point value
represented by the denormalized significand `aSig'.  The normalized exponent
and significand are stored at the locations pointed to by `zExpPtr' and
`zSigPtr', respectively.
-------------------------------------------------------------------------------
*/
static void
 normalizeFloatx80Subnormal( bits64 aSig, int32 *zExpPtr, bits64 *zSigPtr )
{
    int8 shiftCount;

    shiftCount = countLeadingZeros64( aSig );
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*
-------------------------------------------------------------------------------
Packs the sign `zSign', exponent `zExp', and significand `zSig' into an
extended double-precision floating-point value, returning the result.
-------------------------------------------------------------------------------
*/
INLINE floatx80 packFloatx80( flag zSign, int32 zExp, bits64 zSig )
{
    floatx80 z;

    z.low = zSig;
    z.high = ( ( (bits16) zSign )<<15 ) + zExp;
    z.__padding = 0;
    return z;

}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent `zExp',
and extended significand formed by the concatenation of `zSig0' and `zSig1',
and returns the proper extended double-precision floating-point value
corresponding to the abstract input.  Ordinarily, the abstract value is
rounded and packed into the extended double-precision format, with the
inexact exception raised if the abstract input cannot be represented
exactly.  If the abstract value is too large, however, the overflow and
inexact exceptions are raised and an infinity or maximal finite value is
returned.  If the abstract value is too small, the input value is rounded to
a subnormal number, and the underflow and inexact exceptions are raised if
the abstract input cannot be represented exactly as a subnormal extended
double-precision floating-point number.
    If `roundingPrecision' is 32 or 64, the result is rounded to the same
number of bits as single or double precision, respectively.  Otherwise, the
result is rounded to the full precision of the extended double-precision
format.
    The input significand must be normalized or smaller.  If the input
significand is not normalized, `zExp' must be 0; in that case, the result
returned is a subnormal number, and it must not require rounding.  The
handling of underflow and overflow follows the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static floatx80
 roundAndPackFloatx80(
     struct roundingData *roundData, flag zSign, int32 zExp, bits64 zSig0, bits64 zSig1
 )
{
    int8 roundingMode, roundingPrecision;
    flag roundNearestEven, increment, isTiny;
    int64 roundIncrement, roundMask, roundBits;

    roundingMode = roundData->mode;
    roundingPrecision = roundData->precision;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    if ( roundingPrecision == 80 ) goto precision80;
    if ( roundingPrecision == 64 ) {
        roundIncrement = LIT64( 0x0000000000000400 );
        roundMask = LIT64( 0x00000000000007FF );
    }
    else if ( roundingPrecision == 32 ) {
        roundIncrement = LIT64( 0x0000008000000000 );
        roundMask = LIT64( 0x000000FFFFFFFFFF );
    }
    else {
        goto precision80;
    }
    zSig0 |= ( zSig1 != 0 );
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            roundIncrement = 0;
        }
        else {
            roundIncrement = roundMask;
            if ( zSign ) {
                if ( roundingMode == float_round_up ) roundIncrement = 0;
            }
            else {
                if ( roundingMode == float_round_down ) roundIncrement = 0;
            }
        }
    }
    roundBits = zSig0 & roundMask;
    if ( 0x7FFD <= (bits32) ( zExp - 1 ) ) {
        if (    ( 0x7FFE < zExp )
             || ( ( zExp == 0x7FFE ) && ( zSig0 + roundIncrement < zSig0 ) )
           ) {
            goto overflow;
        }
        if ( zExp <= 0 ) {
            isTiny =
                   ( float_detect_tininess == float_tininess_before_rounding )
                || ( zExp < 0 )
                || ( zSig0 <= zSig0 + roundIncrement );
            shift64RightJamming( zSig0, 1 - zExp, &zSig0 );
            zExp = 0;
            roundBits = zSig0 & roundMask;
            if ( isTiny && roundBits ) roundData->exception |= float_flag_underflow;
            if ( roundBits ) roundData->exception |= float_flag_inexact;
            zSig0 += roundIncrement;
            if ( (sbits64) zSig0 < 0 ) zExp = 1;
            roundIncrement = roundMask + 1;
            if ( roundNearestEven && ( roundBits<<1 == roundIncrement ) ) {
                roundMask |= roundIncrement;
            }
            zSig0 &= ~ roundMask;
            return packFloatx80( zSign, zExp, zSig0 );
        }
    }
    if ( roundBits ) roundData->exception |= float_flag_inexact;
    zSig0 += roundIncrement;
    if ( zSig0 < roundIncrement ) {
        ++zExp;
        zSig0 = LIT64( 0x8000000000000000 );
    }
    roundIncrement = roundMask + 1;
    if ( roundNearestEven && ( roundBits<<1 == roundIncrement ) ) {
        roundMask |= roundIncrement;
    }
    zSig0 &= ~ roundMask;
    if ( zSig0 == 0 ) zExp = 0;
    return packFloatx80( zSign, zExp, zSig0 );
 precision80:
    increment = ( (sbits64) zSig1 < 0 );
    if ( ! roundNearestEven ) {
        if ( roundingMode == float_round_to_zero ) {
            increment = 0;
        }
        else {
            if ( zSign ) {
                increment = ( roundingMode == float_round_down ) && zSig1;
            }
            else {
                increment = ( roundingMode == float_round_up ) && zSig1;
            }
        }
    }
    if ( 0x7FFD <= (bits32) ( zExp - 1 ) ) {
        if (    ( 0x7FFE < zExp )
             || (    ( zExp == 0x7FFE )
                  && ( zSig0 == LIT64( 0xFFFFFFFFFFFFFFFF ) )
                  && increment
                )
           ) {
            roundMask = 0;
 overflow:
            roundData->exception |= float_flag_overflow | float_flag_inexact;
            if (    ( roundingMode == float_round_to_zero )
                 || ( zSign && ( roundingMode == float_round_up ) )
                 || ( ! zSign && ( roundingMode == float_round_down ) )
               ) {
                return packFloatx80( zSign, 0x7FFE, ~ roundMask );
            }
            return packFloatx80( zSign, 0x7FFF, LIT64( 0x8000000000000000 ) );
        }
        if ( zExp <= 0 ) {
            isTiny =
                   ( float_detect_tininess == float_tininess_before_rounding )
                || ( zExp < 0 )
                || ! increment
                || ( zSig0 < LIT64( 0xFFFFFFFFFFFFFFFF ) );
            shift64ExtraRightJamming( zSig0, zSig1, 1 - zExp, &zSig0, &zSig1 );
            zExp = 0;
            if ( isTiny && zSig1 ) roundData->exception |= float_flag_underflow;
            if ( zSig1 ) roundData->exception |= float_flag_inexact;
            if ( roundNearestEven ) {
                increment = ( (sbits64) zSig1 < 0 );
            }
            else {
                if ( zSign ) {
                    increment = ( roundingMode == float_round_down ) && zSig1;
                }
                else {
                    increment = ( roundingMode == float_round_up ) && zSig1;
                }
            }
            if ( increment ) {
                ++zSig0;
                zSig0 &= ~ ( ( zSig1 + zSig1 == 0 ) & roundNearestEven );
                if ( (sbits64) zSig0 < 0 ) zExp = 1;
            }
            return packFloatx80( zSign, zExp, zSig0 );
        }
    }
    if ( zSig1 ) roundData->exception |= float_flag_inexact;
    if ( increment ) {
        ++zSig0;
        if ( zSig0 == 0 ) {
            ++zExp;
            zSig0 = LIT64( 0x8000000000000000 );
        }
        else {
            zSig0 &= ~ ( ( zSig1 + zSig1 == 0 ) & roundNearestEven );
        }
    }
    else {
        if ( zSig0 == 0 ) zExp = 0;
    }
    
    return packFloatx80( zSign, zExp, zSig0 );
}

/*
-------------------------------------------------------------------------------
Takes an abstract floating-point value having sign `zSign', exponent
`zExp', and significand formed by the concatenation of `zSig0' and `zSig1',
and returns the proper extended double-precision floating-point value
corresponding to the abstract input.  This routine is just like
`roundAndPackFloatx80' except that the input significand does not have to be
normalized.
-------------------------------------------------------------------------------
*/
static floatx80
 normalizeRoundAndPackFloatx80(
     struct roundingData *roundData, flag zSign, int32 zExp, bits64 zSig0, bits64 zSig1
 )
{
    int8 shiftCount;

    if ( zSig0 == 0 ) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    shiftCount = countLeadingZeros64( zSig0 );
    shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    zExp -= shiftCount;
    return
        roundAndPackFloatx80( roundData, zSign, zExp, zSig0, zSig1 );

}

#endif

/*
-------------------------------------------------------------------------------
Returns the result of converting the 32-bit two's complement integer `a' to
the single-precision floating-point format.  The conversion is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 int32_to_float32(struct roundingData *roundData, int32 a)
{
    flag zSign;

    if ( a == 0 ) return 0;
    if ( a == 0x80000000 ) return packFloat32( 1, 0x9E, 0 );
    zSign = ( a < 0 );
    return normalizeRoundAndPackFloat32( roundData, zSign, 0x9C, zSign ? - a : a );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the 32-bit two's complement integer `a' to
the double-precision floating-point format.  The conversion is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 int32_to_float64( int32 a )
{
    flag aSign;
    uint32 absA;
    int8 shiftCount;
    bits64 zSig;

    if ( a == 0 ) return 0;
    aSign = ( a < 0 );
    absA = aSign ? - a : a;
    shiftCount = countLeadingZeros32( absA ) + 21;
    zSig = absA;
    return packFloat64( aSign, 0x432 - shiftCount, zSig<<shiftCount );

}

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
Returns the result of converting the 32-bit two's complement integer `a'
to the extended double-precision floating-point format.  The conversion
is performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 int32_to_floatx80( int32 a )
{
    flag zSign;
    uint32 absA;
    int8 shiftCount;
    bits64 zSig;

    if ( a == 0 ) return packFloatx80( 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros32( absA ) + 32;
    zSig = absA;
    return packFloatx80( zSign, 0x403E - shiftCount, zSig<<shiftCount );

}

#endif

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic---which means in particular that the conversion is rounded
according to the current rounding mode.  If `a' is a NaN, the largest
positive integer is returned.  Otherwise, if the conversion overflows, the
largest integer with the same sign as `a' is returned.
-------------------------------------------------------------------------------
*/
int32 float32_to_int32( struct roundingData *roundData, float32 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits32 aSig;
    bits64 zSig;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
    if ( aExp ) aSig |= 0x00800000;
    shiftCount = 0xAF - aExp;
    zSig = aSig;
    zSig <<= 32;
    if ( 0 < shiftCount ) shift64RightJamming( zSig, shiftCount, &zSig );
    return roundAndPackInt32( roundData, aSign, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic, except that the conversion is always rounded toward zero.  If
`a' is a NaN, the largest positive integer is returned.  Otherwise, if the
conversion overflows, the largest integer with the same sign as `a' is
returned.
-------------------------------------------------------------------------------
*/
int32 float32_to_int32_round_to_zero( float32 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits32 aSig;
    int32 z;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = aExp - 0x9E;
    if ( 0 <= shiftCount ) {
        if ( a == 0xCF000000 ) return 0x80000000;
        float_raise( float_flag_invalid );
        if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) return 0x7FFFFFFF;
        return 0x80000000;
    }
    else if ( aExp <= 0x7E ) {
        if ( aExp | aSig ) float_raise( float_flag_inexact );
        return 0;
    }
    aSig = ( aSig | 0x00800000 )<<8;
    z = aSig>>( - shiftCount );
    if ( (bits32) ( aSig<<( shiftCount & 31 ) ) ) {
        float_raise( float_flag_inexact );
    }
    return aSign ? - z : z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the double-precision floating-point format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float32_to_float64( float32 a )
{
    flag aSign;
    int16 aExp;
    bits32 aSig;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if ( aSig ) return commonNaNToFloat64( float32ToCommonNaN( a ) );
        return packFloat64( aSign, 0x7FF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( aSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    return packFloat64( aSign, aExp + 0x380, ( (bits64) aSig )<<29 );

}

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
Returns the result of converting the single-precision floating-point value
`a' to the extended double-precision floating-point format.  The conversion
is performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 float32_to_floatx80( float32 a )
{
    flag aSign;
    int16 aExp;
    bits32 aSig;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if ( aSig ) return commonNaNToFloatx80( float32ToCommonNaN( a ) );
        return packFloatx80( aSign, 0x7FFF, LIT64( 0x8000000000000000 ) );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    aSig |= 0x00800000;
    return packFloatx80( aSign, aExp + 0x3F80, ( (bits64) aSig )<<40 );

}

#endif

/*
-------------------------------------------------------------------------------
Rounds the single-precision floating-point value `a' to an integer, and
returns the result as a single-precision floating-point value.  The
operation is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_round_to_int( struct roundingData *roundData, float32 a )
{
    flag aSign;
    int16 aExp;
    bits32 lastBitMask, roundBitsMask;
    int8 roundingMode;
    float32 z;

    aExp = extractFloat32Exp( a );
    if ( 0x96 <= aExp ) {
        if ( ( aExp == 0xFF ) && extractFloat32Frac( a ) ) {
            return propagateFloat32NaN( a, a );
        }
        return a;
    }
    roundingMode = roundData->mode;
    if ( aExp <= 0x7E ) {
        if ( (bits32) ( a<<1 ) == 0 ) return a;
        roundData->exception |= float_flag_inexact;
        aSign = extractFloat32Sign( a );
        switch ( roundingMode ) {
         case float_round_nearest_even:
            if ( ( aExp == 0x7E ) && extractFloat32Frac( a ) ) {
                return packFloat32( aSign, 0x7F, 0 );
            }
            break;
         case float_round_down:
            return aSign ? 0xBF800000 : 0;
         case float_round_up:
            return aSign ? 0x80000000 : 0x3F800000;
        }
        return packFloat32( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x96 - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    if ( roundingMode == float_round_nearest_even ) {
        z += lastBitMask>>1;
        if ( ( z & roundBitsMask ) == 0 ) z &= ~ lastBitMask;
    }
    else if ( roundingMode != float_round_to_zero ) {
        if ( extractFloat32Sign( z ) ^ ( roundingMode == float_round_up ) ) {
            z += roundBitsMask;
        }
    }
    z &= ~ roundBitsMask;
    if ( z != a ) roundData->exception |= float_flag_inexact;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the absolute values of the single-precision
floating-point values `a' and `b'.  If `zSign' is true, the sum is negated
before being returned.  `zSign' is ignored if the result is a NaN.  The
addition is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static float32 addFloat32Sigs( struct roundingData *roundData, float32 a, float32 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig;
    int16 expDiff;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 6;
    bSig <<= 6;
    if ( 0 < expDiff ) {
        if ( aExp == 0xFF ) {
            if ( aSig ) return propagateFloat32NaN( a, b );
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig |= 0x20000000;
        }
        shift32RightJamming( bSig, expDiff, &bSig );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0xFF ) {
            if ( bSig ) return propagateFloat32NaN( a, b );
            return packFloat32( zSign, 0xFF, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig |= 0x20000000;
        }
        shift32RightJamming( aSig, - expDiff, &aSig );
        zExp = bExp;
    }
    else {
        if ( aExp == 0xFF ) {
            if ( aSig | bSig ) return propagateFloat32NaN( a, b );
            return a;
        }
        if ( aExp == 0 ) return packFloat32( zSign, 0, ( aSig + bSig )>>6 );
        zSig = 0x40000000 + aSig + bSig;
        zExp = aExp;
        goto roundAndPack;
    }
    aSig |= 0x20000000;
    zSig = ( aSig + bSig )<<1;
    --zExp;
    if ( (sbits32) zSig < 0 ) {
        zSig = aSig + bSig;
        ++zExp;
    }
 roundAndPack:
    return roundAndPackFloat32( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the absolute values of the single-
precision floating-point values `a' and `b'.  If `zSign' is true, the
difference is negated before being returned.  `zSign' is ignored if the
result is a NaN.  The subtraction is performed according to the IEC/IEEE
Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static float32 subFloat32Sigs( struct roundingData *roundData, float32 a, float32 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig;
    int16 expDiff;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 7;
    bSig <<= 7;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0xFF ) {
        if ( aSig | bSig ) return propagateFloat32NaN( a, b );
        roundData->exception |= float_flag_invalid;
        return float32_default_nan;
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloat32( roundData->mode == float_round_down, 0, 0 );
 bExpBigger:
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        return packFloat32( zSign ^ 1, 0xFF, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig |= 0x40000000;
    }
    shift32RightJamming( aSig, - expDiff, &aSig );
    bSig |= 0x40000000;
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig |= 0x40000000;
    }
    shift32RightJamming( bSig, expDiff, &bSig );
    aSig |= 0x40000000;
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat32( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the single-precision floating-point values `a'
and `b'.  The operation is performed according to the IEC/IEEE Standard for
Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_add( struct roundingData *roundData, float32 a, float32 b )
{
    flag aSign, bSign;

    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign == bSign ) {
        return addFloat32Sigs( roundData, a, b, aSign );
    }
    else {
        return subFloat32Sigs( roundData, a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the single-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_sub( struct roundingData *roundData, float32 a, float32 b )
{
    flag aSign, bSign;

    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign == bSign ) {
        return subFloat32Sigs( roundData, a, b, aSign );
    }
    else {
        return addFloat32Sigs( roundData, a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of multiplying the single-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_mul( struct roundingData *roundData, float32 a, float32 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig;
    bits64 zSig64;
    bits32 zSig;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN( a, b );
        }
        if ( ( bExp | bSig ) == 0 ) {
            roundData->exception |= float_flag_invalid;
            return float32_default_nan;
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        if ( ( aExp | aSig ) == 0 ) {
            roundData->exception |= float_flag_invalid;
            return float32_default_nan;
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x7F;
    aSig = ( aSig | 0x00800000 )<<7;
    bSig = ( bSig | 0x00800000 )<<8;
    shift64RightJamming( ( (bits64) aSig ) * bSig, 32, &zSig64 );
    zSig = zSig64;
    if ( 0 <= (sbits32) ( zSig<<1 ) ) {
        zSig <<= 1;
        --zExp;
    }
    return roundAndPackFloat32( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of dividing the single-precision floating-point value `a'
by the corresponding value `b'.  The operation is performed according to the
IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_div( struct roundingData *roundData, float32 a, float32 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits32 aSig, bSig, zSig;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, b );
        if ( bExp == 0xFF ) {
            if ( bSig ) return propagateFloat32NaN( a, b );
            roundData->exception |= float_flag_invalid;
            return float32_default_nan;
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        return packFloat32( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
                roundData->exception |= float_flag_invalid;
                return float32_default_nan;
            }
            roundData->exception |= float_flag_divbyzero;
            return packFloat32( zSign, 0xFF, 0 );
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x7D;
    aSig = ( aSig | 0x00800000 )<<7;
    bSig = ( bSig | 0x00800000 )<<8;
    if ( bSig <= ( aSig + aSig ) ) {
        aSig >>= 1;
        ++zExp;
    }
    {
        bits64 tmp = ( (bits64) aSig )<<32;
        do_div( tmp, bSig );
        zSig = tmp;
    }
    if ( ( zSig & 0x3F ) == 0 ) {
        zSig |= ( ( (bits64) bSig ) * zSig != ( (bits64) aSig )<<32 );
    }
    return roundAndPackFloat32( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the remainder of the single-precision floating-point value `a'
with respect to the corresponding value `b'.  The operation is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_rem( struct roundingData *roundData, float32 a, float32 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, expDiff;
    bits32 aSig, bSig;
    bits32 q;
    bits64 aSig64, bSig64, q64;
    bits32 alternateASig;
    sbits32 sigMean;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN( a, b );
        }
        roundData->exception |= float_flag_invalid;
        return float32_default_nan;
    }
    if ( bExp == 0xFF ) {
        if ( bSig ) return propagateFloat32NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            roundData->exception |= float_flag_invalid;
            return float32_default_nan;
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig |= 0x00800000;
    bSig |= 0x00800000;
    if ( expDiff < 32 ) {
        aSig <<= 8;
        bSig <<= 8;
        if ( expDiff < 0 ) {
            if ( expDiff < -1 ) return a;
            aSig >>= 1;
        }
        q = ( bSig <= aSig );
        if ( q ) aSig -= bSig;
        if ( 0 < expDiff ) {
            bits64 tmp = ( (bits64) aSig )<<32;
            do_div( tmp, bSig );
            q = tmp;
            q >>= 32 - expDiff;
            bSig >>= 2;
            aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
        }
        else {
            aSig >>= 2;
            bSig >>= 2;
        }
    }
    else {
        if ( bSig <= aSig ) aSig -= bSig;
        aSig64 = ( (bits64) aSig )<<40;
        bSig64 = ( (bits64) bSig )<<40;
        expDiff -= 64;
        while ( 0 < expDiff ) {
            q64 = estimateDiv128To64( aSig64, 0, bSig64 );
            q64 = ( 2 < q64 ) ? q64 - 2 : 0;
            aSig64 = - ( ( bSig * q64 )<<38 );
            expDiff -= 62;
        }
        expDiff += 64;
        q64 = estimateDiv128To64( aSig64, 0, bSig64 );
        q64 = ( 2 < q64 ) ? q64 - 2 : 0;
        q = q64>>( 64 - expDiff );
        bSig <<= 6;
        aSig = ( ( aSig64>>33 )<<( expDiff - 1 ) ) - bSig * q;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (sbits32) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (sbits32) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat32( roundData, aSign ^ zSign, bExp, aSig );

}

/*
-------------------------------------------------------------------------------
Returns the square root of the single-precision floating-point value `a'.
The operation is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float32_sqrt( struct roundingData *roundData, float32 a )
{
    flag aSign;
    int16 aExp, zExp;
    bits32 aSig, zSig;
    bits64 rem, term;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if ( aSig ) return propagateFloat32NaN( a, 0 );
        if ( ! aSign ) return a;
        roundData->exception |= float_flag_invalid;
        return float32_default_nan;
    }
    if ( aSign ) {
        if ( ( aExp | aSig ) == 0 ) return a;
        roundData->exception |= float_flag_invalid;
        return float32_default_nan;
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return 0;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = ( ( aExp - 0x7F )>>1 ) + 0x7E;
    aSig = ( aSig | 0x00800000 )<<8;
    zSig = estimateSqrt32( aExp, aSig ) + 2;
    if ( ( zSig & 0x7F ) <= 5 ) {
        if ( zSig < 2 ) {
            zSig = 0xFFFFFFFF;
        }
        else {
            aSig >>= aExp & 1;
            term = ( (bits64) zSig ) * zSig;
            rem = ( ( (bits64) aSig )<<32 ) - term;
            while ( (sbits64) rem < 0 ) {
                --zSig;
                rem += ( ( (bits64) zSig )<<1 ) | 1;
            }
            zSig |= ( rem != 0 );
        }
    }
    shift32RightJamming( zSig, 1, &zSig );
    return roundAndPackFloat32( roundData, 0, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is equal to the
corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_eq( float32 a, float32 b )
{

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if ( float32_is_signaling_nan( a ) || float32_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    return ( a == b ) || ( (bits32) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than or
equal to the corresponding value `b', and 0 otherwise.  The comparison is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_le( float32 a, float32 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits32) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_lt( float32 a, float32 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits32) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is equal to the
corresponding value `b', and 0 otherwise.  The invalid exception is raised
if either operand is a NaN.  Otherwise, the comparison is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_eq_signaling( float32 a, float32 b )
{

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    return ( a == b ) || ( (bits32) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than or
equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
cause an exception.  Otherwise, the comparison is performed according to the
IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_le_quiet( float32 a, float32 b )
{
    flag aSign, bSign;
    //int16 aExp, bExp;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        /* Do nothing, even if NaN as we're quiet */
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits32) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the single-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
exception.  Otherwise, the comparison is performed according to the IEC/IEEE
Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float32_lt_quiet( float32 a, float32 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        /* Do nothing, even if NaN as we're quiet */
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits32) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic---which means in particular that the conversion is rounded
according to the current rounding mode.  If `a' is a NaN, the largest
positive integer is returned.  Otherwise, if the conversion overflows, the
largest integer with the same sign as `a' is returned.
-------------------------------------------------------------------------------
*/
int32 float64_to_int32( struct roundingData *roundData, float64 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits64 aSig;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
    if ( aExp ) aSig |= LIT64( 0x0010000000000000 );
    shiftCount = 0x42C - aExp;
    if ( 0 < shiftCount ) shift64RightJamming( aSig, shiftCount, &aSig );
    return roundAndPackInt32( roundData, aSign, aSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic, except that the conversion is always rounded toward zero.  If
`a' is a NaN, the largest positive integer is returned.  Otherwise, if the
conversion overflows, the largest integer with the same sign as `a' is
returned.
-------------------------------------------------------------------------------
*/
int32 float64_to_int32_round_to_zero( float64 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits64 aSig, savedASig;
    int32 z;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    shiftCount = 0x433 - aExp;
    if ( shiftCount < 21 ) {
        if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
        goto invalid;
    }
    else if ( 52 < shiftCount ) {
        if ( aExp || aSig ) float_raise( float_flag_inexact );
        return 0;
    }
    aSig |= LIT64( 0x0010000000000000 );
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise( float_flag_invalid );
        return aSign ? 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        float_raise( float_flag_inexact );
    }
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the 32-bit two's complement unsigned integer format.  The conversion
is performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic---which means in particular that the conversion is rounded
according to the current rounding mode.  If `a' is a NaN, the largest
positive integer is returned.  Otherwise, if the conversion overflows, the
largest positive integer is returned.
-------------------------------------------------------------------------------
*/
int32 float64_to_uint32( struct roundingData *roundData, float64 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits64 aSig;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = 0; //extractFloat64Sign( a );
    //if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
    if ( aExp ) aSig |= LIT64( 0x0010000000000000 );
    shiftCount = 0x42C - aExp;
    if ( 0 < shiftCount ) shift64RightJamming( aSig, shiftCount, &aSig );
    return roundAndPackInt32( roundData, aSign, aSig );
}

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the 32-bit two's complement integer format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic, except that the conversion is always rounded toward zero.  If
`a' is a NaN, the largest positive integer is returned.  Otherwise, if the
conversion overflows, the largest positive integer is returned.
-------------------------------------------------------------------------------
*/
int32 float64_to_uint32_round_to_zero( float64 a )
{
    flag aSign;
    int16 aExp, shiftCount;
    bits64 aSig, savedASig;
    int32 z;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    shiftCount = 0x433 - aExp;
    if ( shiftCount < 21 ) {
        if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
        goto invalid;
    }
    else if ( 52 < shiftCount ) {
        if ( aExp || aSig ) float_raise( float_flag_inexact );
        return 0;
    }
    aSig |= LIT64( 0x0010000000000000 );
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise( float_flag_invalid );
        return aSign ? 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        float_raise( float_flag_inexact );
    }
    return z;
}

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the single-precision floating-point format.  The conversion is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
float32 float64_to_float32( struct roundingData *roundData, float64 a )
{
    flag aSign;
    int16 aExp;
    bits64 aSig;
    bits32 zSig;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if ( aSig ) return commonNaNToFloat32( float64ToCommonNaN( a ) );
        return packFloat32( aSign, 0xFF, 0 );
    }
    shift64RightJamming( aSig, 22, &aSig );
    zSig = aSig;
    if ( aExp || zSig ) {
        zSig |= 0x40000000;
        aExp -= 0x381;
    }
    return roundAndPackFloat32( roundData, aSign, aExp, zSig );

}

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
Returns the result of converting the double-precision floating-point value
`a' to the extended double-precision floating-point format.  The conversion
is performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 float64_to_floatx80( float64 a )
{
    flag aSign;
    int16 aExp;
    bits64 aSig;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if ( aSig ) return commonNaNToFloatx80( float64ToCommonNaN( a ) );
        return packFloatx80( aSign, 0x7FFF, LIT64( 0x8000000000000000 ) );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    return
        packFloatx80(
            aSign, aExp + 0x3C00, ( aSig | LIT64( 0x0010000000000000 ) )<<11 );

}

#endif

/*
-------------------------------------------------------------------------------
Rounds the double-precision floating-point value `a' to an integer, and
returns the result as a double-precision floating-point value.  The
operation is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_round_to_int( struct roundingData *roundData, float64 a )
{
    flag aSign;
    int16 aExp;
    bits64 lastBitMask, roundBitsMask;
    int8 roundingMode;
    float64 z;

    aExp = extractFloat64Exp( a );
    if ( 0x433 <= aExp ) {
        if ( ( aExp == 0x7FF ) && extractFloat64Frac( a ) ) {
            return propagateFloat64NaN( a, a );
        }
        return a;
    }
    if ( aExp <= 0x3FE ) {
        if ( (bits64) ( a<<1 ) == 0 ) return a;
        roundData->exception |= float_flag_inexact;
        aSign = extractFloat64Sign( a );
        switch ( roundData->mode ) {
         case float_round_nearest_even:
            if ( ( aExp == 0x3FE ) && extractFloat64Frac( a ) ) {
                return packFloat64( aSign, 0x3FF, 0 );
            }
            break;
         case float_round_down:
            return aSign ? LIT64( 0xBFF0000000000000 ) : 0;
         case float_round_up:
            return
            aSign ? LIT64( 0x8000000000000000 ) : LIT64( 0x3FF0000000000000 );
        }
        return packFloat64( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x433 - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    roundingMode = roundData->mode;
    if ( roundingMode == float_round_nearest_even ) {
        z += lastBitMask>>1;
        if ( ( z & roundBitsMask ) == 0 ) z &= ~ lastBitMask;
    }
    else if ( roundingMode != float_round_to_zero ) {
        if ( extractFloat64Sign( z ) ^ ( roundingMode == float_round_up ) ) {
            z += roundBitsMask;
        }
    }
    z &= ~ roundBitsMask;
    if ( z != a ) roundData->exception |= float_flag_inexact;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the absolute values of the double-precision
floating-point values `a' and `b'.  If `zSign' is true, the sum is negated
before being returned.  `zSign' is ignored if the result is a NaN.  The
addition is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static float64 addFloat64Sigs( struct roundingData *roundData, float64 a, float64 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig;
    int16 expDiff;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 9;
    bSig <<= 9;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FF ) {
            if ( aSig ) return propagateFloat64NaN( a, b );
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig |= LIT64( 0x2000000000000000 );
        }
        shift64RightJamming( bSig, expDiff, &bSig );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FF ) {
            if ( bSig ) return propagateFloat64NaN( a, b );
            return packFloat64( zSign, 0x7FF, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig |= LIT64( 0x2000000000000000 );
        }
        shift64RightJamming( aSig, - expDiff, &aSig );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FF ) {
            if ( aSig | bSig ) return propagateFloat64NaN( a, b );
            return a;
        }
        if ( aExp == 0 ) return packFloat64( zSign, 0, ( aSig + bSig )>>9 );
        zSig = LIT64( 0x4000000000000000 ) + aSig + bSig;
        zExp = aExp;
        goto roundAndPack;
    }
    aSig |= LIT64( 0x2000000000000000 );
    zSig = ( aSig + bSig )<<1;
    --zExp;
    if ( (sbits64) zSig < 0 ) {
        zSig = aSig + bSig;
        ++zExp;
    }
 roundAndPack:
    return roundAndPackFloat64( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the absolute values of the double-
precision floating-point values `a' and `b'.  If `zSign' is true, the
difference is negated before being returned.  `zSign' is ignored if the
result is a NaN.  The subtraction is performed according to the IEC/IEEE
Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static float64 subFloat64Sigs( struct roundingData *roundData, float64 a, float64 b, flag zSign )
{
    int16 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig;
    int16 expDiff;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 10;
    bSig <<= 10;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FF ) {
        if ( aSig | bSig ) return propagateFloat64NaN( a, b );
        roundData->exception |= float_flag_invalid;
        return float64_default_nan;
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloat64( roundData->mode == float_round_down, 0, 0 );
 bExpBigger:
    if ( bExp == 0x7FF ) {
        if ( bSig ) return propagateFloat64NaN( a, b );
        return packFloat64( zSign ^ 1, 0x7FF, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig |= LIT64( 0x4000000000000000 );
    }
    shift64RightJamming( aSig, - expDiff, &aSig );
    bSig |= LIT64( 0x4000000000000000 );
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FF ) {
        if ( aSig ) return propagateFloat64NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig |= LIT64( 0x4000000000000000 );
    }
    shift64RightJamming( bSig, expDiff, &bSig );
    aSig |= LIT64( 0x4000000000000000 );
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat64( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the double-precision floating-point values `a'
and `b'.  The operation is performed according to the IEC/IEEE Standard for
Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_add( struct roundingData *roundData, float64 a, float64 b )
{
    flag aSign, bSign;

    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign == bSign ) {
        return addFloat64Sigs( roundData, a, b, aSign );
    }
    else {
        return subFloat64Sigs( roundData, a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the double-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_sub( struct roundingData *roundData, float64 a, float64 b )
{
    flag aSign, bSign;

    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign == bSign ) {
        return subFloat64Sigs( roundData, a, b, aSign );
    }
    else {
        return addFloat64Sigs( roundData, a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of multiplying the double-precision floating-point values
`a' and `b'.  The operation is performed according to the IEC/IEEE Standard
for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_mul( struct roundingData *roundData, float64 a, float64 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig0, zSig1;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FF ) {
        if ( aSig || ( ( bExp == 0x7FF ) && bSig ) ) {
            return propagateFloat64NaN( a, b );
        }
        if ( ( bExp | bSig ) == 0 ) {
            roundData->exception |= float_flag_invalid;
            return float64_default_nan;
        }
        return packFloat64( zSign, 0x7FF, 0 );
    }
    if ( bExp == 0x7FF ) {
        if ( bSig ) return propagateFloat64NaN( a, b );
        if ( ( aExp | aSig ) == 0 ) {
            roundData->exception |= float_flag_invalid;
            return float64_default_nan;
        }
        return packFloat64( zSign, 0x7FF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( zSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloat64( zSign, 0, 0 );
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x3FF;
    aSig = ( aSig | LIT64( 0x0010000000000000 ) )<<10;
    bSig = ( bSig | LIT64( 0x0010000000000000 ) )<<11;
    mul64To128( aSig, bSig, &zSig0, &zSig1 );
    zSig0 |= ( zSig1 != 0 );
    if ( 0 <= (sbits64) ( zSig0<<1 ) ) {
        zSig0 <<= 1;
        --zExp;
    }
    return roundAndPackFloat64( roundData, zSign, zExp, zSig0 );

}

/*
-------------------------------------------------------------------------------
Returns the result of dividing the double-precision floating-point value `a'
by the corresponding value `b'.  The operation is performed according to
the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_div( struct roundingData *roundData, float64 a, float64 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig;
    bits64 rem0, rem1;
    bits64 term0, term1;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FF ) {
        if ( aSig ) return propagateFloat64NaN( a, b );
        if ( bExp == 0x7FF ) {
            if ( bSig ) return propagateFloat64NaN( a, b );
            roundData->exception |= float_flag_invalid;
            return float64_default_nan;
        }
        return packFloat64( zSign, 0x7FF, 0 );
    }
    if ( bExp == 0x7FF ) {
        if ( bSig ) return propagateFloat64NaN( a, b );
        return packFloat64( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
                roundData->exception |= float_flag_invalid;
                return float64_default_nan;
            }
            roundData->exception |= float_flag_divbyzero;
            return packFloat64( zSign, 0x7FF, 0 );
        }
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( zSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x3FD;
    aSig = ( aSig | LIT64( 0x0010000000000000 ) )<<10;
    bSig = ( bSig | LIT64( 0x0010000000000000 ) )<<11;
    if ( bSig <= ( aSig + aSig ) ) {
        aSig >>= 1;
        ++zExp;
    }
    zSig = estimateDiv128To64( aSig, 0, bSig );
    if ( ( zSig & 0x1FF ) <= 2 ) {
        mul64To128( bSig, zSig, &term0, &term1 );
        sub128( aSig, 0, term0, term1, &rem0, &rem1 );
        while ( (sbits64) rem0 < 0 ) {
            --zSig;
            add128( rem0, rem1, 0, bSig, &rem0, &rem1 );
        }
        zSig |= ( rem1 != 0 );
    }
    return roundAndPackFloat64( roundData, zSign, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns the remainder of the double-precision floating-point value `a'
with respect to the corresponding value `b'.  The operation is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_rem( struct roundingData *roundData, float64 a, float64 b )
{
    flag aSign, bSign, zSign;
    int16 aExp, bExp, expDiff;
    bits64 aSig, bSig;
    bits64 q, alternateASig;
    sbits64 sigMean;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    if ( aExp == 0x7FF ) {
        if ( aSig || ( ( bExp == 0x7FF ) && bSig ) ) {
            return propagateFloat64NaN( a, b );
        }
        roundData->exception |= float_flag_invalid;
        return float64_default_nan;
    }
    if ( bExp == 0x7FF ) {
        if ( bSig ) return propagateFloat64NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            roundData->exception |= float_flag_invalid;
            return float64_default_nan;
        }
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig = ( aSig | LIT64( 0x0010000000000000 ) )<<11;
    bSig = ( bSig | LIT64( 0x0010000000000000 ) )<<11;
    if ( expDiff < 0 ) {
        if ( expDiff < -1 ) return a;
        aSig >>= 1;
    }
    q = ( bSig <= aSig );
    if ( q ) aSig -= bSig;
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        aSig = - ( ( bSig>>2 ) * q );
        expDiff -= 62;
    }
    expDiff += 64;
    if ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        q >>= 64 - expDiff;
        bSig >>= 2;
        aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
    }
    else {
        aSig >>= 2;
        bSig >>= 2;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (sbits64) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (sbits64) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat64( roundData, aSign ^ zSign, bExp, aSig );

}

/*
-------------------------------------------------------------------------------
Returns the square root of the double-precision floating-point value `a'.
The operation is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 float64_sqrt( struct roundingData *roundData, float64 a )
{
    flag aSign;
    int16 aExp, zExp;
    bits64 aSig, zSig;
    bits64 rem0, rem1, term0, term1; //, shiftedRem;
    //float64 z;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if ( aSig ) return propagateFloat64NaN( a, a );
        if ( ! aSign ) return a;
        roundData->exception |= float_flag_invalid;
        return float64_default_nan;
    }
    if ( aSign ) {
        if ( ( aExp | aSig ) == 0 ) return a;
        roundData->exception |= float_flag_invalid;
        return float64_default_nan;
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return 0;
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    zExp = ( ( aExp - 0x3FF )>>1 ) + 0x3FE;
    aSig |= LIT64( 0x0010000000000000 );
    zSig = estimateSqrt32( aExp, aSig>>21 );
    zSig <<= 31;
    aSig <<= 9 - ( aExp & 1 );
    zSig = estimateDiv128To64( aSig, 0, zSig ) + zSig + 2;
    if ( ( zSig & 0x3FF ) <= 5 ) {
        if ( zSig < 2 ) {
            zSig = LIT64( 0xFFFFFFFFFFFFFFFF );
        }
        else {
            aSig <<= 2;
            mul64To128( zSig, zSig, &term0, &term1 );
            sub128( aSig, 0, term0, term1, &rem0, &rem1 );
            while ( (sbits64) rem0 < 0 ) {
                --zSig;
                shortShift128Left( 0, zSig, 1, &term0, &term1 );
                term1 |= 1;
                add128( rem0, rem1, term0, term1, &rem0, &rem1 );
            }
            zSig |= ( ( rem0 | rem1 ) != 0 );
        }
    }
    shift64RightJamming( zSig, 1, &zSig );
    return roundAndPackFloat64( roundData, 0, zExp, zSig );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is equal to the
corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_eq( float64 a, float64 b )
{

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if ( float64_is_signaling_nan( a ) || float64_is_signaling_nan( b ) ) {
            float_raise( float_flag_invalid );
        }
        return 0;
    }
    return ( a == b ) || ( (bits64) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than or
equal to the corresponding value `b', and 0 otherwise.  The comparison is
performed according to the IEC/IEEE Standard for Binary Floating-point
Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_le( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits64) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  The comparison is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_lt( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits64) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is equal to the
corresponding value `b', and 0 otherwise.  The invalid exception is raised
if either operand is a NaN.  Otherwise, the comparison is performed
according to the IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_eq_signaling( float64 a, float64 b )
{

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise( float_flag_invalid );
        return 0;
    }
    return ( a == b ) || ( (bits64) ( ( a | b )<<1 ) == 0 );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than or
equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
cause an exception.  Otherwise, the comparison is performed according to the
IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_le_quiet( float64 a, float64 b )
{
    flag aSign, bSign;
    //int16 aExp, bExp;

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        /* Do nothing, even if NaN as we're quiet */
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign ) return aSign || ( (bits64) ( ( a | b )<<1 ) == 0 );
    return ( a == b ) || ( aSign ^ ( a < b ) );

}

/*
-------------------------------------------------------------------------------
Returns 1 if the double-precision floating-point value `a' is less than
the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
exception.  Otherwise, the comparison is performed according to the IEC/IEEE
Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
flag float64_lt_quiet( float64 a, float64 b )
{
    flag aSign, bSign;

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        /* Do nothing, even if NaN as we're quiet */
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign != bSign ) return aSign && ( (bits64) ( ( a | b )<<1 ) != 0 );
    return ( a != b ) && ( aSign ^ ( a < b ) );

}

#ifdef FLOATX80

/*
-------------------------------------------------------------------------------
Returns the result of converting the extended double-precision floating-
point value `a' to the 32-bit two's complement integer format.  The
conversion is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic---which means in particular that the conversion
is rounded according to the current rounding mode.  If `a' is a NaN, the
largest positive integer is returned.  Otherwise, if the conversion
overflows, the largest integer with the same sign as `a' is returned.
-------------------------------------------------------------------------------
*/
int32 floatx80_to_int32( struct roundingData *roundData, floatx80 a )
{
    flag aSign;
    int32 aExp, shiftCount;
    bits64 aSig;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( ( aExp == 0x7FFF ) && (bits64) ( aSig<<1 ) ) aSign = 0;
    shiftCount = 0x4037 - aExp;
    if ( shiftCount <= 0 ) shiftCount = 1;
    shift64RightJamming( aSig, shiftCount, &aSig );
    return roundAndPackInt32( roundData, aSign, aSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the extended double-precision floating-
point value `a' to the 32-bit two's complement integer format.  The
conversion is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic, except that the conversion is always rounded
toward zero.  If `a' is a NaN, the largest positive integer is returned.
Otherwise, if the conversion overflows, the largest integer with the same
sign as `a' is returned.
-------------------------------------------------------------------------------
*/
int32 floatx80_to_int32_round_to_zero( floatx80 a )
{
    flag aSign;
    int32 aExp, shiftCount;
    bits64 aSig, savedASig;
    int32 z;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    shiftCount = 0x403E - aExp;
    if ( shiftCount < 32 ) {
        if ( ( aExp == 0x7FFF ) && (bits64) ( aSig<<1 ) ) aSign = 0;
        goto invalid;
    }
    else if ( 63 < shiftCount ) {
        if ( aExp || aSig ) float_raise( float_flag_inexact );
        return 0;
    }
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise( float_flag_invalid );
        return aSign ? 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        float_raise( float_flag_inexact );
    }
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the extended double-precision floating-
point value `a' to the single-precision floating-point format.  The
conversion is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float32 floatx80_to_float32( struct roundingData *roundData, floatx80 a )
{
    flag aSign;
    int32 aExp;
    bits64 aSig;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( (bits64) ( aSig<<1 ) ) {
            return commonNaNToFloat32( floatx80ToCommonNaN( a ) );
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    shift64RightJamming( aSig, 33, &aSig );
    if ( aExp || aSig ) aExp -= 0x3F81;
    return roundAndPackFloat32( roundData, aSign, aExp, aSig );

}

/*
-------------------------------------------------------------------------------
Returns the result of converting the extended double-precision floating-
point value `a' to the double-precision floating-point format.  The
conversion is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
float64 floatx80_to_float64( struct roundingData *roundData, floatx80 a )
{
    flag aSign;
    int32 aExp;
    bits64 aSig, zSig;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( (bits64) ( aSig<<1 ) ) {
            return commonNaNToFloat64( floatx80ToCommonNaN( a ) );
        }
        return packFloat64( aSign, 0x7FF, 0 );
    }
    shift64RightJamming( aSig, 1, &zSig );
    if ( aExp || aSig ) aExp -= 0x3C01;
    return roundAndPackFloat64( roundData, aSign, aExp, zSig );

}

/*
-------------------------------------------------------------------------------
Rounds the extended double-precision floating-point value `a' to an integer,
and returns the result as an extended quadruple-precision floating-point
value.  The operation is performed according to the IEC/IEEE Standard for
Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 floatx80_round_to_int( struct roundingData *roundData, floatx80 a )
{
    flag aSign;
    int32 aExp;
    bits64 lastBitMask, roundBitsMask;
    int8 roundingMode;
    floatx80 z;

    aExp = extractFloatx80Exp( a );
    if ( 0x403E <= aExp ) {
        if ( ( aExp == 0x7FFF ) && (bits64) ( extractFloatx80Frac( a )<<1 ) ) {
            return propagateFloatx80NaN( a, a );
        }
        return a;
    }
    if ( aExp <= 0x3FFE ) {
        if (    ( aExp == 0 )
             && ( (bits64) ( extractFloatx80Frac( a )<<1 ) == 0 ) ) {
            return a;
        }
        roundData->exception |= float_flag_inexact;
        aSign = extractFloatx80Sign( a );
        switch ( roundData->mode ) {
         case float_round_nearest_even:
            if ( ( aExp == 0x3FFE ) && (bits64) ( extractFloatx80Frac( a )<<1 )
               ) {
                return
                    packFloatx80( aSign, 0x3FFF, LIT64( 0x8000000000000000 ) );
            }
            break;
         case float_round_down:
            return
                  aSign ?
                      packFloatx80( 1, 0x3FFF, LIT64( 0x8000000000000000 ) )
                : packFloatx80( 0, 0, 0 );
         case float_round_up:
            return
                  aSign ? packFloatx80( 1, 0, 0 )
                : packFloatx80( 0, 0x3FFF, LIT64( 0x8000000000000000 ) );
        }
        return packFloatx80( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x403E - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    roundingMode = roundData->mode;
    if ( roundingMode == float_round_nearest_even ) {
        z.low += lastBitMask>>1;
        if ( ( z.low & roundBitsMask ) == 0 ) z.low &= ~ lastBitMask;
    }
    else if ( roundingMode != float_round_to_zero ) {
        if ( extractFloatx80Sign( z ) ^ ( roundingMode == float_round_up ) ) {
            z.low += roundBitsMask;
        }
    }
    z.low &= ~ roundBitsMask;
    if ( z.low == 0 ) {
        ++z.high;
        z.low = LIT64( 0x8000000000000000 );
    }
    if ( z.low != a.low ) roundData->exception |= float_flag_inexact;
    return z;

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the absolute values of the extended double-
precision floating-point values `a' and `b'.  If `zSign' is true, the sum is
negated before being returned.  `zSign' is ignored if the result is a NaN.
The addition is performed according to the IEC/IEEE Standard for Binary
Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static floatx80 addFloatx80Sigs( struct roundingData *roundData, floatx80 a, floatx80 b, flag zSign )
{
    int32 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig0, zSig1;
    int32 expDiff;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FFF ) {
            if ( (bits64) ( aSig<<1 ) ) return propagateFloatx80NaN( a, b );
            return a;
        }
        if ( bExp == 0 ) --expDiff;
        shift64ExtraRightJamming( bSig, 0, expDiff, &bSig, &zSig1 );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FFF ) {
            if ( (bits64) ( bSig<<1 ) ) return propagateFloatx80NaN( a, b );
            return packFloatx80( zSign, 0x7FFF, LIT64( 0x8000000000000000 ) );
        }
        if ( aExp == 0 ) ++expDiff;
        shift64ExtraRightJamming( aSig, 0, - expDiff, &aSig, &zSig1 );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FFF ) {
            if ( (bits64) ( ( aSig | bSig )<<1 ) ) {
                return propagateFloatx80NaN( a, b );
            }
            return a;
        }
        zSig1 = 0;
        zSig0 = aSig + bSig;
        if ( aExp == 0 ) {
            normalizeFloatx80Subnormal( zSig0, &zExp, &zSig0 );
            goto roundAndPack;
        }
        zExp = aExp;
        goto shiftRight1;
    }
    
    zSig0 = aSig + bSig;

    if ( (sbits64) zSig0 < 0 ) goto roundAndPack; 
 shiftRight1:
    shift64ExtraRightJamming( zSig0, zSig1, 1, &zSig0, &zSig1 );
    zSig0 |= LIT64( 0x8000000000000000 );
    ++zExp;
 roundAndPack:
    return
        roundAndPackFloatx80(
            roundData, zSign, zExp, zSig0, zSig1 );

}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the absolute values of the extended
double-precision floating-point values `a' and `b'.  If `zSign' is true,
the difference is negated before being returned.  `zSign' is ignored if the
result is a NaN.  The subtraction is performed according to the IEC/IEEE
Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
static floatx80 subFloatx80Sigs( struct roundingData *roundData, floatx80 a, floatx80 b, flag zSign )
{
    int32 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig0, zSig1;
    int32 expDiff;
    floatx80 z;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FFF ) {
        if ( (bits64) ( ( aSig | bSig )<<1 ) ) {
            return propagateFloatx80NaN( a, b );
        }
        roundData->exception |= float_flag_invalid;
        z.low = floatx80_default_nan_low;
        z.high = floatx80_default_nan_high;
        z.__padding = 0;
        return z;
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    zSig1 = 0;
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloatx80( roundData->mode == float_round_down, 0, 0 );
 bExpBigger:
    if ( bExp == 0x7FFF ) {
        if ( (bits64) ( bSig<<1 ) ) return propagateFloatx80NaN( a, b );
        return packFloatx80( zSign ^ 1, 0x7FFF, LIT64( 0x8000000000000000 ) );
    }
    if ( aExp == 0 ) ++expDiff;
    shift128RightJamming( aSig, 0, - expDiff, &aSig, &zSig1 );
 bBigger:
    sub128( bSig, 0, aSig, zSig1, &zSig0, &zSig1 );
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FFF ) {
        if ( (bits64) ( aSig<<1 ) ) return propagateFloatx80NaN( a, b );
        return a;
    }
    if ( bExp == 0 ) --expDiff;
    shift128RightJamming( bSig, 0, expDiff, &bSig, &zSig1 );
 aBigger:
    sub128( aSig, 0, bSig, zSig1, &zSig0, &zSig1 );
    zExp = aExp;
 normalizeRoundAndPack:
    return
        normalizeRoundAndPackFloatx80(
            roundData, zSign, zExp, zSig0, zSig1 );

}

/*
-------------------------------------------------------------------------------
Returns the result of adding the extended double-precision floating-point
values `a' and `b'.  The operation is performed according to the IEC/IEEE
Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 floatx80_add( struct roundingData *roundData, floatx80 a, floatx80 b )
{
    flag aSign, bSign;
    
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign == bSign ) {
        return addFloatx80Sigs( roundData, a, b, aSign );
    }
    else {
        return subFloatx80Sigs( roundData, a, b, aSign );
    }
    
}

/*
-------------------------------------------------------------------------------
Returns the result of subtracting the extended double-precision floating-
point values `a' and `b'.  The operation is performed according to the
IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 floatx80_sub( struct roundingData *roundData, floatx80 a, floatx80 b )
{
    flag aSign, bSign;

    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign == bSign ) {
        return subFloatx80Sigs( roundData, a, b, aSign );
    }
    else {
        return addFloatx80Sigs( roundData, a, b, aSign );
    }

}

/*
-------------------------------------------------------------------------------
Returns the result of multiplying the extended double-precision floating-
point values `a' and `b'.  The operation is performed according to the
IEC/IEEE Standard for Binary Floating-point Arithmetic.
-------------------------------------------------------------------------------
*/
floatx80 floatx80_mul( struct roundingData *roundData, floatx80 a, floatx80 b )
{
    flag aSign, bSign, zSign;
    int32 aExp, bExp, zExp;
    bits64 aSig, bSig, zSig0, zSig1;
    floatx80 z;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    bSign = extractFloatx80Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (    (bits64) ( aSig<<1 )
             || ( ( bExp == 0x7FFF ) && (bits64) ( bSig<<1 ) ) ) {
            return propagateFloatx80NaN( a, b );
        }
        if ( ( bExp | bSig ) == 0 ) goto invalid;
        return packFloatx80( zSign, 0x7FFF, LIT64( 0x8000000000000000 ) );
    }
    if ( bExp == 0x7FFF ) {
        if ( (bits64) ( bSig<<1 ) ) return propagateFloatx80NaN( a, b );
        if ( ( aExp | aSig ) == 0 ) {
 invalid:
            roundData->exception |= float_flag_invalid;
            z.low = floatx80_default_nan_low;
            z.high = floatx80_default_nan_high;
            z.__padding = 0;
            return z;
        }
        return packFloatx80( zSign, 0x7FFF, LIT64( 0x8000000000000000 ) );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( zSign, 0, 0 );
 =======normalizeFloatx80Sub======( aSig, &aExp====Sig========}=====if ( bExp======= {======== is pa
 is s C soureturn packe is par( zSign,/*t of ithmetic Package, Release 2.

Written t of, &

Thhn Rithmetic Package,z
This===== +=R. Hi- 0x3FFEithmetmul64To128ithmeticy John ing-0ience I1ithmetic Pack0 < (sbits64)tce I Softce file ishortShift128Lefta7 Cen0,7 Cen1, 1stitutenstitute, located by -- mad.

Written by=====ithmetic roundAndPEC/IEEE ====ithmetic n under gData.  Funn,s
Natn
of t4n
of ttiona
}

/*
-a project to build a fixed-point vector
processor in collaboration with the Un
R=====sthe Uresuly Jo dividingof Caextended double-precison wf=====ngpoint 
value `a' byey,
ocorresponrkeleWawrzynb'.  The operaton Mis performed
acinfon
isyoey,
oI MIP-931Standard for Binary====== and ScienArithmetic.  project to build a fixed-point vector
processor in collaboration with the Uniiv*/
organx80Md it, TH_div( structnal veingrsio *al ve THAn
d it, THITIMES RESULTb )
Streetfla======nter Scde pS SOvided bnt32possi,e in ps cotional 00, 19CompuTHIS Siewas written  ORGANIZATIONrem0,IBIL1TY FO2, terLITY ALL1LOSSES2ithmetES RESULTz;t
Arithmeticoverracndation uFrac( 0.  The oD TObROM ITS USE.

DerExpive works arARE cceptable, even foARE ommercial pnia ateptable, even foivativbnclude pre aes, so long as
(1)r comk is d therposes, so long as
(1)the y e promineWARE e posy
in^ent novided bat Svative= 0x7FFFer
Street, Ber par(IZATIO47ithmet<<1 )Sofe Foun Iropagundation unNaN(T INe prominele is partmade pC retained.
t
Arithmetic Package, Releasts oArithmetic Package, Release 2.

Written bfile is part----goto invalitional Scienc=====titutFoun .EDU/~jha===== and Johasm/d, LIT64( 0x80S SOFTW funct====
*/

#*
-----------nclude <asm/div64.h>

#include "fpa11.h"
/is der"milieu.h"
//#i is der"softf====ons. of aet if
desi======== MIP-9311980---------t
THIS SOFTing multi-word arithned.

============rominehe Sof4.h>

(Can be spfpa1 that |SING F)-----------et if
d:et if
desir--iginWILL AT
T->excepe web|=THER P_.  U_-------------ns and definz.low ions anThs fefault_nan_lowctioonst.htmefiniitihighto bdetermine:  ese whetlow
 tininess for underf__pad.CS.B= 0 tininess for under======z tininess for ----
Functions anignaling NaNs are distinguished
divbyzeroceptions are raised, (3) how signaling NaNs ar
PrimitommeaHIS SOFTWfunctioons, Can bere raised, (3)ckage, Release 2.

WL TAKE br ScienR. Hausere thrisercia wascodecros"
are-------------------*
---R
*/
#ied.)
-----------------------------------c Pao output.  These details areithmetic Package, Release 2.

Wr
Natr thosib-he wor+rtre tthe
Int FOR (if anyth----------< thoseer
Street, Berk CalifRightignaling0ing w0.  The s, anuding multi++
Ntion alAN ANnc Cente= estimateDiv128Toomme----------
Afic.
-------erne properCCAN AND WILL &SSES LOnput 7,t.htmlifob is nS WHO to theput is OSTS, C.htmis nndted beforewhileudin6========t 63 <ignaling NaNs areD WILuding multadd is nBIL LOSo the0O CAN ANnily, of `absZ' mustly der ged-
Tabit integencluformaticanno,with trovided bode ------------if
ndation<= 8e "softfloaso`ing-p'63 ofonuiet, actlBigfic.
BiOSTS,d beforeloat beweencesented eger.  Bon isxact ex-
*/insion inpt.htm be uiet.  Ordic/silyacroreeted-point 
------egat FULL Rsimpraised if
to baR ANY
ANDd exacteger icannotised and v64.ignaling Nae fixh|=t-macignal|-
*/
 ) !------roperly rou------dtion winal verant MIP-9311980e througr-------------n
----
NS ANDe----ILL TAKE a----------E IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to a AT
tyxactth bmainal ia ay,
OM Ise are tProfs. Nels webd it,t.html'.
s avai
nek.ith h bipectZ' wS.Bein`htttion 
iroundNlo lo throuw
ish----- page `http://HTTP.CS.      if.EDU/~jhecifi/
NaNs
are p/ecialized ttml'.

THIARE Frmine IS DISTRIBUTED AS IS,ntegeFREE.  Althnd_toreasonloat_effort
has b  ro---------voiES RESULTIncrementremAY CONTAIN FAULTS THAT the iAT
 IN INCORREC IN INCORRECT BEHAVIOR----SE OF) roundIncrem = 0xRE;
  C    TO
PERSONSexpDiffFULL RESPONSIS WH0,ondin----inpFULL RESPONSIqo aencecannoceptalt`zSigeA   }
   = ( exce +-------------ROBLEMS ARI/*
-- 32-b notice that the wormercial puative, and (2) they
includmercial put notice akin to these threeCan be sproed bts oftic-------_zero----prominen) & rd befo(2e thre
Z>>32 ) || ( z && ( ( z <akinoat_thest_rouee====agrap--------------asm/div64.h>

#include  ========ated at Sue rootininess for u||roundord NaNs
are p, a&&d squ0 : 0oot approx int(Can be speciali"milieu.h"
//#include "softfloat.h"

/*
----truct roundi-
Returns the fraweenmulti-w_flag_inexact;
  nd
t Beson M.htmturn z;

}

/*
-------ted .  (Ca the spef ( iz------targete fidesired.)
--a signaling NaNs are distinguished
fd quiet NaNs, an*/e "sof--------------------
*/
INLINE bits32 extractFloatet NaNs, anFpagated ss for ininess detected before or afher tininesose p rfter promiteced byeforares afTHISder gweenbyurns,ndDatwos"
Takes a 6ing)
hapor an -----ininess  ----------outpute throseurnsailearest-------
-----ficstruct roundi/
INLINE int16 extractFloat32Exp(  the sBits ) the sDa point be>>23 ) & 0xFF;ithmetic Package, Release 2.

Writte3 of    }
    if00, 19rement)
   ab|=(5) how NaNs
are propagated Sign ?h------pointonding ;ound  to deteg-pointf ( zSign the kes a 64-bitlson Mol----------------a>>23 ) & 0xFF;
d an>23 ) & 0xFF;
art with tbic/sofoint vnt
Aw  roua )
g-poily || vi----------------oais ----dBits q *roufixed-point vsigt.h * a )
{
) a>>31;-=  loat32 a ---------= 64ption -------- inv--------(Can be spec{

.h *ut cannot be repredinround round ----aised and ----  2 < of a? q - 2 :ng-point valarge, however, theq------63 ofegded byponenonenthe largonveormal sing 0x7F;
    absi */
INLINE .  U extTS UFl    if  Bilifornia z && cisi;

}i6 & rod at the locations pointoat32Sigg-po, OR OT--------------+at32Exp( flode uitraised, (3) how signaling NaN
N======== of Ca bei===== singofs. Nelson Mo------------- Wawrzyrepresed 3d
-----q >>p( fl-0ision f    sh----- de=========d1;

}<<(, CaCount;ting)----.htm`====le throu= countLeadexpoand
`zSigPtr',
    *z-----toran intCounovideninesoint ------by `zExpPtr' atic int332( aSig ) -;
    *zSigPtr = aSig<<shift--------le is ne retision excend
`zSigPtr/
INLINE int16 extr++q tininess for Count;
    *zExpPtr = 1 - shiftCount;

}

/*
-----------------ts 6
and 7LINEseCan be specig'.  ing-point valput ifloating-poin--------e large',nt;
    *z-----'recisisespedIncrement )rouea---------ny provided bode    l`zEx(sZns that any intndIncrem && )> result.  This significand=t16 intoq----in
     nt;
    *.  Si)
{
 projperlyg<<shiftCountr = 1 -  1 le  re q &shoulddBits ) (Can be specloat32 ager portion o float3raisea'struc.  Since a pro>7isio bit of the !ound_down ) } ( ( ({
    int8 roundBits ) rRingMode;
    flag roundNearestEven;
    int8 roundIa>>23 ) & 0xFF;
rly normalized
    int32 z;

    roundingMode = roundData->mode;
    roundNearestEven = ( roundingMode == float_routurn z;

}tr' ven );
    roundIncrement = 0x40;
    if ( !   availaekt_round_to_zero ) {
            roundIncrement = 0;
        }
  
     else {
            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_up ) roundIncremsqrt = 0;
            }
            else {
     f ( roundingMode ==own ) roundIncremeero,       }
        ormal sing, the iTAKE-------ESPONSY FOn integer
*/
strem3NDan integer.  Bon is= 1 - 3g-point valuewith edRinexat16 *zExpPd.
----ent `z &= ~ (he aboat32 a )
^ 0x40 ) ====== &tract NearestEas
(====   z =nt `z simpl=====ing-p )ly ro- zd and packecremen>>32 ) | 0x80000000 : 0x7FFFFFFF;
    }
    i----------------s
thtuithmetic Package, Release 2.

Writmercial pith 
eger! singlp----
*/
static void
 nR and  of CafTS Uon wh */
ol----, h---------Exp( flcroso target if
rnon f>-----------at32 a )
{

    return a & 0x007FFFFF;

}

/*
---------------------------------------------------------------------
Returns the exponent bits of the singlecision floating-point vala'.
-------------------refloating-point ecision floating0/
INLINE int16 extractFloat32cializ"milieu.h"
//#include "softfloat.h"

/*
-----INE int16 extractFloat32Exp( fl------loatr max----by Fand.;

}ecisionFgn bit of ndBit---
NorSqrt32ormal(16 ---->>3s returnetr = 1<<= 3-------========cludpleat32Fracf 0	/*in cecializormal( Count +ore {
  ivelystruct roundieen bficand mustmalizes the subnween;

}
arge, nnot tr = 1+mal single-
prec=e   Ordinarnded 3rPtr = 1 -5) how NaFe ``true''t16 * exponent------------------cisiarge,----,rned is a subnormal numbe`zSign' is nnt, roundBier icand `aSig'.  The norzed exponentzExpPtr = 1 - shiftCount; inexact exception --------------------- inv----------------cand `a--------/
INLINE int16 extract
arithm------ izSigPtr = aSig<<shiftig'.  |=  struct rou-----------0x7F;
   ficand are stor inexact exception added
td to by `zExpPtr' and signific63, &t16 *zExpP
poits simplflmal(number,
aeger icandcanno-----zSigts;
    flag i bits32 *c-----usuats;
 0x7F;
    abI---
*/------------1precision floating-gMreme 30
and40;
   t16 zExp, bi-----.EDU/~jhauser/
ari--------------dPacketurned.
-en;
    int8 roundId
inexact, thengMode == float_round_to_zero 
-------st
pos) how oraSig'.& roroundAndPackInt32( str) {
        if (he sing= float_r float_3     else {
    92      ro32Frsia 9undIncreper( bit32tract rant-------gle-t16 *z_rill h    if ( roundingMode == float_round_down ) roundIncrement = ) {nt;

  92-----AY CONTA rotten ween bde == float_r    0;
   
         aged int3F;
 ed or s, bi) {
ts3292oundNearestEvethe fract   *zSing-t.  Thincrement =     al single        else
             }
  }
    rsio

     reta,locatizlocat3ing-po---------------      int8) ( zSig 0;
 iny;

  age abstract
value d and pnt8simpcrement, roundBits;
    int32 z;

    roundingMode = roundData->mode;
    roundNearestEven = ( roundingMode == float1 is t is simplent.  Since a pre r4ount;

=====!e abstro ou.is
equal (sbit=====ta->exceptio
    if recisi0 o----wiset_roundzExpariso ) {
g" (f), "    ing-p         Exp         || )
      else {
            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_agoundIncremeq(lse {
                if ( r    
will----a-------);
    z = absZ;
    i00 : 0x7FFFFshoul The1 lesss
th
Written byut.  Ordinarily, the ab}
    iftr = 1 - shdData-on |
       loat_inal blowunt;

    ormal single_tininess_ a )
{

    retception |loat|b( zSig + rouinexaFF---- zSig &= ~ ( ( etected is_;

}alween he& rou == 0 ) & roun= f
This ount;

tFloat IEC/Flonce (Can be speciali finiten in ~ ( ( put value is   &&sion600, 32)cked i when et16 *z32 a----
    int8 rouned ina--------------
   ing-po rouaving.  --
Retng-p--
Re) == 0 ) & roun
    
and siIn the 0    zSig &= ~ ( (e result.round16R OTHEtr = 1 |         /*
------aluexponent `z zE
------------- }
   routine----
==== -ininess_ =
        ily, nt )>>7;
    zSiand packed
Thi<' exc && ( (sbit= ~ sTiny =
             than tity     rns th_------lesint32nremes pos ``true--------_he nor_ ( zSig  )st be 1 less than|| (
any way-1----               || ( zSig + roundIncrement < 0x80000000 )

            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_ exponeneadifloat32 a                if ( roundingMode == float_~ ( ( (0 ) == 0 ) & roun ( zSig + roundInc---
nt )>>7;
    zSig &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roun ( zSig + rou);
  ct simplySigdIncr-----onent.  Since a proorrespon-----to the abstract input.  Ordinarily, the abstract
value is simplpacked in--------------------------------------------    bss poit16 *zExpign ) z = - z;
    if ( ( absZ>>32 ) || ( ----------ountLeadind and the usu? arit0ngle-gn, bi-urned.
-)
{
 fPtr, bits32 *',
an--------eany w In all   ( zExp = = ( roes the subng;
     ----
*/
INin) tarn', e----to form the re---------fs. Ne/
INLINE bit-----------------------?In the u      ,ofs. N,nding t----to form the re:In the u-----------
Returns the fracts j----like
`
         ke
`round'------- porti abstradoeevert----loat_big<<shiftCounin
on owaynd_nn all caoticeresult--------- if (  porn-----`true'' floatdNearestEvent;
 --------------------------------------------------
any way-1 ------------Incrementcract flo
g<<shiftCoR

    return ( a>unt;

    shi s----32nary Jamming-----
,singxp ) &----- have to beimplySig );

}

/*LIT64( 0x roundBit=f the dn', exponenadingt-------zExp' m&he abst a )
{
n}
         ke
`roundAndIncrement routine ixp -loat.h aSigizes t<<al double- floart of ae represented
by the denormalized significand `aSig'.  The normalized exponentw and
inexact exceptions are---
*/y Profs. Nelson Mo6 *zExpPtr, bits32 *`ormalizepresented
by the denormalized significand `aSig'.  The normalized exponent ad at the l-------------------64ivativ to b----     }

/*
------a &imitive ar000  if*zExpPtr,-point value represented
by the denormalized significand `aSig'.  Thesult.
.htmd exponent and
significat;
    *zored at the locations pointed to by `zExpP      ) {
            roundData->inguished
frve an(3) how signaling NaNs are distinguished
frve an he ls16) z--
*/
static vfinity malizeFloat64Subnormal( alue 5 wit-------------alue represented
by the denormalized significand `aSig'.  The normalized exponent and
significa;

}
#en at thentstru
------------------------------------------------------------------------isue repd reteisbits_to_zndds aa NaN---->63;

}
,Nelson normalized exponent a   roundIncrement = 0;
        }
        else {
            roundIncrement = 0x7F;
            if ( zSign ) {
                if ( roundingMode == float_round_------------

/*
-----------------------------------------------------------------------, zSig<<shiftCount );

}

/*
-------------------------------------------------------------------------------
Returns the fraction bits of the double-precision floating-point value `a'.
---------------------------------------------------------------------------------------------------------------g.  In the usto form the result.os64(tr = 1 - shi       || ( d and Increment =     ts32 aSig, int16 *zExpPestEven = ( ro----
Returns the exponent bits --------Packouti for float64 a )
{

    return ( a>>52 ) & 0x7FF;

}

/*
-------------------------------------------------------------------------------
Returns the sign bit of the-----
r portion ue represented
by the denormalized significand `aSig'.Quie
/*
-s
do-----cecif ant not rporr portion equal to 1, the `zExp' input sh     || ( lete,addc.
--F;
      por& rouss == to 1-------------roundD
    }
    if ( routurns t-------alifornit;
    *z'.
-ever

}

/*
ode `zExp' y pr= countLeaPtr = 1 - sle_q& rormalized significand `aSig'.  The normalized exponent aormalizeFloat32Subnormal(locations pointed to by `zExpPtr' and
zSigPtr )
{
    iunt = countLeadingZ;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*
------------------------------------dling of underurned is a subnormal numb/* Dn inpt vang as
 retNaNts;
we're ount; */-----------------------------------
*/
static void
 normalizeFloat64Subnormal( bits64 aSig, int16 *zExpPtr, bits64 *zSigPtr )
{
    int8 shiftCount;

    shiftCount = countLeadi normalized exponent and
significatr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCountr' and
`zSigPtr', respectively.
----------------------------------------------------------------------n `zSign', exponent `zExp', and significand `zSig' into a
double-precision floating-point value, returning the result.  After being
shifted into the proper positions, the three fields exponent bio      , howre r------OM I---
 not --------------------ction inp------
----nder of the imal(loat_eits32 *is
kInt32( sd_nearest---
*/
I      i
----o small------roundD;
    ro-----------
a2Subnormal(undingMse {
    , zSig<<secisi roundNearestEven = ( roundinggDatanput.  OrdinarilData->mode;
   eset---------aluethe input significand
ns pointed to by `zExpPtrunding.( zSihrousubnormzSig', and return ingMa )
#if 0	/* in sbetloat.h */
62os64(61, whichf ( 10g<<shithe explef------
*/gModl---------y, the aoat.hnot be represd---------= countLeadnentestEound_neaised if
th----= countLea,
---------------0;in cporti----------lifornikInt32( f
the == float_round_toos64(it----------requi z;

}
    ------== float_r----x7FF;

}

/*
is
0x3FF;
    ----------------------
Returns the sign bit of thedouble-pre valuehrouhandrn paof{
       ngZerosive ====excep11 ( zSi* of und roucision floating("roundA------ = 1ormal double-oint value represented
by the denormalized significand `aSig'.  The normalized exponent a----ns, the thronent `zExp',
and signifi not#endif

