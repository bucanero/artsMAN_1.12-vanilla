#include <arpa/inet.h>

#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/process.h>
#include <sys/event.h>
#include <sys/syscall.h>
#include <sys/memory.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/sys_time.h>
#include <sys/timer.h>
#include <cell/pad.h>
#include <cell/cell_fs.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

#include "include/vsh_exports.h"

#include "include/misc.h"
#include "include/mem.h"
#include "include/blitting.h"
#include "include/slaunch.h"
#include "../misc/gui.h"

struct timeval {
	int64_t tv_sec;			/* seconds */
	int64_t tv_usec;		/* and microseconds */
};

_slaunch *slaunch = NULL;

char game_type[TYPE_MAX][8]=
{
	"\0",
	"PS1 ",
	"PS2 ",
	"PS3 ",
	"PSP ",
	"Video "
};

// externals
extern uint8_t working;
extern uint8_t smconfig[];
extern _smconfig *sm_config;
extern uint32_t	cur_game;
extern uint8_t	type;
extern uint8_t slaunch_running;

// globals
uint32_t	_cur_game=0;
uint32_t	oldpad=0, curpad=0;
uint32_t	init_delay=0;
uint32_t	games=0;
uint32_t	gpp=10;

uint32_t	disp_w=0;
uint32_t	disp_h=0;

uint8_t		key_repeat=0, can_skip=0;

uint64_t	tick=0x80;

CellPadData pdata;

static void return_to_xmb(void);

static int load_plugin_by_id(int id, void *handler)
{
	xmm0_interface = (xmb_plugin_xmm0 *)paf_23AFB290((uint32_t)paf_F21655F3("xmb_plugin"), 0x584D4D30);
	return xmm0_interface->LoadPlugin3(id, handler, 0);
}

static void web_browser(void)
{
	webbrowser_interface = (webbrowser_plugin_interface *)paf_23AFB290((uint32_t)paf_F21655F3("webbrowser_plugin"), 1);
	if(webbrowser_interface) webbrowser_interface->PluginWakeupWithUrl("http://127.0.0.1/setup.ps3");
}

static void web_browser2(void)
{
	webbrowser_interface = (webbrowser_plugin_interface *)paf_23AFB290((uint32_t)paf_F21655F3("webbrowser_plugin"), 1);
	if(webbrowser_interface) webbrowser_interface->PluginWakeupWithUrl("http://127.0.0.1/refresh.ps3");
}
/*
static int unload_plugin_by_id(int id, void *handler)
{
	xmm0_interface = (xmb_plugin_xmm0 *)paf_23AFB290((uint32_t)paf_F21655F3("xmb_plugin"), 0x584D4D30);//'XMM0'
	if(xmm0_interface) return xmm0_interface->Shutdown(id, handler, 1); else return 0;
}

static void web_browser_stop(void)
{
	webbrowser_interface = (webbrowser_plugin_interface *)paf_23AFB290((uint32_t)paf_F21655F3("webbrowser_plugin"), 1);
	if(webbrowser_interface) webbrowser_interface->Shutdown();
}
*/
static int connect_to_webman(void)
{
	struct sockaddr_in sin;
	int s;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0x7F000001; //127.0.0.1 (localhost)
	sin.sin_port = htons(80);         //http port (80)
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -1;

	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		return -1;

	return s;
}

