/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_main.c

#include "quakedef.h"

qboolean	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;

gpuframedata_t r_framedata;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];
float		r_matview[16];
float		r_matproj[16];
float		r_matviewproj[16];

//johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;
float rs_megatexels;

//
// view origin
//
vec3_t	vup;
vec3_t	vpn;
vec3_t	vright;
vec3_t	r_origin;

float r_fovx, r_fovy; //johnfitz -- rendering fov may be different becuase of r_waterwarp and r_stereo
qboolean water_warp;

//
// screen size info
//
refdef_t	r_refdef;

mleaf_t		*r_viewleaf, *r_oldviewleaf;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


cvar_t	r_norefresh = {"r_norefresh","0",CVAR_NONE};
cvar_t	r_drawentities = {"r_drawentities","1",CVAR_NONE};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1",CVAR_NONE};
cvar_t	r_speeds = {"r_speeds","0",CVAR_NONE};
cvar_t	r_pos = {"r_pos","0",CVAR_NONE};
cvar_t	r_fullbright = {"r_fullbright","0",CVAR_NONE};
cvar_t	r_lightmap = {"r_lightmap","0",CVAR_NONE};
cvar_t	r_shadows = {"r_shadows","0",CVAR_ARCHIVE};
cvar_t	r_wateralpha = {"r_wateralpha","1",CVAR_ARCHIVE};
cvar_t	r_dynamic = {"r_dynamic","1",CVAR_ARCHIVE};
cvar_t	r_novis = {"r_novis","0",CVAR_ARCHIVE};
#if defined(USE_SIMD)
cvar_t	r_simd = {"r_simd","1",CVAR_ARCHIVE};
#endif

cvar_t	gl_finish = {"gl_finish","0",CVAR_NONE};
cvar_t	gl_clear = {"gl_clear","1",CVAR_NONE};
cvar_t	gl_polyblend = {"gl_polyblend","1",CVAR_NONE};
cvar_t	gl_playermip = {"gl_playermip","0",CVAR_NONE};
cvar_t	gl_nocolors = {"gl_nocolors","0",CVAR_NONE};

