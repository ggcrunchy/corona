//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanRenderer.h"
#include "Renderer/Rtt_VulkanContext.h"
#include "Renderer/Rtt_VulkanProgram.h"

#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_ShaderCode.h"
#include "Renderer/Rtt_Texture.h"
#ifdef Rtt_USE_PRECOMPILED_SHADERS
	#include "Renderer/Rtt_ShaderBinary.h"
	#include "Renderer/Rtt_ShaderBinaryVersions.h"
#endif
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

#include <shaderc/shaderc.h>
#include <algorithm>
#include <utility>
#include <stdarg.h>
#include <stdlib.h>

#ifdef free
#undef free
#endif

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <spirv_cross/spirv_glsl.hpp>

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

struct VulkanCompilerMaps {
	struct BufferValue {
		BufferValue( size_t offset, size_t range, bool isUniform )
		:	fLocation( offset, range, isUniform ),
			fStages( 0U )
		{
		}

		VulkanProgram::Location fLocation;
		U32 fStages;
	};

	struct SamplerValue {
		SamplerValue( const spirv_cross::SPIRType::ImageType & details, std::vector< U32 > & counts )
		:	fDetails( details ),
			fStages( 0U )
		{
			fCounts.swap( counts );
		}

		spirv_cross::SPIRType::ImageType fDetails;
		std::vector< U32 > fCounts;
		U32 fStages;
	};

	std::map< std::string, BufferValue > buffer_values;
	std::map< std::string, SamplerValue > samplers;
	std::map< std::string, int > varyings;

	U32 CheckForSampler( const std::string & key /* TODO: info... */ );
	VulkanProgram::Location CheckForUniform( const std::string & key );
};

U32
VulkanCompilerMaps::CheckForSampler( const std::string & key /* TODO: info... */ )
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
VulkanCompilerMaps::CheckForUniform( const std::string & key )
{
	auto iter = buffer_values.find( key );

	if (iter != buffer_values.end())
	{
		return iter->second.fLocation;
	}

	else
	{
		return VulkanProgram::Location{};
	}
}

// ----------------------------------------------------------------------------

VulkanProgram::VulkanProgram( VulkanContext * context )
:	fContext( context ),
	fPushConstantStages( 0U ),
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
	auto ci = fContext->GetCommonInfo();

	for( U32 i = 0; i < Program::kNumVersions; ++i )
	{
		VersionData& data = fData[i];

	#ifndef Rtt_USE_PRECOMPILED_SHADERS
		vkDestroyShaderModule( ci.device, data.fVertexShader, ci.allocator );
		vkDestroyShaderModule( ci.device, data.fFragmentShader, ci.allocator );
	#endif

		Reset( data );
	}
}

void
VulkanProgram::Bind( VulkanRenderer & renderer, Program::Version version )
{
	VersionData& data = fData[version];
	
	#if DEFER_VK_CREATION
		if( !data.fAttemptedCreation )
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

	else
	{
		data.fShadersID = kInvalidID;
	}
}

void
VulkanProgram::CompileState::Report( const char * prefix )
{
	if (HasError())
	{
		CORONA_LOG_ERROR( "Failed to compile %s:\n\n%s", prefix, fError.c_str() );
	}
}

void
VulkanProgram::CompileState::SetError( const char * fmt, ... )
{
	char buf[4096];

	va_list argp;

	va_start( argp, fmt );

	vsprintf( buf, fmt, argp );

	va_end( argp );

	fError += buf;
}

