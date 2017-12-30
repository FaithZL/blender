/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) Blender Foundation
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Lukas Toenne
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/gpencil.c
 *  \ingroup bke
 */

 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_string_utils.h"

#include "BLT_translation.h"

#include "DNA_groom_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_animsys.h"
#include "BKE_global.h"
#include "BKE_groom.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_object.h"

#include "DEG_depsgraph.h"


void BKE_groom_init(Groom *groom)
{
	BLI_assert(MEMCMP_STRUCT_OFS_IS_ZERO(groom, id));
	
	groom->bb = BKE_boundbox_alloc_unit();
	
	groom->curve_res = 12;
}

void *BKE_groom_add(Main *bmain, const char *name)
{
	Groom *groom = BKE_libblock_alloc(bmain, ID_GM, name, 0);

	BKE_groom_init(groom);

	return groom;
}

static void groom_bundles_free(ListBase *bundles)
{
	for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
	{
		if (bundle->curve_cache)
		{
			MEM_freeN(bundle->curve_cache);
		}
		if (bundle->sections)
		{
			MEM_freeN(bundle->sections);
		}
		if (bundle->verts)
		{
			MEM_freeN(bundle->verts);
		}
	}
	BLI_freelistN(bundles);
}

/** Free (or release) any data used by this groom (does not free the groom itself). */
void BKE_groom_free(Groom *groom)
{
	BKE_groom_batch_cache_free(groom);
	
	if (groom->editgroom)
	{
		EditGroom *edit = groom->editgroom;
		
		groom_bundles_free(&edit->bundles);
		
		MEM_freeN(edit);
		groom->editgroom = NULL;
	}
	
	MEM_SAFE_FREE(groom->bb);
	
	groom_bundles_free(&groom->bundles);
	
	BKE_animdata_free(&groom->id, false);
}

/**
 * Only copy internal data of Groom ID from source to already allocated/initialized destination.
 * You probably never want to use that directly, use id_copy or BKE_id_copy_ex for typical needs.
 *
 * WARNING! This function will not handle ID user count!
 *
 * \param flag  Copying options (see BKE_library.h's LIB_ID_COPY_... flags for more).
 */
void BKE_groom_copy_data(Main *UNUSED(bmain), Groom *groom_dst, const Groom *groom_src, const int UNUSED(flag))
{
	groom_dst->bb = MEM_dupallocN(groom_src->bb);
	
	BLI_duplicatelist(&groom_dst->bundles, &groom_src->bundles);
	for (GroomBundle *bundle = groom_dst->bundles.first; bundle; bundle = bundle->next)
	{
		if (bundle->curve_cache)
		{
			bundle->curve_cache = MEM_dupallocN(bundle->curve_cache);
		}
		if (bundle->sections)
		{
			bundle->sections = MEM_dupallocN(bundle->sections);
		}
		if (bundle->verts)
		{
			bundle->verts = MEM_dupallocN(bundle->verts);
		}
	}
	
	groom_dst->editgroom = NULL;
}

Groom *BKE_groom_copy(Main *bmain, const Groom *groom)
{
	Groom *groom_copy;
	BKE_id_copy_ex(bmain, &groom->id, (ID **)&groom_copy, 0, false);
	return groom_copy;
}

void BKE_groom_make_local(Main *bmain, Groom *groom, const bool lib_local)
{
	BKE_id_make_local_generic(bmain, &groom->id, true, lib_local);
}


bool BKE_groom_minmax(Groom *groom, float min[3], float max[3])
{
	// TODO
	UNUSED_VARS(groom, min, max);
	return true;
}

void BKE_groom_boundbox_calc(Groom *groom, float r_loc[3], float r_size[3])
{
	if (groom->bb == NULL)
	{
		groom->bb = MEM_callocN(sizeof(BoundBox), "boundbox");
	}

	float mloc[3], msize[3];
	if (!r_loc)
	{
		r_loc = mloc;
	}
	if (!r_size)
	{
		r_size = msize;
	}

	float min[3], max[3];
	INIT_MINMAX(min, max);
	if (!BKE_groom_minmax(groom, min, max)) {
		min[0] = min[1] = min[2] = -1.0f;
		max[0] = max[1] = max[2] = 1.0f;
	}

	mid_v3_v3v3(r_loc, min, max);

	r_size[0] = (max[0] - min[0]) / 2.0f;
	r_size[1] = (max[1] - min[1]) / 2.0f;
	r_size[2] = (max[2] - min[2]) / 2.0f;

	BKE_boundbox_init_from_minmax(groom->bb, min, max);
	groom->bb->flag &= ~BOUNDBOX_DIRTY;
}


