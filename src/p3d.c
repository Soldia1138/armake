/*
 * Copyright (C)  2016  Felix "KoffeinFlummi" Wiegand
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "docopt.h"
#include "filesystem.h"
#include "utils.h"
#include "p3d.h"


bool float_equal(float f1, float f2) {
    /*
     * Performs a fuzzy float comparison.
     */

    return fabs(1.0 - (f1 / f2)) < 0.01;
}


int read_lods(FILE *f_source, struct mlod_lod *mlod_lods, uint32_t num_lods) {
    /*
     * Reads all LODs (starting at the current position of f_source) into
     * the given LODs array.
     *
     * Returns a 0 on success and a positive integer on failure.
     */

    char buffer[256];
    int i;
    int j;
    int fp_tmp;
    uint32_t tagg_len;

    fseek(f_source, 12, SEEK_SET);

    for (i = 0; i < num_lods; i++) {
        fread(buffer, 4, 1, f_source);
        if (strncmp(buffer, "P3DM", 4) != 0)
            return 1;

        fseek(f_source, 8, SEEK_CUR);
        fread(&mlod_lods[i].num_points, 4, 1, f_source);
        fread(&mlod_lods[i].num_facenormals, 4, 1, f_source);
        fread(&mlod_lods[i].num_faces, 4, 1, f_source);
        fseek(f_source, 4, SEEK_CUR);

        mlod_lods[i].points = (struct point *)malloc(sizeof(struct point) * mlod_lods[i].num_points);
        for (j = 0; j < mlod_lods[i].num_points; j++)
            fread(&mlod_lods[i].points[j], sizeof(struct point), 1, f_source);

        mlod_lods[i].facenormals = (struct triplet *)malloc(sizeof(struct triplet) * mlod_lods[i].num_facenormals);
        for (j = 0; j < mlod_lods[i].num_facenormals; j++)
            fread(&mlod_lods[i].facenormals[j], sizeof(struct triplet), 1, f_source);

        mlod_lods[i].faces = (struct mlod_face *)malloc(sizeof(struct mlod_face) * mlod_lods[i].num_faces);
        for (j = 0; j < mlod_lods[i].num_faces; j++) {
            fread(&mlod_lods[i].faces[j], 72, 1, f_source);

            fp_tmp = ftell(f_source);
            fread(mlod_lods[i].faces[j].texture_name,
                sizeof(mlod_lods[i].faces[j].texture_name), 1, f_source);
            fseek(f_source, fp_tmp + strlen(mlod_lods[i].faces[j].texture_name) + 1, SEEK_SET);

            fp_tmp = ftell(f_source);
            fread(mlod_lods[i].faces[j].material_name,
                sizeof(mlod_lods[i].faces[j].material_name), 1, f_source);
            fseek(f_source, fp_tmp + strlen(mlod_lods[i].faces[j].material_name) + 1, SEEK_SET);
        }

        fread(buffer, 4, 1, f_source);
        if (strncmp(buffer, "TAGG", 4) != 0)
            return 2;

        mlod_lods[i].mass = 0;

        while (true) {
            fseek(f_source, 1, SEEK_CUR);

            fp_tmp = ftell(f_source);
            fread(&buffer, sizeof(buffer), 1, f_source);
            fseek(f_source, fp_tmp + strlen(buffer) + 1, SEEK_SET);

            fread(&tagg_len, 4, 1, f_source);
            fp_tmp = ftell(f_source) + tagg_len;

            if (strcmp(buffer, "#Mass#") == 0) {
                mlod_lods[i].mass = (float *)malloc(sizeof(float) * mlod_lods[i].num_points);
                fread(mlod_lods[i].mass, sizeof(float) * mlod_lods[i].num_points, 1, f_source);
            }

            // @todo TAGGs: property, mass, animation, uvset

            fseek(f_source, fp_tmp, SEEK_SET);

            if (strcmp(buffer, "#EndOfFile#") == 0)
                break;
        }

        fread(&mlod_lods[i].resolution, 4, 1, f_source);
    }

    return 0;
}


