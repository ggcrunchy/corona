//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanProgram_H__
#define _Rtt_VulkanProgram_H__

#include "Renderer/Rtt_GPUResource.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_Uniform.h"
#include "Core/Rtt_Assert.h"
#include <vulkan/vulkan.h>
#include <map>
#include <vector>

#ifdef free
#undef free
#endif

#include <spirv_cross/spirv_glsl.hpp>

// ----------------------------------------------------------------------------

struct shaderc_compiler;
struct shaderc_compile_options;

namespace Rtt
{

class VulkanRenderer;
class VulkanState;

// ----------------------------------------------------------------------------

class VulkanProgram : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanProgram Self;

	public:
		VulkanProgram( VulkanState * state );

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		
		void Bind( VulkanRenderer & renderer, Program::Version version );

		struct Location {
			Location( size_t offset = 0U, size_t range = 0U )
			:	fOffset( offset ),
				fRange( range )
			{
			}

			bool IsValid() const { return !!fRange; }

			size_t fOffset;
			size_t fRange;
		};

		// TODO: cleanup these functions
		inline Location GetUniformLocation( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fUniformLocations[ unit ];
		}

		inline Location GetTranslationLocation( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( unit >= Uniform::kMaskMatrix0 && unit <= Uniform::kMaskMatrix2 );
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fMaskTranslationLocations[ unit - Uniform::kMaskMatrix0 ];
		}

		inline U32 GetUniformTimestamp( U32 unit, Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].fTimestamps[ unit ];
		}

		inline void SetUniformTimestamp( U32 unit, Program::Version version, U32 timestamp)
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			fData[version].fTimestamps[ unit ] = timestamp;
		}

		inline bool IsValid( Program::Version version )
		{
			Rtt_ASSERT( version <= Program::kNumVersions );
			return fData[version].IsValid();
		}

	private:
		// To make custom shader code work seamlessly with masking, multiple
		// versions of each Program are automatically compiled and linked, 
		// with each version supporting a different number of active masks.
		struct VersionData
		{
			bool IsValid() const { return fVertexShader != VK_NULL_HANDLE && fFragmentShader != VK_NULL_HANDLE; }

			VkShaderModule fVertexShader;
			// TODO? mask counts as specialization info
			VkShaderModule fFragmentShader;
			Location fUniformLocations[Uniform::kNumBuiltInVariables];
			Location fMaskTranslationLocations[3]; // these should only ever be present alongside the corresponding MaskMatrix
			// TODO? also supply ranges (else assume always in proper form)
			// TODO? divvy these up between uniforms and push constants
			U32 fTimestamps[Uniform::kNumBuiltInVariables];
			U32 fShadersID;
			
			// Metadata
			int fHeaderNumLines;
		};

		void Create( Program::Version version, VersionData& data );
		void Update( Program::Version version, VersionData& data );
		void Reset( VersionData& data );

		struct Maps {
			struct BufferValue {
				BufferValue( size_t offset, size_t range, bool isUniform )
				:	fLocation( offset, range ),
					fStages( 0U ),
					fIsUniform( isUniform )
				{
				}

				Location fLocation;
				U32 fStages;
				bool fIsUniform;
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
			Location CheckForUniform( const std::string & key );
		};

		void ReplaceVaryings( bool isVertexSource, std::string & code, Maps & maps );
		void Compile( int kind, const char * sources[], int sourceCount, Maps & maps, VkShaderModule & module );
		Maps UpdateShaderSource( Program* program, Program::Version version, VersionData& data );

		static void InitializeCompiler( shaderc_compiler ** compiler, shaderc_compile_options ** options );
		static void CleanUpCompiler( shaderc_compiler * compiler, shaderc_compile_options * options );

		VulkanState * fState;
		VersionData fData[Program::kNumVersions];
		CPUResource* fResource;

		static U32 sID;

		friend class VulkanState;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanProgram_H__
