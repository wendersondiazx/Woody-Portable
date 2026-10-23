TARGET = WoodyPSP
SRC = level render_gl main_engine player instance enemy boss water storm ekovm audio hud hnm ambient blackbox
OBJS = $(addprefix vendor/src/,$(addsuffix .o,$(SRC))) platform_psp.o extras_psp.o launch_psp.o native_gu.o presentation_psp.o
INCDIR = native vendor/src
CXXFLAGS = -O2 -G0 -Wall -Werror=return-type -Werror=narrowing -Werror=write-strings -Wno-format -Wno-misleading-indentation -Wno-missing-field-initializers -std=gnu++17 -fno-exceptions -fno-rtti -fsigned-char -ffunction-sections -fdata-sections -DWOODY_GUI -DWOODY_PSP -include gl_compat.h -MMD -MP
LDFLAGS = -Wl,--gc-sections
LIBS = -lpspgu -lpspaudio -lpsppower -lpsprtc -lm
BUILD_PRX = 1
PSP_LARGE_MEMORY = 1
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = WoodyPSP r8 C++
PSP_EBOOT_ICON = $(wildcard presentation/ICON0.PNG)
PSP_EBOOT_PIC1 = $(wildcard presentation/PIC1.PNG)
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
# The SDK defaults to the C linker even when all inputs are C++ objects.
LINK.c = $(CXX) $(LDFLAGS)
PARAM.SFO: Makefile
# The native SDK frontend implements the engine's local drawing API.
native_gu.o: CXXFLAGS += -DWOODY_GL_IMPLEMENTATION -Werror=infinite-recursion
# The Allegrex FPU implements sqrt.s. Without this flag GCC calls libm for
# every normal length merely to preserve desktop errno semantics.
vendor/src/render_gl.o vendor/src/level.o: CXXFLAGS += -O3 -fno-math-errno -fno-trapping-math
vendor/src/hud.o: CXXFLAGS += -O3 -fno-math-errno -fno-trapping-math
-include $(OBJS:.o=.d)
