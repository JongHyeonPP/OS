#include "gui_internal.h"

static void apps3_append_text(char *buf, int *pos, int max, const char *text) {
    int i = 0;
    if (!buf || max <= 0 || !text) return;
    while (text[i] && *pos + 1 < max) buf[(*pos)++] = text[i++];
    buf[*pos] = 0;
}

static void apps3_append_uint(char *buf, int *pos, int max, uint32_t value) {
    char nbuf[12];
    int i = 0;
    runtime_format_uint(value, nbuf, sizeof(nbuf));
    while (nbuf[i] && *pos + 1 < max) buf[(*pos)++] = nbuf[i++];
    buf[*pos] = 0;
}

static void apps3_format_uint_suffix(uint32_t value, const char *suffix, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0) return;
    buf[0] = 0;
    apps3_append_uint(buf, &pos, max, value);
    apps3_append_text(buf, &pos, max, suffix);
}

static void apps3_append_bytes(char *buf, int *pos, int max, uint32_t bytes) {
    char bbuf[16];
    int i = 0;
    runtime_format_bytes(bytes, bbuf, sizeof(bbuf));
    while (bbuf[i] && *pos + 1 < max) buf[(*pos)++] = bbuf[i++];
    buf[*pos] = 0;
}

static void apps3_format_display(const runtime_system_info_t *sys, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0 || !sys) return;
    buf[0] = 0;
    apps3_append_uint(buf, &pos, max, sys->display_width);
    apps3_append_text(buf, &pos, max, " x ");
    apps3_append_uint(buf, &pos, max, sys->display_height);
    apps3_append_text(buf, &pos, max, " @ ");
    apps3_append_uint(buf, &pos, max, sys->display_bpp);
    apps3_append_text(buf, &pos, max, "bpp");
}

static void apps3_format_hms_age(uint32_t age_seconds, char *buf, int max) {
    datetime_t now;
    uint32_t seconds;
    if (!buf || max < 9) return;
    get_current_datetime(&now);
    seconds = (uint32_t)(now.hour * 3600 + now.minute * 60 + now.second);
    seconds = (seconds + 86400U - (age_seconds % 86400U)) % 86400U;
    buf[0] = (char)('0' + (seconds / 36000U) % 10U);
    buf[1] = (char)('0' + (seconds / 3600U) % 10U);
    buf[2] = ':';
    buf[3] = (char)('0' + ((seconds / 60U) % 60U) / 10U);
    buf[4] = (char)('0' + ((seconds / 60U) % 60U) % 10U);
    buf[5] = ':';
    buf[6] = (char)('0' + (seconds % 60U) / 10U);
    buf[7] = (char)('0' + (seconds % 60U) % 10U);
    buf[8] = 0;
}

static void apps3_format_timecode(uint32_t frame_offset, char *buf, int max) {
    uint32_t total_frames;
    uint32_t seconds;
    uint32_t frames;
    uint32_t minutes;
    uint32_t hours;
    if (!buf || max < 12) return;
    total_frames = ((timer_ticks() / 33U) + frame_offset) % (24U * 60U * 60U * 30U);
    frames = total_frames % 30U;
    seconds = total_frames / 30U;
    hours = (seconds / 3600U) % 24U;
    minutes = (seconds / 60U) % 60U;
    seconds %= 60U;
    buf[0] = (char)('0' + hours / 10U);
    buf[1] = (char)('0' + hours % 10U);
    buf[2] = ':';
    buf[3] = (char)('0' + minutes / 10U);
    buf[4] = (char)('0' + minutes % 10U);
    buf[5] = ':';
    buf[6] = (char)('0' + seconds / 10U);
    buf[7] = (char)('0' + seconds % 10U);
    buf[8] = ':';
    buf[9] = (char)('0' + frames / 10U);
    buf[10] = (char)('0' + frames % 10U);
    buf[11] = 0;
}

static void apps3_format_seconds_centis(uint32_t elapsed_ms, char *buf, int max) {
    uint32_t minutes;
    uint32_t seconds;
    uint32_t centis;
    if (!buf || max < 9) return;
    minutes = (elapsed_ms / 60000U) % 100U;
    seconds = (elapsed_ms / 1000U) % 60U;
    centis = (elapsed_ms % 1000U) / 10U;
    buf[0] = (char)('0' + (minutes / 10U));
    buf[1] = (char)('0' + (minutes % 10U));
    buf[2] = ':';
    buf[3] = (char)('0' + (seconds / 10U));
    buf[4] = (char)('0' + (seconds % 10U));
    buf[5] = '.';
    buf[6] = (char)('0' + (centis / 10U));
    buf[7] = (char)('0' + (centis % 10U));
    buf[8] = 0;
}