void get_centre_of_gravity(struct mlod_lod *mlod_lods, uint32_t num_lods, struct triplet *centre_of_gravity) {
    /*
     * Calculate the centre of gravity for the given LODs and stores
     * it in the given triplet struct.
     */

    int i;
    int j;
    float mass;
    float x;
    float y;
    float z;

    centre_of_gravity->x = 0;
    centre_of_gravity->y = 0;
    centre_of_gravity->z = 0;

    for (i = 0; i < num_lods; i++) {
        if (float_equal(mlod_lods[i].resolution, LOD_GEOMETRY))
            break;
    }

    if (i == num_lods)
        return;

    if (mlod_lods[i].mass == 0)
        return;

    mass = 0;
    for (j = 0; j < mlod_lods[i].num_points; j++) {
        mass += mlod_lods[i].mass[j];
    }

    for (j = 0; j < mlod_lods[i].num_points; j++) {
        x = mlod_lods[i].points[j].x;
        y = mlod_lods[i].points[j].y;
        z = mlod_lods[i].points[j].z;

        centre_of_gravity->x += x * (mlod_lods[i].mass[j] / mass);
        centre_of_gravity->y += y * (mlod_lods[i].mass[j] / mass);
        centre_of_gravity->z += z * (mlod_lods[i].mass[j] / mass);
    }
}


void get_bounding_box(struct mlod_lod *mlod_lods, uint32_t num_lods,
        struct triplet *bbox_min, struct triplet *bbox_max) {
    /*
     * Calculate the bounding box for the given LODs and stores it
     * in the given triplets.
     */

    bool first;
    float x;
    float y;
    float z;
    int i;
    int j;

    bbox_min->x = 0;
    bbox_min->y = 0;
    bbox_min->z = 0;
    bbox_max->x = 0;
    bbox_max->y = 0;
    bbox_max->z = 0;

    first = true;

    for (i = 0; i < num_lods; i++) {
        for (j = 0; j < mlod_lods[i].num_points; j++) {
            x = mlod_lods[i].points[j].x;
            y = mlod_lods[i].points[j].y;
            z = mlod_lods[i].points[j].z;

            if (first || x < bbox_min->x)
                bbox_min->x = x;
            if (first || x > bbox_max->x)
                bbox_max->x = x;

            if (first || y < bbox_min->y)
                bbox_min->y = y;
            if (first || y > bbox_max->y)
                bbox_max->y = y;

            if (first || z < bbox_min->z)
                bbox_min->z = z;
            if (first || z > bbox_max->z)
                bbox_max->z = z;

            first = false;
        }
    }
}


float get_sphere(struct mlod_lod *mlod_lod) {
    /*
     * Calculate and return the bounding sphere for the given LOD.
     */

    long i;
    float sphere;
    float dist;

    sphere = 0;
    for (i = 0; i < mlod_lod->num_points; i++) {
        dist = (float)sqrt((double)(
            mlod_lod->points[i].x * mlod_lod->points[i].x +
            mlod_lod->points[i].y * mlod_lod->points[i].y +
            mlod_lod->points[i].z * mlod_lod->points[i].z));
        if (dist > sphere)
            sphere = dist;
    }

    return sphere;
}


