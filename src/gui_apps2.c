#include "gui_internal.h"

static void apps2_append_text(char *buf, int *pos, int max, const char *text) {
    int i = 0;
    if (!buf || max <= 0 || !text) return;
    while (text[i] && *pos + 1 < max) buf[(*pos)++] = text[i++];
    buf[*pos] = 0;
}

static void apps2_append_char(char *buf, int *pos, int max, char ch) {
    if (!buf || max <= 0 || *pos + 1 >= max) return;
    buf[(*pos)++] = ch;
    buf[*pos] = 0;
}

static void apps2_append_uint(char *buf, int *pos, int max, uint32_t value) {
    char nbuf[12];
    int i = 0;
    runtime_format_uint(value, nbuf, sizeof(nbuf));
    while (nbuf[i] && *pos + 1 < max) buf[(*pos)++] = nbuf[i++];
    buf[*pos] = 0;
}

static void apps2_append_2digit(char *buf, int *pos, int max, int value) {
    if (value < 0) value = 0;
    value %= 100;
    apps2_append_char(buf, pos, max, (char)('0' + value / 10));
    apps2_append_char(buf, pos, max, (char)('0' + value % 10));
}

static void apps2_append_bytes(char *buf, int *pos, int max, uint32_t bytes) {
    char bbuf[16];
    int i = 0;
    runtime_format_bytes(bytes, bbuf, sizeof(bbuf));
    while (bbuf[i] && *pos + 1 < max) buf[(*pos)++] = bbuf[i++];
    buf[*pos] = 0;
}

static void apps2_format_display(const runtime_system_info_t *sys, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0 || !sys) return;
    buf[0] = 0;
    apps2_append_uint(buf, &pos, max, sys->display_width);
    apps2_append_text(buf, &pos, max, "x");
    apps2_append_uint(buf, &pos, max, sys->display_height);
    apps2_append_text(buf, &pos, max, " ");
    apps2_append_uint(buf, &pos, max, sys->display_bpp);
    apps2_append_text(buf, &pos, max, "bpp ");
    apps2_append_text(buf, &pos, max, sys->display);
}

static void apps2_format_used_free(const runtime_storage_info_t *st, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0 || !st) return;
    buf[0] = 0;
    apps2_append_bytes(buf, &pos, max, st->used_bytes);
    apps2_append_text(buf, &pos, max, " used  ");
    apps2_append_bytes(buf, &pos, max, st->free_bytes);
    apps2_append_text(buf, &pos, max, " free");
}

static void apps2_format_time_12h(int total_minutes, char *buf, int max) {
    int pos = 0;
    int hour;
    int minute;
    int h12;
    const char *ampm;
    if (!buf || max <= 0) return;
    while (total_minutes < 0) total_minutes += 24 * 60;
    total_minutes %= 24 * 60;
    hour = total_minutes / 60;
    minute = total_minutes % 60;
    h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    ampm = hour < 12 ? "AM" : "PM";
    buf[0] = 0;
    apps2_append_uint(buf, &pos, max, (uint32_t)h12);
    apps2_append_char(buf, &pos, max, ':');
    apps2_append_2digit(buf, &pos, max, minute);
    apps2_append_char(buf, &pos, max, ' ');
    apps2_append_text(buf, &pos, max, ampm);
}

static void apps2_format_mail_time(int index, int detailed, char *buf, int max) {
    datetime_t now;
    int pos = 0;
    if (!buf || max <= 0) return;
    get_current_datetime(&now);
    buf[0] = 0;
    if (index == 0 || index == 1) {
        char timebuf[12];
        int offset = index == 0 ? 0 : 87;
        apps2_format_time_12h(now.hour * 60 + now.minute - offset, timebuf, sizeof(timebuf));
        if (detailed) apps2_append_text(buf, &pos, max, "Today ");
        apps2_append_text(buf, &pos, max, timebuf);
        return;
    }
    if (index == 2) {
        apps2_append_text(buf, &pos, max, "Yesterday");
        return;
    }
    {
        int offset = index == 3 ? 2 : 4;
        int weekday = (now.weekday + 7 - offset) % 7;
        apps2_append_text(buf, &pos, max,
            detailed ? datetime_weekday_long(weekday) : datetime_weekday_short(weekday));
    }
}

static void apps2_format_message_time(int index, char *buf, int max) {
    datetime_t now;
    int offset;
    if (!buf || max <= 0) return;
    get_current_datetime(&now);
    offset = index * 19;
    apps2_format_time_12h(now.hour * 60 + now.minute - offset, buf, max);
}

