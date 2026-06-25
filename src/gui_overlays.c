#include "gui_internal.h"
#include "process.h"
#include "shell.h"
#include "vfs.h"

static int32_t calc_clamp_i64(int64_t v);
static int32_t calc_apply_op(int32_t a, int32_t b, char op);
static int spot_weather(const char *q, int qlen, char *out, int out_max);
static int spot_define(const char *q, int qlen, char *out, int out_max);
static int spot_unit_convert(const char *q, int qlen, char *out, int out_max);

static char s_textedit_clipboard[TEXTEDIT_MAXCHARS];
static int  s_textedit_clip_len;

static void overlay_append_text(char *buf, int *pos, int max, const char *text) {
    int i = 0;
    if (!buf || max <= 0) return;
    while (text && text[i] && *pos + 1 < max) {
        buf[*pos] = text[i];
        (*pos)++;
        i++;
    }
    buf[*pos] = 0;
}
static void status_set_runtime_about(void) {
    runtime_system_info_t sys;
    int pos = 0;
    runtime_get_system_info(&sys);
    g_status[0] = 0;
    overlay_append_text(g_status, &pos, sizeof(g_status), sys.sysname);
    overlay_append_text(g_status, &pos, sizeof(g_status), " ");
    overlay_append_text(g_status, &pos, sizeof(g_status), sys.release);
    overlay_append_text(g_status, &pos, sizeof(g_status), " ");
    overlay_append_text(g_status, &pos, sizeof(g_status), sys.machine);
}

void stage_manager_draw(void) {
    if (!g_stage_manager) return;
    int sm_w = 110;
    int sm_x = 0;
    int sm_y = MENUBAR_H;
    int sm_h = VGA_HEIGHT - MENUBAR_H - 24;
    /* Semi-transparent strip background */
    uint32_t sm_bg = g_pref_darkmode ? RGB(30,30,34) : RGB(220,220,226);
    vga_fill_rect_alpha(sm_x, sm_y, sm_w, sm_h, sm_bg, 230);
    vga_draw_vline(sm_x+sm_w-1, sm_y, sm_h, g_pref_darkmode?RGB(55,55,60):RGB(190,190,195));
    /* Label */
    uint32_t sm_lbl = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
    vga_draw_string_trans(sm_x+8, sm_y+6, "Stage", sm_lbl);
    vga_draw_string_trans(sm_x+4, sm_y+16, "Manager", sm_lbl);
    vga_draw_hline(sm_x, sm_y+28, sm_w-1, g_pref_darkmode?RGB(55,55,60):RGB(200,200,205));
    /* Thumbnail for each visible window */
    int ty = sm_y + 34;
    int i2;
    int top_visible = win_top_visible();
    uint32_t sm_txt = g_pref_darkmode ? RGB(200,200,208) : RGB(40,40,48);
    uint32_t sm_act = RGB(0,122,255);
    for (i2=0; i2<g_num_windows && ty+66 < sm_y+sm_h; i2++) {
        gui_window_t *w2 = &g_windows[i2];
        if (!w2->visible || !w2->title) continue;
        int is_active = (i2 == top_visible);
        /* Thumbnail frame */
        uint32_t fr_c = is_active ? sm_act : (g_pref_darkmode?RGB(55,55,62):RGB(210,210,216));
        vga_fill_rect(sm_x+6, ty, sm_w-12, 54, g_pref_darkmode?RGB(22,22,26):RGB(240,240,244));
        vga_draw_rect_outline(sm_x+6, ty, sm_w-12, 54, fr_c);
        /* Mini titlebar in thumbnail */
        uint32_t tb_c = is_active ? RGB(0,80,200) : (g_pref_darkmode?RGB(40,40,44):RGB(220,220,224));
        vga_fill_rect(sm_x+7, ty+1, sm_w-14, 10, tb_c);
        /* Traffic dots */
        vga_fill_rect(sm_x+9,  ty+3, 4, 4, RGB(255,95,86));
        vga_fill_rect(sm_x+15, ty+3, 4, 4, RGB(255,189,46));
        vga_fill_rect(sm_x+21, ty+3, 4, 4, RGB(39,201,63));
        /* Title text abbreviated */
        vga_draw_string_trans(sm_x+8, ty+14, w2->title, g_pref_darkmode?RGB(180,180,188):RGB(50,50,60));
        /* Content squiggle lines suggesting window content */
        int li;
        for (li=0; li<3; li++) {
            uint32_t lc = g_pref_darkmode?RGB(50,50,55):RGB(210,210,215);
            vga_draw_hline(sm_x+10, ty+26+li*8, sm_w-20, lc);
        }
        /* Active indicator ring */
        if (is_active) {
            gui_draw_rounded_rect_outline(sm_x+5, ty-1, sm_w-10, 56, 3, sm_act);
        }
        /* App name below thumbnail */
        { int nl = str_len(w2->title);
          if (nl > 12) nl = 12;
          int nx2 = sm_x + (sm_w - nl*8)/2;
          vga_draw_string_trans(nx2, ty+55, w2->title, is_active?sm_act:sm_txt);
        }
        ty += 74;
    }
    /* "+" Add button at bottom */
    int btn_y = sm_y + sm_h - 26;
    vga_fill_rect_alpha(sm_x+30, btn_y, 48, 18, g_pref_darkmode?RGB(55,55,62):RGB(200,200,210), 240);
    gui_draw_rounded_rect_outline(sm_x+30, btn_y, 48, 18, 4, g_pref_darkmode?RGB(80,80,88):RGB(160,160,170));
    vga_draw_string_trans(sm_x+48, btn_y+5, "+", g_pref_darkmode?RGB(200,200,208):RGB(60,60,70));
}

void cc_draw(void) {
    int cx = VGA_WIDTH - CC_W - 4;
    int cy = MENUBAR_H + 2;
    uint32_t bg_panel = g_pref_darkmode ? RGB(28,28,32) : RGB(238,238,242);
    uint32_t tile_on  = g_pref_darkmode ? RGB(50,52,60) : RGB(255,255,255);
    uint32_t tile_off = g_pref_darkmode ? RGB(38,40,46) : RGB(228,228,232);
    uint32_t txt_col  = g_pref_darkmode ? RGB(230,230,240) : RGB(30,30,36);
    uint32_t sub_col  = g_pref_darkmode ? RGB(130,130,145) : RGB(95,95,105);

    /* Drop shadow */
    vga_fill_rect_alpha(cx+5, cy+5, CC_W, CC_H, RGB(0,0,0), 70);
    /* Panel background with frosted glass */
    vga_fill_rect_alpha(cx, cy, CC_W, CC_H, bg_panel, 248);
    gui_draw_rounded_rect_outline(cx, cy, CC_W, CC_H, 10, g_pref_darkmode?RGB(70,70,80):RGB(190,190,195));

    /* --- SECTION 1: Connectivity tiles (WiFi + BT side by side) --- */
    int pad = 10;
    int tile_gap = 6;
    int half_w = (CC_W - pad*2 - tile_gap) / 2;
    int ry = cy + pad;

    /* WiFi tile */
    {
        uint32_t tbg = g_pref_wifi ? tile_on : tile_off;
        uint32_t tft = g_pref_wifi ? RGB(255,255,255) : sub_col;
        gui_draw_rounded_rect(cx+pad, ry, half_w, 56, 8, tbg);
        /* WiFi arc icon */
        int ix = cx+pad+14, iy = ry+18;
        gui_draw_circle(ix, iy, 11, RGB(160,160,160));
        gui_draw_circle(ix, iy,  7, tbg);
        gui_draw_circle(ix, iy,  3, RGB(140,140,140));
        vga_draw_string_trans(cx+pad+6, ry+32, "WiFi", tft);
        vga_draw_string_trans(cx+pad+6, ry+42, g_pref_wifi ? "On" : "Off", tft);
    }
    /* Bluetooth tile */
    {
        uint32_t tbg = g_pref_bt ? tile_on : tile_off;
        uint32_t tft = g_pref_bt ? RGB(255,255,255) : sub_col;
        int bx = cx+pad+half_w+tile_gap;
        gui_draw_rounded_rect(bx, ry, half_w, 56, 8, tbg);
        /* BT icon: simple B-shape */
        int bix = bx+14, biy = ry+14;
        vga_fill_rect(bix, biy, 3, 28, RGB(160,160,160));
        vga_fill_rect(bix, biy+6, 8, 2, RGB(160,160,160));
        vga_fill_rect(bix, biy+14, 8, 2, RGB(160,160,160));
        vga_fill_rect(bix, biy+8, 10, 6, RGB(150,150,150));
        vga_draw_string_trans(bx+6, ry+32, "Bluetooth", tft);
        vga_draw_string_trans(bx+6, ry+42, g_pref_bt ? "On" : "Off", tft);
    }
    ry += 62;

    /* --- SECTION 2: AirDrop + Focus Mode tiles --- */
    {
        uint32_t tbg = tile_off;
        gui_draw_rounded_rect(cx+pad, ry, half_w, 40, 8, tbg);
        /* AirDrop radar icon */
        int aix = cx+pad+12, aiy = ry+12;
        gui_draw_circle(aix, aiy, 12, RGB(30,160,240));
        gui_draw_circle(aix, aiy,  8, tbg);
        gui_draw_circle(aix, aiy,  3, RGB(30,160,240));
        vga_draw_string_trans(cx+pad+28, ry+8, "AirDrop", txt_col);
        vga_draw_string_trans(cx+pad+28, ry+20, "Contacts", sub_col);
    }
    {
        uint32_t tbg = g_pref_dnd ? RGB(100,80,220) : tile_off;
        uint32_t tft = g_pref_dnd ? RGB(255,255,255) : txt_col;
        int fx = cx+pad+half_w+tile_gap;
        gui_draw_rounded_rect(fx, ry, half_w, 40, 8, tbg);
        /* Moon icon for Focus */
        gui_draw_circle(fx+14, ry+14, 10, g_pref_dnd?RGB(200,180,255):sub_col);
        gui_draw_circle(fx+18, ry+10,  9, tbg);
        vga_draw_string_trans(fx+28, ry+8, "Focus", tft);
        vga_draw_string_trans(fx+28, ry+20, g_pref_dnd?"DND On":"Off", sub_col);
    }
    ry += 46;

    /* --- SECTION 3: Stage Manager + Dark Mode side by side --- */
    {
        uint32_t tbg = g_stage_manager ? (g_pref_darkmode?RGB(50,100,210):RGB(0,100,220)) : tile_off;
        uint32_t tft = g_stage_manager ? RGB(255,255,255) : txt_col;
        gui_draw_rounded_rect(cx+pad, ry, half_w, 40, 8, tbg);
        /* Stage manager icon: overlapping rects */
        int six = cx+pad+8, siy = ry+8;
        vga_fill_rect(six, siy, 10, 24, g_stage_manager?RGB(180,210,255):sub_col);
        vga_fill_rect(six+14, siy+4, 16, 18, g_stage_manager?RGB(220,235,255):sub_col);
        vga_draw_string_trans(cx+pad+6, ry+32, "Stage Mgr", tft);
    }
    {
        uint32_t tbg = g_pref_darkmode ? RGB(50,50,55) : tile_off;
        uint32_t tft = txt_col;
        int dmx = cx+pad+half_w+tile_gap;
        gui_draw_rounded_rect(dmx, ry, half_w, 40, 8, tbg);
        /* Moon for dark mode */
        gui_draw_circle(dmx+14, ry+14, 10, g_pref_darkmode?RGB(255,220,100):sub_col);
        gui_draw_circle(dmx+18, ry+10,  9, tbg);
        /* Stars */
        if (g_pref_darkmode) {
            vga_fill_rect(dmx+22, ry+8, 2, 2, RGB(255,255,200));
            vga_fill_rect(dmx+26, ry+14, 2, 2, RGB(255,255,200));
        }
        vga_draw_string_trans(dmx+6, ry+8, "Appearance", tft);
        vga_draw_string_trans(dmx+6, ry+20, g_pref_darkmode?"Dark":"Light", sub_col);
    }
    ry += 46;

    /* --- SECTION 4: Night Shift + Notifications --- */
    {
        uint32_t tbg = g_night_shift ? RGB(200,120,20) : tile_off;
        uint32_t tft = g_night_shift ? RGB(255,255,255) : txt_col;
        gui_draw_rounded_rect(cx+pad, ry, half_w, 40, 8, tbg);
        /* Sun icon */
        int nix = cx+pad+14, niy = ry+14;
        gui_draw_circle(nix, niy, 8, g_night_shift?RGB(255,200,80):sub_col);
        int rr; for(rr=0;rr<4;rr++){
            vga_fill_rect(nix+(rr%2==0?-13:nix+5-nix), niy+(rr==0?-1:rr==3?-1:rr==1?-13:5), 4, 4, g_night_shift?RGB(255,220,120):sub_col);
        }
        vga_draw_string_trans(cx+pad+6, ry+28, "Night Shift", tft);
    }
    {
        uint32_t tbg = g_pref_notifs ? tile_off : RGB(200,60,60);
        uint32_t tft = g_pref_notifs ? txt_col : RGB(255,255,255);
        int nx2 = cx+pad+half_w+tile_gap;
        gui_draw_rounded_rect(nx2, ry, half_w, 40, 8, tbg);
        /* Bell icon */
        int bx2 = nx2+14, by2 = ry+10;
        gui_draw_circle(bx2, by2+8, 8, g_pref_notifs?sub_col:RGB(255,180,180));
        vga_fill_rect(bx2-8, by2+8, 16, 6, g_pref_notifs?sub_col:RGB(255,180,180));
        vga_fill_rect(bx2-3, by2-2, 6, 4, g_pref_notifs?sub_col:RGB(255,180,180));
        vga_draw_string_trans(nx2+6, ry+8, "Notifications", tft);
        vga_draw_string_trans(nx2+6, ry+28, g_pref_notifs?"On":"Off", sub_col);
    }
    ry += 46;

    /* --- SECTION 5: Brightness slider tile --- */
    {
        gui_draw_rounded_rect(cx+pad, ry, CC_W-pad*2, 44, 8, tile_on);
        /* Sun icon */
        int bix = cx+pad+12, biy = ry+14;
        gui_draw_circle(bix, biy, 7, RGB(255,220,30));
        vga_draw_hline(bix-11, biy, 4, RGB(255,220,30));
        vga_draw_hline(bix+8, biy, 4, RGB(255,220,30));
        vga_draw_vline(bix, biy-11, 4, RGB(255,220,30));
        vga_draw_vline(bix, biy+8, 4, RGB(255,220,30));
        /* Slider */
        int slx = cx+pad+26, sly = ry+13;
        int sl_w = CC_W-pad*2-36;
        int fill = sl_w * g_cc_brightness / 100;
        vga_fill_rect(slx, sly, sl_w, 8, g_pref_darkmode?RGB(80,80,90):RGB(200,200,208));
        vga_fill_rect(slx, sly, fill, 8, RGB(255,220,30));
        gui_draw_circle(slx+fill, sly+4, 8, RGB(255,255,255));
        gui_draw_circle_outline(slx+fill, sly+4, 8, RGB(180,180,190));
        vga_draw_string_trans(cx+pad+26, ry+30, "Brightness", sub_col);
        char bpct[5]; int bv=g_cc_brightness;
        bpct[0]='0'+bv/100; bpct[1]='0'+(bv%100)/10; bpct[2]='0'+bv%10; bpct[3]='%'; bpct[4]=0;
        if(bpct[0]=='0'&&bpct[1]=='0') {bpct[0]=bpct[2];bpct[1]='%';bpct[2]=0;}
        else if(bpct[0]=='0') {bpct[0]=bpct[1];bpct[1]=bpct[2];bpct[2]=bpct[3];bpct[3]=0;}
        vga_draw_string_trans(cx+CC_W-pad-32, ry+30, bpct, sub_col);
    }
    ry += 50;

    /* --- SECTION 6: Volume slider tile --- */
    {
        gui_draw_rounded_rect(cx+pad, ry, CC_W-pad*2, 44, 8, tile_on);
        /* Speaker icon */
        int spx = cx+pad+10, spy = ry+10;
        vga_fill_rect(spx, spy+4, 6, 10, RGB(100,160,255));
        vga_fill_rect(spx+5, spy, 4, 18, RGB(100,160,255));
        /* Sound waves */
        if (g_cc_volume > 0) {
            gui_draw_circle_outline(spx+14, spy+9, 5, RGB(100,160,255));
        }
        if (g_cc_volume > 40) {
            gui_draw_circle_outline(spx+14, spy+9, 9, RGB(80,140,220));
        }
        /* Slider */
        int slx = cx+pad+28, sly = ry+13;
        int sl_w = CC_W-pad*2-38;
        int fill = sl_w * g_cc_volume / 100;
        vga_fill_rect(slx, sly, sl_w, 8, g_pref_darkmode?RGB(80,80,90):RGB(200,200,208));
        vga_fill_rect(slx, sly, fill, 8, RGB(100,160,255));
        gui_draw_circle(slx+fill, sly+4, 8, RGB(255,255,255));
        gui_draw_circle_outline(slx+fill, sly+4, 8, RGB(180,180,190));
        vga_draw_string_trans(cx+pad+28, ry+30, "Volume", sub_col);
        char vpct[5]; int vv=g_cc_volume;
        vpct[0]='0'+vv/100; vpct[1]='0'+(vv%100)/10; vpct[2]='0'+vv%10; vpct[3]='%'; vpct[4]=0;
        if(vpct[0]=='0'&&vpct[1]=='0') {vpct[0]=vpct[2];vpct[1]='%';vpct[2]=0;}
        else if(vpct[0]=='0') {vpct[0]=vpct[1];vpct[1]=vpct[2];vpct[2]=vpct[3];vpct[3]=0;}
        vga_draw_string_trans(cx+CC_W-pad-32, ry+30, vpct, sub_col);
    }
    ry += 50;

    /* --- SECTION 7: Siri button --- */
    {
        uint32_t siri_bg = g_siri_visible ? RGB(100,80,220) : (g_pref_darkmode?RGB(70,60,120):RGB(200,190,235));
        gui_draw_rounded_rect(cx+pad, ry, CC_W-pad*2, 28, 8, siri_bg);
        uint32_t siri_tc = g_siri_visible ? RGB(255,255,255) : (g_pref_darkmode?RGB(200,180,255):RGB(80,60,180));
        int scx2 = cx + CC_W/2;
        /* Siri orb dot */
        gui_draw_circle(scx2-20, ry+14, 5, RGB(100,140,255));
        gui_draw_circle(scx2-12, ry+14, 5, RGB(100,200,255));
        gui_draw_circle(scx2-4,  ry+14, 5, RGB(160,100,255));
        gui_draw_circle(scx2+4,  ry+14, 5, RGB(255,100,200));
        vga_draw_string_trans(scx2+12, ry+8, "Siri", siri_tc);
    }
    ry += 36;

    /* --- SECTION 8: Now Playing card --- */
    {
        uint32_t np_bg = g_pref_darkmode ? RGB(38,38,45) : RGB(255,255,255);
        uint32_t np_txt = g_pref_darkmode ? RGB(220,220,230) : RGB(30,30,40);
        uint32_t np_sub = g_pref_darkmode ? RGB(120,120,130) : RGB(100,100,115);
        gui_draw_rounded_rect(cx+pad, ry, CC_W-pad*2, 52, 8, np_bg);
        /* Album art mini square */
        int am_x = cx+pad+6, am_y = ry+6, am_sz = 40;
        { int ay2;
          for(ay2=0;ay2<am_sz;ay2++)
              vga_draw_hline(am_x, am_y+ay2, am_sz,
                  RGB((uint8_t)(220-ay2*80/am_sz),(uint8_t)(60+ay2*60/am_sz),(uint8_t)(100+ay2*100/am_sz)));
        }
        gui_draw_rounded_rect_outline(am_x, am_y, am_sz, am_sz, 4, RGB(80,80,100));
        /* Track info */
        static const char *np_songs[] = {"Midnight Drive","Neon Pulse","Starfall","Cyberwave","Solar Wind"};
        static const char *np_artists[] = {"Synthwave Dreams","Neon Horizon","StarlightFM","CyberBeats","SolarAudio"};
        const char *ns = np_songs[g_music_track % 5];
        const char *na = np_artists[g_music_track % 5];
        vga_draw_string_trans(cx+pad+52, ry+8, ns, np_txt);
        vga_draw_string_trans(cx+pad+52, ry+20, na, np_sub);
        /* Mini progress bar */
        { uint32_t t_np = timer_ticks();
          int track_s = 227;
          int pos_s = g_music_playing ? (int)((t_np/1000)%(uint32_t)track_s) : 83;
          int bar_w = CC_W-pad*2-58-pad;
          int fill_w = bar_w * pos_s / track_s;
          vga_fill_rect(cx+pad+52, ry+32, bar_w, 3, g_pref_darkmode?RGB(60,60,68):RGB(200,200,210));
          vga_fill_rect(cx+pad+52, ry+32, fill_w, 3, RGB(252,60,68));
        }
        /* Play/Pause indicator */
        if (g_music_playing) {
            vga_fill_rect(cx+CC_W-pad-22, ry+14, 5, 14, np_sub);
            vga_fill_rect(cx+CC_W-pad-14, ry+14, 5, 14, np_sub);
        } else {
            int pi5; for(pi5=0;pi5<12;pi5++) vga_draw_hline(cx+CC_W-pad-22, ry+15+pi5, pi5/2+2, np_sub);
        }
    }
}

int cc_click(int mx, int my) {
    if (!g_cc_visible) return 0;
    int cx = VGA_WIDTH - CC_W - 4;
    int cy = MENUBAR_H + 2;
    if (mx < cx || mx > cx+CC_W || my < cy || my > cy+CC_H) {
        g_cc_visible = 0; return 1;
    }
    int pad = 10, tile_gap = 6;
    int half_w = (CC_W - pad*2 - tile_gap) / 2;
    int ry = cy + pad;
    /* Row 1: WiFi (left) + BT (right), h=56 */
    if (my >= ry && my < ry+56) {
        if (mx >= cx+pad && mx < cx+pad+half_w)
            { g_pref_wifi ^= 1; toast_show("Wi-Fi", g_pref_wifi ? "On" : "Off", RGB(0,122,255)); return 1; }
        if (mx >= cx+pad+half_w+tile_gap && mx < cx+pad+half_w*2+tile_gap)
            { g_pref_bt ^= 1; toast_show("Bluetooth", g_pref_bt ? "On" : "Off", RGB(0,122,255)); return 1; }
    }
    ry += 62;
    /* Row 2: AirDrop (left) + Focus (right), h=40 */
    if (my >= ry && my < ry+40) {
        if (mx >= cx+pad+half_w+tile_gap && mx < cx+pad+half_w*2+tile_gap)
            { g_pref_dnd ^= 1; toast_show("Focus", g_pref_dnd?"DND On":"Focus Off", RGB(100,80,220)); return 1; }
    }
    ry += 46;
    /* Row 3: Stage Manager (left) + Dark Mode (right), h=40 */
    if (my >= ry && my < ry+40) {
        if (mx >= cx+pad && mx < cx+pad+half_w)
            { g_stage_manager ^= 1; toast_show("Stage Manager", g_stage_manager?"On":"Off", RGB(0,100,220)); return 1; }
        if (mx >= cx+pad+half_w+tile_gap && mx < cx+pad+half_w*2+tile_gap)
            { g_pref_darkmode ^= 1; toast_show("Appearance", g_pref_darkmode?"Dark Mode":"Light Mode", RGB(50,50,55)); return 1; }
    }
    ry += 46;
    /* Row 4: Night Shift (left) + Notifications (right), h=40 */
    if (my >= ry && my < ry+40) {
        if (mx >= cx+pad && mx < cx+pad+half_w)
            { g_night_shift ^= 1; toast_show("Night Shift", g_night_shift?"On":"Off", RGB(200,120,20)); return 1; }
        if (mx >= cx+pad+half_w+tile_gap && mx < cx+pad+half_w*2+tile_gap)
            { g_pref_notifs ^= 1; toast_show("Notifications", g_pref_notifs?"On":"Off", RGB(200,60,60)); return 1; }
    }
    ry += 46;
    /* Row 5: Brightness slider, h=44 */
    if (my >= ry && my < ry+44) {
        int slx = cx+pad+26, sl_w = CC_W-pad*2-36;
        if (mx >= slx && mx <= slx+sl_w) {
            g_cc_brightness = (mx-slx)*100/sl_w;
            if (g_cc_brightness < 0) g_cc_brightness = 0;
            if (g_cc_brightness > 100) g_cc_brightness = 100;
            return 1;
        }
    }
    ry += 50;
    /* Row 6: Volume slider, h=44 */
    if (my >= ry && my < ry+44) {
        int slx = cx+pad+28, sl_w = CC_W-pad*2-38;
        if (mx >= slx && mx <= slx+sl_w) {
            g_cc_volume = (mx-slx)*100/sl_w;
            if (g_cc_volume < 0) g_cc_volume = 0;
            if (g_cc_volume > 100) g_cc_volume = 100;
            return 1;
        }
    }
    ry += 50;
    /* Row 7: Siri button, h=28 */
    if (my >= ry && my < ry+28) {
        g_siri_visible ^= 1;
        if (g_siri_visible) {
            g_siri_birth=timer_ticks(); g_siri_qlen=0; g_siri_query[0]=0; g_siri_response=0;
            toast_show("Siri","Listening...",RGB(100,80,220));
        }
        return 1;
    }
    ry += 36;
    /* Row 8: Now Playing card, h=52 */
    if (my >= ry && my < ry+52) {
        /* Click play/pause button area */
        if (mx >= cx+CC_W-pad-26 && mx < cx+CC_W-pad) {
            g_music_playing ^= 1;
            toast_show("Music", g_music_playing?"Now Playing":"Paused", RGB(252,60,68));
        } else {
            /* Click anywhere else: open Music */
            g_cc_visible = 0;
            toast_show("Music","Opening Music...",RGB(252,60,68));
        }
        return 1;
    }
    return 1;
}

char g_nc_msgs[NC_MAX_ENTRIES][48];
char g_nc_subs[NC_MAX_ENTRIES][48];
uint32_t g_nc_colors[NC_MAX_ENTRIES];
int g_nc_count = 0;