//johnfitz -- new cvars
cvar_t	r_clearcolor = {"r_clearcolor","2",CVAR_ARCHIVE};
cvar_t	r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t	r_lerplightstyles = {"r_lerplightstyles", "1", CVAR_ARCHIVE}; // 0=off; 1=skip abrupt transitions; 2=always lerp
cvar_t	gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE};
cvar_t	gl_farclip = {"gl_farclip", "16384", CVAR_ARCHIVE};
cvar_t	gl_overbright = {"gl_overbright", "1", CVAR_ARCHIVE};
cvar_t	gl_overbright_models = {"gl_overbright_models", "1", CVAR_ARCHIVE};
cvar_t	r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t	r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t	r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t	r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t	r_lerpmodels = {"r_lerpmodels", "1", CVAR_NONE};
cvar_t	r_lerpmove = {"r_lerpmove", "1", CVAR_NONE};
cvar_t	r_nolerp_list = {"r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_NONE};
cvar_t	r_noshadow_list = {"r_noshadow_list", "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/laser.mdl", CVAR_NONE};

extern cvar_t	r_vfog;
extern cvar_t	vid_fsaa;
//johnfitz

cvar_t	gl_zfix = {"gl_zfix", "1", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t	r_lavaalpha = {"r_lavaalpha","0",CVAR_NONE};
cvar_t	r_telealpha = {"r_telealpha","0",CVAR_NONE};
cvar_t	r_slimealpha = {"r_slimealpha","0",CVAR_NONE};

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float	map_fallbackalpha;

qboolean r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz

cvar_t	r_scale = {"r_scale", "1", CVAR_ARCHIVE};

//==============================================================================
//
// FRAMEBUFFERS
//
//==============================================================================

glframebufs_t framebufs;

/*
=============
GL_CreateFBOAttachment
=============
*/
static GLuint GL_CreateFBOAttachment (GLenum format, int samples, GLenum filter, const char *name)
{
	GLenum target = samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
	GLuint texnum;

	glGenTextures (1, &texnum);
	GL_BindNative (GL_TEXTURE0, target, texnum);
	GL_ObjectLabelFunc (GL_TEXTURE, texnum, -1, name);
	if (samples > 1)
	{
		GL_TexStorage2DMultisampleFunc (target, samples, format, vid.width, vid.height, GL_FALSE);
	}
	else
	{
		GL_TexStorage2DFunc (target, 1, format, vid.width, vid.height);
		glTexParameteri (target, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri (target, GL_TEXTURE_MIN_FILTER, filter);
	}
	glTexParameteri (target, GL_TEXTURE_MAX_LEVEL, 0);

	return texnum;
}

/*
=============
GL_CreateFBO
=============
*/
static GLuint GL_CreateFBO (GLenum target, GLuint colors, GLuint depth, GLuint stencil, const char *name)
{
	GLenum status;
	GLuint fbo;

	GL_GenFramebuffersFunc (1, &fbo);
	GL_BindFramebufferFunc (GL_FRAMEBUFFER, fbo);
	GL_ObjectLabelFunc (GL_FRAMEBUFFER, fbo, -1, name);

	if (colors)
		GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, colors, 0);
	if (depth)
		GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, depth, 0);
	if (stencil)
		GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, target, stencil, 0);

	status = GL_CheckFramebufferStatusFunc (GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
		Sys_Error ("Failed to create %s (status code 0x%X)", name, status);

	return fbo;
}

/*
=============
GL_CreateFrameBuffers
=============
*/
void GL_CreateFrameBuffers (void)
{
	GLenum color_format = GL_RGB10_A2;
	GLenum depth_format = GL_DEPTH24_STENCIL8;

	/* query MSAA limits */
	glGetIntegerv (GL_MAX_COLOR_TEXTURE_SAMPLES, &framebufs.max_color_tex_samples);
	glGetIntegerv (GL_MAX_DEPTH_TEXTURE_SAMPLES, &framebufs.max_depth_tex_samples);
	framebufs.max_samples = q_min (framebufs.max_color_tex_samples, framebufs.max_depth_tex_samples);

	/* main framebuffer (color only) */
	framebufs.composite.color_tex = GL_CreateFBOAttachment (color_format, 1, GL_NEAREST, "composite colors");
	framebufs.composite.fbo = GL_CreateFBO (GL_TEXTURE_2D, framebufs.composite.color_tex, 0, 0, "composite fbo");

	/* scene framebuffer (color + depth + stencil, potentially multisampled) */
	framebufs.scene.samples = Q_nextPow2 ((int) q_max (1.f, vid_fsaa.value));
	framebufs.scene.samples = CLAMP (1, framebufs.scene.samples, framebufs.max_samples);

	framebufs.scene.color_tex = GL_CreateFBOAttachment (color_format, framebufs.scene.samples, GL_NEAREST, "scene colors");
	framebufs.scene.depth_stencil_tex = GL_CreateFBOAttachment (depth_format, framebufs.scene.samples, GL_NEAREST, "scene depth/stencil");
	framebufs.scene.fbo = GL_CreateFBO (framebufs.scene.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D,
		framebufs.scene.color_tex,
		framebufs.scene.depth_stencil_tex,
		framebufs.scene.depth_stencil_tex,
		"scene fbo"
	);

	/* resolved scene framebuffer (color only) */
	if (framebufs.scene.samples > 1)
	{
		framebufs.resolved_scene.color_tex = GL_CreateFBOAttachment (color_format, 1, GL_LINEAR, "resolved scene colors");
		framebufs.resolved_scene.fbo = GL_CreateFBO (GL_TEXTURE_2D, framebufs.resolved_scene.color_tex, 0, 0, "resolved scene fbo");
	}
	else
	{
		framebufs.resolved_scene.color_tex = 0;
		framebufs.resolved_scene.fbo = 0;
	}

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
	GL_BindNative (GL_TEXTURE0, GL_TEXTURE_2D, 0);
}

/*
=============
GL_DeleteFrameBuffers
=============
*/
void GL_DeleteFrameBuffers (void)
{
	GL_DeleteFramebuffersFunc (1, &framebufs.resolved_scene.fbo);
	GL_DeleteFramebuffersFunc (1, &framebufs.scene.fbo);
	GL_DeleteFramebuffersFunc (1, &framebufs.composite.fbo);
	GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);

	GL_DeleteNativeTexture (framebufs.resolved_scene.color_tex);
	GL_DeleteNativeTexture (framebufs.scene.depth_stencil_tex);
	GL_DeleteNativeTexture (framebufs.scene.color_tex);
	GL_DeleteNativeTexture (framebufs.composite.color_tex);

	memset (&framebufs, 0, sizeof (framebufs));
}

