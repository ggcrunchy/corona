//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_GLProgram.h"
#include "Renderer/Rtt_GLGeometry.h"

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_FormatExtensionList.h"
//#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_Texture.h"
#ifdef Rtt_USE_PRECOMPILED_SHADERS
    #include "Renderer/Rtt_ShaderBinary.h"
    #include "Renderer/Rtt_ShaderBinaryVersions.h"
#endif
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Traits.h"
#include "Core/Rtt_String.h"
#include <cstdio>
#include <string.h> // memset.
#include <stdlib.h>
#ifdef Rtt_WIN_PHONE_ENV
    #include <GLES2/gl2ext.h>
#endif

#include "Display/Rtt_ShaderResource.h"
#include "Corona/CoronaLog.h"
#include "Corona/CoronaGraphics.h"

#include <string>
#include <vector>
#include "Rtt_Profiling.h"

// Include GL header for glGetActiveUniform
#include "Renderer/Rtt_GL.h"

// To reduce memory consumption and startup cost, defer the
// creation of GL shaders and programs until they're needed.
// Depending on usage, this could result in framerate dips.

// TODO: verify for updated "uses time" logic, cf. note in Rtt_Scene.cpp
#define DEFER_CREATION 1

// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
    using namespace Rtt;

    // Check that the given shader compiled and log any errors
    void CheckShaderCompilationStatus( GLuint name, bool isVerbose, const char *label, int startLine )
    {
        GLint result;
        glGetShaderiv( name, GL_COMPILE_STATUS, &result );
        if( result == GL_FALSE )
        {
            GLint length;
            glGetShaderiv( name, GL_INFO_LOG_LENGTH, &length );

            GLchar* infoLog = new GLchar[length];
            glGetShaderInfoLog( name, length, NULL, infoLog );

            if ( isVerbose )
            {
                if ( label )
                {
                    Rtt_LogException( "ERROR: An error occurred in the %s kernel.\n", label );
                }
                Rtt_LogException( "%s", infoLog );
                Rtt_LogException( "\tNOTE: Kernel starts at line number (%d), so subtract that from the line numbers above.\n", startLine );
            }
            delete[] infoLog;
        }
    }

    // Check that the given program linked and log any errors
    void CheckProgramLinkStatus( GLuint name, bool isVerbose )
    {
        GLint result;
        glGetProgramiv( name, GL_LINK_STATUS, &result );
        if( result == GL_FALSE )
        {
            GLint length;
            glGetProgramiv( name, GL_INFO_LOG_LENGTH, &length );

            GLchar* infoLog = new GLchar[length];
            glGetProgramInfoLog( name, length, NULL, infoLog );

			if ( isVerbose )
			{
				Rtt_LogException( "%s", infoLog );
			}
			else
			{
				Rtt_LogException(
					"ERROR: A shader failed to compile. To see errors, add the following to the top of your main.lua:\n"
					"\tdisplay.setDefault( 'isShaderCompilerVerbose', true )\n" );
			}
			delete[] infoLog;
		}
	}
	
	const char* kWireframeSource =
		"void main()" \
		"{" \
			"gl_FragColor = vec4(1.0);" \
		"}";
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

struct GLProgramUniformInfo {
    GLProgramUniformInfo()
    {
        for (int i = 0; i < Program::kNumVersions; ++i)
        {
            fLocations[i] = -1;
        }
    }
    
    GLint fLocations[Program::kNumVersions];
    GLint size;
    GLenum type;
    std::string fName;
};

struct GLProgramUniformsCache {
    std::vector< GLProgramUniformInfo > fInfo;
};

GLProgram::GLProgram()
:   fCleanupShellTransform( NULL ),
    fUniformsCache( NULL )
{
    for( U32 i = 0; i < Program::kNumVersions; ++i )
    {
        Reset( fData[i] );
    }
}

void
GLProgram::Create( CPUResource* resource, const RenderContext* )
{
	SUMMED_TIMING( glpc, "Program GPU Resource: Create" );

	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	fResource = resource;

	#if !DEFER_CREATION
		bool usesTime = false;
		for( U32 i = 0; i < kMaximumMaskCount + 1; ++i )
		{
			Create( fData[i], i );
			
			if ( !usesTime && fData[i].HasTime() )
			{
				usesTime = true;
			}
		}
	#endif

    Rtt_STATIC_ASSERT( ( Traits::IsSame< decltype(fCleanupShellTransform),  CoronaShellTransformStateCleanup >::Value ) );
    
    Program* program = static_cast<Program*>( fResource );
    ShaderResource* shaderResource = program->GetShaderResource();
    
    #if !DEFER_CREATION
		if ( usesTime )
		{
			shaderResource->SetUsesTime( true );
		
			ShaderResource::SetAddedUsesTime( true );
		}
	#endif
	
    const CoronaShellTransform * transform = shaderResource->GetShellTransform();

    if (transform && transform->cleanup)
    {
        fCleanupShellTransform = transform->cleanup;
    }
}

void
GLProgram::Update( CPUResource* resource, const RenderContext* )
{
	SUMMED_TIMING( glpu, "Program GPU Resource: Update" );

    Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
    if( fData[Program::kMaskCount0].fProgram ) Update( Program::kMaskCount0, fData[Program::kMaskCount0] );
    if( fData[Program::kMaskCount1].fProgram ) Update( Program::kMaskCount1, fData[Program::kMaskCount1] );
    if( fData[Program::kMaskCount2].fProgram ) Update( Program::kMaskCount2, fData[Program::kMaskCount2] );
    if( fData[Program::kMaskCount3].fProgram ) Update( Program::kMaskCount3, fData[Program::kMaskCount3] );
    if( fData[Program::kWireframe].fProgram ) Update( Program::kWireframe, fData[Program::kWireframe]);
}

void
GLProgram::Destroy()
{
    for( U32 i = 0; i < Program::kNumVersions; ++i )
    {
        VersionData& data = fData[i];
        if( data.fProgram )
        {
#ifndef Rtt_USE_PRECOMPILED_SHADERS
            glDeleteShader( data.fVertexShader );
            glDeleteShader( data.fFragmentShader );
#endif
            glDeleteProgram( data.fProgram );
            GL_CHECK_ERROR();
            Reset( data );
        }
    }
    
    if (fCleanupShellTransform)
    {
        fCleanupShellTransform( &fCleanupShellTransform ); // n.b. used as own key
    }

    Rtt_DELETE( fUniformsCache );
    
    fUniformsCache = NULL;
}

void
GLProgram::Bind( Program::Version version )
{
    VersionData& data = fData[version];
    
    #if DEFER_CREATION
        if( !data.fProgram )
        {
            Create( version, data );
            
            if ( fData[version].HasTime() )
            {
				Program* program = (Program*)fResource;
				
				program->GetShaderResource()->SetUsesTime( true );
				
				ShaderResource::SetAddedUsesTime( true );
            }
        }
    #endif
    
    glUseProgram( data.fProgram );
    GL_CHECK_ERROR();
}

void
GLProgram::Create( Program::Version version, VersionData& data )
{
#ifndef Rtt_USE_PRECOMPILED_SHADERS
    data.fVertexShader = glCreateShader( GL_VERTEX_SHADER );
    data.fFragmentShader = glCreateShader( GL_FRAGMENT_SHADER );
    GL_CHECK_ERROR();
#endif

    data.fProgram = glCreateProgram();
    GL_CHECK_ERROR();

#ifndef Rtt_USE_PRECOMPILED_SHADERS
    glAttachShader( data.fProgram, data.fVertexShader );
    glAttachShader( data.fProgram, data.fFragmentShader );
    GL_CHECK_ERROR();
#endif
    
    Update( version, data );
}

static int
CountLines( const char **segments, int numSegments )
{
    int result = 0;

    for ( int i = 0; i < numSegments; i++ )
    {
        result += Program::CountLines( segments[i] );
    }

    return result;
}

static void
SetShaderSource( GLuint shader, CoronaShellTransformParams & params, const CoronaShellTransform * xform, void * userData, void * key )
{
    const char ** strings = params.sources, ** old = strings;

    if (xform)
    {
        Rtt_ASSERT( xform->begin );
        
        strings = xform->begin( &params, userData, key );

        if (!strings)
        {
            strings = old;
        }
    }

    glShaderSource( shader, params.nsources, strings, NULL );

    if (xform && xform->finish)
    {
        xform->finish( userData, key );
    }

    GL_CHECK_ERROR();
}

static bool
IsDoubleType( CoronaVertexExtensionAttributeType )
{
    return false; // NYI
}

#define ARRAY_AND_N( NAME ) NAME, sizeof(NAME)

