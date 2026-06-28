//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_ShaderResource.h"

#include "Display/Rtt_ShaderData.h"
#include "Renderer/Rtt_Program.h"

#include "Display/Rtt_Display.h"
#include "CoronaLua.h"
#include "CoronaGraphics.h"

#include <string.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

int
ExtraTextureInfo::FindNameInList( const U8* name, const U8* listOfNames, int n, int* offset )
{
	int countFromName = *name++;
	const U8* readPos = listOfNames;
	for ( int i = 0; i < n; ++i )
	{
		int countFromList = BinsForLength( *readPos );
		bool isMatch = ( countFromName == countFromList ) && 0 == memcmp( name, readPos + 1, countFromList );
		
		readPos += countFromList + 1;

		if ( isMatch )
		{
			if ( NULL != offset )
			{
				*offset = (int)( readPos - listOfNames );
			}
			
			return i;
		}
	}
	
	return -1;
}

bool
ExtraTextureInfo::ListsMatch( const U8* listOfNames1, const U8* listOfNames2, int n )
{
	int offset = 0;

	for ( int i = 0; i < n; ++i )
	{
		U8 count1 = listOfNames1[offset];
		if ( count1 == listOfNames2[offset] )
		{
			offset += BinsForLength( count1 ) + 1;
		}
		else
		{
			return false;
		}
	}
	
	return 0 == memcmp( listOfNames1, listOfNames2, offset );
}

U32
ExtraTextureInfo::NamesSize( const U8* listOfNames, int n )
{
	const U8* readPos = listOfNames;
	for ( int i = 0; i < n; ++i )
	{
		int countFromList = *readPos;
		
		readPos += BinsForLength( countFromList ) + 1;
	}
	
	return (U32)( readPos - listOfNames );
}

// Encoding bit layout, for bytes 0-3:
//
// 0 0 0 0 0 0 1 1 | 1 1 1 1 2 2 2 2 | 2 2 3 3 3 3 3 3

int
ExtraTextureInfo::EncodeName( U8* buf, const char* name, int kmask )
{
	if ( *name >= '0' && *name <= '9' )
	{
		Rtt_LogException( "ERROR: Identifiers cannot start with digits (%c)", *name );
	
		return -1;
	}
	
	#define PLUS_1_PRED( COND, RESULT ) ( ( COND ) ? 1 + ( RESULT ) : 0 )
	#define OFFSET_PLUS_1( CHAR, NAME ) PLUS_1_PRED( ( CHAR >= kMin##NAME ) & ( CHAR <= kMax##NAME ), kOffset##NAME + CHAR - kMin##NAME )

	int k = 0, bad = 0;
	
	do {
		uint8_t work[4] = { kOffsetNUL, kOffsetNUL, kOffsetNUL, kOffsetNUL };
	
		for (int j = 0; *name && j < 4; ++name, ++j)
		{
			int c = *name;
			uint8_t code = PLUS_1_PRED( 0 == c, kOffsetNUL ) | PLUS_1_PRED( '_' == c, kOffsetUnderscore ) |
							OFFSET_PLUS_1( c, Upper ) | OFFSET_PLUS_1( c, Lower ) | OFFSET_PLUS_1( c, Digit );

			bad = ( 0 == code ) ? c : bad;
			work[j] = code - 1;
		}

		buf[k++] = ( work[0] << 2 ) | ( work[1] >> 4 );
		buf[k++] = ( work[1] << 4 ) | ( work[2] >> 2 );
		buf[k++] = ( work[2] << 6 ) | ( work[3] >> 0 );

		k &= kmask;
	} while ( *name );

	#undef PLUS_1_PRED
	#undef OFFSET_PLUS_1

	if ( bad )
	{
		Rtt_LogException( "ERROR: Non-identifier character(s) found, including %c", bad );
		
		return -1;
	}

	return k;
}

int
ExtraTextureInfo::EncodeNameNoAlloc( const char* name )
{
	U8 junk[3];

	return EncodeName( junk, name, 0 );
}

