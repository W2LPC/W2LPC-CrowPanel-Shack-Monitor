#define CrowPanel_70
#include <Wire.h>
#include <PCA9557.h>
#include "gfx_conf.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_psram.h"
#include <time.h>

#define WIFI_SSID   "YOUR WIFI"
#define WIFI_PASS   "WIFI PASSWORD"
#define DAEMON_IP   "IP OF YOUR COMPUTER"
#define DAEMON_PORT 5000
#define OWM_KEY     "WEATHER API KEY"
#define CALLSIGN    "YOUR CALLSIGN AND GRID"
#define CPU_LABEL   "YOUR PC TYPE CPU.BOARD"
#define GPU_LABEL   "YOUR VIDEO CARD"
#define DISK_LABEL  "YOUR HARD DISK TYPE/NAME"

#define C_BG     lv_color_hex(0x080c14)
#define C_CARD   lv_color_hex(0x0d1929)
#define C_BORDER lv_color_hex(0x1e3a5a)
#define C_TEXT   lv_color_hex(0xe8f4ff)
#define C_DIM    lv_color_hex(0x4a6a8a)
#define C_GREEN  lv_color_hex(0x3aaa5a)
#define C_YELLOW lv_color_hex(0xddaa00)
#define C_ORANGE lv_color_hex(0xff8c00)
#define C_BLUE   lv_color_hex(0x4a8fc0)
#define C_PURPLE lv_color_hex(0x8a5ad0)
#define C_DARK   lv_color_hex(0x050a12)
#define C_RED    lv_color_hex(0xff3333)

PCA9557 Out;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1, *buf2;
static lv_obj_t *scr_monitor = NULL;
static lv_obj_t *scr_bands   = NULL;

// Monitor screen labels
static lv_obj_t *lbl_date, *lbl_time_top, *lbl_weather, *lbl_uptime_top;
static lv_obj_t *lbl_cpu_val, *lbl_cpu_temp, *core_bars[12];
static lv_obj_t *ring_ram, *lbl_ring_val;
static lv_obj_t *lbl_ram_used, *lbl_ram_free, *bar_ram, *lbl_ram_pct;
static lv_obj_t *lbl_gpu_val, *lbl_gpu_temp, *bar_vram, *lbl_vram;
static lv_obj_t *lbl_dr, *lbl_dw, *bar_dr, *bar_dw;
static lv_obj_t *chart_disk;
static lv_chart_series_t *ser_disk;
static lv_obj_t *lbl_nu, *lbl_nd, *bar_nu, *bar_nd;
static lv_obj_t *chart_net;
static lv_chart_series_t *ser_net;
static lv_obj_t *p_nm[4], *p_pid[4], *p_cpu[4], *p_ram[4], *p_st[4];

// Bands screen labels
static lv_obj_t *lbl_sfi, *lbl_sfi_sub;
static lv_obj_t *lbl_ai,  *lbl_ai_sub;
static lv_obj_t *lbl_ki,  *lbl_ki_sub;
static lv_obj_t *lbl_xr,  *lbl_xr_sub;
static lv_obj_t *lbl_band_day[6], *lbl_band_night[6], *lbl_band_num[6];
static lv_obj_t *lbl_bands_updated;
static lv_obj_t *lbl_bands_local, *lbl_bands_utc;

static unsigned long t_stats=0, t_weather=0, t_bands=0;
#define IV_STATS    2000
#define IV_WEATHER  300000
#define IV_BANDS    300000

// ─── TOUCH ──────────────────────────────────────────────────────────────────
void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int32_t x = 0, y = 0;
    bool touched = tft.getTouch(&x, &y);
    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