static const char kAttributePrefix[] = "a_";
static const char kLongestTypeName[] = "double";
static const char kDeclareAttributeString[] = "attribute %s%s %s;\n";
static const char kDeclareAttributeArrayString[] = "attribute %s%s %s[%i];\n";
static const char kMacroFormatString[] = "#define Corona%c%s a_%s\n";
static const char kSuffixedMacroFormatString[] = "#define Corona%c%s%i a_%s[%i - 1]\n";
static const char kIndexedMacroFormatString[] = "#define Corona%c%sAt( pos ) a_%s[(int)pos]\n";

Rtt_STATIC_ASSERT( sizeof(kDeclareAttributeArrayString) > sizeof(kDeclareAttributeString) );
Rtt_STATIC_ASSERT( sizeof(kSuffixedMacroFormatString) > sizeof(kMacroFormatString) );
Rtt_STATIC_ASSERT( sizeof(kIndexedMacroFormatString) > sizeof(kMacroFormatString) );

#define LEN( str_const, num_specifiers ) ( sizeof(str_const) - 1 /* NUL */ - ( num_specifiers * 2 ) )

enum {
	kAttributePrefixLen = LEN( kAttributePrefix, 0 ),
	kLongestTypeNameLen = LEN( kLongestTypeName, 0 ),
	kDeclareAttributeArrayStringLen = LEN( kDeclareAttributeArrayString, 4 )
		+ kLongestTypeNameLen /* type */ + 1 /* suffix digit */ + 64 /* name */ + 2 /* two-digit array size */ ,
	kSuffixedMacroFormatStringLen = LEN( kSuffixedMacroFormatString, 5 )
		+ ( 0 + 64 ) /* name (includes capitalized first letter) */ + 2 /* two-digit suffix */ + 64 /* name */ + 2 /* two-digit index */,
	kIndexedMacroFormatStringLen = LEN( kIndexedMacroFormatString, 3 )
		+ ( 0 + 64 ) /* name (includes capitalized first letter) */ + 64 /* name */ ,
	kProgramAttribStringSize = FormatExtensionList::kMaxAttribs * ( kDeclareAttributeArrayStringLen + kSuffixedMacroFormatStringLen ) +
								( FormatExtensionList::kMaxAttribs / 2 ) * kIndexedMacroFormatStringLen +
								2 /* newline and NUL */
};

#undef LEN

static int
AppendMacroName( const char* name, char extensionAttribStrs[], int wpos )
{
    // TODO: array
    
	int n = snprintf(
		&extensionAttribStrs[wpos], kProgramAttribStringSize - wpos,
		kMacroFormatString,
		toupper( *name ), name + 1, name
		);
	
	Rtt_ASSERT( n > 0 );
	
	return n;
}

static void
GatherAttributeExtensions( const FormatExtensionList* extensionList, char *extensionAttribStrs, const char names[] )
{
	int wpos = 0;
	
    for (int i = 0; i < extensionList->GetAttributeCount(); ++i)
    {
        const FormatExtensionList::Attribute& attribute = extensionList->GetAttributes()[i];
        char count[2] = {};
        
        if (attribute.GetComponentCount() > 1)
        {
            count[0] = '0' + attribute.GetComponentCount();
        }
        
        const char * prim = "float", * vec = "vec";

        CoronaVertexExtensionAttributeType type = (CoronaVertexExtensionAttributeType)attribute.type;

        if (IsDoubleType( type ))
        {
            prim = "double";
            vec = "dvec";
        }
 
        else if (!attribute.IsFloat())
        {
            prim = "int";
            vec = "ivec";
        }
            
		// TODO: is array?
            
		int n = snprintf(
			&extensionAttribStrs[wpos], kProgramAttribStringSize - wpos,
			kDeclareAttributeString,
			*count ? vec : prim, count, names + i * ( 64 + 1 + kAttributePrefixLen )
			);
            
		Rtt_ASSERT( n > 0 );

		wpos += n;
    }
    
	extensionAttribStrs[wpos++] = '\n';
    
    for (int i = 0; i < extensionList->GetAttributeCount(); ++i)
    {
        wpos += AppendMacroName( names + i * ( 64 + 1 + kAttributePrefixLen ) + kAttributePrefixLen, extensionAttribStrs, wpos );
    }
}

void
GLProgram::UpdateShaderSource( Program* program, Program::Version version, VersionData& data, const char names[] )
{
#ifndef Rtt_USE_PRECOMPILED_SHADERS
    char maskBuffer[] = "#define MASK_COUNT 0\n";
    switch( version )
    {
        case Program::kMaskCount1:    maskBuffer[sizeof( maskBuffer ) - 3] = '1'; break;
        case Program::kMaskCount2:    maskBuffer[sizeof( maskBuffer ) - 3] = '2'; break;
        case Program::kMaskCount3:    maskBuffer[sizeof( maskBuffer ) - 3] = '3'; break;
        default: break;
    }

    char highp_support[] = "#define FRAGMENT_SHADER_SUPPORTS_HIGHP 0\n";
    highp_support[ sizeof( highp_support ) - 3 ] = ( CommandBuffer::GetGpuSupportsHighPrecisionFragmentShaders() ? '1' : '0' );

    //! \TODO Make the definition of "TEX_COORD_Z" conditional.
    char texCoordZBuffer[] = "";//#define TEX_COORD_Z 1\n";

    const char *program_header_source = program->GetHeaderSource();
    const char *header = ( program_header_source ? program_header_source : "" );
    
	ShaderResource * shaderResource = program->GetShaderResource();
    const char *languageExtensions = shaderResource->GetExtensionPrelude();

	char header_with_resolved_exts[BUFSIZ];
	snprintf( ARRAY_AND_N( header_with_resolved_exts ), header, languageExtensions ? languageExtensions : "" );
	// ^^^ would be better if just supplying these as sources below, but header is built the way
	// it is, and extensions need to crowd in there too...

    const char* shader_source[5];
    memset( shader_source, 0, sizeof( shader_source ) );
    shader_source[0] = header_with_resolved_exts;
    shader_source[1] = highp_support;
    shader_source[2] = maskBuffer;
    shader_source[3] = texCoordZBuffer;

    if ( program->IsCompilerVerbose() )
    {
        // All the segments except the last one
        int numSegments = sizeof( shader_source ) / sizeof( shader_source[0] ) - 1;
        data.fHeaderNumLines = CountLines( shader_source, numSegments );
    }
    
    const CoronaShellTransform * shellTransform = shaderResource->GetShellTransform();
    CoronaShellTransformParams params = {};
    const char * hints[] = { "header", "highpSupport", "mask", "texCoordZ", NULL };
    void * shellTransformKey = &fCleanupShellTransform; // n.b. done to make cleanup robust

    std::vector< CoronaEffectDetail > details;
    CoronaEffectDetail detail;

    for (int i = 0; shaderResource->GetEffectDetail( i, detail ); ++i)
    {
        details.push_back( detail );
    }

    params.details = details.data();
    params.ndetails = details.size();
    params.userData = shellTransform ? shellTransform->userData : NULL;

    std::vector< U8 > space;
    U8 * spaceData = NULL;

    if (shellTransform && shellTransform->workSpace)
    {
        space.resize( shellTransform->workSpace );

        spaceData = space.data();
    }

    // Vertex shader.
    {
        const char * extendedSources[7] = {}, * extendedHints[8] = {};
        std::string suffixStr, versionStr;
        
        params.hints = hints;
        params.sources = shader_source;
        params.nsources = sizeof(shader_source) / sizeof(shader_source[0]);
        params.type = "vertex";
        
        shader_source[4] = program->GetVertexShaderSource();
        hints[4] = "vertexSource";

        // add any boilerplate for extended vertices and / or instancing
        const FormatExtensionList* extensionList = shaderResource->GetExtensionList();
        
        if (extensionList)
        {
            for (int i = 0; i < 4; ++i)
            {
                extendedSources[i] = shader_source[i];
                extendedHints[i] = hints[i];
            }
                        
			char extensionAttribStrs[kProgramAttribStringSize];
                        
            GatherAttributeExtensions( extensionList, extensionAttribStrs, names );
            
            const char * originalSource = shader_source[4], * originalHint = hints[4];
            U32 nsources = params.nsources + 1;
            
            extendedSources[4] = extensionAttribStrs;
            extendedHints[4] = "extensionAttributes";
            
            // enable instances and / or provide IDs for the same
            if (extensionList->IsInstanced())
            {
                const char * idSuffix = GLGeometry::InstanceIDSuffix();
                
                if (idSuffix)
                {
                    char buf[BUFSIZ];
            
                    if ('*' == *idSuffix)
                    {
                        ++idSuffix;
                        
                        U32 offset = 0;
                        
                        char version[64] = {};
                        
                        for ( ; '\n' != shader_source[0][offset]; offset++ )
                        {
                            Rtt_ASSERT( offset < 63 );
                            Rtt_ASSERT( shader_source[0][offset] );
                            
                            version[offset] = shader_source[0][offset];
                        }
                        
                        snprintf( ARRAY_AND_N( buf ),
                                "%s\n\n#extension GL_%s_draw_instanced : enable%s",
                                version, idSuffix, shader_source[0] + offset );
                        
                        versionStr = buf;
                        
                        extendedSources[0] = versionStr.c_str();
                        // ^^^ TODO: add this to header_with_resolved_exts, above
                    }
                    
					snprintf( ARRAY_AND_N( buf ),
							"\n#define CoronaInstanceID int(gl_InstanceID%s)\n"
							"\n#define CoronaInstanceFloat float(gl_InstanceID%s)\n\n",
							idSuffix, idSuffix );
                    
                    suffixStr = buf;
                    
                    extendedSources[nsources - 1] = suffixStr.c_str();
                }
                
                else
                {
					extendedSources[nsources - 1] = "\n#define CoronaInstanceID 0\n"
												"\n#define CoronaInstanceFloat 0.\n\n";
                }
                
                extendedHints[nsources - 1] = "instanceID";
                
                ++nsources;
            }

            extendedSources[nsources - 1] = originalSource;
            extendedHints[nsources - 1] = originalHint;

            params.hints = extendedHints;
            params.sources = extendedSources;
            params.nsources = nsources;
        }
        
        SetShaderSource( data.fVertexShader, params, shellTransform, spaceData, shellTransformKey );
    }

    // Fragment shader.
    {
        shader_source[4] = ( version == Program::kWireframe ) ? kWireframeSource : program->GetFragmentShaderSource();

        hints[4] = "fragmentSource";
        params.type = "fragment";
        params.hints = hints;
        params.sources = shader_source;
        params.nsources = sizeof(shader_source) / sizeof(shader_source[0]);
        
        SetShaderSource( data.fFragmentShader, params, shellTransform, spaceData, shellTransformKey );
    }
#endif
}

