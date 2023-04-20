//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Display/Rtt_TesselatorPolygon.h"

#include "Display/Rtt_TesselatorLine.h"
#include "Rtt_Matrix.h"
#include "Rtt_Transform.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class Triangulate
{
	public:
		// Tesselate a contour/polygon into a series of triangles.
		static bool Process(
			const ArrayVertex2 &contour, ArrayVertex2 &result, Rect& bounds );

		// Compute area of a contour/polygon
		static float Area(const ArrayVertex2 &contour);

		// Decide if point Px/Py is inside triangle defined by (Ax,Ay) (Bx,By) (Cx,Cy)
		static bool InsideTriangle(
			float Ax, float Ay,
			float Bx, float By,
			float Cx, float Cy,
			float Px, float Py);

	private:
		static bool Snip(
			const ArrayVertex2 &contour,
			int u, int v, int w, int n, int *V );
};

float
Triangulate::Area( const ArrayVertex2 &contour )
{
	int n = contour.Length();

	float A = 0.0f;

	for( int p = n-1, q = 0; q < n; p = q++ )
	{
		A += contour[p].x * contour[q].y - contour[q].x * contour[p].y;
	}
	return A * 0.5f;
}

// InsideTriangle decides if point P is Inside of the triangle defined by A, B, C.
bool
Triangulate::InsideTriangle(
	float Ax, float Ay,
	float Bx, float By,
	float Cx, float Cy,
	float Px, float Py )
{
	float ax, ay, bx, by, cx, cy, apx, apy, bpx, bpy, cpx, cpy;
	float cCROSSap, bCROSScp, aCROSSbp;

	ax  = Cx - Bx;	ay  = Cy - By;
	bx  = Ax - Cx;	by  = Ay - Cy;
	cx  = Bx - Ax;	cy  = By - Ay;
	apx = Px - Ax;	apy = Py - Ay;
	bpx = Px - Bx;	bpy = Py - By;
	cpx = Px - Cx;	cpy = Py - Cy;

	aCROSSbp = ax*bpy - ay*bpx;
	cCROSSap = cx*apy - cy*apx;
	bCROSScp = bx*cpy - by*cpx;

	return ((aCROSSbp >= 0.0f) && (bCROSScp >= 0.0f) && (cCROSSap >= 0.0f));
};

static const float EPSILON=0.0000000001f;

bool
Triangulate::Snip( const ArrayVertex2 &contour, int u, int v, int w, int n, int *V )
{
	int p;
	float Ax, Ay, Bx, By, Cx, Cy, Px, Py;

	Ax = contour[V[u]].x;
	Ay = contour[V[u]].y;

	Bx = contour[V[v]].x;
	By = contour[V[v]].y;

	Cx = contour[V[w]].x;
	Cy = contour[V[w]].y;

	if ( EPSILON > (((Bx-Ax)*(Cy-Ay)) - ((By-Ay)*(Cx-Ax))) ) { return false; }

	for ( p=0; p < n; p++ )
	{
		if ( (p == u) || (p == v) || (p == w) ) { continue; }
		Px = contour[V[p]].x;
		Py = contour[V[p]].y;
		if ( InsideTriangle( Ax, Ay, Bx, By, Cx, Cy, Px, Py ) ) { return false; }
	}

	return true;
}

bool
Triangulate::Process( const ArrayVertex2 &contour, ArrayVertex2 &result, Rect& bounds )
{
	// allocate and initialize list of Vertices in polygon
	int n = contour.Length();
	if ( n < 3 ) return false;

	int *V = new int[n];

	// we want a counter-clockwise polygon in V
	if ( 0.0f < Area(contour) )
	{
		for ( int v = 0; v < n; v++ )
		{
			V[v] = v;
		}
	}
	else
	{
		for ( int v = 0; v < n; v++ )
		{
			V[v] = (n-1)-v;
		}
	}

	int nv = n;

	// remove nv-2 Vertices, creating 1 triangle every time
	int count = 2*nv;   // error detection

	for ( int m = 0, v = nv-1; nv > 2; )
	{
		// if we loop, it is probably a non-simple polygon 
		if (0 >= (count--))
		{
			//** Triangulate: ERROR - probable bad polygon!
			return false;
		}

		// three consecutive vertices in current polygon, <u,v,w>
		int u = v;   if (nv <= u) { u = 0; }	// previous
		v = u+1;     if (nv <= v) { v = 0; }	// new v   
		int w = v+1; if (nv <= w) { w = 0; }	// next    

		if ( Snip( contour, u, v, w, nv, V ) )
		{
			int a,b,c,s,t;

			// true names of the vertices
			a = V[u]; b = V[v]; c = V[w];

			// output Triangle
			result.Append( contour[a] ); bounds.Union( contour[a] );
			result.Append( contour[b] ); bounds.Union( contour[b] );
			result.Append( contour[c] ); bounds.Union( contour[c] );

			m++;

			// remove v from remaining polygon
			for ( s = v, t = v+1; t < nv; s++, t++ )
			{
				V[s] = V[t];
			}
			nv--;

			// resest error detection counter
			count = 2*nv;
		}
	}

	delete [] V;

	return true;
}

