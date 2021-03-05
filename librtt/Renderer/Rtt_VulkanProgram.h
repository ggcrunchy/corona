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
#include <utility>
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

		typedef std::pair< size_t, size_t > Interval;

	public:
		VulkanProgram( VulkanState * state );

		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		
		void Bind( VulkanRenderer & renderer, Program::Version version );

		struct Location {
			Location( size_t offset = 0U, size_t range = 0U, bool isUniform = false )
			:	fOffset( offset ),
				fRange( range ),
				fIsUniform( isUniform )
			{
			}

			bool IsValid() const { return !!fRange; }

			size_t fOffset;
			size_t fRange;
			bool fIsUniform;
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

		bool HaveFragmentConstants() const { return fFragmentConstants; }
		bool HavePushConstantUniforms() const { return fPushConstantUniforms; }

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
				:	fLocation( offset, range, isUniform ),
					fStages( 0U )
				{
				}

				Location fLocation;
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
			Location CheckForUniform( const std::string & key );
		};

		struct UserdataValue {
			UserdataValue()
			:	fComponentCount( 0U ),
				fStages( 0U )
			{
			}

			bool operator < (const UserdataValue & other) const { return fComponentCount > other.fComponentCount; } // process largest-sized elements first
			bool IsValid() const { return !!fComponentCount; }

			int fIndex;
			U32 fComponentCount;
			U32 fStages;
		};

		struct UserdataDeclaration {
			UserdataValue * fValue;
			size_t fPosition;
			size_t fLength;
		};

		struct UserdataPosition {
			UserdataPosition()
			:	fValue( NULL ),
				fOffset( 0U ),
				fRow( ~0U )
			{
			}

			bool operator < (const UserdataPosition & other) const { return fRow != other.fRow ? fRow < other.fRow : fOffset < other.fOffset; }
			bool IsValid() const { return fRow != ~0U; }

			UserdataValue * fValue;
			U32 fOffset;
			U32 fRow;
		};

		size_t GatherUniformUserdata( bool isVertexSource, std::string & code, UserdataValue values[], const std::vector< Interval > & intervals, std::vector< UserdataDeclaration > & declarations, bool & canUsePushConstants );
		void ReplaceVaryings( bool isVertexSource, std::string & code, Maps & maps, const std::vector< Interval > & intervals );
		void Compile( int kind, std::string & code, Maps & maps, VkShaderModule & module, const std::vector< Interval > & intervals );
		std::pair< bool, int > SearchForFreeRows( const UserdataValue values[], UserdataPosition positions[] );
		U32 AddToString( std::string & str, const UserdataValue & value );
		Maps UpdateShaderSource( Program* program, Program::Version version, VersionData& data );

		static void InitializeCompiler( shaderc_compiler ** compiler, shaderc_compile_options ** options );
		static void CleanUpCompiler( shaderc_compiler * compiler, shaderc_compile_options * options );

		VulkanState * fState;
		VersionData fData[Program::kNumVersions];
		CPUResource* fResource;
		bool fPushConstantUniforms;
		bool fFragmentConstants;

		static U32 sID;

		friend class VulkanState;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanProgram_H__