// The following LUTs comprise the inlined image / sampler type constants from OpenGL 4.6 + ES 3.2,
// along with some metadata. The list used by Slang is taken as the final authority:

// https://raw.githubusercontent.com/KhronosGroup/glslang/refs/heads/main/glslang/MachineIndependent/gl_types.h

// These 102 16-bit constants fall into four low-byte sequences: 0x8-0xF, 0x4C-0x6C, 0xC0-0xC5, and
// 0xC9-0xEA. The low bytes themselves fall into the subsequences 0xC-0xD, 0x5D-0x64, and 0xCE-0xD8.
// As it turns out, a common low byte never occurs in more than two of the constants.

// We can flatten the intervals into an index space: 0x-0xF -> 0-7; 0x4C-0x6C -> 8-40, etc. The
// calculations can be simplified a bit by merging the third and fourth sequence, and addressing
// the narrow 0xC6-0xC8 gap in a different way. This almost lets us access the constants, when
// sorted by low byte.

// We need to apply some offseting to account for low bytes belonging to two constants. The first
// such byte is found at index 4 and the last at 65. This range fits comfortably within a 64-bit
// integer; by mapping indices to bits, we can use popcount techniques to produce an offset.

// A given constant is looked up by low byte and comapred against the sorted list. Since pairs
// are always adjacent, we can check both at once.

// The sorting puts most constants with a common low byte on an even boundary, i.e. position 0,
// 2, 4, etc. In fact, a little nudge will make this universal: pretend 0x5C also belongs to a
// pair, and add dummy LUT entries and low-byte accounting in the proper spots. The later LUT
// entries move ahead, and every such pair of constants now share a bin. Thus we can find just
// one bin for a given low byte and try both values at once.

// In the case of the 0xC6-0xC8 gap and 0x5C dummy, neither considered valid, we only care that
// the given comparison fails, so any out-of-reach value can be used in the dummy position. Each
// of those cases is well along in the list, so the very first element is convenient. (As for the
// metadata, an all-0s constant is likewise bogus.)

// An exhaustive trial was run over constants 0-0xFFFF and confirmed that only image and sampler
// types turn up. Currently, this is used to distinguish these uniforms from atomic counters and
// numeric types, when enumerated linked uniforms, but also flags invalid values.

#define PACK( lo, hi ) ( ( (U32)( hi ) << 16 ) | (U32)( lo ) )

static const U32 kConstantPairs[] = {
	PACK( 0x9108, 0x9109 ), /* SAMPLER_MULTISAMPLE, INT_SAMPLER_MULTISAMPLE */
	PACK( 0x910A, 0x910B ), /* UNSIGNED_INT_SAMPLER_MULTISAMPLE, SAMPLER_MULTISAMPLE_ARRAY */
	PACK( 0x900C, 0x910C ), /* SAMPLER_CUBE_MAP_ARRAY, INT_SAMPLER_MULTISAMPLE_ARRAY */
	PACK( 0x910D, 0x900D ), /* UNSIGNED_INT_SAMPLER_MULTISAMPLE_ARRAY, SAMPLER_CUBE_MAP_ARRAY_SHADOW */
	PACK( 0x900E, 0x900F ), /* INT_SAMPLER_CUBE_MAP_ARRAY, UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY */
	PACK( 0x904C, 0x904D ), /* IMAGE_1D, IMAGE_2D */
	PACK( 0x904E, 0x904F ), /* IMAGE_3D, IMAGE_RECTANGLE */
	PACK( 0x9050, 0x9051 ), /* IMAGE_CUBE, IMAGE_BUFFER */
	PACK( 0x9052, 0x9053 ), /* IMAGE_1D_ARRAY, IMAGE_2D_ARRAY */
	PACK( 0x9054, 0x9055 ), /* IMAGE_CUBE_MAP_ARRAY, IMAGE_MULTISAMPLE */
	PACK( 0x9056, 0x9057 ), /* IMAGE_MULTISAMPLE_ARRAY, INT_IMAGE_1D  */
	PACK( 0x9058, 0x9059 ), /* INT_IMAGE_2D, INT_IMAGE_3D */
	PACK( 0x905A, 0x905B ), /* INT_IMAGE_RECTANGLE, INT_IMAGE_CUBE */
	PACK( 0x905C, 0x9108 ), /* INT_IMAGE_BUFFER, SENTINEL */
	PACK( 0x905D, 0x8B5D ), /* INT_IMAGE_1D_ARRAY, SAMPLER_1D */
	PACK( 0x8B5E, 0x905E ), /* SAMPLER_2D, INT_IMAGE_2D_ARRAY */
	PACK( 0x8B5F, 0x905F ), /* SAMPLER_3D, INT_IMAGE_CUBE_MAP_ARRAY */
	PACK( 0x8B60, 0x9060 ), /* SAMPLER_CUBE, INT_IMAGE_MULTISAMPLE */
	PACK( 0x9061, 0x8B61 ), /* INT_IMAGE_MULTISAMPLE_ARRAY, SAMPLER_1D_SHADOW */
	PACK( 0x8B62, 0x9062 ), /* SAMPLER_2D_SHADOW, UNSIGNED_INT_IMAGE_1D */
	PACK( 0x9063, 0x8B63 ), /* UNSIGNED_INT_IMAGE_2D, SAMPLER_RECT */
	PACK( 0x8B64, 0x9064 ), /* SAMPLER_RECT_SHADOW, UNSIGNED_INT_IMAGE_3D */
	PACK( 0x9065, 0x9066 ), /* UNSIGNED_INT_IMAGE_RECTANGLE, UNSIGNED_INT_IMAGE_CUBE */
	PACK( 0x9067, 0x9068 ), /* UNSIGNED_INT_IMAGE_BUFFER, UNSIGNED_INT_IMAGE_1D_ARRAY */
	PACK( 0x9069, 0x906A ), /* UNSIGNED_INT_IMAGE_2D_ARRAY, UNSIGNED_INT_IMAGE_CUBE_MAP_ARRAY */
	PACK( 0x906B, 0x906C ), /* UNSIGNED_INT_IMAGE_MULTISAMPLE, UNSIGNED_INT_IMAGE_MULTISAMPLE_ARRAY */
	PACK( 0x8DC0, 0x8DC1 ), /* SAMPLER_1D_ARRAY, SAMPLER_2D_ARRAY */
	PACK( 0x8DC2, 0x8DC3 ), /* SAMPLER_BUFFER, SAMPLER_1D_ARRAY_SHADOW */
	PACK( 0x8DC4, 0x8DC5 ), /* SAMPLER_2D_ARRAY_SHADOW, SAMPLER_CUBE_SHADOW */
	PACK( 0x9108, 0x9108 ), /* SENTINEL, SENTINEL */
	PACK( 0x9108, 0x8DC9 ), /* SENTINEL, INT_SAMPLER_1D */
	PACK( 0x8DCA, 0x8DCB ), /* INT_SAMPLER_2D, INT_SAMPLER_3D */
	PACK( 0x8DCC, 0x8DCD ), /* INT_SAMPLER_CUBE, INT_SAMPLER_RECT */
	PACK( 0x8DCE, 0x91CE ), /* INT_SAMPLER_1D_ARRAY, FLOAT16_SAMPLER_1D */
	PACK( 0x91CF, 0x8DCF ), /* FLOAT16_SAMPLER_2D, INT_SAMPLER_2D_ARRAY */
	PACK( 0x91D0, 0x8DD0 ), /* FLOAT16_SAMPLER_3D, INT_SAMPLER_BUFFER */
	PACK( 0x8DD1, 0x91D1 ), /* UNSIGNED_INT_SAMPLER_1D, FLOAT16_SAMPLER_CUBE */
	PACK( 0x8DD2, 0x91D2 ), /* UNSIGNED_INT_SAMPLER_2D, FLOAT16_SAMPLER_2D_RECT */
	PACK( 0x91D3, 0x8DD3 ), /* FLOAT16_SAMPLER_1D_ARRAY, UNSIGNED_INT_SAMPLER_3D */
	PACK( 0x91D4, 0x8DD4 ), /* FLOAT16_SAMPLER_2D_ARRAY, UNSIGNED_INT_SAMPLER_CUBE */
	PACK( 0x91D5, 0x8DD5 ), /* FLOAT16_SAMPLER_CUBE_MAP_ARRAY, UNSIGNED_INT_SAMPLER_RECT */
	PACK( 0x8DD6, 0x91D6 ), /* UNSIGNED_INT_SAMPLER_1D_ARRAY, FLOAT16_SAMPLER_BUFFER */
	PACK( 0x91D7, 0x8DD7 ), /* FLOAT16_SAMPLER_2D_MULTISAMPLE, UNSIGNED_INT_SAMPLER_2D_ARRAY */
	PACK( 0x8DD8, 0x91D8 ), /* UNSIGNED_INT_SAMPLER_BUFFER, FLOAT16_SAMPLER_2D_MULTISAMPLE_ARRAY */
	PACK( 0x91D9, 0x91DA ), /* FLOAT16_SAMPLER_1D_SHADOW, FLOAT16_SAMPLER_2D_SHADOW */
	PACK( 0x91DB, 0x91DC ), /* FLOAT16_SAMPLER_2D_RECT_SHADOW, FLOAT16_SAMPLER_1D_ARRAY_SHADOW */
	PACK( 0x91DD, 0x91DE ), /* FLOAT16_SAMPLER_2D_ARRAY_SHADOW, FLOAT16_SAMPLER_CUBE_SHADOW */
	PACK( 0x91DF, 0x91E0 ), /* FLOAT16_SAMPLER_CUBE_MAP_ARRAY_SHADOW, FLOAT16_IMAGE_1D */
	PACK( 0x91E1, 0x91E2 ), /* FLOAT16_IMAGE_2D, FLOAT16_IMAGE_3D */
	PACK( 0x91E3, 0x91E4 ), /* FLOAT16_IMAGE_2D_RECT, FLOAT16_IMAGE_CUBE */
	PACK( 0x91E5, 0x91E6 ), /* FLOAT16_IMAGE_1D_ARRAY, FLOAT16_IMAGE_2D_ARRAY */
	PACK( 0x91E7, 0x91E8 ), /* FLOAT16_IMAGE_CUBE_MAP_ARRAY, FLOAT16_IMAGE_BUFFER */
	PACK( 0x91E9, 0x91EA )  /* FLOAT16_IMAGE_2D_MULTISAMPLE, FLOAT16_IMAGE_2D_MULTISAMPLE_ARRAY */
};