static void nc_copy(char *dst, const char *src) {
    int i = 0;
    if (!dst) return;
    while (src && src[i] && i < 47) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void nc_add(const char *msg, const char *sub, uint32_t color) {
    int i;
    if (g_nc_count < NC_MAX_ENTRIES) {
        int idx = g_nc_count++;
        nc_copy(g_nc_msgs[idx], msg);
        nc_copy(g_nc_subs[idx], sub);
        g_nc_colors[idx] = color;
    } else {
        /* Shift entries up, discard oldest */
        for (i = 0; i < NC_MAX_ENTRIES - 1; i++) {
            nc_copy(g_nc_msgs[i], g_nc_msgs[i+1]);
            nc_copy(g_nc_subs[i], g_nc_subs[i+1]);
            g_nc_colors[i] = g_nc_colors[i+1];
        }
        nc_copy(g_nc_msgs[NC_MAX_ENTRIES-1], msg);
        nc_copy(g_nc_subs[NC_MAX_ENTRIES-1], sub);
        g_nc_colors[NC_MAX_ENTRIES-1] = color;
    }
}

void nc_draw(void) {
    int nx = VGA_WIDTH - NC_W;
    int ny = MENUBAR_H;
    int nh = VGA_HEIGHT - MENUBAR_H - DOCK_H;
    uint32_t nc_hdr  = g_pref_darkmode ? RGB(44,44,48)    : RGB(210,210,215);
    uint32_t nc_sep  = g_pref_darkmode ? RGB(60,60,65)    : RGB(180,180,185);
    uint32_t nc_txt  = g_pref_darkmode ? RGB(220,220,228) : RGB(50,50,55);
    uint32_t nc_sub  = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
    uint32_t nc_card = g_pref_darkmode ? RGB(44,44,48)    : RGB(255,255,255);
    uint32_t nc_cbd  = g_pref_darkmode ? RGB(60,60,65)    : RGB(210,210,215);
    /* Panel background */
    vga_fill_rect_alpha(nx, ny, NC_W, nh, g_pref_darkmode?RGB(20,20,24):RGB(235,235,238), 240);
    vga_draw_vline(nx, ny, nh, nc_sep);
    /* Header */
    vga_fill_rect(nx, ny, NC_W, 28, nc_hdr);
    vga_draw_string_trans(nx + 8, ny + 10, "Notification Center", nc_txt);
    vga_draw_hline(nx, ny+28, NC_W, nc_sep);

    /* Weather widget card */
    int wy2 = ny + 34;
    gui_draw_rounded_rect(nx+6, wy2, NC_W-12, 80, 8, RGB(30,120,220));
    /* Weather icon: sun */
    gui_draw_circle(nx+30, wy2+24, 12, RGB(255,220,0));
    /* Sun rays */
    {
        int ri;
        for (ri=0; ri<8; ri++) {
            /* Simple cross + diagonal rays approximation */
        }
    }
    vga_draw_hline(nx+14, wy2+24, 8, RGB(255,220,0));
    vga_draw_hline(nx+38, wy2+24, 8, RGB(255,220,0));
    vga_draw_vline(nx+30, wy2+8,  8, RGB(255,220,0));
    vga_draw_vline(nx+30, wy2+32, 8, RGB(255,220,0));
    /* Temperature */
    {
        runtime_weather_info_t weather;
        char tempbuf[12];
        runtime_get_weather_info(&weather);
        runtime_format_temperature_c(weather.temperature_c, tempbuf, sizeof(tempbuf));
        vga_draw_string_trans(nx+56, wy2+8,  tempbuf, RGB(255,255,255));
        vga_draw_string_trans(nx+56, wy2+22, weather.condition, RGB(200,230,255));
        vga_draw_string_trans(nx+8,  wy2+44, weather.location_full, RGB(180,210,255));
    }
    /* 3-day mini forecast */
    {
        runtime_weather_info_t weather3;
        datetime_t now3;
        int fi;
        runtime_get_weather_info(&weather3);
        get_current_datetime(&now3);
        for (fi=0; fi<3; fi++) {
            char ftemp[12];
            int fx = nx+8+fi*70;
            runtime_format_temperature_c(weather3.forecast[fi].temp_c, ftemp, sizeof(ftemp));
            vga_draw_string_trans(fx, wy2+60,
                                  datetime_weekday_short((now3.weekday + fi) % 7),
                                  RGB(200,230,255));
            vga_draw_string_trans(fx+24, wy2+60, ftemp, RGB(255,255,255));
        }
    }

    /* Mini calendar card */
    int cal_y = wy2 + 86;
    gui_draw_rounded_rect(nx+6, cal_y, NC_W-12, 72, 6, nc_card);
    vga_draw_rect_outline(nx+6, cal_y, NC_W-12, 72, nc_cbd);
    /* Month header */
    vga_fill_rect(nx+6, cal_y, NC_W-12, 18, RGB(255,59,48));
    { datetime_t now_cal;
      char monthbuf[32];
      get_current_datetime(&now_cal);
      get_month_year_str(now_cal.year, now_cal.month, monthbuf);
      vga_draw_string_trans(nx+NC_W/2-str_len(monthbuf)*4, cal_y+5, monthbuf, RGB(255,255,255));
    }
    /* Day labels */
    { static const char *dyl[]={"Su","Mo","Tu","We","Th","Fr","Sa"};
      int di2;
      for(di2=0;di2<7;di2++)
          vga_draw_string_trans(nx+8+di2*28, cal_y+22, dyl[di2], nc_sub);
    }
    /* Week row containing today */
    { int di2;
      datetime_t now_cal;
      int week_start;
      int dim;
      get_current_datetime(&now_cal);
      week_start = now_cal.day - now_cal.weekday;
      dim = datetime_days_in_month(now_cal.year, now_cal.month);
      for(di2=0;di2<7;di2++) {
          int dx2=nx+8+di2*28, dy2=cal_y+38;
          int day = week_start + di2;
          char dd[4];
          if(day < 1 || day > dim) continue;
          int_to_str(day, dd);
          if(day == now_cal.day) {
              gui_draw_circle(dx2+7, dy2+4, 9, RGB(255,59,48));
              vga_draw_string_trans(dx2+3, dy2, dd, RGB(255,255,255));
          } else {
              vga_draw_string_trans(dx2+3, dy2, dd, di2==0||di2==6?RGB(200,60,60):nc_txt);
          }
      }
    }
    /* Time indicator */
    { char clk3[12]; get_clock_str(clk3);
      vga_draw_string_trans(nx+8, cal_y+56, clk3, nc_txt); }

    /* TODAY section header */
    int sy = wy2 + 86 + 78;
    vga_draw_string_trans(nx+8, sy, "TODAY", nc_sub);
    vga_draw_hline(nx+6, sy+12, NC_W-12, nc_sep);
    sy += 16;

    /* --- Messages group card --- */
    {
        int cw2=NC_W-12, ch2=62;
        gui_draw_rounded_rect(nx+6, sy, cw2, ch2, 6, nc_card);
        vga_draw_rect_outline(nx+6, sy, cw2, ch2, nc_cbd);
        /* App icon */
        gui_draw_rounded_rect(nx+10, sy+4, 18, 18, 5, RGB(52,199,89));
        vga_draw_string_trans(nx+14, sy+10, "M", RGB(255,255,255));
        vga_draw_string_trans(nx+32, sy+5, "Messages", nc_txt);
        { char agebuf[16]; runtime_format_relative_time(120, agebuf, sizeof(agebuf));
          vga_draw_string_trans(nx+NC_W-44, sy+5, agebuf, nc_sub); }
        vga_draw_hline(nx+8, sy+24, cw2-4, nc_cbd);
        /* Msg 1 */
        vga_draw_string_trans(nx+10, sy+28, "Jane Kim:", nc_txt);
        vga_draw_string_trans(nx+10, sy+40, "Are you free for lunch tmrw?", nc_sub);
        /* Msg 2 preview */
        vga_draw_string_trans(nx+10, sy+52, "+1 more message", nc_sub);
        sy += ch2 + 4;
    }

    /* --- Mail group card --- */
    {
        int cw2=NC_W-12, ch2=62;
        gui_draw_rounded_rect(nx+6, sy, cw2, ch2, 6, nc_card);
        vga_draw_rect_outline(nx+6, sy, cw2, ch2, nc_cbd);
        gui_draw_rounded_rect(nx+10, sy+4, 18, 18, 5, RGB(0,140,255));
        vga_draw_string_trans(nx+14, sy+10, "@", RGB(255,255,255));
        vga_draw_string_trans(nx+32, sy+5, "Mail", nc_txt);
        { char agebuf[16]; runtime_format_relative_time(900, agebuf, sizeof(agebuf));
          vga_draw_string_trans(nx+NC_W-44, sy+5, agebuf, nc_sub); }
        vga_draw_hline(nx+8, sy+24, cw2-4, nc_cbd);
        vga_draw_string_trans(nx+10, sy+28, "GitHub: PR approved", nc_txt);
        vga_draw_string_trans(nx+10, sy+40, "Your PR #247 was approved", nc_sub);
        vga_draw_string_trans(nx+10, sy+52, "+2 more emails", nc_sub);
        sy += ch2 + 4;
    }

    /* --- Calendar reminder card --- */
    {
        int cw2=NC_W-12, ch2=48;
        gui_draw_rounded_rect(nx+6, sy, cw2, ch2, 6, nc_card);
        vga_draw_rect_outline(nx+6, sy, cw2, ch2, nc_cbd);
        gui_draw_rounded_rect(nx+10, sy+4, 18, 18, 5, RGB(255,59,48));
        { datetime_t cal_now; char daybuf[4]; get_current_datetime(&cal_now);
          int_to_str(cal_now.day, daybuf);
          vga_draw_string_trans(nx+14, sy+10, daybuf, RGB(255,255,255)); }
        vga_draw_string_trans(nx+32, sy+5, "Calendar", nc_txt);
        { char agebuf[16]; runtime_format_relative_time(3600, agebuf, sizeof(agebuf));
          vga_draw_string_trans(nx+NC_W-44, sy+5, agebuf, nc_sub); }
        vga_draw_hline(nx+8, sy+24, cw2-4, nc_cbd);
        vga_draw_string_trans(nx+10, sy+28, "Team Standup", nc_txt);
        vga_draw_string_trans(nx+10, sy+38, "Starting soon  *  Conference Room", nc_sub);
        sy += ch2 + 4;
    }

    /* --- App Store card --- */
    {
        if (sy + 44 < ny + nh - 26) {
            int cw2=NC_W-12, ch2=44;
            gui_draw_rounded_rect(nx+6, sy, cw2, ch2, 6, nc_card);
            vga_draw_rect_outline(nx+6, sy, cw2, ch2, nc_cbd);
            gui_draw_rounded_rect(nx+10, sy+4, 18, 18, 5, RGB(30,120,255));
            vga_draw_string_trans(nx+14, sy+10, "A", RGB(255,255,255));
            vga_draw_string_trans(nx+32, sy+5, "App Store", nc_txt);
            { char agebuf[16]; runtime_format_relative_time(3600, agebuf, sizeof(agebuf));
              vga_draw_string_trans(nx+NC_W-44, sy+5, agebuf, nc_sub); }
            vga_draw_hline(nx+8, sy+24, cw2-4, nc_cbd);
            vga_draw_string_trans(nx+10, sy+28, "Updates available", nc_sub);
            sy += ch2 + 4;
        }
    }

    /* Show custom notifications if any */
    if (g_nc_count > 0) {
        int i2;
        for (i2=0; i2<g_nc_count; i2++) {
            int ey2 = sy + i2*44;
            if (ey2+42 > ny+nh-26) break;
            gui_draw_rounded_rect(nx+6, ey2, NC_W-12, 42, 6, nc_card);
            vga_draw_rect_outline(nx+6, ey2, NC_W-12, 42, nc_cbd);
            vga_fill_rect(nx+6, ey2, 4, 42, g_nc_colors[i2]);
            vga_draw_string_trans(nx+14, ey2+6, g_nc_msgs[i2], nc_txt);
            vga_draw_string_trans(nx+14, ey2+18, g_nc_subs[i2], nc_sub);
        }
    }

    /* Clear all button */
    int by = ny + nh - 22;
    gui_draw_rounded_rect(nx+6, by, NC_W-12, 18, 4, g_pref_darkmode?RGB(50,50,56):RGB(200,200,206));
    vga_draw_rect_outline(nx+6, by, NC_W-12, 18, nc_sep);
    vga_draw_string_trans(nx+NC_W/2-36, by+5, "Clear All", nc_txt);
}

int nc_click(int mx, int my) {
    if (!g_nc_visible) return 0;
    int nx = VGA_WIDTH - NC_W;
    if (mx < nx) { g_nc_visible = 0; return 1; }
    /* Clear all button */
    int nh = VGA_HEIGHT - MENUBAR_H - DOCK_H;
    int by = MENUBAR_H + nh - 22;
    if (my >= by && my < by+18 && mx >= nx+6 && mx < VGA_WIDTH-6) {
        g_nc_count = 0; return 1;
    }
    /* Individual dismiss X buttons */
    {
        int ny2 = MENUBAR_H;
        int sy2 = ny2 + 34 + 86 + 78 + 14 + 14; /* weather + calendar + today header */
        int i2;
        for (i2 = 0; i2 < g_nc_count; i2++) {
            int ey = sy2 + i2 * 52;
            if (ey + 50 > ny2 + nh - 24) break;
            int xb = nx + NC_W - 18;
            int yb = ey + 4;
            if (mx >= xb && mx < xb+12 && my >= yb && my < yb+12) {
                /* Remove entry i2, shift down */
                int j2;
                for (j2 = i2; j2 < g_nc_count - 1; j2++) {
                    nc_copy(g_nc_msgs[j2], g_nc_msgs[j2+1]);
                    nc_copy(g_nc_subs[j2], g_nc_subs[j2+1]);
                    g_nc_colors[j2] = g_nc_colors[j2+1];
                }
                g_nc_count--;
                return 1;
            }
        }
    }
    return 1; /* Clicks inside panel are consumed */
}

void mission_control_draw(void) {
    /* Dark muted overlay */
    vga_fill_rect_alpha(0, MENUBAR_H, VGA_WIDTH, VGA_HEIGHT-MENUBAR_H, RGB(20,20,30), 200);

    /* Spaces strip at top */
    {
        int si3;
        int spaces = g_num_spaces;
        if (spaces < 1) spaces = 1;
        if (spaces > 4) spaces = 4;
        int sp_w = VGA_WIDTH / spaces;
        for (si3=1; si3<=spaces; si3++) {
            int sx3 = (si3-1)*sp_w + 4;
            int sy3 = MENUBAR_H + 4;
            int sw3 = sp_w - 8, sh3 = 50;
            /* Space thumbnail background */
            vga_fill_rect_alpha(sx3, sy3, sw3, sh3,
                si3==g_current_space ? RGB(60,100,200) : RGB(40,40,60), 200);
            vga_draw_rect_outline(sx3, sy3, sw3, sh3,
                si3==g_current_space ? RGB(100,150,255) : RGB(80,80,100));
            /* Mini desktop gradient inside */
            { int ry3;
              for(ry3=4;ry3<sh3-4;ry3++) {
                  uint8_t bb3 = (uint8_t)(100+ry3*80/sh3);
                  vga_draw_hline(sx3+2, sy3+ry3, sw3-4, RGB(40,80,bb3));
              }
            }
            /* Space number */
            char sn3[2] = {'0'+(char)si3, 0};
            vga_draw_string_trans(sx3+sw3/2-4, sy3+20, sn3, RGB(255,255,255));
            /* "Space N" label below */
            vga_draw_string_trans(sx3+4, sy3+sh3-10, si3==g_current_space?"Current":"Space",
                                  si3==g_current_space ? RGB(150,200,255) : RGB(130,130,150));
        }
    }
    /* "+" button to add a new space */
    if (g_num_spaces < 4) {
        int plus_x = VGA_WIDTH - 28, plus_y = MENUBAR_H + 4;
        vga_fill_rect_alpha(plus_x, plus_y, 24, 50, RGB(255,255,255), 30);
        gui_draw_rounded_rect_outline(plus_x, plus_y, 24, 50, 6, RGB(180,200,255));
        vga_draw_string_trans(plus_x+8, plus_y+21, "+", RGB(255,255,255));
    }
    vga_draw_string_trans(8, MENUBAR_H+60, "Mission Control", RGB(220,220,220));
    vga_draw_string_trans(8, VGA_HEIGHT-12, "ESC | Click space to switch | Click window to focus", RGB(140,140,140));

    /* Lay out up to 8 visible windows in a row near the top */
    int n_vis = 0, i;
    for (i=0;i<g_num_windows;i++)
        if (g_windows[i].visible) n_vis++;

    if (n_vis == 0) return;

    int mc_top = MENUBAR_H + 70; /* below spaces strip */
    int thumb_h = (VGA_HEIGHT - mc_top - DOCK_H - 40) / 2;
    if (thumb_h < 60) thumb_h = 60;
    int thumb_max_w = (VGA_WIDTH - 24) / (n_vis < 4 ? n_vis : 4);
    if (thumb_max_w > 180) thumb_max_w = 180;
    int thumb_w = thumb_max_w - 8;
    int row = 0, col = 0, vi = 0;
    int top_visible = win_top_visible();

    for (i = 0; i < g_num_windows; i++) {
        const gui_window_t *w = &g_windows[i];
        if (!w->visible) continue;

        /* Position thumbnail */
        if (vi >= 4) { row=1; col = vi-4; }
        else         { row=0; col=vi; }

        int tx = 12 + col * (thumb_w + 8);
        int ty = mc_top + row * (thumb_h + 12);

        /* Thumbnail chrome */
        vga_fill_rect(tx, ty, thumb_w, thumb_h, RGB(248,248,248));
        uint32_t tb_grad = (i==top_visible) ? RGB(210,215,225) : RGB(200,200,205);
        vga_fill_rect(tx, ty, thumb_w, 10, tb_grad);
        /* Mini traffic lights */
        gui_draw_circle(tx+4,  ty+5, 3, RGB(255,95,86));
        gui_draw_circle(tx+11, ty+5, 3, RGB(255,189,46));
        gui_draw_circle(tx+18, ty+5, 3, RGB(39,201,63));
        /* Window title truncated - small font */
        { const char *wt2 = w->title ? w->title : "?";
          int tlen2 = str_len(wt2);
          int max_t = (thumb_w-28)/8;
          if (tlen2 > max_t) tlen2 = max_t;
          int ti2;
          for (ti2=0; ti2<tlen2; ti2++)
              vga_draw_char_trans(tx+24+ti2*8, ty+2, wt2[ti2], RGB(40,40,40));
        }
        /* App-specific mini content */
        int ct_y = ty+11, ct_h = thumb_h-12;
        if (w->title) {
            if (str_eq(w->title,"Terminal") || str_eq(w->title,"Terminal")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(18,18,18));
                int li; for (li=0;li<5&&li*9<ct_h;li++) {
                    int lw2 = 20+li*15; if (lw2>thumb_w-10) lw2=thumb_w-10;
                    vga_fill_rect(tx+6, ct_y+4+li*9, lw2, 3, RGB(0,200,0));
                }
                vga_fill_rect(tx+6, ct_y+4+5*9, 8, 8, RGB(0,255,0));
            } else if (str_eq(w->title,"Notes")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(255,252,190));
                int li; for (li=0;li<4&&li*8<ct_h-8;li++)
                    vga_fill_rect(tx+6, ct_y+6+li*8, thumb_w-14, 2, RGB(220,210,140));
                vga_draw_string_trans(tx+4, ct_y+3, "Shop..", RGB(80,80,60));
            } else if (str_eq(w->title,"Clock")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(245,245,245));
                int cx4=tx+thumb_w/2, cy4=ct_y+ct_h/2, r4=ct_h/3;
                if (r4>thumb_w/3) r4=thumb_w/3;
                gui_draw_circle(cx4, cy4, r4, RGB(245,245,245));
                gui_draw_rounded_rect_outline(cx4-r4, cy4-r4, r4*2, r4*2, r4, RGB(80,80,80));
                vga_draw_line(cx4, cy4, cx4, cy4-r4+4, RGB(30,30,30));
                vga_draw_line(cx4, cy4, cx4+r4/2, cy4+2, RGB(30,30,30));
                vga_draw_line(cx4, cy4, cx4, cy4+4, RGB(200,50,50));
            } else if (str_eq(w->title,"Music")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(24,24,26));
                /* Mini album art */
                vga_fill_rect(tx+thumb_w/2-15, ct_y+4, 30, 30, RGB(200,60,80));
                /* Mini bars */
                static const int mb5[5]={8,12,6,14,10};
                int bi; for (bi=0;bi<5;bi++)
                    vga_fill_rect(tx+5+bi*8, ct_y+ct_h-mb5[bi]-4, 5, mb5[bi], RGB(252,60,68));
            } else if (str_eq(w->title,"Safari")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(255,255,255));
                vga_fill_rect(tx+1, ct_y, thumb_w-2, 8, RGB(0,80,200));
                vga_fill_rect(tx+4, ct_y+10, thumb_w-8, 4, RGB(220,220,220));
                int li; for (li=0;li<4&&li*8<ct_h-20;li++)
                    vga_fill_rect(tx+4, ct_y+16+li*8, thumb_w-12, 3, RGB(200,200,200));
            } else if (str_eq(w->title,"Photos")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(30,30,32));
                vga_fill_rect(tx+3, ct_y+4, (thumb_w-8)/2-1, ct_h/2-2, RGB(40,80,160));
                vga_fill_rect(tx+3+(thumb_w-8)/2+1, ct_y+4, (thumb_w-8)/2-1, ct_h/2-2, RGB(160,60,40));
                vga_fill_rect(tx+3, ct_y+4+ct_h/2, (thumb_w-8)/2-1, ct_h/2-6, RGB(40,140,60));
                vga_fill_rect(tx+3+(thumb_w-8)/2+1, ct_y+4+ct_h/2, (thumb_w-8)/2-1, ct_h/2-6, RGB(140,100,40));
            } else if (str_eq(w->title,"Maps")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(200,230,170));
                vga_fill_rect(tx+1, ct_y, thumb_w/2, ct_h/3, RGB(100,160,230));
                vga_draw_line(tx+4, ct_y+ct_h/2, tx+thumb_w-4, ct_y+ct_h/2, RGB(255,200,0));
                gui_draw_circle(tx+thumb_w/2, ct_y+ct_h/2, 6, RGB(255,59,48));
            } else if (str_eq(w->title,"MyOS Finder") || str_eq(w->title,"Finder")) {
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(255,255,255));
                vga_fill_rect(tx+1, ct_y, thumb_w/4, ct_h, RGB(230,230,235));
                /* Mini folder icons */
                vga_fill_rect(tx+thumb_w/4+4, ct_y+6, 16, 12, RGB(41,128,185));
                vga_fill_rect(tx+thumb_w/4+4, ct_y+22, 16, 12, RGB(52,199,89));
            } else {
                /* Generic */
                uint32_t g_app_c = RGB(200,200,210);
                if (str_eq(w->title,"Mail"))       g_app_c=RGB(0,140,255);
                else if (str_eq(w->title,"Calendar")) g_app_c=RGB(255,59,48);
                else if (str_eq(w->title,"Settings")) g_app_c=RGB(142,142,147);
                vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, g_pref_darkmode?RGB(40,40,44):RGB(248,248,252));
                gui_draw_circle(tx+thumb_w/2, ct_y+ct_h/2, ct_h/4, g_app_c);
            }
        } else {
            vga_fill_rect(tx+1, ct_y, thumb_w-2, ct_h, RGB(230,230,235));
        }
        /* Active window blue border, others thin gray */
        vga_draw_rect_outline(tx, ty, thumb_w, thumb_h,
                              (i==top_visible) ? RGB(0,122,255) : RGB(120,120,130));
        /* App name label below thumbnail */
        if (w->title) {
            int nl2 = str_len(w->title); if (nl2>10) nl2=10;
            vga_draw_string_trans(tx + (thumb_w - nl2*8)/2, ty + thumb_h + 2,
                w->title, RGB(220,220,230));
        }

        vi++;
    }
}

int mission_control_hit(int mx, int my) {
    int n_vis = 0, i;
    for (i=0;i<g_num_windows;i++)
        if (g_windows[i].visible) n_vis++;
    if (n_vis == 0) return -1;
    int mc_top = MENUBAR_H + 70;
    int thumb_h = (VGA_HEIGHT - mc_top - DOCK_H - 40) / 2;
    if (thumb_h < 60) thumb_h = 60;
    int thumb_max_w = (VGA_WIDTH-24) / (n_vis<4?n_vis:4);
    if (thumb_max_w>180) thumb_max_w=180;
    int thumb_w = thumb_max_w - 8;
    int row=0,col=0,vi=0;
    for (i=0;i<g_num_windows;i++) {
        const gui_window_t *w = &g_windows[i];
        if (!w->visible) continue;
        if (vi>=4){row=1;col=vi-4;} else{row=0;col=vi;}
        int tx=12+col*(thumb_w+8), ty=mc_top+row*(thumb_h+12);
        if (mx>=tx&&mx<tx+thumb_w&&my>=ty&&my<ty+thumb_h) return i;
        vi++;
    }
    return -1;
}

/* =========================================================================
 * Spotlight state
 * ======================================================================= */
int  g_spot_visible = 0;
char g_spot_query[SPOT_QUERY_MAX + 1];
int  g_spot_qlen = 0;
int  g_spot_sel = 0; /* selected result index */
/* Search result names (apps + system) */
const char *g_spot_apps[] = {
    "Finder", "Terminal", "TextEdit", "Settings", "Calculator",
    "Clock", "Notes", "Music", "Photos", "Safari", "Maps", "App Store",
    "Calendar", "Mail", "Activity Monitor", "System Info",
    "Weather", "FaceTime", "AirDrop", "Keyboard Shortcuts",
    "Stocks", "News", "Books", "Podcasts", "Reminders", "Home",
    "Messages", "Contacts", "Find My", "Wallet", "Voice Memos", "Freeform",
    "Disk Utility", "Shortcuts", "Time Machine", "Color Picker",
    "Preview", "Script Editor", "Migration Assistant",
    "Screen Time", "Passwords", "Numbers", "Focus",
    "Keynote", "Pages", "GarageBand", "iMovie", "Xcode",
    "GameCenter", "Automator", "Font Book", "Console",
    "iPhone Mirroring", "Instruments", "Network Utility",
    "Math Notes", "Final Cut Pro", "Logic Pro",
    "Motion", "MainStage", "Compressor",
    "Screen Recording", "Sidecar", "Universal Control",
    "Handoff", "Privacy", "Accessibility", "AirPlay",
    "TestFlight", "Reality Composer", "Configurator",
    "Health", "Sudoku",
    "Stickies", "Dictionary", "Chess", "2048", "Grapher", "Wordle", "Snake",
    "Digital Color Meter", "Feedback Assistant", "Photo Booth",
    "SF Symbols", "Transporter", "AR Quick Look",
    "Clips", "Transmit", "Proxyman", "Overflow 3",
    "Lasso", "iStudiez Pro",
    "1Password", "Fantastical", "Things 3", "Raycast",
    "Tot", "Klokki", "Bear", "Reeder 5", "CleanMyMac X",
    "Bartender 4", "Alfred", "Scrobbles",
    "Apple TV", "Minesweeper", "Journal", "Breakout", "Pong", NULL
};

