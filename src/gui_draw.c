#include "gui_internal.h"

void gui_draw_circle(int cx, int cy, int r, uint32_t color) {
    int x0, y0;
    for (y0 = -r; y0 <= r; y0++)
        for (x0 = -r; x0 <= r; x0++)
            if (x0 * x0 + y0 * y0 <= r * r)
                vga_put_pixel(cx + x0, cy + y0, color);
}

void gui_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
    if (r <= 0) { vga_fill_rect(x, y, w, h, color); return; }
    /* centre strip */
    vga_fill_rect(x + r, y,           w - 2 * r, h,           color);
    /* left/right side strips */
    vga_fill_rect(x,         y + r,   r,          h - 2 * r,   color);
    vga_fill_rect(x + w - r, y + r,   r,          h - 2 * r,   color);
    /* corner circles */
    gui_draw_circle(x + r,         y + r,         r, color);
    gui_draw_circle(x + w - 1 - r, y + r,         r, color);
    gui_draw_circle(x + r,         y + h - 1 - r, r, color);
    gui_draw_circle(x + w - 1 - r, y + h - 1 - r, r, color);
}

void gui_draw_rounded_rect_outline(int x, int y, int w, int h, int r, uint32_t color) {
    int i, j;
    /* straight edges */
    vga_draw_hline(x + r, y,         w - 2 * r, color);
    vga_draw_hline(x + r, y + h - 1, w - 2 * r, color);
    vga_draw_vline(x,         y + r,  h - 2 * r, color);
    vga_draw_vline(x + w - 1, y + r,  h - 2 * r, color);
    /* corners */
    for (i = 0; i <= r; i++) {
        j = 0;
        while ((i * i + j * j) < r * r) j++;
        vga_put_pixel(x + r - i,             y + r - j,             color);
        vga_put_pixel(x + w - 1 - r + i,     y + r - j,             color);
        vga_put_pixel(x + r - i,             y + h - 1 - r + j,     color);
        vga_put_pixel(x + w - 1 - r + i,     y + h - 1 - r + j,     color);
    }
}

/* =========================================================================
 * Desktop gradient background
 * ======================================================================= */

static uint8_t lerp8(uint8_t a, uint8_t b, int num, int den) {
    int diff = (int)b - (int)a;
    return (uint8_t)((int)a + diff * num / den);
}

/* Compute the desktop gradient color at absolute pixel y — must match gui_draw_desktop() */
static uint32_t desktop_color_at(int y) {
    int rel = y - DESKTOP_Y;
    if (rel < 0) rel = 0;
    if (rel >= DESKTOP_H) rel = DESKTOP_H - 1;
    if (g_pref_darkmode)
        return RGB(lerp8(15,5,rel,DESKTOP_H), lerp8(15,5,rel,DESKTOP_H), lerp8(20,10,rel,DESKTOP_H));
    int wp_idx = (g_current_space >= 1 && g_current_space <= 4)
                 ? g_space_wallpaper[g_current_space - 1] : g_pref_wallpaper;
    uint8_t r0,g0,b0,rm,gm,bm,r1,g1,b1;
    if (wp_idx == 1) {
        r0=255;g0=140;b0=60; rm=220;gm=60;bm=80; r1=100;g1=20;b1=80;
    } else if (wp_idx == 2) {
        r0=40;g0=100;b0=60;  rm=20;gm=70;bm=30;  r1=10;g1=40;b1=20;
    } else if (wp_idx == 3) {
        r0=5;g0=5;b0=30;     rm=10;gm=5;bm=50;   r1=20;g1=0;b1=60;
    } else if (wp_idx == 4) {
        r0=255;g0=180;b0=60; rm=200;gm=120;bm=40; r1=80;g1=40;b1=20;
    } else { /* dynamic (0) — approximate with current sky color */
        r0=70;g0=130;b0=190; rm=40;gm=85;bm=155; r1=20;g1=50;b1=120;
    }
    int half = DESKTOP_H / 2;
    uint8_t rr,gg,bb;
    if (rel < half) {
        rr=lerp8(r0,rm,rel,half); gg=lerp8(g0,gm,rel,half); bb=lerp8(b0,bm,rel,half);
    } else {
        rr=lerp8(rm,r1,rel-half,half); gg=lerp8(gm,g1,rel-half,half); bb=lerp8(bm,b1,rel-half,half);
    }
    return RGB(rr,gg,bb);
}

/* Stamp window-corner cutouts in desktop color to fake rounded corners.
   r = corner radius; window at (wx,wy) size (ww,wh). */
static void draw_window_corners(int wx, int wy, int ww, int wh, int r) {
    int dy;
    for (dy = 0; dy < r; dy++) {
        /* Find how many corner pixels to erase: circle math dx = r - sqrt(r^2 - (r-dy)^2) */
        int inner = r - dy; /* distance from corner center */
        int x_cut = 0;
        while (x_cut < r && (x_cut)*(x_cut) + inner*inner > r*r) x_cut++;
        /* x_cut = number of pixels from the edge to erase */
        int x;
        uint32_t bg_top = desktop_color_at(wy + dy);
        uint32_t bg_bot = desktop_color_at(wy + wh - 1 - dy);
        for (x = 0; x < x_cut && x < ww; x++) {
            /* Top corners */
            vga_put_pixel(wx + x,          wy + dy,          bg_top);
            vga_put_pixel(wx + ww - 1 - x, wy + dy,          bg_top);
            /* Bottom corners */
            vga_put_pixel(wx + x,          wy + wh - 1 - dy, bg_bot);
            vga_put_pixel(wx + ww - 1 - x, wy + wh - 1 - dy, bg_bot);
        }
    }
}

/* Desktop file icon helper */
static void draw_desktop_icon(int x, int y, const char *name, uint32_t color, int icon_type) {
    /* File icon: small rounded square */
    gui_draw_rounded_rect(x, y, 36, 36, 6, color);
    /* Shine highlight on top of icon */
    vga_fill_rect_alpha(x+2, y+2, 32, 12, RGB(255,255,255), 90);
    /* Icon content based on type */
    switch (icon_type) {
    case 0: /* Drive/disk: HDD rectangle */
        vga_fill_rect(x+6, y+10, 24, 16, RGB(200,220,255));
        vga_fill_rect(x+8, y+12, 6, 4, RGB(100,160,255));
        vga_fill_rect(x+18, y+12, 8, 4, RGB(100,160,255));
        break;
    case 1: /* Notes: yellow pad with lines */
        vga_fill_rect(x+8, y+8, 20, 22, RGB(255,245,140));
        {int li; for (li=0;li<3;li++) vga_draw_hline(x+10, y+13+li*5, 14, RGB(180,160,60));}
        break;
    case 2: /* Trash: can outline */
        vga_fill_rect(x+10, y+13, 16, 16, RGB(200,200,200));
        vga_fill_rect(x+9, y+10, 18, 4, RGB(180,180,180));  /* lid */
        vga_fill_rect(x+14, y+7, 8, 5, RGB(180,180,180));   /* handle */
        {int si; for (si=0;si<3;si++) vga_draw_vline(x+13+si*4, y+15, 10, RGB(140,140,140));}
        break;
    case 3: /* Downloads: arrow down */
        vga_fill_rect(x+16, y+8, 4, 16, RGB(255,255,255));
        {int ai; for (ai=0;ai<8;ai++) vga_draw_hline(x+12+ai/2, y+20+ai, 12-ai, RGB(255,255,255));}
        vga_draw_hline(x+6, y+28, 24, RGB(255,255,255));
        break;
    case 4: /* Documents: page with fold */
        vga_fill_rect(x+8, y+8, 18, 22, RGB(255,255,255));
        vga_fill_rect(x+20, y+8, 6, 6, RGB(color>>16&0xFF, color>>8&0xFF, color&0xFF));
        {int li; for (li=0;li<3;li++) vga_draw_hline(x+10, y+16+li*4, 12, RGB(150,150,200));}
        break;
    default: /* generic letter */
        break;
    }
    /* Label below */
    int nl = str_len(name);
    int lx = x + (36 - nl*8)/2;
    if (lx < x) lx = x;
    /* Label shadow + text */
    vga_draw_string_trans(lx+1, y+39, name, RGB(0,0,0));
    vga_draw_string_trans(lx,   y+38, name, RGB(240,240,240));
}

