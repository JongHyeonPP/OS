#include "gui_internal.h"

static void apps1_append_text(char *buf, int *pos, int max, const char *text) {
    int i = 0;
    if (!buf || max <= 0 || !text) return;
    while (text[i] && *pos + 1 < max) buf[(*pos)++] = text[i++];
    buf[*pos] = 0;
}

static void apps1_append_uint(char *buf, int *pos, int max, uint32_t value) {
    char nbuf[12];
    int i = 0;
    runtime_format_uint(value, nbuf, sizeof(nbuf));
    while (nbuf[i] && *pos + 1 < max) buf[(*pos)++] = nbuf[i++];
    buf[*pos] = 0;
}

static void apps1_append_bytes(char *buf, int *pos, int max, uint32_t bytes) {
    char bbuf[16];
    int i = 0;
    runtime_format_bytes(bytes, bbuf, sizeof(bbuf));
    while (bbuf[i] && *pos + 1 < max) buf[(*pos)++] = bbuf[i++];
    buf[*pos] = 0;
}

static void apps1_format_display(const runtime_system_info_t *sys, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0 || !sys) return;
    buf[0] = 0;
    apps1_append_uint(buf, &pos, max, sys->display_width);
    apps1_append_text(buf, &pos, max, "x");
    apps1_append_uint(buf, &pos, max, sys->display_height);
    apps1_append_text(buf, &pos, max, " ");
    apps1_append_uint(buf, &pos, max, sys->display_bpp);
    apps1_append_text(buf, &pos, max, "bpp ");
    apps1_append_text(buf, &pos, max, sys->display);
}

static void apps1_format_used_total(uint32_t used, uint32_t total, char *buf, int max) {
    int pos = 0;
    if (!buf || max <= 0) return;
    buf[0] = 0;
    apps1_append_bytes(buf, &pos, max, used);
    apps1_append_text(buf, &pos, max, " used / ");
    apps1_append_bytes(buf, &pos, max, total);
}

