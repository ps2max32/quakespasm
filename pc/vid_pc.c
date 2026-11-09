/* vid_pc.c: the video of the PC build (pc/): ps2/vid_ps2.c's GL set-up on
   the host's OpenGL, drawn off screen (EGL pbuffer: no window, no display;
   LIBGL_ALWAYS_SOFTWARE=1 for Mesa's llvmpipe) in the PS2's PAL screen,
   640 x 512. The renderer's paths are the PS2's: libgl's extensions
   (vertex buffers, two texture units, texture_env_combine, no
   texture_env_add, no GLSL, no NPOT), whatever the host's GL has; its
   texture size libgl's 256 (glGetIntegerv, wrapped: Makefile). */

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "quakedef.h"

#define WARP_WIDTH 320
#define WARP_HEIGHT 200
#define PC_W 640
#define PC_H 512

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

cvar_t vid_gamma = {"gamma", "1", CVAR_ARCHIVE};
cvar_t vid_contrast = {"contrast", "1", CVAR_ARCHIVE};
/* the PS2's video cvars, so the same command lines and configs work */
static cvar_t vid_mode = {"vid_mode", "0", CVAR_NONE};
static cvar_t vid_beam = {"vid_beam", "0", CVAR_ARCHIVE};
static cvar_t vid_rgb24 = {"vid_rgb24", "0", CVAR_ARCHIVE};

void *VID_GetWindow(void) { return NULL; }
qboolean VID_HasMouseOrInputFocus(void) { return true; }
qboolean VID_IsMinimized(void) { return false; }
/* default.cfg's vid_unlock: the config's vid_* are read in VID_Init */
static void VID_Unlock(void) {}
void VID_Lock(void) {}
void VID_Toggle(void) {}
void VID_SyncCvars(void) {}
void VID_Shutdown(void) {}

/* libgl's limits: 256 texels a side, two texture units */
void __real_glGetIntegerv(GLenum p, GLint *v);
void __wrap_glGetIntegerv(GLenum p, GLint *v)
{
	__real_glGetIntegerv(p, v);
	if (p == GL_MAX_TEXTURE_SIZE)
		*v = 256;
	else if (p == GL_MAX_TEXTURE_UNITS)
		*v = 2;
}

#define PROC(name) eglGetProcAddress(name)

static void GL_CheckExtensions(void)
{
	GL_BindBufferFunc = (PFNGLBINDBUFFERARBPROC)PROC("glBindBufferARB");
	GL_BufferDataFunc = (PFNGLBUFFERDATAARBPROC)PROC("glBufferDataARB");
	GL_BufferSubDataFunc = (PFNGLBUFFERSUBDATAARBPROC)PROC("glBufferSubDataARB");
	GL_DeleteBuffersFunc = (PFNGLDELETEBUFFERSARBPROC)PROC("glDeleteBuffersARB");
	GL_GenBuffersFunc = (PFNGLGENBUFFERSARBPROC)PROC("glGenBuffersARB");
	gl_vbo_able = !COM_CheckParm("-novbo");
	GL_MTexCoord2fFunc = (PFNGLMULTITEXCOORD2FARBPROC)PROC("glMultiTexCoord2fARB");
	GL_SelectTextureFunc = (PFNGLACTIVETEXTUREARBPROC)PROC("glActiveTextureARB");
	GL_ClientActiveTextureFunc = (PFNGLCLIENTACTIVETEXTUREARBPROC)PROC("glClientActiveTextureARB");
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &gl_max_texture_units);
	gl_mtexable = !COM_CheckParm("-nomtex");
	gl_texture_env_combine = !COM_CheckParm("-nocombine");
	gl_texture_env_add = false;
	gl_stencilbits = 0;
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

static void egl_init(void)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display =
	    (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = get_display ? get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
	                           : EGL_NO_DISPLAY;
	if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL))
		Sys_Error("EGL: no display");
	static const EGLint cattr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
	                               EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
	                               EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
	EGLConfig cfg;
	EGLint n;
	if (!eglChooseConfig(d, cattr, &cfg, 1, &n) || n < 1)
		Sys_Error("EGL: no config");
	static const EGLint sattr[] = {EGL_WIDTH, PC_W, EGL_HEIGHT, PC_H, EGL_NONE};
	EGLSurface s = eglCreatePbufferSurface(d, cfg, sattr);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, NULL);
	if (s == EGL_NO_SURFACE || c == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, c))
		Sys_Error("EGL: no context");
}

void GL_BeginRendering(int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = vid.width;
	*height = vid.height;
}

void GL_EndRendering(void) { glFlush(); }

/* the last frame as a TGA (24-bit, bottom-up, as libgl's mxgl_screenshot) */
int pc_screenshot(const char *path)
{
	static byte px[PC_W * PC_H * 3];
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, PC_W, PC_H, GL_BGR, GL_UNSIGNED_BYTE, px);
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	byte h[18] = {0};
	h[2] = 2;
	h[12] = PC_W & 255, h[13] = PC_W >> 8, h[14] = PC_H & 255, h[15] = PC_H >> 8, h[16] = 24;
	fwrite(h, 1, 18, f);
	fwrite(px, 1, sizeof px, f);
	fclose(f);
	return 0;
}

void VID_Init(void)
{
	Cvar_RegisterVariable(&vid_mode);
	Cvar_RegisterVariable(&vid_beam);
	Cvar_RegisterVariable(&vid_rgb24);
	Cvar_RegisterVariable(&vid_gamma);
	Cvar_RegisterVariable(&vid_contrast);
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
	vid.width = PC_W;
	vid.height = PC_H;
	vid.conwidth = 640;
	vid.conheight = vid.conwidth * vid.height / vid.width;
	vid.numpages = 2;
	vid.recalc_refdef = 1;
	modestate = MS_FULLSCREEN;

	egl_init();
	Cmd_AddCommand("vid_unlock", VID_Unlock);
	Con_SafePrintf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
	GL_CheckExtensions();
	GLAlias_CreateShaders();
	GLWorld_CreateShaders();
	GL_ClearBufferBindings();
	GL_SetupState();
	Con_SafePrintf("Video mode %dx%d (PC, off screen)\n", PC_W, PC_H);
}