void gui_draw_desktop(void) {
    /* Use per-space wallpaper when not in dark mode */
    int wp_idx = (g_current_space >= 1 && g_current_space <= 4)
                 ? g_space_wallpaper[g_current_space - 1]
                 : g_pref_wallpaper;
    int eff_wallpaper = g_pref_darkmode ? g_pref_wallpaper : wp_idx;
    uint8_t r0, g0, b0, r1, g1, b1, rm, gm, bm;
    if (g_pref_darkmode) {
        r0=15;  g0=15;  b0=25;
        rm=10;  gm=10;  bm=20;
        r1=5;   g1=5;   b1=15;
    } else if (eff_wallpaper == 1) { /* sunset */
        r0=255; g0=140; b0=60;
        rm=220; gm=60;  bm=80;
        r1=100; g1=20;  b1=80;
    } else if (eff_wallpaper == 2) { /* forest */
        r0=40;  g0=100; b0=60;
        rm=20;  gm=70;  bm=30;
        r1=10;  g1=40;  b1=20;
    } else if (eff_wallpaper == 3) { /* space */
        r0=5;   g0=5;   b0=30;
        rm=10;  gm=5;   bm=50;
        r1=20;  g1=0;   b1=60;
    } else if (eff_wallpaper == 4) { /* Sequoia: golden California sunrise */
        r0=255; g0=180; b0=60;   /* warm golden sky top */
        rm=200; gm=120; bm=40;   /* rich amber mid */
        r1=80;  g1=40;  b1=20;   /* deep brown earth */
    } else { /* dynamic day-night cycle (wallpaper 0) */
        /* Key colors at 5 time points: night→dawn→morning→noon→sunset */
        /* top_r, top_g, top_b  mid_r, mid_g, mid_b  bot_r, bot_g, bot_b */
        static const uint8_t wp_keys[5][9] = {
            {  5, 10, 40,   8, 12, 55,  12, 18, 70 }, /* night   */
            {200, 80, 30, 220,120, 60, 180, 60, 50 }, /* dawn    */
            { 70,130,190,  40, 85,155,  20, 50,120 }, /* morning */
            {100,160,210,  60,110,180,  30, 70,140 }, /* noon    */
            {230, 90, 30, 200, 50, 60, 100, 20, 60 }, /* sunset  */
        };
        /* Cycle: 0-4 = night, 4-16 = dawn, 16-28 = morning, 28-40 = noon, 40-52 = sunset, 52-64 = night */
        uint32_t t_wp = timer_ticks();
        int phase = (int)((t_wp / 5000) % 64); /* 0-63, one full cycle = ~5.3 min */
        int pi, pf; /* phase index and fractional */
        if      (phase <  4) { pi=0; pf = phase*255/4; }
        else if (phase < 16) { pi=1; pf = (phase-4)*255/12; }
        else if (phase < 28) { pi=2; pf = (phase-16)*255/12; }
        else if (phase < 40) { pi=3; pf = (phase-28)*255/12; }
        else if (phase < 52) { pi=4; pf = (phase-40)*255/12; }
        else                  { pi=0; pf = (phase-52)*255/12; } /* wrap back to night */
        int pi2 = (pi+1) % 5;
        r0 = (uint8_t)(wp_keys[pi][0] + (int)(wp_keys[pi2][0]-wp_keys[pi][0])*pf/255);
        g0 = (uint8_t)(wp_keys[pi][1] + (int)(wp_keys[pi2][1]-wp_keys[pi][1])*pf/255);
        b0 = (uint8_t)(wp_keys[pi][2] + (int)(wp_keys[pi2][2]-wp_keys[pi][2])*pf/255);
        rm = (uint8_t)(wp_keys[pi][3] + (int)(wp_keys[pi2][3]-wp_keys[pi][3])*pf/255);
        gm = (uint8_t)(wp_keys[pi][4] + (int)(wp_keys[pi2][4]-wp_keys[pi][4])*pf/255);
        bm = (uint8_t)(wp_keys[pi][5] + (int)(wp_keys[pi2][5]-wp_keys[pi][5])*pf/255);
        r1 = (uint8_t)(wp_keys[pi][6] + (int)(wp_keys[pi2][6]-wp_keys[pi][6])*pf/255);
        g1 = (uint8_t)(wp_keys[pi][7] + (int)(wp_keys[pi2][7]-wp_keys[pi][7])*pf/255);
        b1 = (uint8_t)(wp_keys[pi][8] + (int)(wp_keys[pi2][8]-wp_keys[pi][8])*pf/255);
    }
    int total = DESKTOP_H;
    int y;
    for (y = 0; y < DESKTOP_H; y++) {
        uint8_t rr, gg, bb;
        if (y < total/2) {
            rr = lerp8(r0, rm, y, total/2);
            gg = lerp8(g0, gm, y, total/2);
            bb = lerp8(b0, bm, y, total/2);
        } else {
            rr = lerp8(rm, r1, y - total/2, total/2);
            gg = lerp8(gm, g1, y - total/2, total/2);
            bb = lerp8(bm, b1, y - total/2, total/2);
        }
        vga_draw_hline(0, DESKTOP_Y + y, VGA_WIDTH, RGB(rr, gg, bb));
    }

    /* Mountain silhouette (Sonoma-style) at bottom of desktop */
    if (!g_pref_darkmode) {
        /* Far mountains (lighter) */
        int mi;
        static const int mh1[VGA_WIDTH/8] = {
            60,65,70,75,80,85,90,95,100,95,90,85,80,75,70,65,
            60,55,50,55,60,65,70,80,90,100,110,115,110,100,90,80,
            70,65,60,55,50,45,50,55,60,65,70,75,80,85,90,95,
            100,110,115,110,100,90,85,80,75,70,65,60,55,50,48,46,
            45,50,55,60,65,70,75,80,85,90,95,100,95,90,85,80,
            75,70,65,60,55,50,45,40,35,40,45,50,55,60,65,70,
            75,80,85,90
        };
        for (mi = 0; mi < VGA_WIDTH/8 && mi*8 < VGA_WIDTH; mi++) {
            int mh = mh1[mi];
            int my2 = DESKTOP_Y + DESKTOP_H - mh;
            uint32_t mcol;
            if (eff_wallpaper == 1) mcol = RGB(140,40,60);
            else if (eff_wallpaper == 2) mcol = RGB(15,50,20);
            else if (eff_wallpaper == 3) mcol = RGB(10,0,30);
            else if (eff_wallpaper == 4) mcol = RGB(100,60,20);
            else { /* dynamic: blend mountain color with bottom gradient */
                uint8_t mr = (uint8_t)(r1 < 20 ? r1 : r1-15);
                uint8_t mgx = (uint8_t)(g1 < 20 ? g1 : g1-15);
                uint8_t mb2 = (uint8_t)(b1 < 20 ? b1 : b1-15);
                mcol = RGB(mr, mgx, mb2);
            }
            vga_fill_rect(mi*8, my2, 8, mh, mcol);
        }
        /* Near mountains (darker, in front) */
        static const int mh2[VGA_WIDTH/4] = {
            20,25,30,35,40,50,60,70,80,90,100,110,115,110,100,90,
            80,70,60,50,45,40,35,30,25,30,35,40,50,60,70,80,
            90,95,100,110,120,125,120,115,110,100,90,85,80,75,70,65,
            60,55,50,45,40,38,36,35,38,42,48,55,62,70,75,80,
            85,90,85,80,75,70,65,60,55,50,45,40,38,36,35,34,
            35,38,40,45,50,55,60,65,70,75,80,85,90,95,100,105,
            110,115,110,100,90,80,70,60,50,40,35,30,25,20,18,16,
            15,18,22,28,35,42,50,60,70,80,90,95,90,85,80,75,
            70,65,60,55,50,45,40,35,30,25,20,18,16,14,12,10,
            12,16,20,25,30,35,40,45,50,55,60,65,70,75,70,65,
            60,55,50,45,40,35,30,28,26,24,22,20,18,16,14,12,
            10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,
            42,44,46,48,50,52,54,56
        };
        for (mi = 0; mi < VGA_WIDTH/4 && mi*4 < VGA_WIDTH; mi++) {
            int mh = mh2[mi] > DESKTOP_H - 30 ? DESKTOP_H - 30 : mh2[mi];
            int my2 = DESKTOP_Y + DESKTOP_H - mh;
            uint32_t mcol2;
            if (eff_wallpaper == 1) mcol2 = RGB(80,15,30);
            else if (eff_wallpaper == 2) mcol2 = RGB(8,30,12);
            else if (eff_wallpaper == 3) mcol2 = RGB(5,0,15);
            else if (eff_wallpaper == 4) mcol2 = RGB(40,20,5);
            else {
                uint8_t mr2 = (uint8_t)(r1/2);
                uint8_t mgx2 = (uint8_t)(g1/2);
                uint8_t mb3 = (uint8_t)(b1/2);
                mcol2 = RGB(mr2, mgx2, mb3);
            }
            vga_fill_rect(mi*4, my2, 4, mh, mcol2);
        }

        /* Sequoia wallpaper: tall redwood tree silhouettes */
        if (eff_wallpaper == 4) {
            /* Sun glow in upper-left area */
            int sun_x = VGA_WIDTH/5, sun_y = DESKTOP_Y + DESKTOP_H/4;
            int gi;
            for (gi = 30; gi > 0; gi--) {
                uint8_t galpha = (uint8_t)(gi * 4);
                vga_fill_rect_alpha(sun_x-gi*3, sun_y-gi*2, gi*6, gi*4, RGB(255,220,80), galpha);
            }
            gui_draw_circle(sun_x, sun_y, 18, RGB(255,240,100));
            gui_draw_circle(sun_x, sun_y, 12, RGB(255,255,200));
            /* Ground mist/fog strip */
            vga_fill_rect_alpha(0, DESKTOP_Y+DESKTOP_H-45, VGA_WIDTH, 30, RGB(220,180,120), 100);
            vga_fill_rect_alpha(0, DESKTOP_Y+DESKTOP_H-30, VGA_WIDTH, 20, RGB(240,200,140), 80);
            /* Redwood trees - tall narrow silhouettes */
            static const struct { int x; int h; int w; } trees[] = {
                {50, 200, 12}, {120, 180, 10}, {190, 220, 14}, {270, 190, 11},
                {340, 210, 13}, {420, 175, 9},  {490, 230, 15}, {560, 195, 11},
                {630, 215, 12}, {700, 185, 10}, {755, 205, 13}
            };
            int ti2;
            for (ti2=0; ti2<11; ti2++) {
                int tx2 = trees[ti2].x, th2 = trees[ti2].h, tw2 = trees[ti2].w;
                int ty2 = DESKTOP_Y + DESKTOP_H - th2 - 12;
                /* Trunk: narrow dark brown */
                uint32_t trunk_c = RGB(30+ti2*2, 15, 5);
                vga_fill_rect(tx2-tw2/6, ty2+th2-30, tw2/3, 30, trunk_c);
                /* Canopy: triangular evergreen shape */
                int ci2;
                for (ci2 = 0; ci2 < th2-30; ci2++) {
                    int seg_w = tw2 * ci2 / (th2-30);
                    if (seg_w < 1) seg_w = 1;
                    uint32_t c_dark = RGB(10+ci2*30/(th2-30), 20+ci2*10/(th2-30), 5);
                    vga_fill_rect(tx2-seg_w/2, ty2+ci2, seg_w, 1, c_dark);
                }
                /* Light edge: golden light hits the right side of tree */
                vga_fill_rect_alpha(tx2+tw2/3, ty2+th2/4, 2, (th2*3)/4, RGB(255,200,80), 60);
            }
        }
    }

    /* Spaces indicator strip above dock */
    {
        int si2, sx0, sy0 = DOCK_Y - 14;
        int spaces = g_num_spaces;
        if (spaces < 1) spaces = 1;
        if (spaces > 4) spaces = 4;
        sx0 = VGA_WIDTH/2 - spaces*18;
        for (si2 = 1; si2 <= spaces; si2++) {
            int sx2 = sx0 + (si2-1)*36;
            if (si2 == g_current_space) {
                vga_fill_rect_alpha(sx2, sy0, 30, 10, RGB(255,255,255), 220);
                vga_draw_rect_outline(sx2, sy0, 30, 10, RGB(200,200,200));
            } else {
                vga_fill_rect_alpha(sx2, sy0, 30, 10, RGB(150,150,170), 160);
                vga_draw_rect_outline(sx2, sy0, 30, 10, RGB(120,120,140));
            }
            /* Space number */
            char snum[2] = {'0'+(char)si2, 0};
            vga_draw_string_trans(sx2+11, sy0+1, snum,
                si2==g_current_space ? RGB(30,30,30) : RGB(200,200,210));
        }
    }

    /* Focus / DND ribbon: 2px colored strip below menubar */
    if (g_pref_dnd) {
        uint32_t focus_colors[]={RGB(100,60,200), RGB(52,199,89), RGB(0,122,255)};
        uint32_t fc = focus_colors[g_pref_dnd % 3];
        vga_fill_rect(0, MENUBAR_H, VGA_WIDTH, 3, fc);
        vga_fill_rect_alpha(0, MENUBAR_H+3, VGA_WIDTH, 8, fc, 60);
    }

    /* Desktop Widgets (left column, macOS Sonoma style) */
    if (g_widgets_visible) {
        int wgx = 6, wgy = DESKTOP_Y + 8;
        uint32_t wg_fg  = RGB(255,255,255);
        uint32_t wg_sub = RGB(190,195,210);

        /* Clock Widget */
        vga_fill_rect_alpha(wgx, wgy, 164, 72, RGB(0,0,15), 130);
        vga_fill_rect_alpha(wgx+1, wgy+1, 162, 70, RGB(255,255,255), 18);
        vga_draw_rect_outline(wgx, wgy, 164, 72, RGB(255,255,255));
        /* Dot decoration */
        gui_draw_circle(wgx+8, wgy+10, 4, RGB(255,59,48));
        gui_draw_circle(wgx+20, wgy+10, 4, RGB(255,149,0));
        gui_draw_circle(wgx+32, wgy+10, 4, RGB(52,199,89));
        /* Day of week */
        { char wday[16];
          get_weekday_upper_str(wday);
          vga_draw_string_trans(wgx+44, wgy+5, wday, wg_sub);
        }
        /* Big time display */
        { char hbuf[12];
          get_clock_str(hbuf);
          hbuf[5] = 0;
          /* Draw bold time by double-rendering */
          vga_draw_string_trans(wgx+8, wgy+22, hbuf, wg_fg);
          vga_draw_string_trans(wgx+9, wgy+22, hbuf, wg_fg);
          vga_draw_string_trans(wgx+8, wgy+23, hbuf, RGB(220,230,255));
        }
        { char datebuf[20];
          get_date_short_str(datebuf);
          vga_draw_string_trans(wgx+8, wgy+52, datebuf, wg_sub);
        }

        /* Calendar Widget */
        wgy += 78;
        vga_fill_rect_alpha(wgx, wgy, 164, 148, RGB(0,0,15), 130);
        vga_fill_rect_alpha(wgx+1, wgy+1, 162, 146, RGB(255,255,255), 18);
        vga_draw_rect_outline(wgx, wgy, 164, 148, RGB(255,255,255));
        /* Month header */
        vga_fill_rect_alpha(wgx, wgy, 164, 20, RGB(0,122,255), 200);
        { int cal_year, cal_month;
          char monthbuf[32];
          gui_calendar_month_from_offset(0, &cal_year, &cal_month);
          get_month_year_str(cal_year, cal_month, monthbuf);
          vga_draw_string_trans(wgx+8, wgy+5, monthbuf, wg_fg);
        }
        /* Day-of-week headers */
        { static const char *dh2[]={"Su","Mo","Tu","We","Th","Fr","Sa"};
          int dhi;
          for (dhi=0;dhi<7;dhi++)
              vga_draw_string_trans(wgx+4+dhi*23, wgy+22, dh2[dhi],
                  dhi==0?RGB(255,90,90):wg_sub);
        }
        /* Calendar days for the current month */
        { int cal_year, cal_month, dim, start_col, today_day;
          int day;
          gui_calendar_month_from_offset(0, &cal_year, &cal_month);
          dim = datetime_days_in_month(cal_year, cal_month);
          start_col = datetime_day_of_week(cal_year, cal_month, 1);
          today_day = gui_calendar_today_day_for_month(cal_year, cal_month);
          for (day = 1; day <= dim; day++) {
              int slot = start_col + day - 1;
              int ri2 = slot / 7;
              int ci2 = slot % 7;
              int cdx = wgx + 4 + ci2 * 23;
              int cdy = wgy + 34 + ri2 * 20;
              uint32_t dcol = ci2==0 ? RGB(255,90,90) : wg_fg;
              char ds2[4];
              if (ri2 >= 6) continue;
              if (day == today_day) {
                  gui_draw_circle(cdx+8, cdy+6, 8, RGB(0,122,255));
                  dcol = wg_fg;
              }
              int_to_str(day, ds2);
              vga_draw_string_trans(cdx+(day<10?5:2), cdy+2, ds2, dcol);
          }
        }

        /* Weather Widget */
        wgy += 154;
        { runtime_weather_info_t weather;
          char tempbuf[8], hlbuf[16];
          runtime_get_weather_info(&weather);
          runtime_format_temperature_c(weather.temperature_c, tempbuf, sizeof(tempbuf));
          runtime_format_high_low(weather.high_c, weather.low_c, hlbuf, sizeof(hlbuf));
          vga_fill_rect_alpha(wgx, wgy, 164, 72, RGB(0,60,120), 160);
          vga_fill_rect_alpha(wgx+1, wgy+1, 162, 70, RGB(255,255,255), 18);
          vga_draw_rect_outline(wgx, wgy, 164, 72, RGB(255,255,255));
          vga_draw_string_trans(wgx+8, wgy+6, weather.location_full, wg_sub);
          gui_draw_circle(wgx+20, wgy+40, 12, RGB(255,200,0));
          gui_draw_circle_outline(wgx+20, wgy+40, 12, RGB(255,220,60));
          vga_draw_string_trans(wgx+40, wgy+26, tempbuf, wg_fg);
          vga_draw_string_trans(wgx+40, wgy+42, weather.condition, wg_sub);
          vga_draw_string_trans(wgx+40, wgy+56, hlbuf, wg_sub);
        }

        /* Battery Widget */
        wgy += 78;
        { runtime_power_info_t power;
          char bpct2[5];
          int bw_pct;
          runtime_get_power_info(&power);
          bw_pct = power.percent;
          uint32_t bw_col=bw_pct<20?RGB(255,59,48):(bw_pct<50?RGB(255,149,0):RGB(52,199,89));
          vga_fill_rect_alpha(wgx, wgy, 164, 60, RGB(0,0,15), 130);
          vga_fill_rect_alpha(wgx+1, wgy+1, 162, 58, RGB(255,255,255), 18);
          vga_draw_rect_outline(wgx, wgy, 164, 60, RGB(255,255,255));
          vga_draw_string_trans(wgx+8, wgy+5, "Battery", wg_sub);
          /* Battery bar */
          vga_fill_rect(wgx+8, wgy+22, 140, 14, RGB(30,30,40));
          vga_fill_rect(wgx+8, wgy+22, 140*bw_pct/100, 14, bw_col);
          vga_draw_rect_outline(wgx+8, wgy+22, 140, 14, RGB(180,180,200));
          vga_fill_rect(wgx+148, wgy+26, 4, 6, RGB(180,180,200)); /* battery nub */
          /* Percentage */
          runtime_format_percent(bw_pct, bpct2, sizeof(bpct2));
          vga_draw_string_trans(wgx+8, wgy+42, bpct2, bw_col);
          vga_draw_string_trans(wgx+40, wgy+42, power.status, wg_sub);
        }
    }

    /* Desktop icons stacked in right column */
    /* Note: icons are at right edge (x=740+) to avoid overlapping startup windows */
    int dx = VGA_WIDTH - 58, dy = DESKTOP_Y + 10;
    draw_desktop_icon(dx, dy,       "MyOS",   RGB(0, 122, 255),   0); /* HDD */
    draw_desktop_icon(dx, dy +  58, "Notes",  RGB(255, 200,   0), 1); /* Notes pad */
    draw_desktop_icon(dx, dy + 116, "Trash",  RGB(160, 160, 160), 2); /* Trash can */
    draw_desktop_icon(dx, dy + 174, "Downlds",RGB(0,  180,  220), 3); /* Downloads */
    draw_desktop_icon(dx, dy + 232, "Docs",   RGB(100, 100, 240), 4); /* Documents */
}