void build_model_info(struct mlod_lod *mlod_lods, uint32_t num_lods, struct model_info *model_info) {
    int i;
    int j;
    struct triplet bbox_total_min;
    struct triplet bbox_total_max;

    model_info->lod_resolutions = (float *)malloc(sizeof(float) * num_lods);

    for (i = 0; i < num_lods; i++)
        model_info->lod_resolutions[i] = mlod_lods[i].resolution;

    model_info->index = 512; // @todo

    model_info->mem_lod_sphere = 0;
    model_info->geo_lod_sphere = 0;
    for (i = 0; i < num_lods; i++) {
        if (float_equal(mlod_lods[i].resolution, LOD_MEMORY))
            model_info->mem_lod_sphere = get_sphere(&mlod_lods[i]);
        if (float_equal(mlod_lods[i].resolution, LOD_GEOMETRY))
            model_info->geo_lod_sphere = get_sphere(&mlod_lods[i]);
    }

    model_info->point_flags[0] = 0; // @todo
    model_info->point_flags[1] = 0; // @todo
    model_info->point_flags[2] = 0; // @todo

    model_info->map_icon_color = 0xff9d8254;
    model_info->map_selected_color = 0xff9d8254;

    model_info->view_density = -1;

    get_centre_of_gravity(mlod_lods, num_lods, &model_info->centre_of_gravity);
    get_bounding_box(mlod_lods, num_lods, &bbox_total_min, &bbox_total_max);

    bbox_total_min.x -= model_info->centre_of_gravity.x;
    bbox_total_min.y -= model_info->centre_of_gravity.y;
    bbox_total_min.z -= model_info->centre_of_gravity.z;
    bbox_total_max.x -= model_info->centre_of_gravity.x;
    bbox_total_max.y -= model_info->centre_of_gravity.y;
    bbox_total_max.z -= model_info->centre_of_gravity.z;

    model_info->bbox_max.x = fabs(bbox_total_max.x - bbox_total_min.x) / 2;
    model_info->bbox_max.y = fabs(bbox_total_max.y - bbox_total_min.y) / 2;
    model_info->bbox_max.z = fabs(bbox_total_max.z - bbox_total_min.z) / 2;
    model_info->bbox_min.x = model_info->bbox_max.x * -1;
    model_info->bbox_min.y = model_info->bbox_max.y * -1;
    model_info->bbox_min.z = model_info->bbox_max.z * -1;

    model_info->cog_offset.x = model_info->bbox_max.x - bbox_total_max.x + model_info->centre_of_gravity.x;
    model_info->cog_offset.y = model_info->bbox_max.y - bbox_total_max.y + model_info->centre_of_gravity.y;
    model_info->cog_offset.z = model_info->bbox_max.z - bbox_total_max.z + model_info->centre_of_gravity.z;

    model_info->centre_of_gravity.x -= model_info->cog_offset.x;
    model_info->centre_of_gravity.y -= model_info->cog_offset.y;
    model_info->centre_of_gravity.z -= model_info->cog_offset.z;

    memcpy(&model_info->centre_of_gravity, &model_info->offset1, sizeof(struct triplet));
    memcpy(&model_info->centre_of_gravity, &model_info->offset2, sizeof(struct triplet));

    model_info->model_mass_vectors[0].x = 0; // @todo
    model_info->model_mass_vectors[0].y = 0;
    model_info->model_mass_vectors[0].z = 0;
    model_info->model_mass_vectors[1].x = 0;
    model_info->model_mass_vectors[1].y = 0;
    model_info->model_mass_vectors[1].z = 0;
    model_info->model_mass_vectors[2].x = 0;
    model_info->model_mass_vectors[2].y = 0;
    model_info->model_mass_vectors[2].z = 0;

    strncpy(model_info->thermal_profile2,
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
        sizeof(model_info->thermal_profile2)); // @todo

    model_info->autocenter = false; // @todo
    model_info->lock_autocenter = false; // @todo
    model_info->can_occlude = false; // @todo
    model_info->can_be_occluded = false; // @todo
    model_info->allow_animation = true; // @todo

    strncpy(model_info->unknown_flags, "\0\0\0\0\0\0", sizeof(model_info->unknown_flags)); // @todo
    strncpy(model_info->thermal_profile,
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
        sizeof(model_info->thermal_profile)); // @todo

    model_info->unknown_long = 0xff000000;

    // @todo: skeleton

    model_info->unknown_byte = 0;
    model_info->n_floats = 0;

    model_info->mass = 0;
    for (i = 0; i < num_lods; i++) {
        if (!float_equal(mlod_lods[i].resolution, LOD_GEOMETRY))
            continue;
        for (j = 0; j < mlod_lods[i].num_points; j++)
            model_info->mass += mlod_lods[i].mass[j];
    }
    model_info->mass_reciprocal = model_info->mass; // @todo
    model_info->alt_mass = model_info->mass; // @todo
    model_info->alt_mass_reciprocal = model_info->mass; // @todo

    strncpy(model_info->unknown_indices,
        "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
        sizeof(model_info->unknown_indices));

    model_info->unknown_long_2 = num_lods;
    model_info->unknown_bool = false;
    model_info->class_type = 0;
    model_info->destruct_type = 0;
    model_info->unknown_bool_2 = false;
    model_info->always_0 = 0;
}


