/*
** $Id: lnum.h,v ... $
** Internal Number model
** See Copyright Notice in lua.h
*/

#ifndef lnum_h
#define lnum_h

#include <math.h>

#include "lobject.h"

/*
** The luai_num* macros define the primitive operations over 'lua_Number's
** (not 'lua_Integer's).
*/
#define luai_numadd(a, b)	((a)+(b))
#define luai_numsub(a, b)	((a)-(b))
#define luai_nummul(a, b)	((a) * (b))
#define luai_numdiv(a, b)	((a) / (b))
#define luai_nummod(a, b)	((a)-floor((a) / (b)) * (b))
#define luai_numpow(a, b)	(pow(a, b))
#define luai_numunm(a)		(-(a))
#define luai_numeq(a, b)	    ((a) == (b))
#define luai_numlt(a, b)	    ((a) < (b))
#define luai_numle(a, b)	    ((a) <= (b))

/*
* If '-ffast-math' is used, there are no NaNs or Infs. We shouldn't pretend
* there is.
*/
#ifdef __FAST_MATH__
# define luai_numisnan(a) (0)   /* has no concept of NANs */
#elif (defined __STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
# define luai_numisnan(a) isnan(a)   /* same regardless of number size */
#else
# define luai_numisnan(a) (!luai_numeq((a), (a)))
#endif

int try_addint(lua_Integer* r, lua_Integer ib, lua_Integer ic);
int try_subint(lua_Integer* r, lua_Integer ib, lua_Integer ic);
int try_mulint(lua_Integer* r, lua_Integer ib, lua_Integer ic);
int try_divint(lua_Integer* r, lua_Integer ib, lua_Integer ic);
int try_modint(lua_Integer* r, lua_Integer ib, lua_Integer ic);
int try_powint(lua_Integer* r, lua_Integer ib, lua_Integer ic);
int try_unmint(lua_Integer* r, lua_Integer ib);

LUAI_FUNC int luaO_str2d(const char* s, lua_Number* res1, lua_Integer* res2);
LUAI_FUNC void luaO_num2buf(char* s, const TValue* o);

LUAI_FUNC int /*bool*/ tt_integer_valued(const TValue* o, lua_Integer* ref);

#endif