Rtt_STATIC_ASSERT( Texture::kNumFamilies <= ( 1 << 2 ) );
Rtt_STATIC_ASSERT( Texture::kNumTargets <= ( 1 << 3 ) );

#define INTEGER_IMAGE( KIND, TARGET ) { Texture::k##KIND##Integer, Texture::k##TARGET, 1, 0 }
#define INTEGER_IMAGE_ARRAY( KIND, TARGET ) { Texture::k##KIND##Integer, Texture::k##TARGET, 1, 1 }
#define IMAGE( TARGET ) { Texture::kFloatingPoint, Texture::k##TARGET, 1, 0 }
#define IMAGE_ARRAY( TARGET ) { Texture::kFloatingPoint, Texture::k##TARGET, 1, 1 }
#define INTEGER_SAMPLER( KIND, TARGET ) { Texture::k##KIND##Integer, Texture::k##TARGET, 0, 0 }
#define INTEGER_SAMPLER_ARRAY( KIND, TARGET ) { Texture::k##KIND##Integer, Texture::k##TARGET, 0, 1 }
#define SAMPLER( TARGET ) { Texture::kFloatingPoint, Texture::k##TARGET, 0, 0 }
#define SAMPLER_ARRAY( TARGET ) { Texture::kFloatingPoint, Texture::k##TARGET, 0, 1 }
#define SHADOW_SAMPLER( TARGET ) { Texture::kOtherFamily, Texture::k##TARGET, 0, 0 }
#define SHADOW_SAMPLER_ARRAY( TARGET ) { Texture::kOtherFamily, Texture::k##TARGET, 0, 1 }