void convert_lod(struct mlod_lod *mlod_lod, struct odol_lod *odol_lod) {
    long points_index;
    long i;
    int j;
    bool first;
    size_t size;
    char *ptr;
    char textures[MAXTEXTURES][512];
    struct triplet point;
    struct triplet normal;

    odol_lod->num_proxies = 0; // @todo
    odol_lod->num_items = 0; // @todo
    odol_lod->num_bonelinks = 0; // @todo

    odol_lod->num_points_mlod = mlod_lod->num_points;

    odol_lod->unknown_v52_float = 0;
    odol_lod->unknown_float_1 = 0;
    odol_lod->unknown_float_2 = 0;

    odol_lod->min_pos.x = 0;
    odol_lod->min_pos.y = 0;
    odol_lod->min_pos.z = 0;
    odol_lod->max_pos.x = 0;
    odol_lod->max_pos.y = 0;
    odol_lod->max_pos.z = 0;
    first = false;
    for (i = 0; i < mlod_lod->num_points; i++) {
        if (first || mlod_lod->points[i].x < odol_lod->min_pos.x)
            odol_lod->min_pos.x = mlod_lod->points[i].x;
        if (first || mlod_lod->points[i].y < odol_lod->min_pos.y)
            odol_lod->min_pos.y = mlod_lod->points[i].y;
        if (first || mlod_lod->points[i].z < odol_lod->min_pos.z)
            odol_lod->min_pos.z = mlod_lod->points[i].z;
        if (first || mlod_lod->points[i].x > odol_lod->max_pos.x)
            odol_lod->max_pos.x = mlod_lod->points[i].x;
        if (first || mlod_lod->points[i].y > odol_lod->max_pos.y)
            odol_lod->max_pos.y = mlod_lod->points[i].y;
        if (first || mlod_lod->points[i].z > odol_lod->max_pos.z)
            odol_lod->max_pos.z = mlod_lod->points[i].z;
    }

    odol_lod->autocenter_pos.x = (odol_lod->min_pos.x + odol_lod->max_pos.x) / 2;
    odol_lod->autocenter_pos.y = (odol_lod->min_pos.y + odol_lod->max_pos.y) / 2;
    odol_lod->autocenter_pos.z = (odol_lod->min_pos.z + odol_lod->max_pos.z) / 2;

    odol_lod->sphere = get_sphere(mlod_lod);

    for (i = 0; i < MAXTEXTURES; i++)
        textures[i][0] = 0;

    odol_lod->num_textures = 0;
    size = 0;
    for (i = 0; i < mlod_lod->num_faces; i++) {
        for (j = 0; j < MAXTEXTURES && textures[j][0] != 0; j++) {
            if (strcmp(mlod_lod->faces[i].texture_name, textures[j]) == 0)
                break;
        }

        if (j >= MAXTEXTURES) {
            printf("WARNING: Maximum amount of textures per LOD (%i) exceeded.", MAXTEXTURES);
            break;
        }

        strcpy(textures[j], mlod_lod->faces[i].texture_name);
        odol_lod->num_textures++;
        size += strlen(textures[j]) + 1;
    }

    odol_lod->textures = (char *)malloc(size);
    ptr = odol_lod->textures;
    for (i = 0; i < MAXTEXTURES && textures[i][0] != 0; i++) {
        strncpy(ptr, textures[i], strlen(textures[i]) + 1);
        ptr += strlen(textures[i]) + 1;
    }

    odol_lod->num_materials = 0; // @todo;
    odol_lod->materials = 0;

    printf("%i %i\n", mlod_lod->num_facenormals, mlod_lod->num_faces);
    odol_lod->num_faces = mlod_lod->num_faces;
    odol_lod->offset_sections = sizeof(struct odol_face) * odol_lod->num_faces + 8;

    odol_lod->always_0 = 0;

    odol_lod->faces = (struct odol_face *)malloc(sizeof(struct odol_face) * odol_lod->num_faces);

    odol_lod->num_points = 0;
    for (i = 0; i < mlod_lod->num_faces; i++) {
        odol_lod->num_points += mlod_lod->faces[i].face_type;
    }

    odol_lod->points = (struct triplet *)malloc(sizeof(struct triplet) * odol_lod->num_points);
    odol_lod->normals = (struct triplet *)malloc(sizeof(struct triplet) * odol_lod->num_points);
#ifdef VERSION70
    odol_lod->point_to_vertex = (uint32_t *)malloc(sizeof(uint32_t) * mlod_lod->num_points);
    odol_lod->vertex_to_point = (uint32_t *)malloc(sizeof(uint32_t) * odol_lod->num_points);
#else
    odol_lod->point_to_vertex = (uint16_t *)malloc(sizeof(uint16_t) * mlod_lod->num_points);
    odol_lod->vertex_to_point = (uint16_t *)malloc(sizeof(uint16_t) * odol_lod->num_points);
#endif

    points_index = 0;
    for (i = 0; i < mlod_lod->num_faces; i++) {
        odol_lod->faces[i].face_type = mlod_lod->faces[i].face_type;

        for (j = 0; j < odol_lod->faces[i].face_type; j++) {
            point.x = mlod_lod->points[mlod_lod->faces[i].table[j].points_index].x;
            point.y = mlod_lod->points[mlod_lod->faces[i].table[j].points_index].y;
            point.z = mlod_lod->points[mlod_lod->faces[i].table[j].points_index].z;
            normal.x = mlod_lod->facenormals[mlod_lod->faces[i].table[j].normals_index].x;
            normal.y = mlod_lod->facenormals[mlod_lod->faces[i].table[j].normals_index].y;
            normal.z = mlod_lod->facenormals[mlod_lod->faces[i].table[j].normals_index].z;

            printf("Point: (%f, %f, %f)\t  Normal: (%f, %f, %f)\n",
                point.x, point.y, point.z,
                normal.x, normal.y, normal.z);

            memcpy(&odol_lod->points[points_index], &point, sizeof(struct triplet));
            memcpy(&odol_lod->normals[points_index], &normal, sizeof(struct triplet));

            odol_lod->faces[i].table[j] = points_index;

            odol_lod->point_to_vertex[mlod_lod->faces[i].table[j].points_index] = points_index;
            odol_lod->vertex_to_point[points_index] = mlod_lod->faces[i].table[j].points_index;
        
            points_index++;
        }
    }

    odol_lod->num_sections = 0; // @todo
    odol_lod->sections = 0;

    odol_lod->num_selections = 0; // @todo
    odol_lod->selections = 0;

    odol_lod->num_properties = 0; // @todo
    odol_lod->properties = 0;

    odol_lod->num_frames = 0; // @todo
    odol_lod->frames = 0;

    odol_lod->icon_color = 0xff9d8254;
    odol_lod->selected_color = 0xff9d8254;

    odol_lod->unknown_residue = 0;
    odol_lod->unknown_byte = 0;

    odol_lod->table_size = sizeof(uint32_t) * 7 + sizeof(float) * 4 +
        sizeof(struct triplet) * 2 * odol_lod->num_points;

    odol_lod->uv_scale[0] = 0;
    odol_lod->uv_scale[1] = 0;
    odol_lod->uv_scale[2] = 1;
    odol_lod->uv_scale[3] = 1;
}