int draw_apps_group1(int idx) {
    if (idx < 0 || idx >= g_num_windows) return 0;

    /* MyOS Finder window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "MyOS Finder")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy + TITLEBAR_H;
        int tool_h = 30, status_h = 18;
        int sb_w = (ww > 320) ? 120 : ww/3;
        uint32_t fn_bg  = g_pref_darkmode ? RGB(28,28,32)    : RGB(248,248,248);
        uint32_t fn_sb  = g_pref_darkmode ? RGB(36,36,42)    : RGB(236,236,244);
        uint32_t fn_tb  = g_pref_darkmode ? RGB(44,44,52)    : RGB(222,222,230);
        uint32_t fn_sep = g_pref_darkmode ? RGB(60,60,70)    : RGB(198,198,210);
        uint32_t fn_txt = g_pref_darkmode ? RGB(210,210,220) : RGB(28,28,38);
        uint32_t fn_sub = g_pref_darkmode ? RGB(128,128,140) : RGB(100,100,110);
        uint32_t fn_hdr = g_pref_darkmode ? RGB(80,80,92)    : RGB(160,160,172);
        /* Background */
        vga_fill_rect(wx+1, cy+1, ww-2, wh-TITLEBAR_H-2, fn_bg);
        /* === Toolbar === */
        vga_fill_rect(wx+1, cy+1, ww-2, tool_h, fn_tb);
        vga_draw_hline(wx+1, cy+tool_h, ww-2, fn_sep);
        /* Back button */
        { int bx=wx+8, by=cy+8, bw=16, bh=14;
          vga_fill_rect(bx, by, bw, bh, g_pref_darkmode?RGB(58,58,68):RGB(208,208,218));
          gui_draw_rounded_rect_outline(bx, by, bw, bh, 3, fn_sep);
          vga_draw_line(bx+10, by+3, bx+6, by+7, fn_txt);
          vga_draw_line(bx+6, by+7, bx+10, by+11, fn_txt);
        }
        /* Forward button */
        { int bx=wx+28, by=cy+8, bw=16, bh=14;
          vga_fill_rect(bx, by, bw, bh, g_pref_darkmode?RGB(58,58,68):RGB(208,208,218));
          gui_draw_rounded_rect_outline(bx, by, bw, bh, 3, fn_sep);
          vga_draw_line(bx+5, by+3, bx+9, by+7, fn_txt);
          vga_draw_line(bx+9, by+7, bx+5, by+11, fn_txt);
        }
        /* View mode buttons (icon/list/column/gallery) */
        { int vx=wx+56, vy=cy+7, bw=15, bh=16, vi;
          for (vi=0;vi<4;vi++) {
              int sel=(vi==0);
              vga_fill_rect(vx+vi*bw, vy, bw, bh, sel?(g_pref_darkmode?RGB(78,78,98):RGB(190,190,210)):fn_tb);
              if (vi==0) {
                  vga_fill_rect(vx+3, vy+4, 3,3, fn_txt); vga_fill_rect(vx+9, vy+4, 3,3, fn_txt);
                  vga_fill_rect(vx+3, vy+10, 3,3, fn_txt); vga_fill_rect(vx+9, vy+10, 3,3, fn_txt);
              } else if (vi==1) {
                  vga_draw_hline(vx+vi*bw+2, vy+5, 11, fn_txt);
                  vga_draw_hline(vx+vi*bw+2, vy+9, 11, fn_txt);
                  vga_draw_hline(vx+vi*bw+2, vy+13, 11, fn_txt);
              } else if (vi==2) {
                  vga_draw_vline(vx+vi*bw+6, vy+4, 8, fn_txt);
                  vga_draw_vline(vx+vi*bw+9, vy+4, 8, fn_txt);
              } else {
                  vga_fill_rect(vx+vi*bw+2, vy+4, 11, 8, fn_txt);
                  vga_fill_rect(vx+vi*bw+2, vy+13, 5, 3, fn_txt);
                  vga_fill_rect(vx+vi*bw+8, vy+13, 5, 3, fn_txt);
              }
          }
          vga_draw_rect_outline(vx, vy, bw*4, bh, fn_sep);
          vga_draw_vline(vx+bw, vy, bh, fn_sep); vga_draw_vline(vx+bw*2, vy, bh, fn_sep); vga_draw_vline(vx+bw*3, vy, bh, fn_sep);
        }
        /* Breadcrumb path */
        { int px=wx+56+62, py=cy+11;
          vga_draw_string_trans(px, py, "MyOS", fn_sub); px+=38;
          if (g_finder_depth>0) {
              vga_draw_string_trans(px-6, py, ">", fn_sub);
              vga_draw_string_trans(px, py, g_finder_stack[0], fn_txt);
              if (g_finder_depth>1) {
                  px += str_len(g_finder_stack[0])*8+8;
                  vga_draw_string_trans(px-6, py, ">", fn_sub);
                  vga_draw_string_trans(px, py, g_finder_stack[1], fn_txt);
              }
          } else { vga_draw_string_trans(px, py, "Home", fn_txt); }
        }
        /* Search icon at right */
        { int bx=wx+ww-26, by=cy+8;
          vga_fill_rect(bx, by, 18, 14, g_pref_darkmode?RGB(58,58,68):RGB(208,208,218));
          gui_draw_rounded_rect_outline(bx, by, 18, 14, 3, fn_sep);
          gui_draw_circle(bx+7, by+6, 4, fn_sub);
          gui_draw_circle(bx+7, by+6, 2, g_pref_darkmode?RGB(58,58,68):RGB(208,208,218));
          vga_draw_line(bx+10, by+9, bx+13, by+12, fn_sub);
        }
        /* === Sidebar === */
        int cont_y = cy+tool_h+1;
        int cont_h2 = wh-TITLEBAR_H-tool_h-status_h;
        vga_fill_rect(wx+1, cont_y, sb_w, cont_h2, fn_sb);
        vga_draw_vline(wx+sb_w+1, cont_y, cont_h2, fn_sep);
        { int sx=wx+8, sy=cont_y+8;
          vga_draw_string_trans(sx, sy, "FAVORITES", fn_hdr);
          sy += 16;
          static const struct { const char *name; uint32_t col; } sb_favs[9] = {
              { "Recents",      RGB(150,150,155) },
              { "iCloud Drive", RGB(0,  122,255) },
              { "Applications", RGB( 80,180, 80) },
              { "Desktop",      RGB( 30,110,200) },
              { "Documents",    RGB( 80,160,240) },
              { "Downloads",    RGB( 50,200, 80) },
              { "Movies",       RGB(220, 50, 50) },
              { "Music",        RGB(252, 60, 68) },
              { "Pictures",     RGB(200, 80,200) }
          };
          int fi2;
          for (fi2=0; fi2<9 && sy+14<=cont_y+cont_h2-4; fi2++) {
              int sel2=(g_finder_depth>0 && fi2>=2 && str_eq(g_finder_stack[g_finder_depth-1], sb_favs[fi2].name));
              if (sel2) vga_fill_rect(wx+2, sy-2, sb_w-2, 16, RGB(0,99,216));
              gui_draw_circle(sx+4, sy+5, 4, sb_favs[fi2].col);
              vga_draw_string_trans(sx+12, sy+1, sb_favs[fi2].name, sel2?RGB(255,255,255):fn_txt);
              sy += 16;
          }
          if (sy+28<=cont_y+cont_h2-4) {
              sy += 4;
              vga_draw_string_trans(sx, sy, "TAGS", fn_hdr); sy+=14;
              static const struct { const char *n; uint32_t c; } tags[4] = {
                  {"Red",RGB(200,50,50)},{"Orange",RGB(255,140,0)},
                  {"Yellow",RGB(220,200,0)},{"Green",RGB(50,180,50)}
              };
              int ti2;
              for (ti2=0; ti2<4 && sy+14<=cont_y+cont_h2-4; ti2++) {
                  gui_draw_circle(sx+4, sy+5, 4, tags[ti2].c);
                  vga_draw_string_trans(sx+12, sy+1, tags[ti2].n, fn_txt);
                  sy += 14;
              }
          }
        }
        /* === Content area: folder icons === */
        { int cx2=wx+sb_w+4, cw=ww-sb_w-6;
          int cy2=cont_y+6, ch2=cont_h2-10;
          int count4=0;
          const folder_icon_t *fols = finder_current_folders(&count4);
          int cell_w=72, cell_h=56;
          int cols2 = cw/cell_w; if (cols2<1) cols2=1;
          int fi3;
          for (fi3=0; fi3<count4; fi3++) {
              int col3=fi3%cols2, row3=fi3/cols2;
              int fx=cx2+col3*cell_w+(cell_w-28)/2;
              int fy=cy2+row3*cell_h;
              if (fy+cell_h > cy2+ch2) break;
              draw_folder(fx, fy, cell_w, &fols[fi3]);
          }
        }
        /* === Status bar === */
        { int sty=wy+wh-status_h-1;
          runtime_storage_info_t storage;
          char itembuf[24];
          char freebuf[32];
          int item_count = 0;
          int ipos = 0;
          int fpos=0;
          vga_fill_rect(wx+1, sty, ww-2, status_h, fn_tb);
          vga_draw_hline(wx+1, sty, ww-2, fn_sep);
          (void)finder_current_folders(&item_count);
          itembuf[0] = 0;
          apps1_append_uint(itembuf, &ipos, sizeof(itembuf), (uint32_t)item_count);
          apps1_append_text(itembuf, &ipos, sizeof(itembuf), item_count == 1 ? " item" : " items");
          vga_draw_string_trans(wx+8, sty+5, itembuf, fn_sub);
          if (runtime_get_storage_info("/", &storage) == 0) {
              freebuf[0] = 0;
              apps1_append_bytes(freebuf, &fpos, sizeof(freebuf), storage.free_bytes);
              apps1_append_text(freebuf, &fpos, sizeof(freebuf), " free");
              vga_draw_string_trans(wx+ww-96, sty+5, freebuf, fn_sub);
          }
        }
        return 1;
    }

    /* Terminal window — identified by title, not index (index changes with z-order) */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Terminal")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx = win->x + 4;
        int wy = win->y + TITLEBAR_H + 4;
        int i;
        uint32_t term_bg = RGB(20, 20, 20);
        uint32_t term_fg = RGB(0, 255, 0);

        /* Dark background for content area */
        vga_fill_rect(win->x+1, win->y+TITLEBAR_H+1,
                      win->w-2, win->h - TITLEBAR_H - 2, term_bg);

        /* History lines with scroll support */
        {
            /* Compute visible range from scroll offset */
            int total = term_num_lines;
            int start_line = total - TERM_LINES - g_term_scroll;
            if (start_line < 0) start_line = 0;
            for (i = 0; i < TERM_LINES; i++) {
                int li = start_line + i;
                if (li < total)
                    vga_draw_string_trans(wx, wy + i*10, term_history[li], term_fg);
            }
        }

        /* Prompt + input (only at bottom when not scrolled) */
        {
            int py = wy + TERM_LINES * 10 + 4;
            if (g_term_scroll == 0) {
                vga_draw_string_trans(wx, py, "MyOS> ", term_fg);
                vga_draw_string_trans(wx + 6*8, py, term_input, term_fg);
                /* Blinking cursor: toggle every 500ms */
                uint32_t t = timer_ticks();
                if ((t / 500) % 2 == 0) {
                    int cx = wx + 6*8 + term_input_len * 8;
                    vga_fill_rect(cx, py, 6, 8, term_fg);
                }
            } else {
                /* Scroll indicator */
                char sc_buf[20]; int si3=0;
                sc_buf[si3++]='[';
                sc_buf[si3++]='0'+g_term_scroll/10;
                sc_buf[si3++]='0'+g_term_scroll%10;
                sc_buf[si3++]=' ';sc_buf[si3++]='l';sc_buf[si3++]='i';
                sc_buf[si3++]='n';sc_buf[si3++]='e';sc_buf[si3++]='s';
                sc_buf[si3++]=' ';sc_buf[si3++]='a';sc_buf[si3++]='b';
                sc_buf[si3++]='o';sc_buf[si3++]='v';sc_buf[si3++]='e';
                sc_buf[si3++]=']';sc_buf[si3]=0;
                vga_draw_string_trans(wx, py, sc_buf, RGB(100,200,100));
            }
        }

        /* Scrollbar indicator on right edge */
        if (term_num_lines > TERM_LINES) {
            int sbh = (win->h - TITLEBAR_H - 20);
            int sby = win->y + TITLEBAR_H + 1;
            vga_fill_rect(win->x+win->w-5, sby, 4, sbh, RGB(40,40,40));
            int thumb_h = sbh * TERM_LINES / term_num_lines;
            if (thumb_h < 8) thumb_h = 8;
            int max_scroll = term_num_lines - TERM_LINES;
            int thumb_y = sby + (sbh - thumb_h) * (max_scroll - g_term_scroll) / max_scroll;
            vga_fill_rect(win->x+win->w-5, thumb_y, 4, thumb_h, RGB(100,100,100));
        }
        return 1;
    }

    /* Settings window — sidebar layout */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Settings")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx2 = win->x, wy2 = win->y, ww2 = win->w, wh2 = win->h;
        int cy0 = wy2 + TITLEBAR_H + 1;
        int content_h = wh2 - TITLEBAR_H - 19;

        uint32_t lbl  = g_pref_darkmode ? RGB(220,220,224) : RGB(40,40,40);
        uint32_t sub  = g_pref_darkmode ? RGB(140,140,148) : RGB(110,110,110);
        uint32_t sep2 = g_pref_darkmode ? RGB(60,60,65)    : RGB(220,220,220);
        uint32_t cat2 = g_pref_darkmode ? RGB(110,110,118) : RGB(120,120,120);
        uint32_t sb_bg = g_pref_darkmode ? RGB(32,32,38)   : RGB(238,238,244);
        uint32_t sb_sel= g_pref_darkmode ? RGB(55,55,66)   : RGB(210,218,240);
        uint32_t ct_bg = g_pref_darkmode ? RGB(26,26,30)   : RGB(248,248,252);

        int sb_w = 130;
        vga_fill_rect(wx2+1, cy0, sb_w, content_h, sb_bg);
        vga_draw_vline(wx2+sb_w, cy0, content_h, sep2);
        vga_fill_rect(wx2+sb_w+1, cy0, ww2-sb_w-2, content_h, ct_bg);

        /* Apple ID block */
        int aid_y = cy0 + 5;
        gui_draw_circle(wx2+18, aid_y+15, 14, RGB(0,100,220));
        gui_draw_circle(wx2+18, aid_y+10, 6,  RGB(220,235,255));
        vga_fill_rect(wx2+8,  aid_y+18, 20, 10, RGB(0,100,220));
        vga_fill_rect(wx2+9,  aid_y+19, 18,  8, RGB(180,210,255));
        vga_draw_string_trans(wx2+37, aid_y+6,  "MyOS User", lbl);
        vga_draw_string_trans(wx2+37, aid_y+18, "Apple ID", cat2);
        vga_draw_hline(wx2+4, aid_y+32, sb_w-8, sep2);

        static const char *sb_names2[] = {
            "General","Appearance","Display",
            "Notifications","Sound","Focus",
            "Network","Bluetooth","Battery",
            "Privacy","iCloud","Screen Time",
            "Keyboard","Accessibility"
        };
        static const uint32_t sb_cols2[] = {
            RGB(142,142,147), RGB(0,122,255),  RGB(0,149,255),
            RGB(255,149,0),   RGB(255,59,48),  RGB(100,80,220),
            RGB(0,176,240),   RGB(0,122,255),  RGB(52,199,89),
            RGB(52,199,89),   RGB(0,122,255),  RGB(252,60,68),
            RGB(100,100,105), RGB(0,122,255)
        };
        int i_sb;
        for (i_sb = 0; i_sb < 14; i_sb++) {
            int iy = cy0 + 44 + i_sb * 20;
            if (i_sb == g_settings_tab) {
                vga_fill_rect(wx2+3, iy, sb_w-5, 18, sb_sel);
                vga_draw_rect_outline(wx2+3, iy, sb_w-5, 18,
                    g_pref_darkmode?RGB(70,70,88):RGB(190,198,225));
            }
            gui_draw_circle(wx2+12, iy+9, 5, sb_cols2[i_sb]);
            vga_draw_string_trans(wx2+22, iy+5, sb_names2[i_sb],
                i_sb == g_settings_tab ? (g_pref_darkmode?RGB(240,240,250):RGB(15,15,15)) : lbl);
        }

        int cx = wx2 + sb_w + 10;
        int cy = cy0 + 10;
        int rw = ww2 - sb_w - 22;
        int tx_r = wx2 + ww2 - 54;

        if (g_settings_tab == 0) {
            /* General */
            runtime_system_info_t sys;
            char membuf[16];
            runtime_get_system_info(&sys);
            runtime_format_bytes(sys.pmm_total_bytes, membuf, sizeof(membuf));
            vga_draw_string_trans(cx, cy, "GENERAL", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "OS Version:", lbl);
            vga_draw_string_trans(cx+88, cy+18, sys.release, sub);
            vga_draw_string_trans(cx, cy+32, "Chip:", lbl);
            vga_draw_string_trans(cx+40, cy+32, sys.cpu_model, sub);
            vga_draw_string_trans(cx, cy+46, "Memory:", lbl);
            vga_draw_string_trans(cx+60, cy+46, membuf, sub);
            vga_draw_hline(cx, cy+60, rw, sep2);
            vga_draw_string_trans(cx, cy+66, "SOFTWARE UPDATE", cat2);
            vga_draw_string_trans(cx, cy+82, "Your system is up to date.", sub);
            vga_draw_string_trans(cx, cy+96, sys.release, RGB(52,199,89));
            vga_draw_hline(cx, cy+110, rw, sep2);
            vga_draw_string_trans(cx, cy+116, "WALLPAPER", cat2);
            {
                static const uint32_t wp_a[5]={RGB(70,130,190),RGB(255,140,60),RGB(40,100,60),RGB(5,5,30),RGB(255,180,60)};
                static const uint32_t wp_b[5]={RGB(20,50,120),RGB(100,20,80),RGB(10,40,20),RGB(20,0,60),RGB(40,20,5)};
                static const char *wp_n[5] = {"Blue","Sunset","Forest","Space","Sequoia"};
                int wi;
                for (wi=0;wi<5;wi++) {
                    int wpx=cx+(wi<4?wi*74:0), wpy=cy+(wi<4?132:170);
                    if (wi<4) {
                        vga_fill_rect(wpx, wpy, 60, 28, wp_a[wi]);
                        vga_fill_rect(wpx, wpy+14, 60, 14, wp_b[wi]);
                        if (g_pref_wallpaper==wi) vga_draw_rect_outline(wpx-1,wpy-1,62,30,RGB(0,122,255));
                        vga_draw_string_trans(wpx+2, wpy+30, wp_n[wi], sub);
                    } else {
                        /* Sequoia thumbnail */
                        vga_fill_rect(cx, wpy, 60, 28, wp_a[4]);
                        vga_fill_rect(cx, wpy+16, 60, 12, wp_b[4]);
                        /* Mini tree silhouettes */
                        int txi; for(txi=0;txi<4;txi++){int tx3=cx+8+txi*14;
                            vga_fill_rect(tx3, wpy+8-txi%2*4, 4, 20+txi%2*4, RGB(20,10,2));}
                        if (g_pref_wallpaper==4) vga_draw_rect_outline(cx-1,wpy-1,62,30,RGB(0,122,255));
                        vga_draw_string_trans(cx+2, wpy+30, "Sequoia", sub);
                    }
                }
            }
            vga_draw_hline(cx, cy+212, rw, sep2);
            vga_draw_string_trans(cx, cy+218, "CONNECTIVITY", cat2);
            vga_draw_string_trans(cx, cy+234, "WiFi", lbl);
            draw_toggle(tx_r, cy+231, g_pref_wifi);
            vga_draw_string_trans(cx+40, cy+234, g_pref_wifi?"Connected":"Off",
                g_pref_wifi?RGB(52,199,89):sub);
            vga_draw_string_trans(cx, cy+254, "Bluetooth", lbl);
            draw_toggle(tx_r, cy+251, g_pref_bt);
            vga_draw_string_trans(cx+74, cy+254, g_pref_bt?"On":"Off",
                g_pref_bt?RGB(52,199,89):sub);
            {
                uint32_t up=timer_ticks()/1000;
                uint32_t hh=up/3600, mm=(up/60)%60, ss=up%60;
                char buf[20];
                buf[0]='U';buf[1]='p';buf[2]=':';buf[3]=' ';
                buf[4]='0'+hh/10;buf[5]='0'+hh%10;buf[6]=':';
                buf[7]='0'+mm/10;buf[8]='0'+mm%10;buf[9]=':';
                buf[10]='0'+ss/10;buf[11]='0'+ss%10;buf[12]=0;
                vga_draw_hline(cx, cy+270, rw, sep2);
                vga_draw_string_trans(cx, cy+276, buf, cat2);
            }
        } else if (g_settings_tab == 1) {
            /* Appearance */
            vga_draw_string_trans(cx, cy, "APPEARANCE", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Appearance:", lbl);
            {
                uint32_t lm_c = !g_pref_darkmode?RGB(0,122,255):(g_pref_darkmode?RGB(50,50,60):RGB(228,228,233));
                uint32_t dm_c =  g_pref_darkmode?RGB(0,122,255):(g_pref_darkmode?RGB(50,50,60):RGB(228,228,233));
                vga_fill_rect(cx+100,cy+14,60,22,lm_c);
                vga_draw_rect_outline(cx+100,cy+14,60,22,sep2);
                vga_draw_string_trans(cx+114,cy+21,"Light",!g_pref_darkmode?RGB(255,255,255):lbl);
                vga_fill_rect(cx+166,cy+14,52,22,dm_c);
                vga_draw_rect_outline(cx+166,cy+14,52,22,sep2);
                vga_draw_string_trans(cx+176,cy+21,"Dark",g_pref_darkmode?RGB(255,255,255):lbl);
            }
            vga_draw_string_trans(cx, cy+44, "Dark Mode", lbl);
            draw_toggle(tx_r, cy+41, g_pref_darkmode);
            vga_draw_string_trans(cx, cy+64, "Transparency", lbl);
            draw_toggle(tx_r, cy+61, 1);
            vga_draw_string_trans(cx, cy+84, "Reduce Motion", lbl);
            draw_toggle(tx_r, cy+81, 0);
            vga_draw_hline(cx, cy+100, rw, sep2);
            vga_draw_string_trans(cx, cy+106, "ACCENT COLOR", cat2);
            {
                static const uint32_t accs2[7]={
                    RGB(0,122,255),RGB(255,59,48),RGB(52,199,89),
                    RGB(255,149,0),RGB(142,68,173),RGB(255,45,85),RGB(100,100,100)};
                static const char *acc_n2[7]={"Blue","Red","Green","Orange","Purple","Pink","Graphite"};
                int ai;
                for (ai=0;ai<7;ai++) {
                    gui_draw_circle(cx+ai*24+12, cy+126, 10, accs2[ai]);
                    if (ai==0)
                        gui_draw_circle_outline(cx+ai*24+12, cy+126, 11,
                            g_pref_darkmode?RGB(240,240,250):RGB(20,20,20));
                }
                vga_draw_string_trans(cx, cy+140, acc_n2[0], sub);
            }
            vga_draw_hline(cx, cy+152, rw, sep2);
            vga_draw_string_trans(cx, cy+158, "DOCK & MENU BAR", cat2);
            vga_draw_string_trans(cx, cy+174, "Position:", lbl);
            vga_draw_string_trans(cx+72, cy+174, "Bottom", sub);
            vga_draw_string_trans(cx, cy+190, "Size:", lbl);
            vga_fill_rect(cx+42, cy+194, rw-44, 4, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
            vga_fill_rect(cx+42, cy+194, (rw-44)*2/3, 4, RGB(0,122,255));
            gui_draw_circle(cx+42+(rw-44)*2/3, cy+196, 6, RGB(255,255,255));
            gui_draw_circle_outline(cx+42+(rw-44)*2/3, cy+196, 6,
                g_pref_darkmode?RGB(80,80,95):RGB(170,170,180));
            vga_draw_string_trans(cx, cy+210, "Magnification:", lbl);
            draw_toggle(tx_r, cy+207, 1);
            vga_draw_hline(cx, cy+228, rw, sep2);
            vga_draw_string_trans(cx, cy+234, "System Font: SF Pro 8pt", sub);
        } else if (g_settings_tab == 2) {
            /* Display */
            vga_draw_string_trans(cx, cy, "DISPLAY", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Brightness", lbl);
            { int brt=80;
              vga_fill_rect(cx, cy+32, rw, 6, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
              vga_fill_rect(cx, cy+32, rw*brt/100, 6, RGB(255,200,0));
              gui_draw_circle(cx+rw*brt/100, cy+35, 6, RGB(255,255,255));
              gui_draw_circle_outline(cx+rw*brt/100, cy+35, 6, g_pref_darkmode?RGB(90,90,105):RGB(170,170,180));
              char bpct[5]; bpct[0]='0'+brt/10; bpct[1]='0'+brt%10; bpct[2]='%'; bpct[3]=0;
              vga_draw_string_trans(cx+rw+4, cy+29, bpct, sub);
            }
            vga_draw_string_trans(cx, cy+50, "Night Shift", lbl);
            draw_toggle(tx_r, cy+47, 1);
            vga_draw_string_trans(cx, cy+70, "True Tone", lbl);
            draw_toggle(tx_r, cy+67, 1);
            vga_draw_string_trans(cx, cy+90, "Auto-brightness", lbl);
            draw_toggle(tx_r, cy+87, 1);
            vga_draw_hline(cx, cy+106, rw, sep2);
            vga_draw_string_trans(cx, cy+112, "RESOLUTION", cat2);
            { int ri;
              static const char *res_opts[] = {"More Space","Default (Recommended)","Larger Text","Scaled"};
              for (ri=0;ri<4;ri++) {
                  int ry=cy+128+ri*22;
                  if (ri==1) {
                      vga_fill_rect(cx-2, ry-2, rw+2, 18, g_pref_darkmode?RGB(40,40,50):RGB(230,240,255));
                      vga_draw_rect_outline(cx-2, ry-2, rw+2, 18, RGB(0,122,255));
                  }
                  gui_draw_circle(cx+7, ry+7, 5,
                      ri==1?RGB(0,122,255):(g_pref_darkmode?RGB(55,55,65):RGB(200,200,205)));
                  if (ri==1) gui_draw_circle(cx+7, ry+7, 2, RGB(255,255,255));
                  vga_draw_string_trans(cx+18, ry+3, res_opts[ri], ri==1?RGB(0,122,255):lbl);
              }
            }
            vga_draw_hline(cx, cy+222, rw, sep2);
            vga_draw_string_trans(cx, cy+228, "COLOR PROFILE: Display P3", sub);
            vga_draw_string_trans(cx, cy+242, "Refresh Rate: 60 Hz", sub);
        } else if (g_settings_tab == 3) {
            /* Notifications */
            vga_draw_string_trans(cx, cy, "NOTIFICATIONS", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Allow Notifications", lbl);
            draw_toggle(tx_r, cy+15, g_pref_notifs);
            vga_draw_string_trans(cx, cy+38, "Show in Notification Center", lbl);
            draw_toggle(tx_r, cy+35, 1);
            vga_draw_hline(cx, cy+54, rw, sep2);
            vga_draw_string_trans(cx, cy+60, "PER APP", cat2);
            static const char *notif_apps2[] = {"Messages","Mail","Calendar","Reminders","News","Podcasts"};
            static int notif_on2[6] = {1,1,1,0,1,0};
            int ni;
            for (ni=0;ni<6;ni++) {
                int ny=cy+76+ni*24;
                vga_fill_rect(cx-2,ny-2,rw+2,20,g_pref_darkmode?RGB(34,34,40):RGB(252,252,255));
                vga_draw_rect_outline(cx-2,ny-2,rw+2,20,sep2);
                vga_draw_string_trans(cx+2,ny+4,notif_apps2[ni],lbl);
                draw_toggle(tx_r,ny,notif_on2[ni]);
                vga_draw_string_trans(cx+110,ny+4,notif_on2[ni]?"Banners":"Off",
                    notif_on2[ni]?RGB(52,199,89):sub);
            }
            vga_draw_hline(cx, cy+224, rw, sep2);
            vga_draw_string_trans(cx, cy+230, "STYLE", cat2);
            vga_draw_string_trans(cx, cy+246, "Alert Style: Banners", sub);
            vga_draw_string_trans(cx, cy+260, "Show Previews: Always", sub);
        } else if (g_settings_tab == 4) {
            /* Sound */
            vga_draw_string_trans(cx, cy, "SOUND", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "OUTPUT VOLUME", cat2);
            { int vol=75;
              vga_fill_rect(cx, cy+34, rw, 8, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
              vga_fill_rect(cx, cy+34, rw*vol/100, 8, RGB(0,122,255));
              gui_draw_circle(cx+rw*vol/100, cy+38, 7, RGB(255,255,255));
              gui_draw_circle_outline(cx+rw*vol/100, cy+38, 7, g_pref_darkmode?RGB(90,90,105):RGB(170,170,180));
              char vbuf[5]; vbuf[0]='0'+vol/10; vbuf[1]='0'+vol%10; vbuf[2]='%'; vbuf[3]=0;
              vga_draw_string_trans(cx+rw+4, cy+32, vbuf, sub);
            }
            vga_draw_hline(cx, cy+54, rw, sep2);
            vga_draw_string_trans(cx, cy+60, "ALERT SOUNDS", cat2);
            static const char *sounds2[] = {"Ping","Basso","Funk","Glass","Hero","Bottle"};
            int si3;
            for (si3=0;si3<6;si3++) {
                int sx=cx+(si3%3)*86, sy=cy+76+(si3/3)*28;
                uint32_t sc=si3==0?RGB(0,122,255):(g_pref_darkmode?RGB(44,44,52):RGB(230,230,235));
                vga_fill_rect(sx, sy, 82, 24, sc);
                vga_draw_rect_outline(sx, sy, 82, 24, sep2);
                vga_draw_string_trans(sx+6, sy+8, sounds2[si3], si3==0?RGB(255,255,255):lbl);
            }
            vga_draw_hline(cx, cy+142, rw, sep2);
            vga_draw_string_trans(cx, cy+148, "OUTPUT DEVICE", cat2);
            vga_draw_string_trans(cx, cy+164, "Built-in Speakers", sub);
            vga_draw_string_trans(cx, cy+184, "INPUT DEVICE", cat2);
            vga_draw_string_trans(cx, cy+200, "Built-in Microphone", sub);
            vga_draw_string_trans(cx, cy+220, "Input Level:", lbl);
            { uint32_t t3=timer_ticks();
              int lv=(int)(40+(t3/200)%30);
              vga_fill_rect(cx, cy+234, rw, 6, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
              vga_fill_rect(cx, cy+234, rw*lv/100, 6, RGB(52,199,89));
            }
            vga_draw_string_trans(cx, cy+250, "Sound Effects:", lbl);
            draw_toggle(tx_r, cy+247, 1);
        } else if (g_settings_tab == 5) {
            /* Focus */
            vga_draw_string_trans(cx, cy, "FOCUS", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Do Not Disturb", lbl);
            draw_toggle(tx_r, cy+15, g_pref_dnd);
            vga_draw_string_trans(cx, cy+38, "Work", lbl);
            draw_toggle(tx_r, cy+35, 0);
            vga_draw_string_trans(cx, cy+58, "Personal", lbl);
            draw_toggle(tx_r, cy+55, 1);
            vga_draw_string_trans(cx, cy+78, "Sleep", lbl);
            draw_toggle(tx_r, cy+75, 0);
            vga_draw_string_trans(cx, cy+98, "Gaming", lbl);
            draw_toggle(tx_r, cy+95, 0);
            vga_draw_hline(cx, cy+114, rw, sep2);
            vga_draw_string_trans(cx, cy+120, "SCHEDULE", cat2);
            vga_draw_string_trans(cx, cy+136, "Sleep:", lbl);
            vga_draw_string_trans(cx+46, cy+136, "22:00 - 07:00", sub);
            vga_draw_string_trans(cx, cy+152, "Work:", lbl);
            vga_draw_string_trans(cx+42, cy+152, "09:00 - 18:00", sub);
            vga_draw_hline(cx, cy+168, rw, sep2);
            vga_draw_string_trans(cx, cy+174, "ALLOWED APPS", cat2);
            static const char *focus_apps2[] = {"Phone","Messages","FaceTime","Contacts"};
            int fi;
            for (fi=0;fi<4;fi++) {
                int fy=cy+190+fi*22;
                vga_fill_rect(cx, fy, rw, 18, g_pref_darkmode?RGB(38,38,46):RGB(250,250,254));
                vga_draw_rect_outline(cx, fy, rw, 18, sep2);
                vga_draw_string_trans(cx+4, fy+4, focus_apps2[fi], lbl);
                vga_draw_string_trans(tx_r-16, fy+4, "OK", RGB(52,199,89));
            }
        } else if (g_settings_tab == 6) {
            /* Network */
            const netif_t *net = runtime_primary_netif();
            uint32_t gw_ip = 0;
            char ipbuf[18];
            char gwbuf[18];
            char txbuf[12];
            char rxbuf[12];
            char pktbuf[32];
            int pi = 0;
            uint32_t ri;
            for (ri = 0; ri < net_route_count(); ri++) {
                const net_route_t *route = net_route_at(ri);
                if (route && route->gateway) {
                    gw_ip = route->gateway;
                    break;
                }
            }
            runtime_format_ipv4(net ? net->ipv4 : 0, ipbuf, sizeof(ipbuf));
            runtime_format_ipv4(gw_ip, gwbuf, sizeof(gwbuf));
            runtime_format_uint(net ? net->tx_packets : 0, txbuf, sizeof(txbuf));
            runtime_format_uint(net ? net->rx_packets : 0, rxbuf, sizeof(rxbuf));
            pktbuf[0] = 0;
            apps1_append_text(pktbuf, &pi, sizeof(pktbuf), txbuf);
            apps1_append_text(pktbuf, &pi, sizeof(pktbuf), " / ");
            apps1_append_text(pktbuf, &pi, sizeof(pktbuf), rxbuf);
            vga_draw_string_trans(cx, cy, "NETWORK", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Status:", lbl);
            vga_draw_string_trans(cx+50, cy+18, (net && net->up)?"Connected":"Offline",
                (net && net->up)?RGB(52,199,89):RGB(255,59,48));
            vga_draw_hline(cx, cy+32, rw, sep2);
            vga_draw_string_trans(cx, cy+38, "WI-FI", cat2);
            vga_draw_string_trans(cx, cy+54, "Network:", lbl);
            vga_draw_string_trans(cx+72, cy+54, net ? net->name : "none", sub);
            vga_draw_string_trans(cx, cy+68, "IP:", lbl);
            vga_draw_string_trans(cx+24, cy+68, ipbuf, sub);
            vga_draw_string_trans(cx, cy+82, "Gateway:", lbl);
            vga_draw_string_trans(cx+68, cy+82, gwbuf, sub);
            vga_draw_string_trans(cx, cy+96, "DNS:", lbl);
            vga_draw_string_trans(cx+36, cy+96, "not configured", sub);
            vga_draw_string_trans(cx, cy+110, "Signal:", lbl);
            { int si2;
              for (si2=0;si2<5;si2++) {
                  int bh2=4+si2*4;
                  uint32_t bc2=(g_pref_wifi&&si2<=3)?RGB(52,199,89):(g_pref_darkmode?RGB(55,55,65):RGB(205,205,205));
                  vga_fill_rect(cx+62+si2*10, cy+120-bh2, 8, bh2, bc2);
              }
            }
            vga_draw_hline(cx, cy+130, rw, sep2);
            vga_draw_string_trans(cx, cy+136, "STATISTICS", cat2);
            vga_draw_string_trans(cx, cy+152, "TX Packets:", lbl);
            vga_draw_string_trans(cx+88, cy+152, txbuf, sub);
            vga_draw_string_trans(cx, cy+166, "RX Packets:", lbl);
            vga_draw_string_trans(cx+88, cy+166, rxbuf, sub);
            vga_draw_string_trans(cx, cy+180, "Packets:", lbl);
            vga_draw_string_trans(cx+66, cy+180, pktbuf, sub);
            vga_draw_string_trans(cx, cy+194, "ARP Entries:", lbl);
            { char arpbuf[8]; runtime_format_uint(net_arp_count(), arpbuf, sizeof(arpbuf));
              vga_draw_string_trans(cx+92, cy+194, arpbuf, RGB(52,199,89)); }
            vga_draw_hline(cx, cy+208, rw, sep2);
            vga_draw_string_trans(cx, cy+214, "VPN", cat2);
            vga_draw_string_trans(cx, cy+230, "VPN: Not Connected", sub);
            draw_toggle(tx_r, cy+227, 0);
        } else if (g_settings_tab == 7) {
            /* Bluetooth */
            vga_draw_string_trans(cx, cy, "BLUETOOTH", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Bluetooth", lbl);
            draw_toggle(tx_r, cy+15, g_pref_bt);
            vga_draw_string_trans(cx+80, cy+18, g_pref_bt?"On":"Off", g_pref_bt?RGB(52,199,89):sub);
            vga_draw_hline(cx, cy+36, rw, sep2);
            vga_draw_string_trans(cx, cy+42, "MY DEVICES", cat2);
            static const char *bt_devs[] = {"Magic Keyboard","Magic Mouse 2","AirPods Pro","Apple Watch"};
            static const int bt_conn[]   = {1, 1, 1, 0};
            int bi;
            for (bi=0;bi<4;bi++) {
                int by=cy+58+bi*26;
                vga_fill_rect(cx-2,by-2,rw+2,22,g_pref_darkmode?RGB(34,34,40):RGB(252,252,255));
                vga_draw_rect_outline(cx-2,by-2,rw+2,22,sep2);
                gui_draw_circle(cx+7, by+9, 5,
                    bt_conn[bi]?RGB(52,199,89):(g_pref_darkmode?RGB(60,60,70):RGB(200,200,205)));
                vga_draw_string_trans(cx+18, by+6, bt_devs[bi], lbl);
                vga_draw_string_trans(cx+175, by+6, bt_conn[bi]?"Connected":"Not Connected",
                    bt_conn[bi]?RGB(52,199,89):sub);
            }
            vga_draw_hline(cx, cy+168, rw, sep2);
            vga_draw_string_trans(cx, cy+174, "NEARBY DEVICES", cat2);
            vga_fill_rect(cx-2,cy+190,rw+2,22,g_pref_darkmode?RGB(34,34,40):RGB(252,252,255));
            vga_draw_rect_outline(cx-2,cy+190,rw+2,22,sep2);
            gui_draw_circle(cx+7, cy+201, 5, RGB(255,149,0));
            vga_draw_string_trans(cx+18, cy+196, "iPhone 15 Pro", lbl);
            vga_draw_string_trans(cx+155, cy+196, "Not Paired", sub);
            vga_draw_hline(cx, cy+220, rw, sep2);
            vga_draw_string_trans(cx, cy+226, "Discoverable as:", sub);
            vga_draw_string_trans(cx, cy+240, "\"MyOS Computer\"", RGB(0,122,255));
        } else if (g_settings_tab == 8) {
            /* Battery */
            vga_draw_string_trans(cx, cy, "BATTERY", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            { runtime_power_info_t power;
              char bpct[8];
              char timebuf[16];
              char healthbuf[8];
              runtime_get_power_info(&power);
              runtime_format_percent(power.percent, bpct, sizeof(bpct));
              runtime_format_minutes(power.minutes_remaining, timebuf, sizeof(timebuf));
              runtime_format_percent(power.health_percent, healthbuf, sizeof(healthbuf));
              uint32_t bcol2 = power.percent<20?RGB(255,59,48):(power.percent<50?RGB(255,149,0):RGB(52,199,89));
              int bix=cx+rw/2-32, biy=cy+16;
              vga_fill_rect(bix, biy, 64, 26, g_pref_darkmode?RGB(40,40,48):RGB(240,240,245));
              vga_draw_rect_outline(bix, biy, 64, 26, g_pref_darkmode?RGB(80,80,90):RGB(160,160,165));
              vga_fill_rect(bix+64, biy+9, 4, 8, g_pref_darkmode?RGB(80,80,90):RGB(160,160,165));
              vga_fill_rect(bix+2, biy+2, 60*power.percent/100, 22, bcol2);
              vga_draw_string_trans(bix+20, biy+9, bpct, RGB(255,255,255));
              vga_draw_string_trans(cx, cy+52, "Status:", lbl);
              vga_draw_string_trans(cx+64, cy+52, power.status, lbl);
              vga_draw_string_trans(cx, cy+66, "Time Remaining:", lbl);
              vga_draw_string_trans(cx+122, cy+66, timebuf, sub);
              vga_draw_hline(cx, cy+82, rw, sep2);
              vga_draw_string_trans(cx, cy+88, "OPTIONS", cat2);
              vga_draw_string_trans(cx, cy+104, "Low Power Mode", lbl);
              draw_toggle(tx_r, cy+101, 0);
              vga_draw_string_trans(cx, cy+124, "Optimized Charging", lbl);
              draw_toggle(tx_r, cy+121, 1);
              vga_draw_string_trans(cx, cy+144, "Battery Health:", lbl);
              vga_draw_string_trans(cx+110, cy+144, healthbuf, RGB(52,199,89));
              vga_draw_string_trans(cx+148, cy+144, power.health_status, RGB(52,199,89));
              vga_draw_hline(cx, cy+160, rw, sep2);
              vga_draw_string_trans(cx, cy+166, "USAGE (24H)", cat2);
              int ubw=(rw-4)/8;
              int ui;
              for (ui=0;ui<8;ui++) {
                  int ux=cx+ui*ubw, uy=cy+210;
                  int sample = power.history[ui];
                  int uh2=sample*48/100;
                  vga_fill_rect(ux, uy+48-uh2, ubw-2, uh2,
                      sample<30?RGB(255,59,48):(sample<60?RGB(255,149,0):RGB(52,199,89)));
              }
            }
        } else if (g_settings_tab == 9) {
            /* Privacy & Security */
            vga_draw_string_trans(cx, cy, "PRIVACY & SECURITY", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            static const char *priv_items2[] = {"Camera","Microphone","Location","Contacts","Photos","Calendar"};
            static int priv_st2[6] = {1,0,1,1,0,1};
            int pi;
            for (pi=0;pi<6;pi++) {
                int py=cy+18+pi*26;
                vga_fill_rect(cx-2,py-2,rw+2,22,g_pref_darkmode?RGB(34,34,40):RGB(252,252,255));
                vga_draw_rect_outline(cx-2,py-2,rw+2,22,sep2);
                vga_draw_string_trans(cx+2,py+6,priv_items2[pi],lbl);
                draw_toggle(tx_r,py+2,priv_st2[pi]);
                vga_draw_string_trans(cx+90,py+6,priv_st2[pi]?"Allowed":"Denied",
                    priv_st2[pi]?RGB(52,199,89):RGB(255,59,48));
            }
            vga_draw_hline(cx, cy+176, rw, sep2);
            vga_draw_string_trans(cx, cy+182, "SECURITY", cat2);
            vga_draw_string_trans(cx, cy+198, "FileVault: Active", RGB(52,199,89));
            vga_draw_string_trans(cx, cy+212, "Firewall: Enabled", RGB(52,199,89));
            vga_draw_string_trans(cx, cy+226, "Gatekeeper: On", RGB(52,199,89));
            vga_draw_hline(cx, cy+242, rw, sep2);
            vga_draw_string_trans(cx, cy+248, "Allow Cross-Site Tracking", lbl);
            draw_toggle(tx_r, cy+245, 0);
        } else if (g_settings_tab == 10) {
            /* iCloud */
            vga_draw_string_trans(cx, cy, "ICLOUD", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "iCloud Storage", lbl);
            { runtime_storage_info_t storage;
              int used = 0;
              if (runtime_get_storage_info("/", &storage) == 0) used = storage.used_percent;
              vga_fill_rect(cx, cy+32, rw, 10, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
              vga_fill_rect(cx, cy+32, rw*used/100, 10, RGB(0,122,255));
              vga_draw_string_trans(cx, cy+46, "Storage provider synced", sub);
            }
            vga_draw_hline(cx, cy+58, rw, sep2);
            vga_draw_string_trans(cx, cy+64, "APPS USING ICLOUD", cat2);
            static const char *ic_apps[] = {"iCloud Drive","Photos","Mail","Contacts","Calendars","Notes","Safari"};
            static int ic_on[7] = {1,1,1,1,1,1,1};
            int ii;
            for (ii=0;ii<7;ii++) {
                int iy2=cy+80+ii*22;
                vga_fill_rect(cx-2,iy2-2,rw+2,18,g_pref_darkmode?RGB(34,34,40):RGB(252,252,255));
                vga_draw_rect_outline(cx-2,iy2-2,rw+2,18,sep2);
                vga_draw_string_trans(cx+2,iy2+2,ic_apps[ii],lbl);
                draw_toggle(tx_r,iy2-2,ic_on[ii]);
            }
            vga_draw_hline(cx, cy+242, rw, sep2);
            vga_draw_string_trans(cx, cy+248, "ADVANCED", cat2);
            vga_draw_string_trans(cx, cy+264, "iCloud Private Relay", lbl);
            draw_toggle(tx_r, cy+261, 1);
        } else if (g_settings_tab == 11) {
            /* Screen Time */
            vga_draw_string_trans(cx, cy, "SCREEN TIME", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Today's Usage:", lbl);
            { uint32_t t5=timer_ticks();
              int mins=(int)((t5/1000)/60);
              char mbuf[16]; int mi=0;
              if (mins>=60) { mbuf[mi++]='0'+mins/60; mbuf[mi++]='h'; mbuf[mi++]=' '; }
              mbuf[mi++]='0'+(mins%60)/10; mbuf[mi++]='0'+(mins%60)%10; mbuf[mi++]='m'; mbuf[mi]=0;
              vga_draw_string_trans(cx+112, cy+18, mbuf, RGB(0,122,255));
            }
            vga_draw_hline(cx, cy+32, rw, sep2);
            vga_draw_string_trans(cx, cy+38, "APP USAGE", cat2);
            static const struct { const char *name; int mins_s; uint32_t col; } st_apps2[] = {
                { "Safari",   82, RGB(0,122,255)  },
                { "Mail",     34, RGB(0,140,255)  },
                { "Messages", 51, RGB(52,199,89)  },
                { "Music",    67, RGB(252,60,68)  },
                { "News",     28, RGB(255,59,48)  },
                { "Other",    19, RGB(142,142,147)},
            };
            int nst=6, total_m=281;
            int si_s;
            for (si_s=0;si_s<nst;si_s++) {
                int sy2=cy+54+si_s*22;
                vga_draw_string_trans(cx, sy2+2, st_apps2[si_s].name, lbl);
                int bw_s=(rw-60)*st_apps2[si_s].mins_s/total_m;
                vga_fill_rect(cx+60, sy2, bw_s, 14, st_apps2[si_s].col);
                char mbuf2[8]; int mm2=st_apps2[si_s].mins_s, mi2=0;
                if(mm2>=60){mbuf2[mi2++]='0'+mm2/60;mbuf2[mi2++]='h';mbuf2[mi2++]=' ';}
                mbuf2[mi2++]='0'+(mm2%60)/10;mbuf2[mi2++]='0'+(mm2%60)%10;mbuf2[mi2++]='m';mbuf2[mi2]=0;
                vga_draw_string_trans(cx+64+bw_s, sy2+2, mbuf2, sub);
            }
            vga_draw_hline(cx, cy+196, rw, sep2);
            vga_draw_string_trans(cx, cy+202, "LIMITS", cat2);
            vga_draw_string_trans(cx, cy+218, "Daily Limit:", lbl);
            vga_draw_string_trans(cx+92, cy+218, "6h 00m", sub);
            vga_draw_string_trans(cx, cy+234, "Screen Time:", lbl);
            draw_toggle(tx_r, cy+231, 1);
            vga_draw_string_trans(cx+100, cy+234, "Active", RGB(52,199,89));
        } else if (g_settings_tab == 12) {
            /* Keyboard */
            vga_draw_string_trans(cx, cy, "KEYBOARD", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "Key Repeat Rate", lbl);
            vga_fill_rect(cx, cy+32, rw, 6, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
            vga_fill_rect(cx, cy+32, rw*7/10, 6, RGB(0,122,255));
            gui_draw_circle(cx+rw*7/10, cy+35, 6, RGB(255,255,255));
            gui_draw_circle_outline(cx+rw*7/10, cy+35, 6, g_pref_darkmode?RGB(80,80,95):RGB(170,170,180));
            vga_draw_string_trans(cx, cy+44, "Slow", sub);
            vga_draw_string_trans(cx+rw-24, cy+44, "Fast", sub);
            vga_draw_string_trans(cx, cy+58, "Delay Until Repeat", lbl);
            vga_fill_rect(cx, cy+72, rw, 6, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
            vga_fill_rect(cx, cy+72, rw/2, 6, RGB(0,122,255));
            gui_draw_circle(cx+rw/2, cy+75, 6, RGB(255,255,255));
            gui_draw_circle_outline(cx+rw/2, cy+75, 6, g_pref_darkmode?RGB(80,80,95):RGB(170,170,180));
            vga_draw_string_trans(cx, cy+84, "Short", sub);
            vga_draw_string_trans(cx+rw-28, cy+84, "Long", sub);
            vga_draw_hline(cx, cy+98, rw, sep2);
            vga_draw_string_trans(cx, cy+104, "TEXT INPUT", cat2);
            vga_draw_string_trans(cx, cy+120, "Autocorrection", lbl);
            draw_toggle(tx_r, cy+117, 1);
            vga_draw_string_trans(cx, cy+140, "Capitalize Words", lbl);
            draw_toggle(tx_r, cy+137, 0);
            vga_draw_string_trans(cx, cy+160, "Smart Quotes", lbl);
            draw_toggle(tx_r, cy+157, 1);
            vga_draw_string_trans(cx, cy+180, "Smart Dashes", lbl);
            draw_toggle(tx_r, cy+177, 1);
            vga_draw_hline(cx, cy+196, rw, sep2);
            vga_draw_string_trans(cx, cy+202, "SHORTCUTS", cat2);
            static const char *sh_names[] = {"Mission Control","App Shortcuts","Screenshots","Spaces"};
            static const char *sh_keys[]  = {"Ctrl+Up","Cmd+.", "Cmd+Sh+4","Ctrl+1-4"};
            int ki;
            for (ki=0;ki<4;ki++) {
                vga_draw_string_trans(cx, cy+218+ki*18, sh_names[ki], lbl);
                vga_draw_string_trans(cx+rw-60, cy+218+ki*18, sh_keys[ki], sub);
            }
        } else {
            /* Accessibility (tab 13) */
            vga_draw_string_trans(cx, cy, "ACCESSIBILITY", cat2);
            vga_draw_hline(cx, cy+12, rw, sep2);
            vga_draw_string_trans(cx, cy+18, "VISION", cat2);
            vga_draw_string_trans(cx, cy+34, "Increase Contrast", lbl);
            draw_toggle(tx_r, cy+31, 0);
            vga_draw_string_trans(cx, cy+54, "Reduce Transparency", lbl);
            draw_toggle(tx_r, cy+51, 0);
            vga_draw_string_trans(cx, cy+74, "Bold Text", lbl);
            draw_toggle(tx_r, cy+71, 0);
            vga_draw_string_trans(cx, cy+94, "Large Text", lbl);
            draw_toggle(tx_r, cy+91, 0);
            vga_draw_string_trans(cx, cy+114, "Display Size:", lbl);
            vga_fill_rect(cx, cy+128, rw, 6, g_pref_darkmode?RGB(55,55,65):RGB(210,210,215));
            vga_fill_rect(cx, cy+128, rw/2, 6, RGB(0,122,255));
            gui_draw_circle(cx+rw/2, cy+131, 6, RGB(255,255,255));
            gui_draw_circle_outline(cx+rw/2, cy+131, 6, g_pref_darkmode?RGB(80,80,95):RGB(170,170,180));
            vga_draw_hline(cx, cy+144, rw, sep2);
            vga_draw_string_trans(cx, cy+150, "HEARING", cat2);
            vga_draw_string_trans(cx, cy+166, "Mono Audio", lbl);
            draw_toggle(tx_r, cy+163, 0);
            vga_draw_string_trans(cx, cy+186, "Flash for Alerts", lbl);
            draw_toggle(tx_r, cy+183, 0);
            vga_draw_hline(cx, cy+202, rw, sep2);
            vga_draw_string_trans(cx, cy+208, "MOTOR", cat2);
            vga_draw_string_trans(cx, cy+224, "Sticky Keys", lbl);
            draw_toggle(tx_r, cy+221, 0);
            vga_draw_string_trans(cx, cy+244, "Slow Keys", lbl);
            draw_toggle(tx_r, cy+241, 0);
            vga_draw_hline(cx, cy+260, rw, sep2);
            vga_draw_string_trans(cx, cy+266, "VoiceOver: Off", sub);
        }
        return 1;
    }

    /* Calculator window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Calculator")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx = win->x, wy = win->y, ww = win->w;

        /* Display panel */
        vga_fill_rect(wx+4, wy+TITLEBAR_H+4, ww-8, 56, RGB(28,28,28));
        /* Right-align display text */
        {
            int dlen = str_len(g_calc_disp);
            int dx = wx + ww - 8 - dlen*8 - 4;
            /* Large font simulation: draw twice at offset for bold effect */
            vga_draw_string_trans(dx,   wy+TITLEBAR_H+28, g_calc_disp, RGB(255,255,255));
            vga_draw_string_trans(dx+1, wy+TITLEBAR_H+28, g_calc_disp, RGB(255,255,255));
        }

        /* Button grid: 4 cols, 5 rows */
        {
            static const char *labels[5][4] = {
                {"AC", "+/-", "%", "/"},
                {"7",  "8",   "9", "*"},
                {"4",  "5",   "6", "-"},
                {"1",  "2",   "3", "+"},
                {"0",  "",    ".", "="}
            };
            int bw=48, bh=30, gx=4, gy=4;
            int bx0=wx+8, by0=wy+TITLEBAR_H+66;
            int r, c;
            for (r=0; r<5; r++) {
                for (c=0; c<4; c++) {
                    if (r==4 && c==1) continue; /* skip — 0 is wide */
                    int bx = bx0 + c*(bw+gx);
                    int by = by0 + r*(bh+gy);
                    int bw2 = (r==4 && c==0) ? bw*2+gx : bw;
                    uint32_t bcol;
                    if (c==3 || (r==4 && c==3)) bcol = RGB(255,149,0);
                    else if (r==0)               bcol = RGB(165,165,165);
                    else                          bcol = RGB(80,80,80);
                    vga_fill_rect(bx, by, bw2, bh, bcol);
                    /* label centred */
                    {
                        int llen = str_len(labels[r][c]);
                        int lx = bx + (bw2 - llen*8)/2;
                        int ly = by + (bh-8)/2;
                        uint32_t tcol = (r==0) ? COLOR_TEXT : COLOR_WHITE;
                        vga_draw_string_trans(lx, ly, labels[r][c], tcol);
                    }
                }
            }
        }
        return 1;
    }

    /* TextEdit window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "TextEdit")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int lh = 12; /* line height */
        /* Formatting toolbar */
        int tbh = 22;
        int tby = win->y + TITLEBAR_H + 1;
        vga_fill_rect(win->x+1, tby, win->w-2, tbh, g_pref_darkmode?RGB(38,38,42):RGB(245,245,248));
        vga_draw_hline(win->x+1, tby+tbh-1, win->w-2, g_pref_darkmode?RGB(60,60,65):RGB(210,210,215));
        /* B button */
        int bx2 = win->x + 6;
        uint32_t bbg = g_edit_bold ? RGB(0,122,255) : RGB(220,220,225);
        vga_fill_rect(bx2, tby+3, 18, 16, bbg);
        vga_draw_rect_outline(bx2, tby+3, 18, 16, RGB(180,180,185));
        vga_draw_string_trans(bx2+5, tby+7, "B", g_edit_bold ? RGB(255,255,255) : RGB(40,40,40));
        if (g_edit_bold) vga_draw_string_trans(bx2+6, tby+7, "B", g_edit_bold ? RGB(255,255,255) : RGB(40,40,40));
        /* I button */
        bx2 += 22;
        uint32_t ibg = g_edit_italic ? RGB(0,122,255) : RGB(220,220,225);
        vga_fill_rect(bx2, tby+3, 18, 16, ibg);
        vga_draw_rect_outline(bx2, tby+3, 18, 16, RGB(180,180,185));
        vga_draw_string_trans(bx2+6, tby+7, "I", g_edit_italic ? RGB(255,255,255) : RGB(40,40,40));
        /* Font size buttons */
        bx2 += 26;
        vga_draw_string_trans(bx2, tby+7, "A-", RGB(60,60,60));
        bx2 += 22;
        vga_draw_string_trans(bx2, tby+7, "A+", RGB(60,60,60));
        /* Color dot */
        bx2 += 22;
        static const uint32_t txt_colors[4] = {0, RGB(200,0,0), RGB(0,100,200), RGB(0,130,50)};
        static const char *color_names[4] = {"Blk","Red","Blu","Grn"};
        vga_draw_string_trans(bx2, tby+7, color_names[g_edit_color%4], RGB(60,60,60));
        /* Word count */
        {
            int wc = 0, in_word = 0;
            int k;
            for (k=0; k<g_edit_len; k++) {
                if (g_edit_text[k]==' '||g_edit_text[k]=='\n'||g_edit_text[k]=='\t') in_word=0;
                else if (!in_word) { in_word=1; wc++; }
            }
            /* Draw "N words" right-aligned */
            char wcbuf[16]; int wci=0;
            wci += (wc>=100?3:wc>=10?2:1);
            if (wc>=100) { wcbuf[0]='0'+wc/100; wcbuf[1]='0'+(wc/10)%10; wcbuf[2]='0'+wc%10; }
            else if (wc>=10) { wcbuf[0]='0'+wc/10; wcbuf[1]='0'+wc%10; }
            else { wcbuf[0]='0'+wc; }
            wcbuf[wci++]=' '; wcbuf[wci++]='w'; wcbuf[wci++]='d'; wcbuf[wci++]='s'; wcbuf[wci]=0;
            vga_draw_string_trans(win->x+win->w-str_len(wcbuf)*8-6, tby+7, wcbuf, RGB(120,120,130));
        }
        /* Content area */
        vga_fill_rect(win->x+1, tby+tbh, win->w-2, win->h-TITLEBAR_H-tbh-19,
                      g_pref_darkmode?RGB(28,28,30):RGB(255,255,255));
        int wx = win->x + 6, wy = tby + tbh + 4;
        int max_w = win->w - 12;
        /* Draw text with word-wrap at character level */
        {
            int cx = wx, cy = wy, j2 = 0;
            /* Compute current length if not yet */
            if (g_edit_len == 0) {
                while (g_edit_text[g_edit_len]) g_edit_len++;
            }
            while (j2 < g_edit_len) {
                char c = g_edit_text[j2];
                if (c == '\n' || cx + 8 > wx + max_w) {
                    cx = wx; cy += lh;
                    if (cy + 8 > win->y + win->h - 20) break;
                    if (c == '\n') { j2++; continue; }
                }
                uint32_t tcol = (g_edit_color > 0 && g_edit_color < 4) ? txt_colors[g_edit_color] : (g_pref_darkmode?RGB(220,220,224):COLOR_TEXT);
                vga_draw_char_trans(cx, cy, c, tcol);
                if (g_edit_bold) vga_draw_char_trans(cx+1, cy, c, tcol);
                if (g_edit_italic) vga_draw_char_trans(cx, cy+1, c, tcol);
                cx += (g_edit_font_size == 2) ? 10 : 8; j2++;
            }
            /* Blinking cursor at end */
            if (g_edit_focused && idx == win_top_visible()) {
                uint32_t t = timer_ticks();
                if ((t/500)%2 == 0) vga_fill_rect(cx, cy, 2, 10, g_pref_darkmode?RGB(220,220,224):COLOR_TEXT);
            }
        }
        /* Focus indicator */
        if (g_edit_focused && idx == win_top_visible()) {
            vga_draw_rect_outline(win->x+1, win->y+TITLEBAR_H+1,
                                  win->w-2, win->h-TITLEBAR_H-19, RGB(80,120,255));
        }
        return 1;
    }

    /* Music player window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Music")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        /* Dark background */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, RGB(28,28,30));
        /* Mini tab bar: Library | Now Playing | Radio */
        {
            int tbx = wx+1, tby = wy+TITLEBAR_H+2, tbw = (ww-2)/3, tbh2 = 20;
            vga_fill_rect(tbx, tby, ww-2, tbh2, RGB(40,40,44));
            static const char *tabs[] = { "Library", "Now Playing", "Radio" };
            int ti;
            for (ti=0;ti<3;ti++) {
                int tx3 = tbx + ti*tbw;
                if (ti==1) vga_fill_rect(tx3, tby, tbw, tbh2, RGB(55,55,60));
                int tw3 = str_len(tabs[ti])*8;
                vga_draw_string_trans(tx3+(tbw-tw3)/2, tby+6, tabs[ti], ti==1?RGB(255,255,255):RGB(150,150,150));
            }
        }
        /* Album art square */
        int art_sz = (ww-2 > 120) ? 100 : ww/2;
        if (art_sz > wh/2 - 20) art_sz = wh/2 - 20;
        int art_x = wx + (ww-art_sz)/2;
        int art_y = wy + TITLEBAR_H + 28;
        /* Rounded art with gradient */
        { int ay;
          for (ay=0;ay<art_sz;ay++) {
              vga_draw_hline(art_x+2, art_y+ay, art_sz-4,
                  RGB((uint8_t)(220-ay*80/art_sz),
                      (uint8_t)(60+ay*80/art_sz),
                      (uint8_t)(80+ay*120/art_sz)));
          }
          /* Diagonal light reflection */
          vga_fill_rect_alpha(art_x+2, art_y+2, art_sz/2, art_sz/3, RGB(255,255,255), 30);
        }
        gui_draw_rounded_rect_outline(art_x, art_y, art_sz, art_sz, 4, RGB(60,60,60));
        /* Song title + artist (track-dependent) */
        int ty2 = art_y + art_sz + 10;
        { static const char *songs[]  = {"Midnight Drive","Neon Pulse","Starfall","Cyberwave","Solar Wind"};
          static const char *artists[] = {"Synthwave Dreams","Neon Horizon","StarlightFM","CyberBeats","SolarAudio"};
          static const char *albums[]  = {"Neon Horizon (2025)","Electric Sky","Cosmos","Matrix (2024)","Aurora"};
          const char *song  = songs[g_music_track % 5];
          const char *artst = artists[g_music_track % 5];
          const char *album = albums[g_music_track % 5];
          int tw2 = str_len(song)*8;
          vga_draw_string_trans(wx+(ww-tw2)/2, ty2,    song,  RGB(255,255,255));
          tw2 = str_len(artst)*8;
          vga_draw_string_trans(wx+(ww-tw2)/2, ty2+14, artst, RGB(180,100,100));
          tw2 = str_len(album)*8;
          if (wx+(ww-tw2)/2 > wx && wx+(ww+tw2)/2 < wx+ww)
              vga_draw_string_trans(wx+(ww-tw2)/2, ty2+26, album, RGB(120,120,120));
        }
        /* Progress bar - dynamic (loops every 227 seconds = 3:47 track) */
        ty2 += 44;
        uint32_t t_music = timer_ticks();
        int track_len_s = 227; /* 3:47 */
        int pos_s = (int)((t_music / 1000) % (uint32_t)track_len_s);
        if (!g_music_playing) pos_s = 83; /* paused at 1:23 */
        int prog = (ww-32) * pos_s / track_len_s;
        vga_fill_rect(wx+16, ty2, ww-32, 3, RGB(70,70,70));
        vga_fill_rect(wx+16, ty2, prog, 3, RGB(252,60,68));
        gui_draw_circle(wx+16+prog, ty2+1, 5, RGB(255,255,255));
        /* Time labels */
        { char tbuf[6]; int ps=pos_s; tbuf[0]='0'+ps/60; tbuf[1]=':'; tbuf[2]='0'+(ps%60)/10; tbuf[3]='0'+ps%10; tbuf[4]=0;
          vga_draw_string_trans(wx+16, ty2+8, tbuf, RGB(100,100,100)); }
        vga_draw_string_trans(wx+ww-48, ty2+8, "3:47", RGB(100,100,100));
        /* Controls row */
        ty2 += 24;
        int mid_x = wx + ww/2;
        /* Shuffle */  vga_draw_string_trans(mid_x-66, ty2+2, "shf", RGB(100,100,100));
        /* |< prev */ vga_fill_rect(mid_x-48, ty2-1, 4, 14, RGB(200,200,200));
                      { int pi3; for(pi3=0;pi3<12;pi3++) vga_draw_hline(mid_x-44-pi3/2, ty2+pi3/14*14, pi3/2, RGB(200,200,200)); }
        /* Play/Pause button */
        if (g_music_playing) {
            vga_fill_rect(mid_x-10, ty2-2, 8, 16, RGB(255,255,255));
            vga_fill_rect(mid_x+2,  ty2-2, 8, 16, RGB(255,255,255));
        } else {
            /* Triangle play */
            int pi2;
            for (pi2=0;pi2<14;pi2++) vga_draw_hline(mid_x-6, ty2-2+pi2, pi2/2, RGB(255,255,255));
        }
        /* >| next */ { int pi3; for(pi3=0;pi3<12;pi3++) vga_draw_hline(mid_x+22, ty2+pi3/14*14, (11-pi3)/2, RGB(200,200,200)); }
                      vga_fill_rect(mid_x+38, ty2-1, 4, 14, RGB(200,200,200));
        /* Repeat */  vga_draw_string_trans(mid_x+46, ty2+2, "rep", RGB(100,100,100));
        /* Volume slider (uses g_music_vol 0-100) */
        ty2 += 24;
        vga_draw_string_trans(wx+16, ty2+2, "vol", RGB(80,80,80));
        vga_fill_rect(wx+44, ty2+4, ww-72, 3, RGB(70,70,70));
        int vol_w = (ww-72) * g_music_vol / 100;
        vga_fill_rect(wx+44, ty2+4, vol_w, 3, RGB(180,180,180));
        gui_draw_circle(wx+44+vol_w, ty2+5, 4, RGB(200,200,200));
        vga_draw_string_trans(wx+ww-24, ty2+2, "(+)", RGB(80,80,80));
        /* Playing indicator + waveform visualizer */
        if (g_music_playing) {
            uint32_t t4 = timer_ticks();
            uint8_t pulse = (uint8_t)(128 + (t4/100) % 2 * 80);
            gui_draw_circle(wx+ww-12, wy+TITLEBAR_H+10, 4, RGB(pulse,50,50));
            /* Mini waveform bars */
            static const int wfamp[16] = {4,7,9,11,13,10,14,8,12,6,14,9,11,7,5,3};
            int wfx = wx+8, wfy = wy+TITLEBAR_H+14;
            int wfi;
            for (wfi=0;wfi<8;wfi++) {
                int phase3 = (int)((t4/80 + wfi*7) % 16);
                int bar3h = wfamp[phase3];
                uint8_t br = (uint8_t)(200+wfi*6);
                vga_fill_rect(wfx+wfi*5, wfy-bar3h, 3, bar3h, RGB(br,40,60));
            }
        }
        /* Full-width animated equalizer at bottom of Music window */
        if (g_music_eq_visible) {
            int eq_bar_count = 20;
            int eq_h_max = 28;
            int eq_bottom = wy + wh - 20; /* above status bar */
            int eq_top = eq_bottom - eq_h_max - 4;
            int eq_bar_w = (ww - 4) / eq_bar_count;
            if (eq_bar_w < 2) eq_bar_w = 2;
            int eq_total = eq_bar_w * eq_bar_count;
            int eq_left = wx + (ww - eq_total) / 2;
            /* Background */
            vga_fill_rect(wx+1, eq_top-2, ww-2, eq_h_max+8, RGB(20,20,22));
            vga_draw_hline(wx+1, eq_top-2, ww-2, RGB(50,50,55));
            uint32_t t_eq = g_music_playing ? timer_ticks() : 0;
            static const int eq_base[20] = {
                6,10,14,18,22,20,16,24,18,12,20,14,22,16,10,18,22,14,8,12
            };
            int ei;
            for (ei = 0; ei < eq_bar_count; ei++) {
                int phase_e = (int)((t_eq / 60 + ei * 11) % 32);
                int osc = (phase_e < 16) ? phase_e : 32 - phase_e; /* 0..15..0 */
                int bar_h = g_music_playing ? (eq_base[ei] + osc * eq_base[ei] / 20) : (eq_base[ei]/4);
                if (bar_h > eq_h_max) bar_h = eq_h_max;
                int bx = eq_left + ei * eq_bar_w;
                int by = eq_bottom - bar_h;
                /* Gradient color: low=red, mid=orange, high=yellow */
                uint8_t rr = 252;
                uint8_t gg = (uint8_t)(60 + bar_h * 150 / eq_h_max);
                uint8_t bb = (uint8_t)(bar_h > eq_h_max*2/3 ? 100 : 0);
                vga_fill_rect(bx, by, eq_bar_w-1, bar_h, RGB(rr,gg,bb));
                /* Peak dot */
                vga_fill_rect(bx, by-2, eq_bar_w-1, 2, RGB(255,255,255));
            }
        }
        return 1;
    }

    /* Photos window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Photos")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int ph_top = wy+TITLEBAR_H+1;
        int ph_h   = wh-TITLEBAR_H-19;
        uint32_t ph_bg    = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);
        uint32_t ph_sb_bg = g_pref_darkmode ? RGB(36,36,40)    : RGB(236,236,238);
        uint32_t ph_sb_bd = g_pref_darkmode ? RGB(55,55,60)    : RGB(210,210,212);
        uint32_t ph_txt   = g_pref_darkmode ? RGB(220,220,224) : RGB(30,30,30);
        uint32_t ph_sub   = g_pref_darkmode ? RGB(130,130,138) : RGB(120,120,120);
        uint32_t ph_hdr   = g_pref_darkmode ? RGB(100,100,108) : RGB(100,100,108);
        uint32_t ph_sel   = RGB(0,122,255);
        uint32_t ph_tb    = g_pref_darkmode ? RGB(40,40,44)    : RGB(246,246,248);
        /* Main background */
        vga_fill_rect(wx+1, ph_top, ww-2, ph_h, ph_bg);
        /* Top toolbar */
        vga_fill_rect(wx+1, ph_top, ww-2, 24, ph_tb);
        vga_draw_hline(wx+1, ph_top+24, ww-2, ph_sb_bd);
        vga_draw_string_trans(wx+8,   ph_top+8, "Library",   RGB(0,122,255));
        vga_draw_string_trans(wx+72,  ph_top+8, "For You",   ph_sub);
        vga_draw_string_trans(wx+132, ph_top+8, "Albums",    ph_sub);
        vga_draw_string_trans(wx+190, ph_top+8, "Search",    ph_sub);
        /* Sidebar (80px) */
        int sb_w = 80;
        vga_fill_rect(wx+1, ph_top+24, sb_w, ph_h-24, ph_sb_bg);
        vga_draw_vline(wx+1+sb_w, ph_top+24, ph_h-24, ph_sb_bd);
        /* Sidebar sections */
        { int sy = ph_top+30;
          vga_draw_string_trans(wx+6, sy, "LIBRARY", ph_hdr); sy+=12;
          static const char *lib_items[] = {"Library","Recents","Favorites","Hidden"};
          int li2;
          for(li2=0;li2<4;li2++) {
              int is_sel=(li2==0);
              if(is_sel) { gui_draw_rounded_rect(wx+2, sy-1, sb_w-3, 14, 4, g_pref_darkmode?RGB(50,50,55):RGB(220,220,228)); }
              uint32_t itc = is_sel?ph_sel:ph_txt;
              vga_draw_string_trans(wx+8, sy+2, lib_items[li2], itc);
              sy+=16;
          }
          sy+=6;
          vga_draw_string_trans(wx+6, sy, "MY ALBUMS", ph_hdr); sy+=12;
          static const char *alb_items[] = {"Seoul Trip","Nature","Screens"};
          int ai;
          for(ai=0;ai<3;ai++) {
              vga_draw_string_trans(wx+8, sy+2, alb_items[ai], ph_txt);
              sy+=16;
          }
        }
        /* Section label in main area */
        int grid_x = wx+1+sb_w+4;
        int grid_y = ph_top+24+2;
        int grid_w = ww-2-sb_w-8;
        int photo_count = 8;
        { char monthbuf[32];
          get_current_month_year_str(monthbuf);
          vga_draw_string_trans(grid_x, grid_y+2, monthbuf, ph_txt); }
        vga_draw_string_trans(grid_x+grid_w-48, grid_y+2, "All", ph_sub);
        grid_y += 14;
        /* Photo grid: 4 columns */
        {
            int cols=4, pad=2;
            int px_w=(grid_w)/cols-pad;
            int px_h=(int)(px_w*0.75f); /* 4:3 aspect */
            if(px_h < 40) px_h=40;
            int i2;
            for (i2=0;i2<photo_count;i2++) {
                int pc=i2%cols, pr=i2/cols;
                int px=grid_x+pc*(px_w+pad), py=grid_y+pr*(px_h+pad);
                if(py+px_h > ph_top+ph_h-2) break;
                int row2; int typ=i2%6;
                if (typ==0) { /* Mountain */
                    for (row2=0;row2<px_h;row2++) {
                        uint8_t tb=(uint8_t)(135-row2*50/(px_h/2>0?px_h/2:1));
                        vga_draw_hline(px, py+row2, px_w,
                            row2<px_h/2?RGB((uint8_t)(100+tb/3),(uint8_t)(160+tb/4),(uint8_t)(200+tb/4))
                                       :RGB(34,(uint8_t)(100+row2*30/px_h),50));
                    }
                    { int mi; for(mi=0;mi<14;mi++) vga_draw_hline(px+px_w/2-mi,py+4+mi,mi*2,RGB(240,245,255)); }
                    { int mi; for(mi=0;mi<px_h/2;mi++) vga_draw_hline(px+px_w/2-mi-6,py+18+mi,mi*2+12,RGB(80,100,60)); }
                } else if (typ==1) { /* City night */
                    vga_fill_rect(px, py, px_w, px_h, RGB(15,15,30));
                    { static const int bh3[6]={35,25,42,30,20,38},bw3[6]={12,16,14,18,12,15};
                      int bi; for(bi=0;bi<6;bi++) {
                        int bx=px+bi*(px_w/6);
                        vga_fill_rect(bx,py+px_h-bh3[bi],bw3[bi],bh3[bi],RGB(30+bi*5,30+bi*3,50+bi*4));
                        { int wr,wc; for(wr=0;wr<bh3[bi]/10;wr++) for(wc=0;wc<bw3[bi]/5;wc++)
                            if((wr+wc)%2==0) vga_fill_rect(bx+2+wc*5,py+px_h-bh3[bi]+3+wr*10,3,4,RGB(255,220,100)); }
                      }
                    }
                    gui_draw_circle(px+px_w-14, py+10, 7, RGB(240,240,200));
                } else if (typ==2) { /* Sunset */
                    for (row2=0;row2<px_h;row2++) {
                        vga_draw_hline(px,py+row2,px_w,RGB((uint8_t)(255-row2*60/px_h),
                            (uint8_t)(row2<px_h/2?140-row2*80/px_h:50),
                            (uint8_t)(row2<px_h*2/3?50:20+row2*30/px_h)));
                    }
                    gui_draw_circle(px+px_w/2,py+px_h*2/3,10,RGB(255,200,50));
                    vga_fill_rect(px,py+px_h*2/3,px_w,px_h-px_h*2/3,RGB(20,20,30));
                    { int ti; for(ti=0;ti<4;ti++) { int tx=px+10+ti*(px_w/4);
                      { int th; for(th=0;th<20;th++) vga_draw_hline(tx,py+px_h*2/3-th,4+th/3,RGB(10,10,20)); } }}
                } else if (typ==3) { /* Beach */
                    for (row2=0;row2<px_h;row2++) {
                        uint32_t c;
                        if(row2<px_h*2/5) c=RGB((uint8_t)(100+row2*30/(px_h*2/5>0?px_h*2/5:1)),(uint8_t)(180+row2*20/(px_h*2/5>0?px_h*2/5:1)),240);
                        else if(row2<px_h*3/5) c=RGB(30,(uint8_t)(120+row2*20/px_h),200);
                        else c=RGB((uint8_t)(220+row2/10),(uint8_t)(195+row2/12),(uint8_t)(140+row2/15));
                        vga_draw_hline(px,py+row2,px_w,c);
                    }
                    gui_draw_circle(px+px_w-16,py+10,9,RGB(255,230,50));
                } else if (typ==4) { /* Forest */
                    vga_fill_rect(px,py,px_w,12,RGB(160,210,240));
                    for (row2=12;row2<px_h;row2++) { uint8_t g4=(uint8_t)(80+row2*60/px_h);
                        vga_draw_hline(px,py+row2,px_w,RGB((uint8_t)(20+row2/8),g4,(uint8_t)(20+row2/12))); }
                    { int ti; for(ti=0;ti<5;ti++) { int tx=px+6+ti*(px_w/5);
                      vga_fill_rect(tx,py+20,5,px_h-20,RGB(80,50,20));
                      gui_draw_circle(tx+2,py+20,14,RGB(30+ti*10,80+ti*15,30)); }}
                    { int pi; for(pi=0;pi<px_h-20;pi++) vga_draw_hline(px+px_w/2-3-pi/8,py+20+pi,6+pi/4,RGB(200,180,120)); }
                } else { /* Flower */
                    vga_fill_rect(px,py,px_w,px_h,RGB(50,150,60));
                    { static const int px4[6]={0,14,14,0,-14,-14},py4[6]={-18,-9,9,18,9,-9};
                      int pe; for(pe=0;pe<6;pe++) gui_draw_circle(px+px_w/2+px4[pe],py+px_h/2+py4[pe],8,RGB(255,80,150)); }
                    gui_draw_circle(px+px_w/2,py+px_h/2,18,RGB(255,50,120));
                    gui_draw_circle(px+px_w/2,py+px_h/2,8,RGB(255,220,50));
                }
                vga_draw_rect_outline(px, py, px_w, px_h, g_pref_darkmode?RGB(50,50,55):RGB(200,200,205));
            }
        }
        /* Status bar */
        vga_fill_rect(wx+1, wy+wh-18, ww-2, 1, ph_sb_bd);
        vga_fill_rect(wx+1, wy+wh-17, ww-2, 16, ph_tb);
        { char statusbuf[48];
          char countbuf[8];
          char monthbuf[32];
          int sp = 0;
          statusbuf[0] = 0;
          runtime_format_uint((uint32_t)photo_count, countbuf, sizeof(countbuf));
          get_current_month_year_str(monthbuf);
          apps1_append_text(statusbuf, &sp, sizeof(statusbuf), countbuf);
          apps1_append_text(statusbuf, &sp, sizeof(statusbuf), " photos  ");
          apps1_append_text(statusbuf, &sp, sizeof(statusbuf), monthbuf);
          vga_draw_string_trans(wx+8, wy+wh-13, statusbuf, ph_sub); }
        return 1;
    }

    /* Safari window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Safari")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        safari_normalize_state();
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        /* Safari dark mode colors */
        uint32_t sf_tab_bg  = g_pref_darkmode ? RGB(40,40,44)    : RGB(230,230,235);
        uint32_t sf_tab_bd  = g_pref_darkmode ? RGB(60,60,65)    : RGB(190,190,195);
        uint32_t sf_atab    = g_pref_darkmode ? RGB(28,28,32)    : RGB(255,255,255);
        uint32_t sf_atab_bd = g_pref_darkmode ? RGB(70,70,75)    : RGB(200,200,200);
        uint32_t sf_tab_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(40,40,40);
        uint32_t sf_tb_bg   = g_pref_darkmode ? RGB(36,36,40)    : RGB(242,242,244);
        uint32_t sf_ab_bg   = g_pref_darkmode ? RGB(44,44,48)    : RGB(255,255,255);
        uint32_t sf_url_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,30);
        uint32_t sf_ctrl    = g_pref_darkmode ? RGB(120,120,128) : RGB(130,130,130);
        uint32_t sf_nav     = g_pref_darkmode ? RGB(100,100,108) : RGB(180,180,180);
        uint32_t sf_pg_bg   = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);

        /* Tab bar */
        int taby = wy+TITLEBAR_H+1;
        vga_fill_rect(wx+1, taby, ww-2, 22, sf_tab_bg);
        vga_draw_hline(wx+1, taby+22, ww-2, sf_tab_bd);
        /* Dynamic multi-tab bar */
        {
            int n_tabs = g_safari_tab_count > 0 ? g_safari_tab_count : 1;
            int avail_w = ww - 2 - 22; /* 22px for "+" button */
            int tab_w = avail_w / n_tabs;
            if (tab_w > 160) tab_w = 160;
            int ti3;
            for (ti3 = 0; ti3 < n_tabs; ti3++) {
                int tx3 = wx + 1 + ti3 * (tab_w + 1);
                int is_act = (ti3 == g_safari_active_tab);
                vga_fill_rect(tx3, taby+2, tab_w, 20, is_act ? sf_atab : sf_tab_bg);
                if (is_act)
                    gui_draw_rounded_rect_outline(tx3, taby+2, tab_w, 20, 3, sf_atab_bd);
                /* Tab title truncated */
                const char *tt = g_safari_tab_titles[ti3][0] ? g_safari_tab_titles[ti3] : "New Tab";
                int tlen3 = str_len(tt);
                int max_c = (tab_w - 18) / 8;
                if (tlen3 > max_c) tlen3 = max_c;
                int ci6;
                for (ci6=0; ci6<tlen3; ci6++)
                    vga_draw_char_trans(tx3+6+ci6*8, taby+7, tt[ci6],
                        is_act ? sf_tab_txt : sf_ctrl);
                /* Close x */
                if (tab_w > 30)
                    vga_draw_string_trans(tx3+tab_w-12, taby+7, "x", sf_ctrl);
            }
            /* + new tab button */
            vga_draw_string_trans(wx+ww-18, taby+7, "+", sf_ctrl);
        }
        /* Toolbar */
        int toolbary = taby + 22;
        vga_fill_rect(wx+1, toolbary, ww-2, 26, sf_tb_bg);
        vga_draw_hline(wx+1, toolbary+26, ww-2, sf_tab_bd);
        /* Back/Forward */
        vga_draw_string_trans(wx+8,  toolbary+9, "<", sf_nav);
        vga_draw_string_trans(wx+22, toolbary+9, ">", sf_nav);
        /* Address bar */
        int ab_x=wx+44, ab_w=ww-90;
        vga_fill_rect(ab_x, toolbary+5, ab_w, 16, sf_ab_bg);
        uint32_t ab_border = g_safari_url_focused ? RGB(0,122,255) : (g_pref_darkmode?RGB(80,80,88):RGB(180,180,185));
        gui_draw_rounded_rect_outline(ab_x, toolbary+5, ab_w, 16, 4, ab_border);
        if (g_safari_url_focused) {
            vga_fill_rect_alpha(ab_x+2, toolbary+6, ab_w-4, 14, RGB(0,122,255), 30);
        }
        /* Lock icon (green padlock) */
        vga_fill_rect(ab_x+4, toolbary+8, 5, 4, RGB(52,199,89));
        vga_draw_rect_outline(ab_x+3, toolbary+7, 7, 6, RGB(52,199,89));
        /* URL text */
        { int url_len = str_len(g_safari_url);
          int max_url_chars = (ab_w - 18) / 8;
          if (url_len > max_url_chars) url_len = max_url_chars;
          int ui;
          for (ui=0; ui<url_len; ui++)
              vga_draw_char_trans(ab_x+14+ui*8, toolbary+8, g_safari_url[ui], sf_url_txt);
          /* Blinking cursor */
          if (g_safari_url_focused) {
              uint32_t t5 = timer_ticks();
              if ((t5/500)%2==0) vga_fill_rect(ab_x+14+url_len*8, toolbary+7, 1, 10, sf_url_txt);
          }
        }
        /* Reload + Share */
        vga_draw_string_trans(wx+ww-38, toolbary+9, "R", sf_ctrl);
        vga_draw_string_trans(wx+ww-20, toolbary+9, "^", sf_ctrl);
        /* Page content area */
        int cy2 = toolbary + 28;
        int ph = wh-TITLEBAR_H-19-22-26-28;
        vga_fill_rect(wx+1, cy2, ww-2, ph, sf_pg_bg);
        /* Detect URL to show different "pages" */
        const char *active_url = g_safari_url;
        int is_settings = (active_url[8]=='s'&&active_url[9]=='e'); /* settings */
        int is_github   = (active_url[8]=='g'); /* github... */
        int is_news     = (active_url[8]=='n'); /* news */
        int is_youtube  = (active_url[8]=='y'); /* youtube */
        int is_store    = (active_url[8]=='a'&&active_url[9]=='p'); /* appstore */
        /* Auto-update current tab title based on URL */
        { const char *auto_title =
              is_settings ? "MyOS Settings" :
              is_github   ? "github.com" :
              is_news     ? "MyOS News" :
              is_youtube  ? "YouTube" :
              is_store    ? "App Store" :
              (active_url[0] ? "MyOS Home" : "New Tab");
          int j4; for (j4=0;j4<23&&auto_title[j4];j4++) g_safari_tab_titles[g_safari_active_tab][j4]=auto_title[j4];
          g_safari_tab_titles[g_safari_active_tab][j4]=0;
        }
        if (is_settings) {
            runtime_storage_info_t storage;
            const netif_t *net = runtime_primary_netif();
            vga_fill_rect(wx+1, cy2, ww-2, 28, RGB(60,60,65));
            vga_draw_string_trans(wx+12, cy2+4,  "MyOS Settings", RGB(255,255,255));
            vga_draw_string_trans(wx+12, cy2+16, "System Preferences Online", RGB(180,180,200));
            vga_draw_string_trans(wx+12, cy2+36, (net && net->up) ? "Network: Connected" : "Network: Offline",
                (net && net->up) ? RGB(52,199,89) : RGB(255,59,48));
            if (runtime_get_storage_info("/", &storage) == 0) {
                char sbuf[32];
                int sp = 0;
                sbuf[0] = 0;
                apps1_append_text(sbuf, &sp, sizeof(sbuf), "Storage: ");
                apps1_append_bytes(sbuf, &sp, sizeof(sbuf), storage.free_bytes);
                apps1_append_text(sbuf, &sp, sizeof(sbuf), " free");
                vga_draw_string_trans(wx+12, cy2+50, sbuf, RGB(100,100,100));
            }
        } else if (is_github) {
            vga_fill_rect(wx+1, cy2, ww-2, 28, RGB(36,41,46));
            vga_draw_string_trans(wx+12, cy2+4,  "github.com/myos", RGB(255,255,255));
            vga_draw_string_trans(wx+12, cy2+16, "MyOS - Bare Metal x86 OS", RGB(180,200,220));
            vga_draw_string_trans(wx+12, cy2+36, "Stars: 1.2k  Forks: 234", RGB(100,120,100));
            vga_draw_string_trans(wx+12, cy2+50, "Language: C  License: MIT", RGB(100,100,100));
        } else if (is_news) {
            /* Apple News-style layout */
            uint32_t nw_bg  = g_pref_darkmode ? RGB(18,18,22)    : RGB(250,250,252);
            uint32_t nw_txt = g_pref_darkmode ? RGB(220,220,228) : RGB(20,20,25);
            uint32_t nw_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,108);
            uint32_t nw_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(210,210,215);
            uint32_t nw_tag = RGB(230,40,40);
            vga_fill_rect(wx+1, cy2, ww-2, 22, nw_tag);
            vga_draw_string_trans(wx+(ww-36)/2, cy2+7, "News", RGB(255,255,255));
            vga_fill_rect(wx+1, cy2+22, ww-2, ph-22, nw_bg);
            /* Hero article */
            int hay = cy2+26;
            vga_fill_rect(wx+4, hay, ww-8, 60, g_pref_darkmode?RGB(0,60,120):RGB(0,90,180));
            vga_draw_string_trans(wx+8, hay+4, "TECHNOLOGY", RGB(200,220,255));
            vga_draw_string_trans(wx+8, hay+16, "Apple Unveils M4 Ultra Mac Pro", RGB(255,255,255));
            vga_draw_string_trans(wx+8, hay+30, "The fastest Mac ever built with", RGB(180,210,255));
            vga_draw_string_trans(wx+8, hay+40, "unprecedented performance gains", RGB(180,210,255));
            vga_draw_string_trans(wx+8, hay+52, "2h ago  *  Tech Crunch", RGB(150,180,220));
            /* Article list */
            static const struct { const char *cat; const char *title; const char *src; const char *ago; uint32_t cat_c; } articles[] = {
                { "WORLD",    "Global Markets Rise on Strong Earnings",      "Reuters",    "1h",  RGB(0,150,80)   },
                { "SCIENCE",  "Webb Telescope Discovers New Exoplanet",       "NASA",       "3h",  RGB(80,60,200)  },
                { "SPORTS",   "World Cup 2026: Opening Match Results",        "ESPN",       "5h",  RGB(255,80,0)   },
                { "HEALTH",   "New Study Links Sleep to Brain Health",        "Med.News",   "6h",  RGB(255,60,100) },
                { "CULTURE",  "Oscars 2026: Full Winners List",               "Variety",    "8h",  RGB(180,120,0)  },
            };
            int nay = hay + 68;
            int ai; for (ai=0; ai<5 && nay+36 < cy2+ph; ai++) {
                vga_draw_hline(wx+6, nay, ww-12, nw_sep);
                nay += 4;
                /* Category tag */
                vga_fill_rect(wx+6, nay, str_len(articles[ai].cat)*8+4, 10, articles[ai].cat_c);
                vga_draw_string_trans(wx+8, nay+1, articles[ai].cat, RGB(255,255,255));
                /* Thumbnail color block */
                int thx = wx+ww-42;
                vga_fill_rect(thx, nay, 36, 28, articles[ai].cat_c);
                vga_fill_rect_alpha(thx, nay, 36, 10, RGB(255,255,255), 40);
                vga_draw_string_trans(wx+6, nay+14, articles[ai].title, nw_txt);
                vga_draw_string_trans(wx+6, nay+24, articles[ai].src, nw_sub);
                vga_draw_string_trans(thx+4, nay+28+2, articles[ai].ago, nw_sub);
                nay += 36;
            }
        } else if (is_youtube) {
            /* YouTube-like page */
            vga_fill_rect(wx+1, cy2, ww-2, 28, RGB(255,0,0));
            vga_draw_string_trans(wx+12, cy2+8, "YouTube", RGB(255,255,255));
            { int yw=0; for(;active_url[yw];yw++) (void)yw; }
            vga_fill_rect(wx+1, cy2+28, ww-2, ph-28, g_pref_darkmode?RGB(15,15,15):RGB(255,255,255));
            /* Fake video thumbnails */
            { int vi3;
              static const char *vtitles[]={"Tech Review 2026","Coding Tips","Game Play","Sunset Walk","Music Mix"};
              static const uint32_t vcols[]={RGB(0,100,200),RGB(0,160,80),RGB(180,50,50),RGB(200,140,0),RGB(120,0,180)};
              int col3=2, vw3=(ww-20)/col3, vh3=48;
              for (vi3=0;vi3<4&&vi3<col3*2;vi3++) {
                  int vr2=vi3/col3, vc=vi3%col3;
                  int vx2=wx+4+vc*(vw3+6), vy2=cy2+32+vr2*(vh3+28);
                  vga_fill_rect(vx2, vy2, vw3, vh3, vcols[vi3%5]);
                  /* Play button */
                  int pc3;
                  for(pc3=0;pc3<12;pc3++) vga_draw_hline(vx2+vw3/2-3, vy2+vh3/2-6+pc3, pc3/2, RGB(255,255,255));
                  vga_draw_string_trans(vx2, vy2+vh3+2, vtitles[vi3%5], g_pref_darkmode?RGB(210,210,218):RGB(30,30,30));
                  vga_draw_string_trans(vx2, vy2+vh3+14, "1.2M views", g_pref_darkmode?RGB(120,120,128):RGB(100,100,100));
              }
            }
        } else if (is_store) {
            /* App Store-like page */
            vga_fill_rect(wx+1, cy2, ww-2, 28, RGB(0,122,255));
            vga_draw_string_trans(wx+12, cy2+8, "App Store", RGB(255,255,255));
            vga_fill_rect(wx+1, cy2+28, ww-2, ph-28, g_pref_darkmode?RGB(24,24,28):RGB(248,248,252));
            vga_draw_string_trans(wx+8, cy2+36, "Featured", g_pref_darkmode?RGB(200,200,208):RGB(30,30,30));
            { int ai3;
              static const char *anames[]={"Xcode","Logic","Sketch","Notion","Figma"};
              static const uint32_t acols[]={RGB(0,122,255),RGB(180,50,50),RGB(255,100,0),RGB(0,0,0),RGB(80,60,200)};
              int aw3=(ww-16)/3, ah3=44;
              for(ai3=0;ai3<3;ai3++) {
                  int ax3=wx+4+ai3*(aw3+4), ay3=cy2+52;
                  gui_draw_rounded_rect(ax3, ay3, aw3, ah3, 8, acols[ai3%5]);
                  vga_draw_string_trans(ax3+4, ay3+14, anames[ai3], RGB(255,255,255));
                  vga_draw_string_trans(ax3+4, ay3+26, "GET", RGB(255,255,255));
              }
            }
        } else {
            /* Safari Start Page (macOS-style new tab) */
            uint32_t sp_bg  = g_pref_darkmode ? RGB(24,24,28)    : RGB(246,246,248);
            uint32_t sp_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,108);
            uint32_t sp_sep = g_pref_darkmode ? RGB(55,55,60)    : RGB(210,210,215);
            vga_fill_rect(wx+1, cy2, ww-2, ph, sp_bg);

            /* Big search bar at top */
            int sbx = wx + ww/2 - 110, sby = cy2 + 12, sbw = 220, sbh = 24;
            vga_fill_rect_alpha(sbx, sby, sbw, sbh, g_pref_darkmode?RGB(50,50,56):RGB(255,255,255), 220);
            gui_draw_rounded_rect_outline(sbx, sby, sbw, sbh, 6, sp_sep);
            /* Search icon */
            gui_draw_circle(sbx+12, sby+12, 5, sp_sub);
            gui_draw_circle(sbx+12, sby+12, 3, sp_bg);
            vga_draw_line(sbx+16, sby+16, sbx+20, sby+20, sp_sub);
            vga_draw_string_trans(sbx+22, sby+8, "Search or enter address", sp_sub);

            /* FAVORITES section */
            int fy = sby + sbh + 14;
            vga_draw_string_trans(wx+12, fy, "FAVOURITES", sp_sub);
            fy += 12;
            static const struct { const char *name; uint32_t col; char letter; } fav_sites[] = {
                { "Google",   RGB(66,133,244),  'G' },
                { "YouTube",  RGB(255,0,0),     'Y' },
                { "GitHub",   RGB(36,41,46),    'G' },
                { "Amazon",   RGB(255,153,0),   'A' },
                { "Twitter",  RGB(29,161,242),  'T' },
                { "Reddit",   RGB(255,69,0),    'R' },
                { "Netflix",  RGB(229,9,20),    'N' },
                { "Wikipedia",RGB(80,80,80),    'W' },
            };
            int n_fav = 8, fav_cols = 4;
            int fav_sz = (ww-24)/fav_cols - 4;
            if (fav_sz > 48) fav_sz = 48;
            int fi;
            for (fi=0; fi<n_fav; fi++) {
                int fc = fi % fav_cols, fr = fi / fav_cols;
                int fx = wx+12 + fc*(fav_sz+10);
                int fya = fy + fr*(fav_sz+24);
                if (fya + fav_sz + 24 > cy2 + ph - 8) break;
                /* Site icon */
                gui_draw_rounded_rect(fx, fya, fav_sz, fav_sz, 10, fav_sites[fi].col);
                vga_fill_rect_alpha(fx+2, fya+2, fav_sz-4, fav_sz/3, RGB(255,255,255), 45);
                { char ltr[2]; ltr[0]=fav_sites[fi].letter; ltr[1]=0;
                  vga_draw_string_trans(fx+fav_sz/2-4, fya+fav_sz/2-4, ltr, RGB(255,255,255)); }
                /* Label */
                int nlen = str_len(fav_sites[fi].name);
                vga_draw_string_trans(fx+(fav_sz-nlen*8)/2, fya+fav_sz+2, fav_sites[fi].name, sp_sub);
            }

            /* RECENTLY VISITED section */
            int rvy = fy + ((n_fav+fav_cols-1)/fav_cols)*(fav_sz+24) + 8;
            if (rvy + 80 < cy2 + ph) {
                vga_draw_hline(wx+8, rvy, ww-16, sp_sep);
                rvy += 6;
                vga_draw_string_trans(wx+12, rvy, "RECENTLY VISITED", sp_sub);
                rvy += 12;
                static const struct { const char *title; const char *url; uint32_t col; } recent[] = {
                    { "Apple",   "apple.com",   RGB(0,0,0)      },
                    { "Hacker News","news.ycombinator.com",RGB(255,102,0) },
                    { "Stack Overflow","stackoverflow.com",RGB(244,128,36) },
                };
                int ri2;
                for (ri2=0; ri2<3; ri2++) {
                    int rx = wx+12 + ri2*(ww-24)/3;
                    int ryw = rvy;
                    if (rx+70 > wx+ww-8) break;
                    gui_draw_rounded_rect(rx, ryw, 48, 30, 6, recent[ri2].col);
                    vga_fill_rect_alpha(rx+2, ryw+2, 44, 10, RGB(255,255,255), 45);
                    vga_draw_string_trans(rx, ryw+34, recent[ri2].title, sp_sub);
                }
            }

            /* Privacy Report footer */
            int prvy = cy2 + ph - 16;
            vga_draw_hline(wx+8, prvy-2, ww-16, sp_sep);
            vga_draw_string_trans(wx+12, prvy+2, "Privacy Report: 3 trackers blocked today", sp_sub);
        }
        /* Status bar */
        vga_draw_string_trans(wx+8, wy+wh-13, g_safari_url_focused?"URL: editing...":"Done",
            g_pref_darkmode?RGB(130,130,138):RGB(100,100,100));
        return 1;
    }

    /* About MyOS window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "About MyOS")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx = win->x, wy = win->y, ww = win->w, wh = win->h;
        int cy = wy + TITLEBAR_H + 8;
        runtime_system_info_t sys;
        runtime_storage_info_t storage;
        runtime_get_system_info(&sys);

        /* Content background */
        vga_fill_rect(wx+1, wy+TITLEBAR_H+1, ww-2, wh-TITLEBAR_H-19, g_pref_darkmode?RGB(28,28,30):RGB(245,245,248));

        /* Rainbow "M" logo */
        {
            int lx = wx + ww/2 - 22, ly = cy + 4;
            /* Background circle */
            gui_draw_circle(wx+ww/2, ly+22, 28, RGB(240,240,244));
            /* 6 colored stripes */
            static const uint32_t rainbow[6] = {
                0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x007AFF, 0x5856D6
            };
            int si;
            for (si=0;si<6;si++) {
                vga_fill_rect(lx+si*8, ly+6, 8, 32, rainbow[si]);
                /* rounded top/bottom via single pixels */
                vga_put_pixel(lx+si*8, ly+5, rainbow[si]);
                vga_put_pixel(lx+si*8+7, ly+5, rainbow[si]);
            }
            /* Clip to circle - overdraw outside with bg */
            int ri2, ci3;
            for (ri2=0;ri2<50;ri2++) for (ci3=0;ci3<50;ci3++) {
                int dx2=ci3-25, dy2=ri2-25;
                if (dx2*dx2+dy2*dy2 > 25*25) {
                    if (lx-3+ci3>=wx+1 && lx-3+ci3<wx+ww-1)
                        vga_put_pixel(lx-3+ci3, ly-3+ri2, RGB(240,240,244));
                }
            }
        }

        /* OS name - large bold */
        {
            const char *nm = sys.sysname;
            int nw2 = str_len(nm)*8;
            vga_draw_string_trans(wx+(ww-nw2)/2,   cy+62, nm, RGB(20,20,20));
            vga_draw_string_trans(wx+(ww-nw2)/2+1, cy+62, nm, RGB(20,20,20));
        }
        /* Version tag */
        {
            char ver[48];
            int vp = 0;
            ver[0] = 0;
            apps1_append_text(ver, &vp, sizeof(ver), "Version ");
            apps1_append_text(ver, &vp, sizeof(ver), sys.release);
            apps1_append_text(ver, &vp, sizeof(ver), " (");
            apps1_append_text(ver, &vp, sizeof(ver), sys.version);
            apps1_append_text(ver, &vp, sizeof(ver), ")");
            int vw = str_len(ver)*8;
            vga_draw_string_trans(wx+(ww-vw)/2, cy+78, ver, RGB(100,100,110));
        }
        vga_draw_hline(wx+16, cy+94, ww-32, RGB(210,210,215));
        /* Specs table */
        {
            char displaybuf[48];
            char membuf[16];
            char storagebuf[32];
            int sp = 0;
            const char *storage_text = "unavailable";
            apps1_format_display(&sys, displaybuf, sizeof(displaybuf));
            runtime_format_bytes(sys.pmm_total_bytes, membuf, sizeof(membuf));
            if (runtime_get_storage_info("/", &storage) == 0) {
                storagebuf[0] = 0;
                apps1_append_text(storagebuf, &sp, sizeof(storagebuf), storage.name);
                apps1_append_text(storagebuf, &sp, sizeof(storagebuf), " ");
                apps1_append_bytes(storagebuf, &sp, sizeof(storagebuf), storage.total_bytes);
                storage_text = storagebuf;
            }
            struct { const char *k; const char *v; } specs[] = {
                { "Architecture:", sys.machine       },
                { "Kernel:",       sys.release       },
                { "Display:",      displaybuf        },
                { "Memory:",       membuf            },
                { "CPU:",          sys.cpu_model     },
                { "Storage:",      storage_text      },
            };
            int si2;
            for (si2=0;si2<6;si2++) {
                int sy2=cy+100+si2*14;
                vga_draw_string_trans(wx+14, sy2, specs[si2].k, RGB(80,80,90));
                vga_draw_string_trans(wx+110, sy2, specs[si2].v, RGB(40,40,40));
            }
        };
        vga_draw_hline(wx+16, cy+188, ww-32, RGB(210,210,215));
        {
            const char *copy = "(C) 2026 MyOS Project";
            vga_draw_string_trans(wx+(ww-str_len(copy)*8)/2, cy+196, copy, RGB(140,140,150));
        }
        return 1;
    }

    /* Notes window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Notes")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx = win->x, wy = win->y, ww = win->w, wh = win->h;
        int content_y = wy + TITLEBAR_H + 1;
        int content_h = wh - TITLEBAR_H - 19;

        /* Dark mode color set for Notes */
        uint32_t nt_sb_bg  = g_pref_darkmode ? RGB(36,36,40)    : RGB(248,248,240);
        uint32_t nt_sb_bd  = g_pref_darkmode ? RGB(60,60,65)    : RGB(210,200,160);
        uint32_t nt_tb_bg  = g_pref_darkmode ? RGB(44,44,48)    : RGB(235,230,200);
        uint32_t nt_tb_txt = g_pref_darkmode ? RGB(180,180,188) : RGB(100,80,40);
        uint32_t nt_sel    = g_pref_darkmode ? RGB(60,56,30)    : RGB(255,240,160);
        uint32_t nt_title  = g_pref_darkmode ? RGB(220,220,224) : RGB(40,30,20);
        uint32_t nt_prev   = g_pref_darkmode ? RGB(140,136,100) : RGB(140,120,80);
        uint32_t nt_sep    = g_pref_darkmode ? RGB(60,60,55)    : RGB(220,210,170);
        uint32_t nt_ct_bg  = g_pref_darkmode ? RGB(38,36,24)    : RGB(255,252,200);
        uint32_t nt_line   = g_pref_darkmode ? RGB(60,56,30)    : RGB(210,200,150);
        uint32_t nt_margin = g_pref_darkmode ? RGB(100,40,40)   : RGB(240,160,160);
        uint32_t nt_body   = g_pref_darkmode ? RGB(210,210,180) : RGB(50,40,20);
        uint32_t nt_status = g_pref_darkmode ? RGB(120,116,80)  : RGB(140,120,80);

        /* Sidebar list (left ~110px) */
        int sb_w = (ww > 220) ? 110 : ww/2;
        vga_fill_rect(wx+1, content_y, sb_w, content_h, nt_sb_bg);
        vga_draw_vline(wx+sb_w, content_y, content_h, nt_sb_bd);

        /* Notes toolbar */
        vga_fill_rect(wx+1, content_y, sb_w, 20, nt_tb_bg);
        vga_draw_hline(wx+1, content_y+20, sb_w, nt_sb_bd);
        vga_draw_string_trans(wx+6, content_y+6, "Notes", nt_tb_txt);

        /* Note list */
        int i;
        for (i = 0; i < NOTES_COUNT; i++) {
            int iy = content_y + 22 + i * 36;
            if (i == g_notes_sel)
                vga_fill_rect(wx+1, iy, sb_w-1, 36, nt_sel);
            vga_draw_string_trans(wx+6, iy+4, g_notes_titles[i], nt_title);
            /* Preview first line */
            char prev[12]; int k;
            const char *body = g_notes_body[i];
            for(k=0;body[k]&&body[k]!='\n'&&k<11;k++) prev[k]=body[k];
            prev[k]=0;
            vga_draw_string_trans(wx+6, iy+18, prev, nt_prev);
            if (i < NOTES_COUNT-1)
                vga_draw_hline(wx+2, iy+35, sb_w-4, nt_sep);
        }

        /* Content area (right side) */
        int cx2 = wx + sb_w + 1;
        int cw2 = ww - sb_w - 2;
        vga_fill_rect(cx2, content_y, cw2, content_h, nt_ct_bg);

        /* Lined paper effect */
        { int line_y = content_y + 22;
          while (line_y < content_y + content_h - 4) {
              vga_draw_hline(cx2+4, line_y, cw2-8, nt_line);
              line_y += 14; }
        }
        /* Left margin line */
        vga_draw_vline(cx2+18, content_y, content_h, nt_margin);

        /* Note title */
        vga_draw_string_trans(cx2+22, content_y+4, g_notes_titles[g_notes_sel], nt_title);

        /* Note body - render with newlines */
        { const char *body2 = g_notes_body[g_notes_sel];
          int tx=cx2+22, ty=content_y+18, j2=0;
          /* Show placeholder for empty new note */
          if (g_notes_sel == NOTES_COUNT-1 && !body2[0]) {
              vga_draw_string_trans(tx, ty, g_notes_focused?"Type your note...":"Empty note - click to edit", RGB(160,160,128));
          } else {
          while (body2[j2]) {
              if (body2[j2]=='\n') { ty+=14; tx=cx2+22; j2++; continue; }
              if (tx+8 > cx2+cw2-4) { ty+=14; tx=cx2+22; }
              if (ty > content_y+content_h-18) break;
              vga_draw_char_trans(tx, ty, body2[j2], nt_body);
              tx+=8; j2++;
          }
          /* Blinking cursor on last note when focused */
          if (g_notes_focused && g_notes_sel==NOTES_COUNT-1) {
              if ((timer_ticks()/400)%2==0) vga_draw_vline(tx, ty, 12, nt_title);
          }
          }
        }

        /* Bottom status bar */
        { char nb_buf[16]; int nn=NOTES_COUNT; nb_buf[0]='0'+nn; nb_buf[1]=' '; nb_buf[2]='n'; nb_buf[3]='o'; nb_buf[4]='t'; nb_buf[5]='e'; nb_buf[6]='s'; nb_buf[7]=0;
          vga_draw_string_trans(wx+4, wy+wh-13, nb_buf, nt_status); }
        return 1;
    }

    /* Clock window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Clock")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wbx = win->x, wby = win->y, wbw = win->w;
        /* Mini tab bar */
        {
            static const char *tabs2[] = {"Clock","Alarm","Timer","Stop"};
            int tbw = wbw / 4, tby2 = wby + TITLEBAR_H + 1;
            int ti;
            uint32_t tab_bg = g_pref_darkmode ? RGB(36,36,40) : RGB(230,230,235);
            uint32_t tab_tx = g_pref_darkmode ? RGB(160,160,168) : RGB(80,80,80);
            uint32_t tab_bd = g_pref_darkmode ? RGB(55,55,60) : RGB(180,180,190);
            vga_fill_rect(wbx+1, tby2, wbw-2, 16, tab_bg);
            for (ti=0;ti<4;ti++) {
                int tbx2 = wbx+1+ti*tbw;
                if (ti==g_clock_tab) vga_fill_rect(tbx2, tby2, tbw, 16, RGB(0,122,255));
                int tl = str_len(tabs2[ti])*8;
                vga_draw_string_trans(tbx2+(tbw-tl)/2, tby2+4, tabs2[ti],
                    ti==g_clock_tab ? RGB(255,255,255) : tab_tx);
            }
            vga_draw_hline(wbx+1, tby2+16, wbw-2, tab_bd);
            /* Fill content area background */
            vga_fill_rect(wbx+1, tby2+17, wbw-2, win->h-TITLEBAR_H-17-18,
                          g_pref_darkmode ? RGB(28,28,30) : RGB(248,248,252));
        }
        /* Content area starts after tab bar */
        int content_y = win->y + TITLEBAR_H + 17;
        int content_h = win->h - TITLEBAR_H - 17 - 18;
        int cx = win->x + wbw/2;
        int cy = content_y + content_h/2;
        int R  = (wbw < content_h) ? wbw/2 - 12 : content_h/2 - 12;

        /* Alarm tab */
        if (g_clock_tab == 1) {
            static const struct { const char *t; int on; } alarms[] = {
                {"07:00", 1}, {"08:30", 0}, {"12:00", 0}, {"18:00", 1}
            };
            int ai;
            uint32_t al_bg  = g_pref_darkmode ? RGB(40,40,44) : RGB(255,255,255);
            uint32_t al_bd  = g_pref_darkmode ? RGB(60,60,65) : RGB(200,200,200);
            uint32_t al_txt = g_pref_darkmode ? RGB(210,210,218) : RGB(30,30,30);
            for (ai=0;ai<4;ai++) {
                int ay = content_y + 6 + ai*32;
                vga_fill_rect_alpha(wbx+4, ay, wbw-8, 28, al_bg, 200);
                vga_draw_rect_outline(wbx+4, ay, wbw-8, 28, al_bd);
                vga_draw_string_trans(wbx+12, ay+10, alarms[ai].t, al_txt);
                int tx2 = wbx+wbw-50;
                uint32_t tbg = alarms[ai].on ? RGB(52,199,89) : RGB(200,200,200);
                gui_draw_rounded_rect(tx2, ay+5, 36, 18, 9, tbg);
                gui_draw_circle(alarms[ai].on ? tx2+22 : tx2+4, ay+9, 8, RGB(255,255,255));
            }
            vga_draw_string_trans(wbx+8, content_y+136, "+ Add Alarm", RGB(0,122,255));
            return 1;
        }

        /* Timer tab */
        if (g_clock_tab == 2) {
            /* Show remaining time */
            int elapsed_s = g_timer_running ? (int)((timer_ticks() - g_timer_start_tick)/1000) : 0;
            int remain = g_timer_set - elapsed_s;
            if (remain < 0) remain = 0;
            int rm = remain/60, rs = remain%60;
            /* Big digital display */
            char tbuf3[6]; tbuf3[0]='0'+rm/10; tbuf3[1]='0'+rm%10;
            tbuf3[2]=':'; tbuf3[3]='0'+rs/10; tbuf3[4]='0'+rs%10; tbuf3[5]=0;
            { int ci3; int tbx3=cx-40;
              for (ci3=0;ci3<5;ci3++) {
                unsigned char ch3=(unsigned char)tbuf3[ci3];
                int r3,c3;
                for(r3=0;r3<8;r3++) for(c3=0;c3<8;c3++)
                    if(font8x8[ch3][r3]&(1u<<c3))
                        vga_fill_rect(tbx3+ci3*16+c3*2,content_y+20+r3*2,2,2,
                            g_pref_darkmode?RGB(200,200,208):RGB(30,30,30));
            }}
            /* Progress circle (rough) */
            if (g_timer_set > 0) {
                int prog_angle = remain * 360 / g_timer_set;
                gui_draw_circle(cx, content_y+80, 28, g_pref_darkmode?RGB(44,44,48):RGB(240,240,240));
                gui_draw_circle_outline(cx, content_y+80, 28, RGB(0,122,255));
                (void)prog_angle;
            }
            /* Start/Stop button */
            uint32_t bcol2 = g_timer_running ? RGB(255,59,48) : RGB(52,199,89);
            gui_draw_rounded_rect(cx-28, content_y+118, 56, 20, 10, bcol2);
            vga_draw_string_trans(cx-(g_timer_running?5*8/2:4*8/2),
                content_y+123, g_timer_running?"Stop":"Start", RGB(255,255,255));
            return 1;
        }

        /* Stopwatch tab */
        if (g_clock_tab == 3) {
            uint32_t elapsed2 = g_stopwatch_elapsed;
            if (g_stopwatch_running) elapsed2 += timer_ticks() - g_stopwatch_start;
            uint32_t ss2 = elapsed2/1000, ms2 = (elapsed2%1000)/10;
            uint32_t mm2 = ss2/60; ss2 %= 60;
            char sbuf[9];
            sbuf[0]='0'+mm2/10; sbuf[1]='0'+mm2%10; sbuf[2]=':';
            sbuf[3]='0'+ss2/10; sbuf[4]='0'+ss2%10; sbuf[5]='.';
            sbuf[6]='0'+ms2/10; sbuf[7]='0'+ms2%10; sbuf[8]=0;
            /* Big display */
            { int ci3; int tbx3=cx-64;
              for (ci3=0;ci3<8;ci3++) {
                unsigned char ch3=(unsigned char)sbuf[ci3];
                int r3,c3;
                for(r3=0;r3<8;r3++) for(c3=0;c3<8;c3++)
                    if(font8x8[ch3][r3]&(1u<<c3))
                        vga_fill_rect(tbx3+ci3*16+c3*2,content_y+20+r3*2,2,2,
                            g_stopwatch_running?RGB(255,59,48):(g_pref_darkmode?RGB(200,200,208):RGB(30,30,30)));
            }}
            /* Start/Stop */
            uint32_t bcol3 = g_stopwatch_running ? RGB(255,59,48) : RGB(52,199,89);
            gui_draw_rounded_rect(cx-28, content_y+88, 56, 20, 10, bcol3);
            vga_draw_string_trans(cx-(g_stopwatch_running?5*8/2:4*8/2),
                content_y+93, g_stopwatch_running?"Stop":"Start", RGB(255,255,255));
            /* Reset */
            if (!g_stopwatch_running) {
                gui_draw_rounded_rect(cx-20, content_y+116, 40, 18, 9, RGB(150,150,155));
                vga_draw_string_trans(cx-20, content_y+121, "Reset", RGB(255,255,255));
            }
            return 1;
        }

        /* Face */
        uint32_t clk_face = g_pref_darkmode ? RGB(44,44,48) : RGB(250,250,250);
        uint32_t clk_ring = g_pref_darkmode ? RGB(80,80,88) : RGB(100,100,100);
        uint32_t clk_tick = g_pref_darkmode ? RGB(160,160,168) : RGB(50,50,50);
        uint32_t clk_hand = g_pref_darkmode ? RGB(220,220,224) : RGB(30,30,30);
        uint32_t clk_dot  = g_pref_darkmode ? RGB(180,180,188) : RGB(50,50,50);
        uint32_t clk_txt  = g_pref_darkmode ? RGB(200,200,208) : COLOR_TEXT;
        gui_draw_circle(cx, cy, R, clk_face);
        gui_draw_rounded_rect_outline(cx-R, cy-R, R*2, R*2, R, clk_ring);

        /* 12 tick marks */
        {
            /* sin/cos for 30-degree increments (fixed-point x1000) */
            static const int cos12[12] = {
                 0,  866,  1000,  866,   0, -866, -1000, -866, 0, 866, 1000, 866
            };
            static const int sin12[12] = {
                1000,  866,    0, -866,-1000, -866,    0,  866, 1000, 866, 0, -866
            };
            /* Corrected: 0=top(12), going clockwise */
            static const int sinT[12] = { 0, 500, 866,1000, 866, 500,  0,-500,-866,-1000,-866,-500};
            static const int cosT[12] = {-1000,-866,-500,   0, 500, 866,1000, 866, 500,    0,-500,-866};
            int t2;
            for (t2 = 0; t2 < 12; t2++) {
                int ix = cx + sinT[t2] * R / 1000;
                int iy = cy + cosT[t2] * R / 1000;
                int ox = cx + sinT[t2] * (R-6) / 1000;
                int oy = cy + cosT[t2] * (R-6) / 1000;
                vga_draw_line(ix, iy, ox, oy, clk_tick);
            }
            (void)cos12; (void)sin12;
        }

        /* Time: use RTC clock */
        {
            datetime_t dt;
            uint32_t h12;
            uint32_t m;
            uint32_t s;
            get_current_datetime(&dt);
            h12 = (uint32_t)(dt.hour % 12);
            m = (uint32_t)dt.minute;
            s = (uint32_t)dt.second;

            /* sin/cos table for 60 steps (minutes/seconds), 12 steps * 5 */
            /* angle_deg = step * 6 degrees, 0 = top, clockwise */
            /* precompute: sin[i] = sin(i*6*PI/180)*1000 */
            static const int s60[60] = {
                   0,  105,  208,  309,  407,  500,  588,  669,  743,  809,
                 866,  914,  951,  978,  995, 1000,  995,  978,  951,  914,
                 866,  809,  743,  669,  588,  500,  407,  309,  208,  105,
                   0, -105, -208, -309, -407, -500, -588, -669, -743, -809,
                -866, -914, -951, -978, -995,-1000, -995, -978, -951, -914,
                -866, -809, -743, -669, -588, -500, -407, -309, -208, -105
            };
            static const int c60[60] = {
               -1000, -995, -978, -951, -914, -866, -809, -743, -669, -588,
                -500, -407, -309, -208, -105,    0,  105,  208,  309,  407,
                 500,  588,  669,  743,  809,  866,  914,  951,  978,  995,
                1000,  995,  978,  951,  914,  866,  809,  743,  669,  588,
                 500,  407,  309,  208,  105,    0, -105, -208, -309, -407,
                -500, -588, -669, -743, -809, -866, -914, -951, -978, -995
            };

            /* Second hand (thin, red) */
            {
                int si = (int)(s % 60);
                int ex = cx + s60[si] * (R-4) / 1000;
                int ey = cy + c60[si] * (R-4) / 1000;
                vga_draw_line(cx, cy, ex, ey, RGB(220, 50, 50));
            }
            /* Minute hand */
            {
                int mi = (int)(m % 60);
                int ex = cx + s60[mi] * (R-10) / 1000;
                int ey = cy + c60[mi] * (R-10) / 1000;
                vga_draw_line_thick(cx, cy, ex, ey, 2, clk_hand);
            }
            /* Hour hand */
            {
                int hi5 = (int)((h12 * 5 + m/12) % 60);
                int ex = cx + s60[hi5] * (R-20) / 1000;
                int ey = cy + c60[hi5] * (R-20) / 1000;
                vga_draw_line_thick(cx, cy, ex, ey, 3, clk_hand);
            }
            /* Center dot */
            gui_draw_circle(cx, cy, 4, clk_dot);

            /* Digital time display */
            {
                char tbuf[9];
                uint32_t hh = (uint32_t)(dt.hour % 24);
                tbuf[0] = '0'+hh/10; tbuf[1] = '0'+hh%10; tbuf[2] = ':';
                tbuf[3] = '0'+m/10;  tbuf[4] = '0'+m%10;  tbuf[5] = ':';
                tbuf[6] = '0'+s/10;  tbuf[7] = '0'+s%10;  tbuf[8] = 0;
                int tw = 8 * 8;
                vga_draw_string_trans(cx - tw/2, cy + R + 4, tbuf, clk_txt);
            }
        }
        return 1;
    }

    /* Maps window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Maps")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy+TITLEBAR_H+1;
        int ch = wh-TITLEBAR_H-19;
        /* Zoom/pan transform: virtual coords [0..ww, 0..ch] → screen coords */
        int mz = g_maps_zoom < 1 ? 1 : (g_maps_zoom > 4 ? 4 : g_maps_zoom);
        int mpx = g_maps_pan_x, mpy = g_maps_pan_y;
#define MX(vx) (wx + ((vx) - ww/2)*mz + ww/2 + mpx)
#define MY(vy) (cy + ((vy) - ch/2)*mz + ch/2 + mpy)
#define MW(vw) ((vw)*mz)
#define MH(vh) ((vh)*mz)
        /* Color scheme */
        uint32_t mp_land  = g_pref_darkmode ? RGB(38,42,38) : RGB(234,230,220);
        uint32_t mp_park  = g_pref_darkmode ? RGB(30,50,30) : RGB(190,224,166);
        uint32_t mp_water = g_pref_darkmode ? RGB(20,35,70) : RGB(154,196,232);
        uint32_t mp_road  = g_pref_darkmode ? RGB(70,68,58) : RGB(255,255,255);
        uint32_t mp_hway  = g_pref_darkmode ? RGB(110,90,40): RGB(255,214,100);
        uint32_t mp_bldg  = g_pref_darkmode ? RGB(55,55,50) : RGB(210,208,200);
        uint32_t mp_txt   = g_pref_darkmode ? RGB(200,200,200):RGB(80,70,60);
        uint32_t mp_water2= g_pref_darkmode ? RGB(15,30,60) : RGB(120,170,220);

        /* Map background */
        vga_fill_rect(wx+1, cy, ww-2, ch, mp_land);

        if (g_maps_view == 1 || g_maps_view == 2) {
            /* Satellite/Hybrid: terrain gradient */
            int ry2;
            for (ry2=0; ry2<ch; ry2++) {
                uint8_t rr2=(uint8_t)(25+ry2*20/ch), gg2=(uint8_t)(55+ry2*30/ch), bb2=(uint8_t)(15);
                vga_draw_hline(wx+1, cy+ry2, ww-2, RGB(rr2,gg2,bb2));
            }
            /* Han River (water body) */
            vga_fill_rect(MX(1), MY(ch*2/5), MW(ww-2), MH(ch/5), RGB(20,50,100));
            /* Tree clusters */
            { int ti; for(ti=0;ti<10;ti++) {
                int tx3 = 10 + ti*32; if (tx3 > ww-18) tx3 = tx3%(ww-18);
                int ty3 = ch/2+ch/5+5+ti*4;
                vga_fill_rect(MX(tx3), MY(ty3), MW(16), MH(12), RGB(20,70,20));
            }}
            if (g_maps_view == 2) {
                /* Hybrid: add road overlay */
                vga_fill_rect(MX(ww/3), MY(0), MW(4), MH(ch), mp_hway);
                vga_fill_rect(MX(1), MY(ch/3), MW(ww-2), MH(4), mp_hway);
                vga_fill_rect(MX(ww*2/3), MY(0), MW(3), MH(ch), mp_road);
                vga_fill_rect(MX(1), MY(ch*2/3), MW(ww-2), MH(3), mp_road);
            }
        } else {
            /* Standard map - detailed street view */
            /* Parks */
            vga_fill_rect(MX(ww/4), MY(ch/4), MW(ww/3), MH(ch/4), mp_park);
            vga_fill_rect(MX(ww*3/5), MY(ch*3/5), MW(ww/5), MH(ch/5), mp_park);
            /* Han River */
            vga_fill_rect(MX(1), MY(ch*2/5), MW(ww-2), MH(ch/6), mp_water);
            /* Inset river detail */
            vga_fill_rect(MX(ww/4), MY(ch*2/5), MW(ww/2), MH(ch/6-2), mp_water2);
            /* Major roads (highways) */
            vga_fill_rect(MX(ww/3-2), MY(0), MW(6), MH(ch), mp_hway);
            vga_fill_rect(MX(1), MY(ch/3-2), MW(ww-2), MH(6), mp_hway);
            /* Secondary roads */
            vga_fill_rect(MX(ww*2/3), MY(0), MW(3), MH(ch), mp_road);
            vga_fill_rect(MX(1), MY(ch*2/3), MW(ww-2), MH(3), mp_road);
            vga_fill_rect(MX(ww/6), MY(0), MW(2), MH(ch), mp_road);
            vga_fill_rect(MX(1), MY(ch/6), MW(ww-2), MH(2), mp_road);
            vga_fill_rect(MX(ww*5/6), MY(0), MW(2), MH(ch), mp_road);
            vga_fill_rect(MX(1), MY(ch*5/6), MW(ww-2), MH(2), mp_road);
            /* Building blocks */
            { int bx, by;
              for (bx=0; bx<4; bx++) for (by=0; by<3; by++) {
                  int vbx2 = 6 + bx*(ww/5);
                  int vby2 = 6 + by*(ch/4);
                  if (vby2+18 < ch*2/5 || vby2 > ch*2/5+ch/6) {
                      int bw = 14+bx*3, bh = 10+by*2;
                      if (bw > ww/5-8) bw=ww/5-8;
                      vga_fill_rect(MX(vbx2), MY(vby2), MW(bw), MH(bh), mp_bldg);
                      vga_draw_rect_outline(MX(vbx2), MY(vby2), MW(bw), MH(bh), g_pref_darkmode?RGB(65,65,60):RGB(190,185,178));
                  }
              }
            }
            /* POI labels (clipped to window) */
            { int lx, ly;
              lx=MX(ww/4+4); ly=MY(ch/4+4);
              if (lx>wx && lx<wx+ww && ly>cy && ly<cy+ch)
                  vga_draw_string_trans(lx, ly, "Yeouido Park", mp_txt);
              lx=MX(8); ly=MY(ch*2/5+6);
              if (lx>wx && lx<wx+ww && ly>cy && ly<cy+ch)
                  vga_draw_string_trans(lx, ly, "Han River", g_pref_darkmode?RGB(100,130,200):RGB(60,100,160));
              lx=MX(ww*2/3+4); ly=MY(ch/2);
              if (lx>wx && lx<wx+ww && ly>cy && ly<cy+ch)
                  vga_draw_string_trans(lx, ly, "Gangnam", mp_txt);
              lx=MX(ww/3-14); ly=MY(ch/3-12);
              if (lx>wx && lx<wx+ww && ly>cy && ly<cy+ch)
                  vga_draw_string_trans(lx, ly, "88", g_pref_darkmode?RGB(160,140,80):RGB(180,140,0));
            }
        }

        /* Current location pin (pan-aware, animated pulse) */
        { int px2 = MX(ww/2+20), py2 = MY(ch/2-10);
          if (px2>wx && px2<wx+ww && py2>cy && py2<cy+ch) {
              uint32_t t6=timer_ticks();
              int pulse_r = (int)((t6/150)%20);
              vga_fill_rect_alpha(px2-pulse_r/2, py2-pulse_r/2, pulse_r, pulse_r, RGB(0,100,255), 40);
              gui_draw_circle(px2, py2, 9, RGB(0,122,255));
              gui_draw_circle(px2, py2, 6, RGB(255,255,255));
              gui_draw_circle(px2, py2, 3, RGB(0,122,255));
              vga_fill_rect_alpha(px2-4, py2+3, 8, 3, RGB(0,0,0), 60);
          }
        }
#undef MX
#undef MY
#undef MW
#undef MH

        /* ---- HUD elements (fixed screen position, not transformed) ---- */

        /* Search bar - floating pill */
        uint32_t mp_sb_bg = g_pref_darkmode ? RGB(44,44,50) : RGB(255,255,255);
        uint32_t mp_sb_tx = g_pref_darkmode ? RGB(110,110,118) : RGB(160,160,165);
        vga_fill_rect_alpha(wx+8, cy+6, ww-72, 22, mp_sb_bg, 240);
        gui_draw_rounded_rect_outline(wx+8, cy+6, ww-72, 22, 6, g_pref_darkmode?RGB(65,65,70):RGB(190,190,195));
        gui_draw_circle(wx+20, cy+17, 5, mp_sb_tx);
        gui_draw_circle(wx+20, cy+17, 3, mp_sb_bg);
        vga_draw_line(wx+24, cy+21, wx+28, cy+25, mp_sb_tx);
        vga_draw_string_trans(wx+30, cy+11, "Search Maps...", mp_sb_tx);

        /* View toggle button group (top right) */
        { static const char *vm_labels[3]={"Map","Sat","Mix"};
          int vi;
          uint32_t vbg_def = g_pref_darkmode ? RGB(44,44,50) : RGB(255,255,255);
          uint32_t vtc_def = g_pref_darkmode ? RGB(180,180,188) : RGB(40,40,40);
          for(vi=0;vi<3;vi++) {
              int vbx=wx+ww-68+vi*22, vby=cy+6;
              uint32_t vbg=(vi==g_maps_view)?RGB(0,122,255):vbg_def;
              uint32_t vtc=(vi==g_maps_view)?RGB(255,255,255):vtc_def;
              gui_draw_rounded_rect(vbx, vby, 22, 22, 4, vbg);
              gui_draw_rounded_rect_outline(vbx, vby, 22, 22, 4, g_pref_darkmode?RGB(65,65,70):RGB(190,190,195));
              vga_draw_string_trans(vbx+2, vby+7, vm_labels[vi], vtc);
          }
        }

        /* Zoom +/- buttons */
        { uint32_t zbg = g_pref_darkmode ? RGB(44,44,50) : RGB(255,255,255);
          uint32_t ztc = g_pref_darkmode ? RGB(180,180,188) : RGB(40,40,40);
          int zx = wx+ww-26;
          gui_draw_rounded_rect(zx, cy+36, 22, 22, 4, zbg);
          gui_draw_rounded_rect_outline(zx, cy+36, 22, 22, 4, g_pref_darkmode?RGB(65,65,70):RGB(190,190,195));
          vga_draw_string_trans(zx+7, cy+43, "+", ztc);
          gui_draw_rounded_rect(zx, cy+60, 22, 22, 4, zbg);
          gui_draw_rounded_rect_outline(zx, cy+60, 22, 22, 4, g_pref_darkmode?RGB(65,65,70):RGB(190,190,195));
          vga_draw_string_trans(zx+7, cy+67, "-", ztc);
          /* Zoom level indicator */
          if (mz > 1) {
              char zlbl[4]; zlbl[0]='x'; zlbl[1]='0'+mz; zlbl[2]=0;
              vga_draw_string_trans(zx+4, cy+85, zlbl, ztc);
          }
        }

        /* Route card at bottom */
        { int rc_y = cy + ch - 38;
          vga_fill_rect_alpha(wx+8, rc_y, ww-16, 32, g_pref_darkmode?RGB(30,30,35):RGB(255,255,255), 230);
          gui_draw_rounded_rect_outline(wx+8, rc_y, ww-16, 32, 6, g_pref_darkmode?RGB(60,60,65):RGB(200,200,205));
          vga_fill_rect(wx+14, rc_y+8, 6, 6, RGB(0,122,255));
          vga_draw_vline(wx+16, rc_y+14, 5, g_pref_darkmode?RGB(100,100,108):RGB(160,160,165));
          vga_fill_rect(wx+14, rc_y+19, 6, 6, RGB(255,59,48));
          vga_draw_string_trans(wx+24, rc_y+6, "Gyeongbokgung Palace", g_pref_darkmode?RGB(210,210,218):RGB(30,30,35));
          vga_draw_string_trans(wx+24, rc_y+18, "12 min  *  3.2 km  *  Drive", g_pref_darkmode?RGB(130,130,138):RGB(100,100,108));
          gui_draw_rounded_rect(wx+ww-56, rc_y+6, 44, 20, 5, RGB(0,122,255));
          vga_draw_string_trans(wx+ww-48, rc_y+10, "Start", RGB(255,255,255));
        }

        /* Scale bar (zoom-aware) */
        { uint32_t mp_sc = g_pref_darkmode?RGB(200,200,200):RGB(60,60,60);
          vga_fill_rect_alpha(wx+ww-58, cy+ch-18, 38, 1, mp_sc, 200);
          const char *scale_lbl = mz==1?"500m":mz==2?"250m":mz==3?"100m":"50m";
          vga_draw_string_trans(wx+ww-57, cy+ch-16, scale_lbl, mp_sc);
        }
        return 1;
    }

    /* Activity Monitor window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Activity Monitor")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy2 = wy+TITLEBAR_H+1;
        int i;

        /* CPU usage graph - dynamic based on timer */
        uint32_t t_am = timer_ticks();
        runtime_system_info_t am_sys;
        runtime_storage_info_t am_storage;
        int am_has_storage;
        runtime_get_system_info(&am_sys);
        am_has_storage = (runtime_get_storage_info("/", &am_storage) == 0);
        uint32_t am_bg_c = g_pref_darkmode ? RGB(28,28,30) : RGB(245,245,248);
        uint32_t am_hdr  = g_pref_darkmode ? RGB(44,44,48) : RGB(220,220,225);
        uint32_t am_txt  = g_pref_darkmode ? RGB(210,210,218) : RGB(60,60,60);
        uint32_t am_sep  = g_pref_darkmode ? RGB(60,60,65) : RGB(200,200,200);
        vga_fill_rect(wx+1, cy2, ww-2, wh-TITLEBAR_H-19, am_bg_c);

        /* Tab bar */
        static const char *am_tabs[] = {"CPU","Memory","Disk","Network"};
        int tab_w = (ww-2)/4;
        for (i=0;i<4;i++) {
            int ttx=wx+1+i*tab_w;
            int sel = (i == g_am_tab);
            vga_fill_rect(ttx, cy2, tab_w, 22, sel?(g_pref_darkmode?RGB(55,55,60):RGB(255,255,255)):am_hdr);
            vga_draw_rect_outline(ttx, cy2, tab_w, 22, am_sep);
            int tl=str_len(am_tabs[i])*8;
            vga_draw_string_trans(ttx+(tab_w-tl)/2, cy2+7, am_tabs[i],
                sel?RGB(0,100,220):am_txt);
            if (sel) vga_fill_rect(ttx, cy2+20, tab_w, 2, RGB(0,100,220));
        }
        cy2 += 24;

        static const int wave60[60] = {
            0,10,19,29,37,44,50,56,59,62,64,65,64,62,59,56,50,44,37,29,
           19,10,0,-10,-19,-29,-37,-44,-50,-56,-59,-62,-64,-65,-64,-62,
          -59,-56,-50,-44,-37,-29,-19,-10,0,10,19,29,37,44,50,56,59,62,
           64,65,64,62,59,56
        };
        static const int wave2[60] = {
            0,5,9,13,17,20,22,24,26,27,28,27,26,24,22,20,17,13,9,5,
            0,-5,-9,-13,-17,-20,-22,-24,-26,-27,-28,-27,-26,-24,-22,-20,
          -17,-13,-9,-5,0,5,9,13,17,20,22,24,26,27,28,27,26,24,22,20,
           17,13,9,5
        };
        if (g_am_tab == 0) {
            /* ---- CPU TAB ---- */
            vga_draw_string_trans(wx+8, cy2+2, "CPU Usage", am_txt);
            static const int cpu_base[6] = {60, 35, 78, 22, 48, 56};
            static const int cpu_amp[6]  = {18,  8, 14,  9, 14, 10};
            int bar_h = 40;
            int bar_w2 = (ww-20)/6;
            for (i=0;i<6;i++) {
                int phase = (int)((t_am/200 + i*13) % 60);
                int dyn = cpu_base[i] + wave60[phase] * cpu_amp[i] / 100;
                if (dyn < 5)  dyn = 5;
                if (dyn > 99) dyn = 99;
                int bx=wx+8+i*bar_w2, by2=cy2+18;
                int filled = bar_h * dyn / 100;
                vga_fill_rect(bx, by2, bar_w2-4, bar_h, am_hdr);
                uint32_t bar_col = dyn>75?RGB(255,59,48):(dyn>50?RGB(255,149,0):RGB(52,199,89));
                vga_fill_rect(bx, by2+bar_h-filled, bar_w2-4, filled, bar_col);
                vga_draw_rect_outline(bx, by2, bar_w2-4, bar_h, am_sep);
                char cn[3]; cn[0]='C'; cn[1]='0'+i; cn[2]=0;
                vga_draw_string_trans(bx, by2+bar_h+2, cn, am_txt);
            }
            cy2 += 72;
            vga_draw_hline(wx+4, cy2, ww-8, am_sep); cy2 += 6;
            /* Process list */
            vga_fill_rect(wx+1, cy2, ww-2, 14, am_hdr);
            vga_draw_string_trans(wx+6, cy2+3, "Process", am_txt);
            vga_draw_string_trans(wx+ww-80, cy2+3, "CPU%", am_txt);
            vga_draw_string_trans(wx+ww-40, cy2+3, "RAM", am_txt);
            cy2 += 14;
            static const struct { const char *name; int cpu_b; int ram; } procs[] = {
                { "gui",        22,  18 },
                { "kernel",      5,  12 },
                { "timer_irq",   3,   1 },
                { "kb_handler",  1,   1 },
                { "mouse_drv",   1,   1 },
                { "vga_blitter", 8,   4 },
            };
            int np = 6;
            for (i=0;i<np;i++) {
                int phase2 = (int)((t_am/400 + i*17) % 60);
                int dyn_cpu = procs[i].cpu_b + wave2[phase2];
                if (dyn_cpu < 0) dyn_cpu = 0;
                if (dyn_cpu > 99) dyn_cpu = 99;
                uint32_t row_bg = (i%2==0) ? (g_pref_darkmode?RGB(38,38,42):RGB(255,255,255))
                                            : (g_pref_darkmode?RGB(32,32,36):RGB(248,248,252));
                vga_fill_rect(wx+1, cy2+i*14, ww-2, 14, row_bg);
                vga_draw_string_trans(wx+6, cy2+i*14+3, procs[i].name, g_pref_darkmode?RGB(210,210,218):RGB(30,30,30));
                int cpub2 = dyn_cpu * (ww/4) / 100;
                vga_fill_rect(wx+ww-110, cy2+i*14+4, cpub2, 6, dyn_cpu>15?RGB(255,59,48):RGB(52,199,89));
                char cpus[4]; cpus[0]='0'+dyn_cpu/10; cpus[1]='0'+dyn_cpu%10; cpus[2]='%'; cpus[3]=0;
                vga_draw_string_trans(wx+ww-80, cy2+i*14+3, cpus, am_txt);
                char rams[5]; rams[0]='0'+procs[i].ram/10; rams[1]='0'+procs[i].ram%10;
                rams[2]='M'; rams[3]='B'; rams[4]=0;
                vga_draw_string_trans(wx+ww-40, cy2+i*14+3, rams, am_txt);
            }
            /* Footer */
            int fy2 = wy+wh-20;
            int avg_cpu = am_sys.cpu_load_percent;
            vga_fill_rect(wx+1, fy2, ww-2, 19, am_hdr);
            vga_draw_hline(wx+1, fy2, ww-2, am_sep);
            vga_draw_string_trans(wx+6, fy2+6, "CPU Load:", am_txt);
            int load_w = (ww-100)*avg_cpu/100;
            vga_fill_rect(wx+80, fy2+6, ww-110, 8, am_sep);
            uint32_t lbar_col2 = avg_cpu>75?RGB(255,59,48):(avg_cpu>50?RGB(255,149,0):RGB(0,122,255));
            vga_fill_rect(wx+80, fy2+6, load_w, 8, lbar_col2);
            { char pct3[4]; pct3[0]='0'+avg_cpu/10; pct3[1]='0'+avg_cpu%10; pct3[2]='%'; pct3[3]=0;
              vga_draw_string_trans(wx+ww-24, fy2+6, pct3, am_txt); }

        } else if (g_am_tab == 1) {
            /* ---- MEMORY TAB ---- */
            uint32_t phys_used = am_sys.pmm_total_bytes >= am_sys.pmm_free_bytes ? am_sys.pmm_total_bytes - am_sys.pmm_free_bytes : 0;
            int phys_used_pct = am_sys.pmm_total_pages ? (int)((am_sys.pmm_total_pages - am_sys.pmm_free_pages) * 100U / am_sys.pmm_total_pages) : 0;
            int phys_free_pct = am_sys.pmm_total_pages ? (int)(am_sys.pmm_free_pages * 100U / am_sys.pmm_total_pages) : 0;
            char mem_total_buf[16];
            char lbl_used[20];
            char lbl_free[20];
            char lbl_heap[20];
            char lbl_tasks[20];
            int lp;
            runtime_format_bytes(am_sys.pmm_total_bytes, mem_total_buf, sizeof(mem_total_buf));
            lbl_used[0] = 0; lp = 0;
            apps1_append_text(lbl_used, &lp, sizeof(lbl_used), "Used ");
            { char pct[8]; runtime_format_percent(phys_used_pct, pct, sizeof(pct)); apps1_append_text(lbl_used, &lp, sizeof(lbl_used), pct); }
            lbl_free[0] = 0; lp = 0;
            apps1_append_text(lbl_free, &lp, sizeof(lbl_free), "Free ");
            { char pct[8]; runtime_format_percent(phys_free_pct, pct, sizeof(pct)); apps1_append_text(lbl_free, &lp, sizeof(lbl_free), pct); }
            lbl_heap[0] = 0; lp = 0;
            apps1_append_text(lbl_heap, &lp, sizeof(lbl_heap), "Heap ");
            { char pct[8]; runtime_format_percent(am_sys.mem_used_percent, pct, sizeof(pct)); apps1_append_text(lbl_heap, &lp, sizeof(lbl_heap), pct); }
            lbl_tasks[0] = 0; lp = 0;
            apps1_append_text(lbl_tasks, &lp, sizeof(lbl_tasks), "Tasks ");
            apps1_append_uint(lbl_tasks, &lp, sizeof(lbl_tasks), am_sys.task_count);
            vga_draw_string_trans(wx+8, cy2+2, "Memory Pressure", am_txt);
            /* Donut-style memory chart */
            int cx_d = wx + ww/2, cy_d = cy2 + 45;
            int r_o = 36, r_i = 24;
            uint32_t m_cols[4] = {RGB(0,122,255), RGB(255,149,0), RGB(255,59,48), RGB(52,199,89)};
            const char *m_lbl[4] = {lbl_used, lbl_free, lbl_heap, lbl_tasks};
            /* Draw filled donut approximation with rectangles */
            { int ang, dr;
              for (ang=0; ang<360; ang+=3) {
                  int seg = (ang < 162) ? 0 : (ang < 227) ? 1 : (ang < 270) ? 2 : 3;
                  for (dr=r_i; dr<r_o; dr++) {
                      int px2 = cx_d + (int)(dr * 10 * (ang%90<45?1:-1) / 100);
                      int py2 = cy_d - dr + (ang/90)*dr/2;
                      /* simplified: just draw colored circles at key angles */
                      (void)px2; (void)py2;
                  }
                  (void)seg;
              }
            }
            /* Simple ring: 4 colored arcs as filled rects */
            gui_draw_circle(cx_d, cy_d, r_o, m_cols[0]);
            gui_draw_circle(cx_d, cy_d, r_o-6, m_cols[1]);
            gui_draw_circle(cx_d-8, cy_d-12, r_o-12, m_cols[2]);
            gui_draw_circle(cx_d, cy_d, r_i+2, am_bg_c); /* inner hole */
            vga_draw_string_trans(cx_d-8, cy_d-4, mem_total_buf, am_txt);
            /* Legend */
            { int li;
              for (li=0;li<4;li++) {
                  int lx2 = wx+8 + (li%2)*(ww/2-8);
                  int ly2 = cy2+90 + (li/2)*14;
                  vga_fill_rect(lx2, ly2+2, 8, 8, m_cols[li]);
                  vga_draw_string_trans(lx2+10, ly2, m_lbl[li], am_txt); } }
            cy2 += 118;
            vga_draw_hline(wx+4, cy2, ww-8, am_sep); cy2 += 6;
            /* Memory detail rows */
            vga_fill_rect(wx+1, cy2, ww-2, 14, am_hdr);
            vga_draw_string_trans(wx+6, cy2+3, "Category", am_txt);
            vga_draw_string_trans(wx+ww-64, cy2+3, "Bytes", am_txt);
            cy2 += 14;
            struct { const char *cat; uint32_t bytes; } mem_rows[] = {
                {"Physical Used", phys_used},
                {"Physical Free", am_sys.pmm_free_bytes},
                {"Heap Used",     am_sys.heap_used_bytes},
                {"Heap Free",     am_sys.heap_free_bytes},
            };
            for (i=0;i<4;i++) {
                uint32_t rb = (i%2==0)?(g_pref_darkmode?RGB(38,38,42):RGB(255,255,255)):(g_pref_darkmode?RGB(32,32,36):RGB(248,248,252));
                vga_fill_rect(wx+1, cy2+i*14, ww-2, 14, rb);
                vga_draw_string_trans(wx+6, cy2+i*14+3, mem_rows[i].cat, am_txt);
                { char bytesbuf[16];
                  runtime_format_bytes(mem_rows[i].bytes, bytesbuf, sizeof(bytesbuf));
                  vga_draw_string_trans(wx+ww-64, cy2+i*14+3, bytesbuf, am_txt); }
            }
            int fy3 = wy+wh-20;
            vga_fill_rect(wx+1, fy3, ww-2, 19, am_hdr);
            vga_draw_hline(wx+1, fy3, ww-2, am_sep);
            { char membuf[40];
              apps1_format_used_total(phys_used, am_sys.pmm_total_bytes, membuf, sizeof(membuf));
              vga_draw_string_trans(wx+6, fy3+6, membuf, am_txt); }

        } else if (g_am_tab == 2) {
            /* ---- DISK TAB ---- */
            vga_draw_string_trans(wx+8, cy2+2, "Disk Activity", am_txt);
            /* Disk usage bar */
            int disk_pct = am_has_storage ? am_storage.used_percent : 0;
            int du_w = ww-20;
            int du_filled = du_w * disk_pct / 100;
            vga_fill_rect(wx+8, cy2+18, du_w, 14, am_sep);
            vga_fill_rect(wx+8, cy2+18, du_filled, 14, RGB(0,122,255));
            vga_draw_rect_outline(wx+8, cy2+18, du_w, 14, am_sep);
            if (am_has_storage) {
                char diskbuf[40];
                apps1_format_used_total(am_storage.used_bytes, am_storage.total_bytes, diskbuf, sizeof(diskbuf));
                vga_draw_string_trans(wx+8, cy2+34, diskbuf, am_txt);
            } else {
                vga_draw_string_trans(wx+8, cy2+34, "storage unavailable", am_txt);
            }
            cy2 += 54;
            /* Read/Write animated graph */
            vga_draw_string_trans(wx+8, cy2, "I/O Throughput", am_txt);
            cy2 += 14;
            { int gi;
              int gw = ww-18, gh = 50;
              vga_fill_rect(wx+8, cy2, gw, gh, am_hdr);
              vga_draw_rect_outline(wx+8, cy2, gw, gh, am_sep);
              /* Animated read/write lines */
              for (gi=0; gi<gw-1; gi++) {
                  int ph_r = (int)((t_am/150 + gi*2) % 60);
                  int ph_w = (int)((t_am/200 + gi*3 + 20) % 60);
                  int rv = gh/2 + wave60[ph_r]*gh/180;
                  int wv = gh/2 + wave60[ph_w]*gh/250;
                  if (rv<1) rv=1;
                  if (rv>=gh) rv=gh-1;
                  if (wv<1) wv=1;
                  if (wv>=gh) wv=gh-1;
                  vga_fill_rect(wx+8+gi, cy2+gh-rv, 1, 2, RGB(0,122,255));
                  vga_fill_rect(wx+8+gi, cy2+gh-wv, 1, 2, RGB(255,149,0));
              }
              cy2 += gh+4;
              vga_draw_string_trans(wx+8,  cy2, "Read: activity", am_txt);
              vga_draw_string_trans(wx+ww/2, cy2, "Write: activity", am_txt);
            }
            cy2 += 14;
            vga_draw_hline(wx+4, cy2, ww-8, am_sep); cy2+=6;
            /* Disk list */
            if (am_has_storage) {
                char diskname[40];
                int dp = 0;
                diskname[0] = 0;
                apps1_append_text(diskname, &dp, sizeof(diskname), am_storage.name);
                apps1_append_text(diskname, &dp, sizeof(diskname), " / ");
                apps1_append_bytes(diskname, &dp, sizeof(diskname), am_storage.total_bytes);
                vga_fill_rect(wx+1, cy2, ww-2, 18, am_hdr);
                vga_draw_string_trans(wx+6, cy2+5, diskname, am_txt);
                vga_fill_rect(wx+ww-70, cy2+5, ww-80, 8, am_sep);
                vga_fill_rect(wx+ww-70, cy2+5, (ww-80)*disk_pct/100, 8, RGB(52,199,89));
            }
            int fy4 = wy+wh-20;
            vga_fill_rect(wx+1, fy4, ww-2, 19, am_hdr);
            vga_draw_hline(wx+1, fy4, ww-2, am_sep);
            if (am_has_storage) {
                char blkbuf[32];
                int bp = 0;
                blkbuf[0] = 0;
                apps1_append_text(blkbuf, &bp, sizeof(blkbuf), "Free blocks: ");
                apps1_append_uint(blkbuf, &bp, sizeof(blkbuf), am_storage.free_blocks);
                vga_draw_string_trans(wx+6, fy4+6, blkbuf, am_txt);
            }

        } else {
            /* ---- NETWORK TAB ---- */
            vga_draw_string_trans(wx+8, cy2+2, "Network Activity", am_txt);
            cy2 += 16;
            /* Animated tx/rx lines */
            { int gi;
              int gw2 = ww-18, gh2 = 60;
              vga_fill_rect(wx+8, cy2, gw2, gh2, am_hdr);
              vga_draw_rect_outline(wx+8, cy2, gw2, gh2, am_sep);
              vga_draw_string_trans(wx+10, cy2+2, "TX", RGB(255,149,0));
              vga_draw_string_trans(wx+28, cy2+2, "RX", RGB(52,199,89));
              for (gi=0; gi<gw2-1; gi++) {
                  int ph_t = (int)((t_am/120 + gi*2) % 60);
                  int ph_r = (int)((t_am/180 + gi*3 + 30) % 60);
                  int tv = gh2/2 + wave60[ph_t]*gh2/190;
                  int rv2 = gh2/2 + wave60[ph_r]*gh2/220;
                  if (tv<2) tv=2;
                  if (tv>=gh2) tv=gh2-1;
                  if (rv2<2) rv2=2;
                  if (rv2>=gh2) rv2=gh2-1;
                  vga_fill_rect(wx+8+gi, cy2+gh2-tv, 1, 2, RGB(255,149,0));
                  vga_fill_rect(wx+8+gi, cy2+gh2-rv2, 1, 2, RGB(52,199,89));
              }
              cy2 += gh2 + 6;
            }
            vga_draw_hline(wx+4, cy2, ww-8, am_sep); cy2+=6;
            /* Interface list */
            vga_fill_rect(wx+1, cy2, ww-2, 14, am_hdr);
            vga_draw_string_trans(wx+6, cy2+3, "Interface", am_txt);
            vga_draw_string_trans(wx+ww-80, cy2+3, "TX", am_txt);
            vga_draw_string_trans(wx+ww-40, cy2+3, "RX", am_txt);
            cy2 += 14;
            for (i=0;i<(int)netif_count() && i<4;i++) {
                const netif_t *net = netif_at((uint32_t)i);
                char txs[12];
                char rxs[12];
                uint32_t rb2 = (i%2==0)?(g_pref_darkmode?RGB(38,38,42):RGB(255,255,255)):(g_pref_darkmode?RGB(32,32,36):RGB(248,248,252));
                if (!net) continue;
                runtime_format_uint(net->tx_packets, txs, sizeof(txs));
                runtime_format_uint(net->rx_packets, rxs, sizeof(rxs));
                vga_fill_rect(wx+1, cy2+i*16, ww-2, 16, rb2);
                vga_draw_string_trans(wx+6, cy2+i*16+4, net->name, am_txt);
                vga_draw_string_trans(wx+ww-80, cy2+i*16+4, txs, am_txt);
                vga_draw_string_trans(wx+ww-40, cy2+i*16+4, rxs, am_txt);
            }
            int fy5 = wy+wh-20;
            vga_fill_rect(wx+1, fy5, ww-2, 19, am_hdr);
            vga_draw_hline(wx+1, fy5, ww-2, am_sep);
            { const netif_t *net = runtime_primary_netif();
              char pktline[40];
              int pp = 0;
              pktline[0] = 0;
              apps1_append_text(pktline, &pp, sizeof(pktline), "Packets In: ");
              apps1_append_uint(pktline, &pp, sizeof(pktline), net ? net->rx_packets : 0);
              apps1_append_text(pktline, &pp, sizeof(pktline), " Out: ");
              apps1_append_uint(pktline, &pp, sizeof(pktline), net ? net->tx_packets : 0);
              vga_draw_string_trans(wx+6, fy5+6, pktline, am_txt); }
        }
        return 1;
    }

    /* App Store window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "App Store")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cy = wy+TITLEBAR_H+1;
        int ph = wh-TITLEBAR_H-19;
        uint32_t as_bg  = g_pref_darkmode ? RGB(22,22,26)    : RGB(248,248,252);
        uint32_t as_txt = g_pref_darkmode ? RGB(220,220,228) : RGB(20,20,25);
        uint32_t as_sub = g_pref_darkmode ? RGB(130,130,138) : RGB(100,100,108);
        uint32_t as_sep = g_pref_darkmode ? RGB(50,50,55)    : RGB(210,210,215);
        uint32_t as_crd = g_pref_darkmode ? RGB(34,34,40)    : RGB(255,255,255);
        vga_fill_rect(wx+1, cy, ww-2, ph, as_bg);

        /* Top navigation tabs: Today / Games / Apps / Arcade */
        int tab_y = cy;
        vga_fill_rect(wx+1, tab_y, ww-2, 26, g_pref_darkmode?RGB(32,32,38):RGB(242,242,246));
        vga_draw_hline(wx+1, tab_y+26, ww-2, as_sep);
        static const char *as_tabs[] = {"Today","Games","Apps","Arcade"};
        int tab_w = (ww-2)/4, ti;
        for (ti=0; ti<4; ti++) {
            int tx = wx+1 + ti*tab_w;
            int is_act = (ti == 0); /* "Today" active */
            if (is_act) {
                vga_draw_hline(tx, tab_y+22, tab_w, RGB(0,122,255));
                vga_draw_hline(tx, tab_y+23, tab_w, RGB(0,122,255));
            }
            int nl = str_len(as_tabs[ti])*8;
            vga_draw_string_trans(tx+(tab_w-nl)/2, tab_y+9,
                as_tabs[ti], is_act?RGB(0,122,255):as_sub);
        }
        cy = tab_y + 28;

        /* Search bar */
        int sb_y = cy + 4;
        vga_fill_rect(wx+8, sb_y, ww-16, 20, g_pref_darkmode?RGB(44,44,50):RGB(235,235,240));
        gui_draw_rounded_rect_outline(wx+8, sb_y, ww-16, 20, 5, as_sep);
        gui_draw_circle(wx+20, sb_y+10, 5, as_sub);
        gui_draw_circle(wx+20, sb_y+10, 3, g_pref_darkmode?RGB(44,44,50):RGB(235,235,240));
        vga_draw_line(wx+24, sb_y+14, wx+28, sb_y+18, as_sub);
        vga_draw_string_trans(wx+30, sb_y+6, "Search games, apps, stories...", as_sub);
        cy = sb_y + 26;

        /* APP OF THE DAY hero card */
        {
            int hero_h = 80;
            /* Gradient hero */
            int hy;
            for (hy=0; hy<hero_h; hy++) {
                uint8_t rr = (uint8_t)(20+hy*60/hero_h);
                uint8_t gg = (uint8_t)(80+hy*40/hero_h);
                uint8_t bb = (uint8_t)(200-hy*60/hero_h);
                vga_draw_hline(wx+4, cy+hy, ww-8, RGB(rr,gg,bb));
            }
            gui_draw_rounded_rect_outline(wx+4, cy, ww-8, hero_h, 8, as_sep);
            vga_draw_string_trans(wx+12, cy+8, "APP OF THE DAY", RGB(180,220,255));
            /* Icon */
            gui_draw_rounded_rect(wx+ww-64, cy+10, 52, 52, 12, RGB(255,149,0));
            vga_fill_rect_alpha(wx+ww-64, cy+10, 52, 18, RGB(255,255,255), 45);
            vga_draw_string_trans(wx+ww-50, cy+30, "N", RGB(255,255,255));
            vga_draw_string_trans(wx+12, cy+22, "Notchmeister", RGB(255,255,255));
            vga_draw_string_trans(wx+12, cy+36, "Transform your notch into", RGB(200,220,255));
            vga_draw_string_trans(wx+12, cy+48, "a living animated wallpaper", RGB(200,220,255));
            /* Stars */
            vga_draw_string_trans(wx+12, cy+64, "***** 4.9 *  FREE", RGB(180,220,255));
            cy += hero_h + 8;
        }

        /* FEATURED section label */
        vga_draw_string_trans(wx+8, cy, "FEATURED", as_sub);
        cy += 14;

        /* 3 featured app cards in a row */
        static const struct {
            const char *name; const char *sub; const char *price;
            uint32_t col; int rating; /* rating out of 5 */
        } feat_apps[] = {
            { "Darkroom",   "Photo & Video Editor",  "FREE",  RGB(50,50,50),    5 },
            { "Bear",       "Writing App for Notes",  "FREE",  RGB(255,102,0),   5 },
            { "Fantastical","Calendar & Tasks",       "$4.99", RGB(252,60,68),   4 },
            { "1Password",  "Password Manager",       "FREE",  RGB(0,148,255),   5 },
            { "Things 3",   "To-Do & Task Manager",   "$9.99", RGB(72,178,255),  5 },
            { "Pixelmator", "Photo Editor",           "$9.99", RGB(50,150,255),  4 },
        };
        int n_feat = 6, feat_cols = 2;
        if (ww > 320) feat_cols = 3;
        int faw = (ww-8)/feat_cols - 4, fah = 62;
        int fi;
        for (fi=0; fi<n_feat; fi++) {
            int fc = fi % feat_cols, fr = fi / feat_cols;
            int fax = wx+4 + fc*(faw+4);
            int fay = cy + fr*(fah+4);
            if (fay + fah > wy+wh-19-6) break;
            gui_draw_rounded_rect(fax, fay, faw, fah, 6, as_crd);
            gui_draw_rounded_rect_outline(fax, fay, faw, fah, 6, as_sep);
            /* App icon */
            gui_draw_rounded_rect(fax+4, fay+6, 40, 40, 10, feat_apps[fi].col);
            vga_fill_rect_alpha(fax+4, fay+6, 40, 14, RGB(255,255,255), 45);
            vga_draw_char_trans(fax+18, fay+22, feat_apps[fi].name[0], RGB(255,255,255));
            /* Name + subtitle */
            int nlen = str_len(feat_apps[fi].name)*8;
            if (nlen > faw-52) nlen = faw-52;
            vga_draw_string_trans(fax+48, fay+10, feat_apps[fi].name, as_txt);
            vga_draw_string_trans(fax+48, fay+22, feat_apps[fi].sub, as_sub);
            /* Stars */
            { int si3; for(si3=0;si3<feat_apps[fi].rating&&si3<5;si3++)
                  vga_draw_char_trans(fax+48+si3*8, fay+34, '*', RGB(255,180,0)); }
            /* Price / GET button */
            if (g_appstore_downloading & (1<<fi)) {
                uint32_t el = timer_ticks() - g_appstore_dl_tick[fi];
                int pr = (int)(el*100/3000); if(pr>100)pr=100;
                if (pr>=100) {
                    g_appstore_downloading &= ~(1<<fi);
                    gui_draw_rounded_rect(fax+faw-34, fay+fah-22, 30, 14, 3, RGB(52,199,89));
                    vga_draw_string_trans(fax+faw-28, fay+fah-19, "OPEN", RGB(255,255,255));
                } else {
                    vga_fill_rect(fax+faw-36, fay+fah-20, 30, 10, as_sep);
                    vga_fill_rect(fax+faw-36, fay+fah-20, pr*30/100, 10, RGB(0,122,255));
                    gui_draw_rounded_rect_outline(fax+faw-36, fay+fah-20, 30, 10, 3, RGB(0,122,255));
                }
            } else {
                gui_draw_rounded_rect(fax+faw-36, fay+fah-22, 32, 14, 7, RGB(0,122,255));
                if (feat_apps[fi].price[0]=='F')
                    vga_draw_string_trans(fax+faw-30, fay+fah-19, "GET", RGB(255,255,255));
                else
                    vga_draw_string_trans(fax+faw-34, fay+fah-19, feat_apps[fi].price, RGB(255,255,255));
            }
        }
        return 1;
    }

    /* Calendar window */
    if (g_windows[idx].title && str_eq(g_windows[idx].title, "Calendar")) {
        const gui_window_t *win = &g_windows[idx];
        if (!win->visible) return 1;
        int wx=win->x, wy=win->y, ww=win->w, wh=win->h;
        int cx0=wx+1, cy0=wy+TITLEBAR_H+1;
        int cw0=ww-2, ch0=wh-TITLEBAR_H-19;

        uint32_t cal_bg   = g_pref_darkmode ? RGB(28,28,30)    : RGB(255,255,255);
        uint32_t cal_hdr  = g_pref_darkmode ? RGB(40,40,44)    : RGB(240,40,40);
        uint32_t cal_hdr2 = g_pref_darkmode ? RGB(36,36,40)    : RGB(248,248,252);
        uint32_t cal_txt  = g_pref_darkmode ? RGB(220,220,224) : RGB(30,30,30);
        uint32_t cal_sub  = g_pref_darkmode ? RGB(120,120,128) : RGB(140,140,140);
        uint32_t cal_bd   = g_pref_darkmode ? RGB(55,55,60)    : RGB(220,220,220);
        uint32_t cal_tday = RGB(255,59,48);      /* today highlight: macOS red */
        uint32_t cal_evt  = g_pref_darkmode ? RGB(0,100,200)   : RGB(0,122,255);

        vga_fill_rect(cx0, cy0, cw0, ch0, cal_bg);

        /* Compute current calendar month/year from today's RTC date plus offset */
        int cal_year;
        int cal_month;
        int dim;
        int start_col;
        int today_day;
        gui_calendar_month_from_offset(g_cal_offset, &cal_year, &cal_month);
        dim = datetime_days_in_month(cal_year, cal_month);
        start_col = datetime_day_of_week(cal_year, cal_month, 1);
        today_day = gui_calendar_today_day_for_month(cal_year, cal_month);
        {
          /* Month header strip */
          vga_fill_rect(cx0, cy0, cw0, 28, cal_hdr);
          /* Build "Month YYYY" string */
          { const char *mn = datetime_month_long(cal_month);
            char yr[5]; int_to_str(cal_year, yr);
            int mlen = str_len(mn)*8 + 8 + str_len(yr)*8;
            int hx = cx0 + (cw0-mlen)/2;
            vga_draw_string_trans(hx, cy0+6, mn, RGB(255,255,255));
            vga_draw_string_trans(hx+str_len(mn)*8+8, cy0+6, yr, RGB(255,255,255));
          }
          vga_draw_string_trans(cx0+8,  cy0+7, "<", RGB(255,255,255));
          vga_draw_string_trans(cx0+cw0-16, cy0+7, ">", RGB(255,255,255));
          cy0 += 28;

          /* Day-of-week headers */
          vga_fill_rect(cx0, cy0, cw0, 18, cal_hdr2);
          const char *cdays[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
          int cell_w = cw0 / 7;
          int d2;
          for (d2=0; d2<7; d2++) {
              int dx = cx0 + d2*cell_w + (cell_w-16)/2;
              vga_draw_string_trans(dx, cy0+4, cdays[d2], cal_sub);
          }
          vga_draw_hline(cx0, cy0+17, cw0, cal_bd);
          cy0 += 18;

          int cell_h = (ch0 - 28 - 18) / 6;
          if (cell_h < 1) cell_h = 1;
          int day_num = 1;
          int row2, col2;
          for (row2=0; row2<6 && day_num<=dim; row2++) {
              for (col2=0; col2<7; col2++) {
                  int gx = cx0 + col2*cell_w;
                  int gy = cy0 + row2*cell_h;
                  vga_draw_rect_outline(gx, gy, cell_w, cell_h, cal_bd);
                  if (row2==0 && col2 < start_col) continue;
                  if (day_num > dim) continue;
                  int is_today = (day_num == today_day);
                  if (is_today) {
                      gui_draw_rounded_rect(gx+cell_w/2-8, gy+3, 16, 14, 7, cal_tday);
                      char ds[4]; int_to_str(day_num, ds);
                      vga_draw_string_trans(gx+cell_w/2-(day_num>=10?8:4), gy+5, ds, RGB(255,255,255));
                  } else {
                      char ds2[4]; int_to_str(day_num, ds2);
                      uint32_t dc = (col2==0||col2==6) ? RGB(255,80,80) : cal_txt;
                      vga_draw_string_trans(gx+(cell_w-16)/2, gy+4, ds2, dc);
                  }
                  /* Show event dots for this day */
                  { int ei3;
                    int dot_x=gx+2, dot_cnt=0;
                    /* Highlight selected day */
                    if(g_cal_offset==0 && day_num==g_cal_sel_day && !is_today)
                        vga_fill_rect(gx+1,gy+1,cell_w-2,cell_h-2,(RGB(0,122,255)&0x303060)|0x103040);
                    for(ei3=0;ei3<g_cal_evt_n&&dot_cnt<3;ei3++){
                        if(g_cal_evt_day[ei3]==day_num && g_cal_offset==0){
                            vga_fill_rect(dot_x+dot_cnt*6, gy+cell_h-5, 4, 4, cal_evt);
                            dot_cnt++;
                        }
                    }
                  }
                  day_num++;
              }
          }
        }
        /* Mini legend */
        int ley = wy+wh-18;
        vga_fill_rect(wx+1, ley, ww-2, 1, cal_bd);
        vga_fill_rect(wx+1, ley+1, ww-2, 17, cal_hdr2);
        vga_fill_rect(wx+8, ley+6, 8, 6, cal_evt);
        vga_draw_string_trans(wx+20, ley+5, "Click day to add event", cal_sub);
        /* Event creation popup */
        if (g_cal_popup && g_cal_sel_day > 0) {
            int pw=min_int(cw0-20,220), ph=70;
            int px2=wx+(ww-pw)/2, py2=wy+TITLEBAR_H+(ch0-ph)/2;
            vga_fill_rect_alpha(wx+1,wy+TITLEBAR_H,ww-2,ch0,RGB(0,0,0),90);
            vga_fill_rect(px2,py2,pw,ph,g_pref_darkmode?RGB(38,38,44):RGB(252,252,255));
            gui_draw_rounded_rect_outline(px2,py2,pw,ph,8,g_pref_darkmode?RGB(70,70,80):RGB(180,180,190));
            /* Title */
            vga_fill_rect(px2,py2,pw,24,g_pref_darkmode?RGB(50,50,58):RGB(240,240,248));
            vga_draw_string_trans(px2+10,py2+7,"New Event",g_pref_darkmode?RGB(220,220,228):RGB(30,30,36));
            { char dstr[32]; int_to_str(g_cal_sel_day,dstr);
              vga_draw_string_trans(px2+pw-40,py2+7,dstr,cal_evt); }
            /* Input field */
            int tf2x=px2+8, tf2w=pw-16;
            vga_fill_rect(tf2x,py2+30,tf2w,16,g_pref_darkmode?RGB(28,28,34):RGB(255,255,255));
            gui_draw_rounded_rect_outline(tf2x,py2+30,tf2w,16,4,cal_evt);
            if(g_cal_evt_input_len>0)
                vga_draw_string_trans(tf2x+4,py2+33,g_cal_evt_input,g_pref_darkmode?RGB(220,220,228):cal_txt);
            else
                vga_draw_string_trans(tf2x+4,py2+33,"Event name...",cal_sub);
            /* Hint */
            vga_draw_string_trans(px2+8,py2+52,"Enter=add  Esc=cancel",cal_sub);
        }
        return 1;
    }


    return 0;
}
