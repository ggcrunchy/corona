/*
** $Id: lobject.h,v 2.20.1.2 2008/08/06 13:29:48 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/* tags for values visible from Lua */
#define LAST_TAG	LUA_TTHREAD

#define NUM_TAGS	(LAST_TAG+1)


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	(LAST_TAG+1)
#define LUA_TUPVAL	(LAST_TAG+2)
#define LUA_TDEADKEY	(LAST_TAG+3)


/* NaN-boxing64 */
#if LUA_PACK_VALUE == 64

#define LUA_TBOX (LAST_TAG+4)

#endif
/* /NaN-boxing64 */

/*
** Union of all collectable objects
*/
typedef union GCObject GCObject;


/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked

/* NaN-boxing64 */
// could pack `CommonHeader' stuff + bytes together with byte fields (with 45 bits for `next' could even get closures all in 64 bits...)
// but we take address of these, so bitfields might be awkward
// plus there is an `a = b = c` with `b' being `next'
/* /NaN-boxing64 */

/*
** Common header in struct form
*/
typedef struct GCheader {
  CommonHeader;
} GCheader;




/*
** Union of all Lua values
*/
typedef union {
  GCObject *gc;
  void *p;
  lua_Number n;
  int b;
} Value;


/*
** Tagged Values
*/

#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

#define TValuefields	Value value; int tt
#define LUA_TVALUE_NIL { NULL }, LUA_TNIL /* NaN-boxing */

typedef struct lua_TValue {
  TValuefields;
} TValue;

/* NaN-boxing64 */
#else

#if LUA_PACK_VALUE == 32

#define TValuefields	union { \
    struct { \
    int _pad0; \
    int tt_sig; \
    } _ts; \
    struct { \
    int _pad; \
    short tt; \
    short sig; \
    } _t; \
    Value value; \
}
#define LUA_NOTNUMBER_SIG (-1)
#define add_sig(tt) ( 0xffff0000 | (tt) )

#elif LUA_PACK_VALUE == 64

/* Some good commentary may be found at https://craftinginterpreters.com/optimization.html#nan-boxing */

#define TValuefields	union { \
    uint64_t u; \
    Value value; \
}

#define LUA_NAN_SIGN_MASK ((uint64_t)0x8000000000000000)
#define LUA_NOTNUMBER_SIG ((uint64_t)0x7FFC000000000000) /* 11 exponent bits, quiet NaN bit, IND bit */
#define LUA_BOXED_PAYLOAD_MASK (LUA_NAN_SIGN_MASK | LUA_NOTNUMBER_SIG)

#define LUA_NAN_PAYLOAD_SHIFT 4 /* type is > 0 and < 16, so 4 bits (0 can be used for actual NaNs) */
#define LUA_NAN_TAGGING_BITS 3
#define LUA_NAN_TAGGING_MASK ((1ULL << LUA_NAN_TAGGING_BITS) - 1ULL)

#define LUA_NAN_POINTER_SHIFT (LUA_NAN_PAYLOAD_SHIFT - LUA_NAN_TAGGING_BITS)

#define LUA_NAN_SQUEEZED_BIT (1ULL << LUA_NAN_PAYLOAD_SHIFT)

#define LUA_NAN_TYPE_MASK ((uint64_t)0xF) 
#define LUA_NAN_PAYLOAD_MASK ((uint64_t)0x3FFFFFFFFFFF0) /* remaining bits */

static const int STATIC_ASSERT_FULL_PAYLOAD[LUA_NAN_TYPE_MASK == (1ULL << LUA_NAN_PAYLOAD_SHIFT) - 1ULL] = { 0 };
static const int STATIC_ASSERT_USES_ALL_BITS[(uint64_t)(LUA_NAN_SIGN_MASK | LUA_NOTNUMBER_SIG | LUA_NAN_PAYLOAD_MASK | LUA_NAN_TYPE_MASK) == ~0ULL] = { 0 };

#define add_sig(tt) ( LUA_NOTNUMBER_SIG | (tt) )

#else /* One-time check */
    error "Bad NaN packing #define constant"
#endif

#define LUA_TVALUE_NIL {0, add_sig(LUA_TNIL)}

typedef TValuefields TValue;

#endif
/* /NaN-boxing64 */

/* Macros to test type */

#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

