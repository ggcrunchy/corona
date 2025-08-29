//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_Texture_H__
#define _Rtt_Texture_H__

#include "Renderer/Rtt_CPUResource.h"
#include "Core/Rtt_Types.h"
#include <stdlib.h>

// ----------------------------------------------------------------------------

struct CustomUploadTextureInfo
{
	U32 fError;
	U32 fTextureTarget;
	U32 fTextureFormat;
	bool fAddedMipmaps;
};
		
namespace Rtt
{

// ----------------------------------------------------------------------------

class Texture : public CPUResource
{
	public:
		typedef CPUResource Super;
		typedef Texture Self;

		typedef enum _FormatValue
		{
			kAlpha,
			kLuminance,
			kRGB,
			kRGBA,
			kBGRA,
			kABGR,
			kARGB,
			kLuminanceAlpha,
			kNonCore,
			kNumFormats
		}
		FormatValue;

		class Format {
		public:
			Format( FormatValue value = kNumFormats );
			
			bool operator == ( FormatValue value ) const;
			
			static Format NonCore( U16 index, U16 layoutDetails );
			
			FormatValue GetValue( U16* index = NULL, U16* layoutDetails = NULL ) const;
			
			static int BlockDimsID( U8 width, U8 height );
			static void GetBlockDims( int blockDimsID, U8& width, U8& height );
			
			static int GetCompressedSize( U8 w, U8 h, U8 blockWidth, U8 blockHeight, U8 blockSize );
			
		private:
			U16 fValue;
			U16 fIndex;
		};

		typedef enum _Filter
		{
			kNearest,
			kLinear,
			kNumFilters
		}
		Filter;

		typedef enum _Wrap
		{
			kClampToEdge,
			kRepeat,
			kMirroredRepeat,

			kNumWraps
		}
		Wrap;

		typedef enum _Unit
		{
			kFill0,
			kFill1,
			kMask0,
			kMask1,
			kMask2,
			kNumUnits
		}
		Unit;

	public:

		Texture( Rtt_Allocator* allocator );
		virtual ~Texture();

		virtual ResourceType GetType() const;
		virtual void Allocate();
		virtual void Deallocate();

		virtual U32 GetWidth() const = 0;
		virtual U32 GetHeight() const = 0;
		virtual Format GetFormat() const = 0;
		virtual Filter GetFilter() const = 0;
		virtual Wrap GetWrapX() const;
		virtual Wrap GetWrapY() const;
		virtual size_t GetSizeInBytes() const;
		virtual U8 GetByteAlignment() const;

		virtual void DoCustomUpload( void* resource, CustomUploadTextureInfo& info ) const;

		virtual const U8* GetData() const;
		virtual void ReleaseData();

		virtual void SetFilter( Filter newValue );
		virtual void SetWrapX( Wrap newValue );
		virtual void SetWrapY( Wrap newValue );
	
		void SetMipmapFilters( Filter newMagValue, Filter newMinValue )
		{
			fMipmapMagFilter = newMagValue;
			fMipmapMinFilter = newMinValue;
		}
	
		void GetMipmapFilters( Filter& magValue, Filter& minValue )
		{
			magValue = (Filter)fMipmapMagFilter;
			minValue = (Filter)fMipmapMinFilter;
		}
	
	public:
		void SetRetina( bool newValue ){ fIsRetina = newValue; }
		bool IsRetina() const { return fIsRetina; }
		void SetTarget( bool newValue ){ fIsTarget = newValue; }
		bool IsTarget() const { return fIsTarget; }
		void SetGenMipmaps( bool newValue ){ fGenMipmaps = newValue; }
		bool GenMipmaps() const { return fGenMipmaps; }
		void SetHasCustomUploader( bool newValue ){ fHasCustomUploader = newValue; }
		bool HasCustomUploader() const { return fHasCustomUploader; }
		
	private:
		U8 fMipmapMagFilter : 2;
		U8 fMipmapMinFilter : 2;
		bool fIsRetina;
		bool fIsTarget;
		bool fGenMipmaps;
		bool fHasCustomUploader;
};

inline bool operator == ( Texture::FormatValue value, const Texture::Format& format )
{
	return format.GetValue() == value;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_Texture_H__