bool
VulkanProgram::CompileState::HasError() const
{
	return !fError.empty();
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

static bool
NoLeadingCharacters( const std::string & code, size_t pos )
{
	return !isalnum( code[pos - 1] ) && code[pos - 1] != '_'; // TODO: code can be UTF8?
}

size_t
VulkanProgram::GatherUniformUserdata( bool isVertexSource, ShaderCode & code, UserdataValue values[], std::vector< UserdataDeclaration > & declarations, CompileState & state )
{
	size_t offset = code.Find( "void main(", 0U ); // skip past declarations in shell; this particular landmark is arbitrary
	size_t result = offset;

	while (true)
	{
		size_t pos = code.Find( "uniform ", offset );

		if (std::string::npos == pos)
		{
			return result;
		}

		if (NoLeadingCharacters( code.GetString(), pos ))
		{
			size_t pastUniformPos = pos + strlen( "uniform " );
			size_t cpos = code.Find( ";", pastUniformPos );

			if (std::string::npos == cpos)	// sanity check: must have semi-colon eventually...
			{
				state.SetError( "`uniform` never followed by a semi-colon" );

				return result;
			}

			pastUniformPos = code.Skip( pastUniformPos, isalpha );

			if (std::string::npos == pastUniformPos || pastUniformPos > cpos)
			{
				state.SetError( "Missing type (and optional precision) after `uniform`" );

				return result;
			}

			char precision[16];

			if (sscanf( code.GetCStr() + pastUniformPos, "P_%15s ", precision ) == 1)
			{
				const char * options[] = { "COLOR", "DEFAULT", "NORMAL", "POSITION", "RANDOM", "UV", NULL }, * choice = NULL;

				for (int i = 0; options[i] && !choice; ++i)
				{
					if (strcmp( precision, options[i] ) == 0)
					{
						choice = options[i];
					}
				}

				if (choice)
				{
					pastUniformPos += strlen( "P_" ) + strlen( choice ) + 1;
				}

				else
				{
					state.SetError( "Invalid precision" );

					return result;
				}
			}

			char punct = '\0';

			do {
				U32 componentCount = 1U;
				char type[16];

				if ('\0' == punct)
				{
					pastUniformPos = code.Skip( pastUniformPos, isalpha );

					if (std::string::npos == pastUniformPos || pastUniformPos > cpos)
					{
						state.SetError( "Missing type after `uniform`" );

						return result;
					}

					if (sscanf( code.GetCStr() + pastUniformPos, "%15s ", type ) == 0)
					{
						state.SetError( "Uniform type in shader was ill-formed" );

						return result;
					}

					else if (strcmp( type, "float" ) != 0)
					{
						Uniform::DataType dataType = Uniform::DataTypeForString( type );

						if (Uniform::kScalar == dataType)
						{
							state.SetError( "Invalid uniform type" );

							return result;
						}

						else
						{
							componentCount = Uniform( dataType ).GetNumValues();
						}
					}

					pastUniformPos += strlen( type ) + 1;
				}

				pastUniformPos = code.Skip( pastUniformPos, isalpha );

				if (std::string::npos == pastUniformPos || pastUniformPos > cpos)
				{
					state.SetError( "Missing `u_UserData?` after `uniform`" );

					return result;
				}

				int index;

				if (sscanf( code.GetCStr() + pastUniformPos, "u_UserData%1i ", &index ) == 0)
				{
					state.SetError( "`u_UserData?` in shader was ill-formed" );

					return result;
				}

				if (index < 0 || index > 3)
				{
					state.SetError( "Invalid uniform userdata `%i`", index );

					return result;
				}

				pastUniformPos += strlen( "u_UserData?" );

				pastUniformPos = code.Skip( pastUniformPos, ispunct );

				Rtt_ASSERT( std::string::npos != pastUniformPos && pastUniformPos <= cpos );

				punct = code.GetCStr()[pastUniformPos++];

				if (punct != ',' && punct != ';')
				{
					state.SetError( "Expected ',' or ';' after declaration" );

					return result;
				}

				VkShaderStageFlagBits stage = isVertexSource ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;

				if (0 == values[index].fStages)
				{
					values[index].fComponentCount = componentCount;
				}
	
				else if (values[index].fStages & stage)
				{
					state.SetError( "Uniform userdata `%i` already defined in %s stage", index, isVertexSource ? "vertex" : "fragment" );

					return result;
				}

				else if (values[index].fComponentCount != componentCount)
				{
					Rtt_ASSERT( !isVertexSource );

					state.SetError( "Uniform userdata `%i` definition differs between vertex and fragment stages", index );

					return result;
				}

				values[index].fStages |= stage;

				UserdataDeclaration declaration = {};

				declaration.fValue = &values[index];
				declaration.fPosition = pos;

				if (';' == punct) // last on line?
				{
					declaration.fLength = cpos - pos + 1; // include semi-colon as well
				}

				declarations.push_back( declaration );
			} while (';' != punct);
		}

		offset = pos + 1U;
	}

	return result;
}

bool
VulkanProgram::ReplaceFragCoords( ShaderCode & code, size_t offset, CompileState & state )
{
	std::vector< size_t > posStack;

	while (true)
	{
		size_t pos = code.Find( "gl_FragCoord", offset );

		if (std::string::npos == pos)
		{
			break;
		}

		offset = pos + strlen( "gl_FragCoord" );

		if (NoLeadingCharacters( code.GetString(), pos ))
		{
			size_t cpos = code.Find( ";", offset );

			if (std::string::npos == cpos)	// sanity check: must have semi-colon eventually...
			{
				state.SetError( "`gl_FragCoord` never followed by a semi-colon" );

				return false;
			}

			else if ('_' != code.GetCStr()[offset] && !isalnum( code.GetCStr()[offset] )) // not part of a larger identifier
			{
				posStack.push_back( pos );
			}
		}
	}

	for (auto && iter = posStack.rbegin(); iter != posStack.rend(); ++iter)
	{
		code.Replace( *iter, strlen( "gl_FragCoord" ), "internal_FragCoord" );
	}

	return !posStack.empty();
}

void
VulkanProgram::ReplaceVertexSamplers( ShaderCode & code, CompileState & state )
{
	// In OpenGL 2.* we can declare samplers in the vertex kernel that get picked up as bindings 0, 1, etc.
	// Vulkan assumes a later GLSL that's more stringent about layout and allocation; we can salvage the old
	// behavior by making those declarations, in order, synonyms for the stock samplers.
	// TODO: figure out if this is well-defined behavior

	struct Sampler {
		char name[64];
		size_t count;
		size_t pos;
	};

	std::vector< Sampler > samplerStack;
	size_t offset = 0U;

	while (true)
	{
		Sampler s;

		s.pos = code.Find( "uniform ", offset );

		if (std::string::npos == s.pos)
		{
			break;
		}

		offset = s.pos + strlen( "uniform " );

		if (NoLeadingCharacters( code.GetString(), s.pos ))
		{
			size_t cpos = code.Find( ";", offset );

			if (std::string::npos == cpos)	// sanity check: must have semi-colon eventually...
			{
				state.SetError( "`uniform` never followed by a semi-colon" );

				return;
			}

			size_t spos = code.Find( "sampler2D", offset );

			if (std::string::npos == spos)	// none left to replace?
			{
				return;
			}

			else if (spos < cpos) // is this a sampler-type uniform?
			{
				size_t bpos = code.Find( "[", offset );

				if (std::string::npos == bpos || cpos < bpos) // not an array? (TODO? this rules out u_Samplers, but of course limits valid usage... )
				{
					if (sscanf( code.GetCStr() + s.pos, "uniform sampler2D %63s", s.name ) < 1)
					{
						state.SetError( "Sampler in shader was ill-formed" );

						return;
					}

					// TODO: see notes about varyings...

					size_t len = strlen( s.name );

					if (ispunct( s.name[len - 1] ))
					{
						s.name[len - 1] = '\0';
					}

					s.count = cpos - s.pos + 1;

					samplerStack.push_back(s);
				}
			}
		}
	}

	char buf[64];

	size_t index = samplerStack.size();

	for (auto && iter = samplerStack.rbegin(); iter != samplerStack.rend(); ++iter)
	{
		sprintf( buf, "#define %s u_FillSampler%i", iter->name, --index );

		code.Replace( iter->pos, iter->count, buf );
	}
}

void
VulkanProgram::ReplaceVaryings( bool isVertexSource, ShaderCode & code, VulkanCompilerMaps & maps, CompileState & state )
{
	struct Varying {
		size_t location;
		size_t pos;
	};

	std::vector< Varying > varyingStack;
	size_t offset = 0U, varyingLocation = 0U;

	while (true)
	{
		Varying v;

		v.pos = code.Find( "varying ", offset );

		if (std::string::npos == v.pos)
		{
			break;
		}

		if (NoLeadingCharacters( code.GetString(), v.pos ))
		{
			char precision[16], type[16], name[64];

			if (sscanf( code.GetCStr() + v.pos, "varying %15s %15s %63s", precision, type, name ) < 3)
			{
				state.SetError( "Varying in shader was ill-formed" );

				return;
			}

			// TODO: validate: known precision, known type, identifier
			// also, if we don't care about mobile we could forgo the type...
			// could also be comma-separated, see GatherUniformUserData()

			v.location = varyingLocation;

			if (!isVertexSource)
			{
				auto & varying = maps.varyings.find( name );

				if (maps.varyings.end() == varying)
				{
					state.SetError( "Fragment kernel refers to varying `%s`, not found on vertex side", name );

					return;
				}

				v.location = varying->second;
			}

			if (isVertexSource)
			{
				maps.varyings[name] = varyingLocation++;
			}

			varyingStack.push_back( v );
		}

		offset = v.pos + strlen( "varying " );
	}

	char buf[64];

	for (auto && iter = varyingStack.rbegin(); iter != varyingStack.rend(); ++iter)
	{
		sprintf( buf, "layout(location = %u) %s", iter->location, isVertexSource ? "out" : "in" );

		code.Replace( iter->pos, strlen( "varying" ), buf );
	}
}

void
VulkanProgram::Compile( int ikind, ShaderCode & code, VulkanCompilerMaps & maps, VkShaderModule & module, CompileState & state )
{
	if (state.HasError())
	{
		return;
	}

	shaderc_shader_kind kind = shaderc_shader_kind( ikind );
	const char * what = shaderc_vertex_shader == kind ? "vertex shader" : "fragment shader";
	bool isVertexSource = 'v' == what[0];

	ReplaceVaryings( isVertexSource, code, maps, state );

	const std::string & codeStr = code.GetString();

	shaderc_compilation_result_t result = shaderc_compile_into_spv( fContext->GetCompiler(), codeStr.data(), codeStr.size(), kind, what, "main", fContext->GetCompileOptions() );
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
				auto iter = maps.buffer_values.insert( std::make_pair( name, VulkanCompilerMaps::BufferValue( br.offset, br.range, true ) ) );

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

				maps.buffer_values.insert( std::make_pair( name, VulkanCompilerMaps::BufferValue( br.offset, br.range, false ) ) );
			}

			switch (kind)
			{
				case shaderc_vertex_shader:
					fPushConstantStages |= VK_SHADER_STAGE_VERTEX_BIT;
					break;
				case shaderc_fragment_shader:
					fPushConstantStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
					break;
				default:
					break;
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

			auto iter = maps.samplers.insert( std::make_pair( sampler.name, VulkanCompilerMaps::SamplerValue( type.image, counts ) ) );

			iter.first->second.fStages |= 1U << kind;
		}

		if (vkCreateShaderModule( fContext->GetDevice(), &createShaderModuleInfo, fContext->GetAllocator(), &module ) != VK_SUCCESS)
		{
			state.SetError( "Failed to create shader module!" );
		}
	}

	else
	{
		state.SetError( shaderc_result_get_error_message( result ) );
	}

	shaderc_result_release( result );
}