#define ttisnil(o)	(ttype(o) == LUA_TNIL)
#define ttisnumber(o)	(ttype(o) == LUA_TNUMBER)
#define ttisstring(o)	(ttype(o) == LUA_TSTRING)
#define ttistable(o)	(ttype(o) == LUA_TTABLE)
#define ttisfunction(o)	(ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o)	(ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o)	(ttype(o) == LUA_TUSERDATA)
#define ttisthread(o)	(ttype(o) == LUA_TTHREAD)
#define ttislightuserdata(o)	(ttype(o) == LUA_TLIGHTUSERDATA)

/* NaN-boxing */
#else

#define ttisnil(o)	(ttype_sig(o) == add_sig(LUA_TNIL))

#if LUA_PACK_VALUE == 32 /* !NaN-boxing64 */

#define ttisnumber(o)	((o)->_t.sig != LUA_NOTNUMBER_SIG)

/* NaN-boxing64 */
#else

#define ttisnumber(o)	(ttype_sig(o) <= LUA_NOTNUMBER_SIG)

#endif
/* /NaN-boxing64 */

#define ttisstring(o)	(ttype_sig(o) == add_sig(LUA_TSTRING))
#define ttistable(o)	(ttype_sig(o) == add_sig(LUA_TTABLE))
#define ttisfunction(o)	(ttype_sig(o) == add_sig(LUA_TFUNCTION))
#define ttisboolean(o)	(ttype_sig(o) == add_sig(LUA_TBOOLEAN))
#define ttisuserdata(o)	(ttype_sig(o) == add_sig(LUA_TUSERDATA))
#define ttisthread(o)	(ttype_sig(o) == add_sig(LUA_TTHREAD))
#define ttislightuserdata(o)	(ttype_sig(o) == add_sig(LUA_TLIGHTUSERDATA))

#endif
/* /NaN-boxing */


/* Macros to access values */
#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

#define ttype(o)	((o)->tt)

/* NaN-boxing */
#elif LUA_PACK_VALUE == 32 /* !Nan-boxing64 */

#define ttype(o)	((o)->_t.sig == LUA_NOTNUMBER_SIG ? (o)->_t.tt : LUA_TNUMBER)
#define ttype_sig(o)	((o)->_ts.tt_sig)

#else

#define ttype_sig(o)	((o)->u & (LUA_NOTNUMBER_SIG | LUA_NAN_TYPE_MASK))
#define ttype(o)	(ttype_sig(o) > LUA_NOTNUMBER_SIG ? (o)->u & LUA_NAN_TYPE_MASK : LUA_TNUMBER)

#endif
/* /NaN-boxing */

/* !Nan-boxing64 */
#if LUA_PACK_VALUE < 64

#define gcvalue(o)	check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o)	check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)
#define rawtsvalue(o)	check_exp(ttisstring(o), &(o)->value.gc->ts)
#define tsvalue(o)	(&rawtsvalue(o)->tsv)
#define rawuvalue(o)	check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define uvalue(o)	(&rawuvalue(o)->uv)
#define clvalue(o)	check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o)	check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o)	check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o)	check_exp(ttisthread(o), &(o)->value.gc->th)

