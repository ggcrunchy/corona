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

typedef void (*CoronaCommandReader)(const unsigned char *, unsigned int);
typedef void (*CoronaCommandWriter)(unsigned char *, const void *, unsigned int);

typedef struct CoronaCommand {
	CoronaCommandReader fReader;
	CoronaCommandWriter fWriter;
} CoronaCommand;


/**
TODO
*/
typedef struct CoronaGraphicsToken {
	unsigned char bytes[3 * sizeof(void *)];
} CoronaGraphicsToken;

typedef void (*CoronaRendererOp)(const CoronaGraphicsToken *, void *);

// Internal
void CoronaGraphicsTokenWrite( CoronaGraphicsToken * tokens, unsigned char type, const void * data, unsigned int size );
void CoronaGraphicsTokenRead( void * buffer, const CoronaGraphicsToken * tokens, unsigned int size );
unsigned char CoronaGraphicsGetTokenType( const CoronaGraphicsToken * tokens );

void CoronaGraphicsEncodeAsTokens( CoronaGraphicsToken token[], unsigned char type, const void * data );
void * GetRenderer( const CoronaGraphicsToken tokens[] );

#endif // _CoronaGraphicsTypes_H__
