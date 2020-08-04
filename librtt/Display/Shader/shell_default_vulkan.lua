local shell = {}

shell.language = "glsl"

shell.category = "default"

shell.name = "default"

-- n.b. be mindful of  https://stackoverflow.com/questions/38172696/should-i-ever-use-a-vec3-inside-of-a-uniform-buffer-or-shader-storage-buffer-o
-- in some of this layout, in particular the mask matrices
shell.vertex =
[[
#define attribute

layout(location = 0) in attribute vec2 a_Position;
layout(location = 1) in attribute vec3 a_TexCoord;
layout(location = 2) in attribute vec4 a_ColorScale;
layout(location = 3) in attribute vec4 a_UserData;

layout(binding = 0) uniform UniformBufferObject {
    // in 2D, these bits are changed only per framebuffer at most
        // back either with small (96 byte) buffers or a (largely wasteful) dynamic buffer
        // IF our minimum alignment is > 512, could coalesce these and user data
            // but only useful if we can dynamically generate shaders or swap out different binary versions...
    mat4 ViewProjectionMatrix;
    vec4 TexelSize;
    vec2 ContentScale;
    float DeltaTime;

    // in 3D, on the other hand, the matrix would change a lot
        // leave as is for now: full-fledged solution will want more anyhow, e.g. inverse modelview et al.

    // these can potentially vary per batch:
    float TotalTime; // might be updated on shader change, if time transform was found
 // int SamplerIndex; // if hardware support available, incremented if a batch had texture slots replaced
    mat3 MaskMatrix0; // update these if the batch's transformation(s) changed
    mat3 MaskMatrix1;
    mat3 MaskMatrix2;

    // 256 total, so makes sense as own dynamic buffer:
    mat4 UserData0;
    mat4 UserData1;
    mat4 UserData2;
    mat4 UserData3;
} ubo;
// TODO: divvy these up according to frequency of use
// allow for "SamplerIndex" if our hardware allows dynamic sampler indexing (http://kylehalladay.com/blog/tutorial/vulkan/2018/01/28/Textue-Arrays-Vulkan.html)
// push constants guarantee at least 128 bytes: could encompass TotalTime + Index + two other values = 4, but mask matrices = + 48 * 3 = 148
// if we crammed those, though... 36 * 3 = 108, for 112...
// by the looks of it, three components of each matrix effectively go unused, though
    // maybe a better packing would be mat2 MaskMatrix?; with accompanying vec2 MaskTranslation?;
    // each mat2 would be 4 * 4 = 16, followed by 2 * 4 = 8 for each vec2...
    // the third vec2 could be complemented by TotalTime and Index
    // thus:
        // 3 mat2 values = 48
        // + 3 vec2 values = 72
        // + 2 float values = 80, a comfortable fit
        // so push constants like:
            // mat2 MaskMatrix0; // vec4 #1
            // mat2 MaskMatrix1; // vec4 #2
            // mat2 MaskMatrix2; // vec4 #3
            // vec2 MaskTranslation0; // vec4 #4
            // vec2 MaskTranslation1;
            // vec2 MaskTranslation2; // vec4 #5
            // float TotalTime;
            // int SamplerIndex;
        // something like this might let us bind less in the common case:
            // vec2 MaskTranslation0; // vector #1
            // float TotalTime;
            // int SamplerIndex;
            // mat2 MaskMatrix0; // vector #2
            // mat2 MaskMatrix1; // vector #3
            // vec2 MaskTranslation1; // vector #4
            // vec2 MaskTranslation2;
            // mat2 MaskMatrix2; // vector #5

#define MAX_FILL_SAMPLERS 2

layout(binding = 1) uniform sampler2D u_Samplers[MAX_FILL_SAMPLERS + 3]; // TODO: does this stage need the "+ 3"?

#define CoronaVertexUserData a_UserData
#define CoronaTexCoord a_TexCoord.xy

#define CoronaTotalTime ubo.TotalTime
#define CoronaDeltaTime ubo.DeltaTime
#define CoronaTexelSize ubo.TexelSize
#define CoronaContentScale ubo.ContentScale
#define u_FillSampler0 u_Samplers[0]
#define u_FillSampler1 u_Samplers[1]

varying P_POSITION vec2 v_Position;
varying P_UV vec2 v_TexCoord;
#ifdef TEX_COORD_Z
	varying P_UV float v_TexCoordZ;
#endif

varying P_COLOR vec4 v_ColorScale;
varying P_DEFAULT vec4 v_UserData;

#if MASK_COUNT > 0
    varying P_UV vec2 v_MaskUV0;
#endif

#if MASK_COUNT > 1
    varying P_UV vec2 v_MaskUV1;
#endif

#if MASK_COUNT > 2
    varying P_UV vec2 v_MaskUV2;
#endif

#ifdef Rtt_WEBGL_ENV
%s
#else
P_POSITION vec2 VertexKernel( P_POSITION vec2 position );
#endif

void main()
{
	// "varying" are only meant as OUTPUT variables. ie: Write-only variables
	// meant to provide to a fragment shader, values computed in a vertex
	// shader.
	//
	// Certain devices, like the "Samsung Galaxy Tab 2", DON'T allow you to
	// use "varying" variable like any other local variables.

	v_TexCoord = a_TexCoord.xy;
#ifdef TEX_COORD_Z
	v_TexCoordZ = a_TexCoord.z;
#endif
	v_ColorScale = a_ColorScale;
	v_UserData = a_UserData;

	P_POSITION vec2 position = VertexKernel( a_Position );

    #if MASK_COUNT > 0
        v_MaskUV0 = ( ubo.MaskMatrix0 * vec3( position, 1.0 ) ).xy;
    #endif

    #if MASK_COUNT > 1
        v_MaskUV1 = ( ubo.MaskMatrix1 * vec3( position, 1.0 ) ).xy;
    #endif

    #if MASK_COUNT > 2
        v_MaskUV2 = ( ubo.MaskMatrix2 * vec3( position, 1.0 ) ).xy;
    #endif

    gl_Position = ubo.ViewProjectionMatrix * vec4( position, 0.0, 1.0 );
}
]]