/* Substring case-insensitive match helper */
int spot_substr_match(const char *haystack, const char *needle, int nlen) {
    int hi;
    if (!haystack || !needle || nlen < 0) return 0;
    if (nlen == 0) return 1;
    for (hi = 0; haystack[hi]; hi++) {
        int match = 1, k;
        for (k = 0; k < nlen; k++) {
            char a = needle[k], b = haystack[hi+k];
            if (!b) { match = 0; break; }
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* =========================================================================
 * App Exposé — shows all windows of the focused/target app
 * ======================================================================= */
void app_expose_draw(void) {
    /* Dark overlay */
    vga_fill_rect_alpha(0, MENUBAR_H, VGA_WIDTH, VGA_HEIGHT - MENUBAR_H, RGB(10,10,20), 210);

    /* Title */
    uint32_t ti_col = RGB(220,220,228);
    const char *app_name = (g_expose_app_idx >= 0 && g_expose_app_idx < NUM_DOCK_ICONS)
                           ? s_dock_icons[g_expose_app_idx].name : "All Windows";
    vga_draw_string_trans(VGA_WIDTH/2 - str_len(app_name)*4, MENUBAR_H + 10, app_name, ti_col);
    vga_draw_string_trans(VGA_WIDTH/2 - str_len(app_name)*4+1, MENUBAR_H + 10, app_name, ti_col);

    /* Hint */
    vga_draw_string_trans(VGA_WIDTH/2 - 60, VGA_HEIGHT - 40,
        "ESC | Click to focus", RGB(150,155,170));

    /* Collect matching windows */
    int matches[MAX_WINDOWS], nm = 0;
    int i;
    for (i = 0; i < g_num_windows; i++) {
        if (!g_windows[i].visible) continue;
        if (g_expose_app_idx < 0) {
            matches[nm++] = i;
        } else {
            /* Match by dock icon name vs window title */
            const char *dname = s_dock_icons[g_expose_app_idx].name;
            const char *wt    = g_windows[i].title ? g_windows[i].title : "";
            /* Rough match: window title starts with or equals dock icon name */
            int dl = str_len(dname), wl = str_len(wt), match = 0;
            if (wl >= dl) {
                int mi2; match = 1;
                for (mi2 = 0; mi2 < dl; mi2++)
                    if (dname[mi2] != wt[mi2]) { match = 0; break; }
            }
            if (match) matches[nm++] = i;
        }
        if (nm >= MAX_WINDOWS) break;
    }

    /* If no match for that specific app, show all windows */
    if (nm == 0) {
        for (i = 0; i < g_num_windows; i++)
            if (g_windows[i].visible) matches[nm++] = i;
    }
    if (nm == 0) {
        vga_draw_string_trans(VGA_WIDTH/2 - 40, MENUBAR_H + 72,
            "No windows", RGB(170,175,190));
        return;
    }

    /* Layout thumbnails in a grid */
    int cols_e = (nm <= 2) ? nm : (nm <= 4) ? 2 : 3;
    int rows_e = (nm + cols_e - 1) / cols_e;
    int tw = (VGA_WIDTH - 40) / cols_e - 12;
    int th = (VGA_HEIGHT - MENUBAR_H - 60) / rows_e - 14;
    if (tw < 60) tw = 60;
    if (th < 40) th = 40;
    int grid_x0 = (VGA_WIDTH - cols_e*(tw+12)) / 2;
    int grid_y0 = MENUBAR_H + 30;

    for (i = 0; i < nm; i++) {
        int col_e = i % cols_e, row_e = i / cols_e;
        int tx3 = grid_x0 + col_e * (tw + 12);
        int ty3 = grid_y0 + row_e * (th + 14);

        /* Thumbnail card */
        gui_draw_rounded_rect(tx3, ty3, tw, th, 6,
            g_pref_darkmode ? RGB(40,40,48) : RGB(220,222,230));
        /* Shadow */
        vga_fill_rect_alpha(tx3+4, ty3+4, tw, th, RGB(0,0,0), 60);
        /* Window title bar */
        gui_draw_rounded_rect(tx3, ty3, tw, 12, 6,
            g_pref_darkmode ? RGB(55,55,66) : RGB(200,200,215));
        /* Traffic light dots */
        gui_draw_circle(tx3+6,  ty3+6, 3, RGB(255,59,48));
        gui_draw_circle(tx3+14, ty3+6, 3, RGB(255,149,0));
        gui_draw_circle(tx3+22, ty3+6, 3, RGB(52,199,89));
        /* Window content (colored fill) */
        {
            uint32_t wc2 = g_pref_darkmode ? RGB(28,28,34) : RGB(245,245,250);
            vga_fill_rect(tx3+1, ty3+12, tw-2, th-13, wc2);
        }
        /* Window title text */
        const char *wt2 = g_windows[matches[i]].title ? g_windows[matches[i]].title : "";
        int tl = str_len(wt2); if (tl > 12) tl = 12;
        int tx4 = tx3 + tw/2 - tl*4;
        {
            int ki;
            for (ki = 0; ki < tl; ki++)
                vga_draw_char_trans(tx4 + ki*8, ty3+6, wt2[ki], RGB(240,240,248));
        }
        /* Window content preview lines */
        if (th > 30) {
            uint32_t lc = g_pref_darkmode ? RGB(50,50,60) : RGB(200,200,210);
            vga_draw_hline(tx3+4, ty3+18, tw-8, lc);
            vga_draw_hline(tx3+4, ty3+24, tw-16, lc);
            if (th > 40) vga_draw_hline(tx3+4, ty3+30, tw-20, lc);
        }
    }
}

void spotlight_draw(void) {
    int sx = (VGA_WIDTH - SPOTLIGHT_W) / 2;
    int sy = 80; /* slightly higher than center for macOS feel */

    /* Collect results: apps, docs, settings, contacts */
    static const uint32_t spot_app_cols[] = {
        RGB(41,128,185), RGB(30,30,30),   RGB(255,140,40),  RGB(142,142,147),
        RGB(200,50,50),  RGB(80,80,240),  RGB(255,204,0),   RGB(252,60,68),
        RGB(240,80,160), RGB(40,160,220), RGB(60,200,80),   RGB(30,120,255),
        RGB(255,59,48),  RGB(0,140,255),  RGB(100,50,200),  RGB(80,140,200),
        RGB(30,130,255), RGB(0,200,100),  RGB(0,122,255),   RGB(100,80,180),
        RGB(255,100,40), RGB(80,200,120), RGB(200,80,160),  RGB(60,140,220),
        RGB(180,60,200), RGB(40,180,180), RGB(220,120,40),  RGB(60,60,180)
    };
    /* Results storage: type 0=app, 1=doc, 2=setting, 3=contact, 4=web, 5=calc */
    struct SpotResult {
        const char *name;
        const char *sub;
        uint32_t    icon_col;
        int         type; /* 0=app,1=doc,2=setting,3=contact */
    };
    struct SpotResult results[24];
    int n_results = 0;

    /* Documents */
    static const struct { const char *name; const char *path; } spot_docs[] = {
        { "Resume.pdf",         "~/Desktop" },
        { "Notes.txt",          "~/Documents" },
        { "Project_Plan.doc",   "~/Documents" },
        { "Screenshot.png",     "~/Downloads" },
        { "Budget_2026.xlsx",   "~/Documents" },
        { "Presentation.key",   "~/Desktop" },
        { "Music_Library.m4a",  "~/Music" },
        { "Vacation_Photos.zip","~/Downloads" },
        { NULL, NULL }
    };
    /* Settings shortcuts */
    static const struct { const char *key; const char *name; const char *sub; } spot_settings[] = {
        { "wifi",       "Wi-Fi",            "Network Settings"   },
        { "dark",       "Dark Mode",         "Appearance"         },
        { "brightness", "Brightness",        "Display Settings"   },
        { "bluetooth",  "Bluetooth",         "System Settings"    },
        { "sound",      "Sound",             "System Settings"    },
        { "notif",      "Notifications",     "System Settings"    },
        { "privacy",    "Privacy & Security","System Settings"    },
        { "stage",      "Stage Manager",     "Desktop & Dock"     },
        { "battery",    "Battery",           "System Settings"    },
        { "network",    "Network",           "System Settings"    },
        { "keyboard",   "Keyboard",          "System Settings"    },
        { "focus",      "Focus",             "System Settings"    },
        { NULL, NULL, NULL }
    };
    /* Contacts */
    static const struct { const char *name; const char *info; } spot_contacts[] = {
        { "Alice Johnson",  "alice@email.com"   },
        { "Bob Smith",      "+1 555-0100"       },
        { "Carol White",    "carol@email.com"   },
        { "David Brown",    "+1 555-0142"       },
        { "Emma Davis",     "emma@work.com"     },
        { NULL, NULL }
    };

    if (g_spot_qlen > 0) {
        int ri;
        /* Apps (up to 4) */
        for (ri = 0; g_spot_apps[ri] && n_results < 4; ri++) {
            if (spot_substr_match(g_spot_apps[ri], g_spot_query, g_spot_qlen)) {
                results[n_results].name     = g_spot_apps[ri];
                results[n_results].sub      = "Application";
                results[n_results].icon_col = (ri < 28) ? spot_app_cols[ri] : RGB(80,80,200);
                results[n_results].type     = 0;
                n_results++;
            }
        }
        /* Docs (up to 3, only if qlen >= 2) */
        if (g_spot_qlen >= 2) {
            int di;
            for (di = 0; spot_docs[di].name && n_results < 7; di++) {
                if (spot_substr_match(spot_docs[di].name, g_spot_query, g_spot_qlen)) {
                    results[n_results].name     = spot_docs[di].name;
                    results[n_results].sub      = spot_docs[di].path;
                    results[n_results].icon_col = RGB(41,128,185);
                    results[n_results].type     = 1;
                    n_results++;
                }
            }
        }
        /* Settings */
        if (g_spot_qlen >= 2) {
            int si;
            for (si = 0; spot_settings[si].key && n_results < 10; si++) {
                if (spot_substr_match(spot_settings[si].name, g_spot_query, g_spot_qlen) ||
                    spot_substr_match(spot_settings[si].key,  g_spot_query, g_spot_qlen)) {
                    results[n_results].name     = spot_settings[si].name;
                    results[n_results].sub      = spot_settings[si].sub;
                    results[n_results].icon_col = RGB(142,142,147);
                    results[n_results].type     = 2;
                    n_results++;
                }
            }
        }
        /* Contacts */
        if (g_spot_qlen >= 2) {
            int ci;
            for (ci = 0; spot_contacts[ci].name && n_results < 13; ci++) {
                if (spot_substr_match(spot_contacts[ci].name, g_spot_query, g_spot_qlen)) {
                    results[n_results].name     = spot_contacts[ci].name;
                    results[n_results].sub      = spot_contacts[ci].info;
                    results[n_results].icon_col = RGB(52,199,89);
                    results[n_results].type     = 3;
                    n_results++;
                }
            }
        }
    }

    /* Calculator detection */
    int calc_valid = 0; int calc_res = 0;
    if (g_spot_qlen >= 3) {
        int64_t a2=0, b2=0;
        int has_op=0, oi=0; char op2=0; int ki;
        for (ki=0; ki<g_spot_qlen; ki++) {
            char c = g_spot_query[ki];
            if (!has_op && (c=='+'||c=='-'||c=='*'||c=='/')) { op2=c; has_op=1; oi=ki; break; }
            if (c>='0' && c<='9') {
                a2 = a2 * 10LL + (int64_t)(c-'0');
                if (a2 > 2147483647LL) a2 = 2147483647LL;
            }
        }
        if (has_op) {
            for (ki=oi+1; ki<g_spot_qlen; ki++) {
                char c=g_spot_query[ki];
                if(c>='0'&&c<='9') {
                    b2 = b2 * 10LL + (int64_t)(c-'0');
                    if (b2 > 2147483647LL) b2 = 2147483647LL;
                }
            }
            if (op2 != '/' || b2) {
                calc_res = calc_apply_op(calc_clamp_i64(a2), calc_clamp_i64(b2), op2);
                calc_valid=1;
            }
        }
    }

    /* Unit conversion (e.g. "100 km in miles") */
    int unit_valid = 0;
    char unit_result[48]; unit_result[0] = 0;
    if (!calc_valid && g_spot_qlen >= 5) {
        unit_valid = spot_unit_convert(g_spot_query, g_spot_qlen, unit_result, 47);
    }

    /* Dictionary define (e.g. "define serenity") */
    int dict_valid = 0;
    char dict_result[64]; dict_result[0] = 0;
    if (!calc_valid && !unit_valid && g_spot_qlen >= 4) {
        dict_valid = spot_define(g_spot_query, g_spot_qlen, dict_result, 63);
    }

    /* Weather query for the current runtime location. */
    int wth_valid = 0;
    char wth_result[64]; wth_result[0] = 0;
    if (!calc_valid && !unit_valid && !dict_valid && g_spot_qlen >= 7) {
        wth_valid = spot_weather(g_spot_query, g_spot_qlen, wth_result, 63);
    }

    /* Clamp selection */
    int total_items = n_results;
    if (g_spot_sel < 0) g_spot_sel = 0;
    if (g_spot_sel >= total_items && total_items > 0) g_spot_sel = total_items - 1;
    if (total_items <= 0) g_spot_sel = 0;

    /* Calculate panel height */
    /* Search box + top hit (52px if results) + result rows (28px each) + category headers */
    int panel_extra = 0;
    if (g_spot_qlen == 0) {
        panel_extra = 160; /* Siri suggestions */
    } else if (n_results > 0 || calc_valid) {
        /* Top Hit row: 52, then remaining rows and inline answers. */
        int remaining = (n_results > 1) ? (n_results - 1) : 0;
        panel_extra = 8 + 52 + (remaining > 0 ? 6 + remaining*28 : 0) + (calc_valid ? 28 : 0) + (unit_valid ? 28 : 0) + (dict_valid ? 28 : 0) + (wth_valid ? 36 : 0);
    } else if (wth_valid) {
        panel_extra = 8 + 36; /* weather */
    } else if (unit_valid || dict_valid) {
        panel_extra = 8 + 28; /* conversion/dict */
    }
    int total_h = SPOTLIGHT_H + panel_extra;

    /* Full-screen dim */
    vga_fill_rect_alpha(0, MENUBAR_H, VGA_WIDTH, VGA_HEIGHT-MENUBAR_H, RGB(0,0,0), 110);

    /* Drop shadow */
    vga_fill_rect_alpha(sx+5, sy+5, SPOTLIGHT_W, total_h, RGB(0,0,0), 90);

    /* Main search box background */
    vga_fill_rect_alpha(sx, sy, SPOTLIGHT_W, SPOTLIGHT_H, RGB(248,248,248), 245);

    /* Results panel background */
    if (panel_extra > 0) {
        vga_draw_hline(sx, sy+SPOTLIGHT_H, SPOTLIGHT_W, RGB(210,210,215));
        vga_fill_rect_alpha(sx, sy+SPOTLIGHT_H, SPOTLIGHT_W, panel_extra, RGB(246,246,248), 245);
    }

    /* Rounded border around whole thing */
    gui_draw_rounded_rect_outline(sx, sy, SPOTLIGHT_W, total_h, 10, RGB(195,195,200));

    /* Magnifier icon */
    gui_draw_circle(sx+18, sy+SPOTLIGHT_H/2, 9, RGB(120,120,130));
    gui_draw_circle(sx+18, sy+SPOTLIGHT_H/2, 7, RGB(248,248,248));
    vga_draw_line(sx+23, sy+SPOTLIGHT_H/2+5, sx+27, sy+SPOTLIGHT_H/2+9, RGB(120,120,130));
    vga_draw_line(sx+24, sy+SPOTLIGHT_H/2+5, sx+28, sy+SPOTLIGHT_H/2+9, RGB(120,120,130));

    /* Query text or empty hint */
    if (g_spot_qlen == 0) {
        vga_draw_string_trans(sx+38, sy+(SPOTLIGHT_H-8)/2, "Spotlight Search", RGB(185,185,190));
        /* Siri Suggestions */
        {
            int sug_y = sy + SPOTLIGHT_H + 10;
            vga_draw_string_trans(sx+12, sug_y, "SIRI SUGGESTIONS", RGB(120,120,130));
            static const char *sug_apps[]={"Safari","Music","Mail","Calendar","Maps","Photos","Notes","Settings"};
            static const uint32_t sug_cols[]={RGB(40,160,220),RGB(252,60,68),RGB(0,140,255),RGB(255,59,48),RGB(60,200,80),RGB(240,80,160),RGB(255,204,0),RGB(142,142,147)};
            int si2; int cell_w = (SPOTLIGHT_W-24)/4;
            for (si2=0; si2<8; si2++) {
                int ax3 = sx+12 + (si2%4)*cell_w;
                int ay3 = sug_y+14 + (si2/4)*64;
                gui_draw_rounded_rect(ax3, ay3, 42, 42, 10, sug_cols[si2]);
                vga_fill_rect_alpha(ax3+2, ay3+2, 38, 14, RGB(255,255,255), 35);
                vga_draw_char_trans(ax3+17, ay3+17, sug_apps[si2][0], RGB(255,255,255));
                int nl2 = str_len(sug_apps[si2]); if(nl2>7) nl2=7;
                vga_draw_string_trans(ax3+(42-nl2*8)/2, ay3+46, sug_apps[si2], RGB(60,60,70));
            }
        }
    } else {
        vga_draw_string_trans(sx+38, sy+(SPOTLIGHT_H-8)/2, g_spot_query, RGB(20,20,30));
    }

    /* Blinking cursor */
    {
        uint32_t t = timer_ticks();
        if ((t/500)%2 == 0) {
            int cx2 = sx + 38 + g_spot_qlen * 8;
            vga_fill_rect(cx2, sy+(SPOTLIGHT_H-16)/2, 2, 16, RGB(0,100,220));
        }
    }
    /* ESC hint */
    vga_draw_string_trans(sx+SPOTLIGHT_W-28, sy+(SPOTLIGHT_H-8)/2, "esc", RGB(175,175,180));

    /* Results panel content */
    if (g_spot_qlen > 0) {
        int ry = sy + SPOTLIGHT_H + 8;
        int global_idx = 0; /* for selection highlight tracking */

        if (n_results > 0) {
            /* TOP HIT - large first result */
            {
                int th_h = 52;
                int is_sel = (g_spot_sel == global_idx);
                uint32_t th_bg = is_sel ? RGB(0,122,255) : RGB(235,235,240);
                uint32_t th_txt = is_sel ? RGB(255,255,255) : RGB(20,20,30);
                uint32_t th_sub = is_sel ? RGB(200,225,255) : RGB(100,100,110);
                gui_draw_rounded_rect(sx+4, ry, SPOTLIGHT_W-8, th_h, 8, th_bg);
                /* Big icon */
                gui_draw_rounded_rect(sx+12, ry+6, 40, 40, 10, results[0].icon_col);
                vga_fill_rect_alpha(sx+12, ry+6, 40, 14, RGB(255,255,255), 40);
                vga_draw_char_trans(sx+28, ry+22, results[0].name[0], RGB(255,255,255));
                /* "TOP HIT" badge */
                vga_draw_string_trans(sx+58, ry+8, "TOP HIT", th_sub);
                vga_draw_string_trans(sx+58, ry+20, results[0].name, th_txt);
                vga_draw_string_trans(sx+58, ry+32, results[0].sub, th_sub);
                if (is_sel)
                    vga_draw_string_trans(sx+SPOTLIGHT_W-14, ry+22, ">", th_sub);
                ry += th_h + 6;
                global_idx++;
            }

            /* Remaining results by category */
            if (n_results > 1) {
                /* Draw category separator header for each category group */
                int cur_type = results[1].type;
                static const char *cat_names[] = {"APPLICATIONS","DOCUMENTS","SYSTEM SETTINGS","CONTACTS"};
                vga_draw_string_trans(sx+12, ry, cat_names[cur_type < 4 ? cur_type : 0], RGB(130,130,140));
                ry += 12;
                int ri2;
                for (ri2 = 1; ri2 < n_results; ri2++) {
                    /* Category header if changed */
                    if (ri2 > 1 && results[ri2].type != results[ri2-1].type) {
                        ry += 2;
                        vga_draw_hline(sx+8, ry, SPOTLIGHT_W-16, RGB(220,220,225));
                        ry += 4;
                        vga_draw_string_trans(sx+12, ry, cat_names[results[ri2].type < 4 ? results[ri2].type : 0], RGB(130,130,140));
                        ry += 12;
                    }
                    int is_sel = (g_spot_sel == global_idx);
                    if (is_sel)
                        gui_draw_rounded_rect(sx+4, ry-2, SPOTLIGHT_W-8, 26, 4, RGB(0,122,255));
                    uint32_t itxt = is_sel ? RGB(255,255,255) : RGB(20,20,30);
                    uint32_t isub = is_sel ? RGB(200,225,255) : RGB(130,130,140);
                    /* Icon */
                    gui_draw_rounded_rect(sx+12, ry+1, 20, 20, 5, results[ri2].icon_col);
                    vga_fill_rect_alpha(sx+12, ry+1, 20, 7, RGB(255,255,255), 35);
                    vga_draw_char_trans(sx+16, ry+5, results[ri2].name[0], RGB(255,255,255));
                    vga_draw_string_trans(sx+38, ry+4, results[ri2].name, itxt);
                    vga_draw_string_trans(sx+38, ry+16, results[ri2].sub, isub);
                    if (is_sel) vga_draw_string_trans(sx+SPOTLIGHT_W-14, ry+7, ">", isub);
                    ry += 28;
                    global_idx++;
                }
            }
        }

        /* Calculator row */
        if (calc_valid) {
            int is_sel = (global_idx < total_items && g_spot_sel == global_idx);
            if (is_sel)
                gui_draw_rounded_rect(sx+4, ry-2, SPOTLIGHT_W-8, 30, 4, RGB(0,122,255));
            uint32_t ctxt = is_sel ? RGB(255,255,255) : RGB(20,20,30);
            uint32_t csub = is_sel ? RGB(200,225,255) : RGB(140,140,140);
            if (n_results == 0) {
                vga_draw_string_trans(sx+12, ry-4, "CALCULATOR", RGB(130,130,140));
                ry += 10;
            }
            vga_fill_rect(sx+12, ry+1, 20, 20, RGB(255,149,0));
            vga_draw_string_trans(sx+16, ry+5, "=", RGB(255,255,255));
            char resbuf[14]; int ri3=0; uint32_t rv;
            if(calc_res<0){
                resbuf[ri3++]='-';
                rv=(uint32_t)(-(calc_res + 1)) + 1U;
            } else {
                rv=(uint32_t)calc_res;
            }
            if(rv==0){resbuf[ri3++]='0';}
            else{char tmp2[12];int tl=0;while(rv){tmp2[tl++]=(char)('0'+rv%10U);rv/=10U;}int j3;for(j3=tl-1;j3>=0;j3--)resbuf[ri3++]=tmp2[j3];}
            resbuf[ri3]=0;
            vga_draw_string_trans(sx+38, ry+4, resbuf, ctxt);
            vga_draw_string_trans(sx+38, ry+16, "Calculator", csub);
            ry += 28;
            global_idx++;
        }

        /* Unit Conversion row */
        if (unit_valid) {
            int is_sel = (global_idx < total_items && g_spot_sel == global_idx);
            if (is_sel)
                gui_draw_rounded_rect(sx+4, ry-2, SPOTLIGHT_W-8, 30, 4, RGB(0,122,255));
            uint32_t utxt = is_sel ? RGB(255,255,255) : RGB(20,20,30);
            uint32_t usub = is_sel ? RGB(200,225,255) : RGB(140,140,140);
            if (n_results == 0 && !calc_valid) {
                vga_draw_string_trans(sx+12, ry-4, "CONVERSION", RGB(130,130,140));
                ry += 10;
            } else {
                vga_draw_hline(sx+8, ry-2, SPOTLIGHT_W-16, RGB(215,215,220));
            }
            vga_fill_rect(sx+12, ry+1, 20, 20, RGB(0,190,140));
            vga_draw_string_trans(sx+14, ry+5, "->", RGB(255,255,255));
            vga_draw_string_trans(sx+38, ry+4, unit_result, utxt);
            vga_draw_string_trans(sx+38, ry+16, "Conversion", usub);
            ry += 28;
            global_idx++;
        }

        /* Dictionary row */
        if (dict_valid) {
            int is_sel = (global_idx < total_items && g_spot_sel == global_idx);
            if (is_sel)
                gui_draw_rounded_rect(sx+4, ry-2, SPOTLIGHT_W-8, 30, 4, RGB(0,122,255));
            uint32_t dtxt = is_sel ? RGB(255,255,255) : RGB(20,20,30);
            uint32_t dsub = is_sel ? RGB(200,225,255) : RGB(140,140,140);
            if (n_results == 0 && !calc_valid && !unit_valid) {
                vga_draw_string_trans(sx+12, ry-4, "DICTIONARY", RGB(130,130,140));
                ry += 10;
            } else {
                vga_draw_hline(sx+8, ry-2, SPOTLIGHT_W-16, RGB(215,215,220));
            }
            vga_fill_rect(sx+12, ry+1, 20, 20, RGB(90,50,180));
            vga_draw_string_trans(sx+15, ry+5, "D", RGB(255,255,255));
            vga_draw_string_trans(sx+38, ry+4, dict_result, dtxt);
            vga_draw_string_trans(sx+38, ry+16, "Dictionary", dsub);
            ry += 28;
            global_idx++;
        }

        /* Weather row */
        if (wth_valid) {
            int is_sel = (global_idx < total_items && g_spot_sel == global_idx);
            if (is_sel)
                gui_draw_rounded_rect(sx+4, ry-2, SPOTLIGHT_W-8, 38, 4, RGB(0,122,255));
            uint32_t wtxt = is_sel ? RGB(255,255,255) : RGB(20,20,30);
            uint32_t wsub = is_sel ? RGB(200,225,255) : RGB(140,140,140);
            if (n_results == 0 && !calc_valid && !unit_valid && !dict_valid) {
                vga_draw_string_trans(sx+12, ry-4, "WEATHER", RGB(130,130,140));
                ry += 10;
            } else {
                vga_draw_hline(sx+8, ry-2, SPOTLIGHT_W-16, RGB(215,215,220));
            }
            /* Weather icon: sun/cloud gradient */
            vga_fill_rect(sx+12, ry+1, 28, 28, RGB(50,140,240));
            gui_draw_circle(sx+26, ry+10, 9, RGB(255,220,50));
            gui_draw_circle(sx+30, ry+14, 7, RGB(220,230,250));
            gui_draw_circle(sx+24, ry+16, 6, RGB(220,230,250));
            gui_draw_circle(sx+34, ry+17, 5, RGB(220,230,250));
            vga_draw_string_trans(sx+46, ry+4, wth_result, wtxt);
            vga_draw_string_trans(sx+46, ry+16, "Weather", wsub);
            ry += 36;
            global_idx++;
        }
    }
}

/* =========================================================================
 * Context menu state
 * ======================================================================= */
/* pref state moved up; see near Control Center code above */

/* Draw a macOS-style toggle switch at (x,y), w=36 h=20 */
void draw_toggle(int x, int y, int on) {
    uint32_t bg = on ? RGB(52, 199, 89) : RGB(200, 200, 200);
    /* Track */
    gui_draw_rounded_rect(x, y, 36, 20, 10, bg);
    /* Thumb */
    int tx = on ? x+18 : x+2;
    gui_draw_rounded_rect(tx, y+2, 16, 16, 8, RGB(255,255,255));
}

int g_ctx_visible = 0;
int g_ctx_x = 0, g_ctx_y = 0;
const char *g_ctx_labels[CTX_MENU_ITEMS] = {
    "Get Info", "---",
    "Change Wallpaper", "Sort By Name", "---",
    "Settings...", "Widgets...", "About MyOS"
};
/* Context target: 0=desktop, 1=window */
int g_ctx_type = 0;

void ctx_menu_draw(void) {
    int total_h = CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 4;
    int cx = g_ctx_x, cy = g_ctx_y;
    /* shadow */
    vga_fill_rect_alpha(cx+3, cy+3, CTX_MENU_W, total_h, RGB(0,0,0), 60);
    /* background */
    uint32_t ctx_bg   = g_pref_darkmode ? RGB(50,50,54)    : RGB(245,245,245);
    uint32_t ctx_bd   = g_pref_darkmode ? RGB(70,70,74)    : RGB(180,180,180);
    uint32_t ctx_sep  = g_pref_darkmode ? RGB(70,70,74)    : RGB(190,190,190);
    uint32_t ctx_txt  = g_pref_darkmode ? RGB(220,220,224) : COLOR_TEXT;
    vga_fill_rect(cx, cy, CTX_MENU_W, total_h, ctx_bg);
    vga_draw_rect_outline(cx, cy, CTX_MENU_W, total_h, ctx_bd);
    int i;
    for (i = 0; i < CTX_MENU_ITEMS; i++) {
        int iy = cy + 2 + i * CTX_MENU_ITEM_H;
        if (g_ctx_labels[i][0] == '-') {
            vga_draw_hline(cx+4, iy + CTX_MENU_ITEM_H/2, CTX_MENU_W-8, ctx_sep);
        } else {
            vga_draw_string_trans(cx + 10, iy + (CTX_MENU_ITEM_H-8)/2, g_ctx_labels[i], ctx_txt);
        }
    }
}

/* Returns 0-based item index if (mx,my) is over a menu item, -1 otherwise */
int ctx_menu_hit(int mx, int my) {
    int total_h = CTX_MENU_ITEMS * CTX_MENU_ITEM_H + 4;
    if (!g_ctx_visible) return -1;
    if (mx < g_ctx_x || mx >= g_ctx_x + CTX_MENU_W) return -1;
    if (my < g_ctx_y || my >= g_ctx_y + total_h) return -1;
    int item = (my - g_ctx_y - 2) / CTX_MENU_ITEM_H;
    if (item < 0 || item >= CTX_MENU_ITEMS) return -1;
    if (g_ctx_labels[item][0] == '-') return -1;
    return item;
}

/* =========================================================================
 * Dock right-click context menu (popover above dock icon)
 * ======================================================================= */
int g_dock_ctx_visible = 0;
int g_dock_ctx_icon    = 0;

#define DOCK_CTX_W      160
#define DOCK_CTX_ITEM_H  20

static const char *s_dock_ctx_labels[DOCK_CTX_ITEMS] = {
    "Open",
    "---",
    "Options",
    "Show in Finder",
    "---",
    "Quit"
};

void dock_ctx_draw(void) {
    int di = g_dock_ctx_icon;
    /* Compute icon x center */
    int num_dock = NUM_DOCK_ICONS;
    int total_dw = num_dock * DOCK_ICON_SIZE + (num_dock-1)*DOCK_ICON_PAD + 24 + 24;
    int dix = (VGA_WIDTH - total_dw) / 2;
    int ix = dix;
    int k;
    for (k = 0; k < di; k++) {
        ix += DOCK_ICON_SIZE + DOCK_ICON_PAD;
        if (k == 5 || k == 11 || k == 15) ix += 8;
    }
    int icon_cx = ix + DOCK_ICON_SIZE / 2;

    int pop_w = DOCK_CTX_W;
    int pop_h = DOCK_CTX_ITEMS * DOCK_CTX_ITEM_H + 8;
    int pop_x = icon_cx - pop_w / 2;
    int pop_y = DOCK_Y - pop_h - 8;
    if (pop_x < 2) pop_x = 2;
    if (pop_x + pop_w > VGA_WIDTH - 2) pop_x = VGA_WIDTH - 2 - pop_w;

    uint32_t bg  = g_pref_darkmode ? RGB(46,46,50)    : RGB(248,248,248);
    uint32_t bd  = g_pref_darkmode ? RGB(80,80,86)    : RGB(190,190,195);
    uint32_t sep = g_pref_darkmode ? RGB(70,70,76)    : RGB(200,200,205);
    uint32_t txt = g_pref_darkmode ? RGB(230,230,235) : RGB(20,20,25);
    uint32_t sub = g_pref_darkmode ? RGB(140,140,148) : RGB(110,110,120);

    /* Running indicator dot */
    int is_running = 0;
    int wi;
    const char *aname = (di < NUM_DOCK_ICONS) ? s_dock_icons[di].name : "?";
    const char *wname = str_eq(aname,"Finder") ? "MyOS Finder" : aname;
    for (wi = 0; wi < g_num_windows; wi++) {
        if (g_windows[wi].title && str_eq(g_windows[wi].title, wname)) {
            is_running = 1; break;
        }
    }

    /* Shadow */
    vga_fill_rect_alpha(pop_x+4, pop_y+4, pop_w, pop_h, RGB(0,0,0), 70);
    /* Panel */
    gui_draw_rounded_rect(pop_x, pop_y, pop_w, pop_h, 8, bg);
    vga_draw_rect_outline(pop_x, pop_y, pop_w, pop_h, bd);

    /* App name header */
    vga_fill_rect_alpha(pop_x+1, pop_y+1, pop_w-2, 24, RGB(255,255,255), 15);
    vga_draw_hline(pop_x, pop_y+24, pop_w, sep);
    /* Icon dot + app name */
    gui_draw_circle(pop_x+12, pop_y+12, 5, s_dock_icons[di < NUM_DOCK_ICONS ? di : 0].color);
    {
        char abuf[14]; int ai;
        for (ai=0; ai<13 && aname[ai]; ai++) abuf[ai]=aname[ai];
        abuf[ai]=0;
        vga_draw_string_trans(pop_x+22, pop_y+8, abuf, txt);
    }
    if (is_running)
        gui_draw_circle(pop_x+pop_w-10, pop_y+12, 3, RGB(52,199,89));

    /* Items */
    int item_y = pop_y + 28;
    int mi;
    for (mi = 0; mi < DOCK_CTX_ITEMS; mi++) {
        if (s_dock_ctx_labels[mi][0] == '-') {
            vga_draw_hline(pop_x+6, item_y + DOCK_CTX_ITEM_H/2, pop_w-12, sep);
        } else if (str_eq(s_dock_ctx_labels[mi], "Quit") && !is_running) {
            /* Grey out Quit when app is not running */
            vga_draw_string_trans(pop_x+12, item_y+(DOCK_CTX_ITEM_H-8)/2, s_dock_ctx_labels[mi], sub);
        } else if (str_eq(s_dock_ctx_labels[mi], "Options")) {
            vga_draw_string_trans(pop_x+12, item_y+(DOCK_CTX_ITEM_H-8)/2, s_dock_ctx_labels[mi], txt);
            vga_draw_string_trans(pop_x+pop_w-14, item_y+(DOCK_CTX_ITEM_H-8)/2, ">", sub);
        } else {
            vga_draw_string_trans(pop_x+12, item_y+(DOCK_CTX_ITEM_H-8)/2, s_dock_ctx_labels[mi], txt);
        }
        item_y += DOCK_CTX_ITEM_H;
    }
}

/* Returns item index (0-based) or -1 */
int dock_ctx_hit(int mx, int my) {
    if (!g_dock_ctx_visible) return -1;
    int di = g_dock_ctx_icon;
    int num_dock = NUM_DOCK_ICONS;
    int total_dw = num_dock * DOCK_ICON_SIZE + (num_dock-1)*DOCK_ICON_PAD + 24 + 24;
    int dix = (VGA_WIDTH - total_dw) / 2;
    int ix = dix;
    int k;
    for (k = 0; k < di; k++) {
        ix += DOCK_ICON_SIZE + DOCK_ICON_PAD;
        if (k == 5 || k == 11 || k == 15) ix += 8;
    }
    int icon_cx = ix + DOCK_ICON_SIZE / 2;
    int pop_w = DOCK_CTX_W;
    int pop_h = DOCK_CTX_ITEMS * DOCK_CTX_ITEM_H + 8;
    int pop_x = icon_cx - pop_w / 2;
    int pop_y = DOCK_Y - pop_h - 8;
    if (pop_x < 2) pop_x = 2;
    if (pop_x + pop_w > VGA_WIDTH - 2) pop_x = VGA_WIDTH - 2 - pop_w;
    if (mx < pop_x || mx >= pop_x + pop_w) return -1;
    if (my < pop_y + 28 || my >= pop_y + pop_h) return -1;
    int item = (my - pop_y - 28) / DOCK_CTX_ITEM_H;
    if (item < 0 || item >= DOCK_CTX_ITEMS) return -1;
    if (s_dock_ctx_labels[item][0] == '-') return -1;
    return item;
}

/* =========================================================================
 * Menu bar dropdown state
 * ======================================================================= */
#define MENU_ITEM_H 20
#define MENU_W 140

const char *g_menubar_titles[N_MENUS] = {
    "MyOS", "File", "Edit", "View", "Window", "Help"
};
/* Items per menu — "---" = separator, NULL = end */
const char *g_menu_items[N_MENUS][MAX_MENU_ITEMS] = {
    { "About MyOS", "---", "Settings...", "Mission Control", "Stage Manager", "---", "Lock Screen", "---", "Restart...", "Shut Down...", NULL },
    { "New Window", "Close Window", "---", "Get Info", "---", "Print...", "Share...", NULL },
    { NULL },
    { "Finder", "Terminal", "Calculator", "Clock", "TextEdit", NULL },
    { NULL },
    { "Quick Help", "---", "About MyOS", NULL }
};

int g_menu_open = -1;  /* -1 = none open */

int menubar_item_x(int idx) {
    int nx = 8, i;
    for (i = 0; i < idx; i++)
        nx += str_len(g_menubar_titles[i]) * 8 + 18;
    return nx;
}

int menubar_hit(int mx, int my) {
    if (my < 0 || my >= MENUBAR_H) return -1;
    int nx = 8, i;
    for (i = 0; i < N_MENUS; i++) {
        int iw = str_len(g_menubar_titles[i]) * 8 + 12;
        if (mx >= nx - 4 && mx < nx + iw) return i;
        nx += iw + 6;
    }
    return -1;
}

void draw_dropdown(int menu_idx) {
    if (menu_idx < 0 || menu_idx >= N_MENUS) return;
    int dx = menubar_item_x(menu_idx) - 4;
    int dy = MENUBAR_H;
    /* Count items */
    int n = 0;
    while (n < MAX_MENU_ITEMS && g_menu_items[menu_idx][n]) n++;
    int total_h = n * MENU_ITEM_H + 4;
    /* Clamp right edge */
    if (dx + MENU_W > VGA_WIDTH) dx = VGA_WIDTH - MENU_W - 2;
    /* Shadow */
    vga_fill_rect_alpha(dx+3, dy+3, MENU_W, total_h, RGB(0,0,0), 50);
    /* Background */
    uint32_t mn_bg  = g_pref_darkmode ? RGB(50,50,54)    : RGB(245,245,245);
    uint32_t mn_bd  = g_pref_darkmode ? RGB(70,70,74)    : RGB(180,180,180);
    uint32_t mn_sep = g_pref_darkmode ? RGB(70,70,74)    : RGB(190,190,190);
    uint32_t mn_txt = g_pref_darkmode ? RGB(220,220,224) : COLOR_TEXT;
    vga_fill_rect(dx, dy, MENU_W, total_h, mn_bg);
    vga_draw_rect_outline(dx, dy, MENU_W, total_h, mn_bd);
    /* Hover highlight */
    int hover_item = dropdown_hit(mouse_get_x(), mouse_get_y());
    int i;
    for (i = 0; i < n; i++) {
        int iy = dy + 2 + i * MENU_ITEM_H;
        const char *label = g_menu_items[menu_idx][i];
        if (label[0] == '-') {
            vga_draw_hline(dx+6, iy + MENU_ITEM_H/2, MENU_W-12, mn_sep);
        } else {
            if (i == hover_item)
                vga_fill_rect_alpha(dx+1, iy, MENU_W-2, MENU_ITEM_H, RGB(0,122,255), 100);
            vga_draw_string_trans(dx+12, iy+(MENU_ITEM_H-8)/2, label, mn_txt);
        }
    }
}

/* Returns item index (0-based) for click in open dropdown, -1 if miss */
int dropdown_hit(int mx, int my) {
    if (g_menu_open < 0) return -1;
    int dx = menubar_item_x(g_menu_open) - 4;
    int dy = MENUBAR_H;
    int n = 0;
    while (n < MAX_MENU_ITEMS && g_menu_items[g_menu_open][n]) n++;
    int total_h = n * MENU_ITEM_H + 4;
    if (dx + MENU_W > VGA_WIDTH) dx = VGA_WIDTH - MENU_W - 2;
    if (mx < dx || mx >= dx + MENU_W || my < dy || my >= dy + total_h) return -1;
    int item = (my - dy - 2) / MENU_ITEM_H;
    if (item < 0 || item >= n) return -1;
    if (g_menu_items[g_menu_open][item][0] == '-') return -1;
    return item;
}

static void textedit_clear_selection_overlay(void) {
    g_edit_sel_start = 0;
    g_edit_sel_end = 0;
}

static int textedit_selection_bounds_overlay(int *start, int *end) {
    int s = g_edit_sel_start;
    int e = g_edit_sel_end;
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    if (s > g_edit_len) s = g_edit_len;
    if (e > g_edit_len) e = g_edit_len;
    if (e < s) {
        int tmp = s;
        s = e;
        e = tmp;
    }
    if (e <= s) return 0;
    *start = s;
    *end = e;
    return 1;
}

static int textedit_delete_selection_overlay(void) {
    int s, e, k;
    if (!textedit_selection_bounds_overlay(&s, &e)) return 0;
    for (k = e; k <= g_edit_len; k++)
        g_edit_text[s + k - e] = g_edit_text[k];
    g_edit_len -= (e - s);
    textedit_clear_selection_overlay();
    return 1;
}

static int textedit_copy_selection_overlay(void) {
    int s, e, k;
    if (!textedit_selection_bounds_overlay(&s, &e)) return 0;
    s_textedit_clip_len = e - s;
    if (s_textedit_clip_len >= TEXTEDIT_MAXCHARS)
        s_textedit_clip_len = TEXTEDIT_MAXCHARS - 1;
    for (k = 0; k < s_textedit_clip_len; k++)
        s_textedit_clipboard[k] = g_edit_text[s + k];
    s_textedit_clipboard[s_textedit_clip_len] = 0;
    return 1;
}

static int textedit_paste_clipboard_overlay(void) {
    int k;
    if (s_textedit_clip_len <= 0) return 0;
    textedit_delete_selection_overlay();
    for (k = 0; k < s_textedit_clip_len && g_edit_len < TEXTEDIT_MAXCHARS - 1; k++) {
        g_edit_text[g_edit_len++] = s_textedit_clipboard[k];
    }
    g_edit_text[g_edit_len] = 0;
    textedit_clear_selection_overlay();
    return k > 0;
}

/* Perform action for dropdown item */
void dropdown_action(int menu_idx, int item_idx) {
    const char *label = g_menu_items[menu_idx][item_idx];
    int active_idx = win_top_visible();
    const char *active_title = (active_idx >= 0 && g_windows[active_idx].title) ? g_windows[active_idx].title : NULL;
    /* MyOS menu */
    if (menu_idx == 0) {
        if (str_eq(label, "About MyOS")) {
            int j;
            for (j=0;j<g_num_windows;j++) {
                if (g_windows[j].title && str_eq(g_windows[j].title,"About This Mac"))
                    { g_windows[j].visible=1; win_bring_to_front(j); return; }
            }
            if (g_num_windows < MAX_WINDOWS) {
                gui_window_t *aw = &g_windows[g_num_windows];
                aw->x=VGA_WIDTH/2-130; aw->y=VGA_HEIGHT/2-130; aw->w=260; aw->h=260;
                aw->title="About This Mac"; aw->visible=1; aw->focused=0;
                aw->dragging=0; aw->maximized=0;
                aw->space=g_current_space;
                g_num_windows++;
            }
        } else if (str_eq(label, "Mission Control")) {
            g_mc_visible = !g_mc_visible; return;
        } else if (str_eq(label, "Stage Manager")) {
            g_stage_manager = !g_stage_manager; return;
        } else if (str_eq(label, "Mission Control")) {
            g_mc_visible = !g_mc_visible; return;
        } else if (str_eq(label, "Stage Manager")) {
            g_stage_manager = !g_stage_manager; return;
        } else if (str_eq(label, "Mission Control")) {
            g_mc_visible = !g_mc_visible; return;
        } else if (str_eq(label, "Stage Manager")) {
            g_stage_manager = !g_stage_manager; return;
        } else if (str_eq(label, "Mission Control")) {
            g_mc_visible = !g_mc_visible; return;
        } else if (str_eq(label, "Stage Manager")) {
            g_stage_manager = !g_stage_manager; return;
        } else if (str_eq(label, "Lock Screen")) {
            extern int g_locked; extern int g_lock_pw_len; extern char g_lock_pw[];
            g_locked=1; g_lock_pw_len=0; g_lock_pw[0]=0; return;
        } else if (str_eq(label, "Restart...") || str_eq(label, "Shut Down...") || str_eq(label, "Quit")) {
            /* Shut down the OS */
            vga_fill_rect(0, 0, VGA_WIDTH, VGA_HEIGHT, COLOR_BLACK);
            { const char *bye = str_eq(label,"Restart...") ? "Restarting MyOS..." : "Goodbye from MyOS!";
              int bx3=(VGA_WIDTH-str_len(bye)*8)/2, by3=(VGA_HEIGHT-8)/2;
              vga_draw_string_trans(bx3, by3, bye, COLOR_WHITE); }
            vga_flip();
            for (;;) __asm__ volatile("hlt");
        } else if (str_eq(label, "Settings...")) {
            int j;
            for (j=0;j<g_num_windows;j++) {
                if (g_windows[j].title && str_eq(g_windows[j].title,"Settings"))
                    { g_windows[j].visible=1; win_bring_to_front(j); return; }
            }
            if (g_num_windows < MAX_WINDOWS) {
                gui_window_t *sw = &g_windows[g_num_windows];
                sw->x=260; sw->y=80; sw->w=280; sw->h=280;
                sw->title="Settings"; sw->visible=1; sw->focused=0;
                sw->dragging=0; sw->maximized=0;
                sw->space=g_current_space;
                g_num_windows++;
            }
        }
    }
    /* File menu */
    if (menu_idx == 1) {
        if (str_eq(label, "New Window")) {
            if (g_num_windows < MAX_WINDOWS) {
                gui_window_t *w = &g_windows[g_num_windows];
                if (active_title && str_eq(active_title, "Terminal")) {
                    w->x=100; w->y=100; w->w=290; w->h=220; w->title="Terminal";
                } else if (active_title) {
                    w->x=250; w->y=120; w->w=320; w->h=240; w->title=active_title;
                } else {
                    w->x=250; w->y=120; w->w=320; w->h=240; w->title="MyOS Finder";
                }
                w->visible=1; w->focused=0; w->dragging=0; w->maximized=0;
                w->space=g_current_space;
                g_num_windows++;
            }
        } else if (str_eq(label, "New")) {
            if (active_title && str_eq(active_title, "TextEdit")) {
                g_edit_text[0] = 0;
                g_edit_len = 0;
                textedit_clear_selection_overlay();
                g_edit_focused = 1;
            }
        } else if (str_eq(label, "New Message")) {
            g_mail_compose = 1;
            g_mail_focused_field = 1;
            g_mail_to[0]=0; g_mail_to_len=0;
            g_mail_subject[0]=0; g_mail_subject_len=0;
            g_mail_body[0]=0; g_mail_body_len=0;
        } else if (str_eq(label, "New Event")) {
            int cy, cm;
            g_cal_offset = 0;
            gui_calendar_month_from_offset(0, &cy, &cm);
            g_cal_sel_day = gui_calendar_today_day_for_month(cy, cm);
            if (g_cal_sel_day <= 0) g_cal_sel_day = 1;
            g_cal_popup = 1;
            g_cal_evt_input[0] = 0;
            g_cal_evt_input_len = 0;
        } else if (str_eq(label, "New Tab") && active_title && str_eq(active_title, "Safari")) {
            safari_normalize_state();
            if (g_safari_tab_count < SAFARI_MAX_TABS) {
                str_cpy(g_safari_tab_urls[g_safari_active_tab], g_safari_url);
                g_safari_active_tab = g_safari_tab_count++;
                safari_reset_tab_state(g_safari_active_tab, "about:home");
                safari_load_current_tab();
                str_cpy(g_safari_tab_titles[g_safari_active_tab], "New Tab");
                g_safari_url_focused = 1;
            }
        } else if (str_eq(label, "Close Tab") && active_title && str_eq(active_title, "Safari")) {
            int j;
            safari_normalize_state();
            if (g_safari_tab_count > 1) {
                for (j = g_safari_active_tab; j < g_safari_tab_count - 1; j++) {
                    safari_copy_tab_state(j, j + 1);
                }
                g_safari_tab_count--;
                safari_reset_tab_state(g_safari_tab_count, "about:home");
                if (g_safari_active_tab >= g_safari_tab_count)
                    g_safari_active_tab = g_safari_tab_count - 1;
                str_cpy(g_safari_url, g_safari_tab_urls[g_safari_active_tab]);
                safari_load_current_tab();
            } else {
                safari_load_url("about:home");
                str_cpy(g_safari_tab_titles[0], "Home");
                g_safari_url_focused = 0;
            }
        } else if (str_eq(label, "Close Window") || str_eq(label, "Close")) {
            /* Close topmost window */
            win_close(win_top_visible());
        } else if (str_eq(label, "Get Info")) {
            status_set_runtime_about();
        } else if (str_eq(label, "Print...")) {
            g_print_visible = !g_print_visible;
        } else if (str_eq(label, "Share...")) {
            g_share_visible = !g_share_visible;
        }
    }
    /* View menu — open app windows */
    if (menu_idx == 3) {
        int j;
        for (j=0;j<g_num_windows;j++) {
            if (g_windows[j].title && str_eq(g_windows[j].title, label)) {
                g_windows[j].visible=1; win_bring_to_front(j); return;
            }
        }
        /* Create window if missing */
        if (str_eq(label,"Calculator") && g_num_windows<MAX_WINDOWS) {
            gui_window_t *nw=&g_windows[g_num_windows];
            nw->x=180;nw->y=100;nw->w=220;nw->h=280;
            nw->title="Calculator";nw->visible=1;nw->focused=0;nw->dragging=0;nw->maximized=0;
            nw->space=g_current_space;
            g_num_windows++;
        } else if (str_eq(label,"Clock") && g_num_windows<MAX_WINDOWS) {
            gui_window_t *nw=&g_windows[g_num_windows];
            nw->x=50;nw->y=80;nw->w=180;nw->h=220;
            nw->title="Clock";nw->visible=1;nw->focused=0;nw->dragging=0;nw->maximized=0;
            nw->space=g_current_space;
            g_num_windows++;
            toast_show("Clock", "Analog & digital time", RGB(0,122,255));
        } else if (str_eq(label,"TextEdit") && g_num_windows<MAX_WINDOWS) {
            gui_window_t *nw=&g_windows[g_num_windows];
            nw->x=120;nw->y=80;nw->w=310;nw->h=260;
            nw->title="TextEdit";nw->visible=1;nw->focused=0;nw->dragging=0;nw->maximized=0;
            nw->space=g_current_space;
            g_edit_focused=1;
            g_num_windows++;
        } else if (str_eq(label,"Terminal") && g_num_windows<MAX_WINDOWS) {
            gui_window_t *nw=&g_windows[g_num_windows];
            nw->x=100;nw->y=100;nw->w=290;nw->h=220;
            nw->title="Terminal";nw->visible=1;nw->focused=0;nw->dragging=0;nw->maximized=0;
            nw->space=g_current_space;
            g_num_windows++;
        } else if (str_eq(label,"Finder") && g_num_windows<MAX_WINDOWS) {
            /* Finder is always already open; just bring to front */
            int j2;
            for (j2=0;j2<g_num_windows;j2++) {
                if (g_windows[j2].title && str_eq(g_windows[j2].title,"MyOS Finder")) {
                    g_windows[j2].visible=1; win_bring_to_front(j2); return;
                }
            }
        }
    }
    /* Help menu */
    if (menu_idx == 5) {
        if (str_eq(label,"About MyOS")) status_set_runtime_about();
        else if (str_eq(label,"Quick Help")) {
            /* Open Keyboard Shortcuts window */
            int jj;
            for (jj=0;jj<g_num_windows;jj++) {
                if (g_windows[jj].title && str_eq(g_windows[jj].title,"Keyboard Shortcuts"))
                    { g_windows[jj].visible=1; win_bring_to_front(jj); return; }
            }
            if (g_num_windows < MAX_WINDOWS) {
                gui_window_t *nwks = &g_windows[g_num_windows];
                nwks->x=90; nwks->y=40; nwks->w=620; nwks->h=500;
                nwks->title="Keyboard Shortcuts"; nwks->visible=1; nwks->focused=0;
                nwks->space=g_current_space;
                g_num_windows++;
            }
        }
    }
    /* App-specific: Shell menu (Terminal) */
    if (str_eq(label,"Clear")) {
        int j; for (j=0;j<TERM_LINES;j++) term_history[j][0]=0; term_num_lines=0;
    }
    /* TextEdit Format actions */
    if (str_eq(label,"Bold")) {
        g_edit_bold = !g_edit_bold;
        g_edit_focused = 1;
    }
    else if (str_eq(label,"Italic")) {
        g_edit_italic = !g_edit_italic;
        g_edit_focused = 1;
    }
    else if (str_eq(label,"Font Size")) {
        g_edit_font_size = (g_edit_font_size + 1) % 3;
        g_edit_focused = 1;
    }
    else if (str_eq(label,"Plain Text")) {
        g_edit_bold = 0;
        g_edit_italic = 0;
        g_edit_font_size = 1;
        g_edit_color = 0;
        g_edit_focused = 1;
    }
    else if (str_eq(label,"Copy")) {
        if (textedit_copy_selection_overlay())
            toast_show("TextEdit","Copied",RGB(255,180,40));
        else
            toast_show("TextEdit","No selection",RGB(120,120,130));
    }
    else if (str_eq(label,"Cut")) {
        if (textedit_copy_selection_overlay()) {
            textedit_delete_selection_overlay();
            toast_show("TextEdit","Cut",RGB(255,180,40));
        } else {
            toast_show("TextEdit","No selection",RGB(120,120,130));
        }
    }
    else if (str_eq(label,"Paste")) {
        if (textedit_paste_clipboard_overlay()) {
            g_edit_focused = 1;
            toast_show("TextEdit","Pasted",RGB(255,180,40));
        } else {
            toast_show("TextEdit","Clipboard empty",RGB(120,120,130));
        }
    }
    else if (str_eq(label,"Select All")) {
        int top = win_top_visible();
        if (g_edit_len == 0) {
            while (g_edit_len < TEXTEDIT_MAXCHARS - 1 && g_edit_text[g_edit_len]) g_edit_len++;
        }
        if (top >= 0 && g_windows[top].title && str_eq(g_windows[top].title, "TextEdit") && g_edit_len > 0) {
            g_edit_sel_start = 0;
            g_edit_sel_end = g_edit_len;
            g_edit_focused = 1;
            toast_show("TextEdit","All text selected",RGB(255,180,40));
        } else {
            g_edit_sel_start = 0;
            g_edit_sel_end = 0;
            toast_show("TextEdit","Nothing to select",RGB(120,120,130));
        }
    }
    /* Music controls */
    if (str_eq(label,"Play")) {
        g_music_playing = 1;
    } else if (str_eq(label,"Pause")) {
        g_music_playing = 0;
    } else if (str_eq(label,"Next")) {
        g_music_track = (g_music_track + 1) % 5;
        g_music_playing = 1;
    } else if (str_eq(label,"Previous")) {
        g_music_track = (g_music_track + 4) % 5;
        g_music_playing = 1;
    }
    /* Safari */
    if (str_eq(label,"Reload Page") && active_title && str_eq(active_title, "Safari")) {
        safari_load_current_tab();
        toast_show("Safari",g_safari_page_status,RGB(40,160,220));
    } else if (str_eq(label,"Home") && active_title && str_eq(active_title, "Safari")) {
        safari_load_url("about:home");
        str_cpy(g_safari_tab_titles[g_safari_active_tab], "Home");
        g_safari_url_focused = 0;
    }
    /* Maps */
    if (str_eq(label,"Current Location")) {
        runtime_weather_info_t wth;
        runtime_get_weather_info(&wth);
        toast_show("Maps",wth.location,RGB(60,200,80));
    }
    /* Clock */
    if (str_eq(label,"World Clock")) { g_clock_tab = 0; }
    else if (str_eq(label,"Timer")) { g_clock_tab = 2; }
    else if (str_eq(label,"Stopwatch")) { g_clock_tab = 3; }
    /* Calendar */
    if (str_eq(label,"Go to Today")) {
        int cy, cm;
        g_cal_offset = 0;
        gui_calendar_month_from_offset(0, &cy, &cm);
        g_cal_sel_day = gui_calendar_today_day_for_month(cy, cm);
        g_cal_popup = 0;
    }
    /* Mail */
    if (str_eq(label,"Inbox")) {
        g_mail_compose = 0;
        g_mail_focused_field = 0;
        g_mail_sel_msg = 0;
    }
}

static int spot_weather(const char *q, int qlen, char *out, int out_max) {
    /* Match "weather", "weather here", or the configured runtime location. */
    if (!q || !out || out_max <= 0 || qlen < 7) return 0;
    out[0] = 0;
    char lq[8];
    int i;
    for (i=0; i<7 && i<qlen; i++) {
        char c = q[i];
        if (c>='A'&&c<='Z') c+=32;
        lq[i]=c;
    }
    lq[7]=0;
    if (lq[0]!='w'||lq[1]!='e'||lq[2]!='a'||lq[3]!='t'||lq[4]!='h'||lq[5]!='e'||lq[6]!='r') return 0;
    if (qlen > 7 && q[7] != ' ') return 0;

    runtime_weather_info_t wth;
    runtime_get_weather_info(&wth);

    i = 7;
    while (i < qlen && q[i] == ' ') i++;
    if (i < qlen) {
        char city[24];
        int ci = 0;
        while (i < qlen && q[i] != ' ' && ci < 23) {
            char c = q[i++];
            if (c >= 'A' && c <= 'Z') c += 32;
            city[ci++] = c;
        }
        city[ci] = 0;
        if (ci > 0) {
            int match_here = (ci == 4 && city[0]=='h' && city[1]=='e' && city[2]=='r' && city[3]=='e');
            int match_loc = 1;
            int li;
            for (li = 0; li < ci || wth.location[li]; li++) {
                char lc = wth.location[li];
                if (lc >= 'A' && lc <= 'Z') lc += 32;
                if (li >= ci || lc != city[li]) { match_loc = 0; break; }
            }
            if (!match_here && !match_loc) return 0;
        }
    }

    char tempbuf[8];
    runtime_format_temperature_c(wth.temperature_c, tempbuf, sizeof(tempbuf));
    int p = 0, si;
#define SPOT_APPEND_CHAR(ch_) do { if (p >= out_max - 1) { out[p] = 0; return 0; } out[p++] = (char)(ch_); } while (0)
    for (si=0; wth.location_full[si]; si++) SPOT_APPEND_CHAR(wth.location_full[si]);
    SPOT_APPEND_CHAR(':'); SPOT_APPEND_CHAR(' ');
    for (si=0; tempbuf[si]; si++) SPOT_APPEND_CHAR(tempbuf[si]);
    SPOT_APPEND_CHAR(' ');
    for (si=0; wth.condition && wth.condition[si]; si++) SPOT_APPEND_CHAR(wth.condition[si]);
#undef SPOT_APPEND_CHAR
    out[p]=0;
    return 1;
}

static int spot_define(const char *q, int qlen, char *out, int out_max) {
    /* Match "define WORD" or a known dictionary word. */
    if (!q || !out || out_max <= 0 || qlen <= 0) return 0;
    out[0] = 0;
    static const struct { const char *word; const char *def; } dict[] = {
        {"serenity",    "calmness; peace of mind"     },
        {"algorithm",   "step-by-step procedure"      },
        {"entropy",     "measure of disorder"          },
        {"ephemeral",   "lasting a very short time"   },
        {"kernel",      "core of an OS or nut"        },
        {"recursion",   "function calling itself"      },
        {"abstraction", "hiding complexity"            },
        {"paradigm",    "a typical example or model"  },
        {"heuristic",   "practical problem-solving"   },
        {"latency",     "time delay in a system"      },
        {"bandwidth",   "data transfer capacity"      },
        {"compile",     "translate code to binary"    },
        {"cache",       "fast temporary storage"      },
        {"mutex",       "mutual exclusion lock"        },
        {"daemon",      "background system process"   },
        {"syntax",      "rules for valid code"        },
        {"boolean",     "true or false value"         },
        {"iteration",   "repeating a process"         },
        {"polymorphism","many forms, one interface"   },
        {"debug",       "find and fix errors"         },
        {"malloc",      "allocate heap memory"        },
        {"interrupt",   "signal to pause the CPU"     },
        {"pixel",       "smallest display element"    },
        {"buffer",      "temporary data storage"      },
        {"socket",      "network endpoint"            },
        {0, 0}
    };
    /* Check "define WORD" prefix */
    int i; char word[24]; int wlen = 0;
    int start = 0;
    /* Case-insensitive match "define " prefix */
    if (qlen >= 8) {
        char p0=q[0],p1=q[1],p2=q[2],p3=q[3],p4=q[4],p5=q[5];
        if ((p0=='d'||p0=='D')&&(p1=='e'||p1=='E')&&(p2=='f'||p2=='F')&&
            (p3=='i'||p3=='I')&&(p4=='n'||p4=='N')&&(p5=='e'||p5=='E')&&q[6]==' ') {
            start = 7;
        }
    }
    /* Extract word */
    for (i = start; i < qlen && wlen < 23 && q[i] != ' '; i++) {
        char c = q[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        word[wlen++] = c;
    }
    word[wlen] = 0;
    if (!wlen) return 0;
    /* Lookup */
    for (i = 0; dict[i].word; i++) {
        int mi = 0, match = 1;
        for (mi = 0; dict[i].word[mi] || word[mi]; mi++)
            if (dict[i].word[mi] != word[mi]) { match = 0; break; }
        if (!match) continue;
        /* Build output: "word: definition" */
        int p = 0, si;
#define SPOT_APPEND_CHAR(ch_) do { if (p >= out_max - 1) { out[p] = 0; return 0; } out[p++] = (char)(ch_); } while (0)
        for (si = 0; dict[i].word[si]; si++) SPOT_APPEND_CHAR(dict[i].word[si]);
        SPOT_APPEND_CHAR(':'); SPOT_APPEND_CHAR(' ');
        for (si = 0; dict[i].def[si]; si++) SPOT_APPEND_CHAR(dict[i].def[si]);
#undef SPOT_APPEND_CHAR
        out[p] = 0;
        return 1;
    }
    return 0;
}

static int spot_unit_convert(const char *q, int qlen, char *out, int out_max) {
    /* Parse: "<number> <from> in <to>" or "<number> <from> to <to>" */
    if (!q || !out || out_max <= 0 || qlen <= 0) return 0;
    out[0] = 0;
    int i = 0;
    while (i < qlen && q[i] == ' ') i++;
    if (i >= qlen || q[i] < '0' || q[i] > '9') return 0;
    int32_t num = 0;
    while (i < qlen && q[i] >= '0' && q[i] <= '9') {
        if (num > 214748364 || (num == 214748364 && q[i] > '7')) return 0;
        num = num * 10 + (int32_t)(q[i] - '0');
        i++;
    }
    while (i < qlen && q[i] == ' ') i++;
    /* Extract from_unit (lowercase) */
    char fu[12]; int flen = 0;
    while (i < qlen && q[i] != ' ' && flen < 11) {
        char c = q[i]; if (c >= 'A' && c <= 'Z') c += 32;
        fu[flen++] = c; i++;
    }
    fu[flen] = 0;
    if (!flen) return 0;
    while (i < qlen && q[i] == ' ') i++;
    /* Expect "in" or "to" */
    if (i + 1 >= qlen) return 0;
    char k0 = q[i], k1 = q[i+1];
    if (k0 >= 'A' && k0 <= 'Z') k0 += 32;
    if (k1 >= 'A' && k1 <= 'Z') k1 += 32;
    if (!((k0=='i'&&k1=='n') || (k0=='t'&&k1=='o'))) return 0;
    i += 2;
    while (i < qlen && q[i] == ' ') i++;
    /* Extract to_unit (lowercase) */
    char tu[12]; int tlen = 0;
    while (i < qlen && q[i] != ' ' && tlen < 11) {
        char c = q[i]; if (c >= 'A' && c <= 'Z') c += 32;
        tu[tlen++] = c; i++;
    }
    tu[tlen] = 0;
    if (!tlen) return 0;

    /* Conversion table: from, to, pre_offset, num_factor, den_factor, post_offset, from_sym, to_sym */
    static const struct {
        const char *from, *to;
        int32_t pre; int32_t nf; int32_t df; int32_t post;
        const char *fsym, *tsym;
    } cv[] = {
        /* Length */
        {"km",    "miles", 0,    621,  1000, 0,  "km",    "mi"  },
        {"km",    "mi",    0,    621,  1000, 0,  "km",    "mi"  },
        {"miles", "km",    0,    1609, 1000, 0,  "mi",    "km"  },
        {"mi",    "km",    0,    1609, 1000, 0,  "mi",    "km"  },
        {"m",     "ft",    0,    3281, 1000, 0,  "m",     "ft"  },
        {"ft",    "m",     0,    305,  1000, 0,  "ft",    "m"   },
        {"cm",    "in",    0,    394,  1000, 0,  "cm",    "in"  },
        {"in",    "cm",    0,    254,  100,  0,  "in",    "cm"  },
        /* Temperature */
        {"f",     "c",     -32,  5,    9,    0,  "F",     "C"   },
        {"c",     "f",     0,    9,    5,    32, "C",     "F"   },
        /* Weight */
        {"kg",    "lb",    0,    2205, 1000, 0,  "kg",    "lb"  },
        {"lb",    "kg",    0,    454,  1000, 0,  "lb",    "kg"  },
        {"g",     "oz",    0,    35,   1000, 0,  "g",     "oz"  },
        {"oz",    "g",     0,    2835, 100,  0,  "oz",    "g"   },
        /* Volume */
        {"l",     "gal",   0,    264,  1000, 0,  "L",     "gal" },
        {"gal",   "l",     0,    3785, 1000, 0,  "gal",   "L"   },
        /* Speed */
        {"mph",   "kph",   0,    1609, 1000, 0,  "mph",   "kph" },
        {"kph",   "mph",   0,    621,  1000, 0,  "kph",   "mph" },
        {0,0,0,0,0,0,0,0}
    };
    int ci;
    for (ci = 0; cv[ci].from; ci++) {
        /* simple string compare */
        int match_f = 1, match_t = 1, mi;
        for (mi = 0; cv[ci].from[mi] || fu[mi]; mi++)
            if (cv[ci].from[mi] != fu[mi]) { match_f = 0; break; }
        for (mi = 0; cv[ci].to[mi] || tu[mi]; mi++)
            if (cv[ci].to[mi] != tu[mi]) { match_t = 0; break; }
        if (!match_f || !match_t) continue;
        int64_t val = (int64_t)num + (int64_t)cv[ci].pre;
        int64_t res64 = val * (int64_t)cv[ci].nf / (int64_t)cv[ci].df + (int64_t)cv[ci].post;
        if (res64 > 2147483647LL || res64 < -2147483647LL - 1LL) return 0;
        int32_t res = (int32_t)res64;
        /* Format: "NUM fsym = RES tsym" */
        char nbuf[12], rbuf[12]; int ni=0, ri=0;
        /* write num */
        if (num == 0) { nbuf[ni++]='0'; } else {
            char tmp[11]; int tl=0; int32_t mv=(num<0)?-num:num;
            if(num<0) nbuf[ni++]='-';
            while(mv){tmp[tl++]=(char)('0'+mv%10);mv/=10;}
            int j; for(j=tl-1;j>=0;j--) nbuf[ni++]=tmp[j];
        }
        nbuf[ni]=0;
        /* write res */
        if (res == 0) { rbuf[ri++]='0'; } else {
            char tmp[11]; int tl=0; int64_t mv=(int64_t)res;
            if(mv<0) { rbuf[ri++]='-'; mv=-mv; }
            while(mv){tmp[tl++]=(char)('0'+(int)(mv%10));mv/=10;}
            int j; for(j=tl-1;j>=0;j--) rbuf[ri++]=tmp[j];
        }
        rbuf[ri]=0;
        /* Build output string in out[] */
        int p=0;
        int si;
#define SPOT_APPEND_CHAR(ch_) do { if (p >= out_max - 1) { out[p] = 0; return 0; } out[p++] = (char)(ch_); } while (0)
        for(si=0;nbuf[si];si++) SPOT_APPEND_CHAR(nbuf[si]);
        SPOT_APPEND_CHAR(' ');
        for(si=0;cv[ci].fsym[si];si++) SPOT_APPEND_CHAR(cv[ci].fsym[si]);
        SPOT_APPEND_CHAR(' '); SPOT_APPEND_CHAR('='); SPOT_APPEND_CHAR(' ');
        for(si=0;rbuf[si];si++) SPOT_APPEND_CHAR(rbuf[si]);
        SPOT_APPEND_CHAR(' ');
        for(si=0;cv[ci].tsym[si];si++) SPOT_APPEND_CHAR(cv[ci].tsym[si]);
#undef SPOT_APPEND_CHAR
        out[p]=0;
        return 1;
    }
    return 0;
}

static int32_t calc_clamp_i64(int64_t v) {
    if (v > 2147483647LL) return 2147483647;
    if (v < -2147483647LL - 1LL) return (int32_t)(-2147483647LL - 1LL);
    return (int32_t)v;
}

static int32_t calc_apply_op(int32_t a, int32_t b, char op) {
    switch (op) {
    case '+': return calc_clamp_i64((int64_t)a + (int64_t)b);
    case '-': return calc_clamp_i64((int64_t)a - (int64_t)b);
    case '*': return calc_clamp_i64((int64_t)a * (int64_t)b);
    case '/':
        if (!b) return a;
        if (a == (int32_t)(-2147483647LL - 1LL) && b == -1) return 2147483647;
        return (int32_t)(a / b);
    default:  return b;
    }
}

void calc_update_disp(int32_t v) {
    char tmp[12]; int i=0, len=0, neg=0;
    uint32_t mag;
    if (v < 0) {
        neg=1;
        mag = (uint32_t)(-(v + 1)) + 1U;
    } else {
        mag = (uint32_t)v;
    }
    if (mag == 0) { tmp[0]='0'; len=1; }
    else { while (mag>0) { tmp[len++]='0'+(int)(mag%10U); mag/=10U; } }
    /* reverse */
    { int a2=0, b2=len-1; while(a2<b2){char c=tmp[a2];tmp[a2]=tmp[b2];tmp[b2]=c;a2++;b2--;} }
    if (neg) g_calc_disp[i++]='-';
    { int j=0; while(j<len && i<14) g_calc_disp[i++]=tmp[j++]; }
    g_calc_disp[i]=0;
}

void calc_press(char action) {
    if (action>='0' && action<='9') {
        int digit = action - '0';
        if (g_calc_fresh) { g_calc_cur=0; g_calc_fresh=0; }
        g_calc_cur = calc_clamp_i64((int64_t)g_calc_cur * 10LL + (int64_t)digit);
        calc_update_disp(g_calc_cur);
    } else if (action=='c') {
        g_calc_a=0; g_calc_cur=0; g_calc_op=0; g_calc_fresh=1;
        g_calc_disp[0]='0'; g_calc_disp[1]=0;
    } else if (action=='n') {
        g_calc_cur=calc_clamp_i64(-(int64_t)g_calc_cur); calc_update_disp(g_calc_cur);
    } else if (action=='p') {
        g_calc_cur=g_calc_cur/100; calc_update_disp(g_calc_cur);
    } else if (action=='+' || action=='-' || action=='*' || action=='/') {
        if (!g_calc_fresh) {
            g_calc_a = calc_apply_op(g_calc_a, g_calc_cur, g_calc_op);
            g_calc_cur=g_calc_a;
        } else { g_calc_a=g_calc_cur; }
        g_calc_op=action; g_calc_fresh=1; calc_update_disp(g_calc_a);
    } else if (action=='=') {
        g_calc_a = calc_apply_op(g_calc_a, g_calc_cur, g_calc_op);
        g_calc_cur=g_calc_a; g_calc_fresh=1; g_calc_op=0;
        calc_update_disp(g_calc_a);
    }
}

/* Returns action char for click at (mx,my) relative to window top-left, or 0 if miss.
   btn_y0 = y pixel where button grid starts (relative to win->y) */
char calc_hit(int mx, int my, const gui_window_t *win) {
    static const char rows[5][4] = {
        {'c','n','p','/'}, {'7','8','9','*'},
        {'4','5','6','-'}, {'1','2','3','+'},
        {'0','0','.','='}
    };
    int bx0 = win->x + 8, by0 = win->y + TITLEBAR_H + 66;
    int bw = 48, bh = 30, gx = 4, gy = 4;
    /* Bottom row: col0+1 are merged as wide '0' button */
    int r, c;
    /* Check wide 0 button (cols 0-1) in row 4 */
    {
        int bx = bx0;
        int by = by0 + 4*(bh+gy);
        if (mx>=bx && mx<bx+bw*2+gx && my>=by && my<by+bh) return '0';
    }
    for (r=0; r<5; r++) {
        for (c=0; c<4; c++) {
            if (r==4 && c<2) continue; /* skip merged 0 area */
            int col_x = bx0 + c*(bw+gx);
            if (r==4 && c>=2) col_x = bx0 + c*(bw+gx); /* shift right for ., = */
            int row_y = by0 + r*(bh+gy);
            if (mx>=col_x && mx<col_x+bw && my>=row_y && my<row_y+bh)
                return rows[r][c];
        }
    }
    return 0;
}

int str_eq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return a == b;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}

static void term_append_char(char *buf, int *pos, char ch) {
    if (*pos < TERM_MAX_COL) buf[(*pos)++] = ch;
    buf[*pos] = 0;
}

static void term_append_text(char *buf, int *pos, const char *text) {
    int i = 0;
    if (!text) return;
    while (text[i] && *pos < TERM_MAX_COL) buf[(*pos)++] = text[i++];
    buf[*pos] = 0;
}

static void term_append_uint(char *buf, int *pos, uint32_t value) {
    char nbuf[12];
    int i = 0;
    runtime_format_uint(value, nbuf, sizeof(nbuf));
    while (nbuf[i] && *pos < TERM_MAX_COL) buf[(*pos)++] = nbuf[i++];
    buf[*pos] = 0;
}

static void term_print2(const char *a, const char *b) {
    char line[TERM_MAX_COL + 1];
    int pos = 0;
    line[0] = 0;
    term_append_text(line, &pos, a);
    term_append_text(line, &pos, b);
    term_println(line);
}

static void term_print3(const char *a, const char *b, const char *c) {
    char line[TERM_MAX_COL + 1];
    int pos = 0;
    line[0] = 0;
    term_append_text(line, &pos, a);
    term_append_text(line, &pos, b);
    term_append_text(line, &pos, c);
    term_println(line);
}

static void term_print_uint_line(const char *label, uint32_t value) {
    char line[TERM_MAX_COL + 1];
    int pos = 0;
    line[0] = 0;
    term_append_text(line, &pos, label);
    term_append_uint(line, &pos, value);
    term_println(line);
}

static void term_print_bytes_line(const char *label, uint32_t bytes) {
    char vbuf[16];
    runtime_format_bytes(bytes, vbuf, sizeof(vbuf));
    term_print2(label, vbuf);
}

static void term_print_display_line(const runtime_system_info_t *sys) {
    char line[TERM_MAX_COL + 1];
    int pos = 0;
    line[0] = 0;
    term_append_text(line, &pos, "Display: ");
    term_append_uint(line, &pos, sys->display_width);
    term_append_char(line, &pos, 'x');
    term_append_uint(line, &pos, sys->display_height);
    term_append_char(line, &pos, 'x');
    term_append_uint(line, &pos, sys->display_bpp);
    term_println(line);
}

static void term_print_runtime_uname(int all) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    if (all) {
        term_print3(sys.sysname, " ", sys.nodename);
        term_print3(sys.release, " ", sys.machine);
        term_print2("Build: ", sys.version);
    } else {
        term_print3(sys.sysname, " ", sys.release);
    }
}

static void term_print_runtime_sysinfo(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_print3("OS: ", sys.sysname, "");
    term_print2("Kernel: ", sys.release);
    term_print2("Host: ", sys.nodename);
    term_print2("CPU: ", sys.cpu_model);
    term_print_bytes_line("Mem: ", sys.pmm_total_bytes);
    term_print_display_line(&sys);
}

static void term_print_runtime_ps(void) {
    uint32_t i;
    term_println("PID STATE CMD");
    for (i = 0; i < PROCESS_MAX; i++) {
        const process_t *p = process_at(i);
        char line[TERM_MAX_COL + 1];
        int pos = 0;
        if (!p) continue;
        line[0] = 0;
        term_append_uint(line, &pos, p->pid);
        term_append_char(line, &pos, ' ');
        term_append_text(line, &pos, process_state_name(p->state));
        term_append_char(line, &pos, ' ');
        term_append_text(line, &pos, p->name);
        term_println(line);
    }
}

static void term_print_runtime_top(void) {
    runtime_system_info_t sys;
    char pct[8];
    runtime_get_system_info(&sys);
    runtime_format_percent(sys.cpu_load_percent, pct, sizeof(pct));
    term_print_uint_line("Tasks: ", sys.task_count);
    term_print2("CPU: ", pct);
    term_print_bytes_line("Mem total: ", sys.pmm_total_bytes);
    term_print_bytes_line("Mem free:  ", sys.pmm_free_bytes);
    term_print_runtime_ps();
}

static void term_print_runtime_term_size(void) {
    char line[TERM_MAX_COL + 1];
    int pos = 0;
    line[0] = 0;
    term_append_uint(line, &pos, TERM_LINES);
    term_append_char(line, &pos, ' ');
    term_append_uint(line, &pos, TERM_MAX_COL);
    term_println(line);
}

static void term_make_home_path(const char *arg, char *out, uint32_t max) {
    uint32_t oi = 0;
    uint32_t ai = 0;
    const char *prefix = "/home/root/";
    if (!out || max == 0) return;
    if (!arg) arg = "";
    while (*arg == ' ') arg++;
    if (arg[0] == '/') {
        while (arg[ai] && oi + 1 < max) out[oi++] = arg[ai++];
    } else {
        while (prefix[ai] && oi + 1 < max) out[oi++] = prefix[ai++];
        ai = 0;
        while (arg[ai] && arg[ai] != ' ' && oi + 1 < max) out[oi++] = arg[ai++];
    }
    out[oi] = 0;
}

static void term_print_runtime_ifconfig(void) {
    uint32_t i;
    if (netif_count() == 0) {
        term_println("no network interfaces");
        return;
    }
    for (i = 0; i < netif_count(); i++) {
        const netif_t *n = netif_at(i);
        char ipbuf[18];
        char macbuf[20];
        if (!n) continue;
        term_print3(n->name, n->up ? ": up" : ": down", "");
        runtime_format_ipv4(n->ipv4, ipbuf, sizeof(ipbuf));
        runtime_format_mac(n->mac, macbuf, sizeof(macbuf));
        term_print2("  inet ", ipbuf);
        term_print2("  mac  ", macbuf);
    }
}

static void term_print_runtime_ipaddr(void) {
    uint32_t i;
    if (netif_count() == 0) {
        term_println("no addresses");
        return;
    }
    for (i = 0; i < netif_count(); i++) {
        const netif_t *n = netif_at(i);
        char ipbuf[18];
        if (!n) continue;
        runtime_format_ipv4(n->ipv4, ipbuf, sizeof(ipbuf));
        term_print3(n->name, n->up ? " inet " : " down ", ipbuf);
    }
}

static void term_print_runtime_df(void) {
    runtime_storage_info_t st;
    if (runtime_get_storage_info("/", &st) < 0) {
        term_println("df: storage info pending");
        return;
    }
    term_println("Filesystem: /");
    term_print_bytes_line("Size:  ", st.total_bytes);
    term_print_bytes_line("Used:  ", st.used_bytes);
    term_print_bytes_line("Avail: ", st.free_bytes);
}

static void term_print_runtime_free(void) {
    runtime_system_info_t sys;
    uint32_t used;
    runtime_get_system_info(&sys);
    used = sys.pmm_total_bytes >= sys.pmm_free_bytes ? sys.pmm_total_bytes - sys.pmm_free_bytes : 0;
    term_print_bytes_line("Mem total: ", sys.pmm_total_bytes);
    term_print_bytes_line("Mem used:  ", used);
    term_print_bytes_line("Mem free:  ", sys.pmm_free_bytes);
}

static void term_print_runtime_dmesg(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_print2("[0.000] Booting ", sys.sysname);
    term_print2("[0.010] Kernel ", sys.release);
    term_print_uint_line("[0.020] Timer Hz ", sys.timer_hz);
    term_print_display_line(&sys);
    term_print_uint_line("[0.040] Drivers ", sys.driver_count);
}

static void term_print_runtime_swvers(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_print2("ProductName: ", sys.sysname);
    term_print2("ProductVersion: ", sys.release);
    term_print2("BuildVersion: ", sys.version);
}

static void term_print_runtime_diskutil(void) {
    runtime_storage_info_t st;
    if (runtime_get_storage_info("/", &st) < 0) {
        term_println("diskutil: no storage info");
        return;
    }
    term_println("/dev/disk0:");
    term_print2("  FS: ", st.name);
    term_print_bytes_line("  Size: ", st.total_bytes);
    term_print_bytes_line("  Free: ", st.free_bytes);
}

static void term_print_runtime_hosts(void) {
    runtime_system_info_t sys;
    const netif_t *n;
    char loopbuf[18];
    runtime_get_system_info(&sys);
    runtime_format_ipv4(0x7F000001U, loopbuf, sizeof(loopbuf));
    term_print3(loopbuf, "  ", "localhost");
    n = runtime_primary_netif();
    if (n && n->ipv4) {
        char ipbuf[18];
        runtime_format_ipv4(n->ipv4, ipbuf, sizeof(ipbuf));
        term_print3(ipbuf, "  ", sys.nodename);
    }
}

static void term_print_runtime_cpuinfo(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_println("processor : 0");
    term_print2("model name: ", sys.cpu_model);
    term_print2("machine   : ", sys.machine);
}

static void term_print_runtime_meminfo(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_print_bytes_line("MemTotal: ", sys.pmm_total_bytes);
    term_print_bytes_line("MemFree:  ", sys.pmm_free_bytes);
    term_print_bytes_line("HeapUsed: ", sys.heap_used_bytes);
}

static void term_print_runtime_lscpu(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_print2("Architecture: ", sys.machine);
    term_print_uint_line("CPU(s):       ", sys.cpu_count);
    term_print2("Model:        ", sys.cpu_model);
}

static void term_print_runtime_netstat(void) {
    term_println("Network state:");
    term_print_uint_line("Interfaces: ", netif_count());
    term_print_uint_line("Routes:     ", net_route_count());
    term_print_uint_line("ARP cache:  ", net_arp_count());
}

static void term_print_runtime_route(void) {
    uint32_t i;
    if (net_route_count() == 0) {
        term_println("no routes");
        return;
    }
    for (i = 0; i < net_route_count(); i++) {
        const net_route_t *r = net_route_at(i);
        char dst[18], gw[18], mask[18];
        if (!r) continue;
        runtime_format_ipv4(r->dest, dst, sizeof(dst));
        runtime_format_ipv4(r->gateway, gw, sizeof(gw));
        runtime_format_ipv4(r->mask, mask, sizeof(mask));
        term_print2("dest ", dst);
        term_print2("gw   ", gw);
        term_print2("mask ", mask);
    }
}

static void term_print_runtime_lsblk(void) {
    runtime_storage_info_t st;
    if (runtime_get_storage_info("/", &st) < 0) {
        term_println("lsblk: no block devices");
        return;
    }
    term_println("NAME   SIZE MOUNT");
    term_print_bytes_line("disk0  ", st.total_bytes);
    term_println("       /");
}

static void term_print_runtime_gui(void) {
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    term_println("GUI running");
    term_print_display_line(&sys);
    term_print2("Mode: ", sys.display);
}

static int term_app_is_known(const char *name) {
    int i;
    if (!name || !name[0]) return 0;
    for (i = 0; g_spot_apps[i]; i++) {
        if (str_eq(name, g_spot_apps[i])) return 1;
    }
    return 0;
}

static void term_focus_opened_app(const char *name) {
    if (str_eq(name, "Dictionary")) {
        g_dict_focused = 1;
        g_edit_focused = 0;
    } else if (str_eq(name, "TextEdit")) {
        g_edit_focused = 1;
        g_dict_focused = 0;
    } else if (str_eq(name, "Wordle")) {
        g_wordle_focused = 1;
    }
}

static void term_reset_game_state(const char *name) {
    if (str_eq(name, "2048")) {
        g2048_new_game();
    } else if (str_eq(name, "Sudoku")) {
        g_sdk_started = 0;
        g_sdk_errors = 0;
        g_sdk_sel_r = -1;
        g_sdk_sel_c = -1;
    } else if (str_eq(name, "Wordle")) {
        int r, c;
        for (r = 0; r < WORDLE_ROWS; r++) {
            for (c = 0; c < WORDLE_COLS; c++) {
                g_wordle_guesses[r][c] = 0;
                g_wordle_results[r][c] = 0;
            }
            g_wordle_guesses[r][WORDLE_COLS] = 0;
        }
        for (r = 0; r < 26; r++) g_wordle_kb_state[r] = 0;
        g_wordle_cur_row = 0;
        g_wordle_cur_col = 0;
        g_wordle_state = 0;
        g_wordle_answer_idx = 0;
        g_wordle_focused = 1;
    } else if (str_eq(name, "Pong")) {
        g_pong_active = 0;
        g_pong_over = 0;
        g_pong_score_p = 0;
        g_pong_score_a = 0;
    } else if (str_eq(name, "Minesweeper")) {
        int r, c;
        g_mine_state = 0;
        g_mine_remaining = MINE_COUNT;
        g_mine_rng = 1;
        for (r = 0; r < MINE_ROWS; r++) {
            for (c = 0; c < MINE_COLS; c++) {
                g_mine_board[r][c] = 0;
                g_mine_vis[r][c] = 0;
                g_mine_flag[r][c] = 0;
            }
        }
    }
}

static void term_configure_app_window(gui_window_t *nw, const char *name) {
    nw->x = 120;
    nw->y = 80;
    nw->w = 280;
    nw->h = 220;
    if (str_eq(name, "Clock"))                 { nw->x=50; nw->y=80; nw->w=180; nw->h=220; }
    else if (str_eq(name, "Calculator"))       { nw->x=180; nw->y=100; nw->w=220; nw->h=280; }
    else if (str_eq(name, "Settings"))         { nw->x=150; nw->y=50; nw->w=500; nw->h=400; }
    else if (str_eq(name, "TextEdit"))         { nw->x=120; nw->y=80; nw->w=310; nw->h=260; }
    else if (str_eq(name, "Terminal"))         { nw->x=100; nw->y=100; nw->w=290; nw->h=220; }
    else if (str_eq(name, "Safari"))           { nw->x=60; nw->y=50; nw->w=480; nw->h=380; }
    else if (str_eq(name, "Music"))            { nw->x=80; nw->y=55; nw->w=280; nw->h=340; }
    else if (str_eq(name, "Photos"))           { nw->x=70; nw->y=50; nw->w=420; nw->h=340; }
    else if (str_eq(name, "Maps"))             { nw->x=80; nw->y=55; nw->w=400; nw->h=320; }
    else if (str_eq(name, "App Store"))        { nw->x=70; nw->y=45; nw->w=440; nw->h=360; }
    else if (str_eq(name, "Mail"))             { nw->x=80; nw->y=50; nw->w=420; nw->h=350; }
    else if (str_eq(name, "Calendar"))         { nw->x=90; nw->y=55; nw->w=400; nw->h=340; }
    else if (str_eq(name, "Notes"))            { nw->x=90; nw->y=60; nw->w=300; nw->h=320; }
    else if (str_eq(name, "Finder"))           { nw->x=80; nw->y=50; nw->w=420; nw->h=320; }
    else if (str_eq(name, "Activity Monitor")) { nw->x=140; nw->y=80; nw->w=320; nw->h=270; }
    else if (str_eq(name, "System Info"))      { nw->x=180; nw->y=100; nw->w=280; nw->h=220; }
    else if (str_eq(name, "Keyboard Shortcuts")) { nw->x=90; nw->y=40; nw->w=620; nw->h=500; }
    else if (str_eq(name, "Dictionary"))       { nw->x=180; nw->y=80; nw->w=300; nw->h=220; g_dict_input_len=0; g_dict_input[0]=0; }
    else if (str_eq(name, "2048"))             { nw->x=150; nw->y=60; nw->w=240; nw->h=280; }
    else if (str_eq(name, "Sudoku"))           { nw->x=90; nw->y=40; nw->w=260; nw->h=320; }
    else if (str_eq(name, "Wordle"))           { nw->x=150; nw->y=40; nw->w=280; nw->h=380; }
    else if (str_eq(name, "Snake"))            { nw->x=120; nw->y=45; nw->w=316; nw->h=252; }
    else if (str_eq(name, "Breakout"))         { nw->x=100; nw->y=40; nw->w=320; nw->h=280; }
    else if (str_eq(name, "Pong"))             { nw->x=120; nw->y=40; nw->w=300; nw->h=260; }
    else if (str_eq(name, "Minesweeper"))      { nw->x=200; nw->y=60; nw->w=200; nw->h=264; }
    else if (str_eq(name, "Journal"))          { nw->x=110; nw->y=50; nw->w=380; nw->h=340; g_journal_sel=0; g_journal_focused=0; }
    else if (str_eq(name, "Contacts"))         { nw->x=150; nw->y=50; nw->w=420; nw->h=320; g_ct_sel=0; }
    else if (str_eq(name, "Preview"))          { nw->x=160; nw->y=60; nw->w=380; nw->h=300; g_preview_page=0; g_preview_zoom=100; g_preview_markup=0; }
    else if (str_eq(name, "Apple TV"))         { nw->x=120; nw->y=50; nw->w=400; nw->h=300; g_atv_sel=0; }
    term_reset_game_state(name);
    term_focus_opened_app(name);
}

static int term_open_app_named(const char *name) {
    int i;
    if (!term_app_is_known(name)) return -1;
    for (i = 0; i < g_num_windows; i++) {
        if (g_windows[i].title && (str_eq(g_windows[i].title, name) ||
            (str_eq(name, "Finder") && str_eq(g_windows[i].title, "MyOS Finder")))) {
            g_windows[i].visible = 1;
            term_focus_opened_app(name);
            win_bring_to_front(i);
            return 0;
        }
    }
    if (g_num_windows >= MAX_WINDOWS) return -2;
    {
        gui_window_t *nw = &g_windows[g_num_windows];
        nw->focused = 0;
        nw->dragging = 0;
        nw->visible = 1;
        nw->maximized = 0;
        nw->title = name;
        term_configure_app_window(nw, name);
        g_win_anim[g_num_windows] = OPEN_ANIM;
        g_win_minimized[g_num_windows] = 0;
        g_win_close_anim[g_num_windows] = 0;
        g_num_windows++;
    }
    return 0;
}

void term_process_command(void) {
    char cmd[INPUT_MAX + 1];
    int i;
    for (i = 0; i <= term_input_len; i++) cmd[i] = term_input[i];

    /* Show prompt + command in history */
    {
        char line[TERM_MAX_COL + 1];
        line[0] = '$'; line[1] = ' ';
        for (i = 0; cmd[i] && i+2 < TERM_MAX_COL; i++) line[i+2] = cmd[i];
        line[i+2] = 0;
        term_println(line);
    }

    /* Helper: check if cmd starts with prefix followed by space or end */
    #define CMD_IS(p) (str_eq(cmd,(p)))
    #define CMD_ARG(p) (cmd[0]==(p)[0]&&cmd[1]==(p)[1]&&cmd[2]==(p)[2]&&cmd[3]==(p)[3]&&(cmd[4]==' '||cmd[4]==0))
    #define CMD_ARG2(p,n) (cmd[0]==(p)[0]&&cmd[1]==(p)[1]&&cmd[2]==(p)[2]&&\
                           cmd[3]==(p)[3]&&cmd[4]==(p)[4]&&(cmd[(n)]==' '||cmd[(n)]==0))

    if (shell_execute_line(cmd, term_println)) {
        term_input_len = 0;
        term_input[0] = 0;
        return;
    }

    if (cmd[0] == 0) {
        /* empty */
    } else if (CMD_IS("hi") || CMD_IS("hello")) {
        term_println("Hello, World!");
    } else if (CMD_IS("cls") || CMD_IS("clear")) {
        int j; for (j=0;j<TERM_LINES;j++) term_history[j][0]=0; term_num_lines=0;
    } else if (CMD_IS("help")) {
        term_println("Commands:");
        term_println("  ls      ls -la     pwd    cd");
        term_println("  cat     echo       date   ver");
        term_println("  ps      top        kill");
        term_println("  uname   whoami     hostname");
        term_println("  uptime  sysinfo    neofetch");
        term_println("  mkdir   touch      rm");
        term_println("  ifconfig  ping  cls  exit");
    } else if (CMD_IS("ver") || CMD_IS("version")) {
        runtime_system_info_t sys;
        runtime_get_system_info(&sys);
        term_print3(sys.sysname, " ", sys.release);
        term_print2("Build: ", sys.version);
    } else if (CMD_IS("uname")) {
        term_print_runtime_uname(0);
    } else if (CMD_IS("uname -a")) {
        term_print_runtime_uname(1);
    } else if (CMD_IS("whoami")) {
        term_println("root");
    } else if (CMD_IS("id")) {
        term_println("uid=0(root) gid=0(root)");
    } else if (CMD_IS("hostname")) {
        runtime_system_info_t sys;
        runtime_get_system_info(&sys);
        term_println(sys.nodename);
    } else if (CMD_IS("pwd")) {
        term_println("/root");
    } else if (CMD_IS("ls")) {
        term_println("Documents/  Downloads/");
        term_println("Desktop/    Applications/");
    } else if (CMD_IS("ls -la") || CMD_IS("ls -l")) {
        term_println("total 32");
        term_println("drwxr-xr-x  root  Documents");
        term_println("drwxr-xr-x  root  Downloads");
        term_println("drwxr-xr-x  root  Desktop");
        term_println("drwxr-xr-x  root  Applications");
    } else if (CMD_IS("ls -a")) {
        term_println(".  ..  .profile  Documents/");
        term_println("Downloads/  Desktop/  Applications/");
    } else if (CMD_IS("ps") || CMD_IS("ps aux")) {
        term_print_runtime_ps();
    } else if (CMD_IS("top")) {
        term_print_runtime_top();
    } else if (CMD_IS("uptime")) {
        uint32_t up = timer_ticks() / 1000;
        uint32_t uh = up/3600, um = (up/60)%60, us = up%60;
        char buf[TERM_MAX_COL+1];
        int bi=0;
        buf[bi++]=' '; buf[bi++]='0'+uh/10; buf[bi++]='0'+uh%10;
        buf[bi++]=':'; buf[bi++]='0'+um/10; buf[bi++]='0'+um%10;
        buf[bi++]=':'; buf[bi++]='0'+us/10; buf[bi++]='0'+us%10;
        buf[bi++]=' '; buf[bi++]='u'; buf[bi++]='p'; buf[bi]=0;
        term_println(buf);
    } else if (CMD_IS("date")) {
        datetime_t dt;
        char datepart[16];
        char clockpart[12];
        char yearpart[8];
        char buf[TERM_MAX_COL+1];
        int bi=0, si=0;
        get_current_datetime(&dt);
        get_menu_date_str(datepart);
        get_clock_str(clockpart);
        int_to_str(dt.year, yearpart);
        for(si=0; datepart[si] && bi<TERM_MAX_COL; si++) buf[bi++]=datepart[si];
        if(bi<TERM_MAX_COL) buf[bi++]=' ';
        for(si=0; clockpart[si] && bi<TERM_MAX_COL; si++) buf[bi++]=clockpart[si];
        if(bi<TERM_MAX_COL) buf[bi++]=' ';
        for(si=0; yearpart[si] && bi<TERM_MAX_COL; si++) buf[bi++]=yearpart[si];
        buf[bi]=0;
        term_println(buf);
    } else if (CMD_IS("sysinfo") || CMD_IS("neofetch")) {
        term_print_runtime_sysinfo();
    } else if (CMD_IS("ifconfig")) {
        term_print_runtime_ifconfig();
    } else if (cmd[0]=='p'&&cmd[1]=='i'&&cmd[2]=='n'&&cmd[3]=='g') {
        term_println("ping: usage ping <ipv4>");
    } else if (cmd[0]=='c'&&cmd[1]=='d') {
        if (cmd[2]==' ' && cmd[3]) term_println("cd: permission denied");
        else term_println("/root");
    } else if (cmd[0]=='m'&&cmd[1]=='k'&&cmd[2]=='d'&&cmd[3]=='i'&&cmd[4]=='r') {
        if (cmd[5]==' '&&cmd[6]) {
            char path[VFS_MAX_PATH];
            term_make_home_path(cmd + 6, path, sizeof(path));
            if (vfs_mkdir(path) < 0) term_println("mkdir: failed");
            else term_println("mkdir: created");
        }
        else term_println("mkdir: missing operand");
    } else if (cmd[0]=='t'&&cmd[1]=='o'&&cmd[2]=='u'&&cmd[3]=='c'&&cmd[4]=='h') {
        if (cmd[5]==' '&&cmd[6]) {
            char path[VFS_MAX_PATH];
            term_make_home_path(cmd + 6, path, sizeof(path));
            if (vfs_create(path) < 0) {
                uint32_t now_ms = timer_ticks();
                if (vfs_utime(path, now_ms, now_ms) < 0) term_println("touch: failed");
                else term_println("touch: updated");
            } else {
                term_println("touch: created");
            }
        }
        else term_println("touch: missing file operand");
    } else if (cmd[0]=='r'&&cmd[1]=='m') {
        if (cmd[2]==' '&&cmd[3]) {
            char path[VFS_MAX_PATH];
            term_make_home_path(cmd + 3, path, sizeof(path));
            if (vfs_unlink(path) < 0) term_println("rm: failed");
            else term_println("rm: removed");
        }
        else term_println("rm: missing operand");
    } else if (cmd[0]=='c'&&cmd[1]=='a'&&cmd[2]=='t') {
        if (cmd[3]==' '&&cmd[4]) { term_println("cat: No such file"); }
        else term_println("cat: missing operand");
    } else if (cmd[0]=='k'&&cmd[1]=='i'&&cmd[2]=='l'&&cmd[3]=='l') {
        term_println("kill: no matching process");
    } else if (CMD_IS("man")) {
        term_println("What manual page do you want?");
        term_println("Try: man ls, man grep, man vi");
    } else if (CMD_IS("history")) {
        term_println("1  ls");
        term_println("2  pwd");
        term_println("3  date");
        term_println("4  help");
        term_println("5  history");
    } else if (CMD_IS("open .") || CMD_IS("open")) {
        int rc = term_open_app_named("Finder");
        if (rc == 0) term_println("open: launched");
        else if (rc == -2) term_println("open: window limit reached");
        else term_println("open: app not found");
    } else if (cmd[0]=='o'&&cmd[1]=='p'&&cmd[2]=='e'&&cmd[3]=='n'&&cmd[4]==' '&&cmd[5]) {
        const char *app_name = cmd + 5;
        int rc = term_open_app_named(app_name);
        if (rc == 0) term_println("open: launched");
        else if (rc == -2) term_println("open: window limit reached");
        else term_println("open: app not found");
    } else if (CMD_IS("gui")) {
        term_print_runtime_gui();
    } else if (CMD_IS("reboot")) {
        term_println("reboot: use the system menu");
    } else if (CMD_IS("shutdown")) {
        term_println("shutdown: use the system menu");
    } else if (CMD_IS("fortune") || CMD_IS("quote")) {
        term_println("\"Keep it simple.\" - Unix");
    } else if (CMD_IS("banner")) {
        term_println("##   ## ##  ##  ###   ###### ");
        term_println("###  ## ## ##  ## ##  ##     ");
        term_println("#### ## ####   ## ##  ####   ");
        term_println("## #### ## ##  #####  ##     ");
        term_println("##  ### ##  ## ##  ## ##     ");
        term_println("##   ## ##  ## ##  ## ######");
    } else if (CMD_IS("df") || CMD_IS("df -h")) {
        term_print_runtime_df();
    } else if (CMD_IS("free") || CMD_IS("free -m")) {
        term_print_runtime_free();
    } else if (CMD_IS("dmesg")) {
        term_print_runtime_dmesg();
    } else if (cmd[0]=='e'&&cmd[1]=='x'&&cmd[2]=='i'&&cmd[3]=='t') {
        term_println("logout");
    } else if (cmd[0]=='e'&&cmd[1]=='c'&&cmd[2]=='h'&&cmd[3]=='o') {
        if (cmd[4]==' ') term_println(cmd+5);
        else term_println("");
    } else if (CMD_IS("sw_vers")) {
        term_print_runtime_swvers();
    } else if (CMD_IS("git status") || CMD_IS("git")) {
        term_println("git: no repository in /root");
    } else if (CMD_IS("diskutil list") || CMD_IS("diskutil")) {
        term_print_runtime_diskutil();
    } else if (CMD_IS("caffeinate")) {
        g_last_input_tick = timer_ticks();
        term_println("caffeinate: session activity refreshed");
    } else if (CMD_IS("defaults")) {
        term_println("defaults: read/write preferences");
        term_println("Usage: defaults read/write <domain> <key>");
    } else if (CMD_IS("launchctl list")) {
        term_println("PID Status Label");
        term_println("1   0      kernel");
        term_println("2   0      gui");
    } else if (CMD_IS("arch")) {
        runtime_system_info_t sys;
        runtime_get_system_info(&sys);
        term_println(sys.machine);
    } else if (CMD_IS("nproc") || CMD_IS("sysctl -n hw.ncpu")) {
        runtime_system_info_t sys;
        runtime_get_system_info(&sys);
        term_print_uint_line("", sys.cpu_count);
    } else if (CMD_IS("env")) {
        term_println("HOME=/root");
        term_println("PATH=/bin:/usr/bin:/usr/local/bin");
        term_println("SHELL=/bin/sh");
        term_println("TERM=myos-console");
    } else if (cmd[0]=='g'&&cmd[1]=='r'&&cmd[2]=='e'&&cmd[3]=='p') {
        term_println("grep: missing file operand");
    } else if (CMD_IS("vi") || CMD_IS("vim") || CMD_IS("nano")) {
        term_println("Hint: open TextEdit from dock");
    } else if (CMD_IS("python") || CMD_IS("python3")) {
        term_println("python: command not found");
    } else if (CMD_IS("node") || CMD_IS("npm")) {
        term_println("node: command not found");
    } else if (CMD_IS("brew")) {
        term_println("brew: command not found");
    } else if (cmd[0]=='b'&&cmd[1]=='r'&&cmd[2]=='e'&&cmd[3]=='w'&&cmd[4]==' ') {
        term_println("brew: command not found");
    } else if (cmd[0]=='s'&&cmd[1]=='u'&&cmd[2]=='d'&&cmd[3]=='o') {
        if (cmd[4]==' ') { term_println("sudo: already root"); }
        else term_println("sudo: what do you want?");
    } else if (CMD_IS("ssh")) {
        term_println("usage: ssh [-l user] hostname");
    } else if (cmd[0]=='s'&&cmd[1]=='s'&&cmd[2]=='h'&&cmd[3]==' ') {
        term_println("ssh: connection not established");
    } else if (CMD_IS("make")) {
        term_println("make: *** No targets. Stop.");
    } else if (CMD_IS("which")) {
        term_println("which: missing argument");
    } else if (cmd[0]=='w'&&cmd[1]=='h'&&cmd[2]=='i'&&cmd[3]=='c'&&cmd[4]=='h'&&cmd[5]==' ') {
        char wbuf[TERM_MAX_COL+1];
        int wi2=0, wj=0; const char *warg=cmd+6;
        wbuf[wi2++]='/'; wbuf[wi2++]='b'; wbuf[wi2++]='i'; wbuf[wi2++]='n'; wbuf[wi2++]='/';
        while(warg[wj]&&wi2<TERM_MAX_COL) wbuf[wi2++]=warg[wj++];
        wbuf[wi2]=0; term_println(wbuf);
    } else if (CMD_IS("wc -l")) {
        term_println("0");
    } else if (cmd[0]=='f'&&cmd[1]=='i'&&cmd[2]=='n'&&cmd[3]=='d') {
        term_println("find: searching...");
        term_println("./Desktop");
        term_println("./Documents");
        term_println("./Downloads");
    } else if (CMD_IS("ls /")) {
        term_println("bin  boot  dev  etc  home");
        term_println("lib  proc  root tmp  usr  var");
    } else if (CMD_IS("ls /usr")) {
        term_println("bin  include  lib  local  share");
    } else if (CMD_IS("cat /etc/hosts")) {
        term_print_runtime_hosts();
    } else if (CMD_IS("cat /etc/passwd")) {
        term_println("root:x:0:0:root:/root:/bin/sh");
        term_println("nobody:x:99:99:nobody:/:/bin/false");
    } else if (CMD_IS("cat /proc/cpuinfo")) {
        term_print_runtime_cpuinfo();
    } else if (CMD_IS("cat /proc/meminfo")) {
        term_print_runtime_meminfo();
    } else if (CMD_IS("curl") || CMD_IS("wget")) {
        term_println("curl: try specifying a URL");
    } else if (cmd[0]=='c'&&cmd[1]=='u'&&cmd[2]=='r'&&cmd[3]=='l'&&cmd[4]==' ') {
        int ci2;
        int line_len = 0;
        char out_line[TERM_MAX_COL + 1];
        safari_load_url(cmd + 5);
        term_println(g_safari_page_status);
        out_line[0] = 0;
        for (ci2 = 0; g_safari_page_text[ci2] && line_len < TERM_MAX_COL; ci2++) {
            char ch2 = g_safari_page_text[ci2];
            if (ch2 == '\n' || ch2 == '\r') break;
            out_line[line_len++] = ch2;
        }
        out_line[line_len] = 0;
        if (line_len > 0) term_println(out_line);
    } else if (CMD_IS("tar")) {
        term_println("tar: try: tar -xzf file.tar.gz");
    } else if (CMD_IS("chmod") || CMD_IS("chown")) {
        term_println("chmod: usage MODE PATH");
    } else if (CMD_IS("ln")) {
        term_println("ln: missing file operand");
    } else if (CMD_IS("alias")) {
        term_println("alias ls='ls --color'");
        term_println("alias ll='ls -la'");
        term_println("alias grep='grep --color'");
    } else if (CMD_IS("export")) {
        term_println("export: list: HOME PATH SHELL TERM");
    } else if (CMD_IS("source") || CMD_IS(". ~/.profile")) {
        term_println("source: profile loaded");
    } else if (CMD_IS("htop")) {
        term_println("htop: use 'top' instead");
    } else if (CMD_IS("lsblk")) {
        term_print_runtime_lsblk();
    } else if (CMD_IS("mount")) {
        runtime_storage_info_t st;
        if (runtime_get_storage_info("/", &st) == 0) {
            term_print3(st.name, " on /", "");
        } else {
            term_println("mount: no filesystems");
        }
    } else if (CMD_IS("lscpu")) {
        term_print_runtime_lscpu();
    } else if (CMD_IS("ip a") || CMD_IS("ip addr")) {
        term_print_runtime_ipaddr();
    } else if (CMD_IS("netstat")) {
        term_print_runtime_netstat();
    } else if (CMD_IS("route")) {
        term_print_runtime_route();
    } else if (CMD_IS("crontab -l")) {
        term_println("# no crontab for root");
    } else if (CMD_IS("jobs")) {
        term_println("jobs: no background jobs");
    } else if (CMD_IS("bg") || CMD_IS("fg")) {
        term_println("job control: no current job");
    } else if (CMD_IS("stty size")) {
        term_print_runtime_term_size();
    } else if (CMD_IS("tput cols")) {
        term_print_uint_line("", TERM_MAX_COL);
    } else if (CMD_IS("tput lines")) {
        term_print_uint_line("", TERM_LINES);
    } else if (CMD_IS("locale")) {
        term_println("LANG=en_US.UTF-8");
        term_println("LC_ALL=en_US.UTF-8");
    } else if (CMD_IS("sleep")) {
        term_println("sleep: specify duration");
    } else if (CMD_IS("true")) {
        /* no output, exit 0 */
    } else if (CMD_IS("false")) {
        term_println("-bash: false: exit 1");
    } else {
        char err[TERM_MAX_COL+1];
        int ei=0, ci=0;
        err[ei++]='-';err[ei++]='b';err[ei++]='a';err[ei++]='s';err[ei++]='h';err[ei++]=':';err[ei++]=' ';
        while(cmd[ci]&&ei<TERM_MAX_COL-2) err[ei++]=cmd[ci++];
        err[ei++]=':';err[ei++]=' ';
        err[ei++]='c';err[ei++]='o';err[ei++]='m';err[ei++]='m';err[ei++]='a';err[ei++]='n';err[ei++]='d';
        err[ei++]=' ';err[ei++]='n';err[ei++]='o';err[ei++]='t';err[ei++]=' ';
        err[ei++]='f';err[ei++]='o';err[ei++]='u';err[ei++]='n';err[ei++]='d';err[ei]=0;
        term_println(err);
    }

    term_input_len = 0;
    term_input[0] = 0;
}

/* =========================================================================
 * Tiny string helpers (no libc)
 * ======================================================================= */
int str_len(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

void str_cpy(char *dst, const char *src) {
    int i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void int_to_str(int n, char *buf) {
    uint32_t mag;
    int i=0, neg=0;
    char tmp[12];
    if (!buf) return;
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    if (n < 0) {
        neg=1;
        mag = (uint32_t)(-(n + 1)) + 1U;
    } else {
        mag = (uint32_t)n;
    }
    while (mag > 0) { tmp[i++]=(char)('0'+(mag%10U)); mag/=10U; }
    if (neg) tmp[i++]='-';
    int j;
    for (j=0; j<i; j++) buf[j]=tmp[i-1-j];
    buf[i]=0;
}

/* =========================================================================
 * Cursor: 12x19 white arrow with 1-px black outline
 *
 * Each row is a 12-bit mask (bit 11 = leftmost column).
 * cursor_fg  = white fill pixels
 * cursor_out = black outline pixels (union of fg + 1-px dilation)
 * ======================================================================= */
#define CURSOR_W  12
#define CURSOR_H  19

static const uint16_t cursor_fg[CURSOR_H] = {
    0x800,  /* row  0 */
    0xC00,  /* row  1 */
    0xE00,  /* row  2 */
    0xF00,  /* row  3 */
    0xF80,  /* row  4 */
    0xFC0,  /* row  5 */
    0xFE0,  /* row  6 */
    0xFF0,  /* row  7 */
    0xFF8,  /* row  8 */
    0xFFC,  /* row  9 */
    0xFE0,  /* row 10 */
    0xEE0,  /* row 11 */
    0xCE0,  /* row 12 */
    0x060,  /* row 13 */
    0x060,  /* row 14 */
    0x030,  /* row 15 */
    0x030,  /* row 16 */
    0x018,  /* row 17 */
    0x000   /* row 18 */
};

static const uint16_t cursor_out[CURSOR_H] = {
    0xC00,
    0xE00,
    0xF00,
    0xF80,
    0xFC0,
    0xFE0,
    0xFF0,
    0xFF8,
    0xFFC,
    0xFFE,
    0xFFF,
    0xFF0,
    0xFF0,
    0x1F0,
    0x0F0,
    0x078,
    0x078,
    0x03C,
    0x018
};

/* saved background so the cursor can be erased without a full redraw */
static uint32_t cursor_bg[CURSOR_W * CURSOR_H];
static int      cursor_bx = -1, cursor_by = -1;

void cursor_save(int x, int y) {
    int cx, cy;
    for (cy = 0; cy < CURSOR_H; cy++)
        for (cx = 0; cx < CURSOR_W; cx++)
            cursor_bg[cy * CURSOR_W + cx] = vga_get_pixel(x + cx, y + cy);
    cursor_bx = x;
    cursor_by = y;
}

void cursor_restore(void) {
    if (cursor_bx < 0) return;
    vga_blit(cursor_bx, cursor_by, CURSOR_W, CURSOR_H, cursor_bg);
    cursor_bx = -1;
    cursor_by = -1;
}

void gui_draw_cursor(int x, int y) {
    int cx, cy;
    for (cy = 0; cy < CURSOR_H; cy++) {
        for (cx = 0; cx < CURSOR_W; cx++) {
            uint16_t bit = (uint16_t)(0x800u >> cx);
            if (cursor_fg[cy] & bit)
                vga_put_pixel(x + cx, y + cy, COLOR_WHITE);
            else if (cursor_out[cy] & bit)
                vga_put_pixel(x + cx, y + cy, COLOR_BLACK);
        }
    }
}

/* =========================================================================
 * Primitive drawing helpers
 * ======================================================================= */


/* =========================================================================
 * Widget Bar (F7) - horizontal strip with mini-widgets
 * ======================================================================= */
#define WIDGET_BAR_H  90
void widget_bar_draw(void) {
    int wy = MENUBAR_H + 4;
    int wx = 10;
    /* Frosted glass background */
    vga_fill_rect_alpha(wx-4, wy-4, VGA_WIDTH-12, WIDGET_BAR_H+8, RGB(0,0,0), 40);
    vga_fill_rect_alpha(wx-4, wy-4, VGA_WIDTH-12, WIDGET_BAR_H+8, RGB(255,255,255), 160);
    vga_draw_rect_outline(wx-4, wy-4, VGA_WIDTH-12, WIDGET_BAR_H+8, RGB(180,180,190));

    /* Widget 1: Clock/Date */
    {
        int wdx=wx, wdy=wy;
        vga_fill_rect_alpha(wdx, wdy, 130, WIDGET_BAR_H, RGB(50,50,200), 180);
        gui_draw_rounded_rect_outline(wdx, wdy, 130, WIDGET_BAR_H, 6, RGB(100,100,255));
        char clk2[16]; get_clock_str(clk2);
        /* Big 2x clock */
        int ci2; int clkx=wdx+10;
        for (ci2=0; clk2[ci2]; ci2++) {
            unsigned char ch3=(unsigned char)clk2[ci2];
            int row3,col3;
            if (ch3>=128) ch3='?';
            for(row3=0;row3<8;row3++) for(col3=0;col3<8;col3++)
                if(font8x8[ch3][row3]&(1u<<col3))
                    vga_fill_rect(clkx+col3*2, wdy+12+row3*2, 2, 2, RGB(255,255,255));
            clkx+=16;
        }
        { char datebuf[32];
          char weekbuf[24];
          get_date_short_str(datebuf);
          get_week_of_year_str(weekbuf);
          vga_draw_string_trans(wdx+10, wdy+56, datebuf, RGB(200,220,255));
          vga_draw_string_trans(wdx+10, wdy+70, weekbuf, RGB(160,180,220));
        }
    }

    /* Widget 2: Weather */
    {
        int wdx=wx+144, wdy=wy;
        runtime_weather_info_t weather;
        char tempdigits[8];
        char hlbuf[20];
        runtime_get_weather_info(&weather);
        runtime_format_uint((uint32_t)weather.temperature_c, tempdigits, sizeof(tempdigits));
        runtime_format_high_low(weather.high_c, weather.low_c, hlbuf, sizeof(hlbuf));
        vga_fill_rect_alpha(wdx, wdy, 160, WIDGET_BAR_H, RGB(40,160,240), 200);
        gui_draw_rounded_rect_outline(wdx, wdy, 160, WIDGET_BAR_H, 6, RGB(100,200,255));
        vga_draw_string_trans(wdx+8, wdy+8, weather.location_full, RGB(255,255,255));
        /* Big temperature */
        vga_draw_string_trans(wdx+8,  wdy+22, tempdigits, RGB(255,255,255));
        { int ci3; for(ci3=0;tempdigits[ci3];ci3++) { /* 2x digit */
            unsigned char ch3=(unsigned char)tempdigits[ci3];
            int r3,c3;
            for(r3=0;r3<8;r3++) for(c3=0;c3<8;c3++)
                if(font8x8[ch3][r3]&(1u<<c3))
                    vga_fill_rect(wdx+8+ci3*16+c3*2,wdy+20+r3*2,2,2,RGB(255,255,255));
        }}
        vga_draw_string_trans(wdx+8+str_len(tempdigits)*16+8, wdy+20, "C", RGB(200,240,255));
        vga_draw_string_trans(wdx+8, wdy+52, weather.condition, RGB(220,240,255));
        /* Sun */
        gui_draw_circle(wdx+135, wdy+25, 18, RGB(255,200,50));
        gui_draw_circle(wdx+125, wdy+22, 16, RGB(80,180,240));
        vga_draw_string_trans(wdx+8, wdy+66, hlbuf, RGB(180,220,255));
        vga_draw_string_trans(wdx+8, wdy+78, weather.next_summary, RGB(160,200,240));
    }

    /* Widget 3: System Stats (dynamic) */
    {
        int wdx=wx+320, wdy=wy;
        runtime_system_info_t sys;
        runtime_storage_info_t storage;
        runtime_power_info_t power;
        int disk_pct = 0;
        char pctbuf[8];
        runtime_get_system_info(&sys);
        runtime_get_power_info(&power);
        if (runtime_get_storage_info("/", &storage) == 0) disk_pct = storage.used_percent;
        vga_fill_rect_alpha(wdx, wdy, 150, WIDGET_BAR_H, RGB(30,30,35), 220);
        gui_draw_rounded_rect_outline(wdx, wdy, 150, WIDGET_BAR_H, 6, RGB(80,80,90));
        vga_draw_string_trans(wdx+8, wdy+6, "System", RGB(180,180,200));
        vga_draw_string_trans(wdx+8, wdy+22, "CPU", RGB(120,120,140));
        vga_fill_rect(wdx+40, wdy+24, 100, 6, RGB(50,50,60));
        vga_fill_rect(wdx+40, wdy+24, sys.cpu_load_percent, 6, RGB(100,200,100));
        runtime_format_percent(sys.cpu_load_percent, pctbuf, sizeof(pctbuf));
        vga_draw_string_trans(wdx+142, wdy+20, pctbuf, RGB(100,200,100));
        vga_draw_string_trans(wdx+8, wdy+36, "RAM", RGB(120,120,140));
        vga_fill_rect(wdx+40, wdy+38, 100, 6, RGB(50,50,60));
        vga_fill_rect(wdx+40, wdy+38, sys.mem_used_percent, 6, RGB(100,140,255));
        runtime_format_percent(sys.mem_used_percent, pctbuf, sizeof(pctbuf));
        vga_draw_string_trans(wdx+142, wdy+34, pctbuf, RGB(100,140,255));
        vga_draw_string_trans(wdx+8, wdy+50, "Dsk", RGB(120,120,140));
        vga_fill_rect(wdx+40, wdy+52, 100, 6, RGB(50,50,60));
        vga_fill_rect(wdx+40, wdy+52, disk_pct, 6, RGB(255,180,50));
        runtime_format_percent(disk_pct, pctbuf, sizeof(pctbuf));
        vga_draw_string_trans(wdx+142, wdy+48, pctbuf, RGB(255,180,50));
        vga_draw_string_trans(wdx+8, wdy+64, "Bat", RGB(120,120,140));
        vga_fill_rect(wdx+40, wdy+66, 100, 6, RGB(50,50,60));
        vga_fill_rect(wdx+40, wdy+66, power.percent, 6, RGB(52,199,89));
        runtime_format_percent(power.percent, pctbuf, sizeof(pctbuf));
        vga_draw_string_trans(wdx+142, wdy+62, pctbuf, RGB(52,199,89));
        { char upb[18];
          runtime_format_uptime(sys.uptime_seconds, upb, sizeof(upb));
          vga_draw_string_trans(wdx+8, wdy+78, "Up", RGB(100,100,120));
          vga_draw_string_trans(wdx+28, wdy+78, upb, RGB(100,100,120)); }
    }

    /* Widget 4: Quick Notes */
    {
        int wdx=wx+486, wdy=wy;
        vga_fill_rect_alpha(wdx, wdy, 160, WIDGET_BAR_H, RGB(255,245,150), 230);
        gui_draw_rounded_rect_outline(wdx, wdy, 160, WIDGET_BAR_H, 6, RGB(200,190,80));
        vga_draw_string_trans(wdx+8, wdy+6, "Quick Notes", RGB(80,80,60));
        vga_draw_hline(wdx+4, wdy+18, 152, RGB(200,190,80));
        {
            int ni;
            for (ni = 0; ni < 4 && ni < NOTES_COUNT; ni++) {
                char note_line[24];
                const char *title = g_notes_titles[ni][0] ? g_notes_titles[ni] : "Untitled";
                int np = 0;
                int ti = 0;
                note_line[np++] = '-';
                note_line[np++] = ' ';
                while (title[ti] && np + 1 < 20) note_line[np++] = title[ti++];
                note_line[np] = 0;
                vga_draw_string_trans(wdx+8, wdy+22+ni*12, note_line, RGB(60,60,40));
            }
        }
        vga_draw_string_trans(wdx+8, wdy+72, "+ Add note...", RGB(150,150,120));
    }

    /* Widget 5: Now Playing mini */
    {
        int wdx=wx+660, wdy=wy;
        if (wdx+120 < VGA_WIDTH-6) {
            vga_fill_rect_alpha(wdx, wdy, VGA_WIDTH-wdx-6, WIDGET_BAR_H, RGB(60,10,10), 220);
            gui_draw_rounded_rect_outline(wdx, wdy, VGA_WIDTH-wdx-6, WIDGET_BAR_H, 6, RGB(150,40,40));
            vga_draw_string_trans(wdx+8, wdy+6, "Now Playing", RGB(255,100,100));
            static const char *sn[] = {"Midnight Drive","Neon Pulse","Starfall","Cyberwave","Solar Wind"};
            vga_draw_string_trans(wdx+8, wdy+22, sn[g_music_track%5], RGB(255,200,200));
            vga_draw_string_trans(wdx+8, wdy+34, "Synthwave Dreams", RGB(180,100,100));
            /* Mini progress bar - dynamic */
            { int mbar_w = VGA_WIDTH-wdx-22;
              int mpos = g_music_playing ? (int)((timer_ticks()/1000) % 227) * mbar_w / 227 : mbar_w*3/7;
              vga_fill_rect(wdx+8, wdy+50, mbar_w, 3, RGB(80,30,30));
              vga_fill_rect(wdx+8, wdy+50, mpos, 3, RGB(252,60,68)); }
            /* Play/Pause symbol */
            if (g_music_playing) {
                vga_fill_rect(wdx+8, wdy+60, 5, 12, RGB(255,255,255));
                vga_fill_rect(wdx+16,wdy+60, 5, 12, RGB(255,255,255));
            } else {
                int pi4; for(pi4=0;pi4<12;pi4++) vga_draw_hline(wdx+8, wdy+60+pi4, pi4/2, RGB(255,255,255));
            }
            vga_draw_string_trans(wdx+28, wdy+62, g_music_playing ? "Playing" : "Paused", RGB(180,100,100));
        }
    }
}

/* =========================================================================
 * App Switcher overlay (F5)
 * ======================================================================= */
/* Siri overlay */
void siri_draw(void) {
    uint32_t now = timer_ticks();
    uint32_t age = now - g_siri_birth;

    /* Frosted dark overlay at bottom */
    int sw3 = 360, sh3 = 140;
    int sx3 = (VGA_WIDTH - sw3) / 2;
    int sy3 = VGA_HEIGHT - sh3 - DOCK_H - 8;

    /* Shadow */
    vga_fill_rect_alpha(sx3+4, sy3+4, sw3, sh3, RGB(0,0,0), 60);

    /* Panel */
    vga_fill_rect_alpha(sx3, sy3, sw3, sh3, RGB(20,20,28), 240);
    gui_draw_rounded_rect_outline(sx3, sy3, sw3, sh3, 12, RGB(80,80,120));

    /* Siri orb - animated glowing circle */
    int orb_x = sx3 + sw3/2, orb_y = sy3 + 42;
    int orb_r = 28;
    /* Outer glow rings (animated) */
    int pulse = (int)((age / 30) % 20);
    uint8_t ga = (uint8_t)(80 + pulse * 4);
    vga_fill_rect_alpha(orb_x - orb_r-8, orb_y - orb_r-8, (orb_r+8)*2, (orb_r+8)*2, RGB(100,120,255), ga/3);
    /* Colored gradient orb: blue→purple gradient */
    { int ri;
      for (ri = orb_r; ri >= 0; ri--) {
          uint8_t rr = (uint8_t)(50 + ri*4);
          uint8_t gg = (uint8_t)(80 + ri*2);
          uint8_t bb = (uint8_t)(255 - ri*2);
          gui_draw_circle(orb_x, orb_y, ri, RGB(rr,gg,bb));
      }
    }
    /* Animated waveform arms around orb */
    { int wi;
      int n_waves = 8;
      for (wi = 0; wi < n_waves; wi++) {
          int wph = (int)((age/20 + wi * 8) % 60);
          static const int sineT[60] = {
              0,10,19,29,37,44,50,56,59,62,64,65,64,62,59,56,
              50,44,37,29,19,10,0,-10,-19,-29,-37,-44,-50,-56,
             -59,-62,-64,-65,-64,-62,-59,-56,-50,-44,-37,-29,
             -19,-10,0,10,19,29,37,44,50,56,59,62,64,65,64,62,
              59,56
          };
          int amp = 6 + sineT[wph] * 4 / 65;
          int angle_frac = wi * 8; /* 0..64 → 0..360 degrees in 8ths */
          /* Approximate cos/sin with lookup */
          int cos8[8] = {8,6,0,-6,-8,-6,0,6};
          int sin8[8] = {0,6,8,6,0,-6,-8,-6};
          int cx2 = orb_x + (orb_r+2)*cos8[wi]/8;
          int cy2 = orb_y + (orb_r+2)*sin8[wi]/8;
          int ex2 = cx2 + amp*cos8[wi]/8;
          int ey2 = cy2 + amp*sin8[wi]/8;
          (void)angle_frac;
          vga_draw_hline(cx2<ex2?cx2:ex2, cy2, (cx2<ex2?ex2-cx2:cx2-ex2)+1, RGB(180,140,255));
          vga_draw_vline(ex2, cy2<ey2?cy2:ey2, (cy2<ey2?ey2-cy2:cy2-ey2)+1, RGB(180,140,255));
      }
    }
    /* White "S" letter on orb */
    vga_draw_string_trans(orb_x-4, orb_y-4, "S", RGB(255,255,255));

    /* Text area */
    if (!g_siri_response) {
        /* Listening state */
        static const char *listmsg[] = {"Listening...", "How can I help?", "Say something..."};
        int lmi = (int)((age / 1200) % 3);
        int lmw = (int)str_len(listmsg[lmi]) * 8;
        vga_draw_string_trans(sx3+(sw3-lmw)/2, sy3+82, listmsg[lmi], RGB(180,180,220));
        /* Animated dot indicator */
        { int di3;
          for (di3=0; di3<3; di3++) {
              int dot_ph = (int)((age/150 + di3*20) % 60);
              static const int damp[60] = {
                  0,5,9,13,17,20,22,24,26,27,28,27,26,24,22,20,
                  17,13,9,5,0,-5,-9,-13,-17,-20,-22,-24,-26,-27,
                 -28,-27,-26,-24,-22,-20,-17,-13,-9,-5,0,5,9,13,
                  17,20,22,24,26,27,28,27,26,24,22,20,17,13,9,5
              };
              int dy3 = 3 - damp[dot_ph]*3/28;
              gui_draw_circle(sx3+sw3/2-16+di3*16, sy3+100+dy3, 4, RGB(100,120,255));
          }
        }
        /* Query text if any */
        if (g_siri_qlen > 0) {
            int qw = g_siri_qlen * 8;
            vga_draw_string_trans(sx3+(sw3-qw)/2, sy3+115, g_siri_query, RGB(255,255,255));
        }
    } else {
        /* Response state */
        char dyn_resp[80];
        int dyn_pos = 0;
        int ri2 = (int)((g_siri_resp_tick / 2000) % 5);
        dyn_resp[0] = 0;
        if (ri2 == 0) {
            char clk[12];
            get_clock_str(clk);
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), "It is ");
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), clk);
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), ".");
        } else if (ri2 == 1) {
            runtime_weather_info_t weather;
            char temp[8];
            runtime_get_weather_info(&weather);
            runtime_format_temperature_c(weather.temperature_c, temp, sizeof(temp));
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), "Weather: ");
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), weather.condition);
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), " ");
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), temp);
        } else if (ri2 == 2) {
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), "Opening Settings...");
        } else if (ri2 == 3) {
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), "I found results.");
        } else {
            overlay_append_text(dyn_resp, &dyn_pos, sizeof(dyn_resp), "Here's what I found:");
        }
        vga_draw_string_trans(sx3+16, sy3+82, "Siri:", RGB(140,140,240));
        vga_draw_string_trans(sx3+48, sy3+82, dyn_resp, RGB(220,220,255));
        vga_draw_string_trans(sx3+(sw3-80)/2, sy3+110, "Press Esc to close", RGB(100,100,140));
    }
}

