#include <stdio.h>
#include <aroma.h>

/* main macros */
#define alog(...) { printf(__VA_ARGS__); printf("\n");}
#define DVDS_MAX	8
#define DIR_UP		0x1
#define DIR_DOWN	0x2
#define DIR_LEFT	0x4
#define DIR_RIGHT	0x8
#define MODE_START	0x0
#define MODE_PLAY	0x1
#define MODE_END	0x2

/* structs */
typedef struct {
	byte present;
	LIBAROMA_CANVASP logo;
	int x;
	int y;
	byte bounce;
	byte direction;
	byte color_index;
	byte crashed;
	int speed;
	int score;
} DVD, * DVDP;

typedef struct {
	int x;
	int y;
	int w;
	int h;
	byte ondrag;
} RACKET, * RACKETP;

typedef struct {
	char scores[128];
	char speeds[128];
	int h;
} INFOBAR, * INFOBARP;

/* global variables */
LIBAROMA_ZIP zip;
RACKET player;
DVD dvds[DVDS_MAX];
LIBAROMA_CANVASP logo;
INFOBAR statusbar;
byte ui_disabled;
byte game_mode;
int dvd_count;
word dvd_colors[4];
long last_tick;
int last_x;

/* function protos */
byte mlx_msg_handler(LIBAROMA_WMP wm, LIBAROMA_MSGP msg);
byte mlx_ui_thread();
byte mlx_new_dvd(int index, byte direction, int speed, byte color_index);
void mlx_rm_dvd(int index);

int main(int argc, char **argv){
	alog("argc=%d argv[1]=%s", argc, (argc>1)?argv[1]:"null");
	if (argc>3) zip = libaroma_zip(argv[3]);
	if (argc>1 && !zip) zip = libaroma_zip(argv[1]);
	if (!zip) zip = libaroma_zip("./res.zip");
	if (!zip) zip = libaroma_zip("/tmp/res.zip");
	if (!zip){
		alog("failed to open zip, F");
		return 1;
	}
	libaroma_config()->runtime_monitor = LIBAROMA_START_MUTEPARENT;
	//force LDPI
	//libaroma_gfx_startup_dpi(120);
	/* start libaroma */
	if (!libaroma_start()){
		alog("failed to start libaroma");
		libaroma_zip_release(zip);
		return 2;
	}
	int ret = 0;
	//libaroma_canvas_setcolor(libaroma_fb()->canvas, 0, 0xFF);
	alog("libaroma started");
	/* load font */
	if (!libaroma_font(0, libaroma_stream_mzip(zip, "Roboto-Regular.ttf"))){
		alog("failed to load font");
		ret = 3;
		goto main_end;
	}
	/* fill colors struct */
	dvd_colors[0]=RGB(FF0000);
	dvd_colors[1]=RGB(00FF00);
	dvd_colors[2]=RGB(0000FF);
	dvd_colors[3]=RGB(FF00FF);
	/* load & scale main logo */
	LIBAROMA_CANVASP logo_res = libaroma_image_mzip(zip, "dvd.png", 0);
	if (!logo_res){
		alog("failed to load image");
		ret = 4;
		goto main_end;
	}
	/* allocate new canvas for scaled logo */
	logo = libaroma_canvas_alpha(libaroma_dp(logo_res->w), libaroma_dp(logo_res->h));
	if (!logo){
		alog("failed to allocate canvas :(");
		libaroma_canvas_free(logo_res);
		ret = 5;
		goto main_end;
	}
	alog("scaling logo res [%dx%d] -> [%dx%d]", logo_res->w, logo_res->h, logo->w, logo->h);
	libaroma_draw_scale_smooth(logo, logo_res, 0, 0, logo->w, logo->h, 0, 0, logo_res->w, logo_res->h);
	/* release loaded resource */
	libaroma_canvas_free(logo_res);
	/* create new player & dvd before starting */
	player.w = libaroma_fb()->w/4;
	player.x = (libaroma_fb()->w-player.w)/2;
	player.y = libaroma_fb()->h-libaroma_dp(32);
	player.h = libaroma_dp(16);
	if (!mlx_new_dvd(dvd_count, DIR_DOWN|DIR_RIGHT, 3, 0)){
		alog("failed to create first dvd");
		ret = 6;
		goto main_end;
	}
	/* setup statusbar */
	statusbar.h = libaroma_dp(24);
	/* set message handler & ui thread */
	libaroma_wm_set_message_handler(&mlx_msg_handler);
	libaroma_wm_set_ui_thread(&mlx_ui_thread);
	/* wait until window manager is stopped */
	do { libaroma_sleep(100); } while(libaroma_wm()->client_started);
	/* cleanup */
	int i;
	for (i=0; i<dvd_count; i++){
		mlx_rm_dvd(i);
	}
	libaroma_canvas_free(logo);
main_end:
	libaroma_zip_release(zip);
	libaroma_end();
	return ret;
}