// ----------------------------------------------------------------------------

TesselatorPolygon::TesselatorPolygon( Rtt_Allocator *allocator )
:	Super(),
	fContour( allocator ),
	fFill( allocator ),
	fSelfBounds(),
	fCenter( kVertexOrigin ),
	fConvexity( kUnknown ),
	fIsFillValid( false ),
	fIsBadPolygon( false ),
	fIsStrip( false ),
	fFillCount( -1 )
{
}

static void
ConvertListToStrip( const ArrayVertex2& fill, ArrayVertex2& temp )
{
	S32 length = fill.Length();

	Rtt_ASSERT( length % 3 == 0 );

	const Vertex2* verts = fill.ReadAccess();
	bool ccw = false;

	temp.Reserve( length + ( length / 3 - 1 ) * 2 );

	Vertex2 v3;

	for ( S32 i = 0; i < length; i += 3, ccw = !ccw )
	{
		if ( i > 0 ) // degenerate
		{
			temp.Append( v3 );
		}

		Vertex2 v2 = verts[i + 1], v1;

		if ( ccw )
		{
			v3 = verts[i];
			v1 = verts[i + 2];
		}

		else
		{
			v1 = verts[i];
			v3 = verts[i + 2];
		}

		if ( i > 0 ) // degenerate
		{
			temp.Append( v1 );
		}

		temp.Append( v1 );
		temp.Append( v2 );
		temp.Append( v3 );
	}
}

static const ArrayVertex2*
GetVertexData( const ArrayVertex2& fill, ArrayVertex2& temp, bool isStrip )
{
	if ( isStrip )
	{
		ConvertListToStrip( fill, temp );

		return &temp;
	}

	else
	{
		return &fill;
	}
}

void
TesselatorPolygon::GenerateFill( ArrayVertex2& vertices )
{
	Update();

	Rtt_ASSERT( !fIsStrip || fIsFillValid );

	ArrayVertex2 temp( fFill.Allocator() );
	const ArrayVertex2* fill = GetVertexData( fFill, temp, fIsStrip );

	for (int i=0; i<fill->Length(); i++) {
		vertices.Append((*fill)[i]);
	}
}

void
TesselatorPolygon::GenerateFillTexture( ArrayVertex2& texCoords, const Transform& t )
{
	Update();

	Real w = fSelfBounds.Width();
	Real h = fSelfBounds.Height();

	Real invW = Rtt_RealDiv( Rtt_REAL_1, w );
	Real invH = Rtt_RealDiv( Rtt_REAL_1, h );

	Rtt_ASSERT( !fIsStrip || fIsFillValid );

	ArrayVertex2 temp( fFill.Allocator() );
	const ArrayVertex2* fill = GetVertexData( fFill, temp, fIsStrip );

	const ArrayVertex2& src = *fill;
	ArrayVertex2& vertices = texCoords;

	// Transform
	if ( t.IsIdentity() )
	{
		for ( int i = 0, iMax = src.Length(); i < iMax; i++ )
		{
			// Normalize: assume src is centered about (0,0)
			Vertex2 v =
			{
				( Rtt_RealMul( src[i].x, invW ) + Rtt_REAL_HALF ),
				( Rtt_RealMul( src[i].y, invH ) + Rtt_REAL_HALF ),
			};
			vertices.Append( v );
		}
	}
	else
	{
		// Normalize/Transform: assume src is centered about (0,0)
		Matrix m;
		m.Scale( Rtt_RealMul( invW, t.GetSx() ), Rtt_RealMul( invH, t.GetSy() ) );
		m.Rotate( - t.GetRotation() );
		m.Translate( t.GetX() + Rtt_REAL_HALF, t.GetY() + Rtt_REAL_HALF );
		m.Apply( vertices.WriteAccess(), vertices.Length() );

		for ( int i = 0, iMax = src.Length(); i < iMax; i++ )
		{
			Vertex2 v = src[i];
			m.Apply( v );
			vertices.Append( v );
		}
	}
}

void
TesselatorPolygon::GenerateStroke( ArrayVertex2& vertices )
{
	TesselatorLine t( fContour, TesselatorLine::kLoopMode );
	t.SetInnerWidth( GetInnerWidth() );
	t.SetOuterWidth( GetOuterWidth() );

	t.GenerateStroke( vertices );
}

void
TesselatorPolygon::GetSelfBounds( Rect& rect )
{
	Update();
	rect = fSelfBounds;
}

Geometry::PrimitiveType
TesselatorPolygon::GetFillPrimitive() const
{
	return fIsStrip ? Geometry::kTriangleStrip : Geometry::kTriangles;
}

U32
TesselatorPolygon::FillVertexCount() const
{
	if ( -1 == fFillCount )
	{
		TesselatorPolygon dummy( fContour.Allocator() );

		dummy.fContour.Reserve( fContour.Length() );

		for (int i = 0, iMax = fContour.Length(); i < iMax; ++i)
		{
			dummy.fContour.Append( fContour[i] );
		}

		dummy.Update();

		fFillCount = S32( dummy.fFill.Length() );
	}

	U32 count = U32( fFillCount );

	if ( fIsStrip && count > 0 )
	{
		count += ( count / 3 - 1 ) * 2;
	}

	return count;
}