/* Writing Tools overlay (macOS Sequoia) */
void writing_tools_draw(void) {
    static const char *tools[]={
        "Proofread","Rewrite","Make Friendly","Make Professional","Make Concise","Summarize"
    };
    static const char *icons[]={ "P","R","F","B","C","S" };
    static const uint32_t icols[]={
        RGB(0,122,255),RGB(52,199,89),RGB(255,149,0),RGB(147,44,246),RGB(255,59,48),RGB(30,200,220)
    };
    int wt_w=300, wt_h=220;
    int wt_x=(VGA_WIDTH-wt_w)/2, wt_y=(VGA_HEIGHT-wt_h)/2;
    uint32_t wt_bg  = g_pref_darkmode ? RGB(34,34,40) : RGB(248,248,252);
    uint32_t wt_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
    uint32_t wt_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
    uint32_t wt_sep = g_pref_darkmode ? RGB(54,54,62)   : RGB(200,200,208);
    /* Dim background */
    vga_fill_rect_alpha(0,0,VGA_WIDTH,VGA_HEIGHT,RGB(0,0,0),120);
    /* Panel */
    vga_fill_rect_alpha(wt_x+4, wt_y+4, wt_w, wt_h, RGB(0,0,0), 60);
    gui_draw_rounded_rect(wt_x, wt_y, wt_w, wt_h, 10, wt_bg);
    vga_draw_rect_outline(wt_x, wt_y, wt_w, wt_h, wt_sep);
    /* Title */
    vga_fill_rect(wt_x+1, wt_y+1, wt_w-2, 26, g_pref_darkmode?RGB(42,42,50):RGB(236,236,240));
    vga_draw_hline(wt_x, wt_y+26, wt_w, wt_sep);
    /* Writing Tools icon (pencil + sparkle) */
    gui_draw_circle(wt_x+16, wt_y+13, 8, RGB(147,44,246));
    vga_draw_string_trans(wt_x+12, wt_y+9, "Wt", RGB(255,255,255));
    vga_draw_string_trans(wt_x+28, wt_y+9, "Writing Tools", wt_txt);
    vga_draw_string_trans(wt_x+wt_w-18, wt_y+9, "X", wt_sub);
    if (g_wt_done == 2 && g_wt_sel >= 0) {
        const char *done_tool = tools[g_wt_sel % 6];
        vga_draw_string_trans(wt_x+8, wt_y+34, done_tool, wt_txt);
        vga_draw_string_trans(wt_x+8, wt_y+52, g_wt_result, wt_sub);
        gui_draw_rounded_rect(wt_x+wt_w/2-30, wt_y+wt_h-24, 60, 18, 4, RGB(0,122,255));
        vga_draw_string_trans(wt_x+wt_w/2-14, wt_y+wt_h-19, "Done", RGB(255,255,255));
    } else if (g_wt_done == 1) {
        g_wt_done=2;
        vga_fill_rect(wt_x+8, wt_y+50, wt_w-16, 8, wt_sep);
    } else {
        /* Tool buttons grid */
        int bi2, bx2=wt_x+8, by2=wt_y+32;
        for (bi2=0;bi2<6;bi2++){
            int brow=bi2/3, bcol=bi2%3;
            int bxp=bx2+bcol*((wt_w-16)/3), byp=by2+brow*52;
            int bw2=(wt_w-16)/3-4, bh2=46;
            gui_draw_rounded_rect(bxp, byp, bw2, bh2, 6,
                g_pref_darkmode?RGB(44,44,52):RGB(240,240,244));
            vga_draw_rect_outline(bxp, byp, bw2, bh2, wt_sep);
            /* Icon */
            gui_draw_circle(bxp+bw2/2, byp+14, 10, icols[bi2]);
            vga_draw_string_trans(bxp+bw2/2-4, byp+10, icons[bi2], RGB(255,255,255));
            /* Label */
            { int tl2=str_len(tools[bi2])*8;
              int lx2=bxp+(bw2-tl2)/2;
              if (lx2<bxp) lx2=bxp+2;
              vga_draw_string_trans(lx2, byp+28, tools[bi2], wt_txt); }
        }
        /* Hint */
        vga_draw_string_trans(wt_x+8, wt_y+wt_h-14, "Click a tool to apply to selection", wt_sub);
    }
}

