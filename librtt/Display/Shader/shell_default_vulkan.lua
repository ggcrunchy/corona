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

// < 256 bytes:
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 ViewProjectionMatrix;
    vec4 TexelSize;
    vec2 ContentScale;
    float DeltaTime;
} ubo;

// 256 bytes:
layout(set = 1, binding = 0) uniform UserDataObject {
    mat4 UserData0;
    mat4 UserData1;
    mat4 UserData2;
    mat4 UserData3;
} userDataObject;

#define MAX_FILL_SAMPLERS 2

layout(set = 2, binding = 0) uniform sampler2D u_Samplers[MAX_FILL_SAMPLERS + 3]; // TODO: does this stage need the "+ 3"?

// these may vary per batch, somewhat independently:
layout(push_constant) uniform PushConstants {
    vec2 MaskTranslation0; // vector #1
    float TotalTime;
    int SamplerIndex;
    vec2 MaskMatrix0[2]; // vector #2
    vec2 MaskMatrix1[2]; // vector #3
    vec2 MaskTranslation1; // vector #4
    vec2 MaskTranslation2;
    vec2 MaskMatrix2[2]; // vector #5
} pc;

#define CoronaVertexUserData a_UserData
#define CoronaTexCoord a_TexCoord.xy

#define CoronaTotalTime pc.TotalTime
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
varying P_DEFAULT float v_TotalTime;
varying P_DEFAULT int v_SamplerIndex;

#if MASK_COUNT > 0
    varying P_UV vec2 v_MaskUV0;
#endif

#if MASK_COUNT > 1
    varying P_UV vec2 v_MaskUV1;
#endif

#if MASK_COUNT > 2
    varying P_UV vec2 v_MaskUV2;
#endif

P_POSITION vec2 VertexKernel( P_POSITION vec2 position );

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
    v_TotalTime = CoronaTotalTime;
    v_SamplerIndex = pc.SamplerIndex;

	P_POSITION vec2 position = VertexKernel( a_Position );

    #if MASK_COUNT > 0
        v_MaskUV0 = mat2(pc.MaskMatrix0[0], pc.MaskMatrix0[1]) * position + pc.MaskTranslation0;
    #endif

    #if MASK_COUNT > 1
        v_MaskUV1 = mat2(pc.MaskMatrix1[0], pc.MaskMatrix1[1]) * position + pc.MaskTranslation1;
    #endif

    #if MASK_COUNT > 2
        v_MaskUV2 = mat2(pc.MaskMatrix2[0], pc.MaskMatrix2[1]) * position + pc.MaskTranslation2;
    #endif

    gl_Position = ubo.ViewProjectionMatrix * vec4( position, 0.0, 1.0 );
}
]]

shell.fragment =
[[

// cf. vertex
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 ViewProjectionMatrix;
    vec4 TexelSize;
    vec2 ContentScale;
    float DeltaTime;
} ubo;

// cf. vertex
layout(set = 1, binding = 1) uniform UserDataObject {
    mat4 UserData0;
    mat4 UserData1;
    mat4 UserData2;
    mat4 UserData3;
} userDataObject;

#define MAX_FILL_SAMPLERS 2

layout(set = 2, binding = 0) uniform sampler2D u_Samplers[MAX_FILL_SAMPLERS + 3];

varying P_POSITION vec2 v_Position;
varying P_UV vec2 v_TexCoord;
#ifdef TEX_COORD_Z
	varying P_UV float v_TexCoordZ;
#endif

varying P_COLOR vec4 v_ColorScale;
varying P_DEFAULT vec4 v_UserData;
varying P_DEFAULT float v_TotalTime;
varying P_DEFAULT int v_SamplerIndex;

#define CoronaColorScale( color ) (v_ColorScale*(color))
#define CoronaVertexUserData v_UserData

#define CoronaTotalTime v_TotalTime
#define CoronaDeltaTime ubo.DeltaTime
#define CoronaTexelSize ubo.TexelSize
#define CoronaContentScale ubo.ContentScale

// TODO: allow for sampler index too...

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

P_COLOR vec4 FragmentKernel( P_UV vec2 texCoord );

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