byte mlx_new_dvd(int index, byte direction, int speed, byte color_index){
	/* check out of bounds */
	if (index>=DVDS_MAX || dvd_count>=DVDS_MAX){
		alog("no room for more dvds :/");
		return 0;
	}
	/* if already present, remove it */
	if (dvds[index].present){
		mlx_rm_dvd(index);
	}
	/* fill struct */
	dvds[index].logo = libaroma_canvas_dup(logo);
	libaroma_canvas_fillcolor(dvds[index].logo, dvd_colors[color_index]);
	dvds[index].present = 1;
	dvds[index].x = (libaroma_fb()->w-dvds[index].logo->w)/2;
	dvds[index].y = (libaroma_fb()->h-dvds[index].logo->h)/2;
	dvds[index].bounce = 0;
	dvds[index].direction = direction;
	dvds[index].speed = speed;
	dvds[index].color_index = color_index;
	dvds[index].crashed = 0;
	dvds[index].score = 0;
	dvd_count++;
	return 1;
}

void mlx_rm_dvd(int index){
	if (!dvds[index].present) return;
	libaroma_canvas_free(dvds[index].logo);
	dvds[index].present = 0;
	dvd_count--;
}

byte mlx_msg_handler(LIBAROMA_WMP wm, LIBAROMA_MSGP msg){
	/* game mode switch */
	switch (game_mode){
		/* ingame screen */
		case MODE_PLAY:{
			/* touch handler */
			if (msg->msg==LIBAROMA_MSG_TOUCH){
				if (player.ondrag){
					/* already dragging, just update x */
					player.x = player.x + msg->x - last_x;
					last_x = msg->x;
					if (msg->state==0) player.ondrag = 0;
				} else if (msg->state==1 && msg->y>=player.y && msg->y<=player.y+player.h &&
						msg->x>=player.x && msg->x<=player.x+player.w){
					/* set mouse down */
					last_x=msg->x;
					player.ondrag=1;
				}
				return 1;
			}
			else if (msg->state==0){
				if (msg->msg==LIBAROMA_MSG_EXIT ||
						msg->msg==LIBAROMA_MSG_KEY_POWER ||
						msg->msg==LIBAROMA_MSG_KEY_SELECT){
					/* finish game by exit/power/select */
					game_mode = MODE_END;
				}
				/* speed up/down on vol/arrow keys */
				else if (msg->msg==LIBAROMA_MSG_KEY_VOLUP || msg->key==LIBAROMA_HID_KEY_UP) dvds[0].speed+=1;
				else if (msg->msg==LIBAROMA_MSG_KEY_VOLDOWN || msg->key==LIBAROMA_HID_KEY_DOWN) dvds[0].speed-=1;
				return 1;
			}
		} break;
		/* startup screen */
		case MODE_START:{
			/* exit/power/select handler */
			if (msg->state==0 && (msg->msg==LIBAROMA_MSG_EXIT ||
					msg->msg==LIBAROMA_MSG_KEY_POWER ||
					msg->msg==LIBAROMA_MSG_KEY_SELECT)){
				/* just switch to play mode */
				game_mode = MODE_PLAY;
				ui_disabled = 0;
				return 1;
			}
		} break;
		/* end screen */
		case MODE_END:{
			/* exit/power/select handler */
			if (msg->state==0 && (msg->msg==LIBAROMA_MSG_EXIT ||
					msg->msg==LIBAROMA_MSG_KEY_POWER ||
					msg->msg==LIBAROMA_MSG_KEY_SELECT)){
				alog("exiting");
				libaroma_wm_set_ui_thread(NULL);
				return 1;
			}
		} break;
	}
	return 0;
}