static bool
AreRowsOpen( const std::vector< int > & used, int row, int nrows, int ncols )
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
GetFirstRow( const std::vector< int > & used, int nrows, int ncols )
{
	for (int row = 0, last = used.size() - nrows; row <= last; ++row)
	{
		if (AreRowsOpen( used, row, nrows, ncols ))
		{
			return row;
		}
	}

	return -1;
}

std::pair< bool, int >
VulkanProgram::SearchForFreeRows( const UserdataValue values[], UserdataPosition positions[], size_t spareVectorCount )
{
	const VkPhysicalDeviceLimits & limits = fContext->GetDeviceDetails().properties.limits;

	std::vector< int > used( spareVectorCount, 0 );

	for (int i = 0; i < 4 && values[i].IsValid(); ++i)
	{
		int ncols, nrows;

		switch (values[i].fComponentCount)
		{
			case 16:
				ncols = nrows = 4;
				break;
			case 9:
				ncols = 4;
				nrows = 3;
				break;
			default:
				nrows = 1;
				ncols = values[i].fComponentCount;
				break;
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
			return std::make_pair( false, i );
		}
	}

	return std::make_pair( true, 0 );
}

static bool
HasPrefix( const std::string & code, const char * prefix, size_t pos )
{
	const size_t len = strlen( prefix );

	if (pos < len || strncmp( code.c_str() + pos - len, prefix, len ) != 0)
	{
		return false;
	}

	return pos == len || NoLeadingCharacters( code, pos - len );
}

