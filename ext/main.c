#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sdk_version.h>
#include <cellstatus.h>

#include <cell/cell_fs.h>
#include <cell/rtc.h>
#include <cell/gcm.h>
#include <cell/pad.h>

#include <sysutil/sysutil_common.h>

#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/event.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/memory.h>
#include <sys/timer.h>
#include <sys/process.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netex/net.h>
#include <netex/errno.h>
#include <netex/libnetctl.h>
#include <netex/sockinfo.h>

#define USE_NTFS	1		// compile with NTFS support
//#define DEBUG		1

#include "misc/gui.h"
#include "misc/types.h"
#include "misc/common.h"
#include "misc/patches.h"

#include "misc/cobra/storage.h"
#include "misc/cobra/cobra.h"
#include "misc/cobra/netiso.h"

#include "vsh/xregistry.h"
#include "vsh/vshmain.h"
#include "vsh/vshtask.h"
#include "vsh/paf.h"

#include "slaunch/include/slaunch.h"

SYS_MODULE_INFO(sman, 0, 1, 1);
SYS_MODULE_START(sm_start);

#define WM_VER				"1.12"
#ifdef USE_NTFS
#define WM_VERSION			WM_VER"n"							// sMAN version with NTFS support
#else
#define WM_VERSION			WM_VER								// sMAN version
#endif

#define MM_ROOT_STD			"/dev_hdd0/game/BLES80608/USRDIR"	// multiMAN root folder
#define MM_ROOT_STL			"/dev_hdd0/tmp/game_repo/main"		// stealthMAN root folder
#define MM_ROOT				MM_ROOT_STD

#define THREAD_INIT			"sm_init"
#define THREAD_MAIN			"sm_main"
#define THREAD_WWWC			"sm_wwwc"
#define THREAD_STOP			"sm_stop"

#define THREAD_NETS			"sm_nets"
#define THREAD_RISO			"sm_riso"

#define THREAD_MENU			"sm_menu"

#define THREAD_FTPD			"sm_ftpd"
#define THREAD_FTPC			"sm_ftpc"

#define THREAD_POLL			"sm_poll"
#define THREAD_REFR			"sm_refr"
#define THREAD_SCAN			"sm_scan"
#define THREAD_HTTP			"sm_http"

#define WWWPORT				(80)
#define FTPPORT				(21)
#define HTTP_BACKLOG		(2001)
#define	HTTP_MAX_CC			(256)
#define	FTP_BACKLOG			(7)

static int ftpd_socket[16];
static int wwwd_socket[16];
static u8 http_threads=0;
static u8 ftp_threads=0;

static u16 pasv_port = 32800;

#define ssend(socket, str) send(socket, str, strlen(str), 0)
#define getPort(p1x, p2x) ((p1x * 256) + p2x)

#define BUFFER_SIZE_FTP		(32*1024)

static sys_ppu_thread_t thread_id_poll	=-1;
static sys_ppu_thread_t thread_id_ftp	=-1;
static sys_ppu_thread_t thread_id_www	=-1;
static sys_ppu_thread_t thread_id_net	=-1;
static sys_ppu_thread_t thread_id_gui	=-1;

#define MIN(a, b)	((a) <= (b) ? (a) : (b))
#define ABS(a)		(((a) < 0) ? -(a) : (a))

#define USB_MASS_STORAGE_1(n)	(0x10300000000000AULL+n) /* For 0-5 */
#define USB_MASS_STORAGE_2(n)	(0x10300000000001FULL+(n-6)) /* For 6-127 */
#define USB_MASS_STORAGE(n)		(((n) < 6) ? USB_MASS_STORAGE_1(n) : USB_MASS_STORAGE_2(n))

static u8 init_running=0;

#define MAX_FANSPEED	(0xFC)
#define MY_TEMP			(75)
static u8 fan_reset=0;
static u8 fan_speed=0x33;
static u8 old_fan=0x33;
static u32 max_temp=MY_TEMP;

//slaunch
static _slaunch slaunch;
u32		cur_game=0;
u8		type=TYPE_ALL;
u8		slaunch_running=0;

volatile u8 working = 1;

float c_firmware=0.0f;
u8 dex_mode=0;

#define FAIL_SAFE (1<<0)
#define SHOW_TEMP (1<<1)
#define PREV_GAME (1<<2)
#define NEXT_GAME (1<<3)
#define SHUT_DOWN (1<<4)
#define RESTARTPS (1<<5)
#define UNLOAD_WM (1<<6)
#define MANUALFAN (1<<7)

#ifdef USE_NTFS

#include "misc/ntfs.h"

ntfs_md *mounts;
int mountCount=-2;

#endif

typedef struct
{
	int (*DoUnk0)(void);	int (*DoUnk1)(void);	int (*DoUnk2)(void);
	int (*DoUnk3)(void);	int (*DoUnk4)(void);	int (*DoUnk5)(void);
	int (*exec_cmd) (const char *, void *, int);
} explore_plugin_if;

static u64 backup[6];

