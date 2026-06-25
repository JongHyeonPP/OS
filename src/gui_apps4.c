#include "gui_internal.h"

static void apps4_append_text(char *buf, int *pos, int max, const char *text) {
    int i = 0;
    if (!buf || max <= 0 || !text) return;
    while (text[i] && *pos + 1 < max) buf[(*pos)++] = text[i++];
    buf[*pos] = 0;
}

static void apps4_append_uint(char *buf, int *pos, int max, uint32_t value) {
    char tmp[10];
    int n = 0;
    if (!buf || max <= 0) return;
    if (value == 0) {
        if (*pos + 1 < max) buf[(*pos)++] = '0';
        buf[*pos] = 0;
        return;
    }
    while (value && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (n && *pos + 1 < max) buf[(*pos)++] = tmp[--n];
    buf[*pos] = 0;
}

static void apps4_format_uint_suffix(uint32_t value, const char *suffix, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0) return;
    buf[0] = 0;
    apps4_append_uint(buf, &pos, max, value);
    apps4_append_text(buf, &pos, max, suffix);
}

static void apps4_format_decimal_tenths(uint32_t tenths, const char *suffix, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0) return;
    buf[0] = 0;
    apps4_append_uint(buf, &pos, max, tenths / 10U);
    apps4_append_text(buf, &pos, max, ".");
    apps4_append_uint(buf, &pos, max, tenths % 10U);
    apps4_append_text(buf, &pos, max, suffix);
}

