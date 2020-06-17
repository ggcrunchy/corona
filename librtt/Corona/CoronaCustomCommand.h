//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaCustomCommand_H__
#define _CoronaCustomCommand_H__

typedef unsigned int (*CoronaCustomCommandReader)(const unsigned char *);
typedef void (*CoronaCustomCommandWriter)(unsigned char *, const void *, unsigned int);

typedef struct CoronaCustomCommand {
	CoronaCustomCommandReader fReader;
	CoronaCustomCommandWriter fWriter;
} CoronaCustomCommand;

#endif // _CoronaCustomCommand_H__