static const SamplerTypeDetails kDetails[] = {
	{ /* SENTINEL */ },
	SAMPLER( Multisample ), /* SAMPLER_MULTISAMPLE */
	INTEGER_SAMPLER( Signed, Multisample ), /* INT_SAMPLER_MULTISAMPLE */
	INTEGER_SAMPLER( Unsigned, Multisample ), /* UNSIGNED_INT_SAMPLER_MULTISAMPLE */
	SAMPLER_ARRAY( Multisample ), /* SAMPLER_MULTISAMPLE_ARRAY */
	SAMPLER_ARRAY( Cube ), /* SAMPLER_CUBE_MAP_ARRAY */
	INTEGER_SAMPLER_ARRAY( Signed, Multisample ), /* INT_SAMPLER_MULTISAMPLE_ARRAY */
	INTEGER_SAMPLER_ARRAY( Unsigned, Multisample ), /* UNSIGNED_INT_SAMPLER_MULTISAMPLE_ARRAY */
	SHADOW_SAMPLER_ARRAY( Cube ), /* SAMPLER_CUBE_MAP_ARRAY_SHADOW */
	INTEGER_SAMPLER_ARRAY( Signed, Cube ), /* INT_SAMPLER_CUBE_MAP_ARRAY */
	INTEGER_SAMPLER_ARRAY( Unsigned, Cube ), /* UNSIGNED_INT_SAMPLER_CUBE_MAP_ARRAY */
	IMAGE( 1D ), /* IMAGE_1D */
	IMAGE( 2D ), /* IMAGE_2D */
	IMAGE( 3D ), /* IMAGE_3D */
	IMAGE( Rectangle ), /* IMAGE_RECTANGLE */
	IMAGE( Cube ), /* IMAGE_CUBE */
	IMAGE( Buffer ), /* IMAGE_BUFFER */
	IMAGE_ARRAY( 1D ), /* IMAGE_1D_ARRAY */
	IMAGE_ARRAY( 2D ), /* IMAGE_2D_ARRAY */
	IMAGE_ARRAY( Cube ), /* IMAGE_CUBE_MAP_ARRAY */
	IMAGE( Multisample ), /* IMAGE_MULTISAMPLE */
	IMAGE_ARRAY( Multisample ), /* IMAGE_MULTISAMPLE_ARRAY */
	INTEGER_IMAGE( Signed, 1D ), /* INT_IMAGE_1D  */
	INTEGER_IMAGE( Signed, 2D ), /* INT_IMAGE_2D */
	INTEGER_IMAGE( Signed, 3D ), /* INT_IMAGE_3D */
	INTEGER_IMAGE( Signed, Rectangle ), /* INT_IMAGE_RECTANGLE */
	INTEGER_IMAGE( Signed, Cube ), /* INT_IMAGE_CUBE */
	INTEGER_IMAGE( Signed, Buffer ), /* INT_IMAGE_BUFFER */
	{ /* SENTINEL */ },
	INTEGER_IMAGE_ARRAY( Signed, 1D ), /* INT_IMAGE_1D_ARRAY */
	SAMPLER( 1D ), /* SAMPLER_1D */
	SAMPLER( 2D ), /* SAMPLER_2D */
	INTEGER_IMAGE_ARRAY( Signed, 2D ), /* INT_IMAGE_2D_ARRAY */
	SAMPLER( 3D ), /* SAMPLER_3D */
	INTEGER_IMAGE_ARRAY( Signed, Cube ), /* INT_IMAGE_CUBE_MAP_ARRAY */
	SAMPLER( Cube ), /* SAMPLER_CUBE */
	INTEGER_IMAGE( Signed, Multisample ), /* INT_IMAGE_MULTISAMPLE */
	INTEGER_IMAGE_ARRAY( Signed, Multisample ), /* INT_IMAGE_MULTISAMPLE_ARRAY */
	SHADOW_SAMPLER( 1D ), /* SAMPLER_1D_SHADOW */
	SHADOW_SAMPLER( 2D ), /* SAMPLER_2D_SHADOW */
	INTEGER_IMAGE( Unsigned, 1D ), /* UNSIGNED_INT_IMAGE_1D */
	INTEGER_IMAGE( Unsigned, 2D ), /* UNSIGNED_INT_IMAGE_2D */
	SAMPLER( Rectangle ), /* SAMPLER_RECT */
	SHADOW_SAMPLER( Rectangle ), /* SAMPLER_RECT_SHADOW */
	INTEGER_IMAGE( Unsigned, 3D ), /* UNSIGNED_INT_IMAGE_3D */
	INTEGER_IMAGE( Unsigned, Rectangle ), /* UNSIGNED_INT_IMAGE_RECTANGLE */
	INTEGER_IMAGE( Unsigned, Cube ), /* UNSIGNED_INT_IMAGE_CUBE */
	INTEGER_IMAGE( Unsigned, Buffer ), /* UNSIGNED_INT_IMAGE_BUFFER */
	INTEGER_IMAGE_ARRAY( Unsigned, 1D ), /* UNSIGNED_INT_IMAGE_1D_ARRAY */
	INTEGER_IMAGE_ARRAY( Unsigned, 2D ), /* UNSIGNED_INT_IMAGE_2D_ARRAY */
	INTEGER_IMAGE_ARRAY( Unsigned, Cube ), /* UNSIGNED_INT_IMAGE_CUBE_MAP_ARRAY */
	INTEGER_IMAGE( Unsigned, Multisample ), /* UNSIGNED_INT_IMAGE_MULTISAMPLE */
	INTEGER_IMAGE_ARRAY( Unsigned, Multisample ), /* UNSIGNED_INT_IMAGE_MULTISAMPLE_ARRAY */
	SAMPLER_ARRAY( 1D ), /* SAMPLER_1D_ARRAY	 */
	SAMPLER_ARRAY( 2D ), /* SAMPLER_2D_ARRAY */
	SAMPLER( Buffer ), /* SAMPLER_BUFFER */
	SHADOW_SAMPLER_ARRAY( 1D ), /* SAMPLER_1D_ARRAY_SHADOW */
	SHADOW_SAMPLER_ARRAY( 2D ), /* SAMPLER_2D_ARRAY_SHADOW */
	SHADOW_SAMPLER( Cube ), /* SAMPLER_CUBE_SHADOW */
	{ /* SENTINEL */ },
	{ /* SENTINEL */ },
	{ /* SENTINEL */ },
	INTEGER_SAMPLER( Signed, 1D ), /* INT_SAMPLER_1D */
	INTEGER_SAMPLER( Signed, 2D ), /* INT_SAMPLER_2D */
	INTEGER_SAMPLER( Signed, 3D ), /* INT_SAMPLER_3D */
	INTEGER_SAMPLER( Signed, Cube ), /* INT_SAMPLER_CUBE */
	INTEGER_SAMPLER( Signed, Rectangle ), /* INT_SAMPLER_RECT */
	INTEGER_SAMPLER_ARRAY( Signed, 1D ), /* INT_SAMPLER_1D_ARRAY */
	SAMPLER( 1D ), /* FLOAT16_SAMPLER_1D */
	SAMPLER( 2D ), /* FLOAT16_SAMPLER_2D */
	INTEGER_SAMPLER_ARRAY( Signed, 2D ), /* INT_SAMPLER_2D_ARRAY */
	SAMPLER( 3D ), /* FLOAT16_SAMPLER_3D */
	INTEGER_SAMPLER( Signed, Buffer ), /* INT_SAMPLER_BUFFER */
	INTEGER_SAMPLER( Unsigned, 1D ), /* UNSIGNED_INT_SAMPLER_1D */
	SAMPLER( Cube ), /* FLOAT16_SAMPLER_CUBE */
	INTEGER_SAMPLER( Unsigned, 2D ), /* UNSIGNED_INT_SAMPLER_2D */
	SAMPLER( 2D ), /* FLOAT16_SAMPLER_2D_RECT */
	SAMPLER_ARRAY( 1D ), /* FLOAT16_SAMPLER_1D_ARRAY */
	INTEGER_SAMPLER( Unsigned, 3D ), /* UNSIGNED_INT_SAMPLER_3D */
	SAMPLER_ARRAY( 2D ), /* FLOAT16_SAMPLER_2D_ARRAY */
	INTEGER_SAMPLER( Unsigned, Cube ), /* UNSIGNED_INT_SAMPLER_CUBE */
	SAMPLER_ARRAY( Cube ), /* FLOAT16_SAMPLER_CUBE_MAP_ARRAY */
	INTEGER_SAMPLER( Unsigned, Rectangle ), /* UNSIGNED_INT_SAMPLER_RECT */
	INTEGER_SAMPLER_ARRAY( Unsigned, 1D ), /* UNSIGNED_INT_SAMPLER_1D_ARRAY */
	SAMPLER( Buffer ), /* FLOAT16_SAMPLER_BUFFER */
	SAMPLER( Multisample ), /* FLOAT16_SAMPLER_2D_MULTISAMPLE */
	INTEGER_SAMPLER_ARRAY( Unsigned, 2D ), /* UNSIGNED_INT_SAMPLER_2D_ARRAY */
	INTEGER_SAMPLER( Unsigned, Buffer ), /* UNSIGNED_INT_SAMPLER_BUFFER  */
	SAMPLER_ARRAY( Multisample ), /* FLOAT16_SAMPLER_2D_MULTISAMPLE_ARRAY */
	SHADOW_SAMPLER( 1D ), /* FLOAT16_SAMPLER_1D_SHADOW */
	SHADOW_SAMPLER( 2D ), /* FLOAT16_SAMPLER_2D_SHADOW */
	SHADOW_SAMPLER( Rectangle ), /* FLOAT16_SAMPLER_2D_RECT_SHADOW */
	SHADOW_SAMPLER_ARRAY( 1D ), /* FLOAT16_SAMPLER_1D_ARRAY_SHADOW */
	SHADOW_SAMPLER_ARRAY( 2D ), /* FLOAT16_SAMPLER_2D_ARRAY_SHADOW */
	SHADOW_SAMPLER( Cube ), /* FLOAT16_SAMPLER_CUBE_SHADOW */
	SHADOW_SAMPLER_ARRAY( Cube ), /* FLOAT16_SAMPLER_CUBE_MAP_ARRAY_SHADOW */
	IMAGE( 1D ), /* FLOAT16_IMAGE_1D */
	IMAGE( 2D ), /* FLOAT16_IMAGE_2D */
	IMAGE( 3D ), /* FLOAT16_IMAGE_3D */
	IMAGE( Rectangle ), /* FLOAT16_IMAGE_2D_RECT */
	IMAGE( Cube ), /* FLOAT16_IMAGE_CUBE */
	IMAGE_ARRAY( 1D ), /* FLOAT16_IMAGE_1D_ARRAY */
	IMAGE_ARRAY( 2D ), /* FLOAT16_IMAGE_2D_ARRAY */
	IMAGE_ARRAY( Cube ), /* FLOAT16_IMAGE_CUBE_MAP_ARRAY */
	IMAGE( Buffer ), /* FLOAT16_IMAGE_BUFFER */
	IMAGE( Multisample ), /* FLOAT16_IMAGE_2D_MULTISAMPLE */
	IMAGE_ARRAY( Multisample ), /* FLOAT16_IMAGE_2D_MULTISAMPLE_ARRAY */
};

#undef INTEGER_IMAGE
#undef INTEGER_IMAGE_ARRAY
#undef IMAGE
#undef IMAGE_ARRAY
#undef INTEGER_SAMPLER
#undef INTEGER_SAMPLER_ARRAY
#undef SAMPLER
#undef SAMPLER_ARRAY
#undef SHADOW_SAMPLER
#undef SHADOW_SAMPLER_ARRAY