int draw_apps_group3(int idx) {
    if (idx < 0 || idx >= g_num_windows) return 0;

    /* Migration Assistant window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Migration Assistant")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t ma_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,252);
        uint32_t ma_hd  = g_pref_darkmode ? RGB(44,44,48)    : RGB(232,232,236);
        uint32_t ma_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t ma_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t ma_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t ma_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, ma_bg);
        /* Header with icon */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 54, ma_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+55, ww-2, ma_sep);
        /* Migration icon (two arrows) */
        int ic_cx = wx+30, ic_cy = wy+TITLEBAR_H+27;
        gui_draw_circle(ic_cx, ic_cy, 18, ma_acc);
        /* Forward arrow */
        vga_draw_hline(ic_cx-10, ic_cy-5, 16, RGB(255,255,255));
        vga_draw_line(ic_cx+4, ic_cy-9, ic_cx+8, ic_cy-5, RGB(255,255,255));
        vga_draw_line(ic_cx+4, ic_cy-1, ic_cx+8, ic_cy-5, RGB(255,255,255));
        /* Back arrow */
        vga_draw_hline(ic_cx-10, ic_cy+5, 16, RGB(255,255,255));
        vga_draw_line(ic_cx-10, ic_cy+5, ic_cx-6, ic_cy+1, RGB(255,255,255));
        vga_draw_line(ic_cx-10, ic_cy+5, ic_cx-6, ic_cy+9, RGB(255,255,255));
        vga_draw_string_trans(wx+55, wy+TITLEBAR_H+15, "Migration Assistant", ma_txt);
        vga_draw_string_trans(wx+55, wy+TITLEBAR_H+28, "Transfer data to this Mac", ma_sub);
        /* Steps */
        int step_y = wy+TITLEBAR_H+64;
        static const char *ma_steps[] = {
            "1. Choose a source",
            "2. Connect devices",
            "3. Select data to transfer",
            "4. Migrate",
        };
        static const char *ma_descs[] = {
            "From a Mac, Time Machine, or startup disk",
            "Use Thunderbolt, USB, or Wi-Fi network",
            "Applications, User accounts, Settings",
            "This may take several hours",
        };
        int ms;
        for (ms=0; ms<4; ms++) {
            int my2 = step_y + ms*38;
            if (my2+30 > wy+wh-50) break;
            /* Circle with number */
            { int step_done = (ms <= g_migration_step);
              gui_draw_circle(wx+16, my2+8, 9, step_done ? ma_acc : ma_sep);
              char snum[2]; snum[0]='1'+ms; snum[1]=0;
              vga_draw_string_trans(wx+12, my2+4, snum, step_done ? RGB(255,255,255) : ma_sub);
              vga_draw_string_trans(wx+28, my2+2,  ma_steps[ms],  step_done ? ma_txt : ma_sub); }
            vga_draw_string_trans(wx+28, my2+14, ma_descs[ms],  ma_sub);
            vga_draw_hline(wx+8, my2+30, ww-16, ma_sep);
        }
        /* Source selection area */
        int src_y = step_y + 30;
        if (src_y + 40 < wy+wh-50) {
            static const char *src_opts[] = { "From a Mac", "Time Machine", "Windows PC" };
            int so;
            for (so=0; so<3; so++) {
                int ox = wx+6+so*(ww-18)/3;
                int ow = (ww-18)/3-2;
                int is_sel = (so==g_migration_source);
                vga_fill_rect(ox, src_y, ow, 28, is_sel ? ma_acc : (g_pref_darkmode?RGB(44,44,48):RGB(230,230,235)));
                gui_draw_rounded_rect_outline(ox, src_y, ow, 28, 3, is_sel ? ma_acc : ma_sep);
                int sl2 = (int)(str_len(src_opts[so])*8);
                vga_draw_string_trans(ox+(ow-sl2)/2, src_y+10, src_opts[so],
                    is_sel ? RGB(255,255,255) : ma_sub);
            }
        }
        /* Continue button */
        int btn_y = wy+wh-40;
        int btn_w = 80, btn_x = wx+ww-btn_w-8;
        vga_fill_rect(btn_x, btn_y, btn_w, 22, ma_acc);
        gui_draw_rounded_rect_outline(btn_x, btn_y, btn_w, 22, 4, ma_acc);
        { const char *next_label = g_migration_step >= 3 ? "Done" : "Continue";
          int bl=str_len(next_label)*8; vga_draw_string_trans(btn_x+(btn_w-bl)/2, btn_y+7, next_label, RGB(255,255,255)); }
        /* Back button */
        int back_x = wx+8;
        vga_fill_rect(back_x, btn_y, 50, 22, g_pref_darkmode?RGB(55,55,60):RGB(220,220,225));
        gui_draw_rounded_rect_outline(back_x, btn_y, 50, 22, 4, ma_sep);
        { int bl=str_len("Back")*8; vga_draw_string_trans(back_x+(50-bl)/2, btn_y+7, "Back", ma_txt); }
        return 1;
    }

    /* ---- Screen Time ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Screen Time")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t st_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,250);
        uint32_t st_hd  = g_pref_darkmode ? RGB(44,44,48)    : RGB(235,235,240);
        uint32_t st_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t st_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t st_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t st_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, st_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 44, st_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+45, ww-2, st_sep);
        /* Screen time clock icon */
        gui_draw_circle(wx+18, wy+TITLEBAR_H+22, 14, RGB(52,199,89));
        vga_draw_string_trans(wx+13, wy+TITLEBAR_H+17, "ST", RGB(255,255,255));
        vga_draw_string_trans(wx+36, wy+TITLEBAR_H+9, "Screen Time", st_txt);
        { char st_today[32];
          char st_minutes[16];
          int stp = 0;
          uint32_t minutes_today = (timer_ticks() / 60000U) % (24U * 60U);
          runtime_format_minutes((int)minutes_today, st_minutes, sizeof(st_minutes));
          st_today[0] = 0;
          apps3_append_text(st_today, &stp, sizeof(st_today), "Usage: ");
          apps3_append_text(st_today, &stp, sizeof(st_today), st_minutes);
          vga_draw_string_trans(wx+36, wy+TITLEBAR_H+23, st_today, st_acc); }
        /* Tabs */
        int tab_y = wy+TITLEBAR_H+50;
        {
            int hw = (ww-20)/2;
            uint32_t t0c = (g_screen_time_tab==0)?st_acc:(g_pref_darkmode?RGB(44,44,48):RGB(215,215,220));
            uint32_t t1c = (g_screen_time_tab==1)?st_acc:(g_pref_darkmode?RGB(44,44,48):RGB(215,215,220));
            gui_draw_rounded_rect(wx+6, tab_y, hw, 22, 4, t0c);
            gui_draw_rounded_rect(wx+10+hw, tab_y, hw, 22, 4, t1c);
            uint32_t t0t = (g_screen_time_tab==0)?RGB(255,255,255):st_txt;
            uint32_t t1t = (g_screen_time_tab==1)?RGB(255,255,255):st_txt;
            vga_draw_string_trans(wx+6+(hw-72)/2, tab_y+7, "App Usage", t0t);
            vga_draw_string_trans(wx+10+hw+(hw-88)/2, tab_y+7, "Downtime", t1t);
        }
        /* Usage history */
        int chart_y = tab_y + 30;
        int chart_h = 70;
        int chart_x = wx+20;
        int chart_w = ww-24;
        vga_fill_rect(chart_x, chart_y, chart_w, chart_h, g_pref_darkmode?RGB(36,36,40):RGB(245,248,250));
        vga_draw_rect_outline(chart_x, chart_y, chart_w, chart_h, st_sep);
        {
            int bar_w2 = (chart_w-8)/7;
            int di2;
            uint32_t base = (timer_ticks() / 60000U) % 50U;
            for (di2=0; di2<7; di2++) {
                int bx2 = chart_x+4+di2*bar_w2;
                int sample = 16 + (int)((base + (uint32_t)di2 * 11U) % 45U);
                int bh2 = sample;
                if (bh2 > chart_h-18) bh2 = chart_h-18;
                vga_draw_vline(bx2+bar_w2/2, chart_y+8, chart_h-16, g_pref_darkmode?RGB(50,50,55):RGB(220,224,228));
                vga_fill_rect(bx2+4, chart_y+chart_h-6-bh2, bar_w2-8, bh2, st_acc);
            }
        }
        /* App breakdown */
        int list_y = chart_y+chart_h+8;
        vga_draw_hline(wx+1, list_y, ww-2, st_sep);
        list_y += 6;
        vga_draw_string_trans(wx+8, list_y, "MOST USED", st_sub);
        list_y += 14;
        {
            static const char *apps[] = {"Safari", "Terminal", "Mail"};
            int ai;
            for (ai=0; ai<3; ai++) {
                char mins[16];
                int row_y = list_y + ai * 20;
                int minutes = 18 + (int)(((timer_ticks() / 60000U) + (uint32_t)ai * 13U) % 70U);
                runtime_format_minutes(minutes, mins, sizeof(mins));
                vga_draw_string_trans(wx+24, row_y+4, apps[ai], st_txt);
                vga_draw_string_trans(wx+ww-72, row_y+4, mins, st_sub);
                vga_fill_rect(wx+84, row_y+8, (ww-170) * minutes / 90, 5, st_acc);
            }
        }
        /* Bottom: limit row */
        int lim_y = wy+wh-26;
        vga_draw_hline(wx+1, lim_y, ww-2, st_sep);
        vga_draw_string_trans(wx+8, lim_y+9, g_screen_time_tab ? "Downtime: 10:00 PM - 7:00 AM" : "Limit: Productivity 2h", st_sub);
        return 1;
    }

    /* ---- Passwords ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Passwords")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t pw_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,250);
        uint32_t pw_sb  = g_pref_darkmode ? RGB(36,36,40)    : RGB(235,235,240);
        uint32_t pw_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t pw_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t pw_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t pw_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, pw_bg);
        /* Sidebar */
        int sb_w = 100;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, sb_w, wh-TITLEBAR_H-2, pw_sb);
        vga_draw_vline(wx+sb_w+1, wy+TITLEBAR_H+1, wh-TITLEBAR_H-2, pw_sep);
        /* Lock icon header */
        gui_draw_circle(wx+16, wy+TITLEBAR_H+14, 10, RGB(0,122,255));
        vga_draw_string_trans(wx+11, wy+TITLEBAR_H+9, "PW", RGB(255,255,255));
        vga_draw_string_trans(wx+30, wy+TITLEBAR_H+10, "Passwords", pw_txt);
        /* Search bar in sidebar */
        int sch_y = wy+TITLEBAR_H+28;
        vga_fill_rect(wx+4, sch_y, sb_w-6, 16, g_pref_darkmode?RGB(44,44,48):RGB(255,255,255));
        vga_draw_rect_outline(wx+4, sch_y, sb_w-6, 16,
            g_passwords_search_focused ? RGB(0,122,255) : pw_sep);
        vga_draw_string_trans(wx+8, sch_y+4,
            g_passwords_search_focused ? "Search*" : "Search", pw_sub);
        /* Sidebar categories */
        int sit_y = sch_y + 22;
        {
            static const char *pw_cats[] = {"All","My Passwords","iCloud","Never Saved",NULL};
            static const int pw_cnts[] = {12,8,4,2};
            int ci;
            for (ci=0; pw_cats[ci]; ci++) {
                int cy3 = sit_y+ci*20;
                if (cy3+16 > wy+wh-30) break;
                int is_sel = (ci==g_passwords_sel);
                if (is_sel) vga_fill_rect(wx+2, cy3, sb_w-2, 18, pw_acc);
                vga_draw_string_trans(wx+6, cy3+5, pw_cats[ci], is_sel?RGB(255,255,255):pw_txt);
                char cnt[4]; int_to_str(pw_cnts[ci], cnt);
                int cw2=str_len(cnt)*8+6;
                uint32_t badge_c = is_sel ? RGB(100,160,255) : pw_sep;
                vga_fill_rect(wx+sb_w-cw2-3, cy3+4, cw2, 12, badge_c);
                vga_draw_string_trans(wx+sb_w-cw2-1, cy3+6, cnt, is_sel?RGB(255,255,255):pw_sub);
            }
        }
        /* Main list column headers */
        int hdr_y = wy+TITLEBAR_H+2;
        vga_fill_rect(wx+sb_w+2, hdr_y, ww-sb_w-4, 24, g_pref_darkmode?RGB(44,44,48):RGB(238,238,242));
        vga_draw_hline(wx+sb_w+2, hdr_y+24, ww-sb_w-4, pw_sep);
        int main_x = wx+sb_w+8;
        int main_w = ww-sb_w-12;
        vga_draw_string_trans(main_x, hdr_y+8, "Website", pw_sub);
        vga_draw_string_trans(main_x+main_w/2, hdr_y+8, "Username", pw_sub);
        vga_draw_string_trans(main_x+main_w-48, hdr_y+8, "Password", pw_sub);
        /* Password rows */
        int entry_y = hdr_y+28;
        {
            static const char *pw_sites[] = {"github.com","google.com","apple.com","twitter.com","amazon.com","netflix.com","dropbox.com","slack.com"};
            static const char *pw_users[] = {"user@email.com","myname@gmail","me@icloud.com","user@email.com","user@email.com","me@gmail.com","user@email.com","user@work.com"};
            int pe;
            for (pe=0; pe<8; pe++) {
                int ey = entry_y+pe*22;
                if (ey+18 > wy+wh-28) break;
                int is_sel2 = (pe==g_passwords_entry && g_passwords_sel<4);
                if (is_sel2) vga_fill_rect_alpha(wx+sb_w+2, ey, ww-sb_w-4, 20, pw_acc, 50);
                /* Key icon */
                gui_draw_circle(main_x+5, ey+8, 5, RGB(255,180,0));
                gui_draw_circle_outline(main_x+5, ey+8, 5, RGB(200,140,0));
                vga_draw_string_trans(main_x+12, ey+4, pw_sites[pe], pw_txt);
                vga_draw_string_trans(main_x+main_w/2, ey+4, pw_users[pe], pw_sub);
                vga_draw_string_trans(main_x+main_w-48, ey+4, "* * * * * *", pw_sub);
                vga_draw_string_trans(main_x+main_w-14, ey+4, ">", pw_sub);
                if (pe<7) vga_draw_hline(main_x, ey+20, main_w, pw_sep);
            }
        }
        /* Bottom bar */
        int bot_y2 = wy+wh-24;
        vga_draw_hline(wx+1, bot_y2, ww-2, pw_sep);
        if (g_passwords_added > 0)
            vga_draw_string_trans(main_x, bot_y2-14, "New password saved", RGB(52,199,89));
        { char pw_total[24]; int ptp = 0;
          pw_total[0] = 0;
          apps3_append_uint(pw_total, &ptp, sizeof(pw_total), (uint32_t)(12 + g_passwords_added));
          apps3_append_text(pw_total, &ptp, sizeof(pw_total), " passwords");
          vga_draw_string_trans(wx+8, bot_y2+8, pw_total, pw_sub); }
        vga_fill_rect(wx+ww-44, bot_y2+4, 36, 16, pw_acc);
        gui_draw_rounded_rect_outline(wx+ww-44, bot_y2+4, 36, 16, 3, pw_acc);
        vga_draw_string_trans(wx+ww-36, bot_y2+8, "+ Add", RGB(255,255,255));
        return 1;
    }

    /* ---- Numbers (spreadsheet) ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Numbers")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t nb_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);
        uint32_t nb_hd  = g_pref_darkmode ? RGB(44,44,48)    : RGB(240,240,245);
        uint32_t nb_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t nb_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t nb_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t nb_acc = RGB(0,122,255);
        uint32_t nb_sel = g_pref_darkmode ? RGB(0,60,160)    : RGB(212,234,255);
        uint32_t nb_hdr_col = g_pref_darkmode ? RGB(200,210,255) : RGB(0,0,150);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, nb_bg);
        /* Toolbar */
        int tb_h = 24;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, tb_h, nb_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+tb_h+1, ww-2, nb_sep);
        {
            static const char *nb_tools[] = {"Table","Chart","Text","Shape"};
            int ti;
            for (ti=0; ti<4; ti++) {
                int tx2=wx+8+ti*50;
                uint32_t tbg2 = (ti==g_numbers_tool)?nb_acc:(g_pref_darkmode?RGB(55,55,60):RGB(210,210,215));
                gui_draw_rounded_rect(tx2, wy+TITLEBAR_H+4, 42, 16, 3, tbg2);
                int tl3=str_len(nb_tools[ti])*8;
                vga_draw_string_trans(tx2+(42-tl3)/2, wy+TITLEBAR_H+8, nb_tools[ti], ti==g_numbers_tool?RGB(255,255,255):nb_txt);
            }
            /* Sum formula button */
            int sum_x = wx+ww-50;
            gui_draw_rounded_rect(sum_x, wy+TITLEBAR_H+4, 42, 16, 3, g_pref_darkmode?RGB(55,55,60):RGB(210,210,215));
            vga_draw_string_trans(sum_x+10, wy+TITLEBAR_H+8, "=Sum", nb_sub);
        }
        /* Formula bar */
        int fb_y = wy+TITLEBAR_H+tb_h+2;
        vga_fill_rect(wx+1, fb_y, 32, 18, nb_hd);
        { char cell_ref[4];
          cell_ref[0] = (char)('A' + g_numbers_sel_col);
          cell_ref[1] = (char)('1' + g_numbers_sel_row);
          cell_ref[2] = 0;
          vga_draw_string_trans(wx+4, fb_y+5, cell_ref, nb_sub); }
        vga_draw_vline(wx+32, fb_y, 18, nb_sep);
        vga_fill_rect(wx+33, fb_y, ww-35, 18, g_pref_darkmode?RGB(36,36,40):RGB(252,252,254));
        vga_draw_string_trans(wx+36, fb_y+5, g_numbers_tool==1?"Chart from selection":(g_numbers_tool==2?"Text cell":"Product"), nb_txt);
        vga_draw_hline(wx+1, fb_y+19, ww-2, nb_sep);
        /* Grid */
        int grid_y = fb_y+20;
        int col_hdr_h = 18;
        int row_hdr_w = 28;
        int col_w2 = (ww-row_hdr_w-2)/5;
        int row_h2 = 18;
        /* Column letters */
        {
            static const char *cols[] = {"A","B","C","D","E"};
            vga_fill_rect(wx+row_hdr_w+1, grid_y, ww-row_hdr_w-3, col_hdr_h, nb_hd);
            vga_draw_hline(wx+1, grid_y+col_hdr_h, ww-2, nb_sep);
            int ci3;
            for (ci3=0; ci3<5; ci3++) {
                int cx4=wx+row_hdr_w+1+ci3*col_w2;
                int is_selc=(ci3==g_numbers_sel_col);
                if (is_selc) vga_fill_rect(cx4, grid_y, col_w2, col_hdr_h, nb_sel);
                vga_draw_string_trans(cx4+(col_w2-8)/2, grid_y+5, cols[ci3], is_selc?nb_acc:nb_sub);
                vga_draw_vline(cx4+col_w2, grid_y, wh-(grid_y-wy)-2, nb_sep);
            }
        }
        /* Row numbers and cells */
        {
            static const char *cell_data[5][5] = {
                {"Product","Q1","Q2","Q3","Total"},
                {"Widget A","1200","1450","1320","3970"},
                {"Widget B","980","1100","1250","3330"},
                {"Widget C","2100","1900","2200","6200"},
                {"Total","4280","4450","4770","13500"},
            };
            int ri2;
            for (ri2=0; ri2<5; ri2++) {
                int ry3=grid_y+col_hdr_h+ri2*row_h2;
                if (ry3+row_h2 > wy+wh-2) break;
                /* Row header */
                int is_selr=(ri2==g_numbers_sel_row);
                vga_fill_rect(wx+1, ry3, row_hdr_w, row_h2, is_selr?nb_sel:nb_hd);
                char rnum[4]; int_to_str(ri2+1, rnum);
                vga_draw_string_trans(wx+row_hdr_w-str_len(rnum)*8-3, ry3+5, rnum, is_selr?nb_acc:nb_sub);
                vga_draw_hline(wx+1, ry3+row_h2, ww-2, nb_sep);
                /* Cells */
                int ci4;
                for (ci4=0; ci4<5; ci4++) {
                    int cx5=wx+row_hdr_w+2+ci4*col_w2;
                    int is_sel3=(ri2==g_numbers_sel_row && ci4==g_numbers_sel_col);
                    if (is_sel3) {
                        vga_fill_rect(cx5, ry3, col_w2-1, row_h2, nb_sel);
                        vga_draw_rect_outline(cx5, ry3, col_w2-1, row_h2, nb_acc);
                    }
                    int is_hdr2=(ri2==0||ri2==4||ci4==0);
                    vga_draw_string_trans(cx5+2, ry3+5, cell_data[ri2][ci4],
                        is_hdr2?nb_hdr_col:nb_txt);
                }
            }
        }
        /* Mini bar chart on right half (rows 1-3, col 3-4 area) */
        {
            int ch_x2=wx+row_hdr_w+2+3*col_w2;
            int ch_y2=grid_y+col_hdr_h+row_h2;
            int ch_w2=col_w2*2-2;
            int ch_h2=row_h2*3-2;
            if (ch_x2+ch_w2 < wx+ww-2 && ch_y2+ch_h2 < wy+wh-2) {
                vga_fill_rect(ch_x2, ch_y2, ch_w2, ch_h2, g_pref_darkmode?RGB(36,36,40):RGB(248,250,255));
                static const int cvals2[] = {40,33,22};
                static const uint32_t cvcolors[] = {RGB(52,120,246),RGB(255,149,0),RGB(52,199,89)};
                int cvi;
                int bw3=(ch_w2-8)/3;
                for (cvi=0; cvi<3; cvi++) {
                    int bx4=ch_x2+4+cvi*bw3;
                    int bh4=(ch_h2-10)*cvals2[cvi]/40;
                    vga_fill_rect(bx4, ch_y2+ch_h2-4-bh4, bw3-2, bh4, cvcolors[cvi]);
                    vga_fill_rect_alpha(bx4, ch_y2+ch_h2-4-bh4, bw3-2, bh4/3+1, RGB(255,255,255), 40);
                }
                vga_draw_rect_outline(ch_x2, ch_y2, ch_w2, ch_h2, nb_sep);
                vga_draw_string_trans(ch_x2+2, ch_y2+2, "Q Chart", nb_sub);
            }
        }
        return 1;
    }

    /* ---- Focus (Do Not Disturb / Focus Modes) ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Focus")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t fc_bg   = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,250);
        uint32_t fc_hd   = g_pref_darkmode ? RGB(44,44,48)    : RGB(235,235,240);
        uint32_t fc_sep  = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t fc_txt  = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t fc_sub  = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, fc_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 42, fc_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+43, ww-2, fc_sep);
        /* Moon icon */
        gui_draw_circle(wx+20, wy+TITLEBAR_H+21, 14, RGB(100,60,200));
        vga_draw_string_trans(wx+14, wy+TITLEBAR_H+16, "Fo", RGB(255,255,255));
        vga_draw_string_trans(wx+38, wy+TITLEBAR_H+10, "Focus", fc_txt);
        vga_draw_string_trans(wx+38, wy+TITLEBAR_H+24, "Manage notifications", fc_sub);
        /* DND master toggle */
        int tog_y = wy+TITLEBAR_H+50;
        vga_fill_rect(wx+1, tog_y, ww-2, 28, g_pref_darkmode?RGB(36,36,40):RGB(245,245,248));
        vga_draw_hline(wx+1, tog_y+28, ww-2, fc_sep);
        vga_draw_string_trans(wx+10, tog_y+10, "Do Not Disturb", fc_txt);
        draw_toggle(wx+ww-50, tog_y+4, g_pref_dnd);
        /* Focus modes */
        int modes_y = tog_y+34;
        vga_draw_string_trans(wx+10, modes_y-4, "FOCUS MODES", fc_sub);
        modes_y += 8;
        static const char *fm_names[] = {"Work","Personal","Sleep","Gaming","Custom"};
        static const uint32_t fm_colors[] = {RGB(0,122,255), RGB(52,199,89), RGB(100,60,200), RGB(255,59,48), RGB(255,149,0)};
        static const char *fm_icons[] = {"W", "P", "Z", "G", "C"};
        static const char *fm_descs[] = {"Mon-Fri 9am-5pm", "All day", "10pm-7am", "Evenings", "Custom schedule"};
        int fm;
        for (fm=0; fm<5; fm++) {
            int fy = modes_y + fm*36;
            int mode_id = (fm==0) ? 2 : (fm==1 ? 1 : (fm==2 ? 3 : (fm==3 ? 4 : 5)));
            int is_active = g_pref_dnd && g_focus_mode == mode_id;
            if (fy+32 > wy+wh-30) break;
            /* Mode icon circle */
            gui_draw_circle(wx+20, fy+14, 12, fm_colors[fm]);
            vga_draw_string_trans(wx+15, fy+9, fm_icons[fm], RGB(255,255,255));
            /* Mode name */
            vga_draw_string_trans(wx+36, fy+6, fm_names[fm], fc_txt);
            vga_draw_string_trans(wx+36, fy+18, fm_descs[fm], fc_sub);
            if (is_active) {
                vga_fill_rect(wx+ww-60, fy+6, 48, 16, fm_colors[fm]);
                gui_draw_rounded_rect_outline(wx+ww-60, fy+6, 48, 16, 3, fm_colors[fm]);
                vga_draw_string_trans(wx+ww-52, fy+10, "Active", RGB(255,255,255));
            } else {
                gui_draw_rounded_rect_outline(wx+ww-60, fy+6, 48, 16, 3, fc_sep);
                vga_draw_string_trans(wx+ww-52, fy+10, "Set Up", fc_sub);
            }
            if (fm < 4) vga_draw_hline(wx+8, fy+34, ww-16, fc_sep);
        }
        /* Schedule row */
        int sch_y = wy+wh-32;
        vga_draw_hline(wx+1, sch_y, ww-2, fc_sep);
        vga_draw_string_trans(wx+10, sch_y+10, "Allow during Focus:", fc_sub);
        vga_draw_string_trans(wx+10+19*8, sch_y+10, "Calls, Alarms", fc_txt);
        return 1;
    }

    /* ---- Keynote (Presentation app) ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Keynote")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t kn_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(242,242,246);
        uint32_t kn_tb  = g_pref_darkmode ? RGB(44,44,48)    : RGB(228,228,232);
        uint32_t kn_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(195,195,200);
        uint32_t kn_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t kn_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, kn_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 26, kn_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+27, ww-2, kn_sep);
        /* Toolbar buttons */
        {
            static const char *kn_btns[] = {"Play","Slides","Add","Format","Insert"};
            uint32_t kn_btn_cols[] = {RGB(52,199,89), RGB(255,149,0), RGB(0,122,255), RGB(142,142,147), RGB(142,142,147)};
            int bi;
            for (bi=0; bi<5; bi++) {
                int bx2=wx+6+bi*54;
                int active = (bi == g_keynote_mode);
                if (bx2+52 > wx+ww-6) break;
                gui_draw_rounded_rect(bx2, wy+TITLEBAR_H+4, 52, 18, 3,
                    active?kn_btn_cols[bi]:(g_pref_darkmode?RGB(55,55,60):RGB(215,215,220)));
                int tl2=str_len(kn_btns[bi])*8;
                vga_draw_string_trans(bx2+(52-tl2)/2, wy+TITLEBAR_H+9, kn_btns[bi], active?RGB(255,255,255):kn_txt);
            }
        }
        /* Left slide panel */
        int kn_sl_w = 70;
        int kn_content_y = wy+TITLEBAR_H+28;
        int kn_content_h = wh-TITLEBAR_H-2-28;
        vga_fill_rect(wx+1, kn_content_y, kn_sl_w, kn_content_h, g_pref_darkmode?RGB(36,36,40):RGB(230,230,234));
        vga_draw_vline(wx+kn_sl_w, kn_content_y, kn_content_h, kn_sep);
        { int si_kn;
          int shown_kn = g_keynote_slide_count;
          int active_kn_slide = g_keynote_slide;
          if (shown_kn < 1) shown_kn = 1;
          if (shown_kn > 5) shown_kn = 5;
          if (active_kn_slide < 0 || active_kn_slide >= shown_kn) active_kn_slide = 0;
          for (si_kn=0; si_kn<shown_kn; si_kn++) {
              char slide_label[16];
              int sp = 0;
              slide_label[0] = 0;
              apps3_append_text(slide_label, &sp, sizeof(slide_label), "Slide ");
              apps3_append_uint(slide_label, &sp, sizeof(slide_label), (uint32_t)(si_kn + 1));
              vga_draw_string_trans(wx+8, kn_content_y+12+si_kn*14, slide_label, si_kn==active_kn_slide?RGB(255,149,0):kn_sub);
          } }
        /* Main slide canvas */
        int cv_x = wx+kn_sl_w+1;
        int cv_w = ww-kn_sl_w-2;
        int cv_h = kn_content_h;
        /* Dark background */
        vga_fill_rect(cv_x, kn_content_y, cv_w, cv_h, g_pref_darkmode?RGB(22,22,24):RGB(160,160,168));
        /* Empty slide area */
        int sl_pad = 12;
        int sl_x = cv_x + sl_pad;
        int sl_y = kn_content_y + sl_pad;
        int sl_w = cv_w - sl_pad*2;
        int sl_h = cv_h - sl_pad*2;
        vga_fill_rect_alpha(sl_x+3, sl_y+3, sl_w, sl_h, RGB(0,0,0), 80);
        vga_fill_rect(sl_x, sl_y, sl_w, sl_h, g_pref_darkmode?RGB(40,40,46):RGB(230,230,235));
        { const char *main_label = g_keynote_editing ? "Editing Presentation" : "Untitled Presentation";
          const char *sub_label = g_keynote_editing ? "Slide content selected" : "Start editing";
          vga_draw_string_trans(sl_x+(sl_w-str_len(main_label)*8)/2, sl_y+sl_h/2-8, main_label, kn_sub);
          vga_draw_string_trans(sl_x+(sl_w-str_len(sub_label)*8)/2, sl_y+sl_h/2+8, sub_label, kn_sub); }
        { char slide_count[24];
          int scp = 0;
          slide_count[0] = 0;
          apps3_append_uint(slide_count, &scp, sizeof(slide_count), (uint32_t)g_keynote_slide_count);
          apps3_append_text(slide_count, &scp, sizeof(slide_count), g_keynote_slide_count==1?" slide":" slides");
          vga_draw_string_trans(cv_x+2, kn_content_y+cv_h-12, slide_count, kn_sub); }
        return 1;
    }

    /* ---- Pages (Word Processor) ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Pages")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t pg_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(240,240,244);
        uint32_t pg_tb  = g_pref_darkmode ? RGB(44,44,48)    : RGB(228,228,232);
        uint32_t pg_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(195,195,200);
        uint32_t pg_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(20,20,30);
        uint32_t pg_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t pg_acc = RGB(255,149,0);  /* Pages orange */
        uint32_t pg_sel = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, pg_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, pg_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, pg_sep);
        {
            /* Bold, Italic, Underline */
            static const char *fmt_btns[] = {"B","I","U"};
            int bi3;
            for (bi3=0; bi3<3; bi3++) {
                int fbx=wx+6+bi3*22;
                int active_fmt = (bi3==0 && g_pages_bold) || (bi3==1 && g_pages_italic) || (bi3==2 && g_pages_underline);
                gui_draw_rounded_rect(fbx, wy+TITLEBAR_H+4, 18, 16, 3, active_fmt?pg_sel:(g_pref_darkmode?RGB(55,55,60):RGB(215,215,220)));
                vga_draw_string_trans(fbx+5, wy+TITLEBAR_H+8, fmt_btns[bi3], active_fmt?RGB(255,255,255):pg_txt);
            }
            /* Font size */
            vga_draw_string_trans(wx+76, wy+TITLEBAR_H+8, "12pt", pg_sub);
            /* Align buttons */
            vga_fill_rect(wx+106, wy+TITLEBAR_H+5, 14, 14, g_pages_align==0?pg_sel:(g_pref_darkmode?RGB(55,55,60):RGB(215,215,220)));
            vga_draw_string_trans(wx+108, wy+TITLEBAR_H+8, "L", g_pages_align==0?RGB(255,255,255):pg_txt);
            vga_fill_rect(wx+122, wy+TITLEBAR_H+5, 14, 14, g_pages_align==1?pg_sel:(g_pref_darkmode?RGB(55,55,60):RGB(215,215,220)));
            vga_draw_string_trans(wx+124, wy+TITLEBAR_H+8, "C", g_pages_align==1?RGB(255,255,255):pg_txt);
            /* Insert, Format */
            int ins_x = wx+ww-100;
            gui_draw_rounded_rect(ins_x, wy+TITLEBAR_H+4, 44, 16, 3, g_pages_insert_count>0?pg_sel:(g_pref_darkmode?RGB(55,55,60):RGB(215,215,220)));
            vga_draw_string_trans(ins_x+8, wy+TITLEBAR_H+8, "Insert", g_pages_insert_count>0?RGB(255,255,255):pg_sub);
            gui_draw_rounded_rect(ins_x+46, wy+TITLEBAR_H+4, 44, 16, 3, g_pages_inspector?pg_sel:(g_pref_darkmode?RGB(55,55,60):RGB(215,215,220)));
            vga_draw_string_trans(ins_x+50, wy+TITLEBAR_H+8, "Format", g_pages_inspector?RGB(255,255,255):pg_sub);
        }
        /* Ruler */
        int ruler_y = wy+TITLEBAR_H+26;
        int ruler_h = 14;
        vga_fill_rect(wx+1, ruler_y, ww-2, ruler_h, g_pref_darkmode?RGB(36,36,40):RGB(230,230,234));
        vga_draw_hline(wx+1, ruler_y+ruler_h, ww-2, pg_sep);
        /* Ruler tick marks */
        {
            int ri2;
            int margin_px = 20;
            for (ri2=0; ri2<(ww-40)/16; ri2++) {
                int rx2 = wx+margin_px+ri2*16;
                if (rx2 >= wx+ww-10) break;
                vga_draw_vline(rx2, ruler_y+ruler_h-5, 5, pg_sub);
                if (ri2 % 4 == 0) {
                    vga_draw_vline(rx2, ruler_y+ruler_h-9, 9, pg_sub);
                }
            }
            /* Margin triangle indicators */
            vga_fill_rect(wx+margin_px, ruler_y+2, 6, 6, pg_acc);
            vga_fill_rect(wx+ww-margin_px-8, ruler_y+2, 6, 6, pg_acc);
        }
        /* Document area: white page with shadow */
        int doc_area_y = ruler_y+ruler_h+1;
        int doc_area_h = wh-TITLEBAR_H-2-26-ruler_h;
        vga_fill_rect(wx+1, doc_area_y, ww-2, doc_area_h, g_pref_darkmode?RGB(30,30,34):RGB(180,180,188));
        /* Page with shadow */
        int page_x = wx + 20;
        int page_w = ww - 40;
        int page_y = doc_area_y + 8;
        int page_h = doc_area_h - 10;
        /* Shadow */
        vga_fill_rect_alpha(page_x+3, page_y+3, page_w, page_h, RGB(0,0,0), 60);
        /* White page */
        uint32_t page_color = g_pref_darkmode ? RGB(44,44,48) : RGB(255,255,255);
        vga_fill_rect(page_x, page_y, page_w, page_h, page_color);
        /* Page content: title + body text */
        int margin = 14;
        int text_x = page_x + margin;
        int text_w = page_w - margin*2;
        int ty4 = page_y + 12;
        /* Document title */
        vga_draw_string_trans(text_x, ty4, "My Document", pg_acc);
        vga_draw_string_trans(text_x+1, ty4, "My Document", pg_acc); /* bold */
        ty4 += 14;
        vga_draw_hline(text_x, ty4, text_w, g_pref_darkmode?RGB(60,60,65):RGB(200,200,205));
        ty4 += 6;
        /* Selected text highlight */
        vga_fill_rect_alpha(text_x, ty4, text_w, 10, RGB(0,122,255), 40);
        vga_draw_string_trans(text_x, ty4+1, "This is a Pages document with formatted", pg_txt);
        ty4 += 12;
        vga_draw_string_trans(text_x, ty4+1, "text, images, and rich media content.", pg_txt);
        ty4 += 12;
        { char style_line[48]; int slp = 0;
          style_line[0] = 0;
          apps3_append_text(style_line, &slp, sizeof(style_line), g_pages_bold ? "B " : "");
          apps3_append_text(style_line, &slp, sizeof(style_line), g_pages_italic ? "I " : "");
          apps3_append_text(style_line, &slp, sizeof(style_line), g_pages_underline ? "U " : "");
          apps3_append_text(style_line, &slp, sizeof(style_line), g_pages_align ? "Centered" : "Left aligned");
          if (g_pages_insert_count > 0) { apps3_append_text(style_line, &slp, sizeof(style_line), " + insert"); }
          vga_draw_string_trans(text_x, ty4, style_line, pg_sub); }
        ty4 += 16;
        /* Paragraph with different style */
        vga_draw_string_trans(text_x, ty4, "Section 1:", pg_txt);
        vga_draw_string_trans(text_x+1, ty4, "Section 1:", pg_txt);
        ty4 += 12;
        {
            static const char *lines[] = {
                "Lorem ipsum dolor sit amet, consectetur",
                "adipiscing elit. Sed do eiusmod tempor",
                "incididunt ut labore et dolore magna.",
                NULL
            };
            int li2;
            for (li2=0; lines[li2] && ty4+10 < page_y+page_h-4; li2++) {
                vga_draw_string_trans(text_x, ty4, lines[li2], pg_txt);
                ty4 += 12;
            }
        }
        /* Inline image frame */
        if (ty4+40 < page_y+page_h-4) {
            vga_fill_rect(text_x, ty4, 60, 40, g_pref_darkmode?RGB(50,60,80):RGB(200,220,240));
            vga_draw_rect_outline(text_x, ty4, 60, 40, pg_sep);
            vga_draw_string_trans(text_x+8, ty4+16, "[Image]", pg_sub);
            vga_draw_string_trans(text_x+64, ty4+4, "Figure 1: MyOS", pg_sub);
            vga_draw_string_trans(text_x+64, ty4+16, "Screenshot", pg_sub);
        }
        /* Page number at bottom */
        {
            const char *pnum = "Page 1";
            int pl = str_len(pnum)*8;
            vga_draw_string_trans(page_x+(page_w-pl)/2, page_y+page_h-12, pnum, pg_sub);
        }
        return 1;
    }

    /* ---- GarageBand ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "GarageBand")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t gb_bg  = RGB(22,22,24);
        uint32_t gb_tb  = RGB(38,38,42);
        uint32_t gb_sep = RGB(60,60,65);
        uint32_t gb_txt = RGB(210,210,218);
        uint32_t gb_sub = RGB(120,120,130);
        uint32_t gb_acc = RGB(220,20,60);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, gb_bg);
        /* Transport bar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 28, gb_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+29, ww-2, gb_sep);
        /* Transport buttons: << | |> | >> | Stop | Record */
        { static const char *tb[] = {"<<","|>",">>","[]","REC"};
          static const uint32_t tc[] = {RGB(80,80,90),RGB(80,80,90),RGB(80,80,90),RGB(80,80,90),RGB(200,20,40)};
          int bi;
          for (bi=0;bi<5;bi++) {
              int bx=wx+6+bi*36;
              uint32_t btn_col = tc[bi];
              if ((bi==1 && g_garageband_playing) || (bi==4 && g_garageband_recording)) btn_col = gb_acc;
              gui_draw_rounded_rect(bx, wy+TITLEBAR_H+4, 30, 20, 4, btn_col);
              vga_draw_string_trans(bx+4, wy+TITLEBAR_H+9, tb[bi], RGB(220,220,220));
          }
        }
        /* BPM + key */
        vga_draw_string_trans(wx+190, wy+TITLEBAR_H+5, "120", RGB(255,149,0));
        vga_draw_string_trans(wx+218, wy+TITLEBAR_H+5, "BPM", gb_sub);
        vga_draw_string_trans(wx+250, wy+TITLEBAR_H+5, "C Maj", gb_txt);
        vga_draw_string_trans(wx+290, wy+TITLEBAR_H+5, "4/4", gb_sub);
        /* Playhead position */
        vga_draw_string_trans(wx+ww-96, wy+TITLEBAR_H+5,
                              g_garageband_recording?"Recording":(g_garageband_playing?"Playing":"Stopped"),
                              g_garageband_recording?gb_acc:RGB(100,200,100));
        /* Left instrument panel */
        int lp_w = 60;
        int track_area_y = wy+TITLEBAR_H+30;
        int track_area_h = wh-TITLEBAR_H-2-30;
        vga_fill_rect(wx+1, track_area_y, lp_w, track_area_h, RGB(30,30,34));
        vga_draw_vline(wx+lp_w, track_area_y, track_area_h, gb_sep);
        /* Track headers */
        static const char *track_names[] = {"Piano","Drums","Bass","Guitar","Vocals"};
        static const uint32_t track_cols[] = {RGB(100,60,200),RGB(220,80,40),RGB(40,180,80),RGB(180,120,40),RGB(200,40,100)};
        int ti;
        for (ti=0;ti<5;ti++) {
            int ty2=track_area_y+ti*28;
            if (ty2+28>track_area_y+track_area_h) break;
            vga_fill_rect_alpha(wx+1, ty2, lp_w-1, 27, track_cols[ti], 60);
            vga_draw_hline(wx+1, ty2+27, lp_w-1, gb_sep);
            gui_draw_circle(wx+12, ty2+13, 6, track_cols[ti]);
            vga_draw_string_trans(wx+22, ty2+8, track_names[ti], gb_txt);
        }
        /* Timeline area */
        int tl_x = wx+lp_w+1;
        int tl_w = ww-lp_w-2;
        /* Bar ruler */
        vga_fill_rect(tl_x, track_area_y, tl_w, 16, RGB(35,35,40));
        vga_draw_hline(tl_x, track_area_y+16, tl_w, gb_sep);
        { int bi2;
          for (bi2=0;bi2<8;bi2++) {
              int rx2=tl_x+bi2*tl_w/8;
              vga_draw_vline(rx2, track_area_y, 16, gb_sep);
              char bn2[3]; bn2[0]='1'+bi2; bn2[1]=0;
              vga_draw_string_trans(rx2+2, track_area_y+4, bn2, gb_sub);
          }
        }
        /* Playhead line */
        int ph_x = tl_x + (g_garageband_playing ? (int)((timer_ticks()/40U) % (uint32_t)(tl_w>1?tl_w:1)) : tl_w/6);
        vga_draw_vline(ph_x, track_area_y, track_area_h, RGB(255,200,0));
        vga_fill_rect(ph_x-4, track_area_y, 9, 14, RGB(255,200,0));
        /* MIDI/audio regions on tracks */
        for (ti=0;ti<5;ti++) {
            int ty2=track_area_y+16+ti*28;
            if (ty2+27>track_area_y+track_area_h) break;
            vga_draw_hline(tl_x, ty2+27, tl_w, gb_sep);
            /* Region blocks */
            { int ri2;
              for (ri2=0;ri2<3;ri2++) {
                  int rx2=tl_x+ri2*(tl_w/3)+2;
                  int rw2=tl_w/3-6;
                  uint32_t rc=track_cols[ti];
                  vga_fill_rect_alpha(rx2, ty2+2, rw2, 23, rc, 160);
                  gui_draw_rounded_rect_outline(rx2, ty2+2, rw2, 23, 3, rc);
                  /* Mini waveform */
                  { int wi2;
                    for (wi2=0;wi2<rw2-4;wi2+=2) {
                        int wh2=(ti==1)?6+((wi2*7)%8):4+((wi2*5+ri2*3)%7);
                        vga_draw_vline(rx2+2+wi2, ty2+13-wh2/2, wh2, RGB(255,255,255));
                    }
                  }
              }
            }
        }
        /* Smart Controls bar at bottom */
        int sc_h=30;
        int sc_y=track_area_y+track_area_h-sc_h;
        vga_fill_rect(wx+1, sc_y, ww-2, sc_h, RGB(35,35,40));
        vga_draw_hline(wx+1, sc_y, ww-2, gb_sep);
        if (g_garageband_take_count > 0) {
            char take_line[28];
            int tp = 0;
            take_line[0] = 0;
            apps3_append_text(take_line, &tp, sizeof(take_line), "Piano take ");
            apps3_append_uint(take_line, &tp, sizeof(take_line), (uint32_t)g_garageband_take_count);
            vga_draw_string_trans(wx+8, sc_y+8, take_line, gb_acc);
        } else {
            vga_draw_string_trans(wx+8,  sc_y+8, "Piano: Concert Grand", gb_txt);
        }
        vga_draw_string_trans(wx+ww-80, sc_y+8, "Vol:", gb_sub);
        vga_fill_rect(wx+ww-60, sc_y+11, 50, 8, RGB(50,50,60));
        vga_fill_rect(wx+ww-60, sc_y+11, 40, 8, gb_acc);
        return 1;
    }

    /* ---- iMovie ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "iMovie")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t im_bg  = RGB(28,28,30);
        uint32_t im_tb  = RGB(40,40,44);
        uint32_t im_sep = RGB(60,60,65);
        uint32_t im_txt = RGB(210,210,218);
        uint32_t im_sub = RGB(120,120,130);
        uint32_t im_acc = RGB(40,140,220);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, im_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, im_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, im_sep);
        { static const char *btns2[] = {"Import","Projects","Theatre"};
          int bi;
          for (bi=0;bi<3;bi++) {
              int bx=wx+6+bi*70;
              int active = (bi == g_imovie_tab);
              gui_draw_rounded_rect(bx, wy+TITLEBAR_H+3, 66, 18, 3, active?im_acc:RGB(55,55,60));
              vga_draw_string_trans(bx+(66-str_len(btns2[bi])*8)/2, wy+TITLEBAR_H+8, btns2[bi], active?RGB(255,255,255):im_txt);
          }
          vga_draw_string_trans(wx+ww-64, wy+TITLEBAR_H+8, "Share", g_imovie_share_count>0?im_acc:im_sub);
        }
        int content_y = wy+TITLEBAR_H+26;
        int content_h = wh-TITLEBAR_H-2-26;
        /* Left: media browser */
        int mb_w = ww/3;
        vga_fill_rect(wx+1, content_y, mb_w, content_h, RGB(32,32,36));
        vga_draw_vline(wx+mb_w, content_y, content_h, im_sep);
        vga_draw_string_trans(wx+8, content_y+6, "MEDIA", im_sub);
        if (g_imovie_import_count > 0) {
            char import_line[24];
            int ip = 0;
            import_line[0] = 0;
            apps3_append_text(import_line, &ip, sizeof(import_line), "Imported ");
            apps3_append_uint(import_line, &ip, sizeof(import_line), (uint32_t)g_imovie_import_count);
            vga_draw_string_trans(wx+58, content_y+6, import_line, im_acc);
        }
        /* Video thumbnails */
        { int vi2;
          static const uint32_t vtc[] = {RGB(40,80,160),RGB(60,120,40),RGB(160,60,40),RGB(80,40,160)};
          for (vi2=0;vi2<4;vi2++) {
              int vx=wx+4+(vi2%2)*(mb_w/2-4);
              int vy=content_y+20+(vi2/2)*48;
              if (vy+44>content_y+content_h-4) break;
              vga_fill_rect(vx, vy, mb_w/2-6, 40, vtc[vi2]);
              /* Play button overlay */
              gui_draw_circle(vx+mb_w/4-6, vy+20, 8, RGB(0,0,0));
              vga_fill_rect_alpha(vx+mb_w/4-6, vy+20, 8, 8, RGB(0,0,0), 80);
              vga_draw_string_trans(vx+mb_w/4-6-2, vy+16, ">", RGB(255,255,255));
              char vln[8]; vln[0]='0'+vi2+1; vln[1]=':'; vln[2]='2'; vln[3]='0'+vi2; vln[4]=0;
              vga_draw_string_trans(vx+2, vy+32, vln, RGB(200,200,200));
          }
        }
        /* Right: preview + timeline */
        int pv_x = wx+mb_w+1;
        int pv_w = ww-mb_w-2;
        int pv_h = content_h*3/5;
        if (pv_w < 1) pv_w = 1;
        if (pv_h < 1) pv_h = 1;
        /* Preview area */
        vga_fill_rect(pv_x, content_y, pv_w, pv_h, RGB(0,0,0));
        /* "Video frame" gradient */
        { int pyi;
          for (pyi=0;pyi<pv_h;pyi++) {
              uint8_t vv=(uint8_t)(40+pyi*80/pv_h);
              vga_draw_hline(pv_x, content_y+pyi, pv_w, RGB(20,vv/2,vv));
          }
        }
        /* Centered play control */
        int pv_cx=pv_x+pv_w/2, pv_cy=content_y+pv_h/2;
        gui_draw_circle(pv_cx, pv_cy, 20, RGB(0,0,0));
        vga_fill_rect_alpha(pv_cx, pv_cy, 20, 20, RGB(0,0,0), 100);
        vga_draw_string_trans(pv_cx-4, pv_cy-4, ">", RGB(255,255,255));
        vga_draw_string_trans(pv_cx-3, pv_cy-4, ">", RGB(255,255,255));
        /* Timecode overlay */
        { char tc_now[12];
          char tc_end[12];
          apps3_format_timecode(0, tc_now, sizeof(tc_now));
          apps3_format_timecode(990, tc_end, sizeof(tc_end));
          vga_draw_string_trans(pv_x+4, content_y+4, tc_now, RGB(200,200,200));
          vga_draw_string_trans(pv_x+pv_w-88, content_y+4, tc_end, im_sub); }
        /* Timeline area */
        int tl_y = content_y+pv_h;
        int tl_h = content_h-pv_h;
        vga_fill_rect(pv_x, tl_y, pv_w, tl_h, RGB(35,35,38));
        vga_draw_hline(pv_x, tl_y, pv_w, im_sep);
        /* Timeline ruler */
        vga_fill_rect(pv_x, tl_y, pv_w, 14, RGB(42,42,46));
        vga_draw_hline(pv_x, tl_y+14, pv_w, im_sep);
        { int ri2;
          for (ri2=0;ri2<8;ri2++) {
              int rx2=pv_x+ri2*pv_w/8;
              vga_draw_vline(rx2, tl_y, 14, im_sep);
              char ts[6]; int sec=ri2*6;
              ts[0]='0'+sec/10; ts[1]='0'+sec%10; ts[2]=':'; ts[3]='0'; ts[4]='0'; ts[5]=0;
              vga_draw_string_trans(rx2+2, tl_y+3, ts, im_sub);
          }
        }
        /* Video clip on timeline */
        if (tl_h > 20) {
            vga_fill_rect_alpha(pv_x+4, tl_y+16, pv_w/2, tl_h-20, RGB(40,140,220), 180);
            gui_draw_rounded_rect_outline(pv_x+4, tl_y+16, pv_w/2, tl_h-20, 3, im_acc);
            { char clip_label[24];
              char clip_tc[12];
              int cp = 0;
              apps3_format_timecode(990, clip_tc, sizeof(clip_tc));
              clip_label[0] = 0;
              apps3_append_text(clip_label, &cp, sizeof(clip_label), "Clip ");
              apps3_append_uint(clip_label, &cp, sizeof(clip_label), (uint32_t)(1 + g_imovie_import_count));
              apps3_append_text(clip_label, &cp, sizeof(clip_label), " - ");
              apps3_append_text(clip_label, &cp, sizeof(clip_label), clip_tc);
              vga_draw_string_trans(pv_x+8, tl_y+20, clip_label, im_txt); }
            /* Playhead */
            int pl_x=pv_x+4+pv_w/6;
            vga_draw_vline(pl_x, tl_y, tl_h, RGB(255,200,0));
            vga_fill_rect(pl_x-4, tl_y, 9, 10, RGB(255,200,0));
        }
        return 1;
    }

    /* ---- Xcode ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Xcode")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t xc_bg  = g_pref_darkmode ? RGB(24,24,26)    : RGB(250,250,252);
        uint32_t xc_tb  = g_pref_darkmode ? RGB(36,36,40)    : RGB(228,228,232);
        uint32_t xc_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(195,195,200);
        uint32_t xc_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(20,20,30);
        uint32_t xc_sub = g_pref_darkmode ? RGB(120,120,130) : RGB(100,100,110);
        uint32_t xc_acc = RGB(30,120,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-2, xc_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 26, xc_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+27, ww-2, xc_sep);
        /* Run + Stop + scheme selector */
        gui_draw_rounded_rect(wx+6, wy+TITLEBAR_H+4, 28, 18, 4, g_xcode_running?RGB(30,120,255):RGB(52,199,89));
        vga_draw_string_trans(wx+9, wy+TITLEBAR_H+9, "Run", RGB(255,255,255));
        gui_draw_rounded_rect(wx+36, wy+TITLEBAR_H+4, 34, 18, 4, g_xcode_running?RGB(255,59,48):RGB(55,55,60));
        vga_draw_string_trans(wx+37, wy+TITLEBAR_H+9, "Stop", g_xcode_running?RGB(255,255,255):xc_txt);
        /* Scheme: MyOS > iPhone runtime */
        vga_draw_string_trans(wx+80, wy+TITLEBAR_H+9, "MyOS | iPad", xc_sub);
        /* Build status */
        { const char *xc_status = g_xcode_running ? "Running MyOS" : (g_xcode_run_count>0 ? "Stopped" : "Build Succeeded");
          vga_draw_string_trans(wx+(ww-str_len(xc_status)*8)/2, wy+TITLEBAR_H+9, xc_status,
                                g_xcode_running?RGB(30,120,255):RGB(52,199,89)); }
        { const char *xc_tail = g_xcode_running ? "Debugging" : "Indexing...";
          vga_draw_string_trans(wx+ww-8-str_len(xc_tail)*8, wy+TITLEBAR_H+9, xc_tail, xc_sub); }
        /* Three-panel layout */
        int content_y = wy+TITLEBAR_H+28;
        int content_h = wh-TITLEBAR_H-2-28;
        int nav_w = 70;  /* Navigator */
        int insp_w = 80; /* Inspector */
        int edit_w = ww-nav_w-insp_w-2;
        /* Navigator panel (left) */
        vga_fill_rect(wx+1, content_y, nav_w, content_h, g_pref_darkmode?RGB(30,30,34):RGB(240,240,244));
        vga_draw_vline(wx+nav_w, content_y, content_h, xc_sep);
        vga_draw_string_trans(wx+4, content_y+4, "PROJECT", xc_sub);
        { static const char *files[] = {"AppDlg.swift","Views/","Models/","ContentV.swift","main.swift",NULL};
          static const int file_indent[] = {0,0,1,1,0};
          int fi;
          for (fi=0;files[fi];fi++) {
              int fy=content_y+18+fi*14;
              if (fy+12>content_y+content_h-4) break;
              int is_sel = (fi==3);
              if (is_sel) vga_fill_rect(wx+2, fy-1, nav_w-3, 13, xc_acc);
              vga_draw_string_trans(wx+4+file_indent[fi]*6, fy, files[fi], is_sel?RGB(255,255,255):xc_txt);
          }
        }
        /* Editor panel (center) */
        int ed_x = wx+nav_w+1;
        vga_fill_rect(ed_x, content_y, edit_w, content_h, xc_bg);
        /* Line numbers */
        int ln_w = 26;
        vga_fill_rect(ed_x, content_y, ln_w, content_h, g_pref_darkmode?RGB(28,28,32):RGB(244,244,248));
        vga_draw_vline(ed_x+ln_w, content_y, content_h, xc_sep);
        /* Code content */
        { static const char *lines2[] = {
              "import SwiftUI",
              "",
              "struct ContentView: View {",
              "  @State var count = 0",
              "",
              "  var body: some View {",
              "    VStack {",
              "      Text(\"Hello, MyOS!\")",
              "      Button(\"Tap\") {",
              "        count += 1",
              "      }",
              "    }",
              "  }",
              "}",
              NULL
          };
          /* Syntax highlight colors */
          static const uint32_t lc[] = {
              RGB(200,130,200), /* import */
              RGB(210,210,218),
              RGB(100,180,255), /* struct */
              RGB(255,180,100), /* @State */
              RGB(210,210,218),
              RGB(100,180,255), /* var */
              RGB(210,210,218), /* VStack */
              RGB(220,100,100), /* Text string */
              RGB(210,210,218), /* Button */
              RGB(120,210,120), /* count */
              RGB(210,210,218),
              RGB(210,210,218),
              RGB(210,210,218),
              RGB(160,160,170),
              0
          };
          int li2;
          for (li2=0;lines2[li2];li2++) {
              int ly=content_y+4+li2*13;
              if (ly+11>content_y+content_h-4) break;
              /* Line number */
              char lnbuf[4]; lnbuf[0]='0'+(li2+1)/10; lnbuf[1]='0'+(li2+1)%10; lnbuf[2]=0;
              if (li2>=9) { lnbuf[0]='1'; lnbuf[1]='0'+(li2-9); lnbuf[2]=0; }
              vga_draw_string_trans(ed_x+2, ly, lnbuf, xc_sub);
              /* Current line highlight */
              if (li2==7) vga_fill_rect_alpha(ed_x+ln_w+1, ly-1, edit_w-ln_w-2, 13, xc_acc, 30);
              uint32_t lcolor = (lc[li2] && g_pref_darkmode) ? lc[li2] : (g_pref_darkmode?RGB(210,210,218):RGB(20,20,30));
              if (!g_pref_darkmode && lc[li2]) lcolor = lc[li2]; /* show colors in light mode too */
              vga_draw_string_trans(ed_x+ln_w+4, ly, lines2[li2], lcolor);
          }
          /* Cursor blink on line 8 */
          { uint32_t t2=timer_ticks();
            if ((t2/400)%2==0) {
                int cl2=7;
                int cy2=content_y+4+cl2*13;
                int col_x=ed_x+ln_w+4+str_len(lines2[cl2])*8;
                vga_fill_rect(col_x, cy2, 2, 12, xc_acc);
            }
          }
        }
        /* Inspector panel (right) */
        int in_x = ed_x+edit_w;
        vga_draw_vline(in_x, content_y, content_h, xc_sep);
        vga_fill_rect(in_x+1, content_y, insp_w-2, content_h, g_pref_darkmode?RGB(30,30,34):RGB(240,240,244));
        vga_draw_string_trans(in_x+4, content_y+4, "INSPEC.", xc_sub);
        /* Inspector properties */
        { static const char *props[] = {"Type:","View","Font:","SF Pro","Color:","Blue","Padding:","16"};
          int pi;
          for (pi=0;pi<8;pi+=2) {
              int py=content_y+20+pi/2*20;
              if (py+16>content_y+content_h-4) break;
              vga_draw_string_trans(in_x+4, py, props[pi], xc_sub);
              vga_draw_string_trans(in_x+4, py+10, props[pi+1], xc_txt);
          }
        }
        /* Status bar at bottom */
        {
            int sb_h=14;
            int sb_y=content_y+content_h-sb_h;
            vga_fill_rect(wx+1, sb_y, ww-2, sb_h, g_pref_darkmode?RGB(28,28,32):RGB(240,240,244));
            vga_draw_hline(wx+1, sb_y, ww-2, xc_sep);
            vga_draw_string_trans(wx+4, sb_y+3, "ContentView.swift  Line 8, Col 30", xc_sub);
        }
        return 1;
    }

    /* ---- GameCenter ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "GameCenter")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        int active_gc = g_gamecenter_tab;
        uint32_t gc_bg  = g_pref_darkmode ? RGB(20,20,24)   : RGB(242,242,247);
        uint32_t gc_card= g_pref_darkmode ? RGB(30,30,36)   : RGB(255,255,255);
        uint32_t gc_txt = g_pref_darkmode ? RGB(220,220,228) : RGB(20,20,30);
        uint32_t gc_sub = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t gc_grn = RGB(52,199,89);
        uint32_t gc_sep = g_pref_darkmode ? RGB(50,50,58)   : RGB(200,200,208);
        if (active_gc < 0 || active_gc > 3) active_gc = 0;
        vga_fill_rect(wx+1, content_y, ww-2, content_h, gc_bg);
        gui_draw_circle(wx+30, content_y+24, 18, gc_grn);
        vga_draw_string_trans(wx+22, content_y+20, "PC", RGB(255,255,255));
        vga_draw_string_trans(wx+54, content_y+16, "Player One", gc_txt);
        vga_draw_string_trans(wx+54, content_y+28, "Level 42  |  1,337 pts", gc_sub);
        { static const char *tabs[] = {"Games","Achievements","Friends","Challenges"};
          int ti, tab_x = wx+4, tab_y = content_y+46;
          vga_fill_rect(wx+1, tab_y, ww-2, 18, g_pref_darkmode?RGB(28,28,34):RGB(235,235,240));
          vga_draw_hline(wx+1, tab_y+18, ww-2, gc_sep);
          for (ti=0;ti<4;ti++) {
              int tw=str_len(tabs[ti])*8+10;
              int is_active = ti == active_gc;
              vga_draw_string_trans(tab_x+5, tab_y+5, tabs[ti], is_active?gc_grn:gc_sub);
              if (is_active) vga_draw_hline(tab_x, tab_y+17, tw, gc_grn);
              tab_x += tw + 4;
          }
        }
        if (active_gc == 0) {
            static const char *games[] = {"MyOS Racer", "2048", "Breakout", "Minesweeper"};
            int gi, gy = content_y+72;
            for (gi=0; gi<4; gi++) {
                vga_fill_rect(wx+4, gy, ww-8, 32, gc_card);
                vga_draw_rect_outline(wx+4, gy, ww-8, 32, gc_sep);
                vga_draw_string_trans(wx+12, gy+7, games[gi], gc_txt);
                vga_draw_string_trans(wx+ww-78, gy+7, gi == 0 ? "Playing" : "Ready", gi == 0 ? gc_grn : gc_sub);
                gy += 36;
            }
            return 1;
        }
        if (active_gc == 1) {
            static const char *ach[]  = {"First Boot","100 Launches","Speed Run","Night Owl","Pixel Perfect"};
            static const char *desc[] = {"Booted MyOS","Opened 100 apps","Sub-60s startup","Used after midnight","Drew every pixel"};
            static int pts[]          = {50,200,500,75,300};
            static int done[]         = {1,1,0,1,0};
            int ai, ay = content_y+70;
            for (ai=0;ai<5;ai++) {
                if (ay+36 > content_y+content_h-4) break;
                uint32_t ic_col = done[ai] ? gc_grn : RGB(160,160,170);
                char pbuf[8];
                vga_fill_rect(wx+4, ay, ww-8, 32, gc_card);
                vga_draw_rect_outline(wx+4, ay, ww-8, 32, gc_sep);
                gui_draw_rounded_rect(wx+8, ay+6, 20, 20, 4, ic_col);
                vga_draw_string_trans(wx+13, ay+12, done[ai]?"*":"?", RGB(255,255,255));
                vga_draw_string_trans(wx+34, ay+6,  ach[ai],  gc_txt);
                vga_draw_string_trans(wx+34, ay+18, desc[ai], gc_sub);
                int_to_str(pts[ai], pbuf);
                vga_draw_string_trans(wx+ww-40, ay+12, pbuf, done[ai]?gc_grn:gc_sub);
                ay += 36;
            }
            return 1;
        }
        if (active_gc == 2) {
            static const char *friends[] = {"Jordan", "Mina", "Alex", "Sam"};
            int fi, fy = content_y+72;
            for (fi=0; fi<4; fi++) {
                vga_fill_rect(wx+4, fy, ww-8, 30, gc_card);
                vga_draw_rect_outline(wx+4, fy, ww-8, 30, gc_sep);
                gui_draw_circle(wx+18, fy+15, 9, fi == 0 ? gc_grn : RGB(142,142,147));
                vga_draw_string_trans(wx+34, fy+7, friends[fi], gc_txt);
                vga_draw_string_trans(wx+ww-70, fy+7, fi == 0 ? "Online" : "Away", fi == 0 ? gc_grn : gc_sub);
                fy += 34;
            }
            return 1;
        }
        {
            static const char *chals[] = {"Win one race", "Beat 2048", "Clear Breakout"};
            int ci, cy_gc = content_y+72;
            for (ci=0; ci<3; ci++) {
                vga_fill_rect(wx+4, cy_gc, ww-8, 34, gc_card);
                vga_draw_rect_outline(wx+4, cy_gc, ww-8, 34, gc_sep);
                vga_draw_string_trans(wx+12, cy_gc+7, chals[ci], gc_txt);
                vga_draw_string_trans(wx+12, cy_gc+20, ci == 0 ? "In progress" : "Open", gc_sub);
                cy_gc += 38;
            }
            return 1;
        }
    }

    /* ---- Automator ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Automator")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t at_bg   = g_pref_darkmode ? RGB(28,28,32)   : RGB(236,236,240);
        uint32_t at_card = g_pref_darkmode ? RGB(38,38,44)   : RGB(248,248,252);
        uint32_t at_txt  = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t at_sub  = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t at_acc  = RGB(238,95,0);
        uint32_t at_sep  = g_pref_darkmode ? RGB(55,55,62)   : RGB(200,200,208);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, at_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 22, g_pref_darkmode?RGB(38,38,44):RGB(220,220,228));
        vga_draw_hline(wx+1, content_y+22, ww-2, at_sep);
        { static const char *tbtn[]={"Run","Stop","Record","Variables"};
          int bi, bx=wx+6;
          for (bi=0;bi<4;bi++){
              int active = ((bi==0 && g_automator_mode==1) ||
                            (bi==2 && g_automator_recording) || (bi==3 && g_automator_mode==3));
              gui_draw_rounded_rect(bx, content_y+4, str_len(tbtn[bi])*8+8, 14, 3,
                  active?at_acc:g_pref_darkmode?RGB(55,55,62):RGB(200,200,208));
              vga_draw_string_trans(bx+4, content_y+7, tbtn[bi], active?RGB(255,255,255):at_txt);
              bx += str_len(tbtn[bi])*8+14;
          }
          vga_draw_string_trans(wx+ww-104, content_y+7,
                                g_automator_recording?"Recording":(g_automator_mode==1?"Running":(g_automator_mode==3?"Variables":"Workflow")),
                                g_automator_recording?at_acc:at_sub);
        }
        /* Left panel: Action Library */
        int lib_w = ww/3;
        vga_fill_rect(wx+1, content_y+23, lib_w, content_h-23, g_pref_darkmode?RGB(32,32,38):RGB(240,240,244));
        vga_draw_vline(wx+lib_w+1, content_y+23, content_h-23, at_sep);
        vga_draw_string_trans(wx+6, content_y+27, "Actions", at_sub);
        /* Search bar */
        vga_fill_rect(wx+4, content_y+38, lib_w-6, 14, g_pref_darkmode?RGB(50,50,58):RGB(228,228,232));
        vga_draw_rect_outline(wx+4, content_y+38, lib_w-6, 14,
            g_automator_search_focused ? at_acc : at_sep);
        vga_draw_string_trans(wx+8, content_y+41,
            g_automator_search_focused ? "Search focused" : "Search actions...", at_sub);
        /* Action categories */
        { static const char *cats[] = {"Files & Folders","Text","PDFs","Music","Photos","Utilities"};
          int ci;
          for (ci=0;ci<6;ci++){
              int cy2=content_y+56+ci*20;
              if (cy2+16>content_y+content_h-4) break;
              if (ci==g_automator_category) vga_fill_rect(wx+2, cy2, lib_w-2, 18, g_pref_darkmode?RGB(45,45,52):RGB(228,228,236));
              vga_draw_string_trans(wx+8, cy2+5, cats[ci], ci==g_automator_category?at_acc:at_txt);
          }
        }
        /* Right panel: Workflow canvas */
        int wf_x = wx + lib_w + 4;
        int wf_w = ww - lib_w - 6;
        vga_draw_string_trans(wf_x+4, content_y+27, "Workflow", at_sub);
        /* Workflow steps */
        { static const char *steps[]={"Get Finder Items","Filter Files","Copy to Folder","Display Results"};
          static const char *step_cat[]={"Files","Files","Files","Utility"};
          int si, sy2=content_y+42;
          for (si=0;si<4;si++){
              if (sy2+38>content_y+content_h-4) break;
              vga_fill_rect(wf_x, sy2, wf_w-4, 34, at_card);
              vga_draw_rect_outline(wf_x, sy2, wf_w-4, 34, si==g_automator_step?at_acc:at_sep);
              gui_draw_rounded_rect(wf_x+4, sy2+7, 18, 18, 4, at_acc);
              vga_draw_string_trans(wf_x+8, sy2+12, step_cat[si], RGB(255,255,255));
              vga_draw_string_trans(wf_x+28, sy2+8,  steps[si], at_txt);
              vga_draw_string_trans(wf_x+28, sy2+20, "Options...", at_sub);
              if (si<3) { /* arrow down */
                  vga_fill_rect(wf_x+wf_w/2-2, sy2+34, 4, 6, at_sep);
                  vga_fill_rect(wf_x+wf_w/2-5, sy2+36, 10, 2, at_sep);
              }
              sy2 += 42;
          }
        }
        if (g_automator_mode == 1 || g_automator_recording) {
            vga_draw_string_trans(wf_x+4, content_y+content_h-16,
                                  g_automator_recording?"Recording workflow input":"Workflow running", at_acc);
        }
        return 1;
    }

    /* ---- Font Book ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Font Book")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t fb_bg   = g_pref_darkmode ? RGB(26,26,30)   : RGB(245,245,248);
        uint32_t fb_card = g_pref_darkmode ? RGB(36,36,42)   : RGB(255,255,255);
        uint32_t fb_txt  = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t fb_sub  = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t fb_acc  = RGB(0,122,255);
        uint32_t fb_sep  = g_pref_darkmode ? RGB(52,52,60)   : RGB(200,200,208);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, fb_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 22, g_pref_darkmode?RGB(36,36,42):RGB(228,228,232));
        vga_draw_hline(wx+1, content_y+22, ww-2, fb_sep);
        vga_draw_string_trans(wx+6, content_y+7, "Collections", fb_sub);
        vga_draw_string_trans(wx+80, content_y+7, "|", fb_sep);
        vga_draw_string_trans(wx+90, content_y+7, "All Fonts", fb_acc);
        /* Left sidebar: font families */
        int sb_w = ww * 2 / 5;
        vga_fill_rect(wx+1, content_y+23, sb_w, content_h-23, g_pref_darkmode?RGB(30,30,36):RGB(238,238,242));
        vga_draw_vline(wx+sb_w+1, content_y+23, content_h-23, fb_sep);
        /* Search */
        vga_fill_rect(wx+4, content_y+26, sb_w-6, 14, g_pref_darkmode?RGB(48,48,56):RGB(224,224,228));
        vga_draw_rect_outline(wx+4, content_y+26, sb_w-6, 14,
            g_fontbook_search_focused ? fb_acc : fb_sep);
        vga_draw_string_trans(wx+8, content_y+29,
            g_fontbook_search_focused ? "Search focused" : "Search...", fb_sub);
        /* Font list */
        { static const char *fonts[]={"Arial","Courier","Georgia","Helvetica","Impact","Monaco","Palatino","Times New Roman","Verdana"};
          int fi2, fy=content_y+44;
          int selected_fb = g_fontbook_selected;
          if (selected_fb < 0 || selected_fb >= 9) selected_fb = 3;
          for (fi2=0;fi2<9;fi2++){
              int is_sel = fi2 == selected_fb;
              if (fy+16>content_y+content_h-4) break;
              if (is_sel) vga_fill_rect(wx+2, fy, sb_w-2, 16, g_pref_darkmode?RGB(44,44,52):RGB(218,218,225));
              vga_draw_string_trans(wx+8, fy+4, fonts[fi2], is_sel?fb_acc:fb_txt);
              fy += 18;
          }
        }
        /* Right: preview area */
        int pv_x = wx + sb_w + 4;
        int pv_w = ww - sb_w - 6;
        vga_fill_rect(pv_x, content_y+23, pv_w, content_h-23, fb_card);
        { static const char *fonts[]={"Arial","Courier","Georgia","Helvetica","Impact","Monaco","Palatino","Times New Roman","Verdana"};
          int selected_fb = g_fontbook_selected;
          char face_line[16];
          int flp = 0;
          if (selected_fb < 0 || selected_fb >= 9) selected_fb = 3;
          face_line[0] = 0;
          apps3_append_uint(face_line, &flp, sizeof(face_line), (uint32_t)(selected_fb + 6));
          apps3_append_text(face_line, &flp, sizeof(face_line), " faces");
          vga_draw_string_trans(pv_x+4, content_y+27, fonts[selected_fb], fb_txt);
          vga_draw_string_trans(pv_x+4, content_y+37, face_line, fb_sub); }
        vga_draw_hline(pv_x, content_y+48, pv_w, fb_sep);
        /* Preview text at different sizes */
        { const char *prev = "Aa Bb Cc";
          int psy[] = {60,76,92,108};
          int psz[] = {1,1,1,1};
          int pi2;
          (void)psz;
          for (pi2=0;pi2<4;pi2++){
              if (content_y+psy[pi2]+12 > content_y+content_h-4) break;
              vga_draw_string_trans(pv_x+4, content_y+psy[pi2], prev, fb_txt);
          }
          vga_draw_string_trans(pv_x+4, content_y+128, "ABCDEFGHIJKLM", fb_txt);
          vga_draw_string_trans(pv_x+4, content_y+142, "NOPQRSTUVWXYZ", fb_txt);
          vga_draw_string_trans(pv_x+4, content_y+156, "0123456789", fb_sub);
          vga_draw_string_trans(pv_x+4, content_y+170, "!@#$%^&*()", fb_sub);
        }
        /* Size slider */
        vga_draw_hline(pv_x+4, content_y+content_h-16, pv_w-8, fb_sep);
        gui_draw_circle(pv_x+4+60, content_y+content_h-16, 4, fb_acc);
        vga_draw_string_trans(pv_x+4, content_y+content_h-10, "Aa", fb_sub);
        vga_draw_string_trans(pv_x+pv_w-20, content_y+content_h-10, "Aa", fb_txt);
        return 1;
    }

    /* ---- Console ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Console")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t co_bg   = g_pref_darkmode ? RGB(18,18,22)   : RGB(248,248,252);
        uint32_t co_tb   = g_pref_darkmode ? RGB(28,28,34)   : RGB(228,228,232);
        uint32_t co_txt  = g_pref_darkmode ? RGB(200,200,210) : RGB(20,20,30);
        uint32_t co_sub  = g_pref_darkmode ? RGB(130,130,140) : RGB(100,100,110);
        uint32_t co_sep  = g_pref_darkmode ? RGB(48,48,56)   : RGB(200,200,208);
        uint32_t co_err  = RGB(255,69,58);
        uint32_t co_wrn  = RGB(255,159,10);
        uint32_t co_inf  = RGB(48,209,88);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, co_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 22, co_tb);
        vga_draw_hline(wx+1, content_y+22, ww-2, co_sep);
        /* Buttons */
        { static const char *levels[]={"All","Errors","Warnings","Info"};
          static uint32_t lc[]={0,0,0,0};
          int li3, bx=wx+4;
          lc[0]=RGB(80,80,88); lc[1]=co_err; lc[2]=co_wrn; lc[3]=co_inf;
          for (li3=0;li3<4;li3++){
              int is_level = li3 == g_console_level_filter;
              gui_draw_rounded_rect(bx, content_y+4, str_len(levels[li3])*8+8, 14, 3,
                  is_level?RGB(0,122,255):co_tb);
              vga_draw_string_trans(bx+4, content_y+7, levels[li3], is_level?RGB(255,255,255):lc[li3]);
              bx += str_len(levels[li3])*8+12;
          }
          /* Search */
          int sf_x = wx+ww-100;
          vga_fill_rect(sf_x, content_y+4, 94, 14, g_pref_darkmode?RGB(40,40,48):RGB(218,218,224));
          vga_draw_rect_outline(sf_x, content_y+4, 94, 14,
              g_console_filter_focused ? RGB(0,122,255) : co_sep);
          vga_draw_string_trans(sf_x+4, content_y+7,
              g_console_filter_focused ? "Filter focused" : "Filter...", co_sub);
        }
        /* Left sidebar: devices */
        int dev_w = ww/4;
        vga_fill_rect(wx+1, content_y+23, dev_w, content_h-23, g_pref_darkmode?RGB(24,24,28):RGB(238,238,242));
        vga_draw_vline(wx+dev_w+1, content_y+23, content_h-23, co_sep);
        vga_draw_string_trans(wx+4, content_y+28, "DEVICES", co_sub);
        { static const char *devs[]={"MyOS","kernel","launchd","GUI","Network"};
          int di, dy=content_y+40;
          for (di=0;di<5;di++){
              if (dy+14>content_y+content_h-4) break;
              int is_dev = di == g_console_device_filter;
              if (is_dev) vga_fill_rect(wx+2, dy, dev_w-2, 14, g_pref_darkmode?RGB(40,40,48):RGB(218,218,228));
              vga_draw_string_trans(wx+6, dy+3, devs[di], is_dev?RGB(0,122,255):co_txt);
              dy += 16;
          }
        }
        /* Log entries */
        int log_x = wx + dev_w + 4;
        int log_w = ww - dev_w - 6;
        { runtime_system_info_t sys;
          char ts0[9], ts1[9], ts2[9], ts3[9], ts4[9], ts5[9], ts6[9], ts7[9], ts8[9];
          char boot_msg[48], gui_msg[48], net_msg[48], timer_msg[48];
          char displaybuf[32];
          int bp = 0, gp = 0, np = 0, tp = 0;
          runtime_get_system_info(&sys);
          apps3_format_hms_age(8, ts0, sizeof(ts0));
          apps3_format_hms_age(7, ts1, sizeof(ts1));
          apps3_format_hms_age(6, ts2, sizeof(ts2));
          apps3_format_hms_age(5, ts3, sizeof(ts3));
          apps3_format_hms_age(4, ts4, sizeof(ts4));
          apps3_format_hms_age(3, ts5, sizeof(ts5));
          apps3_format_hms_age(2, ts6, sizeof(ts6));
          apps3_format_hms_age(1, ts7, sizeof(ts7));
          apps3_format_hms_age(0, ts8, sizeof(ts8));
          apps3_format_display(&sys, displaybuf, sizeof(displaybuf));
          boot_msg[0] = 0;
          apps3_append_text(boot_msg, &bp, sizeof(boot_msg), "[INFO]  ");
          apps3_append_text(boot_msg, &bp, sizeof(boot_msg), sys.sysname);
          apps3_append_text(boot_msg, &bp, sizeof(boot_msg), " ");
          apps3_append_text(boot_msg, &bp, sizeof(boot_msg), sys.release);
          apps3_append_text(boot_msg, &bp, sizeof(boot_msg), " booted OK");
          gui_msg[0] = 0;
          apps3_append_text(gui_msg, &gp, sizeof(gui_msg), "[INFO]  GUI ");
          apps3_append_text(gui_msg, &gp, sizeof(gui_msg), displaybuf);
          net_msg[0] = 0;
          apps3_append_text(net_msg, &np, sizeof(net_msg), netif_count() ? "[INFO]  Network ready" : "[WARN]  No network interface");
          timer_msg[0] = 0;
          apps3_append_text(timer_msg, &tp, sizeof(timer_msg), "[INFO]  Timer tick ");
          apps3_append_uint(timer_msg, &tp, sizeof(timer_msg), sys.timer_hz);
          apps3_append_text(timer_msg, &tp, sizeof(timer_msg), "Hz");
          struct { const char *ts; const char *proc; const char *msg; int lvl; } logs[] = {
              {ts0, "kernel",  boot_msg,                              2},
              {ts1, "gui",     gui_msg,                               2},
              {ts2, "network", net_msg, netif_count() ? 2 : 1          },
              {ts3, "launchd", "[INFO]  Finder launched",             2},
              {ts4, "kernel",  "[INFO]  PS/2 mouse IRQ12 ready",      2},
              {ts5, "gui",     "[ERROR] Font glyph 0xC4 missing",     0},
              {ts6, "kernel",  timer_msg,                             2},
              {ts7, "gui",     "[INFO]  Dock rendered icons",         2},
              {ts8, "launchd", "[WARN]  App 'TextEdit' slow launch",  1},
          };
          int li4, ly=content_y+26;
          int n_logs = 9;
          int shown_logs = 0;
          static const char *dev_filters[] = {"", "kernel", "launchd", "gui", "network"};
          for (li4=0;li4<n_logs;li4++){
              int show_log = 1;
              if (g_console_level_filter > 0 && logs[li4].lvl != g_console_level_filter - 1) show_log = 0;
              if (g_console_device_filter > 0 && !str_eq(logs[li4].proc, dev_filters[g_console_device_filter])) show_log = 0;
              if (!show_log) continue;
              if (ly+12>content_y+content_h-4) break;
              uint32_t lvl_col = logs[li4].lvl==0?co_err:(logs[li4].lvl==1?co_wrn:co_inf);
              vga_draw_string_trans(log_x, ly, logs[li4].ts, co_sub);
              vga_draw_string_trans(log_x+52, ly, logs[li4].proc, co_sub);
              vga_draw_string_trans(log_x+100, ly, logs[li4].msg, lvl_col);
              if ((shown_logs%2)==0 && !g_pref_darkmode)
                  vga_fill_rect_alpha(log_x, ly, log_w, 12, RGB(0,0,0), 8);
              shown_logs++;
              ly += 14;
          }
          if (shown_logs == 0) vga_draw_string_trans(log_x, ly, "No matching logs", co_sub);
          /* Last line blinking cursor */
          { uint32_t t3=timer_ticks();
            if ((t3/400)%2==0) vga_fill_rect(log_x, ly, 6, 10, RGB(0,122,255));
          }
        }
        return 1;
    }

    /* ---- iPhone Mirroring ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "iPhone Mirroring")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        /* Dark outer background */
        vga_fill_rect(wx+1, content_y, ww-2, content_h, RGB(18,18,22));
        /* Phone bezel centered */
        int ph_w = 120, ph_h = content_h - 20;
        if (ph_h > 220) ph_h = 220;
        int ph_x = wx + (ww - ph_w) / 2;
        int ph_y = content_y + (content_h - ph_h) / 2;
        /* Outer bezel (dark) */
        gui_draw_rounded_rect(ph_x-6, ph_y-6, ph_w+12, ph_h+12, 18, RGB(28,28,32));
        /* Screen */
        gui_draw_rounded_rect(ph_x, ph_y, ph_w, ph_h, 12, RGB(10,10,16));
        /* Notch / Dynamic Island at top */
        gui_draw_rounded_rect(ph_x+ph_w/2-20, ph_y+4, 40, 12, 6, RGB(0,0,0));
        /* Mirrored phone illustration */
        { int gri;
          for (gri=0;gri<ph_h-50;gri++) {
              int gr_y=ph_y+30+gri;
              int shade=22+gri*32/(ph_h-50);
              if (gr_y>ph_y && gr_y<ph_y+ph_h) {
                  vga_draw_hline(ph_x+1, gr_y, ph_w-2, RGB(shade,shade+8,shade+24));
              }
          }
        }
        vga_draw_string_trans(ph_x+(ph_w-48)/2, ph_y+ph_h/2-8, "iPhone", RGB(240,240,248));
        vga_draw_string_trans(ph_x+(ph_w-72)/2, ph_y+ph_h/2+4, "Mirrored", RGB(210,230,255));
        vga_draw_string_trans(ph_x+(ph_w-80)/2, ph_y+ph_h/2+20, "Control on", RGB(140,180,230));
        /* Home indicator */
        vga_fill_rect(ph_x+ph_w/2-15, ph_y+ph_h-6, 30, 3, RGB(200,200,210));
        /* Side buttons */
        vga_fill_rect(ph_x-8, ph_y+ph_h/4, 4, 20, RGB(40,40,44));
        vga_fill_rect(ph_x+ph_w+4, ph_y+ph_h/3, 4, 30, RGB(40,40,44));
        vga_draw_string_trans(wx+(ww-112)/2, content_y+6, "iPhone connected", RGB(160,210,255));
        return 1;
    }

    /* ---- Instruments ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Instruments")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t in_bg   = g_pref_darkmode ? RGB(22,22,26)   : RGB(238,238,242);
        uint32_t in_hdr  = g_pref_darkmode ? RGB(32,32,38)   : RGB(220,220,228);
        uint32_t in_card = g_pref_darkmode ? RGB(30,30,36)   : RGB(248,248,252);
        uint32_t in_txt  = g_pref_darkmode ? RGB(220,220,228) : RGB(20,20,28);
        uint32_t in_sub  = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t in_sep  = g_pref_darkmode ? RGB(50,50,58)   : RGB(200,200,208);
        uint32_t in_red  = RGB(220,50,50);
        uint32_t in_yel  = RGB(255,214,10);
        uint32_t in_grn  = RGB(48,209,88);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, in_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 24, in_hdr);
        vga_draw_hline(wx+1, content_y+24, ww-2, in_sep);
        /* Record button + time */
        gui_draw_circle(wx+14, content_y+12, 8, in_red);
        gui_draw_circle(wx+14, content_y+12, 5, RGB(200,20,20));
        { char elapsed_buf[12];
          apps3_format_seconds_centis(timer_ticks() % 600000U, elapsed_buf, sizeof(elapsed_buf));
          vga_draw_string_trans(wx+28, content_y+8, elapsed_buf, in_txt); }
        vga_draw_string_trans(wx+100, content_y+8, "MyOS (PID 1)", in_sub);
        /* Instrument track labels left panel */
        int lp2_w = 90;
        vga_fill_rect(wx+1, content_y+25, lp2_w, content_h-25, g_pref_darkmode?RGB(28,28,34):RGB(232,232,236));
        vga_draw_vline(wx+lp2_w+1, content_y+25, content_h-25, in_sep);
        { static const char *tracks2[]={"CPU Usage","Allocations","Leaks","File Activity","System Calls"};
          static uint32_t tc[]={0,0,0,0,0};
          tc[0]=in_red; tc[1]=RGB(0,122,255); tc[2]=in_yel; tc[3]=in_grn; tc[4]=RGB(147,44,246);
          int ti2, ty2=content_y+30;
          for (ti2=0;ti2<5;ti2++){
              if (ty2+20>content_y+content_h-4) break;
              vga_fill_rect(wx+2, ty2, 4, 16, tc[ti2]);
              vga_draw_string_trans(wx+10, ty2+5, tracks2[ti2], in_txt);
              ty2 += 22;
          }
        }
        /* Timeline area */
        int tl_x = wx + lp2_w + 4;
        int tl_w2 = ww - lp2_w - 6;
        /* Time ruler */
        vga_fill_rect(tl_x, content_y+25, tl_w2, 14, in_hdr);
        { int ri2;
          for (ri2=0;ri2<8;ri2++){
              int rx2=tl_x+ri2*tl_w2/8;
              char tbuf2[6]; tbuf2[0]='0'; tbuf2[1]='.'; tbuf2[2]='0'+ri2; tbuf2[3]='0'; tbuf2[4]='s'; tbuf2[5]=0;
              vga_draw_vline(rx2, content_y+25, 8, in_sep);
              vga_draw_string_trans(rx2+2, content_y+27, tbuf2, in_sub);
          }
        }
        /* Playhead */
        int ph_x2 = tl_x + tl_w2*42/100;
        vga_draw_vline(ph_x2, content_y+25, content_h-25, in_yel);
        /* Track graphs */
        { uint32_t gcol2[]={in_red,RGB(0,122,255),in_yel,in_grn,RGB(147,44,246)};
          static const int peaks[][8]={{30,60,80,40,90,50,70,30},
                                        {10,20,50,80,60,30,40,20},
                                        {0,0,0,5,0,0,3,0},
                                        {20,40,30,60,80,50,40,30},
                                        {50,30,70,40,80,60,30,50}};
          int tr2, ti3;
          for (tr2=0;tr2<5;tr2++){
              int gy2=content_y+40+tr2*22;
              if (gy2+18>content_y+content_h-4) break;
              vga_fill_rect(tl_x, gy2, tl_w2, 18, in_card);
              /* Graph bars */
              for (ti3=0;ti3<8;ti3++){
                  int bx2=tl_x+2+ti3*(tl_w2-4)/8;
                  int bw2=(tl_w2-4)/8-2;
                  int bh2=peaks[tr2][ti3]*14/100;
                  if (bh2<1) bh2=1;
                  vga_fill_rect(bx2, gy2+18-bh2-2, bw2, bh2, gcol2[tr2]);
              }
          }
        }
        /* Bottom details panel */
        int det_y = content_y + content_h - 40;
        vga_fill_rect(wx+1, det_y, ww-2, 38, in_hdr);
        vga_draw_hline(wx+1, det_y, ww-2, in_sep);
        { runtime_system_info_t sys;
          char pctbuf[8];
          char membuf[16];
          char threadbuf[32];
          int tp = 0;
          runtime_get_system_info(&sys);
          runtime_format_percent(sys.cpu_load_percent, pctbuf, sizeof(pctbuf));
          runtime_format_bytes(sys.heap_used_bytes, membuf, sizeof(membuf));
          threadbuf[0] = 0;
          apps3_append_text(threadbuf, &tp, sizeof(threadbuf), "Threads: ");
          apps3_append_uint(threadbuf, &tp, sizeof(threadbuf), sys.task_count);
          vga_draw_string_trans(wx+6, det_y+4,  "CPU Usage:", in_txt);
          vga_draw_string_trans(wx+94, det_y+4, pctbuf, in_txt);
          vga_draw_string_trans(wx+6, det_y+16, "Memory:", in_txt);
          vga_draw_string_trans(wx+70, det_y+16, membuf, in_txt);
          vga_draw_string_trans(wx+6, det_y+28, threadbuf, in_sub);
          vga_draw_string_trans(wx+150, det_y+4,  "Heap blocks:", in_txt);
          { char hbuf[12]; runtime_format_uint(sys.heap_block_count, hbuf, sizeof(hbuf));
            vga_draw_string_trans(wx+246, det_y+4, hbuf, in_txt); } }
        vga_draw_string_trans(wx+150, det_y+16, "Leaks: 0", in_grn);
        vga_draw_string_trans(wx+150, det_y+28, "Persistent: tracked", in_sub);
        return 1;
    }

    /* ---- Network Utility ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Network Utility")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        int active_tab = g_netutil_tab;
        uint32_t gw_ip = 0;
        int ri_nu;
        uint32_t nu_bg   = g_pref_darkmode ? RGB(24,24,28)   : RGB(242,242,246);
        uint32_t nu_card = g_pref_darkmode ? RGB(34,34,40)   : RGB(255,255,255);
        uint32_t nu_txt  = g_pref_darkmode ? RGB(218,218,226) : RGB(20,20,28);
        uint32_t nu_sub  = g_pref_darkmode ? RGB(140,140,150) : RGB(100,100,110);
        uint32_t nu_acc  = RGB(0,122,255);
        uint32_t nu_sep  = g_pref_darkmode ? RGB(52,52,60)   : RGB(200,200,208);
        uint32_t nu_grn  = RGB(48,209,88);
        const netif_t *net = runtime_primary_netif();
        uint32_t dns_ip = runtime_dns_server4();
        if (active_tab < 0 || active_tab > 4) active_tab = 0;
        for (ri_nu = 0; ri_nu < (int)net_route_count(); ri_nu++) {
            const net_route_t *route = net_route_at((uint32_t)ri_nu);
            if (route && route->dest == 0 && route->mask == 0) { gw_ip = route->gateway; break; }
        }
        vga_fill_rect(wx+1, content_y, ww-2, content_h, nu_bg);
        /* Tab bar */
        { static const char *tabs2[]={"Info","Ping","Traceroute","Lookup","Port Scan"};
          int ti4, tx2=wx+4, ty4=content_y+4;
          vga_fill_rect(wx+1, ty4-2, ww-2, 20, g_pref_darkmode?RGB(32,32,38):RGB(228,228,232));
          vga_draw_hline(wx+1, ty4+18, ww-2, nu_sep);
          for (ti4=0;ti4<5;ti4++){
              int tw2=str_len(tabs2[ti4])*8+10;
              int is_active = ti4 == active_tab;
              uint32_t tc2=is_active?nu_acc:nu_sub;
              gui_draw_rounded_rect(tx2, ty4, tw2, 16, 3, is_active?(g_pref_darkmode?RGB(40,60,100):RGB(200,220,255)):nu_bg);
              vga_draw_string_trans(tx2+5, ty4+4, tabs2[ti4], tc2);
              tx2 += tw2+4;
          }
        }
        int pa_y = content_y + 28;
        if (active_tab == 0) {
            char ipbuf[16], gwbuf[16], dnsbuf[16], macbuf[20], txbuf[16], rxbuf[16];
            runtime_format_ipv4(net ? net->ipv4 : 0, ipbuf, sizeof(ipbuf));
            runtime_format_ipv4(gw_ip, gwbuf, sizeof(gwbuf));
            runtime_format_ipv4(dns_ip, dnsbuf, sizeof(dnsbuf));
            if (net) {
                runtime_format_mac(net->mac, macbuf, sizeof(macbuf));
            } else {
                int mp = 0;
                macbuf[0] = 0;
                apps3_append_text(macbuf, &mp, sizeof(macbuf), "none");
            }
            runtime_format_uint(net ? net->tx_packets : 0, txbuf, sizeof(txbuf));
            runtime_format_uint(net ? net->rx_packets : 0, rxbuf, sizeof(rxbuf));
            vga_fill_rect(wx+6, pa_y, ww-12, 148, nu_card);
            vga_draw_rect_outline(wx+6, pa_y, ww-12, 148, nu_sep);
            vga_draw_string_trans(wx+12, pa_y+8,  net && net->name ? net->name : "No active interface", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+28, net && net->up ? "Status: up" : "Status: down", net && net->up ? nu_grn : RGB(255,149,0));
            vga_draw_string_trans(wx+12, pa_y+46, "IPv4:", nu_sub); vga_draw_string_trans(wx+80, pa_y+46, ipbuf, nu_txt);
            vga_draw_string_trans(wx+12, pa_y+64, "Gateway:", nu_sub); vga_draw_string_trans(wx+80, pa_y+64, gw_ip ? gwbuf : "not configured", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+82, "DNS:", nu_sub); vga_draw_string_trans(wx+80, pa_y+82, dns_ip ? dnsbuf : "not configured", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+100, "MAC:", nu_sub); vga_draw_string_trans(wx+80, pa_y+100, macbuf, nu_txt);
            vga_draw_string_trans(wx+12, pa_y+118, "TX/RX:", nu_sub);
            vga_draw_string_trans(wx+80, pa_y+118, txbuf, nu_txt);
            vga_draw_string_trans(wx+128, pa_y+118, rxbuf, nu_txt);
            return 1;
        }
        if (active_tab == 1) {
            /* Target host field */
            vga_draw_string_trans(wx+6, pa_y+4, "Target Host:", nu_sub);
            vga_fill_rect(wx+80, pa_y, ww-160, 16, nu_card);
            vga_draw_rect_outline(wx+80, pa_y, ww-160, 16, nu_sep);
            { runtime_system_info_t sys;
              runtime_get_system_info(&sys);
              vga_draw_string_trans(wx+84, pa_y+4, sys.nodename, nu_txt); }
            gui_draw_rounded_rect(wx+ww-76, pa_y, 68, 16, 3, nu_acc);
            vga_draw_string_trans(wx+ww-72, pa_y+4, "Ping Now", RGB(255,255,255));
            pa_y += 22;
            { char runbuf[28]; int rpos = 0;
              runbuf[0] = 0;
              apps3_append_text(runbuf, &rpos, sizeof(runbuf), "Runs: ");
              apps3_append_uint(runbuf, &rpos, sizeof(runbuf), (uint32_t)g_netutil_ping_count);
              vga_draw_string_trans(wx+6, pa_y+2, "Ping Results (ms):", nu_txt);
              vga_draw_string_trans(wx+ww-76, pa_y+2, runbuf, nu_sub); }
            pa_y += 16;
            vga_fill_rect(wx+6, pa_y, ww-12, 80, nu_card);
            vga_draw_rect_outline(wx+6, pa_y, ww-12, 80, nu_sep);
            { int gi;
              for (gi=1;gi<4;gi++){
                  vga_draw_hline(wx+6, pa_y+gi*20, ww-12, nu_sep);
                  char gbuf2[4]; int_to_str(gi*25, gbuf2);
                  vga_draw_string_trans(wx+ww-24, pa_y+gi*20-8, gbuf2, nu_sub);
              }
            }
            { static const int pings[]={12,15,8,22,10,18,14,11,25,13,9,16,20,7,14};
              int pi3, n=15;
              for (pi3=0;pi3<n;pi3++){
                  int sample = pings[pi3] + (g_netutil_ping_count ? (g_netutil_ping_count + pi3 * 3) % 7 : 0);
                  int bar_x=wx+10+pi3*(ww-20)/n;
                  int bar_h;
                  if (sample > 25) sample = 25;
                  bar_h=sample*70/25;
                  if (bar_h<2) bar_h=2;
                  vga_fill_rect(bar_x, pa_y+78-bar_h, (ww-20)/n-2, bar_h, nu_acc);
              }
            }
            pa_y += 86;
            vga_fill_rect(wx+6, pa_y, ww-12, 50, nu_card);
            vga_draw_rect_outline(wx+6, pa_y, ww-12, 50, nu_sep);
            { uint32_t sent = net ? net->tx_packets + (uint32_t)g_netutil_ping_count : (uint32_t)g_netutil_ping_count;
              uint32_t recv = net ? net->rx_packets + (uint32_t)g_netutil_ping_count : 0;
              int loss = (sent > recv && sent > 0) ? (int)(((sent - recv) * 100U) / sent) : 0;
              char sentbuf[32], recvbuf[32], lossbuf[32], pct[8];
              int sp = 0, rp = 0, lp = 0;
              sentbuf[0] = recvbuf[0] = lossbuf[0] = 0;
              runtime_format_percent(loss, pct, sizeof(pct));
              apps3_append_text(sentbuf, &sp, sizeof(sentbuf), "Packets Sent: ");
              apps3_append_uint(sentbuf, &sp, sizeof(sentbuf), sent);
              apps3_append_text(recvbuf, &rp, sizeof(recvbuf), "Packets Recv: ");
              apps3_append_uint(recvbuf, &rp, sizeof(recvbuf), recv);
              apps3_append_text(lossbuf, &lp, sizeof(lossbuf), "Packet Loss: ");
              apps3_append_text(lossbuf, &lp, sizeof(lossbuf), pct);
              vga_draw_string_trans(wx+10, pa_y+4,  sentbuf, nu_txt);
              vga_draw_string_trans(wx+10, pa_y+16, recvbuf, nu_grn);
              vga_draw_string_trans(wx+10, pa_y+28, lossbuf, loss ? RGB(255,149,0) : nu_grn);
              vga_draw_string_trans(wx+10, pa_y+40, net && net->name ? net->name : "No interface", nu_sub); }
            return 1;
        }
        if (active_tab == 2) {
            char local[16], gateway[16], dns[16];
            runtime_format_ipv4(net ? net->ipv4 : 0, local, sizeof(local));
            runtime_format_ipv4(gw_ip, gateway, sizeof(gateway));
            runtime_format_ipv4(dns_ip, dns, sizeof(dns));
            vga_fill_rect(wx+6, pa_y, ww-12, 112, nu_card);
            vga_draw_rect_outline(wx+6, pa_y, ww-12, 112, nu_sep);
            vga_draw_string_trans(wx+12, pa_y+8, "Traceroute", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+30, "1  local interface", nu_sub); vga_draw_string_trans(wx+150, pa_y+30, local, nu_txt);
            vga_draw_string_trans(wx+12, pa_y+48, "2  default gateway", nu_sub); vga_draw_string_trans(wx+150, pa_y+48, gw_ip ? gateway : "not configured", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+66, "3  resolver", nu_sub); vga_draw_string_trans(wx+150, pa_y+66, dns_ip ? dns : "not configured", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+88, net && net->up ? "Path ready" : "Interface is down", net && net->up ? nu_grn : RGB(255,149,0));
            return 1;
        }
        if (active_tab == 3) {
            runtime_system_info_t sys;
            char dns[16];
            runtime_get_system_info(&sys);
            runtime_format_ipv4(dns_ip, dns, sizeof(dns));
            vga_fill_rect(wx+6, pa_y, ww-12, 100, nu_card);
            vga_draw_rect_outline(wx+6, pa_y, ww-12, 100, nu_sep);
            vga_draw_string_trans(wx+12, pa_y+8, "Lookup", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+30, "Name:", nu_sub); vga_draw_string_trans(wx+80, pa_y+30, sys.nodename, nu_txt);
            vga_draw_string_trans(wx+12, pa_y+48, "Server:", nu_sub); vga_draw_string_trans(wx+80, pa_y+48, dns_ip ? dns : "not configured", nu_txt);
            vga_draw_string_trans(wx+12, pa_y+66, "Records shown from runtime network state", nu_sub);
            return 1;
        }
        {
            int ports[] = {22, 53, 80, 443};
            const char *names[] = {"SSH", "DNS", "HTTP", "HTTPS"};
            int pi4;
            vga_fill_rect(wx+6, pa_y, ww-12, 118, nu_card);
            vga_draw_rect_outline(wx+6, pa_y, ww-12, 118, nu_sep);
            vga_draw_string_trans(wx+12, pa_y+8, "Port Scan", nu_txt);
            for (pi4=0; pi4<4; pi4++) {
                char pbuf[8];
                int row_y = pa_y + 30 + pi4 * 20;
                runtime_format_uint((uint32_t)ports[pi4], pbuf, sizeof(pbuf));
                vga_draw_string_trans(wx+16, row_y, pbuf, nu_sub);
                vga_draw_string_trans(wx+64, row_y, names[pi4], nu_txt);
                vga_draw_string_trans(wx+150, row_y, net && net->up ? "reachable" : "blocked", net && net->up ? nu_grn : RGB(255,149,0));
            }
            return 1;
        }
    }

    /* ---- Math Notes ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Math Notes")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t mn_bg   = g_pref_darkmode ? RGB(24,24,28)   : RGB(254,252,246);
        uint32_t mn_txt  = g_pref_darkmode ? RGB(220,220,228) : RGB(20,20,28);
        uint32_t mn_acc  = RGB(255,149,0);
        uint32_t mn_res  = RGB(0,122,255);
        uint32_t mn_sep  = g_pref_darkmode ? RGB(50,50,58)   : RGB(210,210,218);
        uint32_t mn_hdr  = g_pref_darkmode ? RGB(32,32,38)   : RGB(236,234,228);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, mn_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 20, mn_hdr);
        vga_draw_hline(wx+1, content_y+20, ww-2, mn_sep);
        vga_draw_string_trans(wx+6, content_y+6, "Math Notes", mn_txt);
        vga_draw_string_trans(wx+ww-70, content_y+6, "New Note", mn_acc);
        if (g_math_notes_created > 0) {
            char note_line[24];
            char note_num[8];
            int np = 0;
            note_line[0] = 0;
            apps3_append_text(note_line, &np, sizeof(note_line), "Notes: ");
            runtime_format_uint((uint32_t)g_math_notes_created + 1U, note_num, sizeof(note_num));
            apps3_append_text(note_line, &np, sizeof(note_line), note_num);
            vga_draw_string_trans(wx+8, content_y+22, note_line, mn_acc);
        }
        /* Notebook-style ruling */
        { int li4, ly;
          for (li4=0;li4<20;li4++){
              ly = content_y+24+li4*18;
              if (ly>content_y+content_h-4) break;
              vga_draw_hline(wx+2, ly, ww-4, g_pref_darkmode?RGB(38,38,46):RGB(220,218,210));
          }
          /* Red margin line */
          vga_draw_vline(wx+50, content_y+21, content_h-21, RGB(220,80,80));
        }
        /* Equations with results */
        { struct { const char *expr; const char *result; } eqs[] = {
              {"2 + 2",         "= 4"},
              {"100 / 4",       "= 25"},
              {"12 * 6",        "= 72"},
              {"sqrt(144)",     "= 12"},
              {"15% of 200",    "= 30"},
              {"(5 + 3)^2",     "= 64"},
              {"area = pi*r^2", ""},
              {"where r = 2",   ""},
              {"area",          "= 12.566"},
              {"tax = 0.08",    ""},
              {"price = 49.99", ""},
              {"total",         "= 53.99"},
          };
          int ei, ey=content_y+28;
          int n_eq=12;
          for (ei=0;ei<n_eq;ei++){
              if (ey+14>content_y+content_h-4) break;
              /* Equation text */
              vga_draw_string_trans(wx+56, ey, eqs[ei].expr, mn_txt);
              /* Result aligned right with blue color */
              if (eqs[ei].result[0]) {
                  int rlen=str_len(eqs[ei].result);
                  int rx2=wx+ww-rlen*8-6;
                  vga_draw_string_trans(rx2, ey, eqs[ei].result, mn_res);
                  /* Underline the result */
                  vga_draw_hline(rx2, ey+10, rlen*8, mn_sep);
              }
              ey += 18;
          }
        }
        /* Cursor line */
        { uint32_t t4=timer_ticks();
          if ((t4/400)%2==0){
              int cy2=content_y+28+12*18;
              if (cy2 < content_y+content_h-4)
                  vga_fill_rect(wx+56, cy2, 2, 12, mn_acc);
          }
        }
        return 1;
    }

    /* ---- Final Cut Pro ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Final Cut Pro")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        /* Dark professional color scheme */
        uint32_t fc_bg   = RGB(28,28,32);
        uint32_t fc_tb   = RGB(38,38,44);
        uint32_t fc_txt  = RGB(215,215,225);
        uint32_t fc_sub  = RGB(130,130,140);
        uint32_t fc_acc  = RGB(220,40,40);
        uint32_t fc_sep  = RGB(52,52,60);
        uint32_t fc_tl   = RGB(48,48,56);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, fc_bg);
        /* Top toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 26, fc_tb);
        vga_draw_hline(wx+1, content_y+26, ww-2, fc_sep);
        /* Transport controls */
        { int tx5=wx+6, ty5=content_y+5;
          static const char *tctrls[]={"<<","|>",">>","[]"};
          int ci5;
          for (ci5=0;ci5<4;ci5++){
              int is_fc_ctrl = ci5 == g_finalcut_transport;
              gui_draw_rounded_rect(tx5, ty5, 20, 16, 3, is_fc_ctrl?fc_acc:fc_tl);
              vga_draw_string_trans(tx5+4, ty5+4, tctrls[ci5], is_fc_ctrl?RGB(255,255,255):fc_txt);
              tx5 += 24;
          }
          /* Timecode */
          vga_fill_rect(tx5+4, ty5, 80, 16, RGB(20,20,24));
          vga_draw_rect_outline(tx5+4, ty5, 80, 16, fc_sep);
          { char fc_tc[12];
            apps3_format_timecode(120 + (uint32_t)(g_finalcut_transport * 15), fc_tc, sizeof(fc_tc));
            vga_draw_string_trans(tx5+8, ty5+4, fc_tc, fc_acc); }
          tx5 += 92;
          /* Audio meters */
          { int mi5, my5=ty5, mx5=tx5+4;
            for (mi5=0;mi5<2;mi5++){
                vga_fill_rect(mx5+mi5*6, my5, 4, 14, RGB(20,20,24));
                vga_fill_rect(mx5+mi5*6, my5+14-10, 4, 10, RGB(52,199,89));
                vga_fill_rect(mx5+mi5*6, my5+14-13, 4, 3, RGB(255,214,10));
            }
          }
          /* Zoom/share buttons right side */
          vga_draw_string_trans(wx+ww-70, content_y+9, "Share...", fc_sub);
        }
        /* Three-panel layout */
        int browser_w = ww * 28 / 100;
        int viewer_w  = ww * 42 / 100;
        int insp_w2   = ww - browser_w - viewer_w - 4;
        int panel_y   = content_y + 27;
        int panel_h   = content_h * 60 / 100;
        int tl_y      = panel_y + panel_h;
        int tl_h      = content_h - panel_h - 27;
        /* Browser (left) */
        vga_fill_rect(wx+1, panel_y, browser_w, panel_h, RGB(32,32,38));
        vga_draw_vline(wx+browser_w+1, panel_y, panel_h, fc_sep);
        vga_draw_string_trans(wx+4, panel_y+4, "LIBRARIES", fc_sub);
        { static const char *libs[]={"MyOS Project","B-Roll","Music","Graphics"};
          int li5;
          for (li5=0;li5<4;li5++){
              int ly5=panel_y+18+li5*20;
              if (ly5+16>panel_y+panel_h-4) break;
              if (li5==g_finalcut_library) vga_fill_rect(wx+2, ly5, browser_w-2, 18, RGB(44,44,52));
              /* Folder icon */
              vga_fill_rect(wx+6, ly5+4, 12, 10, li5==g_finalcut_library?fc_acc:fc_sub);
              vga_fill_rect(wx+6, ly5+2, 6, 4, li5==g_finalcut_library?fc_acc:fc_sub);
              vga_draw_string_trans(wx+22, ly5+5, libs[li5], li5==g_finalcut_library?fc_txt:fc_sub);
          }
        }
        /* Viewer (center) */
        int vx2=wx+browser_w+2;
        vga_fill_rect(vx2, panel_y, viewer_w, panel_h, RGB(18,18,22));
        vga_draw_vline(vx2+viewer_w, panel_y, panel_h, fc_sep);
        /* Video frame preview */
        { int vfx=vx2+6, vfy=panel_y+8, vfw=viewer_w-12, vfh=panel_h-28;
          int third_w = vfw / 3;
          if (third_w < 1) third_w = 1;
          vga_fill_rect(vfx, vfy, vfw, vfh, RGB(10,10,14));
          /* Safe area guides */
          vga_draw_rect_outline(vfx+vfw/10, vfy+vfh/10, vfw*8/10, vfh*8/10, RGB(50,50,60));
          /* Frame content: gradient sky */
          { int gri2;
            for (gri2=0;gri2<vfh/2;gri2++){
                vga_draw_hline(vfx+1, vfy+1+gri2, vfw-2, RGB(30+gri2*2,60+gri2,120+gri2));
            }
          }
          /* Ground */
          vga_fill_rect(vfx+1, vfy+vfh/2, vfw-2, vfh/2-1, RGB(20,40,20));
          /* Mountain silhouette in frame */
          { int mi6;
            for (mi6=0;mi6<vfw-2;mi6++){
                int mh6=(mi6<third_w)?(mi6*(vfh/3)/third_w):(vfh/3-(mi6-third_w)*(vfh/4)/third_w);
                if (mh6<0) mh6=0;
                if (mh6>vfh/2-2) mh6=vfh/2-2;
                vga_fill_rect(vfx+1+mi6, vfy+vfh/2-mh6, 1, mh6, RGB(25,55,90));
            }
          }
          /* Playhead in viewer */
          vga_draw_hline(vfx, vfy+vfh-2, vfw, fc_acc);
          /* Timecode overlay */
          vga_fill_rect_alpha(vfx+2, vfy+2, 80, 12, RGB(0,0,0), 150);
          { char fc_overlay_tc[12];
            apps3_format_timecode(120, fc_overlay_tc, sizeof(fc_overlay_tc));
            vga_draw_string_trans(vfx+4, vfy+4, fc_overlay_tc, fc_acc); }
        }
        /* Inspector (right) */
        int ix2=vx2+viewer_w+2;
        vga_fill_rect(ix2, panel_y, insp_w2, panel_h, RGB(32,32,38));
        vga_draw_string_trans(ix2+4, panel_y+4, "INSPECTOR", fc_sub);
        { static const char *iprops[]={"Effect:","Speed:","Opacity:","Color:"};
          int pi5;
          for (pi5=0;pi5<4;pi5++){
              int py5=panel_y+18+pi5*20;
              char prop_value[16];
              if (py5+16>panel_y+panel_h-4) break;
              if (pi5 == 1 || pi5 == 2) runtime_format_percent(100, prop_value, sizeof(prop_value));
              else if (pi5 == 0) { prop_value[0]='N'; prop_value[1]='o'; prop_value[2]='n'; prop_value[3]='e'; prop_value[4]=0; }
              else { prop_value[0]='W'; prop_value[1]='a'; prop_value[2]='r'; prop_value[3]='m'; prop_value[4]=0; }
              vga_draw_string_trans(ix2+4, py5, iprops[pi5], fc_sub);
              vga_draw_string_trans(ix2+4, py5+10, prop_value, fc_txt);
          }
        }
        /* Timeline area */
        vga_fill_rect(wx+1, tl_y, ww-2, tl_h, fc_tl);
        vga_draw_hline(wx+1, tl_y, ww-2, fc_sep);
        /* Track labels */
        int lbl_w2=60;
        vga_fill_rect(wx+1, tl_y+1, lbl_w2, tl_h-1, RGB(32,32,38));
        vga_draw_vline(wx+lbl_w2+1, tl_y+1, tl_h-1, fc_sep);
        { static const char *trks[]={"V1","V2","A1","A2"};
          static uint32_t tc2[]={0,0,0,0};
          tc2[0]=RGB(0,122,255); tc2[1]=RGB(52,199,89); tc2[2]=RGB(255,159,10); tc2[3]=RGB(255,159,10);
          int ti5;
          for (ti5=0;ti5<4;ti5++){
              int ty6=tl_y+4+ti5*(tl_h-8)/4;
              if (ty6>tl_y+tl_h-10) break;
              vga_fill_rect(wx+2, ty6, 4, 14, tc2[ti5]);
              vga_draw_string_trans(wx+10, ty6+4, trks[ti5], fc_txt);
          }
        }
        /* Timeline ruler */
        int tl_rw=ww-lbl_w2-4;
        vga_fill_rect(wx+lbl_w2+2, tl_y+1, tl_rw, 12, RGB(24,24,28));
        { int ri5;
          for (ri5=0;ri5<10;ri5++){
              int rx5=wx+lbl_w2+2+ri5*tl_rw/10;
              char tbf2[4]; tbf2[0]='0'; tbf2[1]=':'; tbf2[2]='0'+ri5; tbf2[3]=0;
              vga_draw_vline(rx5, tl_y+1, 8, fc_sep);
              vga_draw_string_trans(rx5+2, tl_y+3, tbf2, fc_sub);
          }
        }
        /* Clips in timeline */
        { static uint32_t clip_c[]={RGB(0,90,200),RGB(0,122,255),RGB(255,140,0),RGB(255,159,10)};
          int ci6;
          for (ci6=0;ci6<4;ci6++){
              int cy6=tl_y+14+ci6*(tl_h-14)/4;
              int cw6=(ci6%2==0)?tl_rw/3:tl_rw/4;
              int cx6=wx+lbl_w2+2+(ci6%3)*tl_rw/8;
              if (cy6+12>tl_y+tl_h-4) break;
              gui_draw_rounded_rect(cx6, cy6, cw6, 12, 2, clip_c[ci6]);
              vga_fill_rect_alpha(cx6+1, cy6+1, cw6-2, 4, RGB(255,255,255), 20);
              /* Waveform in audio clips */
              if (ci6>=2){
                  int wi5;
                  for (wi5=0;wi5<cw6-2;wi5+=2){
                      int wh6=(wi5%7)*3/2+2;
                      vga_fill_rect(cx6+1+wi5, cy6+12/2-wh6/2, 1, wh6, RGB(255,255,255));
                  }
              }
          }
        }
        /* Playhead in timeline */
        int ph3=wx+lbl_w2+2+tl_rw*42/100;
        vga_fill_rect(ph3-1, tl_y+1, 2, tl_h-2, fc_acc);
        vga_fill_rect(ph3-4, tl_y+1, 8, 6, fc_acc);
        return 1;
    }

    /* ---- Logic Pro ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Logic Pro")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int content_y = wy + TITLEBAR_H;
        int content_h = wh - TITLEBAR_H;
        uint32_t lp_bg   = RGB(24,24,28);
        uint32_t lp_tb   = RGB(34,34,40);
        uint32_t lp_txt  = RGB(215,215,225);
        uint32_t lp_sub  = RGB(130,130,140);
        uint32_t lp_acc  = RGB(0,160,210);
        uint32_t lp_sep  = RGB(48,48,56);
        uint32_t lp_grn  = RGB(48,209,88);
        vga_fill_rect(wx+1, content_y, ww-2, content_h, lp_bg);
        /* Top toolbar */
        vga_fill_rect(wx+1, content_y, ww-2, 28, lp_tb);
        vga_draw_hline(wx+1, content_y+28, ww-2, lp_sep);
        /* Transport */
        { int tx6=wx+4, ty6=content_y+6;
          static const char *tc3[]={"<<","|<","|>",">|",">>"};
          int ci7;
          for (ci7=0;ci7<5;ci7++){
              vga_fill_rect(tx6, ty6, 18, 16, RGB(44,44,52));
              vga_draw_rect_outline(tx6, ty6, 18, 16, lp_sep);
              vga_draw_string_trans(tx6+3, ty6+4, tc3[ci7], ci7==2?lp_grn:lp_txt);
              tx6 += 22;
          }
          /* Tempo/key */
          vga_fill_rect(tx6+4, ty6, 56, 16, RGB(18,18,22));
          vga_draw_rect_outline(tx6+4, ty6, 56, 16, lp_sep);
          { char bpm_buf[12];
            apps3_format_uint_suffix(100U + ((timer_ticks() / 1000U) % 40U), " BPM", bpm_buf, sizeof(bpm_buf));
            vga_draw_string_trans(tx6+8, ty6+4, bpm_buf, lp_acc); }
          tx6 += 64;
          vga_fill_rect(tx6+4, ty6, 40, 16, RGB(18,18,22));
          vga_draw_rect_outline(tx6+4, ty6, 40, 16, lp_sep);
          vga_draw_string_trans(tx6+8, ty6+4, "C Maj", lp_txt);
          /* Timecode */
          tx6 += 48;
          vga_fill_rect(tx6+4, ty6, 72, 16, RGB(18,18,22));
          vga_draw_rect_outline(tx6+4, ty6, 72, 16, lp_sep);
          vga_draw_string_trans(tx6+8, ty6+4, "1 1 1 1000", lp_acc);
        }
        /* Left panel: tracks/instruments */
        int lp_lw = 80;
        vga_fill_rect(wx+1, content_y+29, lp_lw, content_h-29, RGB(30,30,36));
        vga_draw_vline(wx+lp_lw+1, content_y+29, content_h-29, lp_sep);
        { static const char *inst2[]={"Grand Piano","Strings","Synth Lead","Bass","Drums","FX Pad"};
          static uint32_t ic2[]={0,0,0,0,0,0};
          ic2[0]=RGB(0,122,255); ic2[1]=RGB(148,44,246); ic2[2]=lp_acc;
          ic2[3]=RGB(255,159,10); ic2[4]=RGB(220,50,50); ic2[5]=RGB(52,199,89);
          int ii2, track_h2=(content_h-29)/6;
          for (ii2=0;ii2<6;ii2++){
              int iy2=content_y+29+ii2*track_h2;
              if (iy2+track_h2>content_y+content_h-2) break;
              vga_draw_hline(wx+1, iy2, lp_lw, lp_sep);
              /* Instrument color bar */
              vga_fill_rect(wx+2, iy2+2, 4, track_h2-4, ic2[ii2]);
              /* Name */
              vga_draw_string_trans(wx+10, iy2+track_h2/2-4, inst2[ii2], lp_txt);
              /* Mute/Solo micro-buttons */
              vga_fill_rect(wx+lp_lw-18, iy2+3, 8, 8, RGB(255,214,10));
              vga_fill_rect(wx+lp_lw-8,  iy2+3, 6, 8, RGB(80,80,90));
          }
        }
        /* MIDI/Audio regions */
        int lp_tx=wx+lp_lw+2;
        int lp_tw=ww-lp_lw-4;
        /* Beat ruler */
        vga_fill_rect(lp_tx, content_y+29, lp_tw, 12, RGB(22,22,26));
        { int bi3;
          for (bi3=1;bi3<=16;bi3++){
              int bx3=lp_tx+bi3*lp_tw/17;
              vga_draw_vline(bx3, content_y+29, 8, bi3%4==0?lp_sep:RGB(38,38,46));
              if (bi3%4==0) {
                  char bbuf[3]; bbuf[0]='0'+(char)(bi3/4); bbuf[1]=0;
                  vga_draw_string_trans(bx3+2, content_y+31, bbuf, lp_sub);
              }
          }
        }
        /* Track regions */
        { static uint32_t rc2[]={RGB(0,90,200),RGB(120,30,200),RGB(0,130,180),RGB(200,130,0),RGB(180,30,30),RGB(40,170,60)};
          int ri6, track_h3=(content_h-29-12)/6;
          if (track_h3 < 5) track_h3 = 5;
          for (ri6=0;ri6<6;ri6++){
              int ry3=content_y+41+ri6*track_h3;
              if (ry3+track_h3-2>content_y+content_h-2) break;
              vga_draw_hline(lp_tx, ry3, lp_tw, lp_sep);
              /* Region block */
              int rstart=ri6%3, rw3=(ri6%2==0)?lp_tw/2:lp_tw/3;
              int rx3=lp_tx+rstart*lp_tw/8;
              gui_draw_rounded_rect(rx3, ry3+2, rw3, track_h3-4, 2, rc2[ri6]);
              vga_fill_rect_alpha(rx3+1, ry3+3, rw3-2, track_h3/4, RGB(255,255,255), 25);
              /* MIDI notes or waveform */
              if (ri6<4){
                  /* MIDI notes */
                  int ni2;
                  for (ni2=0;ni2<8;ni2++){
                      int npos=ri6*2+ni2*3;
                      int nx3=rx3+2+npos*(rw3-4)/24;
                      int nh3=4+(ni2%3)*3;
                      int ny3=ry3+track_h3-4-nh3;
                      if (nx3+3<rx3+rw3 && ny3>ry3+2)
                          vga_fill_rect(nx3, ny3, 3, nh3, RGB(255,255,255));
                  }
              } else {
                  /* Audio waveform */
                  int wi6;
                  for (wi6=0;wi6<rw3-4;wi6+=2){
                      int wh7=(wi6%(track_h3-4)>track_h3/2)?track_h3/4:wi6%5+2;
                      int wy3=ry3+track_h3/2-wh7/2;
                      vga_fill_rect(rx3+2+wi6, wy3, 1, wh7, RGB(255,255,255));
                  }
              }
          }
        }
        /* Playhead in Logic */
        int ph4=lp_tx+lp_tw*25/100;
        vga_fill_rect(ph4, content_y+29, 1, content_h-29, lp_acc);
        vga_fill_rect(ph4-3, content_y+29, 7, 6, lp_acc);
        return 1;
    }

    /* ---- About This Mac ---- */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "About This Mac")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy0 = wy + TITLEBAR_H + 1;
        uint32_t am_bg  = g_pref_darkmode ? RGB(28,28,32)    : RGB(240,240,246);
        uint32_t am_sep = g_pref_darkmode ? RGB(60,60,68)    : RGB(200,200,210);
        uint32_t am_txt = g_pref_darkmode ? RGB(220,220,230) : RGB(20,20,30);
        uint32_t am_sub = g_pref_darkmode ? RGB(130,130,142) : RGB(90,90,105);
        vga_fill_rect(wx+1, cy0, ww-2, wh-TITLEBAR_H-2, am_bg);

        /* Big macOS-style logo area */
        {
            int logo_cx = wx + ww/2;
            int logo_cy = cy0 + 34;
            /* Outer Sequoia gradient ring */
            gui_draw_circle(logo_cx, logo_cy, 30, RGB(80,160,255));
            gui_draw_circle(logo_cx, logo_cy, 26, RGB(60,120,220));
            gui_draw_circle(logo_cx, logo_cy, 20, RGB(40, 80,180));
            gui_draw_circle(logo_cx, logo_cy, 14, am_bg);
            /* "M" letter in center */
            vga_draw_string_trans(logo_cx-4, logo_cy-4, "M", RGB(255,255,255));
            /* "MyOS" below logo */
            { runtime_system_info_t sys;
              runtime_get_system_info(&sys);
              vga_draw_string_trans(wx+ww/2-16, logo_cy+34, sys.sysname, am_txt);
              vga_draw_string_trans(wx+ww/2-42, logo_cy+48, sys.release, am_sub); }
        }

        /* Divider */
        int cy = cy0 + 96;
        vga_draw_hline(wx+16, cy, ww-32, am_sep); cy += 10;

        /* Hardware info rows */
        {
            runtime_system_info_t sys;
            runtime_storage_info_t storage;
            char membuf[16];
            char storagebuf[32];
            char displaybuf[32];
            int sp = 0;
            const char *storage_text = "checking";
            runtime_get_system_info(&sys);
            runtime_format_bytes(sys.pmm_total_bytes, membuf, sizeof(membuf));
            apps3_format_display(&sys, displaybuf, sizeof(displaybuf));
            if (runtime_get_storage_info("/", &storage) == 0) {
                storagebuf[0] = 0;
                apps3_append_text(storagebuf, &sp, sizeof(storagebuf), storage.name);
                apps3_append_text(storagebuf, &sp, sizeof(storagebuf), " ");
                apps3_append_bytes(storagebuf, &sp, sizeof(storagebuf), storage.total_bytes);
                storage_text = storagebuf;
            }
            struct { const char *label; const char *value; } rows[] = {
                { "Chip",     sys.cpu_model    },
                { "Memory",   membuf           },
                { "Storage",  storage_text     },
                { "Display",  displaybuf       },
                { "Serial",   sys.nodename     },
                { "Build",    sys.version      },
            };
            int ri;
            int lbl_x = wx + 20;
            int val_x = wx + 110;
            for (ri = 0; ri < 6; ri++) {
                uint32_t rbc = (ri%2==0)?(g_pref_darkmode?RGB(35,35,42):RGB(248,248,254)):am_bg;
                vga_fill_rect(wx+1, cy, ww-2, 17, rbc);
                vga_draw_string_trans(lbl_x, cy+4, rows[ri].label, am_sub);
                vga_draw_string_trans(val_x, cy+4, rows[ri].value, am_txt);
                cy += 17;
            }
        }

        /* Divider */
        vga_draw_hline(wx+16, cy+4, ww-32, am_sep); cy += 14;

        /* Uptime */
        {
            uint32_t t_up = timer_ticks()/1000;
            uint32_t up_h=t_up/3600, up_m=(t_up/60)%60, up_s=t_up%60;
            char ub[32]; int ui=0;
            ub[ui++]='U';ub[ui++]='p';ub[ui++]='t';ub[ui++]='i';ub[ui++]='m';ub[ui++]='e';ub[ui++]=':';ub[ui++]=' ';
            ub[ui++]='0'+up_h/10;ub[ui++]='0'+up_h%10;ub[ui++]='h';ub[ui++]=' ';
            ub[ui++]='0'+up_m/10;ub[ui++]='0'+up_m%10;ub[ui++]='m';ub[ui++]=' ';
            ub[ui++]='0'+up_s/10;ub[ui++]='0'+up_s%10;ub[ui++]='s';ub[ui]=0;
            vga_draw_string_trans(wx + ww/2 - str_len(ub)*4, cy, ub, am_sub); cy += 16;
        }

        /* Software Update button */
        {
            int btn_w = 140, btn_h = 20;
            int btn_x = wx + ww/2 - btn_w/2;
            gui_draw_rounded_rect(btn_x, cy, btn_w, btn_h, 6, RGB(0,122,255));
            { const char *update_label = g_about_update_checks > 0 ? "Updates Open" : "Software Update";
              int ul = str_len(update_label) * 8;
              vga_draw_string_trans(btn_x + (btn_w - ul) / 2, cy+6, update_label, RGB(255,255,255)); }
        }

        return 1;
    }

    /* ---- Motion ---- */

    return 0;
}