static void apps4_format_time_24h_from_minutes(int minutes, char *buf, int max) {
    int hour, minute;
    if (!buf || max <= 0) return;
    while (minutes < 0) minutes += 24 * 60;
    minutes %= 24 * 60;
    hour = minutes / 60;
    minute = minutes % 60;
    if (max < 6) {
        buf[0] = 0;
        return;
    }
    buf[0] = (char)('0' + (hour / 10));
    buf[1] = (char)('0' + (hour % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (minute / 10));
    buf[4] = (char)('0' + (minute % 10));
    buf[5] = 0;
}

static void apps4_format_hms(uint32_t total_seconds, char *buf, int max) {
    uint32_t hours, minutes, seconds;
    if (!buf || max <= 0) return;
    hours = (total_seconds / 3600U) % 24U;
    minutes = (total_seconds / 60U) % 60U;
    seconds = total_seconds % 60U;
    if (max < 9) {
        buf[0] = 0;
        return;
    }
    buf[0] = (char)('0' + (hours / 10U));
    buf[1] = (char)('0' + (hours % 10U));
    buf[2] = ':';
    buf[3] = (char)('0' + (minutes / 10U));
    buf[4] = (char)('0' + (minutes % 10U));
    buf[5] = ':';
    buf[6] = (char)('0' + (seconds / 10U));
    buf[7] = (char)('0' + (seconds % 10U));
    buf[8] = 0;
}

static void apps4_format_journal_date(int days_ago, char *buf, int max) {
    datetime_t now;
    int y, m, d, wd;
    char daybuf[4];
    int pos = 0;
    if (!buf || max <= 0) return;
    buf[0] = 0;
    get_current_datetime(&now);
    y = now.year;
    m = now.month;
    d = now.day - days_ago;
    while (d < 1) {
        m--;
        if (m < 1) {
            m = 12;
            y--;
        }
        d += datetime_days_in_month(y, m);
    }
    wd = (now.weekday - (days_ago % 7) + 7) % 7;
    int_to_str(d, daybuf);
    apps4_append_text(buf, &pos, max, datetime_weekday_short(wd));
    apps4_append_text(buf, &pos, max, " ");
    apps4_append_text(buf, &pos, max, datetime_month_short(m));
    apps4_append_text(buf, &pos, max, " ");
    apps4_append_text(buf, &pos, max, daybuf);
}

int draw_apps_group4(int idx) {
    if (idx < 0 || idx >= g_num_windows) return 0;

    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Motion")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t mo_bg  = RGB(24,24,28);
        uint32_t mo_tb  = RGB(34,34,40);
        uint32_t mo_txt = RGB(215,215,225);
        uint32_t mo_sub = RGB(130,130,140);
        uint32_t mo_acc = RGB(30,200,220);
        uint32_t mo_sep = RGB(48,48,56);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, mo_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 24, mo_tb);
        vga_draw_hline(wx+1, content_y+24, ww-2, mo_sep);
        { int tx7=wx+4;
          static const char *tools[]={"Select","Add","Behaviors","Filters","Generators","3D"};
          int ti7;
          for (ti7=0;ti7<6;ti7++){
              gui_draw_rounded_rect(tx7, content_y+4, str_len(tools[ti7])*8+8, 16, 3,
                  ti7==0?mo_acc:mo_tb);
              vga_draw_string_trans(tx7+4, content_y+8, tools[ti7],
                  ti7==0?RGB(10,10,14):mo_sub);
              tx7 += str_len(tools[ti7])*8+12;
          }
          { const char *play_label = g_motion_playing ? "|| Pause" : "|> Play";
            vga_draw_string_trans(wx+ww-72, content_y+8, play_label, g_motion_playing?mo_acc:mo_sub); }
        }
        /* Left panel: layers */
        int mo_lw=80;
        vga_fill_rect(wx+1, content_y+25, mo_lw, content_h-25, RGB(28,28,34));
        vga_draw_vline(wx+mo_lw+1, content_y+25, content_h-25, mo_sep);
        vga_draw_string_trans(wx+4, content_y+28, "LAYERS", mo_sub);
        { static const char *layers[]={"Background","Title Text","Logo","Particles","Glow FX","Transition"};
          static int layer_vis[]={1,1,1,0,1,1};
          int li7;
          for (li7=0;li7<6;li7++){
              int ly7=content_y+40+li7*18;
              if (ly7+14>content_y+content_h-4) break;
              /* Eye icon */
              vga_draw_string_trans(wx+4, ly7+3, layer_vis[li7]?"o":" ", layer_vis[li7]?mo_acc:mo_sep);
              vga_draw_string_trans(wx+14, ly7+3, layers[li7], li7==1?mo_acc:mo_txt);
          }
        }
        /* Canvas (center) */
        int cv_x=wx+mo_lw+2;
        int cv_w=ww-mo_lw-80;
        int cv_y=content_y+25, cv_h=content_h-25;
        if (cv_w < 1) cv_w = 1;
        if (cv_h < 1) cv_h = 1;
        /* Canvas background — animated scene */
        { int gri3;
          for (gri3=0;gri3<cv_h;gri3++){
              int rr=10+gri3*20/cv_h, gg=20+gri3*30/cv_h, bb=40+gri3*60/cv_h;
              vga_draw_hline(cv_x, cv_y+gri3, cv_w, RGB(rr,gg,bb));
          }
        }
        /* Animated particle effect */
        { int pi7;
          uint32_t t7=g_motion_playing ? timer_ticks() : 0;
          int particle_w = cv_w > 40 ? cv_w - 40 : 1;
          int particle_h = cv_h > 20 ? cv_h - 20 : 1;
          for (pi7=0;pi7<12;pi7++){
              int px7=cv_x+20+(pi7*37+t7/20)%particle_w;
              int py7=cv_y+10+(pi7*53+t7/15)%particle_h;
              int pr7=2+pi7%4;
              gui_draw_circle(px7,py7,pr7,mo_acc);
          }
        }
        /* Title text in canvas */
        vga_draw_string_trans(cv_x+(cv_w-80)/2, cv_y+cv_h/2-8, "MyOS Title", RGB(255,255,255));
        /* Safe area frame */
        vga_draw_rect_outline(cv_x+cv_w/10, cv_y+cv_h/10, cv_w*8/10, cv_h*8/10, RGB(50,50,60));
        /* Bounding box around title */
        vga_draw_rect_outline(cv_x+(cv_w-84)/2-2, cv_y+cv_h/2-10, 88, 18, mo_acc);
        /* Right panel: inspector */
        int ri7=cv_x+cv_w+2;
        int ri7w=wx+ww-ri7-2;
        vga_fill_rect(ri7, content_y+25, ri7w, content_h-25, RGB(28,28,34));
        vga_draw_string_trans(ri7+4, content_y+28, "PROPERTIES", mo_sub);
        { static const char *prop_labels[]={"Position","Scale","Rotation","Opacity"};
          int pi8, py8=content_y+40;
          for (pi8=0;pi8<4;pi8++){
              char prop_value[20];
              int pp = 0;
              if (py8+12>content_y+content_h-4) break;
              prop_value[0] = 0;
              if (pi8 == 0) {
                  apps4_append_text(prop_value, &pp, sizeof(prop_value), "X: ");
                  apps4_append_uint(prop_value, &pp, sizeof(prop_value), (uint32_t)(cv_x + cv_w / 2));
              } else if (pi8 == 1 || pi8 == 3) {
                  runtime_format_percent(100, prop_value, sizeof(prop_value));
              } else {
                  apps4_append_uint(prop_value, &pp, sizeof(prop_value), (uint32_t)((g_motion_playing ? timer_ticks() : 0U) / 100U % 360U));
                  apps4_append_text(prop_value, &pp, sizeof(prop_value), " deg");
              }
              vga_draw_string_trans(ri7+4, py8, prop_labels[pi8], mo_sub);
              vga_draw_string_trans(ri7+4, py8+10, prop_value, mo_txt);
              py8 += 22;
          }
          vga_draw_string_trans(ri7+4, py8, "Playback", mo_sub);
          vga_draw_string_trans(ri7+4, py8+10, g_motion_playing?"Playing":"Paused", g_motion_playing?mo_acc:mo_txt);
        }
        return 1;
    }

    /* ---- MainStage ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "MainStage")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t ms_bg  = RGB(15,15,18);
        uint32_t ms_tb  = RGB(28,28,34);
        uint32_t ms_txt = RGB(215,215,225);
        uint32_t ms_sub = RGB(130,130,140);
        uint32_t ms_acc = RGB(200,30,30);
        uint32_t ms_sep = RGB(44,44,52);
        uint32_t ms_grn = RGB(52,199,89);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, ms_bg);
        /* Top toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 24, ms_tb);
        vga_draw_hline(wx+1, content_y+24, ww-2, ms_sep);
        vga_draw_string_trans(wx+6, content_y+8, "Edit", ms_sub);
        vga_draw_string_trans(wx+36, content_y+8, "Perform", ms_txt);
        vga_draw_string_trans(wx+84, content_y+8, "Full Screen", ms_sub);
        gui_draw_rounded_rect(wx+ww-52, content_y+4, 46, 16, 3, ms_acc);
        vga_draw_string_trans(wx+ww-48, content_y+8, "Perform", RGB(255,255,255));
        /* Stage layout */
        int stage_y=content_y+25, stage_h=content_h-25;
        /* Piano keyboard at bottom */
        int kb_h=40, kb_y=stage_y+stage_h-kb_h;
        vga_fill_rect(wx+1, kb_y, ww-2, kb_h, RGB(30,30,36));
        vga_draw_hline(wx+1, kb_y, ww-2, ms_sep);
        /* Piano keys */
        { int ki, key_w=(ww-2)/24;
          for (ki=0;ki<24;ki++){
              int kx=wx+1+ki*key_w;
              /* White key */
              vga_fill_rect(kx, kb_y+2, key_w-1, kb_h-4, RGB(240,240,240));
              vga_draw_rect_outline(kx, kb_y+2, key_w-1, kb_h-4, RGB(180,180,180));
              /* Black keys at semitone positions */
              if (ki%12==1||ki%12==3||ki%12==6||ki%12==8||ki%12==10){
                  vga_fill_rect(kx-key_w/4, kb_y+2, key_w/2, kb_h*6/10, RGB(20,20,24));
              }
          }
        }
        /* Instrument pad grid above keyboard */
        int pad_y=stage_y+4, pad_h=stage_h-kb_h-8;
        /* Three instrument zones */
        { static const char *zones[]={"Organ","Synth Pad","Strings & Lead"};
          static uint32_t zc[]={RGB(60,160,200),RGB(200,50,200),RGB(200,130,0)};
          int zi;
          for (zi=0;zi<3;zi++){
              int zw=(ww-4)/3;
              int zx=wx+2+zi*zw;
              gui_draw_rounded_rect(zx, pad_y, zw-2, pad_h, 6, zc[zi]);
              vga_fill_rect_alpha(zx+1, pad_y+1, zw-3, pad_h/3, RGB(255,255,255), 25);
              int tw8=str_len(zones[zi])*8;
              vga_draw_string_trans(zx+(zw-2-tw8)/2, pad_y+pad_h/2-4, zones[zi], RGB(255,255,255));
              /* Volume/expression indicator */
              int vol_h=pad_h/4;
              vga_fill_rect(zx+4, pad_y+pad_h-vol_h-4, 8, vol_h, RGB(0,0,0));
              vga_fill_rect(zx+4, pad_y+pad_h-vol_h/2-4, 8, vol_h/2, ms_grn);
          }
        }
        /* CPU/Memory meters bottom right */
        { runtime_system_info_t sys;
          char pctbuf[8];
          runtime_get_system_info(&sys);
          runtime_format_percent(sys.cpu_load_percent, pctbuf, sizeof(pctbuf));
          vga_draw_string_trans(wx+ww-80, content_y+8, "CPU:", ms_grn);
          vga_draw_string_trans(wx+ww-42, content_y+8, pctbuf, ms_grn); }
        return 1;
    }

    /* ---- Compressor ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Compressor")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t cp_bg  = g_pref_darkmode ? RGB(26,26,30)   : RGB(240,240,244);
        uint32_t cp_tb  = g_pref_darkmode ? RGB(36,36,42)   : RGB(224,224,228);
        uint32_t cp_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t cp_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t cp_sep = g_pref_darkmode ? RGB(50,50,58)   : RGB(200,200,208);
        uint32_t cp_grn = RGB(52,199,89);
        uint32_t cp_yel = RGB(255,214,10);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, cp_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 22, cp_tb);
        vga_draw_hline(wx+1, content_y+22, ww-2, cp_sep);
        gui_draw_rounded_rect(wx+6, content_y+4, 60, 14, 3, RGB(0,122,255));
        vga_draw_string_trans(wx+10, content_y+7, "Add File", RGB(255,255,255));
        vga_draw_string_trans(wx+80, content_y+7, "Submit All", cp_sub);
        if (g_compressor_submitted > 0) {
            vga_draw_string_trans(wx+164, content_y+7, "Submitted", cp_grn);
        }
        /* Job queue */
        int jq_y=content_y+24;
        vga_draw_string_trans(wx+6, jq_y+4, "Batch", cp_sub);
        { struct { const char *name; const char *fmt; int pct; int done; } jobs[]={
              {"MyOS_demo_4K.mov","Apple ProRes 4K",100,1},
              {"Tutorial_v2.mp4","H.264 1080p", 72,0},
              {"Logo_animation.mov","HEVC 4K HDR", 30,0},
              {"Interview_raw.mxf","H.264 720p",  0,0},
          };
          int ji, jy=jq_y+18;
          for (ji=0;ji<4;ji++){
              if (jy+42>content_y+content_h-4) break;
              /* Job card */
              vga_fill_rect(wx+4, jy, ww-8, 38, g_pref_darkmode?RGB(34,34,40):RGB(252,252,255));
              vga_draw_rect_outline(wx+4, jy, ww-8, 38, cp_sep);
              /* Status dot */
              uint32_t dot_c=jobs[ji].done?cp_grn:(jobs[ji].pct>0?cp_yel:cp_sub);
              gui_draw_circle(wx+14, jy+10, 5, dot_c);
              /* File name */
              vga_draw_string_trans(wx+24, jy+4, jobs[ji].name, cp_txt);
              /* Format */
              vga_draw_string_trans(wx+24, jy+16, jobs[ji].fmt, cp_sub);
              /* Progress bar */
              vga_fill_rect(wx+24, jy+28, ww-30, 6, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
              int bar_w=(ww-30)*jobs[ji].pct/100;
              if (bar_w>0) vga_fill_rect(wx+24, jy+28, bar_w, 6, dot_c);
              /* Percent label */
              if (!jobs[ji].done && jobs[ji].pct>0){
                  char pbuf2[5]; int_to_str(jobs[ji].pct, pbuf2);
                  vga_draw_string_trans(wx+ww-28, jy+26, pbuf2, cp_sub);
              } else if (jobs[ji].done){
                  vga_draw_string_trans(wx+ww-28, jy+26, "Done", cp_grn);
              }
              jy += 42;
          }
          if (g_compressor_added_files > 0 && jy+20 <= content_y+content_h-4) {
              char add_line[32];
              char add_num[8];
              int ap = 0;
              add_line[0] = 0;
              apps4_append_text(add_line, &ap, sizeof(add_line), "Added local clip #");
              runtime_format_uint((uint32_t)g_compressor_added_files, add_num, sizeof(add_num));
              apps4_append_text(add_line, &ap, sizeof(add_line), add_num);
              vga_draw_string_trans(wx+24, jy+4, add_line, cp_txt);
          }
        }
        return 1;
    }

    /* ---- Screen Recording ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Screen Recording")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t sr_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t sr_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t sr_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t sr_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, sr_bg);
        vga_draw_hline(wx+1, cy, ww-2, sr_sep);
        /* Big record button */
        gui_draw_circle(wx+ww/2, cy+50, 28, g_screen_recording_active ? RGB(180,0,0) : RGB(255,59,48));
        gui_draw_circle_outline(wx+ww/2, cy+50, 32, sr_txt);
        vga_draw_string_trans(wx+ww/2-14, cy+50-4, g_screen_recording_active ? "ON" : "REC", RGB(255,255,255));
        /* Options */
        vga_draw_string_trans(wx+10, cy+92, "Microphone:", sr_sub);
        gui_draw_rounded_rect(wx+90, cy+88, 100, 14, 4, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+94, cy+91, "MacBook Pro Mic", sr_sub);
        vga_draw_string_trans(wx+10, cy+112, "Camera:", sr_sub);
        gui_draw_rounded_rect(wx+90, cy+108, 100, 14, 4, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+94, cy+111, "FaceTime HD", sr_sub);
        vga_draw_string_trans(wx+10, cy+132, "Timer Countdown:", sr_sub);
        gui_draw_rounded_rect(wx+116, cy+128, 40, 14, 4, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+126, cy+131, "5s", sr_sub);
        /* Show mouse cursor checkbox */
        vga_fill_rect(wx+10, cy+152, 12, 12, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_rect_outline(wx+10, cy+152, 12, 12, sr_sep);
        vga_draw_string_trans(wx+24, cy+153, "Show Mouse Cursor", sr_txt);
        /* Bottom bar */
        vga_fill_rect(wx+1, cy+wh-TITLEBAR_H-24, ww-2, 24, g_pref_darkmode?RGB(36,36,42):RGB(232,232,236));
        vga_draw_hline(wx+1, cy+wh-TITLEBAR_H-24, ww-2, sr_sep);
        gui_draw_rounded_rect(wx+ww/2-40, cy+wh-TITLEBAR_H-20, 80, 15, 4, RGB(255,59,48));
        vga_draw_string_trans(wx+ww/2-32, cy+wh-TITLEBAR_H-17, g_screen_recording_active ? "Stop Rec" : "Start Rec", RGB(255,255,255));
        if (g_screen_recording_count > 0) {
            char rec_line[24];
            char rec_num[8];
            int rp = 0;
            rec_line[0] = 0;
            apps4_append_text(rec_line, &rp, sizeof(rec_line), "Saved clips: ");
            runtime_format_uint((uint32_t)g_screen_recording_count, rec_num, sizeof(rec_num));
            apps4_append_text(rec_line, &rp, sizeof(rec_line), rec_num);
            vga_draw_string_trans(wx+10, cy+172, rec_line, sr_sub);
        }
        return 1;
    }

    /* ---- Sidecar ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Sidecar")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t sc_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t sc_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t sc_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t sc_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, sc_bg);
        /* iPad graphic */
        gui_draw_rounded_rect(wx+ww/2-45, cy+10, 90, 110, 8, sc_txt);
        gui_draw_rounded_rect(wx+ww/2-40, cy+14, 80, 90, 5, RGB(20,20,28));
        vga_fill_rect(wx+ww/2-40, cy+14, 80, 90, RGB(30,120,255));
        vga_draw_string_trans(wx+ww/2-20, cy+55, "iPad Pro", RGB(255,255,255));
        /* Home button indicator */
        gui_draw_circle(wx+ww/2, cy+112, 5, RGB(80,80,90));
        /* Status */
        vga_draw_string_trans(wx+ww/2-34, cy+130, "iPad Connected", sc_txt);
        vga_draw_string_trans(wx+20, cy+150, "iPad Pro (12.9-inch)", sc_sub);
        vga_draw_hline(wx+20, cy+165, ww-40, sc_sep);
        vga_draw_string_trans(wx+20, cy+170, "Display As:", sc_sub);
        gui_draw_rounded_rect(wx+90, cy+166, 80, 14, 4, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+94, cy+169, "Extended", sc_sub);
        vga_draw_string_trans(wx+20, cy+188, "Sidebar:", sc_sub);
        gui_draw_rounded_rect(wx+90, cy+184, 60, 14, 4, RGB(0,122,255));
        vga_draw_string_trans(wx+94, cy+187, "Left", RGB(255,255,255));
        return 1;
    }

    /* ---- Universal Control ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Universal Control")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t uc_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t uc_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t uc_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t uc_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, uc_bg);
        /* Graphic: Mac + iPad side by side */
        /* Mac */
        gui_draw_rounded_rect(wx+30, cy+20, 90, 70, 6, uc_txt);
        vga_fill_rect(wx+32, cy+22, 86, 60, RGB(40,120,200));
        vga_draw_string_trans(wx+50, cy+48, "My Mac", RGB(255,255,255));
        gui_draw_rounded_rect(wx+20, cy+90, 110, 8, 3, RGB(80,80,90));
        /* No external display is paired. */
        vga_draw_string_trans(wx+ww/2-6, cy+50, "--", uc_sub);
        /* iPad */
        gui_draw_rounded_rect(wx+ww-120, cy+20, 80, 100, 6, uc_txt);
        vga_fill_rect(wx+ww-118, cy+22, 76, 80, RGB(30,30,180));
        vga_draw_string_trans(wx+ww-108, cy+58, "iPad", RGB(255,255,255));
        /* Status */
        vga_draw_hline(wx+10, cy+110, ww-20, uc_sep);
        vga_draw_string_trans(wx+10, cy+118, "Local pairing ready", uc_sub);
        vga_draw_string_trans(wx+10, cy+134, "Nearby Devices:", uc_txt);
        gui_draw_rounded_rect(wx+10, cy+150, ww-20, 20, 4, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+16, cy+156, "iPad - Ready", uc_txt);
        gui_draw_circle(wx+ww-24, cy+160, 4, RGB(52,199,89));
        return 1;
    }

    /* ---- Handoff ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Handoff")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t hf_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t hf_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t hf_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t hf_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, hf_bg);
        vga_draw_string_trans(wx+ww/2-30, cy+8, "Handoff", hf_txt);
        vga_draw_hline(wx+10, cy+22, ww-20, hf_sep);
        /* Recent activities from other devices */
        struct { const char *dev; const char *app; const char *doc; uint32_t col; } acts[]={
            {"iPhone","Safari","Home Page",RGB(255,159,0)},
            {"iPad","Notes","Shopping list",RGB(255,214,10)},
            {"iPhone","Mail","Re: Project Update",RGB(0,122,255)},
            {"Mac","Pages","Annual Report 2026.pages",RGB(255,149,0)},
        };
        int hf_sel = g_handoff_selected;
        if (hf_sel < 0) hf_sel = 0;
        if (hf_sel > 3) hf_sel = 3;
        int ai, ay=cy+30;
        for (ai=0;ai<4;ai++){
            if (ay+36>cy+wh-TITLEBAR_H) break;
            gui_draw_rounded_rect(wx+8, ay, ww-16, 32, 5,
                                  ai==hf_sel ? RGB(0,122,255) : (g_pref_darkmode?RGB(36,36,44):RGB(250,250,255)));
            vga_draw_rect_outline(wx+8, ay, ww-16, 32, ai==hf_sel ? RGB(0,122,255) : hf_sep);
            gui_draw_circle(wx+22, ay+16, 10, acts[ai].col);
            vga_draw_string_trans(wx+22-4, ay+13, acts[ai].app, RGB(255,255,255));
            vga_draw_string_trans(wx+36, ay+6, acts[ai].doc, ai==hf_sel ? RGB(255,255,255) : hf_txt);
            vga_draw_string_trans(wx+36, ay+18, acts[ai].dev, ai==hf_sel ? RGB(220,235,255) : hf_sub);
            ay += 36;
        }
        { char hf_line[48]; int hp = 0;
          hf_line[0] = 0;
          apps4_append_text(hf_line, &hp, sizeof(hf_line), "Continue ");
          apps4_append_text(hf_line, &hp, sizeof(hf_line), acts[hf_sel].app);
          apps4_append_text(hf_line, &hp, sizeof(hf_line), " on this Mac");
          vga_draw_string_trans(wx+10, cy+wh-TITLEBAR_H-18, hf_line, RGB(0,122,255)); }
        return 1;
    }

    /* ---- Privacy ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Privacy")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t pv_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t pv_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t pv_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t pv_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        uint32_t pv_grn = RGB(52,199,89);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, pv_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, cy, 100, wh-TITLEBAR_H, g_pref_darkmode?RGB(34,34,40):RGB(236,236,240));
        vga_draw_vline(wx+100, cy, wh-TITLEBAR_H, pv_sep);
        const char *cats[]={"Location","Contacts","Calendars","Camera","Microphone","Photos","Health","HomeKit","Tracking"};
        int ci, cy2=cy+6;
        for (ci=0;ci<9;ci++){
            vga_draw_string_trans(wx+6, cy2, cats[ci], ci==0?RGB(0,122,255):pv_sub);
            cy2+=20;
        }
        /* Content */
        vga_draw_string_trans(wx+110, cy+8, "Location Services", pv_txt);
        vga_draw_hline(wx+108, cy+22, ww-112, pv_sep);
        vga_draw_string_trans(wx+108, cy+28, "Apps that have requested your location:", pv_sub);
        struct { const char *app; const char *when; int on; } locs[]={
            {"Maps","While Using",1},
            {"Weather","Always",1},
            {"Safari","While Using",0},
            {"Reminders","Never",0},
        };
        int li; int loy=cy+44;
        for (li=0;li<4;li++){
            if (loy+18>cy+wh-TITLEBAR_H) break;
            vga_draw_string_trans(wx+110, loy, locs[li].app, pv_txt);
            vga_draw_string_trans(wx+170, loy, locs[li].when, pv_sub);
            gui_draw_rounded_rect(wx+ww-30, loy-2, 22, 12, 6, locs[li].on?pv_grn:pv_sep);
            gui_draw_circle(wx+ww-(locs[li].on?12:20), loy+4, 4, RGB(255,255,255));
            loy+=22;
        }
        return 1;
    }

    /* ---- Accessibility ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Accessibility")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t ac_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t ac_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t ac_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        uint32_t ac_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, ac_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, cy, 110, wh-TITLEBAR_H, g_pref_darkmode?RGB(34,34,40):RGB(236,236,240));
        vga_draw_vline(wx+110, cy, wh-TITLEBAR_H, ac_sep);
        struct { const char *cat; const char *icon; } cats2[]={
            {"Vision","V"},{"Hearing","H"},{"Motor","M"},{"Cognitive","C"},{"Speech","S"},
        };
        int ci2, cy3=cy+8;
        for (ci2=0;ci2<5;ci2++){
            gui_draw_circle(wx+18, cy3+6, 8, ci2==0?ac_acc:ac_sep);
            vga_draw_string_trans(wx+15, cy3+3, cats2[ci2].icon, RGB(255,255,255));
            vga_draw_string_trans(wx+30, cy3+3, cats2[ci2].cat, ci2==0?ac_acc:ac_txt);
            cy3+=28;
        }
        /* Content */
        vga_draw_string_trans(wx+120, cy+8, "Vision", ac_txt);
        vga_draw_hline(wx+118, cy+22, ww-122, ac_sep);
        struct { const char *name; int on; } feats[]={
            {"VoiceOver",0},{"Zoom",0},{"Display Size",0},{"Bold Text",1},{"Increase Contrast",0},
        };
        int fi4, fy=cy+30;
        for (fi4=0;fi4<5;fi4++){
            vga_draw_string_trans(wx+120, fy, feats[fi4].name, ac_txt);
            int on2=(fi4==3)?1:0;
            gui_draw_rounded_rect(wx+ww-34, fy-2, 26, 14, 7, on2?RGB(52,199,89):ac_sep);
            gui_draw_circle(wx+ww-(on2?12:24), fy+5, 5, RGB(255,255,255));
            fy+=24;
        }
        return 1;
    }

    /* ---- AirPlay ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "AirPlay")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t ap2_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t ap2_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t ap2_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t ap2_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, ap2_bg);
        /* AirPlay icon */
        vga_draw_string_trans(wx+ww/2-30, cy+8, "AirPlay To:", ap2_txt);
        vga_draw_hline(wx+10, cy+22, ww-20, ap2_sep);
        /* Device list */
        struct { const char *name; const char *type; int connected; } devs[]={
            {"Apple TV 4K","Apple TV",1},
            {"Living Room TV","Smart TV",0},
            {"Kitchen HomePod","HomePod",0},
            {"Bedroom HomePod","HomePod mini",0},
        };
        int di, dy=cy+28;
        for (di=0;di<4;di++){
            if (dy+28>cy+wh-TITLEBAR_H) break;
            gui_draw_rounded_rect(wx+8, dy, ww-16, 24, 5,
                devs[di].connected?(g_pref_darkmode?RGB(0,50,120):RGB(210,230,255)):
                (g_pref_darkmode?RGB(36,36,44):RGB(250,250,255)));
            if (devs[di].connected) vga_draw_rect_outline(wx+8, dy, ww-16, 24, RGB(0,122,255));
            gui_draw_circle(wx+20, dy+12, 6, devs[di].connected?RGB(0,122,255):ap2_sep);
            vga_draw_string_trans(wx+30, dy+4, devs[di].name, ap2_txt);
            vga_draw_string_trans(wx+30, dy+14, devs[di].type, ap2_sub);
            if (devs[di].connected) vga_draw_string_trans(wx+ww-56, dy+9, "Playing", RGB(0,122,255));
            dy += 28;
        }
        return 1;
    }

    /* ---- TestFlight ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "TestFlight")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t tf_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t tf_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t tf_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t tf_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, tf_bg);
        /* Header */
        vga_fill_rect(wx+1, cy, ww-2, 28, g_pref_darkmode?RGB(36,36,44):RGB(232,232,236));
        vga_draw_hline(wx+1, cy+28, ww-2, tf_sep);
        vga_draw_string_trans(wx+8, cy+10, "TestFlight", tf_txt);
        vga_draw_string_trans(wx+ww-60, cy+10, "+ Invite", RGB(0,122,255));
        /* App list */
        struct { const char *name; const char *ver; const char *exp; } apps[]={
            {"MyOS Remote","v2.1 (Build 47)","Valid"},
            {"Safari Beta","v18.3 (Build 2)","Valid"},
            {"TestApp","v1.0 (Build 12)","Review"},
        };
        int ai2, ay2=cy+36;
        for (ai2=0;ai2<3;ai2++){
            if (ay2+40>cy+wh-TITLEBAR_H) break;
            vga_fill_rect(wx+4, ay2, ww-8, 36, g_pref_darkmode?RGB(36,36,44):RGB(252,252,255));
            vga_draw_rect_outline(wx+4, ay2, ww-8, 36, tf_sep);
            gui_draw_rounded_rect(wx+8, ay2+6, 24, 24, 6, RGB(0,122,255));
            vga_draw_string_trans(wx+14, ay2+14, "T", RGB(255,255,255));
            vga_draw_string_trans(wx+36, ay2+6, apps[ai2].name, tf_txt);
            vga_draw_string_trans(wx+36, ay2+18, apps[ai2].ver, tf_sub);
            vga_draw_string_trans(wx+36, ay2+28, apps[ai2].exp, tf_sub);
            gui_draw_rounded_rect(wx+ww-50, ay2+10, 42, 16, 4, RGB(0,122,255));
            vga_draw_string_trans(wx+ww-44, ay2+14, "UPDATE", RGB(255,255,255));
            ay2 += 40;
        }
        return 1;
    }

    /* ---- Reality Composer ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Reality Composer")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t rc_bg  = g_pref_darkmode ? RGB(20,20,28) : RGB(240,240,248);
        uint32_t rc_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t rc_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t rc_sep = g_pref_darkmode ? RGB(40,40,52) : RGB(200,200,212);
        uint32_t rc_pur = RGB(147,44,246);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, rc_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, cy, ww-2, 22, g_pref_darkmode?RGB(30,30,40):RGB(228,228,236));
        vga_draw_hline(wx+1, cy+22, ww-2, rc_sep);
        vga_draw_string_trans(wx+8, cy+7, "Reality Composer Pro", rc_txt);
        /* 3D viewport */
        vga_fill_rect(wx+4, cy+26, ww-8, wh-TITLEBAR_H-60, RGB(15,15,25));
        /* Grid */
        int gi;
        for (gi=0;gi<6;gi++) {
            vga_draw_hline(wx+4, cy+26+gi*20, ww-8, RGB(30,30,50));
            vga_draw_vline(wx+4+gi*30, cy+26, wh-TITLEBAR_H-60, RGB(30,30,50));
        }
        /* 3D box object */
        vga_fill_rect(wx+ww/2-25, cy+60, 50, 40, rc_pur);
        vga_draw_rect_outline(wx+ww/2-25, cy+60, 50, 40, RGB(200,150,255));
        /* Isometric lines */
        vga_draw_hline(wx+ww/2-25, cy+60, 15, RGB(200,150,255));
        vga_draw_vline(wx+ww/2-10, cy+45, 15, RGB(200,150,255));
        vga_draw_string_trans(wx+ww/2-8, cy+75, "AR", RGB(255,255,255));
        /* Bottom panel */
        vga_fill_rect(wx+1, cy+wh-TITLEBAR_H-36, ww-2, 36, g_pref_darkmode?RGB(30,30,40):RGB(228,228,236));
        vga_draw_hline(wx+1, cy+wh-TITLEBAR_H-36, ww-2, rc_sep);
        vga_draw_string_trans(wx+8, cy+wh-TITLEBAR_H-28, "Object: Box  Scale: 1.0  Anchor: Horizontal", rc_sub);
        return 1;
    }

    /* ---- Configurator ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Configurator")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t cf_bg  = g_pref_darkmode ? RGB(28,28,32) : RGB(245,245,248);
        uint32_t cf_txt = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t cf_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t cf_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(210,210,218);
        uint32_t cf_acc = RGB(40,120,200);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, cf_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, cy, 100, wh-TITLEBAR_H, g_pref_darkmode?RGB(34,34,40):RGB(236,236,240));
        vga_draw_vline(wx+100, cy, wh-TITLEBAR_H, cf_sep);
        const char *cfcats[]={"Blueprints","Devices","Groups","Apps","Profiles","Docs"};
        int ci3, cy4=cy+8;
        for (ci3=0;ci3<6;ci3++){
            vga_draw_string_trans(wx+8, cy4, cfcats[ci3], ci3==1?cf_acc:cf_sub);
            cy4+=24;
        }
        /* Content */
        vga_draw_string_trans(wx+108, cy+8, "Devices", cf_txt);
        vga_draw_hline(wx+106, cy+22, ww-110, cf_sep);
        struct { const char *name; const char *model; int supervised; } devs2[]={
            {"John's iPhone","iPhone 15 Pro",1},
            {"Conference iPad","iPad Pro M4",1},
            {"Dev MacBook","MacBook Pro M3",0},
        };
        int di2, dy2=cy+30;
        for (di2=0;di2<3;di2++){
            if (dy2+26>cy+wh-TITLEBAR_H) break;
            vga_fill_rect(wx+104, dy2, ww-108, 22, g_pref_darkmode?RGB(36,36,44):RGB(250,250,255));
            vga_draw_rect_outline(wx+104, dy2, ww-108, 22, cf_sep);
            gui_draw_circle(wx+114, dy2+11, 6, cf_acc);
            vga_draw_string_trans(wx+124, dy2+4, devs2[di2].name, cf_txt);
            vga_draw_string_trans(wx+124, dy2+14, devs2[di2].model, cf_sub);
            if (devs2[di2].supervised)
                vga_draw_string_trans(wx+ww-58, dy2+9, "Supervised", RGB(52,199,89));
            dy2 += 26;
        }
        return 1;
    }

    /* ---- Stickies ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Stickies")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        static const uint32_t sticky_cols[4] = {RGB(255,220,50),RGB(100,220,120),RGB(100,180,255),RGB(255,140,140)};
        int sw=(ww-10)/2, sh=(wh-TITLEBAR_H-10)/2;
        int si; for (si=0;si<4;si++) {
            int sx=wx+2+(si%2)*(sw+4), sy=cy+2+(si/2)*(sh+4);
            vga_fill_rect(sx,sy,sw,sh,sticky_cols[si]);
            vga_fill_rect(sx,sy,sw,10,RGB(0,0,0)); vga_fill_rect_alpha(sx,sy,sw,10,sticky_cols[si],160);
            vga_draw_string_trans(sx+4,sy+2,"Note",RGB(40,40,40));
            vga_draw_hline(sx,sy+10,sw,RGB(0,0,0));
            const char *lines[]={"Remember to","call dentist","— Meeting 3pm","— Buy groceries"};
            int li; for(li=0;li<3&&li*12<sh-20;li++)
                vga_draw_string_trans(sx+4,sy+14+li*12,lines[(si+li)%4],RGB(30,30,30));
        }
        return 1;
    }
    /* ---- Dictionary ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Dictionary")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t dbg=g_pref_darkmode?RGB(28,28,32):RGB(252,252,252);
        uint32_t dtxt=g_pref_darkmode?RGB(220,220,228):RGB(20,20,28);
        uint32_t dsub=g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        uint32_t dacc=RGB(60,120,200);
        uint32_t dpos=RGB(150,100,200);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,dbg);
        /* Search bar */
        uint32_t sb_bg = g_dict_focused ? (g_pref_darkmode?RGB(50,50,60):RGB(255,255,255))
                                        : (g_pref_darkmode?RGB(40,40,46):RGB(230,230,235));
        uint32_t sb_bd = g_dict_focused ? RGB(60,120,200)
                                        : (g_pref_darkmode?RGB(70,70,78):RGB(190,190,200));
        vga_fill_rect(wx+8,cy+5,ww-16,18,sb_bg);
        vga_draw_rect_outline(wx+8,cy+5,ww-16,18,sb_bd);
        /* Search icon */
        gui_draw_circle(wx+16, cy+14, 4, dsub);
        gui_draw_circle(wx+16, cy+14, 2, sb_bg);
        vga_draw_line(wx+19, cy+17, wx+22, cy+20, dsub);
        /* Search input text */
        { const char *disp = g_dict_input_len > 0 ? g_dict_input : "Search Dictionary...";
          uint32_t dc = g_dict_input_len > 0 ? dtxt : dsub;
          vga_draw_string_trans(wx+26,cy+10,disp,dc);
          /* Cursor */
          if (g_dict_focused && g_dict_input_len > 0) {
              int cx2 = wx+26+g_dict_input_len*8;
              vga_draw_vline(cx2,cy+8,12,dacc);
          }
        }
        vga_draw_hline(wx+2,cy+27,ww-4,g_pref_darkmode?RGB(50,50,56):RGB(210,210,216));
        /* Find matching word */
        { const dict_entry_t *found = 0;
          int di;
          /* Default: serendipity if no input */
          if (g_dict_input_len == 0) found = &g_dict_words[0];
          else {
              for (di=0; g_dict_words[di].word; di++) {
                  const char *w = g_dict_words[di].word;
                  int match=1, k;
                  for (k=0; k<g_dict_input_len; k++) {
                      char a=g_dict_input[k], b=w[k];
                      if (a>='A'&&a<='Z') a+=32;
                      if (b>='A'&&b<='Z') b+=32;
                      if (!b || a!=b) { match=0; break; }
                  }
                  if (match) { found=&g_dict_words[di]; break; }
              }
          }
          if (found) {
              int ey = cy+36;
              vga_draw_string_trans(wx+10,ey,found->word,dacc); ey+=14;
              vga_draw_string_trans(wx+10,ey,found->phonetic,dsub); ey+=12;
              vga_draw_string_trans(wx+10,ey,found->pos,dpos); ey+=14;
              vga_draw_string_trans(wx+10,ey,found->def1,dtxt); ey+=12;
              vga_draw_string_trans(wx+10,ey,found->def2,dtxt); ey+=14;
              vga_draw_hline(wx+8,ey+2,ww-16,g_pref_darkmode?RGB(50,50,56):RGB(220,220,226)); ey+=6;
              vga_draw_string_trans(wx+10,ey+2,"Also: Thesaurus, Wikipedia",dsub);
          } else {
              vga_draw_string_trans(wx+10,cy+50,"No results found.",dsub);
              vga_draw_string_trans(wx+10,cy+66,"Try a different word.",dsub);
          }
        }
        return 1;
    }
    /* ---- 2048 ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "2048")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t bg2 = RGB(187,173,160);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, RGB(250,248,239));
        /* Header: score */
        vga_fill_rect(wx+4, cy+4, 60, 28, RGB(187,173,160));
        vga_draw_string_trans(wx+8, cy+7, "SCORE", RGB(238,228,218));
        { char sbuf[12]; int sv=g_2048_score;
          if (sv==0) { sbuf[0]='0'; sbuf[1]=0; }
          else { char tmp[12]; int tl=0; while(sv>0){tmp[tl++]='0'+sv%10;sv/=10;} int k; for(k=0;k<tl;k++) sbuf[k]=tmp[tl-1-k]; sbuf[tl]=0; }
          vga_draw_string_trans(wx+8, cy+18, sbuf, RGB(249,246,242)); }
        vga_fill_rect(wx+68, cy+4, 60, 28, RGB(187,173,160));
        vga_draw_string_trans(wx+72, cy+7, "BEST", RGB(238,228,218));
        { char bbuf[12]; int bv=g_2048_best;
          if (bv==0) { bbuf[0]='0'; bbuf[1]=0; }
          else { char tmp2[12]; int tl2=0; while(bv>0){tmp2[tl2++]='0'+bv%10;bv/=10;} int k; for(k=0;k<tl2;k++) bbuf[k]=tmp2[tl2-1-k]; bbuf[tl2]=0; }
          vga_draw_string_trans(wx+72, cy+18, bbuf, RGB(249,246,242)); }
        /* NEW GAME button */
        vga_fill_rect(wx+ww-70, cy+4, 62, 28, RGB(143,122,102));
        vga_draw_string_trans(wx+ww-62, cy+14, "New Game", RGB(249,246,242));
        /* Board */
        int bsz = ww - 12; if (bsz > wh - TITLEBAR_H - 44) bsz = wh - TITLEBAR_H - 44;
        int bx0 = wx + (ww - bsz)/2, by0 = cy + 38;
        int pad2 = 4, tsz = (bsz - pad2*5) / 4;
        vga_fill_rect(bx0, by0, bsz, bsz, bg2);
        static const uint32_t tile_cols[12] = {
            RGB(205,193,180), /* empty */
            RGB(238,228,218), /* 2 */
            RGB(237,224,200), /* 4 */
            RGB(242,177,121), /* 8 */
            RGB(245,149,99),  /* 16 */
            RGB(246,124,95),  /* 32 */
            RGB(246,94,59),   /* 64 */
            RGB(237,207,114), /* 128 */
            RGB(237,204,97),  /* 256 */
            RGB(237,200,80),  /* 512 */
            RGB(237,197,63),  /* 1024 */
            RGB(237,194,46),  /* 2048 */
        };
        int r2, c2;
        for (r2=0; r2<4; r2++) for (c2=0; c2<4; c2++) {
            int tx = bx0 + pad2 + c2*(tsz+pad2);
            int ty = by0 + pad2 + r2*(tsz+pad2);
            int val = g_2048_board[r2][c2];
            int idx2 = 0;
            if (val > 0) { int v2=val; while(v2>1){v2/=2;idx2++;} if(idx2>11)idx2=11; }
            vga_fill_rect(tx, ty, tsz, tsz, tile_cols[idx2]);
            if (val > 0) {
                char vbuf[8]; int vi=val;
                char tmp3[8]; int tl3=0; while(vi>0){tmp3[tl3++]='0'+vi%10;vi/=10;}
                int k; for(k=0;k<tl3;k++) vbuf[k]=tmp3[tl3-1-k]; vbuf[tl3]=0;
                uint32_t tc = (idx2 <= 2) ? RGB(119,110,101) : RGB(249,246,242);
                int vlen = str_len(vbuf);
                int vx2 = tx + (tsz - vlen*8)/2;
                int vy2 = ty + (tsz - 8)/2;
                vga_draw_string_trans(vx2, vy2, vbuf, tc);
            }
        }
        /* Status overlay */
        if (g_2048_state == 0) {
            vga_fill_rect_alpha(bx0, by0, bsz, bsz, RGB(238,228,218), 180);
            vga_draw_string_trans(bx0+bsz/2-36, by0+bsz/2-8, "Arrow keys", RGB(119,110,101));
            vga_draw_string_trans(bx0+bsz/2-32, by0+bsz/2+4, "to start!", RGB(119,110,101));
        } else if (g_2048_state == 2) {
            vga_fill_rect_alpha(bx0, by0, bsz, bsz, RGB(237,194,46), 160);
            vga_draw_string_trans(bx0+bsz/2-16, by0+bsz/2-8, "YOU", RGB(249,246,242));
            vga_draw_string_trans(bx0+bsz/2-12, by0+bsz/2+4, "WIN!", RGB(249,246,242));
        } else if (g_2048_state == 3) {
            vga_fill_rect_alpha(bx0, by0, bsz, bsz, RGB(119,110,101), 160);
            vga_draw_string_trans(bx0+bsz/2-24, by0+bsz/2-8, "Game", RGB(249,246,242));
            vga_draw_string_trans(bx0+bsz/2-12, by0+bsz/2+4, "Over", RGB(249,246,242));
        }
        return 1;
    }
    /* ---- Health ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Health")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t hbg = g_pref_darkmode ? RGB(18,18,18) : RGB(242,242,247);
        uint32_t htxt = g_pref_darkmode ? RGB(255,255,255) : RGB(0,0,0);
        uint32_t hsub = g_pref_darkmode ? RGB(170,170,170) : RGB(110,110,118);
        datetime_t hnow;
        uint32_t health_phase = (timer_ticks() / 30000U) % 20U;
        int move_pct = 58 + (int)(health_phase % 30U);
        int exercise_pct = 42 + (int)((health_phase * 3U) % 40U);
        int stand_pct = 65 + (int)((health_phase * 2U) % 28U);
        char move_val[16];
        char exercise_val[16];
        char stand_val[16];
        char hvals[5][20];
        get_current_datetime(&hnow);
        apps4_format_uint_suffix((uint32_t)(move_pct * 7), " Cal", move_val, sizeof(move_val));
        apps4_format_uint_suffix((uint32_t)(exercise_pct * 3 / 5), " min", exercise_val, sizeof(exercise_val));
        apps4_format_uint_suffix((uint32_t)(stand_pct / 8), " hrs", stand_val, sizeof(stand_val));
        apps4_format_uint_suffix(7000U + health_phase * 137U, "", hvals[0], sizeof(hvals[0]));
        apps4_format_uint_suffix(62U + (health_phase % 18U), " BPM", hvals[1], sizeof(hvals[1]));
        runtime_format_minutes(360 + (int)(health_phase * 3U), hvals[2], sizeof(hvals[2]));
        apps4_format_decimal_tenths(30U + health_phase, " km", hvals[3], sizeof(hvals[3]));
        apps4_format_uint_suffix(1800U + health_phase * 31U, "", hvals[4], sizeof(hvals[4]));
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, hbg);
        /* Title */
        vga_draw_string_trans(wx+8, cy+6, "Summary", htxt);
        vga_draw_string_trans(wx+8, cy+17, datetime_weekday_long(hnow.weekday), hsub);
        /* Activity rings */
        int ring_cx = wx + ww/2;
        int ring_cy = cy + 95;
        draw_ring_arc(ring_cx, ring_cy, 66, 57, move_pct, RGB(255,45,85), RGB(80,15,28));
        draw_ring_arc(ring_cx, ring_cy, 50, 41, exercise_pct, RGB(92,214,92), RGB(12,60,12));
        draw_ring_arc(ring_cx, ring_cy, 34, 25, stand_pct, RGB(10,210,255), RGB(5,55,70));
        /* Ring labels */
        vga_draw_string_trans(wx+8, cy+74, "Move",   RGB(255,45,85));
        vga_draw_string_trans(wx+8, cy+84, move_val,RGB(255,45,85));
        vga_draw_string_trans(wx+8, cy+100,"Exercise",RGB(92,214,92));
        vga_draw_string_trans(wx+8, cy+110,exercise_val, RGB(92,214,92));
        vga_draw_string_trans(wx+8, cy+126,"Stand",  RGB(10,210,255));
        vga_draw_string_trans(wx+8, cy+136,stand_val, RGB(10,210,255));
        /* Stats divider */
        int sy = ring_cy + 78;
        vga_draw_hline(wx+4, sy, ww-8, g_pref_darkmode?RGB(50,50,55):RGB(210,210,215));
        sy += 6;
        /* Metric cards */
        static const char *hlabels[]={"Steps","Heart Rate","Sleep","Distance","Calories"};
        static const char *hunits[] ={"steps today","resting","last night","walked","kcal today"};
        static const uint32_t hcols[]={RGB(255,149,0),RGB(255,45,85),RGB(88,86,214),RGB(52,199,89),RGB(255,59,48)};
        int cw2=(ww-10)/2, ch2=46;
        int hi2;
        for (hi2=0;hi2<5;hi2++) {
            int hcx = wx+3+(hi2%2)*(cw2+4);
            int hcy = sy+(hi2/2)*(ch2+4);
            uint32_t cbg = g_pref_darkmode?RGB(30,30,35):RGB(255,255,255);
            vga_fill_rect(hcx, hcy, hi2==4?ww-8:cw2, ch2, cbg);
            gui_draw_circle(hcx+14, hcy+14, 8, hcols[hi2]);
            vga_draw_string_trans(hcx+6, hcy+29, hlabels[hi2], hsub);
            vga_draw_string_trans(hcx+26, hcy+8, hvals[hi2], htxt);
            vga_draw_string_trans(hcx+26, hcy+20, hunits[hi2], hsub);
        }
        return 1;
    }
    /* ---- Sudoku ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Sudoku")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t sbg2 = g_pref_darkmode ? RGB(20,20,25) : RGB(248,248,252);
        uint32_t stxt2 = g_pref_darkmode ? RGB(230,230,235) : RGB(15,15,25);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, sbg2);
        /* Header */
        vga_draw_string_trans(wx+8, cy+5, "SUDOKU", stxt2);
        { char ebuf[20]; ebuf[0]='E'; ebuf[1]='r'; ebuf[2]='r'; ebuf[3]=':'; ebuf[4]=' '; ebuf[5]='0'+g_sdk_errors; ebuf[6]=0; vga_draw_string_trans(wx+ww-50, cy+5, ebuf, RGB(255,59,48)); }
        if (!g_sdk_started) {
            vga_draw_string_trans(wx+8, cy+16, "Click to start", RGB(110,110,118));
        }
        /* Board */
        int bsz2 = ww-14; if(bsz2>wh-TITLEBAR_H-50) bsz2=wh-TITLEBAR_H-50;
        int bx0 = wx+(ww-bsz2)/2, by0 = cy+26;
        int cell2 = bsz2/9;
        vga_fill_rect(bx0, by0, bsz2, bsz2, g_pref_darkmode?RGB(40,40,50):RGB(255,255,255));
        /* Draw grid lines */
        int gi;
        for (gi=0; gi<=9; gi++) {
            uint32_t glc = (gi%3==0) ? (g_pref_darkmode?RGB(200,200,210):RGB(30,30,40)) : (g_pref_darkmode?RGB(80,80,95):RGB(160,160,175));
            vga_draw_vline(bx0+gi*cell2, by0, bsz2, glc);
            vga_draw_hline(bx0, by0+gi*cell2, bsz2, glc);
        }
        /* Draw numbers */
        int r3, c3;
        for (r3=0;r3<9;r3++) for (c3=0;c3<9;c3++) {
            int val3 = g_sdk_started ? g_sdk_board[r3][c3] : g_sdk_puzzle[r3][c3];
            int given3 = g_sdk_given[r3][c3];
            int cx3 = bx0 + c3*cell2 + (cell2-8)/2;
            int cy3 = by0 + r3*cell2 + (cell2-8)/2;
            /* Selected cell highlight */
            if (g_sdk_sel_r==r3 && g_sdk_sel_c==c3) {
                vga_fill_rect(bx0+c3*cell2+1, by0+r3*cell2+1, cell2-1, cell2-1, RGB(179,213,255));
            }
            if (val3) {
                uint32_t nc3 = given3 ? stxt2 : RGB(0,100,220);
                char nb3[2]; nb3[0]='0'+val3; nb3[1]=0;
                vga_draw_string_trans(cx3, cy3, nb3, nc3);
            }
        }
        /* Number pad at bottom */
        int np_y = by0 + bsz2 + 4;
        for (gi=1;gi<=9;gi++) {
            int npx = bx0 + (gi-1) * (bsz2/9);
            vga_fill_rect(npx+1, np_y, cell2-2, 14, g_pref_darkmode?RGB(60,60,75):RGB(220,220,228));
            char nb4[2]; nb4[0]='0'+gi; nb4[1]=0;
            vga_draw_string_trans(npx+(cell2-8)/2, np_y+3, nb4, stxt2);
        }
        return 1;
    }
    /* ---- Chess ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Chess")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,g_pref_darkmode?RGB(22,22,26):RGB(238,238,242));
        /* Board sizing */
        int bsz = wh - TITLEBAR_H - 32; if (bsz > ww-16) bsz=ww-16; if (bsz>168)bsz=168;
        int bx = wx+(ww-bsz)/2, by=cy+6;
        int sq = bsz/8;
        int r2, c2;
        /* Piece letter lookup: 0=empty,1-6=white P/N/B/R/Q/K, 7-12=black p/n/b/r/q/k */
        static const char piece_ch[13]={' ','P','N','B','R','Q','K','p','n','b','r','q','k'};
        for(r2=0;r2<8;r2++) {
            for(c2=0;c2<8;c2++) {
                int px=bx+c2*sq, py=by+r2*sq;
                /* Square color */
                uint32_t sqc = ((r2+c2)%2==0)?RGB(238,216,181):RGB(181,136,99);
                /* Selection highlight */
                if (r2==g_chess_sel_r && c2==g_chess_sel_c)
                    sqc = RGB(120,200,80);
                /* Last move highlight (flash) */
                vga_fill_rect(px,py,sq,sq,sqc);
                /* Draw piece */
                int piece = g_chess_board[r2][c2];
                if (piece > 0) {
                    int is_white = (piece <= 6);
                    uint32_t pc = is_white ? RGB(255,255,255) : RGB(30,20,10);
                    uint32_t sh = is_white ? RGB(80,60,40)    : RGB(220,200,180);
                    /* Piece shadow */
                    gui_draw_circle(px+sq/2+1, py+sq/2+1, sq/3-1, sh);
                    /* Piece body */
                    uint32_t bc = is_white ? RGB(250,240,220) : RGB(40,30,20);
                    gui_draw_circle(px+sq/2, py+sq/2, sq/3-1, bc);
                    /* Piece letter */
                    char pl[2]; pl[0]=piece_ch[piece]; pl[1]=0;
                    vga_draw_char_trans(px+sq/2-4, py+sq/2-5, pl[0], pc);
                }
            }
        }
        vga_draw_rect_outline(bx,by,bsz,bsz,RGB(80,50,20));
        /* Status bar */
        int sb_y = by+bsz+4;
        const char *turn_str = g_chess_white_turn ? "White to move" : "Black to move";
        if (g_chess_sel_r >= 0) {
            vga_draw_string_trans(wx+4, sb_y, "Click destination to move", g_pref_darkmode?RGB(200,200,208):RGB(60,60,70));
        } else {
            vga_draw_string_trans(wx+4, sb_y, turn_str, g_pref_darkmode?RGB(200,200,208):RGB(60,60,70));
        }
        return 1;
    }
    /* ---- Grapher ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Grapher")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t gg_bg=g_pref_darkmode?RGB(18,18,22):RGB(255,255,255);
        uint32_t gg_grid=g_pref_darkmode?RGB(40,40,48):RGB(220,220,228);
        uint32_t gg_ax=g_pref_darkmode?RGB(120,120,130):RGB(80,80,90);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,gg_bg);
        int gx=wx+(ww-2)/2, gy=cy+(wh-TITLEBAR_H)/2;
        int gw=(ww-20), gh=(wh-TITLEBAR_H-20);
        /* Grid lines */
        int gstep=20, gi;
        for(gi=-gw/2;gi<=gw/2;gi+=gstep) vga_draw_vline(gx+gi,cy+4,gh,gg_grid);
        for(gi=-gh/2;gi<=gh/2;gi+=gstep) vga_draw_hline(wx+4,gy+gi,gw,gg_grid);
        /* Axes */
        vga_draw_hline(wx+4,gy,gw,gg_ax);
        vga_draw_vline(gx,cy+4,gh,gg_ax);
        /* Plot y=sin(x) approximation */
        static const int sinvals[16]={0,9,16,19,18,12,3,-7,-15,-19,-18,-13,-4,6,14,19};
        int prev_x=-1,prev_y=-1;
        for(gi=0;gi<16;gi++){
            int px=gx-60+gi*8, py=gy-sinvals[gi]*3;
            if(prev_x>=0) vga_draw_line(prev_x,prev_y,px,py,RGB(0,122,255));
            prev_x=px; prev_y=py;
        }
        /* y=cos(x) */
        static const int cosvals[16]={19,18,12,3,-7,-15,-19,-18,-13,-4,6,14,19,18,12,3};
        prev_x=-1;
        for(gi=0;gi<16;gi++){
            int px=gx-60+gi*8, py=gy-cosvals[gi]*3;
            if(prev_x>=0) vga_draw_line(prev_x,prev_y,px,py,RGB(255,59,48));
            prev_x=px; prev_y=py;
        }
        vga_draw_string_trans(wx+8,cy+6,"y=sin(x)",RGB(0,122,255));
        vga_draw_string_trans(wx+88,cy+6,"y=cos(x)",RGB(255,59,48));
        return 1;
    }
    /* ---- Digital Color Meter ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Digital Color Meter")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t dcm_bg=g_pref_darkmode?RGB(28,28,32):RGB(244,244,248);
        uint32_t dcm_txt=g_pref_darkmode?RGB(218,218,226):RGB(20,20,28);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,dcm_bg);
        /* Preview of sampled color */
        uint32_t sampled=RGB(0,122,255);
        vga_fill_rect(wx+8,cy+8,60,40,sampled);
        vga_draw_rect_outline(wx+8,cy+8,60,40,g_pref_darkmode?RGB(70,70,78):RGB(160,160,170));
        /* Color values */
        vga_draw_string_trans(wx+76,cy+10,"R: 0",dcm_txt);
        vga_draw_string_trans(wx+76,cy+22,"G: 122",dcm_txt);
        vga_draw_string_trans(wx+76,cy+34,"B: 255",dcm_txt);
        vga_draw_hline(wx+4,cy+56,ww-8,g_pref_darkmode?RGB(50,50,58):RGB(210,210,218));
        vga_draw_string_trans(wx+8,cy+64,"Hex: #007AFF",dcm_txt);
        vga_draw_string_trans(wx+8,cy+78,"CSS: rgba(0,122,255,1.0)",g_pref_darkmode?RGB(140,140,150):RGB(100,100,110));
        vga_draw_string_trans(wx+8,cy+92,"Color Space: sRGB",g_pref_darkmode?RGB(140,140,150):RGB(100,100,110));
        return 1;
    }
    /* ---- Photo Booth ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Photo Booth")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy0 = wy + TITLEBAR_H;
        vga_fill_rect(wx+1, cy0, ww-2, wh-TITLEBAR_H, RGB(12,12,14));

        /* ---- Filter tab strip ---- */
        static const char *pb_filters[] = {"Normal","Sepia","B&W","Vintage","Comic","X-Ray"};
        static const uint32_t pb_acc[] = {
            RGB(200,200,200), RGB(210,160,80), RGB(160,160,160),
            RGB(180,130,80),  RGB(255,200,0),  RGB(100,220,255)
        };
        int nf=6, ftw=ww>nf?ww/nf:1, fty=cy0+2, fth=14;
        { int fi4;
          for (fi4=0;fi4<nf;fi4++) {
              uint32_t bg2 = (fi4==g_pb_filter) ? pb_acc[fi4] : RGB(30,30,34);
              gui_draw_rounded_rect(wx+fi4*ftw+2, fty, ftw-4, fth, 4, bg2);
              int nl3=str_len(pb_filters[fi4]);
              vga_draw_string_trans(wx+fi4*ftw+(ftw-nl3*8)/2+2, fty+3,
                  pb_filters[fi4], (fi4==g_pb_filter)?RGB(20,20,20):RGB(180,180,186));
          }
        }

        /* ---- Viewfinder ---- */
        int vx2=wx+8, vy2=cy0+fth+6, vw2=ww-16, vh2=wh-TITLEBAR_H-fth-60;
        if (vw2 < 1) vw2 = 1;
        if (vh2 < 1) vh2 = 1;
        /* Background */
        uint32_t vbg2 = RGB(18,20,22);
        if (g_pb_filter==1) vbg2=RGB(32,22,8);
        else if (g_pb_filter==2) vbg2=RGB(20,20,20);
        else if (g_pb_filter==3) vbg2=RGB(28,20,10);
        else if (g_pb_filter==4) vbg2=RGB(8,10,8);
        else if (g_pb_filter==5) vbg2=RGB(0,10,20);
        vga_fill_rect(vx2, vy2, vw2, vh2, vbg2);

        /* Scene: person silhouette */
        int fx2=vx2+vw2/2, fy2=vy2+vh2/2;
        /* Background gradient lines */
        { int li2; for(li2=0;li2<vh2;li2+=4) {
              int fill_w2 = vw2 > 2 ? vw2 - 2 : 1;
              uint32_t lc2 = RGB(li2*40/vh2+8, li2*30/vh2+8, li2*50/vh2+18);
              if (g_pb_filter==1) lc2=RGB(li2*50/vh2+20,li2*35/vh2+10,0);
              else if (g_pb_filter==2) { int g2=li2*40/vh2+10; lc2=RGB(g2,g2,g2); }
              else if (g_pb_filter==5) lc2=RGB(0,li2*60/vh2,li2*40/vh2+10);
              vga_fill_rect(vx2+1, vy2+li2, fill_w2, 4, lc2);
        }}

        /* Shoulders / body */
        uint32_t skin2=RGB(220,180,140);
        uint32_t shirt2=RGB(60,100,180);
        if (g_pb_filter==1) { skin2=RGB(180,130,60); shirt2=RGB(120,80,30); }
        else if (g_pb_filter==2) { skin2=RGB(180,180,180); shirt2=RGB(80,80,80); }
        else if (g_pb_filter==3) { skin2=RGB(170,120,50); shirt2=RGB(100,70,30); }
        else if (g_pb_filter==4) { skin2=RGB(255,220,0); shirt2=RGB(255,100,0); }
        else if (g_pb_filter==5) { skin2=RGB(20,180,220); shirt2=RGB(0,60,120); }

        /* Shirt */
        vga_fill_rect(fx2-40, fy2+18, 80, 40, shirt2);
        /* Neck */
        vga_fill_rect(fx2-8, fy2+6, 16, 16, skin2);
        /* Head */
        gui_draw_circle(fx2, fy2-14, 28, skin2);
        /* Hair */
        { int hx; for(hx=-24;hx<=24;hx+=2)
              vga_fill_rect(fx2+hx-1, fy2-14-26, 2, 12, RGB(60,40,20)); }
        /* Eyes */
        uint32_t eyec2=RGB(50,80,130);
        if (g_pb_filter==4) eyec2=RGB(255,60,0);
        else if (g_pb_filter==5) eyec2=RGB(0,255,200);
        gui_draw_circle(fx2-10, fy2-20, 4, RGB(255,255,255));
        gui_draw_circle(fx2+10, fy2-20, 4, RGB(255,255,255));
        gui_draw_circle(fx2-10, fy2-20, 3, eyec2);
        gui_draw_circle(fx2+10, fy2-20, 3, eyec2);
        gui_draw_circle(fx2-10, fy2-20, 1, RGB(10,10,10));
        gui_draw_circle(fx2+10, fy2-20, 1, RGB(10,10,10));
        /* Eyebrows */
        vga_draw_hline(fx2-14, fy2-26, 10, RGB(60,40,20));
        vga_draw_hline(fx2+4,  fy2-26, 10, RGB(60,40,20));
        /* Nose */
        vga_draw_vline(fx2, fy2-14, 8, RGB(180,130,100));
        /* Smile */
        { int si3;
          for(si3=-8;si3<=8;si3++) {
              int dy2=si3*si3/12;
              vga_fill_rect(fx2+si3, fy2-4+dy2, 2, 2, RGB(160,80,70));
          }
        }

        /* Viewfinder corners */
        uint32_t cc2=RGB(255,255,255);
        if (g_pb_filter==5) cc2=RGB(0,255,200);
        vga_draw_hline(vx2,vy2,14,cc2); vga_draw_vline(vx2,vy2,14,cc2);
        vga_draw_hline(vx2+vw2-14,vy2,14,cc2); vga_draw_vline(vx2+vw2-1,vy2,14,cc2);
        vga_draw_hline(vx2,vy2+vh2-1,14,cc2); vga_draw_vline(vx2,vy2+vh2-14,14,cc2);
        vga_draw_hline(vx2+vw2-14,vy2+vh2-1,14,cc2); vga_draw_vline(vx2+vw2-1,vy2+vh2-14,14,cc2);

        /* Filter name label */
        { const char *fn2=pb_filters[g_pb_filter];
          int fnl=str_len(fn2);
          vga_draw_string_trans(vx2+4, vy2+vh2-12, fn2, pb_acc[g_pb_filter]);
          (void)fnl;
        }

        /* Flash effect when photo taken */
        if (g_pb_flash_tick > 0) {
            uint32_t age2 = timer_ticks() - g_pb_flash_tick;
            if (age2 < 200) {
                int alpha2 = (int)((200-age2)*200/200);
                vga_fill_rect_alpha(vx2, vy2, vw2, vh2, RGB(255,255,255), (uint8_t)alpha2);
            } else {
                g_pb_flash_tick = 0;
            }
        }

        /* ---- Control bar ---- */
        int bar_y2=vy2+vh2+4;
        int bar_h2=wh-(bar_y2-wy)-4;
        vga_fill_rect(wx+1, bar_y2, ww-2, bar_h2, RGB(22,22,26));
        vga_draw_hline(wx+1, bar_y2, ww-2, RGB(50,50,56));

        /* Capture button */
        int btn_cx=wx+ww/2, btn_cy=bar_y2+bar_h2/2;
        gui_draw_circle(btn_cx, btn_cy, 16, RGB(60,60,66));
        gui_draw_circle(btn_cx, btn_cy, 12, RGB(220,50,50));
        gui_draw_circle(btn_cx, btn_cy,  9, RGB(255,80,80));

        /* Photo count */
        if (g_pb_captured > 0) {
            char cbuf[4]; cbuf[0]='0'+(g_pb_captured%10); cbuf[1]=0;
            vga_draw_string_trans(btn_cx-3, btn_cy-4, cbuf, RGB(255,255,255));
        }

        /* Thumbnail film strip (right of capture button) */
        { int ti3;
          for (ti3=0;ti3<4;ti3++) {
              int tx2=wx+ww/2+28+ti3*36, ty2=bar_y2+4;
              int th2=bar_h2-8, tw2=32;
              int fi5=g_pb_photos[ti3];
              uint32_t tbg2 = (fi5>=0) ? pb_acc[fi5] : RGB(40,40,46);
              vga_fill_rect(tx2, ty2, tw2, th2, RGB(30,30,34));
              vga_draw_rect_outline(tx2, ty2, tw2, th2, RGB(60,60,66));
              if (fi5 >= 0) {
                  /* Mini face thumbnail with filter color */
                  uint32_t msk2=RGB(180,140,100);
                  if (fi5==2) msk2=RGB(150,150,150);
                  else if (fi5==1) msk2=RGB(160,110,40);
                  else if (fi5==5) msk2=RGB(20,160,200);
                  else if (fi5==4) msk2=RGB(220,180,0);
                  gui_draw_circle(tx2+tw2/2, ty2+th2/2-4, 8, msk2);
                  vga_fill_rect(tx2+tw2/2-8, ty2+th2/2+4, 16, 8, tbg2);
                  vga_draw_string_trans(tx2+2, ty2+th2-9, pb_filters[fi5], RGB(200,200,200));
              } else {
                  vga_draw_string_trans(tx2+6, ty2+th2/2-4, "---", RGB(70,70,78));
              }
          }
        }
        /* Tip label */
        vga_draw_string_trans(wx+4, bar_y2+4, "Click filter tab or", RGB(100,100,108));
        vga_draw_string_trans(wx+4, bar_y2+14, "red button to shoot", RGB(100,100,108));
        return 1;
    }
    /* ---- SF Symbols ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "SF Symbols")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t sf_bg=g_pref_darkmode?RGB(28,28,32):RGB(248,248,252);
        uint32_t sf_acc=RGB(0,122,255);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,sf_bg);
        /* Search bar */
        vga_fill_rect(wx+8,cy+6,ww-90,16,g_pref_darkmode?RGB(40,40,46):RGB(230,230,235));
        vga_draw_rect_outline(wx+8,cy+6,ww-90,16,
            g_sfsymbols_search_focused ? sf_acc : (g_pref_darkmode?RGB(70,70,78):RGB(190,190,200)));
        vga_draw_string_trans(wx+14,cy+11,
            g_sfsymbols_search_focused ? "Search focused" : "Search symbols...",
            g_pref_darkmode?RGB(100,100,110):RGB(150,150,160));
        vga_draw_hline(wx+2,cy+26,ww-4,g_pref_darkmode?RGB(50,50,56):RGB(210,210,216));
        /* Symbol grid */
        static const char *sym_names[]={"star","heart","house","bell","cloud","gear","lock","wifi","bolt","music.note"};
        int si2; for(si2=0;si2<10;si2++) {
            int sx2=wx+10+(si2%5)*((ww-20)/5);
            int sy2=cy+30+(si2/5)*50;
            gui_draw_rounded_rect(sx2,sy2,32,32,6,g_pref_darkmode?RGB(40,40,46):RGB(235,235,240));
            vga_draw_string_trans(sx2+8,sy2+12,sym_names[si2][0]=='s'?"*":"o",sf_acc);
            vga_draw_string_trans(sx2,sy2+36,sym_names[si2],g_pref_darkmode?RGB(120,120,130):RGB(100,100,110));
        }
        return 1;
    }
    /* ---- Transporter ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Transporter")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t tp_bg=g_pref_darkmode?RGB(28,28,32):RGB(248,248,252);
        uint32_t tp_txt=g_pref_darkmode?RGB(218,218,226):RGB(20,20,28);
        uint32_t tp_sub=g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        uint32_t tp_acc=RGB(40,120,200);
        int progress = 0;
        char status[64];
        int sp = 0;
        if (g_transporter_uploading) {
            uint32_t elapsed = timer_ticks() - g_transporter_upload_start_tick;
            progress = (int)(elapsed / 30U);
            if (progress >= 100) {
                progress = 100;
                g_transporter_uploading = 0;
            }
        } else if (g_transporter_upload_count > 0) {
            progress = 100;
        }
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,tp_bg);
        vga_draw_string_trans(wx+10,cy+8,"Transporter",tp_txt);
        vga_draw_hline(wx+2,cy+20,ww-4,g_pref_darkmode?RGB(50,50,56):RGB(215,215,220));
        /* Upload section */
        vga_fill_rect_alpha(wx+10,cy+28,ww-20,60,tp_acc,30);
        vga_draw_rect_outline(wx+10,cy+28,ww-20,60,tp_acc);
        if (g_transporter_uploading) {
            vga_draw_string_trans(wx+(ww-136)/2,cy+50,"Uploading MyApp_v2.1.ipa",tp_txt);
            vga_draw_string_trans(wx+(ww-112)/2,cy+64,"click to restart",tp_sub);
        } else if (g_transporter_upload_count > 0) {
            vga_draw_string_trans(wx+(ww-152)/2,cy+50,"Upload complete",tp_txt);
            vga_draw_string_trans(wx+(ww-128)/2,cy+64,"click to send again",tp_sub);
        } else {
            vga_draw_string_trans(wx+(ww-88)/2,cy+50,"Drop .ipa here",tp_sub);
            vga_draw_string_trans(wx+(ww-120)/2,cy+64,"or click to browse",tp_sub);
        }
        /* Progress */
        vga_draw_string_trans(wx+10,cy+96,
                              (g_transporter_uploading || g_transporter_upload_count > 0) ?
                              "MyApp_v2.1.ipa" : "No package selected",
                              (g_transporter_uploading || g_transporter_upload_count > 0) ? tp_txt : tp_sub);
        vga_fill_rect(wx+10,cy+110,ww-20,8,g_pref_darkmode?RGB(40,40,46):RGB(220,220,226));
        if (progress > 0)
            vga_fill_rect(wx+10,cy+110,(ww-20)*progress/100,8,tp_acc);
        status[0] = 0;
        if (progress >= 100) {
            apps4_append_text(status, &sp, sizeof(status), "Uploaded to App Store Connect");
        } else if (g_transporter_uploading) {
            apps4_append_text(status, &sp, sizeof(status), "Uploading to App Store Connect... ");
            apps4_append_uint(status, &sp, sizeof(status), (uint32_t)progress);
            apps4_append_text(status, &sp, sizeof(status), "%");
        } else {
            apps4_append_text(status, &sp, sizeof(status), "Waiting for package");
        }
        vga_draw_string_trans(wx+10,cy+122,status,tp_sub);
        return 1;
    }
    /* ---- AR Quick Look ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "AR Quick Look")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,RGB(15,15,20));
        /* AR viewport */
        int vw2=ww-2, vh2=wh-TITLEBAR_H-30;
        vga_fill_rect(wx+1,cy,vw2,vh2,RGB(20,25,35));
        /* Grid floor */
        int gcx=wx+ww/2, gcy=cy+vh2*2/3;
        int gi; for(gi=-4;gi<=4;gi++){
            vga_draw_line(gcx+gi*20,gcy,gcx+gi*20-30,gcy+30,RGB(40,60,80));
            vga_draw_line(gcx-80+gi*20,gcy+30,gcx+80+gi*20-80,gcy+30,RGB(40,60,80));
        }
        /* 3D object - simple box wireframe */
        int ox=gcx-25, oy=gcy-50;
        /* Front face */
        vga_draw_rect_outline(ox,oy,50,40,RGB(100,200,255));
        /* Top face */
        vga_draw_line(ox,oy,ox+20,oy-15,RGB(100,200,255));
        vga_draw_line(ox+50,oy,ox+70,oy-15,RGB(100,200,255));
        vga_draw_hline(ox+20,oy-15,50,RGB(100,200,255));
        /* Right face */
        vga_draw_line(ox+50,oy,ox+70,oy-15,RGB(80,160,200));
        vga_draw_line(ox+50,oy+40,ox+70,oy+25,RGB(80,160,200));
        vga_draw_vline(ox+70,oy-15,40,RGB(80,160,200));
        /* AR label */
        vga_draw_string_trans(wx+8,cy+vh2+6,"AR",RGB(100,200,255));
        vga_draw_string_trans(wx+26,cy+vh2+6,"model.usdz",RGB(180,180,190));
        /* Share button */
        gui_draw_rounded_rect(wx+ww-70,cy+vh2+4,60,18,5,RGB(0,122,255));
        vga_draw_string_trans(wx+ww-62,cy+vh2+9,"Share",RGB(255,255,255));
        return 1;
    }
    /* ---- Feedback Assistant ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Feedback Assistant")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t fa_bg=g_pref_darkmode?RGB(28,28,32):RGB(250,250,254);
        uint32_t fa_txt=g_pref_darkmode?RGB(218,218,226):RGB(20,20,28);
        uint32_t fa_sub=g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        uint32_t fa_acc=RGB(0,122,255);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,fa_bg);
        vga_draw_string_trans(wx+10,cy+8,"Submit Feedback",fa_txt);
        vga_draw_hline(wx+2,cy+20,ww-4,g_pref_darkmode?RGB(50,50,56):RGB(215,215,220));
        vga_draw_string_trans(wx+10,cy+30,"Category:",fa_sub);
        gui_draw_rounded_rect(wx+80,cy+26,ww-90,16,4,g_pref_darkmode?RGB(40,40,46):RGB(230,230,235));
        vga_draw_string_trans(wx+86,cy+31,"Incorrect/Unexpected Behavior",fa_txt);
        vga_draw_string_trans(wx+10,cy+50,"Title:",fa_sub);
        vga_fill_rect(wx+10,cy+62,ww-20,16,g_pref_darkmode?RGB(35,35,40):RGB(238,238,243));
        vga_draw_rect_outline(wx+10,cy+62,ww-20,16,fa_acc);
        vga_draw_string_trans(wx+14,cy+67,"Dark mode flickers on wake",fa_txt);
        vga_draw_string_trans(wx+10,cy+86,"Steps to reproduce:",fa_sub);
        vga_fill_rect(wx+10,cy+98,ww-20,50,g_pref_darkmode?RGB(35,35,40):RGB(238,238,243));
        vga_draw_rect_outline(wx+10,cy+98,ww-20,50,g_pref_darkmode?RGB(60,60,68):RGB(200,200,210));
        vga_draw_string_trans(wx+14,cy+102,"1. Open System Settings",fa_txt);
        vga_draw_string_trans(wx+14,cy+114,"2. Toggle Dark Mode",fa_txt);
        if (g_feedback_submissions > 0) {
            char fa_count[16];
            char fa_line[40];
            int fp = 0;
            runtime_format_uint((uint32_t)g_feedback_submissions, fa_count, sizeof(fa_count));
            fa_line[0] = 0;
            apps4_append_text(fa_line, &fp, sizeof(fa_line), "Submitted reports: ");
            apps4_append_text(fa_line, &fp, sizeof(fa_line), fa_count);
            vga_draw_string_trans(wx+10, cy+154, fa_line, RGB(52,199,89));
        }
        /* Submit button */
        gui_draw_rounded_rect(wx+ww-96,cy+wh-TITLEBAR_H-24,86,16,5,
                              g_feedback_submissions>0?RGB(52,199,89):fa_acc);
        vga_draw_string_trans(wx+ww-88,cy+wh-TITLEBAR_H-19,
                              g_feedback_submissions>0?"Submitted":"Submit",RGB(255,255,255));
        return 1;
    }

    /* ---- Clips ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Clips")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,RGB(15,15,18));
        /* Viewfinder */
        int vw3=ww-2, vh3=(wh-TITLEBAR_H-40);
        vga_fill_rect(wx+1,cy,vw3,vh3,RGB(25,25,30));
        /* Selfie graphic */
        int cx3=wx+ww/2, cy3=cy+vh3/2;
        gui_draw_circle(cx3,cy3-8,22,RGB(200,170,140));
        gui_draw_circle(cx3-8,cy3-12,4,RGB(60,90,120));
        gui_draw_circle(cx3+8,cy3-12,4,RGB(60,90,120));
        vga_draw_hline(cx3-6,cy3,12,RGB(160,100,80));
        /* AR sticker (star) */
        vga_draw_string_trans(cx3+20,cy3-20,"*",RGB(255,220,0));
        vga_draw_string_trans(cx3+28,cy3-24,"FUN",RGB(255,180,0));
        /* Record button area */
        int rbar_y=cy+vh3+4;
        vga_fill_rect(wx+1,rbar_y,ww-2,34,RGB(20,20,24));
        gui_draw_circle(cx3,rbar_y+17,14,RGB(255,59,48));
        gui_draw_circle(cx3,rbar_y+17,10,RGB(255,100,100));
        gui_draw_circle(wx+ww-30,rbar_y+17,10,RGB(44,44,50));
        vga_draw_string_trans(wx+14,rbar_y+13,"CLIPS",RGB(100,100,110));
        return 1;
    }
    /* ---- Transmit ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Transmit")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t tr_bg=g_pref_darkmode?RGB(28,28,32):RGB(248,248,252);
        uint32_t tr_txt=g_pref_darkmode?RGB(218,218,226):RGB(20,20,28);
        uint32_t tr_sub=g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        uint32_t tr_acc=RGB(255,149,0);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,tr_bg);
        /* Two-pane FTP client */
        int pw2=(ww-3)/2;
        vga_draw_vline(wx+pw2,cy,wh-TITLEBAR_H,g_pref_darkmode?RGB(50,50,56):RGB(210,210,218));
        /* Left (local) pane */
        vga_fill_rect(wx+1,cy,pw2-1,20,g_pref_darkmode?RGB(36,36,42):RGB(232,232,238));
        vga_draw_string_trans(wx+6,cy+6,"Local",tr_txt);
        const char *lfiles[]={"Desktop/","Documents/","Downloads/","myapp.zip"};
        int li; for(li=0;li<4&&li*18<wh-TITLEBAR_H-30;li++){
            vga_draw_string_trans(wx+8,cy+24+li*18,lfiles[li],li<3?tr_acc:tr_txt);
        }
        /* Right (remote) pane */
        vga_fill_rect(wx+pw2+1,cy,pw2-1,20,g_pref_darkmode?RGB(36,36,42):RGB(232,232,238));
        vga_draw_string_trans(wx+pw2+6,cy+6,"s3://my-bucket",tr_sub);
        const char *rfiles[]={"uploads/","logs/","config.json","README.md"};
        for(li=0;li<4&&li*18<wh-TITLEBAR_H-30;li++){
            vga_draw_string_trans(wx+pw2+8,cy+24+li*18,rfiles[li],li<2?tr_acc:tr_txt);
        }
        /* Transfer button */
        int mid_x=wx+pw2-16, mid_y=cy+(wh-TITLEBAR_H)/2-10;
        gui_draw_rounded_rect(mid_x,mid_y,32,20,5,tr_acc);
        vga_draw_string_trans(mid_x+8,mid_y+6,">>",RGB(255,255,255));
        return 1;
    }
    /* ---- Proxyman ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Proxyman")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t pm_bg=RGB(18,22,28);
        uint32_t pm_txt=RGB(200,210,220);
        uint32_t pm_sub=RGB(100,120,140);
        uint32_t pm_acc=RGB(52,199,89);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,pm_bg);
        /* Sidebar */
        vga_fill_rect(wx+1,cy,80,wh-TITLEBAR_H,RGB(14,18,24));
        vga_draw_string_trans(wx+4,cy+8,"Sessions",pm_sub);
        const char *hosts[]={"api.github","s.apple.com","maps.google","cdn.fast.com"};
        int hi; for(hi=0;hi<4&&hi*20<wh-TITLEBAR_H-20;hi++){
            if(hi==0) vga_fill_rect(wx+2,cy+22+hi*20,78,18,RGB(30,40,55));
            vga_draw_string_trans(wx+4,cy+26+hi*20,hosts[hi],hi==0?pm_acc:pm_sub);
        }
        /* Request/response table */
        vga_draw_string_trans(wx+88,cy+8,"Request",pm_acc);
        vga_draw_string_trans(wx+88,cy+20,"GET /api/v3/users",pm_txt);
        { char resp[24];
          int rp = 0;
          apps4_append_text(resp, &rp, sizeof(resp), "200 OK  ");
          apps4_format_uint_suffix(80U + ((timer_ticks() / 100U) % 180U), "ms", resp + rp, (int)sizeof(resp) - rp);
          vga_draw_string_trans(wx+88,cy+32,resp,RGB(52,199,89)); }
        vga_draw_hline(wx+84,cy+44,ww-88,RGB(30,40,55));
        vga_draw_string_trans(wx+88,cy+52,"Headers",pm_sub);
        const char *hdrs[]={"Content-Type: application/json","Authorization: Bearer ***","Accept: */*"};
        for(hi=0;hi<3&&hi*14<wh-TITLEBAR_H-70;hi++)
            vga_draw_string_trans(wx+88,cy+64+hi*14,hdrs[hi],pm_txt);
        return 1;
    }
    /* ---- Overflow 3 ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Overflow 3")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        uint32_t ov_bg=g_pref_darkmode?RGB(28,28,32):RGB(248,248,252);
        uint32_t ov_txt=g_pref_darkmode?RGB(218,218,226):RGB(20,20,28);
        uint32_t ov_sub=g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        vga_fill_rect(wx+1,cy,ww-2,wh-TITLEBAR_H,ov_bg);
        vga_draw_string_trans(wx+8,cy+8,"Overflow 3 — App Launcher",ov_txt);
        vga_draw_hline(wx+2,cy+20,ww-4,g_pref_darkmode?RGB(50,50,56):RGB(215,215,220));
        /* App grid (3 columns) */
        static const char *ov_apps[]={"Xcode","Terminal","Sketch","Figma","Sublime","VS Code","Tower","Sequel","Paw","Dash"};
        static const uint32_t ov_cols[]={RGB(30,120,255),RGB(30,30,30),RGB(255,100,100),RGB(150,100,255),RGB(255,150,0),RGB(0,120,215),RGB(60,40,140),RGB(0,160,220),RGB(255,80,80),RGB(60,60,200)};
        int ai; for(ai=0;ai<10;ai++){
            int ax2=wx+8+(ai%4)*(ww/4), ay2=cy+28+(ai/4)*56;
            gui_draw_rounded_rect(ax2,ay2,40,40,8,ov_cols[ai]);
            { char icon[2]; icon[0]=ov_apps[ai][0]; icon[1]=0;
              vga_draw_string_trans(ax2+16,ay2+16,icon,RGB(255,255,255)); }
            int nl=str_len(ov_apps[ai]); if(nl>5)nl=5;
            vga_draw_string_trans(ax2+(40-nl*8)/2,ay2+44,ov_apps[ai],ov_sub);
        }
        return 1;
    }

    /* ---- iStudiez Pro ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "iStudiez Pro")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t is_bg=RGB(24,24,28); uint32_t is_txt=RGB(220,220,228); uint32_t is_sub=RGB(120,120,130);
        uint32_t is_acc=RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, is_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H, 80, wh-TITLEBAR_H, RGB(28,28,32));
        vga_draw_vline(wx+80, wy+TITLEBAR_H, wh-TITLEBAR_H, RGB(50,50,56));
        static const char *is_tabs[]={"Today","Week","Grades","Assign"};
        int it;
        for(it=0;it<4;it++){
            int ty2=wy+TITLEBAR_H+10+it*30;
            if(it==0) vga_fill_rect(wx+1, ty2-4, 79, 22, RGB(38,38,44));
            vga_draw_string_trans(wx+8, ty2, is_tabs[it], it==0?is_acc:is_sub);
        }
        /* Today view */
        { datetime_t is_now;
          char is_header[32];
          int ihp = 0;
          get_current_datetime(&is_now);
          is_header[0] = 0;
          apps4_append_text(is_header, &ihp, sizeof(is_header), "Classes - ");
          apps4_append_text(is_header, &ihp, sizeof(is_header), datetime_weekday_long(is_now.weekday));
          vga_draw_string_trans(wx+88, wy+TITLEBAR_H+8, is_header, is_txt); }
        vga_draw_hline(wx+82, wy+TITLEBAR_H+22, ww-84, RGB(50,50,56));
        static const char *is_cls[]={"Math 101","Physics","CS 301"};
        static const int is_offsets[]={30,120,210};
        static const uint32_t is_cc[]={RGB(0,122,255),RGB(220,50,50),RGB(52,199,89)};
        for(it=0;it<3;it++){
            int cy2=wy+TITLEBAR_H+30+it*40;
            datetime_t is_now2;
            char is_row[32];
            char is_time[8];
            int irp = 0;
            get_current_datetime(&is_now2);
            apps4_format_time_24h_from_minutes(is_now2.hour * 60 + is_now2.minute + is_offsets[it], is_time, sizeof(is_time));
            is_row[0] = 0;
            apps4_append_text(is_row, &irp, sizeof(is_row), is_cls[it]);
            apps4_append_text(is_row, &irp, sizeof(is_row), "  ");
            apps4_append_text(is_row, &irp, sizeof(is_row), is_time);
            vga_fill_rect(wx+82, cy2, 4, 30, is_cc[it]);
            vga_fill_rect(wx+88, cy2, ww-92, 30, RGB(34,34,38));
            vga_draw_string_trans(wx+96, cy2+10, is_row, is_txt);
        }
        return 1;
    }

    /* ---- Lasso ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Lasso")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t la_bg=RGB(22,22,28); uint32_t la_txt=RGB(218,218,226);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, la_bg);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Lasso - App License Manager", la_txt);
        vga_draw_hline(wx+2, wy+TITLEBAR_H+22, ww-4, RGB(50,50,56));
        static const char *la_apps[]={"Xcode","Sketch","Nova","BBEdit","Transmit","Kaleidoscope"};
        static const char *la_lic[]={"Free","Licensed","Licensed","Licensed","Licensed","Unlicensed"};
        static const uint32_t la_sc[]={RGB(52,199,89),RGB(52,199,89),RGB(52,199,89),RGB(52,199,89),RGB(52,199,89),RGB(255,59,48)};
        int li;
        for(li=0;li<6;li++){
            int ly2=wy+TITLEBAR_H+30+li*28;
            vga_fill_rect(wx+2, ly2, ww-4, 26, RGB(30,30,36));
            vga_draw_hline(wx+2, ly2+26, ww-4, RGB(44,44,50));
            gui_draw_circle(wx+16, ly2+13, 5, la_sc[li]);
            vga_draw_string_trans(wx+28, ly2+8, la_apps[li], la_txt);
            vga_draw_string_trans(wx+ww-80, ly2+8, la_lic[li], la_sc[li]);
        }
        return 1;
    }

    /* ---- 1Password ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "1Password")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t pw_bg=RGB(20,24,32); uint32_t pw_txt=RGB(220,225,235); uint32_t pw_sub=RGB(120,130,145);
        uint32_t pw_acc=RGB(0,120,200);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, pw_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H, 90, wh-TITLEBAR_H, RGB(24,28,38));
        vga_draw_vline(wx+90, wy+TITLEBAR_H, wh-TITLEBAR_H, RGB(40,48,60));
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Vaults", pw_sub);
        static const char *pw_vaults[]={"All Items","Login","Cards","Notes","Identity"};
        int vi;
        for(vi=0;vi<5;vi++){
            int vy2=wy+TITLEBAR_H+24+vi*22;
            if(vi==0) vga_fill_rect(wx+2, vy2-2, 87, 20, RGB(30,60,100));
            vga_draw_string_trans(wx+8, vy2+2, pw_vaults[vi], vi==0?pw_txt:pw_sub);
        }
        /* Items list */
        vga_draw_string_trans(wx+98, wy+TITLEBAR_H+8, "All Items (142)", pw_sub);
        vga_draw_hline(wx+92, wy+TITLEBAR_H+22, ww-94, RGB(40,48,60));
        /* Search bar */
        gui_draw_rounded_rect(wx+92, wy+TITLEBAR_H+26, ww-96, 20, 4,
                              g_onepassword_search_focused ? RGB(0,60,105) : RGB(30,36,48));
        vga_draw_string_trans(wx+100, wy+TITLEBAR_H+32,
                              g_onepassword_search_focused ? "Search focused" : "Search...", pw_sub);
        static const char *pw_items[]={"GitHub","Google","Apple ID","Netflix","Amazon","Spotify"};
        for(vi=0;vi<6;vi++){
            int iy2=wy+TITLEBAR_H+52+vi*26;
            vga_fill_rect(wx+92, iy2, ww-94, 24, RGB(26,30,42));
            vga_draw_hline(wx+92, iy2+24, ww-94, RGB(36,42,56));
            gui_draw_rounded_rect(wx+96, iy2+4, 16, 16, 3, pw_acc);
            vga_draw_string_trans(wx+100, iy2+9, "p", RGB(255,255,255));
            vga_draw_string_trans(wx+118, iy2+4, pw_items[vi], pw_txt);
            vga_draw_string_trans(wx+118, iy2+14, "user@email.com", pw_sub);
        }
        return 1;
    }

    /* ---- Fantastical ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Fantastical")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t fa_bg=RGB(22,22,26); uint32_t fa_txt=RGB(220,220,228); uint32_t fa_sub=RGB(120,120,132);
        uint32_t fa_acc=RGB(220,40,40);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, fa_bg);
        /* Month header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, 28, RGB(30,30,34));
        vga_draw_hline(wx+1, wy+TITLEBAR_H+28, ww-2, RGB(44,44,50));
        { datetime_t fa_now;
          char fa_year[8];
          int fa_month_len;
          int fa_first_wd;
          get_current_datetime(&fa_now);
          runtime_format_uint((uint32_t)fa_now.year, fa_year, sizeof(fa_year));
          vga_draw_string_trans(wx+(ww-96)/2, wy+TITLEBAR_H+8, datetime_month_long(fa_now.month), fa_acc);
          vga_draw_string_trans(wx+(ww-96)/2+64, wy+TITLEBAR_H+8, fa_year, fa_txt);
        /* Day headers */
        static const char *fa_days[]={"S","M","T","W","T","F","S"};
        int cell_w=(ww-2)/7; int fi;
        for(fi=0;fi<7;fi++) vga_draw_string_trans(wx+2+fi*cell_w+cell_w/2-4, wy+TITLEBAR_H+34, fa_days[fi], fa_sub);
        fa_month_len = datetime_days_in_month(fa_now.year, fa_now.month);
        fa_first_wd = datetime_day_of_week(fa_now.year, fa_now.month, 1);
        int row2,col2;
        for(row2=0;row2<6;row2++){
            for(col2=0;col2<7;col2++){
                int day = row2*7+col2-fa_first_wd+1;
                int cx2=wx+2+col2*cell_w, cy2=wy+TITLEBAR_H+46+row2*18;
                char ds[4];
                if(day<1 || day>fa_month_len) continue;
                if(day==fa_now.day){ gui_draw_circle(cx2+cell_w/2, cy2+7, 7, fa_acc); }
                runtime_format_uint((uint32_t)day, ds, sizeof(ds));
                vga_draw_string_trans(cx2+cell_w/2-4, cy2+3, ds, day==fa_now.day?RGB(255,255,255):fa_txt);
            }
        }
        /* Events panel */
        int ev_y=wy+TITLEBAR_H+140;
        vga_draw_hline(wx+2, ev_y, ww-4, RGB(44,44,50));
        vga_draw_string_trans(wx+8, ev_y+6, "Events", fa_sub);
        static const char *fa_evts[]={"Team Standup","Design Review","1:1 with PM"};
        static const int fa_offsets[]={15,120,240};
        static const uint32_t fa_ecol[]={RGB(0,122,255),RGB(52,199,89),RGB(255,149,0)};
        for(fi=0;fi<3;fi++){
            int ey2=ev_y+22+fi*22;
            char fa_row[32];
            char fa_time[8];
            int frp = 0;
            apps4_format_time_24h_from_minutes(fa_now.hour * 60 + fa_now.minute + fa_offsets[fi], fa_time, sizeof(fa_time));
            fa_row[0] = 0;
            apps4_append_text(fa_row, &frp, sizeof(fa_row), fa_time);
            apps4_append_text(fa_row, &frp, sizeof(fa_row), " ");
            apps4_append_text(fa_row, &frp, sizeof(fa_row), fa_evts[fi]);
            vga_fill_rect(wx+6, ey2, 3, 16, fa_ecol[fi]);
            vga_draw_string_trans(wx+12, ey2+4, fa_row, fa_txt);
        }
        }
        return 1;
    }

    /* ---- Things 3 ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Things 3")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t th_bg=RGB(24,24,28); uint32_t th_txt=RGB(218,218,226); uint32_t th_sub=RGB(120,120,130);
        uint32_t th_acc=RGB(100,60,200);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, th_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H, 90, wh-TITLEBAR_H, RGB(28,28,32));
        vga_draw_vline(wx+90, wy+TITLEBAR_H, wh-TITLEBAR_H, RGB(44,44,50));
        static const char *th_sec[]={"Inbox","Today","Upcoming","Anytime","Someday"};
        static const uint32_t th_sc[]={RGB(255,149,0),RGB(220,40,40),RGB(0,122,255),RGB(52,199,89),RGB(120,120,130)};
        int ti2;
        for(ti2=0;ti2<5;ti2++){
            int sy=wy+TITLEBAR_H+10+ti2*24;
            gui_draw_circle(wx+14, sy+7, 5, th_sc[ti2]);
            vga_draw_string_trans(wx+24, sy+3, th_sec[ti2], ti2==1?th_txt:th_sub);
        }
        /* Tasks */
        vga_draw_string_trans(wx+98, wy+TITLEBAR_H+8, "Today", th_acc);
        vga_draw_hline(wx+92, wy+TITLEBAR_H+22, ww-94, RGB(44,44,50));
        static const char *th_tasks[]={"Review design drafts","Write weekly report","Call with client","Update dependencies","Read article on SwiftUI","Plan sprint tasks"};
        static const int th_done[]={1,1,0,0,0,1};
        for(ti2=0;ti2<6;ti2++){
            int ty2=wy+TITLEBAR_H+30+ti2*26;
            if(th_done[ti2]) gui_draw_circle(wx+104, ty2+10, 7, th_acc);
            else { gui_draw_circle(wx+104, ty2+10, 7, RGB(50,50,60)); gui_draw_circle(wx+104, ty2+10, 5, th_bg); }
            vga_draw_string_trans(wx+118, ty2+6, th_tasks[ti2], th_done[ti2]?th_sub:th_txt);
        }
        return 1;
    }

    /* ---- Raycast ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Raycast")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t rc_bg=RGB(18,18,22); uint32_t rc_txt=RGB(230,230,238); uint32_t rc_sub=RGB(120,120,132);
        uint32_t rc_acc=RGB(255,90,30);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, rc_bg);
        /* Search bar */
        gui_draw_rounded_rect(wx+8, wy+TITLEBAR_H+10, ww-16, 30, 8,
                              g_raycast_search_focused ? RGB(50,36,34) : RGB(28,28,34));
        vga_draw_string_trans(wx+20, wy+TITLEBAR_H+22,
                              g_raycast_search_focused ? "> Search focused" : "> Search commands...", rc_sub);
        vga_draw_hline(wx+2, wy+TITLEBAR_H+46, ww-4, RGB(36,36,44));
        /* Command results */
        static const char *rc_cmds[]={"Calculator","File Search","Clipboard History","Window Management","System: Sleep","App Launcher"};
        static const char *rc_cats[]={"Built-in","Extension","Extension","Extension","System","Built-in"};
        static const uint32_t rc_icols[]={RGB(200,50,50),RGB(0,122,255),RGB(52,199,89),RGB(255,149,0),RGB(120,120,130),RGB(220,40,220)};
        int ri2;
        for(ri2=0;ri2<6;ri2++){
            int ry2=wy+TITLEBAR_H+52+ri2*26;
            if(ri2==0) vga_fill_rect(wx+2, ry2-2, ww-4, 28, RGB(30,30,40));
            gui_draw_rounded_rect(wx+10, ry2+3, 18, 18, 4, rc_icols[ri2]);
            { char cmd_icon[2]; cmd_icon[0] = rc_cmds[ri2][0]; cmd_icon[1] = 0;
              vga_draw_string_trans(wx+14, ry2+9, cmd_icon, RGB(255,255,255)); }
            vga_draw_string_trans(wx+34, ry2+5, rc_cmds[ri2], ri2==0?rc_acc:rc_txt);
            vga_draw_string_trans(wx+ww-64, ry2+8, rc_cats[ri2], rc_sub);
        }
        vga_draw_string_trans(wx+8, wy+wh-18, "Tip: Press Tab to view actions", rc_sub);
        return 1;
    }

    /* ---- Tot ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Tot")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t to_bg=RGB(20,20,24); uint32_t to_txt=RGB(220,220,228);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, to_bg);
        /* 7 dot tabs */
        static const uint32_t to_cols[]={RGB(255,59,48),RGB(255,149,0),RGB(255,204,0),RGB(52,199,89),RGB(0,122,255),RGB(100,60,200),RGB(255,45,85)};
        int di;
        for(di=0;di<7;di++){
            int dx2=wx+6+di*(ww-12)/7;
            gui_draw_circle(dx2+(ww-12)/14, wy+TITLEBAR_H+10, 6, to_cols[di]);
        }
        vga_draw_hline(wx+2, wy+TITLEBAR_H+24, ww-4, RGB(36,36,44));
        /* Text area for active dot (dot 2 - orange) */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+24, ww-2, wh-TITLEBAR_H-24, RGB(22,22,26));
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+32, "Meeting notes:", to_txt);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+46, "- Discuss Q3 roadmap", to_txt);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+60, "- Review open PRs", to_txt);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+74, "- Plan hiring", to_txt);
        /* Blinking cursor */
        vga_fill_rect(wx+8, wy+TITLEBAR_H+88, 2, 10, to_cols[1]);
        return 1;
    }

    /* ---- Klokki ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Klokki")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t kl_bg=RGB(20,24,20); uint32_t kl_txt=RGB(218,228,218); uint32_t kl_sub=RGB(100,130,100);
        uint32_t kl_acc=RGB(52,199,89);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, kl_bg);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Time Tracker", kl_acc);
        vga_draw_hline(wx+2, wy+TITLEBAR_H+22, ww-4, RGB(36,48,36));
        /* Active timer */
        gui_draw_rounded_rect(wx+4, wy+TITLEBAR_H+28, ww-8, 40, 6, RGB(28,38,28));
        gui_draw_circle(wx+20, wy+TITLEBAR_H+48, 8, kl_acc);
        vga_draw_string_trans(wx+32, wy+TITLEBAR_H+34, "MyOS Development", kl_txt);
        { char kl_active[12];
          apps4_format_hms((timer_ticks() / 1000U) % (24U * 3600U), kl_active, sizeof(kl_active));
          vga_draw_string_trans(wx+32, wy+TITLEBAR_H+48, kl_active, kl_acc); }
        vga_draw_string_trans(wx+ww-32, wy+TITLEBAR_H+44, "[]", RGB(200,50,50));
        /* Project list */
        vga_draw_hline(wx+2, wy+TITLEBAR_H+72, ww-4, RGB(36,48,36));
        static const char *kl_proj[]={"MyOS Dev","Design Work","Meeting","Admin"};
        static const int kl_base_minutes[]={210,90,45,20};
        static const uint32_t kl_col[]={RGB(52,199,89),RGB(0,122,255),RGB(255,149,0),RGB(120,120,130)};
        int ki;
        for(ki=0;ki<4;ki++){
            int ky2=wy+TITLEBAR_H+78+ki*26;
            char kl_time[16];
            runtime_format_minutes(kl_base_minutes[ki] + (int)((timer_ticks() / 60000U) % 45U), kl_time, sizeof(kl_time));
            vga_fill_rect(wx+6, ky2+6, 10, 10, kl_col[ki]);
            vga_draw_string_trans(wx+20, ky2+7, kl_proj[ki], kl_txt);
            vga_draw_string_trans(wx+ww-56, ky2+7, kl_time, kl_sub);
        }
        return 1;
    }

    /* ---- Bear ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Bear")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t br_bg=RGB(24,20,16); uint32_t br_txt=RGB(228,218,206); uint32_t br_sub=RGB(130,118,104);
        uint32_t br_acc=RGB(220,80,40);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, br_bg);
        /* Sidebar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H, 80, wh-TITLEBAR_H, RGB(28,24,18));
        vga_draw_vline(wx+80, wy+TITLEBAR_H, wh-TITLEBAR_H, RGB(48,40,32));
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Notes", br_sub);
        static const char *br_notes[]={"Dev Ideas","Meeting","Tasks","Journal","Recipes"};
        int bi2;
        for(bi2=0;bi2<5;bi2++){
            int by2=wy+TITLEBAR_H+24+bi2*22;
            if(bi2==0) vga_fill_rect(wx+2, by2-2, 77, 20, RGB(38,30,22));
            vga_draw_string_trans(wx+8, by2+2, br_notes[bi2], bi2==0?br_acc:br_sub);
        }
        /* Note content */
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+10, "# Dev Ideas", br_acc);
        vga_draw_hline(wx+82, wy+TITLEBAR_H+24, ww-84, RGB(48,40,32));
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+32, "## OS Features", br_txt);
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+46, "- Better Spotlight", br_txt);
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+60, "- Notification Center", br_txt);
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+74, "- Lock screen", br_txt);
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+88, "- Dynamic Island", br_txt);
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+102, "## Progress", br_txt);
        vga_draw_string_trans(wx+90, wy+TITLEBAR_H+116, "Track progress in tasks", br_acc);
        return 1;
    }

    /* ---- Reeder 5 ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Reeder 5")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t re_bg=RGB(22,22,26); uint32_t re_txt=RGB(220,220,228); uint32_t re_sub=RGB(120,120,132);
        uint32_t re_acc=RGB(220,50,50);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, re_bg);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, 80, wh-TITLEBAR_H, RGB(26,26,30));
        vga_draw_vline(wx+80, wy+TITLEBAR_H, wh-TITLEBAR_H, RGB(44,44,50));
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Feeds", re_sub);
        static const char *re_feeds[]={"All","Hacker News","Ars Technica","The Verge","9to5Mac"};
        int ri3;
        for(ri3=0;ri3<5;ri3++){
            int fy=wy+TITLEBAR_H+24+ri3*22;
            if(ri3==0) vga_fill_rect(wx+2, fy-2, 77, 20, RGB(34,34,40));
            vga_draw_string_trans(wx+8, fy+2, re_feeds[ri3], ri3==0?re_txt:re_sub);
            if(ri3>0){ char nc[3]; nc[0]='0'+(ri3*3%9+1); nc[1]=0; vga_draw_string_trans(wx+60, fy+2, nc, re_acc); }
        }
        /* Article list */
        vga_draw_string_trans(wx+88, wy+TITLEBAR_H+8, "Hacker News", re_acc);
        vga_draw_hline(wx+82, wy+TITLEBAR_H+22, ww-84, RGB(44,44,50));
        static const char *re_arts[]={"Ask HN: What are you building?","Show HN: My bare-metal OS","New features in Swift 6","Apple plans ARM Mac Pro","WASM is eating the world"};
        for(ri3=0;ri3<5;ri3++){
            int ay2=wy+TITLEBAR_H+28+ri3*28;
            vga_fill_rect(wx+82, ay2, ww-84, 26, RGB(26,26,32));
            vga_draw_hline(wx+82, ay2+26, ww-84, RGB(40,40,48));
            vga_draw_string_trans(wx+90, ay2+5, re_arts[ri3], re_txt);
            {
                char re_age[24];
                runtime_format_relative_time((uint32_t)(7200U + (uint32_t)ri3 * 1800U), re_age, sizeof(re_age));
                vga_draw_string_trans(wx+90, ay2+17, re_age, re_sub);
            }
        }
        return 1;
    }

    /* ---- CleanMyMac X ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "CleanMyMac X")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t cm_bg=RGB(16,22,16); uint32_t cm_txt=RGB(218,228,218);
        uint32_t cm_acc=RGB(0,200,120);
        runtime_storage_info_t cm_storage;
        runtime_system_info_t cm_sys;
        int cm_has_storage;
        int cm_health;
        runtime_get_system_info(&cm_sys);
        cm_has_storage = (runtime_get_storage_info("/", &cm_storage) == 0);
        cm_health = cm_has_storage ? 100 - cm_storage.used_percent : 100 - cm_sys.mem_used_percent;
        if (cm_health < 0) cm_health = 0;
        if (cm_health > 100) cm_health = 100;
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, cm_bg);
        vga_draw_string_trans(wx+(ww-96)/2, wy+TITLEBAR_H+8, "CleanMyMac X", cm_acc);
        vga_draw_hline(wx+2, wy+TITLEBAR_H+22, ww-4, RGB(28,44,28));
        /* Health ring */
        int hrx=(wx+wx+ww)/2, hry=wy+TITLEBAR_H+70;
        gui_draw_circle(hrx, hry, 36, RGB(28,44,28));
        gui_draw_circle(hrx, hry, 30, cm_acc);
        gui_draw_circle(hrx, hry, 24, cm_bg);
        { char hpct[8];
          runtime_format_percent(cm_health, hpct, sizeof(hpct));
          vga_draw_string_trans(hrx-8, hry-4, hpct, cm_acc); }
        vga_draw_string_trans(hrx-20, hry+10, "Healthy", cm_txt);
        /* Scan button */
        gui_draw_rounded_rect(wx+(ww-90)/2, wy+TITLEBAR_H+112, 90, 22, 8, cm_acc);
        vga_draw_string_trans(wx+(ww-72)/2, wy+TITLEBAR_H+118,
                              g_cleanmymac_scan_count>0?"Scan Again":"Scan", RGB(10,20,10));
        if (g_cleanmymac_scan_count > 0) {
            char cm_scans[24];
            char cm_num[12];
            int cp2 = 0;
            runtime_format_uint((uint32_t)g_cleanmymac_scan_count, cm_num, sizeof(cm_num));
            cm_scans[0] = 0;
            apps4_append_text(cm_scans, &cp2, sizeof(cm_scans), "Scans: ");
            apps4_append_text(cm_scans, &cp2, sizeof(cm_scans), cm_num);
            vga_draw_string_trans(wx+8, wy+TITLEBAR_H+128, cm_scans, cm_acc);
        }
        /* Stats */
        vga_draw_hline(wx+2, wy+TITLEBAR_H+140, ww-4, RGB(28,44,28));
        static const char *cm_cats[]={"Junk Files","Old Files","Large Files","Mail Attachments"};
        { uint32_t base = cm_has_storage ? cm_storage.used_bytes : cm_sys.heap_used_bytes;
          uint32_t cm_sizes[4];
          cm_sizes[0] = base / 4U;
          cm_sizes[1] = base / 8U;
          cm_sizes[2] = base / 3U;
          cm_sizes[3] = base / 16U;
        int ci3;
        for(ci3=0;ci3<4;ci3++){
            char sizebuf[16];
            int cy3=wy+TITLEBAR_H+146+ci3*22;
            runtime_format_bytes(cm_sizes[ci3], sizebuf, sizeof(sizebuf));
            vga_draw_string_trans(wx+8, cy3, cm_cats[ci3], cm_txt);
            vga_draw_string_trans(wx+ww-56, cy3, sizebuf, cm_acc);
        } }
        return 1;
    }

    /* ---- Bartender 4 ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Bartender 4")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t ba_bg=RGB(22,22,28); uint32_t ba_txt=RGB(218,218,226); uint32_t ba_sub=RGB(110,110,122);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, ba_bg);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Bartender 4 — Menu Bar Manager", ba_txt);
        vga_draw_hline(wx+2, wy+TITLEBAR_H+22, ww-4, RGB(44,44,52));
        /* Menu bar preview */
        vga_fill_rect(wx+4, wy+TITLEBAR_H+28, ww-8, 22, RGB(28,28,32));
        gui_draw_rounded_rect(wx+4, wy+TITLEBAR_H+28, ww-8, 22, 4, RGB(40,40,48));
        vga_draw_string_trans(wx+10, wy+TITLEBAR_H+35, "Apple  Finder  File  Edit  View", ba_sub);
        /* Hidden items */
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+56, "Hidden Items:", ba_txt);
        static const char *ba_items[]={"Dropbox","1Password","Klokki","Tot","Alfred"};
        static const int ba_shown[]={0,0,1,0,1};
        int bi3;
        for(bi3=0;bi3<5;bi3++){
            int by3=wy+TITLEBAR_H+68+bi3*24;
            vga_fill_rect(wx+4, by3, ww-8, 22, RGB(28,28,34));
            vga_draw_hline(wx+4, by3+22, ww-8, RGB(40,40,48));
            gui_draw_rounded_rect(wx+8, by3+4, 14, 14, 3, RGB(50,50,60));
            vga_draw_string_trans(wx+26, by3+7, ba_items[bi3], ba_txt);
            vga_draw_string_trans(wx+ww-56, by3+7, ba_shown[bi3]?"Visible":"Hidden", ba_shown[bi3]?RGB(52,199,89):ba_sub);
        }
        return 1;
    }

    /* ---- Scrobbles ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Scrobbles")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t sc_bg=RGB(22,18,18); uint32_t sc_txt=RGB(228,215,215); uint32_t sc_sub=RGB(140,110,110);
        uint32_t sc_acc=RGB(220,40,40);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, sc_bg);
        /* Header */
        vga_draw_string_trans(wx+(ww-64)/2, wy+TITLEBAR_H+8, "Scrobbles", sc_acc);
        vga_draw_string_trans(wx+(ww-104)/2, wy+TITLEBAR_H+20, "Last.fm Scrobbler", sc_sub);
        vga_draw_hline(wx+2, wy+TITLEBAR_H+32, ww-4, RGB(48,32,32));
        /* Now playing */
        vga_fill_rect(wx+2, wy+TITLEBAR_H+34, ww-4, 50, RGB(30,22,22));
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+38, "NOW PLAYING", sc_sub);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+50, "Bohemian Rhapsody", sc_txt);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+62, "Queen  *  A Night at the Opera", sc_sub);
        /* Progress bar */
        vga_fill_rect(wx+8, wy+TITLEBAR_H+76, ww-16, 4, RGB(50,35,35));
        vga_fill_rect(wx+8, wy+TITLEBAR_H+76, (ww-16)*2/3, 4, sc_acc);
        /* Recent scrobbles */
        vga_draw_hline(wx+2, wy+TITLEBAR_H+86, ww-4, RGB(48,32,32));
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+90, "RECENT SCROBBLES", sc_sub);
        static const char *sc_songs[]={"Hotel California","Comfortably Numb","Stairway to Heaven","Wish You Were Here","Smells Like Teen Spirit"};
        static const char *sc_artists[]={"Eagles","Pink Floyd","Led Zeppelin","Pink Floyd","Nirvana"};
        int si3;
        for(si3=0;si3<5;si3++){
            int sy3=wy+TITLEBAR_H+100+si3*28;
            vga_fill_rect(wx+2, sy3, ww-4, 26, RGB(26,20,20));
            vga_draw_hline(wx+2, sy3+26, ww-4, RGB(40,28,28));
            gui_draw_rounded_rect(wx+6, sy3+4, 18, 18, 3, sc_acc);
            vga_draw_char_trans(wx+10, sy3+10, sc_songs[si3][0], RGB(255,255,255));
            vga_draw_string_trans(wx+28, sy3+6, sc_songs[si3], sc_txt);
            vga_draw_string_trans(wx+28, sy3+16, sc_artists[si3], sc_sub);
        }
        vga_draw_string_trans(wx+8, wy+wh-16, "4,821 total scrobbles", sc_sub);
        return 1;
    }

    /* ---- Alfred ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Alfred")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t al_bg=RGB(20,16,24); uint32_t al_txt=RGB(228,220,235); uint32_t al_sub=RGB(130,118,140);
        uint32_t al_acc=RGB(200,50,200);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H, al_bg);
        /* Search box */
        gui_draw_rounded_rect(wx+8, wy+TITLEBAR_H+10, ww-16, 34, 10,
                              g_alfred_search_focused ? RGB(40,24,48) : RGB(28,22,34));
        gui_draw_rounded_rect_outline(wx+8, wy+TITLEBAR_H+10, ww-16, 34, 10,
                                      g_alfred_search_focused ? RGB(255,120,255) : al_acc);
        gui_draw_circle(wx+26, wy+TITLEBAR_H+27, 8, al_sub);
        vga_draw_string_trans(wx+42, wy+TITLEBAR_H+23, "myos", al_txt);
        vga_fill_rect(wx+42+4*8, wy+TITLEBAR_H+24, 2, 12, al_acc);
        /* Results */
        vga_draw_hline(wx+2, wy+TITLEBAR_H+50, ww-4, RGB(36,28,44));
        static const char *al_results[]={"MyOS Project","myos.iso","myos build script","myos.iso (copy)"};
        static const char *al_types[]={"Folder","File","Script","File"};
        static const uint32_t al_ic[]={RGB(0,122,255),RGB(200,200,200),RGB(52,199,89),RGB(200,200,200)};
        int ai2;
        for(ai2=0;ai2<4;ai2++){
            int ay3=wy+TITLEBAR_H+54+ai2*28;
            if(ai2==0) vga_fill_rect(wx+2, ay3-2, ww-4, 30, RGB(30,24,38));
            gui_draw_rounded_rect(wx+8, ay3+2, 20, 20, 4, al_ic[ai2]);
            vga_draw_string_trans(wx+34, ay3+4, al_results[ai2], ai2==0?al_acc:al_txt);
            vga_draw_string_trans(wx+34, ay3+16, al_types[ai2], al_sub);
            vga_draw_string_trans(wx+ww-32, ay3+10, ai2==0?"Ret":"", al_sub);
        }
        vga_draw_string_trans(wx+8, wy+wh-18, "Alfred Powerpack  |  Workflows  |  Clipboard", al_sub);
        return 1;
    }

    /* ---- Pong game ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Pong")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy0=wy+TITLEBAR_H;
        int gx=wx+1, gy=cy0+1, gw=ww-2, gh=wh-TITLEBAR_H-2;
        /* Background */
        vga_fill_rect(gx, gy, gw, gh, RGB(10,10,18));
        /* Center dashed line */
        { int di4; for(di4=0;di4<gh;di4+=8) if(di4%16<8) vga_fill_rect(gx+gw/2-1,gy+di4,2,6,RGB(60,60,80)); }
        /* Scores */
        { char sc[8]; sc[0]='0'+g_pong_score_a; sc[1]=0;
          vga_draw_string_trans(gx+gw/4-4, gy+6, sc, RGB(180,180,200)); }
        { char sc[8]; sc[0]='0'+g_pong_score_p; sc[1]=0;
          vga_draw_string_trans(gx+3*gw/4-4, gy+6, sc, RGB(180,180,200)); }
        /* Paddles */
        int pdh=36, pdw=6;
        /* AI paddle (left) */
        { int ay=gy+g_pong_ay-pdh/2;
          if(ay<gy) ay=gy;
          if(ay+pdh>gy+gh) ay=gy+gh-pdh;
          vga_fill_rect(gx+4, ay, pdw, pdh, RGB(220,220,240)); }
        /* Player paddle (right) */
        { int py=gy+g_pong_py-pdh/2;
          if(py<gy) py=gy;
          if(py+pdh>gy+gh) py=gy+gh-pdh;
          vga_fill_rect(gx+gw-4-pdw, py, pdw, pdh, RGB(100,200,255)); }
        /* Ball */
        if (g_pong_active && !g_pong_over)
            gui_draw_circle(gx+g_pong_bx, gy+g_pong_by, 4, RGB(255,255,100));
        /* Start / Game Over overlay */
        if (!g_pong_active || g_pong_over) {
            vga_fill_rect_alpha(gx, gy, gw, gh, RGB(0,0,0), 100);
            if (g_pong_over) {
                const char *wmsg = g_pong_score_p>g_pong_score_a?"You Win!":"AI Wins!";
                vga_draw_string_trans(gx+gw/2-32, gy+gh/2-10, wmsg, RGB(255,220,60));
            }
            vga_draw_string_trans(gx+gw/2-48, gy+gh/2+4, "Space = Start", RGB(180,180,200));
            vga_draw_string_trans(gx+4, gy+gh-12, "W/S or Up/Down", RGB(100,100,120));
        }
        /* Physics update */
        { uint32_t now4=timer_ticks();
          if (g_pong_active && !g_pong_over && now4-g_pong_last>=16) {
              g_pong_last=now4;
              g_pong_bx+=g_pong_vx; g_pong_by+=g_pong_vy;
              /* Top/bottom wall bounce */
              if (g_pong_by<=0){g_pong_by=0;g_pong_vy=-g_pong_vy;}
              if (g_pong_by>=gh){g_pong_by=gh;g_pong_vy=-g_pong_vy;}
              /* AI tracks ball */
              int ai_cy=g_pong_ay;
              if(g_pong_by<ai_cy-2&&ai_cy>pdh/2) g_pong_ay-=2;
              if(g_pong_by>ai_cy+2&&ai_cy<gh-pdh/2) g_pong_ay+=2;
              /* Paddle collisions */
              int ay2=g_pong_ay-pdh/2; if(ay2<0)ay2=0;
              int py2=g_pong_py-pdh/2; if(py2<0)py2=0;
              if(g_pong_bx<=10+pdw&&g_pong_by>=ay2&&g_pong_by<=ay2+pdh){
                  g_pong_vx=-g_pong_vx; g_pong_bx=10+pdw+1;
                  g_pong_vy+=(g_pong_by-g_pong_ay)/6;
              }
              if(g_pong_bx>=gw-10-pdw&&g_pong_by>=py2&&g_pong_by<=py2+pdh){
                  g_pong_vx=-g_pong_vx; g_pong_bx=gw-10-pdw-1;
                  g_pong_vy+=(g_pong_by-g_pong_py)/6;
              }
              /* Clamp ball vy */
              if(g_pong_vy>5)g_pong_vy=5;
              if(g_pong_vy<-5)g_pong_vy=-5;
              /* Score */
              if(g_pong_bx<0){
                  g_pong_score_p++;
                  if(g_pong_score_p>=7){g_pong_over=1;}
                  else{g_pong_bx=gw/2;g_pong_by=gh/2;g_pong_vx=3;g_pong_vy=2;}
              }
              if(g_pong_bx>gw){
                  g_pong_score_a++;
                  if(g_pong_score_a>=7){g_pong_over=1;}
                  else{g_pong_bx=gw/2;g_pong_by=gh/2;g_pong_vx=-3;g_pong_vy=2;}
              }
          }
        }
        return 1;
    }

    /* ---- Snake game ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Snake")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy0 = wy + TITLEBAR_H;
        int ch0 = wh - TITLEBAR_H - 2;

        uint32_t sn_bg   = RGB(12, 20, 12);
        uint32_t sn_grid = RGB(20, 35, 20);
        uint32_t sn_snake= RGB(52,199,89);
        uint32_t sn_head = RGB(80,240,100);
        uint32_t sn_food = RGB(255,59,48);
        uint32_t sn_txt  = RGB(220,240,220);
        uint32_t sn_sub  = RGB(100,140,100);

        vga_fill_rect(wx+1, cy0, ww-2, ch0, sn_bg);

        /* Grid area */
        int cell = 14;
        int grid_w = SNAKE_GRID_W * cell;
        int grid_h = SNAKE_GRID_H * cell;
        int gx0 = wx + (ww - grid_w) / 2;
        int gy0 = cy0 + 28;
        if (gy0 + grid_h > cy0 + ch0 - 20) gy0 = cy0 + 4;

        /* Draw grid lines */
        { int gi;
          for (gi=0; gi<=SNAKE_GRID_W; gi++)
              vga_draw_vline(gx0+gi*cell, gy0, grid_h, sn_grid);
          for (gi=0; gi<=SNAKE_GRID_H; gi++)
              vga_draw_hline(gx0, gy0+gi*cell, grid_w, sn_grid);
        }

        /* Update snake if active */
        if (g_snake_active && !g_snake_game_over) {
            uint32_t now3 = timer_ticks();
            if (now3 - g_snake_last_tick >= (uint32_t)g_snake_speed) {
                g_snake_last_tick = now3;
                g_snake_dir = g_snake_next_dir;
                int nx = g_snake_x[0], ny = g_snake_y[0];
                if (g_snake_dir==0) nx++;
                else if (g_snake_dir==1) ny++;
                else if (g_snake_dir==2) nx--;
                else ny--;
                /* Wall collision */
                if (nx<0||nx>=SNAKE_GRID_W||ny<0||ny>=SNAKE_GRID_H) {
                    g_snake_game_over=1; g_snake_active=0;
                } else {
                    /* Self collision */
                    int si; int hit=0;
                    for (si=0;si<g_snake_len-1;si++)
                        if (g_snake_x[si]==nx && g_snake_y[si]==ny) { hit=1; break; }
                    if (hit) { g_snake_game_over=1; g_snake_active=0; }
                    else {
                        /* Ate food? */
                        int ate = (nx==g_snake_food_x && ny==g_snake_food_y);
                        /* Shift body */
                        int sj;
                        for (sj=g_snake_len-1;sj>0;sj--) {
                            g_snake_x[sj]=g_snake_x[sj-1];
                            g_snake_y[sj]=g_snake_y[sj-1];
                        }
                        g_snake_x[0]=nx; g_snake_y[0]=ny;
                        if (ate) {
                            g_snake_score++;
                            if (g_snake_len < SNAKE_MAX_LEN-1) g_snake_len++;
                            /* New food position (pseudo-random) */
                            g_snake_food_x = (g_snake_food_x*7+g_snake_score*3+13)%SNAKE_GRID_W;
                            g_snake_food_y = (g_snake_food_y*5+g_snake_score*2+7)%SNAKE_GRID_H;
                            if (g_snake_speed > 80) g_snake_speed -= 5;
                        }
                    }
                }
            }
        }

        /* Draw food (pulsing) */
        { uint32_t t9=timer_ticks();
          int fpulse=(int)(t9/300)%2;
          int fx2=gx0+g_snake_food_x*cell+2, fy2=gy0+g_snake_food_y*cell+2;
          gui_draw_rounded_rect(fx2, fy2, cell-4+fpulse, cell-4+fpulse, 3, sn_food);
        }

        /* Draw snake */
        { int si2;
          for (si2=g_snake_len-1;si2>=0;si2--) {
              int bx2=gx0+g_snake_x[si2]*cell+1, by2=gy0+g_snake_y[si2]*cell+1;
              uint32_t sc = (si2==0) ? sn_head : sn_snake;
              /* Gradient: tail is darker */
              if (si2>0) {
                  int fade=si2*40/g_snake_len;
                  uint8_t r2=(uint8_t)(52-(fade>30?30:fade));
                  uint8_t g2=(uint8_t)(199-(fade*3>120?120:fade*3));
                  uint8_t b2=(uint8_t)(89-(fade>60?60:fade));
                  sc=RGB(r2,g2,b2);
              }
              gui_draw_rounded_rect(bx2, by2, cell-2, cell-2, 2, sc);
              if (si2==0) {
                  /* Eyes on head */
                  int ex1=bx2+(g_snake_dir==2?2:cell-5), ey1=by2+2;
                  int ex2=bx2+(g_snake_dir==2?2:cell-5), ey2=by2+cell-5;
                  if (g_snake_dir==1||g_snake_dir==3) { ex1=bx2+2; ex2=bx2+cell-5; ey1=ey2=by2+(g_snake_dir==3?2:cell-5); }
                  vga_put_pixel(ex1, ey1, RGB(0,0,0));
                  vga_put_pixel(ex2, ey2, RGB(0,0,0));
              }
          }
        }

        /* Score / status header */
        { char sbuf[24]; int si3=0;
          sbuf[si3++]='S'; sbuf[si3++]='c'; sbuf[si3++]='o'; sbuf[si3++]='r'; sbuf[si3++]='e'; sbuf[si3++]=':';
          sbuf[si3++]=' '; sbuf[si3++]=(char)('0'+g_snake_score/10); sbuf[si3++]=(char)('0'+g_snake_score%10);
          sbuf[si3]=0;
          vga_fill_rect(wx+1, cy0, ww-2, 24, RGB(8,15,8));
          vga_draw_string_trans(wx+8, cy0+8, sbuf, sn_txt);
          /* Speed indicator */
          const char *spd = g_snake_speed<=100?"Fast":g_snake_speed<=150?"Normal":"Slow";
          vga_draw_string_trans(wx+ww-60, cy0+8, spd, sn_sub);
          vga_draw_hline(wx+1, cy0+24, ww-2, sn_grid);
        }

        if (!g_snake_active && !g_snake_game_over) {
            /* Start screen */
            vga_fill_rect_alpha(gx0, gy0, grid_w, grid_h, RGB(0,0,0), 160);
            { int tl=str_len("SNAKE")*16, ty2=gy0+grid_h/2-30;
              /* Draw "SNAKE" at 2x scale */
              int ci; for(ci=0;ci<5;ci++){
                  const char s2[]="SNAKE";
                  unsigned char ch2=(unsigned char)s2[ci];
                  int bx2=(gx0+(grid_w-tl)/2)+ci*16, row2,col2;
                  for(row2=0;row2<8;row2++) for(col2=0;col2<8;col2++)
                      if(font8x8[ch2][row2]&(1u<<col2))
                          vga_fill_rect(bx2+col2*2, ty2+row2*2, 2, 2, sn_head);
              }
            }
            vga_draw_string_trans(gx0+(grid_w-str_len("Press SPACE to start")*8)/2,
                gy0+grid_h/2+10, "Press SPACE to start", sn_txt);
            vga_draw_string_trans(gx0+(grid_w-str_len("Arrow keys = move direction")*8)/2,
                gy0+grid_h/2+22, "Arrow keys = move direction", sn_sub);
        }
        if (g_snake_game_over) {
            vga_fill_rect_alpha(gx0, gy0, grid_w, grid_h, RGB(0,0,0), 180);
            vga_draw_string_trans(gx0+(grid_w-str_len("GAME OVER")*8)/2, gy0+grid_h/2-10, "GAME OVER", sn_food);
            { char fbuf[20]; int fi=0;
              fbuf[fi++]='S'; fbuf[fi++]='c'; fbuf[fi++]='o'; fbuf[fi++]='r'; fbuf[fi++]='e'; fbuf[fi++]=':';
              fbuf[fi++]=' '; fbuf[fi++]=(char)('0'+g_snake_score/10); fbuf[fi++]=(char)('0'+g_snake_score%10);
              fbuf[fi]=0;
              vga_draw_string_trans(gx0+(grid_w-str_len(fbuf)*8)/2, gy0+grid_h/2+4, fbuf, sn_txt);
            }
            vga_draw_string_trans(gx0+(grid_w-str_len("Press SPACE to restart")*8)/2,
                gy0+grid_h/2+18, "Press SPACE to restart", sn_sub);
        }
        return 1;
    }

    /* ---- Wordle game ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Wordle")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx = win->x, wy = win->y, ww = win->w, wh = win->h;
        uint32_t bg = g_pref_darkmode ? RGB(18,18,19) : RGB(255,255,255);
        uint32_t hdr_col = g_pref_darkmode ? RGB(30,30,32) : RGB(248,248,250);
        uint32_t border_col = g_pref_darkmode ? RGB(58,58,60) : RGB(211,214,218);
        uint32_t txt_col = g_pref_darkmode ? RGB(215,215,220) : RGB(18,18,19);
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, wh-TITLEBAR_H-18, bg);
        /* Header bar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H, ww-2, 20, hdr_col);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+20, ww-2, border_col);
        { int tl = str_len("WORDLE")*8;
          vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+6, "WORDLE", txt_col); }
        /* Answer for current game */
        const char *answer = g_wordle_words[g_wordle_answer_idx];
        if (!answer) answer = g_wordle_words[0];
        if (!answer) answer = "";
        /* Draw 6x5 grid */
        int cell_sz = 36, cell_gap = 4;
        int grid_w = WORDLE_COLS*(cell_sz+cell_gap)-cell_gap;
        int grid_h = WORDLE_ROWS*(cell_sz+cell_gap)-cell_gap;
        int gx = wx + (ww-grid_w)/2;
        int gy = wy + TITLEBAR_H + 28;
        int r2, c2;
        for (r2=0; r2<WORDLE_ROWS; r2++) {
            for (c2=0; c2<WORDLE_COLS; c2++) {
                int cx3 = gx + c2*(cell_sz+cell_gap);
                int cy3 = gy + r2*(cell_sz+cell_gap);
                uint32_t cell_bg, cell_bd, cell_txt;
                int res = g_wordle_results[r2][c2];
                if (res == 3) { cell_bg=RGB(108,169,100); cell_bd=RGB(108,169,100); cell_txt=RGB(255,255,255); }
                else if (res==2) { cell_bg=RGB(200,182,46); cell_bd=RGB(200,182,46); cell_txt=RGB(255,255,255); }
                else if (res==1) { cell_bg=RGB(120,124,126); cell_bd=RGB(120,124,126); cell_txt=RGB(255,255,255); }
                else { cell_bg=bg; cell_bd=(r2==g_wordle_cur_row&&c2<g_wordle_cur_col)?RGB(120,120,120):border_col; cell_txt=txt_col; }
                vga_fill_rect(cx3, cy3, cell_sz, cell_sz, cell_bg);
                vga_draw_rect_outline(cx3, cy3, cell_sz, cell_sz, cell_bd);
                /* Draw letter */
                char letter = (r2<g_wordle_cur_row) ? g_wordle_guesses[r2][c2] :
                              (r2==g_wordle_cur_row && c2<g_wordle_cur_col) ? g_wordle_guesses[r2][c2] : 0;
                if (letter) {
                    char ls[2]; ls[0]=letter; ls[1]=0;
                    vga_draw_string_trans(cx3+(cell_sz-8)/2, cy3+(cell_sz-8)/2, ls, cell_txt);
                }
            }
        }
        /* Keyboard display */
        {
            static const char *kb_rows[] = {"QWERTYUIOP","ASDFGHJKL","ZXCVBNM",NULL};
            int kr, kc, key_w=18, key_h=22, key_gap=2;
            int kb_y = gy + grid_h + 8;
            for (kr=0; kb_rows[kr]; kr++) {
                const char *row3 = kb_rows[kr];
                int rlen = str_len(row3);
                int row_w = rlen*(key_w+key_gap)-key_gap;
                int kx3 = wx+(ww-row_w)/2;
                for (kc=0; kc<rlen; kc++) {
                    char lc = row3[kc];
                    int ki = lc-'A';
                    uint32_t kb_bg, kb_txt;
                    if (ki>=0 && ki<26) {
                        if (g_wordle_kb_state[ki]==3) { kb_bg=RGB(108,169,100); kb_txt=RGB(255,255,255); }
                        else if (g_wordle_kb_state[ki]==2) { kb_bg=RGB(200,182,46); kb_txt=RGB(255,255,255); }
                        else if (g_wordle_kb_state[ki]==1) { kb_bg=RGB(120,124,126); kb_txt=RGB(255,255,255); }
                        else { kb_bg=g_pref_darkmode?RGB(60,60,65):RGB(215,218,220); kb_txt=txt_col; }
                    } else { kb_bg=RGB(150,150,155); kb_txt=RGB(255,255,255); }
                    vga_fill_rect(kx3+kc*(key_w+key_gap), kb_y+kr*(key_h+key_gap), key_w, key_h, kb_bg);
                    char ks[2]; ks[0]=lc; ks[1]=0;
                    vga_draw_string_trans(kx3+kc*(key_w+key_gap)+(key_w-8)/2,
                        kb_y+kr*(key_h+key_gap)+(key_h-8)/2, ks, kb_txt);
                }
            }
        }
        /* Status overlays */
        if (g_wordle_state==1) {
            vga_fill_rect_alpha(wx+1, wy+TITLEBAR_H+20, ww-2, wh-TITLEBAR_H-38, RGB(0,0,0), 150);
            { int ml=str_len("YOU WIN!")*8;
              vga_draw_string_trans(wx+(ww-ml)/2, wy+TITLEBAR_H+80, "YOU WIN!", RGB(108,169,100)); }
            { const char *w2="Press R to restart";
              vga_draw_string_trans(wx+(ww-str_len(w2)*8)/2, wy+TITLEBAR_H+100, w2, RGB(200,200,200)); }
        } else if (g_wordle_state==2) {
            vga_fill_rect_alpha(wx+1, wy+TITLEBAR_H+20, ww-2, wh-TITLEBAR_H-38, RGB(0,0,0), 150);
            { int ml=str_len("GAME OVER")*8;
              vga_draw_string_trans(wx+(ww-ml)/2, wy+TITLEBAR_H+70, "GAME OVER", RGB(200,80,80)); }
            { int al=str_len(answer)*8;
              vga_draw_string_trans(wx+(ww-al)/2, wy+TITLEBAR_H+86, answer, RGB(255,200,100)); }
            { const char *w2="Press R to restart";
              vga_draw_string_trans(wx+(ww-str_len(w2)*8)/2, wy+TITLEBAR_H+104, w2, RGB(200,200,200)); }
        }
        return 1;
    }

    /* ---- Breakout game ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Breakout")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy0 = wy + TITLEBAR_H;
        int ch0 = wh - TITLEBAR_H - 2;

        /* Game area dimensions */
        int gx=wx+1, gy=cy0+24, gw=ww-2, gh=ch0-24-2;
        int paddle_w=50, paddle_h=6, ball_r=4;
        int brick_w=gw/BRK_COLS, brick_h=12;

        /* Background */
        vga_fill_rect(wx+1, cy0, ww-2, ch0, RGB(8,8,16));

        /* Status bar */
        vga_fill_rect(wx+1, cy0, ww-2, 22, RGB(16,16,28));
        { char sbuf[32]; int si=0;
          /* Score */
          sbuf[si++]='S'; sbuf[si++]='c'; sbuf[si++]='o'; sbuf[si++]='r'; sbuf[si++]='e'; sbuf[si++]=':';
          sbuf[si++]=' '; sbuf[si++]=(char)('0'+g_brk_score/100%10); sbuf[si++]=(char)('0'+g_brk_score/10%10); sbuf[si++]=(char)('0'+g_brk_score%10);
          sbuf[si]=0;
          vga_draw_string_trans(wx+6, cy0+7, sbuf, RGB(220,220,240));
          /* Lives: hearts */
          int li;
          for (li=0;li<g_brk_lives&&li<3;li++)
              vga_draw_string_trans(wx+ww-30+li*10, cy0+7, "*", RGB(255,59,48));
        }
        vga_draw_hline(wx+1, cy0+22, ww-2, RGB(40,40,60));

        if (!g_brk_active && !g_brk_game_over && !g_brk_won) {
            /* Start screen */
            { int ci; const char *s4="BREAKOUT";
              for(ci=0;s4[ci];ci++){
                  unsigned char ch2=(unsigned char)s4[ci];
                  int bx2=gx+(gw-str_len(s4)*16)/2+ci*16, ty2=gy+gh/3, row2,col2;
                  for(row2=0;row2<8;row2++) for(col2=0;col2<8;col2++)
                      if(font8x8[ch2][row2]&(1u<<col2))
                          vga_fill_rect(bx2+col2*2, ty2+row2*2, 2, 2, RGB(100,180,255));
              }
            }
            vga_draw_string_trans(gx+(gw-str_len("Press SPACE to start")*8)/2,
                gy+gh/2+10, "Press SPACE to start", RGB(180,200,240));
            vga_draw_string_trans(gx+(gw-str_len("Mouse or A/D to move paddle")*8)/2,
                gy+gh/2+24, "Mouse or A/D to move paddle", RGB(100,120,160));
        } else if (g_brk_game_over) {
            vga_fill_rect_alpha(gx, gy, gw, gh, RGB(0,0,0), 160);
            vga_draw_string_trans(gx+(gw-str_len("GAME OVER")*8)/2, gy+gh/2-8, "GAME OVER", RGB(255,59,48));
            vga_draw_string_trans(gx+(gw-str_len("Press SPACE to restart")*8)/2, gy+gh/2+8, "Press SPACE to restart", RGB(180,180,200));
        } else if (g_brk_won) {
            vga_fill_rect_alpha(gx, gy, gw, gh, RGB(0,0,0), 120);
            vga_draw_string_trans(gx+(gw-str_len("YOU WIN!")*8)/2, gy+gh/2-8, "YOU WIN!", RGB(52,199,89));
            vga_draw_string_trans(gx+(gw-str_len("Press SPACE to play again")*8)/2, gy+gh/2+8, "Press SPACE to play again", RGB(180,200,180));
        } else {
            /* Update game physics */
            uint32_t now4=timer_ticks();
            if (now4 - g_brk_last_tick >= 16) { /* ~60fps */
                g_brk_last_tick = now4;
                /* Move ball */
                g_brk_ball_x += g_brk_ball_vx;
                g_brk_ball_y += g_brk_ball_vy;
                /* Wall collisions */
                if (g_brk_ball_x <= gx+ball_r) { g_brk_ball_x=gx+ball_r; g_brk_ball_vx=-g_brk_ball_vx; }
                if (g_brk_ball_x >= gx+gw-ball_r) { g_brk_ball_x=gx+gw-ball_r; g_brk_ball_vx=-g_brk_ball_vx; }
                if (g_brk_ball_y <= gy+ball_r) { g_brk_ball_y=gy+ball_r; g_brk_ball_vy=-g_brk_ball_vy; }
                /* Bottom: lose life */
                if (g_brk_ball_y >= gy+gh-ball_r) {
                    g_brk_lives--;
                    if (g_brk_lives <= 0) { g_brk_game_over=1; g_brk_active=0; }
                    else { g_brk_ball_x=gx+gw/2; g_brk_ball_y=gy+gh*2/3; g_brk_ball_vx=2; g_brk_ball_vy=-3; }
                }
                /* Paddle collision */
                int px=gx+g_brk_paddle_x-paddle_w/2, py=gy+gh-paddle_h-4;
                if (g_brk_ball_y+ball_r >= py && g_brk_ball_y-ball_r <= py+paddle_h &&
                    g_brk_ball_x >= px && g_brk_ball_x <= px+paddle_w) {
                    g_brk_ball_vy = -(g_brk_ball_vy < 0 ? -g_brk_ball_vy : g_brk_ball_vy);
                    /* Angle based on paddle hit position */
                    int rel = g_brk_ball_x - (px + paddle_w/2);
                    g_brk_ball_vx = rel * 4 / (paddle_w/2);
                    if (g_brk_ball_vx == 0) g_brk_ball_vx = 1;
                    if (g_brk_ball_vx > 4) g_brk_ball_vx=4;
                    if (g_brk_ball_vx < -4) g_brk_ball_vx=-4;
                }
                /* Brick collisions */
                { int ri, ci2;
                  int alive=0;
                  for (ri=0;ri<BRK_ROWS;ri++) for (ci2=0;ci2<BRK_COLS;ci2++) {
                      if (!g_brk_bricks[ri][ci2]) continue;
                      alive++;
                      int bx2=gx+ci2*brick_w, by2=gy+ri*(brick_h+2)+4;
                      if (g_brk_ball_x+ball_r>=bx2 && g_brk_ball_x-ball_r<=bx2+brick_w-2 &&
                          g_brk_ball_y+ball_r>=by2 && g_brk_ball_y-ball_r<=by2+brick_h) {
                          g_brk_bricks[ri][ci2]=0;
                          g_brk_ball_vy=-g_brk_ball_vy;
                          g_brk_score += (BRK_ROWS-ri)*10;
                      }
                  }
                  if (alive==0) { g_brk_won=1; g_brk_active=0; }
                }
            }
            /* Draw bricks */
            { int ri, ci2;
              static const uint32_t brow_cols[BRK_ROWS]={
                  RGB(255,59,48), RGB(255,149,0), RGB(255,204,0), RGB(52,199,89), RGB(0,122,255)
              };
              for (ri=0;ri<BRK_ROWS;ri++) for (ci2=0;ci2<BRK_COLS;ci2++) {
                  if (!g_brk_bricks[ri][ci2]) continue;
                  int bx2=gx+ci2*brick_w, by2=gy+ri*(brick_h+2)+4;
                  vga_fill_rect(bx2+1, by2, brick_w-2, brick_h, brow_cols[ri]);
                  vga_fill_rect_alpha(bx2+1, by2, brick_w-2, 4, RGB(255,255,255), 40);
              }
            }
            /* Draw paddle */
            { int px=gx+g_brk_paddle_x-paddle_w/2, py=gy+gh-paddle_h-4;
              gui_draw_rounded_rect(px, py, paddle_w, paddle_h, 3, RGB(200,220,255));
              vga_fill_rect_alpha(px, py, paddle_w, 3, RGB(255,255,255), 80);
            }
            /* Draw ball */
            gui_draw_circle(g_brk_ball_x, g_brk_ball_y, ball_r, RGB(255,255,255));
            gui_draw_circle(g_brk_ball_x-1, g_brk_ball_y-1, 2, RGB(200,220,255));
        }
        return 1;
    }

    /* Finder / default window content */
    {
    const gui_window_t *win = &g_windows[idx];
    if (!win->visible) return 1;

    int wx = win->x, wy = win->y, ww = win->w, wh = win->h;

    /* Finder dark mode colors */
    uint32_t fn_tb_bg  = g_pref_darkmode ? RGB(44,44,48)    : RGB(232,232,232);
    uint32_t fn_tb_bd  = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,200);
    uint32_t fn_btn_bg = g_pref_darkmode ? RGB(58,58,62)    : RGB(210,210,210);
    uint32_t fn_btn_bd = g_pref_darkmode ? RGB(72,72,76)    : RGB(180,180,180);
    uint32_t fn_btn_tx = g_pref_darkmode ? RGB(180,180,188) : RGB(80,80,80);
    uint32_t fn_sb_bg  = g_pref_darkmode ? RGB(36,36,40)    : RGB(238,238,238);
    uint32_t fn_sb_bd  = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,200);
    uint32_t fn_sb_cat = g_pref_darkmode ? RGB(110,110,118) : RGB(130,130,130);
    uint32_t fn_sb_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(40,40,40);
    uint32_t fn_ct_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);
    uint32_t fn_lh_bg  = g_pref_darkmode ? RGB(36,36,40)    : RGB(230,230,235);
    uint32_t fn_lh_bd  = g_pref_darkmode ? RGB(55,55,60)    : RGB(200,200,205);
    uint32_t fn_lh_txt = g_pref_darkmode ? RGB(180,180,188) : RGB(60,60,60);
    uint32_t fn_bc_txt = g_pref_darkmode ? RGB(180,180,188) : RGB(60,60,60);

    /* Toolbar strip */
    int tbh = 28;
    vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, tbh, fn_tb_bg);
    vga_draw_hline(wx+1, wy+TITLEBAR_H+tbh, ww-2, fn_tb_bd);
    /* Back button */
    vga_fill_rect(wx+8, wy+TITLEBAR_H+5, 20, 18, fn_btn_bg);
    vga_draw_rect_outline(wx+8, wy+TITLEBAR_H+5, 20, 18, fn_btn_bd);
    vga_draw_string_trans(wx+12, wy+TITLEBAR_H+9, "<", fn_btn_tx);
    /* Forward button */
    vga_fill_rect(wx+30, wy+TITLEBAR_H+5, 20, 18, fn_btn_bg);
    vga_draw_rect_outline(wx+30, wy+TITLEBAR_H+5, 20, 18, fn_btn_bd);
    vga_draw_string_trans(wx+34, wy+TITLEBAR_H+9, ">", fn_btn_tx);
    /* View mode toggle: icon/list buttons */
    {
        int vbx = wx+ww-160, vby = wy+TITLEBAR_H+6;
        /* Icon view button */
        uint32_t ic_bg = (g_finder_view==0)?RGB(0,122,255):RGB(210,210,210);
        vga_fill_rect(vbx, vby, 20, 16, ic_bg);
        vga_draw_rect_outline(vbx, vby, 20, 16, RGB(160,160,160));
        vga_fill_rect(vbx+3, vby+3, 6, 6, g_finder_view==0?RGB(255,255,255):RGB(100,100,100));
        vga_fill_rect(vbx+11, vby+3, 6, 6, g_finder_view==0?RGB(255,255,255):RGB(100,100,100));
        vga_fill_rect(vbx+3, vby+10, 6, 3, g_finder_view==0?RGB(255,255,255):RGB(100,100,100));
        vga_fill_rect(vbx+11, vby+10, 6, 3, g_finder_view==0?RGB(255,255,255):RGB(100,100,100));
        /* List view button */
        uint32_t lv_bg = (g_finder_view==1)?RGB(0,122,255):RGB(210,210,210);
        vga_fill_rect(vbx+22, vby, 20, 16, lv_bg);
        vga_draw_rect_outline(vbx+22, vby, 20, 16, RGB(160,160,160));
        vga_fill_rect(vbx+25, vby+4,  14, 2, g_finder_view==1?RGB(255,255,255):RGB(100,100,100));
        vga_fill_rect(vbx+25, vby+8,  14, 2, g_finder_view==1?RGB(255,255,255):RGB(100,100,100));
        vga_fill_rect(vbx+25, vby+12, 14, 2, g_finder_view==1?RGB(255,255,255):RGB(100,100,100));
        /* Column view button */
        uint32_t cv_bg = (g_finder_view==2)?RGB(0,122,255):RGB(210,210,210);
        vga_fill_rect(vbx+44, vby, 20, 16, cv_bg);
        vga_draw_rect_outline(vbx+44, vby, 20, 16, RGB(160,160,160));
        vga_draw_vline(vbx+44+6,  vby+3, 10, g_finder_view==2?RGB(255,255,255):RGB(100,100,100));
        vga_draw_vline(vbx+44+12, vby+3, 10, g_finder_view==2?RGB(255,255,255):RGB(100,100,100));
    }
    /* Search field */
    {
        int sfx = wx+ww-112, sfw=72, sfh=16;
        vga_fill_rect(sfx, wy+TITLEBAR_H+6, sfw, sfh, g_pref_darkmode?RGB(40,40,44):RGB(255,255,255));
        vga_draw_rect_outline(sfx, wy+TITLEBAR_H+6, sfw, sfh,
            g_finder_search_focused ? RGB(0,122,255) : fn_btn_bd);
        vga_draw_string_trans(sfx+4, wy+TITLEBAR_H+9,
            g_finder_search_focused ? "Search*" : "Search", fn_sb_cat);
    }
    /* Path breadcrumb showing current finder depth */
    {
        int bx = wx + 60;
        vga_draw_string_trans(bx, wy+TITLEBAR_H+10, "Desktop", fn_bc_txt);
        int di;
        for (di = 0; di < g_finder_depth; di++) {
            int sl = str_len(g_finder_stack[di]);
            bx += 7*8 + 4;
            if (di == 0) { vga_draw_string_trans(bx-20, wy+TITLEBAR_H+10, ">", fn_sb_cat); }
            else vga_draw_string_trans(bx-20, wy+TITLEBAR_H+10, ">", fn_sb_cat);
            vga_draw_string_trans(bx, wy+TITLEBAR_H+10, g_finder_stack[di], fn_bc_txt);
            bx += sl*8;
        }
    }

    /* Left sidebar */
    int sb_w = 90;
    int finder_y0 = wy + TITLEBAR_H + 1 + tbh + 1;
    int finder_h  = wh - TITLEBAR_H - 19 - tbh - 1;
    vga_fill_rect(wx+1, finder_y0, sb_w, finder_h, fn_sb_bg);
    vga_draw_vline(wx+sb_w, finder_y0, finder_h, fn_sb_bd);

    /* Sidebar sections */
    vga_draw_string_trans(wx+6, finder_y0+4,  "FAVORITES",  fn_sb_cat);
    /* AirDrop with blue icon */
    gui_draw_circle(wx+14, finder_y0+18, 5, RGB(40,100,220));
    vga_draw_string_trans(wx+10, finder_y0+13, "AirDrop",   RGB(40,80,200));
    /* iCloud Drive */
    vga_fill_rect(wx+8, finder_y0+24, 10, 6, RGB(100,160,255));
    vga_draw_hline(wx+8, finder_y0+24, 10, RGB(80,140,240));
    vga_draw_string_trans(wx+10, finder_y0+25, "iCloud",    fn_sb_txt);
    /* Desktop */
    vga_draw_string_trans(wx+10, finder_y0+37, "Desktop",   fn_sb_txt);
    /* Documents */
    vga_draw_string_trans(wx+10, finder_y0+49, "Documents", fn_sb_txt);
    /* Downloads */
    vga_draw_string_trans(wx+10, finder_y0+61, "Downloads", fn_sb_txt);
    vga_draw_hline(wx+4, finder_y0+74, sb_w-4, fn_sb_bd);
    vga_draw_string_trans(wx+6, finder_y0+78,  "iCLOUD",   fn_sb_cat);
    vga_draw_string_trans(wx+10, finder_y0+90, "iCloud Dr", fn_sb_txt);
    vga_draw_hline(wx+4, finder_y0+104, sb_w-4, fn_sb_bd);
    vga_draw_string_trans(wx+6, finder_y0+108, "LOCATIONS", fn_sb_cat);
    vga_draw_string_trans(wx+10, finder_y0+120, "MyOS",     fn_sb_txt);
    vga_draw_string_trans(wx+10, finder_y0+132, "Network",  fn_sb_txt);
    /* Tags section */
    {
        int tag_y = finder_y0 + 148;
        if (tag_y + 80 < finder_y0 + finder_h - 20) {
            vga_draw_hline(wx+4, tag_y-4, sb_w-4, fn_sb_bd);
            vga_draw_string_trans(wx+6, tag_y, "TAGS", fn_sb_cat);
            static const char *tag_names[]={"Red","Orange","Yellow","Green","Blue","Purple"};
            static const uint32_t tag_cols[]={
                RGB(255,59,48),RGB(255,149,0),RGB(255,204,0),
                RGB(52,199,89),RGB(0,122,255),RGB(175,82,222)};
            int ti;
            for(ti=0;ti<6;ti++) {
                int tiy=tag_y+12+ti*13;
                if (tiy+12 >= finder_y0+finder_h-20) break;
                gui_draw_circle(wx+14, tiy+5, 5, tag_cols[ti]);
                vga_draw_string_trans(wx+22, tiy, tag_names[ti], fn_sb_txt);
            }
        }
    }
    /* Trash at bottom */
    int fn_trash_y = finder_y0 + finder_h - 18;
    if (fn_trash_y > finder_y0 + 150) {
        vga_draw_hline(wx+4, fn_trash_y-4, sb_w-4, fn_sb_bd);
        vga_draw_string_trans(wx+10, fn_trash_y+2, "Trash", fn_sb_txt);
    }

    /* Highlight selected item (Desktop) */
    vga_fill_rect_alpha(wx+2, finder_y0+35, sb_w-4, 12, RGB(0,122,255), 60);

    /* Main file area */
    int content_x = wx + sb_w + 1;
    int content_w = ww - sb_w - 2;
    int content_y = finder_y0;
    int content_h = finder_h;
    vga_fill_rect(content_x, content_y, content_w, content_h, fn_ct_bg);

    /* File content: icon or list view */
    int fcount = 0;
    const folder_icon_t *cur_folders = finder_current_folders(&fcount);
    if (g_finder_view == 0) {
        /* Icon grid view */
        int icon_cell_w = (content_w > 200) ? 100 : content_w / 2;
        int icon_cell_h = 60;
        int grid_x = content_x + (content_w - 2 * icon_cell_w) / 2;
        int grid_y = content_y + 8;
        int r, c;
        for (r = 0; r < 2; r++) {
            for (c = 0; c < 2; c++) {
                int fi = r * 2 + c;
                if (fi >= fcount) break;
                int fx = grid_x + c * icon_cell_w + (icon_cell_w - 28) / 2;
                int fy = grid_y + r * icon_cell_h;
                if (fy + icon_cell_h < content_y + content_h)
                    draw_folder(fx, fy, icon_cell_w, &cur_folders[fi]);
            }
        }
    } else if (g_finder_view == 1) {
        /* List view: column header row + file rows */
        int lx = content_x + 4;
        int lh = 16; /* row height */
        /* Column headers */
        vga_fill_rect(content_x, content_y, content_w, lh, fn_lh_bg);
        vga_draw_hline(content_x, content_y+lh, content_w, fn_lh_bd);
        vga_draw_string_trans(lx+18, content_y+4, "Name", fn_lh_txt);
        vga_draw_string_trans(lx+content_w/2, content_y+4, "Date Modified", fn_lh_txt);
        vga_draw_string_trans(lx+content_w-44, content_y+4, "Size", fn_lh_txt);
        /* File rows */
        static const uint32_t list_sizes[] = {0U,0U,4096U,128U};
        char modified_date[18];
        int fi2;
        get_file_modified_str(modified_date);
        for (fi2=0; fi2<fcount && fi2<6; fi2++) {
            int ry = content_y + lh + 1 + fi2 * lh;
            if (ry + lh > content_y + content_h - 1) break;
            uint32_t row_bg = g_pref_darkmode ?
                ((fi2%2==0) ? RGB(28,28,30) : RGB(34,34,38)) :
                ((fi2%2==0) ? RGB(255,255,255) : RGB(248,248,252));
            vga_fill_rect(content_x, ry, content_w, lh, row_bg);
            vga_draw_hline(content_x, ry+lh-1, content_w, fn_lh_bd);
            /* Small folder icon */
            vga_fill_rect(lx, ry+3, 10, 9, RGB(41,128,185));
            vga_fill_rect(lx, ry+2, 5, 3, RGB(30,100,160));
            /* Name */
            vga_draw_string_trans(lx+14, ry+4, cur_folders[fi2].name, fn_sb_txt);
            /* Date */
            vga_draw_string_trans(lx+content_w/2, ry+4, modified_date, fn_sb_cat);
            /* Size */
            { char sizebuf[16];
              if (fi2 < 2) {
                  sizebuf[0] = '-';
                  sizebuf[1] = '-';
                  sizebuf[2] = 0;
              } else {
                  runtime_format_bytes(list_sizes[fi2<4?fi2:3], sizebuf, sizeof(sizebuf));
              }
              vga_draw_string_trans(lx+content_w-44, ry+4, sizebuf, fn_sb_cat); }
        }
    } else {
        /* Column view: 3 panes */
        int npanes = 3, pi;
        int pane_w = (content_w) / npanes;
        static const char *col_items[3][5] = {
            { "Applications","Desktop","Documents","Downloads","Movies" },
            { "Calculator","Calendar","Clock","Finder","Notes" },
            { NULL,NULL,NULL,NULL,NULL }
        };
        static const int col_sel[3] = { 1, 0, -1 };
        for (pi=0; pi<npanes; pi++) {
            int px2 = content_x + pi*pane_w;
            vga_fill_rect(px2, content_y, pane_w, content_h,
                pi%2==0?(g_pref_darkmode?RGB(28,28,30):RGB(248,248,252)):
                        (g_pref_darkmode?RGB(32,32,36):RGB(240,240,245)));
            vga_draw_vline(px2+pane_w-1, content_y, content_h,
                g_pref_darkmode?RGB(55,55,62):RGB(200,200,208));
            int iy;
            for (iy=0; iy<5; iy++) {
                if (!col_items[pi][iy]) break;
                int ry2 = content_y + 4 + iy*16;
                if (ry2+16 > content_y+content_h-1) break;
                int sel4 = (iy == col_sel[pi]);
                if (sel4) {
                    vga_fill_rect(px2+2, ry2, pane_w-4, 14,
                        g_pref_darkmode?RGB(0,80,190):RGB(0,122,255));
                }
                /* Folder icon */
                vga_fill_rect(px2+4, ry2+3, 10, 8,
                    sel4?RGB(180,220,255):(g_pref_darkmode?RGB(60,120,200):RGB(41,128,185)));
                vga_fill_rect(px2+4, ry2+2, 5, 3,
                    sel4?RGB(140,190,240):(g_pref_darkmode?RGB(45,95,170):RGB(30,100,160)));
                vga_draw_string_trans(px2+18, ry2+3, col_items[pi][iy],
                    sel4?RGB(255,255,255):(g_pref_darkmode?RGB(210,210,220):RGB(30,30,40)));
                /* Disclosure arrow for selected */
                if (sel4)
                    vga_draw_string_trans(px2+pane_w-12, ry2+3, ">",
                        sel4?RGB(220,240,255):(g_pref_darkmode?RGB(130,130,140):RGB(140,140,150)));
            }
        }
        /* Preview pane (3rd column — show info for selected item in 2nd col) */
        {
            int pp_x = content_x + 2*pane_w;
            vga_fill_rect(pp_x, content_y, pane_w, content_h,
                g_pref_darkmode?RGB(24,24,28):RGB(252,252,255));
            int py2 = content_y + 10;
            /* Big folder icon */
            int fic_x = pp_x + pane_w/2 - 18, fic_y = py2;
            vga_fill_rect(fic_x, fic_y+6, 36, 28, RGB(41,128,185));
            vga_fill_rect(fic_x, fic_y+4, 18, 6,  RGB(30,100,160));
            vga_fill_rect_alpha(fic_x, fic_y+6, 36, 8, RGB(255,255,255), 50);
            py2 += 40;
            vga_draw_string_trans(pp_x+pane_w/2-24, py2, "Calculator", g_pref_darkmode?RGB(200,200,208):RGB(30,30,40));
            py2 += 16;
            vga_draw_string_trans(pp_x+4, py2,    "Kind:    App", g_pref_darkmode?RGB(120,120,130):RGB(100,100,110)); py2+=12;
            vga_draw_string_trans(pp_x+4, py2,    "Size:    bundle", g_pref_darkmode?RGB(120,120,130):RGB(100,100,110)); py2+=12;
            { char month_day[12];
              uint32_t meta_col = g_pref_darkmode?RGB(120,120,130):RGB(100,100,110);
              get_month_day_str(month_day);
              vga_draw_string_trans(pp_x+4, py2, "Created:", meta_col);
              vga_draw_string_trans(pp_x+72, py2, month_day, meta_col); py2+=12; }
            { char month_day[12];
              uint32_t meta_col = g_pref_darkmode?RGB(120,120,130):RGB(100,100,110);
              get_month_day_str(month_day);
              vga_draw_string_trans(pp_x+4, py2, "Modified:", meta_col);
              vga_draw_string_trans(pp_x+76, py2, month_day, meta_col);
            }
        }
    }

    /* Status bar text */
    {
        int fcount_status = 0;
        char countbuf[12];
        char msg[24];
        int mpos = 0;
        (void)finder_current_folders(&fcount_status);
        int_to_str(fcount_status, countbuf);
        msg[0] = 0;
        apps4_append_text(msg, &mpos, sizeof(msg), countbuf);
        apps4_append_text(msg, &mpos, sizeof(msg), fcount_status == 1 ? " item" : " items");
        vga_draw_string_trans(wx + sb_w + 8, wy + win->h - 13, msg,
                              g_pref_darkmode?RGB(120,120,128):COLOR_TEXT_GRAY);
    }
    } /* end Finder block */

    /* ---- Reminders ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Reminders")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy+TITLEBAR_H+1;
        uint32_t rb = g_pref_darkmode?RGB(18,18,22):RGB(242,242,247);
        uint32_t rt = g_pref_darkmode?RGB(230,230,238):RGB(20,20,28);
        uint32_t rs = g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        uint32_t rg = g_pref_darkmode?RGB(50,50,58):RGB(210,210,215);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H-1, rb);

        /* Sidebar */
        int sb_w2 = ww<260 ? ww/3 : 90;
        uint32_t rsb = g_pref_darkmode?RGB(28,28,34):RGB(232,232,240);
        vga_fill_rect(wx+1, cy, sb_w2, wh-TITLEBAR_H-1, rsb);
        vga_draw_vline(wx+sb_w2, cy, wh-TITLEBAR_H-1, rg);

        /* Sidebar — lists start at wy+TITLEBAR_H+24 with 22px rows (matches event handler) */
        static const char *rlists[]={"Today","Scheduled","All","Flagged","Completed","Work","Personal"};
        static const uint32_t rlist_cols[]={RGB(255,149,0),RGB(255,59,48),RGB(0,122,255),RGB(255,149,0),RGB(110,110,118),RGB(0,122,255),RGB(147,44,246)};
        static const int rlist_counts[]={3,5,12,2,8,4,5};
        int ri3, rlist_start_y = wy+TITLEBAR_H+24;
        for (ri3=0; ri3<7; ri3++) {
            int ry2 = rlist_start_y+ri3*22;
            if (ry2+20 > wy+wh-2) break;
            if (ri3==5) vga_draw_hline(wx+4, ry2-2, sb_w2-8, rg);
            int sel3 = (ri3==g_reminders_sel_list);
            if (sel3) gui_draw_rounded_rect(wx+2, ry2, sb_w2-4, 20, 4, g_pref_darkmode?RGB(50,50,62):RGB(220,220,228));
            gui_draw_circle(wx+14, ry2+9, 7, rlist_cols[ri3]);
            vga_draw_string_trans(wx+26, ry2+5, rlists[ri3], sel3?RGB(0,122,255):rt);
            { char cb[4]; cb[0]='0'+rlist_counts[ri3]; cb[1]=0;
              vga_draw_string_trans(wx+sb_w2-14, ry2+5, cb, rs); }
        }

        /* Main area */
        int mx2 = wx+sb_w2+6;
        int mw2 = ww-sb_w2-8;
        /* rm_cy matches event handler: wy+TITLEBAR_H+8 */
        int rm_cy2 = wy+TITLEBAR_H+8;
        vga_draw_string_trans(mx2, rm_cy2+4, "Today", rt);
        { char rem_count[24]; int rcp=0;
          rem_count[0]=0;
          apps4_append_uint(rem_count, &rcp, sizeof(rem_count), (uint32_t)(3 + g_reminders_extra_items));
          apps4_append_text(rem_count, &rcp, sizeof(rem_count), " reminders");
          vga_draw_string_trans(mx2+52, rm_cy2+4, rem_count, rs); }
        vga_draw_hline(mx2, rm_cy2+18, mw2, rg);

        /* 3 interactive reminder items at positions matching event handler */
        /* Event handler: row rii at iy3=rm_cy+30+rii*34, checkbox cx4=wx+rm_sb_w+8+9=wx+107 */
        static const struct { const char *text; int flag; int minutes_from_now; } rems[] = {
            { "Buy groceries",       1, 30  },
            { "Team standup call",   0, 90  },
            { "Review pull request", 0, 210 },
        };
        int ri4;
        for (ri4=0; ri4<3; ri4++) {
            int iy3 = rm_cy2+30+ri4*34;  /* row top */
            if (iy3+30 > wy+wh-20) break;
            int is_done = (g_reminders_done>>ri4)&1;
            int chk_x = wx+sb_w2+8+9;  /* = wx+107, matches event handler cx4 */
            int chk_y = iy3+9;          /* = rm_cy+39+ri4*34, circle center */
            /* Checkbox circle */
            if (is_done) {
                gui_draw_circle(chk_x, chk_y, 9, RGB(0,122,255));
                vga_fill_rect(chk_x-5, chk_y-1, 10, 3, RGB(255,255,255));
                vga_fill_rect(chk_x-2, chk_y-4, 3, 8, RGB(255,255,255));
            } else {
                gui_draw_circle(chk_x, chk_y, 9, rg);
                gui_draw_circle(chk_x, chk_y, 7, rb);
            }
            /* Text beside checkbox */
            uint32_t rtxt2 = is_done ? rs : rt;
            {
                datetime_t rem_now;
                char rem_time[12];
                int rem_hour;
                int rem_minute;
                const char *rem_ampm;
                int rem_h12;
                get_current_datetime(&rem_now);
                rem_hour = (rem_now.hour + (rem_now.minute + rems[ri4].minutes_from_now) / 60) % 24;
                rem_minute = (rem_now.minute + rems[ri4].minutes_from_now) % 60;
                rem_ampm = rem_hour >= 12 ? "PM" : "AM";
                rem_h12 = rem_hour % 12;
                if (rem_h12 == 0) rem_h12 = 12;
                rem_time[0] = (char)('0' + (rem_h12 / 10));
                rem_time[1] = (char)('0' + (rem_h12 % 10));
                rem_time[2] = ':';
                rem_time[3] = (char)('0' + (rem_minute / 10));
                rem_time[4] = (char)('0' + (rem_minute % 10));
                rem_time[5] = ' ';
                rem_time[6] = rem_ampm[0];
                rem_time[7] = rem_ampm[1];
                rem_time[8] = 0;
                if (rem_time[0] == '0') {
                    rem_time[0] = rem_time[1];
                    rem_time[1] = ':';
                    rem_time[2] = rem_time[3];
                    rem_time[3] = rem_time[4];
                    rem_time[4] = ' ';
                    rem_time[5] = rem_ampm[0];
                    rem_time[6] = rem_ampm[1];
                    rem_time[7] = 0;
                }
                vga_draw_string_trans(chk_x+14, iy3+4,  rems[ri4].text, rtxt2);
                vga_draw_string_trans(chk_x+14, iy3+16, rem_time, rs);
            }
            if (rems[ri4].flag) {
                vga_draw_string_trans(wx+ww-14, iy3+4, "!", RGB(255,149,0));
            }
            vga_draw_hline(mx2, iy3+30, mw2, rg);
        }

        /* User-added pending items */
        { int extra_i;
          for (extra_i=0; extra_i<g_reminders_extra_items && extra_i<4; extra_i++) {
              int iy3x = rm_cy2+30+3*34+extra_i*24;
              char added_label[24];
              int alp = 0;
              if (iy3x+20 > wy+wh-44) break;
              added_label[0] = 0;
              apps4_append_text(added_label, &alp, sizeof(added_label), "New reminder ");
              apps4_append_uint(added_label, &alp, sizeof(added_label), (uint32_t)(extra_i + 1));
              gui_draw_circle(wx+sb_w2+8+9, iy3x+8, 8, rg);
              gui_draw_circle(wx+sb_w2+8+9, iy3x+8, 6, rb);
              vga_draw_string_trans(wx+sb_w2+22, iy3x+4, added_label, rt);
              vga_draw_hline(mx2, iy3x+20, mw2, rg);
          }
        }

        /* Two static items (done) */
        { static const char *done_items[]={"Call mom","Update meeting notes"};
          int ddi;
          for (ddi=0; ddi<2; ddi++) {
              int iy3d = rm_cy2+30+3*34+(g_reminders_extra_items<4?g_reminders_extra_items:4)*24+ddi*24;
              if (iy3d+20 > wy+wh-20) break;
              gui_draw_circle(wx+sb_w2+8+9, iy3d+8, 8, RGB(0,122,255));
              vga_fill_rect(wx+sb_w2+8+4, iy3d+7, 10, 2, RGB(255,255,255));
              vga_draw_string_trans(wx+sb_w2+22, iy3d+4, done_items[ddi], rs);
              vga_draw_hline(mx2, iy3d+20, mw2, rg);
          }
        }

        /* Add reminder button */
        { int add_y = wy+wh-24;
          vga_draw_hline(mx2, add_y-4, mw2, rg);
          gui_draw_circle(mx2+10, add_y+8, 8, g_pref_darkmode?RGB(40,40,50):RGB(225,225,235));
          vga_draw_string_trans(mx2+7, add_y+4, "+", RGB(0,122,255));
          vga_draw_string_trans(mx2+22, add_y+4, "Add Reminder", rs);
        }
        return 1;
    }

    /* ---- Translate ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Translate")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy+TITLEBAR_H+1;
        uint32_t trbg = g_pref_darkmode?RGB(18,18,22):RGB(248,248,252);
        uint32_t trtx = g_pref_darkmode?RGB(230,230,238):RGB(20,20,28);
        uint32_t trst = g_pref_darkmode?RGB(130,130,140):RGB(100,100,110);
        uint32_t trsp = g_pref_darkmode?RGB(50,50,58):RGB(210,210,215);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H-1, trbg);

        int hw2=ww/2-2;
        /* Language headers */
        uint32_t lhdr=g_pref_darkmode?RGB(30,30,36):RGB(240,240,248);
        vga_fill_rect(wx+1, cy, hw2, 22, lhdr);
        vga_fill_rect(wx+hw2+3, cy, hw2-2, 22, lhdr);
        vga_draw_vline(wx+hw2+1, cy, wh-TITLEBAR_H-1, trsp);
        vga_draw_hline(wx+1, cy+22, ww-2, trsp);
        vga_draw_string_trans(wx+(hw2-40)/2, cy+7, "English", trtx);
        vga_draw_string_trans(wx+hw2+3+(hw2-40)/2, cy+7, "Korean", trtx);
        /* Arrow in middle */
        gui_draw_circle(wx+hw2+1, cy+11, 8, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+hw2-2, cy+7, ">", RGB(0,122,255));

        /* Source text */
        int src_h=wh/2-26;
        vga_draw_string_trans(wx+8, cy+28, "Hello, how are you?", trtx);
        vga_draw_string_trans(wx+8, cy+44, "I am doing well, thank you.", trtx);
        /* Mic button */
        gui_draw_circle(wx+hw2/2, cy+src_h+2, 14, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+hw2/2-5, cy+src_h-4, "MIC", trst);

        /* Result area */
        vga_draw_hline(wx+1, cy+src_h+20, ww-2, trsp);
        uint32_t resbg=g_pref_darkmode?RGB(25,25,30):RGB(243,243,250);
        vga_fill_rect(wx+1, cy+src_h+21, ww-2, wh-TITLEBAR_H-src_h-22, resbg);
        /* Korean translation */
        vga_draw_string_trans(wx+8, cy+src_h+30, "Translation:", trst);
        /* Korean text in ASCII approximation */
        vga_draw_string_trans(wx+8, cy+src_h+44, "Annyeonghaseyo?", trtx);
        vga_draw_string_trans(wx+8, cy+src_h+58, "Jal jinaego itsseoyo, gamsahaeyo.", trtx);
        /* Speaker button */
        gui_draw_circle(wx+ww-20, cy+src_h+40, 12, g_pref_darkmode?RGB(44,44,52):RGB(220,220,228));
        vga_draw_string_trans(wx+ww-28, cy+src_h+36, "spk", trst);
        /* Favorites button */
        if (g_translate_favorites > 0) {
            char fav_line[32];
            char fav_num[8];
            int fp = 0;
            fav_line[0] = 0;
            apps4_append_text(fav_line, &fp, sizeof(fav_line), "Favorited ");
            runtime_format_uint((uint32_t)g_translate_favorites, fav_num, sizeof(fav_num));
            apps4_append_text(fav_line, &fp, sizeof(fav_line), fav_num);
            vga_draw_string_trans(wx+8, cy+wh-TITLEBAR_H-22, fav_line, RGB(0,122,255));
        } else {
            vga_draw_string_trans(wx+8, cy+wh-TITLEBAR_H-22, "* Add to Favorites", RGB(0,122,255));
        }
        return 1;
    }

    /* ---- Minesweeper ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Minesweeper")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int hdr_y = wy+TITLEBAR_H+1;
        int hdr_h = 30;
        uint32_t hdr_col = RGB(192,192,192);
        (void)wh;

        /* Header background with 3D border */
        vga_fill_rect(wx+1, hdr_y, ww-2, hdr_h, hdr_col);
        vga_fill_rect(wx+1, hdr_y, ww-2, 2, RGB(230,230,230));
        vga_fill_rect(wx+1, hdr_y, 2, hdr_h, RGB(230,230,230));
        vga_fill_rect(wx+1, hdr_y+hdr_h-2, ww-2, 2, RGB(100,100,100));
        vga_fill_rect(wx+ww-3, hdr_y, 2, hdr_h, RGB(100,100,100));

        /* Mine counter (left) */
        {
            int mc = g_mine_remaining;
            if (mc < 0) mc = 0;
            if (mc > 999) mc = 999;
            char mcs[4];
            mcs[0]=(char)('0'+mc/100); mcs[1]=(char)('0'+(mc%100)/10);
            mcs[2]=(char)('0'+mc%10); mcs[3]=0;
            int lx=wx+6, ly=hdr_y+4;
            vga_fill_rect(lx, ly, 34, 22, RGB(20,0,0));
            vga_draw_rect_outline(lx, ly, 34, 22, RGB(60,0,0));
            int di3,r3,c3;
            for(di3=0;di3<3;di3++) {
                unsigned char cc3=(unsigned char)mcs[di3];
                for(r3=0;r3<8;r3++) for(c3=0;c3<8;c3++)
                    if(font8x8[cc3][r3]&(1u<<c3))
                        vga_put_pixel(lx+4+di3*10+c3, ly+7+r3, RGB(200,20,20));
            }
        }

        /* Face button (center) */
        {
            int fx=wx+ww/2-12, fy=hdr_y+4;
            vga_fill_rect(fx, fy, 24, 22, RGB(220,220,0));
            vga_draw_rect_outline(fx, fy, 24, 22, RGB(150,150,0));
            vga_fill_rect(fx+1, fy+1, 23, 2, RGB(240,240,50));
            vga_fill_rect(fx+1, fy+1, 2, 21, RGB(240,240,50));
            if (g_mine_state==2)
                vga_draw_string_trans(fx+3, fy+7, "8)", RGB(0,0,0));
            else if (g_mine_state==3)
                vga_draw_string_trans(fx+3, fy+7, "X(", RGB(0,0,0));
            else
                vga_draw_string_trans(fx+3, fy+7, ":)", RGB(0,0,0));
        }

        /* Timer (right) */
        {
            int elapsed=0;
            if (g_mine_state==1) {
                elapsed=(int)((timer_ticks()-g_mine_start_tick)/1000);
                if(elapsed>999)elapsed=999;
            } else if (g_mine_state>=2) {
                elapsed=(int)((g_mine_end_tick-g_mine_start_tick)/1000);
                if(elapsed>999)elapsed=999;
            }
            char tms[4];
            tms[0]=(char)('0'+elapsed/100); tms[1]=(char)('0'+(elapsed%100)/10);
            tms[2]=(char)('0'+elapsed%10); tms[3]=0;
            int lx=wx+ww-42, ly=hdr_y+4;
            vga_fill_rect(lx, ly, 34, 22, RGB(20,0,0));
            vga_draw_rect_outline(lx, ly, 34, 22, RGB(60,0,0));
            int di3,r3,c3;
            for(di3=0;di3<3;di3++) {
                unsigned char cc3=(unsigned char)tms[di3];
                for(r3=0;r3<8;r3++) for(c3=0;c3<8;c3++)
                    if(font8x8[cc3][r3]&(1u<<c3))
                        vga_put_pixel(lx+4+di3*10+c3, ly+7+r3, RGB(200,20,20));
            }
        }

        /* Board background */
        int bx = wx + (ww-MINE_COLS*20)/2;
        int by2 = hdr_y + hdr_h + 2;
        vga_fill_rect(bx-1, by2-1, MINE_COLS*20+2, MINE_ROWS*20+2, RGB(100,100,100));
        vga_fill_rect(bx, by2, MINE_COLS*20, MINE_ROWS*20, hdr_col);

        /* Draw each cell */
        {
            static const uint32_t num_col[9] = {
                0, RGB(0,0,200), RGB(0,128,0), RGB(200,0,0),
                RGB(0,0,128), RGB(128,0,0), RGB(0,128,128),
                RGB(0,0,0), RGB(128,128,128)
            };
            int r, c;
            for(r=0;r<MINE_ROWS;r++) for(c=0;c<MINE_COLS;c++) {
                int px=bx+c*20, py=by2+r*20;
                /* Count adjacent mines for number display */
                int adj=0;
                { int dr2,dc2;
                  for(dr2=-1;dr2<=1;dr2++) for(dc2=-1;dc2<=1;dc2++) {
                      int nr2=r+dr2, nc2=c+dc2;
                      if((dr2||dc2)&&nr2>=0&&nr2<MINE_ROWS&&nc2>=0&&nc2<MINE_COLS)
                          adj+=g_mine_board[nr2][nc2];
                  }
                }
                if (g_mine_vis[r][c]) {
                    /* Revealed cell */
                    vga_fill_rect(px, py, 20, 20, RGB(192,192,192));
                    vga_draw_rect_outline(px, py, 20, 20, RGB(140,140,140));
                    if (g_mine_board[r][c]) {
                        /* Mine: red background + cross */
                        vga_fill_rect(px+1, py+1, 18, 18, RGB(220,50,50));
                        gui_draw_circle(px+10, py+10, 5, RGB(0,0,0));
                        vga_draw_hline(px+4, py+10, 12, RGB(0,0,0));
                        vga_draw_vline(px+10, py+4, 12, RGB(0,0,0));
                    } else if (adj > 0 && adj <= 8) {
                        char nc3[2]; nc3[0]=(char)('0'+adj); nc3[1]=0;
                        vga_draw_string_trans(px+6, py+6, nc3, num_col[adj]);
                    }
                } else if (g_mine_flag[r][c]) {
                    /* Flagged: raised cell + flag icon */
                    vga_fill_rect(px+1, py+1, 18, 18, hdr_col);
                    vga_fill_rect(px, py, 20, 2, RGB(230,230,230));
                    vga_fill_rect(px, py, 2, 20, RGB(230,230,230));
                    vga_fill_rect(px, py+18, 20, 2, RGB(100,100,100));
                    vga_fill_rect(px+18, py, 2, 20, RGB(100,100,100));
                    /* Flag pole+flag */
                    vga_fill_rect(px+10, py+3, 2, 13, RGB(0,0,0));
                    vga_fill_rect(px+5, py+3, 7, 6, RGB(220,20,20));
                    vga_draw_hline(px+7, py+16, 6, RGB(0,0,0));
                    /* Wrong flag X on loss */
                    if (g_mine_state==3 && !g_mine_board[r][c]) {
                        vga_draw_line(px+3, py+3, px+17, py+17, RGB(220,0,0));
                        vga_draw_line(px+17, py+3, px+3, py+17, RGB(220,0,0));
                    }
                } else {
                    /* Hidden: raised 3D cell */
                    vga_fill_rect(px+1, py+1, 18, 18, hdr_col);
                    vga_fill_rect(px, py, 20, 2, RGB(230,230,230));
                    vga_fill_rect(px, py, 2, 20, RGB(230,230,230));
                    vga_fill_rect(px, py+18, 20, 2, RGB(100,100,100));
                    vga_fill_rect(px+18, py, 2, 20, RGB(100,100,100));
                    /* On loss, show unflagged mines */
                    if (g_mine_state==3 && g_mine_board[r][c]) {
                        gui_draw_circle(px+10, py+10, 5, RGB(0,0,0));
                        vga_draw_hline(px+4, py+10, 12, RGB(0,0,0));
                        vga_draw_vline(px+10, py+4, 12, RGB(0,0,0));
                    }
                }
            }
        }

        /* Status bar */
        {
            int sb_y = by2 + MINE_ROWS*20 + 4;
            const char *status = (g_mine_state==0) ? "Click to start" :
                                 (g_mine_state==2) ? "You win! :)" :
                                 (g_mine_state==3) ? "Game over. :(" : "Playing...";
            vga_draw_string_trans(wx+4, sb_y, status,
                g_mine_state==2 ? RGB(52,199,89) :
                g_mine_state==3 ? RGB(255,59,48) :
                (g_pref_darkmode ? RGB(180,180,188) : RGB(80,80,80)));
        }
        return 1;
    }

    /* ---- Journal ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Journal")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy=wy+TITLEBAR_H+1;
        int ch2=wh-TITLEBAR_H-19;
        uint32_t jbg  = g_pref_darkmode?RGB(24,24,28):RGB(255,255,255);
        uint32_t jsbg = g_pref_darkmode?RGB(32,32,38):RGB(242,242,248);
        uint32_t jtx  = g_pref_darkmode?RGB(220,220,228):RGB(20,20,28);
        uint32_t jst  = g_pref_darkmode?RGB(130,130,140):RGB(110,110,120);
        uint32_t jbd  = g_pref_darkmode?RGB(55,55,62):RGB(210,210,216);
        uint32_t jacc = RGB(255,149,0);

        /* Sidebar */
        int sb_w = 120;
        vga_fill_rect(wx+1, cy, sb_w, ch2, jsbg);
        vga_draw_vline(wx+sb_w, cy, ch2, jbd);
        /* Sidebar header */
        vga_fill_rect(wx+1, cy, sb_w, 28, g_pref_darkmode?RGB(44,44,50):RGB(230,230,238));
        vga_draw_string_trans(wx+8, cy+4, "JOURNAL", jst);
        vga_draw_string_trans(wx+sb_w-14, cy+4, "+", jacc);
        vga_draw_hline(wx+1, cy+28, sb_w, jbd);

        /* Entry list */
        int i;
        for(i=0;i<g_journal_count&&i<JOURNAL_MAX;i++) {
            char jdate[16];
            int ey = cy+32+i*48;
            if(ey+48 > cy+ch2-10) break;
            apps4_format_journal_date(i, jdate, sizeof(jdate));
            uint32_t row_bg = (i==g_journal_sel) ?
                (g_pref_darkmode?RGB(50,50,58):RGB(220,225,240)) : jsbg;
            vga_fill_rect(wx+1, ey, sb_w-1, 46, row_bg);
            if(i==g_journal_sel)
                vga_fill_rect(wx+1, ey, 3, 46, jacc);
            gui_draw_circle(wx+10, ey+10, 4, jacc);
            vga_draw_string_trans(wx+18, ey+3, jdate, jst);
            /* Title (truncate to fit) */
            int tl=str_len(g_journal_titles[i]);
            if(tl>12) tl=12;
            char tbuf[13]; int ti3;
            for(ti3=0;ti3<tl;ti3++) tbuf[ti3]=g_journal_titles[i][ti3];
            tbuf[tl]=0;
            vga_draw_string_trans(wx+8, ey+14, tbuf, jtx);
            /* Body preview (first 12 chars) */
            { int bl=str_len(g_journal_bodies[i]);
              if(bl>14)bl=14;
              char bbuf[15]; int bi3;
              for(bi3=0;bi3<bl;bi3++) {
                  bbuf[bi3]=g_journal_bodies[i][bi3];
                  if(bbuf[bi3]=='\n') { bbuf[bi3]=0; bl=bi3; break; }
              }
              bbuf[bl]=0;
              vga_draw_string_trans(wx+8, ey+26, bbuf, jst);
            }
            vga_draw_hline(wx+1, ey+46, sb_w, jbd);
        }

        /* Main content area */
        int cx2=wx+sb_w+1;
        int cw2=ww-sb_w-2;
        vga_fill_rect(cx2, cy, cw2, ch2, jbg);
        /* Header bar */
        vga_fill_rect(cx2, cy, cw2, 28, g_pref_darkmode?RGB(44,44,50):RGB(230,230,238));
        vga_draw_hline(cx2, cy+28, cw2, jbd);
        /* Date */
        if(g_journal_sel>=0&&g_journal_sel<g_journal_count) {
            char jdate[16];
            apps4_format_journal_date(g_journal_sel, jdate, sizeof(jdate));
            vga_draw_string_trans(cx2+8, cy+8, jdate, jtx);
        }
        /* Divider line */
        vga_draw_hline(cx2+8, cy+50, cw2-16, jbd);
        /* Title */
        if(g_journal_sel>=0&&g_journal_sel<g_journal_count) {
            vga_draw_string_trans(cx2+8, cy+32, g_journal_titles[g_journal_sel], jtx);
        }
        /* Body text (multi-line) */
        if(g_journal_sel>=0&&g_journal_sel<g_journal_count) {
            const char *body = g_journal_bodies[g_journal_sel];
            int line_y = cy+56, lx3 = cx2+8;
            int max_w = cw2-16;
            int ci5=0, li=0;
            while(body[ci5] && line_y < cy+ch2-20 && li<8) {
                char lbuf[32]; int ll=0;
                while(body[ci5]&&body[ci5]!='\n'&&ll<max_w/8-1)
                    lbuf[ll++]=body[ci5++];
                lbuf[ll]=0;
                if(body[ci5]=='\n') ci5++;
                vga_draw_string_trans(lx3, line_y, lbuf, jtx);
                line_y+=14; li++;
            }
            /* Cursor if editing */
            if(g_journal_focused && ((timer_ticks()/400)%2==0))
                vga_fill_rect(lx3, line_y, 6, 10, jacc);
        }
        /* "+ Write New Entry" button */
        int btn_y = cy+ch2-22;
        vga_fill_rect_alpha(cx2+8, btn_y, cw2-16, 18, jacc, 200);
        vga_draw_string_trans(cx2+(cw2-10*8)/2, btn_y+4, "+ New Entry", RGB(255,255,255));
        return 1;
    }

    /* ---- Preview ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Preview")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy+TITLEBAR_H;
        uint32_t pvbg  = g_pref_darkmode ? RGB(30,30,35)   : RGB(245,245,248);
        uint32_t pvtb  = g_pref_darkmode ? RGB(40,40,46)   : RGB(232,232,238);
        uint32_t pvtxt = g_pref_darkmode ? RGB(220,220,228) : RGB(28,28,38);
        uint32_t pvsub = g_pref_darkmode ? RGB(120,120,130) : RGB(100,100,115);
        uint32_t pvsep = g_pref_darkmode ? RGB(55,55,62)   : RGB(205,205,212);
        uint32_t pvacc = RGB(0,122,255);
        vga_fill_rect(wx+1, cy, ww-2, wh-TITLEBAR_H, pvbg);
        /* Toolbar */
        vga_fill_rect(wx+1, cy, ww-2, 28, pvtb);
        vga_draw_hline(wx+1, cy+28, ww-2, pvsep);
        { static const char *tools[]={"<",">","-","+","Fit","Mark"};
          int ti2; for(ti2=0;ti2<6;ti2++) {
              int tx=wx+4+ti2*30;
              uint32_t tool_bg = (ti2==5 && g_preview_markup) ? pvacc : pvsep;
              uint32_t tool_fg = (ti2==5 && g_preview_markup) ? RGB(255,255,255) : pvtxt;
              vga_fill_rect_alpha(tx, cy+4, 26, 20, tool_bg, 80);
              vga_draw_string_trans(tx+(26-str_len(tools[ti2])*8)/2, cy+10, tools[ti2], tool_fg);
          }
        }
        /* Thumbnail sidebar */
        int ts_w = 60;
        vga_fill_rect(wx+1, cy+29, ts_w, wh-TITLEBAR_H-29, pvtb);
        vga_draw_vline(wx+ts_w+1, cy+29, wh-TITLEBAR_H-29, pvsep);
        { static const char *pnames[]={"Pg 1","Pg 2","Pg 3"};
          int pi2; for(pi2=0;pi2<3;pi2++) {
              int ty2=cy+34+pi2*48;
              uint32_t tbg=(pi2==g_preview_page)?(g_pref_darkmode?RGB(0,72,195):pvacc):pvbg;
              vga_fill_rect(wx+4, ty2, ts_w-6, 42, tbg);
              vga_draw_rect_outline(wx+4, ty2, ts_w-6, 42, (pi2==g_preview_page)?RGB(0,122,255):pvsep);
              /* Miniature page content */
              vga_fill_rect(wx+6, ty2+2, ts_w-10, 38, g_pref_darkmode?RGB(50,50,58):RGB(255,255,255));
              { int li2; for(li2=0;li2<5;li2++)
                    vga_draw_hline(wx+8, ty2+8+li2*6, ts_w-14, g_pref_darkmode?RGB(80,80,95):RGB(180,180,190)); }
              vga_draw_string_trans(wx+8, ty2+36, pnames[pi2], (pi2==g_preview_page)?RGB(255,255,255):pvsub);
          }
        }
        /* Main document view */
        int doc_x = wx+ts_w+4;
        int doc_w = ww-ts_w-6;
        int doc_y = cy+32;
        int doc_h = wh-TITLEBAR_H-32;
        /* Page shadow + white page */
        int pv_page_w = 160 * g_preview_zoom / 100;
        int pv_page_h = (doc_h - 20) * g_preview_zoom / 100;
        if (pv_page_w < 110) pv_page_w = 110;
        if (pv_page_w > doc_w - 12) pv_page_w = doc_w - 12;
        if (pv_page_h < 120) pv_page_h = 120;
        if (pv_page_h > doc_h - 10) pv_page_h = doc_h - 10;
        int pv_page_x = doc_x + (doc_w - pv_page_w) / 2;
        int pv_page_y = doc_y + 3;
        vga_fill_rect_alpha(pv_page_x+3, pv_page_y+3, pv_page_w, pv_page_h, RGB(0,0,0), 40);
        vga_fill_rect(pv_page_x, pv_page_y, pv_page_w, pv_page_h, RGB(255,255,255));
        vga_draw_rect_outline(pv_page_x, pv_page_y, pv_page_w, pv_page_h, RGB(200,200,210));
        /* Page content based on page index */
        int px2=pv_page_x+8, py2=pv_page_y+11;
        if(g_preview_page==0) {
            vga_draw_string_trans(px2, py2,    "MyOS Preview", pvtxt);
            vga_draw_hline(px2, py2+12, pv_page_w-16, pvsep);
            { int li2; for(li2=0;li2<7 && py2+22+li2*10<pv_page_y+pv_page_h-12;li2++) {
                  int line_w = pv_page_w - 20 - li2*8;
                  if (line_w < 40) line_w = 40;
                  vga_draw_hline(px2, py2+20+li2*10, line_w, pvsep);
                  vga_draw_hline(px2, py2+22+li2*10, line_w-8, g_pref_darkmode?RGB(50,50,58):RGB(210,210,220));
            } }
            /* Image frame */
            if (pv_page_h > 150) {
                int img_w = pv_page_w - 60;
                int img_h = 48;
                if (img_w > 120) img_w = 120;
                if (img_w < 70) img_w = 70;
                vga_fill_rect(px2+20, py2+88, img_w, img_h, RGB(180,210,240));
                { int mi2; for(mi2=0;mi2<img_w;mi2++) {
                      int mhv=14+(mi2*13%24);
                      vga_fill_rect(px2+20+mi2, py2+88+img_h-mhv, 1, mhv, RGB(40,80,140));
                  }
                }
                gui_draw_circle(px2+img_w-30, py2+102, 10, RGB(255,220,60));
            }
        } else if(g_preview_page==1) {
            vga_draw_string_trans(px2, py2, "Chapter 2", pvtxt);
            vga_draw_hline(px2, py2+12, pv_page_w-16, pvsep);
            { int li2; for(li2=0;li2<12 && py2+20+li2*10<pv_page_y+pv_page_h-14;li2++)
                  vga_draw_hline(px2, py2+20+li2*10, pv_page_w-22, pvsep); }
        } else {
            vga_draw_string_trans(px2, py2, "Chapter 3", pvtxt);
            vga_draw_hline(px2, py2+12, pv_page_w-16, pvsep);
            { int li2; for(li2=0;li2<10 && py2+20+li2*10<pv_page_y+pv_page_h-14;li2++) {
                  int line_w = pv_page_w - 40 + (li2%3)*10;
                  if (line_w > pv_page_w-18) line_w = pv_page_w-18;
                  vga_draw_hline(px2, py2+20+li2*10, line_w, pvsep);
              } }
        }
        if (g_preview_markup) {
            vga_draw_hline(pv_page_x+20, pv_page_y+pv_page_h-32, pv_page_w-40, RGB(255,59,48));
            vga_draw_string_trans(pv_page_x+24, pv_page_y+pv_page_h-46, "Marked", RGB(255,59,48));
        }
        /* Page indicator */
        { char pbuf[28]; char zbuf[8]; int pp=0; int pv=g_preview_page+1;
          pbuf[0]=0;
          apps4_append_text(pbuf, &pp, sizeof(pbuf), "Page ");
          apps4_append_uint(pbuf, &pp, sizeof(pbuf), (uint32_t)pv);
          apps4_append_text(pbuf, &pp, sizeof(pbuf), " of 3  ");
          runtime_format_percent(g_preview_zoom, zbuf, sizeof(zbuf));
          apps4_append_text(pbuf, &pp, sizeof(pbuf), zbuf);
          vga_draw_string_trans(doc_x+(doc_w-str_len(pbuf)*8)/2, doc_y+doc_h-14, pbuf, pvsub); }
        return 1;
    }

    /* ---- Apple TV ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Apple TV")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy+TITLEBAR_H;
        /* Cinematic dark background */
        { int ri2; for(ri2=0; ri2<wh-TITLEBAR_H; ri2++) {
              uint8_t rv=(uint8_t)(8+ri2*8/(wh-TITLEBAR_H));
              vga_draw_hline(wx+1, cy+ri2, ww-2, RGB(rv,rv/2,0));
          }
        }
        vga_fill_rect_alpha(wx+1, cy, ww-2, wh-TITLEBAR_H, RGB(0,0,0), 160);
        /* Top nav */
        { static const char *navs[]={"Watch Now","Movies","TV Shows","Sports","Kids"};
          int ni2; for(ni2=0;ni2<5;ni2++) {
              int nx=wx+8+ni2*((ww-16)/5);
              uint32_t nc2=(ni2==g_atv_sel)?RGB(255,255,255):RGB(150,150,155);
              vga_draw_string_trans(nx+(((ww-16)/5)-str_len(navs[ni2])*8)/2, cy+6, navs[ni2], nc2);
              if(ni2==g_atv_sel)
                  vga_fill_rect(nx+(((ww-16)/5)-16)/2, cy+16, 16, 2, RGB(255,255,255));
          }
          vga_draw_hline(wx+1, cy+20, ww-2, RGB(50,50,55));
        }
        /* Featured content hero */
        { int hw=ww-8, hh=80;
          int hx=wx+4, hy=cy+26;
          /* Gradient hero bg */
          { int ri2; for(ri2=0;ri2<hh;ri2++) {
                uint8_t rr=(uint8_t)(20+ri2*60/hh);
                uint8_t rb=(uint8_t)(60+ri2*80/hh);
                vga_draw_hline(hx, hy+ri2, hw, RGB(rr,rr/3,rb));
            }
          }
          /* Abstract art */
          gui_draw_circle(hx+hw*3/4, hy+hh/2, 30, RGB(255,80,30));
          gui_draw_circle(hx+hw*3/4+10, hy+hh/2-8, 22, RGB(220,40,10));
          vga_fill_rect_alpha(hx+hw*3/4-35, hy+hh/2-4, 70, 8, RGB(255,255,255), 60);
          /* Title overlay */
          vga_draw_string_trans(hx+8, hy+hh-28, "FEATURED", RGB(255,255,255));
          vga_draw_string_trans(hx+8, hy+hh-16, "Top 10 Tonight", RGB(200,200,200));
          /* Play button */
          vga_fill_rect_alpha(hx+8, hy+hh-42, 52, 16, RGB(255,255,255), 220);
          vga_draw_string_trans(hx+14, hy+hh-38, g_atv_playing ? "Pause" : "Play", RGB(0,0,0));
          if (g_atv_playing)
              vga_draw_string_trans(hx+64, hy+hh-38, "Now Playing", RGB(255,255,255));
        }
        /* Content rows */
        { static const char *rowt[]={"Continue Watching","Top Picks for You","New Releases"};
          static const uint32_t rowc[]={RGB(255,100,50),RGB(100,180,255),RGB(150,255,100)};
          int ri2; for(ri2=0;ri2<3;ri2++) {
              int ry=cy+114+ri2*38;
              if(ry+36 > wy+wh-4) break;
              vga_draw_string_trans(wx+6, ry, rowt[ri2], RGB(200,200,205));
              /* Show thumbnails */
              int ti2; for(ti2=0;ti2<5;ti2++) {
                  int tx=wx+6+ti2*((ww-12)/5);
                  int tw=(ww-12)/5-4;
                  int th=26;
                  if(tx+tw > wx+ww-4) break;
                  vga_fill_rect(tx, ry+10, tw, th, rowc[ri2]);
                  vga_fill_rect_alpha(tx, ry+10, tw, th/2, RGB(255,255,255), 20);
                  /* Title text */
                  char tn[4]; tn[0]='A'+ti2; tn[1]='p'; tn[2]='0'+ri2; tn[3]=0;
                  vga_draw_string_trans(tx+2, ry+14, tn, RGB(255,255,255));
              }
          }
        }
        /* Bottom tab bar */
        vga_fill_rect(wx+1, wy+wh-20, ww-2, 18, RGB(15,15,18));
        vga_draw_hline(wx+1, wy+wh-20, ww-2, RGB(50,50,55));
        { static const char *tabs[]={"Home","Library","Search","Account"};
          int ti2; for(ti2=0;ti2<4;ti2++) {
              int tx=wx+4+ti2*((ww-8)/4);
              vga_draw_string_trans(tx+((ww-8)/4-str_len(tabs[ti2])*8)/2, wy+wh-16, tabs[ti2],
                  ti2==g_atv_bottom_tab?RGB(255,255,255):RGB(120,120,125));
          }
        }
        return 1;
    }

    return 0;
}
