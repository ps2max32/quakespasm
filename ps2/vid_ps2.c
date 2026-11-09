/* vid_ps2.c: Quakespasm's video layer on maxsdk (gl_vidsdl.c of the SDL
   build): the screen is libgl's (libs/libgl): the TV's (NTSC 640 x 448,
   PAL 640 x 512) or 1080i (vid_mode 1), with or without beam racing
   (vid_beam 1), in 16 or 24-bit colour (vid_rgb24 1: beam racing in the
   TV's mode only); the Video Options menu sets them. libgl's extensions
   choose the renderer's paths (GL_CheckExtensions): multitexture (two
   units), texture_env_combine (the overbright lightmaps in one pass),
   vertex buffers; no GLSL (Quakespasm's fixed-function paths). */

#include <mx/mx.h>
#include <mxgl.h>

#include "quakedef.h"
#include "cfgfile.h"

void SCR_Conwidth_f(cvar_t *var);

#define WARP_WIDTH 320
#define WARP_HEIGHT 200

static const char *gl_vendor;
static const char *gl_renderer;
static const char *gl_version;
static const char *gl_extensions;

viddef_t vid;
modestate_t modestate = MS_UNINIT;
qboolean scr_skipupdate;

qboolean gl_mtexable = false;
qboolean gl_packed_pixels = false;
qboolean gl_texture_env_combine = false;
qboolean gl_texture_env_add = false;
qboolean gl_swap_control = false;
qboolean gl_anisotropy_able = false;
float gl_max_anisotropy = 1;
qboolean gl_texture_NPOT = false;
qboolean gl_vbo_able = false;
qboolean gl_glsl_able = false;
GLint gl_max_texture_units = 0;
qboolean gl_glsl_gamma_able = false;
qboolean gl_glsl_alias_able = false;
int gl_stencilbits;

PFNGLMULTITEXCOORD2FARBPROC GL_MTexCoord2fFunc = NULL;
PFNGLACTIVETEXTUREARBPROC GL_SelectTextureFunc = NULL;
PFNGLCLIENTACTIVETEXTUREARBPROC GL_ClientActiveTextureFunc = NULL;
PFNGLBINDBUFFERARBPROC GL_BindBufferFunc = NULL;
PFNGLBUFFERDATAARBPROC GL_BufferDataFunc = NULL;
PFNGLBUFFERSUBDATAARBPROC GL_BufferSubDataFunc = NULL;
PFNGLDELETEBUFFERSARBPROC GL_DeleteBuffersFunc = NULL;
PFNGLGENBUFFERSARBPROC GL_GenBuffersFunc = NULL;

QS_PFNGLCREATESHADERPROC GL_CreateShaderFunc = NULL;
QS_PFNGLDELETESHADERPROC GL_DeleteShaderFunc = NULL;
QS_PFNGLDELETEPROGRAMPROC GL_DeleteProgramFunc = NULL;
QS_PFNGLSHADERSOURCEPROC GL_ShaderSourceFunc = NULL;
QS_PFNGLCOMPILESHADERPROC GL_CompileShaderFunc = NULL;
QS_PFNGLGETSHADERIVPROC GL_GetShaderivFunc = NULL;
QS_PFNGLGETSHADERINFOLOGPROC GL_GetShaderInfoLogFunc = NULL;
QS_PFNGLGETPROGRAMIVPROC GL_GetProgramivFunc = NULL;
QS_PFNGLGETPROGRAMINFOLOGPROC GL_GetProgramInfoLogFunc = NULL;
QS_PFNGLCREATEPROGRAMPROC GL_CreateProgramFunc = NULL;
QS_PFNGLATTACHSHADERPROC GL_AttachShaderFunc = NULL;
QS_PFNGLLINKPROGRAMPROC GL_LinkProgramFunc = NULL;
QS_PFNGLBINDATTRIBLOCATIONFUNC GL_BindAttribLocationFunc = NULL;
QS_PFNGLUSEPROGRAMPROC GL_UseProgramFunc = NULL;
QS_PFNGLGETATTRIBLOCATIONPROC GL_GetAttribLocationFunc = NULL;
QS_PFNGLVERTEXATTRIBPOINTERPROC GL_VertexAttribPointerFunc = NULL;
QS_PFNGLENABLEVERTEXATTRIBARRAYPROC GL_EnableVertexAttribArrayFunc = NULL;
QS_PFNGLDISABLEVERTEXATTRIBARRAYPROC GL_DisableVertexAttribArrayFunc = NULL;
QS_PFNGLGETUNIFORMLOCATIONPROC GL_GetUniformLocationFunc = NULL;
QS_PFNGLUNIFORM1IPROC GL_Uniform1iFunc = NULL;
QS_PFNGLUNIFORM1FPROC GL_Uniform1fFunc = NULL;
QS_PFNGLUNIFORM3FPROC GL_Uniform3fFunc = NULL;
QS_PFNGLUNIFORM4FPROC GL_Uniform4fFunc = NULL;
QS_PFNGENERATEMIPMAP GL_GenerateMipmap = NULL;

