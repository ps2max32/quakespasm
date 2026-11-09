# The PC build of the same engine (Quake/, the paths the PS2 build takes)
# on the host's OpenGL, off screen: the reference picture for libgl.
#
#   make -C pc [FRAMES=n] [ARGS="+map e1m1"]   $(OUT)/quakespasm-pc
#   make -C pc run                              runs it in build/pcsx2-test
#                                               (the game data there) on
#                                               Mesa's llvmpipe: quakespasm-pc.tga
#
# FRAMES, ARGS: as the PS2 build's (../Makefile, with FIXEDFPS=50), the
# same command line.

HERE    := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PS2MAX  ?= $(abspath $(HERE)/../../..)
OUT     ?= $(PS2MAX)/build/apps/quakespasm-pc
Q       := $(HERE)/../Quake

CC      ?= cc
CFLAGS  := -O2 -g -std=gnu11 -w -MMD -fno-strict-aliasing -DNDEBUG -DUSE_SDL2 -DUSE_CODEC_WAVE \
           -DGL_GLEXT_PROTOTYPES -I$(HERE)/include -I$(Q) -I$(OUT)
LDLIBS  := -Wl,--wrap=glGetIntegerv -lEGL -lOpenGL -lm

ENGINE  := strlcat strlcpy \
           gl_refrag gl_rlight gl_rmain gl_fog gl_rmisc r_part r_world gl_screen gl_sky gl_warp \
           gl_draw image gl_texmgr gl_mesh r_sprite r_alias r_brush gl_model \
           snd_dma snd_mix snd_mem bgmusic snd_codec snd_wave cd_null \
           net_loop net_main \
           chase cl_demo cl_input cl_main cl_parse cl_tent console keys menu sbar view wad cmd \
           common miniz crc cvar cfgfile host host_cmd mathlib pr_cmds pr_edict pr_exec \
           sv_main sv_move sv_phys sv_user world zone
OBJS    := $(ENGINE:%=$(OUT)/qs/%.o) $(OUT)/pc/sys_pc.o $(OUT)/pc/vid_pc.o $(OUT)/pc/net_ps2.o

all: $(OUT)/quakespasm-pc

# the network's files: the loopback only (../ps2/net_ps2.h, net_ps2.c)
$(OUT)/qs/net_%.o $(OUT)/pc/net_%.o: CFLAGS += -include $(HERE)/../ps2/net_ps2.h
$(OUT)/qs/%.o: $(Q)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/pc/%.o: $(HERE)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/pc/net_ps2.o: $(HERE)/../ps2/net_ps2.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

RUNCFG := $(OUT)/runcfg.h
$(shell mkdir -p $(OUT); printf '#define PS2_FRAMES %s\n#define PS2_ARGS "%s"\n' \
  '$(or $(FRAMES),0)' '$(ARGS)' > $(RUNCFG).new; \
  cmp -s $(RUNCFG).new $(RUNCFG) || mv $(RUNCFG).new $(RUNCFG); rm -f $(RUNCFG).new)

$(OUT)/quakespasm-pc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

RUNDIR := $(PS2MAX)/build/pcsx2-test
run: $(OUT)/quakespasm-pc
	cp $(Q)/quakespasm.pak $(RUNDIR)/quakespasm.pak
	cd $(RUNDIR) && LIBGL_ALWAYS_SOFTWARE=1 $(OUT)/quakespasm-pc

-include $(OBJS:.o=.d)
.PHONY: all run
