#include "common.h"
struct	v_vert
{
float4 pos	: POSITION;
float4	color	: COLOR0;
};
struct vf
{
float4 hpos	: POSITION;
float4 c	: COLOR0;
float fog	: FOG;
};
vf main (v_vert v)
{
vf o;
o.hpos = mul	(m_VP, v.pos);
o.c = v.color;
o.fog = calc_fogging (v.pos);
return o;
}