#define COUNT_IN_RANGE( low, high ) ( high - low + 1 )
#define GET_OFFSET_IN_RANGE( low, high, base, value ) ( ( ( (U8)( value - low ) ) <= (U8)( high - low ) ) ? ( base + value - low ) : 0 )
#define SPLAT( v ) PACK( v, v )

static int
ClassifySampler( GLenum type )
{
	const int Base1 = 0;
	const int Base2 = Base1 + COUNT_IN_RANGE( 0x8, 0xF );
	const int Base3 = Base2 + COUNT_IN_RANGE( 0x4C, 0x6C );

	GLenum low = type & 0xFF;
	int offset = GET_OFFSET_IN_RANGE( 0x8, 0xF, Base1, low ) |
				GET_OFFSET_IN_RANGE( 0x4C, 0x6C, Base2, low ) |
				GET_OFFSET_IN_RANGE( 0xC0, 0xEA, Base3, low );

	// When we sort the values by low byte, the first one with two entries is at
	// index 4, and the last at index 65. With a slight offset this fits neatly
	// into a 64-bit integer, and clamping at either end satisfies empty or full
	// outside the range. (There is a false pair for 0x5C; it has one legitimate
	// full value, and also a sentinel. With that done, however, each proper one
	// starts at an even offset, and thus both values pair up.)
	int shift = Min( Max( offset - 4, 0 ), 63 );
	uint64_t mask = ( 1ULL << shift ) - 1;

	offset += __builtin_popcountll( 0x3FF800001FF00003ULL & mask ); // TODO: Windows

	// Two-element SWAR to check both values in pair.
	uint32_t diff = kConstantPairs[offset / 2] ^ SPLAT( type );
    uint32_t hits = ( diff - SPLAT( 0x0001 ) ) & ~diff & SPLAT( 0x8000 );

	return hits ? ( offset & ~1 ) + ( 0x80000000UL == hits ) + 1 : 0;
}

#undef COUNT_IN_RANGE
#undef GET_OFFSET_IN_RANGE
#undef SPLAT
#undef PACK

const size_t kFillSamplerNameLength = sizeof( "u_FillSampler?" ) - 1;
const size_t kMaskSamplerNameLength = sizeof( "u_MaskSampler?" ) - 1;

Rtt_STATIC_ASSERT( kFillSamplerNameLength == kMaskSamplerNameLength );

static bool
IsBuiltInSampler( GLchar* buf )
{
	const union {
		char _[8];
		U64 u64;
		
		bool operator == (U64 other) const { return u64 == other; }
	}

	// first half:
	kFill = { 'u', '_', 'F', 'i', 'l', 'l', 'S', 'a' },
	kMask = { 'u', '_', 'M', 'a', 's', 'k', 'S', 'a' },
	
	// second half:
	kDigit0 = { 'm', 'p', 'l', 'e', 'r', '0' }, // n.b. adds a couple NULs
	kDigit1 = { 'm', 'p', 'l', 'e', 'r', '1' },
	kDigit2 = { 'm', 'p', 'l', 'e', 'r', '2' },
	kWipeJunk = { "\xFF\xFF\xFF\xFF\xFF\xFF\xFF" }; // terminating NUL wipes spurious final byte (builtins have 14-character names + their own NUL)

	U64 name1, name2;
	
	// Reinterpret buf as two U64s. It should be sufficiently aligned that a
	// cast would also work, but compilers seem to smile on the idiom here.
	memcpy(&name1, buf, sizeof(U64));
	memcpy(&name2, buf + sizeof(U64), sizeof(U64));

	U64 digit = kWipeJunk.u64 & name2;

	// n.b. 01 satisfies both 01 and 11, but only the latter matches 10, i.e. limiting '2' to mask samplers	
	int prefix_mask = ( kFill == name1 ? 0b01 : 0 ) | ( kMask == name1 ? 0b11 : 0 );
	int suffix_mask = ( kDigit0 == digit ? 0b01 : 0 ) | ( kDigit1 == digit ? 0b01 : 0 ) | ( kDigit2 == digit ? 0b10 : 0 );

	return !!( prefix_mask & suffix_mask );
}

struct SamplerItem {
	GLchar* buf;
	GLint extraLoc;
	SamplerTypeDetails details;
	U32 extraLocUnit;
	U32 length;
	
	static int Compare( const void* p1, const void* p2 )
	{
		const SamplerItem *si1 = (const SamplerItem*)p1, *si2 = (const SamplerItem*)p2;

		return strcmp( (const char*)si1->buf, (const char*)si2->buf );
	}
};

static bool
ValidateLaterVersion( const ShaderResource* shaderResource, const SamplerTypeDetails builtinInfo[2], int numUnits, SamplerItem items[] )
{
	if ( shaderResource->GetExtraTextureCount() != numUnits )
	{
		Rtt_LogException( "ERROR: shader versions disagree about extra texture counts" );
		return false;
	}
	else if ( !shaderResource->GetFillInfo(0).Matches( builtinInfo[0] ) || !shaderResource->GetFillInfo(1).Matches( builtinInfo[1] ) )
	{
		Rtt_LogException( "ERROR: shader versions disagree in fill sampler details" );
		return false;
	}
	else
	{
		const SamplerTypeDetails* extraDetails = shaderResource->GetExtraTextureDetails();
		const U8* extraTextureNames = shaderResource->GetExtraTextureNames();
		
		for ( int i = 0; i < numUnits; i++ )
		{
			U8 name[ExtraTextureInfo::kMaxPackedNameLength];
			
			ExtraTextureInfo::EncodeName( name, items[i].buf );

			int pos = ExtraTextureInfo::FindNameInList( name, extraTextureNames, numUnits );
			if ( pos >= 0 && items[i].details.Matches( extraDetails[pos] ) ) // if found, check that details also agree
			{
				items[i].extraLocUnit = (U8)pos;
			}
			else
			{
				Rtt_LogException( "ERROR: shader versions disagree about extra bound textures or their sampler details" );
				return false;
			}
		}
	}

	return true;
}

static void
GatherSamplers( GLuint program, GLchar stash[], SamplerItem items[], const int numItems, SamplerTypeDetails builtinInfo[], LengthAccumulator& nameLengths )
{
	GLint activeUniformCount = 0, maxUnits;
	glGetProgramiv( program, GL_ACTIVE_UNIFORMS, &activeUniformCount );
	glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &maxUnits );

	GLchar * buf = stash;
    for ( GLint i = 0; i < activeUniformCount; i++ )
    {
		GLint size;
		GLenum type;
		GLsizei length;
		glGetActiveUniform( program, i, ExtraTextureInfo::kMaxNameLength + 1, &length, &size, &type, buf );

		int details_index = ClassifySampler( type );
		if ( 0 == details_index ) // not a sampler?
		{
			continue;
		}
	
		if ( kFillSamplerNameLength == length && IsBuiltInSampler( buf ) ) // built-in?
		{
			int index = ( 'F' == buf[2] ) ? buf[length - 1] - '0' : 2;
			
			builtinInfo[index] = kDetails[details_index];
		
			continue;
		}
		else if ( ExtraTextureInfo::kMaxNameLength + 1 == length )
		{
			Rtt_LogException( "WARNING: sampler name `%s` is too long; skipping", buf );
			continue;
		}
		else if ( numItems == nameLengths.GetCount() || maxUnits == nameLengths.GetCount() )
		{
			Rtt_LogException( "WARNING: sampler `%s` potentially valid, but %u units already allocated; ignoring", buf, numItems );
			continue;
		}
		else if ( 0 == strncmp( buf, "gl_", 3 ) || 0 == strncmp( buf, "__", 2 ) )
		{
			Rtt_LogException( "WARNING: samplers with `%s` prefix are reserved", 'g' == *buf ? "gl_" : "__" );
			continue;
		}
	
		GLint loc = glGetUniformLocation( program, buf );
	
		Rtt_ASSERT( -1 != loc );

		int numUnits = nameLengths.GetCount();

		items[numUnits].buf = buf;
		items[numUnits].extraLoc = loc;
		items[numUnits].length = (U8)length;
		items[numUnits].details = kDetails[details_index];
		
		nameLengths.AddLength( length );
		
		buf += ExtraTextureInfo::kMaxNameLength;
	}
}