/* no hardware gamma on the GS: gamma and contrast are kept for the menus
   and the config, the picture is as at 1 */
cvar_t vid_gamma = {"gamma", "1", CVAR_ARCHIVE};
cvar_t vid_contrast = {"contrast", "1", CVAR_ARCHIVE};

/* 0: the TV's mode (NTSC or PAL, the console's), 1: 1080i (not kept: a
   TV without 1080i would show nothing at the next start) */
static cvar_t vid_mode = {"vid_mode", "0", CVAR_NONE};
/* beam racing (libgl's MXGL_BEAM): one buffer drawn just ahead of the beam,
   which leaves GS memory for twice the textures; at most a frame a field */
static cvar_t vid_beam = {"vid_beam", "0", CVAR_ARCHIVE};
/* 24-bit colour (MXGL_RGB24), with beam racing in the TV's mode */
static cvar_t vid_rgb24 = {"vid_rgb24", "0", CVAR_ARCHIVE};
/* the textures' top levels left out: -1 only while GS memory overflows or
   the swap file is read (MXGL_TEX_AUTO, the default); 0, 1, 2 always */
static cvar_t gl_ps2texquality = {"gl_ps2texquality", "-1", CVAR_ARCHIVE};
/* its limits (mxgl_texture_limits): GS memory high and low (% of the
   room), swap-ins a second */
static cvar_t gl_ps2texlimits = {"gl_ps2texlimits", "120 80 30", CVAR_ARCHIVE};

static qboolean vid_locked;

void *VID_GetWindow(void) { return NULL; }
qboolean VID_HasMouseOrInputFocus(void) { return true; }
qboolean VID_IsMinimized(void) { return false; }
/* default.cfg's vid_unlock: the config's vid_* are read in VID_Init */
static void VID_Unlock(void) {}
void VID_Lock(void) { vid_locked = true; }
void VID_Toggle(void) {}
void VID_SyncCvars(void) {}
void VID_Shutdown(void) {}

/* ---- GL ------------------------------------------------------------------ */

static qboolean GL_ParseExtensionList(const char *list, const char *name)
{
	size_t n = strlen(name);
	for (const char *p = list; p && (p = strstr(p, name)); p += n)
		if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n]))
			return true;
	return false;
}

#define PROC(name) mxglGetProcAddress(name)

static void GL_CheckExtensions(void)
{
	if (!COM_CheckParm("-novbo") && GL_ParseExtensionList(gl_extensions, "GL_ARB_vertex_buffer_object")) {
		GL_BindBufferFunc = (PFNGLBINDBUFFERARBPROC)PROC("glBindBufferARB");
		GL_BufferDataFunc = (PFNGLBUFFERDATAARBPROC)PROC("glBufferDataARB");
		GL_BufferSubDataFunc = (PFNGLBUFFERSUBDATAARBPROC)PROC("glBufferSubDataARB");
		GL_DeleteBuffersFunc = (PFNGLDELETEBUFFERSARBPROC)PROC("glDeleteBuffersARB");
		GL_GenBuffersFunc = (PFNGLGENBUFFERSARBPROC)PROC("glGenBuffersARB");
		gl_vbo_able = GL_BindBufferFunc && GL_BufferDataFunc && GL_BufferSubDataFunc &&
		              GL_DeleteBuffersFunc && GL_GenBuffersFunc;
	}
	Con_Printf("vertex buffer objects: %s\n", gl_vbo_able ? "yes" : "no");

	if (!COM_CheckParm("-nomtex") && GL_ParseExtensionList(gl_extensions, "GL_ARB_multitexture")) {
		GL_MTexCoord2fFunc = (PFNGLMULTITEXCOORD2FARBPROC)PROC("glMultiTexCoord2fARB");
		GL_SelectTextureFunc = (PFNGLACTIVETEXTUREARBPROC)PROC("glActiveTextureARB");
		GL_ClientActiveTextureFunc = (PFNGLCLIENTACTIVETEXTUREARBPROC)PROC("glClientActiveTextureARB");
		if (GL_MTexCoord2fFunc && GL_SelectTextureFunc && GL_ClientActiveTextureFunc) {
			glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_max_texture_units);
			gl_mtexable = gl_max_texture_units >= 2;
		}
	}
	Con_Printf("multitexture: %s (%d units)\n", gl_mtexable ? "yes" : "no", (int)gl_max_texture_units);

	if (!COM_CheckParm("-nocombine") && (GL_ParseExtensionList(gl_extensions, "GL_ARB_texture_env_combine") ||
	                                     GL_ParseExtensionList(gl_extensions, "GL_EXT_texture_env_combine")))
		gl_texture_env_combine = true;
	if (!COM_CheckParm("-noadd") && (GL_ParseExtensionList(gl_extensions, "GL_ARB_texture_env_add") ||
	                                 GL_ParseExtensionList(gl_extensions, "GL_EXT_texture_env_add")))
		gl_texture_env_add = true;
	Con_Printf("texture_env_combine: %s, texture_env_add: %s\n", gl_texture_env_combine ? "yes" : "no",
	           gl_texture_env_add ? "yes" : "no");
	if (GL_ParseExtensionList(gl_extensions, "GL_ARB_texture_non_power_of_two"))
		gl_texture_NPOT = true;
	glGetIntegerv(GL_STENCIL_BITS, &gl_stencilbits);
}