/* === Depsgraph evaluation === */

/* linear bspline section eval */
static void groom_eval_curve_cache_section_linear(
        GroomBundle *bundle,
        int isection,
        int curve_res)
{
	BLI_assert(bundle->totsections > 1);
	BLI_assert(isection < bundle->totsections - 1);
	BLI_assert(curve_res >= 1);
	
	GroomSection *section = &bundle->sections[isection];
	const float *co0 = section->center;
	const float *co1 = (section+1)->center;
	
	float dx[3];
	sub_v3_v3v3(dx, co1, co0);
	mul_v3_fl(dx, 1.0f / curve_res);
	
	GroomCurveCache *cache = bundle->curve_cache + curve_res * isection;
	float x[3];
	copy_v3_v3(x, co0);
	for (int i = 0; i <= curve_res; ++i, ++cache)
	{
		copy_v3_v3(cache->co, x);
		add_v3_v3(x, dx);
	}
}

/* forward differencing method for cubic polynomial eval */
static void groom_forward_diff_cubic(float a, float b, float c, float d, float *p, int it, int stride)
{
	float f = (float)it;
	a *= 1.0f / (f*f*f);
	b *= 1.0f / (f*f);
	c *= 1.0f / (f);
	
	float q0 = d;
	float q1 = a + b + c;
	float q2 = 6 * a + 2 * b;
	float q3 = 6 * a;

	for (int i = 0; i <= it; i++) {
		*p = q0;
		p = POINTER_OFFSET(p, stride);
		q0 += q1;
		q1 += q2;
		q2 += q3;
	}
}

/* cubic bspline section eval */
static void groom_eval_curve_cache_section_cubic(
        GroomBundle *bundle,
        int isection,
        int curve_res)
{
	BLI_assert(bundle->totsections > 2);
	BLI_assert(isection < bundle->totsections - 1);
	BLI_assert(curve_res >= 1);
	
	GroomSection *section = &bundle->sections[isection];
	GroomCurveCache *cache = bundle->curve_cache + curve_res * isection;
	
	const float *co0 = (section-1)->center;
	const float *co1 = section->center;
	const float *co2 = (section+1)->center;
	const float *co3 = (section+2)->center;
	
	float a, b, c, d;
	for (int k = 0; k < 3; ++k)
	{
		/* define tangents from segment direction */
		float n1, n2;
		if (isection == 0)
		{
			n1 = co2[k] - co1[k];
			n2 = 0.5f * (co3[k] - co1[k]);
		}
		else if (isection == bundle->totsections - 2)
		{
			n1 = 0.5f * (co2[k] - co0[k]);
			n2 = co2[k] - co1[k];
		}
		else
		{
			n1 = 0.5f * (co2[k] - co0[k]);
			n2 = 0.5f * (co3[k] - co1[k]);
		}
		
		/* Hermite spline interpolation */
		a = 2.0f * (co1[k] - co2[k]) + n1 + n2;
		b = 3.0f * (co2[k] - co1[k]) - 2.0f * n1 - n2;
		c = n1;
		d = co1[k];
		
		groom_forward_diff_cubic(a, b, c, d, cache->co + k, curve_res, sizeof(GroomCurveCache));
	}
}

static void groom_eval_curve_step(float mat[3][3], const float mat_prev[3][3], const float co0[3], const float co1[3])
{
	float dir[3];
	sub_v3_v3v3(dir, co1, co0);
	normalize_v3(dir);
	
	float dir_prev[3];
	normalize_v3_v3(dir_prev, mat_prev[2]);
	float rot[3][3];
	rotation_between_vecs_to_mat3(rot, dir_prev, dir);
	
	mul_m3_m3m3(mat, rot, mat_prev);
}