static char smonth[12][ 4]={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static char drives[ 2][16]={"/dev_hdd0", "/dev_usb000"};
static char paths [10][16]={"GAMES", "GAMEZ", "PS3ISO", "BDISO", "DVDISO", "PS2ISO", "PSXISO", "PSXGAMES", "PSPISO", "ISO"};

u8 smconfig[sizeof(_smconfig)];
_smconfig *sm_config = (_smconfig*) smconfig;

int sm_start(size_t args, void *argp);

static void get_temperature(u32 _dev, u32 *_temp);
static void fan_control(u8 temp0, u8 maxtemp);
static void led(u64 color, u64 mode);
static void restore_fan(u8 settemp);

static void absPath(char* absPath_s, const char* path, const char* cwd);
static int isDir(const char* path);
static int ssplit(const char* str, char* left, int lmaxlen, char* right, int rmaxlen);

extern int my_atoi(const char *c);

static int do_umount_iso(void);
static void mount_game(const char *_path, u8 action);

#ifdef DEBUG
	extern int stdc_C01D9F97(const char *fmt, ...);                                       // printf()
	#define SMAN_LOG "--- SMAN: "
	#define printf stdc_C01D9F97
#else
	#define printf(...) {}
#endif

#define RED		0
#define GREEN	1
#define YELLOW	2

#define OFF		0
#define ON		1
#define BLINK	2

static inline void led(u64 color, u64 mode)
{
	system_call_2(386, (u64)color, (u64)mode);
}

static inline void get_temperature(u32 _dev, u32 *_temp)
{
	system_call_2(383, (u64)_dev, (u64)(u32)_temp);
}

static inline int sys_sm_set_fan_policy(u8 arg0, u8 arg1, u8 arg2)
{
    system_call_3(389, (u64) arg0, (u64) arg1, (u64) arg2);
    return_to_user_prog(int);
}

static inline int sys_sm_get_fan_policy(u8 id, u8 *st, u8 *mode, u8 *speed, u8 *unknown)
{
    system_call_5(409, (u64) id, (u64)(u32) st, (u64)(u32) mode, (u64)(u32) speed, (u64)(u32) unknown);
    return_to_user_prog(int);
}

static inline void _sys_ppu_thread_exit(u64 val)
{
	system_call_1(41, val);
}

static inline sys_prx_id_t prx_get_module_id_by_address(void *addr)
{
	system_call_1(461, (u64)(u32)addr);
	return (int)p1;
}

static bool gui_allowed(bool action)
{
	if(slaunch_running) return 0;

	if(xsetting_CC56EB2D()->GetCurrentUserNumber()<0) // user not logged in
	{
		if(action) show_msg("Not logged in!");
		return 0;
	}

	if(
		vshmain_EB757101() || // in-game
		paf_F21655F3("videoplayer_plugin") ||
		paf_F21655F3("sysconf_plugin") ||
		paf_F21655F3("netconf_plugin") ||
		paf_F21655F3("software_update_plugin") ||
		paf_F21655F3("photoviewer_plugin") ||
		paf_F21655F3("audioplayer_plugin") ||
		paf_F21655F3("bdp_plugin") ||
		paf_F21655F3("download_plugin")
	)
	{
		if(action) show_msg("sMAN: GUI not available!");
		return 0;
	}

	return 1;
}

static u64 read_file(char *filename, void *buffer, u64 size)
{
	u64 read=0;
	int fs;

	if(cellFsOpen(filename, CELL_FS_O_RDONLY, &fs, 0, 0)==CELL_FS_SUCCEEDED)
	{
		cellFsLseek(fs, 0, CELL_FS_SEEK_SET, &read);
		if(cellFsRead(fs, (void *)buffer, size, &read)!=CELL_FS_SUCCEEDED) read=0;
		cellFsClose(fs);
	}

	return read;
}

/*
static void copy_file_from_ntfs(const char *src, const char* dst)
{
	int fs=0;
	int fd=0;

	fs=ps3ntfs_open(src, O_RDONLY, 0);
	if(fs>0)
	{
		u8* buf=(u8*)malloc(BUFFER_SIZE_FTP);
		if(buf)
		{
			cellFsOpen(dst, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0);
			int read_e = 0;
			ps3ntfs_seek64(fs, 0, SEEK_SET);

			while(working)
			{
				read_e = ps3ntfs_read(fs, (void *)buf, BUFFER_SIZE_FTP);
				if(read_e>0)
					cellFsWrite(fd, buf, read_e, NULL);
				else
					break;
			}
			cellFsClose(fd);
		}
		ps3ntfs_close(fs);
		free(buf);
	}
}
*/

static u64 copy_file(const char *src, const char* dst)
{
	u64 read=0;
	int fs=0;
	int fd=0;

	if(cellFsOpen(src, CELL_FS_O_RDONLY, &fs, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		if(cellFsOpen(dst, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
		{
			u8* buf=(u8*)malloc(8192);

			u64 r=0;
			while(working)
			{
				if(cellFsRead(fs, buf, 8192, &r)==CELL_FS_SUCCEEDED)
				{
					read+=r;
					if(r) cellFsWrite(fd, buf, r, NULL);
						else break;
				} else break;
			}

			cellFsClose(fd);
			free(buf);
		}
		cellFsClose(fs);
	}

	return read;
}

static void param_sfo_info(u8 *mem, u16 size, char *title_name, char *title_id)
{
	int indx = 0;
	unsigned str = (mem[8]+(mem[9]<<8));
	unsigned pos = (mem[0xc]+(mem[0xd]<<8));
	if(title_name) title_name[0]=0;
	if(title_id) title_id[0]=0;

	while(str<size)
	{
		if(mem[str]==0) break;

		if(title_name!=NULL && !strcmp((char *) &mem[str], "TITLE"))
		{
			memset(title_name, 0, 128);
			strncpy(title_name, (char *) &mem[pos], 128);
		}
		if(title_id!=NULL && !strcmp((char *) &mem[str], "TITLE_ID"))
		{
			memset(title_id, 0, 16);
			strncpy(title_id, (char *) &mem[pos], 4);
			strncpy(title_id+4, (char *) &mem[pos+4], 5);
		}

		if( ( (title_name!=NULL && title_name[0]) || title_name==NULL) && ( (title_id!=NULL && title_id[0]) || title_id==NULL)) break;

		while(mem[str]) str++;str++;
		pos+=(mem[0x1c+indx]+(mem[0x1d+indx]<<8));
		indx+=16;
		if(str>4090) break;
	}
}

static unsigned int parse_cue(const u8 *cue_buf, const u32 size, TrackDef *tracks)
{
	unsigned int n_tracks=0;
	char* line=(char*)malloc(512);

	tracks[0].lba = 0;
	tracks[0].is_audio = 0;

	if(line && size>10)
	{
		char tcode[16];
		u8 tmin=0, tsec=0, tfrm=0;
		u8 use_pregap=0;
		u32 lp=0;

		while (lp<size)
		{
			u8 line_found=0;
			line[0]=0;
			for(u32 l=0; l<511; l++)
			{
				if(l>=size) break;
				if(lp<size && cue_buf[lp] && cue_buf[lp]!='\n' && cue_buf[lp]!='\r')
				{
					line[l]=cue_buf[lp];
					line[l+1]=0;
				}
				else
				{
					line[l]=0;
				}
				if(cue_buf[lp]=='\n' || cue_buf[lp]=='\r') line_found=1;
				lp++;
				if(cue_buf[lp]=='\n' || cue_buf[lp]=='\r') lp++;

				if(line[l]==0) break;
			}

			if(!line_found) break;

			if(strstr(line, "PREGAP")) {use_pregap=1; continue;}
			if(!strstr(line, "INDEX 01") && !strstr(line, "INDEX 1 ")) continue;

			snprintf(tcode, 16, "%s", strrchr(line, ' ')+1); tcode[8]=0;
			if(strlen(tcode)!=8 || tcode[2]!=':' || tcode[5]!=':') continue;
			tmin=(tcode[0]&0x0f)*10 + (tcode[1]&0x0f);
			tsec=(tcode[3]&0x0f)*10 + (tcode[4]&0x0f);
			tfrm=(tcode[6]&0x0f)*10 + (tcode[7]&0x0f);
			if(use_pregap && n_tracks) tsec+=2;

			if(n_tracks) tracks[n_tracks].is_audio = 1;
			tracks[n_tracks].lba=(tmin*60 + tsec)*75 + tfrm;

			n_tracks++; if(n_tracks>31) break;
		}
		free(line);
	}

	if(!n_tracks) n_tracks++;

	return n_tracks;
}

typedef struct
{
	u64 device;
	u32 emu_mode;
	u32 num_sections;
	u32 num_tracks;
} __attribute__((packed)) rawseciso_args; //20 bytes


#ifdef USE_NTFS

static sys_ppu_thread_t thread_id_rs = -1;
static void rawseciso_thread(uint64_t arg);
static void rawseciso_stop_thread(uint64_t arg);

static void prepNTFS(int conn_s)
{
	char path[256];
	char path0[512];

	u64 read = 0;
	int i, parts, fd, r;
	int cue = 0;

	int emu_mode=0;
	unsigned int num_tracks;
	TrackDef tracks[32]; // 256 bytes
	ScsiTrackDescriptor *scsi_tracks;

	#define MAX_SECTIONS		(8189) //((0x10000-sizeof(rawseciso_args))/8)

	rawseciso_args *p_args;

	CellFsDirent dir;
	struct CellFsStat s;

    DIR_ITER *pdir = NULL;
    struct stat st;

	char c_path[5][9]={"PS3ISO", "BDISO", "DVDISO", "PSXISO", "PSXGAMES"};
	char prefix[2][8]={"/", "/PS3/"};

	if(mountCount==-2)
	for (i = 0; i<2; i++)
	{
		mountCount = ntfsMountAll(&mounts, NTFS_SU | NTFS_FORCE);
		if(mountCount>0) break;
		sys_timer_sleep(2);
	}

	if(mountCount>0)
	{
		for(int u=0;u<mountCount;u++)
		{
			snprintf(path, sizeof(path), "%s:/", mounts[u].name);
			pdir = ps3ntfs_diropen(path);
			if(pdir) ps3ntfs_dirclose(pdir);
			else { mountCount=-2; break; }
		}
	}

	if(mountCount < 1)
	{
		if(cellFsOpendir(WMTMP, &fd)==CELL_FS_SUCCEEDED)
		{
			while(!cellFsReaddir(fd, &dir, &read) && read)
				if(strstr(dir.d_name, ".ntfs[")) {sprintf(path0, WMTMP "/%s", dir.d_name); cellFsUnlink(path0);}

			cellFsClosedir(fd);
		}
	}

	if (mountCount < 1) {mountCount=-2; return;}

	u8* plugin_args		= (u8* ) malloc		 (130*1024); if(!plugin_args) return;
	u32* sectionsP		= (u32*)(plugin_args+( 64*1024));
	u32* sections_sizeP	= (u32*)(sectionsP+	 ( 32*1024));
	u8* tmp_buf			= (u8* ) sectionsP;
	u8* cue_buf			= (u8* )(sectionsP+	 ( 64*1024)); //2K

	sys_ppu_thread_t thr_id;
	u64 exit_code;

	sys_device_info_t disc_info;
	u32 sec_size;

	unsigned int real_disctype;
	sys_storage_ext_get_disc_type(&real_disctype, NULL, NULL);
	if (real_disctype != 0)	fake_eject_event();

	for (i = 0; i < mountCount; i++)
	{

		if(sys_storage_get_device_info(USB_MASS_STORAGE((mounts[i].interface->ioType & 0xff) - '0'), &disc_info) != 0) continue;
		sec_size=(u32) disc_info.sector_size;

		for(u8 n=0;n<2;n++)
		{
			for(u8 m=0;m<5;m++)
			{
				snprintf(path, sizeof(path), "%s:%s%s", mounts[i].name, prefix[n], c_path[m]);
				pdir = ps3ntfs_diropen(path);
				if(pdir)
				{
					while(ps3ntfs_dirnext(pdir, dir.d_name, &st) == 0)
					{
						if(st.st_mode & S_IFDIR) continue;

						if( ((strstr(dir.d_name, ".ISO") || strstr(dir.d_name, ".iso")) && (dir.d_name[strlen(dir.d_name)-4]=='.')) ||
							( (m==3 || m==4) && ( (strstr(dir.d_name, ".MDF") || strstr(dir.d_name, ".mdf")) || (strstr(dir.d_name, ".IMG") || strstr(dir.d_name, ".img")) || ((strstr(dir.d_name, ".BIN") || strstr(dir.d_name, ".bin")) && (dir.d_name[strlen(dir.d_name)-4]=='.')) ) ) )
						{
							if(conn_s)
							{
								strcpy(path, dir.d_name);
								path[strlen(path)-4]=0;
								strcat(path, "<br/>");
								ssend(conn_s, path);
							}

							snprintf(path, sizeof(path), WMTMP "/%s.ntfs[%s]", dir.d_name, c_path[m]);
							if(cellFsStat(path, &s)==CELL_FS_SUCCEEDED) continue;

							snprintf(path, sizeof(path), "%s:%s%s/%s", mounts[i].name, prefix[n], c_path[m], dir.d_name);
							/*
							char path3[256];
							sprintf(path3, "/dev_hdd0/PS3ISO/%s", dir.d_name);
							if(m==0)
							{
								ssend(conn_s, "Copying to: ");
								ssend(conn_s, path3);
								ssend(conn_s, "<br>");
								if(!file_exists(path3))
									copy_file_from_ntfs(path, path3);
								else
									ssend(conn_s, "Skipped! ");
								ssend(conn_s, "Done!<br>");
							}
							*/
							parts = ps3ntfs_file_to_sectors(path, sectionsP, sections_sizeP, MAX_SECTIONS, 1);

							if (parts == MAX_SECTIONS)
								continue;

							else if (parts > 0)
							{
								num_tracks = 1;
								if(m==0) emu_mode = EMU_PS3;
								else if(m==1) emu_mode = EMU_BD;
								else if(m==2) emu_mode = EMU_DVD;
								else if(m==3 || m==4)
								{
									emu_mode = EMU_PSX;
									cue=0;

									path[strlen(path)-3]=0; strcat(path, "CUE");
									fd = ps3ntfs_open(path, O_RDONLY, 0);
									if(fd<0)
									{
										path[strlen(path)-3]=0; strcat(path, "cue");
										fd = ps3ntfs_open(path, O_RDONLY, 0);
									}

									if (fd >= 0)
									{
										int r = ps3ntfs_read(fd, (void*)cue_buf, 2048);
										ps3ntfs_close(fd);

										num_tracks=parse_cue(cue_buf, r, tracks);
										cue=1;
									}
								}

								p_args = (rawseciso_args *)plugin_args; memset(p_args, 0x0, 0x10000);
								p_args->device = USB_MASS_STORAGE((mounts[i].interface->ioType & 0xff) - '0');
								p_args->emu_mode = emu_mode;
								p_args->num_sections = parts;

								memcpy(plugin_args+sizeof(rawseciso_args), sectionsP, parts*sizeof(u32));
								memcpy(plugin_args+sizeof(rawseciso_args)+(parts*sizeof(u32)), sections_sizeP, parts*sizeof(u32));

								if (emu_mode == EMU_PSX)
								{
									if (parts >= (int)(MAX_SECTIONS - ((num_tracks*sizeof(ScsiTrackDescriptor)) / 8)))	continue;

									p_args->num_tracks = num_tracks;
									scsi_tracks = (ScsiTrackDescriptor *)(plugin_args+sizeof(rawseciso_args)+(2*parts*sizeof(u32)));

									if (!cue)
									{
										scsi_tracks[0].adr_control = 0x14;
										scsi_tracks[0].track_number = 1;
										scsi_tracks[0].track_start_addr = 0;
									}
									else
									{
										for (u8 j = 0; j < num_tracks; j++)
										{
											scsi_tracks[j].adr_control = (tracks[j].is_audio) ? 0x10 : 0x14;
											scsi_tracks[j].track_number = j+1;
											scsi_tracks[j].track_start_addr = tracks[j].lba;
										}
									}
								}

								snprintf(path, sizeof(path), WMTMP "/%s.ntfs[%s]", dir.d_name, c_path[m]);
								if(cellFsOpen(path, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
								{
									cellFsWrite(fd, plugin_args, (sizeof(rawseciso_args)+(parts*sizeof(u32))*2)+(num_tracks*sizeof(ScsiTrackDescriptor)), NULL);
									cellFsClose(fd);
								}

								u8 img_found=1;

								snprintf(path, sizeof(path), WMTMP "/%s", dir.d_name);
								u16 pl=strlen(path);

								path[pl-3]=0; strcat(path, "jpg");
								if(cellFsStat(path, &s)!=CELL_FS_SUCCEEDED) {path[pl-3]=0; strcat(path, "JPG");} else goto for_sfo;
								if(cellFsStat(path, &s)!=CELL_FS_SUCCEEDED) {path[pl-3]=0; strcat(path, "png");} else goto for_sfo;
								if(cellFsStat(path, &s)!=CELL_FS_SUCCEEDED) {path[pl-3]=0; strcat(path, "PNG");} else goto for_sfo;
								if(cellFsStat(path, &s)==CELL_FS_SUCCEEDED) goto for_sfo;

								snprintf(path0, sizeof(path0), "%s:%s%s/%s", mounts[i].name, prefix[n], c_path[m], dir.d_name);
								u16 pl0=strlen(path0);

								path0[pl0-3]=0; strcat(path0, "jpg");
								if(ps3ntfs_stat(path0, &st)<0) {path0[pl0-3]=0; strcat(path0, "JPG");} else goto img_ok;
								if(ps3ntfs_stat(path0, &st)<0) {path0[pl0-3]=0; strcat(path0, "png");} else goto img_ok;
								if(ps3ntfs_stat(path0, &st)<0) {path0[pl0-3]=0; strcat(path0, "PNG");} else goto img_ok;
								if(ps3ntfs_stat(path0, &st)<0) {img_found=0; goto for_sfo;}

img_ok:
								path[pl-3]=path0[pl0-3];path[pl-2]=path0[pl0-2];path[pl-1]=path0[pl0-1];
								if(cellFsOpen(path, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
								{
									cue = ps3ntfs_open(path0, O_RDONLY, 0);
									if (cue >= 0)
									{
										while(working){
											r = ps3ntfs_read(cue, (void*)tmp_buf, 65536);
											if(r>0) cellFsWrite(fd, tmp_buf, r, NULL); else break;
										}
										ps3ntfs_close(cue);
									}
									cellFsClose(fd);
								}
for_sfo:
								if(m==0) //PS3ISO
								{
									u8 param_sfo=0;
									path[pl-3]=0; strcat(path, "SFO");
									if(cellFsStat(path, &s)==CELL_FS_SUCCEEDED) param_sfo=1;
									if(!param_sfo || !img_found)
									{
										p_args->emu_mode=real_disctype;
										p_args->num_tracks=sec_size;

										sys_ppu_thread_create(&thread_id_rs, rawseciso_thread, (u64)(u32)p_args, -0x1d8, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_RISO);

										for(r=0;r<20;r++)
										{

											sys_timer_usleep(140000);
											if(!param_sfo)
											{
												read=copy_file("/dev_bdvd/PS3_GAME/PARAM.SFO", path);
												if(read)
													param_sfo=1;
											}

											if(!img_found)
											if(cellFsStat("/dev_bdvd/PS3_GAME/ICON0.PNG", &s) == CELL_FS_SUCCEEDED)
											{
												path[strlen(path)-3]=0; strcat(path, "PNG");

												if(copy_file("/dev_bdvd/PS3_GAME/ICON0.PNG", path))
													img_found=1;
											}

											if(img_found && param_sfo) break;
										}
										sys_timer_usleep(170000);
										sys_ppu_thread_create(&thr_id, rawseciso_stop_thread, 0, 0, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, "");
										sys_ppu_thread_join(thr_id, &exit_code);

										sys_timer_usleep(30000);
									}
								}
							}
						}
					}
					ps3ntfs_dirclose(pdir);
				}
			}
		}
	}

	sys_storage_ext_get_disc_type(&real_disctype, NULL, NULL);
	fake_eject_event();
	if (real_disctype != 0)	fake_insert_event(real_disctype);

	free(plugin_args);
	return;
}
#endif

static int connect_to_server(char *server, uint16_t port)
{
	struct sockaddr_in sin;
	unsigned int temp;
	int s;

	if ((temp = inet_addr(server)) != (unsigned int)-1)
	{
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = temp;
	}
	else
	{
		struct hostent *hp;

		if ((hp = gethostbyname(server)) == NULL)
			return -1;

		sin.sin_family = hp->h_addrtype;
		memcpy(&sin.sin_addr, hp->h_addr, hp->h_length);
	}

	sin.sin_port = htons(port);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;

	struct timeval tv;
	tv.tv_usec = 0;

	tv.tv_sec = 3;
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		return -1;

	tv.tv_sec = 60;
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	return s;
}

static int remote_stat(int s, char *path, int *is_directory, int64_t *file_size, u64 *mtime, u64 *ctime, u64 *atime, int *abort_connection)
{
	netiso_stat_cmd cmd;
	netiso_stat_result res;
	int len;

	*abort_connection = 0;

	len = strlen(path);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = (NETISO_CMD_STAT_FILE);
	cmd.fp_len = (len);

	if (send(s, &cmd, sizeof(cmd), 0) != sizeof(cmd))
	{
		*abort_connection = 1;
		return -1;
	}

	if (send(s, path, len, 0) != len)
	{
		*abort_connection = 1;
		return -1;
	}

	if (recv(s, &res, sizeof(res), MSG_WAITALL) != sizeof(res))
	{
		*abort_connection = 1;
		return -1;
	}

	*file_size = (res.file_size);
	if (*file_size == -1)
		return -1;

	*is_directory = res.is_directory;
	*mtime = (res.mtime);
	*ctime = (res.ctime);
	*atime = (res.atime);

	return 0;
}

static int read_remote_file(int s, void *buf, u64 offset, u32 size, int *abort_connection)
{
	netiso_read_file_cmd cmd;
	netiso_read_file_result res;

	*abort_connection = 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = (NETISO_CMD_READ_FILE);
	cmd.offset = (offset);
	cmd.num_bytes = (size);

	if (send(s, &cmd, sizeof(cmd), 0) != sizeof(cmd))
	{
		*abort_connection = 1;
		return -1;
	}

	if (recv(s, &res, sizeof(res), MSG_WAITALL) != sizeof(res))
	{
		*abort_connection = 1;
		return -1;
	}

	int bytes_read = (res.bytes_read);
	if (bytes_read <= 0)
		return bytes_read;

	if (recv(s, buf, bytes_read, MSG_WAITALL) != bytes_read)
	{
		*abort_connection = 1;
		return -1;
	}

	return bytes_read;
}

static int64_t open_remote_file_2(int s, char *path, int *abort_connection)
{
	netiso_open_cmd cmd;
	netiso_open_result res;
	int len;

	*abort_connection = 0;

	len = strlen(path);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = BE16(NETISO_CMD_OPEN_FILE);
	cmd.fp_len = BE16(len);

	if (send(s, &cmd, sizeof(cmd), 0) != sizeof(cmd))
	{
		*abort_connection = 1;
		return -1;
	}

	if (send(s, path, len, 0) != len)
	{
		*abort_connection = 1;
		return -1;
	}

	if (recv(s, &res, sizeof(res), MSG_WAITALL) != sizeof(res))
	{
		*abort_connection = 1;
		return -1;
	}

	return (res.file_size);
}

//netiso
static sys_event_queue_t command_queue = -1;
static uint64_t discsize;
enum STORAGE_COMMAND{CMD_READ_ISO};

static int read_remote_file_critical(int g_socket, uint64_t offset, void *buf, uint32_t size)
{
	netiso_read_file_critical_cmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = NETISO_CMD_READ_FILE_CRITICAL;
	cmd.num_bytes = size;
	cmd.offset = offset;
	if (send(g_socket, &cmd, sizeof(cmd), 0) != sizeof(cmd)) return -1;
	if (recv(g_socket, buf, size, MSG_WAITALL) != (int)size) return -1;
	return 0;
}

static int process_read_iso_cmd(int g_socket, uint8_t *buf, uint64_t offset, uint32_t size)
{
	uint64_t read_end = offset + size;

	if (read_end >= discsize)
	{
		if (offset >= discsize)
		{
			memset(buf, 0, size);
			return 0;
		}

		memset(buf+(discsize-offset), 0, read_end-discsize);
		size = discsize-offset;
	}

	return read_remote_file_critical(g_socket, offset, buf, size);
}

static inline int sys_storage_ext_mount_discfile_proxy(sys_event_port_t result_port, sys_event_queue_t command_queue, int emu_type, uint64_t disc_size_bytes, uint32_t read_size, unsigned int trackscount, ScsiTrackDescriptor *tracks)
{
	system_call_8(8, SYSCALL8_OPCODE_MOUNT_DISCFILE_PROXY, result_port, command_queue, emu_type, disc_size_bytes, read_size, trackscount, (uint64_t)(uint32_t)tracks);
	return (int)p1;
}

static void netiso_thread(uint64_t arg)
{
	netiso_args *args;
	sys_event_port_t result_port;
	sys_event_queue_attribute_t queue_attr;
	int64_t ret64;
	int ret;

	args = (netiso_args *)(uint32_t)arg;

	ret64 = open_remote_file_2(args->numtracks, args->path, &ret);	if (ret64 < 0) {args->numtracks=0; goto quit1;}
	discsize = (uint64_t)ret64;

	ret = sys_event_port_create(&result_port, 1, SYS_EVENT_PORT_NO_NAME); if (ret != 0) goto quit1;
	sys_event_queue_attribute_initialize(queue_attr);
	ret = sys_event_queue_create(&command_queue, &queue_attr, 0, 1); if (ret != 0) goto quit2;

	if (args->emu_mode != 0)	fake_eject_event();

	ret = sys_storage_ext_mount_discfile_proxy(result_port, command_queue, EMU_DVD, discsize, 256*1024, 0, NULL);
	fake_insert_event(args->emu_mode); if (ret != 0) goto quit2;

	while (working)
	{
		sys_event_t event;

		ret = sys_event_queue_receive(command_queue, &event, 0);
		if (ret != 0) break;

		void *buf = (void *)(uint32_t)(event.data3>>32ULL);
		uint64_t offset = event.data2;
		uint32_t size = event.data3&0xFFFFFFFF;

		if(event.data1==CMD_READ_ISO)
			ret = process_read_iso_cmd(args->numtracks, buf, offset, size);

		ret = sys_event_port_send(result_port, ret, 0, 0);
		if (ret != 0) break;
	}

	fake_eject_event();
	sys_storage_ext_umount_discfile();

	sys_event_port_disconnect(result_port);

	sys_event_queue_destroy(command_queue, SYS_EVENT_QUEUE_DESTROY_FORCE);
	command_queue = (sys_event_queue_t)-1;

quit2:
	sys_event_port_destroy(result_port);

quit1:
	sys_ppu_thread_exit(0);
}

static void netiso_stop_thread(uint64_t arg)
{
	uint64_t exit_code;

	if (command_queue != (sys_event_queue_t)-1)
		sys_event_queue_destroy(command_queue, SYS_EVENT_QUEUE_DESTROY_FORCE);

	if (thread_id_net != (sys_ppu_thread_t)-1)
		sys_ppu_thread_join(thread_id_net, &exit_code);

	command_queue = (sys_event_queue_t)-1;

	sys_ppu_thread_exit(0);
}

#ifdef USE_NTFS
//rawseciso
static uint64_t sec_size =  512;
static uint32_t *sections, *sections_size, num_sections;

static inline void get_next_read(uint64_t discoffset, uint64_t bufsize, uint64_t *offset, uint64_t *readsize, int *idx)
{
	uint64_t base = 0;
	*idx = -1;
	*readsize = bufsize;
	*offset = 0;

	for (uint32_t i = 0; i < num_sections; i++)
	{
		uint64_t last = base + ((uint64_t)sections_size[i] * sec_size);

		if (discoffset >= base && discoffset < last)
		{
			uint64_t maxfileread = last-discoffset;

			if (bufsize > maxfileread)
				*readsize = maxfileread;
			else
				*readsize = bufsize;

			*idx = i;
			*offset = discoffset-base;
			return;
		}

		base += ((uint64_t)sections_size[i] * sec_size);
	}
}

static int process_read_iso_cmd_rs(sys_device_handle_t handle, uint8_t *buf, uint64_t offset, uint64_t size)
{
	uint64_t remaining;

	//printf("read iso: %p %lx %lx\n", buf, offset, size);
	remaining = size;

	while (remaining > 0)
	{
		uint64_t pos, readsize;
		int idx;
		int ret;
		uint8_t tmp[sec_size];
		uint32_t sector;
		uint32_t r;

		get_next_read(offset, remaining, &pos, &readsize, &idx);

		if (idx == -1 || sections[idx] == 0xFFFFFFFF)
		{
			memset(buf, 0, readsize);
			buf += readsize;
			offset += readsize;
			remaining -= readsize;
			continue;
		}

		if (pos & (sec_size-1))
		{
			uint64_t csize;

			sector = sections[idx] + pos/sec_size;
			ret = sys_storage_read(handle, 0, sector, 1, tmp, &r, 0);
			if (ret != 0 || r != 1)
			{
				//printf("sys_storage_read failed: %x 1 -> %x\n", sector, ret);
				return -1;
			}

			csize = sec_size-(pos&(sec_size-1));

			if (csize > readsize)
				csize = readsize;

			memcpy(buf, tmp+(pos&(sec_size-1)), csize);
			buf += csize;
			offset += csize;
			pos += csize;
			remaining -= csize;
			readsize -= csize;
		}

		if (readsize > 0)
		{
			uint32_t n = readsize / sec_size;

			if (n > 0)
			{
				uint64_t s;

				sector = sections[idx] + pos/sec_size;
				ret = sys_storage_read(handle, 0, sector, n, buf, &r, 0);
				if (ret != 0 || r != n)
				{
					//printf("sys_storage_read failed: %x %x -> %x\n", sector, n, ret);
					return -1;
				}

				s = n * sec_size;
				buf += s;
				offset += s;
				pos += s;
				remaining -= s;
				readsize -= s;
			}

			if (readsize > 0)
			{
				sector = sections[idx] + pos/sec_size;
				ret = sys_storage_read(handle, 0, sector, 1, tmp, &r, 0);
				if (ret != 0 || r != 1)
				{
					//printf("sys_storage_read failed: %x 1 -> %x\n", sector, ret);
					return -1;
				}

				memcpy(buf, tmp, readsize);
				buf += readsize;
				offset += readsize;
				remaining -= readsize;
			}
		}
	}

	return 0;
}

static void rawseciso_thread(uint64_t arg)
{
	rawseciso_args *args;
	sys_event_port_t result_port;
	sys_event_queue_attribute_t queue_attr;
	sys_device_handle_t handle = -1;
	int ret;

	args = (rawseciso_args *)(uint32_t)arg;

	num_sections = args->num_sections;
	sections = (uint32_t *)(args+1);
	sections_size = sections + num_sections;

	discsize = 0;

	for (uint32_t i = 0; i < num_sections; i++)
		discsize += sections_size[i];

	sec_size = args->num_tracks;
	discsize = discsize * sec_size;
	if(!discsize) goto exit_thread4;

	if(sys_storage_open(args->device, 0, &handle, 0)) goto exit_thread4;
	if(sys_event_port_create(&result_port, 1, SYS_EVENT_PORT_NO_NAME)) goto exit_thread3;

	sys_event_queue_attribute_initialize(queue_attr);
	if(sys_event_queue_create(&command_queue, &queue_attr, 0, 1)) goto exit_thread2;

	if (args->emu_mode != 0)	fake_eject_event();
	ret = sys_storage_ext_mount_discfile_proxy(result_port, command_queue, EMU_DVD, discsize, 256*1024, 0, NULL);
	fake_insert_event(args->emu_mode);

	if (ret != 0) goto exit_thread;

	while(working)
	{
		sys_event_t event;

		ret = sys_event_queue_receive(command_queue, &event, 0);
		if (ret != 0) break;

		void *buf = (void *)(uint32_t)(event.data3>>32ULL);
		uint64_t offset = event.data2;
		uint32_t size = event.data3&0xFFFFFFFF;

		if(event.data1==CMD_READ_ISO)
			ret = process_read_iso_cmd_rs(handle, buf, offset, size);

		ret = sys_event_port_send(result_port, ret, 0, 0);
		if (ret != 0) break;
	}

	fake_eject_event();
	sys_storage_ext_umount_discfile();

exit_thread:
	sys_event_port_disconnect(result_port);
	sys_event_queue_destroy(command_queue, SYS_EVENT_QUEUE_DESTROY_FORCE);
	command_queue = (sys_event_queue_t)-1;

exit_thread2:
	sys_event_port_destroy(result_port);
exit_thread3:
	sys_storage_close(handle);
exit_thread4:
	sys_ppu_thread_exit(0);

}

static void rawseciso_stop_thread(uint64_t arg)
{
	uint64_t exit_code;

	if (command_queue != (sys_event_queue_t)-1)
		sys_event_queue_destroy(command_queue, SYS_EVENT_QUEUE_DESTROY_FORCE);

	if (thread_id_rs != (sys_ppu_thread_t)-1)
		sys_ppu_thread_join(thread_id_rs, &exit_code);

	command_queue = (sys_event_queue_t)-1;

	sys_ppu_thread_exit(0);
}

#endif

static void absPath(char* absPath_s, const char* path, const char* cwd)
{
	if(path[0] == '/')
		strcpy(absPath_s, path);
	else
	{
		strcpy(absPath_s, cwd);

		if(cwd[strlen(cwd) - 1] != '/')
			strcat(absPath_s, "/");

		strcat(absPath_s, path);
	}
}

static int isDir(const char* path)
{
	struct CellFsStat s;
	if(cellFsStat(path, &s)==CELL_FS_SUCCEEDED)
		return ((s.st_mode & CELL_FS_S_IFDIR) != 0);
	else
		return 0;
}


static int ssplit(const char* str, char* left, int lmaxlen, char* right, int rmaxlen)
{
	int ios = strcspn(str, " ");
	int ret = (ios < (int)strlen(str) - 1);
	int lmax = (ios < lmaxlen) ? ios : lmaxlen;

	strncpy(left, str, lmax);
	left[lmax] = '\0';

	if(ret)
	{
		strncpy(right, str + ios + 1, rmaxlen);
		right[rmaxlen] = '\0';
	}
	else
	{
		right[0] = '\0';
	}

	return ret;
}

static int slisten(int port, int backlog)
{
	int list_s = socket(AF_INET, SOCK_STREAM, 0);
	if(list_s<0) return list_s;

	struct sockaddr_in sa;
	socklen_t sin_len = sizeof(sa);
	memset(&sa, 0, sin_len);

	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(list_s, (struct sockaddr *)&sa, sin_len);
	listen(list_s, backlog);

	return list_s;
}

void sclose(int *socket_e)
{
	if(*socket_e != -1)
	{
		shutdown(*socket_e, SHUT_RDWR);
		socketclose(*socket_e);
		*socket_e = -1;
	}
}

static int open_remote_dir(int s, char *path, int *abort_connection)
{
	netiso_open_dir_cmd cmd;
	netiso_open_dir_result res;
	int len;

	*abort_connection = 0;

	len = strlen(path);
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = (NETISO_CMD_OPEN_DIR);
	cmd.dp_len = (len);

	if (send(s, &cmd, sizeof(cmd), 0) != sizeof(cmd))
	{
		*abort_connection = 1;
		return -1;
	}

	if (send(s, path, len, 0) != len)
	{
		*abort_connection = 1;
		return -1;
	}

	if (recv(s, &res, sizeof(res), MSG_WAITALL) != sizeof(res))
	{
		*abort_connection = 1;
		return -1;
	}

	return (res.open_result);
}

static int read_remote_dir(int s, u32 *data2 /*netiso_read_dir_result_data **data*/, int *abort_connection)
{
	netiso_read_dir_entry_cmd cmd;
	netiso_read_dir_result res;
	int len;

	*abort_connection = 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = (NETISO_CMD_READ_DIR);

	if (send(s, &cmd, sizeof(cmd), 0) != sizeof(cmd))
	{
		*abort_connection = 1;
		return -1;
	}
	if (recv(s, &res, sizeof(res), MSG_WAITALL) != sizeof(res))
	{
		*abort_connection = 1;
		return -1;
	}

	if (res.dir_size > 0)
	{
		if(res.dir_size>3300) res.dir_size=3300; // 529 bytes/entry
again:
		len = sizeof(netiso_read_dir_result_data)*res.dir_size;
		u8* data=(u8*)malloc(((len+1024)/1024)*1024);
		if(!data && res.dir_size>10)
		{
			res.dir_size-=10;
			goto again;
		}

		if(data)
		{
			if (recv(s, data, len, MSG_WAITALL) != len)
			{
				*abort_connection = 1;
				free(data);
				return -1;
			}
		}
		*data2=(u32)data;
	}

	return (res.dir_size);
}

static void set_default_icon(char* icon, char *path, char *file)
{
	strcpy(icon, WMRES "/");
	if(strstr(path, "/PS2ISO"))
		strcat(icon, "PS2");
	else if(strstr(path, "/PSXISO") || strstr(path, "/PSXGAMES") || strstr(file, ".ntfs[PSXISO]") || strstr(file, ".ntfs[PSXGAMES]"))
		strcat(icon, "PS1");
	else if(strstr(path, "/PSPISO") || strstr(path, "/ISO"))
		strcat(icon, "PSP");
	else if(strstr(path, "/BDISO") || strstr(file, ".ntfs[BDISO]"))
		strcat(icon, "BDM");
	else if(strstr(path, "/DVDISO")  || strstr(file, ".ntfs[DVDISO]"))
		strcat(icon, "DVD");
	else
		strcat(icon, "PS3");
	strcat(icon, ".png");
}

static void find_cover(char* tempID, char* icon)
{
	struct CellFsStat buf;
	sprintf(icon, MM_ROOT "/covers/%s.JPG", tempID);
	if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
	{
		sprintf(icon, MM_ROOT "/covers/%s.PNG", tempID);
		if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
		{
			sprintf(icon, WMTMP "/%s.JPG", tempID);
			if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
			{
				sprintf(icon, WMTMP "/%s.PNG", tempID);
				if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED) icon[0]=0;
			}
		}
	}
}

static void set_html(u8 prop, int s, const char* id, const char* value_c, u32 value_n)
{
	char tmp[64];
	sprintf(tmp,"document.getElementById(\"%s\").", id);
	ssend(s, tmp);

	// checkbox
	if(!prop)
		sprintf(tmp, "checked=%i;", value_n);

	// string value
	else if(prop==1)
		sprintf(tmp, "value=\"%s\";", value_c);

	// numeric value
	else if(prop==2)
		sprintf(tmp, "value=\"%i\";", value_n);

	// innerHTML string
	else if(prop==3)
		sprintf(tmp, "innerHTML=\"%s\";", value_c);

	ssend(s, tmp);
}

static void send_file(const char* filename, int s)
{
	int fd;

	if(cellFsOpen(filename, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		u8* buffer=(u8*)malloc(8192);

		if(buffer)
		{
			u64 read_e = 0;
			while(working)
			{
				if(cellFsRead(fd, (void *)buffer, 8192, &read_e)==CELL_FS_SUCCEEDED)
				{
					if(read_e>0)
						{if(send(s, buffer, (size_t)read_e, 0)<0) break;}
					else
						break;
				}
				else
					break;
			}
			free(buffer);
		}
		cellFsClose(fd);

		if(strstr(filename, "www_foot"))
		{
			ssend(s, "<script language=\"javascript\">");
			set_html(3, s, "smv", "sMAN " WM_VERSION, 0);
			ssend(s, "</script></body></html>");
		}
	}
}

#define SPRX_PLUGIN "idle_plugin"

static void write_xml_entry(int fd, u16 rf, const char* icon, const char* title, const char* path, u8 type, const char* id, char* addr)
{
	snprintf(addr, 1024, "<Table key=\"%04i\"><Pair key=\"icon\"><String>%s</String></Pair><Pair key=\"title\"><String>%s</String></Pair><Pair key=\"module_name\"><String>" SPRX_PLUGIN "</String></Pair><Pair key=\"module_action\"><String>%s</String></Pair><Pair key=\"bar_action\"><String>none</String></Pair><Pair key=\"info\"><String>",
			rf, icon, title, path);
	for(u16 u=0;u<strlen(addr);u++) if(addr[u]==0xA) addr[u]=0x20;

	if(type==TYPE_PS3)
	{
		strcat(addr, id);
		strcat(addr, " | ");
	}
		 if(strstr(path, ".ntfs[")  ) strcat(addr, "NTFS");
	else if(strstr(path, "dev_usb") ) strcat(addr, "USB");
	else if(strstr(path, "dev_hdd0")) strcat(addr, "HDD");
	else if(strstr(path, "/net")    ) strcat(addr, "NET");
		 if(strstr(path, "/GAME")	) strcat(addr, " | JB");

	strcat(addr, "</String></Pair></Table>");

	cellFsWrite(fd, addr, strlen(addr), NULL);
}

static void write_xml_tbl(int fd, u16 rf, const char* title, char* addr, u16 count)
{
	if(rf>=1 && rf<=5)
		snprintf(addr, 1024, "<Table key=\"rf%i\"><Pair key=\"icon\"><String>" WMRES "/rf%i.png</String></Pair><Pair key=\"title\"><String>%s</String></Pair><Pair key=\"str_noitem\"><String>msg_error_no_content</String></Pair><Pair key=\"info\"><String>%i %s</String></Pair></Table>",
			rf, rf, title, count, (count==1?"title":"titles"));

	cellFsWrite(fd, addr, strlen(addr), NULL);
}

static void create_xml(void)
{
		char* addr		=(char*)malloc(1024);
		int fd=0, fs=0;
		u64 tmp=0;
		u16 types[TYPE_MAX];

		cellFsOpen(SMAN_XML, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0);
		strncpy(addr, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><XMBML version=\"1.0\"><View id=\"seg_mygames\"><Attributes><Table key=\"eject\"><Pair key=\"icon\"><String>" WMRES "/sm_e.png</String></Pair><Pair key=\"title\"><String>" STR_UNMOUNT "</String></Pair><Pair key=\"module_name\"><String>" SPRX_PLUGIN "</String></Pair><Pair key=\"module_action\"><String>/mount_ps3/unmount</String></Pair><Pair key=\"bar_action\"><String>none</String></Pair></Table>", 1024);
		write_xml_tbl(fd, 0, NULL, addr, 0);

		u32 _games=(file_exists(SMAN_BIN))/sizeof(_slaunch);
		_slaunch slaunch;

		if(cellFsOpen((char*)SMAN_BIN, CELL_FS_O_RDONLY, &fs, NULL, 0) == CELL_FS_SUCCEEDED)
		{
			for(u16 i=0; i<_games; i++)
			{
				if(cellFsRead(fs, &slaunch, sizeof(_slaunch), NULL) == CELL_FS_SUCCEEDED)
					types[slaunch.type]++;
			}
			cellFsClose(fs);

			for(u8 i=1;i<TYPE_MAX;i++)
				if(types[i])
					write_xml_tbl(fd, i,

							(i==1?"PLAYSTATION®1":
								(i==2?"PLAYSTATION®2":
									(i==3?"PLAYSTATION®3":
										(i==4?"PLAYSTATION®PORTABLE":
												"Blu-ray™ and DVD"
										)
									)
								)
							), addr, types[i]);
		}

		strncpy(addr, "<Table key=\"sman\"><Pair key=\"icon\"><String>" WMRES "/sm_f.png</String></Pair><Pair key=\"title\"><String>sMAN GUI</String></Pair><Pair key=\"info\"><String>" STR_HOLDL2R2 "</String></Pair><Pair key=\"module_name\"><String>" SPRX_PLUGIN "</String></Pair><Pair key=\"module_action\"><String>sman</String></Pair><Pair key=\"bar_action\"><String>none</String></Pair></Table>", 1024);
		write_xml_tbl(fd, 0, NULL, addr, 0);
		strncpy(addr, "<Table key=\"setup\"><Pair key=\"icon\"><String>" WMRES "/sm_s.png</String></Pair><Pair key=\"title\"><String>" STR_SETUP "</String></Pair><Pair key=\"module_name\"><String>webbrowser_plugin</String></Pair><Pair key=\"module_action\"><String>http://127.0.0.1/setup.ps3</String></Pair></Table></Attributes><Items><Item class=\"type:x-xmb/module-action\" key=\"eject\" attr=\"eject\"/>", 1024);
		write_xml_tbl(fd, 0, NULL, addr, 0);

		for(u8 i=1;i<TYPE_MAX;i++)
			if(types[i])
			{
				snprintf(addr, 1024, "<Query class=\"type:x-xmb/folder-pixmap\" key=\"rf%i\" attr=\"rf%i\" src=\"#seg_rf%i\"/>", i, i, i);
				write_xml_tbl(fd, 0, NULL, addr, 0);
			}

		strncpy(addr, "<Item class=\"type:x-xmb/module-action\" key=\"sman\" attr=\"sman\"/><Item class=\"type:x-xmb/module-action\" key=\"setup\" attr=\"setup\"/></Items></View>", 1024);
		write_xml_tbl(fd, 0, NULL, addr, 0);

		if(cellFsOpen((char*)SMAN_BIN, CELL_FS_O_RDONLY, &fs, NULL, 0) == CELL_FS_SUCCEEDED)
		{
			for(u8 i=1;i<TYPE_MAX;i++)
				if(types[i])
				{
					snprintf(addr, 1024, "<View id=\"seg_rf%i\"><Attributes>", i);
					write_xml_tbl(fd, 0, NULL, addr, 0);
					cellFsLseek(fs, 0, CELL_FS_SEEK_SET, &tmp);
					for(u16 j=0;j<_games;j++)
					{
						cellFsRead(fs, &slaunch, sizeof(_slaunch), NULL);
						if(slaunch.type==i)
							write_xml_entry(fd, j, slaunch.icon, slaunch.name, slaunch.path, i, slaunch.id, addr);

					}
					cellFsWrite(fd, "</Attributes><Items>", 20, NULL);

					cellFsLseek(fs, 0, CELL_FS_SEEK_SET, &tmp);
					for(u16 j=0;j<_games;j++)
					{
						cellFsRead(fs, &slaunch, sizeof(_slaunch), NULL);
						if(slaunch.type==i)
						{
							snprintf(addr, 1024, "<Item class=\"type:x-xmb/module-action\" key=\"%04i\" attr=\"%04i\"/>", j, j);
							cellFsWrite(fd, addr, strlen(addr), NULL);
						}
					}
					cellFsWrite(fd, "</Items></View>", 15, NULL);
				}

			cellFsClose(fs);
		}

		cellFsWrite(fd, "</XMBML>", 8, NULL);
		cellFsClose(fd);

		free(addr);
}

static void content_scan(u64 arg0)
{
		init_running=1;

		struct CellFsStat buf;
		int fd, fdsl;

		int conn_s=arg0>>32;
		u32 arg = arg0&0xffffffff;

		char* addr		=(char*)malloc(6144);	if(!addr) {init_running=0; return;}

		char* templn	=addr;			// 1024
		char* tempstr	=addr+1024;		// 4096
		char* param		=addr+5120;		//  512
		char* icon		=addr+5632;		//  512
		netiso_args *mynet_iso	= (netiso_args*)malloc(2048);

		u8 is_net=0;
		u8 delay_mount=0;
		sys_ppu_thread_t thr_id;
		uint64_t exit_code;

		if(sm_config->xmbi)
		{
			cellFsOpen("/dev_hdd0/xmlhost/game_plugin/fb.xml", CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0);
			sprintf(tempstr, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><XMBML version=\"1.0\"><View id=\"seg_fb\"><Attributes><Table key=\"mgames\"><Pair key=\"icon\"><String>" WMRES "/rf0.png</String></Pair><Pair key=\"title\"><String>sMAN</String></Pair><Pair key=\"info\"><String>ver. " WM_VERSION "</String></Pair><Pair key=\"ingame\"><String>disable</String></Pair></Table></Attributes><Items><Query class=\"type:x-xmb/folder-pixmap\" key=\"mgames\" attr=\"mgames\" src=\"xmb://localhost" SMAN_XML "#seg_mygames\"/></Items></View></XMBML>");
			cellFsWrite(fd, tempstr, strlen(tempstr), NULL);
			cellFsClose(fd);
			if(arg==0xC0FEBABA) goto leave;
		}

		do_umount_iso();

		int discboot=0xff;
		xsetting_0AF1F161()->GetSystemDiscBootFirstEnabled(&discboot);

		if(arg==0xC0FEBABE && sm_config->refr==1)
			goto load_last_game;

		else
		{
			show_msg(STR_SCANS);

			if(discboot==1)
				xsetting_0AF1F161()->SetSystemDiscBootFirstEnabled(0);

			memset(&slaunch, 0, sizeof(_slaunch));
			cellFsUnlink(SMAN_BIN);
			cellFsOpen(SMAN_BIN, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fdsl, NULL, 0);

			int ns=-2;
			unsigned int real_disctype;
			u8 retries=0;
			u8 max_usb=1;

			led(YELLOW, BLINK);

			for(u8 f0=0; f0<10; f0++)
			{
				if(f0>1 && f0<7) continue;
				max_usb=1;
				if(f0==1) max_usb=128; // /dev_usb***
				for(u8 ud=0; ud<max_usb; ud++)
				{
					if(f0==1)
					{
						sprintf(drives[1], "/dev_usb%03i", ud);
						if(cellFsStat(drives[1], &buf)!=CELL_FS_SUCCEEDED) continue;
					}

					for(u8 f1=0; f1<10; f1++)
					{

						if(f0==9 && f1) break; //ntfs
						if(f0==7 && (!sm_config->netd0 || f1>6)) break;
						if(f0==8 && (!sm_config->netd1 || f1>6)) break;
						if(f0==7 || f0==8) is_net=1; else is_net=0;

						if(is_net && ns==-2 &&
							( (f0==7 && sm_config->netp0 && sm_config->neth0[0]) ||
							(  f0==8 && sm_config->netp1 && sm_config->neth1[0]) )
							)
						{
reconnect:
							if(f0==7)
								ns=connect_to_server(sm_config->neth0, sm_config->netp0);
							else
								ns=connect_to_server(sm_config->neth1, sm_config->netp1);
							if(ns<0)
							{
								if(retries<3)
								{
									retries++;
									sys_timer_usleep(300000);
									goto reconnect;
								}
							}
						}

						if(ns<0 && is_net) break;

						if(is_net)
							sprintf(param, "/%s", paths[f1]);
						else
						{
							if(f0==9)//ntfs
								strcpy(param, WMTMP);
							else
								sprintf(param, "%s/%s", drives[f0], paths[f1]);
						}

						if(f1==5 && f0!=0) continue; // PS2ISO supported only from /dev_hdd0

#ifdef USE_NTFS
						if(f0==9)
							prepNTFS(conn_s);
#endif
						if(!is_net && cellFsOpendir( param, &fd) != CELL_FS_SUCCEEDED) continue;

						int abort_connection=0;
						if(is_net && open_remote_dir(ns, param, &abort_connection) < 0) continue;

						//CellFsDirent entry;
						CellFsDirectoryEntry entry;
						u32 read_e;
						int fdw, fs;

						u64 msiz = 0;

						u8 is_iso=0;
						char tempID[16]; tempID[0]=0;

						u32 data2=NULL;
						int v3_entries=0;
						int v3_entry=0;

						int is_directory=0;
						int64_t file_size;
						u64 mtime, ctime, atime;
						int bytes_read=0;
						int boff=0;

						netiso_read_dir_result_data *data=NULL;

						if(is_net)
						{
							v3_entries = read_remote_dir(ns, &data2, &abort_connection);
							if(data2==NULL) continue;
							data=(netiso_read_dir_result_data*)data2;
							sys_storage_ext_get_disc_type(&real_disctype, NULL, NULL);
							if (real_disctype != 0)	fake_eject_event();
						}

						while
						(
							(is_net && v3_entry<v3_entries) ||
							(!is_net &&	!cellFsGetDirectoryEntries(fd, &entry, sizeof(entry), &read_e) && read_e > 0)
						)
						{
							if(is_net)
							{
								icon[0]=0;

								if(!data[v3_entry].is_directory)
								{
									if(!strstr(data[v3_entry].name, ".ISO") && !strstr(data[v3_entry].name, ".iso") && !strstr(data[v3_entry].name, ".BIN") && !strstr(data[v3_entry].name, ".bin") && !strstr(data[v3_entry].name, ".MDF") && !strstr(data[v3_entry].name, ".mdf") && !strstr(data[v3_entry].name, ".IMG") && !strstr(data[v3_entry].name, ".img")) {v3_entry++; continue;}
									if(data[v3_entry].name[strlen(data[v3_entry].name)-4]!='.') {v3_entry++; continue;}
								}
								else
									if(!strstr(param, "/GAME")) {v3_entry++; continue;}

								if(f1<3)//PS3 games only
								{
									sprintf(templn, WMTMP "/%s.SFO", data[v3_entry].name);
									if(!data[v3_entry].is_directory)
									{
										templn[strlen(templn)-7]=0; strcat(templn, "SFO");
									}

									if(data[v3_entry].is_directory && cellFsStat(templn, &buf)!=CELL_FS_SUCCEEDED)
									{
										sprintf(templn, "%s/%s/PS3_GAME/PARAM.SFO", param, data[v3_entry].name);
										if(remote_stat(ns, templn, &is_directory, &file_size, &mtime, &ctime, &atime, &abort_connection)!=0) {v3_entry++; continue;}
										sprintf(templn, WMTMP "/%s.SFO", data[v3_entry].name);

										if(file_size && cellFsOpen(templn, CELL_FS_O_CREAT|CELL_FS_O_RDWR|CELL_FS_O_TRUNC, &fdw, NULL, 0)==CELL_FS_SUCCEEDED)
										{
											sprintf(templn, "%s/%s/PS3_GAME/PARAM.SFO", param, data[v3_entry].name);
											open_remote_file_2(ns, templn, &abort_connection);
											boff=0;

											while(boff<file_size)
											{
												bytes_read = read_remote_file(ns, (char*)tempstr, boff, 4096, &abort_connection);
												if(bytes_read)
													cellFsWrite(fdw, (char*)tempstr, bytes_read, NULL);
												boff+=bytes_read;
												if(bytes_read<4096 || boff>=file_size) break;
											}
											cellFsClose(fdw);
										}
										sprintf(templn, WMTMP "/%s.SFO", data[v3_entry].name);
									}

									msiz = read_file(templn, (void*)tempstr, 4096);

									if(!msiz && f1==2 && !data[v3_entry].is_directory && (strstr(data[v3_entry].name, ".ISO") || strstr(data[v3_entry].name, ".iso"))) //PS3ISO only
									{
										memset(mynet_iso, 0, 2048);

										sprintf(mynet_iso->path, "%s/%s", param, data[v3_entry].name);
										mynet_iso->numtracks=ns;
										mynet_iso->emu_mode=real_disctype;

										sys_ppu_thread_create(&thread_id_net, netiso_thread, (u64)(u32)mynet_iso, -0x1d8, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_NETS);

										for(u8 n=0;n<30;n++)
										{
											sys_timer_usleep(130000);
											if(cellFsStat("/dev_bdvd/PS3_GAME/PARAM.SFO", &buf)==CELL_FS_SUCCEEDED)
											{
												msiz = copy_file("/dev_bdvd/PS3_GAME/PARAM.SFO", templn);

												if(msiz>256)
												{
													msiz = read_file(templn, (void*)tempstr, 4096);

													templn[strlen(templn)-4]=0; strcat(templn, ".PNG");
													if(cellFsStat(templn, &buf)!=CELL_FS_SUCCEEDED)
														copy_file("/dev_bdvd/PS3_GAME/ICON0.PNG", templn);

												}
												break;
											}
										}
										sys_timer_usleep(120000);
										sys_ppu_thread_create(&thr_id, netiso_stop_thread, 0, 0, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, "");
										sys_ppu_thread_join(thr_id, &exit_code);
										open_remote_file_2(ns, (char*)"/CLOSEFILE", &abort_connection);
										if(abort_connection || mynet_iso->numtracks==0)
										{
											if(f0==7)
												ns=connect_to_server(sm_config->neth0, sm_config->netp0);
											else
												ns=connect_to_server(sm_config->neth1, sm_config->netp1);
											if(ns<0) break;
										}
									}

									strcpy(templn, data[v3_entry].name);
									if(msiz>256)
									{
										param_sfo_info((u8*)tempstr, msiz, (!sm_config->sfo ? templn : NULL), tempID);

										icon[0]=0;
										if(tempID[0]) find_cover(tempID, icon);
									}
								}
								else
									strcpy(templn, data[v3_entry].name);

								if(templn[strlen(templn)-4]=='.') templn[strlen(templn)-4]=0;

								if(!icon[0])
								{
									sprintf(icon, WMTMP "/%s.PNG", data[v3_entry].name);
									boff=strlen(icon);
									if(!data[v3_entry].is_directory)
									{
										icon[boff-7]=0; strcat(icon, "PNG");
										boff=strlen(icon);
									} else goto go3;

									if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
									{
										icon[boff-3]=0; strcat(icon, "JPG");
									} else goto c_load2;

									if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
									{
										icon[boff-3]=0; strcat(icon, "jpg");
									} else goto c_load2;

									if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
									{
										icon[boff-3]=0; strcat(icon, "png");
									} else goto c_load2;
go3:
									if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
									{
										if(data[v3_entry].is_directory)
										{
											sprintf(tempstr, "%s/%s/PS3_GAME/ICON0.PNG", param, data[v3_entry].name);
											icon[boff-3]=0; strcat(icon, "PNG");
										}
										else
										{
											sprintf(tempstr, "%s/%s", param, data[v3_entry].name);
											icon[boff-3]=0; strcat(icon, "jpg");
											tempstr[strlen(tempstr)-3]=0; strcat(tempstr, "jpg");
										}

										abort_connection=0;
										if(remote_stat(ns, tempstr, &is_directory, &file_size, &mtime, &ctime, &atime, &abort_connection)!=0)
										{
											tempstr[strlen(tempstr)-3]=0; strcat(tempstr, "png");
											icon[boff-3]=0; strcat(icon, "PNG");

											if(remote_stat(ns, tempstr, &is_directory, &file_size, &mtime, &ctime, &atime, &abort_connection)!=0)
											{
												icon[0]=0;
												goto c_load2;
											}
										}

										if(file_size && open_remote_file_2(ns, tempstr, &abort_connection)>0 && !abort_connection)
										{
											if(cellFsOpen(icon, CELL_FS_O_CREAT|CELL_FS_O_RDWR|CELL_FS_O_TRUNC, &fdw, NULL, 0)==CELL_FS_SUCCEEDED)
											{
												boff=0;
												while(boff<file_size)
												{
													bytes_read = read_remote_file(ns, (char*)tempstr, boff, 4096, &abort_connection);
													if(bytes_read)
														cellFsWrite(fdw, (char*)tempstr, bytes_read, NULL);
													boff+=bytes_read;
													if(bytes_read<4096 || abort_connection || boff>=file_size) break;
												}
												cellFsClose(fdw);
												if(boff<1 || abort_connection) cellFsUnlink(icon);
												open_remote_file_2(ns, (char*)"/CLOSEFILE", &bytes_read);
											}
										}
									}
								}
c_load2:
								if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
									set_default_icon(icon, param, data[v3_entry].name);

								memset(&slaunch, 0, sizeof(_slaunch));
								strncpy(slaunch.name, templn, 128);
								strncpy(slaunch.icon, icon, 160);
								snprintf(slaunch.path, 160, "/mount_ps3/net%i%s/%s", (f0-7), param, data[v3_entry].name);
								strncpy(slaunch.id, tempID, 9);
								slaunch.type=TYPE_PS3;
								if(strstr(param, "/PSX")) slaunch.type=TYPE_PS1;
								else if((strstr(param, "/BDISO") || strstr(param, "/DVDISO"))) slaunch.type=TYPE_VID;
								cellFsWrite(fdsl, (void *)&slaunch, sizeof(_slaunch), NULL);

								v3_entry++;
							}
							else
							{
								sprintf(templn, "%s/%s/PS3_GAME/PARAM.SFO", param, entry.entry_name.d_name);
								is_iso=((strstr(param, "/PS3ISO") || strstr(param, "/PS2ISO") ||
										strstr(param, "/PSPISO") || strstr(param, "/ISO")||
										strstr(param, "/PSX") || strstr(param, "/DVDISO") || strstr(param, "/BDISO"))
									&&
									(
									  (strstr(entry.entry_name.d_name, ".ISO") || strstr(entry.entry_name.d_name, ".iso") ||
										( (strstr(param, "/PS2") || strstr(param, "/PSX")) &&
											(strstr(entry.entry_name.d_name, ".BIN") || strstr(entry.entry_name.d_name, ".bin") || strstr(entry.entry_name.d_name, ".MDF") || strstr(entry.entry_name.d_name, ".mdf") || strstr(entry.entry_name.d_name, ".IMG") || strstr(entry.entry_name.d_name, ".img"))
										)
									   )
									&&
									  ((entry.entry_name.d_name[strlen(entry.entry_name.d_name)-1]=='0' && entry.entry_name.d_name[strlen(entry.entry_name.d_name)-2]=='.') || entry.entry_name.d_name[strlen(entry.entry_name.d_name)-4]=='.')
									)) || (strstr(param, WMTMP) && strstr(entry.entry_name.d_name, ".ntfs[") );
								if(is_iso || (!strstr(param, WMTMP) && !is_iso && entry.entry_name.d_name[0]!='.' && f1<2 && cellFsStat(templn, &buf)==CELL_FS_SUCCEEDED))
								{
									msiz=0;
									tempID[0]=0;
									if(!is_iso)
									{
										msiz=read_file(templn, (void*)tempstr, 4096);
										sprintf(templn, "%s", entry.entry_name.d_name);
										if(msiz>256)
											param_sfo_info((u8*)tempstr, msiz, (!sm_config->sfo ? templn : NULL), tempID);
									}
									else
									{
										tempID[0]=0;
										if((strstr(param, "/PS3ISO") && f0<9) || (f0==9 && strstr(entry.entry_name.d_name, "ntfs[PS3ISO]")))
										{
											sprintf(templn, WMTMP "/%s", entry.entry_name.d_name);
											if(strstr(templn, ".ntfs[")) templn[strrchr(templn, '.')-templn]=0;
											if(templn[strlen(templn)-2]=='.') templn[strlen(templn)-2]=0;
											if(templn[strlen(templn)-4]=='.') templn[strlen(templn)-4]=0;

											strcat(templn, ".PNG");

											if(f0<9 && f1==2 && cellFsStat(templn, &buf)!=CELL_FS_SUCCEEDED)
											{
												//extract icon0.png from hdd/usb (not from ntfs)
												char *cobra_iso_list[1];
												sprintf(icon, "%s/%s", param, entry.entry_name.d_name);
												cobra_iso_list[0]=icon;

												cobra_send_fake_disc_eject_event();
												sys_timer_usleep(10000);
												cobra_umount_disc_image();
												cobra_mount_dvd_disc_image(cobra_iso_list, 1);
												sys_timer_usleep(10000);
												cobra_send_fake_disc_insert_event();
												sys_timer_usleep(150000);

												sprintf(icon, WMTMP "/%s", entry.entry_name.d_name);
												if(icon[strlen(icon)-2]=='.') icon[strlen(icon)-2]=0;
												if(icon[strlen(icon)-4]=='.') icon[strlen(icon)-4]=0;

												strcat(icon, ".SFO");
												boff=strlen(icon);

												for(u8 n=0;n<50;n++)
												{
													sys_timer_usleep(150000);
													if(cellFsStat("/dev_bdvd/PS3_GAME/PARAM.SFO", &buf)==CELL_FS_SUCCEEDED)
													{
														if(copy_file("/dev_bdvd/PS3_GAME/PARAM.SFO", icon))
														{
															icon[boff-3]=0; strcat(icon, "PNG");
															copy_file("/dev_bdvd/PS3_GAME/ICON0.PNG", icon);
														}
														break;
													}
												}
												sys_timer_usleep(150000);
												cobra_send_fake_disc_eject_event();
												sys_timer_usleep(50000);
												cobra_umount_disc_image();
												sys_timer_usleep(150000);
											}

											templn[strlen(templn)-3]=0; strcat(templn, "SFO");

											msiz=read_file(templn, (void*)tempstr, 4096);
											if(msiz)
											{
												sprintf(templn, "%s", entry.entry_name.d_name);
												if(strstr(templn, ".ntfs[")) templn[strrchr(templn, '.')-templn]=0;
												if(templn[strlen(templn)-2]=='.') templn[strlen(templn)-2]=0;
												if(templn[strlen(templn)-4]=='.') templn[strlen(templn)-4]=0;
												if(msiz>256)
													param_sfo_info((u8*)tempstr, msiz, (!sm_config->sfo ? templn : NULL), tempID);
											}
											else
											{
												sprintf(templn, "%s", entry.entry_name.d_name);
												if(strstr(templn, ".ntfs[")) templn[strrchr(templn, '.')-templn]=0;
												if(templn[strlen(templn)-2]=='.') templn[strlen(templn)-2]=0;
												if(templn[strlen(templn)-4]=='.') templn[strlen(templn)-4]=0;
											}
										}
										else
										{
											sprintf(templn, "%s", entry.entry_name.d_name);
											if(f0==9)
											{
												if(strstr(templn, ".ntfs[")) templn[strrchr(templn, '.')-templn]=0;
												else continue;
											}

											if(templn[strlen(templn)-2]=='.') templn[strlen(templn)-2]=0;
											if(templn[strlen(templn)-4]=='.') templn[strlen(templn)-4]=0;
										}

										if(strstr(param, "/PS3ISO") && tempID[0]==0 && f0<9)
										{
											sprintf(icon, "%s/%s", param, entry.entry_name.d_name);
											if (cellFsOpen(icon, CELL_FS_O_RDONLY, &fs, NULL, 0) == CELL_FS_SUCCEEDED)
											{
												if(cellFsLseek(fs, 0x810, CELL_FS_SEEK_SET, &msiz) == CELL_FS_SUCCEEDED)
												{
													if(cellFsRead(fs, (void *)&tempID, 11, &msiz) == CELL_FS_SUCCEEDED)
													{
														tempID[4]=tempID[5];
														tempID[5]=tempID[6];
														tempID[6]=tempID[7];
														tempID[7]=tempID[8];
														tempID[8]=tempID[9];
														tempID[9]=0;
													}
												}
												cellFsClose(fs);
											}
										}
									}

									icon[0]=0;
									if(tempID[0]) find_cover(tempID, icon);

									if(is_iso)
									{
										if(!icon[0])
										{
											sprintf(icon, "%s/%s", param, entry.entry_name.d_name);
											if(strstr(icon, ".ntfs[")) icon[strrchr(icon, '.')-icon]=0;
											if(icon[strlen(icon)-2]=='.') icon[strlen(icon)-2]=0;
											boff=strlen(icon);
											if(icon[boff-4]=='.')
											{
												icon[boff-3]=0; strcat(icon, "jpg");
												if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
												{
													icon[boff-3]=0; strcat(icon, "JPG");
													if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
													{
														icon[boff-3]=0; strcat(icon, "png");
														if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
														{
															icon[boff-3]=0; strcat(icon, "PNG");
															if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED) icon[0]=0;
														}
													}
												}
											}

											if(!icon[0])
											{
												sprintf(icon, WMTMP "/%s", entry.entry_name.d_name);
												if(icon[strlen(icon)-2]=='.') icon[strlen(icon)-2]=0;
												if(icon[strlen(icon)-4]=='.') icon[strlen(icon)-4]=0;

												strcat(icon, ".jpg");
												boff=strlen(icon);
												if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
												{
													icon[boff-3]=0; strcat(icon, "JPG");
													if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
													{
														icon[boff-3]=0; strcat(icon, "png");
														if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED)
														{
															icon[boff-3]=0; strcat(icon, "PNG");
															if(cellFsStat(icon, &buf)!=CELL_FS_SUCCEEDED) icon[0]=0;
														}
													}
												}
											}
										}

										if(!icon[0]) set_default_icon(icon, param, entry.entry_name.d_name);

									}
									else
									{
										if(!icon[0])
											sprintf(icon, "%s/%s/PS3_GAME/ICON0.PNG", param, entry.entry_name.d_name);
									}

									memset(&slaunch, 0, sizeof(_slaunch));
									strncpy(slaunch.name, templn, 128);
									strncpy(slaunch.icon, icon, 160);
									snprintf(slaunch.path, 160, "/mount_ps3%s/%s", param, entry.entry_name.d_name);
									strncpy(slaunch.id, tempID, 9);

									slaunch.type=TYPE_PS3;

									if(strstr(param, "/PS2ISO")) slaunch.type=TYPE_PS2;
									else
									if(strstr(param, "/PSPISO") || strstr(param, "/ISO")) slaunch.type=TYPE_PSP;
									else
									if((strstr(param, "/PSX") || strstr(entry.entry_name.d_name, "ntfs[PSX"))) slaunch.type=TYPE_PS1;
									else
									if((strstr(param, "/BDISO") || strstr(param, "/DVDISO") || strstr(entry.entry_name.d_name, "ntfs[DVDISO]") || strstr(entry.entry_name.d_name, "ntfs[BDISO]"))) slaunch.type=TYPE_VID;

									cellFsWrite(fdsl, (void *)&slaunch, sizeof(_slaunch), NULL);
								}
							}
							if(conn_s && slaunch.type && !strstr(slaunch.path, ".ntfs["))
							{
								slaunch.type=0;
								sprintf(tempstr, "%s <br/>", slaunch.name);
								ssend(conn_s, tempstr);
							}
						}
						if(!is_net) cellFsClosedir(fd);
						if(is_net) free(data);
					}
				}
				if(is_net && ns>=0) {shutdown(ns, SHUT_RDWR); socketclose(ns); ns=-2;}
			}

			cellFsClose(fdsl);

			sys_storage_ext_get_disc_type(&real_disctype, NULL, NULL);
			fake_eject_event();
			if (real_disctype != 0)	fake_insert_event(real_disctype);

			//sort gamelist
			u32 games=(file_exists(SMAN_BIN))/sizeof(_slaunch);
			if(games>=MAX_GAMES) games=MAX_GAMES-1;

			if(games>1)
			{
				_slaunch *slaunch_f = (_slaunch*)malloc(games*sizeof(_slaunch));

				if(slaunch_f)
				{
					memset(slaunch_f, 0, games*sizeof(_slaunch));
					read_file((char*)SMAN_BIN, (void *)slaunch_f, sizeof(_slaunch)*games);

					_slaunch swap;
					if(games>1)
					for(u32 n=0; n<(games-1); n++)
					{
						for(u32 m=(n+1); m<games; m++)
						{
							if(strcasecmp(slaunch_f[n].name, slaunch_f[m].name)>0)
							{
								swap=slaunch_f[n];
								slaunch_f[n]=slaunch_f[m];
								slaunch_f[m]=swap;
							}
						}
					}

					cellFsOpen(SMAN_BIN, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fdsl, NULL, 0);
					cellFsWrite(fdsl, (void *)slaunch_f, sizeof(_slaunch)*games, NULL);
					cellFsClose(fdsl);
					free(slaunch_f);
				}
			}

			if(discboot==1)
				xsetting_0AF1F161()->SetSystemDiscBootFirstEnabled(1);

			if(arg!=0xC0FEBAB0) show_msg(STR_READY);
		}

load_last_game:

		if(sm_config->xmbi) create_xml();

#ifdef USE_NTFS
		if(mountCount==-2)
		{
			mountCount = ntfsMountAll(&mounts, NTFS_SU | NTFS_FORCE);
			if (mountCount < 1) mountCount=-2;
		}
#endif

		if(arg==0xC0FEBAB0)
		{
			show_msg(STR_SCANC);
			goto leave;
		}

		strcpy(tempstr, "/dev_hdd0/PS3ISO/AUTOBOOT.ISO");
		if(sm_config->autob && cellFsStat(tempstr, &buf)==CELL_FS_SUCCEEDED)
			delay_mount=2;

		else
		{
			if(cellFsOpen(LASTGAMETXT, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
			{
				u64 read_e = 0;
				if(cellFsRead(fd, (void *)tempstr, 511, &read_e)==CELL_FS_SUCCEEDED)
				{
					tempstr[read_e]=0;
					if(strstr(tempstr, "/PS3_GAME/USRDIR/EBOOT.BIN")) tempstr[strlen(tempstr)-26]=0;
					u8 retries1=0;
					if(strlen(tempstr)>10 || strstr(tempstr, "/net") || strstr(tempstr, ".ntfs["))
					{
again1:
						if(strstr(tempstr, "/dev_usb") && cellFsStat(tempstr, &buf)!=CELL_FS_SUCCEEDED)
						{
							sys_timer_sleep(250000);
							retries1++;
							if(retries1<5) goto again1;
						}
						if(strstr(tempstr, "/net") || cellFsStat(tempstr, &buf)==CELL_FS_SUCCEEDED)
							delay_mount=1;
					}
				}

				cellFsClose(fd);
			}
		}

		if(delay_mount)
		{
			if(delay_mount==1)	// last game
			{
				if(discboot==1)
					xsetting_0AF1F161()->SetSystemDiscBootFirstEnabled(0);

				mount_game(tempstr, 0);

				if(discboot==1)
				{
					sys_timer_sleep(15);
					xsetting_0AF1F161()->SetSystemDiscBootFirstEnabled(1);
				}
			}
			else	// AUTOBOOT.ISO
			{
				sys_timer_sleep(10);
				mount_game(tempstr, 0);
			}
		}

leave:
		init_running=0;
		free(mynet_iso);
		free(addr);
		return;
}

static void scan_thread(u64 arg)
{
	content_scan(arg);
	init_running=0;
	led(YELLOW, OFF);
	led(GREEN, ON);
	sys_ppu_thread_exit(0);
}

static void get_mime_type(char *param, char *header)
{
	if(strstr(param, ".JP") || strstr(param, ".jp") || strstr(param, ".STH"))
		strcat(header, "image/jpeg");
	else
	if(strstr(param, ".PNG") || strstr(param, ".png"))
		strcat(header, "image/png");
	else
	if(strstr(param, ".TXT") || strstr(param, ".txt") || strstr(param, ".LOG") || strstr(param, ".log") || strstr(param, ".INI") || strstr(param, ".ini") || strstr(param, ".HIP") || strstr(param, ".HIS"))
		strcat(header, "text/plain");
	else
	if(strstr(param, ".css"))
		strcat(header, "text/css");
	else
	if(strstr(param, ".htm"))
		strcat(header, "text/html");
	else
	if(strstr(param, ".svg"))
		strcat(header, "image/svg+xml");
	else
	if(strstr(param, ".GIF") || strstr(param, ".gif"))
		strcat(header, "image/gif");
	else
	if(strstr(param, ".AVI") || strstr(param, ".avi"))
		strcat(header, "video/x-msvideo");
	else
	if(strstr(param, ".MKV") || strstr(param, ".mkv"))
		strcat(header, "video/x-matroska");
	else
	if(strstr(param, ".MP4") || strstr(param, ".mp4"))
		strcat(header, "video/mp4");
	else
	if(strstr(param, ".MPG") || strstr(param, ".mpg") || strstr(param, ".MPE") || strstr(param, ".mpe") || strstr(param, ".MP2") || strstr(param, ".mp2"))
		strcat(header, "video/mpeg");
	else
	if(strstr(param, ".VOB") || strstr(param, ".vob"))
		strcat(header, "video/vob");
	else
	if(strstr(param, ".WMV") || strstr(param, ".wmv"))
		strcat(header, "video/x-ms-wmv");
	else
	if(strstr(param, ".MOV") || strstr(param, ".mov"))
		strcat(header, "video/quicktime");
	else
	if(strstr(param, ".MP3") || strstr(param, ".mp3"))
		strcat(header, "audio/mpeg");
	else
	if(strstr(param, ".WAV") || strstr(param, ".wav"))
		strcat(header, "audio/x-wav");
	else
	if(strstr(param, ".BMP") || strstr(param, ".bmp"))
		strcat(header, "image/bmp");
	else
	if(strstr(param, ".TIF") || strstr(param, ".tif"))
		strcat(header, "image/tiff");
	else
	if(strstr(param, ".ZIP") || strstr(param, ".zip"))
		strcat(header, "application/zip");
	else
	if(strstr(param, ".PDF") || strstr(param, ".pdf"))
		strcat(header, "application/pdf");
	else
	if(strstr(param, ".SWF") || strstr(param, ".swf"))
		strcat(header, "application/x-shockwave-flash");
	else
		strcat(header, "application/octet-stream");
}

static void http_send_file(char* file, u64 size, int conn_s, u8 is_ntfs, u8* buffer, u32 buf_size)
{
	int fd=0;

#ifdef USE_NTFS
	if(is_ntfs)
	{
		fd = ps3ntfs_open(file+5, O_RDONLY, 0);
		if(fd>0)
		{
			ps3ntfs_seek64(fd, 0, SEEK_SET);
			while(working)
			{
				int read_n = ps3ntfs_read(fd, (void *)buffer, buf_size);
				if(read_n<=0) break;
				if(send(conn_s, buffer, (size_t)read_n, 0)<0) break;
				sys_timer_usleep(2048);
			}
			ps3ntfs_close(fd);
		}
	}
	else
#endif
	if(cellFsOpen(file, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		u64 read_e = 0, pos;
		cellFsLseek(fd, 0, CELL_FS_SEEK_SET, &pos);
		while(1)
		{
			if(cellFsRead(fd, (void *)buffer, buf_size, &read_e)!=CELL_FS_SUCCEEDED) break;
			if(!read_e || send(conn_s, buffer, (size_t)read_e, 0)<0) break;
			sys_timer_usleep(2048);
		}
		cellFsClose(fd);
	}
}

static void send_table_head(int conn_s, const char* param)
{
	ssend(conn_s, "<table class=\"propfont\"><thead><tr bgcolor=\"#404040\"><th align=left>");
	ssend(conn_s, param);
	ssend(conn_s, ":</th><th>Size</th><th>Date</th></tr></thead><tbody>");
}

static void send_http_ok(const int conn_s)
{
	ssend(conn_s, "HTTP/1.0 200 OK\r\n\r\nOK!");
}

static void find_name_icon(const char* path, char* name, char* icon)
{
	_slaunch swap;
	u64 msiz=0;
	int fd=0;

	if(cellFsOpen((char*)SMAN_BIN, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		while(working)
		{
			if(cellFsRead(fd, &swap, sizeof(_slaunch), &msiz) == CELL_FS_SUCCEEDED)
			{
				if(strstr(swap.path, path))
				{
					if(name) strcpy(name, swap.name);
					if(icon) strcpy(icon, swap.icon);
					break;
				}
				if(msiz<sizeof(_slaunch)) break;
			}
			else break;
		}
		cellFsClose(fd);
	}
}

#define MAX_HTTP_THREADS	(6)
#define BUFFER_SIZE_HTTP	(65536)/(MAX_HTTP_THREADS)
#define BUFFER_SIZE_HTTP2	(65536)

static void handleclient_www(u64 conn_s_p)
{
	u8 is_binary = 0;
	u64 c_len = 0;
	int conn_s = 0;

	char* addr		=(char*)memalign(128, BUFFER_SIZE_HTTP);	if(!addr) goto leave1;

	char* templn	=addr;			// 1024
	char* tempstr	=addr+1024;		// 4096
	char* param		=addr+5120;		//  512
	char* header	=addr+5632;		//  512
	char* buffer1	=addr+6144;		//  512

	char cmd[16];

	struct CellFsStat buf;
#ifdef USE_NTFS
	struct stat bufn;
	struct statvfs vbuf;
#endif
	int fd;
	CellRtcDateTime rDate;
	bool keep_alive;

	struct timeval tv;
	tv.tv_usec = 0;

	u8 max_cc;	// max client connections per persistent connection

	while(working)
	{
	sys_timer_usleep(8192);
	conn_s = accept((int)(conn_s_p&0xffff), NULL, NULL);
	if(conn_s<0)
	{
		wwwd_socket[0]=-1;
		printf(SMAN_LOG "accept() error %08x | www_thread_id:%i\r\n", conn_s, conn_s_p>>16);
		break;
	}
	wwwd_socket[http_threads]=conn_s;

	tv.tv_sec = 3;
	setsockopt(conn_s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	tv.tv_sec = 8;
	setsockopt(conn_s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	max_cc=0;

keep_alive_l:
	keep_alive=0;

	memset(addr, 0, BUFFER_SIZE_HTTP);

	if(recv(conn_s, buffer1, 512, 0) > 0 && buffer1[0]=='G' && buffer1[4]=='/') // serve only GET /xxx requests
	{
		if(strstr(buffer1, "Connection: keep-alive"))
		{
			keep_alive=1;
			max_cc++;
			printf(SMAN_LOG "keep_alive s:%03i c:%02i th:%i\r\n", conn_s, max_cc, conn_s_p>>16);
		}

		buffer1[strcspn(buffer1, "\n")] = '\0';
		buffer1[strcspn(buffer1, "\r")] = '\0';

		ssplit(buffer1, cmd, 15, header, 511);
		ssplit(header, param, 511, cmd, 15);

		if(strstr(param, "%"))
		{
			strcpy(buffer1, param);
			int pos=0;
			for(u32 i=0;i<strlen(buffer1);i++)
			{
				if(buffer1[i]!='%')
					param[pos]=buffer1[i];
				else
				{
					if(buffer1[i+2]>='0' && buffer1[i+2]<='9') param[pos]=buffer1[i+2]-0x30;
					else
						if(buffer1[i+2]>='A' && buffer1[i+2]<='F') param[pos]=buffer1[i+2]-55;

					if(buffer1[i+1]>='0' && buffer1[i+1]<='9') param[pos]+=(buffer1[i+1]-0x30)*16;
					else
						if(buffer1[i+1]>='A' && buffer1[i+1]<='F') param[pos]+=(buffer1[i+1]-55)*16;

					i+=2;
				}
				pos++;
				param[pos]=0;
			}
		}

		if(strstr(param, "?random="))
			param[strchr(param, '?')-param]=0;

		if(strstr(param, "/favicon.ico")) strcpy(param, WMRES "/PS3.png");

		printf(SMAN_LOG "getRequest s:%03i c:%02i th:%i [%s]\r\n", conn_s, max_cc, conn_s_p>>16, param);

		if(strstr(param, "popup.ps3"))
		{
			if(strlen(param)>10)
			{
				send_http_ok(conn_s);
				show_msg(param+11);
			}
			goto leave2;
		}

		if(strstr(param, "quit.ps3"))
		{
			show_msg(STR_WMUNL);
			sclose(&conn_s);
			free(addr);
			fan_reset=1;
			restore_fan(2);
			working=0;
			goto leave1;
		}

		if(strstr(param, "shutdown.ps3"))
		{
			send_http_ok(conn_s);
			sclose(&conn_s);
			// {system_call_4(379,0x1100,0,0,0);}
			working=0;
			vshmain_87BB0001(1);
			goto leave1;
		}
		if(strstr(param, "restart.ps3"))
		{
			send_http_ok(conn_s);
			sclose(&conn_s);
			// {system_call_3(379, 0x8201, NULL, 0);}
			// {system_call_4(379,0x1200,0,0,0);}
			working=0;
			vshmain_87BB0001(2);
			goto leave1;
		}
		if(strstr(param, "refresh_ps3"))
		{
			while(working && init_running) sys_timer_usleep(300000);
			init_running=1;
			sys_ppu_thread_t id3;
			sys_ppu_thread_create(&id3, scan_thread, (u64)0xC0FEBAB0, 1990, 0xC000, 0, THREAD_REFR);
			goto leave2;
		}

		u8 is_ntfs = (strstr(param, "/dev_nt")!=NULL);

		if(strstr(param, "index.ps3")
			|| strstr(param, "mount.ps3/")
			|| strstr(param, "mount_ps3/")
			|| strstr(param, "refresh.ps3")
			|| strstr(param, "setup.ps3")
			|| strstr(param, "cpursx.ps3")
			)
			is_binary=0;
		else
		{
			is_binary=1;
#ifdef USE_NTFS
			if(is_ntfs)
			{
				param[10]=':';
				if(param[11]!='/'){param[11]='/', param[12]=0;}

				if(ps3ntfs_stat(param + 5, &bufn)<0) goto error_response;

				c_len = bufn.st_size;
				if(bufn.st_mode & S_IFDIR) is_binary=2;
			}
			else
#endif
			if(cellFsStat(param, &buf)==CELL_FS_SUCCEEDED)
			{
				c_len=buf.st_size;
				if((buf.st_mode & S_IFDIR) != 0) is_binary=2;
			}
			else
			{
				if(param[1]=='n')//net0/net1
				{
					is_binary=2;
				}
				else
				{
error_response:
					c_len=0;
					is_binary=0;
					ssend(conn_s, "HTTP/1.0 404 Not Found\r\n\r\n");
					goto leave2;
				}
			}
		}

		header[0]=0;
		strcpy(header, "HTTP/1.0 200 OK\r\nContent-Type: ");
		if(is_binary==1)
			get_mime_type(param, header);

		else
			strcat(header, "text/html");

		strcat(header, "\r\n");

		if(strlen(param)>1 && param[strlen(param)-1]=='/') param[strlen(param)-1]=0;

		if(is_binary==1) //file - send it
		{
			if(keep_alive)
			{
				strcat(header, "Keep-Alive: timeout=3,max=250\r\n");
				strcat(header, "Connection: keep-alive");
				strcat(header, "\r\n");
			}
			sprintf(templn, "Content-Length: %llu\r\n\r\n", (unsigned long long)c_len); strcat(header, templn);
			ssend(conn_s, header);

			if(c_len)
			{
				if(c_len>1024*1024)
				{
					u8*addr2=memalign(128, BUFFER_SIZE_HTTP2);
					if(addr2)
					{
						http_send_file(param, c_len, conn_s, is_ntfs, (u8*)addr2, BUFFER_SIZE_HTTP2);
						free(addr2);
						goto leave_file;
					}
				}

				http_send_file(param, c_len, conn_s, is_ntfs, (u8*)addr, BUFFER_SIZE_HTTP);
			}
leave_file:

			if(keep_alive && max_cc<HTTP_MAX_CC-1)
			{
				printf(SMAN_LOG "reuse_socket:%03i c:%02i th:%i\r\n", conn_s, max_cc, conn_s_p>>16);
				goto keep_alive_l;
			}
			goto leave2;
		}

		strcat(header, "\r\n\r\n");
		if((param[1]!='n'))
		{
			if(!strstr(param, "mount_ps3"))
			{
				ssend(conn_s, header);
				send_file(WMRES "/www_head.htm", conn_s);
			}
		}

		if(strstr(param, "cpursx.ps3"))
		{
			if(!sm_config->fanc)
			{
				u8 st, mode, unknown;
				if(!dex_mode)
				{
					if(c_firmware>=4.55f)
					{
						backup[5]=peekq(0x8000000000009E38ULL);
						lv2poke32(0x8000000000009E38ULL, 0x38600001); // sys 409 get_fan_policy
						sys_sm_get_fan_policy(0, &st, &mode, &fan_speed, &unknown);
						pokeq(0x8000000000009E38ULL, backup[5]);
					}
					else
					{
						backup[5]=peekq(0x8000000000009E28ULL);
						lv2poke32(0x8000000000009E28ULL, 0x38600001); // sys 409 get_fan_policy
						sys_sm_get_fan_policy(0, &st, &mode, &fan_speed, &unknown);
						pokeq(0x8000000000009E28ULL, backup[5]);
					}
				}
				else // DEX
				{
					if(c_firmware>=4.55f)
					{
							backup[5]=peekq(0x8000000000009EB8ULL);
							lv2poke32(0x8000000000009EB8ULL, 0x38600001); // sys 409 get_fan_policy
							sys_sm_get_fan_policy(0, &st, &mode, &fan_speed, &unknown);
							pokeq(0x8000000000009EB8ULL, backup[5]);
					}
					else if(c_firmware>=4.21f && c_firmware<=4.53f)
					{
							backup[5]=peekq(0x8000000000009EA8ULL);
							lv2poke32(0x8000000000009EA8ULL, 0x38600001);
							sys_sm_get_fan_policy(0, &st, &mode, &fan_speed, &unknown);
							pokeq(0x8000000000009EA8ULL, backup[5]);
					}
				}
			}

			u32 t1=0, t2=0, t1f=0, t2f=0;
			get_temperature(0, &t1); // 3E030000 -> 3E.03'C -> 62.(03/256)'C
			get_temperature(1, &t2);
			t1>>=24;
			t2>>=24;
			t1f=(1.8f*(float)t1+32.f);
			t2f=(1.8f*(float)t2+32.f);

			typedef struct {
				u32 total;
				u32 avail;
			} _meminfo;
			_meminfo meminfo;

			{system_call_1(352, (u64)(u32)&meminfo);}
			sprintf(templn, "<hr><font size=42px><b>CPU: %i°C (MAX: %i°C)<br>RSX: %i°C<hr>CPU: %i°F (MAX: %i°F)<br>RSX: %i°F<hr>MEM: %iKB<hr>FAN SPEED: 0x%X (%i%%)</b></font><hr><script language=\"javascript\">setTimeout(function(){ window.location.href='/cpursx.ps3'; }, 10000);</script>", t1, max_temp, t2, t1f, (int)(1.8f*(float)max_temp+32.f), t2f, (meminfo.avail>>10), fan_speed, (int)((int)fan_speed*100)/255);
			ssend(conn_s, templn);
			goto leave2_f;
		}

		if(is_binary==2) // folder listing
		{
				u8 is_net = (param[1]=='n');

#ifdef USE_NTFS
				DIR_ITER *pdir = NULL;

				if(is_ntfs)
				{
					param[10]=':';
					if(param[11]!='/'){param[11]='/', param[12]=0;}
					pdir = ps3ntfs_diropen(param+5); // /dev_ntfs1v -> ntfs1:
					if(!pdir) is_ntfs = 0;
				}
#endif
				if(is_ntfs || is_net || cellFsOpendir( param, &fd) == CELL_FS_SUCCEEDED)
				{
					CellFsDirent entry;
					u64 read_e;
					unsigned long long sz=0;
					u16 idx=0;
					char sf[8];
					char fsize[256];

					if(is_net)
					{
						int ns=-1;
						int abort_connection=0;
						if(param[4]=='0')
							ns=connect_to_server(sm_config->neth0, sm_config->netp0);
						if(param[4]=='1')
							ns=connect_to_server(sm_config->neth1, sm_config->netp1);
						if(ns>=0)
						{
							strcat(param, "/");
							if(open_remote_dir(ns, param+5, &abort_connection)>=0)
							{
								ssend(conn_s, header);
								send_file(WMRES "/www_head.htm", conn_s);
								send_table_head(conn_s, param);
								strcpy(templn, param); if(templn[strlen(templn)-1]=='/') templn[strlen(templn)-1]=0;
								if(strrchr(templn, '/')) templn[strrchr(templn, '/')-templn]=0; if(strlen(templn)<6 && strlen(param)<8) {templn[0]='/'; templn[1]=0;}
								sprintf(tempstr, "<tr><td><a class=\"f\" href=\"%s\">..</a></td><td align=right>&nbsp; &lt;dir&gt; &nbsp;</td><td>11-Nov-2006 11:11</td></tr>", templn);
								ssend(conn_s, tempstr);

								u32 data2=NULL;
								netiso_read_dir_result_data *data=NULL;
								int v3_entries=0;
								v3_entries = read_remote_dir(ns, &data2, &abort_connection);
								if(data2==NULL) goto leave2;
								data=(netiso_read_dir_result_data*)data2;

								for(int n=0;n<v3_entries;n++)
								{
									if(data[n].name[0]=='.' && data[n].name[1]==0) continue;
									if(strlen(param)<2)
										sprintf(templn, "/%s", data[n].name);
									else
									{
										sprintf(templn, "%s%s", param, data[n].name);
										if(templn[strlen(templn)-1]=='/') templn[strlen(templn)-1]=0;
									}

									cellRtcSetTime_t(&rDate, data[n].mtime);
									sz=(unsigned long long)data[n].file_size;
									if(sz<10240) sprintf(sf, "b");
									else if(sz>=10240 && sz<2097152) {sprintf(sf, "KB"); sz>>=10;}
									else if(sz>=2097152 && sz<2147483648U) {sprintf(sf, "MB"); sz>>=20;}
									else if(sz>=2147483648U) {sprintf(sf, "GB"); sz>>=30;}
									if(data[n].is_directory)
										sprintf(fsize, "<a href=\"/mount.ps3%s\">&lt;dir&gt;</a>", templn);
									else
										sprintf(fsize, "%llu %s", sz, sf);

									sprintf(tempstr, "<tr><td><a%shref=\"%s\">%s</a></td><td align=right>&nbsp; %s &nbsp;</td><td>%02i-%s-%04i %02i:%02i</td></tr>", //<td> %s%s%s%s%s%s%s%s%s</td>
									((data[n].is_directory) != 0) ? " class=\"f\" " : " ",
									templn, data[n].name,
									fsize,
									rDate.day, smonth[rDate.month-1], rDate.year,
									rDate.hour, rDate.minute);
									ssend(conn_s, tempstr);

									if(!working) break;
								}
								free(data);
							}
							else //may be a file
							{
								if(param[strlen(param)-1]=='/') param[strlen(param)-1]=0;
								int is_directory=0, bytes_read=0;
								int64_t file_size;
								u64 mtime, ctime, atime;
								if(remote_stat(ns, param+5, &is_directory, &file_size, &mtime, &ctime, &atime, &abort_connection)==0)
								{
									if(file_size && !is_directory)
									{
										if(open_remote_file_2(ns, param+5, &abort_connection)>0)
										{
											tempstr[0]=0;
											get_mime_type(param, tempstr);

											header[0]=0;
											sprintf(header, "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %llu\r\n\r\n", tempstr, (unsigned long long)file_size);
											ssend(conn_s, header);
											int boff=0;
											while(boff<file_size)
											{
												bytes_read = read_remote_file(ns, (char*)tempstr, boff, 4096, &abort_connection);
												if(bytes_read)
												{
													if(send(conn_s, tempstr, bytes_read, 0)<0) break;
												}
												boff+=bytes_read;
												if(bytes_read<4096 || boff>=file_size) break;
											}
											open_remote_file_2(ns, (char*)"/CLOSEFILE", &abort_connection);
											shutdown(ns, SHUT_RDWR); socketclose(ns);
											goto leave2;
										}
									}
								}
								else
								{
									shutdown(ns, SHUT_RDWR); socketclose(ns);
									ssend(conn_s, header);
									send_file(WMRES "/www_head.htm", conn_s);
									ssend(conn_s, "Network server not available!");
									goto leave2_f;
								}
							}
							shutdown(ns, SHUT_RDWR); socketclose(ns);
						}
					}
					else
					{
						send_table_head(conn_s, param);

						while(working)
						{
#ifdef USE_NTFS
							if(is_ntfs)
							{
								if(ps3ntfs_dirnext(pdir, entry.d_name, &bufn)) break;
								if(entry.d_name[0]=='$') continue;
								buf.st_mode = bufn.st_mode;
								buf.st_size = bufn.st_size;
								cellRtcSetTime_t(&rDate, bufn.st_mtime);
								if(!idx && strlen(param)<13)
								{
									ssend(conn_s, "<tr><td><a class=\"f\" href=\"/\">..</a></td><td align=right>&nbsp; &lt;dir&gt; &nbsp;</td><td>11-Nov-2006 11:11</td></tr>");
									idx++;
								}
							}
							else
#endif
							if(!(!cellFsReaddir(fd, &entry, &read_e) && read_e > 0)) break;

							if(entry.d_name[0]=='.' && entry.d_name[1]==0) continue;
#ifdef USE_NTFS
							// use host_root to expand all /dev_ntfs entries in root
							bool is_root = (!strcmp(entry.d_name, "host_root") && mountCount>0 && mounts);
							if(is_root || strcmp(entry.d_name, "host_root"))
							{
								u8 ntmp=1;
								if(is_root) ntmp=mountCount;
								for (u8 u = 0; u < ntmp; u++)
								{
									if(is_root) sprintf(entry.d_name, "dev_%s:", mounts[u].name);
#endif
									if(strlen(param)<2)
										sprintf(templn, "/%s", entry.d_name);
									else
									{
										if(is_ntfs && strlen(param)<13)
										sprintf(templn, "%s%s", param, entry.d_name);
										else
										sprintf(templn, "%s/%s", param, entry.d_name);
										if(templn[strlen(templn)-1]=='/') templn[strlen(templn)-1]=0;
									}

									if(!is_ntfs)
									{
										cellFsStat(templn, &buf);
										cellRtcSetTime_t(&rDate, buf.st_mtime);
									}

									sz=(unsigned long long)buf.st_size;
									if(sz<10240) sprintf(sf, "b");
									else if(sz>=10240 && sz<2097152) {sprintf(sf, "KB"); sz>>=10;}
									else if(sz>=2097152 && sz<2147483648U) {sprintf(sf, "MB"); sz>>=20;}
									else if(sz>=2147483648U) {sprintf(sf, "GB"); sz>>=30;}
									if((buf.st_mode & S_IFDIR) != 0)
										sprintf(fsize, "<a href=\"/mount.ps3%s\">&lt;dir&gt;</a>", templn);
									else
										sprintf(fsize, "%llu %s", sz, sf);

									sprintf(tempstr, "<tr><td><a%shref=\"%s\">%s</a></td><td align=right>&nbsp; %s &nbsp;</td><td>%02i-%s-%04i %02i:%02i</td></tr>", //<td> %s%s%s%s%s%s%s%s%s</td>
									((buf.st_mode & S_IFDIR) != 0) ? " class=\"f\" " : " ",
									templn, entry.d_name,
									fsize,
									rDate.day, smonth[rDate.month-1], rDate.year,
									rDate.hour, rDate.minute);
									ssend(conn_s, tempstr);
									idx++;

									if(!working) break;
#ifdef USE_NTFS
								}
							}
#endif
						}
#ifdef USE_NTFS
						if(is_ntfs && pdir) ps3ntfs_dirclose(pdir);
#endif
						if(!is_ntfs) cellFsClosedir(fd);
					}

					if(strlen(param)<4)
					{
						if(sm_config->netd0 && sm_config->neth0[0] && sm_config->netp0)
						{
							sprintf(tempstr, "<tr><td><a class=\"f\" href=\"/net0\">net0 (%s:%i)</a></td><td align=right>&nbsp; &lt;dir&gt; &nbsp;</td><td>11-Nov-2006 11:11</td></tr>", sm_config->neth0, sm_config->netp0);
							ssend(conn_s, tempstr);
							idx++;
						}
						if(sm_config->netd1 && sm_config->neth1[0] && sm_config->netp1)
						{
							sprintf(tempstr, "<tr><td><a class=\"f\" href=\"/net1\">net1 (%s:%i)</a></td><td align=right>&nbsp; &lt;dir&gt; &nbsp;</td><td>11-Nov-2006 11:11</td></tr>", sm_config->neth1, sm_config->netp1);
							ssend(conn_s, tempstr);
							idx++;
						}
					}

					ssend(conn_s, "</tbody></table>");

					if(strlen(param)>6)
					{
						strcpy(templn, param);
						if(strchr(param+1, '/'))
							param[strchr(param+1, '/')-param]=0;

#ifdef USE_NTFS
						if(is_ntfs)
						{
							if(strlen(templn)<13)
							{
								param[11]='/'; param[12]=0;
								ps3ntfs_statvfs(param+5, &vbuf);
								sprintf(templn, "<hr><b><a href=\"%s\">%s</a>: %lu MB free</b><br>", param, param, (long unsigned int)((vbuf.f_bfree * (vbuf.f_bsize>>10))>>10));
							}
							else strcpy(templn, "<hr>");
						}
						else
#endif
						{
							u32 blockSize;
							u64 freeSize;
							cellFsGetFreeSize(param, &blockSize, &freeSize);
							sprintf(templn, "<hr><b><a href=\"%s\">%s</a>: %i MB free</b><br>", param, param, (int)((blockSize*freeSize)>>20));
						}
						ssend(conn_s, templn);
					}
					else
						ssend(conn_s, "<hr>PS3 sMAN | <a href=\"http://deanbg.com/donate\">Click here if you like sMAN, webMAN, multiMAN or multiAVCHD.</a><br>");
				}
				send_file(WMRES "/www_file.htm", conn_s);
				goto leave2_f;
		}
		else
		{
			if(strstr(param, "refresh.ps3"))
			{
				while(working && init_running) sys_timer_usleep(300000);
				init_running=1;
				sys_ppu_thread_t id3;
				sys_ppu_thread_create(&id3, scan_thread, (u64)(((u64)conn_s<<32) | 0xC0FEBAB0), 1000, 0xC000, 0, THREAD_REFR);
				while(working && init_running) sys_timer_sleep(1);

				ssend(conn_s, "<script language=\"javascript\">");
				set_html(3, conn_s, "content", "", 0);
				ssend(conn_s, "</script>");
				goto index_ps3;
			}
			else
			if(strstr(param, "setup.ps3?"))
			{
				u8 tmp_resv=sm_config->resv;
				memset(sm_config, 0, sizeof(_smconfig));
				sm_config->resv=tmp_resv;
				sm_config->type=type;
				sm_config->cur_game=cur_game;

				if(strstr(param, "xmbi")) sm_config->xmbi=1;
				if(strstr(param, "refr")) sm_config->refr=1;
				if(strstr(param, "auto")) sm_config->autob=1;
				if(strstr(param, "ftpd")) sm_config->ftpd=1;
				if(strstr(param, "flsh")) sm_config->flsh=1;

				if(strstr(param, "sfo")) sm_config->sfo=1;
				if(strstr(param, "focus")) sm_config->focus=1;

				if(strstr(param, "fanc")) sm_config->fanc=1;

				if(strstr(param, "netd0"))  sm_config->netd0=1;
				if(strstr(param, "netd1"))  sm_config->netd1=1;

				sm_config->combo=0;
				if(!strstr(param, "failsaf")) sm_config->combo|=FAIL_SAFE;
				if(!strstr(param, "showtem")) sm_config->combo|=SHOW_TEMP;
				if(!strstr(param, "prevgam")) sm_config->combo|=PREV_GAME;
				if(!strstr(param, "nextgam")) sm_config->combo|=NEXT_GAME;
				if(!strstr(param, "shutdow")) sm_config->combo|=SHUT_DOWN;
				if(!strstr(param, "restart")) sm_config->combo|=RESTARTPS;
				if(!strstr(param, "unloadw")) sm_config->combo|=UNLOAD_WM;
				if(!strstr(param, "manualf")) sm_config->combo|=MANUALFAN;


				sm_config->temp1=MY_TEMP;
				sm_config->minfan=22;

				if(strstr(param, "mfan="))
				{
					char *pos=strstr(param, "mfan=") + 5;
					char mytemp[3];
					for(u8 n=0;n<2;n++)
					{
						if(pos[n]!='&') {mytemp[n]=pos[n]; mytemp[n+1]=0;}
						else break;
					}
					sm_config->minfan=my_atoi(mytemp);
					if(sm_config->minfan<20) sm_config->minfan=20;
					if(sm_config->minfan>98) sm_config->minfan=99;
				}


				sm_config->manu=37;
				sm_config->temp0=0;

				if(strstr(param, "step="))
				{
					char *pos=strstr(param, "step=") + 5;
					char mytemp[3];
					for(u8 n=0;n<2;n++)
					{
						if(pos[n]!='&') {mytemp[n]=pos[n]; mytemp[n+1]=0;}
						else break;
					}
					sm_config->temp1=my_atoi(mytemp);
					if(sm_config->temp1<40) sm_config->temp1=40;
					if(sm_config->temp1>83) sm_config->temp1=83;
				}

				if(strstr(param, "fsp0="))
				{
					char *pos=strstr(param, "fsp0=") + 5;
					char mytemp[3];
					for(u8 n=0;n<2;n++)
					{
						if(pos[n]!='&') {mytemp[n]=pos[n]; mytemp[n+1]=0;}
						else break;
					}
					sm_config->ps2temp=my_atoi(mytemp);
					if(sm_config->ps2temp<20) sm_config->ps2temp=20;
					if(sm_config->ps2temp>99) sm_config->ps2temp=99;
				}

				if(strstr(param, "manu="))
				{
					char *pos=strstr(param, "manu=") + 5;
					char mytemp[3];
					for(u8 n=0;n<2;n++)
					{
						if(pos[n]!='&') {mytemp[n]=pos[n]; mytemp[n+1]=0;}
						else break;
					}
					sm_config->manu=my_atoi(mytemp);
				}
				if(sm_config->manu<20) sm_config->manu=20;

				sm_config->temp0=0;
				if(strstr(param, "temp=1"))
					sm_config->temp0= (u8)(((float)sm_config->manu * 255.f)/100.f);

				max_temp=0;
				if(sm_config->fanc)
				{
					if(sm_config->temp0==0) max_temp=sm_config->temp1;
					fan_control(sm_config->temp0, 0);
				}
				else
					restore_fan(0);

				sm_config->warn=0;
				if(strstr(param, "warn=1")) sm_config->warn=1;

				sm_config->neth0[0]=0;
				sm_config->neth1[0]=0;

				char *pos=strstr(param, "neth0=") + 6;
				char netp[7];
				if(strstr(param, "neth0="))
				{
					for(u8 n=0;n<16;n++)
					{
						if(pos[n]!='&') {sm_config->neth0[n]=pos[n];sm_config->neth0[n+1]=0;}
						else break;
					}

					pos=strstr(param, "netp0=") + 6;
					if(pos!=NULL)
					{
						for(u8 n=0;n<6;n++)
						{
							if(pos[n]!='&') {netp[n]=pos[n]; netp[n+1]=0;}
							else break;
						}
						sm_config->netp0=my_atoi(netp);
					}
				}
				if(strstr(param, "netp1="))
				{
					pos=strstr(param, "neth1=") + 6;
					for(u8 n=0;n<16;n++)
					{
						if(pos[n]!='&') {sm_config->neth1[n]=pos[n];sm_config->neth1[n+1]=0;}
						else break;
					}

					pos=strstr(param, "netp1=") + 6;
					if(pos!=NULL)
					{
						for(u8 n=0;n<6;n++)
						{
							if(pos[n]!='&') {netp[n]=pos[n]; netp[n+1]=0;}
							else break;
						}
						sm_config->netp1=my_atoi(netp);
					}
				}

				save_settings();
				ssend(conn_s, "<br>" STR_SETTINGSUPD);

			}
			else
			if(strstr(param, "setup.ps3"))
			{
				send_file(WMRES "/www_setup.htm", conn_s);

				ssend(conn_s, "<script language=\"javascript\">");

				set_html(0, conn_s, "xmbi", NULL, sm_config->xmbi);
				set_html(0, conn_s, "auto", NULL, sm_config->autob);
				set_html(0, conn_s, "refr", NULL, sm_config->refr);
				set_html(0, conn_s, "ftpd", NULL, sm_config->ftpd);
				set_html(0, conn_s, "flsh", NULL, sm_config->flsh);
				set_html(0, conn_s, "sfo" , NULL, sm_config->sfo);
				set_html(0, conn_s, "focus",NULL, sm_config->focus);

				set_html(0, conn_s, "fanc", NULL, sm_config->fanc);

				set_html(0, conn_s, "t_0" , NULL, (sm_config->temp0==0));
				set_html(2, conn_s, "step", NULL, sm_config->temp1);
				set_html(0, conn_s, "t_2" , NULL, (sm_config->fanc && sm_config->temp0==0));

				set_html(0, conn_s, "t_1" , NULL, (sm_config->temp0!=0));
				set_html(2, conn_s, "manu", NULL, sm_config->manu);

				set_html(0, conn_s, "warn", NULL, sm_config->warn);
				set_html(2, conn_s, "mfan", NULL, sm_config->minfan);
				set_html(2, conn_s, "fsp0", NULL, sm_config->ps2temp);

				set_html(0, conn_s, "nd1" , NULL, sm_config->netd0);
				set_html(0, conn_s, "nd2" , NULL, sm_config->netd1);

				set_html(1, conn_s, "neth0", sm_config->neth0, 0);
				set_html(1, conn_s, "neth1", sm_config->neth1, 0);

				set_html(2, conn_s, "netp0", NULL, sm_config->netp0);
				set_html(2, conn_s, "netp1", NULL, sm_config->netp1);

				set_html(0, conn_s, "c1", NULL, (!(sm_config->combo & FAIL_SAFE)));
				set_html(0, conn_s, "c2", NULL, (!(sm_config->combo & SHOW_TEMP)));
				set_html(0, conn_s, "c3", NULL, (!(sm_config->combo & PREV_GAME)));
				set_html(0, conn_s, "c4", NULL, (!(sm_config->combo & NEXT_GAME)));
				set_html(0, conn_s, "c5", NULL, (!(sm_config->combo & SHUT_DOWN)));
				set_html(0, conn_s, "c6", NULL, (!(sm_config->combo & RESTARTPS)));
				set_html(0, conn_s, "c7", NULL, (!(sm_config->combo & UNLOAD_WM)));
				set_html(0, conn_s, "c8", NULL, (!(sm_config->combo & MANUALFAN)));

				ssend(conn_s, "</script>");

				ssend(conn_s, "<hr color=\"#FF0000\"/><a href=\"http://www.deanbg.com/sman.sprx\">sMAN - Latest version</a><br>");

			}
			else
			if(strstr(param, "mount.ps3") || strstr(param, "mount_ps3"))
			{
				//mount game
				if(strstr(param, "ps3/unmount"))
				{
					cellFsUnlink(LASTGAMETXT);

					do_umount_iso();
					sys_timer_usleep(20000);

					if(strstr(param, "mount_ps3")) goto leave2;

					ssend(conn_s, STR_GAMEUM);
				}
				else
				{
					if(strstr(param, "mount.ps3"))
					{
						param[6]='_';
						strcpy(tempstr, param+10);
						strcpy(buffer1, WMRES "/DVD.png");
						find_name_icon(param, tempstr, buffer1);

						if(strstr(param, "/PSPISO") || strstr(param, "/ISO"))
							sprintf(templn, STR_GAMETOM ": %s<hr/><img src=\"%s\"><hr/>" STR_PSPLOADED, tempstr, buffer1);
						else if(strstr(param, "/BDISO") || strstr(param, "/DVDISO") || strstr(param, ".ntfs[BDISO]") || strstr(param, ".ntfs[DVDISO]"))
							sprintf(templn, STR_MOVIETOM ": %s<hr/><img src=\"%s\"><hr/>" STR_MOVIELOADED, tempstr, buffer1);
						else
							sprintf(templn, STR_GAMETOM ": %s<hr/><img src=\"%s\"><hr/>", tempstr, buffer1);

						ssend(conn_s, templn);
						mount_game(param+10, 3);
						goto leave2_f;
					}

					mount_game(param+10, 3);
					goto leave2;
				}
			}
			else
			{
index_ps3:

				buf.st_size=0;

				cellFsStat(SMAN_BIN, &buf);
				if((buf.st_size)%sizeof(_slaunch)) {cellFsUnlink(SMAN_BIN); buf.st_size=0;}

				u32 slaunch_games=(buf.st_size)/sizeof(_slaunch);

				if(slaunch_games)
				{
					int fs;
					if(cellFsOpen(SMAN_BIN, CELL_FS_O_RDONLY, &fs, 0, 0)==CELL_FS_SUCCEEDED)
					{
						_slaunch *slaunch = (_slaunch*) malloc(sizeof(_slaunch));
						if(slaunch)
						{
							CellRtcTick pTick;
							cellRtcGetCurrentTick(&pTick);
							for(u16 idx=0;idx<slaunch_games;idx++)
							{
								cellFsRead(fs, (void *)slaunch, sizeof(_slaunch), NULL);
								slaunch->path[6]='.';
								sprintf(tempstr, "<div class=\"gc\"><div class=\"ic\"><a href=\"%s?random=%i\"><img src=\"%s\" class=\"gi\"></a></div><div class=\"gn\">%s</div></div>",
										slaunch->path, (int)((pTick.tick&0xeffff)+idx), slaunch->icon, slaunch->name);

								ssend(conn_s, tempstr);
							}
							free(slaunch);
						}
						cellFsClose(fs);
					}
				}
			}
		}
	}
	else
	{
		if(buffer1[0])
		{
			if(strstr(buffer1, "sman"))
			{
				if(gui_allowed(1))
					sys_ppu_thread_create(&thread_id_gui, slaunch_thread, 0, 2000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_MENU);
				else
					sys_timer_sleep(3);
			}
			else
			{
				printf(SMAN_LOG "unknown -- s:%03i c:%02i th:%i [%s]\r\n", conn_s, max_cc, conn_s_p>>16, buffer1);
				send_http_ok(conn_s);
			}
		}
		else
			printf(SMAN_LOG "timeout -- s:%03i c:%02i th:%i\r\n", conn_s, max_cc, conn_s_p>>16);

		goto leave2;
	}

leave2_f:
	send_file(WMRES "/www_foot.htm", conn_s);
leave2:
	sclose(&conn_s);
	if(!addr) break;
	}
leave1:
	if(addr) free(addr);

	http_threads--;
	printf(SMAN_LOG "WWW_THREAD_ID: %i [EXIT]\r\n", conn_s_p>>16);
	sys_ppu_thread_exit(0);
}

static void handleclient_ftp(u64 conn_s_ftp_p)
{

	int conn_s_ftp = (int)conn_s_ftp_p; // main communications socket
	int data_s = -1;			// data socket
	int data_ls = -1;

	int connactive = 1;			// whether the ftp connection is active or not
	int dataactive = 0;			// prevent the data connection from being closed at the end of the loop
	int loggedin = 0;			// whether the user is logged in or not

	int rest = 0;				// for resuming file transfers

	char* addr		=(char*)malloc(3072);	if(!addr) goto leave;

	char* buffer	=addr;			// 1024
	char* cwd		=addr+1024;		//  512
	char* tempcwd	=addr+1536;		//  512
	char* param		=addr+2048;		//  512
	char* filename	=addr+2560;		//  256
	char* rnfr		=addr+2816;		//  256

	char cmd[16];
	struct CellFsStat buf;

#ifdef USE_NTFS
	struct stat bufn;
	struct statvfs vbuf;
#endif
	int fd=-1;
	u8* sysmem=NULL;

	int p1x = 0;
	int p2x = 0;

	CellRtcDateTime rDate;

	sys_net_sockinfo_t conn_info;
	sys_net_get_sockinfo(conn_s_ftp, &conn_info, 1);

	char ip_address[16];
	char pasv_output[56];
	sprintf(ip_address, "%s", inet_ntoa(conn_info.local_adr));
	for(u8 n=0;n<strlen(ip_address);n++) if(ip_address[n]=='.') ip_address[n]=',';

	strcpy(cwd, "/");

#ifdef USE_NTFS
	sprintf(buffer, "220 sMAN " WM_VERSION " [NTFS:%i]\r\n", mountCount);
#else
	sprintf(buffer, "220 sMAN " WM_VERSION "\r\n");
#endif
	ssend(conn_s_ftp, buffer);

	while(connactive == 1 && working)
	{

		if(recv(conn_s_ftp, buffer, 1023, 0) > 0)
		{
			buffer[strcspn(buffer, "\n")] = '\0';
			buffer[strcspn(buffer, "\r")] = '\0';

			int split = ssplit(buffer, cmd, 15, param, 511);

			if(loggedin == 1)
			{
				if(strcasecmp(cmd, "CWD") == 0)
				{

					strcpy(tempcwd, cwd);

					if(split == 1)
					{
						absPath(tempcwd, param, cwd);
					}
#ifdef USE_NTFS
					if(strstr(tempcwd, "dev_ntfs"))
					{
						strcpy(cwd, tempcwd);
						tempcwd[10]=':';
						if(strlen(tempcwd)<13 || (ps3ntfs_stat(tempcwd+5, &bufn)>=0 && (bufn.st_mode & S_IFDIR)))
						{
							ssend(conn_s_ftp, "250 OK\r\n");
						}
						else
							ssend(conn_s_ftp, "550 ERR\r\n");
					}
					else
#endif
					if(isDir(tempcwd))
					{
						strcpy(cwd, tempcwd);
						ssend(conn_s_ftp, "250 OK\r\n");
					}
					else
					{
						ssend(conn_s_ftp, "550 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "CDUP") == 0)
				{
					int pos = strlen(cwd) - 2;

					for(int i = pos; i > 0; i--)
					{
						if(i < pos && cwd[i] == '/')
						{
							break;
						}
						else
						{
							cwd[i] = '\0';
						}
					}
					ssend(conn_s_ftp, "250 OK\r\n");
				}
				else
				if(strcasecmp(cmd, "PWD") == 0)
				{
					sprintf(buffer, "257 \"%s\"\r\n", cwd);
					ssend(conn_s_ftp, buffer);
				}
				else
				if(strcasecmp(cmd, "TYPE") == 0)
				{
					ssend(conn_s_ftp, "200 TYPE OK\r\n");
					dataactive = 1;
				}
				else
				if(strcasecmp(cmd, "REST") == 0)
				{
					if(split == 1)
					{
						ssend(conn_s_ftp, "350 REST command successful\r\n");
						rest = my_atoi(param);
						dataactive = 1;
					}
					else
					{
						ssend(conn_s_ftp, "501 No restart point\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "QUIT") == 0 || strcasecmp(cmd, "BYE") == 0)
				{
					ssend(conn_s_ftp, "221 BYE\r\n");
					connactive = 0;
				}
				else
				if(strcasecmp(cmd, "FEAT") == 0)
				{
					ssend(conn_s_ftp, "211-Ext:\r\n");
					ssend(conn_s_ftp, " SIZE\r\n");
					ssend(conn_s_ftp, " MDTM\r\n");
					ssend(conn_s_ftp, " PORT\r\n");
					ssend(conn_s_ftp, " CDUP\r\n");
					ssend(conn_s_ftp, " ABOR\r\n");
					ssend(conn_s_ftp, " REST STREAM\r\n");
					ssend(conn_s_ftp, " PASV\r\n");
					ssend(conn_s_ftp, " LIST\r\n");
					ssend(conn_s_ftp, " MLSD\r\n");
					ssend(conn_s_ftp, " MLST type*;size*;modify*;UNIX.mode*;UNIX.uid*;UNIX.gid*;\r\n");
					ssend(conn_s_ftp, "211 End\r\n");
				}
				else
				if(strcasecmp(cmd, "PORT") == 0)
				{
					rest = 0;

					if(split == 1)
					{
						char data[6][4];
						int i = 0;
						u8 k=0;

						for(u8 j=0;j<=strlen(param);j++)
						{
							if(param[j]!=',' && param[j]!=0) { data[i][k]=param[j]; k++; }
							else {data[i][k]=0; i++; k=0;}
							if(i>=6) break;
						}

						if(i == 6)
						{
							char ipaddr[16];
							sprintf(ipaddr, "%s.%s.%s.%s", data[0], data[1], data[2], data[3]);

							data_s=connect_to_server(ipaddr, getPort(my_atoi(data[4]), my_atoi(data[5])));

							if(data_s>=0)
							{
								ssend(conn_s_ftp, "200 OK\r\n");
								dataactive = 1;
							}
							else
							{
								ssend(conn_s_ftp, "451 ERR\r\n");
							}
						}
						else
						{
							ssend(conn_s_ftp, "501 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "SITE") == 0)
				{
					if(split == 1)
					{
						split = ssplit(param, cmd, 31, filename, 511);

						if(strcasecmp(cmd, "HELP") == 0)
						{
							ssend(conn_s_ftp, "214-CMDs:\r\n");
							ssend(conn_s_ftp, " SITE SHUTDOWN\r\n");
							ssend(conn_s_ftp, " SITE RESTART\r\n");
#ifdef USE_NTFS
							ssend(conn_s_ftp, " SITE NTFS\r\n");
#endif
							ssend(conn_s_ftp, "214 End\r\n");

						}
						else
						if(strcasecmp(cmd, "SHUTDOWN") == 0)
						{
							ssend(conn_s_ftp, "221 OK\r\n");
							if(sysmem) free(sysmem);
							free(addr);
							working=0;
							// {system_call_4(379,0x1100,0,0,0);}
							vshmain_87BB0001(1);
							goto leave;
						}
						else
						if(strcasecmp(cmd, "RESTART") == 0)
						{
							ssend(conn_s_ftp, "221 OK\r\n");
#ifdef USE_NTFS
							if(mounts && mountCount) for (u8 u = 0; u < mountCount; u++) ntfsUnmount(mounts[u].name, 1);
#endif
							if(sysmem) free(sysmem);
							// {system_call_4(379,0x1200,0,0,0);}
							// {system_call_3(379, 0x8201, NULL, 0);}
							free(addr);
							working=0;
							vshmain_87BB0001(2);
							goto leave;
						}
#ifdef USE_NTFS
						else
						if(strcasecmp(cmd, "NTFS") == 0)
						{
							sprintf(buffer, "221 OK [NTFS VOLUMES: %i]\r\n", mountCount);
							ssend(conn_s_ftp, buffer);
						}
#endif
						else
						{
							ssend(conn_s_ftp, "500 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "NOOP") == 0)
				{
					ssend(conn_s_ftp, "200 NOOP\r\n");
				}
				else
				if(strcasecmp(cmd, "MLSD") == 0 || strcasecmp(cmd, "LIST") == 0 || strcasecmp(cmd, "MLST") == 0)
				{
					if(data_s > 0)
					{
						int nolist = (strcasecmp(cmd, "MLSD") == 0 || strcasecmp(cmd, "MLST") == 0);

						strcpy(tempcwd, cwd);

						if(split == 1)
							absPath(tempcwd, param, cwd);

						char dirtype[2];
#ifdef USE_NTFS
						if(strstr(tempcwd, "/dev_nt"))
						{
							DIR_ITER *pdir;
							tempcwd[10]=':';
							if(tempcwd[11]!='/'){tempcwd[11]='/'; tempcwd[12]=0;}
							pdir = ps3ntfs_diropen(tempcwd+5); // /dev_ntfs1v -> ntfs1:
							if(pdir)
							{
								ssend(conn_s_ftp, "150 OK\r\n");
								while (ps3ntfs_dirnext(pdir, filename, &bufn) == 0)
								{
									if(filename[0]=='$') continue;
									if(nolist)
									{
										cellRtcSetTime_t(&rDate, bufn.st_mtime);
										if(strcmp(filename, ".") == 0)
										{
											dirtype[0] = 'c';
										}
										else
										if(strcmp(filename, "..") == 0)
										{
											dirtype[0] = 'p';
										}
										else
										{
											dirtype[0] = '\0';
										}

										dirtype[1] = '\0';

										if(strcasecmp(cmd, "MLSD") == 0)
										sprintf(buffer, "type=%s%s;siz%s=%llu;modify=%04i%02i%02i%02i%02i%02i;UNIX.mode=0%i%i%i;UNIX.uid=root;UNIX.gid=root; %s\r\n",
											dirtype,
											((bufn.st_mode & S_IFDIR) != 0) ? "dir" : "file",
											((bufn.st_mode & S_IFDIR) != 0) ? "d" : "e", (unsigned long long)bufn.st_size, rDate.year, rDate.month, rDate.day, rDate.hour, rDate.minute, rDate.second,
											(((bufn.st_mode & S_IRUSR) != 0) * 4 + ((bufn.st_mode & S_IWUSR) != 0) * 2 + ((bufn.st_mode & S_IXUSR) != 0) * 1),
											(((bufn.st_mode & S_IRGRP) != 0) * 4 + ((bufn.st_mode & S_IWGRP) != 0) * 2 + ((bufn.st_mode & S_IXGRP) != 0) * 1),
											(((bufn.st_mode & S_IROTH) != 0) * 4 + ((bufn.st_mode & S_IWOTH) != 0) * 2 + ((bufn.st_mode & S_IXOTH) != 0) * 1),
											filename);
										else
											sprintf(buffer, " type=%s%s;siz%s=%llu;modify=%04i%02i%02i%02i%02i%02i;UNIX.mode=0%i%i%i;UNIX.uid=root;UNIX.gid=root; %s\r\n",
												dirtype,
												((bufn.st_mode & S_IFDIR) != 0) ? "dir" : "file",
												((bufn.st_mode & S_IFDIR) != 0) ? "d" : "e", (unsigned long long)bufn.st_size, rDate.year, rDate.month, rDate.day, rDate.hour, rDate.minute, rDate.second,
												(((bufn.st_mode & S_IRUSR) != 0) * 4 + ((bufn.st_mode & S_IWUSR) != 0) * 2 + ((bufn.st_mode & S_IXUSR) != 0) * 1),
												(((bufn.st_mode & S_IRGRP) != 0) * 4 + ((bufn.st_mode & S_IWGRP) != 0) * 2 + ((bufn.st_mode & S_IXGRP) != 0) * 1),
												(((bufn.st_mode & S_IROTH) != 0) * 4 + ((bufn.st_mode & S_IWOTH) != 0) * 2 + ((bufn.st_mode & S_IXOTH) != 0) * 1),
												filename);
									}
									else
										sprintf(buffer, "%s%s%s%s%s%s%s%s%s%s   1 root  root        %llu %s %02i %02i:%02i %s\r\n",
										((bufn.st_mode & S_IFDIR) != 0) ? "d" : "-",
										((bufn.st_mode & S_IRUSR) != 0) ? "r" : "-",
										((bufn.st_mode & S_IWUSR) != 0) ? "w" : "-",
										((bufn.st_mode & S_IXUSR) != 0) ? "x" : "-",
										((bufn.st_mode & S_IRGRP) != 0) ? "r" : "-",
										((bufn.st_mode & S_IWGRP) != 0) ? "w" : "-",
										((bufn.st_mode & S_IXGRP) != 0) ? "x" : "-",
										((bufn.st_mode & S_IROTH) != 0) ? "r" : "-",
										((bufn.st_mode & S_IWOTH) != 0) ? "w" : "-",
										((bufn.st_mode & S_IXOTH) != 0) ? "x" : "-",
										(unsigned long long)bufn.st_size, smonth[rDate.month-1], rDate.day,
										rDate.hour, rDate.minute, filename);

									if(ssend(data_s, buffer)<0) break;
									sys_timer_usleep(1000);
								}
								ps3ntfs_dirclose(pdir);
								if(strlen(tempcwd)<14)
								{
									tempcwd[11]='/'; tempcwd[12]=0;
									ps3ntfs_statvfs(tempcwd+5, &vbuf);
									tempcwd[10]=0;
									sprintf(filename, "226 [%s] [ %lu MB / %lu GB free]\r\n", tempcwd, (long unsigned int)((vbuf.f_bfree * (vbuf.f_bsize>>10))>>10),(long unsigned int)((vbuf.f_bfree * (vbuf.f_bsize>>10))>>20));
									ssend(conn_s_ftp, filename);
								}
								else
									ssend(conn_s_ftp, "226 OK\r\n");
							}
							else
							{
								sprintf(buffer, "550 ERR [%s]\r\n", tempcwd+5);
								ssend(conn_s_ftp, buffer);
							}
						}
						else
#endif
						if(cellFsOpendir( (isDir(tempcwd) ? tempcwd : cwd), &fd) == CELL_FS_SUCCEEDED)
						{
							ssend(conn_s_ftp, "150 OK\r\n");

							CellFsDirent entry_r;
							u64 read_r;
							CellFsDirectoryEntry entry;
							u32 read_f;

							while(1)
							{
								if(!tempcwd[1]) // list root folder using the slower readdir
								{
									if(cellFsReaddir(fd, &entry_r, &read_r) || !read_r) break;
									strcpy(entry.entry_name.d_name, entry_r.d_name);
								}
								else
									if(cellFsGetDirectoryEntries(fd, &entry, sizeof(entry), &read_f) || !read_f) break;

#ifdef USE_NTFS
								// use host_root to expand all /dev_ntfs entries in root
								bool is_ntfs=(!strcmp(entry.entry_name.d_name, "host_root") && mountCount>0 && mounts);
								if(is_ntfs || strcmp(entry.entry_name.d_name, "host_root"))
								{
									u8 ntmp=1;
									if(is_ntfs) ntmp=mountCount;
									for (u8 u = 0; u < ntmp; u++)
									{
										if(is_ntfs) sprintf(entry.entry_name.d_name, "dev_%s0", mounts[u].name);
#endif
										if(!strcmp(entry.entry_name.d_name, "app_home") || !strcmp(entry.entry_name.d_name, "host_root")) continue;
										absPath(filename, entry.entry_name.d_name, cwd);

										if(!tempcwd[1])
										{
											cellFsStat(filename, &buf);
											entry.attribute.st_mode=buf.st_mode;
											entry.attribute.st_size=buf.st_size;
											entry.attribute.st_mtime=buf.st_mtime;
										}

										cellRtcSetTime_t(&rDate, entry.attribute.st_mtime);
										if(nolist)
										{
											if(strcmp(entry.entry_name.d_name, ".") == 0)	dirtype[0] = 'c';
											else
											if(strcmp(entry.entry_name.d_name, "..") == 0)	dirtype[0] = 'p';
											else								dirtype[0] = '\0';

											dirtype[1] = '\0';

											if(strcasecmp(cmd, "MLSD") == 0)
											sprintf(buffer, "type=%s%s;siz%s=%llu;modify=%04i%02i%02i%02i%02i%02i;UNIX.mode=0%i%i%i;UNIX.uid=root;UNIX.gid=root; %s\r\n",
												dirtype,
												((entry.attribute.st_mode & S_IFDIR) != 0) ? "dir" : "file",
												((entry.attribute.st_mode & S_IFDIR) != 0) ? "d" : "e", (unsigned long long)entry.attribute.st_size, rDate.year, rDate.month, rDate.day, rDate.hour, rDate.minute, rDate.second,
												(((entry.attribute.st_mode & S_IRUSR) != 0) * 4 + ((entry.attribute.st_mode & S_IWUSR) != 0) * 2 + ((entry.attribute.st_mode & S_IXUSR) != 0) * 1),
												(((entry.attribute.st_mode & S_IRGRP) != 0) * 4 + ((entry.attribute.st_mode & S_IWGRP) != 0) * 2 + ((entry.attribute.st_mode & S_IXGRP) != 0) * 1),
												(((entry.attribute.st_mode & S_IROTH) != 0) * 4 + ((entry.attribute.st_mode & S_IWOTH) != 0) * 2 + ((entry.attribute.st_mode & S_IXOTH) != 0) * 1),
												entry.entry_name.d_name);
											else
												sprintf(buffer, " type=%s%s;siz%s=%llu;modify=%04i%02i%02i%02i%02i%02i;UNIX.mode=0%i%i%i;UNIX.uid=root;UNIX.gid=root; %s\r\n",
													dirtype,
													((entry.attribute.st_mode & S_IFDIR) != 0) ? "dir" : "file",
													((entry.attribute.st_mode & S_IFDIR) != 0) ? "d" : "e", (unsigned long long)entry.attribute.st_size, rDate.year, rDate.month, rDate.day, rDate.hour, rDate.minute, rDate.second,
													(((entry.attribute.st_mode & S_IRUSR) != 0) * 4 + ((entry.attribute.st_mode & S_IWUSR) != 0) * 2 + ((entry.attribute.st_mode & S_IXUSR) != 0) * 1),
													(((entry.attribute.st_mode & S_IRGRP) != 0) * 4 + ((entry.attribute.st_mode & S_IWGRP) != 0) * 2 + ((entry.attribute.st_mode & S_IXGRP) != 0) * 1),
													(((entry.attribute.st_mode & S_IROTH) != 0) * 4 + ((entry.attribute.st_mode & S_IWOTH) != 0) * 2 + ((buf.st_mode & S_IXOTH) != 0) * 1),
													entry.entry_name.d_name);
										}
										else
											sprintf(buffer, "%s%s%s%s%s%s%s%s%s%s   1 root  root        %llu %s %02i %02i:%02i %s\r\n",
											((entry.attribute.st_mode & S_IFDIR) != 0) ? "d" : "-",
											((entry.attribute.st_mode & S_IRUSR) != 0) ? "r" : "-",
											((entry.attribute.st_mode & S_IWUSR) != 0) ? "w" : "-",
											((entry.attribute.st_mode & S_IXUSR) != 0) ? "x" : "-",
											((entry.attribute.st_mode & S_IRGRP) != 0) ? "r" : "-",
											((entry.attribute.st_mode & S_IWGRP) != 0) ? "w" : "-",
											((entry.attribute.st_mode & S_IXGRP) != 0) ? "x" : "-",
											((entry.attribute.st_mode & S_IROTH) != 0) ? "r" : "-",
											((entry.attribute.st_mode & S_IWOTH) != 0) ? "w" : "-",
											((entry.attribute.st_mode & S_IXOTH) != 0) ? "x" : "-",
											(unsigned long long)entry.attribute.st_size, smonth[rDate.month-1], rDate.day,
											rDate.hour, rDate.minute, entry.entry_name.d_name);

										if(ssend(data_s, buffer)<0) break;
#ifdef USE_NTFS
									}
								}
#endif
							}

							cellFsClosedir(fd);
							if(strlen(tempcwd)>1 && strlen(tempcwd)<13)
							{
								u32 blockSize;
								u64 freeSize;

								if(strchr(tempcwd+1, '/'))
									tempcwd[strchr(tempcwd+1, '/')-tempcwd]=0;
								cellFsGetFreeSize(tempcwd, &blockSize, &freeSize);
								sprintf(filename, "226 [%s] [ %i MB free ]\r\n", tempcwd, (int)((blockSize*freeSize)>>20));
								ssend(conn_s_ftp, filename);
							}
							else
							{
								ssend(conn_s_ftp, "226 OK\r\n");
							}
						}
						else
						{
							ssend(conn_s_ftp, "550 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "425 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "PASV") == 0)
				{
					u16 pasv_retry=0;
					rest = 0;

pasv_again:
					pasv_port++;
					if(pasv_port<32800 || pasv_port>65000) pasv_port=32800;

					p1x = (pasv_port >> 8 ); // use ports 32768 -> 65279 (0x8000 -> 0xFEFF)
					p2x = (pasv_port & 0xff);

					data_ls = slisten(getPort(p1x, p2x), 1);
					ftpd_socket[ftp_threads+7]=data_ls;
					if(data_ls >= 0)
					{
						sprintf(pasv_output, "227 Entering Passive Mode (%s,%i,%i)\r\n", ip_address, p1x, p2x);
						ssend(conn_s_ftp, pasv_output);

						if((data_s = accept(data_ls, NULL, NULL)) >= 0)
						{
							ftpd_socket[ftp_threads+7]=conn_s_ftp;
							dataactive = 1;
						}
						else
						{
							ssend(conn_s_ftp, "451 ERR\r\n");
						}

					}
					else
					{
						if(pasv_retry<23000)
						{
							pasv_retry++;
							goto pasv_again;
						}
						ssend(conn_s_ftp, "451 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "RETR") == 0)
				{
					if(data_s > 0)
					{
						if(split == 1)
						{
							absPath(filename, param, cwd);
							int rr=-4;
#ifdef USE_NTFS
							if(strstr(filename, "/dev_nt"))
							{
								filename[10]=':';
								fd=ps3ntfs_open(filename+5, O_RDONLY, 0);
								if(fd>0)
								{
									if(!sysmem) sysmem=(u8*)malloc(BUFFER_SIZE_FTP);
									if(sysmem)
									{
										int read_e = 0;
										ps3ntfs_seek64(fd, rest, SEEK_SET);
										rest = 0;
										ssend(conn_s_ftp, "150 OK\r\n");
										rr=0;

										while(working)
										{
											read_e = ps3ntfs_read(fd, (void *)sysmem, BUFFER_SIZE_FTP);
											if(read_e>=0)
											{
												if(read_e>0)
												{
													if(send(data_s, sysmem, (size_t)read_e, 0)<0) {rr=-3; break;}
												}
												else
													break;
											}
											else
												{rr=-2;break;}
										}
									}
									ps3ntfs_close(fd);
								}

								if( rr == 0)
									ssend(conn_s_ftp, "226 OK\r\n");

								else if( rr == -4)
									ssend(conn_s_ftp, "550 ERR\r\n");
								else
									ssend(conn_s_ftp, "451 ERR\r\n");
							}
							else
#endif
							{
								if(cellFsOpen(filename, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
								{
									if(!sysmem) sysmem=(u8*)malloc(BUFFER_SIZE_FTP);
									if(sysmem)
									{
										u64 read_e = 0, pos; //, write_e

										cellFsLseek(fd, rest, CELL_FS_SEEK_SET, &pos);
										rest = 0;

										ssend(conn_s_ftp, "150 OK\r\n");
										rr=0;

										while(working)
										{
											sys_timer_usleep(1668);
											if(cellFsRead(fd, (void *)sysmem, BUFFER_SIZE_FTP, &read_e)==CELL_FS_SUCCEEDED)
											{
												if(read_e>0)
												{
													if(send(data_s, sysmem, (size_t)read_e, 0)<0) {rr=-3; break;}
												}
												else
													break;
											}
											else
												{rr=-2;break;}
										}
									}
									cellFsClose(fd);
								}

								if( rr == 0)
									ssend(conn_s_ftp, "226 OK\r\n");

								else if( rr == -4)
									ssend(conn_s_ftp, "550 ERR\r\n");
								else
									ssend(conn_s_ftp, "451 ERR\r\n");

							}

						}
						else
							ssend(conn_s_ftp, "501 ERR\r\n");
					}
					else
					{
						ssend(conn_s_ftp, "425 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "DELE") == 0)
				{
					if(split == 1)
					{
#ifdef USE_NTFS
						absPath(filename, param, cwd);
						if(strstr(filename, "/dev_nt"))
						{
							filename[10]=':';
							if(ps3ntfs_unlink(filename+5)>=0) // /dev_ntfs1v -> ntfs1:
							{
								ssend(conn_s_ftp, "250 OK\r\n");
							}
							else
							{
								ssend(conn_s_ftp, "550 ERR\r\n");
							}
						}
						else
#endif
						if(cellFsUnlink(filename) == 0)
						{
							ssend(conn_s_ftp, "250 OK\r\n");
						}
						else
						{
							ssend(conn_s_ftp, "550 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "MKD") == 0)
				{
					if(split == 1)
					{

						absPath(filename, param, cwd);
#ifdef USE_NTFS
						if(strstr(filename, "/dev_nt"))
						{
							filename[10]=':';
							if(ps3ntfs_mkdir(filename+5, 0777)>=0)
							{
								sprintf(buffer, "257 \"%s\" OK\r\n", param);
								ssend(conn_s_ftp, buffer);
							}
							else
								ssend(conn_s_ftp, "550 ERR\r\n");
						}
						else
#endif
						if(cellFsMkdir(filename, CELL_FS_S_IFDIR | 0777) == 0)
						{
							sprintf(buffer, "257 \"%s\" OK\r\n", param);
							ssend(conn_s_ftp, buffer);
						}
						else
							ssend(conn_s_ftp, "550 ERR\r\n");
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "RMD") == 0)
				{
					if(split == 1)
					{
						absPath(filename, param, cwd);
#ifdef USE_NTFS
						if(strstr(filename, "/dev_nt"))
						{
							filename[10]=':';
							if(ps3ntfs_unlink(filename+5)>=0)
								ssend(conn_s_ftp, "250 OK\r\n");
							else
								ssend(conn_s_ftp, "550 ERR\r\n");
						}
						else
#endif
						if(cellFsRmdir(filename) == 0)
						{
							ssend(conn_s_ftp, "250 OK\r\n");
						}
						else
						{
							ssend(conn_s_ftp, "550 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "STOR") == 0)
				{
					if(data_s > 0)
					{
						if(split == 1)
						{
							absPath(filename, param, cwd);

							int rr=-1;
							u64 pos=0;
							ssize_t read_e = 0;
#ifdef USE_NTFS
							if(strstr(filename, "/dev_nt"))
							{
								filename[10]=':';
								if(rest)
									fd=ps3ntfs_open(filename+5, O_CREAT | O_WRONLY, 0777);
								else
									fd=ps3ntfs_open(filename+5, O_CREAT | O_WRONLY | O_TRUNC, 0777);

								if(fd>0)
								{
									if(!sysmem) sysmem=(u8*)malloc(BUFFER_SIZE_FTP);
									if(sysmem)
									{
										ps3ntfs_seek64(fd, rest, SEEK_SET);

										rest = 0;
										rr = 0;

										ssend(conn_s_ftp, "150 OK\r\n");

										while(working)
										{
											sys_timer_usleep(1668);
											read_e = recv(data_s, sysmem, BUFFER_SIZE_FTP, MSG_WAITALL);
											if(read_e > 0)
											{
												if(ps3ntfs_write(fd, (const char*)sysmem, read_e)!=read_e) {rr=-1;break;}
											}
											else
											if(read_e < 0)
												{rr=-1; break;}
											else
												break;
										}
									}
									else rr=-2;
									ps3ntfs_close(fd);
									if(!working || rr!=0) ps3ntfs_unlink(filename+5);
								}
								else rr=-3;
							}
							else
#endif
							if(cellFsOpen(filename, CELL_FS_O_CREAT|CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
							{

								if(!sysmem) sysmem=(u8*)malloc(BUFFER_SIZE_FTP);
								if(sysmem)
								{
									if(rest)
										cellFsLseek(fd, rest, CELL_FS_SEEK_SET, &pos);
									else
										cellFsFtruncate(fd, 0);

									rest = 0;
									rr = 0;

									ssend(conn_s_ftp, "150 OK\r\n");

									while(working)
									{
										//sys_timer_usleep(1668);
										read_e = recv(data_s, sysmem, BUFFER_SIZE_FTP, MSG_WAITALL);
										if(read_e > 0)
										{
											if(cellFsWrite(fd, sysmem, read_e, NULL)!=CELL_FS_SUCCEEDED) {rr=-1;break;}
										}
										else
										if(read_e < 0)
											{rr=-1; break;}
										else
											break;
									}

								}
								else rr=-2;
								cellFsClose(fd);
								cellFsChmod(filename, 0666);
								if(!working || rr!=0) cellFsUnlink(filename);
							}

							if(rr == 0)
								ssend(conn_s_ftp, "226 OK\r\n");
							else
							if(rr == -2 || rr == -3)
							{
								sys_timer_usleep(1668);
								ssend(conn_s_ftp, "450 ERR\r\n");
							}
							else
								ssend(conn_s_ftp, "451 ERR\r\n");
						}
						else
						{
							ssend(conn_s_ftp, "501 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "425 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "SIZE") == 0)
				{
					if(split == 1)
					{
						absPath(filename, param, cwd);
#ifdef USE_NTFS
						if(strstr(filename, "/dev_nt"))
						{
							filename[10]=':';
							if(ps3ntfs_stat(filename+5, &bufn)>=0) // /dev_ntfs1v -> ntfs1:
							{
								sprintf(buffer, "213 %llu\r\n", (unsigned long long)bufn.st_size);
								ssend(conn_s_ftp, buffer);
								dataactive = 1;
							}
							else
							{
								ssend(conn_s_ftp, "550 ERR\r\n");
							}
						}
						else
#endif
						if(cellFsStat(filename, &buf)==CELL_FS_SUCCEEDED)
						{
							sprintf(buffer, "213 %llu\r\n", (unsigned long long)buf.st_size);
							ssend(conn_s_ftp, buffer);
							dataactive = 1;
						}
						else
						{
							ssend(conn_s_ftp, "550 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "SYST") == 0)
				{
					ssend(conn_s_ftp, "215 UNIX Type: L8\r\n");
				}
				else
				if(strcasecmp(cmd, "MDTM") == 0)
				{
					if(split == 1)
					{
						absPath(filename, param, cwd);
#ifdef USE_NTFS
						if(strstr(filename, "/dev_nt"))
						{
							filename[10]=':';
							if(ps3ntfs_stat(filename+5, &bufn)>=0) // /dev_ntfs1v -> ntfs1:
							{
								cellRtcSetTime_t(&rDate, bufn.st_mtime);
								sprintf(buffer, "213 %04i%02i%02i%02i%02i%02i\r\n", rDate.year, rDate.month, rDate.day, rDate.hour, rDate.minute, rDate.second);
								ssend(conn_s_ftp, buffer);
							}
							else
								ssend(conn_s_ftp, "550 ERR\r\n");
						}
						else
#endif
						if(cellFsStat(filename, &buf)==CELL_FS_SUCCEEDED)
						{
							cellRtcSetTime_t(&rDate, buf.st_mtime);
							sprintf(buffer, "213 %04i%02i%02i%02i%02i%02i\r\n", rDate.year, rDate.month, rDate.day, rDate.hour, rDate.minute, rDate.second);
							ssend(conn_s_ftp, buffer);
						}
						else
						{
							ssend(conn_s_ftp, "550 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "ABOR") == 0)
				{
					sclose(&data_s);
					ssend(conn_s_ftp, "226 ABOR OK\r\n");
				}

				else
				if(strcasecmp(cmd, "RNFR") == 0)
				{
					if(split == 1)
					{
						absPath(rnfr, param, cwd);

						if(cellFsStat(rnfr, &buf)==CELL_FS_SUCCEEDED
#ifdef USE_NTFS
							|| strstr(rnfr, "/dev_nt")
#endif
							)
						{
							ssend(conn_s_ftp, "350 RNFR OK\r\n");
						}
						else
						{
							rnfr[0]=0;
							ssend(conn_s_ftp, "550 RNFR ER\r\n");
						}
					}
					else
					{
						rnfr[0]=0;
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}

				else
				if(strcasecmp(cmd, "RNTO") == 0)
				{
					if(split == 1 && rnfr[0]=='/')
					{
						absPath(filename, param, cwd);
#ifdef USE_NTFS
						if(strstr(rnfr, "/dev_nt") && strstr(filename, "/dev_nt"))
						{
							rnfr[10]=':'; filename[10]=':';
							if(ps3ntfs_rename(rnfr+5, filename+5)>=0)
								ssend(conn_s_ftp, "250 OK\r\n");
							else
								ssend(conn_s_ftp, "550 ERR\r\n");
						}
						else
#endif
						if(cellFsRename(rnfr, filename) == CELL_FS_SUCCEEDED)
						{
							ssend(conn_s_ftp, "250 OK\r\n");
						}
						else
						{
							ssend(conn_s_ftp, "550 ERR\r\n");
						}
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
					rnfr[0]=0;
				}

				else
				if(strcasecmp(cmd, "USER") == 0 || strcasecmp(cmd, "PASS") == 0)
				{
					ssend(conn_s_ftp, "230 OK\r\n");
				}
				else
				{
					ssend(conn_s_ftp, "502 ERR\r\n");
				}

				if(dataactive == 1)
				{
					dataactive = 0;
				}
				else
				{
					sclose(&data_s);
					if(data_ls>0) {sclose(&data_ls); data_ls=-1;}
					rest = 0;
				}
			}
			else
			{
				// available commands when not logged in
				if(strcasecmp(cmd, "USER") == 0)
				{
					if(split == 1)
					{
						ssend(conn_s_ftp, "331 OK\r\n");
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "PASS") == 0)
				{
					if(split == 1)
					{
						ssend(conn_s_ftp, "230 OK\r\n");
						loggedin = 1;
					}
					else
					{
						ssend(conn_s_ftp, "501 ERR\r\n");
					}
				}
				else
				if(strcasecmp(cmd, "QUIT") == 0 || strcasecmp(cmd, "BYE") == 0)
				{
					ssend(conn_s_ftp, "221 OK\r\n");
					connactive = 0;
					break;
				}
				else
				{
					ssend(conn_s_ftp, "530 ERR\r\n");
				}
			}
		}
		else
		{
			connactive = 0;
			break;
		}
	}

	if(sysmem) free(sysmem);
	if(addr) free(addr);
	sclose(&data_s);

leave:
	sclose(&conn_s_ftp);
	printf(SMAN_LOG "ftpc_thread [EXIT]\r\n");
	ftp_threads--;
	sys_ppu_thread_exit(0);
}

static void ftpd_thread(u64 arg)
{
	int list_s=-1;

relisten_f:
	list_s = slisten(FTPPORT, FTP_BACKLOG);

	if(list_s<0 && working)
	{
		sys_timer_sleep(3);
		goto relisten_f;
	}

	if(list_s >= 0)
	{
		ftpd_socket[0]=list_s;
		while(working)
		{
			sys_timer_usleep(100000);
			if(ftp_threads>6) {sys_timer_sleep(1); continue;}
			int conn_s_ftp;
			if((conn_s_ftp = accept(list_s, NULL, NULL)) >= 0)
			{
				ftp_threads++;
				ftpd_socket[ftp_threads]=conn_s_ftp;
				if(!working) break;
				sys_ppu_thread_t id;
				sys_ppu_thread_create(&id, handleclient_ftp, (u64)conn_s_ftp, (800+ftp_threads*10), 0x2000, 0, THREAD_FTPC);
			}
			else
				if(sys_net_errno==SYS_NET_EBADF || sys_net_errno==SYS_NET_ENETDOWN)
				{
					sclose(&list_s);
					if(working) goto relisten_f;
				}
		}

		sclose(&list_s);
	}
	printf(SMAN_LOG "ftpd_thread [EXIT]\r\n");
	sys_ppu_thread_exit(0);
}

static void sm_stop_thread(u64 arg);
static void finalize_module(void);
static void wwwd_thread(u64 list_ss);

static void poll_thread(u64 poll)
{
	u8 to=0;
	u32 t1=0, t2=0;
	u8 lasttemp=0;
	old_fan=0;
	u8 stall=0;
	u8 step_up=5;
	//u8 step_down=2;
	u8 smoothstep=0;
	int delta=0;
	u32 usb_handle = -1;
	u8 tmp[512];
	u32 r;
	old_fan=0;

	CellPadData		data;
	CellPadInfo2	infobuf;

	while(working)
	{
		if(max_temp && !(to&3))
		{
			t1=0;
			get_temperature(0, &t1); // 3E030000 -> 3E.03'C -> 62.(03/256)'C
			t2=t1;

			get_temperature(1, &t2); // 3E030000 -> 3E.03'C -> 62.(03/256)'C

			t1=t1>>24;
			t2=t2>>24;

			{if(t2>t1) t1=t2;}

			if(!lasttemp) lasttemp=t1;

			delta=(lasttemp-t1);

			lasttemp=t1;

			if(t1>=max_temp || t1>84)
			{
				if(delta< 0) fan_speed+=2;
				if(delta==0 && t1!=(max_temp-1)) fan_speed++;
				if(delta==0 && t1>=(max_temp+1)) fan_speed+=(2+(t1-max_temp));
				if(delta> 0)
				{
					smoothstep++;
					if(smoothstep>1)
					{
						fan_speed--;
						smoothstep=0;
					}
				}
				if(t1>84)	 fan_speed+=step_up;
				if(delta< 0 && (t1-max_temp)>=2) fan_speed+=step_up;
			}
			else
			{
				if(delta< 0 && t1>=(max_temp-1)) fan_speed+=2;
				if(delta==0 && t1<=(max_temp-2))
				{
					smoothstep++;
					if(smoothstep>1)
					{
						fan_speed--;
						if(t1<=(max_temp-5)) fan_speed--;
						if(t1<=(max_temp-8)) fan_speed--;
						smoothstep=0;
					}
				}
				//if(delta==0 && t1>=(max_temp-1)) fan_speed++;
				if(delta> 0)
				{
					smoothstep++;
					if(smoothstep)
					{
						fan_speed--;
						smoothstep=0;
					}
				}
			}

			if(t1>76 && old_fan<0x43) fan_speed++;
			if(t1>84 && fan_speed<0xB0) {old_fan=0; fan_speed=0xB0;}

			if(fan_speed<((sm_config->minfan*255)/100)) fan_speed=(sm_config->minfan*255)/100;
			if(fan_speed>MAX_FANSPEED) fan_speed=MAX_FANSPEED;

			if(old_fan!=fan_speed || stall>35)
			{
				//if(t1>76 && fan_speed<0x50) fan_speed=0x50;
				//if(t1>77 && fan_speed<0x58) fan_speed=0x58;
				if(t1>78 && fan_speed<0x50) fan_speed+=2;
				if(old_fan!=fan_speed)
				{
					old_fan=fan_speed;
					fan_control(fan_speed, 1);
					//sprintf(debug, "OFAN: %x | CFAN: %x | TEMP: %i | SPEED APPLIED!\r\n", old_fan, fan_speed, t1); ssend(data_s, mytxt);
					stall=0;
				}
			}
			else
				if( old_fan>fan_speed && (old_fan-fan_speed)>8 && t1<(max_temp-3) )
					stall++;
		}

/*
FAIL SAFE: SELECT+L3+L2+R2
SHOW TEMP: SELECT+R3
PREV GAME: SELECT+L1
NEXT GAME: SELECT+R1
SHUTDOWN : L3+R2+X
RESTART  : L3+R2+O
FAN CNTRL: L3+R2+/\
UNLOAD SM: L3+R2+R3
*/
		if(!cellPadGetInfo2(&infobuf))
		{
			data.len=0;
			if (infobuf.port_status[0] == CELL_PAD_STATUS_CONNECTED && (cellPadGetData(0, &data) == CELL_PAD_OK) && data.len) goto pad_ok;
			if (infobuf.port_status[1] == CELL_PAD_STATUS_CONNECTED && (cellPadGetData(1, &data) == CELL_PAD_OK) && data.len) goto pad_ok;
		}
		else
			sys_timer_sleep(2);

		sys_timer_sleep(1);
		goto no_pad;

pad_ok:
		if ((data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_L2) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_R2))
		{
			if(gui_allowed(1))
			{
				sys_ppu_thread_create(&thread_id_gui, slaunch_thread, 0, 2000, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_MENU);
			}
			sys_timer_sleep(3);
		}
		else
		if ((data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_SELECT))
		{
			if( !(sm_config->combo & FAIL_SAFE)
				&& (data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_L3)
				&& (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_L2) // fail-safe mode
				&& (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_R2) // SELECT+L3+L2+R2
				)
			{
				cellFsUnlink((char*)"/dev_hdd0/boot_plugins.txt");
				working=0;
				// {system_call_3(379, 0x8201, NULL, 0);}
				vshmain_87BB0001(2);
				goto leave;
			}
			else
			if(!(sm_config->combo & SHOW_TEMP) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_R3) ) // SELECT+R3 show temperatures
			{
				get_temperature(0, &t1);
				get_temperature(1, &t2);

				u32 blockSize;
				u64 freeSize;
				cellFsGetFreeSize((char*)"/dev_hdd0", &blockSize, &freeSize);

				u8 st, mode, speed, unknown;
				if(!sm_config->fanc)
				{
					if(!dex_mode)
					{
						if(c_firmware>=4.55f)
						{
							backup[5]=peekq(0x8000000000009E38ULL);
							lv2poke32(0x8000000000009E38ULL, 0x38600001); // sys 409 get_fan_policy
						}
						else
						{
							backup[5]=peekq(0x8000000000009E28ULL);
							lv2poke32(0x8000000000009E28ULL, 0x38600001); // sys 409 get_fan_policy
						}
					}
					else //DEX
					{
						if(c_firmware>=4.55f)
						{
								backup[5]=peekq(0x8000000000009EB8ULL);
								lv2poke32(0x8000000000009EB8ULL, 0x38600001);
						}
						else if(c_firmware>=4.21f && c_firmware<=4.53f)
						{
								backup[5]=peekq(0x8000000000009EA8ULL);
								lv2poke32(0x8000000000009EA8ULL, 0x38600001);
						}
					}
				}

				sys_sm_get_fan_policy(0, &st, &mode, &speed, &unknown);

				if(!sm_config->fanc)
				{
					if(!dex_mode)
					{
						if(c_firmware>=4.55f)
							pokeq(0x8000000000009E38ULL, backup[5]);
						else
							pokeq(0x8000000000009E28ULL, backup[5]);
					}
					else //DEX
					{
						if(c_firmware>=4.55f)
								pokeq(0x8000000000009EB8ULL, backup[5]);
						else if(c_firmware>=4.21f && c_firmware<=4.53f)
								pokeq(0x8000000000009EA8ULL, backup[5]);
					}
				}

				sprintf((char*)tmp, "CPU: %i°C  RSX: %i°C  FAN: %i%%   \r\n " STR_STORAGE ": %i " STR_MBFREE, t1>>24, t2>>24, (int)(((int)speed*100)/255), (int)((blockSize*freeSize)>>20));
				show_msg((char*)tmp);
				sys_timer_sleep(2);
			}
			else
			if(sm_config->fanc && !(sm_config->combo & MANUALFAN) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_UP) ) // SELECT+UP increase TEMP/FAN
			{
				if(max_temp) //auto mode
				{
					max_temp++;
					if(max_temp>85) max_temp=85;
					sm_config->temp1=max_temp;
					sprintf((char*)tmp, STR_FANCH1 "%i°C", max_temp);
				}
				else
				{
					sm_config->manu++;
					sm_config->temp0= (u8)(((float)sm_config->manu * 255.f)/100.f);
					if(sm_config->temp0<0x33) sm_config->temp0=0x33;
					if(sm_config->temp0>MAX_FANSPEED) sm_config->temp0=MAX_FANSPEED;
					fan_control(sm_config->temp0, 0);
					sprintf((char*)tmp, STR_FANCH2 "%i%%", sm_config->manu);
				}
				save_settings();
				show_msg((char*)tmp);
				sys_timer_sleep(2);
			}
			else
			if(sm_config->fanc && !(sm_config->combo & MANUALFAN) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_DOWN) ) // SELECT+DOWN decrease TEMP/FAN
			{
				if(max_temp) //auto mode
				{
					if(max_temp>30) max_temp--;
					sm_config->temp1=max_temp;
					sprintf((char*)tmp, STR_FANCH1 "%i°C", max_temp);
				}
				else
				{
					if(sm_config->manu>20) sm_config->manu--;
					sm_config->temp0= (u8)(((float)sm_config->manu * 255.f)/100.f);
					if(sm_config->temp0<0x33) sm_config->temp0=0x33;
					if(sm_config->temp0>MAX_FANSPEED) sm_config->temp0=MAX_FANSPEED;
					fan_control(sm_config->temp0, 0);
					sprintf((char*)tmp, STR_FANCH2 "%i%%", sm_config->manu);
				}
				save_settings();
				show_msg((char*)tmp);
				sys_timer_sleep(2);
			}
			else
			if(!(sm_config->combo & PREV_GAME) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_L1) ) // SELECT+L1 (previous title)
			{
				led(GREEN, BLINK);
				mount_game((char*)"_prev", 3);
				sys_timer_sleep(3);
				led(GREEN, ON);
			}
			else
			if(!(sm_config->combo & NEXT_GAME) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_R1) ) // SELECT+R1 (next title)
			{
				led(GREEN, BLINK);
				mount_game((char*)"_next", 3);
				sys_timer_sleep(3);
				led(GREEN, ON);
			}
		}
		else
		if ((data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_L3) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_R2))
		{
			if(!(sm_config->combo & SHUT_DOWN) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_CROSS) ) // L3+R2+X (shutdown)
			{
				// power off
				working=0;
				// {system_call_4(379,0x1100,0,0,0);}
				vshmain_87BB0001(1);
				goto leave;
			}
			else if(!(sm_config->combo & RESTARTPS) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_CIRCLE) ) // L3+R2+O (restart)
			{
				// reboot
				working=0;
				// {system_call_4(379,0x1200,0,0,0);}
				// {system_call_3(379, 0x8201, NULL, 0);}
				vshmain_87BB0001(2);
				goto leave;
			}
			else if(!(sm_config->combo & UNLOAD_WM) && (data.button[CELL_PAD_BTN_OFFSET_DIGITAL1] & CELL_PAD_CTRL_R3)) // L3+R3+R2 (quit sMAN)
			{
				restore_fan(0);
				show_msg((char*)STR_WMUNL);
				goto leave;
			}
			else if((data.button[CELL_PAD_BTN_OFFSET_DIGITAL2] & CELL_PAD_CTRL_TRIANGLE) ) // L3+R2+/\ (enable/disable fancontrol)
			{
				if(sm_config->fanc) sm_config->fanc=0; else sm_config->fanc=1;
				max_temp=0;
				strcpy((char*)tmp, STR_FANCTRL3 " ");
				if(sm_config->fanc)
				{
					if(sm_config->temp0==0) max_temp=sm_config->temp1; else max_temp=0;
					fan_control(sm_config->temp0, 0);
					strcat((char*)tmp, STR_ENABLED);
				}
				else
				{
					restore_fan(0);
					strcat((char*)tmp, STR_DISABLED);
				}
				save_settings();
				show_msg((char*)tmp);
				sys_timer_sleep(2);
			}
		}
		else
			sys_timer_sleep(1);

no_pad:

		to++;
		if((to%32==0))
		{
			get_temperature(0, &t1);
			get_temperature(1, &t2);
			t1>>=24; t2>>=24;
			if(t1>83 || t2>83)
			{
				if(!sm_config->warn)
				{
					sprintf((char*)tmp, STR_OVERHEAT "\r\n CPU: %i°C   RSX: %i°C", t1, t2);
					show_msg((char*)tmp);
					sys_timer_sleep(2);
				}
				if(t1>85 || t2>85)
				{
					if(!max_temp) max_temp=82;
					if(fan_speed<0xB0)
						fan_speed=0xB0;
					else
						if(fan_speed<MAX_FANSPEED) fan_speed+=8;

					old_fan=fan_speed;
					fan_control(fan_speed, 0);
					show_msg((char*)STR_OVERHEAT2);
				}
			}
		}

		if(to==127) // check USB drives each 127 seconds
		{
			for(u8 f0=0; f0<8; f0++)
			{
				if(sys_storage_open(((f0<6)?USB_MASS_STORAGE_1(f0):USB_MASS_STORAGE_2(f0)), 0, &usb_handle, 0)==0)
				{
					sys_storage_read(usb_handle, 0, to, 1, tmp, &r, 0);
					sys_storage_close(usb_handle);
				}
			}
			to=0;
		}

		if(!(to&7) && wwwd_socket[0]==-1 && working)
		{
			printf(SMAN_LOG "port 80 listen() error, relisten and respawn!\r\n");
			int list_s = slisten(WWWPORT, HTTP_BACKLOG);

			if(list_s >= 0)
			{
				for(u8 s=1;s<16;s++)
					sys_net_abort_socket(wwwd_socket[s], SYS_NET_ABORT_STRICT_CHECK);
				sys_timer_sleep(1);
				wwwd_socket[0]=list_s;
				sys_ppu_thread_create(&thread_id_www, wwwd_thread, (u64)list_s, 1200, 0x1000, 0, THREAD_MAIN);
			}
		}
	}

leave:
	printf(SMAN_LOG "poll_thread [EXIT]\r\n");

	working=0;
	for(u8 s=0;s<16;s++)
	{
		sys_net_abort_socket(ftpd_socket[s], SYS_NET_ABORT_STRICT_CHECK);
		sys_net_abort_socket(wwwd_socket[s], SYS_NET_ABORT_STRICT_CHECK);
	}

	u64 exit_code;
	sys_ppu_thread_create(&exit_code, sm_stop_thread, 0, 0, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_STOP);
	sys_ppu_thread_join(exit_code, &exit_code);

	sys_timer_sleep(2);
	printf(SMAN_LOG "prx [EXIT]\r\n");
	finalize_module();
	_sys_ppu_thread_exit(0);
}

void restore_fan(u8 settemp)
{
	if(backup[0]==1)
	{

		if(sm_config->ps2temp<20) sm_config->ps2temp=20;
		sys_sm_set_fan_policy(0, 1, ((sm_config->ps2temp*255)/100));
		if(settemp==1) sys_sm_set_fan_policy(0, 2, ((sm_config->ps2temp*255)/100));
		if(settemp==2) sys_sm_set_fan_policy(0, 2, ((sm_config->manu*255)/100));

		if(!dex_mode)
		{
			if(c_firmware>=4.55f)
			{
				pokeq(0x800000000000A334ULL, backup[4]);
				pokeq(0x8000000000009E38ULL, backup[5]);
			}
			else
			{
				pokeq(0x800000000000A324ULL, backup[4]);
				pokeq(0x8000000000009E28ULL, backup[5]);
			}
		}
		else //DEX
		{
			if(c_firmware>=4.55f)
			{
				pokeq(0x800000000000A3B4ULL, backup[4]);  // sys 389 set_fan_policy
				pokeq(0x8000000000009EB8ULL, backup[5]);
			}
			else if(c_firmware>=4.21f && c_firmware<=4.53f)
			{
				pokeq(0x800000000000A3A4ULL, backup[4]);  // sys 389 set_fan_policy
				pokeq(0x8000000000009EA8ULL, backup[5]);
			}
		}
		backup[0]=0;
	}
}

void fan_control(u8 temp0, u8 initial)
{
	if(c_firmware>=4.21f)
	{
		if(!initial)
		{
			if(backup[0]==0)
			{
				backup[0]=1;
				if(!dex_mode)
				{
					if(c_firmware>=4.55f)
					{
						backup[4]=peekq(0x800000000000A334ULL);
						backup[5]=peekq(0x8000000000009E38ULL);
						lv2poke32(0x8000000000009E38ULL, 0x38600001); // sys 409 get_fan_policy
						lv2poke32(0x800000000000A334ULL, 0x38600001); // sys 389 set_fan_policy
					}
					else
					{
						backup[4]=peekq(0x800000000000A324ULL);
						backup[5]=peekq(0x8000000000009E28ULL);
						lv2poke32(0x8000000000009E28ULL, 0x38600001); // sys 409 get_fan_policy
						lv2poke32(0x800000000000A324ULL, 0x38600001); // sys 389 set_fan_policy
					}
				}
				else //DEX
				{
					if(c_firmware>=4.55f)
					{
							backup[4]=peekq(0x800000000000A3B4ULL);
							backup[5]=peekq(0x8000000000009EB8ULL);
							lv2poke32(0x8000000000009EB8ULL, 0x38600001);
							lv2poke32(0x800000000000A3B4ULL, 0x38600001); // sys 389 set_fan_policy
					}
					else if(c_firmware>=4.21f && c_firmware<=4.53f)
					{
							backup[4]=peekq(0x800000000000A3A4ULL);
							backup[5]=peekq(0x8000000000009EA8ULL);
							lv2poke32(0x8000000000009EA8ULL, 0x38600001);
							lv2poke32(0x800000000000A3A4ULL, 0x38600001); // sys 389 set_fan_policy
					}
				}

				sys_sm_set_fan_policy(0, 2, 0x33);
			}
		}

		if(temp0<0x33)
		{
			u8 st, mode, unknown;
			u8 fan_speed8=0;
			sys_sm_get_fan_policy(0, &st, &mode, &fan_speed8, &unknown);
			if(fan_speed8<0x33) return;
			fan_speed=fan_speed8;
		}
		else
			fan_speed=temp0;

		if(fan_speed<0x33 || fan_speed>0xFC)
		{
			fan_speed=0x48;
			sys_sm_set_fan_policy(0, 2, fan_speed);
			sys_timer_sleep(2);
		}
		old_fan=fan_speed;
		sys_sm_set_fan_policy(0, 2, fan_speed);
	}
}

void save_settings()
{
	int fdwm=0;
	if(cellFsOpen(SMAN_CNF, CELL_FS_O_CREAT|CELL_FS_O_WRONLY, &fdwm, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		cellFsWrite(fdwm, (void *)smconfig, sizeof(_smconfig), NULL);
		cellFsClose(fdwm);
	}
}

void reset_settings()
{
	memset(sm_config, 0, sizeof(_smconfig));

	sm_config->type=TYPE_ALL;
	sm_config->cur_game=0;
	sm_config->gpp=10;

	sm_config->autob=0;
	sm_config->refr=1;

	sm_config->combo=0xF1;

	sm_config->fanc=1;
	sm_config->temp1=MY_TEMP;
	sm_config->ps2temp=37;
	sm_config->minfan=22;
	sm_config->manu=37;
	sm_config->temp0=0;

	sm_config->netd0=0;
	sm_config->neth0[0]=0;
	sm_config->netp0=38008;

	sm_config->netd1=0;
	sm_config->neth1[0]=0;
	sm_config->netp1=38008;

	sm_config->xmbi=1;
	sm_config->resv=0;
	sm_config->ftpd=0;

	if(!read_file((char*)SMAN_CNF, (void *)smconfig, sizeof(_smconfig)))
		save_settings();

	if(sm_config->warn>1) sm_config->warn=0;
	if(sm_config->minfan<20) sm_config->minfan=22;
}

static u8 extract_resources(void)
{
	typedef struct
	{
		u32		offset;
		u32		size;
		char	name[16];
	}
	res_def;

	int fs, fd;
	struct CellFsStat s;
	char sman[128];
	sman[0]=0;

	if (cellFsOpen("/dev_hdd0/boot_plugins.txt", CELL_FS_O_RDONLY, &fd, 0, 0) == CELL_FS_SUCCEEDED)
	{
		while (working)
		{
			char path[128];
			int eof;

			if (read_text_line(fd, path, sizeof(path), &eof) > 0)
			{
				if(strstr(path, "/sman.sprx") && !sman[0] && cellFsStat(path, &s)==CELL_FS_SUCCEEDED)
				{
					strncpy(sman, path, 127);
					break;
				}
			}
			else break;

			if (eof) break;
		}
		cellFsClose(fd);
	}
	else return 0;

	cellFsChmod(sman, 0666);
	if(cellFsOpen(sman, CELL_FS_O_RDONLY, &fs, 0, 0)!=CELL_FS_SUCCEEDED) return 0;

	u64 msiz = 0;
	u8 res_files=0;

#define SPRX_PAD	160*1024			// offset of resource data from start of sman.sprx
#define RES_VER		(9)					// resource files version

	cellFsLseek(fs, SPRX_PAD, CELL_FS_SEEK_SET, &msiz);
	cellFsRead(fs, &res_files, 1, &msiz);
	if(res_files!=23)					// sanity check
		{cellFsClose(fs); return 0;}

	res_def res_data[res_files];		// offset, size and name array for resource files
	cellFsRead(fs, &res_data, sizeof(res_data), &msiz);

	char dest[128];
	u32 buf_size=0;

	for(int n=0; n<res_files; n++) if(res_data[n].size>buf_size) buf_size=res_data[n].size;

	u8* buf = (u8*) malloc(buf_size);	// largest resource file size used for buffer size

	if(buf)
	{
		for(int n=0; n<res_files; n++)
		{
			sprintf(dest, WMRES "/%s", res_data[n].name);
			if(/*!strstr(dest, "/www_") &&*/ sm_config->resv==RES_VER && file_exists(dest)) continue;

			cellFsOpen(dest, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0);
			cellFsLseek(fs, SPRX_PAD+res_data[n].offset, CELL_FS_SEEK_SET, &msiz);
			cellFsRead(fs, buf, res_data[n].size, &msiz);
			cellFsWrite(fd, buf, msiz, NULL);
			cellFsClose(fd);
			cellFsChmod(dest, 0666);
		}

		cellFsClose(fs);
		free(buf);
		if(sm_config->resv!=RES_VER)
		{
			sm_config->resv=RES_VER;
			save_settings();
		}
	}
	else {cellFsClose(fs); return 0;}

	return 1;
}

static void wwwd_thread(u64 list_ss)
{
	sys_ppu_thread_t id;
	int list_s=(int)list_ss;
	wwwd_socket[0]=list_s;

	while(http_threads<MAX_HTTP_THREADS)
	{
		http_threads++;
		printf(SMAN_LOG "CREATING WWW THREAD: %i\r\n", http_threads);
#ifdef DEBUG
		if(sys_ppu_thread_create(&id, handleclient_www, (u64)list_s|(((u64)http_threads<<16)), 1550, 0x3000, 0, THREAD_WWWC)!=CELL_OK)
#else
		if(sys_ppu_thread_create(&id, handleclient_www, (u64)list_s, 1550, 0x2000, 0, THREAD_WWWC)!=CELL_OK)
#endif
		{
			http_threads--;
			sys_timer_sleep(2);
		}
	}

	sys_ppu_thread_exit(0);
}

static void sm_init(u64 arg)
{
	if(sm_config->xmbi)
		content_scan(0xC0FEBABA); //create only fb.xml

	int list_s = extract_resources();

	sys_timer_sleep(sm_config->refr?12:25);	// do not interfere with the rest of the boot process

	if(!list_s) {show_msg(STR_SERROR); sys_ppu_thread_exit(0);}

	{sys_map_path((char*)"/dev_flash/vsh/module/" SPRX_PLUGIN ".sprx", (char*)SMAN_XMB);}

	if(sm_config->xmbi)
		show_msg((char*)"sMAN " WM_VERSION);
	else
		show_msg((char*)"sMAN " WM_VERSION "\r\n"STR_HOLDL2R2);

	init_running=1;

	led(YELLOW, BLINK);
	sys_ppu_thread_t id;
	sys_ppu_thread_create(&id, scan_thread, (u64)0xC0FEBABE, 920, 0x8000, 0, THREAD_SCAN);

relisten:

	list_s = slisten(WWWPORT, HTTP_BACKLOG);

	if(list_s<0 && working)
	{
		sys_timer_sleep(1);
		goto relisten;
	}

	if(list_s >= 0)
		sys_ppu_thread_create(&thread_id_www, wwwd_thread, (u64)list_s, 1200, 0x1000, 0, THREAD_MAIN);

	sys_ppu_thread_exit(0);
}

int sm_start(size_t args, void *argp)
{
	cellFsMkdir(PSTMP, CELL_FS_S_IFDIR | 0777);
	cellFsMkdir(WMTMP, CELL_FS_S_IFDIR | 0777);
	cellFsMkdir(WMRES, CELL_FS_S_IFDIR | 0777);

	u8 cconfig[15];
	CobraConfig *cobra_config = (CobraConfig*) cconfig;
	memset(cobra_config, 0, 15);
	cobra_read_config(cobra_config);

	if(cobra_config->ps2softemu==0 && cobra_get_ps2_emu_type()==PS2_EMU_SW)
	{
		cobra_config->ps2softemu=1;
		cobra_write_config(cobra_config);
	}

	backup[0]=0;

	u64 CEX=0x4345580000000000ULL;
	u64 DEX=0x4445580000000000ULL;

	dex_mode=0;
	c_firmware=0.00f;

			if(peekq(0x80000000002ED808ULL)==CEX) {c_firmware=4.80f;}
	else	if(peekq(0x80000000002ED818ULL)==CEX) {c_firmware=4.75f;}
	else	if(peekq(0x80000000002ED778ULL)==CEX) {c_firmware=4.70f;}
	else	if(peekq(0x80000000002ED860ULL)==CEX) {c_firmware=4.65f;}
	else	if(peekq(0x80000000002ED850ULL)==CEX) {c_firmware=4.60f;}
	else	if(peekq(0x80000000002EC5E0ULL)==CEX) {c_firmware=4.55f;}
	else	if(peekq(0x80000000002E9D70ULL)==CEX) {c_firmware=4.53f;}
	else	if(peekq(0x80000000002E9BE0ULL)==CEX) {c_firmware=4.50f;}
	else	if(peekq(0x80000000002EA9B8ULL)==CEX) {c_firmware=4.46f;}
	else	if(peekq(0x80000000002D83D0ULL)==CEX) {c_firmware=3.55f;}

	else	if(peekq(0x800000000030F3B0ULL)==DEX) {c_firmware=4.81f; dex_mode=2;}
	else	if(peekq(0x800000000030F2D0ULL)==DEX) {c_firmware=4.75f; dex_mode=2;}
	else	if(peekq(0x800000000030F240ULL)==DEX) {c_firmware=4.70f; dex_mode=2;}
	else	if(peekq(0x800000000030F1A8ULL)==DEX) {c_firmware=4.65f; dex_mode=2;}
	else	if(peekq(0x800000000030D6A8ULL)==DEX) {c_firmware=4.55f; dex_mode=2;}
	else	if(peekq(0x800000000030AEA8ULL)==DEX) {c_firmware=4.53f; dex_mode=2;}
	else	if(peekq(0x8000000000309698ULL)==DEX) {c_firmware=4.50f; dex_mode=2;}
	else	if(peekq(0x8000000000305410ULL)==DEX) {c_firmware=4.46f; dex_mode=2;}

	if(!dex_mode) patches(c_firmware);

	sm_config = (_smconfig*) smconfig;
	reset_settings();

	if(!sm_config->flsh) {system_call_8(837, (u64)(char*)"CELL_FS_IOS:BUILTIN_FLSH1", (u64)(char*)"CELL_FS_FAT", (u64)(char*)"/dev_blind", 0, 0, 0, 0, 0);}
	if(!sm_config->ftpd) sys_ppu_thread_create(&thread_id_ftp,  ftpd_thread, NULL, 1150, 0x1000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_FTPD);

	if(sm_config->fanc)
	{
		if(sm_config->temp0==0) max_temp=sm_config->temp1; else max_temp=0;
		fan_control(sm_config->temp0, 0);
	}

	if(!sm_config->xmbi)
		cellFsUnlink("/dev_hdd0/xmlhost/game_plugin/fb.xml");
	else
	{
		cellFsMkdir("/dev_hdd0/xmlhost", CELL_FS_S_IFDIR | 0777);
		cellFsMkdir("/dev_hdd0/xmlhost/game_plugin", CELL_FS_S_IFDIR | 0777);
		cellFsUnlink(SMAN_XML);
	}

	sys_ppu_thread_create(&thread_id_poll, poll_thread, NULL, 890, 0x2000, SYS_PPU_THREAD_CREATE_JOINABLE, THREAD_POLL);

	sys_ppu_thread_create(&thread_id_www, sm_init, NULL, 1200, 0x2000, 0, THREAD_INIT);

	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}

static void sm_stop_thread(u64 arg)
{
	if(!fan_reset) restore_fan(0);

	u64 exit_code;

	if (thread_id_gui != (sys_ppu_thread_t)-1)
		sys_ppu_thread_join(thread_id_gui, &exit_code);

	if (thread_id_ftp != (sys_ppu_thread_t)-1)
		sys_ppu_thread_join(thread_id_ftp, &exit_code);

//	if (thread_id_poll != (sys_ppu_thread_t)-1)
//		sys_ppu_thread_join(thread_id_poll, &exit_code);

	sys_ppu_thread_exit(0);
}

static void finalize_module(void)
{
	u64 meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address(finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (u64)(u32)meminfo);
}

static int do_umount_iso()
{
	unsigned int real_disctype, effective_disctype, iso_disctype;

	cobra_unload_vsh_plugin(0);
	sys_timer_usleep(10000);
	cobra_unset_psp_umd();
	sys_timer_usleep(10000);
	cobra_get_disc_type(&real_disctype, &effective_disctype, &iso_disctype);

	// If there is an effective disc in the system, it must be ejected
	if (effective_disctype != DISC_TYPE_NONE)
	{
		cobra_send_fake_disc_eject_event();
		sys_timer_usleep(4000);
	}

	if (iso_disctype != DISC_TYPE_NONE)
		cobra_umount_disc_image();

	{sys_map_path((char*)"/dev_bdvd", NULL);}
	{sys_map_path((char*)"//dev_bdvd", NULL);}
	{sys_map_path((char*)"/app_home", NULL);}

	// If there is a real disc in the system, issue an insert event
	if (real_disctype != DISC_TYPE_NONE)
	{
		struct CellFsStat bufc;
		cobra_send_fake_disc_insert_event();
		for(u8 m=0; m<22; m++)
		{
			sys_timer_usleep(4000);

			if(cellFsStat("/dev_bdvd", &bufc)==CELL_FS_SUCCEEDED) break;
		}
		cobra_disc_auth();
	}
	return 0;
}

// action:
// 0 - load game
// 1 -   and add it to last_game list
// 3 -   and change xmb column (and dev focus unless discboot is enabled)
static void mount_game(const char *_path0, u8 action)
{
	explore_plugin_if *explore_if=0;

	int discboot=0xff;
	xsetting_0AF1F161()->GetSystemDiscBootFirstEnabled(&discboot);

	if(action==3 && gui_allowed(0))
	{
		explore_if = (explore_plugin_if *)paf_23AFB290((u32)paf_F21655F3((char*)"explore_plugin"), 1);
		if(explore_if)
		{
			explore_if->exec_cmd((char*)"close_all_list", 0, 0);
			if(strstr(_path0, "BDISO") || strstr(_path0, "DVDISO"))
				explore_if->exec_cmd((char*)"focus_category video", 0, 0);
			else
				explore_if->exec_cmd((char*)"focus_category game", 0, 0);
		}
		if(discboot) sys_timer_sleep(1);
	}

	#define MAX_LAST_GAMES (5)
	typedef struct
	{
		u8 last;
		char game[MAX_LAST_GAMES][204];
	} __attribute__((packed)) _lastgames;

	char* addr				= (char*)malloc(11264);	if(!addr) return; // 11KB

	char* tempstr			= addr;				// 4096
	char* tmp_iso			= addr+4096;		// 4096
	char* _path				= addr+8192;		//  512
	char* tmp_str			= addr+8704;		//  512
	char* path2				= addr+9216;		//  512
	_lastgames* lastgames	= (_lastgames*)(addr+9728);		// 1021
								//+10749;		//
	strcpy(_path, _path0);

	if(action)
	{
		int fd=0;

		memset(lastgames, 0, sizeof(_lastgames));
		lastgames->last=250;
		read_file((char*)LASTGAMES, (void*)lastgames, sizeof(_lastgames));

		if(strstr(_path0, "_prev") || strstr(_path0, "_next"))
		{
			if(lastgames->last>(MAX_LAST_GAMES-1)) return;
			if(strstr(_path0, "_prev"))
			{
				if(lastgames->last==0) lastgames->last=(MAX_LAST_GAMES-1); else lastgames->last--;
			}

			if(strstr(_path0, "_next"))
			{
				if(lastgames->last==(MAX_LAST_GAMES-1)) lastgames->last=0; else lastgames->last++;
			}
			if(lastgames->game[lastgames->last][0]!='/') lastgames->last=0;
			if(lastgames->game[lastgames->last][0]!='/' || strlen(lastgames->game[lastgames->last])<7) return;
			strcpy(_path, lastgames->game[lastgames->last]);
		}
		else
		{
			if(lastgames->last==250)
			{
				lastgames->last=0;
				strcpy(lastgames->game[lastgames->last], _path);
			}
			else
			{
				u8 found=0;
				for(u8 n=0;n<MAX_LAST_GAMES;n++)
				{
					if(!strcmp(lastgames->game[n], _path)) {found=1; break;}
				}
				if(!found)
				{
					lastgames->last++;
					if(lastgames->last>(MAX_LAST_GAMES-1)) lastgames->last=0;
					strcpy(lastgames->game[lastgames->last], _path);
				}
			}
		}

		if(cellFsOpen((char*)LASTGAMES, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
		{
			cellFsWrite(fd, (void *)lastgames, sizeof(_lastgames), NULL);
			cellFsClose(fd);
			cellFsChmod((char*)LASTGAMES, 0666);
		}

		if(_path[0]=='_' || strrchr(_path, '/')==NULL) return;


		sprintf(path2, "%s", (strrchr(_path, '/')+1));
		if(strstr(path2, ".ntfs[")) path2[strrchr(path2, '.')-path2]=0;
		if(strrchr(path2, '.')!=NULL) path2[strrchr(path2, '.')-path2]=0;

		find_name_icon(_path, path2, NULL);

		strcat(path2, "  ");
		show_msg(path2);

		if(cellFsOpen(LASTGAMETXT, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
		{
			cellFsWrite(fd, (void *)_path, strlen(_path), NULL);
			cellFsClose(fd);
		}
	}

	cobra_unload_vsh_plugin(0);
	sys_timer_usleep(10000);

	do_umount_iso();
	sys_timer_usleep(4000);
	cobra_send_fake_disc_eject_event();
	sys_timer_usleep(4000);

	if(strstr(_path, "/PS3ISO/") || strstr(_path, "/BDISO/") || strstr(_path, "/DVDISO/") || strstr(_path, "/PS2ISO/") || strstr(_path, "/PSPISO/") || strstr(_path, "/ISO/") || strstr(_path, "/PSXISO/") || strstr(_path, "/PSXGAMES/")
		|| strstr(_path, "/net0") || strstr(_path, "/net1") || strstr(_path, ".ntfs["))
	{
		if(strstr(_path0, "_prev") || strstr(_path0, "_next"))
			sys_timer_sleep(1);
		else
			sys_timer_usleep(50000);

		struct CellFsStat s;

		char *cobra_iso_list[15];
		u8 iso_num;
		for(iso_num=0;iso_num<15;iso_num++) cobra_iso_list[iso_num]=NULL;
		iso_num=1;

		cobra_iso_list[0] = (char*)malloc(strlen(_path)+1);
		strcpy(cobra_iso_list[0], _path);

		if(strstr(_path, "iso.0") || strstr(_path, "ISO.0"))
		{
			sprintf(path2, "%s", _path);
			path2[strlen(path2)-2]=0;
			for(u8 n=1;n<15;n++)
			{
				sprintf(tmp_iso, "%s.%i", path2, n);
				if(cellFsStat(tmp_iso, &s)==CELL_FS_SUCCEEDED)
				{
					iso_num++;
					cobra_iso_list[n] = (char*)malloc(strlen(tmp_iso)+1);
					strcpy(cobra_iso_list[n], tmp_iso);
				}
				else
					break;
			}
		}

		if(strstr(_path, ".ntfs["))
		{
			u8* sprx_data=(u8*)malloc(65536);
			if(sprx_data)
			{
				u64 msiz = read_file(_path, sprx_data, 65536);
				if(msiz)
				{
					sys_timer_usleep(10000);
					cobra_load_vsh_plugin(0, (char*)SPRX_NTFS, sprx_data, msiz);
				}
				free(sprx_data);
			}

			if(strstr(_path, ".ntfs[PS3ISO]"))
			{
				strcpy(tmp_str, _path);
				tmp_str[strlen(tmp_str)-17]=0;
				strcat(tmp_str, ".SFO");
				if(cellFsStat(tmp_str, &s)!=CELL_FS_SUCCEEDED)
				{
					for(u8 n=0;n<30;n++)
					{
						sys_timer_usleep(300000);
						if(cellFsStat("/dev_bdvd/PS3_GAME/PARAM.SFO", &s)==CELL_FS_SUCCEEDED)
						{
							copy_file("/dev_bdvd/PS3_GAME/PARAM.SFO", tmp_str);
							tmp_str[strlen(tmp_str)-4]=0; strcat(tmp_str, ".PNG");
							copy_file("/dev_bdvd/PS3_GAME/ICON0.PNG", tmp_str);
							break;
						}
					}
				}
			}
			for(iso_num=0;iso_num<15;iso_num++) if(cobra_iso_list[iso_num]) {free(cobra_iso_list[iso_num]);cobra_iso_list[iso_num]=NULL;}
			goto patch;
		}

		if(strstr(_path, "/net0") || strstr(_path, "/net1"))
		{
			u8* addr=(u8*)malloc(65536);
			if(addr)
			{
				netiso_args *mynet_iso	= (netiso_args*)addr;
				memset(mynet_iso, 0, 65536);

				if( (strstr(_path, "/net0") && sm_config->netd0 && sm_config->neth0[0] && sm_config->netp0>0)
					|| (strstr(_path, "/net1") && sm_config->netd1 && sm_config->neth1[0] && sm_config->netp1>0) )
				{
					if(strstr(_path, "/net1"))
					{
						strcpy(mynet_iso->server, sm_config->neth1);
						mynet_iso->port=sm_config->netp1;
					}

					else
					{
						strcpy(mynet_iso->server, sm_config->neth0);
						mynet_iso->port=sm_config->netp0;
					}
				}
				else
				{
					free(addr);
					return;
				}

				strcpy(mynet_iso->path, _path+5);
				if(strstr(_path, "/PS3ISO/")) mynet_iso->emu_mode=EMU_PS3;
				else if(strstr(_path, "/BDISO/")) mynet_iso->emu_mode=EMU_BD;
				else if(strstr(_path, "/DVDISO/")) mynet_iso->emu_mode=EMU_DVD;
				else if(strstr(_path, "/PSX"))
				{
					TrackDef tracks[32];
					tracks[0].lba = 0;
					tracks[0].is_audio = 0;
					unsigned int num_tracks=0;
					int abort_connection=0;

					strcpy(mynet_iso->path, _path+5);
					mynet_iso->path[strlen(mynet_iso->path)-3]=0; strcat(mynet_iso->path, "CUE");

					int ns=connect_to_server(mynet_iso->server, mynet_iso->port);
					if(ns>=0)
					{
						if(open_remote_file_2(ns, mynet_iso->path, &abort_connection)<1)
						{
							mynet_iso->path[strlen(mynet_iso->path)-3]=0; strcat(mynet_iso->path, "cue");
							if(open_remote_file_2(ns, mynet_iso->path, &abort_connection)<1) goto cancel_net;
						}

						u8* cue_buf=(u8*)malloc(4096);
						if(cue_buf)
						{
							u64 msiz = read_remote_file(ns, cue_buf, 0, 4096, &abort_connection);
							open_remote_file_2(ns, (char*)"/CLOSEFILE", &abort_connection);
							num_tracks=parse_cue(cue_buf, msiz, tracks);
							free(cue_buf);
						}
					}

cancel_net:
					if(ns>=0) {shutdown(ns, SHUT_RDWR); socketclose(ns);}

					if(!num_tracks) num_tracks++;

					strcpy(mynet_iso->path, _path+5);
					mynet_iso->emu_mode=EMU_PSX;
					mynet_iso->numtracks=num_tracks;

					ScsiTrackDescriptor *scsi_tracks;
					scsi_tracks = (ScsiTrackDescriptor *)&mynet_iso->tracks[0];

					if (num_tracks==1)
					{
						scsi_tracks[0].adr_control = 0x14;
						scsi_tracks[0].track_number = 1;
						scsi_tracks[0].track_start_addr = 0;
					}
					else
					{
						for (u8 j = 0; j < num_tracks; j++)
						{
							scsi_tracks[j].adr_control = (tracks[j].is_audio) ? 0x10 : 0x14;
							scsi_tracks[j].track_number = j+1;
							scsi_tracks[j].track_start_addr = tracks[j].lba;
						}
					}
				}
				else if(strstr(_path, "/GAMES/") || strstr(_path, "/GAMEZ/"))
				{
					mynet_iso->emu_mode=EMU_PS3;
					sprintf(mynet_iso->path, "/***PS3***%s", _path+5);
				}
				else
				{
					mynet_iso->emu_mode=EMU_DVD;
					sprintf(mynet_iso->path, "/***DVD***%s", _path+5);
				}

				sys_timer_usleep(10000);
				cobra_load_vsh_plugin(0, (char*)SPRX_NET, addr, 65536);
				free(addr);

				if(mynet_iso->emu_mode==EMU_PS3)
				{
					sprintf(tmp_str, WMTMP "/%s", (strrchr(_path, '/')+1));
					if(!strstr(mynet_iso->path, "/***PS3***")) tmp_str[strlen(tmp_str)-4]=0;
					strcat(tmp_str, ".SFO");
					if(cellFsStat(tmp_str, &s)!=CELL_FS_SUCCEEDED)
					{
						for(u8 n=0;n<30;n++)
						{
							sys_timer_usleep(300000);
							if(cellFsStat("/dev_bdvd/PS3_GAME/PARAM.SFO", &s)==CELL_FS_SUCCEEDED)
							{
								copy_file("/dev_bdvd/PS3_GAME/PARAM.SFO", tmp_str);
								tmp_str[strlen(tmp_str)-4]=0; strcat(tmp_str, ".PNG");
								copy_file("/dev_bdvd/PS3_GAME/ICON0.PNG", tmp_str);
								break;
							}
						}
					}
				}
			}
			for(iso_num=0;iso_num<15;iso_num++) if(cobra_iso_list[iso_num]) {free(cobra_iso_list[iso_num]);cobra_iso_list[iso_num]=NULL;}
			goto patch;
		}
		else
		{
			if(strstr(_path, "/PS3ISO/"))
			{
				cobra_mount_ps3_disc_image(cobra_iso_list, iso_num);
				sys_map_path("/app_home", NULL);
				sys_timer_usleep(2500);
				cobra_send_fake_disc_insert_event();

				sprintf(tmp_str, WMTMP "/%s", (strrchr(_path, '/')+1));
				if(tmp_str[strlen(tmp_str)-2]=='.') tmp_str[strlen(tmp_str)-2]=0;
				if(tmp_str[strlen(tmp_str)-4]=='.') tmp_str[strlen(tmp_str)-4]=0;
				strcat(tmp_str, ".SFO");
				if(cellFsStat(tmp_str, &s)!=CELL_FS_SUCCEEDED)
				{
					for(u8 n=0;n<10;n++)
					{
						sys_timer_usleep(300000);
						if(cellFsStat("/dev_bdvd/PS3_GAME/PARAM.SFO", &s)==CELL_FS_SUCCEEDED)
						{
							copy_file("/dev_bdvd/PS3_GAME/PARAM.SFO", tmp_str);
							tmp_str[strlen(tmp_str)-4]=0; strcat(tmp_str, ".PNG");
							copy_file("/dev_bdvd/PS3_GAME/ICON0.PNG", tmp_str);
							break;
						}
					}
				}

				for(iso_num=0;iso_num<15;iso_num++) if(cobra_iso_list[iso_num]) {free(cobra_iso_list[iso_num]);cobra_iso_list[iso_num]=NULL;}
				goto patch;

			}
			else if(strstr(_path, "/PSPISO/") || strstr(_path, "/ISO/"))
			{
				cellFsUnlink((char*)"/dev_hdd0/game/PSPC66820/PIC1.PNG");
				cobra_unset_psp_umd();
				int result=cobra_set_psp_umd2(_path, NULL, (char*)"/dev_hdd0/tmp/psp_icon.png", 2); //EMU_400
				if(result==ENOTSUP || result==EABORT)
					return;
				else if(!result)
				{
					cobra_send_fake_disc_insert_event();
					goto patch;
				}
			}
			else if(strstr(_path, "/BDISO/"))
				cobra_mount_bd_disc_image(cobra_iso_list, iso_num);
			else if(strstr(_path, "/DVDISO/"))
				cobra_mount_dvd_disc_image(cobra_iso_list, iso_num);
			else if(strstr(_path, "/PS2ISO/") && (strstr(_path, ".ISO") || strstr(_path, ".iso")))
			{
				TrackDef tracks[1];
				tracks[0].lba = 0;
				tracks[0].is_audio = 0;
				cobra_mount_ps2_disc_image(cobra_iso_list, 1, tracks, 1);
				if(sm_config->fanc) fan_control( ((sm_config->ps2temp*255)/100), 0);
			}
			else if(strstr(_path, "/PSXISO/") || strstr(_path, "/PSXGAMES/") || strstr(_path, "/PS2ISO/"))
			{
				if(strstr(_path, ".BIN") || strstr(_path, ".bin") || strstr(_path, ".IMG") || strstr(_path, ".img"))
				{
					_path[strlen(_path)-3]=0; strcat(_path, "CUE");

					struct CellFsStat s;
					if(cellFsStat(_path, &s)!=CELL_FS_SUCCEEDED)
					{
						_path[strlen(_path)-3]=0; strcat(_path, "cue");
					}
					if(cellFsStat(_path, &s)!=CELL_FS_SUCCEEDED)
					{
						TrackDef tracks[1];
						tracks[0].lba = 0;
						tracks[0].is_audio = 0;
						if(strstr(_path, "/PS2ISO/"))
						{
							cobra_mount_ps2_disc_image(cobra_iso_list, 1, tracks, 1);
							if(sm_config->fanc) fan_control( ((sm_config->ps2temp*255)/100), 0);
						}
						else
							cobra_mount_psx_disc_image_iso(cobra_iso_list[0], tracks, 1);
					}
					else
					{
						unsigned int num_tracks=0;
						u64 msiz = 0;

						u8* buf1=(u8*)malloc(4096);
						if(buf1)
						{
							msiz = read_file(_path, (void*)buf1, 4000);
							if(msiz)
							{
								TrackDef tracks[32];
								tracks[0].lba = 0;
								tracks[0].is_audio = 0;

								num_tracks=parse_cue((u8*)buf1, msiz, tracks);
								if(strstr(_path, "/PS2ISO/"))
								{
									cobra_mount_ps2_disc_image(cobra_iso_list, 1, tracks, num_tracks);
									if(sm_config->fanc) fan_control( ((sm_config->ps2temp*255)/100), 0);
								}
								else
									cobra_mount_psx_disc_image(cobra_iso_list[0], tracks, num_tracks);
							}
							else
							{
								TrackDef tracks[1];
								tracks[0].lba = 0;
								tracks[0].is_audio = 0;
								if(strstr(_path, "/PS2ISO/"))
								{
									cobra_mount_ps2_disc_image(cobra_iso_list, 1, tracks, 1);
									if(sm_config->fanc) fan_control( ((sm_config->ps2temp*255)/100), 0);
								}
								else
									cobra_mount_psx_disc_image_iso(cobra_iso_list[0], tracks, 1);
							}

							free(buf1);
						}
					}
				}
				else
				{
					TrackDef tracks[1];
					tracks[0].lba = 0;
					tracks[0].is_audio = 0;
					if(strstr(_path, "/PS2ISO/"))
					{
						cobra_mount_ps2_disc_image(cobra_iso_list, 1, tracks, 1);
						if(sm_config->fanc) fan_control( ((sm_config->ps2temp*255)/100), 0);
					}
					else
						cobra_mount_psx_disc_image_iso(cobra_iso_list[0], tracks, 1);
				}
			}

			sys_timer_usleep(2500);
			cobra_send_fake_disc_insert_event();
			sys_timer_usleep(150000);

		}
		for(iso_num=0;iso_num<15;iso_num++) if(cobra_iso_list[iso_num]) {free(cobra_iso_list[iso_num]);cobra_iso_list[iso_num]=NULL;}
	}
	else
	{
		char tempID[16];

		u64 msiz = 0;
		int indx=0;
		struct CellFsStat s;

		if(strstr(_path, "/dev_usb") && cellFsStat(_path, &s)==CELL_FS_SUCCEEDED)
		{
			for(u8 f0=0; f0<128; f0++) sys_storage_ext_fake_storage_event(4, 0, USB_MASS_STORAGE(f0));
			for(u8 f0=0; f0<128; f0++) sys_storage_ext_fake_storage_event(8, 0, USB_MASS_STORAGE(f0));

			sys_timer_sleep(1);

			indx=(_path[10]-0x30)+(_path[9]-0x30)*10+(_path[8]-0x30)*100;

			sys_storage_ext_fake_storage_event(7, 0, USB_MASS_STORAGE(indx));
			sys_storage_ext_fake_storage_event(3, 0, USB_MASS_STORAGE(indx));

			sys_timer_sleep(3);

			for(u8 f0=0; f0<128; f0++)
			{
				if(f0!=indx)
				{
					sys_storage_ext_fake_storage_event(7, 0, USB_MASS_STORAGE(f0));
					sys_storage_ext_fake_storage_event(3, 0, USB_MASS_STORAGE(f0));
				}
			}

			sprintf(tempstr, "Setting primary USB HDD:\r\n     /dev_usb%03i", indx);
			show_msg(tempstr);
		}

		memset(tempID, 0, 16);

		sprintf(tempstr, "%s/PS3_GAME/PARAM.SFO", _path);
		msiz = read_file(tempstr, (void*)tempstr, 4096);
		if(msiz>256)
			param_sfo_info((u8*)tempstr, msiz, NULL, tempID);

		strcpy(tempstr, _path);

		int special_mode=0;
		if(tempID[0] && tempID[8]>0x2f)
			cobra_map_game(tempstr, tempID, &special_mode);
		else
			cobra_map_game(tempstr, (char*)"TEST00000", &special_mode);
	}

patch:

	if(addr) free(addr);

	if(action==3 && !sm_config->focus && !discboot && gui_allowed(0))
	{
		sys_timer_usleep(3500000);
		explore_if = (explore_plugin_if *)paf_23AFB290((u32)paf_F21655F3((char*)"explore_plugin"), 1);
		if(explore_if)
		{
			if(strstr(_path0, "BDISO"))
				explore_if->exec_cmd((char*)"focus_segment_index seg_bdmav_device", 0, 0);
			else
			if(strstr(_path0, "DVDISO"))
				explore_if->exec_cmd((char*)"focus_segment_index seg_dvdv_device", 0, 0);
			else
				explore_if->exec_cmd((char*)"focus_segment_index seg_device", 0, 0);
		}
	}
}