/* Quick Note floating panel */
void quick_note_draw(void) {
    int qn_w=200, qn_h=140;
    int qn_x=VGA_WIDTH-qn_w-10, qn_y=MENUBAR_H+30;
    uint32_t qn_bg=RGB(255,240,110);
    uint32_t qn_txt=RGB(30,20,0);
    uint32_t qn_sep=RGB(220,200,60);
    /* Shadow */
    vga_fill_rect_alpha(qn_x+3, qn_y+3, qn_w, qn_h, RGB(0,0,0), 60);
    /* Note body */
    vga_fill_rect(qn_x, qn_y, qn_w, qn_h, qn_bg);
    vga_draw_rect_outline(qn_x, qn_y, qn_w, qn_h, qn_sep);
    /* Fold corner */
    vga_fill_rect(qn_x+qn_w-14, qn_y, 14, 14, RGB(220,200,60));
    vga_draw_line(qn_x+qn_w-14, qn_y, qn_x+qn_w, qn_y+14, qn_sep);
    /* Title bar */
    vga_fill_rect(qn_x, qn_y, qn_w-14, 18, RGB(240,220,80));
    vga_draw_hline(qn_x, qn_y+18, qn_w, qn_sep);
    vga_draw_string_trans(qn_x+4, qn_y+5, "Quick Note", qn_txt);
    vga_draw_string_trans(qn_x+qn_w-22, qn_y+5, "X", qn_txt);
    /* Note lines */
    { int li8, ly8=qn_y+22;
      for (li8=0;li8<5;li8++){
          vga_draw_hline(qn_x+2, ly8, qn_w-4, RGB(200,180,60));
          ly8+=18;
      }
    }
    /* Text content */
    { int ty9=qn_y+24, qi=0;
      int row8=0;
      char line8[26]={0};
      int col8=0;
      while (qi<g_qn_len && row8<5){
          if (g_qn_text[qi]=='\n'||col8>=24){
              line8[col8]=0;
              vga_draw_string_trans(qn_x+4, ty9+row8*18, line8, qn_txt);
              row8++; col8=0; line8[0]=0;
              if (g_qn_text[qi]=='\n') qi++;
          } else {
              line8[col8++]=g_qn_text[qi++];
          }
      }
      if (col8>0 && row8<5){
          line8[col8]=0;
          vga_draw_string_trans(qn_x+4, ty9+row8*18, line8, qn_txt);
      }
    }
    /* Blinking cursor */
    { uint32_t t9=timer_ticks();
      if ((t9/400)%2==0) vga_fill_rect(qn_x+4, qn_y+qn_h-12, 2, 10, qn_txt);
    }
}