/* =========================================================================
 * RTC-backed clock/date helpers
 * ======================================================================= */
static datetime_t s_cached_dt;
static uint32_t s_cached_dt_second = 0;
static int s_cached_dt_valid = 0;

void get_current_datetime(datetime_t *dt) {
    uint32_t sec = timer_ticks() / 1000;
    if (!s_cached_dt_valid || s_cached_dt_second != sec) {
        datetime_now(&s_cached_dt);
        s_cached_dt_second = sec;
        s_cached_dt_valid = 1;
    }
    *dt = s_cached_dt;
}

static void append_char(char *buf, int *pos, char ch) {
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = 0;
}

static void append_str(char *buf, int *pos, const char *s) {
    int i;
    if (!s) return;
    for (i = 0; s[i]; i++)
        append_char(buf, pos, s[i]);
}

static void append_int(char *buf, int *pos, int value) {
    char tmp[16];
    int i;
    int_to_str(value, tmp);
    for (i = 0; tmp[i]; i++)
        append_char(buf, pos, tmp[i]);
}

static void append_2digit(char *buf, int *pos, int value) {
    if (value < 0) value = 0;
    value %= 100;
    append_char(buf, pos, (char)('0' + value / 10));
    append_char(buf, pos, (char)('0' + value % 10));
}

static char ascii_upper(char ch) {
    if (ch >= 'a' && ch <= 'z')
        return (char)(ch - ('a' - 'A'));
    return ch;
}

void get_clock_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_2digit(buf, &pos, dt.hour);
    append_char(buf, &pos, ':');
    append_2digit(buf, &pos, dt.minute);
    append_char(buf, &pos, ':');
    append_2digit(buf, &pos, dt.second);
}

/* 12-hour AM/PM format for the menubar. */
static void get_clock_str_12h(char *buf) {
    datetime_t dt;
    const char *ampm;
    int h12;
    int pos = 0;
    get_current_datetime(&dt);
    ampm = (dt.hour < 12) ? "AM" : "PM";
    h12 = dt.hour % 12;
    if (h12 == 0) h12 = 12;
    append_int(buf, &pos, h12);
    append_char(buf, &pos, ':');
    append_2digit(buf, &pos, dt.minute);
    append_char(buf, &pos, ' ');
    append_str(buf, &pos, ampm);
}

void get_date_long_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_str(buf, &pos, datetime_weekday_long(dt.weekday));
    append_str(buf, &pos, ", ");
    append_str(buf, &pos, datetime_month_long(dt.month));
    append_char(buf, &pos, ' ');
    append_int(buf, &pos, dt.day);
    append_str(buf, &pos, ", ");
    append_int(buf, &pos, dt.year);
}

void get_date_short_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_str(buf, &pos, datetime_month_short(dt.month));
    append_char(buf, &pos, ' ');
    append_int(buf, &pos, dt.day);
    append_str(buf, &pos, ", ");
    append_int(buf, &pos, dt.year);
}

void get_month_day_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_str(buf, &pos, datetime_month_short(dt.month));
    append_char(buf, &pos, ' ');
    append_int(buf, &pos, dt.day);
}

void get_file_modified_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_str(buf, &pos, datetime_month_short(dt.month));
    append_char(buf, &pos, ' ');
    append_int(buf, &pos, dt.day);
    append_str(buf, &pos, "  ");
    append_2digit(buf, &pos, dt.hour);
    append_char(buf, &pos, ':');
    append_2digit(buf, &pos, dt.minute);
}

void get_menu_date_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_str(buf, &pos, datetime_weekday_short(dt.weekday));
    append_char(buf, &pos, ' ');
    append_str(buf, &pos, datetime_month_short(dt.month));
    append_char(buf, &pos, ' ');
    append_int(buf, &pos, dt.day);
}

void get_weekday_upper_str(char *buf) {
    datetime_t dt;
    const char *weekday;
    int i;
    get_current_datetime(&dt);
    weekday = datetime_weekday_long(dt.weekday);
    for (i = 0; weekday[i]; i++)
        buf[i] = ascii_upper(weekday[i]);
    buf[i] = 0;
}

void get_month_year_str(int year, int month, char *buf) {
    int pos = 0;
    append_str(buf, &pos, datetime_month_long(month));
    append_char(buf, &pos, ' ');
    append_int(buf, &pos, year);
}

void get_current_month_year_str(char *buf) {
    datetime_t dt;
    get_current_datetime(&dt);
    get_month_year_str(dt.year, dt.month, buf);
}

void get_week_of_year_str(char *buf) {
    datetime_t dt;
    int pos = 0;
    get_current_datetime(&dt);
    append_str(buf, &pos, "Week ");
    append_int(buf, &pos, datetime_week_of_year(dt.year, dt.month, dt.day));
    append_str(buf, &pos, " of ");
    append_int(buf, &pos, dt.year);
}

void gui_calendar_month_from_offset(int offset, int *year, int *month) {
    datetime_t dt;
    int y;
    int m;
    get_current_datetime(&dt);
    y = dt.year;
    m = dt.month + offset;
    while (m > 12) { m -= 12; y++; }
    while (m < 1)  { m += 12; y--; }
    if (year) *year = y;
    if (month) *month = m;
}

int gui_calendar_today_day_for_month(int year, int month) {
    datetime_t dt;
    get_current_datetime(&dt);
    if (dt.year == year && dt.month == month)
        return dt.day;
    return 0;
}

/* =========================================================================
 * Per-app dynamic menu bar
 * Updates g_menubar_titles[] and g_menu_items[][] based on focused window.
 * ======================================================================= */
