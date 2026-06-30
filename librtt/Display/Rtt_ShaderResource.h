//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_ShaderResource_H__
#define _Rtt_ShaderResource_H__

#include <map>
#include <string>

#include "Core/Rtt_SharedPtr.h"
#include "Display/Rtt_ShaderTypes.h"
#include "Renderer/Rtt_Uniform.h"

#include <vector>

// ----------------------------------------------------------------------------

struct lua_State;
struct CoronaEffectCallbacks;
struct CoronaEffectDetail;
struct CoronaShellTransform;

namespace Rtt
{

class Program;
class ShaderData;
class Texture;
class FormatExtensionList;

// ----------------------------------------------------------------------------

struct SamplerTypeDetails {
	bool IsDefault() const { return ( 0 == family ) && ( 0 == target ) && !isImage && !isArray; }
	bool Matches( const SamplerTypeDetails& rhs) const { return 0 == memcmp( this, &rhs, sizeof(*this) ); }

	U8 family : 2;
	U8 target : 3;
	U8 isImage : 1;
	U8 isArray : 1;
};

// ----------------------------------------------------------------------------

struct ExtraTextureInfo
{
	// The info is a big blob of bytes, used by shaders and paints in
	// slightly different ways. Both cases include a list of names.

	// Counts are 6-bit values, per the details that follow, and can
	// be stored in a byte. One of the leftover bits is reserved as
	// a behavior flag, e.g. "uses compression", to be interpreted
	// in the same way by both shaders and paints; the high bit is
	// intended for "local" use.

	// At the moment, counts are interspersed between names. While
	// slightly wasteful in the general case, owing to padding, it
	// does make for a simpler decoding process.

	enum {
		// Names are basically "identifiers" as in C89 or GLSL, i.e.
		// some combination of underscores, ASCII letters, and non-
		// leading digits with a terminating NUL byte, which lends
		// itself perfectly to a Base64-style, 6-bits-per-element
		// encoding. Every three bytes can thus hold four of these
		// elements, and we can eke out longer names by storing the
		// count in terms of triples instead. (For purposes of this
		// length, the terminating NUL is not included. Also, NULs
		// will be added to pad any shortfall in the final triple.)
		kMaxPackedNameCount = 64,
		kMaxPackedNameLength = kMaxPackedNameCount * 3,
	
		// Longest length of raw name that may be packed.
		kMaxNameLength = kMaxPackedNameLength * 4,
	
		#define COUNT_AND_OFFSET( NAME, COUNT, OFFSET ) kCount##NAME = COUNT, kOffset##NAME = OFFSET
		#define AFTER_PREV( PREV ) kOffset##PREV + kCount##PREV
		#define NEXT_COUNT_AND_OFFSET( NAME, OFFSET ) COUNT_AND_OFFSET( NAME, kMax##NAME - kMin##NAME + 1, OFFSET )
	
		kMinUpper = 'A', kMaxUpper = 'Z',
		kMinLower = 'a', kMaxLower = 'z',
		kMinDigit = '0', kMaxDigit = '9',
	
		NEXT_COUNT_AND_OFFSET( Upper, 0 ),
		NEXT_COUNT_AND_OFFSET( Lower, AFTER_PREV( Upper ) ),
		NEXT_COUNT_AND_OFFSET( Digit, AFTER_PREV( Lower ) ),
		kOffsetUnderscore = AFTER_PREV( Digit ),
		kOffsetNUL = kOffsetUnderscore + 1
	
		#undef COUNT_AND_OFFSET
		#undef AFTER_PREV
		#undef NEXT_COUNT_AND_OFFSET
		
	};

	Rtt_STATIC_ASSERT( ( kOffsetNUL + 1 == 64 ) && ( kMaxPackedNameLength % 3 == 0 ) && ( kMaxNameLength % 4 == 0 ) );

	static U32 BinsForLength( U32 length ) { return ( length + 3 ) / 4; }
	static U32 Advance( U32 binCount ) { return binCount * 3; }
	