byte mlx_ui_thread(){
	/* if ui isn't disabled and last update was >= 16ms ago */
	if ((!ui_disabled) && libaroma_tick()-last_tick>16){
		last_tick=libaroma_tick();
		switch (game_mode){
			case MODE_START:{
				libaroma_canvas_setcolor(libaroma_fb()->canvas, 0, 0xFF);
				LIBAROMA_CANVASP splash = libaroma_canvas_dup(logo);
				if (!splash){
					alog("failed to load splash");
					return 1;
				}
				libaroma_canvas_fillcolor(splash, RGB(FFFFFF));
				int splash_x, splash_y;
				splash_x = (libaroma_fb()->w-splash->w)/2;
				splash_y = (libaroma_fb()->h-splash->h)/2;
				libaroma_draw(libaroma_fb()->canvas, splash, splash_x, splash_y, 1);
				libaroma_draw_text(libaroma_fb()->canvas, "\nDVD-PONG\n\n"
					"Initial speed: 3\n"
					"Initial DVDs: 1", 0, splash_y+splash->h, RGB(FFFFFF), libaroma_fb()->w, LIBAROMA_FONT(0,4)|LIBAROMA_TEXT_CENTER, 0);
				ui_disabled=1;
				return 1;
			} break;
			case MODE_PLAY:{
				/* erase background */
				libaroma_canvas_setcolor(libaroma_fb()->canvas, 0, 0xFF);
				/* initialize statusbar text */
				int scores_offset=snprintf(statusbar.scores, 128, "Score: ");
				int speeds_offset=snprintf(statusbar.speeds, 128, "Speed: ");
				/* draw player */
				libaroma_draw_rect(libaroma_fb()->canvas, player.x, player.y, 
					(player.x<0)?(player.w+player.x):player.w, player.h, 
					RGB(FFFFFF), 0xFF);
				/* draw all dvds */
				int i;
				for (i=0; i<dvd_count; i++){
					DVDP dvd = &dvds[i];
					/* update statusbar scores */
					if (scores_offset<128) scores_offset += snprintf(statusbar.scores+scores_offset, 128, "%d/", dvd->score);
					if (speeds_offset<128) speeds_offset += snprintf(statusbar.speeds+speeds_offset, 128, "%d/", dvd->speed);
					/* check bounce */
					if (dvd->bounce){
						/* change color and update index */
						libaroma_canvas_fillcolor(dvd->logo, dvd_colors[dvd->color_index]);
						dvd->color_index++;
						if (dvd->color_index>3) dvd->color_index=0;
						dvd->bounce=0;
					}
					/* check bounds */
					if (dvd->x+dvd->logo->w>=libaroma_fb()->w){
						alog("right bounce");
						/* if right border touched, change direction & set bounce */
						dvd->direction &= ~DIR_RIGHT;
						dvd->direction |= DIR_LEFT;
						dvd->bounce = 1;
					}
					if (dvd->x<0){
						alog("left bounce");
						/* same for left border */
						dvd->direction &= ~DIR_LEFT;
						dvd->direction |= DIR_RIGHT;
						dvd->bounce = 1;
					}
					if (dvd->y+dvd->logo->h>=player.y){
						alog("racket bounce");
						/* if dvd is at/below player position, check for bounce or game end */
						if (dvd->x > player.x+player.w || dvd->x+dvd->logo->w < player.x || dvd->crashed){
							/* set crashed */
							dvd->crashed=1;
							/* if bottom border is touched, change game mode */
							if (dvd->y+dvd->logo->h>=libaroma_fb()->h){
								dvd->crashed=0;
								game_mode = MODE_END;
							}
						}
						else {
							/* change horizontal direction depending on left/right half being touched */
							if (dvd->x+dvd->logo->w > player.x+(player.w/2)){
								if (dvd->direction&DIR_LEFT){
									dvd->direction &= ~DIR_LEFT;
									dvd->direction |= DIR_RIGHT;
								}
								else {
									dvd->direction &= ~DIR_RIGHT;
									dvd->direction |= DIR_LEFT;	
								}
							}
							else {
								if (dvd->direction&DIR_LEFT){
									dvd->direction &= ~DIR_RIGHT;
									dvd->direction |= DIR_LEFT;	
								}
								else {
									dvd->direction &= ~DIR_LEFT;
									dvd->direction |= DIR_RIGHT;
								}
							}
							/* change vertical direction & set bounce */
							dvd->direction &= ~DIR_DOWN;
							dvd->direction |= DIR_UP;
							dvd->bounce = 1;
							dvd->speed += 1;
							dvd->score++;
							if ((dvd->score == 6) && dvd_count < DVDS_MAX){
								mlx_new_dvd(dvd_count, DIR_DOWN|DIR_RIGHT, 3, 0);
							}
						}
					}
					if (dvd->y<=statusbar.h){
						alog("top bounce");
						/* top border touched, bounce downwards */
						dvd->direction &= ~DIR_UP;
						dvd->direction |= DIR_DOWN;
						dvd->bounce = 1;
					}
					/* clean previous logo (removed because of multilogo) */
					//libaroma_draw_rect(libaroma_fb()->canvas, dvd->x, dvd->y, dvd->logo->w, dvd->logo->h, 0, 0xFF);
					/* update coords using speed */
					if (dvd->direction&DIR_RIGHT) dvd->x+=dvd->speed;
					else if (dvd->direction&DIR_LEFT) dvd->x-=dvd->speed;
					if (dvd->direction&DIR_DOWN) dvd->y+=dvd->speed;
					else if (dvd->direction&DIR_UP) dvd->y-=dvd->speed;
					/* prevent logo overlapping statusbar */
					if (dvd->y<statusbar.h) dvd->y=statusbar.h;
					//alog("dvd #%d x=%d y=%d", i+1, dvd->x, dvd->y);
					/* draw logo at new coords */
					libaroma_draw(libaroma_fb()->canvas, dvd->logo, dvd->x, dvd->y, 1);
				}
				/* fix scores/speeds & draw status bar */
				if (statusbar.scores[scores_offset-1]=='/') statusbar.scores[scores_offset-1]='\0';
				if (statusbar.speeds[speeds_offset-1]=='/') statusbar.speeds[speeds_offset-1]='\0';
				//alog("%s |%s| %s", statusbar.scores, "DVD-PONG", statusbar.speeds);
				libaroma_draw_text(libaroma_fb()->canvas, "DVD-PONG", 0, 0, RGB(FFFFFF), libaroma_fb()->w, LIBAROMA_FONT(0,3)|LIBAROMA_TEXT_SINGLELINE|LIBAROMA_TEXT_CENTER, 0);
				libaroma_draw_text(libaroma_fb()->canvas, statusbar.scores, 0, 0, RGB(FFFFFF), libaroma_fb()->w, LIBAROMA_FONT(0,3)|LIBAROMA_TEXT_SINGLELINE, 0);
				libaroma_draw_text(libaroma_fb()->canvas, statusbar.speeds, 0, 0, RGB(FFFFFF), libaroma_fb()->w, LIBAROMA_FONT(0,3)|LIBAROMA_TEXT_SINGLELINE|LIBAROMA_TEXT_RIGHT, 0);
				return 1;
			} break;
			case MODE_END:{
				//libaroma_canvas_setcolor(libaroma_fb()->canvas, RGB(444444), 0x44);
				libaroma_draw_rect(libaroma_fb()->canvas, 0, 0, libaroma_fb()->w, libaroma_fb()->h, RGB(444444), 0x88);
				LIBAROMA_TEXT lost_text = libaroma_text("F", RGB(FFFFFF), libaroma_fb()->w, LIBAROMA_FONT(0,15)|LIBAROMA_TEXT_CENTER, 0);
				int lost_h=libaroma_text_height(lost_text);
				int lost_y=(libaroma_fb()->h-lost_h)/2;
				libaroma_text_draw(libaroma_fb()->canvas, lost_text, 0, lost_y);
				char lost_extra[256]={0};
				snprintf(lost_extra, 256, 
					"You lost.\n"
					"%s\n" /* Score: */
					"%s\n" /* Speeds: */
					"DVD count: %d",
					statusbar.scores, statusbar.speeds, dvd_count);
				libaroma_draw_text(libaroma_fb()->canvas, lost_extra, 0, lost_y+lost_h, RGB(FFFFFF), libaroma_fb()->w, LIBAROMA_FONT(0,4)|LIBAROMA_TEXT_CENTER, 0);
				libaroma_text_free(lost_text);
				ui_disabled=1;
				return 1;
			} break;
		}
	}
	return 0;
}