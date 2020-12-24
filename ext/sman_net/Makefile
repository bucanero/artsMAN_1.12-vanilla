CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

BUILD_TYPE     	= release

LIBSTUB_DIR		= ../lib
PRX_DIR		= .
INSTALL			= cp
PEXPORTPICKUP		= ppu-lv2-prx-exportpickup
PRX_LDFLAGS_EXTRA	= -L ../../lib -Wl,--strip-unused-data

CRT_HEAD                += $(shell ppu-lv2-gcc -print-file-name'='ecrti.o)
CRT_HEAD                += $(shell ppu-lv2-gcc -print-file-name'='crtbegin.o)
CRT_TAIL                += $(shell ppu-lv2-gcc -print-file-name'='crtend.o)
CRT_HEAD                += $(shell ppu-lv2-gcc -print-file-name'='ecrtn.o)

PPU_SRCS = main.c libc.c asmcode.s
PPU_PRX_TARGET = netiso.prx
PPU_PRX_LDFLAGS += $(PRX_LDFLAGS_EXTRA)
PPU_PRX_STRIP_FLAGS = -s
PPU_PRX_LDLIBS 	+= -lfs_stub -lnet_stub -lallocator_export_stub -lstdc_export_stub
PPU_CFLAGS	= -g -Os -std=c99 -W -Wall -Wuninitialized -Wcomment -Wchar-subscripts -Wdeprecated-declarations -Wendif-labels -Wformat=2 -Wformat-extra-args -Wimplicit -Wimport -Winline -Wmissing-braces -Wparentheses -Wpointer-arith -Wredundant-decls -Wreturn-type -Wshadow -Wsign-compare -Wswitch -Wunknown-pragmas -Wunused -Wwrite-strings -Wmain -Wbad-function-cast -Wmissing-declarations -Wnested-externs -Wstrict-prototypes -Wmissing-prototypes -I ./vsh   -DCELL_DATA_DIR=\"/msys/1.0/cell/sample_data\" -Os -fno-builtin-printf -nodefaultlibs -fmerge-all-constants -std=gnu99 -Wno-shadow -Wno-unused-parameter 


CLEANFILES = $(PRX_DIR)/$(PPU_SPRX_TARGET)

all:
	@$(MAKE) $(PPU_OBJS_DEPENDS) > nul
	@$(PPU_PRX_STRIP) --strip-debug --strip-section-header $(PPU_PRX_TARGET) > nul
	F:/BKP/Tools/scetool -v -p F:/BKP/Tools/data -0 SELF -1 TRUE -s FALSE -2 04 -3 1070000052000001 -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000 -e $(PPU_PRX_TARGET) $(PPU_SPRX_TARGET) > nul

include $(CELL_MK_DIR)/sdk.target.mk