	static int FindNameInList( const U8* name, const U8* listOfNames, int n, int* offset = NULL );
	static bool ListsMatch( const U8* listOfNames1, const U8* listOfNames2, int n );
	static U32 NamesSize( const U8* listOfNames, int n );
	static int EncodeName( U8* buf, const char* name, int kmask = kMaxPackedNameLength - 1 );
	static int EncodeNameNoAlloc( const char* name );
	static void DecodeName( char* name, const U8* buf, int n );

	// N.B. name`must have a terminating NUL (when encoding) or an
	// extra slot to receive the same (when decoding).

	U8 *fData;
};

// ----------------------------------------------------------------------------

// This is mutable state related to RenderData that might possibly
// be resolved via Renderer::Insert(), as an inout argument.
struct RenderDataState {
	enum SyncState {
		kUnsynced, // not yet able to check for consistency
		kSyncConsistent, // sync attempt made and successful
		kSyncInconsistent, // sync attempt failed
	};
	
	enum {
		kSyncBits = 2,
		kOccupancyBits = 30,

		kSyncShift = 0,
		kOccupancyShift = kSyncBits,

		kSyncMask = ( 1 << kSyncBits ) - 1,
		kOccupancyMask = ( 1 << kOccupancyBits ) - 1,

		kSyncWipeMask = ~( kSyncMask << kSyncShift ),
		kOccupancyWipeMask = ~( kOccupancyMask << kOccupancyShift )
	};

	Rtt_STATIC_ASSERT( kOccupancyBits + kSyncBits <= sizeof(int) * 8 );

	#define GET_BITS( NAME, TYPE ) (TYPE)( ( ( *fState ) >> k##NAME##Shift ) & k##NAME##Mask )
	#define SET_BITS( NAME, ARG ) *fState = ( *fState & ~( k##NAME##Mask << k##NAME##Shift ) ) | ( ( ARG & k##NAME##Mask ) << k##NAME##Shift )
	
	void SetSyncState( SyncState state ) { SET_BITS( Sync, state ); }
	SyncState GetSyncState() const { return GET_BITS( Sync, SyncState ); }

	void SetOccupancy( U32 occ ) { SET_BITS( Occupancy, occ ); }
	U32 GetOccupancy() const { return GET_BITS( Occupancy, U32 ); }
	
	#undef GET_BITS
	#undef SET_BITS
	
	int *fState;
};

// ----------------------------------------------------------------------------

struct TimeTransform
{
    typedef Real (*Func)( Real time, Real arg1, Real arg2, Real arg3 );

    TimeTransform() : func( NULL ), arg1( 0 ), arg2( 0 ), arg3( 0 )
    {
    }

	Real Apply( Real value ) const;
    int Push( lua_State *L ) const;
    void SetDefault();
    void SetFunc( lua_State *L, int arg, const char *what, const char *fname );

    static const char* FindFunc( lua_State *L, int arg, const char *what );

    Func func;
    Real arg1, arg2, arg3;
};

// ----------------------------------------------------------------------------

class ShaderResource
{
    public:

        typedef enum ProgramMod
        {
            kDefault    = 0,
            k25D        = 1,
            kNumProgramMods,
        }
        ProgramMod;
    
        typedef std::map< std::string, int > VertexDataMap;

        struct UniformData
        {
            int index;
            Uniform::DataType dataType;
        };
        typedef std::map< std::string, UniformData > UniformDataMap;

    public:
        // Shader takes ownership of the program
        ShaderResource( Program *program, ShaderTypes::Category category );
        ShaderResource( Program *program, ShaderTypes::Category category, const char *name );
        
    public:
        ~ShaderResource();

    public:
        ShaderTypes::Category GetCategory() const { return fCategory; }
        const std::string& GetName() const { return fName; }
        const char *GetTag( int index ) const { return NULL; }
        int GetNumTags() const { return 0; }

    public:
        bool UsesUniforms() const { return fUsesUniforms; }
        void SetUsesUniforms( bool newValue ) { fUsesUniforms = newValue; }

        bool UsesTime() const { return fUsesTime; }
        void SetUsesTime( bool newValue ) { fUsesTime = newValue; }
    
        const CoronaEffectCallbacks * GetEffectCallbacks() const { return fEffectCallbacks; }
        void SetEffectCallbacks( CoronaEffectCallbacks * callbacks );
        const CoronaShellTransform * GetShellTransform() const { return fShellTransform; }
        void SetShellTransform( CoronaShellTransform * shellTransform );

