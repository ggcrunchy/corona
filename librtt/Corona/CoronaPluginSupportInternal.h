#pragma once
//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaPublicSupportInternal_H__
#define _CoronaPublicSupportInternal_H__

#include "Corona/CoronaPublicTypes.h"
#include <functional>

struct CoronaRenderer { long _r = 0L; };
struct CoronaRenderData { long _rd = 0L; };
struct CoronaShader { long _s = 0L; };
struct CoronaShaderData { long _sd = 0L; };
struct CoronaDisplayObject { long _do = 0L; };
struct CoronaGroupObject { long _go = 0L; };

struct CoronaFunctionPointer {
    void (*fFunc)();
};

class CoronaTempStore {
public:
	CoronaTempStore( const char * str )
		: fIndex( 0U )
	{
		fInitialHash = std::hash< const char * >{}( str );
	}
	
private:
	template<typename H>
	union HandleKey {
		H fHandle;
		size_t fKey;
	};

	struct Bucket {
		void * fObject = NULL;
		size_t fKey;
		int fExtra;
		bool fHeld = false;
	};

public:
	template<typename H>
	class BucketRef {
	public:
		BucketRef( Bucket * bucket, H handle )
		:	fBucket( bucket ),
			fHandle( handle )
		{
			fBucket->fHeld = true;
		}

		~BucketRef()
		{
			if (fBucket)
			{
				fBucket->fObject = NULL;
				fBucket->fHeld = false;
			}
		}

		H GetHandle() const { return fHandle; }

		void Forget()
		{
			if (fBucket)
			{
				fBucket->fHeld = false;
			}

			fBucket = NULL;
		}

	private:
		Bucket * fBucket;
		H fHandle;
	};

	template<typename H>
	BucketRef< H > Add( void * object, int extra = 0 )
	{
		size_t key = std::hash< void * >{}( object ) + fInitialHash;

		for (int i = 0; i < 8; ++i)
		{
			Bucket * bucket = &fBuckets[fIndex];

			fIndex = (fIndex + 1) % 8;

			if (!bucket->fHeld)
			{
				bucket->fObject = object;
				bucket->fExtra = extra;
				bucket->fKey = key;
				bucket->fHeld = true;

				HandleKey< H > u;

				u.fKey = key;

				return BucketRef< H >( bucket, u.fHandle );
			}
		}

		return BucketRef< H >( NULL, NULL );
	}

	template<typename H>
	void * Find( H handle, int * extra = NULL ) const
	{
		HandleKey< H > u = { handle };

		for (int i = 0; i < 8; ++i)
		{
			if (fBuckets[i].fKey == u.fKey && fBuckets[i].fObject)
			{
				if (extra)
				{
					*extra = fBuckets[i].fExtra;
				}

				return fBuckets[i].fObject;
			}
		}

		return NULL;
	}

private:
	Bucket fBuckets[8];
	size_t fInitialHash;
	unsigned int fIndex;
};

CoronaTempStore::BucketRef< CoronaRendererHandle > CoronaInternalStoreRenderer( const void * renderer );
CoronaTempStore::BucketRef< CoronaRenderDataHandle > CoronaInternalStoreRenderData( const void * renderData );
CoronaTempStore::BucketRef< CoronaShaderHandle > CoronaInternalStoreShader( const void * shader );
CoronaTempStore::BucketRef< CoronaShaderDataHandle > CoronaInternalStoreShaderData( const void * shaderData );
CoronaTempStore::BucketRef< CoronaDisplayObjectHandle > CoronaInternalStoreDisplayObject( const void * displayObject );
CoronaTempStore::BucketRef< CoronaGroupObjectHandle > CoronaInternalStoreGroupObject( const void * groupObject );

#endif // _CoronaPublicSupportInternal_H__
