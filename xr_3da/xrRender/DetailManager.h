// DetailManager.h: interface for the CDetailManager class.
//
//////////////////////////////////////////////////////////////////////

#ifndef DetailManagerH
#define DetailManagerH
#pragma once

#include "xrpool.h"
#include "detailformat.h"
#include "detailmodel.h"

#ifdef _EDITOR
#include "ESceneClassList.h"
const int dm_max_decompress = 14;
#else
const int dm_max_decompress = 7;
#endif
const int dm_size = 65;									  //!
const int dm_cache1_count = 4;							  //
const int dm_cache1_line = dm_size * 2 / dm_cache1_count; //! dm_size*2 must be div dm_cache1_count
const int dm_max_objects = 64;
const int dm_obj_in_slot = 4;
const int dm_cache_line = dm_size + 1 + dm_size;
const int dm_cache_size = dm_cache_line * dm_cache_line;
const float dm_fade = float(2 * dm_size) - .5f;
const float dm_slot_size = DETAIL_SLOT_SIZE;

class CDetailManager
{
  public:
	struct SlotItem
	{ // ���� ������
		float scale;
		float scale_calculated;
		Fmatrix mRotY;
		u32 vis_ID; // ������ � visibility ������ �� �� ��� [�� ��������, ��������1, ��������2]
		float c_hemi;
		float c_sun;
#if RENDER == R_R1
		Fvector c_rgb;
#endif
	};
	DEFINE_VECTOR(SlotItem*, SlotItemVec, SlotItemVecIt);
	struct SlotPart
	{							//
		u32 id;					// ID ��������
		SlotItemVec items;		// ������ ��������
		SlotItemVec r_items[3]; // ������ �������� for render
	};
	enum SlotType
	{
		stReady = 0, // Ready to use
		stPending,	 // Pending for decompression

		stFORCEDWORD = 0xffffffff
	};
	struct Slot
	{ // ������������ ���� �������� DETAIL_SLOT_SIZE
		struct
		{
			u32 empty : 1;
			u32 type : 1;
			u32 frame : 30;
		};
		int sx, sz;					// ���������� ����� X x Y
		vis_data vis;				//
		SlotPart G[dm_obj_in_slot]; //

		Slot()
		{
			frame = 0;
			empty = 1;
			type = stReady;
			sx = sz = 0;
			vis.clear();
		}
	};
	struct CacheSlot1
	{
		u32 empty;
		vis_data vis;
		Slot** slots[dm_cache1_count * dm_cache1_count];
		CacheSlot1()
		{
			empty = 1;
			vis.clear();
		}
	};

	typedef xr_vector<xr_vector<SlotItemVec*>> vis_list;
	typedef svector<CDetail*, dm_max_objects> DetailVec;
	typedef DetailVec::iterator DetailIt;
	typedef poolSS<SlotItem, 4096> PSS;

  public:
	int dither[16][16];

  public:
	// swing values
	struct SSwingValue
	{
		float rot1;
		float rot2;
		float amp1;
		float amp2;
		float speed;
		void lerp(const SSwingValue& v1, const SSwingValue& v2, float factor);
	};
	SSwingValue swing_desc[2];
	SSwingValue swing_current;

  public:
	IReader* dtFS;
	DetailHeader dtH;
	DetailSlot* dtSlots; // note: pointer into VFS
	DetailSlot DS_empty;

  public:
	DetailVec objects;
	vis_list m_visibles[3]; // 0=still, 1=Wave1, 2=Wave2

#ifndef _EDITOR
	xrXRC xrc;
#endif
	CacheSlot1 cache_level1[dm_cache1_line][dm_cache1_line];
	Slot* cache[dm_cache_line][dm_cache_line]; // grid-cache itself
	svector<Slot*, dm_cache_size> cache_task;  // non-unpacked slots
	Slot cache_pool[dm_cache_size];			   // just memory for slots
	int cache_cx;
	int cache_cz;

	PSS poolSI; // pool �� �������� ���������� SlotItem

	void UpdateVisibleM();
	void UpdateVisibleS();

  public:
#ifdef _EDITOR
	virtual ObjectList* GetSnapList() = 0;
#endif

	IC bool UseVS()
	{
		return HW.Caps.geometry_major >= 1;
	}

	// Software processor
	ref_geom soft_Geom;
	void soft_Load();
	void soft_Unload();
	void soft_Render();

	// Hardware processor
	ref_geom hw_Geom;
	u32 hw_BatchSize;
	IDirect3DVertexBuffer9* hw_VB;
	IDirect3DIndexBuffer9* hw_IB;
	ref_constant hwc_consts;
	ref_constant hwc_wave;
	ref_constant hwc_wind;
	ref_constant hwc_array;
	ref_constant hwc_s_consts;
	ref_constant hwc_s_xform;
	ref_constant hwc_s_array;
	void hw_Load();
	void hw_Unload();
	void hw_Render();
	void hw_Render_dump(ref_constant array, u32 var_id, u32 lod_id, u32 c_base);

  public:
	// get unpacked slot
	DetailSlot& QueryDB(int sx, int sz);

	void cache_Initialize();
	void cache_Update(int sx, int sz, Fvector& view, int limit);
	void cache_Task(int gx, int gz, Slot* D);
	Slot* cache_Query(int sx, int sz);
	void cache_Decompress(Slot* D);
	BOOL cache_Validate();
	// cache grid to world
	int cg2w_X(int x)
	{
		return cache_cx - dm_size + x;
	}
	int cg2w_Z(int z)
	{
		return cache_cz - dm_size + (dm_cache_line - 1 - z);
	}
	// world to cache grid
	int w2cg_X(int x)
	{
		return x - cache_cx + dm_size;
	}
	int w2cg_Z(int z)
	{
		return cache_cz - dm_size + (dm_cache_line - 1 - z);
	}

	void Load();
	void Unload();
	void Render();

	/// MT stuff
	xrCriticalSection MT;
	volatile u32 m_frame_calc;
	volatile u32 m_frame_rendered;

	void __stdcall MT_CALC();
	ICF void MT_SYNC()
	{
		if (m_frame_calc == Device.dwFrame)
			return;

		MT_CALC();
	}

	CDetailManager();
	virtual ~CDetailManager();
};

#endif // DetailManagerH