#else

    #define BIT_48 0x1000000000000
    #define BITS_ABOVE_48 0xFFFE000000000000
    #define BIT_49 0x200000000000
    #define BITS_ABOVE_49 0xFFFC000000000000

    /*
      https://stackoverflow.com/questions/6716946/why-do-x86-64-systems-have-only-a-48-bit-virtual-address-space/45525064#45525064
      https://stackoverflow.com/a/66249936
    */
    #if defined(__x86_64__) || defined(_M_X64)
    #define signextend(v) v.u |= (v.u & BIT_48) ? BITS_ABOVE_48 : 0
    #elif defined(__aarch64__) || defined(_M_ARM64)
    #define signextend(v) v.u |= (v.u & BIT_49) ? BITS_ABOVE_49 : 0
    #else
    #define signextend(v)
    #endif

    static inline Value getupointervalue (const TValue* tv)
    {
        TValue copy = *tv;

        copy.u &= LUA_NAN_PAYLOAD_MASK;

        int shift = (copy.u & LUA_NAN_SQUEEZED_BIT) ? LUA_NAN_POINTER_SHIFT + 1 : LUA_NAN_PAYLOAD_SHIFT + 1;

        copy.u &= ~LUA_NAN_SQUEEZED_BIT;
        copy.u >>= shift;

        signextend(copy);

        return copy.value;
    }

    static inline Value getpointervalue (const TValue* tv)
    {
        TValue copy = *tv;

        copy.u &= LUA_NAN_PAYLOAD_MASK;
        copy.u >>= LUA_NAN_POINTER_SHIFT;

        signextend(copy);

        return copy.value;
    }

    #define gcvalue(o)	check_exp(iscollectable(o), getpointervalue(o).gc) 
    #define pvalue(o)	check_exp(ttislightuserdata(o), ((o)->u & LUA_NAN_SIGN_MASK) ? getpointervalue(o).gc->u.uv.env : getpointervalue(o).p)
    #define nvalue(o)	check_exp(ttisnumber(o), (o)->value.n)
    #define rawtsvalue(o)	check_exp(ttisstring(o), &getpointervalue(o).gc->ts)
    #define tsvalue(o)	(&rawtsvalue(o)->tsv)
    #define rawuvalue(o)	check_exp(ttisuserdata(o), &getpointervalue(o).gc->u)
    #define uvalue(o)	(&rawuvalue(o)->uv)
    #define clvalue(o)	check_exp(ttisfunction(o), &getpointervalue(o).gc->cl)
    #define hvalue(o)	check_exp(ttistable(o), &getpointervalue(o).gc->h)
    #define bvalue(o)	check_exp(ttisboolean(o), ((o)->value.b & LUA_NAN_PAYLOAD_MASK) != 0)
    #define thvalue(o)	check_exp(ttisthread(o), &getpointervalue(o).gc->th)

#endif

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

/*
** for internal debug only
*/
#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

/* NaN-boxing */
#elif LUA_PACK_VALUE == 32 /* !Nan-boxing64 */

#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch._t.tt))

#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  ((ttype(obj) == (obj)->value.gc->gch._t.tt) && !isdead(g, (obj)->value.gc)))
    // ^^ TODO: are these right? (gch has no _t, correct?)

#else

/* methods defined in lgc.c */

#define checkconsistency(obj) \
  lua_assert(!iscollectable(obj) || (typesmatch(obj) != islargeobjectboxed(obj)))

#define checkliveness(g,obj) \
  lua_assert(!iscollectable(obj) || \
  (typesmatch(obj) != islargeobjectboxed(obj) && !isdead(g, getpointervalue(obj).gc)))

#endif
/* /NaN-boxing */


/* Macros to set values */
#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

#define setnilvalue(obj) ((obj)->tt=LUA_TNIL)

#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); i_o->tt=LUA_TNUMBER; }

#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->tt=LUA_TLIGHTUSERDATA; }

#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->tt=LUA_TBOOLEAN; }

#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TSTRING; \
    checkliveness(G(L),i_o); }

#define setuvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TUSERDATA; \
    checkliveness(G(L),i_o); }

#define setthvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTHREAD; \
    checkliveness(G(L),i_o); }

#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TFUNCTION; \
    checkliveness(G(L),i_o); }

#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TTABLE; \
    checkliveness(G(L),i_o); }

#define setptvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->tt=LUA_TPROTO; \
    checkliveness(G(L),i_o); }




#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; o1->tt=o2->tt; \
    checkliveness(G(L),o1); }

/* NaN-boxing */
#elif LUA_PACK_VALUE == 32 /* !Nan-boxing64 */ /* LUA_PACK_VALUE != 0 */

#define setnilvalue(obj) ( ttype_sig(obj) = add_sig(LUA_TNIL) )

#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n=(x); }

#define setpvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.p=(x); i_o->_ts.tt_sig=add_sig(LUA_TLIGHTUSERDATA);}

#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.b=(x); i_o->_ts.tt_sig=add_sig(LUA_TBOOLEAN);}

#define setsvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->_ts.tt_sig=add_sig(LUA_TSTRING); \
    checkliveness(G(L),i_o); }

#define setuvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->_ts.tt_sig=add_sig(LUA_TUSERDATA); \
    checkliveness(G(L),i_o); }

#define setthvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->_ts.tt_sig=add_sig(LUA_TTHREAD); \
    checkliveness(G(L),i_o); }

#define setclvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->_ts.tt_sig=add_sig(LUA_TFUNCTION); \
    checkliveness(G(L),i_o); }