static bool
UsesPushConstant( const ShaderCode & code, const char * what, size_t offset )
{
	size_t pos = code.Find( what, offset );

	if (std::string::npos == pos)
	{
		return false;
	}

	const size_t TotalTimeLen = strlen( "TotalTime" );
	const std::string & codeStr = code.GetString();

	if (isalnum( codeStr[pos + TotalTimeLen] ) || codeStr[pos + TotalTimeLen] == '_' )
	{
		return false;
	}

	return HasPrefix( codeStr, "u_", pos ) || HasPrefix( codeStr, "Corona", pos );
}

static void
PadVector( std::string & str, U32 lastUpTo, U32 & paddingCount)
{
	const char * padding[] = { "vec3 pad", "vec2 pad" "float pad" };

	str += padding[lastUpTo - 1];
	str += '0' + paddingCount;
	str += ";  ";

	++paddingCount;
}

U32
VulkanProgram::AddToString( std::string & str, const UserdataValue & value )
{
	U32 componentCount = value.fComponentCount;

	switch (componentCount)
	{
	case 16:
		str += "mat4 ";
		break;
	case 9:
		str += "mat3 ";
		break;
	case 4:
		str += "vec4 ";
		break;
	case 3:
		str += "vec3 ";
		break;
	case 2:
		str += "vec2 ";
		break;
	case 1:
		str += "float ";
		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}

	str += "UserData";
	str += '0' + value.fIndex;
	str += ";";

	return componentCount;
}

