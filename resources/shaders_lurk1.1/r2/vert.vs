#include "common.h"
struct vf
{
float4 hpos	: POSITION;
float2 tc0	: TEXCOORD0;
float3 c0	: COLOR0;
float fog	: FOG;
};
/*
struct	v_vert
{
float4 P	: POSITION;
float4	N	: NORMAL;
float4 T	: TANGENT;
float4 B	: BINORMAL;
float4	color	: COLOR0;
float2 uv	: TEXCOORD0;
};
struct v_static {
float4 P : POSITION;
float4 Nh : NORMAL;
float4 T : TANGENT;
float4 B : BINORMAL;
float2 tc : TEXCOORD0;
float2 lmh : TEXCOORD1;
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
float4 color : COLOR0;
#endif
};
*/
vf main (v_static v)
{
vf o;
float3 N = unpack_normal	(v.Nh);
o.hpos = mul	(m_VP, v.P);
o.tc0	= unpack_tc_base	(v.tc,v.T.w,v.B.w);
float3 L_rgb = v.color.xyz;
float3 L_hemi = v_hemi(N)*v.Nh.w;
float3 L_sun = v_sun(N)*v.color.w;
float3 L_final	= L_rgb + L_hemi + L_sun + L_ambient;
o.c0	= L_final;
o.fog = calc_fogging (v.P);
return o;
}