void menubar_update_for_active_app(void) {
    const char *active = NULL;
    int top = win_top_visible();
    if (top >= 0 && g_windows[top].title)
        active = g_windows[top].title;

    /* Reset to default (Finder/generic) */
    g_menubar_titles[0]="MyOS"; g_menubar_titles[1]="File";
    g_menubar_titles[2]="Edit"; g_menubar_titles[3]="View";
    g_menubar_titles[4]="Window"; g_menubar_titles[5]="Help";
    g_menu_items[1][0]="New Window"; g_menu_items[1][1]="Close Window";
    g_menu_items[1][2]="---";        g_menu_items[1][3]="Get Info";
    g_menu_items[1][4]="---";        g_menu_items[1][5]="Print...";
    g_menu_items[1][6]="Share...";   g_menu_items[1][7]=NULL;
    g_menu_items[2][0]="Undo"; g_menu_items[2][1]="---";
    g_menu_items[2][2]="Cut";  g_menu_items[2][3]="Copy";
    g_menu_items[2][4]="Paste";g_menu_items[2][5]=NULL;
    g_menu_items[3][0]="Finder"; g_menu_items[3][1]="Terminal";
    g_menu_items[3][2]="Calculator"; g_menu_items[3][3]="Clock";
    g_menu_items[3][4]="TextEdit"; g_menu_items[3][5]=NULL;
    g_menu_items[4][0]="Bring All to Front"; g_menu_items[4][1]=NULL;
    g_menu_items[5][0]="Quick Help"; g_menu_items[5][1]="---";
    g_menu_items[5][2]="About MyOS"; g_menu_items[5][3]=NULL;

    if (!active || str_eq(active,"MyOS Finder")) return;

    if (str_eq(active,"Terminal")) {
        g_menubar_titles[3]="Shell";
        g_menu_items[1][0]="New Window"; g_menu_items[1][1]="---";
        g_menu_items[1][2]="Close"; g_menu_items[1][3]=NULL;
        g_menu_items[3][0]="New Tab";   g_menu_items[3][1]="---";
        g_menu_items[3][2]="Clear";     g_menu_items[3][3]="---";
        g_menu_items[3][4]="Close Tab"; g_menu_items[3][5]=NULL;
    } else if (str_eq(active,"TextEdit")) {
        g_menubar_titles[3]="Format";
        g_menu_items[1][0]="New"; g_menu_items[1][1]="---";
        g_menu_items[1][2]="Save"; g_menu_items[1][3]="---";
        g_menu_items[1][4]="Close"; g_menu_items[1][5]=NULL;
        g_menu_items[2][0]="Undo"; g_menu_items[2][1]="Redo";
        g_menu_items[2][2]="---";  g_menu_items[2][3]="Cut";
        g_menu_items[2][4]="Copy"; g_menu_items[2][5]="Paste";
        g_menu_items[2][6]="Select All"; g_menu_items[2][7]=NULL;
        g_menu_items[3][0]="Bold";  g_menu_items[3][1]="Italic";
        g_menu_items[3][2]="---";   g_menu_items[3][3]="Font Size";
        g_menu_items[3][4]="---";   g_menu_items[3][5]="Plain Text";
        g_menu_items[3][6]=NULL;
    } else if (str_eq(active,"Notes")) {
        g_menubar_titles[1]="Note";
        g_menubar_titles[3]="Format";
        g_menu_items[1][0]="New Note"; g_menu_items[1][1]="---";
        g_menu_items[1][2]="Delete Note"; g_menu_items[1][3]="---";
        g_menu_items[1][4]="Close"; g_menu_items[1][5]=NULL;
        g_menu_items[3][0]="Bold"; g_menu_items[3][1]="Italic";
        g_menu_items[3][2]="---";  g_menu_items[3][3]="Checklist";
        g_menu_items[3][4]="---";  g_menu_items[3][5]="Title";
        g_menu_items[3][6]=NULL;
    } else if (str_eq(active,"Music")) {
        g_menubar_titles[3]="Controls";
        g_menu_items[3][0]="Play";   g_menu_items[3][1]="Pause";
        g_menu_items[3][2]="---";    g_menu_items[3][3]="Next";
        g_menu_items[3][4]="Previous"; g_menu_items[3][5]="---";
        g_menu_items[3][6]="Shuffle"; g_menu_items[3][7]="Repeat";
    } else if (str_eq(active,"Safari")) {
        g_menubar_titles[4]="History";
        g_menu_items[1][0]="New Tab";    g_menu_items[1][1]="New Window";
        g_menu_items[1][2]="---";        g_menu_items[1][3]="Close Tab";
        g_menu_items[1][4]=NULL;
        g_menu_items[3][0]="Reload Page"; g_menu_items[3][1]="Stop";
        g_menu_items[3][2]="---";         g_menu_items[3][3]="Zoom In";
        g_menu_items[3][4]="Zoom Out";    g_menu_items[3][5]=NULL;
        g_menu_items[4][0]="Back";  g_menu_items[4][1]="Forward";
        g_menu_items[4][2]="---";   g_menu_items[4][3]="Home";
        g_menu_items[4][4]="---";   g_menu_items[4][5]="Clear History";
        g_menu_items[4][6]=NULL;
    } else if (str_eq(active,"Photos")) {
        g_menubar_titles[3]="Image";
        g_menu_items[3][0]="Rotate Left";  g_menu_items[3][1]="Rotate Right";
        g_menu_items[3][2]="---";          g_menu_items[3][3]="Crop";
        g_menu_items[3][4]="---";          g_menu_items[3][5]="Enhance";
        g_menu_items[3][6]=NULL;
    } else if (str_eq(active,"Maps")) {
        g_menubar_titles[3]="Go";
        g_menu_items[3][0]="Current Location"; g_menu_items[3][1]="---";
        g_menu_items[3][2]="Home"; g_menu_items[3][3]="Work";
        g_menu_items[3][4]="---"; g_menu_items[3][5]="Recents";
        g_menu_items[3][6]=NULL;
    } else if (str_eq(active,"Clock")) {
        g_menubar_titles[3]="Alarm";
        g_menu_items[3][0]="Add Alarm"; g_menu_items[3][1]="---";
        g_menu_items[3][2]="World Clock"; g_menu_items[3][3]="Timer";
        g_menu_items[3][4]="Stopwatch";  g_menu_items[3][5]=NULL;
    } else if (str_eq(active,"Calculator")) {
        g_menubar_titles[3]="Convert";
        g_menu_items[3][0]="Basic";      g_menu_items[3][1]="Scientific";
        g_menu_items[3][2]="---";        g_menu_items[3][3]="Currency";
        g_menu_items[3][4]="Units";      g_menu_items[3][5]=NULL;
    } else if (str_eq(active,"App Store")) {
        g_menubar_titles[3]="Store";
        g_menu_items[3][0]="Featured";  g_menu_items[3][1]="Top Charts";
        g_menu_items[3][2]="---";       g_menu_items[3][3]="Updates";
        g_menu_items[3][4]="Purchases"; g_menu_items[3][5]=NULL;
    } else if (str_eq(active,"Calendar")) {
        g_menubar_titles[3]="View";
        g_menu_items[1][0]="New Event";  g_menu_items[1][1]="---";
        g_menu_items[1][2]="Close";      g_menu_items[1][3]=NULL;
        g_menu_items[3][0]="Day";        g_menu_items[3][1]="Week";
        g_menu_items[3][2]="Month";      g_menu_items[3][3]="Year";
        g_menu_items[3][4]="---";        g_menu_items[3][5]="Go to Today";
        g_menu_items[3][6]=NULL;
    } else if (str_eq(active,"Mail")) {
        g_menubar_titles[3]="Mailbox";
        g_menu_items[1][0]="New Message"; g_menu_items[1][1]="---";
        g_menu_items[1][2]="Reply";       g_menu_items[1][3]="Forward";
        g_menu_items[1][4]="---";         g_menu_items[1][5]="Close";
        g_menu_items[1][6]=NULL;
        g_menu_items[3][0]="Get Mail";    g_menu_items[3][1]="---";
        g_menu_items[3][2]="Inbox";       g_menu_items[3][3]="Sent";
        g_menu_items[3][4]="---";         g_menu_items[3][5]="Archive";
        g_menu_items[3][6]=NULL;
    }
}

/* =========================================================================
 * Lock screen overlay
 * ======================================================================= */
