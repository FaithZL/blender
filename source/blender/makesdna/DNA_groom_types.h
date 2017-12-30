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

/** \file DNA_groom_types.h
 *  \ingroup DNA
 */

#ifndef __DNA_GROOM_TYPES_H__
#define __DNA_GROOM_TYPES_H__

#include "DNA_defs.h"
#include "DNA_listBase.h"
#include "DNA_ID.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Vertex in a closed curve for a bundle section */
typedef struct GroomSectionVertex
{
	int flag;
	float co[2];                            /* Location in the section plane */
} GroomSectionVertex;

typedef enum GroomVertexFlag
{
	GM_VERTEX_SELECT        = (1 << 0),
} GroomVertexFlag;

/* Cross-section of a bundle */
typedef struct GroomSection {
	int flag;
	int pad;
	
	float center[3];                        /* Center point */
	
	float mat[3][3];                        /* Local coordinate frame */
} GroomSection;

typedef enum GroomSectionFlag
{
	GM_SECTION_SELECT       = (1 << 0),
} GroomSectionFlag;

/* Single interpolated step along a groom curve */
typedef struct GroomCurveCache
{
	float co[3];                        /* Translation vector */
	float mat[3][3];                    /* Local coordinate frame */
} GroomCurveCache;

/* Bundle of hair strands following the same curve path */
typedef struct GroomBundle {
	struct GroomBundle *next, *prev;    /* Pointers for ListBase element */
	
	int flag;
	
	int numloopverts;                       /* Vertices per section loop */
	int totsections;                        /* Number of sections along the curve */
	int totverts;                           /* Number of vertices of all sections combined */
	int totcache;                           /* Number of cached curve steps */
	int pad;
	
	struct GroomSection *sections;          /* List of sections */
	struct GroomSectionVertex *verts;       /* List of vertices */
	struct GroomCurveCache *curve_cache;    /* Cached curve step */
} GroomBundle;

typedef enum GroomBundleFlag
{
	GM_BUNDLE_SELECT        = (1 << 0),
} GroomBundleFlag;

/* Editable groom data */
typedef struct EditGroom {
	ListBase bundles;           /* List of GroomBundle */
} EditGroom;

/* Groom curves for creating hair styles */
typedef struct Groom {
	ID id;                      /* Groom data is a datablock */
	struct AnimData *adt;       /* Animation data - for animating settings */
	
	int curve_res;              /* Curve resolution */
	int pad;
	
	struct BoundBox *bb;
	
	ListBase bundles;           /* List of GroomBundle */
	
	EditGroom *editgroom;
	void *batch_cache;
} Groom;

#ifdef __cplusplus
}
#endif

#endif /* __DNA_GROOM_TYPES_H__ */