void write_skeleton(FILE *f_target, struct skeleton *skeleton) {
    fwrite("\0", 1, 1, f_target);
    // @todo
}


void write_model_info(FILE *f_target, uint32_t num_lods, struct model_info *model_info) {
    int i;

    fwrite( model_info->lod_resolutions,     sizeof(float) * num_lods, 1, f_target);
    fwrite(&model_info->index,               sizeof(uint32_t), 1, f_target);
    fwrite(&model_info->mem_lod_sphere,      sizeof(float), 1, f_target);
    fwrite(&model_info->geo_lod_sphere,      sizeof(float), 1, f_target);
    fwrite( model_info->point_flags,         sizeof(uint32_t) * 3, 1, f_target);
    fwrite(&model_info->offset1,             sizeof(struct triplet), 1, f_target);
    fwrite(&model_info->map_icon_color,      sizeof(uint32_t), 1, f_target);
    fwrite(&model_info->map_selected_color,  sizeof(uint32_t), 1, f_target);
    fwrite(&model_info->view_density,        sizeof(float), 1, f_target);
    fwrite(&model_info->bbox_min,            sizeof(struct triplet), 1, f_target);
    fwrite(&model_info->bbox_max,            sizeof(struct triplet), 1, f_target);
    fwrite(&model_info->centre_of_gravity,   sizeof(struct triplet), 1, f_target);
    fwrite(&model_info->offset2,             sizeof(struct triplet), 1, f_target);
    fwrite(&model_info->cog_offset,          sizeof(struct triplet), 1, f_target);
    fwrite( model_info->model_mass_vectors,  sizeof(struct triplet) * 3, 1, f_target);
    fwrite( model_info->thermal_profile2,    sizeof(char) * 24, 1, f_target);
    fwrite(&model_info->autocenter,          sizeof(bool), 1, f_target);
    fwrite(&model_info->lock_autocenter,     sizeof(bool), 1, f_target);
    fwrite(&model_info->can_occlude,         sizeof(bool), 1, f_target);
    fwrite(&model_info->can_be_occluded,     sizeof(bool), 1, f_target);
    fwrite(&model_info->allow_animation,     sizeof(bool), 1, f_target);
    fwrite( model_info->unknown_flags,       sizeof(char) * 6, 1, f_target);
    fwrite( model_info->thermal_profile,     sizeof(char) * 24, 1, f_target);
    fwrite(&model_info->unknown_long,        sizeof(uint32_t), 1, f_target);
    write_skeleton(f_target, &model_info->skeleton);
    fwrite(&model_info->unknown_byte,        sizeof(char), 1, f_target);
    fwrite(&model_info->n_floats,            sizeof(uint32_t), 1, f_target);
    fwrite("\0\0\0\0\0", 5, 1, f_target); // compression header for empty array
    fwrite(&model_info->mass,                sizeof(float), 1, f_target);
    fwrite(&model_info->mass_reciprocal,     sizeof(float), 1, f_target);
    fwrite(&model_info->alt_mass,            sizeof(float), 1, f_target);
    fwrite(&model_info->alt_mass_reciprocal, sizeof(float), 1, f_target);
    fwrite( model_info->unknown_indices,     sizeof(char) * 14, 1, f_target);
    fwrite(&model_info->unknown_long_2,      sizeof(uint32_t), 1, f_target);
    fwrite(&model_info->unknown_bool,        sizeof(bool), 1, f_target);
    fwrite(&model_info->class_type,          sizeof(char), 1, f_target);
    fwrite(&model_info->destruct_type,       sizeof(char), 1, f_target);
    fwrite(&model_info->unknown_bool_2,      sizeof(bool), 1, f_target);
    fwrite(&model_info->always_0,            sizeof(uint32_t), 1, f_target);

    for (i = 0; i < num_lods; i++)
        fwrite("\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff", 12, 1, f_target);
}