void lock_screen_draw(void) {
    int y;
    /* Blurred wallpaper-like gradient (deep navy → midnight blue) */
    for (y = 0; y < VGA_HEIGHT; y++) {
        uint8_t rb = (uint8_t)(8  + y*15/VGA_HEIGHT);
        uint8_t gb = (uint8_t)(12 + y*25/VGA_HEIGHT);
        uint8_t bb = (uint8_t)(50 + y*70/VGA_HEIGHT);
        vga_draw_hline(0, y, VGA_WIDTH, RGB(rb,gb,bb));
    }
    /* Subtle star dots */
    {
        static const int sx[]={80,160,320,480,640,720,200,560,400,50,750,300};
        static const int sy[]={30,80,50,40,60,30,130,110,90,70,50,140};
        int si; for(si=0;si<12;si++) vga_put_pixel(sx[si],sy[si],RGB(200,210,230));
    }
    /* Large 2x-scale clock */
    {
        char clk[16];
        get_clock_str(clk);
        int clen = str_len(clk);
        int bx0 = VGA_WIDTH/2 - clen*8;
        int by0 = VGA_HEIGHT/2 - 120;
        int ci;
        for (ci=0; clk[ci]; ci++) {
            unsigned char ch = (unsigned char)clk[ci];
            int bx = bx0 + ci*16, row2, col2;
            for (row2=0; row2<8; row2++)
                for (col2=0; col2<8; col2++)
                    if (font8x8[ch][row2] & (1u<<col2))
                        vga_fill_rect(bx+col2*2, by0+row2*2, 2, 2, RGB(255,255,255));
        }
    }
    /* Date */
    {
        char ds[32];
        int dlen;
        get_date_long_str(ds);
        dlen = str_len(ds);
        vga_draw_string_trans(VGA_WIDTH/2 - dlen*4, VGA_HEIGHT/2 - 74, ds, RGB(200,215,240));
    }
    /* Face ID / Touch ID ring animation */
    {
        uint32_t t_lock = timer_ticks();
        int ax = VGA_WIDTH/2, ay = VGA_HEIGHT/2 - 8;
        /* Pulsing outer ring */
        int phase = (int)((t_lock / 400) % 3);
        uint32_t ring_col = g_lock_pw_len > 0 ? RGB(52,199,89) : RGB(0,150,255);
        /* Multiple concentric rings with fade */
        gui_draw_circle_outline(ax, ay, 44 + phase*4, ring_col);
        vga_fill_rect_alpha(ax-(44+phase*4), ay-(44+phase*4),
                            (44+phase*4)*2, (44+phase*4)*2, ring_col, 15);
        gui_draw_circle_outline(ax, ay, 40, ring_col);
        /* Avatar circle (frosted glass) */
        gui_draw_circle(ax, ay, 36, RGB(40,60,120));
        vga_fill_rect_alpha(ax-36, ay-36, 72, 72, RGB(255,255,255), 25);
        /* Face (stylized person icon) */
        gui_draw_circle(ax, ay-12, 13, RGB(210,175,155));
        vga_fill_rect(ax-12, ay+2, 24, 16, RGB(210,175,155));
        /* User initials or icon overlay */
        vga_draw_string_trans(ax-4, ay+42, "PC", RGB(150,190,240));
        /* Face ID corner brackets when g_lock_pw_len == 0 */
        if (g_lock_pw_len == 0) {
            int br=48, bs=10;
            /* Top-left */
            vga_draw_hline(ax-br, ay-br, bs, ring_col);
            vga_draw_vline(ax-br, ay-br, bs, ring_col);
            /* Top-right */
            vga_draw_hline(ax+br-bs, ay-br, bs, ring_col);
            vga_draw_vline(ax+br-1, ay-br, bs, ring_col);
            /* Bottom-left */
            vga_draw_hline(ax-br, ay+br-1, bs, ring_col);
            vga_draw_vline(ax-br, ay+br-bs, bs, ring_col);
            /* Bottom-right */
            vga_draw_hline(ax+br-bs, ay+br-1, bs, ring_col);
            vga_draw_vline(ax+br-1, ay+br-bs, bs, ring_col);
        }
        /* Password field */
        int pw_w = 140, pw_h = 28, pw_x = ax-pw_w/2, pw_y = ay+58;
        vga_fill_rect_alpha(pw_x, pw_y, pw_w, pw_h, RGB(255,255,255), 35);
        gui_draw_rounded_rect_outline(pw_x, pw_y, pw_w, pw_h, 6,
            g_lock_pw_len > 0 ? RGB(52,199,89) : RGB(100,140,220));
        /* Password dots */
        int di2;
        for (di2=0; di2<8; di2++) {
            int dot_x = pw_x + 10 + di2*15;
            int dot_y = pw_y + pw_h/2;
            if (di2 < g_lock_pw_len)
                gui_draw_circle(dot_x, dot_y, 5, RGB(255,255,255));
            else
                gui_draw_circle_outline(dot_x, dot_y, 4, RGB(160,190,230));
        }
        /* Hint below */
        const char *hint = g_lock_pw_len > 0 ? "Press Enter to unlock" : "Face ID  |  Enter Password";
        int hlen = str_len(hint);
        vga_draw_string_trans(ax - hlen*4, pw_y + 36, hint, RGB(140,165,205));
    }
    /* Lock screen notification cards */
    {
        static const char *ls_apps[]={"Messages","Calendar","Mail"};
        static const char *ls_msgs[]={"Jane Kim: Are you free for lunch?","Team Standup - starting soon","GitHub: PR was approved"};
        static const uint32_t ls_age_s[]={120,3600,900};
        static const uint32_t ls_cols[]={RGB(52,199,89),RGB(255,59,48),RGB(0,140,255)};
        int notif_y = VGA_HEIGHT/2 + 120;
        int ni2, total_n = 3 + (g_nc_count > 0 ? (g_nc_count < 2 ? g_nc_count : 2) : 0);
        if (total_n > 4) total_n = 4;
        for (ni2=0; ni2<3 && notif_y+46 < VGA_HEIGHT-10; ni2++) {
            char agebuf[16];
            runtime_format_relative_time(ls_age_s[ni2], agebuf, sizeof(agebuf));
            vga_fill_rect_alpha(VGA_WIDTH/2-160, notif_y, 320, 42, RGB(255,255,255), 40);
            gui_draw_rounded_rect_outline(VGA_WIDTH/2-160, notif_y, 320, 42, 8, RGB(200,210,230));
            /* App icon */
            gui_draw_rounded_rect(VGA_WIDTH/2-154, notif_y+4, 18, 18, 4, ls_cols[ni2]);
            vga_draw_char_trans(VGA_WIDTH/2-150, notif_y+10, ls_apps[ni2][0], RGB(255,255,255));
            vga_draw_string_trans(VGA_WIDTH/2-132, notif_y+5, ls_apps[ni2], RGB(200,215,245));
            vga_draw_string_trans(VGA_WIDTH/2+80, notif_y+5, agebuf, RGB(150,170,210));
            vga_draw_string_trans(VGA_WIDTH/2-154, notif_y+26, ls_msgs[ni2], RGB(230,235,250));
            notif_y += 48;
        }
        /* Extra notifications from NC queue */
        if (g_nc_count > 0) {
            int ni3;
            for (ni3=0; ni3<g_nc_count && notif_y+46 < VGA_HEIGHT-10; ni3++) {
                vga_fill_rect_alpha(VGA_WIDTH/2-160, notif_y, 320, 42, RGB(255,255,255), 40);
                gui_draw_rounded_rect_outline(VGA_WIDTH/2-160, notif_y, 320, 42, 8, RGB(200,210,230));
                vga_fill_rect(VGA_WIDTH/2-156, notif_y+2, 4, 38, g_nc_colors[ni3]);
                vga_draw_string_trans(VGA_WIDTH/2-148, notif_y+8, g_nc_msgs[ni3], RGB(230,235,248));
                vga_draw_string_trans(VGA_WIDTH/2-148, notif_y+22, g_nc_subs[ni3], RGB(170,180,210));
                notif_y += 48;
            }
        }
    }
    /* Swipe up hint */
    vga_draw_string_trans(VGA_WIDTH/2-60, VGA_HEIGHT-18, "Press Enter to unlock", RGB(140,160,200));
}

/* =========================================================================
 * Menu bar
 * ======================================================================= */
static const char *music_current_song(void) {
    static const char *s[] = {"Midnight Drive","Neon Pulse","Starfall","Cyberwave","Solar Wind"};
    return s[g_music_track % 5];
}
const char *focus_mode_name(int mode) {
    static const char *names[] = {"","Personal","Work","Sleep","Gaming","Driving"};
    if (mode < 1 || mode > 5) return "Focus";
    return names[mode];
}
void gui_draw_menubar(void) {
    menubar_update_for_active_app();
    /* Base fill - dark mode aware */
    uint32_t mb_bg  = g_pref_darkmode ? RGB(28,28,30)   : RGB(240,240,240);
    uint32_t mb_top = g_pref_darkmode ? RGB(40,40,44)   : RGB(252,252,252);
    uint32_t mb_sep = g_pref_darkmode ? RGB(60,60,64)   : RGB(190,190,190);
    uint32_t mb_txt = g_pref_darkmode ? RGB(230,230,230) : RGB(30,30,30);
    /* Translucent frosted-glass menubar — wallpaper shows through */
    vga_fill_rect_alpha(0, MENUBAR_Y, VGA_WIDTH, MENUBAR_H, mb_bg, 195);

    /* Subtle top highlight (semi-transparent so wallpaper still bleeds) */
    vga_fill_rect_alpha(0, MENUBAR_Y, VGA_WIDTH, 2, mb_top, 90);

    /* Bottom separator line */
    vga_draw_hline(0, MENUBAR_Y + MENUBAR_H - 1, VGA_WIDTH, mb_sep);
    (void)mb_txt; /* used below */

    int ty = (MENUBAR_H - 8) / 2;   /* vertically centred text y */

    /* Active app name (after MyOS menu, like macOS shows app name bold) */
    const char *active_app = "Finder";
    {
        int top = win_top_visible();
        if (top >= 0 && g_windows[top].title) {
            const char *wt = g_windows[top].title;
            if (str_eq(wt, "MyOS Finder")) active_app = "Finder";
            else active_app = wt;
        }
    }

    /* Menu bar items with highlight for open menu */
    {
        int nx = 8, i;
        for (i = 0; i < N_MENUS; i++) {
            /* Item 0 is "MyOS" (our Apple menu); after it, insert active app name */
            const char *title = g_menubar_titles[i];
            int iw = str_len(title) * 8 + 12;
            if (g_menu_open == i) {
                vga_fill_rect(nx - 4, 0, iw + 4, MENUBAR_H, RGB(100, 100, 200));
                vga_draw_string_trans(nx, ty, title, COLOR_WHITE);
            } else {
                vga_draw_string_trans(nx, ty, title, mb_txt);
            }
            nx += iw + 6;
            /* After "MyOS", draw the active app name in bold-ish style */
            if (i == 0) {
                int aw = str_len(active_app) * 8 + 14;
                vga_draw_string_trans(nx, ty, active_app, mb_txt);
                vga_draw_string_trans(nx+1, ty, active_app, mb_txt); /* bold */
                nx += aw + 2;
            }
        }
    }

    /* Right side: battery + wifi arcs + date + clock */
    {
        int rx = VGA_WIDTH - 8;
        char clk[16];
        get_clock_str_12h(clk);
        int cw = str_len(clk) * 8;
        rx -= cw;
        vga_draw_string_trans(rx, ty, clk, mb_txt);
        rx -= 8;
        /* Date abbreviation */
        { char datebuf[16];
          int date_w;
          get_menu_date_str(datebuf);
          date_w = str_len(datebuf) * 8;
          vga_draw_string_trans(rx - date_w, ty, datebuf, mb_txt);
          rx -= date_w + 12;
        }
        /* WiFi arcs (3 arcs if connected) */
        if (g_pref_wifi) {
            int wfx = rx - 14, wfy = ty + 3;
            /* 3 arcs of increasing size */
            uint32_t wf_col = g_pref_darkmode ? RGB(220,220,220) : RGB(40,40,40);
            vga_put_pixel(wfx+6, wfy+10, wf_col);
            vga_draw_hline(wfx+3, wfy+7,  8, wf_col);
            vga_draw_hline(wfx+1, wfy+4,  12, wf_col);
            vga_draw_hline(wfx,   wfy+1,  14, wf_col);
            rx -= 18;
        } else {
            vga_draw_string_trans(rx - 24, ty, "--", RGB(150,150,150));
            rx -= 28;
        }
        /* Battery: icon + percentage */
        {
            runtime_power_info_t power;
            uint32_t batt_col = g_pref_darkmode ? RGB(200,200,200) : RGB(40,40,40);
            char bpct[5]; int i2;
            runtime_get_power_info(&power);
            runtime_format_percent(power.percent, bpct, sizeof(bpct));
            i2 = str_len(bpct);
            vga_draw_string_trans(rx - i2*8 - 26, ty, bpct, batt_col);
            rx -= i2*8 + 4;
            /* Battery icon */
            int bx = rx - 22, by = ty + 3;
            vga_draw_rect_outline(bx, by, 18, 10, batt_col);
            vga_fill_rect(bx+18, by+3, 2, 4, batt_col); /* nub */
            int fill = 16 * power.percent / 100;
            uint32_t fcol = power.percent > 20 ? RGB(52,199,89) : RGB(255,59,48);
            vga_fill_rect(bx+1, by+1, fill, 8, fcol);
            rx -= 24;
        }
        /* Privacy indicators: green dot (camera) / orange dot (mic/screen) */
        if (g_cam_in_use || g_mic_in_use || g_screen_shared) {
            rx -= 4;
            /* Green dot for camera */
            if (g_cam_in_use) {
                gui_draw_circle(rx-6, ty+8, 5, RGB(52,199,89));
                rx -= 14;
            }
            /* Orange dot for mic or screen sharing */
            if (g_mic_in_use || g_screen_shared) {
                gui_draw_circle(rx-6, ty+8, 5, RGB(255,149,0));
                rx -= 14;
            }
        }
        /* Focus / DND indicator with named mode */
        if (g_pref_dnd) {
            const char *fname = focus_mode_name(g_focus_mode);
            int fw = str_len(fname)*8 + 10;
            rx -= 4;
            gui_draw_rounded_rect(rx-fw, ty-1, fw, 16, 4, RGB(100,60,200));
            gui_draw_rounded_rect_outline(rx-fw, ty-1, fw, 16, 4, RGB(80,50,180));
            vga_draw_string_trans(rx-fw+5, ty+3, fname, RGB(255,255,255));
            rx -= fw + 4;
        }
        /* Night Shift indicator */
        if (g_night_shift) {
            rx -= 2;
            gui_draw_rounded_rect(rx-26, ty-1, 26, 16, 4, RGB(255,160,40));
            vga_draw_string_trans(rx-22, ty+3, "NS", RGB(255,255,255));
            rx -= 30;
        }
        /* Live Activity: music now playing pill */
        if (g_music_playing) {
            const char *song = music_current_song();
            int sl = str_len(song); if (sl > 8) sl = 8;
            int pw = sl*8 + 22;
            rx -= 4;
            gui_draw_rounded_rect(rx-pw, ty-1, pw, 16, 6, g_pref_darkmode?RGB(44,44,48):RGB(50,50,55));
            /* Music note */
            vga_draw_string_trans(rx-pw+4, ty+3, "~", RGB(255,60,68));
            /* Song name truncated */
            int si2;
            for (si2=0;si2<sl;si2++)
                vga_draw_char_trans(rx-pw+14+si2*8, ty+3, song[si2], RGB(220,220,225));
            rx -= pw + 4;
        }
        /* (iCloud sync and AirPlay icons suppressed to save menubar space) */
        /* Siri button — fixed-size colored circle with mic */
        {
            int siri_r = 6;
            int sr_x = rx - siri_r - 2;
            int sr_y = MENUBAR_H / 2;  /* keep within menubar */
            /* Siri gradient circle: blue-purple */
            gui_draw_circle(sr_x, sr_y, siri_r, RGB(80,80,220));
            gui_draw_circle(sr_x, sr_y, siri_r-2, RGB(100,100,240));
            /* Mic icon inside circle */
            vga_fill_rect(sr_x-1, sr_y-3, 3, 5, RGB(255,255,255));
            gui_draw_circle(sr_x, sr_y+2, 2, RGB(255,255,255));
            rx -= siri_r*2 + 4;
        }
    }
}