        const FormatExtensionList * GetExtensionList() const { return &*fExtensionList; }
        void SetExtensionList( const SharedPtr<FormatExtensionList>& list ) { fExtensionList = list; }

        void AddEffectDetail( const char * name, const char * value );

        int GetEffectDetail( int index, CoronaEffectDetail & detail ) const;

        TimeTransform *GetTimeTransform() const { return fTimeTransform; }
        void SetTimeTransform( TimeTransform *transform ) { fTimeTransform = transform; }
    public:
        // Shader either stores params on per-vertex basis or in uniforms.
        // Batching most likely breaks as soon as you use uniforms,
        // so params are either per-vertex OR uniforms --- never both.
        // The mapping between the (Lua API) property name and the internal
        // location in per-vertex/uniform data is stored by the maps.
        int GetDataIndex( const char *key ) const;
        UniformData GetUniformData( const char *key ) const;
        
        /*
        const VertexDataMap& GetVertexDataMap() const { return fVertexDataMap; }
        const UniformDataMap& GetUniformDataMap() const { return fUniformDataMap; }
        */

    //protected:
        VertexDataMap& GetVertexDataMap() { return fVertexDataMap; }
        UniformDataMap& GetUniformDataMap() { return fUniformDataMap; }

    public:
        // A filter's default effect param values are stored here.
        ShaderData *GetDefaultData() const { return fDefaultData; }
        void SetDefaultData( ShaderData *defaultData );
        
    public:
        void SetProgramMod(ProgramMod mod, Program *program);
        Program *GetProgramMod(ProgramMod mod) const;
        
	public:
		static void SetAddedUsesTime( bool newValue ) { sAddedUsesTime = newValue; }
		static bool GetAddedUsesTime() { return sAddedUsesTime; }

	public:
		void SetTextureInfo( const U8* info, U8 count, SamplerTypeDetails fillInfo[2] );

		static bool DetailsAgree( U32 formatBackingValue, const SamplerTypeDetails& details );

		bool HasTextureInfo() const { return fTextureInfoIsSet; }
        U32 GetExtraTextureCount() const { return fExtraTextureCount; }
        SamplerTypeDetails GetFillInfo(int index) const { return fFillTextureInfo[index]; }
        const SamplerTypeDetails* GetExtraTextureDetails() const;
        const U8* GetExtraTextureNames() const;

	public:
		bool AreTexturesConsistent( const Texture* fill0, const Texture* fill1, Texture* extraTextures[], U32 extraCount, const U8* paintNames ) const;

	public:
		const Program* GetFirstBoundProgram() const;

		bool IsSyncPending() const { return fSyncPending; }
		bool IsFirstBoundMod25D() const { return fIsFirstMod25D; }
		int GetFirstBoundVersion() const { return fFirstVersion; }
		void PrepareFirstBind( const Program* program, int version );
		void SyncBinding();
        
    private:
        void Init(Program *defaultProgram);

    private:
        Program *fPrograms[kNumProgramMods];
        
        ShaderTypes::Category fCategory;
        std::string fName;
        VertexDataMap fVertexDataMap;
        UniformDataMap fUniformDataMap;
        ShaderData *fDefaultData;
        CoronaEffectCallbacks *fEffectCallbacks;
        CoronaShellTransform *fShellTransform;
        SharedPtr<FormatExtensionList> fExtensionList;
        std::vector< std::string > fDetailNames;
        std::vector< std::string > fDetailValues;
        const ExtraTextureInfo *fExtraTextureInfo;
        U32 fDetailsCount;
        TimeTransform *fTimeTransform;
        SamplerTypeDetails fFillTextureInfo[2];
        U8 fExtraTextureCount;
        U8 fFirstVersion : 2;
        U8 fIsFirstMod25D : 1;
        U8 fAnyVersionBound : 1;
        U8 fSyncPending : 1;
        bool fUsesUniforms;
        bool fUsesTime;
        bool fTextureInfoIsSet;
        
        static bool sAddedUsesTime; // has ANY ShaderResource added the "uses time" flag?

};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_ShaderResource_H__