shell.fragment =
[[
#define MAX_FILL_SAMPLERS 2

layout(binding = 1) uniform sampler2D u_Samplers[MAX_FILL_SAMPLERS + 3];

layout(binding = 0) uniform UniformBufferObject {
    mat4 ViewProjectionMatrix;
    vec4 TexelSize;
    vec2 ContentScale;
    float DeltaTime;
    float TotalTime;
    mat3 MaskMatrix0;
    mat3 MaskMatrix1;
    mat3 MaskMatrix2;
    mat4 UserData0;
    mat4 UserData1;
    mat4 UserData2;
    mat4 UserData3;
} ubo;
// TODO: can we rearrange this and elide the irrelevant bits at the end? (masks, then projection?)

varying P_POSITION vec2 v_Position;
varying P_UV vec2 v_TexCoord;
#ifdef TEX_COORD_Z
	varying P_UV float v_TexCoordZ;
#endif

varying P_COLOR vec4 v_ColorScale;
varying P_DEFAULT vec4 v_UserData;

#define CoronaColorScale( color ) (v_ColorScale*(color))
#define CoronaVertexUserData v_UserData

#define CoronaTotalTime ubo.TotalTime
#define CoronaDeltaTime ubo.DeltaTime
#define CoronaTexelSize ubo.TexelSize
#define CoronaContentScale ubo.ContentScale
#define CoronaSampler0 u_Samplers[0]
#define CoronaSampler1 u_Samplers[1]

#if MASK_COUNT > 0
    varying P_UV vec2 v_MaskUV0;
#endif

#if MASK_COUNT > 1
    varying P_UV vec2 v_MaskUV1;
#endif

#if MASK_COUNT > 2
    varying P_UV vec2 v_MaskUV2;
#endif

layout(location = 0) out P_COLOR vec4 fragColor;

#ifdef Rtt_WEBGL_ENV
%s
#else
P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord );
#endif

#define texture2D texture

void main()
{
#ifdef TEX_COORD_Z
    P_COLOR vec4 result = FragmentKernel( v_TexCoord.xy / v_TexCoordZ );
#else
    P_COLOR vec4 result = FragmentKernel( v_TexCoord );
#endif
    
    #if MASK_COUNT > 0
        result *= texture2D( u_Samplers[MAX_FILL_SAMPLERS + 0], v_MaskUV0 ).r;
    #endif

    #if MASK_COUNT > 1
        result *= texture2D( u_Samplers[MAX_FILL_SAMPLERS + 1], v_MaskUV1 ).r;
    #endif

    #if MASK_COUNT > 2
        result *= texture2D( u_Samplers[MAX_FILL_SAMPLERS + 2], v_MaskUV2 ).r;
    #endif

    fragColor = result;
}
]]

return shell