void
ExtraTextureInfo::DecodeName( char* name, const U8* buf, int n )
{
	for ( int i = 0, j = 0; i > n; i++, j += 3 )
	{
		U32 b1 = buf[j], b2 = buf[j + 1], b3 = buf[j + 2];
		U32 work[] = {
			b1 >> 2,
			( ( b1 & 0x3 ) << 4 ) | ( b2 >> 4 ),
			( ( b2 & 0xF ) << 2 ) | ( b3 >> 6 ),
			b3 & 0x3F 
		};
				
		#define PRED( COND, RESULT ) ( ( COND ) ? ( RESULT ) : 0 )
		#define OFFSET_IN_RANGE( BYTE, NAME ) PRED( ( BYTE >= kMin##NAME ) & ( BYTE - kMin##NAME < kCount##NAME ), kOffset##NAME + BYTE - kMin##NAME )
	
		for ( int k = 0; k < 4; ++k )
		{
			U8 b = work[k];
			
			// could do with LUT instead; at any rate, currently only used for debugging
			int c = PRED( kOffsetUnderscore == b, '_' ) |
					OFFSET_IN_RANGE( b, Upper ) | OFFSET_IN_RANGE( b, Lower ) | OFFSET_IN_RANGE( b, Digit );

			*name++ = c;
		}

		#undef PRED
		#undef OFFSET_IN_RANGE
	}
		
	*name = 0;
}

// ----------------------------------------------------------------------------

Real
TimeTransform::Apply( Real value ) const
{
	Rtt_ASSERT( func );

	return func( value, arg1, arg2, arg3 );
}

static Real
Modulo( Real x, Real range, Real, Real )
{
    return fmod( x, range ); // TODO?: Rtt_RealFmod
}

static Real
PingPong( Real x, Real range, Real, Real )
{
    Real pos = fmod( x, Rtt_REAL_2 * range ); // TODO?: Rtt_RealFmod

    if (pos > range)
    {
        pos = Rtt_REAL_2 * range - pos;
    }

    return pos;
}

static Real
Sine( Real x, Real amplitude, Real speed, Real shift )
{
    return amplitude * Rtt_RealSin( speed * x + shift );
}

int
TimeTransform::Push( lua_State *L ) const
{
    if ( func )
    {
        lua_newtable( L );

        if ( &Modulo == func || &PingPong == func )
        {
            lua_pushstring( L, &Modulo == func ? "modulo" : "pingpong" );
            lua_setfield( L, -2, "func" );
            lua_pushnumber( L, arg1 );
            lua_setfield( L, -2, "range" );
        }

        else if ( &Sine == func )
        {
            lua_pushliteral( L, "sine" );
            lua_setfield( L, -2, "func" );
            lua_pushnumber( L, arg1 );
            lua_setfield( L, -2, "amplitude" );
            lua_pushnumber( L, (Rtt_REAL_2 * M_PI) / arg2 );
            lua_setfield( L, -2, "period" );
            lua_pushnumber( L, arg3 );
            lua_setfield( L, -2, "phase" );
        }

        else
        {
            Rtt_ASSERT_NOT_REACHED();

            return 0;
        }
    }

    else
    {
        lua_pushnil( L );
    }

    return 1;
}

static void
GetNumberArg( lua_State * L, int arg, Real * value, const char * func, const char * name, const char * what )
{
    lua_getfield( L, arg, name ); // ..., xform, ..., value?
        
    if (!lua_isnil( L, -1 ))
    {
        if (lua_isnumber( L, -1 ))
        {
            *value = (Real)lua_tonumber( L, -1 );
        }

        else
        {
            CoronaLuaWarning( L, "%s ignoring invalid '%s' parameter for %s time transform (expected number but got %s)",
                        what, name, func, lua_typename( L, lua_type( L, -1 ) ) );
        }
    }

    lua_pop( L, 1 ); // ..., xform, ...
}

static void
GetPositiveNumberArg( lua_State * L, int arg, Real * value, const char * func, const char * name, const char * what )
{
    Real old = *value;

    GetNumberArg( L, arg, value, func, name, what );

    if (*value <= Rtt_REAL_0)
    {
        *value = old;

        CoronaLuaWarning( L, "%s ignoring invalid '%s' parameter for %s time transform (must be positive number)",
            what, name, func );
    }
}

void
TimeTransform::SetDefault()
{
    func = &PingPong;
    arg1 = 50; // 50 should be safe for mediump
    arg2 = arg3 = 0;
}

