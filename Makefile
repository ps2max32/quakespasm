# Quakespasm (Quake/) on maxsdk: the PS2 port.
#
#   make [OUT=<dir>] [FRAMES=n] [FIXEDFPS=n] [ARGS="+map e1m1"]   $(OUT)/quakespasm.elf
#   make run             quakespasm.elf in PCSX2 with the game data in
#                        build/pcsx2-test/id1 (Q1DATA: the retail ID1
#                        directory, copied there once)
#
# The engine draws through libs/libgl (OpenGL on the GS) on Quakespasm's
# fixed-function paths; ps2/ has the system, video, input, sound and
# network layers in place of the SDL ones (ps2/include/SDL.h stands in for
# SDL's headers). CD audio is the null driver, the network the loopback.

HERE     := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
PS2MAX   ?= $(abspath $(HERE)/../..)
MAXSDK   ?= $(PS2MAX)/libs/maxsdk
MXGL     ?= $(PS2MAX)/libs/libgl
OUT      ?= $(PS2MAX)/build/apps/quakespasm
all: $(OUT)/quakespasm.elf
# the textures up to 256 a side (GL_MAX_TEXTURE_SIZE, which Quakespasm's
# texture manager scales to)
MXGL_MAX_TEXTURE ?= 256
# beam racing's record of a frame: 512 KB (each frame's stream up to 384
# KB; Quakespasm's world in batches and passes is about 200 KB, more than
# the default's 192)
MXGL_REC_BYTES ?= 524288
include $(MXGL)/mk/mxgl.mk
# the program in VM (ld/maxsdk-vm.ld), started by mxlink; MX_MEMORY=phys:
# all of it in physical memory (32 MiB, no VM in PCSX2's recompiler)
MX_MEMORY ?= vm
include $(MAXSDK)/mk/maxsdk.mk
# in VM: the game in VM, libgl in core
MX_CORE_OBJS = $(MXGL_OBJS)

PICOLIBC ?= $(PS2MAX)/build/libs/picolibc/install
LIBC_LIBS = $(MAXSDK_OUT)/libc/glue.c.o $(MAXSDK_OUT)/libmxc.a $(PICOLIBC)/lib/libc.a \
            $(MX_LIB) $(MAXSDK_OUT)/librt.a

Q        := $(HERE)/Quake
P        := $(HERE)/ps2
# unsuffixed literals and the math.h functions are float (ps2/ps2_math.h):
# the FPU has no doubles
QSCFLAGS  = $(filter-out -fno-common -ffreestanding -Wall -Wextra -Wno-unused-parameter -std=gnu11,$(CFLAGS)) \
            -std=gnu11 -w -MMD -fno-strict-aliasing -ffast-math -fapprox-func \
            -cl-single-precision-constant -nostdlibinc -isystem $(PICOLIBC)/include \
            -D__PS2__ -DNDEBUG -DUSE_SDL2 -DUSE_CODEC_WAVE -DGL_GLEXT_PROTOTYPES \
            -I$(P)/include -I$(Q) -I$(P) -I$(OUT)/ps2 -I$(MXGL_INC) $(MXGL_DEFS) \
            -include $(P)/ps2_stdio.h -include $(P)/ps2_math.h

ENGINE   := strlcat strlcpy \
            gl_refrag gl_rlight gl_rmain gl_fog gl_rmisc r_part r_world gl_screen gl_sky gl_warp \
            gl_draw image gl_texmgr gl_mesh r_sprite r_alias r_brush gl_model \
            snd_dma snd_mix snd_mem bgmusic snd_codec snd_wave cd_null \
            net_loop net_main \
            chase cl_demo cl_input cl_main cl_parse cl_tent console keys menu sbar view wad cmd \
            common miniz crc cvar cfgfile host host_cmd mathlib pr_cmds pr_edict pr_exec \
            sv_main sv_move sv_phys sv_user world zone
PS2SRC   := sys_ps2 vid_ps2 in_ps2 snd_ps2 net_ps2 ps2_file

OBJS     := $(patsubst %,$(OUT)/qs/%.o,$(ENGINE)) $(patsubst %,$(OUT)/ps2/%.o,$(PS2SRC))

# the network's files: no sockets (the loopback only), ps2/net_ps2.h
$(OUT)/qs/net_%.o $(OUT)/ps2/net_%.o: QSCFLAGS += -include $(P)/net_ps2.h
$(OUT)/qs/%.o: $(Q)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(QSCFLAGS) -c $< -o $@
$(OUT)/ps2/%.o: $(P)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(QSCFLAGS) -c $< -o $@

# FRAMES: end after that many frames with a picture (host:quakespasm.tga);
# FIXEDFPS=n: the game's time 1/n s a frame, not the clock's (the same
# picture at the same frame, whatever the speed); ARGS: more of the
# command line. In $(OUT)/ps2/runcfg.h, rewritten only when they change.
RUNCFG := $(OUT)/ps2/runcfg.h
$(shell mkdir -p $(OUT)/ps2; printf '#define PS2_FRAMES %s\n#define PS2_ARGS "%s"\n#define PS2_PROF %s\n#define PS2_VM %s\n#define PS2_FIXED_FPS %s\n' \
  '$(or $(FRAMES),0)' '$(ARGS)' '$(or $(PROF),0)' '$(if $(filter vm,$(MX_MEMORY)),1,0)' '$(or $(FIXEDFPS),0)' > $(RUNCFG).new; \
  cmp -s $(RUNCFG).new $(RUNCFG) || mv $(RUNCFG).new $(RUNCFG); rm -f $(RUNCFG).new)

$(OUT)/quakespasm.elf: $(OBJS) $(MXGL_OBJS) $(HLSL_OBJS) $(MX_LIB) $(MX_CRT0) $(MX_LD) $(LIBC_LIBS)
	$(call link,$@,--defsym=__stack_size=0x100000 $(MX_CRT0) $(OBJS) $(MXGL_OBJS) $(HLSL_OBJS) $(LIBC_LIBS))

# ---- running --------------------------------------------------------------------
# Q1DATA: the retail game's ID1 directory (PAK0.PAK, PAK1.PAK), copied to
# $(RUNDIR)/id1 when the paks are not there yet
RUNDIR   := $(PS2MAX)/build/pcsx2-test
run: $(OUT)/quakespasm.elf
	@mkdir -p $(RUNDIR)/id1
	@for f in pak0 pak1; do [ -f $(RUNDIR)/id1/$$f.pak ] || \
	  { [ -n "$(Q1DATA)" ] || { echo "Q1DATA: the game's ID1 directory"; exit 1; }; \
	    cp -v "$(Q1DATA)/$$(echo $$f | tr a-z A-Z).PAK" $(RUNDIR)/id1/$$f.pak; }; done
	cp $(Q)/quakespasm.pak $(RUNDIR)/quakespasm.pak
ifeq ($(MX_MEMORY),vm)
	cp $(OUT)/quakespasm.elf $(RUNDIR)/quakespasm.elf
	MXLOAD="host:quakespasm.elf" PCSX2_EE=interp $(PCSX2_RUN) $(MX_LOADER) $(or $(TIMEOUT),1800)
else
	$(PCSX2_RUN) $(OUT)/quakespasm.elf $(or $(TIMEOUT),600)
endif

clean:
	rm -rf $(OUT)

.PHONY: all run clean

-include $(shell find $(OUT) -name '*.d' 2>/dev/null)