static void GL_SetupState(void)
{
	glClearColor(0.15, 0.15, 0.15, 0);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel(GL_FLAT);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glDepthRange(0, 1);
	glDepthFunc(GL_LEQUAL);
}

static void GL_Info_f(void)
{
	Con_SafePrintf("GL_VENDOR: %s\n", gl_vendor);
	Con_SafePrintf("GL_RENDERER: %s\n", gl_renderer);
	Con_SafePrintf("GL_VERSION: %s\n", gl_version);
	Con_Printf("GL_EXTENSIONS: %s\n", gl_extensions);
}

static void GL_Init(void)
{
	gl_vendor = (const char *)glGetString(GL_VENDOR);
	gl_renderer = (const char *)glGetString(GL_RENDERER);
	gl_version = (const char *)glGetString(GL_VERSION);
	gl_extensions = (const char *)glGetString(GL_EXTENSIONS);
	Con_SafePrintf("GL_VENDOR: %s\n", gl_vendor);
	Con_SafePrintf("GL_RENDERER: %s\n", gl_renderer);
	Con_SafePrintf("GL_VERSION: %s\n", gl_version);
	GL_CheckExtensions();
	GLAlias_CreateShaders();
	GLWorld_CreateShaders();
	GL_ClearBufferBindings();
}

/* the shared palette (GL_EXT_shared_texture_palette): libgl keeps the
   textures whose texels are all of it as 8-bit indices, index 255 (alpha
   0) the transparent one */
static void VID_SharedPalette(void)
{
	if (!GL_ParseExtensionList(gl_extensions, "GL_EXT_shared_texture_palette"))
		return;
	byte *rgb = COM_LoadMallocFile("gfx/palette.lmp", NULL);
	if (!rgb)
		return;
	static byte pal[256 * 4];
	for (int i = 0; i < 256; i++) {
		pal[i * 4 + 0] = rgb[i * 3 + 0];
		pal[i * 4 + 1] = rgb[i * 3 + 1];
		pal[i * 4 + 2] = rgb[i * 3 + 2];
		pal[i * 4 + 3] = i == 255 ? 0 : 255;
	}
	free(rgb);
	glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, pal);
	glEnable(GL_SHARED_TEXTURE_PALETTE_EXT);
}

/* ---- the mode -------------------------------------------------------------- */

/* the mode of vid_mode, vid_beam, vid_rgb24; the 2D (console, status bar,
   menus) in 640 wide pixels (scr_conwidth / scr_conscale make it other),
   the 3D in all of the screen */
static int vid_cur = -1;
static void VID_ApplyMode(void)
{
	int hd = vid_mode.value == 1;
	int flags = (vid_beam.value ? MXGL_BEAM : 0) | (vid_rgb24.value ? MXGL_RGB24 : 0);
	int want = (hd ? MXGL_1080I : MXGL_SD) | flags;
	if (want == vid_cur)
		return;
	vid_cur = want;
	if (mxgl_set_mode(want) < 0) {
		Con_Printf("Beam racing: not enough memory\n");
		Cvar_SetValueQuick(&vid_beam, 0);
		vid_cur &= ~MXGL_BEAM;
	}
	int w, h;
	mxgl_screen(&w, &h);
	vid.width = w;
	vid.height = h;
	if (host_initialized)
		SCR_Conwidth_f(NULL);
	else {
		vid.conwidth = 640;
		vid.conheight = vid.conwidth * vid.height / vid.width;
	}
	vid.numpages = 2;
	vid.recalc_refdef = 1;
	modestate = MS_FULLSCREEN;
	flags = mxgl_mode_flags();
	Con_SafePrintf("Video mode %dx%d (%s)%s%s\n", w, h, hd ? "1080i" : mxgl_mode() == MXGL_PAL ? "PAL" : "NTSC",
	               flags & MXGL_BEAM ? ", beam racing" : "", flags & MXGL_RGB24 ? ", 24-bit colour" : "");
}