int draw_apps_group2(int idx) {
    if (idx < 0 || idx >= g_num_windows) return 0;

    /* Mail window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Mail")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy0=wy+TITLEBAR_H+1;
        int ch0=wh-TITLEBAR_H-19;

        uint32_t ml_bg    = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);
        uint32_t ml_sb    = g_pref_darkmode ? RGB(36,36,40)    : RGB(236,236,238);
        uint32_t ml_sb_bd = g_pref_darkmode ? RGB(55,55,60)    : RGB(210,210,212);
        uint32_t ml_tb    = g_pref_darkmode ? RGB(40,40,44)    : RGB(230,230,234);
        uint32_t ml_tb_bd = g_pref_darkmode ? RGB(58,58,62)    : RGB(200,200,204);
        uint32_t ml_sel   = g_pref_darkmode ? RGB(0,80,160)    : RGB(0,122,255);
        uint32_t ml_txt   = g_pref_darkmode ? RGB(220,220,224) : RGB(20,20,20);
        uint32_t ml_sub   = g_pref_darkmode ? RGB(130,130,138) : RGB(120,120,120);
        uint32_t ml_sep   = g_pref_darkmode ? RGB(55,55,60)    : RGB(210,210,215);
        uint32_t ml_unrd  = RGB(0,122,255);

        /* If composing, show compose overlay */
        if (g_mail_compose) {
            uint32_t cp_bg  = g_pref_darkmode ? RGB(36,36,40) : RGB(250,250,252);
            uint32_t cp_fld = g_pref_darkmode ? RGB(44,44,50) : RGB(255,255,255);
            uint32_t cp_bd  = g_pref_darkmode ? RGB(70,70,78) : RGB(180,180,190);
            uint32_t cp_lbl = g_pref_darkmode ? RGB(130,130,138) : RGB(120,120,130);
            uint32_t cp_txt = g_pref_darkmode ? RGB(220,220,226) : RGB(20,20,25);
            vga_fill_rect(wx+1, cy0, ww-2, ch0, cp_bg);
            /* Compose header bar */
            vga_fill_rect(wx+1, cy0, ww-2, 28, ml_tb);
            vga_draw_hline(wx+1, cy0+27, ww-2, ml_tb_bd);
            vga_draw_string_trans(wx+8, cy0+9, "Cancel", RGB(0,122,255));
            { int tl=str_len("New Message")*8;
              vga_draw_string_trans(wx+(ww-tl)/2, cy0+9, "New Message", cp_txt); }
            /* Send button */
            gui_draw_rounded_rect(wx+ww-58, cy0+5, 50, 18, 5, RGB(0,122,255));
            vga_draw_string_trans(wx+ww-44, cy0+9, "Send", RGB(255,255,255));
            cy0 += 30;
            /* To: field */
            vga_draw_string_trans(wx+8, cy0+6, "To:", cp_lbl);
            vga_fill_rect(wx+32, cy0+2, ww-40, 18, cp_fld);
            gui_draw_rounded_rect_outline(wx+32, cy0+2, ww-40, 18, 3,
                g_mail_focused_field==1 ? RGB(0,122,255) : cp_bd);
            { int ml=str_len(g_mail_to); int mx2=ml>((ww-48)/8)?((ww-48)/8):ml; int j;
              for(j=0;j<mx2;j++) vga_draw_char_trans(wx+36+j*8, cy0+5, g_mail_to[j], cp_txt);
              if(g_mail_focused_field==1&&(timer_ticks()/400)%2==0)
                  vga_fill_rect(wx+36+ml*8, cy0+4, 1, 12, cp_txt); }
            vga_draw_hline(wx+1, cy0+20, ww-2, ml_sep); cy0 += 22;
            /* Subject: field */
            vga_draw_string_trans(wx+8, cy0+6, "Sub:", cp_lbl);
            vga_fill_rect(wx+40, cy0+2, ww-48, 18, cp_fld);
            gui_draw_rounded_rect_outline(wx+40, cy0+2, ww-48, 18, 3,
                g_mail_focused_field==2 ? RGB(0,122,255) : cp_bd);
            { int ml=str_len(g_mail_subject); int mx2=ml>((ww-56)/8)?((ww-56)/8):ml; int j;
              for(j=0;j<mx2;j++) vga_draw_char_trans(wx+44+j*8, cy0+5, g_mail_subject[j], cp_txt);
              if(g_mail_focused_field==2&&(timer_ticks()/400)%2==0)
                  vga_fill_rect(wx+44+ml*8, cy0+4, 1, 12, cp_txt); }
            vga_draw_hline(wx+1, cy0+20, ww-2, ml_sep); cy0 += 22;
            /* Body area */
            int bh = wh-TITLEBAR_H-19-30-22-22-6;
            vga_fill_rect(wx+1, cy0, ww-2, bh, cp_fld);
            { uint32_t ab_bd = g_mail_focused_field==3 ? RGB(0,122,255) : cp_bd;
              vga_draw_rect_outline(wx+1, cy0, ww-2, bh, ab_bd); }
            { const char *body2=g_mail_body; int tx=wx+6, ty=cy0+4, j=0;
              while(body2[j]) {
                  if(body2[j]=='\n'){ty+=12;tx=wx+6;j++;continue;}
                  if(tx+8>wx+ww-4){ty+=12;tx=wx+6;}
                  if(ty>cy0+bh-14) break;
                  vga_draw_char_trans(tx,ty,body2[j],cp_txt); tx+=8; j++;
              }
              if(g_mail_focused_field==3&&(timer_ticks()/400)%2==0)
                  vga_fill_rect(tx,ty,1,10,cp_txt);
            }
            return 1;
        }

        /* Toolbar */
        vga_fill_rect(wx+1, cy0, ww-2, 26, ml_tb);
        vga_draw_hline(wx+1, cy0+25, ww-2, ml_tb_bd);
        /* Compose button (highlighted) */
        gui_draw_rounded_rect(wx+4, cy0+4, 28, 18, 4, RGB(0,122,255));
        vga_draw_string_trans(wx+8,  cy0+8, "New", RGB(255,255,255));
        vga_draw_string_trans(wx+36, cy0+7, "[<]", ml_sub);   /* reply */
        vga_draw_string_trans(wx+64, cy0+7, "[X]", ml_sub);   /* delete */
        vga_draw_string_trans(wx+ww-70, cy0+7, "[Search]", ml_sub);
        cy0 += 26;

        /* Sidebar + message list + preview pane */
        int sb_w = 68;   /* sidebar width */
        int list_w = (ww-2-sb_w)*2/5;
        int prev_w = (ww-2-sb_w) - list_w;

        /* Sidebar */
        vga_fill_rect(wx+1, cy0, sb_w, ch0-26, ml_sb);
        vga_draw_vline(wx+1+sb_w, cy0, ch0-26, ml_sb_bd);
        static const struct { const char *name; int cnt; } folders[] = {
            {"Inbox",   3}, {"Sent",  0}, {"Drafts", 1},
            {"Junk",    0}, {"Trash", 0}, {"Archive",0}
        };
        int fi3;
        for (fi3=0; fi3<6; fi3++) {
            int fy = cy0 + 6 + fi3*22;
            uint32_t ftxt = (fi3==0) ? ml_txt : ml_sub;
            if (fi3==0) vga_fill_rect(wx+1, fy-2, sb_w, 20, ml_sel);
            vga_draw_string_trans(wx+6, fy+2, folders[fi3].name, fi3==0?RGB(255,255,255):ftxt);
            if (folders[fi3].cnt > 0) {
                char cbuf[4]; int_to_str(folders[fi3].cnt, cbuf);
                int cbx = wx+1+sb_w-8-(int)str_len(cbuf)*8;
                vga_fill_rect(cbx-3, fy, (int)str_len(cbuf)*8+6, 14, ml_unrd);
                vga_draw_string_trans(cbx, fy+2, cbuf, RGB(255,255,255));
            }
        }

        /* Message list */
        int lx = wx+1+sb_w;
        vga_fill_rect(lx, cy0, list_w, ch0-26, ml_bg);
        vga_draw_vline(lx+list_w, cy0, ch0-26, ml_sep);
        static const struct { const char *from; const char *subj; int unread; } msgs[] = {
            {"Alice Chen",   "Meeting reminder",      1},
            {"Bob Smith",    "Re: Project update",    1},
            {"Newsletter",   "Weekly Digest",         1},
            {"GitHub",       "PR merged: feature",    0},
            {"Dave Park",    "Lunch plans?",          0},
        };
        int mi;
        for (mi=0; mi<5; mi++) {
            int my2 = cy0 + mi*38;
            char timebuf[20];
            if (my2+38 > cy0+ch0-26) break;
            uint32_t row_bg2 = (mi==0) ? (g_pref_darkmode?RGB(34,56,90):RGB(225,240,255)) : ml_bg;
            apps2_format_mail_time(mi, 0, timebuf, sizeof(timebuf));
            vga_fill_rect(lx, my2, list_w, 38, row_bg2);
            vga_draw_hline(lx, my2+37, list_w, ml_sep);
            if (msgs[mi].unread) vga_fill_rect(lx+4, my2+15, 6, 6, ml_unrd);
            vga_draw_string_trans(lx+14, my2+5,  msgs[mi].from,  ml_txt);
            vga_draw_string_trans(lx+14, my2+18, msgs[mi].subj,  ml_sub);
            vga_draw_string_trans(lx+list_w-44, my2+5, timebuf, ml_sub);
        }

        /* Preview pane */
        int px = lx+list_w;
        vga_fill_rect(px, cy0, prev_w, ch0-26, ml_bg);
        int pcy = cy0+8;
        static const struct {
            const char *subj; const char *from;
            const char *l1; const char *l2; const char *l3; const char *l4; const char *sign;
        } previews[] = {
            { "Meeting reminder", "Alice Chen <alice@example.com>",
              "Hi, Just a reminder", "we have our weekly", "sync coming up.", "Please be prepared!", "Alice" },
            { "Re: Project update", "Bob Smith <bob@work.com>",
              "Thanks for the update.", "The new feature looks", "great! Let's ship it", "by end of week.", "Bob" },
            { "Weekly Digest", "Newsletter <news@digest.com>",
              "Top stories this week:", "- AI hits new milestone", "- Markets up 3.2%", "- MyOS now trending!", "The Digest Team" },
            { "PR merged: feature", "GitHub <noreply@github.com>",
              "Your pull request was", "merged into main.", "Branch: feature/gui", "Congratulations!", "GitHub" },
            { "Lunch plans?", "Dave Park <dave@friend.io>",
              "Hey! Are you free for", "lunch on Thursday?", "The usual place at 1pm", "Let me know!", "Dave" },
        };
        char preview_time[24];
        int sel_m = g_mail_sel_msg; if(sel_m<0)sel_m=0; if(sel_m>4)sel_m=4;
        apps2_format_mail_time(sel_m, 1, preview_time, sizeof(preview_time));
        vga_draw_string_trans(px+8, pcy, previews[sel_m].subj, ml_txt); pcy+=14;
        vga_draw_string_trans(px+8, pcy, previews[sel_m].from, ml_sub); pcy+=12;
        vga_draw_string_trans(px+8, pcy, "To: Me", ml_sub); pcy+=12;
        vga_draw_string_trans(px+8, pcy, preview_time, ml_sub); pcy+=16;
        vga_draw_hline(px+4, pcy, prev_w-8, ml_sep); pcy+=10;
        vga_draw_string_trans(px+8, pcy, previews[sel_m].l1, ml_txt); pcy+=14;
        vga_draw_string_trans(px+8, pcy, previews[sel_m].l2, ml_txt); pcy+=12;
        vga_draw_string_trans(px+8, pcy, previews[sel_m].l3, ml_txt); pcy+=12;
        vga_draw_string_trans(px+8, pcy, previews[sel_m].l4, ml_txt); pcy+=18;
        vga_draw_string_trans(px+8, pcy, "Best,", ml_sub); pcy+=12;
        vga_draw_string_trans(px+8, pcy, previews[sel_m].sign, ml_txt); pcy+=16;
        /* Reply button */
        if(pcy+20 < cy0+ch0-30) {
            gui_draw_rounded_rect(px+8, pcy, 44, 18, 5, ml_sel);
            vga_draw_string_trans(px+14, pcy+5, "Reply", RGB(255,255,255));
            gui_draw_rounded_rect(px+58, pcy, 52, 18, 5, ml_sel);
            vga_draw_string_trans(px+62, pcy+5, "Forward", RGB(255,255,255));
        }

        /* Status bar */
        int sby = wy+wh-18;
        vga_fill_rect(wx+1, sby, ww-2, 1, ml_sep);
        vga_fill_rect(wx+1, sby+1, ww-2, 17, ml_tb);
        vga_draw_string_trans(wx+8, sby+4, "3 unread, 5 messages", ml_sub);
        return 1;
    }

    /* Stocks window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Stocks")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t sk_bg  = g_pref_darkmode ? RGB(18,18,20)    : RGB(245,245,250);
        uint32_t sk_hd  = g_pref_darkmode ? RGB(30,30,34)    : RGB(230,230,235);
        uint32_t sk_sep = g_pref_darkmode ? RGB(50,50,55)    : RGB(200,200,205);
        uint32_t sk_txt = g_pref_darkmode ? RGB(220,220,225) : RGB(25,25,35);
        uint32_t sk_sub = g_pref_darkmode ? RGB(120,120,130) : RGB(100,100,110);
        uint32_t sk_up  = RGB(52,199,89);   /* green for gains */
        uint32_t sk_dn  = RGB(255,69,58);   /* red for losses */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, sk_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 26, sk_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+27, ww-2, sk_sep);
        { int tl=str_len("Stocks")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+9, "Stocks", sk_txt); }
        /* Animated sparkline for AAPL */
        uint32_t t_sk = timer_ticks();
        /* Stock rows */
        static const struct { const char *sym; const char *name; int price; int cents; int chg; int pos; } stocks[] = {
            { "AAPL",  "Apple Inc.",       189, 42,  237, 1 },
            { "MSFT",  "Microsoft",        415, 88, -112, 0 },
            { "GOOGL", "Alphabet",         175, 30,  519, 1 },
            { "AMZN",  "Amazon",           198, 75, -89, 0 },
            { "NVDA",  "NVIDIA",           875, 60, 1423, 1 },
            { "META",  "Meta Platforms",   524, 12,  334, 1 },
            { "TSLA",  "Tesla",            248, 95, -201, 0 },
        };
        int ns = 7, si2;
        int row_h = (wh - TITLEBAR_H - 50) / ns;
        if (row_h < 28) row_h = 28;
        for (si2=0; si2<ns; si2++) {
            int ry2 = wy + TITLEBAR_H + 32 + si2*row_h;
            if (ry2 + row_h > wy+wh-22) break;
            if (si2 % 2 == 0) {
                uint32_t stripe = g_pref_darkmode ? RGB(25,25,28) : RGB(238,238,243);
                vga_fill_rect(wx+2, ry2, ww-4, row_h-1, stripe);
            }
            /* Symbol */
            vga_draw_string_trans(wx+8, ry2+4, stocks[si2].sym, sk_txt);
            /* Name (truncated) */
            vga_draw_string_trans(wx+8, ry2+14, stocks[si2].name, sk_sub);
            /* Mini sparkline */
            int spx = wx + ww - 100;
            int i_sp;
            static const int8_t spark[8] = {0,3,-2,5,-1,7,-3,6};
            for (i_sp=0; i_sp<7; i_sp++) {
                int y1s = ry2+18 - (int)spark[i_sp];
                int y2s = ry2+18 - (int)spark[i_sp+1];
                int xp  = spx + i_sp*8;
                uint32_t sc = stocks[si2].pos ? sk_up : sk_dn;
                vga_draw_line(xp,y1s, xp+8,y2s, sc);
            }
            /* Price + change badge */
            char pstr[12];
            pstr[0]='$'; pstr[1]='0'+stocks[si2].price/100;
            pstr[2]='0'+(stocks[si2].price/10)%10;
            pstr[3]='0'+stocks[si2].price%10;
            pstr[4]='.';
            pstr[5]='0'+stocks[si2].cents/10;
            pstr[6]='0'+stocks[si2].cents%10;
            pstr[7]=0;
            vga_draw_string_trans(wx+ww-92, ry2+4, pstr, sk_txt);
            uint32_t badge = stocks[si2].pos ? sk_up : sk_dn;
            gui_draw_rounded_rect(wx+ww-44, ry2+2, 40, 18, 5, badge);
            char cstr[8];
            int cv = stocks[si2].chg < 0 ? -stocks[si2].chg : stocks[si2].chg;
            cstr[0] = stocks[si2].pos ? '+' : '-';
            cstr[1] = '0' + cv/100;
            cstr[2] = '.';
            cstr[3] = '0' + (cv/10)%10;
            cstr[4] = '%'; cstr[5] = 0;
            vga_draw_string_trans(wx+ww-42, ry2+5, cstr, RGB(255,255,255));
            vga_draw_hline(wx+4, ry2+row_h-1, ww-8, sk_sep);
        }
        /* Market status bar */
        int sb_y = wy+wh-36;
        vga_fill_rect(wx+2, sb_y, ww-4, 16, sk_hd);
        { char nasdaq[24];
          char sp500[24];
          int np = 0, sp = 0;
          int n_delta = 40 + (int)((timer_ticks() / 1000U) % 80U);
          int s_delta = 25 + (int)((timer_ticks() / 1500U) % 70U);
          nasdaq[0] = 0;
          sp500[0] = 0;
          apps2_append_text(nasdaq, &np, sizeof(nasdaq), "NASDAQ  +");
          apps2_append_text(sp500, &sp, sizeof(sp500), "S&P 500 +");
          nasdaq[np++] = (char)('0' + n_delta / 100); nasdaq[np++] = '.';
          nasdaq[np++] = (char)('0' + (n_delta / 10) % 10); nasdaq[np++] = (char)('0' + n_delta % 10); nasdaq[np++] = '%'; nasdaq[np] = 0;
          sp500[sp++] = (char)('0' + s_delta / 100); sp500[sp++] = '.';
          sp500[sp++] = (char)('0' + (s_delta / 10) % 10); sp500[sp++] = (char)('0' + s_delta % 10); sp500[sp++] = '%'; sp500[sp] = 0;
          vga_draw_string_trans(wx+8, sb_y+4, nasdaq, sk_up);
          vga_draw_string_trans(wx+ww/2-20, sb_y+4, sp500, sk_up); }
        (void)t_sk;
        return 1;
    }

    /* News window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "News")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t nw_bg  = g_pref_darkmode ? RGB(20,20,22)    : RGB(248,248,250);
        uint32_t nw_hd  = g_pref_darkmode ? RGB(32,32,36)    : RGB(232,232,238);
        uint32_t nw_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(205,205,210);
        uint32_t nw_txt = g_pref_darkmode ? RGB(215,215,220) : RGB(25,25,35);
        uint32_t nw_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(110,110,120);
        uint32_t nw_acc = RGB(255,59,48);   /* News red */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, nw_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, nw_hd);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+8, "Apple", nw_acc);
        vga_draw_string_trans(wx+8+5*8+4, wy+TITLEBAR_H+8, "News", nw_txt);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, nw_sep);
        /* TOP STORY - large feature */
        int feat_y = wy+TITLEBAR_H+28;
        vga_fill_rect(wx+8, feat_y, ww-16, 70, RGB(210,40,40));
        vga_draw_string_trans(wx+12, feat_y+4, "TODAY'S TOP STORY", RGB(255,200,200));
        vga_draw_string_trans(wx+12, feat_y+18, "AI Revolution Changes", RGB(255,255,255));
        vga_draw_string_trans(wx+12, feat_y+30, "How We Work Forever", RGB(255,255,255));
        vga_draw_string_trans(wx+12, feat_y+44, "Tech & Business | 2 min read", RGB(255,200,200));
        { char clockbuf[12];
          char timebuf[32];
          datetime_t story_now;
          int tp = 0;
          get_current_datetime(&story_now);
          get_clock_str(clockbuf);
          timebuf[0] = 0;
          apps2_append_text(timebuf, &tp, sizeof(timebuf), datetime_weekday_short(story_now.weekday));
          apps2_append_text(timebuf, &tp, sizeof(timebuf), ", ");
          apps2_append_text(timebuf, &tp, sizeof(timebuf), clockbuf);
          vga_draw_string_trans(wx+12, feat_y+56, timebuf, nw_sub); }
        /* Article list */
        static const struct { const char *cat; const char *hed; const char *src; uint32_t age_seconds; } arts[] = {
            { "SCIENCE",    "New Space Station Module", "NASA",    1U * 3600U },
            { "POLITICS",   "Global Climate Summit",   "Reuters", 2U * 3600U },
            { "SPORTS",     "World Cup Qualifiers",    "ESPN",    3U * 3600U },
            { "HEALTH",     "Breakthrough Vaccine",    "WHO",     4U * 3600U },
            { "TECHNOLOGY", "Quantum Computing Leap",  "Wired",   5U * 3600U },
        };
        int na=5, ai2;
        for (ai2=0; ai2<na; ai2++) {
            int ay = feat_y+76+ai2*34;
            if (ay+34 > wy+wh-22) break;
            uint32_t cat_c = (ai2==0)?RGB(52,199,89):(ai2==1)?RGB(255,59,48):(ai2==2)?RGB(0,122,255):(ai2==3)?RGB(255,149,0):RGB(147,44,246);
            gui_draw_rounded_rect(wx+8, ay+2, (int)str_len(arts[ai2].cat)*7+4, 12, 3, cat_c);
            vga_draw_string_trans(wx+10, ay+4, arts[ai2].cat, RGB(255,255,255));
            vga_draw_string_trans(wx+8, ay+16, arts[ai2].hed, nw_txt);
            vga_draw_string_trans(wx+8, ay+26, arts[ai2].src, nw_sub);
            { char agebuf[16];
              runtime_format_relative_time(arts[ai2].age_seconds, agebuf, sizeof(agebuf));
              vga_draw_string_trans(wx+ww-24, ay+26, agebuf, nw_sub); }
            vga_draw_hline(wx+4, ay+32, ww-8, nw_sep);
        }
        return 1;
    }

    /* Maps window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Maps")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int my_top = wy+TITLEBAR_H;
        /* Map background - satellite-like tiles */
        int r2, c2;
        uint32_t map_colors[4][4] = {
            {RGB(90,130,80), RGB(85,125,75), RGB(180,170,140), RGB(170,160,130)},
            {RGB(80,120,70), RGB(75,115,65), RGB(160,150,120), RGB(190,175,145)},
            {RGB(200,190,160),RGB(185,175,150),RGB(75,110,65),RGB(80,120,70)},
            {RGB(170,160,130),RGB(80,120,70), RGB(90,130,80), RGB(190,180,155)},
        };
        int mh = wh - TITLEBAR_H - 18;
        for (r2=0; r2<4; r2++) {
            for (c2=0; c2<4; c2++) {
                vga_fill_rect(wx+1+c2*(ww-2)/4, my_top+r2*mh/4, (ww-2)/4, mh/4, map_colors[r2%4][c2%4]);
            }
        }
        /* Roads */
        vga_draw_line(wx+1, my_top+mh*2/5, wx+ww-1, my_top+mh*2/5, RGB(230,220,180));
        vga_draw_line(wx+1, my_top+mh*2/5+1, wx+ww-1, my_top+mh*2/5+1, RGB(230,220,180));
        vga_draw_line(wx+ww/3, my_top, wx+ww/3, my_top+mh, RGB(230,220,180));
        vga_draw_line(wx+ww/3+1, my_top, wx+ww/3+1, my_top+mh, RGB(230,220,180));
        vga_draw_line(wx+ww*2/3, my_top, wx+ww*2/3, my_top+mh, RGB(200,210,170));
        /* Blue river/water area */
        vga_fill_rect(wx+ww*3/5, my_top+mh/3, ww/5, mh/4, RGB(100,160,220));
        vga_draw_string_trans(wx+ww*3/5+4, my_top+mh/3+6, "Han River", RGB(255,255,255));
        /* Location pin */
        int pin_x = wx+ww/2, pin_y = my_top+mh/2;
        gui_draw_circle(pin_x, pin_y-14, 10, RGB(255,59,48));
        gui_draw_circle(pin_x, pin_y-14, 4, RGB(255,255,255));
        vga_draw_line(pin_x, pin_y-4, pin_x, pin_y+4, RGB(255,59,48));
        vga_draw_string_trans(pin_x-16, pin_y+6, "You are here", RGB(255,255,255));
        /* Search bar */
        vga_fill_rect(wx+8, my_top+4, ww-16, 20, RGB(255,255,255));
        gui_draw_rounded_rect_outline(wx+8, my_top+4, ww-16, 20, 5, RGB(180,180,180));
        vga_draw_string_trans(wx+14, my_top+10, "Search Maps...", RGB(150,150,150));
        /* Compass */
        int cp_x = wx+ww-26, cp_y = my_top+mh-26;
        gui_draw_circle(cp_x, cp_y, 14, RGB(255,255,255));
        gui_draw_circle(cp_x, cp_y, 13, RGB(200,200,205));
        vga_draw_string_trans(cp_x-4, cp_y-10, "N", RGB(255,59,48));
        vga_draw_string_trans(cp_x-4, cp_y+2, "S", RGB(80,80,80));
        /* Scale bar */
        vga_fill_rect(wx+12, my_top+mh-14, 60, 2, RGB(50,50,50));
        vga_draw_string_trans(wx+12, my_top+mh-10, "500m", RGB(40,40,40));
        /* Category buttons */
        static const char *cats[] = { "Directions", "Transit", "Explore" };
        int ci3;
        for (ci3=0; ci3<3; ci3++) {
            int bx = wx+8+ci3*((ww-16)/3);
            gui_draw_rounded_rect(bx, my_top+mh+1, (ww-16)/3-2, 14, 4, RGB(0,122,255));
            vga_draw_string_trans(bx+2, my_top+mh+4, cats[ci3], RGB(255,255,255));
        }
        return 1;
    }

    /* Messages window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Messages")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t ms_bg   = g_pref_darkmode ? RGB(22,22,26)    : RGB(242,242,248);
        uint32_t ms_sb   = g_pref_darkmode ? RGB(30,30,34)    : RGB(228,228,236);
        uint32_t ms_sep  = g_pref_darkmode ? RGB(55,55,60)    : RGB(205,205,210);
        uint32_t ms_txt  = g_pref_darkmode ? RGB(212,212,220) : RGB(28,28,38);
        uint32_t ms_sub  = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
        uint32_t ms_blu  = RGB(0,122,255);
        uint32_t ms_grn  = RGB(52,199,89);
        uint32_t ms_bub  = g_pref_darkmode ? RGB(44,44,52)    : RGB(228,228,234);
        uint32_t ms_hdr  = g_pref_darkmode ? RGB(30,30,36)    : RGB(248,248,252);
        uint32_t ms_sel  = g_pref_darkmode ? RGB(0,72,195)    : RGB(0,99,216);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, ms_bg);
        /* ---- Sidebar ---- */
        int sb_w = 130;
        if (sb_w > ww/2) sb_w = ww/2;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, sb_w, wh-TITLEBAR_H-19, ms_sb);
        vga_draw_vline(wx+sb_w+1, wy+TITLEBAR_H+1, wh-TITLEBAR_H-19, ms_sep);
        /* Search bar */
        { int sx=wx+6, sy=wy+TITLEBAR_H+5, sw=sb_w-10, sh=18;
          vga_fill_rect(sx, sy, sw, sh, g_pref_darkmode?RGB(44,44,52):RGB(212,212,220));
          gui_draw_rounded_rect_outline(sx, sy, sw, sh, 5, ms_sep);
          gui_draw_circle(sx+10, sy+9, 4, ms_sub);
          gui_draw_circle(sx+10, sy+9, 2, g_pref_darkmode?RGB(44,44,52):RGB(212,212,220));
          vga_draw_line(sx+13, sy+12, sx+16, sy+15, ms_sub);
          vga_draw_string_trans(sx+20, sy+5, "Search", ms_sub);
        }
        /* Conversations */
        static const struct {
            const char *name; const char *last;
            uint32_t av; int unread; int imsg;
        } ms_convs[] = {
            { "Alice Kim",  "Sounds great!",    RGB(255,100,100), 3, 1 },
            { "Bob Chen",   "See you later",    RGB(100,130,255), 0, 1 },
            { "Carol Park", "Thanks!",          RGB(255,200,50),  1, 1 },
            { "MyOS Team",  "Meeting soon",     RGB(52,199,89),   2, 0 },
            { "David Lee",  "On my way",        RGB(147,44,246),  0, 1 },
            { "Jenny Wu",   "haha yes!!",       RGB(255,149,0),   0, 1 },
        };
        int ms_nc=6, ms_ci;
        for (ms_ci=0; ms_ci<ms_nc; ms_ci++) {
            int cy4=wy+TITLEBAR_H+27+ms_ci*33;
            char conv_time[12];
            if (cy4+33>wy+wh-20) break;
            apps2_format_message_time(ms_ci, conv_time, sizeof(conv_time));
            if (ms_ci==g_ms_sel) {
                gui_draw_rounded_rect(wx+2, cy4-1, sb_w-2, 33, 6, ms_sel);
            }
            /* Avatar */
            gui_draw_circle(wx+18, cy4+15, 13, ms_convs[ms_ci].av);
            vga_draw_char_trans(wx+14, cy4+11, ms_convs[ms_ci].name[0], RGB(255,255,255));
            /* iMessage green dot */
            if (ms_convs[ms_ci].imsg) {
                gui_draw_circle(wx+27, cy4+4, 4, ms_grn);
                gui_draw_circle(wx+27, cy4+4, 2, g_pref_darkmode?RGB(22,22,26):RGB(228,228,236));
            }
            /* Name + time on one row */
            uint32_t ntc = ms_ci==g_ms_sel ? RGB(255,255,255) : ms_txt;
            uint32_t stc = ms_ci==g_ms_sel ? RGB(200,220,255) : ms_sub;
            vga_draw_string_trans(wx+35, cy4+5,  ms_convs[ms_ci].name, ntc);
            vga_draw_string_trans(wx+sb_w-54, cy4+5, conv_time, stc);
            /* Last message */
            vga_draw_string_trans(wx+35, cy4+18, ms_convs[ms_ci].last, stc);
            /* Unread badge */
            if (ms_convs[ms_ci].unread > 0) {
                gui_draw_circle(wx+sb_w-10, cy4+10, 8, ms_blu);
                vga_draw_char_trans(wx+sb_w-14, cy4+6, '0'+ms_convs[ms_ci].unread, RGB(255,255,255));
            }
            if (ms_ci > 0) vga_draw_hline(wx+35, cy4+32, sb_w-37, ms_sep);
        }
        /* ---- Chat area ---- */
        int chat_x = wx+sb_w+4;
        int chat_w = ww-sb_w-6;
        int max_chat_y = wy+wh-42;
        /* Contact header — shows selected conversation */
        vga_fill_rect(chat_x-2, wy+TITLEBAR_H+1, chat_w+2, 36, ms_hdr);
        vga_draw_hline(chat_x-2, wy+TITLEBAR_H+37, chat_w+2, ms_sep);
        { int sel2 = (g_ms_sel >= 0 && g_ms_sel < ms_nc) ? g_ms_sel : 0;
          gui_draw_circle(chat_x+18, wy+TITLEBAR_H+19, 14, ms_convs[sel2].av);
          vga_draw_char_trans(chat_x+14, wy+TITLEBAR_H+15, ms_convs[sel2].name[0], RGB(255,255,255));
          vga_draw_string_trans(chat_x+37, wy+TITLEBAR_H+8,  ms_convs[sel2].name,  ms_txt);
          vga_draw_string_trans(chat_x+37, wy+TITLEBAR_H+20, ms_convs[sel2].imsg?"iMessage":"SMS", ms_sub);
        }
        /* Call / Video icons */
        { int hbx=wx+ww-52;
          gui_draw_circle(hbx+10, wy+TITLEBAR_H+18, 11, g_pref_darkmode?RGB(48,48,56):RGB(218,218,228));
          vga_draw_string_trans(hbx+5, wy+TITLEBAR_H+14, "ph", ms_blu);
          gui_draw_circle(hbx+34, wy+TITLEBAR_H+18, 11, g_pref_darkmode?RGB(48,48,56):RGB(218,218,228));
          vga_draw_string_trans(hbx+29, wy+TITLEBAR_H+14, "vd", ms_blu);
        }
        /* Current date separator */
        int bub_y = wy+TITLEBAR_H+44;
        { datetime_t sep_now;
          const char *sep_label;
          int sl7;
          int sep_left = chat_x+4, sep_right = chat_x+chat_w-4;
          int sep_mid = chat_x + chat_w/2;
          get_current_datetime(&sep_now);
          sep_label = datetime_weekday_long(sep_now.weekday);
          sl7=(int)str_len(sep_label)*8;
          vga_draw_hline(sep_left, bub_y+4, sep_mid-sep_left-sl7/2-4, ms_sep);
          vga_draw_string_trans(sep_mid-sl7/2, bub_y, sep_label, ms_sub);
          vga_draw_hline(sep_mid+sl7/2+4, bub_y+4, sep_right-(sep_mid+sl7/2+4), ms_sep);
        }
        bub_y += 14;
        /* ---- Message bubbles ---- */
        static const struct {
            const char *msg; int out; int react; int is_img;
        } ms_bubs[] = {
            { "Hey! Are you free today?",   0, 0, 0 },
            { "Yes! What's up?",            1, 1, 0 },
            { "Want to grab coffee?",       0, 0, 0 },
            { "",                           0, 0, 1 },
            { "Haha cute pic!",             1, 0, 0 },
            { "Sure, 2pm at the usual?",    0, 0, 0 },
            { "Sounds great! See u then",   1, 2, 0 },
        };
        int ms_nb=7, ms_bi;
        uint32_t t_ms = timer_ticks();
        for (ms_bi=0; ms_bi<ms_nb; ms_bi++) {
            if (bub_y+20 > max_chat_y) break;
            if (ms_bubs[ms_bi].is_img) {
                /* Photo attachment */
                int iw=chat_w*5/9, ih=48;
                int ix=chat_x+8;
                gui_draw_rounded_rect(ix, bub_y, iw, ih, 8, RGB(30,110,170));
                vga_fill_rect_alpha(ix+2, bub_y+2, iw-4, 12, RGB(255,255,255), 40);
                /* Landscape illustration */
                { int mi2;
                  for (mi2=0;mi2<iw-4;mi2++) {
                      int mhv = 12 + (mi2*7%17);
                      vga_fill_rect(ix+2+mi2, bub_y+ih-2-mhv, 1, mhv, RGB(20,55,95));
                  }
                }
                /* Sun */
                gui_draw_circle(ix+iw-20, bub_y+14, 8, RGB(255,220,80));
                bub_y += ih + 4;
            } else {
                int blen=(int)str_len(ms_bubs[ms_bi].msg);
                int bw=blen*8+18;
                if (bw>chat_w-16) bw=chat_w-16;
                if (bw<24) bw=24;
                int bh=18;
                if (ms_bubs[ms_bi].out) {
                    int bx=wx+ww-bw-8;
                    gui_draw_rounded_rect(bx, bub_y, bw, bh, 8, ms_blu);
                    vga_draw_string_trans(bx+9, bub_y+5, ms_bubs[ms_bi].msg, RGB(255,255,255));
                } else {
                    gui_draw_rounded_rect(chat_x+8, bub_y, bw, bh, 8, ms_bub);
                    vga_draw_string_trans(chat_x+17, bub_y+5, ms_bubs[ms_bi].msg, ms_txt);
                }
                /* Tapback reaction badge */
                if (ms_bubs[ms_bi].react == 1) {
                    int rx2 = ms_bubs[ms_bi].out ? (wx+ww-bw-8+bw-12) : (chat_x+8+bw-12);
                    vga_fill_rect(rx2-2, bub_y+bh-5, 14, 12, g_pref_darkmode?RGB(40,40,48):RGB(248,248,252));
                    gui_draw_rounded_rect_outline(rx2-2, bub_y+bh-5, 14, 12, 4, ms_sep);
                    vga_draw_string_trans(rx2, bub_y+bh-3, "<3", RGB(255,59,48));
                } else if (ms_bubs[ms_bi].react == 2) {
                    int rx2 = ms_bubs[ms_bi].out ? (wx+ww-bw-8+bw-16) : (chat_x+8+bw-16);
                    vga_fill_rect(rx2-2, bub_y+bh-5, 18, 12, g_pref_darkmode?RGB(40,40,48):RGB(248,248,252));
                    gui_draw_rounded_rect_outline(rx2-2, bub_y+bh-5, 18, 12, 4, ms_sep);
                    vga_draw_string_trans(rx2, bub_y+bh-3, "+1!", RGB(255,149,0));
                }
                bub_y += bh + 5 + (ms_bubs[ms_bi].react ? 6 : 0);
            }
        }
        /* Sent messages for this conversation */
        { int si7;
          for (si7=0; si7<g_ms_sent_n && bub_y+20<=max_chat_y; si7++) {
              if (g_ms_sent_conv[si7] != g_ms_sel) continue;
              int blen7=(int)str_len(g_ms_sent[si7]);
              int bw7=blen7*8+18; if (bw7>chat_w-16) bw7=chat_w-16; if (bw7<24) bw7=24;
              int bx7=wx+ww-bw7-8;
              gui_draw_rounded_rect(bx7, bub_y, bw7, 18, 8, ms_blu);
              vga_draw_string_trans(bx7+9, bub_y+5, g_ms_sent[si7], RGB(255,255,255));
              bub_y += 23;
          }
        }
        /* Auto-replies for this conversation */
        { int ri7;
          for (ri7=0; ri7<g_ms_reply_n && bub_y+20<=max_chat_y; ri7++) {
              if (g_ms_reply_conv[ri7] != g_ms_sel) continue;
              uint32_t age7 = timer_ticks() - g_ms_reply_tick[ri7];
              if (age7 < 1200) continue; /* 1.2s delay before reply appears */
              int blen7=(int)str_len(g_ms_reply[ri7]);
              int bw7=blen7*8+18; if (bw7>chat_w-16) bw7=chat_w-16; if (bw7<24) bw7=24;
              gui_draw_rounded_rect(chat_x+8, bub_y, bw7, 18, 8, ms_bub);
              vga_draw_string_trans(chat_x+17, bub_y+5, g_ms_reply[ri7], ms_txt);
              bub_y += 23;
          }
        }
        /* Typing indicator (animated 3 dots) — show while reply is pending */
        { int ri7p; int has_pending=0;
          for (ri7p=0; ri7p<g_ms_reply_n; ri7p++) {
              if (g_ms_reply_conv[ri7p]==g_ms_sel) {
                  uint32_t age7p=timer_ticks()-g_ms_reply_tick[ri7p];
                  if (age7p<1200) { has_pending=1; break; }
              }
          }
          if (has_pending && bub_y+20 <= max_chat_y) {
              gui_draw_rounded_rect(chat_x+8, bub_y, 38, 18, 8, ms_bub);
              int dp=(int)((t_ms/350)%3);
              int di3;
              for(di3=0;di3<3;di3++){
                  uint32_t dc3=(di3==dp)?ms_txt:ms_sub;
                  gui_draw_circle(chat_x+18+di3*9, bub_y+9, 3, dc3);
              }
              bub_y += 22;
          }
        }
        /* Read receipt */
        if (bub_y < max_chat_y) {
            char read_time[12];
            char readbuf[24];
            int rp = 0;
            apps2_format_message_time(0, read_time, sizeof(read_time));
            readbuf[0] = 0;
            apps2_append_text(readbuf, &rp, sizeof(readbuf), "Read ");
            apps2_append_text(readbuf, &rp, sizeof(readbuf), read_time);
            vga_draw_string_trans(wx+ww-88, bub_y-2, readbuf, ms_sub);
        }
        /* ---- Input bar ---- */
        int inp_y = wy+wh-40;
        vga_fill_rect(chat_x-2, inp_y, chat_w+2, 21, g_pref_darkmode?RGB(30,30,36):RGB(242,242,248));
        vga_draw_hline(chat_x-2, inp_y, chat_w+2, ms_sep);
        /* + button */
        gui_draw_circle(chat_x+11, inp_y+11, 9, g_pref_darkmode?RGB(50,50,60):RGB(214,214,224));
        vga_draw_string_trans(chat_x+7, inp_y+7, "+", ms_txt);
        /* Camera button */
        gui_draw_circle(chat_x+31, inp_y+11, 9, g_pref_darkmode?RGB(50,50,60):RGB(214,214,224));
        vga_draw_string_trans(chat_x+27, inp_y+7, "O", ms_sub);
        /* Input field */
        int tf_x=chat_x+44, tf_w=chat_w-66;
        vga_fill_rect(tf_x, inp_y+3, tf_w, 15,
            g_ms_focused?(g_pref_darkmode?RGB(50,50,62):RGB(255,255,255))
                        :(g_pref_darkmode?RGB(44,44,54):RGB(242,242,248)));
        gui_draw_rounded_rect_outline(tf_x, inp_y+3, tf_w, 15, 5,
            g_ms_focused?ms_blu:ms_sep);
        if (g_ms_input_len > 0) {
            vga_draw_string_trans(tf_x+6, inp_y+6, g_ms_input, ms_txt);
            /* Blinking cursor */
            if ((t_ms/500)%2==0) {
                int cx7 = tf_x+6+g_ms_input_len*8;
                vga_fill_rect(cx7, inp_y+5, 1, 10, ms_txt);
            }
        } else {
            vga_draw_string_trans(tf_x+6, inp_y+6, "iMessage", ms_sub);
        }
        /* Send button — green when text typed, gray otherwise */
        uint32_t send_col = g_ms_input_len>0 ? ms_grn : (g_pref_darkmode?RGB(60,60,70):RGB(190,190,200));
        gui_draw_circle(tf_x+tf_w+12, inp_y+11, 9, send_col);
        vga_draw_string_trans(tf_x+tf_w+7, inp_y+7, "^", RGB(255,255,255));
        return 1;
    }

    /* Books window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Books")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t bk_bg  = g_pref_darkmode ? RGB(20,18,16)    : RGB(250,245,235);
        uint32_t bk_hd  = g_pref_darkmode ? RGB(34,30,26)    : RGB(235,228,215);
        uint32_t bk_sep = g_pref_darkmode ? RGB(60,55,50)    : RGB(210,200,185);
        uint32_t bk_txt = g_pref_darkmode ? RGB(215,210,200) : RGB(30,24,16);
        uint32_t bk_sub = g_pref_darkmode ? RGB(130,120,110) : RGB(110,100,85);
        uint32_t bk_acc = RGB(255,149,0);   /* Orange accent */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, bk_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, bk_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, bk_sep);
        { int tl=str_len("Books")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+8, "Books", bk_acc); }
        /* Tab bar */
        int tab_y = wy+TITLEBAR_H+26;
        static const char *tabs[] = { "Reading Now", "Library", "Store" };
        int ti;
        for (ti=0; ti<3; ti++) {
            int tx = wx+4+ti*(ww-8)/3;
            if (ti==0) {
                gui_draw_rounded_rect(tx, tab_y+2, (ww-8)/3-2, 16, 4, bk_acc);
                vga_draw_string_trans(tx+4, tab_y+6, tabs[ti], RGB(255,255,255));
            } else {
                vga_draw_string_trans(tx+4, tab_y+6, tabs[ti], bk_sub);
            }
        }
        vga_draw_hline(wx+1, tab_y+20, ww-2, bk_sep);
        /* "Continue Reading" book */
        int cont_y = tab_y+24;
        vga_draw_string_trans(wx+8, cont_y, "CONTINUE READING", bk_sub);
        /* Book cover */
        vga_fill_rect(wx+8, cont_y+12, 44, 60, RGB(60,40,100));
        vga_fill_rect(wx+10, cont_y+14, 40, 56, RGB(80,55,130));
        vga_draw_string_trans(wx+14, cont_y+22, "The", RGB(255,220,150));
        vga_draw_string_trans(wx+12, cont_y+34, "Pragmatic", RGB(255,220,150));
        vga_draw_string_trans(wx+12, cont_y+46, "Programmer", RGB(255,220,150));
        /* Book info */
        vga_draw_string_trans(wx+58, cont_y+12, "The Pragmatic", bk_txt);
        vga_draw_string_trans(wx+58, cont_y+24, "Programmer", bk_txt);
        vga_draw_string_trans(wx+58, cont_y+36, "David Thomas", bk_sub);
        /* Progress bar */
        { int book_pct = 40 + (int)((timer_ticks() / 10000U) % 55U);
          char pctbuf[24];
          int ppos;
        vga_fill_rect(wx+58, cont_y+50, ww-68, 6, bk_sep);
        vga_fill_rect(wx+58, cont_y+50, (ww-68)*book_pct/100, 6, bk_acc);
        runtime_format_percent(book_pct, pctbuf, sizeof(pctbuf));
        ppos = (int)str_len(pctbuf);
        apps2_append_text(pctbuf, &ppos, sizeof(pctbuf), " complete");
        vga_draw_string_trans(wx+58, cont_y+58, pctbuf, bk_sub); }
        vga_draw_hline(wx+4, cont_y+74, ww-8, bk_sep);
        /* Library books grid */
        vga_draw_string_trans(wx+8, cont_y+78, "MY LIBRARY", bk_sub);
        static const uint32_t cover_cols[] = { RGB(180,60,60), RGB(60,100,180), RGB(50,140,60), RGB(140,80,180), RGB(180,120,40), RGB(40,140,160) };
        static const char *bk_titles[] = { "Clean Code", "SICP", "Linux", "Design", "Go Lang", "Rust" };
        int bi2;
        for (bi2=0; bi2<6; bi2++) {
            int bx = wx+8 + (bi2%3)*((ww-16)/3);
            int by = cont_y+90 + (bi2/3)*70;
            if (by+70 > wy+wh-22) break;
            vga_fill_rect(bx, by, (ww-16)/3-4, 56, cover_cols[bi2]);
            vga_draw_string_trans(bx+2, by+22, bk_titles[bi2], RGB(255,255,255));
        }
        return 1;
    }

    /* Podcasts window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Podcasts")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t pc_bg  = g_pref_darkmode ? RGB(22,22,26)    : RGB(242,242,248);
        uint32_t pc_hd  = g_pref_darkmode ? RGB(38,38,42)    : RGB(228,228,234);
        uint32_t pc_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(205,205,210);
        uint32_t pc_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t pc_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
        uint32_t pc_acc = RGB(147,44,246); /* purple accent */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, pc_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 26, pc_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+27, ww-2, pc_sep);
        { int tl=str_len("Podcasts")*8;
          vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+9, "Podcasts", pc_acc); }
        /* Featured episode */
        int ep_y = wy+TITLEBAR_H+32;
        vga_fill_rect(wx+8, ep_y, ww-16, 60, pc_acc);
        vga_fill_rect_alpha(wx+8, ep_y, ww-16, 60, RGB(255,255,255), 15);
        /* Album art frame */
        vga_fill_rect(wx+12, ep_y+4, 50, 50, RGB(100,50,180));
        gui_draw_rounded_rect_outline(wx+12, ep_y+4, 50, 50, 6, RGB(255,255,255));
        vga_draw_string_trans(wx+22, ep_y+20, "POD", RGB(255,255,255));
        vga_draw_string_trans(wx+22, ep_y+32, "CAST", RGB(220,200,255));
        vga_draw_string_trans(wx+68, ep_y+6,  "Tech News Daily", RGB(255,255,255));
        vga_draw_string_trans(wx+68, ep_y+18, "Episode 247", RGB(220,200,255));
        vga_draw_string_trans(wx+68, ep_y+30, "AI in 2026: What's next?", RGB(200,180,240));
        vga_draw_string_trans(wx+68, ep_y+42, "43 min", pc_sub);
        /* Progress bar */
        uint32_t t_pc = timer_ticks();
        int pc_dur = 2580; /* 43 min in seconds */
        int pc_pos = (int)((t_pc/1000) % (uint32_t)pc_dur);
        int pb_w = ww-32, pb_x = wx+12;
        vga_fill_rect(pb_x, ep_y+56, pb_w, 3, RGB(80,40,120));
        vga_fill_rect(pb_x, ep_y+56, pb_w*pc_pos/pc_dur, 3, RGB(255,255,255));
        /* Play controls */
        int ctl_y = ep_y+64;
        /* Prev */
        vga_draw_string_trans(wx+ww/2-48, ctl_y+2, "<<", pc_sub);
        /* Play/Pause */
        gui_draw_circle(wx+ww/2, ctl_y+8, 12, pc_acc);
        vga_draw_string_trans(wx+ww/2-4, ctl_y+4, ">", RGB(255,255,255));
        /* Next */
        vga_draw_string_trans(wx+ww/2+36, ctl_y+2, ">>", pc_sub);
        /* Playback speed */
        vga_draw_string_trans(wx+ww/2-8, ctl_y+22, "1x", pc_sub);
        /* Episode list */
        vga_draw_hline(wx+4, ctl_y+32, ww-8, pc_sep);
        vga_draw_string_trans(wx+8, ctl_y+36, "MORE EPISODES", pc_sub);
        static const char *eps[] = {
            "Ep246: Quantum Computing",
            "Ep245: Space Tech Update",
            "Ep244: Neural Interfaces",
        };
        int ei;
        for (ei=0; ei<3; ei++) {
            int ey = ctl_y+50+ei*28;
            if (ey+28 > wy+wh-22) break;
            vga_fill_rect(wx+8, ey, 24, 24, RGB(80,40,160));
            vga_draw_char_trans(wx+14, ey+8, '0'+4+ei, RGB(255,255,255));
            vga_draw_string_trans(wx+36, ey+4, eps[ei], pc_txt);
            vga_draw_string_trans(wx+36, ey+14, "38 min", pc_sub);
            vga_draw_hline(wx+4, ey+26, ww-8, pc_sep);
        }
        return 1;
    }

    /* Freeform window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Freeform")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t ff_bg  = g_pref_darkmode ? RGB(22,20,28)    : RGB(252,250,245);
        uint32_t ff_hd  = g_pref_darkmode ? RGB(34,30,42)    : RGB(235,230,222);
        uint32_t ff_sep = g_pref_darkmode ? RGB(55,50,65)    : RGB(210,200,188);
        uint32_t ff_txt = g_pref_darkmode ? RGB(210,205,220) : RGB(28,24,18);
        uint32_t ff_sub = g_pref_darkmode ? RGB(120,115,132) : RGB(110,100,85);
        uint32_t ff_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, ff_bg);
        /* Header toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 26, ff_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+27, ww-2, ff_sep);
        { int tl=str_len("Freeform")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+9, "Freeform", ff_txt); }
        /* Tool buttons */
        static const char *tools[] = { "Pen", "Text", "Shape", "Note", "Media" };
        int ti6;
        for (ti6=0; ti6<5; ti6++) {
            int tx=wx+6+ti6*(ww-12)/5;
            if(ti6==0) { gui_draw_rounded_rect(tx, wy+TITLEBAR_H+30, (ww-12)/5-2, 16, 4, ff_acc); vga_draw_string_trans(tx+2, wy+TITLEBAR_H+34, tools[ti6], RGB(255,255,255)); }
            else { gui_draw_rounded_rect_outline(tx, wy+TITLEBAR_H+30, (ww-12)/5-2, 16, 4, ff_sep); vga_draw_string_trans(tx+2, wy+TITLEBAR_H+34, tools[ti6], ff_sub); }
        }
        vga_draw_hline(wx+1, wy+TITLEBAR_H+48, ww-2, ff_sep);
        /* Canvas area with some drawn elements */
        int cv_y = wy+TITLEBAR_H+50;
        /* Sticky notes */
        vga_fill_rect(wx+20, cv_y+10, 90, 60, RGB(255,230,100));
        vga_draw_string_trans(wx+24, cv_y+14, "Ideas", RGB(80,60,0));
        vga_draw_string_trans(wx+24, cv_y+26, "- Feature A", RGB(80,60,0));
        vga_draw_string_trans(wx+24, cv_y+38, "- Feature B", RGB(80,60,0));
        vga_draw_string_trans(wx+24, cv_y+50, "- MVP goals", RGB(80,60,0));
        /* Another sticky */
        vga_fill_rect(wx+130, cv_y+30, 80, 50, RGB(180,230,255));
        vga_draw_string_trans(wx+134, cv_y+34, "Tech Stack", RGB(0,50,100));
        vga_draw_string_trans(wx+134, cv_y+46, "Bare Metal", RGB(0,50,100));
        { runtime_system_info_t sys;
          char osbuf[32];
          int opos = 0;
          runtime_get_system_info(&sys);
          osbuf[0] = 0;
          apps2_append_text(osbuf, &opos, sizeof(osbuf), sys.sysname);
          apps2_append_text(osbuf, &opos, sizeof(osbuf), " ");
          apps2_append_text(osbuf, &opos, sizeof(osbuf), sys.release);
          vga_draw_string_trans(wx+134, cv_y+58, osbuf, RGB(0,50,100)); }
        /* Shape: rectangle */
        vga_draw_rect_outline(wx+ww/2-40, cv_y+90, 80, 40, RGB(52,199,89));
        vga_draw_string_trans(wx+ww/2-28, cv_y+105, "Database", RGB(52,199,89));
        /* Arrow from note to shape */
        vga_draw_line(wx+115, cv_y+50, wx+ww/2-40, cv_y+105, ff_sub);
        /* Hand-drawn line sketch */
        { int li2;
          for(li2=0; li2<20; li2++) {
              int x1=wx+20+li2*8, y1=cv_y+140+(li2%3)*3;
              vga_fill_rect(x1, y1, 4, 2, RGB(50,50,200));
          }
        }
        vga_draw_string_trans(wx+20, cv_y+148, "User Flow:", ff_txt);
        /* Circle nodes */
        gui_draw_circle(wx+100, cv_y+170, 12, RGB(255,149,0));
        gui_draw_circle(wx+150, cv_y+170, 12, RGB(52,199,89));
        gui_draw_circle(wx+200, cv_y+170, 12, RGB(0,122,255));
        vga_draw_line(wx+112, cv_y+170, wx+138, cv_y+170, ff_txt);
        vga_draw_line(wx+162, cv_y+170, wx+188, cv_y+170, ff_txt);
        vga_draw_string_trans(wx+93, cv_y+185, "Start", ff_sub);
        vga_draw_string_trans(wx+143, cv_y+185, "Auth", ff_sub);
        vga_draw_string_trans(wx+193, cv_y+185, "App", ff_sub);
        /* Text annotation */
        if (wh > 300) {
            vga_draw_string_trans(wx+20, cv_y+wh-TITLEBAR_H-80, "Canvas - Click to add content", ff_sub);
        }
        return 1;
    }

    /* Disk Utility window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Disk Utility")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t du_bg  = g_pref_darkmode ? RGB(20,20,24)    : RGB(245,245,250);
        uint32_t du_hd  = g_pref_darkmode ? RGB(32,32,36)    : RGB(228,228,234);
        uint32_t du_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(200,200,205);
        uint32_t du_txt = g_pref_darkmode ? RGB(215,215,220) : RGB(28,28,38);
        uint32_t du_sub = g_pref_darkmode ? RGB(115,115,124) : RGB(100,100,110);
        runtime_storage_info_t storage;
        int has_storage = (runtime_get_storage_info("/", &storage) == 0);
        char totalbuf[16];
        if (has_storage) runtime_format_bytes(storage.total_bytes, totalbuf, sizeof(totalbuf));
        else totalbuf[0] = 0;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, du_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 22, du_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+23, ww-2, du_sep);
        { int tl=str_len("Disk Utility")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+7, "Disk Utility", du_txt); }
        /* Drive list sidebar */
        int dsb_w = 100;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+24, dsb_w, wh-TITLEBAR_H-42, g_pref_darkmode?RGB(28,28,32):RGB(238,238,242));
        vga_draw_vline(wx+dsb_w, wy+TITLEBAR_H+24, wh-TITLEBAR_H-42, du_sep);
        vga_draw_string_trans(wx+6, wy+TITLEBAR_H+28, "INTERNAL", du_sub);
        /* Disk icon + name */
        vga_fill_rect(wx+6, wy+TITLEBAR_H+38, 20, 16, RGB(100,100,200));
        vga_draw_rect_outline(wx+6, wy+TITLEBAR_H+38, 20, 16, du_sep);
        vga_fill_rect(wx+8, wy+TITLEBAR_H+41, 16, 4, RGB(150,150,220));
        vga_draw_string_trans(wx+30, wy+TITLEBAR_H+38, "MyOS Disk", du_txt);
        vga_draw_string_trans(wx+30, wy+TITLEBAR_H+48, has_storage ? totalbuf : "unavailable", du_sub);
        vga_fill_rect(wx+6, wy+TITLEBAR_H+60, 16, 12, RGB(52,199,89));
        vga_draw_rect_outline(wx+6, wy+TITLEBAR_H+60, 16, 12, du_sep);
        vga_draw_string_trans(wx+26, wy+TITLEBAR_H+60, "EFI", du_sub);
        vga_draw_string_trans(wx+26, wy+TITLEBAR_H+70, "boot volume", du_sub);
        /* Main content: disk info */
        int ci6 = wx+dsb_w+8;
        int cy6 = wy+TITLEBAR_H+28;
        vga_draw_string_trans(ci6, cy6, "MyOS Disk", du_txt);
        if (has_storage) {
            char capbuf[36];
            int cp = 0;
            capbuf[0] = 0;
            apps2_append_bytes(capbuf, &cp, sizeof(capbuf), storage.total_bytes);
            apps2_append_text(capbuf, &cp, sizeof(capbuf), " total capacity");
            vga_draw_string_trans(ci6, cy6+14, capbuf, du_sub);
        } else {
            vga_draw_string_trans(ci6, cy6+14, "capacity unavailable", du_sub);
        }
        /* Capacity bar */
        vga_draw_string_trans(ci6, cy6+32, "Capacity:", du_sub);
        int bar_w3=ww-dsb_w-20;
        vga_fill_rect(ci6, cy6+44, bar_w3, 16, du_sep);
        if (has_storage) {
            char ufbuf[40];
            int used_w = bar_w3 * storage.used_percent / 100;
            vga_fill_rect(ci6, cy6+44, used_w, 16, RGB(0,122,255));
            apps2_format_used_free(&storage, ufbuf, sizeof(ufbuf));
            vga_draw_string_trans(ci6, cy6+64, ufbuf, du_sub);
        }
        /* Color legend */
        vga_fill_rect(ci6, cy6+78, 10, 10, RGB(0,122,255));
        vga_draw_string_trans(ci6+14, cy6+78, "Apps & Data", du_sub);
        vga_fill_rect(ci6+100, cy6+78, 10, 10, RGB(255,149,0));
        vga_draw_string_trans(ci6+114, cy6+78, "Other", du_sub);
        /* Action buttons */
        static const char *actions[] = { "First Aid", "Partition", "Erase", "Mount" };
        int ai3;
        for (ai3=0; ai3<4; ai3++) {
            int ax=ci6+ai3*((ww-dsb_w-16)/4);
            gui_draw_rounded_rect(ax, cy6+96, (ww-dsb_w-16)/4-2, 20, 4, g_pref_darkmode?RGB(50,50,55):RGB(215,215,220));
            vga_draw_rect_outline(ax, cy6+96, (ww-dsb_w-16)/4-2, 20, du_sep);
            vga_draw_string_trans(ax+2, cy6+102, actions[ai3], du_txt);
        }
        /* S.M.A.R.T. status */
        vga_draw_string_trans(ci6, cy6+124, "S.M.A.R.T. Status: Verified", du_sub);
        gui_draw_circle(ci6+ww-dsb_w-24, cy6+129, 6, RGB(52,199,89));
        return 1;
    }

    /* Shortcuts window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Shortcuts")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t sc_bg  = g_pref_darkmode ? RGB(20,18,24)    : RGB(248,245,252);
        uint32_t sc_hd  = g_pref_darkmode ? RGB(32,28,40)    : RGB(232,226,242);
        uint32_t sc_sep = g_pref_darkmode ? RGB(55,50,65)    : RGB(205,198,218);
        uint32_t sc_sub = g_pref_darkmode ? RGB(125,118,138) : RGB(108,100,120);
        uint32_t sc_acc = RGB(255,149,0);   /* Orange accent */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, sc_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, sc_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, sc_sep);
        { int tl=str_len("Shortcuts")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+8, "Shortcuts", sc_acc); }
        /* Categories */
        static const char *scats[] = { "My Shortcuts", "Gallery", "Automation" };
        int si2;
        for (si2=0; si2<3; si2++) {
            int sx=wx+4+si2*(ww-8)/3;
            if(si2==0) { gui_draw_rounded_rect(sx, wy+TITLEBAR_H+27, (ww-8)/3-2, 14, 4, sc_acc); vga_draw_string_trans(sx+2, wy+TITLEBAR_H+30, scats[si2], RGB(255,255,255)); }
            else { vga_draw_string_trans(sx+2, wy+TITLEBAR_H+30, scats[si2], sc_sub); }
        }
        vga_draw_hline(wx+1, wy+TITLEBAR_H+43, ww-2, sc_sep);
        /* Shortcut cards */
        static const struct { const char *name; const char *desc; uint32_t col; } scuts[] = {
            { "Send Location",  "Share GPS position",     RGB(52,199,89)   },
            { "Daily Summary",  "Morning briefing",       RGB(0,122,255)   },
            { "Focus Mode",     "Toggle DND + wallpaper", RGB(147,44,246)  },
            { "Quick Note",     "Create instant note",    RGB(255,204,0)   },
            { "Resize Images",  "Batch image converter",  RGB(255,149,0)   },
            { "Combine PDFs",   "Merge PDF files",        RGB(255,59,48)   },
        };
        int nsc=6, sci2;
        int card_w=(ww-18)/2, card_h=50;
        for (sci2=0; sci2<nsc; sci2++) {
            int cx2=wx+6+(sci2%2)*(card_w+4);
            int cy2=wy+TITLEBAR_H+48+(sci2/2)*(card_h+4);
            if (cy2+card_h > wy+wh-22) break;
            gui_draw_rounded_rect(cx2, cy2, card_w, card_h, 8, scuts[sci2].col);
            vga_fill_rect_alpha(cx2+2, cy2+2, card_w-4, card_h/3, RGB(255,255,255), 25);
            vga_fill_rect(cx2+6, cy2+8, 18, 18, RGB(255,255,255));
            gui_draw_rounded_rect_outline(cx2+6, cy2+8, 18, 18, 4, scuts[sci2].col);
            vga_draw_char_trans(cx2+10, cy2+11, scuts[sci2].name[0], scuts[sci2].col);
            vga_draw_string_trans(cx2+6, cy2+30, scuts[sci2].name, RGB(255,255,255));
            vga_draw_string_trans(cx2+6, cy2+40, scuts[sci2].desc, RGB(220,220,230));
        }
        return 1;
    }

    /* Voice Memos window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Voice Memos")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t vm_bg  = g_pref_darkmode ? RGB(18,16,22)    : RGB(248,245,252);
        uint32_t vm_hd  = g_pref_darkmode ? RGB(30,26,38)    : RGB(232,226,242);
        uint32_t vm_sep = g_pref_darkmode ? RGB(55,50,65)    : RGB(205,198,218);
        uint32_t vm_txt = g_pref_darkmode ? RGB(215,210,225) : RGB(28,22,40);
        uint32_t vm_sub = g_pref_darkmode ? RGB(125,118,138) : RGB(108,100,120);
        uint32_t vm_acc = RGB(255,59,48);   /* Red for recording */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, vm_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 22, vm_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+23, ww-2, vm_sep);
        { int tl=str_len("Voice Memos")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+7, "Voice Memos", vm_txt); }
        /* Waveform visualization */
        uint32_t t_vm = timer_ticks();
        int wave_y = wy+TITLEBAR_H+36;
        static const int8_t waveform[32] = {
            4,8,12,20,28,36,32,24,14,8,6,10,18,30,40,38,28,18,10,6,4,8,14,22,32,38,34,24,16,10,6,4
        };
        int wi;
        for (wi=0; wi<32; wi++) {
            int bar_h = waveform[wi] + (int)((t_vm/100 + wi*5) % 16) - 8;
            if (bar_h < 2) bar_h = 2;
            int bx = wx+8+wi*(ww-16)/32;
            int bw2 = (ww-16)/32 - 1;
            if (bw2 < 2) bw2 = 2;
            uint32_t bar_col = (wi >= 12 && wi <= 22) ? vm_acc : RGB(100,80,140);
            vga_fill_rect(bx, wave_y+40-bar_h, bw2, bar_h*2, bar_col);
        }
        /* Record button */
        int rec_y = wave_y+86;
        gui_draw_circle(wx+ww/2, rec_y+16, 18, RGB(255,59,48));
        gui_draw_circle(wx+ww/2, rec_y+16, 12, RGB(200,0,0));
        /* Mic icon */
        vga_fill_rect(wx+ww/2-3, rec_y+8, 6, 12, RGB(255,255,255));
        gui_draw_circle(wx+ww/2, rec_y+8, 3, RGB(255,255,255));
        /* Duration */
        uint32_t dur_s = (t_vm/1000)%60;
        char dur_str[8];
        dur_str[0]='0'; dur_str[1]=':';
        dur_str[2]='0'+dur_s/10; dur_str[3]='0'+dur_s%10;
        dur_str[4]=0;
        { int tl=str_len(dur_str)*8; vga_draw_string_trans(wx+(ww-tl)/2, rec_y+38, dur_str, vm_sub); }
        /* Memo list */
        vga_draw_hline(wx+4, rec_y+50, ww-8, vm_sep);
        vga_draw_string_trans(wx+8, rec_y+54, "ALL RECORDINGS", vm_sub);
        static const struct { const char *name; const char *len; } memos[] = {
            { "Meeting Notes",   "2:34" },
            { "Grocery List",    "0:45" },
            { "Ideas for App",   "5:12" },
            { "Brainstorm",      "3:20" },
        };
        int nm2=4, mi2;
        for (mi2=0; mi2<nm2; mi2++) {
            int my3=rec_y+68+mi2*24;
            if(my3+24>wy+wh-22) break;
            gui_draw_circle(wx+16, my3+12, 8, RGB(255,59,48));
            vga_draw_string_trans(wx+30, my3+6, memos[mi2].name, vm_txt);
            vga_draw_string_trans(wx+ww-32, my3+6, memos[mi2].len, vm_sub);
            vga_draw_hline(wx+4, my3+22, ww-8, vm_sep);
        }
        return 1;
    }

    /* Find My window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Find My")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        /* Map background */
        int fm_h = wh - TITLEBAR_H - 18;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, fm_h, RGB(80,110,70));
        /* Road grid */
        { int r3;
          for (r3=0; r3<5; r3++) {
              vga_draw_hline(wx+1, wy+TITLEBAR_H+1+r3*fm_h/5, ww-2, RGB(210,205,165));
              vga_draw_vline(wx+1+r3*(ww-2)/5, wy+TITLEBAR_H+1, fm_h, RGB(210,205,165));
          }
        }
        /* Water feature */
        vga_fill_rect(wx+ww*2/5, wy+TITLEBAR_H+fm_h/4, ww/4, fm_h/3, RGB(90,150,210));
        vga_draw_string_trans(wx+ww*2/5+4, wy+TITLEBAR_H+fm_h/4+10, "Han River", RGB(200,230,255));
        /* Devices */
        static const struct { const char *name; int rx; int ry; uint32_t col; } fdevs[] = {
            { "iPhone",   50, 40, RGB(0,122,255) },
            { "MacBook",  60, 60, RGB(52,199,89) },
            { "AirPods", 70, 30, RGB(255,149,0)  },
        };
        int fd;
        for (fd=0; fd<3; fd++) {
            int fx3=(wx+1+fdevs[fd].rx*(ww-2)/100), fy3=(wy+TITLEBAR_H+1+fdevs[fd].ry*fm_h/100);
            /* Pulse ring */
            uint32_t t_fm=timer_ticks();
            int phase=(int)(t_fm/300)%3;
            if(fd==phase) gui_draw_circle(fx3, fy3, 14, fdevs[fd].col);
            gui_draw_circle(fx3, fy3, 8, fdevs[fd].col);
            gui_draw_circle(fx3, fy3, 4, RGB(255,255,255));
            vga_draw_string_trans(fx3+10, fy3-4, fdevs[fd].name, RGB(255,255,255));
        }
        /* Search bar */
        vga_fill_rect(wx+4, wy+TITLEBAR_H+3, ww-8, 20, RGB(255,255,255));
        gui_draw_rounded_rect_outline(wx+4, wy+TITLEBAR_H+3, ww-8, 20, 5, RGB(180,180,180));
        vga_draw_string_trans(wx+10, wy+TITLEBAR_H+9, "Search people and devices...", RGB(150,150,150));
        /* Bottom panel */
        int bp_y = wy+TITLEBAR_H+fm_h-44;
        vga_fill_rect_alpha(wx+1, bp_y, ww-2, 44, RGB(20,20,24), 200);
        vga_draw_string_trans(wx+8, bp_y+4, "DEVICES NEARBY", RGB(150,150,160));
        int fd2;
        for (fd2=0; fd2<3; fd2++) {
            int dx3=wx+8+fd2*((ww-16)/3);
            gui_draw_circle(dx3+8, bp_y+28, 7, fdevs[fd2].col);
            vga_draw_string_trans(dx3+18, bp_y+24, fdevs[fd2].name, RGB(200,200,210));
        }
        return 1;
    }

    /* Wallet window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Wallet")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t wl_bg = g_pref_darkmode ? RGB(18,18,22) : RGB(245,245,252);
        uint32_t wl_txt = g_pref_darkmode ? RGB(210,210,220) : RGB(28,28,40);
        uint32_t wl_sub = g_pref_darkmode ? RGB(120,120,130) : RGB(100,100,115);
        uint32_t wl_sep = g_pref_darkmode ? RGB(50,50,58) : RGB(205,200,215);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, wl_bg);
        /* Header */
        { int tl=str_len("Wallet")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+8, "Wallet", wl_txt); }
        vga_draw_hline(wx+1, wy+TITLEBAR_H+22, ww-2, wl_sep);
        /* Cards - stacked appearance */
        static const struct { const char *bank; const char *num; const char *exp; uint32_t c1; uint32_t c2; } cards[] = {
            { "MyOS Bank",    "**** 4521", "12/28", RGB(30,120,255),  RGB(0,80,200) },
            { "Visa Gold",    "**** 8834", "06/27", RGB(200,160,20),  RGB(150,110,10) },
            { "Transit Card", "Balance",   "$24.50", RGB(52,199,89), RGB(30,140,60) },
        };
        int nc2=3, ci5;
        for (ci5=nc2-1; ci5>=0; ci5--) {
            int cy5 = wy+TITLEBAR_H+28 + ci5*12;
            int cw5=ww-20, ch5=60;
            /* Card with gradient-like two-tone */
            vga_fill_rect(wx+8, cy5, cw5, ch5, cards[ci5].c1);
            vga_fill_rect(wx+8+cw5/2, cy5, cw5/2, ch5, cards[ci5].c2);
            gui_draw_rounded_rect_outline(wx+8, cy5, cw5, ch5, 8, RGB(255,255,255));
            vga_draw_string_trans(wx+16, cy5+8, cards[ci5].bank, RGB(255,255,255));
            /* Chip */
            vga_fill_rect(wx+16, cy5+22, 20, 14, RGB(200,170,60));
            vga_draw_rect_outline(wx+16, cy5+22, 20, 14, RGB(180,150,40));
            vga_draw_string_trans(wx+cw5-2-str_len(cards[ci5].num)*8, cy5+22, cards[ci5].num, RGB(200,220,255));
            vga_draw_string_trans(wx+cw5-2-str_len(cards[ci5].exp)*8, cy5+40, cards[ci5].exp, RGB(200,220,255));
        }
        /* Apple Pay button */
        int btn_y = wy+TITLEBAR_H+28+nc2*12+70;
        gui_draw_rounded_rect(wx+(ww-120)/2, btn_y, 120, 28, 8, RGB(0,0,0));
        vga_draw_string_trans(wx+(ww-80)/2, btn_y+10, "Pay with Face ID", RGB(255,255,255));
        /* Recent transactions */
        vga_draw_hline(wx+4, btn_y+34, ww-8, wl_sep);
        vga_draw_string_trans(wx+8, btn_y+38, "RECENT TRANSACTIONS", wl_sub);
        static const char *txns[] = { "Coffee Shop -$4.50", "Grocery Store -$38.20", "Bus Fare -$1.50" };
        int ti5;
        for (ti5=0; ti5<3; ti5++) {
            int ty=btn_y+52+ti5*18;
            if(ty+18>wy+wh-22) break;
            vga_draw_string_trans(wx+8, ty, txns[ti5], wl_txt);
            vga_draw_hline(wx+4, ty+16, ww-8, wl_sep);
        }
        return 1;
    }

    /* Home app window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Home")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t hm_bg  = g_pref_darkmode ? RGB(18,18,22)    : RGB(245,245,252);
        uint32_t hm_hd  = g_pref_darkmode ? RGB(32,30,38)    : RGB(228,225,240);
        uint32_t hm_sep = g_pref_darkmode ? RGB(55,52,65)    : RGB(205,200,215);
        uint32_t hm_txt = g_pref_darkmode ? RGB(210,208,220) : RGB(28,25,40);
        uint32_t hm_sub = g_pref_darkmode ? RGB(120,115,130) : RGB(100,95,115);
        uint32_t hm_acc = RGB(255,149,0);   /* Orange Home accent */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, hm_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, hm_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, hm_sep);
        { int tl=str_len("Home")*8; vga_draw_string_trans(wx+(ww-tl)/2, wy+TITLEBAR_H+8, "Home", hm_acc); }
        /* Room selector */
        int rm_y = wy+TITLEBAR_H+28;
        static const char *rooms[] = { "Living Room", "Bedroom", "Kitchen", "Office" };
        int ri;
        vga_draw_hline(wx+4, rm_y+18, ww-8, hm_sep);
        /* Device grid */
        vga_draw_string_trans(wx+8, rm_y+22, "ACCESSORIES", hm_sub);
        uint32_t t_hm = timer_ticks();
        /* Room tab highlighting */
        for (ri=0; ri<4; ri++) {
            int rx3 = wx+4+ri*(ww-8)/4;
            if (ri==g_home_room) { gui_draw_rounded_rect(rx3, rm_y, (ww-8)/4-2, 16, 4, hm_acc); vga_draw_string_trans(rx3+2, rm_y+4, rooms[ri], RGB(255,255,255)); }
            else { gui_draw_rounded_rect_outline(rx3, rm_y, (ww-8)/4-2, 16, 4, hm_sep); vga_draw_string_trans(rx3+2, rm_y+4, rooms[ri], hm_sub); }
        }
        static const struct { const char *name; const char *icon; uint32_t col; } devs[] = {
            { "Living Light", "Lamp",  RGB(255,200,50) },
            { "Smart TV",     "TV",    RGB(60,100,200) },
            { "Thermostat",   "Temp",  RGB(255,100,50) },
            { "Speaker",      "Music", RGB(147,44,246) },
            { "Curtains",     "Blind", RGB(100,120,100)},
            { "Door Lock",    "Lock",  RGB(255,59,48)  },
        };
        int nd=6, di;
        int cell_w=(ww-16)/3, cell_h=58;
        for (di=0; di<nd; di++) {
            int dx=wx+8+(di%3)*cell_w, dy=rm_y+36+(di/3)*cell_h;
            if (dy+cell_h > wy+wh-22) break;
            int dev_on = g_home_dev_on[di];
            uint32_t tile_bg = dev_on ?
                (g_pref_darkmode ? RGB(40,38,50) : RGB(228,224,245)) :
                (g_pref_darkmode ? RGB(28,28,32) : RGB(238,238,242));
            gui_draw_rounded_rect(dx, dy, cell_w-4, cell_h-4, 8, tile_bg);
            if (dev_on) {
                uint32_t glow = devs[di].col;
                /* Animated pulse for on-devices */
                int pulse = (int)(t_hm / 500) % 2;
                if (pulse) vga_fill_rect_alpha(dx+1, dy+1, cell_w-6, cell_h-6, glow, 20);
                gui_draw_circle(dx+(cell_w-4)/2, dy+18, 10, glow);
                /* Thermostat: show temp */
                if (di==2) { char tb[8]; int_to_str(g_home_temp, tb); tb[str_len(tb)]=0; vga_draw_string_trans(dx+2, dy+30, tb, hm_txt); }
                else vga_draw_string_trans(dx+2, dy+30, devs[di].icon, hm_txt);
                /* Light: show brightness bar */
                if (di==0) { int bw=(cell_w-10)*g_home_brightness/100; vga_fill_rect(dx+2, dy+cell_h-10, cell_w-10, 4, hm_sep); vga_fill_rect(dx+2, dy+cell_h-10, bw, 4, glow); }
            } else {
                gui_draw_circle(dx+(cell_w-4)/2, dy+18, 10, hm_sep);
                vga_draw_string_trans(dx+2, dy+30, devs[di].icon, hm_sub);
            }
            vga_draw_string_trans(dx+2, dy+40, devs[di].name, dev_on ? hm_txt : hm_sub);
            /* on/off indicator dot */
            vga_fill_rect(dx+cell_w-10, dy+4, 6, 6, dev_on ? RGB(52,199,89) : RGB(150,150,155));
        }
        return 1;
    }

    /* Music app window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Music")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int mw=ww;
        uint32_t mu_bg  = g_pref_darkmode ? RGB(18,16,22)    : RGB(248,245,252);
        uint32_t mu_hd  = g_pref_darkmode ? RGB(30,26,38)    : RGB(232,226,242);
        uint32_t mu_sep = g_pref_darkmode ? RGB(55,50,68)    : RGB(205,198,218);
        uint32_t mu_txt = g_pref_darkmode ? RGB(215,210,225) : RGB(28,22,40);
        uint32_t mu_sub = g_pref_darkmode ? RGB(125,118,138) : RGB(108,100,120);
        uint32_t mu_acc = RGB(252,60,68);   /* Music red */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, mw-2, wh-TITLEBAR_H-19, mu_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, mw-2, 22, mu_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+23, mw-2, mu_sep);
        { int tl=str_len("Music")*8; vga_draw_string_trans(wx+(mw-tl)/2, wy+TITLEBAR_H+7, "Music", mu_acc); }
        /* Now Playing */
        int art_y = wy+TITLEBAR_H+28;
        int art_x = wx + (mw-80)/2;
        /* Album art */
        gui_draw_rounded_rect(art_x, art_y, 80, 80, 10, RGB(60,0,100));
        vga_fill_rect_alpha(art_x+2, art_y+2, 76, 30, RGB(255,255,255), 30);
        gui_draw_circle(art_x+40, art_y+40, 20, RGB(40,0,80));
        gui_draw_circle(art_x+40, art_y+40, 4, RGB(255,255,255));
        vga_draw_string_trans(art_x+4, art_y+56, "MyOS Vibes", RGB(255,180,255));
        vga_draw_string_trans(art_x+4, art_y+66, "Dark Synthwave", RGB(180,140,200));
        /* Track info */
        int ti_y = art_y+86;
        { int tl=str_len("Neon Horizon")*8; vga_draw_string_trans(wx+(mw-tl)/2, ti_y, "Neon Horizon", mu_txt); }
        { int tl=str_len("SynthWave Collective")*8; vga_draw_string_trans(wx+(mw-tl)/2, ti_y+12, "SynthWave Collective", mu_sub); }
        /* Heart */
        vga_draw_string_trans(art_x-16, ti_y+4, "<3", mu_acc);
        /* Progress bar */
        uint32_t t_mu = timer_ticks();
        int mu_dur=240, mu_pos=(int)((t_mu/1000)%mu_dur);
        int pb_x=wx+16, pb_w=mw-32;
        vga_fill_rect(pb_x, ti_y+26, pb_w, 3, mu_sep);
        vga_fill_rect(pb_x, ti_y+26, pb_w*mu_pos/mu_dur, 3, mu_acc);
        /* Time stamps */
        char ts_cur[6], ts_tot[6];
        ts_cur[0]='0'; ts_cur[1]=':'; ts_cur[2]='0'+mu_pos/60; ts_cur[3]='0'+(mu_pos%60)/10; ts_cur[4]='0'+mu_pos%10; ts_cur[5]=0;
        ts_tot[0]='4'; ts_tot[1]=':'; ts_tot[2]='0'; ts_tot[3]='0'; ts_tot[4]='0'; ts_tot[5]=0;
        vga_draw_string_trans(pb_x, ti_y+32, ts_cur, mu_sub);
        vga_draw_string_trans(pb_x+pb_w-5*8, ti_y+32, ts_tot, mu_sub);
        /* Controls */
        int ctrl_y = ti_y+44;
        /* Shuffle */
        vga_draw_string_trans(wx+12, ctrl_y+4, "<<", mu_sub);
        /* Prev */
        vga_draw_string_trans(wx+mw/2-40, ctrl_y+4, "|<", mu_sub);
        /* Play */
        int pl_x=wx+mw/2, pl_y=ctrl_y+12;
        gui_draw_circle(pl_x, pl_y, 14, mu_acc);
        vga_draw_string_trans(pl_x-4, pl_y-4, ">>", RGB(255,255,255));
        /* Next */
        vga_draw_string_trans(wx+mw/2+30, ctrl_y+4, ">|", mu_sub);
        /* Repeat */
        vga_draw_string_trans(wx+mw-24, ctrl_y+4, ">>", mu_sub);
        /* Volume */
        int vol_y = ctrl_y+32;
        vga_draw_string_trans(wx+8, vol_y+2, "Vol", mu_sub);
        vga_fill_rect(wx+36, vol_y+4, mw-56, 4, mu_sep);
        vga_fill_rect(wx+36, vol_y+4, (mw-56)*75/100, 4, mu_acc);
        vga_draw_string_trans(wx+mw-20, vol_y+2, "75", mu_sub);
        /* Song list */
        vga_draw_hline(wx+4, vol_y+14, mw-8, mu_sep);
        vga_draw_string_trans(wx+8, vol_y+18, "UP NEXT", mu_sub);
        static const char *songs[] = { "Cyber Dreams", "Electric Sky", "Night Drive" };
        int si3;
        for (si3=0; si3<3; si3++) {
            int sy=vol_y+30+si3*22;
            if (sy+22 > wy+wh-22) break;
            vga_fill_rect(wx+8, sy, 14, 14, RGB(80,0,140));
            vga_draw_char_trans(wx+11, sy+3, '0'+si3+2, RGB(255,255,255));
            vga_draw_string_trans(wx+26, sy+3, songs[si3], si3==0?mu_acc:mu_txt);
            vga_draw_string_trans(wx+mw-32, sy+3, "4:00", mu_sub);
        }
        return 1;
    }

    /* Reminders window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Reminders")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t rm_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,252);
        uint32_t rm_sb  = g_pref_darkmode ? RGB(38,38,42)    : RGB(238,238,242);
        uint32_t rm_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(205,205,210);
        uint32_t rm_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t rm_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
        uint32_t rm_acc = RGB(255,59,48); /* red accent */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, rm_bg);
        /* Left sidebar */
        int sb_w2 = 90;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, sb_w2, wh-TITLEBAR_H-19, rm_sb);
        vga_draw_vline(wx+sb_w2, wy+TITLEBAR_H+1, wh-TITLEBAR_H-19, rm_sep);
        /* Sidebar lists */
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+10, "MY LISTS", rm_sub);
        static const struct { const char *name; uint32_t col; int cnt; } rm_lists[] = {
            { "Today",     RGB(255,59,48),  3 },
            { "Scheduled", RGB(255,149,0),  5 },
            { "All",       RGB(142,142,147),8 },
            { "Flagged",   RGB(255,149,0),  2 },
            { "Work",      RGB(52,199,89),  4 },
            { "Personal",  RGB(0,122,255),  6 },
        };
        int li2, ns_rm=6;
        for (li2=0; li2<ns_rm; li2++) {
            int ly = wy+TITLEBAR_H+24+li2*22;
            if (li2==g_reminders_sel_list)
                vga_fill_rect(wx+2, ly-2, sb_w2-2, 20, g_pref_darkmode?RGB(50,50,55):RGB(220,220,225));
            gui_draw_circle(wx+16, ly+8, 7, rm_lists[li2].col);
            vga_draw_string_trans(wx+26, ly+4, rm_lists[li2].name, rm_txt);
            /* Badge count */
            if (rm_lists[li2].cnt > 0) {
                char cb2[3]; cb2[0]='0'+rm_lists[li2].cnt; cb2[1]=0;
                vga_draw_string_trans(wx+sb_w2-12, ly+4, cb2, rm_sub);
            }
        }
        /* Main content: Today's reminders */
        int cx3 = wx+sb_w2+8;
        int cy3 = wy+TITLEBAR_H+8;
        static const char *list_labels[] = {"Today","Scheduled","All","Flagged","Work","Personal"};
        vga_draw_string_trans(cx3, cy3, list_labels[g_reminders_sel_list], rm_txt);
        vga_draw_string_trans(cx3, cy3+12, "3 Reminders", rm_sub);
        vga_draw_hline(wx+sb_w2, cy3+24, ww-sb_w2-1, rm_sep);
        static const struct { const char *text; int flag; } rm_items[] = {
            { "Buy groceries", 0 },
            { "Call dentist",  1 },
            { "Review PR #42", 0 },
        };
        int ii2, ni_rm=3;
        for (ii2=0; ii2<ni_rm; ii2++) {
            int iy2 = cy3+30+ii2*34;
            int is_done = (g_reminders_done >> ii2) & 1;
            /* Interactive checkbox */
            if (is_done) {
                gui_draw_circle(cx3+9, iy2+9, 9, rm_acc);
                vga_draw_string_trans(cx3+5, iy2+5, "v", RGB(255,255,255));
            } else {
                gui_draw_circle_outline(cx3+9, iy2+9, 9, rm_sep);
            }
            /* Text - strikethrough if done */
            uint32_t tc = is_done ? rm_sub : rm_txt;
            vga_draw_string_trans(cx3+22, iy2+5, rm_items[ii2].text, tc);
            if (is_done) vga_draw_hline(cx3+22, iy2+9, str_len(rm_items[ii2].text)*8, rm_sub);
            /* Flag */
            if (rm_items[ii2].flag && !is_done)
                vga_draw_string_trans(cx3+22, iy2+16, "! Flagged", RGB(255,149,0));
            vga_draw_hline(wx+sb_w2+4, iy2+30, ww-sb_w2-8, rm_sep);
        }
        /* "Add Reminder" button at bottom */
        int ab_y = wy+wh-34;
        vga_draw_string_trans(cx3, ab_y, "+ New Reminder", rm_acc);
        return 1;
    }

    /* Contacts window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Contacts")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int ct_top = wy+TITLEBAR_H+1;
        int ct_h   = wh-TITLEBAR_H-19;
        uint32_t ct_bg   = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);
        uint32_t ct_sb   = g_pref_darkmode ? RGB(36,36,40)    : RGB(236,236,238);
        uint32_t ct_bd   = g_pref_darkmode ? RGB(55,55,60)    : RGB(205,205,210);
        uint32_t ct_txt  = g_pref_darkmode ? RGB(210,210,218) : RGB(20,20,25);
        uint32_t ct_sub  = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,108);
        uint32_t ct_hdr  = g_pref_darkmode ? RGB(90,90,98)    : RGB(100,100,108);
        uint32_t ct_sel  = RGB(0,122,255);
        uint32_t ct_tb   = g_pref_darkmode ? RGB(40,40,44)    : RGB(246,246,248);
        /* Background */
        vga_fill_rect(wx+1, ct_top, ww-2, ct_h, ct_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, ct_top, ww-2, 24, ct_tb);
        vga_draw_hline(wx+1, ct_top+24, ww-2, ct_bd);
        vga_draw_string_trans(wx+8, ct_top+8, "+", ct_sel);
        vga_draw_string_trans(wx+20, ct_top+8, "Add", ct_sub);
        vga_draw_string_trans(wx+ww-56, ct_top+8, "Groups", ct_sub);
        /* Search bar below toolbar */
        int sb_y2 = ct_top+26;
        vga_fill_rect(wx+8, sb_y2+2, ww-16, 16, g_pref_darkmode?RGB(44,44,50):RGB(238,238,242));
        gui_draw_rounded_rect_outline(wx+8, sb_y2+2, ww-16, 16, 4, ct_bd);
        vga_draw_string_trans(wx+16, sb_y2+6, "Search", ct_sub);
        /* Two-pane layout */
        int list_w = ww*2/5;
        int det_x  = wx+1+list_w;
        /* Contact list (left pane) */
        vga_fill_rect(wx+1, ct_top+44, list_w, ct_h-44, ct_sb);
        vga_draw_vline(wx+1+list_w, ct_top+44, ct_h-44, ct_bd);
        static const struct {
            const char *name; const char *phone; uint32_t col; char letter;
        } ct_list[] = {
            { "Alice Johnson",  "+1 555-0101", RGB(255,45,85),   'A' },
            { "Bob Smith",      "+1 555-0142", RGB(0,122,255),   'B' },
            { "Carol White",    "+1 555-0178", RGB(52,199,89),   'C' },
            { "David Brown",    "+1 555-0165", RGB(255,149,0),   'D' },
            { "Emma Davis",     "+1 555-0133", RGB(147,44,246),  'E' },
            { "Frank Wilson",   "+1 555-0190", RGB(0,199,190),   'F' },
            { "Grace Lee",      "+1 555-0121", RGB(255,59,48),   'G' },
            { "Henry Chen",     "+1 555-0155", RGB(100,80,220),  'H' },
        };
        int n_ct = 8, li3;
        /* A section header */
        vga_draw_string_trans(wx+8, ct_top+48, "A", ct_hdr);
        vga_draw_hline(wx+8, ct_top+58, list_w-10, ct_bd);
        for(li3=0; li3<n_ct; li3++) {
            int ly3 = ct_top+62+li3*32;
            if(ly3+32 > ct_top+ct_h) break;
            /* Add section headers B, C, D... */
            if(li3>0 && ct_list[li3].letter != ct_list[li3-1].letter) {
                char hc[2]; hc[0]=ct_list[li3].letter; hc[1]=0;
                vga_draw_string_trans(wx+8, ly3-10, hc, ct_hdr);
                vga_draw_hline(wx+8, ly3, list_w-10, ct_bd);
            }
            int is_sel = (li3==g_ct_sel);
            if(is_sel) vga_fill_rect(wx+2, ly3, list_w-2, 30, g_pref_darkmode?RGB(0,70,160):RGB(200,218,250));
            /* Avatar circle */
            gui_draw_circle(wx+16, ly3+15, 12, ct_list[li3].col);
            char ltr[2]; ltr[0]=ct_list[li3].letter; ltr[1]=0;
            vga_draw_string_trans(wx+12, ly3+11, ltr, RGB(255,255,255));
            /* Name + phone */
            vga_draw_string_trans(wx+32, ly3+6,  ct_list[li3].name,  ct_txt);
            vga_draw_string_trans(wx+32, ly3+18, ct_list[li3].phone, ct_sub);
            vga_draw_hline(wx+32, ly3+29, list_w-34, ct_bd);
        }
        /* Detail pane (right, showing selected contact) */
        vga_fill_rect(det_x, ct_top+44, ww-1-list_w, ct_h-44, ct_bg);
        { int sel = g_ct_sel < n_ct ? g_ct_sel : 0;
          /* Large avatar */
          int av_x = det_x + (ww-1-list_w)/2;
          int av_y = ct_top+60;
          gui_draw_circle(av_x, av_y+24, 28, ct_list[sel].col);
          char avl[2]; avl[0]=ct_list[sel].letter; avl[1]=0;
          vga_draw_string_trans(av_x-4, av_y+16, avl, RGB(255,255,255));
          /* Name */
          { const char *fn=ct_list[sel].name; int nl=str_len(fn)*8;
            vga_draw_string_trans(av_x-(nl/2), av_y+58, fn, ct_txt); }
          /* Action buttons */
          int ab_y2 = av_y+72;
          static const char *ct_acts[]={"Call","FaceTime","Message","Mail"};
          static const uint32_t ct_act_c[]={RGB(52,199,89),RGB(0,199,190),RGB(52,199,89),RGB(0,122,255)};
          int na=4, ai3; int ab_w=(ww-1-list_w-8)/na;
          for(ai3=0;ai3<na;ai3++) {
              int abx=det_x+2+ai3*ab_w;
              gui_draw_rounded_rect(abx, ab_y2, ab_w-2, 24, 6, g_pref_darkmode?RGB(44,44,50):RGB(232,232,238));
              gui_draw_rounded_rect_outline(abx, ab_y2, ab_w-2, 24, 6, ct_bd);
              gui_draw_circle(abx+(ab_w-2)/2, ab_y2+8, 6, ct_act_c[ai3]);
              int nal=str_len(ct_acts[ai3])*8;
              vga_draw_string_trans(abx+(ab_w-2-nal)/2, ab_y2+18, ct_acts[ai3], ct_sub);
          }
          /* Contact info fields (per-contact data) */
          static const struct {const char *phone; const char *email; const char *bday;} ct_data[]={
              {"+1 555-0101","alice@email.com","April 5"},
              {"+1 555-0142","bob@email.com","July 12"},
              {"+1 555-0178","carol@email.com","Jan 20"},
              {"+1 555-0165","david@email.com","Sept 3"},
              {"+1 555-0133","emma@email.com","Nov 15"},
              {"+1 555-0190","frank@email.com","Feb 28"},
              {"+1 555-0121","grace@email.com","Aug 7"},
              {"+1 555-0155","henry@email.com","Mar 22"},
          };
          int inf_y = ab_y2+44;
          vga_fill_rect(det_x, inf_y, ww-1-list_w, 1, ct_bd);
          struct { const char *label; const char *val; uint32_t vc; } ct_info2[3];
          ct_info2[0].label="mobile"; ct_info2[0].val=ct_data[sel].phone; ct_info2[0].vc=RGB(0,122,255);
          ct_info2[1].label="email";  ct_info2[1].val=ct_data[sel].email; ct_info2[1].vc=RGB(0,122,255);
          ct_info2[2].label="bday";   ct_info2[2].val=ct_data[sel].bday;  ct_info2[2].vc=ct_txt;
          int ii3; int inf_row_h=22;
          for(ii3=0;ii3<3;ii3++) {
              int iy3=inf_y+4+ii3*inf_row_h;
              if(iy3+inf_row_h > ct_top+ct_h-2) break;
              vga_draw_string_trans(det_x+4, iy3+6, ct_info2[ii3].label, ct_sub);
              vga_draw_string_trans(det_x+56, iy3+6, ct_info2[ii3].val, ct_info2[ii3].vc);
              vga_draw_hline(det_x+4, iy3+inf_row_h-1, ww-list_w-8, ct_bd);
          }
        }
        return 1;
    }

    /* Weather window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Weather")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int by = wy+TITLEBAR_H+1;
        int bh = wh-TITLEBAR_H-19;
        int ww_safe = ww > 0 ? ww : 1;
        if (bh < 1) bh = 1;
        /* Dynamic sky gradient using time-of-day from timer */
        uint32_t t_wth = timer_ticks();
        int phase_wth = (int)((t_wth / 5000) % 64);
        runtime_weather_info_t weather;
        runtime_get_weather_info(&weather);
        /* Pick sky colors for morning (blue gradient) */
        int row3;
        for (row3=0; row3<bh; row3++) {
            int frac = row3*100/bh;
            uint8_t r3, g3, b3;
            if (phase_wth < 16) { /* dawn/morning */
                r3=(uint8_t)(80-frac*30/100); g3=(uint8_t)(120+frac*20/100); b3=(uint8_t)(200-frac*40/100);
            } else if (phase_wth < 32) { /* noon */
                r3=(uint8_t)(40-frac*20/100); g3=(uint8_t)(100+frac*10/100); b3=(uint8_t)(210-frac*50/100);
            } else if (phase_wth < 48) { /* sunset */
                r3=(uint8_t)(180-frac*80/100); g3=(uint8_t)(100-frac*40/100); b3=(uint8_t)(160-frac*60/100);
            } else { /* night */
                r3=(uint8_t)(10); g3=(uint8_t)(15+frac*5/100); b3=(uint8_t)(50+frac*20/100);
            }
            vga_draw_hline(wx+1, by+row3, ww-2, RGB(r3,g3,b3));
        }
        /* Animated clouds */
        { int cloud_ox = (int)((t_wth/200) % (uint32_t)ww_safe);
          vga_fill_rect_alpha(wx+2+(cloud_ox%100), by+30, 40, 16, RGB(255,255,255), 60);
          vga_fill_rect_alpha(wx+2+(cloud_ox%100)-4, by+38, 48, 10, RGB(255,255,255), 60);
          vga_fill_rect_alpha(wx+ww/2+(cloud_ox%80), by+45, 30, 12, RGB(255,255,255), 50);
        }
        /* City name */
        { int ll=str_len(weather.location)*8;
          vga_draw_string_trans(wx+(ww-ll)/2, by+6, weather.location, RGB(255,255,255)); }
        /* BIG temperature — 3x scale */
        { char temp[8];
          runtime_format_uint((uint32_t)weather.temperature_c, temp, sizeof(temp));
          int ci5; int tx3=wx+(ww-str_len(temp)*24-20)/2; int ty3=by+22;
          for (ci5=0; temp[ci5]; ci5++) {
              int cix=tx3+ci5*24, cir, cic;
              for (cir=0;cir<24;cir++) for (cic=0;cic<8;cic++) {
                  if (font8x8[(int)temp[ci5]][cir/3] & (1<<cic))
                      vga_fill_rect(cix+cic*3, ty3+cir, 3, 1, RGB(255,255,255));
              }
          }
          vga_draw_string_trans(tx3+str_len(temp)*24+4, ty3, "o", RGB(255,255,255));
          vga_draw_string_trans(tx3+str_len(temp)*24+12, ty3+4, "C", RGB(255,255,255));
        }
        /* Condition text */
        { int cl2=str_len(weather.condition)*8;
          vga_draw_string_trans(wx+(ww-cl2)/2, by+52, weather.condition, RGB(220,240,255)); }
        { char hl[20]; runtime_format_high_low(weather.high_c, weather.low_c, hl, sizeof(hl));
          int hl2=str_len(hl)*8;
          vga_draw_string_trans(wx+(ww-hl2)/2, by+64, hl, RGB(200,220,255)); }
        /* Sun icon (animated rays) */
        { int sx2=wx+ww-32, sy2=by+14; int ray_off=(int)(t_wth/300)%8;
          gui_draw_circle(sx2, sy2, 10, RGB(255,220,60));
          static const int rx_t[8]={0,9,13,9,0,-9,-13,-9}, ry_t[8]={-13,-9,0,9,13,9,0,-9};
          int ri3; for(ri3=0;ri3<8;ri3++) {
              int rr=(ri3+ray_off)%8;
              vga_draw_line(sx2+rx_t[rr]/2,sy2+ry_t[rr]/2,sx2+rx_t[rr],sy2+ry_t[rr],RGB(255,220,60));
          }
        }
        /* HOURLY FORECAST strip */
        int hf_y = by+80;
        vga_fill_rect_alpha(wx+1, hf_y, ww-2, 42, RGB(0,0,0), 70);
        vga_draw_hline(wx+1, hf_y, ww-2, RGB(255,255,255));
        vga_draw_string_trans(wx+4, hf_y+2, "HOURLY FORECAST", RGB(200,220,255));
        { int hi4,n_hr=RUNTIME_HOURLY_COUNT; int hw=(ww-4)/n_hr;
          if (hw < 1) hw = 1;
          for(hi4=0;hi4<n_hr;hi4++) {
              int hx=wx+2+hi4*hw;
              vga_draw_string_trans(hx+(hw-str_len(weather.hourly_label[hi4])*8)/2, hf_y+12, weather.hourly_label[hi4], RGB(200,220,255));
              /* Icon: sun=yellow circle, pc=half cloud */
              int ic_x=hx+hw/2, ic_y=hf_y+28;
              if(weather.hourly_icon[hi4][0]=='s') {
                  gui_draw_circle(ic_x, ic_y, 5, RGB(255,220,60));
              } else {
                  vga_fill_rect(ic_x-5, ic_y-2, 10, 5, RGB(200,210,220));
                  gui_draw_circle(ic_x-2, ic_y-4, 4, RGB(220,225,230));
                  gui_draw_circle(ic_x+2, ic_y-2, 3, RGB(220,225,230));
              }
              /* Temp */
              { char tc[8]; runtime_format_uint((uint32_t)weather.hourly_temp_c[hi4], tc, sizeof(tc));
                vga_draw_string_trans(hx+(hw-16)/2, hf_y+34, tc, RGB(255,255,255)); }
          }
        }
        /* 3-DAY FORECAST */
        int fc_y = by+128;
        vga_fill_rect_alpha(wx+1, fc_y, ww-2, bh-(128), RGB(0,0,0), 70);
        vga_draw_hline(wx+1, fc_y, ww-2, RGB(255,255,255));
        vga_draw_string_trans(wx+4, fc_y+2, "3-DAY FORECAST", RGB(200,220,255));
        { datetime_t now_w;
          int di; int dw2=(ww-4)/RUNTIME_WEATHER_DAYS;
          if (dw2 < 1) dw2 = 1;
          get_current_datetime(&now_w);
          for(di=0;di<RUNTIME_WEATHER_DAYS;di++) {
              int dx2=wx+2+di*dw2;
              int dc=fc_y+14;
              const char *day_name = datetime_weekday_short((now_w.weekday + di) % 7);
              vga_draw_string_trans(dx2+(dw2-24)/2, dc, day_name, RGB(200,220,255));
              /* Icon */
              int di_x=dx2+dw2/2, di_y=dc+16;
              if(weather.forecast[di].condition[0]=='S') {
                  gui_draw_circle(di_x, di_y, 6, RGB(255,220,60));
              } else {
                  vga_fill_rect(di_x-6, di_y-3, 12, 6, RGB(200,205,215));
                  gui_draw_circle(di_x-2, di_y-5, 5, RGB(215,218,225));
              }
              /* Temp range bar */
              int bar_y=dc+30;
              vga_fill_rect_alpha(dx2+2, bar_y+4, dw2-4, 4, RGB(150,150,160), 180);
              int bar_lo=(weather.forecast[di].low_c-weather.low_c)*dw2/20;
              int bar_hi=(weather.forecast[di].high_c-weather.low_c)*dw2/20;
              if(bar_lo<0) bar_lo=0;
              if(bar_hi>dw2-4) bar_hi=dw2-4;
              vga_fill_rect(dx2+2+bar_lo, bar_y+4, bar_hi-bar_lo, 4, RGB(255,200,50));
              char hb[8]; runtime_format_uint((uint32_t)weather.forecast[di].high_c, hb, sizeof(hb));
              char lb[8]; runtime_format_uint((uint32_t)weather.forecast[di].low_c, lb, sizeof(lb));
              vga_draw_string_trans(dx2+2, bar_y+10, lb, RGB(180,200,240));
              vga_draw_string_trans(dx2+dw2-18, bar_y+10, hb, RGB(255,255,255));
          }
        }
        return 1;
    }

    /* FaceTime window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "FaceTime")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int vy = wy+TITLEBAR_H+1;
        int vh = wh-TITLEBAR_H-19;
        if (vh < 1) vh = 1;
        /* Dark gradient background */
        { int vr;
          for (vr=0; vr<vh; vr++) {
              uint8_t rr2=(uint8_t)(15+vr*12/vh), bb2=(uint8_t)(25+vr*15/vh);
              vga_draw_hline(wx+1, vy+vr, ww-2, RGB(rr2, rr2, bb2));
          }
        }

        g_facetime_active = 0;
        vga_draw_string_trans(wx+8, vy+8, "FaceTime", RGB(255,255,255));
        gui_draw_circle(wx+ww/2, vy+vh/2-18, 34, RGB(50,80,130));
        vga_draw_string_trans(wx+(ww-160)/2, vy+vh/2+28, "Call service unavailable", RGB(220,220,228));
        vga_draw_string_trans(wx+(ww-144)/2, vy+vh/2+44, "No account configured", RGB(140,140,160));
        return 1;
    }

    /* Time Machine window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Time Machine")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy_tm = wy+TITLEBAR_H+1;
        int ch_tm = wh-TITLEBAR_H-19;
        if (ch_tm < 1) ch_tm = 1;
        /* Space background gradient (dark purple to black) */
        int tmr;
        for (tmr=0; tmr<ch_tm; tmr++) {
            uint8_t r5=(uint8_t)(20-tmr*10/ch_tm>0?20-tmr*10/ch_tm:0);
            uint8_t b5=(uint8_t)(40+tmr*20/ch_tm);
            vga_draw_hline(wx+1, cy_tm+tmr, ww-2, RGB(r5, 0, b5));
        }
        /* Animated starfield */
        uint32_t t_tm = timer_ticks();
        { static const uint8_t stx[16]={12,34,67,89,120,145,200,230,45,78,110,160,190,22,55,180};
          static const uint8_t sty[16]={8,25,42,60,15,38,22,50,70,35,55,12,45,68,30,58};
          int star_w = ww > 4 ? ww - 4 : 1;
          int star_h = ch_tm > 4 ? ch_tm - 4 : 1;
          int si4;
          for (si4=0; si4<16; si4++) {
              int sx=(int)stx[si4] % star_w;
              int sy=(int)sty[si4] % star_h;
              uint8_t bri=(uint8_t)(150+((t_tm/200+si4*17)%3)*30);
              vga_put_pixel(wx+2+sx, cy_tm+sy, RGB(bri,bri,bri));
          }
        }
        /* Window stack perspective - 3 receding snapshots */
        { int depth, n_snap=3;
          for (depth=n_snap-1; depth>=0; depth--) {
              int scale = 80 - depth*18;
              int sw2 = ww*scale/100, sh2 = (ch_tm-40)*scale/100;
              int snap_x = wx + (ww-sw2)/2 - depth*6;
              int snap_y = cy_tm+20 + depth*20;
              uint8_t dim = (uint8_t)(200 - depth*50);
              vga_fill_rect(snap_x, snap_y, sw2, sh2, RGB(dim/3, dim/3, dim/3+20));
              vga_draw_rect_outline(snap_x, snap_y, sw2, sh2, RGB(100+depth*20, 120+depth*20, 200+depth*10));
              /* Window header bar */
              vga_fill_rect(snap_x+1, snap_y+1, sw2-2, 8, RGB(50+depth*10, 50+depth*10, 80+depth*10));
              /* Content lines */
              { int li3;
                for (li3=0;li3<3;li3++) {
                    uint8_t lc=(uint8_t)(80-depth*20);
                    vga_fill_rect(snap_x+4, snap_y+12+li3*7, sw2-8, 4, RGB(lc,lc,lc+20));
                }
              }
              /* Date label on each snapshot */
              { char dl[12];
                datetime_t dt;
                int yr, mn;
                get_current_datetime(&dt);
                yr = dt.year;
                mn = dt.month - depth;
                while (mn <= 0) { mn += 12; yr--; }
                dl[0]='0'+mn/10; dl[1]='0'+mn%10; dl[2]='-';
                dl[3]='0'+yr/1000%10; dl[4]='0'+yr/100%10; dl[5]='0'+yr/10%10; dl[6]='0'+yr%10; dl[7]=0;
                int dw2=str_len(dl)*8;
                vga_draw_string_trans(snap_x+(sw2-dw2)/2, snap_y-10, dl, RGB(180,180,220));
              }
          }
        }
        /* Right timeline */
        int tl_x = wx+ww-28;
        vga_fill_rect_alpha(tl_x, cy_tm, 24, ch_tm, RGB(0,0,0), 100);
        vga_draw_vline(tl_x+12, cy_tm, ch_tm, RGB(0,122,255));
        { int ti_i;
          static const int tl_offs[] = {10, 30, 60, 90, 120, 150};
          for (ti_i=0; ti_i<6; ti_i++) {
              int ty = cy_tm + tl_offs[ti_i];
              if (ty > cy_tm+ch_tm-20) break;
              gui_draw_circle(tl_x+12, ty, 3, ti_i==0?RGB(255,255,255):RGB(100,120,200));
          }
        }
        /* Bottom control bar */
        int bc_y = cy_tm+ch_tm-24;
        vga_fill_rect_alpha(wx+1, bc_y, ww-2, 22, RGB(0,0,0), 180);
        /* Cancel button */
        vga_fill_rect(wx+8, bc_y+3, 60, 16, RGB(70,70,75));
        gui_draw_rounded_rect_outline(wx+8, bc_y+3, 60, 16, 4, RGB(120,120,128));
        vga_draw_string_trans(wx+20, bc_y+7, "Cancel", RGB(200,200,210));
        /* Restore button */
        vga_fill_rect(wx+ww-72, bc_y+3, 64, 16, RGB(0,122,255));
        gui_draw_rounded_rect_outline(wx+ww-72, bc_y+3, 64, 16, 4, RGB(0,160,255));
        vga_draw_string_trans(wx+ww-60, bc_y+7, "Restore", RGB(255,255,255));
        /* Title */
        { int tl=str_len("Time Machine")*8;
          vga_draw_string_trans(wx+(ww-tl)/2, bc_y+7, "Time Machine", RGB(200,200,220)); }
        return 1;
    }

    /* AirDrop window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "AirDrop")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t ad_bg  = g_pref_darkmode ? RGB(22,22,26)    : RGB(242,242,248);
        uint32_t ad_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(200,200,205);
        uint32_t ad_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t ad_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
        uint32_t ad_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, ad_bg);
        /* Title bar content */
        { int tw2 = str_len("AirDrop")*8;
          vga_draw_string_trans(wx+(ww-tw2)/2, wy+TITLEBAR_H+6, "AirDrop", ad_acc); }
        vga_draw_hline(wx+1, wy+TITLEBAR_H+18, ww-2, ad_sep);
        /* Radar animation */
        uint32_t t_ad = timer_ticks();
        int cx3 = wx + ww/2;
        int cy3 = wy + TITLEBAR_H + 80;
        int ring = (int)((t_ad / 400) % 4);
        /* Outer rings pulsing */
        int ri2;
        for (ri2=3; ri2>=0; ri2--) {
            int pr = 20 + ri2*16;
            int phase2 = (ring + ri2) % 4;
            uint8_t alpha2 = (uint8_t)(60 - phase2*12);
            uint32_t rc2 = ad_acc;
            (void)alpha2;
            if (phase2 < 2)
                gui_draw_circle_outline(cx3, cy3, pr, rc2);
        }
        /* Center device icon */
        gui_draw_circle(cx3, cy3, 16, ad_acc);
        vga_draw_string_trans(cx3-8, cy3-4, "My", RGB(255,255,255));
        vga_draw_string_trans(cx3-4, cy3+4, "OS", RGB(255,255,255));
        /* Nearby device 1 */
        int d1x = wx+30, d1y = wy+TITLEBAR_H+52;
        gui_draw_circle(d1x, d1y, 10, g_pref_darkmode?RGB(55,55,60):RGB(220,220,228));
        gui_draw_circle_outline(d1x, d1y, 10, ad_sep);
        vga_draw_string_trans(d1x-8, d1y+14, "iPhone", ad_sub);
        /* Nearby device 2 */
        int d2x = wx+ww-36, d2y = wy+TITLEBAR_H+60;
        gui_draw_circle(d2x, d2y, 10, g_pref_darkmode?RGB(55,55,60):RGB(220,220,228));
        gui_draw_circle_outline(d2x, d2y, 10, ad_sep);
        vga_draw_string_trans(d2x-8, d2y+14, "MacBook", ad_sub);
        /* Status text */
        { const char *st = "Searching for nearby devices...";
          int sl = str_len(st)*8;
          if (sl > ww-16) st = "Searching...";
          sl = str_len(st)*8;
          vga_draw_string_trans(wx+(ww-sl)/2, wy+TITLEBAR_H+148, st, ad_sub);
        }
        /* Discover mode selector */
        vga_draw_hline(wx+8, wy+TITLEBAR_H+162, ww-16, ad_sep);
        vga_draw_string_trans(wx+10, wy+TITLEBAR_H+168, "Allow to be discovered:", ad_sub);
        /* Contacts Only button */
        int btn1x=wx+8, btn1y=wy+TITLEBAR_H+182, btn1w=(ww-20)/2;
        vga_fill_rect(btn1x, btn1y, btn1w, 16, ad_acc);
        gui_draw_rounded_rect_outline(btn1x, btn1y, btn1w, 16, 3, ad_acc);
        { int bl=str_len("Contacts Only")*8;
          vga_draw_string_trans(btn1x+(btn1w-bl)/2, btn1y+4, "Contacts Only", RGB(255,255,255)); }
        /* Everyone button */
        int btn2x=wx+ww/2+2, btn2w=btn1w;
        vga_fill_rect_alpha(btn2x, btn1y, btn2w, 16, ad_sep, 200);
        gui_draw_rounded_rect_outline(btn2x, btn1y, btn2w, 16, 3, ad_sep);
        { int bl=str_len("Everyone")*8;
          vga_draw_string_trans(btn2x+(btn2w-bl)/2, btn1y+4, "Everyone", ad_txt); }
        return 1;
    }

    /* Keyboard Shortcuts window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Keyboard Shortcuts")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t ks_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,252);
        uint32_t ks_hd  = g_pref_darkmode ? RGB(44,44,48)    : RGB(232,232,236);
        uint32_t ks_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t ks_key = g_pref_darkmode ? RGB(55,55,60)    : RGB(220,220,225);
        uint32_t ks_kbd = g_pref_darkmode ? RGB(180,180,188) : RGB(60,60,70);
        uint32_t ks_lbl = g_pref_darkmode ? RGB(120,120,128) : RGB(90,90,100);
        uint32_t ks_val = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t ks_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, ks_bg);
        /* Header */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 28, ks_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+29, ww-2, ks_sep);
        vga_draw_string_trans(wx+10, wy+TITLEBAR_H+10, "Keyboard Shortcuts", ks_acc);
        /* Column headers */
        int ky = wy+TITLEBAR_H+38;
        int col_count = (ww >= 500) ? 2 : 1;
        int col_w = (ww - 20) / col_count;
        int col_i;
        for (col_i = 0; col_i < col_count; col_i++) {
            int cx2 = wx + 10 + col_i * col_w;
            vga_draw_string_trans(cx2,     ky, "Key", ks_lbl);
            vga_draw_string_trans(cx2+86,  ky, "Action", ks_lbl);
        }
        vga_draw_hline(wx+4, ky+12, ww-8, ks_sep);
        ky += 16;
        /* Shortcut entries */
        static const struct { const char *key; const char *action; } shortcuts[] = {
            { "Ctrl+G",   "Stage Manager"      },
            { "Ctrl+A",   "Activity Monitor"   },
            { "Ctrl+B",   "Control Center"     },
            { "Ctrl+D",   "Toggle Dark Mode"   },
            { "Ctrl+C",   "Messages"           },
            { "Ctrl+E",   "Reminders"          },
            { "Ctrl+F",   "FaceTime"           },
            { "Ctrl+H",   "AirDrop Panel"      },
            { "Ctrl+\\",  "Snake Game"         },
            { "Ctrl+]",   "Breakout Game"      },
            { "Ctrl+^",   "Translate"          },
            { "Ctrl+_",   "Split View"         },
            { "Ctrl+J",   "Home"               },
            { "Ctrl+Q",   "Music"              },
            { "Ctrl+K",   "Lock Screen"        },
            { "Ctrl+L",   "Launchpad"          },
            { "Ctrl+M",   "Mission Control"    },
            { "Ctrl+N",   "Notifications"      },
            { "Ctrl+O",   "Podcasts"           },
            { "Ctrl+P",   "System Info"        },
            { "Ctrl+R",   "AirDrop"            },
            { "Ctrl+S",   "Settings"           },
            { "Ctrl+T",   "Stocks"             },
            { "Ctrl+U",   "Books"              },
            { "Ctrl+V",   "Maps"               },
            { "Ctrl+W",   "Weather"            },
            { "Ctrl+X",   "News"               },
            { "Ctrl+Y",   "Calendar"           },
            { "Ctrl+Z",   "Mail"               },
            { "Tab",      "Spotlight Search"   },
            { "P",        "Screenshot Tool"    },
            { "F1",       "Night Shift"        },
            { "F2",       "Focus Mode"         },
            { "F3",       "App Expose"         },
            { "F4",       "Launchpad"          },
            { "F5",       "App Switcher"       },
            { "F6",       "Switch App"         },
            { "F7",       "Widget Bar"         },
            { "F8",       "Keyboard Shortcuts" },
            { "F9",       "Find My"            },
            { "F10",      "Wallet"             },
            { "F11",      "Voice Memos"        },
            { "F12",      "Freeform"           },
            { "ESC",      "Close Window"       },
            { "Insert",   "Writing Tools"      },
            { "Delete",   "Quick Note"         },
            { "Ctrl+1-4", "Switch Spaces"      },
            { "Ctrl+Left","Tile: Left Half"    },
            { "Ctrl+Rght","Tile: Right Half"   },
            { "Ctrl+Up",  "Tile: Maximize"     },
            { "Ctrl+Down","Tile: Restore"      },
        };
        int ns2 = (int)(sizeof(shortcuts) / sizeof(shortcuts[0]));
        int rows_per_col = (ns2 + col_count - 1) / col_count;
        int i_ks;
        for (i_ks=0; i_ks<ns2; i_ks++) {
            int col = i_ks / rows_per_col;
            int row = i_ks % rows_per_col;
            int base_x = wx + 8 + col * col_w;
            int ry2 = ky + row*14;
            if (col >= col_count || ry2+13 > wy+wh-22) break;
            /* Key badge */
            int kw = str_len(shortcuts[i_ks].key)*8 + 6;
            vga_fill_rect(base_x, ry2-1, kw, 12, ks_key);
            gui_draw_rounded_rect_outline(base_x, ry2-1, kw, 12, 2, ks_sep);
            vga_draw_string_trans(base_x+3, ry2+1, shortcuts[i_ks].key, ks_kbd);
            /* Action label */
            vga_draw_string_trans(base_x+86, ry2+1, shortcuts[i_ks].action, ks_val);
            /* Row separator */
            vga_draw_hline(base_x, ry2+12, col_w-6, ks_sep);
        }
        return 1;
    }

    /* System Info window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "System Info")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t si_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,252);
        uint32_t si_hd  = g_pref_darkmode ? RGB(44,44,48)    : RGB(232,232,236);
        uint32_t si_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t si_lbl = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t si_val = g_pref_darkmode ? RGB(220,220,228) : RGB(30,30,40);
        uint32_t si_acc = RGB(0,122,255);
        runtime_system_info_t sys;
        runtime_storage_info_t storage;
        const netif_t *net;
        runtime_get_system_info(&sys);
        net = runtime_primary_netif();
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, si_bg);
        /* Header: OS logo area */
        int hh = 52;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, hh, si_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+hh, ww-2, si_sep);
        /* MyOS logo circle */
        gui_draw_circle(wx+30, wy+TITLEBAR_H+1+hh/2, 18, RGB(0,100,220));
        vga_draw_string_trans(wx+23, wy+TITLEBAR_H+1+hh/2-4, "OS", RGB(255,255,255));
        vga_draw_string_trans(wx+54, wy+TITLEBAR_H+8, sys.sysname, si_val);
        { char ver[48];
          int vp = 0;
          ver[0] = 0;
          apps2_append_text(ver, &vp, sizeof(ver), "Version ");
          apps2_append_text(ver, &vp, sizeof(ver), sys.release);
          apps2_append_text(ver, &vp, sizeof(ver), " (");
          apps2_append_text(ver, &vp, sizeof(ver), sys.version);
          apps2_append_text(ver, &vp, sizeof(ver), ")");
          vga_draw_string_trans(wx+54, wy+TITLEBAR_H+20, ver, si_lbl); }
        { datetime_t dt;
          char yearbuf[8];
          char copy[40];
          int cp = 0;
          get_current_datetime(&dt);
          int_to_str(dt.year, yearbuf);
          copy[0] = 0;
          apps2_append_text(copy, &cp, sizeof(copy), "Copyright ");
          apps2_append_text(copy, &cp, sizeof(copy), yearbuf);
          apps2_append_text(copy, &cp, sizeof(copy), " MyOS Project");
          vga_draw_string_trans(wx+54, wy+TITLEBAR_H+32, copy, si_lbl); }
        /* Specs table */
        int sy = wy+TITLEBAR_H+1+hh+8;
        {
            char membuf[16];
            char displaybuf[48];
            char storagebuf[32];
            int sp = 0;
            const char *storage_text = "unavailable";
            runtime_format_bytes(sys.pmm_total_bytes, membuf, sizeof(membuf));
            apps2_format_display(&sys, displaybuf, sizeof(displaybuf));
            if (runtime_get_storage_info("/", &storage) == 0) {
                storagebuf[0] = 0;
                apps2_append_text(storagebuf, &sp, sizeof(storagebuf), storage.name);
                apps2_append_text(storagebuf, &sp, sizeof(storagebuf), " ");
                apps2_append_bytes(storagebuf, &sp, sizeof(storagebuf), storage.total_bytes);
                storage_text = storagebuf;
            }
            struct { const char *lbl; const char *val; } specs[] = {
                { "Processor",  sys.cpu_model },
                { "Memory",     membuf },
                { "Display",    displaybuf },
                { "Storage",    storage_text },
                { "Kernel",     sys.release },
                { "Boot",       sys.boot_loader },
                { "Network",    net ? net->name : "none" },
            };
            int ns = 7;
            int i_si;
            for (i_si=0; i_si<ns; i_si++) {
                int ry = sy + i_si*18;
                vga_draw_string_trans(wx+8,   ry, specs[i_si].lbl, si_lbl);
                vga_draw_string_trans(wx+100, ry, specs[i_si].val, si_val);
                vga_draw_hline(wx+4, ry+14, ww-8, si_sep);
            }
        }
        /* Uptime at bottom */
        char upbuf[32];
        runtime_format_uptime(sys.uptime_seconds, upbuf, sizeof(upbuf));
        vga_draw_string_trans(wx+8, sy+7*18, "Up", si_acc);
        vga_draw_string_trans(wx+30, sy+7*18, upbuf, si_acc);
        return 1;
    }

    /* Color Picker window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Color Picker")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t cp_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,252);
        uint32_t cp_hd  = g_pref_darkmode ? RGB(44,44,48)    : RGB(232,232,236);
        uint32_t cp_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t cp_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t cp_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,110);
        uint32_t cp_acc = RGB(0,122,255);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, cp_bg);
        /* Header toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, cp_hd);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, cp_sep);
        /* Mode tabs: Wheel | Sliders | Palette */
        static const char *cp_tabs[] = { "Wheel", "Sliders", "Palette" };
        int cpt_x = wx+4, cpt_y = wy+TITLEBAR_H+6;
        int ct;
        for (ct=0; ct<3; ct++) {
            int tw = (int)(str_len(cp_tabs[ct])*8+8);
            int is_sel = (ct==0);
            if (is_sel) { vga_fill_rect(cpt_x, cpt_y-2, tw, 16, cp_acc); gui_draw_rounded_rect_outline(cpt_x, cpt_y-2, tw, 16, 2, cp_acc); }
            else        { vga_fill_rect(cpt_x, cpt_y-2, tw, 16, cp_hd); gui_draw_rounded_rect_outline(cpt_x, cpt_y-2, tw, 16, 2, cp_sep); }
            vga_draw_string_trans(cpt_x+4, cpt_y+2, cp_tabs[ct], is_sel ? RGB(255,255,255) : cp_sub);
            cpt_x += tw + 4;
        }
        /* Color wheel */
        int wheel_cx = wx + ww/2;
        int wheel_cy = wy + TITLEBAR_H + 68;
        int wheel_r  = 38;
        /* Draw color ring */
        int wa, wr;
        for (wa=0; wa<360; wa+=2) {
            uint8_t rr2,gg2,bb2;
            int ang = wa;
            /* HSV → RGB at S=1, V=1 */
            int h6 = ang*6/360;
            int f = (ang*6)%360;
            int q = 255 - (255*f/360);
            int t2 = (255*f/360);
            switch(h6) {
                case 0: rr2=255;gg2=(uint8_t)t2;bb2=0;break;
                case 1: rr2=(uint8_t)q;gg2=255;bb2=0;break;
                case 2: rr2=0;gg2=255;bb2=(uint8_t)t2;break;
                case 3: rr2=0;gg2=(uint8_t)q;bb2=255;break;
                case 4: rr2=(uint8_t)t2;gg2=0;bb2=255;break;
                default: rr2=255;gg2=0;bb2=(uint8_t)q;break;
            }
            uint32_t hc = RGB(rr2,gg2,bb2);
            for (wr=wheel_r-10; wr<=wheel_r; wr++) {
                int px2 = wheel_cx + wr*vga_cos_approx(ang) / 100;
                int py2 = wheel_cy + wr*vga_sin_approx(ang) / 100;
                if (px2 >= wx+1 && px2 < wx+ww-1 && py2 > wy+TITLEBAR_H+26 && py2 < wy+wh-20)
                    vga_put_pixel(px2, py2, hc);
            }
        }
        /* White center gradient */
        int wi2;
        for (wi2=0; wi2<wheel_r-12; wi2++) {
            uint8_t brightness = (uint8_t)(255 - wi2*255/(wheel_r-12));
            gui_draw_circle(wheel_cx, wheel_cy, wi2,
                g_pref_darkmode ? RGB(brightness/4, brightness/4, brightness/4)
                                : RGB(brightness, brightness, brightness));
        }
        /* Crosshair on wheel (showing selected hue ~200° = cyan-blue) */
        int sel_ang = 210;
        int sel_r   = wheel_r - 5;
        int crs_x   = wheel_cx + sel_r * vga_cos_approx(sel_ang) / 100;
        int crs_y   = wheel_cy + sel_r * vga_sin_approx(sel_ang) / 100;
        gui_draw_circle_outline(crs_x, crs_y, 5, RGB(255,255,255));
        gui_draw_circle_outline(crs_x, crs_y, 4, RGB(0,80,200));
        /* Selected color swatch */
        int sw_x = wx+ww-44, sw_y = wy+TITLEBAR_H+32, sw_w = 36, sw_h = 36;
        vga_fill_rect(sw_x, sw_y, sw_w, sw_h, RGB(0,122,255));
        gui_draw_rounded_rect_outline(sw_x, sw_y, sw_w, sw_h, 3, cp_sep);
        vga_draw_string_trans(sw_x+2, sw_y-10, "Color", cp_sub);
        /* Hex value */
        vga_draw_string_trans(wx+4, wy+TITLEBAR_H+115, "Hex:", cp_sub);
        vga_fill_rect(wx+36, wy+TITLEBAR_H+112, ww-46, 14, g_pref_darkmode?RGB(44,44,48):RGB(240,240,244));
        gui_draw_rounded_rect_outline(wx+36, wy+TITLEBAR_H+112, ww-46, 14, 2, cp_sep);
        vga_draw_string_trans(wx+40, wy+TITLEBAR_H+115, "#007AFF", cp_txt);
        /* RGB sliders */
        int sl_y = wy+TITLEBAR_H+132;
        static const char *rgb_lbl[]  = {"R","G","B","A"};
        static const int   rgb_vals[] = {0, 122, 255, 255};
        static const uint32_t rgb_col[] = {RGB(220,60,60), RGB(60,180,60), RGB(60,60,220), RGB(160,160,168)};
        int si2;
        for (si2=0; si2<4; si2++) {
            int ly = sl_y + si2*20;
            vga_draw_string_trans(wx+4,   ly+2, rgb_lbl[si2], cp_sub);
            int sbar_x=wx+18, sbar_w=ww-50, sbar_h=8;
            vga_fill_rect(sbar_x, ly+3, sbar_w, sbar_h, g_pref_darkmode?RGB(55,55,60):RGB(210,210,215));
            gui_draw_rounded_rect_outline(sbar_x, ly+3, sbar_w, sbar_h, 2, cp_sep);
            int fill_w = rgb_vals[si2]*sbar_w/255;
            if (fill_w>0) vga_fill_rect(sbar_x, ly+3, fill_w, sbar_h, rgb_col[si2]);
            /* Thumb */
            int thx = sbar_x+fill_w;
            gui_draw_circle(thx, ly+7, 6, RGB(255,255,255));
            gui_draw_circle_outline(thx, ly+7, 6, cp_sep);
            /* Value */
            char vbuf[5]; int vi=0; int vv=rgb_vals[si2];
            if(vv>=100){vbuf[vi++]='0'+vv/100;vv%=100;vbuf[vi++]='0'+vv/10;}
            else if(vv>=10){vbuf[vi++]=' ';vbuf[vi++]='0'+vv/10;}
            else{vbuf[vi++]=' ';vbuf[vi++]=' ';}
            vbuf[vi++]='0'+vv%10; vbuf[vi]=0;
            vga_draw_string_trans(wx+ww-30, ly+2, vbuf, cp_txt);
        }
        /* Recent colors swatches */
        int rcy = wy+wh-38;
        vga_draw_hline(wx+4, rcy-4, ww-8, cp_sep);
        vga_draw_string_trans(wx+4, rcy-13, "Recent:", cp_sub);
        static const uint32_t recent_cols[] = {
            RGB(255,59,48), RGB(255,149,0), RGB(255,204,0), RGB(52,199,89),
            RGB(0,199,190), RGB(0,122,255), RGB(88,86,214), RGB(255,45,85)
        };
        int rc2;
        for (rc2=0; rc2<8; rc2++) {
            int rx2 = wx+4+rc2*22;
            vga_fill_rect(rx2, rcy, 18, 18, recent_cols[rc2]);
            gui_draw_rounded_rect_outline(rx2, rcy, 18, 18, 2, cp_sep);
        }
        return 1;
    }

    /* Preview window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Preview")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t pv_bg  = g_pref_darkmode ? RGB(28,28,30)    : RGB(248,248,252);
        uint32_t pv_tb  = g_pref_darkmode ? RGB(44,44,48)    : RGB(232,232,236);
        uint32_t pv_sep = g_pref_darkmode ? RGB(60,60,65)    : RGB(200,200,205);
        uint32_t pv_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,40);
        uint32_t pv_sub = g_pref_darkmode ? RGB(120,120,128) : RGB(100,100,110);
        uint32_t pv_acc = RGB(0,122,255);
        uint32_t pv_pg  = g_pref_darkmode ? RGB(50,50,55)    : RGB(180,180,185);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, pv_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 26, pv_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+27, ww-2, pv_sep);
        /* Toolbar buttons */
        static const char *pv_btns[] = { "<", ">", "Zoom-", "Zoom+", "Fit", "Markup", "Share" };
        int pb_x = wx+4;
        int pb;
        for (pb=0; pb<7; pb++) {
            int pbw = (int)(str_len(pv_btns[pb])*8+6);
            vga_fill_rect(pb_x, wy+TITLEBAR_H+5, pbw, 16, g_pref_darkmode?RGB(55,55,60):RGB(215,215,220));
            gui_draw_rounded_rect_outline(pb_x, wy+TITLEBAR_H+5, pbw, 16, 2, pv_sep);
            vga_draw_string_trans(pb_x+3, wy+TITLEBAR_H+8, pv_btns[pb], pv_txt);
            pb_x += pbw+3;
            if (pb==1 || pb==4) pb_x += 4;
        }
        /* Sidebar (thumbnail strip) */
        int sb_w = 56;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+28, sb_w, wh-TITLEBAR_H-48, pv_pg);
        vga_draw_vline(wx+1+sb_w, wy+TITLEBAR_H+28, wh-TITLEBAR_H-48, pv_sep);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+40, "No", pv_sub);
        vga_draw_string_trans(wx+8, wy+TITLEBAR_H+54, "pages", pv_sub);
        /* Main content area - document page */
        int doc_x = wx+1+sb_w+1;
        int doc_w = ww-sb_w-2;
        vga_fill_rect(doc_x, wy+TITLEBAR_H+28, doc_w, wh-TITLEBAR_H-48, pv_pg);
        /* Empty document area */
        int page_m = 10;
        int page_x = doc_x+page_m, page_y=wy+TITLEBAR_H+38;
        int page_w = doc_w-page_m*2, page_h = wh-TITLEBAR_H-68;
        vga_fill_rect(page_x+3, page_y+3, page_w, page_h, g_pref_darkmode?RGB(20,20,22):RGB(140,140,145));
        vga_fill_rect(page_x, page_y, page_w, page_h, g_pref_darkmode?RGB(55,55,60):RGB(255,255,255));
        gui_draw_rounded_rect_outline(page_x, page_y, page_w, page_h, 1, pv_sep);
        int cx2 = page_x+10;
        vga_draw_string_trans(cx2, page_y+10, "Preview", pv_txt);
        vga_draw_hline(cx2, page_y+22, page_w-20, pv_sep);
        vga_draw_string_trans(cx2, page_y+46, "No document open", pv_sub);
        vga_draw_string_trans(cx2, page_y+62, "No document selected", pv_sub);
        /* Status bar */
        vga_fill_rect(wx+1, wy+wh-20, ww-2, 18, pv_tb);
        vga_draw_hline(wx+1, wy+wh-20, ww-2, pv_sep);
        vga_draw_string_trans(wx+6, wy+wh-14, "No document", pv_sub);
        { char zoom_buf[8];
          runtime_format_percent(100, zoom_buf, sizeof(zoom_buf));
          int zl=str_len(zoom_buf)*8; vga_draw_string_trans(wx+ww-zl-6, wy+wh-14, zoom_buf, pv_acc); }
        return 1;
    }

    /* Script Editor window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Script Editor")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        uint32_t se_bg  = g_pref_darkmode ? RGB(20,22,26)    : RGB(248,252,248);
        uint32_t se_tb  = g_pref_darkmode ? RGB(36,38,44)    : RGB(232,236,232);
        uint32_t se_sep = g_pref_darkmode ? RGB(55,60,65)    : RGB(195,205,195);
        uint32_t se_txt = g_pref_darkmode ? RGB(198,209,195) : RGB(30,50,30);
        uint32_t se_kw  = g_pref_darkmode ? RGB(249,158, 25) : RGB(170,80,0);
        uint32_t se_str = g_pref_darkmode ? RGB(252,106,  93): RGB(196,26,22);
        uint32_t se_cmt = g_pref_darkmode ? RGB(112,128, 100): RGB(100,120,100);
        uint32_t se_num = g_pref_darkmode ? RGB(147,191,255) : RGB(28,0,207);
        uint32_t se_acc = RGB(0,122,255);
        uint32_t se_gutter = g_pref_darkmode ? RGB(30,32,38) : RGB(240,244,240);
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, se_bg);
        /* Toolbar */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, 24, se_tb);
        vga_draw_hline(wx+1, wy+TITLEBAR_H+25, ww-2, se_sep);
        /* Run/Stop buttons */
        vga_fill_rect(wx+4,  wy+TITLEBAR_H+5, 32, 14, RGB(52,199,89));
        gui_draw_rounded_rect_outline(wx+4, wy+TITLEBAR_H+5, 32, 14, 3, RGB(30,160,60));
        vga_draw_string_trans(wx+6,  wy+TITLEBAR_H+8, "Run", RGB(255,255,255));
        vga_fill_rect(wx+40, wy+TITLEBAR_H+5, 32, 14, RGB(255,59,48));
        gui_draw_rounded_rect_outline(wx+40, wy+TITLEBAR_H+5, 32, 14, 3, RGB(200,30,20));
        vga_draw_string_trans(wx+42, wy+TITLEBAR_H+8, "Stop", RGB(255,255,255));
        /* Script name */
        vga_draw_string_trans(wx+80, wy+TITLEBAR_H+8, "Untitled Script.scpt", se_txt);
        /* Gutter */
        int gutter_w = 22;
        vga_fill_rect(wx+1, wy+TITLEBAR_H+26, gutter_w, wh-TITLEBAR_H-60, se_gutter);
        vga_draw_vline(wx+1+gutter_w, wy+TITLEBAR_H+26, wh-TITLEBAR_H-60, se_sep);
        /* Code content */
        int code_x = wx+1+gutter_w+4;
        int code_y = wy+TITLEBAR_H+30;
        int line_h = 13;
        /* Syntax-colored code lines */
        /* Use "\n" as end-of-line sentinel (col=0, tok="\n") and NULL tok to terminate */
        static const char se_eol[] = "\n";
        struct { const char *tok; uint32_t col; } code_toks[] = {
            /* line 1: tell application "Finder" */
            { "tell ",      se_kw  }, { "application ",  se_txt },
            { "\"Finder\"", se_str }, { se_eol, 0 },
            /* line 2:   set x to 42 */
            { "  set ",     se_kw  }, { "x ",           se_txt  },
            { "to ",        se_kw  }, { "42",           se_num  }, { se_eol, 0 },
            /* line 3:   display dialog x */
            { "  display ", se_kw  }, { "dialog ",      se_txt  },
            { "x",          se_txt }, { se_eol, 0 },
            /* line 4: end tell */
            { "end ",       se_kw  }, { "tell",         se_kw   }, { se_eol, 0 },
            /* line 5: -- auto-run */
            { "-- auto-run result:", se_cmt }, { se_eol, 0 },
            /* terminator */
            { NULL, 0 },
        };
        int cl=0, tx_x=code_x, ty=code_y, ln=1;
        /* Line number */
        char lnbuf[4]; lnbuf[0]='0'+ln; lnbuf[1]=0;
        vga_draw_string_trans(wx+3, ty+2, lnbuf, se_cmt);
        while (code_toks[cl].tok) {
            if (code_toks[cl].col == 0) {
                /* End of line (se_eol marker) */
                ty += line_h; tx_x = code_x; ln++;
                if (ty+line_h > wy+wh-36) break;
                lnbuf[0]='0'+ln; lnbuf[1]=0;
                vga_draw_string_trans(wx+3, ty+2, lnbuf, se_cmt);
                cl++;
                continue;
            }
            vga_draw_string_trans(tx_x, ty+2, code_toks[cl].tok, code_toks[cl].col);
            tx_x += (int)(str_len(code_toks[cl].tok)*8);
            cl++;
        }
        /* Result pane */
        int res_y = wy+wh-38;
        vga_draw_hline(wx+1, res_y, ww-2, se_sep);
        vga_fill_rect(wx+1, res_y+1, ww-2, 36, se_gutter);
        vga_draw_string_trans(wx+4, res_y+4, "Result:", se_cmt);
        vga_draw_string_trans(wx+52, res_y+4, "42", se_acc);
        /* Status bar */
        vga_fill_rect(wx+1, wy+wh-20, ww-2, 18, se_tb);
        vga_draw_hline(wx+1, wy+wh-20, ww-2, se_sep);
        vga_draw_string_trans(wx+4, wy+wh-14, "AppleScript  Ln 1, Col 1", se_cmt);
        return 1;
    }


    return 0;
}