#define sethvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->_ts.tt_sig=add_sig(LUA_TTABLE); \
    checkliveness(G(L),i_o); }

#define setptvalue(L,obj,x) \
  { TValue *i_o=(obj); \
    i_o->value.gc=cast(GCObject *, (x)); i_o->_ts.tt_sig=add_sig(LUA_TPROTO); \
    checkliveness(G(L),i_o); }




#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; \
    checkliveness(G(L),o1); }

#else

#define setnilvalue(obj) ( ((TValue *)(obj))->u = add_sig(LUA_TNIL) )

// todo: if NaN, mask off type bits...
static inline lua_Number canonicalizeifnan(lua_Number n)
{
    TValue tv;
    tv.value.n = n;

    if ((tv.u & LUA_NOTNUMBER_SIG) == LUA_NOTNUMBER_SIG)
        tv.u &= ~LUA_NAN_TYPE_MASK;

    return tv.value.n;
}

#define setnvalue(obj,x) \
  { TValue *i_o=(obj); i_o->value.n = canonicalizeifnan(x); }

#define LUA_BITS_UP_TO_48 ((uint64_t)~BITS_ABOVE_48)
#define LUA_BITS_UP_TO_45 (LUA_BITS_UP_TO_48 >> 3)

static inline uint64_t packlightuserdata (void* p)
{
    TValue tv;
    tv.value.p = p;

    if (tv.u <= LUA_BITS_UP_TO_45) /* raw value will fit */
        tv.u <<= LUA_NAN_PAYLOAD_SHIFT + 1; /* leave one bit for 0 ("squeezed?") */
    else if ((tv.u & LUA_NAN_TAGGING_MASK) == 0 && tv.u <= LUA_BITS_UP_TO_48) { /* would fit without tagging bits */
        tv.u <<= LUA_NAN_POINTER_SHIFT + 1; /* leave one bit for 1 */
        tv.u |= LUA_NAN_SQUEEZED_BIT;
    }
    else /* too large */
        return 0ULL;

    return add_sig(LUA_TLIGHTUSERDATA) | tv.u;
}

#define setpvalue(obj,x) \
  { TValue *i_o=(obj); if (!(i_o->u = packlightuserdata(x))) boxpointer(L, obj, x); } /* n.b. only called in lapi.c, where declared */

#define setbvalue(obj,x) \
  { TValue *i_o=(obj); i_o->u = add_sig(LUA_TBOOLEAN) | ((x != 0) << LUA_NAN_PAYLOAD_SHIFT); }

// cf. note above for signextend()
#if defined(__x86_64__) || defined(_M_X64)
#define maskhighbits(v) v.u &= ~BITS_ABOVE_48
#define sehighbits(v) BITS_ABOVE_48
#elif defined(__aarch64__) || defined(_M_ARM64)
#define maskhighbits(v) v.u &= ~BITS_ABOVE_49
#define sehighbits(v) BITS_ABOVE_49
#else
#define maskhighbits(v)
#define sehighbits(v) 0
#endif

static inline uint64_t packgcvalue (GCObject * gc)
{
    TValue tv;
    tv.value.gc = gc;

    lua_assert((tv.u & LUA_NAN_TAGGING_MASK) == 0);
    lua_assert((tv.u & sehighbits()) == 0 || (tv.u & sehighbits()) == sehighbits());

    maskhighbits(tv);

    return tv.u << LUA_NAN_POINTER_SHIFT;
}

#define setgctvalue(L,obj,x,t) \
  { TValue *i_o=(obj); \
    i_o->u = add_sig(t) | packgcvalue(cast(GCObject *, (x))); \
    checkliveness(G(L),i_o); }

#define setlargepvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TLIGHTUSERDATA)

#define setsvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TSTRING)
#define setuvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TUSERDATA)
#define setthvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TTHREAD)
#define setclvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TFUNCTION)
#define sethvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TTABLE)
#define setptvalue(L,obj,x) setgctvalue(L,obj,x,LUA_TPROTO)




#define setobj(L,obj1,obj2) \
  { const TValue *o2=(obj2); TValue *o1=(obj1); \
    o1->value = o2->value; \
    checkliveness(G(L),o1); }

#endif
/* /NaN-boxing */


/*
** different types of sets, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj
#define setsvalue2s	setsvalue
#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to table */
#define setobj2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

#define setttype(obj, tt) (ttype(obj) = (tt))

