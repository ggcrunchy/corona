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
class FormatExtensionList;

// ----------------------------------------------------------------------------

struct SamplerTypeDetails {
	U8 family : 2;
	U8 target : 3;
	U8 isImage : 1;
	U8 isArray : 1;
};

// TODO: if this breaks, must instead use index and unpack details, but then need to know where LUT is, etc.
Rtt_STATIC_ASSERT( sizeof(SamplerTypeDetails) == 1 );

// ----------------------------------------------------------------------------

struct ExtraTextureInfo
{
	// The info is a big blob of bytes, used by shaders and paints in
	// slightly different ways to hold some associated data. In each
	// case, part of the payload is an optional list of names.

	// Names themselves are stored as a byte (a 6-bit length, with two
	// bits reserved for flags), followed by the packed representation.

	enum {
		// Names are basically "identifiers" as in C89 or GLSL, i.e.
		// some combination of underscores, ASCII letters, and non-
		// leading digits with a terminating NUL byte, that fit neatly
		// into 6-bits (a Base64-style encoding). Every three bytes
		// can thus hold four such elements, and we can eke out longer
		// names by storing the count in terms of triples instead.
		// (For purposes of the length, the terminating NUL is not
		// included; however, names with non-multiple-of-3 lengths
		// are padded with trailing NULs.)
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
	static U32 NamesSize( U32 binCount ) { return binCount * 3; }
	
	static int FindNameInList( const U8* name, const U8* listOfNames, int n );
	static int EncodeName( U8* buf, const char* name, int kmask = kMaxPackedNameLength - 1 );
	static int EncodeNameNoAlloc( const char* name );
	static void DecodeName( char* name, const U8* buf, int n );

	// In the above, `name` must have a terminating NUL (when encoding) or an
	// extra character for the same (when decoding).

	U8 *fData;
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
		void SetTextureInfo( const U8* info, U8 count, U8 fillInfo[2] );

        S8 GetExtraTextureCount() const { return fExtraTextureCount; }
        U8 GetFillInfo(int index) const { return fFillTextureInfo[index]; }
        const U8* GetExtraTextureDetails() const;
        const U8* GetExtraTextureNames() const;
        
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
        U8 fFillTextureInfo[2];
        S8 fExtraTextureCount;
        bool fUsesUniforms;
        bool fUsesTime;
        
        static bool sAddedUsesTime; // has ANY ShaderResource added the "uses time" flag?

};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_ShaderResource_H__
