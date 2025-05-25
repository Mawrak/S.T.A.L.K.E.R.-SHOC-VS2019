////////////////////////////////////////////////////////////////////////////
//Created 	: 07.01.2025
//Author	: Deathman, Mihan-323
//Basic idea 	: xRay engine 2.0, sm 3.0 render
////////////////////////////////////////////////////////////////////////////
#include "common.h"
////////////////////////////////////////////////////////////////////////////
uniform sampler2D s_gbuffer_1;
uniform sampler2D s_gbuffer_2;
uniform sampler2D s_zbuffer;
////////////////////////////////////////////////////////////////////////////
uniform float4 pos_decompression_params;
////////////////////////////////////////////////////////////////////////////
struct GBuffer
{
    	float3 Albedo;
    	float3 Position;
    	float3 Normal;
    	float Glossiness;
    	float AO;
	float MaterialID;
};

struct GBufferPacked
{
    	float4 rt_GBuffer_1 : COLOR0;
    	float4 rt_GBuffer_2 : COLOR1;
    	float4 rt_ZBuffer : COLOR2;
};
////////////////////////////////////////////////////////////////////////////
float CheckDepth(float Depth)
{
	return Depth < pos_decompression_params.z ? pos_decompression_params.w : Depth;
}
////////////////////////////////////////////////////////////////////////////
float2 PackNormal(float3 normal)
{
   	float2 packed_normal;
   	packed_normal = 0.5 * (normal.xy + float2(1, 1));
   	packed_normal.x *= (normal.z < 0 ? -1.0 : 1.0);
   	packed_normal.x = packed_normal.x * 0.5 + 0.5;
   	return packed_normal;
}

float3 UnpackNormal(float2 packed_normal)
{
   	float3 unpacked_normal;
   	packed_normal.x = packed_normal.x * 2.0 - 1.0;
   	unpacked_normal.xy = (2.0 * abs(packed_normal)) - float2(1,1);
   	unpacked_normal.z = (packed_normal.x < 0 ? -1.0 : 1.0) * sqrt(abs(1 - unpacked_normal.x * unpacked_normal.x - unpacked_normal.y * unpacked_normal.y));
   	return unpacked_normal;
}

float PackPosition(float3 position)
{
   	return position.z;
}

float3 UnpackPosition(float Depth, float2 TexCoords)
{
	float3 position;
	position.z = Depth;
	position.xy = position.z * (TexCoords * 2.0f * pos_decompression_params.xy - pos_decompression_params.xy);
	return position;
}
////////////////////////////////////////////////////////////////////////////
float GetDepth(float2 TexCoords)
{
    	float Depth = tex2D(s_zbuffer, TexCoords).r;

    	return CheckDepth(Depth);
}

float3 GetPosition(float2 TexCoords)
{
    	float Depth = tex2D(s_zbuffer, TexCoords).r;

    	Depth = CheckDepth(Depth);

    	return UnpackPosition(Depth, TexCoords);
}

float3 GetNormal(float2 TexCoords)
{
	float2 PackedNormal = tex2D(s_gbuffer_2, TexCoords).xy;
	
	return UnpackNormal(PackedNormal);
}
////////////////////////////////////////////////////////////////////////////
GBufferPacked PackGBuffer(GBuffer Input)
{
    	GBufferPacked GBuffer;

    	GBuffer.rt_GBuffer_1 = float4(Input.Albedo, Input.Glossiness);
    	GBuffer.rt_GBuffer_2 = float4(PackNormal(Input.Normal), PackPosition(Input.Position), Input.AO);
    	GBuffer.rt_ZBuffer = float4(0, 0, 0, 0);

    	return GBuffer;
}

GBuffer UnpackGBuffer(float2 TexCoords)
{
    	GBuffer GBuffer;

    	float4 GBuffer_1 = tex2D(s_gbuffer_1, TexCoords);
    	float4 GBuffer_2 = tex2D(s_gbuffer_2, TexCoords);
	//float ZBuffer = tex2D(s_zbuffer, TexCoords).r;

    	GBuffer.Albedo = GBuffer_1.rgb;
    	GBuffer.Glossiness = GBuffer_1.a;
    	GBuffer.Normal = UnpackNormal(GBuffer_2.rg);
	GBuffer.MaterialID = 0.0f;//GBuffer_2.b;
    	GBuffer.AO = GBuffer_2.a;
    	GBuffer.Position = UnpackPosition(CheckDepth(GBuffer_2.b), TexCoords);

    	return GBuffer;
}
////////////////////////////////////////////////////////////////////////////