void
VulkanProgram::UpdateShaderSource( VulkanCompilerMaps & maps, Program* program, Program::Version version, VersionData& data )
{
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

	CompileState vertexCompileState, fragmentCompileState;
	ShaderCode vertexCode, fragmentCode;

	// Vertex shader.
	{
		shader_source[3] = program->GetVertexShaderSource();

		vertexCode.SetSources( shader_source, sizeof( shader_source ) / sizeof( shader_source[0] ) );

		ReplaceVertexSamplers( vertexCode, vertexCompileState );
	}


	// Fragment shader.
	{
		shader_source[3] = ( version == Program::kWireframe ) ? kWireframeSource : program->GetFragmentShaderSource();

		fragmentCode.SetSources( shader_source, sizeof( shader_source ) / sizeof( shader_source[0] ) );

		size_t fpos = fragmentCode.Find( "#define USING_GL_FRAG_COORD 0", 0U );

		if (std::string::npos == fpos)
		{
			fragmentCompileState.SetError( "Unable to find gl_FragCoord stub" );
		}

		if (!fragmentCompileState.HasError() && ReplaceFragCoords( fragmentCode, fpos, fragmentCompileState ))
		{
			size_t offsetTo0 = strlen( "#define USING_GL_FRAG_COORD " );

			fragmentCode.Replace( fpos + offsetTo0, 1, "1" );
		}
	}

	if (!vertexCompileState.HasError() && !fragmentCompileState.HasError())
	{
		std::vector< UserdataDeclaration > vertexDeclarations, fragmentDeclarations;

		UserdataValue values[4];

		size_t vertexOffset = GatherUniformUserdata( true, vertexCode, values, vertexDeclarations, vertexCompileState );
		size_t fragmentOffset = GatherUniformUserdata( false, fragmentCode, values, fragmentDeclarations, fragmentCompileState );

		int vertexExtra = 0, fragmentExtra = 0;

		if (
			!(vertexCompileState.HasError() && fragmentCompileState.HasError()) &&
			!(vertexDeclarations.empty() && fragmentDeclarations.empty())
			)
		{
			for (int i = 0; i < 4; ++i)
			{
				values[i].fIndex = i;
			}

			std::sort( values, values + 4 ); // sort from highest component count to lowest

			UserdataPosition positions[4];

			for (int i = 0; i < 4; ++i)
			{
				positions[i].fValue = &values[i];
			}

			const VkPhysicalDeviceLimits & limits = fContext->GetDeviceDetails().properties.limits;
			auto findResult = SearchForFreeRows( values, positions, limits.maxPushConstantsSize / 16 - 6 ); // cf. shell_default_vulkan.lua

			std::sort( positions, positions + 4 ); // sort from lowest (row, offset) to highest

			bool canUsePushConstants = true;
			const char * toReplace = NULL;
			U32 paddingCount = 0U;
			std::string replacement;

			if (!findResult.first)
			{
				U32 total = 0;

				for (int i = findResult.second; i < 4 && values[i].IsValid(); ++i)
				{
					total += values[i].fComponentCount;
				}

				if (1U == total && !UsesPushConstant( vertexCode, "TotalTime", vertexOffset ) && !UsesPushConstant( fragmentCode, "TotalTime", fragmentOffset ))
				{
					toReplace = "float TotalTime;";
					replacement = "float u_UserData";

					replacement += '0' + values[findResult.second].fIndex;
					replacement += ';';
				}

				else if (total <= 4U && !UsesPushConstant( vertexCode, "TexelSize", vertexOffset ) && !UsesPushConstant( fragmentCode, "TexelSize", fragmentOffset ))
				{
					toReplace = "vec4 TexelSize;";

					for (int i = findResult.second; i < 4 && values[i].IsValid(); ++i)
					{
						if (i > 0)
						{
							replacement += " ";
						}

						AddToString( replacement, values[i] );
					}

					if (total < 4U)
					{
						replacement += " ";

						PadVector( replacement, total, paddingCount );
					}
				}

				else
				{
					canUsePushConstants = false;

					for (int i = 0; i < 4; ++i)
					{
						positions[i].fValue = &values[i];
					}

					auto result = SearchForFreeRows( values, positions, 16U ); // four userdata matrices

					std::sort( positions, positions + 4 ); // as above

					if (!result.first)
					{
						// error!
					}
				}
			}

			const char * userDataMarker = canUsePushConstants ? "PUSH_CONSTANTS_EXTRA" : "mat4 Stub[4];";
			std::string userDataGroup( canUsePushConstants ? " pc" : " userDataObject" );

			std::string extra;
			U32 lastComponentCount;

			for (int i = 0; i < 4 && positions[i].IsValid(); ++i)
			{
				extra += " ";

				if (i > 0 && 0U == positions[i].fOffset)
				{
					U32 lastUpTo = positions[i - 1].fOffset + lastComponentCount;

					if (lastUpTo < 4U) // incomplete vector?
					{
						PadVector( extra, lastUpTo, paddingCount );
					}
				}

				lastComponentCount = AddToString( extra, *positions[i].fValue );
			}

			if (!vertexDeclarations.empty())
			{
				size_t epos = vertexCode.Find( userDataMarker, 0U );

				if (std::string::npos != epos)
				{
					if (canUsePushConstants)
					{
						vertexExtra = vertexCode.Insert( epos + strlen( userDataMarker ), extra );
					}

					else
					{
						vertexExtra = vertexCode.Replace( epos, strlen( userDataMarker ), extra );
					}
				}

				else
				{
					vertexCompileState.SetError( "Failed to find user data marker" );
				}
			}

			if (!fragmentDeclarations.empty())
			{
				size_t epos = fragmentCode.Find( userDataMarker, 0U );

				if (std::string::npos != epos)
				{
					if (canUsePushConstants)
					{
						fragmentExtra = fragmentCode.Insert( epos + strlen( userDataMarker ), extra );
					}

					else
					{
						fragmentExtra = fragmentCode.Replace( epos, strlen( userDataMarker ), extra );
					}
				}

				else
				{
					fragmentCompileState.SetError( "Failed to find user data marker" );
				}
			}

			if (!vertexCompileState.HasError() && !fragmentCompileState.HasError())
			{
				std::string prefix1( "#define u_UserData" ), prefix2 = userDataGroup + ".UserData";

				for (auto && iter = vertexDeclarations.rbegin(); iter != vertexDeclarations.rend(); ++iter)
				{
					const char suffix[] = { '0' + iter->fValue->fIndex, 0 };
					std::string declaration = (prefix1 + suffix) + (prefix2 + suffix);

					if (iter->fLength)
					{
						vertexCode.Replace( iter->fPosition + vertexExtra, iter->fLength, declaration );
					}

					else
					{
						vertexCode.Insert( iter->fPosition + vertexExtra, declaration + '\n' );
					}
				}

				for (auto && iter = fragmentDeclarations.rbegin(); iter != fragmentDeclarations.rend(); ++iter)
				{
					const char suffix[] = { '0' + iter->fValue->fIndex, 0 };
					std::string declaration = (prefix1 + suffix) + (prefix2 + suffix);

					if (iter->fLength)
					{
						fragmentCode.Replace( iter->fPosition + fragmentExtra, iter->fLength, declaration );
					}

					else
					{
						fragmentCode.Insert( iter->fPosition + fragmentExtra, declaration + '\n' );
					}
				}

				if (toReplace)
				{
					size_t vertexPos = vertexCode.Find( toReplace, 0U );
					size_t fragmentPos = fragmentCode.Find( toReplace, 0U );

					if (std::string::npos != vertexPos)
					{
						vertexCode.Replace( vertexPos, strlen( toReplace ), replacement );
					}

					if (std::string::npos != fragmentPos)
					{
						fragmentCode.Replace( fragmentPos, strlen( toReplace ), replacement );
					}
				}

				fPushConstantUniforms = canUsePushConstants;
			}
		}

		// Vertex shader.
		Compile( shaderc_vertex_shader, vertexCode, maps, data.fVertexShader, vertexCompileState );

		// Fragment shader.
		Compile( shaderc_fragment_shader, fragmentCode, maps, data.fFragmentShader, fragmentCompileState );
	}

	vertexCompileState.Report( "vertex shader" );
	fragmentCompileState.Report( "fragment shader" );
#else
	// no need to compile, but reflection here...
#endif
	data.fAttemptedCreation = true;
}

void
VulkanProgram::Update( Program::Version version, VersionData& data )
{
	Program* program = static_cast<Program*>( fResource );

	VulkanCompilerMaps maps;
	
	UpdateShaderSource( maps, program, version,	data );

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
	data.fUniformLocations[Uniform::kContentSize] = maps.CheckForUniform( "ContentSize" );
	data.fUniformLocations[Uniform::kUserData0] = maps.CheckForUniform( "UserData0" );
	data.fUniformLocations[Uniform::kUserData1] = maps.CheckForUniform( "UserData1" );
	data.fUniformLocations[Uniform::kUserData2] = maps.CheckForUniform( "UserData2" );
	data.fUniformLocations[Uniform::kUserData3] = maps.CheckForUniform( "UserData3" );

	data.fMaskTranslationLocations[0] = maps.CheckForUniform( "MaskTranslation0" );
	data.fMaskTranslationLocations[1] = maps.CheckForUniform( "MaskTranslation1" );
	data.fMaskTranslationLocations[2] = maps.CheckForUniform( "MaskTranslation2" );
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
	
	data.fAttemptedCreation = false;
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