//==============================================================================
//
// POSTPROCESSING
//
//==============================================================================

extern GLuint gl_palette_lut;
extern GLuint gl_palette_buffer;

/*
=============
GL_PostProcess
=============
*/
void GL_PostProcess (void)
{
	if (vid_gamma.value == 1 && vid_contrast.value == 1 && !softemu)
		return;

	GL_BeginGroup ("Postprocess");

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, 0);
	glViewport (glx, gly, glwidth, glheight);

	GL_UseProgram (glprogs.postprocess[softemu]);
	GL_SetState (GLS_BLEND_OPAQUE | GLS_NO_ZTEST | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(0));
	GL_BindNative (GL_TEXTURE0, GL_TEXTURE_2D, framebufs.composite.color_tex);
	GL_BindNative (GL_TEXTURE1, GL_TEXTURE_3D, gl_palette_lut);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 0, gl_palette_buffer, 0, 256 * sizeof (GLuint));
	GL_Uniform3fFunc (0, vid_gamma.value, q_min(2.0, q_max(1.0, vid_contrast.value)), 1.f/r_refdef.scale);

	glDrawArrays (GL_TRIANGLES, 0, 3);

	GL_EndGroup ();
}

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;
	byte signbits;
	float vec[3];
	for (i = 0;i < 4;i++)
	{
		p = frustum + i;
		signbits = p->signbits;
		vec[0] = ((signbits % 2)<1) ? emaxs[0] : emins[0];
		vec[1] = ((signbits % 4)<2) ? emaxs[1] : emins[1];
		vec[2] = ((signbits % 8)<4) ? emaxs[2] : emins[2];
		if (p->normal[0]*vec[0] + p->normal[1]*vec[1] + p->normal[2]*vec[2] < p->dist)
			return true;
	}
	return false;
}

/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;

	if (e->angles[0] || e->angles[2]) //pitch or roll
	{
		VectorAdd (e->origin, e->model->rmins, mins);
		VectorAdd (e->origin, e->model->rmaxs, maxs);
	}
	else if (e->angles[1]) //yaw
	{
		VectorAdd (e->origin, e->model->ymins, mins);
		VectorAdd (e->origin, e->model->ymaxs, maxs);
	}
	else //no rotation
	{
		VectorAdd (e->origin, e->model->mins, mins);
		VectorAdd (e->origin, e->model->maxs, maxs);
	}

	return R_CullBox (mins, maxs);
}

/*
===============
R_EntityMatrix
===============
*/
#define DEG2RAD( a ) ( (a) * M_PI_DIV_180 )
void R_EntityMatrix (float matrix[16], vec3_t origin, vec3_t angles)
{
	float yaw   = DEG2RAD(angles[YAW]);
	float pitch = angles[PITCH];
	float roll  = angles[ROLL];
	if (pitch == 0.f && roll == 0.f)
	{
		float sy = sin(yaw);
		float cy = cos(yaw);

		// First column
		matrix[ 0] = cy;
		matrix[ 1] = sy;
		matrix[ 2] = 0.f;
		matrix[ 3] = 0.f;

		// Second column
		matrix[ 4] = -sy;
		matrix[ 5] = cy;
		matrix[ 6] = 0.f;
		matrix[ 7] = 0.f;

		// Third column
		matrix[ 8] = 0.f;
		matrix[ 9] = 0.f;
		matrix[10] = 1.f;
		matrix[11] = 0.f;
	}
	else
	{
		float sy, sp, sr, cy, cp, cr;
		pitch = DEG2RAD(pitch);
		roll = DEG2RAD(roll);
		sy = sin(yaw);
		sp = sin(pitch);
		sr = sin(roll);
		cy = cos(yaw);
		cp = cos(pitch);
		cr = cos(roll);

		// https://www.symbolab.com/solver/matrix-multiply-calculator FTW!

		// First column
		matrix[ 0] = cy*cp;
		matrix[ 1] = sy*cp;
		matrix[ 2] = sp;
		matrix[ 3] = 0.f;

		// Second column
		matrix[ 4] = -cy*sp*sr - cr*sy;
		matrix[ 5] = cr*cy - sy*sp*sr;
		matrix[ 6] = cp*sr;
		matrix[ 7] = 0.f;

		// Third column
		matrix[ 8] = sy*sr - cr*cy*sp;
		matrix[ 9] = -cy*sr - cr*sy*sp;
		matrix[10] = cr*cp;
		matrix[11] = 0.f;
	}

	// Fourth column
	matrix[12] = origin[0];
	matrix[13] = origin[1];
	matrix[14] = origin[2];
	matrix[15] = 1.f;
}