U32
TesselatorPolygon::StrokeVertexCount() const
{
	return TesselatorLine::VertexCountFromPoints( fContour, true );
}

void
TesselatorPolygon::Invalidate()
{
	SetTypeChanged( fIsStrip );

	fFillCount = -1;
	fConvexity = kUnknown;

	fIsFillValid = false;
	fIsBadPolygon = false;
	fIsStrip = false;
}

// Convexity checks, used in Process(): https://math.stackexchange.com/a/1745427
#define PROCESS_EPSILON 1e-2
#define PROCESS_NOT_EQUAL_ZERO( x ) ( x < -PROCESS_EPSILON || x > +PROCESS_EPSILON )
#define PROCESS_LESS_THAN_ZERO( x ) ( x < -PROCESS_EPSILON )
#define PROCESS_GREATER_THAN_ZERO( x ) ( x > +PROCESS_EPSILON )

static bool
HandleFlips( Real v, int &firstSign, int &sign, int &flips )
{
	if ( PROCESS_GREATER_THAN_ZERO( v ) )
	{
		if ( 0 == sign )
		{
			firstSign = +1;
		}
		else if ( sign < 0 )
		{
			++flips;
		}

		sign = +1;
	}
	else if ( PROCESS_LESS_THAN_ZERO( v ) )
	{
		if ( 0 == sign )
		{
			firstSign = -1;
		}
		else if ( sign > 0 )
		{
			++flips;
		}

		sign = -1;
	}

	return flips <= 2;
}

static bool
CheckConvexity( const ArrayVertex2 &contour )
{
	S32 n = contour.Length();
	int detSign = 0; // First nonzero orientation (positive or negative)
	int xSign = 0, ySign = 0;
    int xFirstSign = 0, yFirstSign = 0; // Sign of first nonzero edge vector x, y
    int xFlips = 0, yFlips = 0; // Number of sign changes in x, y
	Vertex2 vp1 = contour[n - 2], vp2 = contour[n - 1];

	for ( S32 i = 0; i < n; ++i )
	{
		Vertex2 vp3 = contour[i];
 
		// Previous, next edge vectors ("before", "after"):
		Real bx = vp2.x - vp1.x, ax = vp3.x - vp2.x;
		Real by = vp2.y - vp1.y, ay = vp3.y - vp2.y; 

		// Calculate sign flips using the next edge vector ("after"), recording the first sign.
		if ( !HandleFlips( ax, xFirstSign, xSign, xFlips ) || !HandleFlips( ay, yFirstSign, ySign, yFlips ) )
		{
			return false;
		}

		// Find out the orientation of this pair of edges,
		// and ensure it does not differ from previous ones.
		Real det = bx*ay - ax*by;
		if ( 0 == detSign && PROCESS_NOT_EQUAL_ZERO( det ) )
		{
			detSign = det;
		}
		else if ( detSign > 0 && PROCESS_LESS_THAN_ZERO( det ) )
		{
			return false;
		}
		else if ( detSign < 0 && PROCESS_GREATER_THAN_ZERO( det ) )
		{
			return false;
		}

		vp1 = vp2;
		vp2 = vp3;
	}

	// Final / wraparound sign flips:
	if ( 0 != xSign && 0 != xFirstSign && xSign != xFirstSign )
	{
		++xFlips;
	}
	if ( 0 != ySign && 0 != yFirstSign && ySign != yFirstSign )
	{
		++yFlips;
	}

	// Concave polygons have two sign flips along each axis.
	return 2 == xFlips && 2 == yFlips;
}

#undef PROCESS_EPSILON
#undef PROCESS_NOT_EQUAL_ZERO
#undef PROCESS_LESS_THAN_ZERO
#undef PROCESS_GREATER_THAN_ZERO

bool
TesselatorPolygon::IsConvex() const
{
	if ( kUnknown == fConvexity )
	{
		fConvexity = CheckConvexity( fContour ) ? kConvex : kConcave;
	}

	return kConvex == fConvexity;
}

void
TesselatorPolygon::Update()
{
	// NOTE: fIsBadPolygon can only be true if there was already an
	// attempt to Update() in which Process() failed.
	if ( ! fIsFillValid
		 && ! fIsBadPolygon )
	{
		fSelfBounds.SetEmpty();

		fIsFillValid = Triangulate::Process( fContour, fFill, fSelfBounds );
		fIsBadPolygon = ! fIsFillValid;

		if ( fIsFillValid )
		{
			// Center vertices about the origin
			Vertex2 center;
			fSelfBounds.GetCenter( center );

			fCenter = center;
		}
		else
		{
			// Failure case
			Rtt_TRACE_SIM( ( "WARNING: Polygon could not be generated. The polygon outline is invalid, possibly due to holes or self-intersection.\n" ) );
			fFill.Empty();
			fSelfBounds.SetEmpty();
		}
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

