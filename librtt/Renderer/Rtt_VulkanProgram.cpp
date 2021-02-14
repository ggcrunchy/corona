//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanRenderer.h"
#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanProgram.h"

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_Texture.h"
#ifdef Rtt_USE_PRECOMPILED_SHADERS
	#include "Renderer/Rtt_ShaderBinary.h"
	#include "Renderer/Rtt_ShaderBinaryVersions.h"
#endif
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

#include <shaderc/shaderc.h>
#include <algorithm>
#include <string>
#include <utility>
#include <stdlib.h>

// To reduce memory consumption and startup cost, defer the
// creation of Vulkan shaders and programs until they're needed.
// Depending on usage, this could result in framerate dips.
#define DEFER_VK_CREATION 1

// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
	using namespace Rtt;

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

VulkanProgram::VulkanProgram( VulkanState * state )
:	fState( state ),
	fFragmentConstants( false ),
	fPushConstantUniforms( false )
{
	for( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		Reset( fData[i] );
	}
}

void 
VulkanProgram::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	fResource = resource;
	
	#if !DEFER_VK_CREATION
		for( U32 i = 0; i < kMaximumMaskCount + 1; ++i )
		{
			Create( fData[i], i );
		}
	#endif
}

void
VulkanProgram::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kProgram == resource->GetType() );
	if( fData[Program::kMaskCount0].IsValid() ) Update( Program::kMaskCount0, fData[Program::kMaskCount0] );
	if( fData[Program::kMaskCount1].IsValid() ) Update( Program::kMaskCount1, fData[Program::kMaskCount1] );
	if( fData[Program::kMaskCount2].IsValid() ) Update( Program::kMaskCount2, fData[Program::kMaskCount2] );
	if( fData[Program::kMaskCount3].IsValid() ) Update( Program::kMaskCount3, fData[Program::kMaskCount3] );
	if( fData[Program::kWireframe].IsValid() ) Update( Program::kWireframe, fData[Program::kWireframe]);
}

void 
VulkanProgram::Destroy()
{
	auto ci = fState->GetCommonInfo();

	for( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		VersionData& data = fData[i];
		if( data.IsValid() )
		{
#ifndef Rtt_USE_PRECOMPILED_SHADERS
			vkDestroyShaderModule( ci.device, data.fVertexShader, ci.allocator );
			vkDestroyShaderModule( ci.device, data.fFragmentShader, ci.allocator );
#endif
			Reset( data );
		}
	}
}

void
VulkanProgram::Bind( VulkanRenderer & renderer, Program::Version version )
{
	VersionData& data = fData[version];
	
	#if DEFER_VK_CREATION
		if( !data.IsValid() )
		{
			Create( version, data );
		}
	#endif

	std::vector< VkVertexInputAttributeDescription > inputAttributeDescriptions;

	VkVertexInputAttributeDescription description = {};

	description.location = Geometry::kVertexPositionAttribute;
	description.format = VK_FORMAT_R32G32B32_SFLOAT;
	description.offset = offsetof( Geometry::Vertex, x );

	inputAttributeDescriptions.push_back( description );

	description.location = Geometry::kVertexTexCoordAttribute;
	description.offset = offsetof( Geometry::Vertex, u );

	inputAttributeDescriptions.push_back( description );

	description.location = Geometry::kVertexColorScaleAttribute;
	description.format = VK_FORMAT_R8G8B8A8_UNORM;
	description.offset = offsetof( Geometry::Vertex, rs );

	inputAttributeDescriptions.push_back( description );

	description.location = Geometry::kVertexUserDataAttribute;
	description.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	description.offset = offsetof( Geometry::Vertex, ux );

	inputAttributeDescriptions.push_back( description );
	
	U32 inputAttributesID = 0U; // TODO: for future use

	renderer.SetAttributeDescriptions( inputAttributesID, inputAttributeDescriptions );

	std::vector< VkPipelineShaderStageCreateInfo > stages;

	VkPipelineShaderStageCreateInfo shaderStageInfo = {};

	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.pName = "main";

	shaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStageInfo.module = data.fVertexShader;

	stages.push_back( shaderStageInfo );

	shaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStageInfo.module = data.fFragmentShader;

	stages.push_back( shaderStageInfo );

	renderer.SetShaderStages( data.fShadersID, stages );
}