/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
	if (gl_clipcontrol_able)
		offset = -offset;

	if (offset > 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(1, offset);
	}
	else if (offset < 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1, offset);
	}
	else
	{
		glDisable (GL_POLYGON_OFFSET_FILL);
		glDisable (GL_POLYGON_OFFSET_LINE);
	}
}

/*
=============
GL_DepthRange

Wrapper around glDepthRange that handles clip control/reversed Z differences
=============
*/
void GL_DepthRange (zrange_t range)
{
	switch (range)
	{
	default:
	case ZRANGE_FULL:
		glDepthRange (0.f, 1.f);
		break;

	case ZRANGE_VIEWMODEL:
		if (gl_clipcontrol_able)
			glDepthRange (0.7f, 1.f);
		else
			glDepthRange (0.f, 0.3f);
		break;

	case ZRANGE_NEAR:
		if (gl_clipcontrol_able)
			glDepthRange (1.f, 1.f);
		else
			glDepthRange (0.f, 0.f);
		break;
	}
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

static unsigned short visedict_keys[MAX_VISEDICTS];
static unsigned short visedict_order[2][MAX_VISEDICTS];
static entity_t *cl_sorted_visedicts[MAX_VISEDICTS + 1]; // +1 for worldspawn
static int cl_modtype_ofs[mod_numtypes*2 + 1]; // x2: opaque/translucent; +1: total in last slot

/*
=============
R_SortEntities
=============
*/
static void R_SortEntities (void)
{
	int i, j, pass;
	int bins[256];
	int typebins[mod_numtypes*2];

	if (!r_drawentities.value)
		cl_numvisedicts = 0;

	// remove entities with no or invisible models
	for (i = 0, j = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[i];
		if (!ent->model || ent->alpha == ENTALPHA_ZERO)
			continue;
		if (ent->model->type == mod_brush && R_CullModelForEntity (ent))
			continue;
		cl_visedicts[j++] = ent;
	}
	cl_numvisedicts = j;

	memset (typebins, 0, sizeof(typebins));
	if (r_drawworld.value)
		typebins[mod_brush * 2 + 0]++; // count worldspawn

	// fill entity sort key array, initial order, and per-type counts
	for (i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[i];
		qboolean translucent = !ENTALPHA_OPAQUE (ent->alpha);
		if (ent->model->type == mod_alias)
			visedict_keys[i] = ent->model->sortkey | (ent->skinnum & MODSORT_FRAMEMASK);
		else
			visedict_keys[i] = ent->model->sortkey | (ent->frame & MODSORT_FRAMEMASK);

		if ((unsigned)ent->model->type >= (unsigned)mod_numtypes)
			Sys_Error ("Model '%s' has invalid type %d", ent->model->name, ent->model->type);
		typebins[ent->model->type * 2 + translucent]++;

		visedict_order[0][i] = i;
	}

	// convert typebin counts into offsets
	for (i = 0, j = 0; i < countof(typebins); i++)
	{
		int tmp = typebins[i];
		cl_modtype_ofs[i] = typebins[i] = j;
		j += tmp;
	}
	cl_modtype_ofs[i] = j;

	// LSD-first radix sort: 2 passes x 8 bits
	for (pass = 0; pass < 2; pass++)
	{
		unsigned short *src = visedict_order[pass];
		unsigned short *dst = visedict_order[pass ^ 1];
		int shift = pass * 8;
		int sum;

		// count number of entries in each bin
		memset (bins, 0, sizeof(bins));
		for (i = 0; i < cl_numvisedicts; i++)
			bins[(visedict_keys[i] >> shift) & 255]++;

		// turn bin counts into offsets
		sum = 0;
		for (i = 0; i < 256; i++)
		{
			int tmp = bins[i];
			bins[i] = sum;
			sum += tmp;
		}

		// reorder
		for (i = 0; i < cl_numvisedicts; i++)
			dst[bins[(visedict_keys[src[i]] >> shift) & 255]++] = src[i];
	}

	// write sorted list
	if (r_drawworld.value)
		cl_sorted_visedicts[typebins[mod_brush * 2 + 0]++] = &cl_entities[0]; // add the world as the first brush entity
	for (i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *ent = cl_visedicts[visedict_order[0][i]];
		qboolean translucent = !ENTALPHA_OPAQUE (ent->alpha);
		cl_sorted_visedicts[typebins[ent->model->type * 2 + translucent]++] = ent;
	}
}

int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j=0 ; j<3 ; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
=============
GL_FrustumMatrix
=============
*/
#define NEARCLIP 4
static void GL_FrustumMatrix(float matrix[16], float fovx, float fovy)
{
	const float w = 1.0f / tanf(fovx * 0.5f);
	const float h = 1.0f / tanf(fovy * 0.5f);

	// reduce near clip distance at high FOV's to avoid seeing through walls
	const float d = 12.f * q_min(w, h);
	const float n = CLAMP(0.5f, d, NEARCLIP);
	const float f = gl_farclip.value;

	memset(matrix, 0, 16 * sizeof(float));

	if (gl_clipcontrol_able)
	{
		// reversed Z projection matrix with the coordinate system conversion baked in
		matrix[0*4 + 2] = -n / (f - n);
		matrix[0*4 + 3] = 1.f;
		matrix[1*4 + 0] = -w;
		matrix[2*4 + 1] = h;
		matrix[3*4 + 2] = f * n / (f - n);
	}
	else
	{
		// standard projection matrix with the coordinate system conversion baked in
		matrix[0*4 + 2] = (f + n) / (f - n);
		matrix[0*4 + 3] = 1.f;
		matrix[1*4 + 0] = -w;
		matrix[2*4 + 1] = h;
		matrix[3*4 + 2] = -2.f * f * n / (f - n);
	}
}

/*
===============
ExtractFrustumPlane

Extracts the normalized frustum plane from the given view-projection matrix
that corresponds to a value of 'ndcval' on the 'axis' axis in NDC space.
===============
*/
void ExtractFrustumPlane (float mvp[16], int axis, float ndcval, qboolean flip, mplane_t *out)
{
	float scale;
	out->normal[0] =  (mvp[0*4 + axis] - ndcval * mvp[0*4 + 3]);
	out->normal[1] =  (mvp[1*4 + axis] - ndcval * mvp[1*4 + 3]);
	out->normal[2] =  (mvp[2*4 + axis] - ndcval * mvp[2*4 + 3]);
	out->dist      = -(mvp[3*4 + axis] - ndcval * mvp[3*4 + 3]);

	scale = (flip ? -1.f : 1.f) / sqrtf (DotProduct (out->normal, out->normal));
	out->normal[0] *= scale;
	out->normal[1] *= scale;
	out->normal[2] *= scale;
	out->dist      *= scale;

	out->type      = PLANE_ANYZ;
	out->signbits  = SignbitsForPlane (out);
}

/*
===============
R_SetFrustum
===============
*/
void R_SetFrustum (void)
{
	float logznear, logzfar;
	float translation[16];
	float rotation[16];
	GL_FrustumMatrix(r_matproj, DEG2RAD(r_fovx), DEG2RAD(r_fovy));

	// View matrix
	RotationMatrix(r_matview, DEG2RAD(-r_refdef.viewangles[ROLL]), 0);
	RotationMatrix(rotation, DEG2RAD(-r_refdef.viewangles[PITCH]), 1);
	MatrixMultiply(r_matview, rotation);
	RotationMatrix(rotation, DEG2RAD(-r_refdef.viewangles[YAW]), 2);
	MatrixMultiply(r_matview, rotation);

	TranslationMatrix(translation, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
	MatrixMultiply(r_matview, translation);

	// View projection matrix
	memcpy(r_matviewproj, r_matproj, 16 * sizeof(float));
	MatrixMultiply(r_matviewproj, r_matview);

	ExtractFrustumPlane (r_matviewproj, 0,  1.f, true,  &frustum[0]); // right
	ExtractFrustumPlane (r_matviewproj, 0, -1.f, false, &frustum[1]); // left
	ExtractFrustumPlane (r_matviewproj, 1, -1.f, false, &frustum[2]); // bottom
	ExtractFrustumPlane (r_matviewproj, 1,  1.f, true,  &frustum[3]); // top

	logznear = log2f (NEARCLIP);
	logzfar = log2f (gl_farclip.value);
	memcpy (r_framedata.global.viewproj, r_matviewproj, 16 * sizeof (float));
	r_framedata.global.zlogscale = LIGHT_TILES_Z / (logzfar - logznear);
	r_framedata.global.zlogbias = -r_framedata.global.zlogscale * logznear;
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	GL_BindFramebufferFunc (GL_FRAMEBUFFER, framebufs.scene.fbo);
	glViewport (0, 0, r_refdef.vrect.width / r_refdef.scale, r_refdef.vrect.height / r_refdef.scale);
}

/*
=============
R_Clear -- johnfitz -- rewritten and gutted
=============
*/
void R_Clear (void)
{
	GLbitfield clearbits = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	if (gl_clear.value)
		clearbits |= GL_COLOR_BUFFER_BIT;

	GL_SetState (glstate & ~GLS_NO_ZWRITE); // make sure depth writes are enabled
	glClear (clearbits);
}

/*
===============
R_SetupScene -- johnfitz -- this is the stuff that needs to be done once per eye in stereo mode
===============
*/
void R_SetupScene (void)
{
	R_SetupGL ();
}

/*
===============
R_UploadFrameData
===============
*/
void R_UploadFrameData (void)
{
	GLuint	buf;
	GLbyte	*ofs;
	size_t	size;

	size = sizeof(r_framedata.global) + sizeof(r_framedata.lights[0]) * q_max (r_framedata.global.numlights, 1); // avoid zero-length array
	GL_Upload (GL_SHADER_STORAGE_BUFFER, &r_framedata, size, &buf, &ofs);
	GL_BindBufferRange (GL_SHADER_STORAGE_BUFFER, 0, buf, (GLintptr)ofs, size);
}

/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per frame, even in stereo mode
===============
*/
void R_SetupView (void)
{
	R_AnimateLight ();

	r_framecount++;
	r_framedata.global.time = cl.time;
	r_framedata.global.eyepos[0] = r_refdef.vieworg[0];
	r_framedata.global.eyepos[1] = r_refdef.vieworg[1];
	r_framedata.global.eyepos[2] = r_refdef.vieworg[2];

	Fog_SetupFrame (); //johnfitz

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	//johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	water_warp = false;
	if (r_waterwarp.value)
	{
		int contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			if (r_waterwarp.value > 1.f)
			{
				//variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
				r_fovx = atan(tan(DEG2RAD(r_refdef.fov_x) / 2) * (0.97 + sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
				r_fovy = atan(tan(DEG2RAD(r_refdef.fov_y) / 2) * (1.03 - sin(cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			}
			else
			{
				water_warp = true;
			}
		}
	}
	//johnfitz

	R_SetFrustum ();

	R_MarkSurfaces (); //johnfitz -- create texture chains from PVS

	R_SortEntities ();

	R_PushDlights ();

	//johnfitz -- cheat-protect some draw modes
	r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value) r_drawworld_cheatsafe = false;

		if (r_fullbright.value || !cl.worldmodel->lightdata) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
	//johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_GetVisEntities
=============
*/
entity_t **R_GetVisEntities (modtype_t type, qboolean translucent, int *outcount)
{
	entity_t **entlist = cl_sorted_visedicts;
	int *ofs = cl_modtype_ofs + type * 2 + (translucent ? 1 : 0);
	*outcount = ofs[1] - ofs[0];
	return entlist + ofs[0];
}

/*
=============
R_DrawWater
=============
*/
static void R_DrawWater (void)
{
	entity_t **entlist = cl_sorted_visedicts;
	int *ofs = cl_modtype_ofs + 2 * mod_brush;

	// only opaque entities can have opaque water
	R_DrawBrushModels_Water (entlist + ofs[0], ofs[1] - ofs[0], false);

	// all entities can have translucent water
	R_DrawBrushModels_Water (entlist + ofs[0], ofs[2] - ofs[0], true);
}

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (qboolean alphapass) //johnfitz -- added parameter
{
	int		*ofs;
	entity_t **entlist = cl_sorted_visedicts;

	GL_BeginGroup (alphapass ? "Translucent entities" : "Opaque entities");

	ofs = cl_modtype_ofs + (alphapass ? 1 : 0);
	R_DrawBrushModels  (entlist + ofs[2*mod_brush ], ofs[2*mod_brush +1] - ofs[2*mod_brush ]);
	R_DrawAliasModels  (entlist + ofs[2*mod_alias ], ofs[2*mod_alias +1] - ofs[2*mod_alias ]);
	R_DrawSpriteModels (entlist + ofs[2*mod_sprite], ofs[2*mod_sprite+1] - ofs[2*mod_sprite]);

	GL_EndGroup ();
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (void)
{
	entity_t *e = &cl.viewent;
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	if (!e->model)
		return;

	//johnfitz -- this fixes a crash
	if (e->model->type != mod_alias)
		return;
	//johnfitz

	GL_BeginGroup ("View model");

	// hack the depth range to prevent view model from poking into walls
	GL_DepthRange (ZRANGE_VIEWMODEL);
	R_DrawAliasModels (&e, 1);
	GL_DepthRange (ZRANGE_FULL);

	GL_EndGroup ();
}

/*
================
R_ShowTris -- johnfitz
================
*/
void R_ShowTris (void)
{
	extern cvar_t r_particles;
	int		*ofs;
	entity_t **entlist = cl_sorted_visedicts;
	entity_t *e;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1)
		return;

	GL_BeginGroup ("Show tris");

	Fog_DisableGFog (); //johnfitz
	R_UploadFrameData ();

	if (r_showtris.value == 1)
		GL_DepthRange (ZRANGE_NEAR);
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);

	ofs = cl_modtype_ofs;
	R_DrawBrushModels_ShowTris  (entlist + ofs[2*mod_brush ], ofs[2*mod_brush +2] - ofs[2*mod_brush ]);
	R_DrawAliasModels_ShowTris  (entlist + ofs[2*mod_alias ], ofs[2*mod_alias +2] - ofs[2*mod_alias ]);
	R_DrawSpriteModels_ShowTris (entlist + ofs[2*mod_sprite], ofs[2*mod_sprite+2] - ofs[2*mod_sprite]);

	// viewmodel
	e = &cl.viewent;
	if (r_drawviewmodel.value
		&& !chase_active.value
		&& cl.stats[STAT_HEALTH] > 0
		&& !(cl.items & IT_INVISIBILITY)
		&& e->model
		&& e->model->type == mod_alias)
	{
		if (r_showtris.value != 1.f)
			GL_DepthRange (ZRANGE_VIEWMODEL);

		R_DrawAliasModels_ShowTris (&e, 1);

		GL_DepthRange (ZRANGE_FULL);
	}

	R_DrawParticles_ShowTris ();

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset (OFFSET_NONE);
	if (r_showtris.value == 1)
		GL_DepthRange (ZRANGE_FULL);

	Sbar_Changed (); //so we don't get dots collecting on the statusbar

	GL_EndGroup ();
}

/*
================
R_RenderScene
================
*/
void R_RenderScene (void)
{
	R_SetupScene (); //johnfitz -- this does everything that should be done once per call to RenderScene

	R_Clear ();

	Fog_EnableGFog (); //johnfitz

	R_DrawViewModel (); //johnfitz -- moved here from R_RenderView

	S_ExtraUpdate (); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList (false); //johnfitz -- false means this is the pass for nonalpha entities

	Sky_DrawSky (); //johnfitz

	R_DrawWater ();

	R_DrawEntitiesOnList (true); //johnfitz -- true means this is the pass for alpha entities

	R_DrawParticles ();

	R_ShowTris (); //johnfitz
}

/*
================
R_WarpScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill 
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_WarpScaleView (void)
{
	int srcx, srcy, srcw, srch;
	qboolean postprocess = vid_gamma.value != 1.f || vid_contrast.value != 1.f || softemu;
	qboolean msaa = framebufs.scene.samples > 1;

	// copied from R_SetupGL()
	srcx = glx + r_refdef.vrect.x;
	srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	srcw = r_refdef.vrect.width / r_refdef.scale;
	srch = r_refdef.vrect.height / r_refdef.scale;

	if (msaa)
	{
		GL_BeginGroup ("MSAA resolve");
		GL_BindFramebufferFunc (GL_READ_FRAMEBUFFER, framebufs.scene.fbo);
		GL_BindFramebufferFunc (GL_DRAW_FRAMEBUFFER, framebufs.resolved_scene.fbo);
		GL_BlitFramebufferFunc (0, 0, srcw, srch, 0, 0, srcw, srch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		GL_EndGroup ();
	}

	GL_BeginGroup ("Warp/scale view");

	GL_BindFramebufferFunc (GL_FRAMEBUFFER, postprocess ? framebufs.composite.fbo : 0);
	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	if (water_warp || (v_blend[3] && gl_polyblend.value))
	{
		float smax = srcw/(float)vid.width;
		float tmax = srch/(float)vid.height;

		GL_UseProgram (glprogs.warpscale[water_warp]);
		GL_SetState (GLS_BLEND_OPAQUE | GLS_NO_ZTEST | GLS_NO_ZWRITE | GLS_CULL_NONE | GLS_ATTRIBS(0));

		GL_Uniform4fFunc (0, smax, tmax, water_warp ? 1.f/256.f : 0.f, cl.time);
		if (v_blend[3] && gl_polyblend.value)
			GL_Uniform4fvFunc (1, 1, v_blend);
		else
			GL_Uniform4fFunc (1, 0.f, 0.f, 0.f, 0.f);
		GL_BindNative (GL_TEXTURE0, GL_TEXTURE_2D, msaa ? framebufs.resolved_scene.color_tex : framebufs.scene.color_tex);
		glDrawArrays (GL_TRIANGLES, 0, 3);
	}
	else
	{
		GL_BindFramebufferFunc (GL_READ_FRAMEBUFFER, msaa ? framebufs.resolved_scene.fbo : framebufs.scene.fbo);
		GL_BlitFramebufferFunc (0, 0, srcw, srch, srcx, srcy, srcx + r_refdef.vrect.width, srcy + r_refdef.vrect.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}

	GL_EndGroup ();
}

/*
================
R_RenderView
================
*/
void R_RenderView (void)
{
	double	time1, time2;

	if (r_norefresh.value)
		return;

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	time1 = 0; /* avoid compiler warning */
	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_DoubleTime ();

		//johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_megatexels =
		rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	R_SetupView (); //johnfitz -- this does everything that should be done once per frame
	R_RenderScene ();
	R_WarpScaleView ();

	//johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();
	if (r_pos.value)
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int)cl_entities[cl.viewentity].origin[0],
			(int)cl_entities[cl.viewentity].origin[1],
			(int)cl_entities[cl.viewentity].origin[2],
			(int)cl.viewangles[PITCH],
			(int)cl.viewangles[YAW],
			(int)cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_brushpasses,
					rs_aliaspolys,
					rs_aliaspasses,
					rs_dynamiclightmaps,
					rs_skypolys,
					rs_skypasses,
					TexMgr_FrameUsage ());
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
					(int)((time2-time1)*1000),
					rs_brushpolys,
					rs_aliaspolys,
					rs_dynamiclightmaps);
	//johnfitz
}