/* Quick Look overlay (Space bar) */
void quick_look_draw(void) {
    int ql_w = 400, ql_h = 300;
    int ql_x = (VGA_WIDTH - ql_w) / 2;
    int ql_y = (VGA_HEIGHT - ql_h) / 2;

    /* Dark overlay */
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 180);

    /* Panel */
    vga_fill_rect(ql_x, ql_y, ql_w, ql_h, RGB(245,245,248));
    gui_draw_rounded_rect_outline(ql_x, ql_y, ql_w, ql_h, 8, RGB(180,180,185));

    /* Title bar */
    vga_fill_rect(ql_x, ql_y, ql_w, 28, RGB(220,220,225));
    vga_draw_hline(ql_x, ql_y+28, ql_w, RGB(180,180,185));
    { int nl=str_len(g_ql_filename)*8;
      vga_draw_string_trans(ql_x+(ql_w-nl)/2, ql_y+10, g_ql_filename, RGB(50,50,50)); }
    /* Close button */
    gui_draw_circle(ql_x+14, ql_y+14, 7, RGB(255,59,48));
    vga_draw_string_trans(ql_x+11, ql_y+10, "x", RGB(150,0,0));

    /* File preview area */
    vga_fill_rect(ql_x+8, ql_y+34, ql_w-16, ql_h-70, RGB(255,255,255));

    /* Determine file type from extension */
    const char *dot = 0;
    int fi;
    for(fi=0;g_ql_filename[fi];fi++) if(g_ql_filename[fi]=='.') dot=g_ql_filename+fi;

    if(dot && (str_eq(dot,".txt") || str_eq(dot,".md"))) {
        /* Text file preview */
        vga_draw_string_trans(ql_x+14, ql_y+42, "Welcome to TextEdit!", RGB(30,30,30));
        vga_draw_string_trans(ql_x+14, ql_y+56, "Start typing...", RGB(80,80,80));
        vga_draw_string_trans(ql_x+14, ql_y+74, "This is a plain text file.", RGB(80,80,80));
    } else if(dot && str_eq(dot,".png")) {
        /* Image preview - gradient rectangle */
        int img_y;
        for(img_y=0;img_y<ql_h-80;img_y++)
            vga_draw_hline(ql_x+8, ql_y+34+img_y, ql_w-16,
                RGB((uint8_t)(100+img_y*80/(ql_h-80)),
                    (uint8_t)(80+img_y*60/(ql_h-80)),
                    (uint8_t)(200-img_y*80/(ql_h-80))));
        vga_draw_string_trans(ql_x+ql_w/2-20, ql_y+ql_h/2-10, "Image", RGB(255,255,255));
    } else {
        /* Generic document icon */
        int di_x=ql_x+ql_w/2-24, di_y=ql_y+50;
        vga_fill_rect(di_x, di_y, 48, 60, RGB(220,230,255));
        vga_draw_rect_outline(di_x, di_y, 48, 60, RGB(150,160,200));
        vga_fill_rect(di_x+30, di_y, 18, 18, RGB(245,248,255));
        vga_draw_string_trans(di_x+6, di_y+25, dot?dot+1:"file", RGB(100,110,160));
        vga_draw_string_trans(ql_x+14, ql_y+130, g_ql_filename, RGB(60,60,60));
    }

    /* Bottom toolbar */
    vga_fill_rect(ql_x, ql_y+ql_h-30, ql_w, 30, RGB(220,220,225));
    vga_draw_hline(ql_x, ql_y+ql_h-30, ql_w, RGB(180,180,185));
    vga_draw_string_trans(ql_x+ql_w/2-20, ql_y+ql_h-20, "Open", RGB(0,80,200));
    vga_draw_string_trans(ql_x+14, ql_y+ql_h-20, "Share", RGB(100,100,100));
    vga_draw_string_trans(ql_x+ql_w-54, ql_y+ql_h-20, "Done", RGB(100,100,100));
}

void share_sheet_draw(void) {
    /* Bottom sheet, slides up */
    int ss_w = 280, ss_h = 220;
    int ss_x = (VGA_WIDTH - ss_w) / 2;
    int ss_y = VGA_HEIGHT - ss_h - 28; /* above dock */
    uint32_t ss_bg  = g_pref_darkmode ? RGB(36,36,40)    : RGB(250,250,254);
    uint32_t ss_hd  = g_pref_darkmode ? RGB(50,50,55)    : RGB(235,235,240);
    uint32_t ss_sep = g_pref_darkmode ? RGB(65,65,70)    : RGB(200,200,205);
    uint32_t ss_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
    uint32_t ss_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
    uint32_t ss_acc = RGB(0,122,255);
    /* Shadow */
    vga_fill_rect_alpha(ss_x+4, ss_y+4, ss_w, ss_h, RGB(0,0,0), 80);
    /* Panel */
    vga_fill_rect(ss_x, ss_y, ss_w, ss_h, ss_bg);
    gui_draw_rounded_rect_outline(ss_x, ss_y, ss_w, ss_h, 8, ss_sep);
    /* Handle bar */
    vga_fill_rect(ss_x+ss_w/2-20, ss_y+6, 40, 4, ss_sep);
    gui_draw_rounded_rect_outline(ss_x+ss_w/2-20, ss_y+6, 40, 4, 2, ss_sep);
    /* Header */
    vga_fill_rect(ss_x, ss_y, ss_w, 28, ss_hd);
    gui_draw_rounded_rect_outline(ss_x, ss_y, ss_w, 28, 8, ss_sep);
    vga_fill_rect(ss_x, ss_y+14, ss_w, 14, ss_hd); /* bottom straight */
    vga_draw_hline(ss_x, ss_y+28, ss_w, ss_sep);
    { int hl = str_len("Share")*8; vga_draw_string_trans(ss_x+(ss_w-hl)/2, ss_y+10, "Share", ss_txt); }
    /* Close button */
    vga_fill_rect(ss_x+ss_w-26, ss_y+6, 18, 16, ss_sep);
    gui_draw_rounded_rect_outline(ss_x+ss_w-26, ss_y+6, 18, 16, 3, ss_sep);
    vga_draw_string_trans(ss_x+ss_w-22, ss_y+9, "X", ss_sub);
    /* App icons row */
    static const char *share_apps[] = { "Mail", "Msgs", "Notes", "AirDrop", "Copy", "More" };
    static const uint32_t share_cols[] = {
        RGB(0,122,255), RGB(52,199,89), RGB(255,214,10),
        RGB(0,190,255), RGB(120,120,128), RGB(150,150,160)
    };
    int sa_y = ss_y + 36, sa_icon_w = ss_w / 6;
    int si3;
    for (si3=0; si3<6; si3++) {
        int ix2 = ss_x + si3*sa_icon_w + sa_icon_w/2;
        gui_draw_circle(ix2, sa_y+14, 14, share_cols[si3]);
        int llen = str_len(share_apps[si3])*8;
        if (llen > sa_icon_w) llen = sa_icon_w;
        vga_draw_string_trans(ix2-llen/2, sa_y+30, share_apps[si3], ss_sub);
    }
    /* Separator */
    vga_draw_hline(ss_x+8, ss_y+88, ss_w-16, ss_sep);
    /* Action rows */
    static const char *actions[] = { "Copy Link", "Save to Files", "Print", "Add to Notes" };
    int ai2;
    for (ai2=0; ai2<4; ai2++) {
        int ay = ss_y+96+ai2*22;
        vga_draw_string_trans(ss_x+12, ay+5, actions[ai2], ss_txt);
        vga_draw_hline(ss_x+8, ay+20, ss_w-16, ss_sep);
    }
    if (g_share_action_count > 0) {
        char status[48];
        int sp = 0;
        status[0] = 0;
        overlay_append_text(status, &sp, sizeof(status), "Last: ");
        overlay_append_text(status, &sp, sizeof(status), g_share_last_action);
        vga_draw_string_trans(ss_x+12, ss_y+184, status, ss_sub);
    }
    /* Done button at bottom */
    int done_y = ss_y+ss_h-26;
    vga_fill_rect(ss_x+8, done_y, ss_w-16, 20, ss_acc);
    gui_draw_rounded_rect_outline(ss_x+8, done_y, ss_w-16, 20, 4, ss_acc);
    { int dl = str_len("Done")*8; vga_draw_string_trans(ss_x+(ss_w-dl)/2, done_y+6, "Done", RGB(255,255,255)); }
}

void print_dialog_draw(void) {
    int pd_w = 340, pd_h = 260;
    int pd_x = (VGA_WIDTH - pd_w) / 2;
    int pd_y = (VGA_HEIGHT - pd_h) / 2;
    uint32_t pd_bg  = g_pref_darkmode ? RGB(38,38,42)    : RGB(248,248,252);
    uint32_t pd_hd  = g_pref_darkmode ? RGB(52,52,58)    : RGB(232,232,238);
    uint32_t pd_sep = g_pref_darkmode ? RGB(68,68,74)    : RGB(198,198,205);
    uint32_t pd_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
    uint32_t pd_sub = g_pref_darkmode ? RGB(128,128,136) : RGB(100,100,110);
    uint32_t pd_acc = RGB(0,122,255);
    /* Dim overlay */
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 140);
    /* Shadow */
    vga_fill_rect_alpha(pd_x+5, pd_y+5, pd_w, pd_h, RGB(0,0,0), 100);
    /* Panel */
    vga_fill_rect(pd_x, pd_y, pd_w, pd_h, pd_bg);
    gui_draw_rounded_rect_outline(pd_x, pd_y, pd_w, pd_h, 6, pd_sep);
    /* Header */
    vga_fill_rect(pd_x, pd_y, pd_w, 30, pd_hd);
    vga_fill_rect(pd_x, pd_y+20, pd_w, 10, pd_hd); /* bottom straight */
    gui_draw_rounded_rect_outline(pd_x, pd_y, pd_w, 30, 6, pd_sep);
    vga_draw_hline(pd_x, pd_y+30, pd_w, pd_sep);
    { int hl=str_len("Print")*8; vga_draw_string_trans(pd_x+(pd_w-hl)/2, pd_y+11, "Print", pd_txt); }
    /* Printer preview (left side) */
    int prev_x = pd_x+8, prev_y = pd_y+38, prev_w = 90, prev_h = 120;
    vga_fill_rect(prev_x, prev_y, prev_w, prev_h, g_pref_darkmode?RGB(50,50,55):RGB(220,225,235));
    gui_draw_rounded_rect_outline(prev_x, prev_y, prev_w, prev_h, 2, pd_sep);
    vga_draw_string_trans(prev_x+13, prev_y+52, "Ready", pd_sub);
    vga_draw_string_trans(prev_x+13, prev_y+66, "preview", pd_sub);
    vga_draw_string_trans(prev_x+4, prev_y+prev_h+4, "MyOS PDF", pd_sub);
    /* Right panel: settings */
    int set_x = prev_x + prev_w + 8;
    int set_w = pd_w - prev_w - 24;
    int sy3 = pd_y + 38;
    /* Printer selector */
    vga_draw_string_trans(set_x, sy3, "Printer:", pd_sub);
    vga_fill_rect(set_x, sy3+12, set_w, 16, g_pref_darkmode?RGB(50,50,56):RGB(236,236,242));
    gui_draw_rounded_rect_outline(set_x, sy3+12, set_w, 16, 2, pd_sep);
    vga_draw_string_trans(set_x+4, sy3+16, "MyOS PDF Printer", pd_txt);
    vga_draw_string_trans(set_x+set_w-12, sy3+16, ">", pd_sub);
    if (g_print_jobs > 0) {
        char job_line[24];
        int jp = 0;
        char jn[8];
        job_line[0] = 0;
        overlay_append_text(job_line, &jp, sizeof(job_line), "Jobs: ");
        runtime_format_uint((uint32_t)g_print_jobs, jn, sizeof(jn));
        overlay_append_text(job_line, &jp, sizeof(job_line), jn);
        vga_draw_string_trans(set_x, sy3+31, job_line, pd_sub);
    }
    /* Copies */
    sy3 += 34;
    vga_draw_string_trans(set_x, sy3, "Copies:", pd_sub);
    vga_fill_rect(set_x, sy3+12, 36, 16, g_pref_darkmode?RGB(50,50,56):RGB(236,236,242));
    gui_draw_rounded_rect_outline(set_x, sy3+12, 36, 16, 2, pd_sep);
    { char cb[4]; cb[0]='0'+g_print_copies; cb[1]=0;
      vga_draw_string_trans(set_x+14, sy3+16, cb, pd_txt); }
    vga_draw_string_trans(set_x+40, sy3+16, "-  +", pd_sub);
    /* Pages */
    sy3 += 34;
    vga_draw_string_trans(set_x, sy3, "Pages:", pd_sub);
    vga_draw_string_trans(set_x, sy3+12, "All", pd_txt);
    vga_draw_string_trans(set_x+28, sy3+12, "From:", pd_sub);
    { char pb[4]; pb[0]='0'+g_print_page_from; pb[1]=0;
      vga_fill_rect(set_x+60, sy3+10, 20, 14, g_pref_darkmode?RGB(50,50,56):RGB(236,236,242));
      gui_draw_rounded_rect_outline(set_x+60, sy3+10, 20, 14, 2, pd_sep);
      vga_draw_string_trans(set_x+64, sy3+13, pb, pd_txt); }
    vga_draw_string_trans(set_x+84, sy3+12, "To:", pd_sub);
    { char pb[4]; pb[0]='0'+g_print_page_to; pb[1]=0;
      vga_fill_rect(set_x+100, sy3+10, 20, 14, g_pref_darkmode?RGB(50,50,56):RGB(236,236,242));
      gui_draw_rounded_rect_outline(set_x+100, sy3+10, 20, 14, 2, pd_sep);
      vga_draw_string_trans(set_x+104, sy3+13, pb, pd_txt); }
    /* Color mode */
    sy3 += 32;
    vga_draw_string_trans(set_x, sy3, "Color:", pd_sub);
    vga_draw_string_trans(set_x, sy3+12, g_print_color ? "Color" : "B&W", pd_txt);
    /* Quality */
    sy3 += 28;
    vga_draw_string_trans(set_x, sy3, "Quality:", pd_sub);
    static const char *ql_names[] = {"Draft","Normal","Best"};
    vga_draw_string_trans(set_x, sy3+12, ql_names[g_print_quality], pd_txt);
    /* Separator before buttons */
    vga_draw_hline(pd_x+8, pd_y+pd_h-40, pd_w-16, pd_sep);
    /* Cancel + Print buttons */
    int btn_y2 = pd_y+pd_h-30;
    vga_fill_rect(pd_x+8, btn_y2, 70, 20, g_pref_darkmode?RGB(55,55,60):RGB(218,218,224));
    gui_draw_rounded_rect_outline(pd_x+8, btn_y2, 70, 20, 4, pd_sep);
    { int cl=str_len("Cancel")*8; vga_draw_string_trans(pd_x+8+(70-cl)/2, btn_y2+6, "Cancel", pd_txt); }
    vga_fill_rect(pd_x+pd_w-88, btn_y2, 80, 20, pd_acc);
    gui_draw_rounded_rect_outline(pd_x+pd_w-88, btn_y2, 80, 20, 4, pd_acc);
    { int pl=str_len("Print")*8; vga_draw_string_trans(pd_x+pd_w-88+(80-pl)/2, btn_y2+6, "Print", RGB(255,255,255)); }
}

/* =========================================================================
 * AirDrop panel
 * ======================================================================= */
void airdrop_draw(void) {
    int ad_w = 300, ad_h = 280;
    int ad_x = (VGA_WIDTH - ad_w) / 2;
    int ad_y = (VGA_HEIGHT - ad_h) / 2;
    uint32_t ad_bg  = g_pref_darkmode ? RGB(36,36,40)    : RGB(248,248,252);
    uint32_t ad_hd  = g_pref_darkmode ? RGB(50,50,55)    : RGB(230,230,235);
    uint32_t ad_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
    uint32_t ad_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
    uint32_t ad_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
    uint32_t ad_acc = RGB(0,122,255);

    /* Shadow */
    vga_fill_rect_alpha(ad_x+4, ad_y+4, ad_w, ad_h, RGB(0,0,0), 80);
    /* Panel */
    vga_fill_rect(ad_x, ad_y, ad_w, ad_h, ad_bg);
    gui_draw_rounded_rect_outline(ad_x, ad_y, ad_w, ad_h, 10, ad_sep);

    /* Header */
    vga_fill_rect(ad_x, ad_y, ad_w, 32, ad_hd);
    vga_draw_hline(ad_x, ad_y+32, ad_w, ad_sep);
    { int tl=str_len("AirDrop")*8; vga_draw_string_trans(ad_x+(ad_w-tl)/2, ad_y+12, "AirDrop", ad_txt); }
    /* X close button */
    gui_draw_circle(ad_x+16, ad_y+16, 8, ad_sep);
    vga_draw_string_trans(ad_x+13, ad_y+12, "x", ad_txt);

    /* Radar/signal animation circles */
    int cx2 = ad_x + ad_w/2, cy2 = ad_y + 105;
    uint32_t ring_col = g_pref_darkmode ? RGB(60,90,180) : RGB(100,140,220);
    gui_draw_circle_outline(cx2, cy2, 60, ring_col);
    gui_draw_circle_outline(cx2, cy2, 42, ring_col);
    gui_draw_circle_outline(cx2, cy2, 24, ring_col);
    /* Self avatar */
    gui_draw_circle(cx2, cy2, 18, ad_acc);
    vga_draw_string_trans(cx2-8, cy2-5, "PC", RGB(255,255,255));

    if (g_airdrop_sending == 0) {
        /* Scanning mode - show nearby devices */
        static const char *devices[] = {"iPhone", "iPad", "MacBook"};
        static const int dev_angles_x[] = {-55, 55, 0};
        static const int dev_angles_y[] = {-30, -30, -55};
        int di;
        for (di=0; di<3; di++) {
            int dx2=cx2+dev_angles_x[di], dy2=cy2+dev_angles_y[di];
            gui_draw_circle(dx2, dy2, 12, g_pref_darkmode?RGB(60,60,70):RGB(200,210,220));
            vga_draw_char_trans(dx2-4, dy2-5, devices[di][0], ad_txt);
            vga_draw_string_trans(dx2-str_len(devices[di])*4, dy2+14, devices[di], ad_sub);
        }
        /* Status text */
        { int sl3=str_len("Searching...")*8;
          vga_draw_string_trans(ad_x+(ad_w-sl3)/2, ad_y+170, "Searching...", ad_sub); }
        /* Tip text */
        { int tl2=str_len("Hold iPhone near this Mac")*8;
          if (tl2 > ad_w-20) tl2 = ad_w-20;
          vga_draw_string_trans(ad_x+10, ad_y+188, "Hold iPhone near this Mac", ad_sub); }
    } else {
        /* Sending file - show progress */
        const char *send_status;
        int bar_x = ad_x+20, bar_y = ad_y+193, bar_w = ad_w-40, bar_h = 8;
        if (g_airdrop_sending == 2 && g_airdrop_progress < 100) {
            uint32_t elapsed = timer_ticks() - g_airdrop_start_tick;
            g_airdrop_progress = (int)(elapsed / 20U);
            if (g_airdrop_progress > 100) g_airdrop_progress = 100;
        }
        send_status = g_airdrop_progress >= 100 ? "Sent to iPhone" : "Sending to iPhone...";
        vga_draw_string_trans(ad_x+20, ad_y+175, send_status, ad_txt);
        vga_fill_rect(bar_x, bar_y, bar_w, bar_h, ad_sep);
        int prog_w = bar_w * g_airdrop_progress / 100;
        vga_fill_rect(bar_x, bar_y, prog_w, bar_h, ad_acc);
        gui_draw_rounded_rect_outline(bar_x, bar_y, bar_w, bar_h, 4, ad_sep);
        char pct[5]; int_to_str(g_airdrop_progress, pct);
        vga_draw_string_trans(bar_x+bar_w+4, bar_y, pct, ad_sub);
    }

    /* Bottom button */
    int btn_y3 = ad_y + ad_h - 34;
    vga_draw_hline(ad_x, btn_y3, ad_w, ad_sep);
    { int cl2=str_len(g_airdrop_sending?"Cancel":"Allow Everyone")*8;
      vga_draw_string_trans(ad_x+(ad_w-cl2)/2, btn_y3+12,
          g_airdrop_sending?"Cancel":"Allow Everyone", ad_acc); }
}

/* =========================================================================
 * Handoff notification (badge slides in from dock)
 * ======================================================================= */
void handoff_draw(void) {
    int hf_w = 160, hf_h = 48;
    int hf_x = 10;
    int elapsed = (int)(timer_ticks() - (uint32_t)g_handoff_tick);
    /* Slide in from left over 30 ticks, auto-dismiss after 180 */
    int slide = elapsed < 30 ? elapsed * hf_w / 30 - hf_w : 0;
    if (elapsed > 180) { g_handoff_visible = 0; return; }
    int hf_y = DOCK_Y - 60;
    hf_x = slide;  /* negative while sliding in */

    uint32_t hf_bg  = g_pref_darkmode ? RGB(44,44,50) : RGB(250,250,254);
    uint32_t hf_sep = g_pref_darkmode ? RGB(70,70,78) : RGB(195,195,202);
    uint32_t hf_txt = g_pref_darkmode ? RGB(210,210,220) : RGB(30,30,40);
    uint32_t hf_sub = g_pref_darkmode ? RGB(120,120,130) : RGB(100,100,110);

    vga_fill_rect_alpha(hf_x+3, hf_y+3, hf_w, hf_h, RGB(0,0,0), 50);
    vga_fill_rect(hf_x, hf_y, hf_w, hf_h, hf_bg);
    gui_draw_rounded_rect_outline(hf_x, hf_y, hf_w, hf_h, 8, hf_sep);

    /* App icon */
    gui_draw_circle(hf_x+22, hf_y+24, 14, RGB(0,122,255));
    vga_draw_string_trans(hf_x+17, hf_y+19, "Ha", RGB(255,255,255));

    /* Text */
    vga_draw_string_trans(hf_x+42, hf_y+10, "Handoff", hf_txt);
    vga_draw_string_trans(hf_x+42, hf_y+24, "Continue on iPhone", hf_sub);
}

void app_switcher_draw(void) {
    int n = g_num_windows;
    if (n == 0) return;
    if (g_switcher_sel >= n) g_switcher_sel = n - 1;

    int icon_w = 72, icon_h = 90;
    int cols = (n < 6) ? n : 6;
    int sw2 = cols * icon_w + 32;
    int sh2 = icon_h + 24;
    int sx = (VGA_WIDTH - sw2) / 2;
    int sy = (VGA_HEIGHT - sh2) / 2;

    /* Dark frosted background */
    vga_fill_rect_alpha(sx - 4, sy - 4, sw2 + 8, sh2 + 8, RGB(0,0,0), 80);
    vga_fill_rect_alpha(sx, sy, sw2, sh2, RGB(50,50,55), 230);
    gui_draw_rounded_rect_outline(sx, sy, sw2, sh2, 14, RGB(120,120,130));

    int i;
    for (i = 0; i < n && i < 6; i++) {
        int ix = sx + 16 + i * icon_w;
        int iy = sy + 12;
        uint32_t col = RGB(80, 120, 200);
        const char *title = g_windows[i].title ? g_windows[i].title : "?";
        /* Match dock icon color */
        int di;
        for (di = 0; di < 12; di++) {
            const char *dn = s_dock_icons[di].name;
            if (str_eq(title, dn) ||
                (str_eq(dn,"Finder") && str_eq(title,"MyOS Finder"))) {
                col = s_dock_icons[di].color; break;
            }
        }
        /* Icon square */
        gui_draw_rounded_rect(ix, iy, 56, 56, 10, col);
        /* Shine */
        vga_fill_rect_alpha(ix+2, iy+2, 52, 18, RGB(255,255,255), 50);
        /* Selected highlight */
        if (i == g_switcher_sel)
            gui_draw_rounded_rect_outline(ix-2, iy-2, 60, 60, 12, RGB(255,255,255));
        /* Title under icon (truncate at 7 chars) */
        {
            char buf[9]; int k;
            for (k=0; title[k] && k<7; k++) buf[k]=title[k];
            if (title[7]) { buf[5]='.'; buf[6]='.'; buf[7]=0; } else buf[k]=0;
            int tlen = str_len(buf);
            vga_draw_string_trans(ix + (56-tlen*8)/2, iy+60, buf,
                (i==g_switcher_sel) ? RGB(255,255,255) : RGB(180,180,180));
        }
    }
}

