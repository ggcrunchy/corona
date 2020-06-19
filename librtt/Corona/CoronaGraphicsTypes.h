//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaGraphicsTypes_H__
#define _CoronaGraphicsTypes_H__

typedef unsigned int (*CoronaCommandReader)(const unsigned char *);
typedef void (*CoronaCommandWriter)(unsigned char *, const void *, unsigned int);

typedef struct CoronaCommand {
	CoronaCommandReader fReader;
	CoronaCommandWriter fWriter;
} CoronaCommand;


/**
TODO
*/
typedef struct CoronaGraphicsToken {
	unsigned char bytes[2 * sizeof(void *)];
} CoronaGraphicsToken;

void CoronaGraphicsTokenWrite( CoronaGraphicsToken * tokens, unsigned char type, const void * data, unsigned int size );
void CoronaGraphicsTokenRead( void * buffer, const CoronaGraphicsToken * tokens, unsigned int size );
unsigned char CoronaGraphicsGetTokenType( const CoronaGraphicsToken * tokens );

void CoronaGraphicsEncodeAsTokens ( CoronaGraphicsToken token[], unsigned char type, const void * data );

typedef void (*CoronaRendererOp)(CoronaGraphicsToken *, void *);

#endif // _CoronaGraphicsTypes_H__