U32 VulkanProgram::sID;

void
VulkanProgram::Create( Program::Version version, VersionData& data )
{
	Update( version, data );

	if (data.IsValid())
	{
		data.fShadersID = sID++;
	}
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

static std::string 
CombineSources( const char * sources[], int sourceCount )
{
	std::string code;

	for (int i = 0; i < sourceCount; ++i)
	{
		code += sources[i];
	}

	return code;
}

void
VulkanProgram::GatherUniformUserdata( bool isVertexSource, std::string & code, UserdataValue values[], std::vector< UserdataDeclaration > & declarations, bool & canUsePushConstants )
{
	size_t offset = code.find( "void main(" ); // skip past declarations in shell; this particular landmark is arbitrary
// TODO: we must ignore anything in comments
	while (true)
	{
		size_t pos = code.find( "uniform ", offset );

		if (std::string::npos == pos)
		{
			return;
		}

		size_t cpos = code.find( ";", pos + sizeof( "uniform " ) );

		if (std::string::npos == cpos)	// sanity check: must have semi-colon eventually...
		{
			CoronaLog( "`uniform` never followed by a semi-colon" );

			return;
		}

		char precision[16], type[16];
		int index;

		if (sscanf( code.c_str() + pos, "uniform %15s %15s u_UserData%1i", precision, type, &index ) < 3)
		{
			CoronaLog( "Uniform in shader was ill-formed" );

			return;
		}

		if (index < 0 || index > 3)
		{
			CoronaLog( "Invalid uniform userdata `%i`", index );

			return;
		}

		U32 componentCount = 1U;

		if (strcmp( type, "float" ) != 0)
		{
			switch (Uniform::DataTypeForString( type ))
			{
				case Uniform::kVec2:
					componentCount = 2U; break;
				case Uniform::kVec3:
					componentCount = 3U; break;
				case Uniform::kVec4:
					componentCount = 4U; break;
				case Uniform::kMat4:
					componentCount = 16U; break;
				case Uniform::kMat3:
					canUsePushConstants = false; // contract broken
					break;
				// TODO: allow mat2?
				default:
					CoronaLog( "Ill-formed uniform type" );

					return;
			}
		}

		VkShaderStageFlagBits stage = isVertexSource ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;

		if (0 == values[index].fStages)
		{
			values[index].fComponentCount = componentCount;
		}
	
		else if (values[index].fStages & stage)
		{
			CoronaLog( "Uniform userdata `%i` already defined in %s stage", index, isVertexSource ? "vertex" : "kernel" );

			return;
		}

		else if (values[index].fComponentCount != componentCount)
		{
			Rtt_ASSERT( !isVertexSource );

			CoronaLog( "Uniform userdata `%i` definition differs between vertex and fragment stages", index );

			return;
		}

		values[index].fStages |= stage;

		UserdataDeclaration declaration;

		declaration.fValue = &values[index];
		declaration.fPosition = pos;
		declaration.fLength = cpos - pos + 1; // include semi-colon as well

		declarations.push_back( declaration );

		offset = pos + 1U;
	}
}

void
VulkanProgram::ReplaceVaryings( bool isVertexSource, std::string & code, Maps & maps )
{
	size_t offset = 0U, varyingLocation = 0U;

	while (true)
	{
		size_t pos = code.find( "varying ", offset );

		if (std::string::npos == pos)
		{
			return;
		}

		char precision[16], type[16], name[64];

		if (sscanf( code.c_str() + pos, "varying %15s %15s %63s", precision, type, name ) < 3)
		{
			CoronaLog( "Varying in shader was ill-formed" );

			return;
		}

		// TODO: validate: known precision, known type, identifier
		// also, if we don't care about mobile we could forgo the type...

		size_t location = varyingLocation;

		if (!isVertexSource)
		{
			auto & varying = maps.varyings.find( name );

			if (maps.varyings.end() == varying)
			{
				CoronaLog( "Fragment kernel refers to varying `%s`, not found on vertex side", name );

				return;
			}

			location = varying->second;
		}

		char buf[64];

		sprintf( buf, "layout(location = %u) %s ", location, isVertexSource ? "out" : "in" );

		code.replace( pos, sizeof( "varying" ), buf );

		if (isVertexSource)
		{
			maps.varyings[name] = varyingLocation++;
		}

		offset = pos + 1U;
	}
}

void
VulkanProgram::Compile( int ikind, std::string & code, Maps & maps, VkShaderModule & module )
{
	shaderc_shader_kind kind = shaderc_shader_kind( ikind );
	const char * what = shaderc_vertex_shader == kind ? "vertex shader" : "fragment shader";
	bool isVertexSource = 'v' == what[0];

	ReplaceVaryings( isVertexSource, code, maps );

	shaderc_compilation_result_t result = shaderc_compile_into_spv( fState->GetCompiler(), code.data(), code.size(), kind, what, "main", fState->GetCompileOptions() );
	shaderc_compilation_status status = shaderc_result_get_compilation_status( result );

	if (shaderc_compilation_status_success == status)
	{
		const uint32_t * ir = reinterpret_cast< const uint32_t * >( shaderc_result_get_bytes( result ) );
		VkShaderModuleCreateInfo createShaderModuleInfo = {};

		createShaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createShaderModuleInfo.codeSize = shaderc_result_get_length( result );
		createShaderModuleInfo.pCode = ir;

		spirv_cross::CompilerGLSL comp( ir, createShaderModuleInfo.codeSize / 4U );
		spirv_cross::ShaderResources resources = comp.get_shader_resources();

		auto uniformBuffers = resources.uniform_buffers;

		for (auto & buffer : uniformBuffers)
		{
			auto ranges = comp.get_active_buffer_ranges( buffer.id );

			for (auto & br : ranges)
			{
				std::string name = comp.get_member_name( buffer.base_type_id, br.index );
				auto iter = maps.buffer_values.insert( std::make_pair( name, Maps::BufferValue( br.offset, br.range, true ) ) );

				iter.first->second.fStages |= 1U << kind;
				// TODO: ensure no clashes when found in both
			}
		}
		
		for (auto & buffer : resources.push_constant_buffers) // 0 or 1
		{
			auto ranges = comp.get_active_buffer_ranges( buffer.id );

			for (auto & br : ranges)
			{
				std::string name = comp.get_member_name( buffer.base_type_id, br.index );

				maps.buffer_values.insert( std::make_pair( name, Maps::BufferValue( br.offset, br.range, false ) ) );
			}
		}

		for (auto & sampler : resources.sampled_images)
		{
			const spirv_cross::SPIRType & type = comp.get_type_from_variable( sampler.id );
			std::vector< U32 > counts;

			for (uint32_t i = 0; i < type.array.size(); ++i)
			{
				counts.push_back( type.array[i] );
			}

			auto iter = maps.samplers.insert( std::make_pair( sampler.name, Maps::SamplerValue( type.image, counts ) ) );

			iter.first->second.fStages |= 1U << kind;
		}

		if (vkCreateShaderModule( fState->GetDevice(), &createShaderModuleInfo, fState->GetAllocator(), &module ) != VK_SUCCESS)
		{
			CoronaLog( "Failed to create shader module!" );
		}
	}

	else
	{
		CoronaLog( "Failed to compile %s:\n\n%s", what, shaderc_result_get_error_message( result ) );
	}

	shaderc_result_release( result );
}

static bool
AreRowsOpen( int used[], int row, int nrows, int ncols )
{
	for (int i = 0; i < nrows; ++i)
	{
		int count = used[row++];

		if (count + ncols > 4)
		{
			return false;
		}
	}

	return true;
}

static int
GetFirstRow( int used[], int nrows, int ncols )
{
	for (int row = 0, last = 11 - nrows; row <= last; ++row)
	{
		if (AreRowsOpen( used, row, nrows, ncols ))
		{
			return row;
		}
	}

	return -1;
}

VulkanProgram::Maps
VulkanProgram::UpdateShaderSource( Program* program, Program::Version version, VersionData& data )
{
	Maps maps;

#ifndef Rtt_USE_PRECOMPILED_SHADERS
	char maskBuffer[] = "#define MASK_COUNT 0\n";
	switch( version )
	{
		case Program::kMaskCount1:	maskBuffer[sizeof( maskBuffer ) - 3] = '1'; break;
		case Program::kMaskCount2:	maskBuffer[sizeof( maskBuffer ) - 3] = '2'; break;
		case Program::kMaskCount3:	maskBuffer[sizeof( maskBuffer ) - 3] = '3'; break;
		default: break;
	}

	const char *program_header_source = program->GetHeaderSource();
	const char *header = ( program_header_source ? program_header_source : "" );

	const char* shader_source[4];
	memset( shader_source, 0, sizeof( shader_source ) );
	shader_source[0] = header;
	shader_source[1] = "#define FRAGMENT_SHADER_SUPPORTS_HIGHP 1\n"; // TODO? this seems a safe assumption on Vulkan...
	shader_source[2] = maskBuffer;

	if ( program->IsCompilerVerbose() )
	{
		// All the segments except the last one
		int numSegments = sizeof( shader_source ) / sizeof( shader_source[0] ) - 1;
		data.fHeaderNumLines = CountLines( shader_source, numSegments );
	}

// TODO: if we have uniform userdata it MIGHT be possible to place them in the push constants;
// the prequisites may be violated in either the vertex or fragment stages, so we should avoid
// compiling them until both have been examined; it should actually be possible to just handle
// this part first, then do what follows here.

	std::string vertexCode, fragmentCode;
	std::vector< UserdataDeclaration > vertexDeclarations, fragmentDeclarations;

	UserdataValue values[4];
	bool canUsePushConstants = true;

	// Vertex shader.
	{
		shader_source[3] = program->GetVertexShaderSource();
		vertexCode = CombineSources( shader_source, sizeof( shader_source ) / sizeof( shader_source[0] ) );

		GatherUniformUserdata( true, vertexCode, values, vertexDeclarations, canUsePushConstants );
	}

	// Fragment shader.
	{
		shader_source[3] = ( version == Program::kWireframe ) ? kWireframeSource : program->GetFragmentShaderSource();
		fragmentCode = CombineSources( shader_source, sizeof( shader_source ) / sizeof( shader_source[0] ) );

		GatherUniformUserdata( false, fragmentCode, values, fragmentDeclarations, canUsePushConstants );
	}

	U32 vertexExtra = 0U, fragmentExtra = 0U;

	if (!vertexDeclarations.empty() || !fragmentDeclarations.empty())
	{
		// TODO: still valid?

		for (int i = 0; i < 4; ++i)
		{
			values[i].fIndex = i;
		}

		std::sort( values, values + 4 );

		UserdataPosition positions[4];

		for (int i = 0; i < 4; ++i)
		{
			positions[i].fValue = &values[i];
		}

		int used[11] = {}; // 11 vectors, cf. shell_default_vulkan.lua

		for (int i = 0; i < 4 && values[i].IsValid(); ++i)
		{
			int nrows = 1, ncols = values[i].fComponentCount;

			if (16 == values[i].fComponentCount)
			{
				ncols = nrows = 4;
			}

			int row = GetFirstRow( used, nrows, ncols );

			if (row >= 0)
			{
				positions[i].fRow = row;
				positions[i].fOffset = used[row];

				for (int j = 0; j < nrows; ++j)
				{
					used[row + j] += ncols;
				}
			}

			else
			{
				canUsePushConstants = false;

				break;
			}
		}

		std::sort( positions, positions + 4 );

		if (canUsePushConstants)
		{
			std::string extra;
			U32 paddingCount = 0U, lastComponentCount;

			for (int i = 0; i < 4 && positions[i].IsValid(); ++i)
			{
				extra += " ";

				if (i > 0 && 0U == positions[i].fOffset)
				{
					U32 lastUpTo = positions[i - 1].fOffset + lastComponentCount;

					if (lastUpTo < 4U) // incomplete vector?
					{
						const char * padding[] = { "vec3 pad", "vec2 pad" "float pad" };

						extra += padding[lastUpTo - 1];
						extra += '0' + paddingCount;
						extra += ";  ";

						++paddingCount;
					}
				}

				U32 componentCount = positions[i].fValue->fComponentCount;

				switch (componentCount)
				{
				case 16:
					extra += "mat4 ";
					break;
				case 4:
					extra += "vec4 ";
					break;
				case 3:
					extra += "vec3 ";
					break;
				case 2:
					extra += "vec2 ";
					break;
				case 1:
					extra += "float ";
					break;
				default:
					Rtt_ASSERT_NOT_REACHED();
				}

				extra += "UserData";
				extra += '0' + positions[i].fValue->fIndex;
				extra += ";";

				lastComponentCount = componentCount;
			}

			if (!vertexDeclarations.empty())
			{
				size_t epos = vertexCode.find( "PUSH_CONSTANTS_EXTRA" );

				if (std::string::npos == epos)
				{
					// error!
				}

				vertexCode.insert( epos + sizeof( "PUSH_CONSTANTS_EXTRA" ) - 1, extra );

				vertexExtra = extra.size();
			}

			if (!fragmentDeclarations.empty())
			{
				size_t epos = fragmentCode.find( "PUSH_CONSTANTS_EXTRA" );

				if (std::string::npos == epos)
				{
					// error!
				}

				fragmentCode.insert( epos + sizeof( "PUSH_CONSTANTS_EXTRA" ) - 1, extra );

				fragmentExtra = extra.size();

				fFragmentConstants = true;
			}
		}

		else
		{
			// do something similar with userDataObject
		}

		std::string prefix1( "#define u_UserData" ), prefix2( " pc.UserData" );

		for (auto && iter = vertexDeclarations.rbegin(); iter != vertexDeclarations.rend(); ++iter)
		{
			const char suffix[] = { '0' + iter->fValue->fIndex, 0 };

			vertexCode.replace( iter->fPosition + vertexExtra, iter->fLength, (prefix1 + suffix) + (prefix2 + suffix) );
		}
if (!vertexDeclarations.empty()) CoronaLog( "Vertex code: %s\n\n", vertexCode.c_str() );
		for (auto && iter = fragmentDeclarations.rbegin(); iter != fragmentDeclarations.rend(); ++iter)
		{
			const char suffix[] = { '0' + iter->fValue->fIndex, 0 };

			fragmentCode.replace( iter->fPosition + fragmentExtra, iter->fLength, (prefix1 + suffix) + (prefix2 + suffix) );
		}
if (!fragmentDeclarations.empty()) CoronaLog( "Fragment code: %s\n\n", fragmentCode.c_str() );
		fPushConstantUniforms = true;
	}

	// Vertex shader.
	Compile( shaderc_vertex_shader, vertexCode, maps, data.fVertexShader );

	// Fragment shader.
	Compile( shaderc_fragment_shader, fragmentCode, maps, data.fFragmentShader );

#else
	// no need to compile, but reflection here...
#endif

	return maps;
}

U32
VulkanProgram::Maps::CheckForSampler( const std::string & key /* TODO: info... */ )
{
	auto iter = samplers.find( key );

	if (iter != samplers.end())
	{
		auto imageType = iter->second;

		return 0U;	// TODO: maybe just want binding decorator, from above?
					// imageType would be handy for later expansion...
					// TODO 2: might need array index, etc. now
	}

	else
	{
		return ~0U;
	}
}

VulkanProgram::Location
VulkanProgram::Maps::CheckForUniform( const std::string & key )
{
	auto iter = buffer_values.find( key );

	if (iter != buffer_values.end())
	{
		return iter->second.fLocation;
	}

	else
	{
		return Location{};
	}
}

void
VulkanProgram::Update( Program::Version version, VersionData& data )
{
	Program* program = static_cast<Program*>( fResource );

	Maps maps = UpdateShaderSource( program,
									version,
									data );

#ifdef Rtt_USE_PRECOMPILED_SHADERS // TODO! (can probably just load spv?)
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

	// TODO!

	if ( isVerbose )
	{
		kernelStartLine = data.fHeaderNumLines + program->GetVertexShellNumLines();
	}

	if ( isVerbose )
	{
		kernelStartLine = data.fHeaderNumLines + program->GetFragmentShellNumLines();
	}
#endif

	data.fUniformLocations[Uniform::kViewProjectionMatrix] = maps.CheckForUniform( "ViewProjectionMatrix" );
	data.fUniformLocations[Uniform::kMaskMatrix0] = maps.CheckForUniform( "MaskMatrix0" );
	data.fUniformLocations[Uniform::kMaskMatrix1] = maps.CheckForUniform( "MaskMatrix1" );
	data.fUniformLocations[Uniform::kMaskMatrix2] = maps.CheckForUniform( "MaskMatrix2" );
	data.fUniformLocations[Uniform::kTotalTime] = maps.CheckForUniform( "TotalTime" );
	data.fUniformLocations[Uniform::kDeltaTime] = maps.CheckForUniform( "DeltaTime" );
	data.fUniformLocations[Uniform::kTexelSize] = maps.CheckForUniform( "TexelSize" );
	data.fUniformLocations[Uniform::kContentScale] = maps.CheckForUniform( "ContentScale" );
	data.fUniformLocations[Uniform::kUserData0] = maps.CheckForUniform( "UserData0" );
	data.fUniformLocations[Uniform::kUserData1] = maps.CheckForUniform( "UserData1" );
	data.fUniformLocations[Uniform::kUserData2] = maps.CheckForUniform( "UserData2" );
	data.fUniformLocations[Uniform::kUserData3] = maps.CheckForUniform( "UserData3" );

	data.fMaskTranslationLocations[0] = maps.CheckForUniform( "MaskTranslation0" );
	data.fMaskTranslationLocations[1] = maps.CheckForUniform( "MaskTranslation1" );
	data.fMaskTranslationLocations[2] = maps.CheckForUniform( "MaskTranslation2" );

/*
	glUniform1i( glGetUniformLocation( data.fProgram, "u_FillSampler0" ), Texture::kFill0 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_FillSampler1" ), Texture::kFill1 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler0" ), Texture::kMask0 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler1" ), Texture::kMask1 );
	glUniform1i( glGetUniformLocation( data.fProgram, "u_MaskSampler2" ), Texture::kMask2 );
*/
}

void
VulkanProgram::Reset( VersionData& data )
{
	data.fVertexShader = VK_NULL_HANDLE;
	data.fFragmentShader = VK_NULL_HANDLE;

	const uint32_t kInactiveLocation = ~0U;

	for( U32 i = 0; i < Uniform::kNumBuiltInVariables; ++i )
	{
		data.fUniformLocations[ i ] = kInactiveLocation;

		// CommandBuffer also initializes timestamp to zero
		const U32 kTimestamp = 0;
		data.fTimestamps[ i ] = kTimestamp;
	}

	for ( U32 i = 0; i < 3; ++i )
	{
		data.fMaskTranslationLocations[ i ] = kInactiveLocation;
	}
	
	data.fHeaderNumLines = 0;
}

void
VulkanProgram::InitializeCompiler( shaderc_compiler_t * compiler, shaderc_compile_options_t * options )
{
	if (compiler && options)
	{
		*compiler = shaderc_compiler_initialize();
		*options = shaderc_compile_options_initialize();

// void shaderc_compile_options_set_generate_debug_info(shaderc_compile_options_t options)
// void shaderc_compile_options_set_optimization_level(shaderc_compile_options_t options, shaderc_optimization_level level)
// void shaderc_compile_options_set_invert_y(shaderc_compile_options_t options, bool enable)
	}
}

void
VulkanProgram::CleanUpCompiler( shaderc_compiler_t compiler, shaderc_compile_options_t options )
{
	shaderc_compiler_release( compiler );
	shaderc_compile_options_release( options );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------