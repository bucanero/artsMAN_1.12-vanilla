CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

LIBSTUB_DIR		= ./lib
PRX_DIR			= .
INSTALL			= cp
PEXPORTPICKUP		= ppu-lv2-prx-exportpickup
PRX_LDFLAGS_EXTRA	= -L ./lib -Wl,--strip-unused-data
PPU_PRX_TARGET		= sman.prx
PPU_PRX_LDFLAGS		+= $(PRX_LDFLAGS_EXTRA)
PPU_PRX_STRIP_FLAGS	= -s

CRT_HEAD                += $(shell ppu-lv2-gcc -print-file-name'='ecrti.o)
CRT_HEAD                += $(shell ppu-lv2-gcc -print-file-name'='crtbegin.o)
CRT_TAIL                += $(shell ppu-lv2-gcc -print-file-name'='crtend.o)
CRT_HEAD                += $(shell ppu-lv2-gcc -print-file-name'='ecrtn.o)

PPU_INCDIRS		= -I ./vsh
PPU_SRCS		= $(wildcard misc/*.c) $(wildcard misc/cobra/*.c) main.c 
PPU_SRCS		+= $(wildcard slaunch/*.c)

PPU_PRX_LDLIBS		=	-lfs_stub -lnet_stub -lnetctl_stub -lrtc_stub -lio_stub -lgcm_sys_stub -lntfs_prx
PPU_PRX_LDLIBS		+=	-ljpgdec_stub \
				-lstdc_export_stub \
				-lallocator_export_stub \
				-lpngdec_ppuonly_export_stub \
				-lvsh_export_stub \
				-lvshtask_export_stub \
				-lpaf_export_stub \
				-lvshmain_export_stub \
				-lsdk_export_stub \
				-lxsetting_export_stub \
				-lsys_io_export_stub

PPU_CFLAGS		= -g -Os -std=c99 -W -Wall --param max-inline-insns-single=32 -Wuninitialized -Wcomment -Wchar-subscripts -Wdeprecated-declarations -Wendif-labels -Wformat=2 -Wformat-extra-args -Wimplicit -Wimport -Winline -Wmissing-braces -Wparentheses -Wpointer-arith -Wredundant-decls -Wreturn-type -Wshadow -Wsign-compare -Wswitch -Wunknown-pragmas -Wunused -Wwrite-strings -Wmain -Wbad-function-cast -Wmissing-declarations -Wnested-externs -Wstrict-prototypes -Wmissing-prototypes -I ./vsh -DCELL_DATA_DIR=\"/msys/1.0/cell/sample_data\" -Os -fno-builtin-printf -nodefaultlibs -fmerge-all-constants -std=gnu99 -Wno-shadow -Wno-unused-parameter -DMAPI -DART

# CLEANFILES = $(PRX_DIR)/$(PPU_SPRX_TARGET) *.elf *.sprx ext/sman_ntf/*.sprx ext/sman_net/*.sprx ext/sman_xmb/*.sprx ext/sman_ntf/*.sym ext/sman_net/*.sym ext/sman_xmb/*.sym ext/sman_ntf/*.prx ext/sman_net/*.prx ext/sman_xmb/*.prx res/sman.net res/sman.ntf res/sman.xmb
CLEANFILES = $(PRX_DIR)/$(PPU_SPRX_TARGET) *.elf *.sprx ext/sman_ntf/*.sprx ext/sman_net/*.sprx ext/sman_xmb/*.sprx ext/sman_ntf/*.sym ext/sman_net/*.sym ext/sman_xmb/*.sym ext/sman_ntf/*.prx ext/sman_net/*.prx ext/sman_xmb/*.prx res/sman.net res/sman.ntf res/sman.xmb

clean:
	rm -rf ext/sman_ntf/objs ext/sman_net/objs ext/sman_xmb/objs *.a *.elf *.prx

all:
	F:/BKP/Tools/scetool -v -p F:/BKP/Tools/data -0 SELF -1 TRUE -s FALSE -2 04 -3 1070000052000001 -4 01000002 -5 APP -6 0003004000000000 -A 0001000000000000 -e $(PPU_PRX_TARGET) $(PPU_SPRX_TARGET)

	# @rm sman.sym
#	rm sman.prx

	@make -C ext/sman_ntf/
	@mv ext/sman_ntf/sman.ntf.sprx res/sman.ntf
#	@make -C ext/sman_ntf/ clean

	@make -C ext/sman_net/
	@mv ext/sman_net/netiso.sprx res/sman.net
#	@make -C ext/sman_net/ clean

	@make -C ext/sman_xmb/
	@mv ext/sman_xmb/wmproxy.sprx res/sman.xmb
#	@make -C ext/sman_xmb/ clean

	./sman_res.exe res 175
	@cat res.bin >> sman.sprx
	@rm res.bin
	@rm sman.sym
	@rm sman.prx
	rm -f -r objs
	mv sMAN.sprx artsMAN_ntfs.sprx

	# @make -C ext/sman_ntf/
	# @mv ext/sman_ntf/rawseciso.sprx res/sman.ntf
#	@make -C ext/sman_ntf/ clean

	# @make -C ext/sman_net/
	# @mv ext/sman_net/netiso.sprx res/sman.net
#	@make -C ext/sman_net/ clean

	# @make -C ext/sman_xmb/
	# @mv ext/sman_xmb/wmproxy.sprx res/sman.xmb
#	@make -C ext/sman_xmb/ clean

	# @sman_res.exe res 160
	# @cat res.bin >> sman.sprx
	# @rm res.bin


include $(CELL_MK_DIR)/sdk.target.mk