static void
AttachExtraTextureInfo( ShaderResource* shaderResource, SamplerItem items[], SamplerTypeDetails builtinInfo[], const LengthAccumulator& nameLengths )
{
	U8* extraTextureInfo = NULL;
	int numUnits = nameLengths.GetCount();
	if ( numUnits > 0 )
	{
		qsort( items, numUnits, sizeof(SamplerItem), SamplerItem::Compare ); // n.b. also done by Paint
	
		extraTextureInfo = (U8*)Rtt_MALLOC( NULL, numUnits * ( 1 + sizeof(SamplerTypeDetails) ) + nameLengths.GetTotalBytes() ); // details + (count, name) arrays

		SamplerTypeDetails* details = (SamplerTypeDetails*)extraTextureInfo;
		U8* names = extraTextureInfo + numUnits * sizeof(SamplerTypeDetails);
		NamesEncoder encoder( names, nameLengths );
		for ( U32 i = 0; i < numUnits; i++ )
		{
			items[i].extraLocUnit = i;

			*details++ = items[i].details;

			encoder.Encode( items[i].buf, items[i].length );
		}
		encoder.CheckTotalCount();
	}
	
	shaderResource->SetTextureInfo( extraTextureInfo, (U8)numUnits, builtinInfo );
}

void
GLProgram::Update( Program::Version version, VersionData& data )
{
    Program* program = static_cast<Program*>( fResource );

#ifndef Rtt_USE_PRECOMPILED_SHADERS
    glBindAttribLocation( data.fProgram, Geometry::kVertexPositionAttribute, "a_Position" );
    glBindAttribLocation( data.fProgram, Geometry::kVertexTexCoordAttribute, "a_TexCoord" );
    glBindAttribLocation( data.fProgram, Geometry::kVertexColorScaleAttribute, "a_ColorScale" );
    glBindAttribLocation( data.fProgram, Geometry::kVertexUserDataAttribute, "a_UserData" );
    GL_CHECK_ERROR();

    const FormatExtensionList* extensionList = program->GetShaderResource()->GetExtensionList();
    
    char names[FormatExtensionList::kMaxAttribs * ( 64 + 1 + kAttributePrefixLen )];

    if (extensionList)
    {
        GLuint first = Geometry::FirstExtraAttribute();

		char *buf = names;
		for ( auto&& iter : extensionList->NamedAttributes() )
		{
			memcpy( buf, kAttributePrefix, kAttributePrefixLen );

			String::DecodeIdentifier( buf + kAttributePrefixLen, iter.nameData, iter.triplesCount );
			
            glBindAttribLocation( data.fProgram, first + iter.attributeIndex, buf );
            
            buf += kAttributePrefixLen + ( 64 + 1 );
        }

        GL_CHECK_ERROR();
    }
#endif

    UpdateShaderSource( program,
                        version,
                        data,
                        names );

#ifdef Rtt_USE_PRECOMPILED_SHADERS
    ShaderBinary *shaderBinary = program->GetCompiledShaders()->Get(version);
    glProgramBinaryOES(data.fProgram, GL_PROGRAM_BINARY_ANGLE, shaderBinary->GetBytes(), shaderBinary->GetByteCount());
    GL_CHECK_ERROR();
    GLint linkResult = 0;
    glGetProgramiv(data.fProgram, GL_LINK_STATUS, &linkResult);
    if (!linkResult)
    {
        const int MAX_MESSAGE_LENGTH = 1024;
        char message[MAX_MESSAGE_LENGTH];
        GLint resultLength = 0;
        glGetProgramInfoLog(data.fProgram, MAX_MESSAGE_LENGTH, &resultLength, message);
        Rtt_LogException(message);
    }
    int locationIndex;
    locationIndex = glGetAttribLocation(data.fProgram, "a_Position");
    locationIndex = glGetAttribLocation(data.fProgram, "a_TexCoord");
    locationIndex = glGetAttribLocation(data.fProgram, "a_ColorScale");
    locationIndex = glGetAttribLocation(data.fProgram, "a_UserData");
#else
    bool isVerbose = program->IsCompilerVerbose();
    int kernelStartLine = 0;

    glCompileShader( data.fVertexShader );
    if ( isVerbose )
    {
        kernelStartLine = data.fHeaderNumLines + program->GetVertexShellNumLines();
    }
    CheckShaderCompilationStatus( data.fVertexShader, isVerbose, "vertex", kernelStartLine );
    GL_CHECK_ERROR();

    glCompileShader( data.fFragmentShader );
    if ( isVerbose )
    {
        kernelStartLine = data.fHeaderNumLines + program->GetFragmentShellNumLines();
    }
    CheckShaderCompilationStatus( data.fFragmentShader, isVerbose, "fragment", kernelStartLine );
    GL_CHECK_ERROR();

    glLinkProgram( data.fProgram );
    CheckProgramLinkStatus( data.fProgram, isVerbose );
    GL_CHECK_ERROR();
#endif

    data.fUniformLocations[Uniform::kViewProjectionMatrix] = glGetUniformLocation( data.fProgram, "u_ViewProjectionMatrix" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kMaskMatrix0] = glGetUniformLocation( data.fProgram, "u_MaskMatrix0" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kMaskMatrix1] = glGetUniformLocation( data.fProgram, "u_MaskMatrix1" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kMaskMatrix2] = glGetUniformLocation( data.fProgram, "u_MaskMatrix2" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kTotalTime] = glGetUniformLocation( data.fProgram, "u_TotalTime" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kDeltaTime] = glGetUniformLocation( data.fProgram, "u_DeltaTime" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kTexelSize] = glGetUniformLocation( data.fProgram, "u_TexelSize" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kContentScale] = glGetUniformLocation( data.fProgram, "u_ContentScale" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kUserData0] = glGetUniformLocation( data.fProgram, "u_UserData0" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kUserData1] = glGetUniformLocation( data.fProgram, "u_UserData1" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kUserData2] = glGetUniformLocation( data.fProgram, "u_UserData2" );
    GL_CHECK_ERROR();
    data.fUniformLocations[Uniform::kUserData3] = glGetUniformLocation( data.fProgram, "u_UserData3" );
    GL_CHECK_ERROR();
   
    glUseProgram( data.fProgram );
    glUniform1i( glGetUniformLocation( data.fProgram, "u_FillSampler0" ), Texture::kFill0 );
    glUniform1i( glGetUniformLocation( data.fProgram, "u_FillSampler1" ), Texture::kFill1 );
    glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler0" ), Texture::kMask0 );
    glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler1" ), Texture::kMask1 );
    glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler2" ), Texture::kMask2 );
	
	GLint maxLength;
	glGetProgramiv( data.fProgram, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxLength );

	if ( maxLength > ExtraTextureInfo::kMaxNameLength )
	{
		Rtt_LogException( "WARNING: at least one uniform greater than %i character in length (%i)", ExtraTextureInfo::kMaxNameLength, maxLength );
	}

    Rtt_STATIC_ASSERT( sizeof( kDetails ) / sizeof( kDetails[0] ) < 256 );
    
    SamplerItem items[ 32 - Texture::kNumUnits ];
    
    const U32 kNumItems = sizeof( items ) / sizeof( *items );

	SamplerTypeDetails builtinInfo[3] = {}; // 0-1 = fill(0|1); 2 = mask (junk)
    GLchar stash[kNumItems * ExtraTextureInfo::kMaxNameLength + 2]; // n.b. 2 bytes for NUL + one guard character
	
	LengthAccumulator nameLengths;
	GatherSamplers( data.fProgram, stash, items, kNumItems, builtinInfo, nameLengths );

	ShaderResource* shaderResource = program->GetShaderResource();
	if ( !shaderResource->HasTextureInfo() )
	{
		AttachExtraTextureInfo( shaderResource, items, builtinInfo, nameLengths );
	}
	else
	{
		Rtt_VERIFY( ValidateLaterVersion( shaderResource, builtinInfo, nameLengths.GetCount(), items ) );
	}
	
	for (int i = 0, iMax = nameLengths.GetCount(); i < iMax; i++)
	{
		glUniform1i( items[i].extraLoc, Texture::kNumUnits + items[i].extraLocUnit );
	}
    
    glUseProgram( 0 );
    GL_CHECK_ERROR();
}