void write_odol_lod(FILE *f_target, struct odol_lod *odol_lod) {
    int x, y, z;
    long i;
    uint32_t temp;
    char *ptr;

    /*
     * All the writes involving temp are compression headers. Nothing's
     * actually being compressed though to save on processing power and
     * time. File size improvement would be minimal.
     */

    fwrite(&odol_lod->num_proxies,       sizeof(uint32_t), 1, f_target);
    // @todo proxies
    fwrite(&odol_lod->num_items,         sizeof(uint32_t), 1, f_target);
    fwrite( odol_lod->items,             sizeof(uint32_t) * odol_lod->num_items, 1, f_target);
    fwrite(&odol_lod->num_bonelinks,     sizeof(uint32_t), 1, f_target);
    // @todo bonelinks
    fwrite(&odol_lod->num_points,        sizeof(uint32_t), 1, f_target);
    fwrite(&odol_lod->unknown_v52_float, sizeof(float), 1, f_target);
    fwrite(&odol_lod->unknown_float_1,   sizeof(float), 1, f_target);
    fwrite(&odol_lod->unknown_float_2,   sizeof(float), 1, f_target);
    fwrite(&odol_lod->min_pos,           sizeof(struct triplet), 1, f_target);
    fwrite(&odol_lod->max_pos,           sizeof(struct triplet), 1, f_target);
    fwrite(&odol_lod->autocenter_pos,    sizeof(struct triplet), 1, f_target);
    fwrite(&odol_lod->sphere,            sizeof(float), 1, f_target);
    fwrite(&odol_lod->num_textures,      sizeof(uint32_t), 1, f_target);

    ptr = odol_lod->textures;
    for (i = 0; i < odol_lod->num_textures; i++)
        ptr += strlen(ptr) + 1;

    fwrite( odol_lod->textures,          ptr - odol_lod->textures, 1, f_target);
    fwrite(&odol_lod->num_materials,     sizeof(uint32_t), 1, f_target);
    // @todo materials

#ifdef VERSION70
    temp = sizeof(uint32_t) * odol_lod->num_points_mlod + sizeof(uint32_t);
#else
    temp = sizeof(uint16_t) * odol_lod->num_points_mlod + sizeof(uint32_t);
#endif
    fwrite(&temp,                        sizeof(uint32_t), 1, f_target);
    fputc(0, f_target);
    fwrite(&odol_lod->num_points_mlod,   sizeof(uint32_t), 1, f_target);
#ifdef VERSION70
    fwrite( odol_lod->point_to_vertex,   sizeof(uint32_t) * odol_lod->num_points_mlod, 1, f_target);
#else
    fwrite( odol_lod->point_to_vertex,   sizeof(uint16_t) * odol_lod->num_points_mlod, 1, f_target);
#endif
#ifdef VERSION70
    temp = sizeof(uint32_t) * odol_lod->num_points + sizeof(uint32_t);
#else
    temp = sizeof(uint16_t) * odol_lod->num_points + sizeof(uint32_t);
#endif
    fwrite(&temp,                        sizeof(uint32_t), 1, f_target);
    fputc(0, f_target);
    fwrite(&odol_lod->num_points,        sizeof(uint32_t), 1, f_target);
#ifdef VERSION70
    fwrite( odol_lod->vertex_to_point,   sizeof(uint32_t) * odol_lod->num_points, 1, f_target);
#else
    fwrite( odol_lod->vertex_to_point,   sizeof(uint16_t) * odol_lod->num_points, 1, f_target);
#endif

    fwrite(&odol_lod->num_faces,         sizeof(uint32_t), 1, f_target);
    fwrite(&odol_lod->offset_sections,   sizeof(uint32_t), 1, f_target);
    fwrite(&odol_lod->always_0,          sizeof(uint16_t), 1, f_target);

    for (i = 0; i < odol_lod->num_faces; i++) {
        fwrite(&odol_lod->faces[i].face_type, sizeof(uint32_t), 1, f_target);
#ifdef VERSION70
        fwrite( odol_lod->faces[i].table, sizeof(uint32_t) * odol_lod->faces[i].face_type, 1, f_target);
#else
        fwrite( odol_lod->faces[i].table, sizeof(uint16_t) * odol_lod->faces[i].face_type, 1, f_target);
#endif
    }

    fwrite(&odol_lod->num_sections,      sizeof(uint32_t), 1, f_target);
    // @todo sections
    fwrite(&odol_lod->num_selections,    sizeof(uint32_t), 1, f_target);
    temp = odol_lod->num_selections * 42; // @todo
    fwrite(&temp,                        sizeof(uint32_t), 1, f_target);
    fputc(0, f_target);
    // @todo selections
    fwrite(&odol_lod->num_properties,    sizeof(uint32_t), 1, f_target);
    // @todo properties
    fwrite(&odol_lod->num_frames,        sizeof(uint32_t), 1, f_target);
    // @todo frames
    fwrite(&odol_lod->icon_color,        sizeof(uint32_t), 1, f_target);
    fwrite(&odol_lod->selected_color,    sizeof(uint32_t), 1, f_target);
    fwrite(&odol_lod->unknown_residue,   sizeof(uint32_t), 1, f_target);
    fwrite(&odol_lod->unknown_byte,      sizeof(char), 1, f_target);
    fwrite(&odol_lod->table_size,        sizeof(uint32_t), 1, f_target);

    // pointflags
    temp = 9;
    fwrite(&temp,                        sizeof(uint32_t), 1, f_target);
    fputc(0, f_target);
    fwrite("\0\0\0\0\x01\0\0\0\0",       9, 1, f_target);

    // vertextable
    fwrite( odol_lod->uv_scale,          sizeof(float) * 4, 1, f_target);
    fwrite("\x01\0\0\0",                 4, 1, f_target);

    fwrite(&odol_lod->num_points,        sizeof(uint32_t), 1, f_target);
    fwrite( odol_lod->points,            sizeof(struct triplet) * odol_lod->num_points, 1, f_target);

    fwrite(&odol_lod->num_points,        sizeof(uint32_t), 1, f_target);
    temp = sizeof(uint32_t) * odol_lod->num_points + 5;
    fwrite(&temp,                        sizeof(uint32_t), 1, f_target);
    fputc(0, f_target);
    fwrite(&odol_lod->num_points,        sizeof(uint32_t), 1, f_target);
    fputc(0, f_target);
    for (i = 0; i < odol_lod->num_points; i++) {
        // write compressed triplet
        x = (int)(-511.0f * odol_lod->normals[i].x + 0.5);
        y = (int)(-511.0f * odol_lod->normals[i].y + 0.5);
        z = (int)(-511.0f * odol_lod->normals[i].z + 0.5);

        //printf("%i %i %i %i\n", i, odol_lod->normals[i].z, y, z);

        x = MAX(MIN(x, 511), -511);
        y = MAX(MIN(y, 511), -511);
        z = MAX(MIN(z, 511), -511);

        temp = (((uint32_t)z & 0x3FF) << 20) | (((uint32_t)y & 0x3FF) << 10) | ((uint32_t)x & 0x3FF);
        fwrite(&temp, sizeof(uint32_t), 1, f_target);
    }

    fwrite("\0\0\0\0\0\0\0\0\0\0\0\0",   12, 1, f_target);
}