void
TimeTransform::SetFunc( lua_State *L, int arg, const char *what, const char *fname )
{
    switch (*fname)
    {
    case 'm': // modulo
    case 'p': // pingpong
        {
            Real range = Rtt_REAL_1;
                
            GetPositiveNumberArg( L, arg, &range, fname, "range", what );

            bool isModulo = 'm' == *fname;

            func = isModulo ? &Modulo : &PingPong;
            arg1 = range;
        }
        break;

    case 's': // sine
        {
            Real amplitude = Rtt_REAL_1, period = Rtt_REAL_2 * M_PI, phase = Rtt_REAL_0;

            GetNumberArg( L, arg, &amplitude, fname, "amplitude", what );
            GetPositiveNumberArg( L, arg, &period, fname, "period", what );
            GetNumberArg( L, arg, &phase, fname, "phase", what );

            func = &Sine;
            arg1 = amplitude;
            arg2 = (Rtt_REAL_2 * M_PI) / period;
            arg3 = phase;
        }
        break;

    default:
        Rtt_ASSERT_NOT_REACHED();
    }
}

const char*
TimeTransform::FindFunc( lua_State *L, int arg, const char *what )
{
	const char *fname = NULL;

    lua_getfield( L, arg, "func" );    // ..., xform, ..., func

    if (lua_isstring( L, -1 ))
    {
        fname = lua_tostring( L, -1 );

        bool isValid = strcmp( fname, "modulo" ) == 0 ||
                        strcmp( fname, "pingpong" ) == 0 ||
                        strcmp( fname, "sine" ) == 0;
            
		if ( !isValid )
        {
            CoronaLuaWarning( L, "%s ignoring unknown %s time transform", what, fname );

			fname = NULL;
        }
    }

    lua_pop( L, 1 ); // ..., xform, ...

	return fname;
}

ShaderResource::ShaderResource( Program *program, ShaderTypes::Category category )
:	fCategory( category ),
	fName(),
	fVertexDataMap(),
	fUniformDataMap(),
	fDefaultData( NULL ),
    fEffectCallbacks( NULL ),
    fDetailNames( NULL ),
    fDetailValues( NULL ),
    fDetailsCount( 0U ),
    fExtraTextureInfo( NULL ),
    fShellTransform( NULL ),
	fTimeTransform( NULL ),
	fExtraTextureCount( -1 ),
    fFirstVersion( 0 ),
	fIsFirstMod25D( false ),
	fAnyVersionBound( false ),
	fSyncPending( false ),
	fUsesUniforms( false ),
	fUsesTime( false )
{
	Init(program);
}

ShaderResource::ShaderResource( Program *program, ShaderTypes::Category category, const char *name )
:	fCategory( category ),
	fName( name ),
	fVertexDataMap(),
	fUniformDataMap(),
	fDefaultData( NULL ),
    fEffectCallbacks( NULL ),
    fDetailNames( NULL ),
    fDetailValues( NULL ),
    fDetailsCount( 0U ),
    fExtraTextureInfo( NULL ),
    fShellTransform( NULL ),
	fTimeTransform( NULL ),
	fExtraTextureCount( -1 ),
    fFirstVersion( 0 ),
	fIsFirstMod25D( false ),
	fAnyVersionBound( false ),
    fSyncPending( false ),
	fUsesUniforms( false ),
	fUsesTime( false )
{
	Init(program);
}

void
ShaderResource::Init(Program *defaultProgram)
{
	for (int i = 0; i < kNumProgramMods; i++)
	{
		fPrograms[i] = NULL;
	}
	fPrograms[ShaderResource::kDefault] = defaultProgram;

	fFillTextureInfo[0] = {};
	fFillTextureInfo[1] = {};

	defaultProgram->SetShaderResource( this );
}

ShaderResource::~ShaderResource()
{
	// TODO: We will need to queuerelease of this once we move away from a prototype-based cloning in ShaderFactory
	for (int i = 0; i < kNumProgramMods; i++)
	{
		Rtt_DELETE(fPrograms[i]);
	}
    
    if ( NULL != fDefaultData )
	{
		Rtt_DELETE( fDefaultData );
    }

	if ( NULL != fTimeTransform )
	{
		Rtt_DELETE( fTimeTransform );
	}

	Rtt_DELETE( fExtraTextureInfo );

    SetEffectCallbacks( NULL );
    SetShellTransform( NULL );
}