/* NaN-boxing */
#elif LUA_PACK_VALUE == 32 /* !NaN-boxing64 */ /* LUA_PACK_VALUE != 0 */

/* considering it used only in lgc to set LUA_TDEADKEY */
/* we could define it this way */
#define setttype(obj, _tt) ( ttype_sig(obj) = add_sig(_tt) )

#else

/* per comment above */
#define setttype(obj, _tt) ( (obj)->u = add_sig(_tt) )

#endif
/* /NaN-boxing */

#if LUA_PACK_VALUE < 64 /* !NaN-boxing64 */

#define iscollectable(o)	(ttype(o) >= LUA_TSTRING)

/* NaN-boxing64 */
#else

#define iscollectable(o)	(ttype_sig(o) >= add_sig(LUA_TSTRING) || (((o)->u & LUA_BOXED_PAYLOAD_MASK) == LUA_BOXED_PAYLOAD_MASK))

#endif
/* /NaN-boxing64 */

typedef TValue *StkId;  /* index to stack elements */


/*
** String headers for string table
*/
typedef union TString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  struct {
    CommonHeader;
    lu_byte reserved;
    unsigned int hash;
    size_t len;
  } tsv;
} TString;


#define getstr(ts)	cast(const char *, (ts) + 1)
#define svalue(o)       getstr(rawtsvalue(o))



typedef union Udata {
  L_Umaxalign dummy;  /* ensures maximum alignment for `local' udata */
  struct {
    CommonHeader;
    struct Table *metatable;
    struct Table *env;
    size_t len;
  } uv;
} Udata;




/*
** Function Prototypes
*/
typedef struct Proto {
  CommonHeader;
  TValue *k;  /* constants used by the function */
  Instruction *code;
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines */
  struct LocVar *locvars;  /* information about local variables */
  TString **upvalues;  /* upvalue names */
  TString  *source;
  int sizeupvalues;
  int sizek;  /* size of `k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of `p' */
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  GCObject *gclist;
  lu_byte nups;  /* number of upvalues */
  lu_byte numparams;
  lu_byte is_vararg;
  lu_byte maxstacksize;
} Proto;


/* masks for new-style vararg */
#define VARARG_HASARG		1
#define VARARG_ISVARARG		2
#define VARARG_NEEDSARG		4


typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;



/*
** Upvalues
*/

typedef struct UpVal {
  CommonHeader;
  TValue *v;  /* points to stack or to its own value */
  union {
    TValue value;  /* the value (when closed) */
    struct {  /* double linked list (when open) */
      struct UpVal *prev;
      struct UpVal *next;
    } l;
  } u;
} UpVal;


/*
** Closures
*/

#define ClosureHeader \
	CommonHeader; lu_byte isC; lu_byte nupvalues; GCObject *gclist; \
	struct Table *env

typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];
} CClosure;


typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];
} LClosure;


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define iscfunction(o)	(ttype(o) == LUA_TFUNCTION && clvalue(o)->c.isC)
#define isLfunction(o)	(ttype(o) == LUA_TFUNCTION && !clvalue(o)->c.isC)


/*
** Tables
*/

#if LUA_PACK_VALUE == 0 /* !NaN-boxing */

typedef union TKey {
  struct {
    TValuefields;
    struct Node *next;  /* for chaining */
  } nk;
  TValue tvk;
} TKey;

#define LUA_TKEY_NIL {LUA_TVALUE_NIL, NULL} /* NaN-boxing */

/* NaN-boxing */
#else

typedef struct TKey {
    TValue tvk;
    struct {
        struct Node* next; /* for chaining */
    } nk;
} TKey;

#define LUA_TKEY_NIL {LUA_TVALUE_NIL}, {NULL}

#endif
/* /NaN-boxing */


typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


typedef struct Table {
  CommonHeader;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */ 
  lu_byte lsizenode;  /* log2 of size of `node' array */
  struct Table *metatable;
  TValue *array;  /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  GCObject *gclist;
  int sizearray;  /* size of `array' array */
} Table;



/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


#define luaO_nilobject		(&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x)	(luaO_log2((x)-1) + 1)

LUAI_FUNC int luaO_log2 (unsigned int x);
LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_rawequalObj (const TValue *t1, const TValue *t2);
LUAI_FUNC int luaO_str2d (const char *s, lua_Number *result);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