/* =========================================================================
 * Dock
 * ======================================================================= */


const dock_icon_t s_dock_icons[17] = {
    { "Finder",      RGB(41,  128, 185), 'F' },
    { "Terminal",    RGB(30,   30,  30), 'T' },
    { "TextEdit",    RGB(255, 180,  40), 'E' },
    { "Settings",    RGB(142, 142, 147), 'S' },
    { "Calculator",  RGB(200,  50,  50), 'C' },
    { "Clock",       RGB(80,   80, 240), 'K' },
    { "Notes",       RGB(255, 204,   0), 'N' },
    { "Music",       RGB(252,  60,  68), 'M' },
    { "Photos",      RGB(240,  80, 160), 'P' },
    { "Safari",      RGB(40,  160, 220), 'W' },
    { "Maps",        RGB(60,  200,  80), 'G' },
    { "App Store",   RGB(30,  120, 255), 'A' },
    { "Calendar",    RGB(255,  59,  48), 'L' },
    { "Mail",        RGB(0,  140, 255),  '@' },
    { "Passwords",   RGB(10,  190, 150), 'P' },
    { "FaceTime",    RGB(52,  199,  89), 'V' },
    { "Launchpad",   RGB(80,  140, 220), 'L' }
};

void gui_draw_dock(void) {
    int num = NUM_DOCK_ICONS;
    int total_w = num * DOCK_ICON_SIZE + (num - 1) * DOCK_ICON_PAD + 34; /* +18 separators + 16 padding */
    int dock_x  = (VGA_WIDTH - total_w) / 2;
    int dock_y  = DOCK_Y;
    int mx = mouse_get_x(), my = mouse_get_y();

    /* Translucent white pill background */
    vga_fill_rect_alpha(dock_x - 8, dock_y, total_w, DOCK_H, RGB(255, 255, 255), 128);
    vga_draw_rect_outline(dock_x - 8, dock_y, total_w, DOCK_H - 1, RGB(200, 200, 200));

    int i;
    int ix = dock_x;
    for (i = 0; i < num; i++) {
        /* Base icon position */
        int base_iy = dock_y + (DOCK_H - DOCK_ICON_SIZE) / 2 - 4;
        int icon_sz = DOCK_ICON_SIZE;
        int icon_r  = DOCK_ICON_R;

        /* Mouse distance to icon center */
        int ic_cx = ix + DOCK_ICON_SIZE / 2;
        int ic_cy = base_iy + DOCK_ICON_SIZE / 2;
        int ddx = mx - ic_cx, ddy = my - ic_cy;
        int dist2 = ddx*ddx + ddy*ddy;

        /* Magnify within a generous radius (80px) */
        int mag = 0; /* 0=normal, 1=near, 2=hover */
        if (my >= dock_y - 30) {
            if (dist2 < 36*36)       mag = 2;
            else if (dist2 < 60*60)  mag = 1;
        }

        if (mag == 2) { icon_sz = 50; icon_r = 9; }
        else if (mag == 1) { icon_sz = 42; icon_r = 7; }

        /* Anchor to bottom edge of base slot, grow upward */
        int iy = base_iy + DOCK_ICON_SIZE - icon_sz;

        /* Bounce animation: triangle-wave offset upward */
        if (g_dock_bounce[i] > 0) {
            int bp = g_dock_bounce[i];
            int phase = bp % 8;  /* 0-7 within each bounce cycle */
            int up = (phase <= 4) ? phase : (8 - phase);  /* 0-4-0 triangle */
            iy -= up * 4;        /* max 16px upward at peak */
            g_dock_bounce[i]--;
        }

        gui_draw_rounded_rect(ix, iy, icon_sz, icon_sz, icon_r, s_dock_icons[i].color);

        /* Subtle top shine gradient */
        {
            int si;
            for (si = 0; si < icon_sz/3; si++) {
                uint8_t alpha = (uint8_t)(60 - si * 60 / (icon_sz/3));
                vga_draw_hline(ix + icon_r/2, iy + si, icon_sz - icon_r, RGB(255,255,255));
                (void)alpha;
            }
            /* Use alpha blending for the shine - approximate with mid-color line */
            vga_fill_rect_alpha(ix+2, iy+2, icon_sz-4, icon_sz/3, RGB(255,255,255), 55);
        }

        /* Highlight ring for hovered icon */
        if (mag == 2) {
            gui_draw_rounded_rect_outline(ix, iy, icon_sz, icon_sz, icon_r, RGB(255,255,255));
        }

        /* Draw app-specific icon symbol */
        {
            int cx2 = ix + icon_sz/2, cy2 = iy + icon_sz/2;
            int s2 = icon_sz/3;
            if (i == 0) { /* Finder: folder shape */
                /* Folder body */
                vga_fill_rect(cx2-s2, cy2-s2/2, s2*2, s2+s2/2, RGB(255,255,255));
                /* Folder tab */
                vga_fill_rect(cx2-s2, cy2-s2/2-4, s2, 6, RGB(220,220,255));
            } else if (i == 1) { /* Terminal: >_ */
                vga_fill_rect(ix+4, iy+4, icon_sz-8, icon_sz-8, RGB(0,0,0));
                vga_draw_string_trans(ix+6, cy2-4, ">_", RGB(0,255,0));
            } else if (i == 2) { /* TextEdit: document */
                vga_fill_rect(cx2-s2, cy2-s2, s2*2, s2*2, RGB(255,255,255));
                int li;
                for (li=0;li<3;li++)
                    vga_draw_hline(cx2-s2+2, cy2-s2+4+li*5, s2*2-4, RGB(120,120,200));
            } else if (i == 3) { /* Settings: gear */
                gui_draw_circle(cx2, cy2, s2, RGB(255,255,255));
                gui_draw_circle(cx2, cy2, s2-3, s_dock_icons[i].color);
                gui_draw_circle_outline(cx2, cy2, s2, RGB(200,200,200));
            } else if (i == 4) { /* Calculator: grid */
                int ri, ci2;
                for (ri=0;ri<3;ri++) for (ci2=0;ci2<3;ci2++) {
                    int bx=cx2-s2+ci2*(s2*2/3), by2=cy2-s2/2+ri*(s2*2/3);
                    vga_fill_rect(bx+1,by2+1, s2*2/3-2, s2*2/3-2, RGB(255,255,255));
                }
            } else if (i == 5) { /* Clock: analog face */
                gui_draw_circle(cx2, cy2, s2, RGB(255,255,255));
                vga_draw_line(cx2, cy2, cx2, cy2-s2+2, RGB(50,50,50));  /* 12 hand */
                vga_draw_line(cx2, cy2, cx2+s2-3, cy2, RGB(100,100,100));  /* 3 hand */
            } else if (i == 6) { /* Notes: notepad */
                vga_fill_rect(cx2-s2, cy2-s2, s2*2, s2*2, RGB(255,240,100));
                int li;
                for (li=0;li<3;li++)
                    vga_draw_hline(cx2-s2+3, cy2-s2+6+li*6, s2*2-6, RGB(180,160,60));
            } else if (i == 7) { /* Music: note */
                vga_fill_rect(cx2, cy2-s2, 4, s2+2, RGB(255,255,255));
                vga_fill_rect(cx2, cy2-s2, s2, 4, RGB(255,255,255));
                gui_draw_circle(cx2-3, cy2+2, 5, RGB(255,255,255));
            } else if (i == 8) { /* Photos: mountain + sun */
                vga_fill_rect(ix+4, iy+4, icon_sz-8, icon_sz-8, RGB(80,180,240));
                /* Mountain triangle approximation */
                int mi;
                for (mi=0;mi<icon_sz/3;mi++)
                    vga_draw_hline(cx2-mi, cy2+mi/2, mi*2, RGB(100,200,80));
                gui_draw_circle(cx2+s2-2, cy2-s2+2, s2/2, RGB(255,220,0));
            } else if (i == 9) { /* Safari: compass/globe */
                gui_draw_circle(cx2, cy2, s2, RGB(255,255,255));
                vga_draw_line(cx2-s2, cy2, cx2+s2, cy2, RGB(100,150,220));
                vga_draw_line(cx2, cy2-s2, cx2, cy2+s2, RGB(100,150,220));
            } else if (i == 10) { /* Maps: green pin on map */
                vga_fill_rect(cx2-s2, cy2-s2/2, s2*2, s2+s2/2, RGB(210,235,170));
                vga_fill_rect(cx2-s2, cy2, s2*2, s2/2+1, RGB(150,195,230));
                vga_draw_hline(cx2-s2, cy2, s2*2, RGB(255,255,255));
                gui_draw_circle(cx2, cy2-4, s2/2+1, RGB(220,50,50));
                vga_fill_rect(cx2-1, cy2-4, 3, s2-2, RGB(220,50,50));
            } else if (i == 14) { /* Passwords: keyhole lock */
                /* Lock body */
                vga_fill_rect(cx2-s2+2, cy2-2, s2*2-4, s2+2, RGB(255,255,255));
                gui_draw_rounded_rect(cx2-s2+2, cy2-2, s2*2-4, s2+2, 3, RGB(255,255,255));
                /* Lock shackle (arch) */
                gui_draw_circle_outline(cx2, cy2-2, s2-3, RGB(255,255,255));
                vga_fill_rect(cx2-s2+3, cy2-2, 4, s2/2, s_dock_icons[i].color); /* left leg */
                vga_fill_rect(cx2+s2-7, cy2-2, 4, s2/2, s_dock_icons[i].color); /* right leg */
                /* Keyhole */
                gui_draw_circle(cx2, cy2+s2/3, s2/4, s_dock_icons[i].color);
                vga_fill_rect(cx2-2, cy2+s2/3, 4, s2/3, s_dock_icons[i].color);
            } else if (i == 15) { /* FaceTime: camera */
                /* Camera body (rounded rect) */
                gui_draw_rounded_rect(cx2-s2, cy2-s2/2, s2*2-s2/3, s2, 3, RGB(255,255,255));
                /* Camera lens (circle) */
                gui_draw_circle(cx2-s2/4, cy2, s2/3, s_dock_icons[i].color);
                gui_draw_circle(cx2-s2/4, cy2, s2/5, RGB(100,255,100));
                /* Camera triangle (play/record indicator) */
                { int ti;
                  for (ti=0;ti<s2/2;ti++)
                      vga_draw_hline(cx2+s2/3+1, cy2-ti/2, ti/2+2, RGB(255,255,255));
                }
            } else if (i == 16) { /* Launchpad: 3x3 grid of colored dots */
                int gi, lp_sz=5, lp_pad=4;
                int lp_total=3*(lp_sz+lp_pad)-lp_pad;
                int lp_ox=cx2-lp_total/2, lp_oy=cy2-lp_total/2;
                static const uint32_t lp_colors[9] = {
                    RGB(255,59,48),  RGB(255,149,0), RGB(255,204,0),
                    RGB(52,199,89),  RGB(0,122,255), RGB(88,86,214),
                    RGB(175,82,222), RGB(255,45,85), RGB(100,210,255)
                };
                for (gi=0;gi<9;gi++) {
                    int gx=lp_ox+(gi%3)*(lp_sz+lp_pad);
                    int gy=lp_oy+(gi/3)*(lp_sz+lp_pad);
                    gui_draw_circle(gx+lp_sz/2, gy+lp_sz/2, lp_sz/2+1, lp_colors[gi]);
                }
            } else { /* fallback: letter icon */
                gui_draw_circle(cx2, cy2, s2, RGB(255,255,255));
                vga_draw_string_trans(cx2-4, cy2-4, "A", s_dock_icons[i].color);
            }
        }

        /* Running dot: check if this app has an open/minimized window */
        {
            int dot_color_found = 0;
            int j3;
            /* Launchpad uses g_lp_visible instead of a window */
            if (str_eq(s_dock_icons[i].name,"Launchpad")) {
                if (g_lp_visible)
                    vga_fill_rect(ix+icon_sz/2-2, dock_y+DOCK_H-5, 4, 4, RGB(60,60,60));
            } else {
                const char *dname = str_eq(s_dock_icons[i].name,"Finder") ? "MyOS Finder" : s_dock_icons[i].name;
                for (j3=0;j3<g_num_windows;j3++) {
                    if (g_windows[j3].title && str_eq(g_windows[j3].title, dname)) {
                        if (g_win_minimized[j3]) {
                            vga_fill_rect(ix+icon_sz/2-4, dock_y+DOCK_H-6, 8, 4, RGB(200,200,200));
                        } else if (g_windows[j3].visible) {
                            vga_fill_rect(ix+icon_sz/2-2, dock_y+DOCK_H-5, 4, 4, RGB(60,60,60));
                        }
                        dot_color_found = 1; break;
                    }
                }
            }
            (void)dot_color_found;
        }

        /* Notification badge */
        if (g_dock_badges[i] > 0) {
            int bx = ix + icon_sz - 8;
            int by3 = iy;
            gui_draw_circle(bx, by3, 8, RGB(255, 59, 48));
            gui_draw_circle_outline(bx, by3, 8, RGB(255,255,255));
            {
                char bn[3]; int bv = g_dock_badges[i];
                if (bv >= 10) { bn[0]='1'; bn[1]=(char)('0'+bv-10); bn[2]=0;
                    vga_draw_string_trans(bx-6, by3-4, bn, RGB(255,255,255));
                } else {
                    bn[0]=(char)('0'+bv); bn[1]=0;
                    vga_draw_string_trans(bx-3, by3-4, bn, RGB(255,255,255));
                }
            }
        }

        /* Tooltip label on hover */
        if (mag == 2) {
            const char *label = s_dock_icons[i].name;
            int llen = str_len(label);
            int lbx = ic_cx - llen * 4 - 4;
            int lby = iy - 20;
            vga_fill_rect(lbx, lby, llen * 8 + 8, 16, RGB(50, 50, 50));
            vga_draw_string_trans(lbx + 4, lby + 4, label, RGB(255, 255, 255));
        }

        ix += DOCK_ICON_SIZE + DOCK_ICON_PAD;

        /* Dock separator after icon 5, icon 11, icon 15 (before Launchpad) */
        if (i == 5 || i == 11 || i == 15) {
            int sep_x = ix - DOCK_ICON_PAD/2;
            int sep_y = dock_y + 8;
            int sep_h = DOCK_H - 16;
            vga_fill_rect(sep_x, sep_y, 1, sep_h, RGB(160,160,165));
            vga_fill_rect(sep_x+1, sep_y, 1, sep_h, RGB(220,220,225));
            ix += 6; /* extra space for separator */
        }
    }
}