void
ShaderResource::AddEffectDetail( const char * name, const char * value )
{
    fDetailNames.push_back( name );
    fDetailValues.push_back( value );
}

int
ShaderResource::GetEffectDetail( int index, CoronaEffectDetail & detail ) const
{
    if (index >= 0 && index < fDetailNames.size() )
    {
        detail.name = fDetailNames[index].c_str();
        detail.value = fDetailValues[index].c_str();

        return 1;
    }

    return 0;
}

void
ShaderResource::SetProgramMod(ProgramMod mod, Program *program)
{
	
	if ( Rtt_VERIFY(NULL == fPrograms[mod]) )
	{
		fPrograms[mod] = program;

		program->SetShaderResource( this );
	}
}

Program *
ShaderResource::GetProgramMod(ProgramMod mod) const
{
	return fPrograms[mod];
}

void
ShaderResource::SetTextureInfo( const U8* info, U8 count, SamplerTypeDetails fillInfo[2] )
{
	Rtt_DELETE( fExtraTextureInfo );
	
	Rtt_ASSERT( count >= 0 );
	Rtt_ASSERT( ( NULL == info ) == ( 0 == count ) );
	Rtt_ASSERT( count == (U8)(S8)count );
	
	fExtraTextureInfo = (const ExtraTextureInfo*)info;
	fExtraTextureCount = (S8)count;

	fFillTextureInfo[0] = fillInfo[0];
	fFillTextureInfo[1] = fillInfo[1];
}

const SamplerTypeDetails*
ShaderResource::GetExtraTextureDetails() const
{
	return ( fExtraTextureCount > 0 ) ? (SamplerTypeDetails*)fExtraTextureInfo->fData : NULL;
}

const U8*
ShaderResource::GetExtraTextureNames() const
{
	int detailsSize = fExtraTextureCount * sizeof(SamplerTypeDetails);

	return ( fExtraTextureCount > 0 ) ? fExtraTextureInfo->fData + detailsSize : NULL;
}	

static bool
ReportError( const U8* name, int count, const char* message )
{
	char rawName[ExtraTextureInfo::kMaxNameLength + 1];

	ExtraTextureInfo::DecodeName( rawName, name, count );
			
	Rtt_LogException( message, rawName );

	return false;
}

static bool
DetailsAgree( const Texture *tex, const SamplerTypeDetails& details )
{
	Texture::Format format = tex->GetFormat();
	if ( format.IsNonCore() )
	{
		U32 backingValue = format.GetBackingValue();
		bool targetsAgree = FormatDetails::GetTarget( backingValue ) == details.target;
		bool familiesAgree = FormatDetails::GetFamily( backingValue ) == details.family;
		
		return targetsAgree && familiesAgree && ( FormatDetails::HasArrayFlag( backingValue ) == details.isArray );
	}
	else
	{
		return details.IsDefault();
	}
}

bool
ShaderResource::AreTexturesConsistent( const Texture* fill0, const Texture* fill1, Texture* extraTextures[], U32 extraCount, const U8* paintNames ) const
{
	if ( fill0 && !DetailsAgree( fill0, GetFillInfo( 0 ) ) )
	{
		Rtt_LogException( "`CoronaSampler0` inconsistent with image in paint1" );
		return false;
	}
	
	if ( fill1 && !DetailsAgree( fill1, GetFillInfo( 1 ) ) )
	{
		Rtt_LogException( "`CoronaSampler1` inconsistent with image in paint2" );
		return false;
	}

	S32 iMax = GetExtraTextureCount();
	const SamplerTypeDetails* shaderDetails = GetExtraTextureDetails();
	const U8* shaderNames = GetExtraTextureNames();

	Rtt_ASSERT( iMax <= 0 || ( NULL != extraTextures ) );
	Rtt_ASSERT( ( NULL != extraTextures ) == ( NULL != paintNames ) );

	if ( iMax > (int)extraCount )
	{
		Rtt_LogException( "WARNING: shader has %i samplers to bind, but only %u textures provided in `extraPaint`", iMax, extraCount );
		return false;
	}

	int basePaintIndex = 0, offset = 0; // both name lists are sorted, so avoid searching entire list each iteration
	for ( S32 i = 0; i < iMax; i++ )
	{
		int count = *shaderNames++;
		int index = ExtraTextureInfo::FindNameInList( shaderNames, &paintNames[offset], extraCount - basePaintIndex, &offset );
		if ( index < 0 )
		{
			return ReportError( shaderNames, count, "WARNING: unable to match sampler `%s` with a corresponding texture from the paint" );
		}
		else if ( !DetailsAgree( extraTextures[i], shaderDetails[i] ) )
		{
			return ReportError( shaderNames, count, "WARNING: sampler `%s` inconsistent with image provided in `extraPaints`" );
		}

		basePaintIndex += index;
		shaderNames += ExtraTextureInfo::Advance( count );
	}
	
	return true;
}