void
GLProgram::Reset( VersionData& data )
{
    data.fProgram = 0;
    data.fVertexShader = 0;
    data.fFragmentShader = 0;

    for( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
    {
        // OpenGL uses the location -1 for inactive uniforms
        const GLint kInactiveLocation = -1;
        data.fUniformLocations[ i ] = kInactiveLocation;

        // CommandBuffer also initializes timestamp to zero
        const U32 kTimestamp = 0;
        data.fTimestamps[ i ] = kTimestamp;
    }
    
    data.fHeaderNumLines = 0;
}

GLExtraUniforms::GLExtraUniforms()
:   fVersion( Program::kNumVersions ),
    fVersionData( NULL ),
    fCache( NULL )
{
}

GLExtraUniforms::GLExtraUniforms( Program::Version version, const GLProgram::VersionData * versionData, GLProgramUniformsCache ** cache )
:   fVersion( version ),
    fVersionData( versionData ),
    fCache( cache )
{
}

static ptrdiff_t
LengthWithDecayedArrayStart( const char* name )
{
	const char * lastBracket = strrchr( name, '[' );
	
	if ( ( NULL != lastBracket ) && ( 0 == strcmp( lastBracket, "[0]" ) ) )
	{
		return lastBracket - name;
	}
	else
	{
		return -1;
	}
}

static int
CompareUniformNames( const std::string& existing, const char* name, ptrdiff_t decayedLen )
{
	if ( decayedLen > 0 ) // name at least one character
	{
		return existing.compare( 0, (size_t)decayedLen, name ); 
	}
	else
	{
		return existing.compare( name );
	}
}

GLint
GLExtraUniforms::Find( const char * name, GLint & size, GLenum & type )
{
    if (!fCache)
    {
        Rtt_LogException( "Extra uniforms cache not yet initialized" );
        
        return -1;
    }
    
    // Has this name ever been found?
    int entryIndex = -1;
    
    if (*fCache)
    {
		ptrdiff_t decayedLen = LengthWithDecayedArrayStart( name );
    
        for (size_t i = 0; i < (*fCache)->fInfo.size(); ++i)
        {
            const auto & pos = (*fCache)->fInfo[i];
			if ( 0 == CompareUniformNames( pos.fName, name, decayedLen ) )
            {
                entryIndex = (int)i;
                
                if (pos.fLocations[fVersion] >= 0) // version as well?
                {
                    size = pos.size;
                    type = pos.type;
                    
                    return pos.fLocations[fVersion];
                }
                
                break;
            }
        }
    }

    // Does the uniform even exist?
    const GLProgram::VersionData & versionData = fVersionData[fVersion];
    GLint location = glGetUniformLocation( versionData.fProgram, reinterpret_cast< const GLchar * >( name ) );

    if (-1 == location)
    {
        Rtt_LogException( "WARNING: uniform `%s` not found in effect", name );
        
        return -1;
    }
    
    // No entry yet?
    if (-1 == entryIndex)
    {
        // Not a built-in?
        if ('u' == name[0] && '_' == name[1])
        {
            for (int i = 0; i < Uniform::kNumBuiltInVariables; ++i)
            {
                if (versionData.fUniformLocations[i] == location)
                {
                    Rtt_LogException( "WARNING: `%s` is a built-in uniform", name );
                    
                    return -1;
                }
            }
        }
        
        // Gather details.
        GLint count, uniformIndex;
        glGetProgramiv( versionData.fProgram, GL_ACTIVE_UNIFORMS, &count );
        
        GLchar nameBuf[GLProgram::kUniformNameBufferSize];
        GLsizei length;
        
        for ( uniformIndex = 0; uniformIndex < count; ++uniformIndex )
        {
            glGetActiveUniform( versionData.fProgram, (GLuint)uniformIndex, GLProgram::kUniformNameBufferSize - 1, &length, &size, &type, nameBuf );

			if ( 0 != ClassifySampler( type ) || ( 0x92DB == type ) /* GL_UNSIGNED_INT_ATOMIC_COUNTER */ )
			{
				continue;
			}

            ptrdiff_t decayedLen = LengthWithDecayedArrayStart( nameBuf ); // canonicalize if array ending in "[0]"
            if ( decayedLen > 0 )
            {
                length = (GLsizei)decayedLen;
            }

            if (0 == strncmp( name, nameBuf, length ))
            {
                break;
            }
        }
        
        if (uniformIndex == count)
        {
            Rtt_LogException( "Location of uniform `%s` found, but no active info: name too long?", name );
            
            return -1;
        }
        
        switch (type)
        {
        case GL_FLOAT:
        case GL_FLOAT_VEC2:
        case GL_FLOAT_VEC3:
        case GL_FLOAT_VEC4:
        case GL_FLOAT_MAT2:
        case GL_FLOAT_MAT3:
        case GL_FLOAT_MAT4:
            break;
#if 0
	// For possible expansion:
	// If these turn up, the shader presumably linked and the driver supports them...
	// so need to look up the symbol and add commands

	#define GL_DOUBLE                         0x140A
	#define GL_DOUBLE_VEC2                    0x8FFC
	#define GL_DOUBLE_VEC3                    0x8FFD
	#define GL_DOUBLE_VEC4                    0x8FFE

	#define GL_INT                            0x1404
	#define GL_INT_VEC2                       0x8B53
	#define GL_INT_VEC3                       0x8B54
	#define GL_INT_VEC4                       0x8B55

	#define GL_UNSIGNED_INT                   0x1405
	#define GL_UNSIGNED_INT_VEC2              0x8DC6
	#define GL_UNSIGNED_INT_VEC3              0x8DC7
	#define GL_UNSIGNED_INT_VEC4              0x8DC8

	#define GL_INT64_ARB                      0x140E
	#define GL_INT64_VEC2_ARB                 0x8FE9
	#define GL_INT64_VEC3_ARB                 0x8FEA
	#define GL_INT64_VEC4_ARB                 0x8FEB

	#define GL_UNSIGNED_INT64_ARB             0x140F
	#define GL_UNSIGNED_INT64_VEC2_ARB        0x8FE5
	#define GL_UNSIGNED_INT64_VEC3_ARB        0x8FE6
	#define GL_UNSIGNED_INT64_VEC4_ARB        0x8FE7

	#define GL_BOOL                           0x8B56
	#define GL_BOOL_VEC2                      0x8B57
	#define GL_BOOL_VEC3                      0x8B58
	#define GL_BOOL_VEC4                      0x8B59

	#define GL_FLOAT_MAT2x3                   0x8B65
	#define GL_FLOAT_MAT2x4                   0x8B66
	#define GL_FLOAT_MAT3x2                   0x8B67
	#define GL_FLOAT_MAT3x4                   0x8B68
	#define GL_FLOAT_MAT4x2                   0x8B69
	#define GL_FLOAT_MAT4x3                   0x8B6A

	#define GL_DOUBLE_MAT2                    0x8F46
	#define GL_DOUBLE_MAT3                    0x8F47
	#define GL_DOUBLE_MAT4                    0x8F48
	#define GL_DOUBLE_MAT2x3                  0x8F49
	#define GL_DOUBLE_MAT2x4                  0x8F4A
	#define GL_DOUBLE_MAT3x2                  0x8F4B
	#define GL_DOUBLE_MAT3x4                  0x8F4C
	#define GL_DOUBLE_MAT4x2                  0x8F4D
	#define GL_DOUBLE_MAT4x3                  0x8F4E

	#define GL_FLOAT16_NV                     0x8FF8
	#define GL_FLOAT16_VEC2_NV                0x8FF9
	#define GL_FLOAT16_VEC3_NV                0x8FFA
	#define GL_FLOAT16_VEC4_NV                0x8FFB

	#define GL_FLOAT16_MAT2_AMD               0x91C5
	#define GL_FLOAT16_MAT3_AMD               0x91C6
	#define GL_FLOAT16_MAT4_AMD               0x91C7
	#define GL_FLOAT16_MAT2x3_AMD             0x91C8
	#define GL_FLOAT16_MAT2x4_AMD             0x91C9
	#define GL_FLOAT16_MAT3x2_AMD             0x91CA
	#define GL_FLOAT16_MAT3x4_AMD             0x91CB
	#define GL_FLOAT16_MAT4x2_AMD             0x91CC
	#define GL_FLOAT16_MAT4x3_AMD             0x91CD
#endif
        default:
            Rtt_LogException( "Location of uniform `%s` found, but type unsupported", name );
                
            return -1;
        }
          
        // No cache yet?
        if (!*fCache)
        {
            *fCache = Rtt_NEW( NULL, GLProgramUniformsCache );
        }
    
        // Install the details.
        entryIndex = (int)(*fCache)->fInfo.size();
        
        (*fCache)->fInfo.push_back( GLProgramUniformInfo{} );
        
        GLProgramUniformInfo & newInfo = (*fCache)->fInfo.back();
        
        newInfo.size = size;
        newInfo.type = type;
        newInfo.fName = name;
    }
    
    else
    {
        size = (*fCache)->fInfo[entryIndex].size;
        type = (*fCache)->fInfo[entryIndex].type;
    }
    
    // Register the location and return it.
    (*fCache)->fInfo[entryIndex].fLocations[fVersion] = location;
    
    return location;
}

void
GLProgram::GetExtraUniformsInfo( Program::Version version, GLExtraUniforms& extraUniforms )
{
    extraUniforms = GLExtraUniforms( version, fData, &fUniformsCache );
}

#undef ARRAY_AND_N

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