void GL_BeginRendering(int *x, int *y, int *width, int *height)
{
	VID_ApplyMode();
	static int q = -2;
	if ((int)gl_ps2texquality.value != q) {
		mxgl_texture_quality(q = (int)gl_ps2texquality.value);
		int hi = 120, lo = 80, sw = 30;
		sscanf(gl_ps2texlimits.string, "%d%*c%d%*c%d", &hi, &lo, &sw);
		mxgl_texture_limits(hi, lo, sw);
	}
	*x = *y = 0;
	*width = vid.width;
	*height = vid.height;
}

void GL_EndRendering(void)
{
	mxgl_end_frame();
	mxgl_frame_done();
}

/* ---- the Video Options menu: resolution, beam racing, colour; enter or
   left / right changes the line's setting, which is set at once ---- */

static int vid_cursor;
#define VID_LINES 3

static void VID_Menu_f(void)
{
	key_dest = key_menu;
	m_state = m_video;
	m_entersound = true;
}

static void VID_MenuDraw(void)
{
	qpic_t *p = Draw_CachePic("gfx/vidmodes.lmp");
	M_DrawPic((320 - p->width) / 2, 4, p);
	int flags = mxgl_mode_flags();
	M_Print(16, 64, "        Resolution");
	M_Print(168, 64, vid_mode.value == 1 ? "1080i (1920x1080)" : "TV (NTSC/PAL)");
	M_Print(16, 72, "       Beam racing");
	M_Print(168, 72, flags & MXGL_BEAM ? "on" : "off");
	M_Print(16, 80, "            Colour");
	M_Print(168, 80, flags & MXGL_RGB24 ? "24-bit" : vid_rgb24.value ? "16 (24: TV+beam)" : "16-bit");
	M_DrawCharacter(152, 64 + 8 * vid_cursor, 12 + ((int)(realtime * 4) & 1));
	M_Print(16, 104, "Beam racing: more room for textures,");
	M_Print(16, 112, "at most 60 (PAL 50) frames a second");
	M_Print(16, 128, "1080i needs a TV that takes it");
	M_Print(16, 136, "(component cable)");
}

static void VID_MenuKey(int key)
{
	switch (key) {
	case K_ESCAPE:
	case K_BBUTTON:
		S_LocalSound("misc/menu1.wav");
		M_Menu_Options_f();
		break;
	case K_UPARROW:
		S_LocalSound("misc/menu1.wav");
		vid_cursor = (vid_cursor + VID_LINES - 1) % VID_LINES;
		break;
	case K_DOWNARROW:
		S_LocalSound("misc/menu1.wav");
		vid_cursor = (vid_cursor + 1) % VID_LINES;
		break;
	case K_LEFTARROW:
	case K_RIGHTARROW:
	case K_ENTER:
	case K_ABUTTON:
		S_LocalSound("misc/menu2.wav");
		if (vid_cursor == 0)
			Cvar_SetValueQuick(&vid_mode, vid_mode.value != 1);
		else if (vid_cursor == 1)
			Cvar_SetValueQuick(&vid_beam, !vid_beam.value);
		else
			Cvar_SetValueQuick(&vid_rgb24, !vid_rgb24.value);
		VID_ApplyMode();
		break;
	}
}

/* ---- init ------------------------------------------------------------------ */

void VID_Init(void)
{
	Cvar_RegisterVariable(&vid_mode);
	Cvar_RegisterVariable(&vid_beam);
	Cvar_RegisterVariable(&vid_rgb24);
	Cvar_RegisterVariable(&gl_ps2texquality);
	Cvar_RegisterVariable(&gl_ps2texlimits);
	Cvar_RegisterVariable(&vid_gamma);
	Cvar_RegisterVariable(&vid_contrast);

	/* the config's video settings now (the SDL build reads its vid_*
	   the same way, before the window) */
	const char *read_vars[] = {"vid_beam", "vid_rgb24"};
	if (CFG_OpenConfig("config.cfg") == 0) {
		CFG_ReadCvars(read_vars, Q_COUNTOF(read_vars));
		CFG_CloseConfig();
	}
	CFG_ReadCvarOverrides(read_vars, Q_COUNTOF(read_vars));

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

	mxgl_init();
	VID_ApplyMode();
	GL_Init();
	GL_SetupState();
	VID_SharedPalette();
	Cmd_AddCommand("gl_info", GL_Info_f);
	Cmd_AddCommand("vid_unlock", VID_Unlock);

	vid_menucmdfn = VID_Menu_f;
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;
	vid_locked = true;
}
