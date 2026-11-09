/* in_ps2.c: Quakespasm's input on maxsdk (in_sdl.c of the SDL build): pad 0
   as SDL's game controller. Its buttons are Quakespasm's controller keys
   (A = cross, B = circle, X = square, Y = triangle, the shoulders L1 / R1,
   the triggers L2 / R2, the sticks' buttons L3 / R3, START escape, SELECT
   tab, the d-pad the arrows), bound in its default.cfg; the left stick
   moves, the right one turns, with in_sdl.c's dead zones and easing
   (joy_* cvars). */

#include <mx/mx.h>
#include <mx/pad.h>

#include "quakedef.h"

cvar_t joy_deadzone_look = {"joy_deadzone_look", "0.175", CVAR_ARCHIVE};
cvar_t joy_deadzone_move = {"joy_deadzone_move", "0.175", CVAR_ARCHIVE};
cvar_t joy_outer_threshold_look = {"joy_outer_threshold_look", "0.02", CVAR_ARCHIVE};
cvar_t joy_outer_threshold_move = {"joy_outer_threshold_move", "0.02", CVAR_ARCHIVE};
cvar_t joy_sensitivity_yaw = {"joy_sensitivity_yaw", "240", CVAR_ARCHIVE};
cvar_t joy_sensitivity_pitch = {"joy_sensitivity_pitch", "130", CVAR_ARCHIVE};
cvar_t joy_invert = {"joy_invert", "0", CVAR_ARCHIVE};
cvar_t joy_exponent = {"joy_exponent", "2", CVAR_ARCHIVE};
cvar_t joy_exponent_move = {"joy_exponent_move", "2", CVAR_ARCHIVE};
cvar_t joy_swapmovelook = {"joy_swapmovelook", "0", CVAR_ARCHIVE};
cvar_t joy_enable = {"joy_enable", "1", CVAR_ARCHIVE};

extern cvar_t cl_maxpitch;
extern cvar_t cl_minpitch;

static int pad_ok;
static const struct {
	u32 bit;
	int key;
} pad_keys[] = {
	{MX_PAD_CROSS, K_ABUTTON},     {MX_PAD_CIRCLE, K_BBUTTON},   {MX_PAD_SQUARE, K_XBUTTON},
	{MX_PAD_TRIANGLE, K_YBUTTON},  {MX_PAD_SELECT, K_TAB},       {MX_PAD_START, K_ESCAPE},
	{MX_PAD_L3, K_LTHUMB},         {MX_PAD_R3, K_RTHUMB},        {MX_PAD_L1, K_LSHOULDER},
	{MX_PAD_R1, K_RSHOULDER},      {MX_PAD_L2, K_LTRIGGER},      {MX_PAD_R2, K_RTRIGGER},
	{MX_PAD_UP, K_UPARROW},        {MX_PAD_DOWN, K_DOWNARROW},   {MX_PAD_LEFT, K_LEFTARROW},
	{MX_PAD_RIGHT, K_RIGHTARROW},
};
#define NKEYS (sizeof pad_keys / sizeof pad_keys[0])

static u32 held;           /* the buttons, read by IN_SendKeyEvents */
static float axis[4];      /* left x, left y, right x, right y: -1 .. 1 */
static u32 was;            /* the buttons IN_Commands sent */
static float was_axis[4];
static double key_timer[NKEYS];
static double emul_timer[4];

void IN_Init(void)
{
	Cvar_RegisterVariable(&joy_sensitivity_yaw);
	Cvar_RegisterVariable(&joy_sensitivity_pitch);
	Cvar_RegisterVariable(&joy_deadzone_look);
	Cvar_RegisterVariable(&joy_deadzone_move);
	Cvar_RegisterVariable(&joy_outer_threshold_look);
	Cvar_RegisterVariable(&joy_outer_threshold_move);
	Cvar_RegisterVariable(&joy_invert);
	Cvar_RegisterVariable(&joy_exponent);
	Cvar_RegisterVariable(&joy_exponent_move);
	Cvar_RegisterVariable(&joy_swapmovelook);
	Cvar_RegisterVariable(&joy_enable);
	pad_ok = mx_pad_init() >= 0;
	Con_Printf("pad: %s\n", pad_ok ? "ok" : "no driver");
}

void IN_Shutdown(void) {}
void IN_MouseMotion(int dx, int dy) {}
void IN_UpdateInputMode(void) {}
void IN_ClearStates(void) {}
void IN_Activate(void) {}
void IN_Deactivate(qboolean free_cursor) {}