int send_wm_request(const char *cmd)
{
	// send command
	int conn_s = -1;
	conn_s = connect_to_webman();

	if(conn_s >= 0)
	{
		struct timeval tv;
		tv.tv_usec = 0;
		tv.tv_sec = 10;
		setsockopt(conn_s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		int pa=0;
		char proxy_action[512];

		proxy_action[pa++] = 'G';
		proxy_action[pa++] = 'E';
		proxy_action[pa++] = 'T';
		proxy_action[pa++] = ' ';

		for(uint16_t i=0;(i < strlen(cmd)) && (pa < 500); i++)
		{
			if(cmd[i] != 0x20)
				proxy_action[pa++] = cmd[i];
			else
			{
				proxy_action[pa++] = '%';
				proxy_action[pa++] = '2';
				proxy_action[pa++] = '0';
			}
		}

		proxy_action[pa++] = '\r';
		proxy_action[pa++] = '\n';
		proxy_action[pa] = 0;
		send(conn_s, proxy_action, pa, 0);
		sclose(&conn_s);
	}
	return conn_s;
}

static void pad_read(void)
{
		// check only pad ports 0 and 1
		for(int32_t port=0; port<2; port++)
			{MyPadGetData(port, &pdata); curpad = (pdata.button[2] | (pdata.button[3] << 8)); if(curpad && (pdata.len > 0)) break;}  // use MyPadGetData() during VSH menu
}

uint64_t file_exists(const char* path)
{
	struct CellFsStat s;
	s.st_size=0;
	cellFsStat(path, &s);
	return(s.st_size);
}

static void load_background(void)
{
	load_img_bitmap(0, WMRES "/sman.jpg");
	// /dev_flash/vsh/resource/explore/icon/cinfo-bg-storegame.jpg
}

static void draw_selection(uint32_t game_idx)
{
		uint8_t slot = 1 + game_idx % gpp;
		char one_of[32];

		if(disp_w==1920)	set_font(32.f, 32.f, 1.0f, 1);
		else		set_font(32.f, 32.f, 3.0f, 1);
		ctx.fg_color=0xffc0c0c0;
		print_text(ctx.menu, CANVAS_W, 0, 0, slaunch[game_idx].name );

		if(disp_w==1920)	set_font(24.f, 16.f, 1.0f, 1);
		else		set_font(32.f, 16.f, 2.5f, 1);
		ctx.fg_color=0xff808080;
		print_text(ctx.menu, CANVAS_W, 0, 40, slaunch[game_idx].path+10 );

		if(disp_w==1920)	set_font(20.f, 20.f, 1.0f, 1);
		else		set_font(32.f, 20.f, 2.5f, 1);
		ctx.fg_color=0xffA0A0A0;
		sprintf(one_of, "%s%i / %i", game_type[type], game_idx+1, games);
		print_text(ctx.menu, CANVAS_W, 0, 64, one_of );

		set_texture_direct(ctx.menu, 0, 900, CANVAS_W, 96);
		memcpy((uint8_t *)ctx.menu, (uint8_t *)(ctx.canvas)+900*CANVAS_W*4, CANVAS_W*96*4);
		set_frame(slot, 0xffc00000ffc00000);
}

static void draw_page(uint32_t game_idx, uint8_t key_repeat)
{
	if(!games || game_idx>=games) return;
	uint8_t slot = 0;
	uint32_t i, j;
	int px=56, py=90;	// top-left

	// draw background and menu strip
	flip_frame((uint64_t*)ctx.canvas);
	memcpy((uint8_t *)ctx.menu, (uint8_t *)(ctx.canvas)+900*CANVAS_W*4, CANVAS_W*96*4);
	set_textbox(0xffa0a0a0ffa0a0a0, 0, 890, CANVAS_W, 1);
	set_textbox(0xff808080ff808080, 0, 891, CANVAS_W, 1);
	set_textbox(0xff606060ff606060, 0, 892, CANVAS_W, 1);
	set_textbox(0xff606060ff606060, 0, 998, CANVAS_W, 1);
	set_textbox(0xff808080ff808080, 0, 999, CANVAS_W, 1);
	set_textbox(0xffa0a0a0ffa0a0a0, 0, 1000, CANVAS_W, 1);

	// draw game icons (5x2)
	j=(game_idx/gpp)*gpp;
	for(i=j;(slot<gpp&&i<games);i++)
	{
		slot++;
		if(load_img_bitmap(slot, slaunch[i].icon)<0) break;
		if(gpp==10)
		{
			py=((i-j)/5)*400+90+(300-ctx.img[slot].h)/2;
			ctx.img[slot].x=((px+(320-ctx.img[slot].w)/2)/2)*2;
			ctx.img[slot].y=py;
			set_backdrop(slot, 0);
			set_texture(slot, ctx.img[slot].x, ctx.img[slot].y);
			px+=(320+48); if(px>1600) px=56;
		}
		else
		{
			py=((i-j)/10)*200+90+(150-ctx.img[slot].h)/2;
			ctx.img[slot].x=((px+(160-ctx.img[slot].w)/2)/2)*2;
			ctx.img[slot].y=py;
			set_backdrop(slot, 0);
			set_texture(slot, ctx.img[slot].x, ctx.img[slot].y);
			px+=(160+24); if(px>1800) px=56;
		}

		if(key_repeat) break;
	}
	draw_selection(game_idx);
}

static void draw_side_menu_option(uint8_t option)
{
	memset((uint8_t *)ctx.side, 0x40, SM_M);
	ctx.fg_color=0xffe0e0e0;
	set_font(27.f, 23.f, 1.f, 0); print_text(ctx.side, (CANVAS_W-SM_X), SM_TO, SM_Y, "sMAN");

	ctx.fg_color=(option==1 ? 0xffc0c0c0 : 0xff808080);
	print_text(ctx.side, (CANVAS_W-SM_X), SM_TO+(option!=1)*32, SM_Y+4*22, STR_REFRESH);
	ctx.fg_color=(option==2 ? 0xffc0c0c0 : 0xff808080);
	print_text(ctx.side, (CANVAS_W-SM_X), SM_TO+(option!=2)*32, SM_Y+6*22, STR_UNMOUNT);
	ctx.fg_color=(option==3 ? 0xffc0c0c0 : 0xff808080);
	print_text(ctx.side, (CANVAS_W-SM_X), SM_TO+(option!=3)*32, SM_Y+8*22, STR_RESTART);
	ctx.fg_color=(option==4 ? 0xffc0c0c0 : 0xff808080);
	print_text(ctx.side, (CANVAS_W-SM_X), SM_TO+(option!=4)*32, SM_Y+10*22, STR_SHUTDOWN);
	ctx.fg_color=(option==5 ? 0xffc0c0c0 : 0xff808080);
	print_text(ctx.side, (CANVAS_W-SM_X), SM_TO+(option!=5)*32, SM_Y+17*22, STR_SETUP);
	ctx.fg_color=(option==6 ? 0xffc0c0c0 : 0xff808080);
	print_text(ctx.side, (CANVAS_W-SM_X), SM_TO+(option!=6)*32, SM_Y+19*22, STR_UNLOADSM);

	set_texture_direct(ctx.side, SM_X, 0, (CANVAS_W-SM_X), CANVAS_H/2);
	set_textbox(0x4040404040404040, SM_X, CANVAS_H/2, (CANVAS_W-SM_X), CANVAS_H/2);

	set_textbox(0xff808080ff808080, SM_X+SM_TO, SM_Y+28, CANVAS_W-SM_X-SM_TO*2, 1);
	set_textbox(0xff808080ff808080, SM_X+SM_TO, SM_Y+14*22, CANVAS_W-SM_X-SM_TO*2, 1);
}

static uint8_t draw_side_menu(void)
{
	uint8_t option=1;
	play_rco_sound("snd_cursor");

	dump_bg(SM_X-6, 0, (CANVAS_W-SM_X)+6, CANVAS_H);

	set_textbox(0xffe0e0e0ffd0d0d0, SM_X-6, 0, 2, CANVAS_H);
	set_textbox(0xffc0c0c0ffb0b0b0, SM_X-4, 0, 2, CANVAS_H);
	set_textbox(0xffa0a0a0ff909090, SM_X-2, 0, 2, CANVAS_H);

	draw_side_menu_option(option);

	while(slaunch_running && working)
	{
		pad_read();

		if(curpad)
		{
			if(curpad==oldpad)	// key-repeat
			{
				init_delay++;
				if(init_delay<=40) continue;
				else { sys_timer_usleep(40000); key_repeat=1; }
			}
			else
			{
				init_delay=0;
				key_repeat=0;
			}

			oldpad = curpad;

			// test screenshot
/*			if(curpad & PAD_SELECT)
			{
				dump_bg(0, 0, CANVAS_W, CANVAS_H);
				int fd;
				cellFsOpen("/dev_hdd0/screen2.raw", CELL_FS_O_CREAT|CELL_FS_O_RDWR|CELL_FS_O_TRUNC, &fd, NULL, 0);
				for(int i=0;i<1920*1080*4;i+=4)	cellFsWrite(fd, (uint8_t*)(ctx.canvas)+i+1, 3, NULL);
				cellFsClose(fd);
			}
*/
			if(curpad & PAD_UP)		option--;
			if(curpad & PAD_DOWN)	option++;

			if(option<1) option=6;
			if(option>6) option=1;

			if(curpad & PAD_TRIANGLE || curpad & PAD_CIRCLE) {option=0; play_rco_sound("snd_cancel"); break;}

			if(curpad & PAD_CROSS) {play_rco_sound("snd_system_ok"); break;}

			play_rco_sound("snd_cursor");
			draw_side_menu_option(option);

		}
		else
		{
			init_delay=0; oldpad=0;
		}
	}
	init_delay=key_repeat=0;
	set_texture_direct(ctx.canvas, SM_X-6, 0, (CANVAS_W-SM_X)+6, CANVAS_H);
	load_background();
	while(1) {sys_timer_usleep(250000); pad_read(); if(!(curpad & PAD_CROSS)) break;}
	return option;
}

static void sort_games(uint8_t type)
{
	int fd=0;

	uint32_t _games=(file_exists(SMAN_BIN))/sizeof(_slaunch);
	_slaunch swap;

	games=0;
	if(cellFsOpen((char*)SMAN_BIN, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		if(type==TYPE_ALL)
		{
			games=_games;
			cellFsRead(fd, (void *)slaunch, sizeof(_slaunch)*games, NULL);
		}
		else
		{
			for(uint32_t i=0; i<_games; i++)
			{
				if(cellFsRead(fd, &swap, sizeof(_slaunch), NULL) == CELL_FS_SUCCEEDED)
				{
					if(swap.type==type)
					{
						slaunch[games]=swap;
						games++;
					}
				}
			}
		}
		cellFsClose(fd);

		// add [GAMEID] to the game name
		for(uint32_t n=0; n<games; n++)
		{
			if(slaunch[n].id[0] && !strstr(slaunch[n].name, slaunch[n].id))
			{
				strcat(slaunch[n].name, " [");
				strcat(slaunch[n].name, slaunch[n].id);
				strcat(slaunch[n].name, "]");
			}
		}

		// sort game list
		/* // already sorted during scan
		if(games>1)
		{
			for(uint32_t n=0; n<(games-1); n++)
			{
				for(uint32_t m=(n+1); m<games; m++)
				{
					if(strcasecmp(slaunch[n].name, slaunch[m].name)>0)
					{
						swap=slaunch[n];
						slaunch[n]=slaunch[m];
						slaunch[m]=swap;
					}
				}
			}
		}
		*/
	}
	if(cur_game>=games) cur_game=_cur_game=0;
}


static void show_no_content(uint32_t type)
{
	char no_content[32];
	sprintf(no_content, "There are no %stitles.", game_type[type]);
	ctx.fg_color=0xffc0c0c0;
	set_font(32.f, 32.f, 1.5f, 1); print_text(ctx.canvas, CANVAS_W, 0, 520, no_content);
	flip_frame((uint64_t*)ctx.canvas);
	if(type) load_background();
}

static void load_config(void)
{
	reset_settings();

	type=sm_config->type;
	cur_game=sm_config->cur_game;
	gpp=sm_config->gpp;
	if(gpp!=10 && gpp!=40) gpp=10;
}

static void save_config(void)
{
	sm_config->type=type;
	sm_config->cur_game=cur_game;
	sm_config->gpp=gpp;

	save_settings();
}

static void load_data(void)
{
	slaunch = NULL;
	if((file_exists(SMAN_BIN))%sizeof(_slaunch)) cellFsUnlink(SMAN_BIN);

	games=(file_exists(SMAN_BIN))/sizeof(_slaunch);
	if(games>=MAX_GAMES) games=MAX_GAMES-1;

	load_background();

	if(games)
	{
		// allocate memory for game list in MC memory
		slaunch = (_slaunch*)mem_alloc((games+1)*sizeof(_slaunch));
		memset(slaunch, 0, (games+1)*sizeof(_slaunch));
		load_config();
		sort_games(type);
		if(!games) show_no_content(type);
	}
	else
	{
		// no games - show "no content" message and open refresh.ps3 page
		show_no_content(0);
		sys_timer_sleep(3);
		return_to_xmb();
		load_plugin_by_id(0x1B, (void *)web_browser2);
	}
}

static void start_VSH_Menu(void)
{
	int32_t ret, mem_size;
	// int smgui=1;

	// create VSH Menu heap memory from memory container 1 ("app")
	mem_size = (((CANVAS_W * CANVAS_H * 4) * 2 + (CANVAS_W * 96 * 4) + (FONT_CACHE_MAX * 32 * 32) + (MAX_WH4) + (SM_M) + ((MAX_GAMES+1)*sizeof(_slaunch))) + MB(1)) / MB(1);
	ret = create_heap(1, mem_size); //11MB

	if(ret) return;

	// rsx_fifo_pause(1);
	rsx_fifo_pause(0,1);

	disp_w = getDisplayWidth();
	disp_h = getDisplayHeight();

	// initialize VSH Menu graphics
	init_graphic();

	// stop vsh pad
	start_stop_vsh_pad(0);

	// load game list
	load_data();

	// set slaunch_running on
	if(slaunch) slaunch_running = 1;
}

//////////////////////////////////////////////////////////////////////
//                       STOP VSH MENU                              //
//////////////////////////////////////////////////////////////////////

static void stop_VSH_Menu(void)
{
	save_config();

	// gui off
	slaunch_running = 0;

	// unbind renderer and kill font-instance
	font_finalize();

	// free heap memory
	destroy_heap();

	// continue rsx rendering
	// rsx_fifo_pause(0);
	rsx_fifo_pause(0,0);

	// restart vsh pad
	start_stop_vsh_pad(1);
}

static void return_to_xmb(void)
{
	//dump_bg(0, 0, CANVAS_W, CANVAS_H);
	dim_bg();

	stop_VSH_Menu();
}


////////////////////////////////////////////////////////////////////////
//                      PLUGIN MAIN PPU THREAD                        //
////////////////////////////////////////////////////////////////////////
void slaunch_thread(uint64_t arg)
{
	play_rco_sound("snd_system_ng");

	if(vshmain_EB757101() == 0)
	{
		slaunch=NULL;
		start_VSH_Menu();
		if(!slaunch) goto quit;
		draw_page(cur_game, 0);
		init_delay=0;
	}

	while(slaunch_running && working)
	{

		pad_read();

		if(curpad)
		{
			if(curpad==oldpad)	// key-repeat
			{
				init_delay++;
				if(init_delay<=40) continue;
				else { sys_timer_usleep(40000); key_repeat=1; }
			}
			else
			{
				init_delay=0;
				key_repeat=0;
			}

			can_skip=0;
			oldpad = curpad;
			_cur_game=cur_game;

				 if(curpad & PAD_DOWN)	if(gpp==10) cur_game+=5; else cur_game+=10;
			else if(curpad & PAD_UP)	if(gpp==10) cur_game-=5; else cur_game-=10;
			else if(curpad & PAD_RIGHT) if(gpp==10 && (cur_game%10==4 || cur_game%10==9)) cur_game+=6;
										else if(gpp==40 && (cur_game%40==9 || cur_game%40==19 || cur_game%40==29 || cur_game%40==39)) cur_game+=31; else cur_game+=1;

			else if(curpad & PAD_LEFT)	if(!cur_game) cur_game=games-1; else if(gpp==10 && (cur_game%10==0 || cur_game%10==5)) cur_game-=6;
										else if(gpp==40 && (cur_game%40==0 || cur_game%40==10 || cur_game%40==20 || cur_game%40==30)) cur_game-=31; else cur_game-=1;

			else if(curpad & PAD_R1)	{can_skip=1; cur_game+=gpp;}
			else if(curpad & PAD_L1)	{can_skip=1; if(!cur_game) cur_game=games-1; else cur_game-=gpp; }

			else if(curpad & PAD_R3 && games)	{if(gpp==10) gpp=40; else gpp=10; draw_page(cur_game, 0); draw_selection(cur_game);}

			else if(curpad & PAD_SQUARE)					// next game category
				{
					play_rco_sound("snd_cursor");
					type++;
					if(type>=TYPE_MAX) type=TYPE_ALL;
					sort_games(type);
					if(!games)
					{
						show_no_content(type);
						sys_timer_usleep(40000);
					}
					else
						draw_page(cur_game, key_repeat);
				}
			else if(curpad & PAD_CIRCLE)					// back to XMB
				{
					play_rco_sound("snd_cancel");
					while(1) {sys_timer_usleep(250000); pad_read(); if(!(curpad & PAD_CIRCLE)) break;}
					break;
				}

			else if((curpad & PAD_CROSS) && games)			// mount game with sM and exit to XMB
				{
					play_rco_sound("snd_system_ok");

					for(uint8_t u=0;u<10;u++)				// selection flash
					{
						set_frame(1 + cur_game % gpp, 0xff400000ff400000);
						sys_timer_usleep(25020);
						set_frame(1 + cur_game % gpp, 0xffff0000ffff0000);
						sys_timer_usleep(25020);
					}

					while(1) {sys_timer_usleep(250000); pad_read(); if(!(curpad & PAD_CROSS)) break;}

					char launch_cmd[160];
					snprintf(launch_cmd, 160, "%s", slaunch[cur_game].path);
					return_to_xmb();
					send_wm_request(launch_cmd);
					break;
				}

			else if(curpad & PAD_TRIANGLE)					// open side-menu
				{
					uint8_t option=draw_side_menu();
					if(option)
					{
						return_to_xmb();
							 if(option==1) send_wm_request("/refresh_ps3");
						else if(option==2) send_wm_request("/mount_ps3/unmount");
						else if(option==3) send_wm_request("/restart.ps3");
						else if(option==4) send_wm_request("/shutdown.ps3");
						else if(option==5) load_plugin_by_id(0x1B, (void *)web_browser);
						else if(option==6) send_wm_request("/quit.ps3");

						goto quit;
					}
					if(!games)
					{
						show_no_content(type);
						sys_timer_usleep(40000);
					}
				}

			if(cur_game!=_cur_game && games)
			{
				play_rco_sound("snd_cursor");
				tick=0xc0;
				set_backdrop((1+_cur_game%gpp), 1);			// draw icon shadow
				if(cur_game>=games) cur_game=0;
				if((cur_game/gpp)*gpp != (_cur_game/gpp)*gpp)	// new page must be drawn
					draw_page(cur_game, key_repeat & can_skip);
				else
					draw_selection(cur_game);				// draw only selection frame
			}
		}
		else
		{
			if(init_delay>40 && (oldpad & PAD_R1 || oldpad & PAD_L1))
				draw_page(cur_game, 0);

			tick+=4; tick&=0xff;							// pulsing selection frame
			if(tick<0x80) tick=0x80;
			if(games) set_frame(1 + cur_game % gpp, 0xff000000ff000000|tick<<48|tick<<16);
			init_delay=0; oldpad=0;
		}
	}

quit:
	if(slaunch_running)
		stop_VSH_Menu();

	sys_ppu_thread_exit(0);
}