/* =========================================================================
 * Window chrome
 * ======================================================================= */
void gui_draw_window(int idx) {
    if (idx < 0 || idx >= g_num_windows) return;
    const gui_window_t *win = &g_windows[idx];
    if (!win->visible) return;

    /* Window open animation: scale up from center */
    if (g_win_anim[idx] > 0) {
        int anim = g_win_anim[idx];
        int scale_n = OPEN_ANIM - anim + 1; /* 1..OPEN_ANIM */
        int cx = win->x + win->w / 2;
        int cy = win->y + win->h / 2;
        int sw = win->w * scale_n / OPEN_ANIM;
        int sh = win->h * scale_n / OPEN_ANIM;
        if (sw < 8) sw = 8;
        if (sh < 8) sh = 8;
        int sx = cx - sw / 2;
        int sy = cy - sh / 2;
        uint8_t alpha = (uint8_t)(200 * scale_n / OPEN_ANIM);
        vga_fill_rect_alpha(sx + 3, sy + 3, sw, sh, RGB(0,0,0), 60);
        vga_fill_rect_alpha(sx, sy, sw, sh, RGB(235, 235, 235), alpha);
        vga_fill_rect_alpha(sx, sy, sw, (sh > TITLEBAR_H ? TITLEBAR_H : sh), RGB(210,210,210), alpha);
        g_win_anim[idx]--;
        return;
    }

    /* Minimize (close) animation: scale down toward dock */
    if (g_win_close_anim[idx] > 0) {
        int anim = g_win_close_anim[idx];
        int cx3 = win->x + win->w / 2;
        int cy3 = win->y + win->h / 2;
        int sw3 = win->w * anim / OPEN_ANIM;
        int sh3 = win->h * anim / OPEN_ANIM;
        if (sw3 < 4) sw3 = 4;
        if (sh3 < 4) sh3 = 4;
        /* Shift toward dock bottom as it shrinks */
        int dock_target_y = DOCK_Y + DOCK_H / 2;
        int sy3 = cy3 + (dock_target_y - cy3) * (OPEN_ANIM - anim) / OPEN_ANIM - sh3 / 2;
        int sx3 = cx3 - sw3 / 2;
        uint8_t alpha3 = (uint8_t)(200 * anim / OPEN_ANIM);
        vga_fill_rect_alpha(sx3 + 3, sy3 + 3, sw3, sh3, RGB(0,0,0), 50);
        vga_fill_rect_alpha(sx3, sy3, sw3, sh3, RGB(235, 235, 235), alpha3);
        vga_fill_rect_alpha(sx3, sy3, sw3, (sh3 > TITLEBAR_H ? TITLEBAR_H : sh3), RGB(210,210,210), alpha3);
        g_win_close_anim[idx]--;
        if (g_win_close_anim[idx] == 0) {
            g_windows[idx].visible = 0;
            g_win_minimized[idx] = 1;
        }
        return;
    }

    int wx = win->x, wy = win->y, ww = win->w, wh = win->h;

    /* Drop shadow (three layers of decreasing alpha) */
    vga_fill_rect_alpha(wx + 5, wy + 5, ww, wh, RGB(0, 0, 0), 90);
    vga_fill_rect_alpha(wx + 3, wy + 3, ww, wh, RGB(0, 0, 0), 55);
    vga_fill_rect_alpha(wx + 2, wy + 2, ww, wh, RGB(0, 0, 0), 30);

    /* Client area */
    /* Active = topmost visible window. */
    int is_active = (idx == win_top_visible());

    vga_fill_rect(wx, wy + TITLEBAR_H, ww, wh - TITLEBAR_H,
                  g_pref_darkmode ? RGB(40,40,42) : RGB(248,248,248));

    /* Title bar gradient: active brighter, inactive muted; dark mode aware */
    {
        int ty;
        for (ty = 0; ty < TITLEBAR_H; ty++) {
            uint8_t v;
            if (g_pref_darkmode) {
                v = is_active ? (uint8_t)(60 - ty*10/TITLEBAR_H) : (uint8_t)(44 - ty*4/TITLEBAR_H);
            } else {
                v = is_active ? (uint8_t)(220 - ty*20/TITLEBAR_H) : (uint8_t)(185 - ty*10/TITLEBAR_H);
            }
            vga_draw_hline(wx, wy + ty, ww, g_pref_darkmode ? RGB(v,v,v+4) : RGB(v,v,v));
        }
        vga_draw_hline(wx, wy, ww, g_pref_darkmode ?
            (is_active ? RGB(70,70,74) : RGB(55,55,58)) :
            (is_active ? RGB(240,240,240) : RGB(200,200,200)));
    }

    /* Traffic-light buttons — grayed out on inactive windows */
    {
        int btn_cy = wy + TITLEBAR_H / 2;
        if (is_active) {
            gui_draw_circle(wx + 12, btn_cy, 6, COLOR_BTN_RED);
            gui_draw_circle(wx + 30, btn_cy, 6, COLOR_BTN_YELLOW);
            gui_draw_circle(wx + 48, btn_cy, 6, COLOR_BTN_GREEN);
            vga_put_pixel(wx + 12, btn_cy, RGB(200, 60,  50));
            vga_put_pixel(wx + 30, btn_cy, RGB(200, 140, 30));
            vga_put_pixel(wx + 48, btn_cy, RGB(30,  160, 50));
        } else {
            gui_draw_circle(wx + 12, btn_cy, 6, RGB(180,180,180));
            gui_draw_circle(wx + 30, btn_cy, 6, RGB(180,180,180));
            gui_draw_circle(wx + 48, btn_cy, 6, RGB(180,180,180));
        }
    }

    /* Centred window title */
    {
        int tlen = str_len(win->title);
        int tx = wx + (ww - tlen * 8) / 2;
        int ty = wy + (TITLEBAR_H - 8) / 2;
        uint32_t tc = is_active ? COLOR_TITLEBAR_TEXT : RGB(160,160,160);
        vga_draw_string_trans(tx, ty, win->title, tc);
    }

    /* Window border - blue outline for active window */
    if (is_active) {
        gui_draw_rounded_rect_outline(wx-1, wy-1, ww+2, wh+2, 9, RGB(0,122,255));
    }
    gui_draw_rounded_rect_outline(wx, wy, ww, wh, 8, RGB(160,160,160));

    /* Separator below title bar */
    vga_draw_hline(wx + 8, wy + TITLEBAR_H, ww - 16, COLOR_BORDER);

    /* Status bar strip at window bottom */
    {
        int sb_y = wy + wh - 18;
        vga_draw_hline(wx + 1, sb_y, ww - 2, COLOR_BORDER);
        vga_fill_rect(wx + 1, sb_y + 1, ww - 2, 16, RGB(232, 232, 232));
    }

    /* Resize grip — 3 diagonal lines in bottom-right corner */
    if (!win->maximized) {
        int gx = wx + ww - 2, gy = wy + wh - 2;
        int d;
        for (d = 2; d <= 10; d += 4) {
            vga_put_pixel(gx - d,     gy,         RGB(160,160,160));
            vga_put_pixel(gx - d + 1, gy,         RGB(220,220,220));
            vga_put_pixel(gx,         gy - d,     RGB(160,160,160));
            vga_put_pixel(gx,         gy - d + 1, RGB(220,220,220));
        }
    }

    /* Rounded corner cutouts — paint desktop gradient over square window corners */
    draw_window_corners(wx, wy, ww, wh, 8);
}