static void groom_eval_curve_cache_mats(GroomCurveCache *cache, int totcache, float basemat[3][3])
{
	BLI_assert(totcache > 0);
	
	if (totcache == 1)
	{
		/* nothing to rotate, use basemat */
		copy_m3_m3(cache->mat, basemat);
		return;
	}
	
	/* align to first segment */
	groom_eval_curve_step(cache[0].mat, basemat, cache[1].co, cache[0].co);
	++cache;
	
	/* align interior segments to average of prev and next segment */
	for (int i = 1; i < totcache - 1; ++i)
	{
		groom_eval_curve_step(cache[0].mat, cache[-1].mat, cache[1].co, cache[-1].co);
		++cache;
	}
	
	/* align to last segment */
	groom_eval_curve_step(cache[0].mat, cache[-1].mat, cache[0].co, cache[-1].co);
}

void BKE_groom_eval_curve_cache(const EvaluationContext *UNUSED(eval_ctx), Scene *UNUSED(scene), Object *ob)
{
	BLI_assert(ob->type == OB_GROOM);
	Groom *groom = (Groom *)ob->data;
	ListBase *bundles = (groom->editgroom ? &groom->editgroom->bundles : &groom->bundles);
	
	for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
	{
		const int totsections = bundle->totsections;
		if (totsections == 0)
		{
			/* clear cache */
			if (bundle->curve_cache)
			{
				MEM_freeN(bundle->curve_cache);
				bundle->curve_cache = NULL;
				bundle->totcache = 0;
			}
			
			/* nothing to do */
			continue;
		}
		
		bundle->totcache = (totsections-1) * groom->curve_res + 1;
		bundle->curve_cache = MEM_reallocN_id(bundle->curve_cache, sizeof(GroomCurveCache) * bundle->totcache, "groom bundle curve cache");
		
		if (totsections == 1)
		{
			/* degenerate case */
			copy_v3_v3(bundle->curve_cache[0].co, bundle->sections[0].center);
		}
		else if (totsections == 2)
		{
			/* single section, linear */
			groom_eval_curve_cache_section_linear(bundle, 0, groom->curve_res);
		}
		else
		{
			/* cubic splines */
			GroomSection *section = bundle->sections;
			for (int i = 0; i < totsections-1; ++i, ++section)
			{
				groom_eval_curve_cache_section_cubic(bundle, i, groom->curve_res);
			}
		}
		
		float basemat[3][3];
		unit_m3(basemat); // TODO
		groom_eval_curve_cache_mats(bundle->curve_cache, bundle->totcache, basemat);
		
		/* Copy coordinate frame to sections */
		{
			GroomSection *section = bundle->sections;
			GroomCurveCache *cache = bundle->curve_cache;
			for (int i = 0; i < totsections; ++i, ++section, cache += groom->curve_res)
			{
				copy_m3_m3(section->mat, cache->mat);
			}
		}
	}
}

static void groom_bundles_curve_cache_clear(ListBase *bundles)
{
	for (GroomBundle *bundle = bundles->first; bundle; bundle = bundle->next)
	{
		if (bundle->curve_cache)
		{
			MEM_freeN(bundle->curve_cache);
			bundle->curve_cache = NULL;
			bundle->totcache = 0;
		}
	}
}

void BKE_groom_clear_curve_cache(Object *ob)
{
	BLI_assert(ob->type == OB_GROOM);
	Groom *groom = (Groom *)ob->data;
	
	groom_bundles_curve_cache_clear(&groom->bundles);
	if (groom->editgroom)
	{
		groom_bundles_curve_cache_clear(&groom->editgroom->bundles);
	}
}

void BKE_groom_eval_geometry(const EvaluationContext *UNUSED(eval_ctx), Groom *groom)
{
	if (G.debug & G_DEBUG_DEPSGRAPH) {
		printf("%s on %s\n", __func__, groom->id.name);
	}
	
	if (groom->bb == NULL || (groom->bb->flag & BOUNDBOX_DIRTY)) {
		BKE_groom_boundbox_calc(groom, NULL, NULL);
	}
}


/* === Draw Cache === */

void (*BKE_groom_batch_cache_dirty_cb)(Groom* groom, int mode) = NULL;
void (*BKE_groom_batch_cache_free_cb)(Groom* groom) = NULL;

void BKE_groom_batch_cache_dirty(Groom* groom, int mode)
{
	if (groom->batch_cache)
	{
		BKE_groom_batch_cache_dirty_cb(groom, mode);
	}
}

void BKE_groom_batch_cache_free(Groom *groom)
{
	if (groom->batch_cache)
	{
		BKE_groom_batch_cache_free_cb(groom);
	}
}