int mlod2odol(char *source, char *target) {
    /*
     * Converts the MLOD P3D to ODOL. Overwrites the target if it already
     * exists.
     *
     * Returns 0 on success and a positive integer on failure.
     */

    FILE *f_source;
    FILE *f_temp;
    FILE *f_target;
    char buffer[4096];
    int datasize;
    int i;
    long fp_lods;
    long fp_temp;
    uint32_t num_lods;
    struct mlod_lod *mlod_lods;
    struct model_info model_info;
    struct odol_lod odol_lod;

#ifdef _WIN32
    char temp_name[2048];
    if (!GetTempFileName(getenv("HOMEPATH"), "amk", 0, temp_name)) {
        printf("Failed to get temp file name.\n");
        return 1;
    }
    f_temp = fopen(temp_name, "w+");
#else
    f_temp = tmpfile();
#endif

    if (!f_temp) {
        printf("Failed to open temp file.\n");
        return 1;
    }

    // Open source and read LODs
    f_source = fopen(source, "r");
    if (!f_source) {
        printf("Failed to open source file.\n");
        return 2;
    }

    fgets(buffer, sizeof(buffer), f_source);
    if (strncmp(buffer, "MLOD", 4) != 0) {
        printf("Source file is not MLOD.\n");
        return 3;
    }

    fseek(f_source, 8, SEEK_SET);
    fread(&num_lods, 4, 1, f_source);
    mlod_lods = (struct mlod_lod *)malloc(sizeof(struct mlod_lod) * num_lods);
    if (read_lods(f_source, mlod_lods, num_lods)) {
        printf("Failed to read LODs.\n");
        return 4;
    }

    //for (i = 0; i < num_lods; i++) {
    //    printf("LOD %i: %f %i %i %i\n", i, mlod_lods[i].resolution,
    //        mlod_lods[i].num_points,
    //        mlod_lods[i].num_facenormals,
    //        mlod_lods[i].num_faces);
    //}

    fclose(f_source);

    // Write header
    fwrite("ODOL", 4, 1, f_temp);
#ifdef VERSION70
    fwrite("\x46\0\0\0", 4, 1, f_temp); // version 70
#else
    fwrite("\x44\0\0\0", 4, 1, f_temp); // version 68
#endif
    fwrite("\0", 1, 1, f_temp); // prefix
    // there seem to be another 4 bytes here, no idea what for
    fwrite("\0\0\0\0", 4, 1, f_temp);
    fwrite(&num_lods, 4, 1, f_temp);

    // Write model info
    build_model_info(mlod_lods, num_lods, &model_info);
    write_model_info(f_temp, num_lods, &model_info);

    // Write animations (@todo)
    fwrite("\0", 1, 1, f_temp); // nope

    fp_lods = ftell(f_temp);

    // Write LOD face defaults (or rather, don't)
    for (i = 0; i < num_lods; i++)
        fwrite("\0", 1, 1, f_temp);

    // Write LODs
    for (i = 0; i < num_lods; i++) {
        // Write start address
        fp_temp = ftell(f_temp);
        fseek(f_temp, fp_lods + i * 4, SEEK_SET);
        fwrite(&fp_temp, 4, 1, f_temp);
        fseek(f_temp, 0, SEEK_END);

        // Convert to ODOL
        convert_lod(&mlod_lods[i], &odol_lod);

        // Write to file
        write_odol_lod(f_temp, &odol_lod);

        // Clean up
        free(odol_lod.textures);
        free(odol_lod.faces);

        // Write end address
        fp_temp = ftell(f_temp);
        fseek(f_temp, fp_lods + (num_lods + i) * 4, SEEK_SET);
        fwrite(&fp_temp, 4, 1, f_temp);
        fseek(f_temp, 0, SEEK_END);
    }

    // Write PhysX (@todo)
    fwrite("\0\0\0\0", 4, 1, f_temp);
    fwrite("\x00\x04\x02\x03", 4, 1, f_temp);
    fwrite("\0\0\0\0", 4, 1, f_temp);
    fwrite("\x00\x04\x02\x03", 4, 1, f_temp);
    fwrite("\0\0\0\0", 4, 1, f_temp);

    // Write temp to target
    fseek(f_temp, 0, SEEK_END);
    datasize = ftell(f_temp);

    f_target = fopen(target, "w");
    if (!f_target) {
        printf("Failed to open target file.\n");
        return 2;
    }

    fseek(f_temp, 0, SEEK_SET);
    for (i = 0; datasize - i >= sizeof(buffer); i += sizeof(buffer)) {
        fread(buffer, sizeof(buffer), 1, f_temp);
        fwrite(buffer, sizeof(buffer), 1, f_target);
    }
    fread(buffer, datasize - i, 1, f_temp);
    fwrite(buffer, datasize - i, 1, f_target);

    // Clean up
    fclose(f_temp);
    fclose(f_target);

#ifdef _WIN32
    DeleteFile(temp_name);
#endif

    for (i = 0; i < num_lods; i++) {
        free(mlod_lods[i].points);
        free(mlod_lods[i].facenormals);
        free(mlod_lods[i].faces);
        free(mlod_lods[i].mass);
    }
    free(mlod_lods);

    free(model_info.lod_resolutions);

    return 0;
}