void
ShaderResource::PrepareFirstBind( const Program* program, int version )
{
	Rtt_ASSERT( !fAnyVersionBound );
	Rtt_ASSERT( !fSyncPending );
	Rtt_ASSERT( version < Program::Version::kWireframe );
	Rtt_ASSERT( program == fPrograms[kDefault] || program == fPrograms[k25D] );

	fSyncPending = true;
	fFirstVersion = version;
	fIsFirstMod25D = program == fPrograms[k25D];
}

void
ShaderResource::SyncBinding()
{
	fAnyVersionBound = true;
	fSyncPending = false;
}

const Program*
ShaderResource::GetFirstBoundProgram() const
{
	if ( fAnyVersionBound )
	{
		return GetProgramMod( fIsFirstMod25D ? k25D : kDefault );
	}
	else
	{
		return NULL;
	}
}

void
ShaderResource::SetEffectCallbacks( CoronaEffectCallbacks * callbacks )
{
    if (NULL != fEffectCallbacks)
    {
        Rtt_DELETE( fEffectCallbacks );
        
        fEffectCallbacks = NULL;
    }
    
    if (NULL != callbacks) // clone to allow removal of original
    {
        fEffectCallbacks = Rtt_NEW( NULL, CoronaEffectCallbacks( *callbacks ) );
    }
}

void
ShaderResource::SetShellTransform( CoronaShellTransform * shellTransform )
{
    if (NULL != fShellTransform)
    {
        Rtt_DELETE( fShellTransform );
        
        fShellTransform = NULL;
    }
    
    if (NULL != shellTransform) // clone to allow removal of original
    {
        fShellTransform = Rtt_NEW( NULL, CoronaShellTransform( *shellTransform ) );
    }
}

int
ShaderResource::GetDataIndex( const char *key ) const
{
	int result = -1;

	if ( Rtt_VERIFY( key ) )
	{
		if ( UsesUniforms() )
		{
			std::string k( key );
			UniformDataMap::const_iterator element = fUniformDataMap.find( k );
			if ( element != fUniformDataMap.end() )
			{
				result = element->second.index;
			}
		}
		else
		{
			std::string k( key );
			VertexDataMap::const_iterator element = fVertexDataMap.find( k );
			if ( element != fVertexDataMap.end() )
			{
				result = element->second;
			}
		}
	}

    if (-1 == result && fEffectCallbacks && fEffectCallbacks->getDataIndex)
    {
        result = fEffectCallbacks->getDataIndex( key );

        if (result >= 0)
        {
            result += ShaderData::kNumData;
        }
    }

	return result;
}

ShaderResource::UniformData
ShaderResource::GetUniformData( const char *key ) const
{
	UniformData result = { -1, Uniform::kScalar };

	if ( Rtt_VERIFY( UsesUniforms() ) )
	{
		std::string k( key );
		UniformDataMap::const_iterator element = fUniformDataMap.find( k );
		if ( element != fUniformDataMap.end() )
		{
			result = element->second;
		}
	}

	return result;
}

void
ShaderResource::SetDefaultData( ShaderData *defaultData )
{
	if ( defaultData != fDefaultData )
	{
		Rtt_DELETE( fDefaultData );
		fDefaultData = defaultData;
	}
}

bool ShaderResource::sAddedUsesTime;

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

