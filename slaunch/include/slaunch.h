#ifndef _SLAUNCH_H
#define _SLAUNCH_H

#ifdef __cplusplus
extern "C" {
#endif

#define PSTMP				"/dev_hdd0/tmp"
#define WMTMP				PSTMP "/wmtmp"
#define WMRES				WMTMP "/res"


#define SMAN_BIN			WMTMP "/sman_ntfs.bin"
#define SMAN_CNF			WMTMP "/sman_ntfs.cnf"
#define SMAN_XML			WMTMP "/sman.xml"
#define LASTGAMES			WMTMP "/sman.lgt"
#define LASTGAMETXT			WMTMP "/last_game.txt"

#define SPRX_NTFS			WMRES "/sman.ntf"	// rawseciso
#define SPRX_NET			WMRES "/sman.net"	// netiso
#define SMAN_XMB			WMRES "/sman.xmb"	// wmproxy

#define MAX_GAMES 2000

#define TYPE_ALL (0)
#define TYPE_PS1 (1)
#define TYPE_PS2 (2)
#define TYPE_PS3 (3)
#define TYPE_PSP (4)
#define TYPE_VID (5)
#define TYPE_MAX (6)

void slaunch_thread(uint64_t arg);

typedef struct	// 524b/title = 1MB for 2000+1 titles
{
	uint8_t type;
	char id[10];
	char name[141]; // 128+13 for added ' [BXXX12345]'
	char icon[160];
	char path[160];
	char padd[52];
} __attribute__((packed)) _slaunch;

typedef struct
{

	uint8_t type;
	uint32_t cur_game;

	uint8_t autob;
	uint8_t refr;

	uint8_t nopad;
	uint16_t combo;

	uint8_t fanc;
	uint8_t temp1;
	uint8_t ps2temp;
	uint8_t minfan;
	uint8_t manu;
	uint8_t temp0;
	uint8_t warn;

	uint8_t netd0;
	char neth0[16];
	uint32_t netp0;

	uint8_t netd1;
	char neth1[16];
	uint32_t netp1;

	uint8_t gpp;
	uint8_t xmbi;
	uint8_t resv;
	uint8_t ftpd;
	uint8_t flsh;

	uint8_t sfo;
	uint8_t focus;


} __attribute__((packed)) _smconfig;

void reset_settings(void);
void save_settings(void);
uint64_t file_exists(const char* path);

void show_msg(const char* msg);
int send_wm_request(const char *cmd);
void sclose(int *socket_e);

#define show_msg(msg) vshtask_A02D46E7(0, msg)

#ifdef __cplusplus
}
#endif

#endif /* _SLAUNCH_H */
