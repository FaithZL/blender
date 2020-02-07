
/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "render/colorspace.h"
#include "render/mesh.h"
#include "render/object.h"

#include "blender/blender_sync.h"
#include "blender/blender_util.h"

CCL_NAMESPACE_BEGIN

static void sync_smoke_volume(Scene *scene, BL::Object &b_ob, Mesh *mesh, float frame)
{
  BL::FluidDomainSettings b_domain = object_fluid_gas_domain_find(b_ob);
  if (!b_domain) {
    return;
  }

  ImageManager *image_manager = scene->image_manager;
  AttributeStandard attributes[] = {ATTR_STD_VOLUME_DENSITY,
                                    ATTR_STD_VOLUME_COLOR,
                                    ATTR_STD_VOLUME_FLAME,
                                    ATTR_STD_VOLUME_HEAT,
                                    ATTR_STD_VOLUME_TEMPERATURE,
                                    ATTR_STD_VOLUME_VELOCITY,
                                    ATTR_STD_NONE};

  for (int i = 0; attributes[i] != ATTR_STD_NONE; i++) {
    AttributeStandard std = attributes[i];
    if (!mesh->need_attribute(scene, std)) {
      continue;
    }

    mesh->volume_isovalue = b_domain.clipping();

    Attribute *attr = mesh->attributes.add(std);
    VoxelAttribute *volume_data = attr->data_voxel();
    ImageMetaData metadata;
    bool animated = false;

    volume_data->manager = image_manager;
    volume_data->slot = image_manager->add_image(Attribute::standard_name(std),
                                                 b_ob.ptr.data,
                                                 animated,
                                                 frame,
                                                 INTERPOLATION_LINEAR,
                                                 EXTENSION_CLIP,
                                                 IMAGE_ALPHA_AUTO,
                                                 u_colorspace_raw,
                                                 metadata);
  }

  /* Create a matrix to transform from object space to mesh texture space.
   * This does not work with deformations but that can probably only be done
   * well with a volume grid mapping of coordinates. */
  if (mesh->need_attribute(scene, ATTR_STD_GENERATED_TRANSFORM)) {
    Attribute *attr = mesh->attributes.add(ATTR_STD_GENERATED_TRANSFORM);
    Transform *tfm = attr->data_transform();

    BL::Mesh b_mesh(b_ob.data());
    float3 loc, size;
    mesh_texture_space(b_mesh, loc, size);

    *tfm = transform_translate(-loc) * transform_scale(size);
  }
}

static void sync_volume_object(BL::BlendData &b_data, BL::Object &b_ob, Scene *scene, Mesh *mesh)
{
  BL::Volume b_volume(b_ob.data());
  b_volume.grids.load(b_data.ptr.data);

  bool transform_added = false;

  mesh->volume_isovalue = 1e-3f; /* TODO: make user setting. */

  /* Find grid with matching name. */
  BL::Volume::grids_iterator b_grid_iter;
  for (b_volume.grids.begin(b_grid_iter); b_grid_iter != b_volume.grids.end(); ++b_grid_iter) {
    BL::VolumeGrid b_grid = *b_grid_iter;
    ustring name = ustring(b_grid.name());
    AttributeStandard std = ATTR_STD_NONE;

    /* TODO: find nicer solution to detect standard attribute. */
    if (name == Attribute::standard_name(ATTR_STD_VOLUME_DENSITY)) {
      std = ATTR_STD_VOLUME_DENSITY;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_COLOR)) {
      std = ATTR_STD_VOLUME_COLOR;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_FLAME)) {
      std = ATTR_STD_VOLUME_FLAME;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_HEAT)) {
      std = ATTR_STD_VOLUME_HEAT;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_TEMPERATURE)) {
      std = ATTR_STD_VOLUME_TEMPERATURE;
    }
    else if (name == Attribute::standard_name(ATTR_STD_VOLUME_VELOCITY)) {
      std = ATTR_STD_VOLUME_VELOCITY;
    }

    if ((std != ATTR_STD_NONE && mesh->need_attribute(scene, std)) ||
        mesh->need_attribute(scene, name)) {
      Attribute *attr = (std != ATTR_STD_NONE) ?
                            mesh->attributes.add(std) :
                            mesh->attributes.add(name, TypeDesc::TypeFloat, ATTR_ELEMENT_VOXEL);
      VoxelAttribute *volume_data = attr->data_voxel();
      ImageMetaData metadata;
      const bool animated = false;
      const float frame = b_volume.grids.frame();

      volume_data->manager = scene->image_manager;
      volume_data->slot = scene->image_manager->add_image(name.c_str(),
                                                          b_volume.ptr.data,
                                                          animated,
                                                          frame,
                                                          INTERPOLATION_LINEAR,
                                                          EXTENSION_CLIP,
                                                          IMAGE_ALPHA_AUTO,
                                                          u_colorspace_raw,
                                                          metadata);

      /* TODO: support each grid having own transform. */
      /* TODO: support full transform instead of only using boundbox. */
      /* TODO: avoid computing bounds multiple times, perhaps by postponing
       * setting this transform until voxels are loaded. */
      if (!transform_added && mesh->need_attribute(scene, ATTR_STD_GENERATED_TRANSFORM)) {
        Attribute *attr = mesh->attributes.add(ATTR_STD_GENERATED_TRANSFORM);
        Transform *tfm = attr->data_transform();

        b_grid.load();

        VolumeGrid *volume_grid = (VolumeGrid *)b_grid.ptr.data;
        size_t min[3], max[3];
        if (BKE_volume_grid_dense_bounds(volume_grid, min, max)) {
          float mat[4][4];
          BKE_volume_grid_dense_transform_matrix(volume_grid, min, max, mat);
          *tfm = transform_inverse(get_transform(mat));
        }
        else {
          *tfm = transform_identity();
        }

        transform_added = true;
      }
    }
  }
}

void BlenderSync::sync_volume(BL::Object &b_ob, Mesh *mesh)
{
  bool old_has_voxel_attributes = mesh->has_voxel_attributes();

  /* TODO: support disabling volumes in view layer. */
  if (b_ob.type() == BL::Object::type_VOLUME) {
    /* Volume object. Create only attributes, bounding mesh will then
     * be automatically generated later. */
    sync_volume_object(b_data, b_ob, scene, mesh);
  }
  else {
    /* Smoke domain. */
    sync_smoke_volume(scene, b_ob, mesh, b_scene.frame_current());
  }

  /* Tag update. */
  bool rebuild = (old_has_voxel_attributes != mesh->has_voxel_attributes());
  mesh->tag_update(scene, rebuild);
}

CCL_NAMESPACE_END
