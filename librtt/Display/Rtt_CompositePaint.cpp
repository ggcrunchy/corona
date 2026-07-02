//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_CompositePaint.h"
#include "Display/Rtt_ShaderResource.h"
#include "Display/Rtt_TextureResource.h"

#include "Renderer/Rtt_RenderData.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

using PtrTR = SharedPtr<TextureResource>;

// ----------------------------------------------------------------------------

// extra info layout = (Texture*)[n], PtrTR[n], (U8 = count, U8-triple[count])[n] | n = fExtraCount

// ----------------------------------------------------------------------------

CompositePaint::CompositePaint( Paint *paint0, Paint *paint1 )
:	Super(),
	fPaint0( paint0 ),
	fPaint1( paint1 ),
	fExtraInfo( NULL ),
	fExtraCount( 0 )
{
	Initialize( kMultitexture );
}

CompositePaint::~CompositePaint()
{
	Rtt_DELETE( fPaint1 );
	Rtt_DELETE( fPaint0 );

	ClearExtraInfo();
}

void
CompositePaint::UpdatePaint( RenderData& data )
{
	Super::UpdatePaint( data );

	if ( 0 == fExtraCount )
	{
		data.fTextures.SetFill0( fPaint0->GetTexture() );
		data.fTextures.SetFill1( fPaint1->GetTexture() );
	}
	else
	{
		data.fTextures.PointToArray( GetTexturesList(), fExtraCount + 2 );
	}
}

Texture *
CompositePaint::GetTexture() const
{
	// Rtt_ASSERT_NOT_REACHED();

	// Just in case...
	return fPaint0->GetTexture();
}

const Texture*
CompositePaint::GetTexture0() const
{
	return fPaint0->GetTexture();
}

const Texture*
CompositePaint::GetTexture1() const
{
	return fPaint1->GetTexture();
}

const Paint*
CompositePaint::AsPaint( Type type ) const
{
	const Paint *result = Super::AsPaint( type );

	if ( ! result )
	{
		result = fPaint0->AsPaint( type );
	}

	if ( ! result )
	{
		result = fPaint1->AsPaint( type );
	}

	return result;
}
	
	
void
CompositePaint::ApplyPaintUVTransformations( ArrayVertex2& vertices ) const
{
	fPaint0->ApplyPaintUVTransformations( vertices );
}

/*
const MLuaUserdataAdapter&
CompositePaint::GetAdapter() const
{
}
*/

static U32
TextureInfoSize( U32 extraCount, bool includeResources )
{
	U32 total = ( extraCount + 2 ) * sizeof(Texture*);
	
	return total + ( includeResources ? extraCount * sizeof(PtrTR) : 0 );
}

void
CompositePaint::PrepareExtraTextures( U32 count, const LengthAccumulator& names )
{
	ClearExtraInfo();

	if ( count > 0 )
	{
		fExtraInfo = (ExtraTextureInfo*)Rtt_MALLOC( NULL, TextureInfoSize( count, true ) + count + names.GetTotalBytes() );
		fExtraCount = count;
		
		Texture** texturesList = GetTexturesList();

		texturesList[0] = fPaint0 ? fPaint0->GetTexture() : NULL;
		texturesList[1] = fPaint1 ? fPaint1->GetTexture() : NULL;		
	}
	else
	{
		fExtraInfo = NULL;
		fExtraCount = 0;
	}
}

void
CompositePaint::CommitExtraTextures()
{
	Texture** texturesList = GetTexturesList();
	PtrTR* list = (PtrTR*)GetTextureResourceList();
	for ( U32 i = 0; i < fExtraCount; i++ )
	{
		texturesList[i + 2] = &list[i]->GetTexture();
	}
}

void
CompositePaint::ClearExtraInfo()
{
	Rtt_ASSERT( ( NULL == fExtraInfo ) == ( 0 == fExtraCount ) );
	
	PtrTR* list = (PtrTR*)GetTextureResourceList();
	for ( U32 i = 0; i < fExtraCount; i++ )
	{
		list[i].~PtrTR();
	}
	
	Rtt_FREE( fExtraInfo );
}
		
void*
CompositePaint::GetTextureResourceList() const
{
	return fExtraInfo ? fExtraInfo->fData + TextureInfoSize( fExtraCount, false ) : NULL;
}

Texture**
CompositePaint::GetTexturesList() const
{
	return fExtraInfo ? (Texture**)fExtraInfo->fData : NULL;
}

U8*
CompositePaint::GetNameList() const
{
	return fExtraInfo ? fExtraInfo->fData + TextureInfoSize( fExtraCount, true ) : NULL;
}

const U8*
CompositePaint::GetNameListGivenTextureList( Texture** list, U32 extraCount )
{
	return (const U8*)( list ) + TextureInfoSize( extraCount, true );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