/* =========================================================================
 * Screenshot Tool (macOS style overlay)
 * ======================================================================= */
void screenshot_tool_draw(void) {
    if (!g_scr_visible) return;
    int W = VGA_WIDTH, H = VGA_HEIGHT;
    vga_fill_rect_alpha(0, 0, W, H, RGB(0,0,0), 60);
    if (g_scr_visible == 2) {
        /* Show thumbnail in bottom-right corner */
        int tx = W - 136, ty2 = H - 90;
        vga_fill_rect_alpha(tx, ty2, 120, 76, RGB(20,20,20), 220);
        vga_draw_rect_outline(tx, ty2, 120, 76, RGB(100,100,100));
        int mi;
        for (mi = 0; mi < 76; mi++) {
            uint8_t r2=(uint8_t)(200-mi*2>0?200-mi*2:0);
            uint8_t g2=(uint8_t)(120-mi>0?120-mi:0);
            uint8_t b2=(uint8_t)(40+mi/3);
            vga_draw_hline(tx+2, ty2+mi, 116, RGB(r2,g2,b2));
        }
        vga_draw_string_trans(tx+18, ty2+28, "Screenshot", RGB(255,255,255));
        vga_draw_string_trans(tx+18, ty2+42, "preview ready", RGB(160,200,255));
        return;
    }
    /* Toolbar at bottom center */
    int tb_w = 340, tb_h = 44;
    int tb_x = (W - tb_w) / 2, tb_y = H - 90;
    vga_fill_rect_alpha(tb_x+4, tb_y+4, tb_w, tb_h, RGB(0,0,0), 80);
    vga_fill_rect_alpha(tb_x, tb_y, tb_w, tb_h, RGB(40,40,42), 230);
    gui_draw_rounded_rect_outline(tb_x, tb_y, tb_w, tb_h, 8, RGB(80,80,82));
    /* Mode buttons */
    { static const char *ml[]  = {"Screen","Window","Area"};
      static const char *mi2[] = {"[]","[W]","[+]"};
      int bi;
      for (bi = 0; bi < 3; bi++) {
          int bx = tb_x + 12 + bi*72, by2 = tb_y + 6, bw = 64, bh = 32;
          uint32_t btn_bg = (bi == g_scr_mode) ? RGB(70,70,190) : RGB(56,56,60);
          gui_draw_rounded_rect(bx, by2, bw, bh, 6, btn_bg);
          if (bi == g_scr_mode)
              gui_draw_rounded_rect_outline(bx, by2, bw, bh, 6, RGB(110,110,220));
          int il = str_len(mi2[bi]);
          vga_draw_string_trans(bx + (bw - il*8)/2, by2 + 4, mi2[bi], RGB(220,220,220));
          int ll = str_len(ml[bi]);
          vga_draw_string_trans(bx + (bw - ll*8)/2, by2 + 18, ml[bi],
              (bi==g_scr_mode)?RGB(255,255,255):RGB(155,155,160));
      }
    }
    /* Capture button */
    int cbx = tb_x + tb_w - 76, cby = tb_y + 8, cbw = 68, cbh = 28;
    gui_draw_rounded_rect(cbx, cby, cbw, cbh, 6, RGB(0,110,220));
    vga_draw_string_trans(cbx + 10, cby + 10, "Capture", RGB(255,255,255));
    /* Help text */
    vga_draw_string_trans((W - 18*8)/2, tb_y - 18,
        "S=Capture  Esc=Cancel  Tab=Mode", RGB(190,190,195));
    /* Crosshair in area mode */
    if (g_scr_mode == 2) {
        int mx = mouse_get_x();
        int my = mouse_get_y();
        vga_draw_hline(0, my, W, RGB(255,255,255));
        vga_draw_vline(mx, 0, H, RGB(255,255,255));
    }
}

void crash_reporter_draw(void) {
    if (!g_crash_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 110);
    int dw=460, dh=200;
    int dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 12, RGB(44,44,48));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 12, RGB(80,80,88));
    vga_fill_rect(dx+20, dy+20, 48, 48, RGB(0,120,220));
    gui_draw_rounded_rect_outline(dx+20, dy+20, 48, 48, 8, RGB(60,160,255));
    vga_draw_string_trans(dx+34, dy+36, "APP", RGB(255,255,255));
    vga_draw_string_trans(dx+78, dy+22, "No crash report captured", RGB(230,230,235));
    vga_draw_string_trans(dx+78, dy+40, "Diagnostics watcher is idle.", RGB(160,160,168));
    vga_draw_string_trans(dx+20, dy+100, "Open reports appear here after a fault.", RGB(0,120,255));
    int by2=dy+dh-44;
    gui_draw_rounded_rect(dx+dw-110, by2, 90, 30, 6, RGB(0,105,215));
    vga_draw_string_trans(dx+dw-89, by2+11, "Done", RGB(255,255,255));
}

void system_update_draw(void) {
    if (!g_update_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 100);
    int dw=520, dh=360, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(40,40,44));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(75,75,85));
    int i; for(i=0;i<64;i++){
        int t=i*255/64;
        vga_draw_hline(dx,dy+i,dw,RGB(20+t*20/255,20+t*30/255,40+t*60/255));
    }
    vga_fill_rect(dx+20, dy+8, 48, 48, RGB(30,120,40));
    vga_draw_string_trans(dx+28, dy+24, "SYS", RGB(200,255,200));
    vga_draw_string_trans(dx+80, dy+12, "Software Update", RGB(240,240,245));
    vga_draw_string_trans(dx+80, dy+28, "Current version installed", RGB(180,220,255));
    vga_draw_hline(dx, dy+64, dw, RGB(75,75,85));
    { runtime_system_info_t sys;
      char line[96];
      int pos = 0;
      runtime_get_system_info(&sys);
      line[0] = 0;
      overlay_append_text(line, &pos, sizeof(line), sys.sysname);
      overlay_append_text(line, &pos, sizeof(line), " ");
      overlay_append_text(line, &pos, sizeof(line), sys.release);
      vga_draw_string_trans(dx+20, dy+76, line, RGB(230,230,235)); }
    vga_draw_string_trans(dx+20, dy+100, "Updates are managed by the local system image.", RGB(200,200,210));
    vga_fill_rect(dx+20, dy+136, dw-40, 16, RGB(55,55,60));
    vga_draw_string_trans(dx+20, dy+164, "Status: up to date", RGB(160,210,175));
    int by2=dy+dh-50; vga_draw_hline(dx, by2-8, dw, RGB(75,75,85));
    gui_draw_rounded_rect(dx+dw-110, by2, 90, 32, 6, RGB(65,65,72));
    vga_draw_string_trans(dx+dw-89, by2+12, "Done", RGB(220,220,228));
}

void focus_filter_draw(void) {
    if (!g_focus_filter_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 100);
    int dw=480, dh=380, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(42,42,46));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(78,78,88));
    vga_draw_string_trans(dx+(dw-13*8)/2, dy+18, "Focus Filters", RGB(235,235,240));
    vga_draw_hline(dx, dy+42, dw, RGB(72,72,82));
    static const char *m[]={"Work","Personal","Sleep","Driving"};
    static const uint32_t tc[]={RGB(0,110,255),RGB(52,199,89),RGB(100,65,165),RGB(255,159,10)};
    int t2; for(t2=0;t2<4;t2++){
        int tx=dx+20+t2*112, ty2=dy+56;
        gui_draw_rounded_rect(tx, ty2, 100, 28, 6, (t2==g_focus_filter_mode)?tc[t2]:RGB(58,58,65));
        int ml=str_len(m[t2]);
        vga_draw_string_trans(tx+(100-ml*8)/2, ty2+10, m[t2],
            (t2==g_focus_filter_mode)?RGB(255,255,255):RGB(180,180,190));
    }
    int fy=dy+100;
    vga_draw_string_trans(dx+20, fy, "Allowed Notifications:", RGB(200,200,210));
    fy+=22;
    static const char *fl[]={"Phone Calls from Favorites","Messages from Contacts",
        "Time-sensitive alerts","Reminders with due dates","Calendar invitations"};
    static int fon[]={1,1,1,0,1};
    int fi; for(fi=0;fi<5;fi++){
        int fy2=fy+fi*34;
        gui_draw_rounded_rect(dx+20, fy2+4, 38, 22, 11, fon[fi]?RGB(52,199,89):RGB(80,80,90));
        gui_draw_circle(fon[fi]?(dx+30):(dx+39), fy2+15, 8, RGB(255,255,255));
        vga_draw_string_trans(dx+68, fy2+8, fl[fi], RGB(210,210,218));
    }
    fy+=5*34+10; vga_draw_hline(dx, fy, dw, RGB(72,72,82)); fy+=10;
    vga_draw_string_trans(dx+20, fy, "App Filters:", RGB(200,200,210));
    static const char *af[]={"Calendar","Mail","Messages","Safari"};
    int ai; for(ai=0;ai<4;ai++){
        int ax=dx+20+ai*110, ay=fy+18;
        gui_draw_rounded_rect(ax, ay, 100, 26, 6, RGB(0,100,210));
        vga_draw_string_trans(ax+(100-str_len(af[ai])*8)/2, ay+9, af[ai], RGB(255,255,255));
    }
    int by2=dy+dh-44; vga_draw_hline(dx, by2-8, dw, RGB(72,72,82));
    gui_draw_rounded_rect(dx+dw-100, by2, 80, 30, 6, RGB(0,105,215));
    vga_draw_string_trans(dx+dw-84, by2+11, "Done", RGB(255,255,255));
    gui_draw_rounded_rect(dx+20, by2, 80, 30, 6, RGB(65,65,72));
    vga_draw_string_trans(dx+30, by2+11, "Cancel", RGB(220,220,228));
}

void icloud_panel_draw(void) {
    if (!g_icloud_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 90);
    int dw=380, dh=320, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(42,42,48));
    vga_fill_rect(dx, dy, dw, 56, RGB(0,95,215));
    gui_draw_circle(dx+dw/2-18, dy+28, 14, RGB(255,255,255));
    gui_draw_circle(dx+dw/2, dy+24, 18, RGB(255,255,255));
    gui_draw_circle(dx+dw/2+20, dy+28, 12, RGB(255,255,255));
    vga_fill_rect(dx+dw/2-30, dy+28, 62, 18, RGB(255,255,255));
    vga_draw_string_trans(dx+(dw-21*8)/2, dy+62, "No iCloud account set", RGB(190,190,200));
    static const struct{const char *nm;const char *st;int p;} it[]={
        {"Photos","Signed out",0},{"iCloud Drive","Signed out",0},
        {"Notes","Signed out",0},{"Contacts","Signed out",0},{"Keychain","Local only",0}};
    int si; for(si=0;si<5;si++){
        int iy=dy+84+si*38;
        vga_draw_string_trans(dx+20, iy, it[si].nm, RGB(225,225,230));
        vga_draw_string_trans(dx+20, iy+14, it[si].st, RGB(140,140,155));
        vga_fill_rect(dx+dw-110, iy+4, 90, 8, RGB(55,55,65));
        if(si<4) vga_draw_hline(dx+16, iy+30, dw-32, RGB(65,65,75));
    }
    int by2=dy+dh-40; vga_draw_hline(dx, by2-8, dw, RGB(65,65,75));
    { runtime_storage_info_t st;
      char sline[64];
      int sp=0;
      sline[0]=0;
      overlay_append_text(sline,&sp,sizeof(sline),"Local storage: ");
      if(runtime_get_storage_info("/", &st)==0) {
          char bbuf[18];
          runtime_format_bytes(st.free_bytes, bbuf, sizeof(bbuf));
          overlay_append_text(sline,&sp,sizeof(sline),bbuf);
          overlay_append_text(sline,&sp,sizeof(sline)," free");
      } else {
          overlay_append_text(sline,&sp,sizeof(sline),"checking");
      }
      vga_draw_string_trans(dx+20, by2, sline, RGB(160,160,175)); }
    gui_draw_rounded_rect(dx+dw-100, by2-5, 80, 26, 5, RGB(65,65,75));
    vga_draw_string_trans(dx+dw-88, by2+4, "Done", RGB(220,220,228));
}

void bluetooth_dialog_draw(void) {
    if (!g_bt_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 90);
    int dw=400, dh=320, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(42,42,48));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(78,78,90));
    vga_fill_rect(dx+(dw-32)/2, dy+12, 32, 32, RGB(0,120,250));
    vga_draw_string_trans(dx+(dw-8)/2, dy+20, "B", RGB(255,255,255));
    vga_draw_string_trans(dx+(dw-9*8)/2, dy+50, "Bluetooth", RGB(230,230,235));
    vga_draw_string_trans(dx+(dw-12*8)/2, dy+64, g_pref_bt ? "Bluetooth On" : "Bluetooth Off", RGB(160,160,175));
    vga_draw_hline(dx, dy+82, dw, RGB(68,68,80));
    gui_draw_rounded_rect(dx+16, dy+104, dw-32, 54, 5, RGB(52,52,60));
    vga_draw_string_trans(dx+28, dy+122, g_pref_bt ? "MyOS Keyboard connected" : "Bluetooth is off", RGB(230,230,235));
    vga_draw_string_trans(dx+28, dy+138, g_pref_bt ? "MyOS Mouse ready to pair" : "Turn on to pair devices", RGB(160,160,175));
    int by2=dy+dh-42; vga_draw_hline(dx, by2-8, dw, RGB(68,68,80));
    gui_draw_rounded_rect(dx+20, by2, 80, 28, 5, RGB(65,65,75));
    vga_draw_string_trans(dx+30, by2+9, g_pref_bt ? "Turn Off" : "Turn On", RGB(220,220,228));
    gui_draw_rounded_rect(dx+dw-100, by2, 80, 28, 5, RGB(65,65,75));
    vga_draw_string_trans(dx+dw-88, by2+9, "Done", RGB(220,220,228));
}

void keyboard_shortcuts_draw(void) {
    if (!g_kbshort_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 140);
    int dw=570, dh=450, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(36,36,40));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(72,72,82));
    vga_fill_rect(dx, dy, dw, 44, RGB(28,28,32));
    vga_draw_string_trans(dx+(dw-18*8)/2, dy+16, "Keyboard Shortcuts", RGB(235,235,240));
    char pg2[4]; pg2[0]='0'+g_kbshort_page+1; pg2[1]='/'; pg2[2]='3'; pg2[3]=0;
    vga_draw_string_trans(dx+dw-38, dy+16, pg2, RGB(140,140,155));
    vga_draw_hline(dx, dy+44, dw, RGB(65,65,75));
    static const char *kk[3][14]={
      {"Tab","Ctrl+C","Ctrl+N","Ctrl+M","Ctrl+L","Ctrl+A","Ctrl+E",
       "Ctrl+S","Ctrl+H","Ctrl+I","Ctrl+B","Ctrl+U","F8","Esc"},
      {"p","Space","Ctrl+D","F1","F2","F3","F4","F5","Ctrl+Q",
       "Insert","Ctrl+F","Ctrl+1-4","TL Corner","BR Corner"},
      {"Ctrl+T","Ctrl+W","Ctrl+G","Ctrl+2","Ctrl+3","Ctrl+4","Ctrl+5",
       "Ctrl+\\","Ctrl+]","Ctrl+J","Ctrl+8","Ctrl+0","Ctrl+Z","Ctrl+O"}};
    static const char *dd[3][14]={
      {"Spotlight","Control Ctr","Notif Ctr","Mission Ctrl","Launchpad","App Switcher","App Expose",
       "Siri","AirDrop","iCloud","Bluetooth","Sys Update","Shortcut Guide","Close"},
      {"Screenshot","Quick Look","Dark Mode","Night Shift","Focus Mode","App Expose","Launchpad",
       "App Switcher","Quick Note","Writing Tools","Focus Filters","Switch Space","Mission Ctrl","Screen Saver"},
      {"Time Machine","Safari","Chess","2048","Pong","Sudoku","Wordle",
       "Snake","Breakout","Messages","Photo Booth","Stage Manager","Zoom","FaceTime"}};
    int cw=(dw-40)/2, si2;
    for(si2=0;si2<14;si2++){
        int c2=si2/7, r2=si2%7;
        int kx=dx+20+c2*cw, ky=dy+54+r2*52;
        int kw=str_len(kk[g_kbshort_page][si2])*8+12; if(kw<40)kw=40;
        gui_draw_rounded_rect(kx, ky, kw, 20, 3, RGB(60,60,70));
        vga_draw_string_trans(kx+5, ky+6, kk[g_kbshort_page][si2], RGB(215,215,235));
        vga_draw_string_trans(kx+kw+8, ky+6, dd[g_kbshort_page][si2], RGB(175,175,195));
    }
    int pi3;
    for(pi3=0;pi3<3;pi3++)
        gui_draw_circle(dx+dw/2-20+pi3*20, dy+dh-18, 5,
            (pi3==g_kbshort_page)?RGB(255,255,255):RGB(90,90,105));
    if(g_kbshort_page>0){
        gui_draw_rounded_rect(dx+20,dy+dh-36,70,24,4,RGB(60,60,70));
        vga_draw_string_trans(dx+30,dy+dh-26,"< Prev",RGB(210,210,225));
    }
    if(g_kbshort_page<2){
        gui_draw_rounded_rect(dx+dw-90,dy+dh-36,70,24,4,RGB(0,95,210));
        vga_draw_string_trans(dx+dw-80,dy+dh-26,"Next >",RGB(255,255,255));
    } else {
        gui_draw_rounded_rect(dx+dw-90,dy+dh-36,70,24,4,RGB(60,60,70));
        vga_draw_string_trans(dx+dw-78,dy+dh-26,"Done",RGB(210,210,225));
    }
}

void time_machine_draw(void) {
    char line[72];
    char num[12];
    char age[20];
    int lp;
    int pct;
    if (!g_timemachine_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 150);
    int dw=480, dh=350, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 16, RGB(28,28,32));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 16, RGB(65,65,80));
    vga_fill_rect(dx, dy, dw, 56, RGB(20,20,26));
    int cx2=dx+dw/2, cy2=dy+28;
    gui_draw_circle(cx2, cy2, 22, RGB(100,160,220));
    gui_draw_circle_outline(cx2, cy2, 22, RGB(60,120,180));
    vga_draw_string_trans(cx2-12, cy2-5, "TM", RGB(255,255,255));
    vga_draw_string_trans(dx+(dw-12*8)/2, dy+18, "Time Machine", RGB(220,220,235));
    vga_draw_string_trans(dx+(dw-21*8)/2, dy+64, "Local snapshots ready", RGB(200,200,215));
    int bx=dx+40, by=dy+88, bw=dw-80, bh=16;
    pct = 20 + g_tm_snapshot_count * 12;
    if (pct > 100) pct = 100;
    vga_fill_rect(bx, by, bw, bh, RGB(40,40,50));
    vga_fill_rect(bx, by, bw * pct / 100, bh, RGB(52,199,89));
    gui_draw_rounded_rect_outline(bx, by, bw, bh, 8, RGB(70,70,90));
    vga_draw_string_trans(dx+40, dy+116, "Destination: local snapshots", RGB(160,160,180));
    line[0]=0; lp=0;
    overlay_append_text(line, &lp, sizeof(line), "Snapshots: ");
    runtime_format_uint((uint32_t)g_tm_snapshot_count, num, sizeof(num));
    overlay_append_text(line, &lp, sizeof(line), num);
    vga_draw_string_trans(dx+40, dy+132, line, RGB(160,160,180));
    vga_draw_hline(dx, dy+160, dw, RGB(55,55,70));
    vga_draw_string_trans(dx+20, dy+168, "Recent Backups:", RGB(190,190,210));
    gui_draw_rounded_rect(dx+20, dy+188, dw-40, 42, 4, RGB(38,38,48));
    if (g_tm_snapshot_count > 0) {
        uint32_t elapsed = (timer_ticks() - g_tm_last_snapshot_tick) / 1000U;
        runtime_format_relative_time(elapsed, age, sizeof(age));
        line[0]=0; lp=0;
        overlay_append_text(line, &lp, sizeof(line), "Snapshot #");
        runtime_format_uint((uint32_t)g_tm_snapshot_count, num, sizeof(num));
        overlay_append_text(line, &lp, sizeof(line), num);
        overlay_append_text(line, &lp, sizeof(line), " - ");
        overlay_append_text(line, &lp, sizeof(line), age);
        vga_draw_string_trans(dx+30, dy+198, line, RGB(200,240,210));
        vga_draw_string_trans(dx+30, dy+214, "Macintosh HD - /", RGB(150,170,190));
    } else {
        vga_draw_string_trans(dx+30, dy+198, "No local snapshots yet", RGB(200,200,218));
        vga_draw_string_trans(dx+30, dy+214, "Click Back Up Now to create one", RGB(150,150,170));
    }
    int by2=dy+dh-44; vga_draw_hline(dx, by2-8, dw, RGB(55,55,70));
    gui_draw_rounded_rect(dx+20, by2, 150, 28, 5, RGB(0,105,215));
    vga_draw_string_trans(dx+42, by2+9, "Back Up Now", RGB(255,255,255));
    gui_draw_rounded_rect(dx+dw-100, by2, 80, 28, 5, RGB(60,60,75));
    vga_draw_string_trans(dx+dw-88, by2+9, "Done", RGB(220,220,228));
}

void color_meter_draw(void) {
    if (!g_colormeter_visible) return;
    int mx2=mouse_get_x(), my2=mouse_get_y();
    uint32_t px=vga_get_pixel(mx2, my2);
    int r2=(px>>16)&0xFF, gv=(px>>8)&0xFF, b2=px&0xFF;
    int dw=200, dh=160;
    int dx=mx2+20; if(dx+dw>VGA_WIDTH) dx=mx2-dw-10;
    int dy=my2+20; if(dy+dh>VGA_HEIGHT) dy=my2-dh-10;
    gui_draw_rounded_rect(dx, dy, dw, dh, 10, RGB(35,35,40));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 10, RGB(70,70,85));
    vga_draw_string_trans(dx+8, dy+8, "Color Meter", RGB(220,220,230));
    vga_draw_hline(dx, dy+24, dw, RGB(60,60,75));
    vga_fill_rect(dx+8, dy+30, 44, 44, RGB(r2,gv,b2));
    gui_draw_rounded_rect_outline(dx+8, dy+30, 44, 44, 4, RGB(90,90,105));
    vga_draw_hline(mx2-8, my2, 17, RGB(255,255,255));
    vga_draw_vline(mx2, my2-8, 17, RGB(255,255,255));
    char rv[8], gv2[8], bv[8];
    int_to_str(r2, rv); int_to_str(gv, gv2); int_to_str(b2, bv);
    static const char hc[]="0123456789ABCDEF";
    char hx[8]; hx[0]='#';
    hx[1]=hc[(r2>>4)&0xF]; hx[2]=hc[r2&0xF];
    hx[3]=hc[(gv>>4)&0xF]; hx[4]=hc[gv&0xF];
    hx[5]=hc[(b2>>4)&0xF]; hx[6]=hc[b2&0xF]; hx[7]=0;
    char rs[8]; rs[0]='R'; rs[1]=':'; rs[2]=' ';
    { int i=3; const char *s=rv; while(*s) rs[i++]=*s++; rs[i]=0; }
    char gs[8]; gs[0]='G'; gs[1]=':'; gs[2]=' ';
    { int i=3; const char *s=gv2; while(*s) gs[i++]=*s++; gs[i]=0; }
    char bs[8]; bs[0]='B'; bs[1]=':'; bs[2]=' ';
    { int i=3; const char *s=bv; while(*s) bs[i++]=*s++; bs[i]=0; }
    vga_draw_string_trans(dx+60, dy+32, rs, RGB(255,100,100));
    vga_draw_string_trans(dx+60, dy+48, gs, RGB(100,220,100));
    vga_draw_string_trans(dx+60, dy+64, bs, RGB(100,160,255));
    vga_draw_string_trans(dx+60, dy+82, hx, RGB(220,220,230));
    vga_fill_rect(dx+8, dy+100, r2*184/255, 7, RGB(255,80,80));
    vga_fill_rect(dx+8, dy+112, gv*184/255, 7, RGB(80,220,80));
    vga_fill_rect(dx+8, dy+124, b2*184/255, 7, RGB(80,140,255));
}

void notif_history_draw(void) {
    if (!g_notifhist_visible) return;
    int dw=340, dh=460, dx=VGA_WIDTH-dw-8, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 12, RGB(30,30,36));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 12, RGB(65,65,80));
    vga_fill_rect(dx, dy, dw, 44, RGB(24,24,30));
    vga_draw_string_trans(dx+(dw-20*8)/2, dy+14, "Notification History", RGB(230,230,240));
    vga_draw_hline(dx, dy+44, dw, RGB(58,58,72));
    gui_draw_rounded_rect(dx+16, dy+72, dw-32, 64, 4, RGB(40,40,50));
    vga_draw_string_trans(dx+28, dy+94, g_notifhist_clear_count ? "History cleared" : "No notification history", RGB(190,190,205));
    vga_draw_string_trans(dx+28, dy+112, "New notifications are recorded here.", RGB(130,130,150));
    int by2=dy+dh-42; vga_draw_hline(dx, by2, dw, RGB(58,58,72));
    vga_draw_string_trans(dx+20, by2+12, "Clear All", RGB(255,59,48));
    gui_draw_rounded_rect(dx+dw-100, by2+8, 80, 26, 5, RGB(60,60,75));
    vga_draw_string_trans(dx+dw-88, by2+14, "Done", RGB(220,220,228));
}

void wifi_panel_draw(void) {
    if (!g_wifi_visible) return;
    int dw=340, dh=380, dx=VGA_WIDTH-dw-12, dy=28;
    gui_draw_rounded_rect(dx, dy, dw, dh, 12, RGB(34,34,38));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 12, RGB(70,70,82));
    vga_fill_rect(dx, dy, dw, 52, RGB(28,28,32));
    vga_draw_string_trans(dx+(dw-6*8)/2, dy+10, "Wi-Fi", RGB(235,235,240));
    gui_draw_rounded_rect(dx+16, dy+28, dw-32, 20, 5, g_pref_wifi ? RGB(0,122,255) : RGB(65,65,75));
    vga_draw_string_trans(dx+(dw-9*8)/2, dy+32, g_pref_wifi ? "Wi-Fi On" : "Wi-Fi Off", RGB(220,220,228));
    vga_draw_hline(dx, dy+52, dw, RGB(62,62,75));
    vga_draw_string_trans(dx+16, dy+62, "MY NETWORKS", RGB(120,120,140));
    gui_draw_rounded_rect(dx+12, dy+80, dw-24, 54, 6, RGB(44,44,52));
    vga_draw_string_trans(dx+24, dy+98, g_pref_wifi ? "MyOS Lab" : "Wi-Fi is off", RGB(230,230,238));
    vga_draw_string_trans(dx+24, dy+114, g_pref_wifi ? "Connected, WPA2" : "Turn on to join networks", RGB(150,150,170));
    vga_draw_hline(dx, dy+220, dw, RGB(62,62,75));
    vga_draw_string_trans(dx+16, dy+230, "OTHER NETWORKS", RGB(120,120,140));
    gui_draw_rounded_rect(dx+12, dy+248, dw-24, 42, 6, RGB(44,44,52));
    vga_draw_string_trans(dx+24, dy+264, g_pref_wifi ? "Guest Network" : "No scan while off", RGB(220,220,230));
    int by2=dy+dh-38; vga_draw_hline(dx, by2, dw, RGB(62,62,75));
    vga_draw_string_trans(dx+16, by2+12, "Network Settings...", RGB(0,122,255));
    vga_draw_string_trans(dx+dw-72, by2+12, "Done", RGB(0,122,255));
}

void display_settings_draw(void) {
    if (!g_display_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 90);
    int dw=480, dh=380, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(40,40,46));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(72,72,85));
    vga_fill_rect(dx, dy, dw, 48, RGB(32,32,38));
    vga_draw_string_trans(dx+(dw-7*8)/2, dy+16, "Display", RGB(235,235,240));
    vga_draw_hline(dx, dy+48, dw, RGB(65,65,78));
    int bry=dy+68;
    vga_draw_string_trans(dx+24, bry, "Brightness", RGB(200,200,212));
    int bx=dx+140, bw=dw-170, bh=12, by=bry+4;
    vga_fill_rect(bx, by, bw, bh, RGB(55,55,65));
    gui_draw_rounded_rect_outline(bx, by, bw, bh, 6, RGB(80,80,95));
    int bp=bw*g_display_brightness/100;
    vga_fill_rect(bx, by, bp, bh, RGB(255,255,255));
    gui_draw_circle(bx+bp, by+bh/2, 9, RGB(255,255,255));
    gui_draw_circle_outline(bx+bp, by+bh/2, 9, RGB(200,200,220));
    vga_draw_hline(dx, bry+36, dw, RGB(58,58,72));
    int ry=bry+50;
    vga_draw_string_trans(dx+24, ry, "Resolution", RGB(200,200,212));
    static const char *res[]={"2560x1600 (Retina)","1920x1200","1680x1050","1440x900"};
    int active_res = g_pref_resolution;
    if (active_res < 0 || active_res > 3) active_res = 0;
    int ri; for(ri=0;ri<4;ri++){
        int ry2=ry+24+ri*34;
        gui_draw_rounded_rect(dx+140, ry2, dw-164, 26, 4, ri==active_res?RGB(0,90,210):RGB(50,50,60));
        vga_draw_string_trans(dx+152, ry2+8, res[ri], ri==active_res?RGB(255,255,255):RGB(185,185,200));
        if(ri==active_res){gui_draw_circle(dx+dw-28, ry2+13, 7, RGB(255,255,255)); gui_draw_circle_outline(dx+dw-28, ry2+13, 7, RGB(150,200,255));}
    }
    int ty=ry+24+4*34+6; vga_draw_hline(dx, ty, dw, RGB(58,58,72)); ty+=16;
    static const char *togs[]={"True Tone","Night Shift","Auto Brightness"};
    static int tvals[]={1,0,1};
    int ti; for(ti=0;ti<3;ti++){
        int ty2=ty+ti*36;
        vga_draw_string_trans(dx+24, ty2+5, togs[ti], RGB(200,200,212));
        gui_draw_rounded_rect(dx+dw-62, ty2, 44, 24, 12, tvals[ti]?RGB(52,199,89):RGB(70,70,85));
        gui_draw_circle(tvals[ti]?(dx+dw-28):(dx+dw-50), ty2+12, 9, RGB(255,255,255));
        if(ti<2) vga_draw_hline(dx+16, ty2+30, dw-32, RGB(58,58,72));
    }
    int by3=dy+dh-44; vga_draw_hline(dx, by3, dw, RGB(65,65,78));
    gui_draw_rounded_rect(dx+dw-100, by3+8, 80, 28, 5, RGB(0,100,215));
    vga_draw_string_trans(dx+dw-88, by3+16, "Done", RGB(255,255,255));
}

void sound_settings_draw(void) {
    if (!g_sound_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 90);
    int dw=460, dh=360, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(40,40,46));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(72,72,85));
    vga_fill_rect(dx, dy, dw, 48, RGB(32,32,38));
    vga_draw_string_trans(dx+(dw-5*8)/2, dy+16, "Sound", RGB(235,235,240));
    vga_draw_hline(dx, dy+48, dw, RGB(65,65,78));
    int sy=dy+64;
    vga_draw_string_trans(dx+24, sy, "Output Volume", RGB(200,200,212));
    gui_draw_circle(dx+160, sy+6, 6, RGB(120,120,140));
    gui_draw_circle_outline(dx+160, sy+6, 6, RGB(90,90,110));
    int vx=dx+178, vw=dw-210, vh=10, vy=sy+2;
    vga_fill_rect(vx, vy, vw, vh, RGB(55,55,68));
    gui_draw_rounded_rect_outline(vx, vy, vw, vh, 5, RGB(80,80,95));
    int vp=vw*g_sound_volume/100;
    vga_fill_rect(vx, vy, vp, vh, RGB(255,255,255));
    gui_draw_circle(vx+vp, vy+vh/2, 8, RGB(255,255,255));
    gui_draw_circle_outline(vx+vp, vy+vh/2, 8, RGB(200,200,220));
    vga_draw_string_trans(dx+dw-30, sy, ")", RGB(160,160,180));
    vga_draw_hline(dx, sy+28, dw, RGB(58,58,72));
    int sy2=sy+42;
    vga_draw_string_trans(dx+24, sy2, "Output Device", RGB(200,200,212));
    static const char *od[]={"Built-in Speakers","AirPods Pro","HDMI Output","USB Audio"};
    int active_out = g_sound_output_device;
    if (active_out < 0 || active_out > 3) active_out = 0;
    int oi; for(oi=0;oi<4;oi++){
        int oy=sy2+22+oi*32;
        gui_draw_rounded_rect(dx+130, oy, dw-154, 24, 4, oi==active_out?RGB(0,85,200):RGB(48,48,60));
        vga_draw_string_trans(dx+142, oy+7, od[oi], oi==active_out?RGB(255,255,255):RGB(185,185,205));
        if(oi==active_out){gui_draw_circle(dx+dw-28, oy+12, 6, RGB(200,255,200)); gui_draw_circle_outline(dx+dw-28, oy+12, 6, RGB(100,200,100));}
    }
    vga_draw_hline(dx, sy2+22+4*32+4, dw, RGB(58,58,72));
    int sy3=sy2+22+4*32+20;
    vga_draw_string_trans(dx+24, sy3, "Input Device", RGB(200,200,212));
    static const char *id2[]={"Built-in Microphone","External Mic"};
    int active_in = g_sound_input_device;
    if (active_in < 0 || active_in > 1) active_in = 0;
    int ii; for(ii=0;ii<2;ii++){
        int iy=sy3+22+ii*32;
        gui_draw_rounded_rect(dx+130, iy, dw-154, 24, 4, ii==active_in?RGB(0,85,200):RGB(48,48,60));
        vga_draw_string_trans(dx+142, iy+7, id2[ii], ii==active_in?RGB(255,255,255):RGB(185,185,205));
    }
    vga_draw_string_trans(dx+24, sy3+86, "Input Level:", RGB(160,160,180));
    int ilx=dx+130, ilw=dw-154;
    vga_fill_rect(ilx, sy3+84, ilw, 8, RGB(48,48,60));
    vga_fill_rect(ilx, sy3+84, ilw*3/10, 8, RGB(52,199,89));
    int by3=dy+dh-44; vga_draw_hline(dx, by3, dw, RGB(65,65,78));
    gui_draw_rounded_rect(dx+dw-100, by3+8, 80, 28, 5, RGB(0,100,215));
    vga_draw_string_trans(dx+dw-88, by3+16, "Done", RGB(255,255,255));
}