/* =========================================================================
 * Button
 * ======================================================================= */
void gui_draw_button(const gui_button_t *btn) {
    uint32_t bg;
    if (btn->pressed)
        bg = RGB(180, 180, 180);
    else if (btn->hover)
        bg = RGB(215, 230, 255);
    else
        bg = btn->color;

    gui_draw_rounded_rect(btn->x, btn->y, btn->w, btn->h, 4, bg);
    gui_draw_rounded_rect_outline(btn->x, btn->y, btn->w, btn->h, 4,
                                  btn->pressed ? RGB(130, 130, 130) : RGB(160, 160, 160));

    {
        int llen = str_len(btn->label);
        int lx = btn->x + (btn->w - llen * 8) / 2;
        int ly = btn->y + (btn->h - 8) / 2;
        if (btn->pressed) { lx++; ly++; }
        vga_draw_string_trans(lx, ly, btn->label, btn->text_color);
    }
}

int gui_button_hit(const gui_button_t *btn, int x, int y) {
    return x >= btn->x && x < btn->x + btn->w &&
           y >= btn->y && y < btn->y + btn->h;
}

/* =========================================================================
 * Folder icon grid drawn inside main window content area
 * ======================================================================= */

/* Finder navigation state */
int   g_finder_depth = 0;   /* 0 = Desktop root */
const char *g_finder_stack[FINDER_DEPTH_MAX];

/* Per-folder file lists */
static const folder_icon_t s_folders_desktop[4] = {
    { "Documents",    RGB(70,  130, 200) },
    { "Downloads",    RGB(50,  180,  80) },
    { "Applications", RGB(200,  80,  80) },
    { "Desktop",      RGB(200, 160,  30) }
};
static const folder_icon_t s_folders_documents[4] = {
    { "Resume.pdf",   RGB(220,  50,  50) },
    { "Notes.txt",    RGB(255, 204,   0) },
    { "Project/",     RGB(70,  130, 200) },
    { "Archive/",     RGB(142, 142, 147) }
};
static const folder_icon_t s_folders_downloads[4] = {
    { "MyOS.iso",     RGB(80,  80, 200) },
    { "photo.png",    RGB(240, 80, 160) },
    { "video.mp4",    RGB(200, 60,  60) },
    { "app.dmg",      RGB(60, 160, 220) }
};
static const folder_icon_t s_folders_apps[4] = {
    { "Terminal",     RGB(30,   30,  30) },
    { "TextEdit",     RGB(255, 140,  40) },
    { "Calculator",   RGB(200,  50,  50) },
    { "Settings",     RGB(142, 142, 147) }
};
static const folder_icon_t s_folders_desktop2[4] = {
    { "Screenshot",   RGB(60,  60, 200) },
    { "Notes.txt",    RGB(255, 204,   0) },
    { ".DS_Store",    RGB(180, 180, 180) },
    { "README",       RGB(100, 180, 100) }
};

const folder_icon_t *finder_current_folders(int *count) {
    *count = 4;
    if (g_finder_depth == 0) return s_folders_desktop;
    const char *top = g_finder_stack[g_finder_depth-1];
    if (str_eq(top,"Documents")) return s_folders_documents;
    if (str_eq(top,"Downloads")) return s_folders_downloads;
    if (str_eq(top,"Applications")) return s_folders_apps;
    if (str_eq(top,"Desktop")) return s_folders_desktop2;
    return s_folders_desktop;
}

static const folder_icon_t *s_folders __attribute__((unused)) = s_folders_desktop;

void draw_folder(int x, int y, int cell_w, const folder_icon_t *fi) {
    /* Tab */
    vga_fill_rect(x, y, 14, 6, fi->color);
    /* Body */
    vga_fill_rect(x, y + 4, 28, 22, fi->color);
    /* Sheen */
    vga_draw_hline(x + 1, y + 5, 26, RGB(255, 255, 255));
    /* Name below — centered on cell, not just the icon */
    int llen = str_len(fi->name);
    int lw   = llen * 8;
    int cell_left = x - (cell_w - 28) / 2;   /* left edge of the cell */
    int lx = cell_left + (cell_w - lw) / 2;
    if (lx < cell_left) lx = cell_left;
    vga_draw_string_trans(lx, y + 28, fi->name, COLOR_TEXT);
}

/* ---- Health ring drawing ---- */
void draw_ring_arc(int cx, int cy, int r_out, int r_in, int pct, uint32_t ring_col, uint32_t track_col) {
    int x0, y0;
    if (pct > 100) pct = 100;
    int target = pct * 36; /* 0-3600 = 0-360 degrees * 10 */
    for (y0=-r_out; y0<=r_out; y0++) {
        for (x0=-r_out; x0<=r_out; x0++) {
            int d2 = x0*x0+y0*y0;
            if (d2 < r_in*r_in || d2 > r_out*r_out) continue;
            int xr = x0, yr = -y0; /* yr positive upward */
            int ang;
            if (xr >= 0 && yr >= 0)      { ang = (xr+yr>0) ? 900*xr/(xr+yr) : 0; }
            else if (xr >= 0 && yr < 0)  { int ay=-yr; ang = 900 + (xr+ay>0 ? 900*ay/(xr+ay) : 0); }
            else if (xr < 0 && yr <= 0)  { int ax=-xr,ay=-yr; ang = 1800 + (ax+ay>0 ? 900*ax/(ax+ay) : 0); }
            else                          { int ax=-xr; ang = 2700 + (ax+yr>0 ? 900*yr/(ax+yr) : 0); }
            vga_put_pixel(cx+x0, cy+y0, ang<=target ? ring_col : track_col);
        }
    }
}

/* Sudoku state */
int g_sdk_puzzle[SDK_SZ][SDK_SZ] = {
    {5,3,0, 0,7,0, 0,0,0},
    {6,0,0, 1,9,5, 0,0,0},
    {0,9,8, 0,0,0, 0,6,0},
    {8,0,0, 0,6,0, 0,0,3},
    {4,0,0, 8,0,3, 0,0,1},
    {7,0,0, 0,2,0, 0,0,6},
    {0,6,0, 0,0,0, 2,8,0},
    {0,0,0, 4,1,9, 0,0,5},
    {0,0,0, 0,8,0, 0,7,9}
};
int g_sdk_board[SDK_SZ][SDK_SZ];
int g_sdk_given[SDK_SZ][SDK_SZ]; /* 1 = given (read-only) */
int g_sdk_sel_r = -1, g_sdk_sel_c = -1;
int g_sdk_started = 0;
int g_sdk_errors = 0;

/* ---- 2048 game logic ---- */
void g2048_spawn(void) {
    int empty[16], ne=0, r2, c2;
    for (r2=0;r2<4;r2++) for (c2=0;c2<4;c2++)
        if (!g_2048_board[r2][c2]) empty[ne++]=r2*4+c2;
    if (!ne) return;
    static int g2048_rng=42;
    g2048_rng = g2048_rng*1664525+1013904223;
    int pos = empty[(g2048_rng<0?-g2048_rng:g2048_rng)%ne];
    g2048_rng = g2048_rng*1664525+1013904223;
    g_2048_board[pos/4][pos%4] = (g2048_rng<0?-g2048_rng:g2048_rng)%4<3 ? 2 : 4;
}
static int g2048_slide_row(int row[4]) { /* returns 1 if changed */
    int tmp[4]={0}, ti=0, changed=0, i2;
    for (i2=0;i2<4;i2++) if (row[i2]) tmp[ti++]=row[i2];
    for (i2=0;i2<ti-1;i2++) {
        if (tmp[i2]==tmp[i2+1]) {
            tmp[i2]*=2; g_2048_score+=tmp[i2];
            if (g_2048_score>g_2048_best) g_2048_best=g_2048_score;
            if (tmp[i2]==2048 && g_2048_state==1) g_2048_state=2;
            tmp[i2+1]=0; i2++;
        }
    }
    int out[4]={0}; ti=0;
    for (i2=0;i2<4;i2++) if (tmp[i2]) out[ti++]=tmp[i2];
    for (i2=0;i2<4;i2++) { if (out[i2]!=row[i2]) changed=1; row[i2]=out[i2]; }
    return changed;
}
int g2048_move(int dir) { /* 0=left,1=right,2=up,3=down */
    int changed=0, r2, c2;
    if (dir==0) { for(r2=0;r2<4;r2++) changed|=g2048_slide_row(g_2048_board[r2]); }
    else if (dir==1) { /* right: reverse each row, slide, reverse back */
        for(r2=0;r2<4;r2++) {
            int rev[4]={g_2048_board[r2][3],g_2048_board[r2][2],g_2048_board[r2][1],g_2048_board[r2][0]};
            changed|=g2048_slide_row(rev);
            g_2048_board[r2][0]=rev[3];g_2048_board[r2][1]=rev[2];g_2048_board[r2][2]=rev[1];g_2048_board[r2][3]=rev[0];
        }
    } else if (dir==2) { /* up: transpose, slide left, transpose back */
        for(c2=0;c2<4;c2++) {
            int col[4]={g_2048_board[0][c2],g_2048_board[1][c2],g_2048_board[2][c2],g_2048_board[3][c2]};
            changed|=g2048_slide_row(col);
            for(r2=0;r2<4;r2++) g_2048_board[r2][c2]=col[r2];
        }
    } else { /* down: transpose reversed */
        for(c2=0;c2<4;c2++) {
            int col[4]={g_2048_board[3][c2],g_2048_board[2][c2],g_2048_board[1][c2],g_2048_board[0][c2]};
            changed|=g2048_slide_row(col);
            for(r2=0;r2<4;r2++) g_2048_board[3-r2][c2]=col[r2];
        }
    }
    if (changed) g2048_spawn();
    /* Check game over */
    if (g_2048_state==1) {
        int has_empty=0; int r3,c3;
        for(r3=0;r3<4;r3++) for(c3=0;c3<4;c3++) {
            if (!g_2048_board[r3][c3]) has_empty=1;
            if (r3<3 && g_2048_board[r3][c3]==g_2048_board[r3+1][c3]) has_empty=1;
            if (c3<3 && g_2048_board[r3][c3]==g_2048_board[r3][c3+1]) has_empty=1;
        }
        if (!has_empty) g_2048_state=3;
    }
    return changed;
}
void g2048_new_game(void) {
    int r2,c2; for(r2=0;r2<4;r2++) for(c2=0;c2<4;c2++) g_2048_board[r2][c2]=0;
    g_2048_score=0; g_2048_state=1;
    g2048_spawn(); g2048_spawn();
}