/* the pad's state, once a frame (Sys_SendKeyEvents) */
void IN_SendKeyEvents(void)
{
	if (!pad_ok)
		return;
	held = mx_pad_buttons(0);
	u8 st[4];
	mx_pad_sticks(0, st); /* right x, y, left x, y */
	axis[0] = ((int)st[2] - 128) / 128.0f;
	axis[1] = ((int)st[3] - 128) / 128.0f;
	axis[2] = ((int)st[0] - 128) / 128.0f;
	axis[3] = ((int)st[1] - 128) / 128.0f;
}

/* in_sdl.c's: a key event at each change, repeats while held */
static void key_event(qboolean wasdown, qboolean isdown, int key, double *timer)
{
	double now = Sys_DoubleTime();
	if (wasdown) {
		if (isdown) {
			if (now >= *timer) {
				*timer = now + 0.1;
				Key_Event(key, true);
			}
		} else {
			*timer = 0;
			Key_Event(key, false);
		}
	} else if (isdown) {
		*timer = now + 0.5;
		Key_Event(key, true);
	}
}

void IN_Commands(void)
{
	if (!pad_ok || !joy_enable.value)
		return;
	u32 now = held;
	float ax[4];
	memcpy(ax, axis, sizeof ax);
	for (unsigned i = 0; i < NKEYS; i++)
		key_event((was & pad_keys[i].bit) != 0, (now & pad_keys[i].bit) != 0, pad_keys[i].key, &key_timer[i]);
	/* the left stick as the arrows in the menus */
	if (key_dest != key_game) {
		const float t = 0.9f;
		key_event(was_axis[0] < -t, ax[0] < -t, K_LEFTARROW, &emul_timer[0]);
		key_event(was_axis[0] > t, ax[0] > t, K_RIGHTARROW, &emul_timer[1]);
		key_event(was_axis[1] < -t, ax[1] < -t, K_UPARROW, &emul_timer[2]);
		key_event(was_axis[1] > t, ax[1] > t, K_DOWNARROW, &emul_timer[3]);
	}
	was = now;
	memcpy(was_axis, ax, sizeof ax);
}

typedef struct {
	float x, y;
} joyaxis_t;

static joyaxis_t deadzone(joyaxis_t a, float dz, float outer)
{
	joyaxis_t r = {0, 0};
	float m = sqrtf(a.x * a.x + a.y * a.y);
	if (m > dz) {
		float nm = q_min(1.0f, (m - dz) / (1.0f - dz - outer));
		r.x = a.x * nm / m;
		r.y = a.y * nm / m;
	}
	return r;
}

static joyaxis_t easing(joyaxis_t a, float e)
{
	joyaxis_t r = {0, 0};
	float m = sqrtf(a.x * a.x + a.y * a.y);
	if (m == 0)
		return r;
	float em = powf(m, e);
	r.x = a.x * em / m;
	r.y = a.y * em / m;
	return r;
}

void IN_Move(usercmd_t *cmd)
{
	extern cvar_t sv_maxspeed;
	if (!pad_ok || !joy_enable.value || cl.paused || key_dest != key_game)
		return;
	joyaxis_t mv = {axis[0], axis[1]}, lk = {axis[2], axis[3]};
	if (joy_swapmovelook.value) {
		joyaxis_t t = mv;
		mv = lk;
		lk = t;
	}
	mv = easing(deadzone(mv, joy_deadzone_move.value, joy_outer_threshold_move.value), joy_exponent_move.value);
	lk = easing(deadzone(lk, joy_deadzone_look.value, joy_outer_threshold_look.value), joy_exponent.value);

	float speed;
	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0f || cl_forwardspeed.value >= sv_maxspeed.value))
		speed = sv_maxspeed.value;
	else if (cl_forwardspeed.value >= sv_maxspeed.value)
		speed = q_min(sv_maxspeed.value, cl_forwardspeed.value / cl_movespeedkey.value);
	else
		speed = cl_forwardspeed.value;
	cmd->sidemove += speed * mv.x;
	cmd->forwardmove -= speed * mv.y;

	cl.viewangles[YAW] -= lk.x * joy_sensitivity_yaw.value * host_frametime;
	cl.viewangles[PITCH] += lk.y * joy_sensitivity_pitch.value * (joy_invert.value ? -1.0f : 1.0f) * host_frametime;
	if (lk.x != 0 || lk.y != 0)
		V_StopPitchDrift();
	if (cl.viewangles[PITCH] > cl_maxpitch.value)
		cl.viewangles[PITCH] = cl_maxpitch.value;
	if (cl.viewangles[PITCH] < cl_minpitch.value)
		cl.viewangles[PITCH] = cl_minpitch.value;
}