// ─── DISPLAY FLUSH ──────────────────────────────────────────────────────────
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    tft.startWrite();
    tft.setAddrWindow(area->x1,area->y1,area->x2-area->x1+1,area->y2-area->y1+1);
    tft.pushColors((uint16_t*)&color_p->full,(area->x2-area->x1+1)*(area->y2-area->y1+1),true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

// ─── HELPER WIDGETS ─────────────────────────────────────────────────────────
lv_obj_t* mk_cont(lv_obj_t *p,int x,int y,int w,int h,lv_color_t bg,bool brd) {
    lv_obj_t *c=lv_obj_create(p);
    lv_obj_set_pos(c,x,y); lv_obj_set_size(c,w,h);
    lv_obj_set_style_bg_color(c,bg,0); lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(c,C_BORDER,0);
    lv_obj_set_style_border_width(c,brd?1:0,0);
    lv_obj_set_style_radius(c,brd?5:0,0);
    lv_obj_set_style_pad_all(c,0,0);
    lv_obj_clear_flag(c,LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t* mk_lbl(lv_obj_t *p,const char *t,int x,int y,const lv_font_t *f,lv_color_t col) {
    lv_obj_t *l=lv_label_create(p);
    lv_label_set_text(l,t);
    lv_obj_set_style_text_font(l,f,0);
    lv_obj_set_style_text_color(l,col,0);
    lv_obj_set_pos(l,x,y);
    return l;
}

lv_obj_t* mk_bar(lv_obj_t *p,int x,int y,int w,int h,int mx,lv_color_t col) {
    lv_obj_t *b=lv_bar_create(p);
    lv_obj_set_pos(b,x,y); lv_obj_set_size(b,w,h);
    lv_bar_set_range(b,0,mx); lv_bar_set_value(b,0,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x1a3a5a),0);
    lv_obj_set_style_bg_color(b,col,LV_PART_INDICATOR);
    lv_obj_set_style_radius(b,2,0); lv_obj_set_style_radius(b,2,LV_PART_INDICATOR);
    return b;
}

lv_obj_t* mk_chart(lv_obj_t *p,int x,int y,int w,int h,
                    lv_chart_type_t type,int ymax,
                    lv_color_t col,lv_chart_series_t **sout) {
    lv_obj_t *ch=lv_chart_create(p);
    lv_obj_set_pos(ch,x,y); lv_obj_set_size(ch,w,h);
    lv_chart_set_type(ch,type);
    lv_chart_set_range(ch,LV_CHART_AXIS_PRIMARY_Y,0,ymax);
    lv_chart_set_point_count(ch,20);
    lv_obj_set_style_bg_color(ch,lv_color_hex(0x060e1a),0);
    lv_obj_set_style_bg_opa(ch,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(ch,0,0);
    lv_obj_set_style_radius(ch,3,0);
    lv_obj_set_style_pad_all(ch,2,0);
    lv_chart_set_div_line_count(ch,0,0);
    *sout=lv_chart_add_series(ch,col,LV_CHART_AXIS_PRIMARY_Y);
    if (type==LV_CHART_TYPE_LINE) lv_obj_set_style_line_width(ch,2,LV_PART_ITEMS);
    lv_obj_set_style_size(ch,0,LV_PART_INDICATOR);
    return ch;
}

// ─── MONITOR SCREEN ─────────────────────────────────────────────────────────
void build_monitor_screen() {
    scr_monitor=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_monitor,C_BG,0);
    lv_obj_set_style_bg_opa(scr_monitor,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(scr_monitor,0,0);

    lv_obj_t *tb=mk_cont(scr_monitor,0,0,800,50,lv_color_hex(0x0a1525),false);
    mk_cont(tb,0,49,800,1,C_BORDER,false);
    lbl_date    = mk_lbl(tb,"--- --- -- ----",8,4,&lv_font_montserrat_12,C_DIM);
    lbl_time_top= mk_lbl(tb,"00:00:00",8,18,&lv_font_montserrat_20,C_TEXT);
    lbl_weather = mk_lbl(tb,"-- --",290,17,&lv_font_montserrat_16,C_BLUE);
    mk_lbl(tb,CALLSIGN,560,4,&lv_font_montserrat_12,C_DIM);
    lbl_uptime_top=mk_lbl(tb,"UP: ---",560,22,&lv_font_montserrat_12,C_DIM);

    lv_obj_t *btn=lv_btn_create(tb);
    lv_obj_set_pos(btn,702,9); lv_obj_set_size(btn,90,30);
    lv_obj_set_style_bg_color(btn,lv_color_hex(0x1a3a5a),0);
    lv_obj_set_style_bg_color(btn,lv_color_hex(0x2a4a70),LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn,C_BLUE,0);
    lv_obj_set_style_border_width(btn,1,0);
    lv_obj_set_style_radius(btn,4,0);
    lv_obj_set_style_shadow_width(btn,0,0);
    lv_obj_add_event_cb(btn,[](lv_event_t *e){
        lv_scr_load_anim(scr_bands,LV_SCR_LOAD_ANIM_MOVE_LEFT,250,0,false);
    },LV_EVENT_CLICKED,NULL);
    lv_obj_t *bl=lv_label_create(btn);
    lv_label_set_text(bl,"BANDS >");
    lv_obj_set_style_text_font(bl,&lv_font_montserrat_12,0);
    lv_obj_set_style_text_color(bl,C_TEXT,0);
    lv_obj_center(bl);

    int r1y=52,r1h=162,cw=264;

    lv_obj_t *cc=mk_cont(scr_monitor,0,r1y,cw,r1h,C_CARD,true);
    mk_lbl(cc,"CPU",8,5,&lv_font_montserrat_12,C_DIM);
    mk_lbl(cc,CPU_LABEL,cw-88,5,&lv_font_montserrat_12,C_DIM);
    lbl_cpu_val=mk_lbl(cc,"0%",10,20,&lv_font_montserrat_20,C_TEXT);
    lbl_cpu_temp=lv_label_create(cc);
    lv_label_set_text(lbl_cpu_temp,"TEMP\n--C");
    lv_obj_set_style_text_font(lbl_cpu_temp,&lv_font_montserrat_12,0);
    lv_obj_set_style_text_color(lbl_cpu_temp,C_YELLOW,0);
    lv_obj_set_pos(lbl_cpu_temp,76,18);
    int bw=36;
    for (int i=0;i<12;i++) {
        int row=i/6,col=i%6;
        int bx=6+col*40,by=68+row*38;
        core_bars[i]=mk_bar(cc,bx,by,bw,8,100,C_BLUE);
        char cl[5]; snprintf(cl,5,"C%d",i+1);
        mk_lbl(cc,cl,bx+bw/2-6,by+10,&lv_font_montserrat_12,C_DIM);
    }

    lv_obj_t *rc=mk_cont(scr_monitor,267,r1y,cw,r1h,C_CARD,true);
    mk_lbl(rc,"RAM",8,5,&lv_font_montserrat_12,C_DIM);
    mk_lbl(rc,"64 GB DDR4",cw-78,5,&lv_font_montserrat_12,C_DIM);
    ring_ram=lv_arc_create(rc);
    lv_obj_set_size(ring_ram,90,90); lv_obj_set_pos(ring_ram,6,18);
    lv_arc_set_range(ring_ram,0,100); lv_arc_set_value(ring_ram,0);
    lv_arc_set_bg_angles(ring_ram,0,360);
    lv_arc_set_rotation(ring_ram,270);
    lv_obj_set_style_arc_color(ring_ram,lv_color_hex(0x0a1e30),LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring_ram,C_BLUE,LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring_ram,10,LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_ram,10,LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ring_ram,LV_OPA_TRANSP,0);
    lv_obj_remove_style(ring_ram,NULL,LV_PART_KNOB);
    lbl_ring_val=lv_label_create(rc);
    lv_label_set_text(lbl_ring_val,"--\nGB");
    lv_obj_set_style_text_font(lbl_ring_val,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(lbl_ring_val,C_TEXT,0);
    lv_obj_set_style_text_align(lbl_ring_val,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_pos(lbl_ring_val,40,52);
    mk_lbl(rc,"USED",108,20,&lv_font_montserrat_12,C_DIM);
    lbl_ram_used=mk_lbl(rc,"-- GB",108,32,&lv_font_montserrat_16,C_TEXT);
    mk_lbl(rc,"FREE",108,56,&lv_font_montserrat_12,C_DIM);
    lbl_ram_free=mk_lbl(rc,"-- GB",108,68,&lv_font_montserrat_16,C_GREEN);
    mk_lbl(rc,"TOTAL",108,92,&lv_font_montserrat_12,C_DIM);
    mk_lbl(rc,"64.0 GB",108,104,&lv_font_montserrat_14,C_DIM);
    bar_ram=mk_bar(rc,8,124,cw-16,6,100,C_BLUE);
    lbl_ram_pct=mk_lbl(rc,"0% utilized",8,134,&lv_font_montserrat_12,C_DIM);

    lv_obj_t *gc=mk_cont(scr_monitor,534,r1y,266,r1h,C_CARD,true);
    mk_lbl(gc,"GPU",8,5,&lv_font_montserrat_12,C_DIM);
    mk_lbl(gc,GPU_LABEL,266-68,5,&lv_font_montserrat_12,C_DIM);
    lbl_gpu_val=mk_lbl(gc,"0%",10,20,&lv_font_montserrat_20,C_TEXT);
    lbl_gpu_temp=lv_label_create(gc);
    lv_label_set_text(lbl_gpu_temp,"TEMP\n--C");
    lv_obj_set_style_text_font(lbl_gpu_temp,&lv_font_montserrat_12,0);
    lv_obj_set_style_text_color(lbl_gpu_temp,C_YELLOW,0);
    lv_obj_set_pos(lbl_gpu_temp,76,18);
    mk_lbl(gc,"VRAM",8,68,&lv_font_montserrat_12,C_DIM);
    bar_vram=mk_bar(gc,8,82,250,8,100,C_PURPLE);
    lbl_vram=mk_lbl(gc,"-- / -- GB",8,96,&lv_font_montserrat_12,C_DIM);

    int r2y=217,r2h=128;
    lv_obj_t *dc=mk_cont(scr_monitor,0,r2y,393,r2h,C_CARD,true);
    mk_lbl(dc,"DISK  READ",8,5,&lv_font_montserrat_12,C_DIM);
    mk_lbl(dc,DISK_LABEL,393-76,5,&lv_font_montserrat_12,C_DIM);
    lbl_dr=mk_lbl(dc,"0",8,18,&lv_font_montserrat_20,C_GREEN);
    mk_lbl(dc,"MB/s",62,26,&lv_font_montserrat_12,C_DIM);
    bar_dr=mk_bar(dc,8,52,175,5,300,C_GREEN);
    mk_lbl(dc,"WRITE",205,5,&lv_font_montserrat_12,C_DIM);
    lbl_dw=mk_lbl(dc,"0",205,18,&lv_font_montserrat_20,C_ORANGE);
    mk_lbl(dc,"MB/s",255,26,&lv_font_montserrat_12,C_DIM);
    bar_dw=mk_bar(dc,205,52,175,5,300,C_ORANGE);
    mk_lbl(dc,"ACTIVITY",8,62,&lv_font_montserrat_12,C_DIM);
    chart_disk=mk_chart(dc,8,78,377,42,LV_CHART_TYPE_BAR,300,C_GREEN,&ser_disk);

    lv_obj_t *nc=mk_cont(scr_monitor,396,r2y,404,r2h,C_CARD,true);
    mk_lbl(nc,"NETWORK",8,5,&lv_font_montserrat_12,C_DIM);
    mk_lbl(nc,DAEMON_IP,404-86,5,&lv_font_montserrat_12,C_DIM);
    mk_lbl(nc,"UPLOAD",8,18,&lv_font_montserrat_12,C_DIM);
    lbl_nu=mk_lbl(nc,"0.0",8,28,&lv_font_montserrat_20,C_BLUE);
    mk_lbl(nc,"MB/s",54,36,&lv_font_montserrat_12,C_DIM);
    bar_nu=mk_bar(nc,8,52,185,5,100,C_BLUE);
    mk_lbl(nc,"DOWNLOAD",205,18,&lv_font_montserrat_12,C_DIM);
    lbl_nd=mk_lbl(nc,"0.0",205,28,&lv_font_montserrat_20,C_GREEN);
    mk_lbl(nc,"MB/s",251,36,&lv_font_montserrat_12,C_DIM);
    bar_nd=mk_bar(nc,205,52,185,5,100,C_GREEN);
    mk_lbl(nc,"THROUGHPUT",8,62,&lv_font_montserrat_12,C_DIM);
    chart_net=mk_chart(nc,8,78,386,42,LV_CHART_TYPE_LINE,50,C_GREEN,&ser_net);

    lv_obj_t *pc=mk_cont(scr_monitor,0,348,800,98,C_CARD,true);
    mk_lbl(pc,"TOP PROCESSES",8,4,&lv_font_montserrat_12,C_DIM);
    mk_lbl(pc,"PROCESS", 8,18,&lv_font_montserrat_12,C_DIM);
    mk_lbl(pc,"PID",   290,18,&lv_font_montserrat_12,C_DIM);
    mk_lbl(pc,"CPU%",  370,18,&lv_font_montserrat_12,C_DIM);
    mk_lbl(pc,"RAM",   460,18,&lv_font_montserrat_12,C_DIM);
    mk_lbl(pc,"STATUS",610,18,&lv_font_montserrat_12,C_DIM);
    mk_cont(pc,8,32,784,1,C_BORDER,false);
    for (int i=0;i<4;i++) {
        int py=36+i*15;
        p_nm[i] =mk_lbl(pc,"---",     8,py,&lv_font_montserrat_12,C_TEXT);
        p_pid[i]=mk_lbl(pc,"----",  290,py,&lv_font_montserrat_12,C_DIM);
        p_cpu[i]=mk_lbl(pc,"0.0%",  370,py,&lv_font_montserrat_12,C_YELLOW);
        p_ram[i]=mk_lbl(pc,"-- MB", 460,py,&lv_font_montserrat_12,C_DIM);
        p_st[i] =mk_lbl(pc,"---",   610,py,&lv_font_montserrat_12,C_GREEN);
    }

    lv_obj_t *ft=mk_cont(scr_monitor,0,449,800,31,C_DARK,false);
    mk_cont(ft,0,0,800,1,C_BORDER,false);
    mk_lbl(ft,"COMPUTER GEEKS OF AMERICA  |  YOUR CALLSIGN HERE  |  LPC Systems LLC  |  YOUR COMPUTER NAME HERE",
           140,9,&lv_font_montserrat_12,lv_color_hex(0x2a4a6a));
}

// ─── BAND CONDITIONS COLOR ───────────────────────────────────────────────────
lv_color_t band_color(const char *cond) {
    if (strstr(cond,"xcellent")) return C_GREEN;
    if (strstr(cond,"ood"))      return C_BLUE;
    if (strstr(cond,"air"))      return C_YELLOW;
    if (strstr(cond,"oor"))      return C_ORANGE;
    return C_DIM;
}

lv_color_t xray_color(const char *xr) {
    if (xr[0]=='X') return C_RED;
    if (xr[0]=='M') return C_ORANGE;
    if (xr[0]=='C') return C_YELLOW;
    return C_GREEN;
}

lv_color_t kindex_color(int k) {
    if (k>=5) return C_RED;
    if (k>=3) return C_YELLOW;
    return C_GREEN;
}

// ─── BANDS SCREEN ───────────────────────────────────────────────────────────
void build_bands_screen() {
    scr_bands=lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_bands,C_BG,0);
    lv_obj_set_style_bg_opa(scr_bands,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(scr_bands,0,0);

    // TOP BAR
    lv_obj_t *tb=mk_cont(scr_bands,0,0,800,50,lv_color_hex(0x0a1525),false);
    mk_cont(tb,0,49,800,1,C_BORDER,false);
    lv_obj_t *btn=lv_btn_create(tb);
    lv_obj_set_pos(btn,8,8); lv_obj_set_size(btn,80,32);
    lv_obj_set_style_bg_color(btn,lv_color_hex(0x1a3a5a),0);
    lv_obj_set_style_bg_color(btn,lv_color_hex(0x2a4a70),LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn,C_BLUE,0);
    lv_obj_set_style_border_width(btn,1,0);
    lv_obj_set_style_radius(btn,4,0);
    lv_obj_set_style_shadow_width(btn,0,0);
    lv_obj_add_event_cb(btn,[](lv_event_t *e){
        lv_scr_load_anim(scr_monitor,LV_SCR_LOAD_ANIM_MOVE_RIGHT,250,0,false);
    },LV_EVENT_CLICKED,NULL);
    lv_obj_t *bl=lv_label_create(btn);
    lv_label_set_text(bl,"< BACK");
    lv_obj_set_style_text_font(bl,&lv_font_montserrat_12,0);
    lv_obj_set_style_text_color(bl,C_TEXT,0);
    lv_obj_center(bl);
    mk_lbl(tb,"BAND CONDITIONS",290,4,&lv_font_montserrat_16,C_TEXT);
    lbl_bands_updated=mk_lbl(tb,"hamqsl.com",310,28,&lv_font_montserrat_12,C_DIM);
    lbl_bands_local=mk_lbl(tb,"00:00:00 LOC",95,18,&lv_font_montserrat_14,C_BLUE);
    lbl_bands_utc  =mk_lbl(tb,"00:00:00 UTC",570,18,&lv_font_montserrat_14,C_BLUE);
    mk_lbl(tb,"W2LPC  FN20in09",570,4,&lv_font_montserrat_12,C_DIM);

    // SOLAR INDEX CARDS  y=52 h=90  4 cards
    int cy=52,ch=90,cpad=4;
    int cw4=(800-cpad*5)/4;  // ~194px each

    // SFI
    lv_obj_t *c1=mk_cont(scr_bands,cpad,cy,cw4,ch,C_CARD,true);
    mk_lbl(c1,"SOLAR FLUX INDEX",8,6,&lv_font_montserrat_12,C_DIM);
    lbl_sfi=mk_lbl(c1,"---",cw4/2-20,22,&lv_font_montserrat_20,C_TEXT);
    lbl_sfi_sub=mk_lbl(c1,"---",8,72,&lv_font_montserrat_12,C_GREEN);

    // A-INDEX
    lv_obj_t *c2=mk_cont(scr_bands,cpad*2+cw4,cy,cw4,ch,C_CARD,true);
    mk_lbl(c2,"A-INDEX",8,6,&lv_font_montserrat_12,C_DIM);
    lbl_ai=mk_lbl(c2,"--",cw4/2-12,22,&lv_font_montserrat_20,C_GREEN);
    lbl_ai_sub=mk_lbl(c2,"QUIET",8,72,&lv_font_montserrat_12,C_GREEN);

    // K-INDEX
    lv_obj_t *c3=mk_cont(scr_bands,cpad*3+cw4*2,cy,cw4,ch,C_CARD,true);
    mk_lbl(c3,"K-INDEX",8,6,&lv_font_montserrat_12,C_DIM);
    lbl_ki=mk_lbl(c3,"--",cw4/2-12,22,&lv_font_montserrat_20,C_GREEN);
    lbl_ki_sub=mk_lbl(c3,"NO STORM",8,72,&lv_font_montserrat_12,C_GREEN);

    // X-RAY
    lv_obj_t *c4=mk_cont(scr_bands,cpad*4+cw4*3,cy,cw4,ch,C_CARD,true);
    mk_lbl(c4,"X-RAY",8,6,&lv_font_montserrat_12,C_DIM);
    lbl_xr=mk_lbl(c4,"---",cw4/2-20,22,&lv_font_montserrat_20,C_GREEN);
    lbl_xr_sub=mk_lbl(c4,"NORMAL",8,72,&lv_font_montserrat_12,C_GREEN);

    // BAND TABLE  y=146
    lv_obj_t *tbl=mk_cont(scr_bands,0,146,800,290,C_CARD,true);

    // Header row
    mk_cont(tbl,0,0,800,1,C_BORDER,false);
    mk_lbl(tbl,"BAND",  8,6,&lv_font_montserrat_12,C_DIM);
    mk_lbl(tbl,"DAY",   200,6,&lv_font_montserrat_12,C_DIM);
    mk_lbl(tbl,"NIGHT", 380,6,&lv_font_montserrat_12,C_DIM);
    mk_lbl(tbl,"STATUS",580,6,&lv_font_montserrat_12,C_DIM);
    mk_cont(tbl,0,24,800,1,C_BORDER,false);

    const char *bands[]={"80m","40m","20m","15m","10m","6m"};
    for (int i=0;i<6;i++) {
        int ry=28+i*42;
        // band name big
        mk_lbl(tbl,bands[i],8,ry+8,&lv_font_montserrat_20,C_TEXT);
        // day condition
        lbl_band_day[i]=mk_lbl(tbl,"---",200,ry+10,&lv_font_montserrat_16,C_DIM);
        // night condition
        lbl_band_night[i]=mk_lbl(tbl,"---",380,ry+10,&lv_font_montserrat_16,C_DIM);
        // status bar visual
        lbl_band_num[i]=mk_lbl(tbl,"",580,ry+10,&lv_font_montserrat_14,C_DIM);
        // divider
        if (i<5) mk_cont(tbl,0,ry+38,800,1,C_BORDER,false);
    }

    // FOOTER
    lv_obj_t *ft=mk_cont(scr_bands,0,440,800,40,C_DARK,false);
    mk_cont(ft,0,0,800,1,C_BORDER,false);
    mk_lbl(ft,"COMPUTER GEEKS OF AMERICA  |  W2LPC  |  LPC Systems LLC",
           180,13,&lv_font_montserrat_12,lv_color_hex(0x2a4a6a));
}

// ─── XML HELPER ─────────────────────────────────────────────────────────────
String xml_val(const String &xml, const char *tag) {
    String open = String("<") + tag + ">";
    String close= String("</") + tag + ">";
    int s=xml.indexOf(open);
    if (s<0) return "---";
    s+=strlen(tag)+2;
    int e=xml.indexOf(close,s);
    if (e<0) return "---";
    return xml.substring(s,e);
}

String xml_band(const String &xml, const char *name, const char *time) {
    String search = String("<band name=\"") + name + "\" time=\"" + time + "\">";
    int s=xml.indexOf(search);
    if (s<0) return "---";
    s+=search.length();
    int e=xml.indexOf("</band>",s);
    if (e<0) return "---";
    return xml.substring(s,e);
}

// ─── POLL BANDS ─────────────────────────────────────────────────────────────
void poll_bands() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://www.hamqsl.com/solarxml.php");
    http.setTimeout(8000);
    int code=http.GET();
    if (code!=200) { http.end(); return; }
    String xml=http.getString();
    http.end();

    // Solar indices
    String sfi=xml_val(xml,"solarflux");
    String ai =xml_val(xml,"aindex");
    String ki =xml_val(xml,"kindex");
    String xr =xml_val(xml,"xray");
    String knt=xml_val(xml,"kindexnt");

    lv_label_set_text(lbl_sfi, sfi.c_str());
    int sfiv=sfi.toInt();
    if      (sfiv>=150) { lv_label_set_text(lbl_sfi_sub,"EXCELLENT"); lv_obj_set_style_text_color(lbl_sfi_sub,C_GREEN,0); }
    else if (sfiv>=120) { lv_label_set_text(lbl_sfi_sub,"GOOD");      lv_obj_set_style_text_color(lbl_sfi_sub,C_BLUE,0); }
    else if (sfiv>=90)  { lv_label_set_text(lbl_sfi_sub,"FAIR");      lv_obj_set_style_text_color(lbl_sfi_sub,C_YELLOW,0); }
    else                { lv_label_set_text(lbl_sfi_sub,"POOR");      lv_obj_set_style_text_color(lbl_sfi_sub,C_ORANGE,0); }

    lv_label_set_text(lbl_ai, ai.c_str());
    int aiv=ai.toInt();
    lv_color_t ac = (aiv<=7)?C_GREEN:(aiv<=15)?C_YELLOW:(aiv<=29)?C_ORANGE:C_RED;
    lv_obj_set_style_text_color(lbl_ai,ac,0);
    if      (aiv<=7)  { lv_label_set_text(lbl_ai_sub,"QUIET");   lv_obj_set_style_text_color(lbl_ai_sub,C_GREEN,0); }
    else if (aiv<=15) { lv_label_set_text(lbl_ai_sub,"UNSETTLED"); lv_obj_set_style_text_color(lbl_ai_sub,C_YELLOW,0); }
    else if (aiv<=29) { lv_label_set_text(lbl_ai_sub,"ACTIVE");  lv_obj_set_style_text_color(lbl_ai_sub,C_ORANGE,0); }
    else              { lv_label_set_text(lbl_ai_sub,"STORM");   lv_obj_set_style_text_color(lbl_ai_sub,C_RED,0); }

    lv_label_set_text(lbl_ki, ki.c_str());
    int kiv=ki.toInt();
    lv_color_t kc=kindex_color(kiv);
    lv_obj_set_style_text_color(lbl_ki,kc,0);
    char knt2[24]; strncpy(knt2,knt.c_str(),23); knt2[23]=0;
    for(int i=0;knt2[i];i++) knt2[i]=toupper(knt2[i]);
    lv_label_set_text(lbl_ki_sub,knt2);
    lv_obj_set_style_text_color(lbl_ki_sub,kc,0);

    lv_label_set_text(lbl_xr, xr.c_str());
    lv_color_t xc=xray_color(xr.c_str());
    lv_obj_set_style_text_color(lbl_xr,xc,0);
    if      (xr[0]=='X') { lv_label_set_text(lbl_xr_sub,"MAJOR FLARE"); lv_obj_set_style_text_color(lbl_xr_sub,C_RED,0); }
    else if (xr[0]=='M') { lv_label_set_text(lbl_xr_sub,"MOD FLARE");   lv_obj_set_style_text_color(lbl_xr_sub,C_ORANGE,0); }
    else if (xr[0]=='C') { lv_label_set_text(lbl_xr_sub,"MINOR FLARE"); lv_obj_set_style_text_color(lbl_xr_sub,C_YELLOW,0); }
    else                 { lv_label_set_text(lbl_xr_sub,"NORMAL");       lv_obj_set_style_text_color(lbl_xr_sub,C_GREEN,0); }

    // Band conditions
    const char *bnames[]={"80m-40m","80m-40m","30m-20m","17m-15m","12m-10m","6m"};
    for (int i=0;i<6;i++) {
        String day  =xml_band(xml,bnames[i],"day");
        String night=xml_band(xml,bnames[i],"night");
        char d2[16],n2[16];
        strncpy(d2,day.c_str(),15); d2[15]=0;
        strncpy(n2,night.c_str(),15); n2[15]=0;
        for(int j=0;d2[j];j++) d2[j]=toupper(d2[j]);
        for(int j=0;n2[j];j++) n2[j]=toupper(n2[j]);
        lv_label_set_text(lbl_band_day[i],d2);
        lv_label_set_text(lbl_band_night[i],n2);
        lv_obj_set_style_text_color(lbl_band_day[i],band_color(day.c_str()),0);
        lv_obj_set_style_text_color(lbl_band_night[i],band_color(night.c_str()),0);
        // status summary
        bool dx_worthy=(sfiv>=120 && kiv<=2);
        lv_label_set_text(lbl_band_num[i], dx_worthy?"DX WORTHY":"");
        lv_obj_set_style_text_color(lbl_band_num[i],C_GREEN,0);
    }

    // Updated timestamp
    struct tm ti;
    if (getLocalTime(&ti)) {
        char buf[32];
        snprintf(buf,sizeof(buf),"hamqsl.com  %02d:%02d",ti.tm_hour,ti.tm_min);
        lv_label_set_text(lbl_bands_updated,buf);
    }

    lv_timer_handler();
}

// ─── WEATHER ────────────────────────────────────────────────────────────────
void poll_weather() {
    if (strlen(OWM_KEY)==0) return;
    HTTPClient http;
    String url="http://api.openweathermap.org/data/2.5/weather?q=South+Plainfield,US&appid=";
    url+=OWM_KEY; url+="&units=imperial";
    http.begin(url); http.setTimeout(5000);
    if (http.GET()==200) {
        JsonDocument doc;
        if (!deserializeJson(doc,http.getString())) {
            float temp=doc["main"]["temp"]|0.0f;
            const char *desc=doc["weather"][0]["description"]|"---";
            char ud[28]; strncpy(ud,desc,27); ud[27]=0;
            for (int i=0;ud[i];i++) ud[i]=toupper(ud[i]);
            char wb[40]; snprintf(wb,sizeof(wb),"%.0fF  %s",temp,ud);
            lv_label_set_text(lbl_weather,wb);
        }
    }
    http.end();
}

// ─── UPDATE MONITOR ─────────────────────────────────────────────────────────
void update_display(JsonDocument &doc) {
    char b[40];

    struct tm ti;
    if (getLocalTime(&ti)) {
        const char *D[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
        const char *M[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
        snprintf(b,sizeof(b),"%s %s %02d %04d",D[ti.tm_wday],M[ti.tm_mon],ti.tm_mday,ti.tm_year+1900);
        lv_label_set_text(lbl_date,b);
        snprintf(b,sizeof(b),"%02d:%02d:%02d",ti.tm_hour,ti.tm_min,ti.tm_sec);
        lv_label_set_text(lbl_time_top,b);
    }
    const char *up=doc["uptime"]|"---";
    snprintf(b,sizeof(b),"UP %s",up);
    lv_label_set_text(lbl_uptime_top,b);

    float cpu=doc["cpu_pct"]|0.0f;
    snprintf(b,sizeof(b),"%.0f%%",cpu); lv_label_set_text(lbl_cpu_val,b);
    float ct=doc["cpu_temp"]|0.0f;
    snprintf(b,sizeof(b),"TEMP\n%.0fC",ct); lv_label_set_text(lbl_cpu_temp,b);
    JsonArray cores=doc["cpu_cores"];
    int nc=(int)cores.size();
    for (int i=0;i<12&&i<nc;i++)
        lv_bar_set_value(core_bars[i],(int)(cores[i]|0.0f),LV_ANIM_OFF);

    float ru=doc["ram_used"]|0.0f,rf=doc["ram_free"]|0.0f,rp=doc["ram_pct"]|0.0f;
    snprintf(b,sizeof(b),"%.0f\nGB",ru); lv_label_set_text(lbl_ring_val,b);
    lv_arc_set_value(ring_ram,(int)rp);
    snprintf(b,sizeof(b),"%.1f GB",ru); lv_label_set_text(lbl_ram_used,b);
    snprintf(b,sizeof(b),"%.1f GB",rf); lv_label_set_text(lbl_ram_free,b);
    lv_bar_set_value(bar_ram,(int)rp,LV_ANIM_ON);
    snprintf(b,sizeof(b),"%.0f%% utilized",rp); lv_label_set_text(lbl_ram_pct,b);

    float gp=doc["gpu_pct"]|0.0f,gt=doc["gpu_temp"]|0.0f;
    float vu=doc["vram_used"]|0.0f,vt=doc["vram_total"]|0.0f;
    snprintf(b,sizeof(b),"%.0f%%",gp); lv_label_set_text(lbl_gpu_val,b);
    snprintf(b,sizeof(b),"TEMP\n%.0fC",gt); lv_label_set_text(lbl_gpu_temp,b);
    lv_bar_set_value(bar_vram,vt>0?(int)(vu/vt*100):0,LV_ANIM_ON);
    snprintf(b,sizeof(b),"%.1f / %.1f GB",vu,vt); lv_label_set_text(lbl_vram,b);

    float dr=doc["disk_read"]|0.0f,dw=doc["disk_write"]|0.0f;
    snprintf(b,sizeof(b),"%.0f",dr); lv_label_set_text(lbl_dr,b);
    snprintf(b,sizeof(b),"%.0f",dw); lv_label_set_text(lbl_dw,b);
    int drv=(int)dr; if(drv>300)drv=300;
    int dwv=(int)dw; if(dwv>300)dwv=300;
    lv_bar_set_value(bar_dr,drv,LV_ANIM_ON);
    lv_bar_set_value(bar_dw,dwv,LV_ANIM_ON);
    lv_chart_set_next_value(chart_disk,ser_disk,drv);

    float nu=doc["net_up"]|0.0f,nd=doc["net_down"]|0.0f;
    snprintf(b,sizeof(b),"%.1f",nu); lv_label_set_text(lbl_nu,b);
    snprintf(b,sizeof(b),"%.1f",nd); lv_label_set_text(lbl_nd,b);
    int nuv=(int)(nu*10); if(nuv>100)nuv=100;
    int ndv=(int)(nd*10); if(ndv>100)ndv=100;
    lv_bar_set_value(bar_nu,nuv,LV_ANIM_ON);
    lv_bar_set_value(bar_nd,ndv,LV_ANIM_ON);
    lv_chart_set_next_value(chart_net,ser_net,(int)(nd*2));

    JsonArray procs=doc["processes"];
    int np=(int)procs.size();
    for (int i=0;i<4;i++) {
        if (i<np) {
            const char *nm=procs[i]["name"]|"---";
            int pid=procs[i]["pid"]|0;
            float pc=procs[i]["cpu"]|0.0f;
            int pr=procs[i]["ram"]|0;
            const char *st=procs[i]["status"]|"---";
            char nm2[28]; strncpy(nm2,nm,27); nm2[27]=0;
            lv_label_set_text(p_nm[i],nm2);
            snprintf(b,sizeof(b),"%d",pid); lv_label_set_text(p_pid[i],b);
            snprintf(b,sizeof(b),"%.1f%%",pc); lv_label_set_text(p_cpu[i],b);
            if(pr>=1024) snprintf(b,sizeof(b),"%.1f GB",pr/1024.0f);
            else         snprintf(b,sizeof(b),"%d MB",pr);
            lv_label_set_text(p_ram[i],b);
            char st2[12]; strncpy(st2,st,11); st2[11]=0;
            for(int j=0;st2[j];j++) st2[j]=toupper(st2[j]);
            lv_label_set_text(p_st[i],st2);
        }
    }
}

// ─── POLL STATS ─────────────────────────────────────────────────────────────
void poll_stats() {
    HTTPClient http;
    String url=String("http://")+DAEMON_IP+":"+DAEMON_PORT+"/stats";
    http.begin(url); http.setTimeout(1500);
    if (http.GET()==200) {
        JsonDocument doc;
        if (!deserializeJson(doc,http.getString()))
            update_display(doc);
    }
    http.end();
}

// ─── SETUP ──────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(19,20);
    delay(100);

    Out.reset();
    Out.setMode(IO_OUTPUT);
    Out.setState(IO0, IO_LOW);
    Out.setState(IO1, IO_LOW);
    delay(20);
    Out.setState(IO0, IO_HIGH);
    delay(100);
    Out.setMode(IO1, IO_INPUT);
    delay(200);

    buf1=(lv_color_t*)ps_malloc(800*20*sizeof(lv_color_t));
    buf2=(lv_color_t*)ps_malloc(800*20*sizeof(lv_color_t));
    if (!buf1||!buf2) { Serial.println("buf fail"); while(1); }

    tft.begin();
    tft.setBrightness(255);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf,buf1,buf2,800*20);
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res=800;
    disp_drv.ver_res=480;
    disp_drv.flush_cb=my_disp_flush;
    disp_drv.draw_buf=&draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    WiFi.begin(WIFI_SSID,WIFI_PASS);
    for (int i=0;WiFi.status()!=WL_CONNECTED&&i<20;i++) delay(500);
    if (WiFi.status()==WL_CONNECTED)
        configTime(-4*3600,0,"pool.ntp.org");

    build_monitor_screen();
    build_bands_screen();
    lv_scr_load(scr_monitor);

    poll_weather();
    poll_stats();
    poll_bands();
    t_stats=t_weather=t_bands=millis();
}

// ─── LOOP ───────────────────────────────────────────────────────────────────
void loop() {
    lv_timer_handler();
    unsigned long now=millis();
    if (now-t_stats  >=IV_STATS)   { poll_stats();   t_stats=now; }
    if (now-t_weather>=IV_WEATHER) { poll_weather(); t_weather=now; }
    if (now-t_bands  >=IV_BANDS)   { poll_bands();   t_bands=now; }
    static unsigned long t_clk=0;
    if (now-t_clk>=1000) {
        struct tm ti;
        if (getLocalTime(&ti)) {
            const char *D[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
            const char *M[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
            char db[24],tb2[12];
            snprintf(db,sizeof(db),"%s %s %02d %04d",D[ti.tm_wday],M[ti.tm_mon],ti.tm_mday,ti.tm_year+1900);
            snprintf(tb2,sizeof(tb2),"%02d:%02d:%02d",ti.tm_hour,ti.tm_min,ti.tm_sec);
            lv_label_set_text(lbl_date,db);
            lv_label_set_text(lbl_time_top,tb2);
            if (lbl_bands_local && lbl_bands_utc) {
                char loc[16]; snprintf(loc,sizeof(loc),"%02d:%02d:%02d LOC",ti.tm_hour,ti.tm_min,ti.tm_sec);
                time_t tnow=time(NULL); struct tm tiu; gmtime_r(&tnow,&tiu);
                char utcs[16]; snprintf(utcs,sizeof(utcs),"%02d:%02d:%02d UTC",tiu.tm_hour,tiu.tm_min,tiu.tm_sec);
                lv_label_set_text(lbl_bands_local,loc);
                lv_label_set_text(lbl_bands_utc,utcs);
            }
        }
        t_clk=now;
    }
    delay(5);
}
