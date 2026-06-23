//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Program_H__
#define _Rtt_Program_H__

#include "Renderer/Rtt_CPUResource.h"
#include "Core/Rtt_Types.h"

// ----------------------------------------------------------------------------

#if defined( Rtt_USE_PRECOMPILED_SHADERS )
	namespace Rtt { class ShaderBinaryVersions; }
#endif
struct Rtt_Allocator;

namespace Rtt
{

class ProgramHeader;
class ShaderResource;

// ----------------------------------------------------------------------------



class Program : public CPUResource
{
	public:
		typedef CPUResource Super;
		typedef CPUResource Self;

	public:
		typedef enum _Language
		{
			kDefault = 0,
			kOpenGL_2_1 = 1,
			kOpenGL_ES_2 = kDefault,
			kVulkanGLSL = 2
		}
		Language;

		typedef enum _Version
		{
			kMaskCount0 = 0,
			kMaskCount1,
			kMaskCount2,
			kMaskCount3,
			kWireframe,
			kNumVersions
		}
		Version;

		static const char *HeaderForLanguage( Language language, const ProgramHeader& headerData );

		static int CountLines( const char *str );
	public:
		Program( Rtt_Allocator* allocator );
		virtual ~Program();

		virtual ResourceType GetType() const;
		virtual void Allocate();
		virtual void Deallocate();

		const char *GetVertexShaderSource() const;
		void SetVertexShaderSource( const char *source );

		const char *GetFragmentShaderSource() const;
		void SetFragmentShaderSource( const char *source );

		const char *GetHeaderSource() const { return fHeaderSource; }
		void SetHeaderSource( const char *source );

		ShaderResource *GetShaderResource() { return fResource; }
		void SetShaderResource( ShaderResource *resource ) { fResource = resource; }
#if defined( Rtt_USE_PRECOMPILED_SHADERS )
		ShaderBinaryVersions* GetCompiledShaders() const { return fCompiledShaders; }
#endif

		int GetVertexShellNumLines() const { return fVertexShellNumLines; }
		void SetVertexShellNumLines( int newValue ) { fVertexShellNumLines = newValue; }

		int GetFragmentShellNumLines() const { return fFragmentShellNumLines; }
		void SetFragmentShellNumLines( int newValue ) { fFragmentShellNumLines = newValue; }

		bool IsCompilerVerbose() const { return fCompilerVerbose; }
		void SetCompilerVerbose( bool newValue ) { fCompilerVerbose = newValue; }

	public:
		bool AnyPending() const { return 0 != fBoundVersionsPending; }
		bool IsCurrent( Version v ) const { return 0 != ( fBoundVersionsCurrent & ( 1 << v ) ); }
		void SetPending( Version v ) { fBoundVersionsPending |= ( 1 << v ); }
		void SyncPending() { fBoundVersionsCurrent |= fBoundVersionsPending; fBoundVersionsPending = 0; }

	private:
		char *fVertexShaderSource;
		char *fFragmentShaderSource;
		char *fHeaderSource;
#if defined( Rtt_USE_PRECOMPILED_SHADERS )
		ShaderBinaryVersions *fCompiledShaders;
#endif
		int fVertexShellNumLines;
		int fFragmentShellNumLines;
// SAS TODO:
		U16 fIsLocal : 1; // if set, next field is valid (few enough samplers + bound versions agree)
		U16 fLocalSamplersUsed : 6; // bits indicating sampler info entries in use
		U16 fBoundVersionsCurrent : 4; // bits indicating already-bound (mask count-based) versions
		U16 fBoundVersionsPending : 4; // bits denoting versions being bound this frame
		U16 fUnused : 1; // ???: could use to look up a name map?
		U8 fSamplerInfo[6]; // target + subtype + is-array info (cf. Rtt_Texture.h) for samplers in use
/*
  https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel, pared back for 6 bits:
  U16 n = i - ( ( i >> 1 ) & 0x55 )
  
  n = ( ( n >> 2 ) & 0x33 ) + ( n & 0x33 );
  n = ( ( n >> 4 ) + n ) & 0x0F;

ctz:	
	x ← x ^ (x − 1)
    return popcount(x) − 1
*/
		
// /SAS
		ShaderResource *fResource;
		bool fCompilerVerbose;
		
// SAS TODO: flag[versions] to detect if bound yet, second one for "bound this frame" (10 bits total)
	// keep this in some small list... at some point, do a sync
	// could actually do in DEFER_CREATION setup... (no actual issue if immmediate?)
		// flags maybe could mean something else?
		// a few flags:
			// U8: #0's info
			// U8: #1's info
			// bool: > 2? (can be in spare bit, actually, since above is 7 bits...)
			// bool: < sizeof(pointer) AND consistent?
				// 64 / 7 -> 9 textures
				// 16 / 7 -> 2 and 2 bits...
			// 2 "normal" samplers vs. 1 sampler ambiguous
				// anyhow, 32 bits not enough for pointer on many platforms
	// idea: 8 flags
		// per-version bit (although, importantly, wireframe does not use fragment textures
			// has its own implications for some ideas...
			// will by itself cause inconsistency
		// 4 bits, then: mask counts 0-3
		// 3-bit count: 0-7... could then just have remaining 7 bytes of a U64
			// used even for just 2 entries
			// if not local, still consulted
			// awkward: textures might not be dense :/
				// need bitmask instead
				// could be 7-bit flag, 1 bit for local (if not local, still consult first two flags, entries)
				// want those 4 bits elsewhere, then...
					// could just be 6-bit flag, 6 entries (2 local, 4 added)
						// 5 bits: checked; local
						// 6 bits: occupied
		// > "local" flag (could be low-bit and double as pointer check?)
	// could try to keep consistency in "far" cases, but need to be ready to realloc
	
	// after first frame, can decide at Draw() time if everything is consistent
	// otherwise, need to do so conditionally
};

// ----------------------------------------------------------------------------

class ProgramHeader
{
	public:
		typedef enum _Type
		{
			kUnknownType = -1,
			kDefaultType = 0,
			kRandomType,
			kPositionType,
			kNormalType,
			kUVType,
			kColorType,

			// NOTE: Always the last one
			kNumType
		}
		Type;

		typedef enum _Precision
		{
			kUnknownPrecision = -1,
			kLowPrecision = 0,
			kMediumPrecision,
			kHighPrecision,
		}
		Precision;

	public:
		// static const char *StringForType( Type value );
		static Type TypeForString( const char *value );

		static const char *StringForPrecision( Precision value );
		static Precision PrecisionForString( const char *value );

	public:
		ProgramHeader();

	public:
		void CopyHeaderSource( Program::Language language, char *dst, int dstSize ) const;
		void SetPrecision( Type t, Precision p );
		void SetPrecision( Precision p );

	private:
		S8 fPrecision[kNumType]; // Precision values for each Type index 
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Program_H__