void activity_monitor_draw(void) {
    if (!g_actmon_visible) return;
    runtime_system_info_t sys;
    runtime_get_system_info(&sys);
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 80);
    int dw=580, dh=420, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 12, RGB(28,28,32));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 12, RGB(65,65,80));
    vga_fill_rect(dx, dy, dw, 44, RGB(22,22,28));
    vga_draw_string_trans(dx+(dw-16*8)/2, dy+14, "Activity Monitor", RGB(235,235,240));
    vga_draw_hline(dx, dy+44, dw, RGB(58,58,72));
    vga_fill_rect(dx, dy+44, dw, 22, RGB(34,34,42));
    vga_draw_string_trans(dx+20, dy+52, "Process Name", RGB(160,160,180));
    vga_draw_string_trans(dx+280, dy+52, "CPU %", RGB(160,160,180));
    vga_draw_string_trans(dx+380, dy+52, "Memory", RGB(160,160,180));
    vga_draw_string_trans(dx+480, dy+52, "PID", RGB(160,160,180));
    vga_draw_hline(dx, dy+66, dw, RGB(50,50,65));
    {
        struct { const char *name; uint32_t count; uint32_t bytes; } rows[] = {
            {"Processes", sys.process_count, 0},
            {"Tasks", sys.task_count, 0},
            {"Drivers", sys.driver_count, 0},
            {"Heap Used", 0, sys.heap_used_bytes},
            {"Physical Free", 0, sys.pmm_free_bytes}
        };
        int pi;
        for(pi=0;pi<5;pi++){
            int py=dy+70+pi*30;
            char val[20];
            if(pi%2==0) vga_fill_rect(dx, py, dw, 30, RGB(32,32,40));
            vga_draw_string_trans(dx+20, py+10, rows[pi].name, RGB(220,220,228));
            if(rows[pi].bytes) runtime_format_bytes(rows[pi].bytes, val, sizeof(val));
            else runtime_format_uint(rows[pi].count, val, sizeof(val));
            vga_draw_string_trans(dx+380, py+10, val, RGB(200,210,228));
            vga_draw_string_trans(dx+480, py+10, "-", RGB(180,180,200));
        }
        vga_draw_string_trans(dx+20, dy+238, "Live totals are sampled from kernel counters.", RGB(150,150,170));
    }
    vga_draw_hline(dx, dy+372, dw, RGB(58,58,72));
    vga_fill_rect(dx, dy+372, dw, 48, RGB(22,22,28));
    vga_draw_string_trans(dx+20, dy+380, "CPU Usage:", RGB(150,150,170));
    int cux=dx+110, culen=180;
    vga_fill_rect(cux, dy+378, culen, 14, RGB(42,42,55));
    vga_fill_rect(cux, dy+378, culen*sys.cpu_load_percent/100, 14, RGB(60,200,80));
    { char mbuf[40]; char used[16]; char total[16]; int mp=0;
      runtime_format_bytes(sys.pmm_total_bytes - sys.pmm_free_bytes, used, sizeof(used));
      runtime_format_bytes(sys.pmm_total_bytes, total, sizeof(total));
      mbuf[0]=0;
      overlay_append_text(mbuf, &mp, sizeof(mbuf), "Memory: ");
      overlay_append_text(mbuf, &mp, sizeof(mbuf), used);
      overlay_append_text(mbuf, &mp, sizeof(mbuf), " / ");
      overlay_append_text(mbuf, &mp, sizeof(mbuf), total);
      vga_draw_string_trans(dx+20, dy+396, mbuf, RGB(150,150,170)); }
    gui_draw_rounded_rect(dx+dw-100, dy+dh-36, 80, 26, 5, RGB(60,60,75));
    vga_draw_string_trans(dx+dw-88, dy+dh-26, "Done", RGB(220,220,228));
}

void facetime_draw(void) {
    if (!g_facetime_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 200);
    int dw=360, dh=280, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 18, RGB(22,22,28));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 18, RGB(60,60,80));
    int i; for(i=0;i<dh;i++){
        int t=i*40/dh;
        vga_draw_hline(dx, dy+i, dw, RGB(10+t,10+t,20+t*2));
    }
    int cx2=dx+dw/2, cy2=dy+80;
    gui_draw_circle(cx2, cy2, 38, RGB(80,130,200));
    gui_draw_circle_outline(cx2, cy2, 38, RGB(60,100,180));
    gui_draw_circle(cx2, cy2-10, 16, RGB(160,200,240));
    gui_draw_circle(cx2, cy2+22, 26, RGB(100,150,210));
    vga_draw_string_trans(dx+(dw-11*8)/2, cy2+52, g_facetime_calling ? "Connected" : "Ready to call", RGB(220,220,228));
    vga_draw_string_trans(dx+(dw-21*8)/2, cy2+70, "Local camera session", RGB(160,180,210));
    int btn_y=dy+dh-70;
    gui_draw_circle(dx+dw/2-80, btn_y+20, 28, RGB(52,52,60));
    gui_draw_circle_outline(dx+dw/2-80, btn_y+20, 28, RGB(80,80,100));
    vga_draw_string_trans(dx+dw/2-96, btn_y+15, "Close", RGB(200,200,218));
    gui_draw_circle(dx+dw/2+80, btn_y+20, 28, g_facetime_calling ? RGB(255,59,48) : RGB(52,199,89));
    gui_draw_circle_outline(dx+dw/2+80, btn_y+20, 28, RGB(80,80,100));
    vga_draw_string_trans(dx+dw/2+68, btn_y+15, g_facetime_calling ? "End" : "Start", RGB(255,255,255));
    vga_draw_string_trans(dx+dw/2-32, btn_y+44, "FaceTime", RGB(120,140,170));
}

void privacy_panel_draw(void) {
    if (!g_privacy_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 90);
    int dw=500, dh=400, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(38,38,44));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(72,72,85));
    vga_fill_rect(dx, dy, dw, 48, RGB(30,30,38));
    vga_draw_string_trans(dx+(dw-20*8)/2, dy+16, "Privacy & Security", RGB(235,235,240));
    vga_draw_hline(dx, dy+48, dw, RGB(65,65,78));
    static const char *tabs[]={"Location","Camera","Microphone","Screen Rec"};
    int ti; for(ti=0;ti<4;ti++){
        int tx=dx+16+ti*120;
        gui_draw_rounded_rect(tx, dy+56, 112, 26, 5,
            (ti==g_privacy_tab)?RGB(0,90,210):RGB(50,50,62));
        vga_draw_string_trans(tx+(112-str_len(tabs[ti])*8)/2, dy+64, tabs[ti],
            (ti==g_privacy_tab)?RGB(255,255,255):RGB(170,170,188));
    }
    vga_draw_hline(dx, dy+90, dw, RGB(58,58,72));
    int cy=dy+106;
    if(g_privacy_tab==0){
        vga_draw_string_trans(dx+20, cy, "Location Services", RGB(200,200,215));
        gui_draw_rounded_rect(dx+dw-64, cy-2, 44, 24, 12, RGB(52,199,89));
        gui_draw_circle(dx+dw-30, cy+10, 9, RGB(255,255,255));
        cy+=34; vga_draw_hline(dx+16, cy, dw-32, RGB(55,55,70)); cy+=14;
        static const char *apps[]={"Maps","Camera","Weather","Find My","Safari","Photos","Fitness"};
        static int perms[]={2,1,2,2,0,1,1};
        static const char *pstr[]={"Never","Ask","Always"};
        static const uint32_t pcol[]={RGB(255,80,80),RGB(255,180,40),RGB(60,200,80)};
        int ai; for(ai=0;ai<7;ai++){
            vga_draw_string_trans(dx+20, cy+ai*30+8, apps[ai], RGB(205,205,218));
            gui_draw_rounded_rect(dx+dw-100, cy+ai*30, 80, 22, 4, RGB(48,48,62));
            vga_draw_string_trans(dx+dw-85, cy+ai*30+6, pstr[perms[ai]], pcol[perms[ai]]);
            if(ai<6) vga_draw_hline(dx+16, cy+ai*30+26, dw-32, RGB(52,52,68));
        }
    } else if(g_privacy_tab==1){
        vga_draw_string_trans(dx+20, cy, "Camera Access", RGB(200,200,215)); cy+=30;
        static const char *ca[]={"FaceTime","Safari","Messages","Zoom","Teams"};
        static int cv[]={1,0,1,1,0};
        int ci; for(ci=0;ci<5;ci++){
            vga_draw_string_trans(dx+20, cy+ci*32+8, ca[ci], RGB(205,205,218));
            gui_draw_rounded_rect(dx+dw-66, cy+ci*32+2, 46, 22, 11, cv[ci]?RGB(52,199,89):RGB(75,75,88));
            gui_draw_circle(cv[ci]?(dx+dw-32):(dx+dw-53), cy+ci*32+13, 8, RGB(255,255,255));
            if(ci<4) vga_draw_hline(dx+16, cy+ci*32+28, dw-32, RGB(52,52,68));
        }
    } else if(g_privacy_tab==2){
        vga_draw_string_trans(dx+20, cy, "Microphone Access", RGB(200,200,215)); cy+=30;
        static const char *ma[]={"FaceTime","Voice Memos","Siri","Zoom","Discord"};
        static int mv[]={1,1,1,0,1};
        int mi2; for(mi2=0;mi2<5;mi2++){
            vga_draw_string_trans(dx+20, cy+mi2*32+8, ma[mi2], RGB(205,205,218));
            gui_draw_rounded_rect(dx+dw-66, cy+mi2*32+2, 46, 22, 11, mv[mi2]?RGB(52,199,89):RGB(75,75,88));
            gui_draw_circle(mv[mi2]?(dx+dw-32):(dx+dw-53), cy+mi2*32+13, 8, RGB(255,255,255));
            if(mi2<4) vga_draw_hline(dx+16, cy+mi2*32+28, dw-32, RGB(52,52,68));
        }
    } else {
        vga_draw_string_trans(dx+20, cy, "Screen Recording", RGB(200,200,215)); cy+=30;
        vga_draw_string_trans(dx+20, cy+10, "No apps have requested Screen Recording access.", RGB(160,160,180));
        cy+=50;
        vga_draw_string_trans(dx+20, cy, "FileVault", RGB(200,200,215));
        gui_draw_rounded_rect(dx+20, cy+22, dw-40, 56, 8, RGB(45,45,58));
        gui_draw_circle(dx+36, cy+50, 14, RGB(52,199,89));
        vga_draw_string_trans(dx+58, cy+38, "FileVault is ON", RGB(52,199,89));
        vga_draw_string_trans(dx+58, cy+56, "Your disk is encrypted.", RGB(155,155,175));
    }
    int by3=dy+dh-44; vga_draw_hline(dx, by3, dw, RGB(65,65,78));
    gui_draw_rounded_rect(dx+dw-100, by3+8, 80, 28, 5, RGB(0,100,215));
    vga_draw_string_trans(dx+dw-88, by3+16, "Done", RGB(255,255,255));
}

void reminders_draw(void) {
    if (!g_reminders_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 80);
    int dw=460, dh=420, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(30,30,36));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(65,65,78));
    vga_fill_rect(dx, dy, dw, 48, RGB(24,24,30));
    vga_draw_string_trans(dx+(dw-9*8)/2, dy+16, "Reminders", RGB(235,235,240));
    vga_draw_hline(dx, dy+48, dw, RGB(58,58,72));
    static const struct{const char *name;uint32_t col;int cnt;} lists[]={
        {"Today",RGB(255,59,48),3},{"Scheduled",RGB(255,149,0),5},
        {"All",RGB(0,122,255),12},{"Flagged",RGB(255,204,0),2},
        {"Personal",RGB(52,199,89),4},{"Work",RGB(0,122,255),6},
        {"Shopping",RGB(255,149,0),3}};
    int total_lists = 7 + g_reminders_extra_lists;
    if(g_reminders_list==0){
        int li; for(li=0;li<total_lists && li<7;li++){
            int ly=dy+56+li*46;
            int show_new = (g_reminders_extra_lists > 0 && li == 6);
            int built_in = li < 7 && !show_new;
            const char *lname = built_in ? lists[li].name : "New List";
            uint32_t lcol = built_in ? lists[li].col : RGB(0,122,255);
            int lcnt = built_in ? lists[li].cnt : g_reminders_extra_items;
            gui_draw_rounded_rect(dx+16, ly, dw-32, 38, 8, RGB(38,38,48));
            gui_draw_circle(dx+36, ly+19, 14, lcol);
            vga_draw_string_trans(dx+58, ly+13, lname, RGB(225,225,232));
            char cs[4]; int_to_str(lcnt, cs);
            vga_draw_string_trans(dx+dw-30-str_len(cs)*8, ly+13, cs, RGB(140,140,160));
            vga_draw_string_trans(dx+dw-18, ly+13, ">", RGB(100,100,120));
        }
        int by3=dy+dh-46; vga_draw_hline(dx, by3, dw, RGB(58,58,72));
        gui_draw_rounded_rect(dx+20, by3+9, 120, 28, 5, RGB(0,90,200));
        vga_draw_string_trans(dx+28, by3+17, "+ Add List", RGB(255,255,255));
    } else {
        vga_draw_string_trans(dx+20, dy+60, "< Back", RGB(0,122,255));
        static const char *tasks[]={"Buy groceries","Call dentist","Review PR #42",
            "Exercise 30min","Read chapter 5","Send weekly report"};
        static int done[]={1,0,0,1,0,0};
        { int task_limit = g_reminders_extra_items > 0 ? 5 : 6;
          int ti; for(ti=0;ti<task_limit;ti++){
            int ty=dy+80+ti*44;
            gui_draw_rounded_rect(dx+16, ty, dw-32, 36, 6, RGB(38,38,50));
            gui_draw_circle_outline(dx+36, ty+18, 12, done[ti]?RGB(52,199,89):RGB(80,80,100));
            if(done[ti]) gui_draw_circle(dx+36, ty+18, 8, RGB(52,199,89));
            vga_draw_string_trans(dx+56, ty+12, tasks[ti], done[ti]?RGB(100,100,120):RGB(220,220,230));
            if(done[ti]){
                int tl=str_len(tasks[ti]);
                vga_draw_hline(dx+56, ty+19, tl*8, RGB(100,100,120));
            }
          }
        }
        if (g_reminders_extra_items > 0) {
            char rline[32];
            char rnum[8];
            int rp = 0;
            int extra_y = dy + 80 + 5 * 44;
            rline[0] = 0;
            overlay_append_text(rline, &rp, sizeof(rline), "New reminder ");
            runtime_format_uint((uint32_t)g_reminders_extra_items, rnum, sizeof(rnum));
            overlay_append_text(rline, &rp, sizeof(rline), rnum);
            gui_draw_rounded_rect(dx+16, extra_y, dw-32, 36, 6, RGB(38,38,50));
            gui_draw_circle_outline(dx+36, extra_y+18, 12, RGB(80,80,100));
            vga_draw_string_trans(dx+56, extra_y+12, rline, RGB(220,220,230));
        }
        int by3=dy+dh-46; vga_draw_hline(dx, by3, dw, RGB(58,58,72));
        gui_draw_rounded_rect(dx+20, by3+9, 120, 28, 5, RGB(0,90,200));
        vga_draw_string_trans(dx+28, by3+17, "+ New Reminder", RGB(255,255,255));
        gui_draw_rounded_rect(dx+dw-90, by3+9, 70, 28, 5, RGB(55,55,68));
        vga_draw_string_trans(dx+dw-82, by3+17, "Done", RGB(220,220,228));
    }
}

void calendar_draw(void) {
    if (!g_calendar_visible) return;
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), 90);
    int dw=500, dh=420, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
    gui_draw_rounded_rect(dx, dy, dw, dh, 14, RGB(28,28,34));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 14, RGB(65,65,80));
    vga_fill_rect(dx, dy, dw, 48, RGB(22,22,30));
    vga_draw_string_trans(dx+24, dy+16, "<", RGB(0,122,255));
    vga_draw_string_trans(dx+dw-18, dy+16, ">", RGB(0,122,255));
    datetime_t now_dt;
    get_current_datetime(&now_dt);
    if (g_calendar_year < 1) {
        g_calendar_year = now_dt.year;
        g_calendar_month = now_dt.month - 1;
    }
    if (g_calendar_month < 0) g_calendar_month = 0;
    if (g_calendar_month > 11) g_calendar_month = 11;
    static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char mstr[16]; int mi=0;
    { const char *s=months[g_calendar_month]; while(*s) mstr[mi++]=*s++; }
    mstr[mi++]=' ';
    char yr[8]; int_to_str(g_calendar_year, yr);
    { const char *s=yr; while(*s) mstr[mi++]=*s++; }
    mstr[mi]=0;
    vga_draw_string_trans(dx+(dw-mi*8)/2, dy+16, mstr, RGB(235,235,240));
    vga_draw_hline(dx, dy+48, dw, RGB(58,58,72));
    static const char *dnames[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int di; for(di=0;di<7;di++)
        vga_draw_string_trans(dx+20+di*68, dy+56, dnames[di], RGB(150,150,170));
    vga_draw_hline(dx, dy+74, dw, RGB(52,52,68));
    int dim=datetime_days_in_month(g_calendar_year, g_calendar_month + 1);
    int start_dow=datetime_day_of_week(g_calendar_year, g_calendar_month + 1, 1);
    static const int evdays[]={5,12,18,24,28};
    static const char *evnames[]={"Meeting","Dentist","Lunch","Review","Trip"};
    static const uint32_t evcols[]={RGB(255,59,48),RGB(52,199,89),RGB(0,122,255),RGB(255,149,0),RGB(175,82,222)};
    int day; for(day=1;day<=dim;day++){
        int pos=day-1+start_dow;
        int col=pos%7, row=pos/7;
        int cx2=dx+20+col*68, cy2=dy+82+row*52;
        int is_today=(day==now_dt.day &&
                      g_calendar_month + 1 == now_dt.month &&
                      g_calendar_year == now_dt.year);
        if(is_today){
            gui_draw_circle(cx2+12, cy2+12, 14, RGB(255,59,48));
            vga_draw_string_trans(day<10?(cx2+8):(cx2+4), cy2+7,
                day<10?"0":"", RGB(255,255,255));
            char ds[4]; int_to_str(day, ds); vga_draw_string_trans(day<10?(cx2+16):(cx2+5), cy2+7, ds, RGB(255,255,255));
        } else {
            char ds[4]; int_to_str(day, ds);
            vga_draw_string_trans(cx2+4, cy2+7, ds, RGB(210,210,225));
        }
        int ei; for(ei=0;ei<5;ei++){
            if(evdays[ei]==day){
                gui_draw_rounded_rect(cx2, cy2+26, 60, 14, 3, evcols[ei]);
                vga_draw_string_trans(cx2+4, cy2+29, evnames[ei], RGB(255,255,255));
            }
        }
    }
    if (g_calendar_added_events > 0 && g_calendar_added_day > 0 &&
        g_calendar_month + 1 == now_dt.month && g_calendar_year == now_dt.year) {
        int pos2=g_calendar_added_day-1+start_dow;
        int col2=pos2%7, row2=pos2/7;
        int ex=dx+20+col2*68, ey=dy+82+row2*52;
        gui_draw_rounded_rect(ex, ey+42, 60, 10, 3, RGB(0,122,255));
        vga_draw_string_trans(ex+4, ey+44, "New", RGB(255,255,255));
    }
    int by3=dy+dh-38; vga_draw_hline(dx, by3, dw, RGB(58,58,72));
    vga_draw_string_trans(dx+(dw-16*8)/2, by3+12, "Add New Event...", RGB(0,122,255));
}

void airplay_draw(void) {
    if (!g_airplay_visible) return;
    int dw=280, dh=320, dx=VGA_WIDTH-dw-12, dy=28;
    gui_draw_rounded_rect(dx, dy, dw, dh, 12, RGB(34,34,40));
    gui_draw_rounded_rect_outline(dx, dy, dw, dh, 12, RGB(68,68,82));
    vga_fill_rect(dx, dy, dw, 44, RGB(26,26,34));
    vga_draw_string_trans(dx+(dw-7*8)/2, dy+14, "AirPlay", RGB(235,235,240));
    vga_draw_hline(dx, dy+44, dw, RGB(62,62,76));
    vga_draw_string_trans(dx+16, dy+54, "OUTPUT TO:", RGB(115,115,138));
    gui_draw_rounded_rect(dx+12, dy+72, dw-24, 54, 8, g_airplay_selected == 0 ? RGB(52,62,84) : RGB(42,42,55));
    gui_draw_circle(dx+34, dy+99, 14, RGB(80,80,100));
    vga_draw_string_trans(dx+28, dy+94, "MC", RGB(200,200,215));
    vga_draw_string_trans(dx+56, dy+88, "This Mac", RGB(255,255,255));
    vga_draw_string_trans(dx+56, dy+106, "Built-in display", RGB(190,220,255));
    vga_draw_hline(dx+16, dy+144, dw-32, RGB(52,52,68));
    if (g_airplay_scan_count > 0) {
        gui_draw_rounded_rect(dx+12, dy+152, dw-24, 54, 8, g_airplay_selected == 1 ? RGB(52,62,84) : RGB(42,42,55));
        gui_draw_circle(dx+34, dy+179, 14, RGB(0,122,255));
        vga_draw_string_trans(dx+26, dy+174, "TV", RGB(255,255,255));
        vga_draw_string_trans(dx+56, dy+168, "Living Room TV", RGB(255,255,255));
        vga_draw_string_trans(dx+56, dy+186, g_airplay_selected == 1 ? "Streaming display" : "AirPlay receiver", RGB(190,220,255));
    } else {
        vga_draw_string_trans(dx+16, dy+166, "No AirPlay receivers found", RGB(200,200,215));
    }
    if (g_airplay_scan_count > 0) {
        char aline[48];
        char anum[12];
        int ap = 0;
        aline[0] = 0;
        if (g_airplay_selected == 1) {
            overlay_append_text(aline, &ap, sizeof(aline), "Output: Living Room TV");
            vga_draw_string_trans(dx+16, dy+216, aline, RGB(145,145,165));
            aline[0] = 0;
            ap = 0;
        }
        overlay_append_text(aline, &ap, sizeof(aline), "Last scan: ");
        runtime_format_uint((uint32_t)g_airplay_scan_count, anum, sizeof(anum));
        overlay_append_text(aline, &ap, sizeof(aline), anum);
        overlay_append_text(aline, &ap, sizeof(aline), " completed");
        vga_draw_string_trans(dx+16, g_airplay_selected == 1 ? dy+234 : dy+216, aline, RGB(145,145,165));
    } else {
        vga_draw_string_trans(dx+16, dy+184, "Click Scan to discover receivers", RGB(145,145,165));
    }
    int by3=dy+dh-38; vga_draw_hline(dx, by3, dw, RGB(62,62,76));
    vga_draw_string_trans(dx+16, by3+12, "Scan", RGB(0,122,255));
    vga_draw_string_trans(dx+dw-52, by3+12, "Done", RGB(0,122,255));
}

/* Returns 1 if click consumed, 0 otherwise */
int new_overlays_click(int mx, int my) {
    #define HIT(x,y,w2,h2) (mx>=(x)&&mx<(x)+(w2)&&my>=(y)&&my<(y)+(h2))
    #define HITR(cx2,cy2,r2) (((mx-(cx2))*(mx-(cx2))+(my-(cy2))*(my-(cy2)))<=(r2)*(r2))

    /* Writing Tools */
    if (g_wt_visible) {
        int wt_w=300, wt_h=220;
        int wt_x=(VGA_WIDTH-wt_w)/2, wt_y=(VGA_HEIGHT-wt_h)/2;
        int bi2;
        if (!HIT(wt_x, wt_y, wt_w, wt_h)) { g_wt_visible=0; return 1; }
        if (HIT(wt_x+wt_w-24, wt_y, 24, 26)) { g_wt_visible=0; return 1; }
        if (g_wt_done == 2 && HIT(wt_x+wt_w/2-30, wt_y+wt_h-24, 60, 18)) { g_wt_visible=0; return 1; }
        if (g_wt_done == 0) {
            int bx2=wt_x+8, by2=wt_y+32;
            for (bi2=0; bi2<6; bi2++) {
                int brow=bi2/3, bcol=bi2%3;
                int bxp=bx2+bcol*((wt_w-16)/3), byp=by2+brow*52;
                int bw2=(wt_w-16)/3-4;
                if (HIT(bxp, byp, bw2, 46)) { (void)writing_tools_apply(bi2); return 1; }
            }
        }
        return 1;
    }

    /* Crash Reporter */
    if (g_crash_visible) {
        int dw=460, dh=200;
        int dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) return 0;
        int by2=dy+dh-44;
        if (HIT(dx+dw-110, by2, 90, 30)) { g_crash_visible=0; return 1; }
        return 1;
    }

    /* System Update */
    if (g_update_visible) {
        int dw=520, dh=360, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_update_visible=0; return 1; }
        int by2=dy+dh-50;
        if (HIT(dx+dw-110, by2, 90, 32)) { g_update_visible=0; return 1; }
        return 1;
    }

    /* Focus Filter */
    if (g_focus_filter_visible) {
        int dw=480, dh=380, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_focus_filter_visible=0; return 1; }
        int t2; for(t2=0;t2<4;t2++){
            if (HIT(dx+20+t2*112, dy+56, 100, 28)) { g_focus_filter_mode=t2; return 1; }
        }
        int by2=dy+dh-44;
        if (HIT(dx+20, by2, 80, 30)) { g_focus_filter_visible=0; return 1; }
        if (HIT(dx+dw-100, by2, 80, 30)) { g_focus_filter_visible=0; toast_show("Focus Filters","Applied",RGB(100,65,165)); return 1; }
        return 1;
    }

    /* iCloud */
    if (g_icloud_visible) {
        int dw=380, dh=320, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_icloud_visible=0; return 1; }
        int by2=dy+dh-40;
        if (HIT(dx+dw-100, by2-5, 80, 26)) { g_icloud_visible=0; return 1; }
        return 1;
    }

    /* Bluetooth */
    if (g_bt_visible) {
        int dw=400, dh=320, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_bt_visible=0; return 1; }
        int by2=dy+dh-42;
        if (HIT(dx+20, by2, 80, 28)) { g_pref_bt^=1; toast_show("Bluetooth",g_pref_bt?"On":"Off",RGB(0,122,255)); return 1; }
        if (HIT(dx+dw-100, by2, 80, 28)) { g_bt_visible=0; return 1; }
        return 1;
    }

    /* Keyboard Shortcuts */
    if (g_kbshort_visible) {
        int dw=570, dh=450, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_kbshort_visible=0; return 1; }
        if (g_kbshort_page>0 && HIT(dx+20,dy+dh-36,70,24)) { g_kbshort_page--; return 1; }
        if (g_kbshort_page<2 && HIT(dx+dw-90,dy+dh-36,70,24)) { g_kbshort_page++; return 1; }
        if (g_kbshort_page==2 && HIT(dx+dw-90,dy+dh-36,70,24)) { g_kbshort_visible=0; return 1; }
        return 1;
    }

    /* Time Machine */
    if (g_timemachine_visible) {
        int dw=480, dh=350, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_timemachine_visible=0; return 1; }
        int by2=dy+dh-44;
        if (HIT(dx+20, by2, 150, 28)) {
            if (g_tm_snapshot_count < 99) g_tm_snapshot_count++;
            g_tm_last_snapshot_tick = timer_ticks();
            toast_show("Time Machine","Local snapshot created",RGB(52,199,89));
            return 1;
        }
        if (HIT(dx+dw-100, by2, 80, 28)) { g_timemachine_visible=0; return 1; }
        return 1;
    }

    /* Color Meter */
    if (g_colormeter_visible) {
        g_colormeter_visible=0; return 1;
    }

    /* Notification History */
    if (g_notifhist_visible) {
        int dw=340, dh=460, dx=VGA_WIDTH-dw-8, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_notifhist_visible=0; return 1; }
        int by2=dy+dh-42;
        if (HIT(dx+20, by2+8, 80, 20)) {
            g_notifhist_clear_count++;
            g_nc_count = 0;
            toast_show("Notifications","History cleared",RGB(100,100,120));
            return 1;
        }
        if (HIT(dx+dw-100, by2+8, 80, 26)) { g_notifhist_visible=0; return 1; }
        return 1;
    }

    /* WiFi Panel */
    if (g_wifi_visible) {
        int dw=340, dh=380, dx=VGA_WIDTH-dw-12, dy=28;
        if (!HIT(dx,dy,dw,dh)) { g_wifi_visible=0; return 1; }
        if (HIT(dx+16, dy+28, dw-32, 20) || HIT(dx+12, dy+80, dw-24, 54)) {
            g_pref_wifi^=1; toast_show("Wi-Fi",g_pref_wifi?"On":"Off",RGB(0,122,255));
            return 1;
        }
        int by2=dy+dh-38;
        if (HIT(dx+dw-72, by2, 72, 20)) { g_wifi_visible=0; return 1; }
        return 1;
    }

    /* Display Settings */
    if (g_display_visible) {
        int dw=480, dh=380, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_display_visible=0; return 1; }
        int by3=dy+dh-44;
        if (HIT(dx+dw-100, by3, 80, 28)) { g_display_visible=0; return 1; }
        int bry=dy+68;
        int bx=dx+140, bw=dw-170;
        if (HIT(bx, bry, bw, 16)) { g_display_brightness=(mx-bx)*100/bw; if(g_display_brightness>100)g_display_brightness=100; if(g_display_brightness<0)g_display_brightness=0; return 1; }
        { int ry=dy+68+50, ri;
          for (ri=0; ri<4; ri++) {
              int ry2=ry+24+ri*34;
              if (HIT(dx+140, ry2, dw-164, 26)) { g_pref_resolution=ri; return 1; }
          } }
        return 1;
    }

    /* Sound Settings */
    if (g_sound_visible) {
        int dw=460, dh=360, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_sound_visible=0; return 1; }
        int by3=dy+dh-44;
        if (HIT(dx+dw-100, by3, 80, 28)) { g_sound_visible=0; return 1; }
        int sy=dy+64;
        int vx=dx+178, vw=dw-210;
        if (HIT(vx, sy, vw, 16)) { g_sound_volume=(mx-vx)*100/vw; if(g_sound_volume>100)g_sound_volume=100; if(g_sound_volume<0)g_sound_volume=0; return 1; }
        { int sy2=sy+42, oi;
          for (oi=0; oi<4; oi++) {
              int oy=sy2+22+oi*32;
              if (HIT(dx+130, oy, dw-154, 24)) { g_sound_output_device=oi; return 1; }
          }
          { int sy3=sy2+22+4*32+20, ii;
            for (ii=0; ii<2; ii++) {
                int iy=sy3+22+ii*32;
                if (HIT(dx+130, iy, dw-154, 24)) { g_sound_input_device=ii; return 1; }
            } } }
        return 1;
    }

    /* Activity Monitor */
    if (g_actmon_visible) {
        int dw=580, dh=420, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_actmon_visible=0; return 1; }
        if (HIT(dx+dw-100, dy+dh-36, 80, 26)) { g_actmon_visible=0; return 1; }
        return 1;
    }

    /* FaceTime */
    if (g_facetime_visible) {
        int dw=360, dh=280, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) return 0;
        int btn_y=dy+dh-70;
        if (HITR(dx+dw/2-80, btn_y+20, 28)) { g_facetime_visible=0; return 1; }
        if (HITR(dx+dw/2+80, btn_y+20, 28)) {
            g_facetime_calling^=1;
            toast_show("FaceTime",g_facetime_calling?"Connected":"Ended",g_facetime_calling?RGB(52,199,89):RGB(255,59,48));
            return 1;
        }
        return 1;
    }

    /* Privacy */
    if (g_privacy_visible) {
        int dw=500, dh=400, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_privacy_visible=0; return 1; }
        int ti; for(ti=0;ti<4;ti++){
            if (HIT(dx+16+ti*120, dy+56, 112, 26)) { g_privacy_tab=ti; return 1; }
        }
        if (HIT(dx+dw-100, dy+dh-44, 80, 28)) { g_privacy_visible=0; return 1; }
        return 1;
    }

    /* Reminders */
    if (g_reminders_visible) {
        int dw=460, dh=420, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_reminders_visible=0; return 1; }
        if (g_reminders_list==0) {
            int li; for(li=0;li<7;li++) {
                if (HIT(dx+16, dy+56+li*46, dw-32, 38)) { g_reminders_list=1; return 1; }
            }
            int by3=dy+dh-46;
            if (HIT(dx+20, by3+9, 120, 28)) {
                g_reminders_extra_lists++;
                toast_show("Reminders","List added",RGB(255,59,48));
                return 1;
            }
        } else {
            if (HIT(dx+20, dy+60, 60, 16)) { g_reminders_list=0; return 1; }
            int by3=dy+dh-46;
            if (HIT(dx+20, by3+9, 120, 28)) {
                g_reminders_extra_items++;
                toast_show("Reminders","Reminder added",RGB(255,59,48));
                return 1;
            }
            if (HIT(dx+dw-90, by3+9, 70, 28)) { g_reminders_list=0; return 1; }
        }
        return 1;
    }

    /* Calendar */
    if (g_calendar_visible) {
        int dw=500, dh=420, dx=(VGA_WIDTH-dw)/2, dy=(VGA_HEIGHT-dh)/2;
        if (!HIT(dx,dy,dw,dh)) { g_calendar_visible=0; return 1; }
        if (g_calendar_year < 1) {
            datetime_t now_dt;
            get_current_datetime(&now_dt);
            g_calendar_year = now_dt.year;
            g_calendar_month = now_dt.month - 1;
        }
        if (HIT(dx+16, dy+8, 24, 24)) {
            if (--g_calendar_month < 0) { g_calendar_month=11; g_calendar_year--; }
            return 1;
        }
        if (HIT(dx+dw-24, dy+8, 24, 24)) {
            if (++g_calendar_month > 11) { g_calendar_month=0; g_calendar_year++; }
            return 1;
        }
        if (HIT(dx+(dw-16*8)/2, dy+dh-38, 16*8, 20)) {
            datetime_t now_dt2;
            get_current_datetime(&now_dt2);
            g_calendar_added_day = now_dt2.day;
            g_calendar_added_events++;
            toast_show("Calendar","Event added for today",RGB(255,59,48));
            return 1;
        }
        return 1;
    }

    /* AirPlay */
    if (g_airplay_visible) {
        int dw=280, dh=320, dx=VGA_WIDTH-dw-12, dy=28;
        if (!HIT(dx,dy,dw,dh)) { g_airplay_visible=0; return 1; }
        int by3=dy+dh-38;
        if (HIT(dx+16, by3, 150, 20)) {
            g_airplay_scan_count++;
            g_airplay_last_scan_tick = timer_ticks();
            toast_show("AirPlay","Scan complete",RGB(0,122,255));
            return 1;
        }
        if (HIT(dx+dw-52, by3, 52, 20)) { g_airplay_visible=0; return 1; }
        if (HIT(dx+12, dy+72, dw-24, 54)) {
            g_airplay_selected = 0;
            toast_show("AirPlay","Using built-in display",RGB(0,122,255));
            return 1;
        }
        if (g_airplay_scan_count > 0 && HIT(dx+12, dy+152, dw-24, 54)) {
            g_airplay_selected = 1;
            toast_show("AirPlay","Streaming to Living Room TV",RGB(0,122,255));
            return 1;
        }
        return 1;
    }

    #undef HIT
    #undef HITR
    return 0;
}
