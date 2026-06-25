#include "gui_internal.h"
#include "task.h"
#include "tty.h"


static uint32_t gui_netutil_default_target(void) {
    const netif_t *net = runtime_primary_netif();
    uint32_t target = runtime_dns_server4();
    int i;
    for (i = 0; i < (int)net_route_count(); i++) {
        const net_route_t *route = net_route_at((uint32_t)i);
        if (route && route->dest == 0 && route->mask == 0 && route->gateway) {
            target = route->gateway;
            break;
        }
    }
    if (!target && net) target = net->ipv4;
    return target;
}

static int gui_netutil_scan_tcp_port(uint32_t target, uint16_t dst_port) {
    uint8_t chunk[NET_TCP_PAYLOAD_MAX];
    uint32_t ifindex = 0;
    uint32_t seq;
    uint32_t srcip = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint16_t srcp = 0;
    uint16_t flags = 0;
    uint16_t src_port;
    int tries;
    if (!target || !dst_port || net_route_lookup4(target, &ifindex, 0) < 0)
        return -2;
    src_port = (uint16_t)(46000U + ((timer_ticks() + (uint32_t)dst_port * 17U) % 12000U));
    seq = 0x4E555000U ^ timer_ticks() ^ (uint32_t)dst_port;
    if (net_tcp_send4(ifindex, target, src_port, dst_port, seq, 0,
                      NET_TCP_SYN, 0, 0) < 0)
        return -2;
    for (tries = 0; tries < 70; tries++) {
        int n;
        srcip = 0;
        srcp = 0;
        rseq = 0;
        rack = 0;
        flags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk), &srcip, &srcp,
                          &rseq, &rack, &flags);
        if (n < 0) break;
        if (srcip != target || srcp != dst_port) continue;
        if ((flags & NET_TCP_SYN) && (flags & NET_TCP_ACK)) {
            (void)net_tcp_send4(ifindex, target, src_port, dst_port,
                                seq + 1U, rseq + 1U,
                                NET_TCP_ACK | NET_TCP_RST, 0, 0);
            return 1;
        }
        if (flags & NET_TCP_RST)
            return -1;
    }
    return -2;
}


static int gui_window_visible_named(const char *title) {
    int i;
    for (i = 0; i < g_num_windows; i++) {
        if (!g_windows[i].visible || !g_windows[i].title) continue;
        if (str_eq(g_windows[i].title, title)) return 1;
    }
    return 0;
}

static int gui_top_window_named(const char *title) {
    int top = win_top_visible();
    return top >= 0 &&
           g_windows[top].title &&
           str_eq(g_windows[top].title, title);
}

static int gui_focused_search_owner(void) {
    if (g_mail_search_focused && gui_top_window_named("Mail")) return GUI_SEARCH_MAIL;
    if (g_ms_search_focused && gui_top_window_named("Messages")) return GUI_SEARCH_MESSAGES;
    if (g_contacts_search_focused && gui_top_window_named("Contacts")) return GUI_SEARCH_CONTACTS;
    if (g_photos_search_focused && gui_top_window_named("Photos")) return GUI_SEARCH_PHOTOS;
    if (g_maps_search_focused && gui_top_window_named("Maps")) return GUI_SEARCH_MAPS;
    if (g_appstore_search_focused && gui_top_window_named("App Store")) return GUI_SEARCH_APPSTORE;
    if (g_findmy_search_focused && gui_top_window_named("Find My")) return GUI_SEARCH_FINDMY;
    if (g_passwords_search_focused && gui_top_window_named("Passwords")) return GUI_SEARCH_PASSWORDS;
    if (g_automator_search_focused && gui_top_window_named("Automator")) return GUI_SEARCH_AUTOMATOR;
    if (g_fontbook_search_focused && gui_top_window_named("Font Book")) return GUI_SEARCH_FONTBOOK;
    if (g_console_filter_focused && gui_top_window_named("Console")) return GUI_SEARCH_CONSOLE;
    if (g_sfsymbols_search_focused && gui_top_window_named("SF Symbols")) return GUI_SEARCH_SFSYMBOLS;
    if (g_onepassword_search_focused && gui_top_window_named("1Password")) return GUI_SEARCH_ONEPASSWORD;
    if (g_raycast_search_focused && gui_top_window_named("Raycast")) return GUI_SEARCH_RAYCAST;
    if (g_finder_search_focused && gui_top_window_named("MyOS Finder")) return GUI_SEARCH_FINDER;
    if (g_alfred_search_focused && gui_top_window_named("Alfred")) return GUI_SEARCH_ALFRED;
    return 0;
}

static void gui_clear_search_focus(int owner) {
    if (owner == GUI_SEARCH_MAIL) {
        g_mail_search_focused = 0;
    } else if (owner == GUI_SEARCH_MESSAGES) {
        g_ms_search_focused = 0;
    } else if (owner == GUI_SEARCH_CONTACTS) {
        g_contacts_search_focused = 0;
    } else if (owner == GUI_SEARCH_PHOTOS) {
        g_photos_search_focused = 0;
    } else if (owner == GUI_SEARCH_MAPS) {
        g_maps_search_focused = 0;
    } else if (owner == GUI_SEARCH_APPSTORE) {
        g_appstore_search_focused = 0;
    } else if (owner == GUI_SEARCH_FINDMY) {
        g_findmy_search_focused = 0;
    } else if (owner == GUI_SEARCH_PASSWORDS) {
        g_passwords_search_focused = 0;
    } else if (owner == GUI_SEARCH_AUTOMATOR) {
        g_automator_search_focused = 0;
    } else if (owner == GUI_SEARCH_FONTBOOK) {
        g_fontbook_search_focused = 0;
    } else if (owner == GUI_SEARCH_CONSOLE) {
        g_console_filter_focused = 0;
    } else if (owner == GUI_SEARCH_SFSYMBOLS) {
        g_sfsymbols_search_focused = 0;
    } else if (owner == GUI_SEARCH_ONEPASSWORD) {
        g_onepassword_search_focused = 0;
    } else if (owner == GUI_SEARCH_RAYCAST) {
        g_raycast_search_focused = 0;
    } else if (owner == GUI_SEARCH_FINDER) {
        g_finder_search_focused = 0;
    } else if (owner == GUI_SEARCH_ALFRED) {
        g_alfred_search_focused = 0;
    }
    gui_search_blur(owner);
}

static const char *gui_search_owner_name(int owner) {
    if (owner == GUI_SEARCH_MAIL) return "Mail";
    if (owner == GUI_SEARCH_MESSAGES) return "Messages";
    if (owner == GUI_SEARCH_CONTACTS) return "Contacts";
    if (owner == GUI_SEARCH_PHOTOS) return "Photos";
    if (owner == GUI_SEARCH_MAPS) return "Maps";
    if (owner == GUI_SEARCH_APPSTORE) return "App Store";
    if (owner == GUI_SEARCH_FINDMY) return "Find My";
    if (owner == GUI_SEARCH_PASSWORDS) return "Passwords";
    if (owner == GUI_SEARCH_AUTOMATOR) return "Automator";
    if (owner == GUI_SEARCH_FONTBOOK) return "Font Book";
    if (owner == GUI_SEARCH_CONSOLE) return "Console";
    if (owner == GUI_SEARCH_SFSYMBOLS) return "SF Symbols";
    if (owner == GUI_SEARCH_ONEPASSWORD) return "1Password";
    if (owner == GUI_SEARCH_RAYCAST) return "Raycast";
    if (owner == GUI_SEARCH_FINDER) return "Finder";
    if (owner == GUI_SEARCH_ALFRED) return "Alfred";
    return "Search";
}

static const char *g_contact_names_for_search[] = {
    "Alice Johnson", "Bob Smith", "Carol White", "David Brown",
    "Emma Davis", "Frank Wilson", "Grace Lee", "Henry Chen"
};

static const char *g_contact_phones_for_search[] = {
    "+1 555-0101", "+1 555-0142", "+1 555-0178", "+1 555-0165",
    "+1 555-0133", "+1 555-0190", "+1 555-0121", "+1 555-0155"
};

static int gui_contacts_index_for_visible_row(int row) {
    int i;
    int shown = 0;
    if (row < 0) return -1;
    for (i = 0; i < 8; i++) {
        if (!gui_search_matches(GUI_SEARCH_CONTACTS,
                                g_contact_names_for_search[i],
                                g_contact_phones_for_search[i]))
            continue;
        if (shown == row) return i;
        shown++;
    }
    return -1;
}

static int gui_contacts_visible_selection(void) {
    int sel = g_ct_sel;
    int first = gui_contacts_index_for_visible_row(0);
    if (sel < 0 || sel >= 8) sel = 0;
    if (gui_search_matches(GUI_SEARCH_CONTACTS,
                           g_contact_names_for_search[sel],
                           g_contact_phones_for_search[sel]))
        return sel;
    return first >= 0 ? first : 0;
}

static int gui_filename_has_ext(const char *name, const char *ext) {
    const char *dot = 0;
    int i;
    for (i = 0; name && name[i]; i++) {
        if (name[i] == '.') dot = name + i;
    }
    return dot && str_eq(dot, ext);
}

static int gui_open_basic_app(const char *title) {
    int i;
    for (i = 0; i < g_num_windows; i++) {
        if (g_windows[i].title && str_eq(g_windows[i].title, title)) {
            g_windows[i].visible = 1;
            win_bring_to_front(i);
            if (str_eq(title, "TextEdit")) { g_edit_focused = 1; g_dict_focused = 0; }
            return 1;
        }
    }
    if (g_num_windows >= MAX_WINDOWS) return 0;
    {
        gui_window_t *nw = &g_windows[g_num_windows];
        nw->focused = 0;
        nw->dragging = 0;
        nw->resizing = 0;
        nw->visible = 1;
        nw->maximized = 0;
        nw->title = title;
        if (str_eq(title, "TextEdit")) {
            nw->x=120; nw->y=80; nw->w=310; nw->h=260;
            g_edit_focused = 1; g_dict_focused = 0;
        } else if (str_eq(title, "Preview")) {
            nw->x=160; nw->y=60; nw->w=380; nw->h=300;
            g_preview_page = 0; g_preview_zoom = 100; g_preview_markup = 0;
        } else if (str_eq(title, "Mail")) {
            nw->x=100; nw->y=80; nw->w=360; nw->h=260;
        } else if (str_eq(title, "FaceTime")) {
            nw->x=200; nw->y=80; nw->w=220; nw->h=260;
            g_facetime_active = 2; g_facetime_contact = 0;
        } else if (str_eq(title, "Messages")) {
            nw->x=130; nw->y=60; nw->w=340; nw->h=290;
        } else if (str_eq(title, "Notes")) {
            nw->x=18; nw->y=270; nw->w=260; nw->h=240;
        } else if (str_eq(title, "MyOS Finder")) {
            nw->x=200; nw->y=80; nw->w=380; nw->h=340;
        } else {
            nw->x=160; nw->y=80; nw->w=300; nw->h=240;
        }
        g_win_anim[g_num_windows] = OPEN_ANIM;
        g_win_minimized[g_num_windows] = 0;
        g_win_close_anim[g_num_windows] = 0;
        g_num_windows++;
        return 1;
    }
}

static void gui_record_share_action(const char *label, uint32_t color) {
    g_share_action_count++;
    g_share_last_action = label;
    toast_show("Share", label, color);
}

static void gui_quicklook_open_current(void) {
    if (gui_filename_has_ext(g_ql_filename, ".txt") || gui_filename_has_ext(g_ql_filename, ".md")) {
        if (gui_open_basic_app("TextEdit")) toast_show("Quick Look", "Opened in TextEdit", RGB(0,122,255));
        else toast_show("Quick Look", "Window limit reached", RGB(255,59,48));
    } else {
        if (gui_open_basic_app("Preview")) toast_show("Quick Look", "Opened in Preview", RGB(170,50,170));
        else toast_show("Quick Look", "Window limit reached", RGB(255,59,48));
    }
}

static void gui_mail_start_compose(const char *to, const char *subject, const char *body) {
    int i;
    g_mail_compose = 1;
    g_mail_focused_field = 1;
    g_mail_to_len = 0;
    g_mail_subject_len = 0;
    g_mail_body_len = 0;
    g_mail_to[0] = 0;
    g_mail_subject[0] = 0;
    g_mail_body[0] = 0;
    if (to) {
        for (i = 0; to[i] && i < 63; i++) g_mail_to[i] = to[i];
        g_mail_to[i] = 0;
        g_mail_to_len = i;
    }
    if (subject) {
        for (i = 0; subject[i] && i < 63; i++) g_mail_subject[i] = subject[i];
        g_mail_subject[i] = 0;
        g_mail_subject_len = i;
    }
    if (body) {
        for (i = 0; body[i] && i < 255; i++) g_mail_body[i] = body[i];
        g_mail_body[i] = 0;
        g_mail_body_len = i;
    }
}

static void textedit_clear_selection(void) {
    g_edit_sel_start = 0;
    g_edit_sel_end = 0;
}

static int textedit_selection_bounds(int *start, int *end) {
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

static int textedit_delete_selection(void) {
    int s, e, k;
    if (!textedit_selection_bounds(&s, &e)) return 0;
    for (k = e; k <= g_edit_len; k++)
        g_edit_text[s + k - e] = g_edit_text[k];
    g_edit_len -= (e - s);
    textedit_clear_selection();
    return 1;
}

static int gui_needs_realtime_frame(void) {
    if (g_saver_on || g_toast_visible || g_tile_flash > 0) return 1;
    if (g_siri_visible) return 1;
    if (g_wt_visible && g_wt_done == 1) return 1;
    if (g_handoff_visible) return 1;
    if (g_pb_flash_tick > 0) return 1;
    if (g_brk_active || g_pong_active || g_snake_active) return 1;
    { int bi; for (bi=0; bi<NUM_DOCK_ICONS; bi++) if (g_dock_bounce[bi]>0) return 1; }
    { int wi; for (wi=0; wi<g_num_windows; wi++) if (g_win_close_anim[wi]>0) return 1; }
    if (g_music_playing &&
        (g_cc_visible || g_nc_visible || g_widget_visible ||
         gui_window_visible_named("Music"))) {
        return 1;
    }
    return 0;
}

static int gui_drain_tty_output(void) {
    static uint32_t read_pos = 0;
    static char line[TERM_MAX_COL + 1];
    static int line_len = 0;
    char buf[128];
    int n;
    int dirty = 0;
    while ((n = tty_output_read(&read_pos, buf, sizeof(buf))) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                line[line_len] = 0;
                term_println(line);
                line_len = 0;
                dirty = 1;
            } else if (line_len < TERM_MAX_COL) {
                line[line_len++] = c;
            }
        }
    }
    return dirty;
}

/* Periodic system notification pool */
static const struct { const char *app; const char *msg; uint32_t color; int type; } s_sys_notifs[] = {
    { "Messages",   "Sarah: Are you free tonight?",      RGB(52, 199,  89), TOAST_TYPE_REPLY   },
    { "Mail",       "New: Q3 Review - Please review",    RGB( 0, 122, 255), TOAST_TYPE_REPLY   },
    { "Calendar",   "Calendar event reminder",            RGB(255,  59,  48),TOAST_TYPE_SNOOZE  },
    { "Messages",   "Alex: Just sent you the file",      RGB(52, 199,  89), TOAST_TYPE_REPLY   },
    { "News",       "Breaking: New AI model released",   RGB(255,  59,  48),TOAST_TYPE_DEFAULT },
    { "Mail",       "Your order has shipped",            RGB( 0, 122, 255), TOAST_TYPE_DEFAULT },
    { "Reminders",  "Buy groceries",                     RGB(255, 149,   0),TOAST_TYPE_SNOOZE  },
    { "Calendar",   "Calendar event scheduled",           RGB(255,  59,  48),TOAST_TYPE_SNOOZE  },
    { "Messages",   "Mom: Call me when you can",         RGB(52, 199,  89), TOAST_TYPE_REPLY   },
    { "App Store",  "Browse apps in App Store",          RGB( 0, 122, 255), TOAST_TYPE_DEFAULT },
    { "Maps",       "Leave now for on-time arrival",     RGB(255, 149,   0),TOAST_TYPE_DEFAULT },
    { "Photos",     "New memory from Photos",            RGB(240,  80, 160),TOAST_TYPE_DEFAULT },
    { "Wallet",     "Transaction from Netflix",          RGB( 52, 199,  89),TOAST_TYPE_DEFAULT },
    { "FaceTime",   "Missed call from Jordan",           RGB(52, 199,  89), TOAST_TYPE_REPLY   },
    { "Music",      "New release from Taylor Swift",     RGB(252,  60,  68),TOAST_TYPE_DEFAULT },
};
#define SYS_NOTIF_COUNT 15
#define NOTIF_INTERVAL  25000   /* ms between auto notifications */

void gui_run(void) {
    int prev_btn = 0;
    int prev_mx = 0, prev_my = 0;
    int i;
    uint32_t last_frame = 0;
    uint32_t last_clock_second = 0;
    uint32_t last_notif_tick = 0;
    int notif_idx = 0;

    /* Initial render */
    draw_scene(mouse_get_x(), mouse_get_y());
    vga_flip();
    last_clock_second = timer_ticks() / 1000;

    for (;;) {
        /* ~60fps cap for event polling and active animations */
        while (timer_ticks() - last_frame < 16)
            __asm__ volatile("hlt");
        uint32_t now = timer_ticks();
        last_frame = now;

        int mx = mouse_get_x();
        int my = mouse_get_y();
        int mb = (int)mouse_get_buttons();
        int dirty = 0;

        if (mx != prev_mx || my != prev_my || mb != prev_btn)
            dirty = 1;
        if (now / 1000 != last_clock_second) {
            last_clock_second = now / 1000;
            dirty = 1;
        }
        if (gui_needs_realtime_frame())
            dirty = 1;
        if (gui_drain_tty_output())
            dirty = 1;

        /* Button hover update */
        for (i = 0; i < g_num_buttons; i++) {
            int hover;
            if (g_buttons[i].win_idx < -1 ||
                (g_buttons[i].win_idx >= 0 &&
                 (g_buttons[i].win_idx >= g_num_windows ||
                  !g_windows[g_buttons[i].win_idx].visible))) {
                hover = 0;
            } else {
                hover = gui_button_hit(&g_buttons[i], mx, my);
            }
            if (g_buttons[i].hover != hover) dirty = 1;
            g_buttons[i].hover = hover;
        }

        /* Left press */
        if ((mb & MOUSE_LEFT) && !(prev_btn & MOUSE_LEFT)) {
            g_last_input_tick = timer_ticks();
            /* Find topmost window at click position (last = topmost in z-order) */
            int top_win_idx = -1;
            { int ii;
              for (ii = 0; ii < g_num_windows; ii++) {
                  gui_window_t *ww = &g_windows[ii];
                  if (!ww->visible) continue;
                  if (mx >= ww->x && mx < ww->x+ww->w && my >= ww->y && my < ww->y+ww->h)
                      top_win_idx = ii;
              }
            }
            /* Unlock lock screen */
            if (g_locked) { g_locked = 0; dirty = 1; goto end_left_press; }
            /* Overlay click handlers */
            if (new_overlays_click(mx, my)) { dirty=1; goto end_left_press; }
            /* Toast action buttons */
            if (g_toast_visible) {
                int ttx = VGA_WIDTH - TOAST_W - 10;
                int tty = MENUBAR_H + 8;
                int btn_y3 = tty + 52;
                if (mx >= ttx && mx < ttx + TOAST_W && my >= tty && my < tty + TOAST_H) {
                    if (my >= btn_y3 && my < btn_y3 + 18) {
                        /* Button row click */
                        int bw3 = (TOAST_W - 18) / 2;
                        if (g_toast_type == TOAST_TYPE_REPLY || g_toast_type == TOAST_TYPE_SNOOZE) {
                            int left_end = ttx + 6 + bw3;
                            if (mx < left_end) {
                                /* Reply / Snooze */
                                if (g_toast_type == TOAST_TYPE_REPLY) {
                                    int jj, found_msg = 0;
                                    for (jj = 0; jj < g_num_windows; jj++) {
                                        if (g_windows[jj].title && str_eq(g_windows[jj].title, "Messages")) {
                                            g_windows[jj].visible = 1;
                                            win_bring_to_front(jj);
                                            found_msg = 1;
                                            break;
                                        }
                                    }
                                    if (!found_msg && g_num_windows < MAX_WINDOWS) {
                                        gui_window_t *nwmsg = &g_windows[g_num_windows];
                                        nwmsg->x = 130; nwmsg->y = 60; nwmsg->w = 340; nwmsg->h = 290;
                                        nwmsg->title = "Messages"; nwmsg->visible = 1; nwmsg->focused = 0;
                                        nwmsg->dragging = 0; nwmsg->maximized = 0;
                                        g_win_anim[g_num_windows] = OPEN_ANIM;
                                        g_win_minimized[g_num_windows] = 0;
                                        g_win_close_anim[g_num_windows] = 0;
                                        g_num_windows++;
                                    }
                                    g_ms_sel = 0;
                                    g_ms_focused = 1;
                                    g_ms_input[0] = 0;
                                    g_ms_input_len = 0;
                                    toast_show("Messages", "Reply field focused", RGB(52,199,89));
                                } else {
                                    g_toast_visible = 0;
                                    toast_show("Notifications", "Snoozed for later", RGB(0,122,255));
                                }
                            } else {
                                /* Dismiss */
                                g_toast_visible = 0;
                            }
                        } else {
                            g_toast_visible = 0;
                        }
                    } else {
                        /* Click on toast body — open the relevant app */
                        g_toast_visible = 0;
                    }
                    dirty = 1; goto end_left_press;
                }
            }
            /* Photos fullscreen — must be checked FIRST before menubar/window handlers */
            if (g_photos_fullscreen) {
                int edit_panel_w3 = g_photos_edit_mode ? 140 : 0;
                int photo_w3 = VGA_WIDTH - edit_panel_w3;
                /* Edit panel clicks */
                if (g_photos_edit_mode && mx >= photo_w3) {
                    if (my < 28) {
                        int eti3 = (mx - photo_w3) / (edit_panel_w3/3);
                        if (eti3 >= 0 && eti3 < 3) { g_photos_edit_tool=eti3; dirty=1; goto end_left_press; }
                    }
                    if (my >= VGA_HEIGHT-36 && my < VGA_HEIGHT-12) {
                        g_photos_edit_mode=0; dirty=1; goto end_left_press;
                    }
                    if (g_photos_edit_tool==0 && my>=76 && my<88) {
                        int sl_x3=photo_w3+8, sl_w3=edit_panel_w3-16;
                        if (mx>=sl_x3 && mx<sl_x3+sl_w3) { g_photos_brightness=(mx-sl_x3)*100/sl_w3; dirty=1; goto end_left_press; }
                    }
                    if (g_photos_edit_tool==0 && my>=120 && my<132) {
                        int sl_x3=photo_w3+8, sl_w3=edit_panel_w3-16;
                        if (mx>=sl_x3 && mx<sl_x3+sl_w3) { g_photos_contrast=(mx-sl_x3)*100/sl_w3; dirty=1; goto end_left_press; }
                    }
                    if (g_photos_edit_tool==0 && my>=164 && my<176) {
                        int sl_x3=photo_w3+8, sl_w3=edit_panel_w3-16;
                        if (mx>=sl_x3 && mx<sl_x3+sl_w3) { g_photos_saturation=(mx-sl_x3)*100/sl_w3; dirty=1; goto end_left_press; }
                    }
                    dirty=1; goto end_left_press;
                }
                if (mx>=photo_w3-28 && mx<photo_w3-8 && my>=4 && my<24) {
                    g_photos_fullscreen=0; g_photos_edit_mode=0; dirty=1; goto end_left_press;
                }
                if (mx>=photo_w3-72 && mx<photo_w3-32 && my>=4 && my<24) {
                    g_photos_edit_mode^=1; dirty=1; goto end_left_press;
                }
                if (mx>=4 && mx<28 && my>=VGA_HEIGHT/2-20 && my<VGA_HEIGHT/2+20) {
                    g_photos_sel=(g_photos_sel+5)%6; dirty=1; goto end_left_press;
                }
                if (!g_photos_edit_mode && mx>=photo_w3-28 && mx<photo_w3-4 && my>=VGA_HEIGHT/2-20 && my<VGA_HEIGHT/2+20) {
                    g_photos_sel=(g_photos_sel+1)%6; dirty=1; goto end_left_press;
                }
                if (mx < photo_w3) { g_photos_fullscreen=0; g_photos_edit_mode=0; dirty=1; goto end_left_press; }
                dirty=1; goto end_left_press;
            }
            /* Screenshot Tool click */
            if (g_scr_visible) {
                if (g_scr_visible == 2) {
                    g_scr_visible = 0; dirty = 1; goto end_left_press;
                }
                {
                    int tb_w2 = 340, tb_x2 = (VGA_WIDTH - tb_w2) / 2, tb_y2 = VGA_HEIGHT - 90;
                    int bi2;
                    for (bi2 = 0; bi2 < 3; bi2++) {
                        int bx2 = tb_x2 + 12 + bi2*72, by2 = tb_y2 + 6;
                        if (mx >= bx2 && mx < bx2 + 64 && my >= by2 && my < by2 + 32) {
                            g_scr_mode = bi2; dirty = 1; goto end_left_press;
                        }
                    }
                    if (mx >= tb_x2 + tb_w2 - 76 && mx < tb_x2 + tb_w2 - 8 &&
                        my >= tb_y2 + 8 && my < tb_y2 + 36) {
                        g_scr_visible = 2;
                        toast_show("Screenshot", "Preview ready", RGB(52,199,89));
                        dirty = 1; goto end_left_press;
                    }
                    if (mx < tb_x2 || mx >= tb_x2 + tb_w2 || my < tb_y2 - 24 || my >= tb_y2 + 44) {
                        g_scr_visible = 0; dirty = 1; goto end_left_press;
                    }
                }
                dirty = 1; goto end_left_press;
            }
            /* Widget bar click */
            if (g_widget_visible) {
                int wb_h2 = 90;
                int wb_x2 = 8;
                int wb_y2 = VGA_HEIGHT - wb_h2 - 32;
                int qn_x2 = wb_x2 + 486;
                int qn_y2 = wb_y2;
                if (mx >= qn_x2 && mx < qn_x2 + 160 && my >= qn_y2 && my < qn_y2 + wb_h2) {
                    (void)gui_open_basic_app("Notes");
                    g_notes_sel = NOTES_COUNT - 1;
                    g_notes_focused = 1;
                    g_widget_visible = 0;
                    toast_show("Quick Notes", "New note ready", RGB(255,204,0));
                    dirty = 1;
                    goto end_left_press;
                }
                if (my < wb_y2 - 4 || my >= wb_y2 + wb_h2 + 8) {
                    g_widget_visible = 0;
                    dirty = 1;
                    goto end_left_press;
                }
            }
            /* Control Center click */
            if (g_cc_visible) {
                cc_click(mx, my);
                dirty = 1;
                goto end_left_press;
            }
            /* Notification Center click */
            if (g_nc_visible && my >= MENUBAR_H) {
                nc_click(mx, my);
                dirty = 1;
                goto end_left_press;
            }
            /* Quick Look click */
            if (g_ql_visible) {
                int ql_w2=400, ql_h2=300;
                int ql_x2=(VGA_WIDTH-ql_w2)/2, ql_y2=(VGA_HEIGHT-ql_h2)/2;
                int ql_by2 = ql_y2 + ql_h2 - 30;
                if (mx>=ql_x2+7 && mx<=ql_x2+21 && my>=ql_y2+7 && my<=ql_y2+21) {
                    g_ql_visible = 0; dirty = 1; goto end_left_press;
                }
                if (my >= ql_by2 && my < ql_y2+ql_h2) {
                    if (mx >= ql_x2+ql_w2/2-50 && mx < ql_x2+ql_w2/2+50) {
                        gui_quicklook_open_current();
                        g_ql_visible = 0; dirty = 1; goto end_left_press;
                    }
                    if (mx >= ql_x2+8 && mx < ql_x2+80) {
                        g_share_visible = 1;
                        g_ql_visible = 0;
                        gui_record_share_action("Ready to share", RGB(0,122,255));
                        dirty = 1; goto end_left_press;
                    }
                    if (mx >= ql_x2+ql_w2-80 && mx < ql_x2+ql_w2-8) {
                        g_ql_visible = 0; dirty = 1; goto end_left_press;
                    }
                }
                if (mx < ql_x2 || mx > ql_x2+ql_w2 || my < ql_y2 || my > ql_y2+ql_h2) {
                    g_ql_visible = 0; dirty = 1;
                }
                goto end_left_press;
            }
            /* Share Sheet click */
            if (g_share_visible) {
                int ss_w2=280, ss_h2=220;
                int ss_x2=(VGA_WIDTH-ss_w2)/2, ss_y2=VGA_HEIGHT-ss_h2-28;
                if ((mx>=ss_x2+ss_w2-26 && mx<ss_x2+ss_w2-8 && my>=ss_y2+6 && my<ss_y2+22) ||
                    (mx>=ss_x2+8 && mx<ss_x2+ss_w2-8 && my>=ss_y2+ss_h2-26 && my<ss_y2+ss_h2-6)) {
                    g_share_visible = 0; dirty = 1; goto end_left_press;
                }
                if (my >= ss_y2+36 && my < ss_y2+80 && mx >= ss_x2 && mx < ss_x2+ss_w2) {
                    int sa_icon_w2 = ss_w2 / 6;
                    int si_share = (mx - ss_x2) / sa_icon_w2;
                    if (si_share == 0) { (void)gui_open_basic_app("Mail"); gui_record_share_action("Shared with Mail", RGB(0,122,255)); }
                    else if (si_share == 1) { (void)gui_open_basic_app("Messages"); gui_record_share_action("Shared with Messages", RGB(52,199,89)); }
                    else if (si_share == 2) { (void)gui_open_basic_app("Notes"); gui_record_share_action("Added to Notes", RGB(255,214,10)); }
                    else if (si_share == 3) { g_airdrop_visible=1; g_airdrop_sending=2; g_airdrop_progress=0; g_airdrop_start_tick=timer_ticks(); gui_record_share_action("Sending with AirDrop", RGB(0,190,255)); }
                    else if (si_share == 4) { gui_record_share_action("Link copied", RGB(120,120,128)); }
                    else { gui_record_share_action("More sharing options", RGB(150,150,160)); }
                    g_share_visible = 0; dirty = 1; goto end_left_press;
                }
                if (my >= ss_y2+96 && my < ss_y2+184 && mx >= ss_x2+8 && mx < ss_x2+ss_w2-8) {
                    int ai_share = (my - (ss_y2+96)) / 22;
                    if (ai_share == 0) gui_record_share_action("Link copied", RGB(120,120,128));
                    else if (ai_share == 1) { (void)gui_open_basic_app("MyOS Finder"); gui_record_share_action("Saved to Files", RGB(41,128,185)); }
                    else if (ai_share == 2) { g_print_visible = 1; gui_record_share_action("Print prepared", RGB(0,122,255)); }
                    else { (void)gui_open_basic_app("Notes"); gui_record_share_action("Added to Notes", RGB(255,214,10)); }
                    g_share_visible = 0; dirty = 1; goto end_left_press;
                }
                if (mx < ss_x2 || mx > ss_x2+ss_w2 || my < ss_y2 || my > ss_y2+ss_h2) {
                    g_share_visible = 0; dirty = 1;
                }
                goto end_left_press;
            }
            /* Print Dialog click */
            if (g_print_visible) {
                int pd_w2=340, pd_h2=260;
                int pd_x2=(VGA_WIDTH-pd_w2)/2, pd_y2=(VGA_HEIGHT-pd_h2)/2;
                int pd_prev_x2 = pd_x2 + 8, pd_prev_w2 = 90;
                int pd_set_x2 = pd_prev_x2 + pd_prev_w2 + 8;
                int pd_sy2 = pd_y2 + 38;
                int pd_copies_y2 = pd_sy2 + 34;
                int pd_pages_y2 = pd_copies_y2 + 34;
                int pd_color_y2 = pd_pages_y2 + 32;
                int pd_quality_y2 = pd_color_y2 + 28;
                /* Cancel button */
                if (mx>=pd_x2+8 && mx<pd_x2+78 && my>=pd_y2+pd_h2-30 && my<pd_y2+pd_h2-10) {
                    g_print_visible = 0; dirty = 1; goto end_left_press;
                }
                /* Print button */
                if (mx>=pd_x2+pd_w2-88 && mx<pd_x2+pd_w2-8 && my>=pd_y2+pd_h2-30 && my<pd_y2+pd_h2-10) {
                    g_print_jobs++;
                    g_print_visible = 0;
                    toast_show("Print", "Job sent to MyOS PDF", RGB(0,122,255));
                    dirty = 1; goto end_left_press;
                }
                if (mx>=pd_set_x2 && mx<pd_x2+pd_w2-16 && my>=pd_sy2+12 && my<pd_sy2+28) {
                    toast_show("Print", "MyOS PDF Printer selected", RGB(0,122,255));
                    dirty = 1; goto end_left_press;
                }
                if (my>=pd_copies_y2+12 && my<pd_copies_y2+28) {
                    if (mx>=pd_set_x2+40 && mx<pd_set_x2+56) {
                        if (g_print_copies > 1) g_print_copies--;
                        toast_show("Print", "Copies updated", RGB(0,122,255));
                        dirty = 1; goto end_left_press;
                    }
                    if (mx>=pd_set_x2+64 && mx<pd_set_x2+82) {
                        if (g_print_copies < 9) g_print_copies++;
                        toast_show("Print", "Copies updated", RGB(0,122,255));
                        dirty = 1; goto end_left_press;
                    }
                }
                if (my>=pd_pages_y2+10 && my<pd_pages_y2+25) {
                    if (mx>=pd_set_x2+60 && mx<pd_set_x2+80) {
                        g_print_page_from++;
                        if (g_print_page_from > g_print_page_to) g_print_page_from = 1;
                        toast_show("Print", "Start page updated", RGB(0,122,255));
                        dirty = 1; goto end_left_press;
                    }
                    if (mx>=pd_set_x2+100 && mx<pd_set_x2+120) {
                        g_print_page_to++;
                        if (g_print_page_to > 9) g_print_page_to = g_print_page_from;
                        if (g_print_page_to < g_print_page_from) g_print_page_to = g_print_page_from;
                        toast_show("Print", "End page updated", RGB(0,122,255));
                        dirty = 1; goto end_left_press;
                    }
                }
                if (mx>=pd_set_x2 && mx<pd_set_x2+80 && my>=pd_color_y2+10 && my<pd_color_y2+26) {
                    g_print_color ^= 1;
                    toast_show("Print", g_print_color ? "Color mode" : "Black and white", RGB(0,122,255));
                    dirty = 1; goto end_left_press;
                }
                if (mx>=pd_set_x2 && mx<pd_set_x2+88 && my>=pd_quality_y2+10 && my<pd_quality_y2+26) {
                    g_print_quality++;
                    if (g_print_quality > 2) g_print_quality = 0;
                    toast_show("Print", "Quality updated", RGB(0,122,255));
                    dirty = 1; goto end_left_press;
                }
                /* Click outside = close */
                if (mx < pd_x2 || mx > pd_x2+pd_w2 || my < pd_y2 || my > pd_y2+pd_h2) {
                    g_print_visible = 0; dirty = 1;
                }
                goto end_left_press;
            }
            /* Mission Control click */
            /* App Exposé click */
            if (g_expose_visible) {
                /* Find which thumbnail was clicked and focus that window */
                int i3, nm3 = 0, matches3[MAX_WINDOWS];
                for (i3 = 0; i3 < g_num_windows; i3++) {
                    if (!g_windows[i3].visible) continue;
                    if (g_expose_app_idx < 0) {
                        matches3[nm3++] = i3;
                    } else {
                        const char *dn3 = s_dock_icons[g_expose_app_idx].name;
                        const char *wt3 = g_windows[i3].title ? g_windows[i3].title : "";
                        int dl3 = str_len(dn3), mi3, match3 = (str_len(wt3) >= dl3);
                        if (match3) for (mi3=0; mi3<dl3; mi3++) if (dn3[mi3]!=wt3[mi3]){match3=0;break;}
                        if (match3) matches3[nm3++] = i3;
                    }
                    if (nm3 >= MAX_WINDOWS) break;
                }
                if (nm3 == 0) {
                    for (i3=0;i3<g_num_windows;i3++) if(g_windows[i3].visible) matches3[nm3++]=i3;
                }
                if (nm3 == 0) {
                    g_expose_visible = 0; dirty = 1; goto end_left_press;
                }
                int cols_e3 = (nm3<=2)?nm3:(nm3<=4)?2:3;
                int tw3 = (VGA_WIDTH-40)/cols_e3-12; if(tw3<60)tw3=60;
                int th3 = (VGA_HEIGHT-MENUBAR_H-60)/((nm3+cols_e3-1)/cols_e3)-14; if(th3<40)th3=40;
                int gx3 = (VGA_WIDTH - cols_e3*(tw3+12))/2;
                int gy3 = MENUBAR_H + 30;
                for (i3=0; i3<nm3; i3++) {
                    int cx3 = gx3+(i3%cols_e3)*(tw3+12);
                    int cy3 = gy3+(i3/cols_e3)*(th3+14);
                    if (mx>=cx3 && mx<cx3+tw3 && my>=cy3 && my<cy3+th3) {
                        win_bring_to_front(matches3[i3]);
                        break;
                    }
                }
                g_expose_visible = 0; dirty = 1; goto end_left_press;
            }
            if (g_mc_visible) {
                /* Check space strip clicks (top 54px below menubar) */
                if (my >= MENUBAR_H + 4 && my < MENUBAR_H + 58) {
                    int ns3 = g_num_spaces < 1 ? 1 : (g_num_spaces > 4 ? 4 : g_num_spaces);
                    int sp_w = VGA_WIDTH / ns3;
                    int clicked_sp = mx / sp_w + 1;
                    if (clicked_sp >= 1 && clicked_sp <= ns3) {
                        g_current_space = clicked_sp;
                        toast_show("Spaces", clicked_sp==1?"Space 1":clicked_sp==2?"Space 2":clicked_sp==3?"Space 3":"Space 4", RGB(0,122,255));
                    }
                    g_mc_visible = 0; dirty = 1;
                    goto end_left_press;
                }
                /* Check "+" button to add a space */
                if (my >= MENUBAR_H+4 && my < MENUBAR_H+58 && mx >= VGA_WIDTH-30) {
                    if (g_num_spaces < 4) { g_num_spaces++; g_current_space = g_num_spaces; }
                    g_mc_visible = 0; dirty = 1; goto end_left_press;
                }
                int mc_item = mission_control_hit(mx, my);
                if (mc_item >= 0) {
                    win_bring_to_front(mc_item);
                }
                g_mc_visible = 0;
                dirty = 1;
                goto end_left_press;
            }
            /* Launchpad click */
            if (g_lp_visible) {
                int lp_item = launchpad_hit(mx, my);
                if (lp_item >= 0) {
                    const char *aname = s_lp_icons[lp_item].name;
                    int j2, found2=0;
                    for (j2=0;j2<g_num_windows;j2++) {
                        const char *wt = g_windows[j2].title;
                        if (wt && (str_eq(wt,aname) ||
                            (str_eq(aname,"Finder") && str_eq(wt,"MyOS Finder")))) {
                            g_windows[j2].visible=1; win_bring_to_front(j2);
                            if (str_eq(aname,"Wordle")) { g_wordle_focused=1; }
                            if (str_eq(aname,"Dictionary")) { g_dict_focused=1; g_edit_focused=0; }
                            if (str_eq(aname,"TextEdit")) { g_edit_focused=1; g_dict_focused=0; }
                            found2=1; break;
                        }
                    }
                    if (!found2 && g_num_windows < MAX_WINDOWS) {
                        gui_window_t *nw2 = &g_windows[g_num_windows];
                        nw2->focused=0; nw2->dragging=0; nw2->visible=1; nw2->maximized=0;
                        if (str_eq(aname,"Calculator"))  { nw2->x=180;nw2->y=100;nw2->w=220;nw2->h=280; }
                        else if (str_eq(aname,"Settings")) { nw2->x=150;nw2->y=50; nw2->w=500;nw2->h=400; }
                        else if (str_eq(aname,"TextEdit")) { nw2->x=120;nw2->y=80; nw2->w=310;nw2->h=260; g_edit_focused=1; }
                        else if (str_eq(aname,"Terminal")) { nw2->x=100;nw2->y=100;nw2->w=290;nw2->h=220; }
                        else if (str_eq(aname,"Clock"))    { nw2->x=50; nw2->y=80; nw2->w=180;nw2->h=220; }
                        else if (str_eq(aname,"Notes"))    { nw2->x=18; nw2->y=270;nw2->w=260;nw2->h=240; }
                        else if (str_eq(aname,"Calendar")) { nw2->x=200;nw2->y=80; nw2->w=300;nw2->h=260; }
                        else if (str_eq(aname,"Mail"))             { nw2->x=100;nw2->y=80; nw2->w=360;nw2->h=260; }
                        else if (str_eq(aname,"Activity Monitor")) { nw2->x=140;nw2->y=80; nw2->w=320;nw2->h=270; }
                        else if (str_eq(aname,"System Info"))      { nw2->x=180;nw2->y=100;nw2->w=280;nw2->h=220; }
                        else if (str_eq(aname,"Stocks"))    { nw2->x=140;nw2->y=60;nw2->w=320;nw2->h=280; }
                        else if (str_eq(aname,"News"))      { nw2->x=150;nw2->y=70;nw2->w=340;nw2->h=290; }
                        else if (str_eq(aname,"Books"))     { nw2->x=160;nw2->y=70;nw2->w=300;nw2->h=280; }
                        else if (str_eq(aname,"Podcasts"))  { nw2->x=160;nw2->y=70;nw2->w=280;nw2->h=260; }
                        else if (str_eq(aname,"Reminders")) { nw2->x=200;nw2->y=80;nw2->w=260;nw2->h=240; }
                        else if (str_eq(aname,"Home"))      { nw2->x=150;nw2->y=65;nw2->w=340;nw2->h=290; }
                        else if (str_eq(aname,"Messages"))  { nw2->x=130;nw2->y=60;nw2->w=340;nw2->h=290; }
                        else if (str_eq(aname,"Find My"))   { nw2->x=140;nw2->y=65;nw2->w=320;nw2->h=270; }
                        else if (str_eq(aname,"Wallet"))    { nw2->x=180;nw2->y=70;nw2->w=260;nw2->h=300; }
                        else if (str_eq(aname,"Voice Memos")) { nw2->x=160;nw2->y=70;nw2->w=240;nw2->h=300; }
                        else if (str_eq(aname,"Shortcuts")) { nw2->x=150;nw2->y=70;nw2->w=300;nw2->h=280; }
                        else if (str_eq(aname,"Translate")) { nw2->x=160;nw2->y=65;nw2->w=310;nw2->h=280; }
                        else if (str_eq(aname,"Freeform"))  { nw2->x=110;nw2->y=55;nw2->w=360;nw2->h=310; }
                        else if (str_eq(aname,"Disk Utility")) { nw2->x=130;nw2->y=60;nw2->w=340;nw2->h=260; }
                        else if (str_eq(aname,"Weather"))   { nw2->x=180;nw2->y=70;nw2->w=260;nw2->h=280; }
                        else if (str_eq(aname,"FaceTime"))  { nw2->x=200;nw2->y=80;nw2->w=220;nw2->h=260; g_facetime_active=0; g_facetime_contact=0; }
                        else if (str_eq(aname,"AirDrop"))             { nw2->x=240;nw2->y=100;nw2->w=240;nw2->h=220; }
                        else if (str_eq(aname,"Keyboard Shortcuts"))  { nw2->x=90;nw2->y=40;nw2->w=620;nw2->h=500; }
                        else if (str_eq(aname,"Color Picker"))        { nw2->x=220;nw2->y=80; nw2->w=220;nw2->h=310; }
                        else if (str_eq(aname,"Script Editor"))       { nw2->x=130;nw2->y=70; nw2->w=340;nw2->h=290; }
                        else if (str_eq(aname,"Migration Assistant")) { nw2->x=170;nw2->y=80; nw2->w=300;nw2->h=290; }
                        else if (str_eq(aname,"Screen Time"))        { nw2->x=160;nw2->y=60; nw2->w=320;nw2->h=320; }
                        else if (str_eq(aname,"Passwords"))          { nw2->x=120;nw2->y=60; nw2->w=380;nw2->h=300; }
                        else if (str_eq(aname,"Numbers"))            { nw2->x=100;nw2->y=55; nw2->w=400;nw2->h=300; }
                        else if (str_eq(aname,"Focus"))              { nw2->x=200;nw2->y=65; nw2->w=280;nw2->h=340; }
                        else if (str_eq(aname,"Keynote"))            { nw2->x=80; nw2->y=50; nw2->w=420;nw2->h=320; }
                        else if (str_eq(aname,"Pages"))              { nw2->x=100;nw2->y=50; nw2->w=380;nw2->h=320; }
                        else if (str_eq(aname,"GarageBand"))         { nw2->x=60; nw2->y=45; nw2->w=450;nw2->h=330; }
                        else if (str_eq(aname,"iMovie"))             { nw2->x=70; nw2->y=45; nw2->w=440;nw2->h=330; }
                        else if (str_eq(aname,"Xcode"))              { nw2->x=60; nw2->y=45; nw2->w=460;nw2->h=330; }
                        else if (str_eq(aname,"GameCenter"))         { nw2->x=150;nw2->y=60; nw2->w=340;nw2->h=320; }
                        else if (str_eq(aname,"Automator"))          { nw2->x=80; nw2->y=50; nw2->w=420;nw2->h=320; }
                        else if (str_eq(aname,"Font Book"))          { nw2->x=120;nw2->y=60; nw2->w=360;nw2->h=300; }
                        else if (str_eq(aname,"Console"))            { nw2->x=90; nw2->y=50; nw2->w=440;nw2->h=310; }
                        else if (str_eq(aname,"iPhone Mirroring"))   { nw2->x=250;nw2->y=50; nw2->w=260;nw2->h=310; }
                        else if (str_eq(aname,"Instruments"))        { nw2->x=70; nw2->y=45; nw2->w=450;nw2->h=320; }
                        else if (str_eq(aname,"Network Utility"))    { nw2->x=100;nw2->y=55; nw2->w=400;nw2->h=300; }
                        else if (str_eq(aname,"Math Notes"))         { nw2->x=160;nw2->y=50; nw2->w=300;nw2->h=340; }
                        else if (str_eq(aname,"Final Cut Pro"))      { nw2->x=50; nw2->y=35; nw2->w=500;nw2->h=340; }
                        else if (str_eq(aname,"Logic Pro"))          { nw2->x=50; nw2->y=35; nw2->w=500;nw2->h=340; }
                        else if (str_eq(aname,"Motion"))             { nw2->x=55; nw2->y=40; nw2->w=480;nw2->h=330; }
                        else if (str_eq(aname,"MainStage"))          { nw2->x=50; nw2->y=40; nw2->w=490;nw2->h=320; }
                        else if (str_eq(aname,"Compressor"))         { nw2->x=130;nw2->y=55; nw2->w=380;nw2->h=310; }
                        else if (str_eq(aname,"Screen Recording"))   { nw2->x=200;nw2->y=70; nw2->w=300;nw2->h=260; }
                        else if (str_eq(aname,"Sidecar"))            { nw2->x=180;nw2->y=60; nw2->w=280;nw2->h=280; }
                        else if (str_eq(aname,"Universal Control"))  { nw2->x=130;nw2->y=55; nw2->w=360;nw2->h=260; }
                        else if (str_eq(aname,"Handoff"))            { nw2->x=160;nw2->y=60; nw2->w=320;nw2->h=280; }
                        else if (str_eq(aname,"Privacy"))            { nw2->x=110;nw2->y=50; nw2->w=400;nw2->h=300; }
                        else if (str_eq(aname,"Accessibility"))      { nw2->x=110;nw2->y=50; nw2->w=400;nw2->h=300; }
                        else if (str_eq(aname,"AirPlay"))            { nw2->x=200;nw2->y=60; nw2->w=280;nw2->h=260; }
                        else if (str_eq(aname,"TestFlight"))         { nw2->x=150;nw2->y=55; nw2->w=340;nw2->h=300; }
                        else if (str_eq(aname,"Reality Composer"))   { nw2->x=80; nw2->y=40; nw2->w=440;nw2->h=320; }
                        else if (str_eq(aname,"Configurator"))       { nw2->x=110;nw2->y=50; nw2->w=400;nw2->h=300; }
                        else if (str_eq(aname,"Stickies"))           { nw2->x=160;nw2->y=80; nw2->w=280;nw2->h=220; }
                        else if (str_eq(aname,"Dictionary"))         { nw2->x=180;nw2->y=80; nw2->w=300;nw2->h=220; g_dict_focused=1; g_dict_input_len=0; g_dict_input[0]=0; }
                        else if (str_eq(aname,"Chess"))              { nw2->x=170;nw2->y=70; nw2->w=240;nw2->h=240; }
                        else if (str_eq(aname,"2048"))               { nw2->x=150;nw2->y=60; nw2->w=240;nw2->h=280; g2048_new_game(); }
                        else if (str_eq(aname,"Grapher"))            { nw2->x=140;nw2->y=60; nw2->w=320;nw2->h=260; }
                        else if (str_eq(aname,"Digital Color Meter")){ nw2->x=200;nw2->y=80; nw2->w=280;nw2->h=200; }
                        else if (str_eq(aname,"Photo Booth"))        { nw2->x=140;nw2->y=60; nw2->w=320;nw2->h=280; }
                        else if (str_eq(aname,"SF Symbols"))         { nw2->x=130;nw2->y=60; nw2->w=340;nw2->h=270; }
                        else if (str_eq(aname,"Transporter"))        { nw2->x=170;nw2->y=70; nw2->w=300;nw2->h=240; }
                        else if (str_eq(aname,"AR Quick Look"))      { nw2->x=130;nw2->y=55; nw2->w=340;nw2->h=280; }
                        else if (str_eq(aname,"Feedback Assistant")) { nw2->x=150;nw2->y=60; nw2->w=340;nw2->h=260; }
                        else if (str_eq(aname,"Snake"))              { nw2->x=120;nw2->y=45; nw2->w=316;nw2->h=252; toast_show("Snake","Space=start, Arrows=move",RGB(52,199,89)); }
                        else if (str_eq(aname,"Wordle"))             { nw2->x=150;nw2->y=40; nw2->w=280;nw2->h=380; { int wr,wc; for(wr=0;wr<WORDLE_ROWS;wr++){for(wc=0;wc<WORDLE_COLS;wc++){g_wordle_guesses[wr][wc]=0;g_wordle_results[wr][wc]=0;} g_wordle_guesses[wr][WORDLE_COLS]=0;} for(wr=0;wr<26;wr++)g_wordle_kb_state[wr]=0; g_wordle_cur_row=0;g_wordle_cur_col=0;g_wordle_state=0; g_wordle_answer_idx=0; g_wordle_focused=1; } toast_show("Wordle","Type 5-letter word, Enter to guess",RGB(108,169,100)); }
                        else if (str_eq(aname,"Breakout"))           { nw2->x=100;nw2->y=40; nw2->w=320;nw2->h=280; toast_show("Breakout","Space=start, A/D=move",RGB(100,180,255)); }
                        else if (str_eq(aname,"Pong"))               { nw2->x=120;nw2->y=40; nw2->w=300;nw2->h=260; toast_show("Pong","Space=start, W/S=move",RGB(255,180,50)); g_pong_active=0;g_pong_over=0;g_pong_score_p=0;g_pong_score_a=0; }
                        else if (str_eq(aname,"Minesweeper")) { nw2->x=200;nw2->y=60;nw2->w=200;nw2->h=264; g_mine_state=0;g_mine_remaining=MINE_COUNT;g_mine_rng=1; int _r,_c; for(_r=0;_r<MINE_ROWS;_r++)for(_c=0;_c<MINE_COLS;_c++){g_mine_board[_r][_c]=0;g_mine_vis[_r][_c]=0;g_mine_flag[_r][_c]=0;} toast_show("Minesweeper","Left=reveal, Right=flag",RGB(80,80,200)); }
                        else if (str_eq(aname,"Journal"))     { nw2->x=110;nw2->y=50; nw2->w=380;nw2->h=340; g_journal_sel=0; g_journal_focused=0; toast_show("Journal","Select an entry to read",RGB(255,149,0)); }
                        else if (str_eq(aname,"Contacts"))    { nw2->x=150;nw2->y=50; nw2->w=420;nw2->h=320; g_ct_sel=0; toast_show("Contacts","Your address book",RGB(0,122,255)); }
                        else if (str_eq(aname,"Preview"))     { nw2->x=160;nw2->y=60; nw2->w=380;nw2->h=300; g_preview_page=0; g_preview_zoom=100; g_preview_markup=0; toast_show("Preview","Open documents and images",RGB(170,50,170)); }
                        else if (str_eq(aname,"Apple TV"))    { nw2->x=120;nw2->y=50; nw2->w=400;nw2->h=300; g_atv_sel=0; toast_show("Apple TV","Stream movies and TV shows",RGB(255,255,255)); }
                        else { nw2->x=200;nw2->y=120;nw2->w=280;nw2->h=220; }
                        nw2->title = aname;
                        g_win_anim[g_num_windows] = OPEN_ANIM;
                        g_num_windows++;
                        toast_show(aname, "Opened from Launchpad", s_lp_icons[lp_item].color);
                    }
                }
                g_lp_visible = 0;
                dirty = 1;
                goto end_left_press;
            }
            /* Menu bar: toggle dropdown */
            if (my < MENUBAR_H && !g_photos_fullscreen) {
                /* Right side of menu bar:
                 *  Siri area         (VGA_WIDTH-220..VGA_WIDTH-200) → open Siri
                 *  battery/wifi area (VGA_WIDTH-160..VGA_WIDTH-80)  → Control Center
                 *  clock/date area   (VGA_WIDTH-80..VGA_WIDTH)      → Notification Center */
                if (mx >= VGA_WIDTH - 220 && mx < VGA_WIDTH - 200) {
                    /* Siri toggle */
                    g_siri_visible = !g_siri_visible;
                    if (g_siri_visible) { g_siri_qlen=0; g_siri_query[0]=0; g_siri_response=0; }
                    dirty = 1;
                    goto end_left_press;
                }
                if (mx >= VGA_WIDTH - 80) {
                    g_nc_visible = !g_nc_visible;
                    g_cc_visible = 0;
                    g_menu_open = -1;
                    dirty = 1;
                    goto end_left_press;
                }
                if (mx >= VGA_WIDTH - 160) {
                    g_cc_visible = !g_cc_visible;
                    g_nc_visible = 0;
                    g_menu_open = -1;
                    dirty = 1;
                    goto end_left_press;
                }
                int hit_menu = menubar_hit(mx, my);
                /* If dropdown is open: click on same title closes, item click triggers */
                if (g_menu_open >= 0) {
                    int ditem = dropdown_hit(mx, my);
                    if (ditem >= 0) dropdown_action(g_menu_open, ditem);
                    g_menu_open = -1;
                } else if (hit_menu >= 0) {
                    g_menu_open = hit_menu;
                }
                /* Close context menu */
                g_ctx_visible = 0;
                goto end_left_press;
            }
            /* Close open dropdown/context on any click outside menu bar */
            if (g_menu_open >= 0) {
                int ditem = dropdown_hit(mx, my);
                if (ditem >= 0) dropdown_action(g_menu_open, ditem);
                g_menu_open = -1;
                goto end_left_press;
            }
            g_ctx_visible = 0;
            /* Dock context menu click */
            if (g_dock_ctx_visible) {
                int dci = dock_ctx_hit(mx, my);
                g_dock_ctx_visible = 0;
                if (dci >= 0) {
                    int dii = g_dock_ctx_icon;
                    const char *an2 = (dii < NUM_DOCK_ICONS) ? s_dock_icons[dii].name : "?";
                    const char *wn2 = str_eq(an2,"Finder") ? "MyOS Finder" : an2;
                    /* item 0=Open, 2=Options, 3=Show in Finder, 5=Quit */
                    if (dci == 0) { /* Open */
                        /* same as left-click on dock icon */
                        int fnd2 = 0, j3;
                        for (j3 = 0; j3 < g_num_windows; j3++) {
                            if (g_windows[j3].title && str_eq(g_windows[j3].title, wn2)) {
                                g_windows[j3].visible = 1;
                                g_win_minimized[j3] = 0;
                                win_bring_to_front(j3);
                                fnd2 = 1; break;
                            }
                        }
                        if (!fnd2 && g_num_windows < MAX_WINDOWS) {
                            gui_window_t *nw2 = &g_windows[g_num_windows];
                            nw2->focused=0; nw2->dragging=0; nw2->visible=1; nw2->maximized=0;
                            nw2->x=120; nw2->y=80; nw2->w=260; nw2->h=200; nw2->title=an2;
                            nw2->space = g_current_space;
                            g_win_anim[g_num_windows] = OPEN_ANIM;
                            g_num_windows++;
                            toast_show(an2, "Opened", s_dock_icons[dii].color);
                        }
                    } else if (dci == 5) { /* Quit */
                        int j3;
                        for (j3 = g_num_windows-1; j3 >= 0; j3--) {
                            if (g_windows[j3].title && str_eq(g_windows[j3].title, wn2)) {
                                win_close(j3); break;
                            }
                        }
                        toast_show(an2, "Quit", RGB(255,59,48));
                    } else if (dci == 3) { /* Show in Finder */
                        /* Open Finder */
                        int fnd3 = 0, j3;
                        for (j3=0; j3<g_num_windows; j3++) {
                            if (g_windows[j3].title && str_eq(g_windows[j3].title,"MyOS Finder")) {
                                g_windows[j3].visible=1; win_bring_to_front(j3); fnd3=1; break;
                            }
                        }
                        if (!fnd3 && g_num_windows < MAX_WINDOWS) {
                            gui_window_t *nf = &g_windows[g_num_windows];
                            nf->focused=0; nf->dragging=0; nf->visible=1; nf->maximized=0;
                            nf->x=200; nf->y=80; nf->w=380; nf->h=340; nf->title="MyOS Finder";
                            nf->space=g_current_space;
                            g_win_anim[g_num_windows]=OPEN_ANIM; g_num_windows++;
                        }
                        toast_show("Finder", an2, RGB(80,140,200));
                    }
                    dirty = 1;
                }
                goto end_left_press;
            }
            /* Z-ordering: find topmost window under cursor, bring to front */
            {
                int ci;
                for (ci = g_num_windows - 1; ci >= 0; ci--) {
                    gui_window_t *w = &g_windows[ci];
                    if (!w->visible) continue;
                    if (mx >= w->x && mx < w->x + w->w &&
                        my >= w->y && my < w->y + w->h) {
                        const char *clicked_title = w->title;
                        win_bring_to_front(ci); /* no-op if already on top */
                        /* Set TextEdit focus based on which window is now on top */
                        g_edit_focused = (clicked_title && str_eq(clicked_title, "TextEdit")) ? 1 : 0;
                        break;
                    }
                }
            }
            /* Settings sidebar clicks — run early so topmost Settings always wins */
            { int si3;
              for (si3 = g_num_windows-1; si3 >= 0; si3--) {
                  gui_window_t *sw = &g_windows[si3];
                  if (!sw->visible || !sw->title || !str_eq(sw->title,"Settings")) continue;
                  /* Only process if Settings is the topmost window at click position */
                  int sw_top = 1;
                  { int ci2;
                    for (ci2 = si3+1; ci2 < g_num_windows; ci2++) {
                        gui_window_t *ww2 = &g_windows[ci2];
                        if (!ww2->visible) continue;
                        if (mx >= ww2->x && mx < ww2->x+ww2->w &&
                            my >= ww2->y && my < ww2->y+ww2->h) { sw_top=0; break; }
                    }
                  }
                  if (!sw_top) break;
                  /* Sidebar click */
                  int scy = sw->y + TITLEBAR_H + 1;
                  int ssb_w = 130;
                  if (mx >= sw->x+1 && mx < sw->x+ssb_w &&
                      my >= scy && my < sw->y+sw->h-19) {
                      int srel = my - (scy + 44);
                      if (srel >= 0) {
                          int sti = srel / 20;
                          if (sti >= 0 && sti < 14) {
                              g_settings_tab = sti; dirty=1; goto end_left_press;
                          }
                      }
                  }
                  /* Content toggles */
                  int stx_r = sw->x + sw->w - 54;
                  int sth = 20, stwid = 36;
                  int scx3 = sw->x + ssb_w + 10;
                  int scy3 = scy + 10;
                  if (g_settings_tab == 0) {
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+231&&my<scy3+231+sth){g_pref_wifi^=1;toast_show("Wi-Fi",g_pref_wifi?"On":"Off",RGB(0,122,255));dirty=1;}
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+251&&my<scy3+251+sth){g_pref_bt^=1;toast_show("Bluetooth",g_pref_bt?"On":"Off",RGB(0,122,255));dirty=1;}
                      {int swi;for(swi=0;swi<5;swi++){
                       int swx=swi<4?scx3+swi*74:scx3, swy=swi<4?scy3+132:scy3+170;
                       if(mx>=swx&&mx<swx+62&&my>=swy&&my<swy+30){g_pref_wallpaper=swi;dirty=1;}}}
                  } else if (g_settings_tab == 1) {
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+41&&my<scy3+41+sth){g_pref_darkmode^=1;dirty=1;}
                      { int sai; for (sai=0; sai<7; sai++) {
                        int sax=scx3+sai*24+12, say=scy3+126;
                        if (mx>=sax-12 && mx<sax+12 && my>=say-12 && my<say+12) {
                            g_pref_accent=sai; toast_show("Appearance","Accent selected",RGB(0,122,255)); dirty=1; goto end_left_press;
                        } } }
                  } else if (g_settings_tab == 2) {
                      { int sri; for (sri=0; sri<4; sri++) { int sry=scy3+128+sri*22;
                        if (mx>=scx3-2 && mx<scx3+sw->w-ssb_w-20 && my>=sry-2 && my<sry+16) {
                            g_pref_resolution=sri; toast_show("Display","Resolution selected",RGB(0,122,255)); dirty=1; goto end_left_press;
                        } } }
                  } else if (g_settings_tab == 3) {
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+15&&my<scy3+15+sth){g_pref_notifs^=1;dirty=1;}
                  } else if (g_settings_tab == 4) {
                      { int sai; for (sai=0; sai<6; sai++) { int sx=scx3+(sai%3)*86, sy=scy3+76+(sai/3)*28;
                        if (mx>=sx && mx<sx+82 && my>=sy && my<sy+24) {
                            g_pref_alert_sound=sai; toast_show("Sound","Alert sound selected",RGB(0,122,255)); dirty=1; goto end_left_press;
                        } } }
                  } else if (g_settings_tab == 5) {
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+15&&my<scy3+15+sth){g_pref_dnd^=1;dirty=1;}
                  } else if (g_settings_tab == 6) {
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+227&&my<scy3+227+sth){g_pref_vpn^=1;toast_show("VPN",g_pref_vpn?"Connected":"Disconnected",RGB(0,122,255));dirty=1;}
                  } else if (g_settings_tab == 7) {
                      if (mx>=stx_r&&mx<stx_r+stwid&&my>=scy3+15&&my<scy3+15+sth){g_pref_bt^=1;toast_show("Bluetooth",g_pref_bt?"On":"Off",RGB(0,122,255));dirty=1;}
                  }
                  break;
              }
            }
            /* Traffic light buttons */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible) continue;
                int btn_cy = w->y + TITLEBAR_H / 2;
                /* Red close (x+12) */
                {
                    int dx = mx - (w->x + 12), dy = my - btn_cy;
                    if (dx*dx + dy*dy <= 36) {
                        win_close(i);
                        str_cpy(g_status, "Window closed.");
                        dirty = 1;
                        goto end_left_press;
                    }
                }
                /* Yellow minimize (x+30) */
                {
                    int dx = mx - (w->x + 30), dy = my - btn_cy;
                    if (dx*dx + dy*dy <= 36) {
                        win_minimize(i);
                        str_cpy(g_status, "Minimized.");
                        dirty = 1;
                        goto end_left_press;
                    }
                }
                /* Green maximize / full-screen (x+48) */
                {
                    int dx = mx - (w->x + 48), dy = my - btn_cy;
                    if (dx*dx + dy*dy <= 36) {
                        if (w->maximized) {
                            /* Restore */
                            w->x=w->orig_x; w->y=w->orig_y;
                            w->w=w->orig_w; w->h=w->orig_h;
                            w->maximized=0;
                        } else {
                            /* Maximize to full screen area */
                            w->orig_x=w->x; w->orig_y=w->y;
                            w->orig_w=w->w; w->orig_h=w->h;
                            w->x=0; w->y=MENUBAR_H;
                            w->w=VGA_WIDTH; w->h=DOCK_Y-MENUBAR_H;
                            w->maximized=1;
                        }
                        dirty=1;
                    }
                }
            }
            /* Photos fullscreen click handler */
            if (g_photos_fullscreen) {
                int edit_panel_w2 = g_photos_edit_mode ? 140 : 0;
                int photo_w2 = VGA_WIDTH - edit_panel_w2;
                /* Edit panel clicks */
                if (g_photos_edit_mode && mx >= photo_w2) {
                    /* Tool tabs */
                    if (my < 28) {
                        int eti2 = (mx - photo_w2) / (edit_panel_w2/3);
                        if (eti2 >= 0 && eti2 < 3) { g_photos_edit_tool=eti2; dirty=1; goto end_left_press; }
                    }
                    /* Done button */
                    if (my >= VGA_HEIGHT-36 && my < VGA_HEIGHT-12) {
                        g_photos_edit_mode=0; dirty=1; goto end_left_press;
                    }
                    /* Brightness slider */
                    if (g_photos_edit_tool==0 && my>=76 && my<88) {
                        int sl_x2=photo_w2+8, sl_w2=edit_panel_w2-16;
                        if (mx>=sl_x2 && mx<sl_x2+sl_w2) {
                            g_photos_brightness=(mx-sl_x2)*100/sl_w2; dirty=1; goto end_left_press;
                        }
                    }
                    /* Contrast slider */
                    if (g_photos_edit_tool==0 && my>=120 && my<132) {
                        int sl_x2=photo_w2+8, sl_w2=edit_panel_w2-16;
                        if (mx>=sl_x2 && mx<sl_x2+sl_w2) {
                            g_photos_contrast=(mx-sl_x2)*100/sl_w2; dirty=1; goto end_left_press;
                        }
                    }
                    /* Saturation slider */
                    if (g_photos_edit_tool==0 && my>=164 && my<176) {
                        int sl_x2=photo_w2+8, sl_w2=edit_panel_w2-16;
                        if (mx>=sl_x2 && mx<sl_x2+sl_w2) {
                            g_photos_saturation=(mx-sl_x2)*100/sl_w2; dirty=1; goto end_left_press;
                        }
                    }
                    dirty=1; goto end_left_press;
                }
                /* Close button */
                if (mx>=photo_w2-28 && mx<photo_w2-8 && my>=4 && my<24) {
                    g_photos_fullscreen=0; g_photos_edit_mode=0; dirty=1; goto end_left_press;
                }
                /* Edit button */
                if (mx>=photo_w2-72 && mx<photo_w2-32 && my>=4 && my<24) {
                    g_photos_edit_mode^=1; dirty=1; goto end_left_press;
                }
                /* Left arrow */
                if (mx>=4 && mx<28 && my>=VGA_HEIGHT/2-20 && my<VGA_HEIGHT/2+20) {
                    g_photos_sel = (g_photos_sel+5)%6; dirty=1; goto end_left_press;
                }
                /* Right arrow (only if not in edit mode) */
                if (!g_photos_edit_mode && mx>=photo_w2-28 && mx<photo_w2-4 && my>=VGA_HEIGHT/2-20 && my<VGA_HEIGHT/2+20) {
                    g_photos_sel = (g_photos_sel+1)%6; dirty=1; goto end_left_press;
                }
                /* Click in photo area: close */
                if (mx < photo_w2) { g_photos_fullscreen=0; g_photos_edit_mode=0; dirty=1; goto end_left_press; }
                dirty=1; goto end_left_press;
            }
            /* Safari tab bar clicks */
            safari_normalize_state();
            for (i=0; i<g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Safari")) continue;
                if (i != win_top_visible()) continue;
                int sf_taby2 = w->y+TITLEBAR_H+1;
                if (my >= sf_taby2 && my < sf_taby2+22) {
                    /* "+" new tab button */
                    if (mx >= w->x+w->w-24 && mx < w->x+w->w-4) {
                        if (g_safari_tab_count < SAFARI_MAX_TABS) {
                            str_cpy(g_safari_tab_urls[g_safari_active_tab], g_safari_url);
                            g_safari_active_tab = g_safari_tab_count++;
                            safari_reset_tab_state(g_safari_active_tab, "about:home");
                            safari_load_current_tab();
                            dirty=1;
                        }
                        goto end_left_press;
                    }
                    /* Tab click: switch or close */
                    { int n_tabs2 = g_safari_tab_count > 0 ? g_safari_tab_count : 1;
                      int avail_w2 = w->w - 2 - 22;
                      int tab_w2 = avail_w2 / n_tabs2;
                      if (tab_w2 > 160) tab_w2 = 160;
                      if (tab_w2 < 1) tab_w2 = 1;
                      int ti4;
                      for (ti4=0; ti4<n_tabs2; ti4++) {
                          int tx4 = w->x + 1 + ti4*(tab_w2+1);
                          if (mx >= tx4 && mx < tx4+tab_w2) {
                              /* Close button (right 14px of tab) */
                              if (tab_w2 > 30 && mx >= tx4+tab_w2-14) {
                                  if (g_safari_tab_count > 1) {
                                      int j5;
                                      for (j5=ti4; j5<g_safari_tab_count-1; j5++) {
                                          safari_copy_tab_state(j5, j5+1);
                                      }
                                      g_safari_tab_count--;
                                      safari_reset_tab_state(g_safari_tab_count, "about:home");
                                      if (g_safari_active_tab > ti4)
                                          g_safari_active_tab--;
                                      if (g_safari_active_tab >= g_safari_tab_count)
                                          g_safari_active_tab = g_safari_tab_count-1;
                                      str_cpy(g_safari_url, g_safari_tab_urls[g_safari_active_tab]);
                                      safari_load_current_tab();
                                  }
                              } else {
                                  /* Switch tab */
                                  str_cpy(g_safari_tab_urls[g_safari_active_tab], g_safari_url);
                                  g_safari_active_tab = ti4;
                                  str_cpy(g_safari_url, g_safari_tab_urls[g_safari_active_tab]);
                                  safari_load_current_tab();
                              }
                              dirty=1; goto end_left_press;
                          }
                      }
                    }
                    goto end_left_press;
                }
                break;
            }
            /* Safari URL bar click: focus for typing */
            for (i=0; i<g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Safari")) continue;
                if (i != win_top_visible()) continue;
                int tbary = w->y+TITLEBAR_H+1+22;
                int ab_x2 = w->x+44, ab_w2 = w->w-90;
                if (my>=tbary+5 && my<tbary+21) {
                    if (mx>=w->x+4 && mx<w->x+20) {
                        g_safari_url_focused=0;
                        g_safari_form_focused=-1;
                        if (safari_can_go_back()) safari_go_back();
                        else toast_show("Safari", "No back history", RGB(120,120,128));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+20 && mx<w->x+36) {
                        g_safari_url_focused=0;
                        g_safari_form_focused=-1;
                        if (safari_can_go_forward()) safari_go_forward();
                        else toast_show("Safari", "No forward history", RGB(120,120,128));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+w->w-24 && mx<w->x+w->w-6) {
                        g_safari_url_focused=0;
                        g_safari_form_focused=-1;
                        g_share_visible=1;
                        gui_record_share_action("Sharing web page", RGB(40,160,220));
                        dirty=1; goto end_left_press;
                    }
                }
                if (mx>=ab_x2 && mx<ab_x2+ab_w2 && my>=tbary+5 && my<tbary+21) {
                    g_safari_url_focused=1; g_safari_form_focused=-1; dirty=1; goto end_left_press;
                }
                if (mx>=w->x+w->w-42 && mx<w->x+w->w-28 && my>=tbary+5 && my<tbary+21) {
                    g_safari_form_focused=-1; safari_reload(); dirty=1; goto end_left_press;
                }
                {
                    int cy_home = tbary + 28;
                    int ph_home = w->h-TITLEBAR_H-19-22-26-28;
                    if (safari_is_home_url(g_safari_url) && my >= cy_home && my < cy_home + ph_home) {
                        int sbx2 = w->x + w->w/2 - 110;
                        int sby2 = cy_home + 12;
                        int sbw2 = 220;
                        int sbh2 = 24;
                        if (mx>=sbx2 && mx<sbx2+sbw2 && my>=sby2 && my<sby2+sbh2) {
                            g_safari_url_focused=1; g_safari_form_focused=-1; g_safari_url[0]=0; dirty=1; goto end_left_press;
                        }
                        {
                            int fy2 = sby2 + sbh2 + 14 + 12;
                            int fav_cols2 = 4;
                            int fav_sz2 = (w->w-24)/fav_cols2 - 4;
                            int n_fav2 = safari_home_site_count();
                            int fi2;
                            if (fav_sz2 > 48) fav_sz2 = 48;
                            for (fi2=0; fi2<n_fav2; fi2++) {
                                int fc2 = fi2 % fav_cols2, fr2 = fi2 / fav_cols2;
                                int fx2 = w->x+12 + fc2*(fav_sz2+10);
                                int fya2 = fy2 + fr2*(fav_sz2+24);
                                const safari_home_site_t *site2 = safari_home_site_at(fi2);
                                if (!site2) continue;
                                if (mx>=fx2 && mx<fx2+fav_sz2 && my>=fya2 && my<fya2+fav_sz2+14) {
                                    safari_load_url(site2->url); dirty=1; goto end_left_press;
                                }
                            }
                        }
                    }
                    if (!safari_is_home_url(g_safari_url) && g_safari_link_count > 0 &&
                        my >= cy_home && my < cy_home + ph_home) {
                        int link_y2 = cy_home + 12 + 36 + 12;
                        int li_hit = (my - link_y2) / 12;
                        if (mx >= w->x+12 && mx < w->x+w->w-12 &&
                            li_hit >= 0 && li_hit < g_safari_link_count &&
                            my >= link_y2 + li_hit*12 && my < link_y2 + li_hit*12 + 12) {
                            safari_load_url(g_safari_link_urls[li_hit]); dirty=1; goto end_left_press;
                        }
                    }
                    if (!safari_is_home_url(g_safari_url) && g_safari_form_count > 0 &&
                        my >= cy_home && my < cy_home + ph_home) {
                        int form_y2 = cy_home + 12 + 36;
                        int fi_hit;
                        if (g_safari_link_count > 0)
                            form_y2 += 12 + g_safari_link_count * 12 + 8;
                        fi_hit = (my - (form_y2 + 12)) / 28;
                        if (fi_hit >= 0 && fi_hit < g_safari_form_count &&
                            my >= form_y2 + 12 + fi_hit * 28 &&
                            my < form_y2 + 12 + fi_hit * 28 + 24) {
                            g_safari_url_focused=0;
                            safari_focus_form(fi_hit);
                            if (mx >= w->x+w->w-52 && mx < w->x+w->w-12) {
                                safari_submit_form(fi_hit);
                                toast_show("Safari", g_safari_page_status, RGB(40,160,220));
                            }
                            dirty=1; goto end_left_press;
                        }
                    }
                }
                /* Clicking elsewhere in Safari = lose URL focus */
                if (my > w->y+TITLEBAR_H) { g_safari_url_focused=0; g_safari_form_focused=-1; dirty=1; }
            }
            /* Maps view mode toggle + zoom buttons */
            for (i=0; i<g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Maps")) continue;
                int cy3 = w->y+TITLEBAR_H+1;
                if (mx>=w->x+8 && mx<w->x+w->w-64 && my>=cy3+6 && my<cy3+28) {
                    g_maps_search_focused=1; gui_search_focus(GUI_SEARCH_MAPS); dirty=1; goto end_left_press;
                } else if (g_maps_search_focused) {
                    g_maps_search_focused=0; gui_search_blur(GUI_SEARCH_MAPS); dirty=1;
                }
                int vi;
                for (vi=0;vi<3;vi++) {
                    int vbx=w->x+w->w-70+vi*22, vby=cy3+4;
                    if (mx>=vbx && mx<vbx+20 && my>=vby && my<vby+20) {
                        g_maps_view=vi; dirty=1; goto end_left_press;
                    }
                }
                /* Zoom + button */
                { int zx=w->x+w->w-26;
                  if (mx>=zx && mx<zx+22 && my>=cy3+36 && my<cy3+58) {
                      if (g_maps_zoom < 4) g_maps_zoom++;
                      dirty=1; goto end_left_press;
                  }
                  /* Zoom - button */
                  if (mx>=zx && mx<zx+22 && my>=cy3+60 && my<cy3+82) {
                      if (g_maps_zoom > 1) { g_maps_zoom--; g_maps_pan_x=g_maps_pan_x/2; g_maps_pan_y=g_maps_pan_y/2; }
                      dirty=1; goto end_left_press;
                  }
                }
                { int ch=w->h-TITLEBAR_H-19;
                  int rc_y=cy3+ch-38;
                  if (mx>=w->x+w->w-56 && mx<w->x+w->w-12 && my>=rc_y+6 && my<rc_y+26) {
                      g_maps_route_started ^= 1;
                      toast_show("Maps",g_maps_route_started?"Route started":"Route stopped",RGB(0,122,255));
                      dirty=1; goto end_left_press;
                  } }
            }
            /* FaceTime accept / decline / end call buttons */
            for (i=0; i<g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"FaceTime")) continue;
                int fcy = w->y+TITLEBAR_H+1;
                int fch = w->h-TITLEBAR_H-19;
                if (g_facetime_active == 1) {
                    /* Ringing: Accept (green) and Decline (red) */
                    int btn_y = fcy+fch-38;
                    /* Decline: left */
                    if (mx>=w->x+w->w/2-60-18 && mx<w->x+w->w/2-60+18 && my>=btn_y-18 && my<btn_y+18) {
                        win_close(i); g_facetime_active=0; dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+w->w/2+60-18 && mx<w->x+w->w/2+60+18 && my>=btn_y-18 && my<btn_y+18) {
                        g_facetime_active=2; toast_show("FaceTime","Connected",RGB(52,199,89)); dirty=1; goto end_left_press;
                    }
                } else if (g_facetime_active == 2) {
                    int cb_y = fcy+fch-32;
                    if (mx>=w->x+w->w/2-20 && mx<w->x+w->w/2+20 && my>=cb_y-14 && my<cb_y+18) {
                        g_facetime_active=0; dirty=1; goto end_left_press;
                    }
                } else {
                    int cb_y = fcy+fch-32;
                    if (mx>=w->x+w->w/2-20 && mx<w->x+w->w/2+20 && my>=cb_y-14 && my<cb_y+18) {
                        g_facetime_active=2; toast_show("FaceTime","Connected",RGB(52,199,89)); dirty=1; goto end_left_press;
                    }
                }
            }
            /* Finder view toggle click */
            for (i=0; i<g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"MyOS Finder")) continue;
                { int tvx=w->x+56, tvy=w->y+TITLEBAR_H+7, tvi;
                  for (tvi=0; tvi<4; tvi++) {
                      if (mx>=tvx+tvi*15 && mx<tvx+(tvi+1)*15 && my>=tvy && my<tvy+16) {
                          g_finder_view=tvi; dirty=1; goto end_left_press;
                      }
                  } }
                int vbx = w->x+w->w-160;
                int vby = w->y+TITLEBAR_H+6;
                if (my>=vby && my<vby+16) {
                    if (mx>=vbx && mx<vbx+20) { g_finder_view=0; dirty=1; goto end_left_press; }
                    if (mx>=vbx+22 && mx<vbx+42) { g_finder_view=1; dirty=1; goto end_left_press; }
                    if (mx>=vbx+44 && mx<vbx+64) { g_finder_view=2; dirty=1; goto end_left_press; }
                }
            }
            /* Preview toolbar and thumbnails */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Preview")) continue;
                if (i != top_win_idx) continue;
                {
                    int cy_pv = w->y + TITLEBAR_H;
                    int ti_pv;
                    for (ti_pv=0; ti_pv<6; ti_pv++) {
                        int tx_pv = w->x + 4 + ti_pv * 30;
                        if (mx>=tx_pv && mx<tx_pv+26 && my>=cy_pv+4 && my<cy_pv+24) {
                            if (ti_pv == 0 && g_preview_page > 0) g_preview_page--;
                            else if (ti_pv == 1 && g_preview_page < 2) g_preview_page++;
                            else if (ti_pv == 2 && g_preview_zoom > 75) g_preview_zoom -= 25;
                            else if (ti_pv == 3 && g_preview_zoom < 150) g_preview_zoom += 25;
                            else if (ti_pv == 4) g_preview_zoom = 100;
                            else if (ti_pv == 5) g_preview_markup ^= 1;
                            dirty = 1; goto end_left_press;
                        }
                    }
                    for (ti_pv=0; ti_pv<3; ti_pv++) {
                        int ty_pv = cy_pv + 34 + ti_pv * 48;
                        if (mx>=w->x+4 && mx<w->x+58 && my>=ty_pv && my<ty_pv+42) {
                            g_preview_page = ti_pv;
                            dirty = 1; goto end_left_press;
                        }
                    }
                }
                break;
            }
            /* Screen Time tabs */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Screen Time")) continue;
                if (i != top_win_idx) continue;
                {
                    int tab_y_st = w->y + TITLEBAR_H + 50;
                    int hw_st = (w->w - 20) / 2;
                    if (my >= tab_y_st && my < tab_y_st + 22) {
                        if (mx >= w->x + 6 && mx < w->x + 6 + hw_st) {
                            g_screen_time_tab = 0; dirty = 1; goto end_left_press;
                        }
                        if (mx >= w->x + 10 + hw_st && mx < w->x + 10 + hw_st + hw_st) {
                            g_screen_time_tab = 1; dirty = 1; goto end_left_press;
                        }
                    }
                }
                break;
            }
            /* Photos thumbnail click: open fullscreen */
            for (i=0; i<g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Photos")) continue;
                if (i != top_win_idx) continue;
                { int ph_top_ev=w->y+TITLEBAR_H+1;
                  if (mx>=w->x+8 && mx<w->x+64 && my>=ph_top_ev+4 && my<ph_top_ev+24) {
                      g_photos_section=0; dirty=1; goto end_left_press;
                  }
                  if (mx>=w->x+72 && mx<w->x+128 && my>=ph_top_ev+4 && my<ph_top_ev+24) {
                      g_photos_section=1; dirty=1; goto end_left_press;
                  }
                  if (mx>=w->x+132 && mx<w->x+188 && my>=ph_top_ev+4 && my<ph_top_ev+24) {
                      g_photos_section=2; dirty=1; goto end_left_press;
                  }
                  if (mx>=w->x+188 && mx<w->x+248 && my>=ph_top_ev+6 && my<ph_top_ev+24) {
                      g_photos_search_focused=1; gui_search_focus(GUI_SEARCH_PHOTOS); dirty=1; goto end_left_press;
                  } else if (g_photos_search_focused) {
                      g_photos_search_focused=0; gui_search_blur(GUI_SEARCH_PHOTOS); dirty=1;
                  }
                  if (mx>=w->x+2 && mx<w->x+80) {
                      int lib_start=ph_top_ev+42;
                      int alb_start=ph_top_ev+124;
                      if (my>=lib_start && my<lib_start+4*16) {
                          static const int lib_map_ev[] = {0,3,4,5};
                          int li_ev=(my-lib_start)/16;
                          if (li_ev>=0 && li_ev<4) { g_photos_section=lib_map_ev[li_ev]; dirty=1; goto end_left_press; }
                      }
                      if (my>=alb_start && my<alb_start+3*16) {
                          int ai_ev=(my-alb_start)/16;
                          if (ai_ev>=0 && ai_ev<3) { g_photos_section=6+ai_ev; dirty=1; goto end_left_press; }
                      }
                  }
                  { int sb_w_ev=80, grid_x_ev=w->x+1+sb_w_ev+4;
                    int grid_y_ev=ph_top_ev+40;
                    int grid_w_ev=w->w-2-sb_w_ev-8;
                    int cols3=4, pad3=2;
                    int px_w3=grid_w_ev/cols3-pad3;
                    int px_h3=(int)(px_w3*0.75f);
                    int i2;
                    if (px_h3 < 40) px_h3=40;
                    for (i2=0;i2<8;i2++) {
                        int pc3=i2%cols3, pr3=i2/cols3;
                        int px3=grid_x_ev+pc3*(px_w3+pad3);
                        int py3=grid_y_ev+pr3*(px_h3+pad3);
                        if (mx>=px3 && mx<px3+px_w3 && my>=py3 && my<py3+px_h3) {
                            g_photos_sel=i2%6; g_photos_fullscreen=1; dirty=1; goto end_left_press;
                        }
                    } } }
            }
            /* Reminders: checkbox clicks and sidebar list selection */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Reminders")) continue;
                if (i != top_win_idx) continue;
                int rm_sb_w = 90;
                int rm_cy = w->y + TITLEBAR_H + 8;
                /* Sidebar list click */
                if (mx >= w->x+1 && mx < w->x+rm_sb_w && my >= w->y+TITLEBAR_H+24) {
                    int rli = (my - (w->y+TITLEBAR_H+24)) / 22;
                    if (rli >= 0 && rli < 7) { g_reminders_sel_list=rli; dirty=1; goto end_left_press; }
                }
                /* Checkbox clicks */
                { int rii;
                  for (rii=0; rii<3; rii++) {
                      int iy3 = rm_cy+30+rii*34;
                      int cx4 = w->x+rm_sb_w+8+9;
                      int dx3=mx-cx4, dy3=my-(iy3+9);
                      if (dx3*dx3+dy3*dy3 <= 100) {
                          g_reminders_done ^= (1<<rii); dirty=1; goto end_left_press;
                      }
                  }
                }
                /* Add Reminder button */
                { int add_y = w->y + w->h - 24;
                  if (mx >= w->x + rm_sb_w + 6 && mx < w->x + w->w - 8 &&
                      my >= add_y - 2 && my < add_y + 20) {
                      if (g_reminders_extra_items < 8) g_reminders_extra_items++;
                      toast_show("Reminders","Reminder added",RGB(255,149,0));
                      dirty=1; goto end_left_press;
                  } }
                break;
            }
            /* Photo Booth: filter tabs + capture button */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Photo Booth")) continue;
                if (i != top_win_idx) continue;
                int cy_pb = w->y + TITLEBAR_H;
                int fth_pb = 14, nf_pb = 6, ftw_pb = w->w / nf_pb;
                if (ftw_pb < 1) ftw_pb = 1;
                /* Filter tab strip */
                { int fi_pb;
                  for (fi_pb=0; fi_pb<nf_pb; fi_pb++) {
                      int ftx=w->x+fi_pb*ftw_pb+2;
                      if (mx>=ftx && mx<ftx+ftw_pb-4 && my>=cy_pb+2 && my<cy_pb+2+fth_pb) {
                          g_pb_filter = fi_pb;
                          dirty=1; goto end_left_press;
                      }
                  }
                }
                /* Capture button (red circle center of control bar) */
                { int vh_pb  = w->h - TITLEBAR_H - fth_pb - 60;
                  int bar_y_pb = cy_pb + fth_pb + 6 + vh_pb + 4;
                  int bar_h_pb = w->h - (bar_y_pb - w->y) - 4;
                  int btn_cx = w->x + w->w/2, btn_cy = bar_y_pb + bar_h_pb/2;
                  if (mx>=btn_cx-16 && mx<=btn_cx+16 && my>=btn_cy-16 && my<=btn_cy+16) {
                      /* Take photo */
                      g_pb_flash_tick = timer_ticks();
                      g_pb_photos[g_pb_captured % 4] = g_pb_filter;
                      g_pb_captured++;
                      static const char *pbn[]={"Normal","Sepia","B&W","Vintage","Comic","X-Ray"};
                      toast_show("Photo Booth", pbn[g_pb_filter], RGB(220,50,50));
                      dirty=1; goto end_left_press;
                  }
                }
                break;
            }
            /* Contacts app: click list or contact action buttons */
            { int ci_ct;
              for (ci_ct = g_num_windows-1; ci_ct >= 0; ci_ct--) {
                gui_window_t *w = &g_windows[ci_ct];
                if (!w->visible || !w->title || !str_eq(w->title,"Contacts")) continue;
                { int jj, cov=0;
                  for (jj=ci_ct+1; jj<g_num_windows; jj++) {
                      gui_window_t *wj=&g_windows[jj];
                      if (!wj->visible) continue;
                      if (mx>=wj->x&&mx<wj->x+wj->w&&my>=wj->y&&my<wj->y+wj->h){cov=1;break;}
                  }
                  if (cov) break;
                }
                int ct_top2 = w->y + TITLEBAR_H + 1;
                if (mx>=w->x+8 && mx<w->x+w->w-8 && my>=ct_top2+28 && my<ct_top2+44) {
                    g_contacts_search_focused=1; gui_search_focus(GUI_SEARCH_CONTACTS); dirty=1; goto end_left_press;
                } else if (g_contacts_search_focused) {
                    g_contacts_search_focused=0; gui_search_blur(GUI_SEARCH_CONTACTS); dirty=1;
                }
                int list_w2 = w->w * 2 / 5;
                int det_x2 = w->x + 1 + list_w2;
                int det_w2 = w->w - 1 - list_w2;
                int ab_y_ct = ct_top2 + 132;
                int ab_w_ct = (det_w2 - 8) / 4;
                int ca;
                if (mx>=w->x+8 && mx<w->x+52 && my>=ct_top2+4 && my<ct_top2+24) {
                    if (g_contacts_added < 99) g_contacts_added++;
                    toast_show("Contacts","Contact added",RGB(0,122,255));
                    dirty=1; goto end_left_press;
                }
                for (ca = 0; ca < 4; ca++) {
                    int abx_ct = det_x2 + 2 + ca * ab_w_ct;
                    if (mx>=abx_ct && mx<abx_ct+ab_w_ct-2 && my>=ab_y_ct && my<ab_y_ct+24) {
                        static const char *emails[8] = {
                            "alice@email.com", "bob@email.com", "carol@email.com", "david@email.com",
                            "emma@email.com", "frank@email.com", "grace@email.com", "henry@email.com"
                        };
                        int sel_ct = gui_contacts_visible_selection();
                        if (ca == 0) { g_facetime_visible=1; g_facetime_calling=1; toast_show("Contacts","Calling contact",RGB(52,199,89)); }
                        else if (ca == 1) { (void)gui_open_basic_app("FaceTime"); toast_show("Contacts","FaceTime started",RGB(0,199,190)); }
                        else if (ca == 2) { (void)gui_open_basic_app("Messages"); toast_show("Contacts","Message thread opened",RGB(52,199,89)); }
                        else { (void)gui_open_basic_app("Mail"); gui_mail_start_compose(emails[sel_ct], "Hello", ""); toast_show("Contacts","Mail draft ready",RGB(0,122,255)); }
                        dirty = 1; goto end_left_press;
                    }
                }
                if (mx >= w->x+1 && mx < w->x+1+list_w2 && my >= ct_top2+44) {
                    int row = (my - (ct_top2+62)) / 32;
                    int contact_index = gui_contacts_index_for_visible_row(row);
                    if (contact_index >= 0) {
                        g_ct_sel = contact_index;
                        dirty = 1;
                        goto end_left_press;
                    }
                }
                break;
              }
            }
            /* Messages: sidebar conv click + input field focus */
            { int ci_ms;
              for (ci_ms = g_num_windows-1; ci_ms >= 0; ci_ms--) {
                gui_window_t *w = &g_windows[ci_ms];
                if (!w->visible || !w->title || !str_eq(w->title,"Messages")) continue;
                { int jj2, cov2=0;
                  for (jj2=ci_ms+1; jj2<g_num_windows; jj2++) {
                      gui_window_t *wj2=&g_windows[jj2];
                      if (!wj2->visible) continue;
                      if (mx>=wj2->x&&mx<wj2->x+wj2->w&&my>=wj2->y&&my<wj2->y+wj2->h){cov2=1;break;}
                  }
                  if (cov2) break;
                }
                int sb_w2 = 130; if (sb_w2 > w->w/2) sb_w2=w->w/2;
                if (mx>=w->x+6 && mx<w->x+6+sb_w2-10 &&
                    my>=w->y+TITLEBAR_H+6 && my<w->y+TITLEBAR_H+24) {
                    g_ms_search_focused=1; gui_search_focus(GUI_SEARCH_MESSAGES); g_ms_focused=0; dirty=1; goto end_left_press;
                } else if (g_ms_search_focused) {
                    g_ms_search_focused=0; gui_search_blur(GUI_SEARCH_MESSAGES); dirty=1;
                }
                /* Sidebar conversation click */
                if (mx >= w->x+1 && mx < w->x+sb_w2 && my >= w->y+TITLEBAR_H+27) {
                    int ci2 = (my - (w->y+TITLEBAR_H+27)) / 33;
                    if (ci2 >= 0 && ci2 < 6) {
                        g_ms_sel = ci2;
                        g_ms_focused = 0;
                        dirty = 1;
                        goto end_left_press;
                    }
                }
                /* Input field click */
                { int chat_x2=w->x+sb_w2+4, chat_w2=w->w-sb_w2-6;
                  int tf_x2=chat_x2+44, tf_w2=chat_w2-66;
                  int inp_y2=w->y+w->h-40;
                  if (mx>=tf_x2 && mx<tf_x2+tf_w2 && my>=inp_y2+3 && my<inp_y2+18) {
                      g_ms_focused = 1;
                      dirty = 1;
                      goto end_left_press;
                  }
                }
                /* Click elsewhere = unfocus input */
                if (mx >= w->x && mx < w->x+w->w && my >= w->y && my < w->y+w->h) {
                    g_ms_focused = 0;
                    dirty = 1;
                }
                break;
              }
            }
            /* Dictionary: click search bar to focus */
            { int ci_di;
              for (ci_di=g_num_windows-1; ci_di>=0; ci_di--) {
                gui_window_t *w=&g_windows[ci_di];
                if (!w->visible||!w->title||!str_eq(w->title,"Dictionary")) continue;
                { int jjd,covd=0;
                  for(jjd=ci_di+1;jjd<g_num_windows;jjd++){
                      gui_window_t *wjd=&g_windows[jjd];
                      if(!wjd->visible)continue;
                      if(mx>=wjd->x&&mx<wjd->x+wjd->w&&my>=wjd->y&&my<wjd->y+wjd->h){covd=1;break;}
                  }
                  if(covd)break;
                }
                /* Click in search bar area */
                int sb_top=w->y+TITLEBAR_H+5, sb_bot=sb_top+18;
                if (my>=sb_top && my<sb_bot && mx>=w->x+8 && mx<w->x+w->w-8) {
                    g_dict_focused=1; dirty=1; goto end_left_press;
                } else {
                    g_dict_focused=0; dirty=1;
                }
                break;
              }
            }
            /* Sudoku: click cell to select + number pad */
            { int ci_sdk;
              for (ci_sdk=g_num_windows-1; ci_sdk>=0; ci_sdk--) {
                gui_window_t *w=&g_windows[ci_sdk];
                if (!w->visible||!w->title||!str_eq(w->title,"Sudoku")) continue;
                int cy_sdk = w->y + TITLEBAR_H;
                int bsz_sdk = w->w-14; if(bsz_sdk>w->h-TITLEBAR_H-50) bsz_sdk=w->h-TITLEBAR_H-50;
                int bx0_sdk = w->x+(w->w-bsz_sdk)/2, by0_sdk = cy_sdk+26;
                int cell_sdk = bsz_sdk/9;
                if (cell_sdk < 1) break;
                /* Board click */
                if (mx>=bx0_sdk && mx<bx0_sdk+bsz_sdk && my>=by0_sdk && my<by0_sdk+bsz_sdk) {
                    if (!g_sdk_started) {
                        /* Init board from puzzle */
                        int r3,c3;
                        for(r3=0;r3<9;r3++) for(c3=0;c3<9;c3++) {
                            g_sdk_board[r3][c3]=g_sdk_puzzle[r3][c3];
                            g_sdk_given[r3][c3]=(g_sdk_puzzle[r3][c3]!=0)?1:0;
                        }
                        g_sdk_started=1; g_sdk_errors=0;
                    }
                    g_sdk_sel_r=(my-by0_sdk)/cell_sdk;
                    g_sdk_sel_c=(mx-bx0_sdk)/cell_sdk;
                    if(g_sdk_sel_r>8)g_sdk_sel_r=8;
                    if(g_sdk_sel_c>8)g_sdk_sel_c=8;
                    dirty=1; goto end_left_press;
                }
                /* Number pad click */
                int np_y_sdk = by0_sdk + bsz_sdk + 4;
                if (my>=np_y_sdk && my<np_y_sdk+14 && mx>=bx0_sdk && mx<bx0_sdk+bsz_sdk) {
                    int num_sdk = (mx-bx0_sdk)/cell_sdk + 1;
                    if (num_sdk>=1&&num_sdk<=9 && g_sdk_sel_r>=0&&g_sdk_sel_c>=0 &&
                        !g_sdk_given[g_sdk_sel_r][g_sdk_sel_c]) {
                        g_sdk_board[g_sdk_sel_r][g_sdk_sel_c]=num_sdk;
                        /* Check against puzzle solution */
                        if (g_sdk_puzzle[g_sdk_sel_r][g_sdk_sel_c]!=0 &&
                            g_sdk_puzzle[g_sdk_sel_r][g_sdk_sel_c]!=num_sdk) g_sdk_errors++;
                        dirty=1;
                    }
                    goto end_left_press;
                }
                break;
              }
            }
            /* Compressor toolbar actions */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Compressor")) continue;
                if (i != top_win_idx) continue;
                { int cy_cp = w->y + TITLEBAR_H;
                  if (mx>=w->x+6 && mx<w->x+66 && my>=cy_cp+4 && my<cy_cp+18) {
                      g_compressor_added_files++;
                      toast_show("Compressor","File added",RGB(0,122,255));
                      dirty=1; goto end_left_press;
                  }
                  if (mx>=w->x+80 && mx<w->x+160 && my>=cy_cp+4 && my<cy_cp+20) {
                      g_compressor_submitted++;
                      toast_show("Compressor","Batch submitted",RGB(52,199,89));
                      dirty=1; goto end_left_press;
                  } }
                break;
            }
            /* Screen Recording start/stop */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Screen Recording")) continue;
                if (i != top_win_idx) continue;
                { int cy_sr = w->y + TITLEBAR_H;
                  int by_sr = cy_sr + w->h - TITLEBAR_H - 20;
                  if ((mx-w->x-w->w/2)*(mx-w->x-w->w/2)+(my-(cy_sr+50))*(my-(cy_sr+50)) <= 32*32 ||
                      (mx>=w->x+w->w/2-40 && mx<w->x+w->w/2+40 && my>=by_sr && my<by_sr+15)) {
                      g_screen_recording_active ^= 1;
                      if (!g_screen_recording_active) g_screen_recording_count++;
                      g_screen_shared = g_screen_recording_active;
                      toast_show("Screen Recording",g_screen_recording_active?"Recording started":"Recording saved",RGB(255,59,48));
                      dirty=1; goto end_left_press;
                  } }
                break;
            }
            /* AR Quick Look and Final Cut share buttons */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title) continue;
                if (i != top_win_idx) continue;
                if (str_eq(w->title,"AR Quick Look")) {
                    int cy_ar = w->y + TITLEBAR_H;
                    int vh_ar = w->h - TITLEBAR_H - 30;
                    if (mx>=w->x+w->w-70 && mx<w->x+w->w-10 && my>=cy_ar+vh_ar+4 && my<cy_ar+vh_ar+22) {
                        g_share_visible = 1;
                        gui_record_share_action("Sharing AR model",RGB(100,200,255));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Final Cut Pro")) {
                    int cy_fc = w->y + TITLEBAR_H;
                    int ci_fc;
                    for (ci_fc=0; ci_fc<4; ci_fc++) {
                        int cx_fc=w->x+6+ci_fc*24;
                        if (mx>=cx_fc && mx<cx_fc+20 && my>=cy_fc+5 && my<cy_fc+21) {
                            g_finalcut_transport=ci_fc;
                            toast_show("Final Cut Pro","Transport selected",RGB(220,40,40));
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int browser_w=w->w*28/100;
                      int panel_y=cy_fc+27;
                      int li_fc;
                      for (li_fc=0; li_fc<4; li_fc++) {
                          int ly_fc=panel_y+18+li_fc*20;
                          if (mx>=w->x+2 && mx<w->x+browser_w && my>=ly_fc && my<ly_fc+18) {
                              g_finalcut_library=li_fc;
                              toast_show("Final Cut Pro","Library selected",RGB(220,40,40));
                              dirty=1; goto end_left_press;
                          }
                      } }
                    if (mx>=w->x+w->w-78 && mx<w->x+w->w-8 && my>=cy_fc+5 && my<cy_fc+25) {
                        g_share_visible = 1;
                        gui_record_share_action("Sharing timeline",RGB(120,120,130));
                        dirty=1; goto end_left_press;
                    }
                }
                break;
            }
            /* Transporter upload area */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Transporter")) continue;
                if (i != top_win_idx) continue;
                {
                    int cy_tp = w->y + TITLEBAR_H;
                    if (mx>=w->x+10 && mx<w->x+w->w-10 &&
                        my>=cy_tp+28 && my<cy_tp+88) {
                        g_transporter_uploading = 1;
                        g_transporter_upload_start_tick = timer_ticks();
                        if (g_transporter_upload_count < 999) g_transporter_upload_count++;
                        toast_show("Transporter","Upload started",RGB(40,120,200));
                        dirty=1; goto end_left_press;
                    }
                }
                break;
            }
            /* Sidecar connect/disconnect */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Sidecar")) continue;
                if (i != top_win_idx) continue;
                {
                    int cy_sc = w->y + TITLEBAR_H;
                    int by_sc = cy_sc + w->h - TITLEBAR_H - 28;
                    if (mx>=w->x+w->w/2-50 && mx<w->x+w->w/2+50 &&
                        my>=by_sc && my<by_sc+18) {
                        g_sidecar_connected ^= 1;
                        toast_show("Sidecar", g_sidecar_connected ? "Connected" : "Disconnected", RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                }
                break;
            }
            /* Game Center tabs */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"GameCenter")) continue;
                if (i != top_win_idx) continue;
                { static const char *tabs_gc[] = {"Games","Achievements","Friends","Challenges"};
                  int cy_gc = w->y + TITLEBAR_H;
                  int tab_x_gc = w->x + 4;
                  int ti_gc;
                  if (my>=cy_gc+46 && my<cy_gc+64) {
                      for (ti_gc=0; ti_gc<4; ti_gc++) {
                          int tw_gc = str_len(tabs_gc[ti_gc])*8+10;
                          if (mx>=tab_x_gc && mx<tab_x_gc+tw_gc) {
                              g_gamecenter_tab = ti_gc;
                              toast_show("GameCenter", tabs_gc[ti_gc], RGB(52,199,89));
                              dirty=1; goto end_left_press;
                          }
                          tab_x_gc += tw_gc + 4;
                      }
                  }
                }
                break;
            }
            /* Network Utility tabs and ping action */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Network Utility")) continue;
                if (i != top_win_idx) continue;
                { static const char *tabs_nu[]={"Info","Ping","Route","Resolver","Port Scan"};
                  int cy_nu = w->y + TITLEBAR_H;
                  int tx_nu = w->x + 4;
                  int ti_nu;
                  if (my>=cy_nu+4 && my<cy_nu+20) {
                      for (ti_nu=0; ti_nu<5; ti_nu++) {
                          int tw_nu = str_len(tabs_nu[ti_nu])*8+10;
                          if (mx>=tx_nu && mx<tx_nu+tw_nu) {
                              g_netutil_tab = ti_nu;
                              toast_show("Network Utility", tabs_nu[ti_nu], RGB(0,122,255));
                              dirty=1; goto end_left_press;
                          }
                          tx_nu += tw_nu + 4;
                      }
                  }
                  if (g_netutil_tab == 1 &&
                      mx>=w->x+w->w-76 && mx<w->x+w->w-8 &&
                      my>=cy_nu+28 && my<cy_nu+44) {
                      uint32_t target_nu = gui_netutil_default_target();
                      uint32_t ifindex_nu = 0;
                      uint32_t start_nu;
                      int ping_ok_nu = -1;
                      if (target_nu &&
                          net_route_lookup4(target_nu, &ifindex_nu, 0) == 0) {
                          start_nu = timer_ticks();
                          ping_ok_nu = net_ping4(ifindex_nu, target_nu);
                          g_netutil_ping_last_ms = timer_ticks() - start_nu;
                          if (ping_ok_nu == 0 && g_netutil_ping_last_ms == 0)
                              g_netutil_ping_last_ms = 1;
                      } else {
                          g_netutil_ping_last_ms = 0;
                      }
                      if (g_netutil_ping_count < 999) g_netutil_ping_count++;
                      g_netutil_ping_target = target_nu;
                      g_netutil_ping_ok = (ping_ok_nu == 0) ? 1 : -1;
                      g_netutil_ping_last_tick = timer_ticks();
                      toast_show("Network Utility",
                                 ping_ok_nu == 0 ? "Ping reply received" : "Ping failed",
                                 ping_ok_nu == 0 ? RGB(52,199,89) : RGB(255,149,0));
                      dirty=1; goto end_left_press;
                  }
                  if (g_netutil_tab == 4 &&
                      mx>=w->x+w->w-76 && mx<w->x+w->w-8 &&
                      my>=cy_nu+28 && my<cy_nu+44) {
                      static const uint16_t scan_ports_nu[4] = {22, 53, 80, 443};
                      uint32_t target_nu = gui_netutil_default_target();
                      int spi_nu;
                      g_netutil_port_scan_target = target_nu;
                      for (spi_nu = 0; spi_nu < 4; spi_nu++)
                          g_netutil_port_status[spi_nu] =
                              gui_netutil_scan_tcp_port(target_nu, scan_ports_nu[spi_nu]);
                      if (g_netutil_port_scan_count < 999) g_netutil_port_scan_count++;
                      toast_show("Network Utility",
                                 target_nu ? "Port scan complete" : "No scan target",
                                 target_nu ? RGB(52,199,89) : RGB(255,149,0));
                      dirty=1; goto end_left_press;
                  }
                }
                break;
            }
            /* Universal Control connect/disconnect */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Universal Control")) continue;
                if (i != top_win_idx) continue;
                { int cy_uc = w->y + TITLEBAR_H;
                  int btn_w_uc = 110;
                  int btn_x_uc = w->x + (w->w - btn_w_uc) / 2;
                  int btn_y_uc = cy_uc + w->h - TITLEBAR_H - 36;
                  if ((mx>=w->x+10 && mx<w->x+w->w-10 && my>=cy_uc+150 && my<cy_uc+170) ||
                      (mx>=btn_x_uc && mx<btn_x_uc+btn_w_uc && my>=btn_y_uc && my<btn_y_uc+22)) {
                      g_universal_connected ^= 1;
                      toast_show("Universal Control", g_universal_connected ? "Connected to iPad" : "Disconnected", RGB(0,122,255));
                      dirty=1; goto end_left_press;
                  }
                }
                break;
            }
            /* Translate favorite and Math Notes new note */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title) continue;
                if (i != top_win_idx) continue;
                if (str_eq(w->title,"Translate")) {
                    int cy_tr = w->y + TITLEBAR_H;
                    int fav_y = cy_tr + w->h - TITLEBAR_H - 26;
                    if (mx>=w->x+8 && mx<w->x+170 && my>=fav_y && my<fav_y+18) {
                        g_translate_favorites++;
                        toast_show("Translate","Added to Favorites",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Math Notes")) {
                    int cy_mn = w->y + TITLEBAR_H;
                    if (mx>=w->x+w->w-78 && mx<w->x+w->w-6 && my>=cy_mn+2 && my<cy_mn+20) {
                        g_math_notes_created++;
                        toast_show("Math Notes","New note created",RGB(255,149,0));
                        dirty=1; goto end_left_press;
                    }
                }
                break;
            }
            /* Migration Assistant, Passwords, and Books actions */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title) continue;
                if (i != top_win_idx) continue;
                if (str_eq(w->title,"Migration Assistant")) {
                    int step_y_ma = w->y + TITLEBAR_H + 64;
                    int src_y_ma = step_y_ma + 30;
                    int so_ma;
                    for (so_ma=0; so_ma<3; so_ma++) {
                        int ox_ma = w->x + 6 + so_ma * (w->w - 18) / 3;
                        int ow_ma = (w->w - 18) / 3 - 2;
                        if (mx>=ox_ma && mx<ox_ma+ow_ma && my>=src_y_ma && my<src_y_ma+28) {
                            g_migration_source = so_ma;
                            toast_show("Migration Assistant","Source selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int btn_y_ma = w->y + w->h - 40;
                      if (mx>=w->x+w->w-88 && mx<w->x+w->w-8 && my>=btn_y_ma && my<btn_y_ma+22) {
                          if (g_migration_step < 3) g_migration_step++;
                          toast_show("Migration Assistant",g_migration_step>=3?"Ready to migrate":"Next step",RGB(0,122,255));
                          dirty=1; goto end_left_press;
                      }
                      if (mx>=w->x+8 && mx<w->x+58 && my>=btn_y_ma && my<btn_y_ma+22) {
                          if (g_migration_step > 0) g_migration_step--;
                          toast_show("Migration Assistant","Back",RGB(0,122,255));
                          dirty=1; goto end_left_press;
                      }
                    }
                } else if (str_eq(w->title,"Passwords")) {
                    int sb_w_pw = 100;
                    int sch_y_pw = w->y + TITLEBAR_H + 28;
                    int sit_y_pw = sch_y_pw + 22;
                    if (mx>=w->x+4 && mx<w->x+sb_w_pw-2 && my>=sch_y_pw && my<sch_y_pw+16) {
                        g_passwords_search_focused=1; gui_search_focus(GUI_SEARCH_PASSWORDS); dirty=1; goto end_left_press;
                    } else if (g_passwords_search_focused) {
                        g_passwords_search_focused=0; gui_search_blur(GUI_SEARCH_PASSWORDS); dirty=1;
                    }
                    int cat_pw;
                    for (cat_pw=0; cat_pw<4; cat_pw++) {
                        int cy_pw = sit_y_pw + cat_pw * 20;
                        if (mx>=w->x+2 && mx<w->x+sb_w_pw && my>=cy_pw && my<cy_pw+18) {
                            g_passwords_sel = cat_pw;
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int hdr_y_pw = w->y + TITLEBAR_H + 2;
                      int entry_y_pw = hdr_y_pw + 28;
                      int pe_pw;
                      for (pe_pw=0; pe_pw<8; pe_pw++) {
                          int ey_pw = entry_y_pw + pe_pw * 22;
                          if (mx>=w->x+sb_w_pw+2 && mx<w->x+w->w-2 && my>=ey_pw && my<ey_pw+20) {
                              g_passwords_entry = pe_pw;
                              dirty=1; goto end_left_press;
                          }
                      }
                    }
                    { int bot_y_pw = w->y + w->h - 24;
                      if (mx>=w->x+w->w-44 && mx<w->x+w->w-8 && my>=bot_y_pw+4 && my<bot_y_pw+20) {
                          g_passwords_added++;
                          toast_show("Passwords","Password added",RGB(0,122,255));
                          dirty=1; goto end_left_press;
                      }
                    }
                } else if (str_eq(w->title,"Books")) {
                    int tab_y_bk = w->y + TITLEBAR_H + 26;
                    int tab_w_bk = (w->w - 8) / 3;
                    int cont_y_bk = tab_y_bk + 24;
                    int tab_bk;
                    if (tab_w_bk < 1) tab_w_bk = 1;
                    if (my>=tab_y_bk && my<tab_y_bk+20 && mx>=w->x+4 && mx<w->x+w->w-4) {
                        tab_bk = (mx - (w->x + 4)) / tab_w_bk;
                        if (tab_bk < 0) tab_bk = 0;
                        if (tab_bk > 2) tab_bk = 2;
                        g_books_tab = tab_bk;
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+8 && mx<w->x+w->w-8 && my>=cont_y_bk+12 && my<cont_y_bk+78) {
                        g_books_reading ^= 1;
                        toast_show("Books",g_books_reading?"Reading":"Paused",RGB(255,149,0));
                        dirty=1; goto end_left_press;
                    }
                    { int bi_bk;
                      for (bi_bk=0; bi_bk<6; bi_bk++) {
                          int bx_bk = w->x + 8 + (bi_bk % 3) * ((w->w - 16) / 3);
                          int by_bk = cont_y_bk + 90 + (bi_bk / 3) * 70;
                          int bw_bk = (w->w - 16) / 3 - 4;
                          if (mx>=bx_bk && mx<bx_bk+bw_bk && my>=by_bk && my<by_bk+56) {
                              g_books_selected = bi_bk;
                              g_books_tab = 1;
                              dirty=1; goto end_left_press;
                          }
                      }
                    }
                }
                break;
            }
            /* App Store tab/search/download actions */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"App Store")) continue;
                if (i != top_win_idx) continue;
                { int cy_as = w->y + TITLEBAR_H + 1;
                  int tab_y_as = cy_as;
                  int tab_w_as = (w->w - 2) / 4;
                  int sb_y_as = tab_y_as + 32;
                  static const char *tab_names_as[] = {"Today","Games","Apps","Arcade"};
                  if (tab_w_as < 1) tab_w_as = 1;
                  if (my >= tab_y_as && my < tab_y_as + 26 &&
                      mx >= w->x + 1 && mx < w->x + w->w - 1) {
                      int tab_as = (mx - (w->x + 1)) / tab_w_as;
                      if (tab_as < 0) tab_as = 0;
                      if (tab_as > 3) tab_as = 3;
                      g_appstore_tab = tab_as;
                      g_appstore_search_focused = 0; gui_search_blur(GUI_SEARCH_APPSTORE);
                      toast_show("App Store",tab_names_as[tab_as],RGB(0,122,255));
                      dirty=1; goto end_left_press;
                  }
                  if (mx >= w->x + 8 && mx < w->x + w->w - 8 &&
                      my >= sb_y_as && my < sb_y_as + 20) {
                      g_appstore_search_focused = 1; gui_search_focus(GUI_SEARCH_APPSTORE);
                      toast_show("App Store","Search active",RGB(0,122,255));
                      dirty=1; goto end_left_press;
                  }
                  if (g_appstore_search_focused) {
                      g_appstore_search_focused = 0; gui_search_blur(GUI_SEARCH_APPSTORE);
                      dirty = 1;
                  }
                  { static const char *feat_names_as[] = {
                        "Darkroom","Bear","Fantastical","1Password","Things 3","Pixelmator"
                    };
                    static const char *feat_subs_as[] = {
                        "Photo & Video Editor", "Writing App for Notes", "Calendar & Tasks",
                        "Password Manager", "To-Do & Task Manager", "Photo Editor"
                    };
                    int card_y_as = tab_y_as + 160;
                    int feat_cols_as = (w->w > 320) ? 3 : 2;
                    int faw_as = (w->w - 8) / feat_cols_as - 4;
                    int fah_as = 62;
                    int fi_as;
                    int shown_as = 0;
                    for (fi_as=0; fi_as<6; fi_as++) {
                        int fc_as, fr_as, fax_as, fay_as;
                        if (!gui_search_matches(GUI_SEARCH_APPSTORE, feat_names_as[fi_as], feat_subs_as[fi_as]))
                            continue;
                        fc_as = shown_as % feat_cols_as;
                        fr_as = shown_as / feat_cols_as;
                        fax_as = w->x + 4 + fc_as * (faw_as + 4);
                        fay_as = card_y_as + fr_as * (fah_as + 4);
                        shown_as++;
                        int bw_as = 46;
                        int bx_as = fax_as + faw_as - bw_as - 5;
                        int by_as = fay_as + fah_as - 21;
                        if (fay_as + fah_as > w->y + w->h - 19 - 6) break;
                        if (mx >= bx_as && mx < bx_as + bw_as &&
                            my >= by_as && my < by_as + 15) {
                            int already_as = (g_appstore_downloads >> fi_as) & 1;
                            g_appstore_downloads |= (1 << fi_as);
                            toast_show("App Store",already_as ? "Opening app" : feat_names_as[fi_as],RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                  } }
                break;
            }
            /* Apple TV navigation and playback actions */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Apple TV")) continue;
                if (i != top_win_idx) continue;
                { int cy_tv = w->y + TITLEBAR_H;
                  int nav_w_tv = (w->w - 16) / 5;
                  static const char *nav_names_tv[] = {"Watch Now","Movies","TV Shows","Sports","Kids"};
                  if (nav_w_tv < 1) nav_w_tv = 1;
                  if (my >= cy_tv && my < cy_tv + 20 &&
                      mx >= w->x + 8 && mx < w->x + w->w - 8) {
                      int nav_tv = (mx - (w->x + 8)) / nav_w_tv;
                      if (nav_tv < 0) nav_tv = 0;
                      if (nav_tv > 4) nav_tv = 4;
                      g_atv_sel = nav_tv;
                      toast_show("Apple TV",nav_names_tv[nav_tv],RGB(255,255,255));
                      dirty=1; goto end_left_press;
                  }
                  { int hx_tv = w->x + 4;
                    int hy_tv = cy_tv + 26;
                    if (mx >= hx_tv + 8 && mx < hx_tv + 60 &&
                        my >= hy_tv + 38 && my < hy_tv + 54) {
                        g_atv_playing ^= 1;
                        toast_show("Apple TV",g_atv_playing ? "Playing" : "Paused",RGB(255,255,255));
                        dirty=1; goto end_left_press;
                    }
                  }
                  { int tab_w_tv = (w->w - 8) / 4;
                    int tab_y_tv = w->y + w->h - 20;
                    if (tab_w_tv < 1) tab_w_tv = 1;
                    if (my >= tab_y_tv && my < tab_y_tv + 18 &&
                        mx >= w->x + 4 && mx < w->x + w->w - 4) {
                        int btab_tv = (mx - (w->x + 4)) / tab_w_tv;
                        if (btab_tv < 0) btab_tv = 0;
                        if (btab_tv > 3) btab_tv = 3;
                        g_atv_bottom_tab = btab_tv;
                        dirty=1; goto end_left_press;
                    }
                  } }
                break;
            }
            /* Additional application CTA actions */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title) continue;
                if (i != top_win_idx) continue;
                if (str_eq(w->title,"Focus")) {
                    int cy_fc=w->y+TITLEBAR_H;
                    int tog_y=cy_fc+56;
                    int modes_y=tog_y+42;
                    int fm_ev;
                    for (fm_ev=0; fm_ev<5; fm_ev++) {
                        int fy_ev=modes_y+fm_ev*36;
                        int mode_id_ev=(fm_ev==0)?2:(fm_ev==1?1:(fm_ev==2?3:(fm_ev==3?4:5)));
                        if (mx>=w->x+8 && mx<w->x+w->w-8 && my>=fy_ev && my<fy_ev+32) {
                            g_focus_mode=mode_id_ev;
                            g_pref_dnd=1;
                            toast_show("Focus",focus_mode_name(g_focus_mode),RGB(100,60,200));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Motion")) {
                    int cy_mo = w->y + TITLEBAR_H;
                    { static const char *tools_mo[]={"Select","Add","Behaviors","Filters","Generators","3D"};
                      int tx_mo=w->x+4, ti_mo;
                      if (my>=cy_mo+4 && my<cy_mo+22) {
                          for (ti_mo=0; ti_mo<6; ti_mo++) {
                              int tw_mo=str_len(tools_mo[ti_mo])*8+8;
                              if (mx>=tx_mo && mx<tx_mo+tw_mo) {
                                  g_motion_tool=ti_mo;
                                  toast_show("Motion","Tool selected",RGB(30,200,220));
                                  dirty=1; goto end_left_press;
                              }
                              tx_mo += str_len(tools_mo[ti_mo])*8+12;
                          }
                      }
                    }
                    if (mx>=w->x+2 && mx<w->x+80 && my>=cy_mo+40 && my<cy_mo+40+6*18) {
                        int layer_i=(my-(cy_mo+40))/18;
                        if (layer_i>=0 && layer_i<6) {
                            g_motion_layer=layer_i;
                            toast_show("Motion","Layer selected",RGB(30,200,220));
                            dirty=1; goto end_left_press;
                        }
                    }
                    if (mx>=w->x+w->w-78 && mx<w->x+w->w-4 && my>=cy_mo+4 && my<cy_mo+22) {
                        g_motion_playing ^= 1;
                        toast_show("Motion",g_motion_playing?"Playback started":"Playback paused",RGB(30,200,220));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Feedback Assistant")) {
                    int by_fa = w->y + w->h - 24;
                    if (mx>=w->x+w->w-96 && mx<w->x+w->w-10 && my>=by_fa && my<by_fa+16) {
                        g_feedback_submissions++;
                        toast_show("Feedback Assistant","Report submitted",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"MainStage")) {
                    int cy_ms=w->y+TITLEBAR_H;
                    int stage_y_ms=cy_ms+25;
                    int stage_h_ms=w->h-TITLEBAR_H-25;
                    int kb_h_ms=40;
                    int pad_y_ms=stage_y_ms+4;
                    int pad_h_ms=stage_h_ms-kb_h_ms-8;
                    int zi_ms;
                    if (mx>=w->x+w->w-70 && mx<w->x+w->w-6 && my>=cy_ms+4 && my<cy_ms+20) {
                        g_mainstage_performing ^= 1;
                        toast_show("MainStage",g_mainstage_performing?"Performing":"Stopped",RGB(200,30,30));
                        dirty=1; goto end_left_press;
                    }
                    for (zi_ms=0; zi_ms<3; zi_ms++) {
                        int zw_ms=(w->w-4)/3;
                        int zx_ms=w->x+2+zi_ms*zw_ms;
                        if (mx>=zx_ms && mx<zx_ms+zw_ms-2 && my>=pad_y_ms && my<pad_y_ms+pad_h_ms) {
                            g_mainstage_zone=zi_ms;
                            toast_show("MainStage","Zone selected",RGB(52,199,89));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Instruments")) {
                    int cy_in=w->y+TITLEBAR_H;
                    int ti_in;
                    if ((mx-(w->x+14))*(mx-(w->x+14)) + (my-(cy_in+12))*(my-(cy_in+12)) <= 10*10) {
                        g_instruments_recording ^= 1;
                        toast_show("Instruments",g_instruments_recording?"Recording":"Paused",RGB(220,50,50));
                        dirty=1; goto end_left_press;
                    }
                    for (ti_in=0; ti_in<5; ti_in++) {
                        int ty_in=cy_in+30+ti_in*22;
                        if (mx>=w->x+1 && mx<w->x+91 && my>=ty_in-2 && my<ty_in+20) {
                            g_instruments_track=ti_in;
                            toast_show("Instruments","Track selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"iPhone Mirroring")) {
                    int cy_ip=w->y+TITLEBAR_H;
                    int content_h_ip=w->h-TITLEBAR_H;
                    int ph_w_ip=120;
                    int ph_h_ip=content_h_ip-20;
                    int btn_x_ip=w->x+(w->w-92)/2;
                    int btn_y_ip=cy_ip+content_h_ip-24;
                    if (ph_h_ip>220) ph_h_ip=220;
                    { int ph_x_ip=w->x+(w->w-ph_w_ip)/2;
                      int ph_y_ip=cy_ip+(content_h_ip-ph_h_ip)/2;
                      if (mx>=btn_x_ip && mx<btn_x_ip+92 && my>=btn_y_ip && my<btn_y_ip+18) {
                          g_iphone_mirroring_connected ^= 1;
                          toast_show("iPhone Mirroring",g_iphone_mirroring_connected?"Connected":"Disconnected",RGB(0,122,255));
                          dirty=1; goto end_left_press;
                      }
                      if (mx>=ph_x_ip && mx<ph_x_ip+ph_w_ip && my>=ph_y_ip && my<ph_y_ip+ph_h_ip) {
                          if (!g_iphone_mirroring_connected) g_iphone_mirroring_connected=1;
                          else g_iphone_mirroring_taps++;
                          toast_show("iPhone Mirroring","Phone tapped",RGB(0,122,255));
                          dirty=1; goto end_left_press;
                      } }
                } else if (str_eq(w->title,"Logic Pro")) {
                    int cy_lp=w->y+TITLEBAR_H;
                    int lp_lw_ev=80;
                    int track_h_lp=(w->h-TITLEBAR_H-29)/6;
                    int ti_lp;
                    for (ti_lp=0; ti_lp<5; ti_lp++) {
                        int tx_lp=w->x+4+ti_lp*22;
                        if (mx>=tx_lp && mx<tx_lp+18 && my>=cy_lp+6 && my<cy_lp+22) {
                            g_logic_transport=ti_lp;
                            toast_show("Logic Pro","Transport selected",RGB(48,209,88));
                            dirty=1; goto end_left_press;
                        }
                    }
                    if (track_h_lp < 1) track_h_lp = 1;
                    for (ti_lp=0; ti_lp<6; ti_lp++) {
                        int ty_lp=cy_lp+29+ti_lp*track_h_lp;
                        if (mx>=w->x+1 && mx<w->x+lp_lw_ev && my>=ty_lp && my<ty_lp+track_h_lp) {
                            g_logic_track=ti_lp;
                            toast_show("Logic Pro","Track selected",RGB(0,160,210));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"CleanMyMac X")) {
                    int bx_cm = w->x + (w->w - 90) / 2;
                    int by_cm = w->y + TITLEBAR_H + 112;
                    if (mx>=bx_cm && mx<bx_cm+90 && my>=by_cm && my<by_cm+22) {
                        g_cleanmymac_scan_count++;
                        toast_show("CleanMyMac X","Scan complete",RGB(0,200,120));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Pages")) {
                    int cy_pg = w->y + TITLEBAR_H;
                    int bi_pg;
                    for (bi_pg=0; bi_pg<3; bi_pg++) {
                        int fbx=w->x+6+bi_pg*22;
                        if (mx>=fbx && mx<fbx+18 && my>=cy_pg+4 && my<cy_pg+20) {
                            if (bi_pg==0) g_pages_bold ^= 1;
                            else if (bi_pg==1) g_pages_italic ^= 1;
                            else g_pages_underline ^= 1;
                            toast_show("Pages","Style toggled",RGB(255,149,0));
                            dirty=1; goto end_left_press;
                        }
                    }
                    if (mx>=w->x+106 && mx<w->x+120 && my>=cy_pg+5 && my<cy_pg+19) {
                        g_pages_align=0; toast_show("Pages","Left aligned",RGB(255,149,0)); dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+122 && mx<w->x+136 && my>=cy_pg+5 && my<cy_pg+19) {
                        g_pages_align=1; toast_show("Pages","Centered",RGB(255,149,0)); dirty=1; goto end_left_press;
                    }
                    { int ins_x=w->x+w->w-100;
                      if (mx>=ins_x && mx<ins_x+44 && my>=cy_pg+4 && my<cy_pg+20) {
                          if (g_pages_insert_count < 99) g_pages_insert_count++;
                          toast_show("Pages","Object inserted",RGB(255,149,0));
                          dirty=1; goto end_left_press;
                      }
                      if (mx>=ins_x+46 && mx<ins_x+90 && my>=cy_pg+4 && my<cy_pg+20) {
                          g_pages_inspector ^= 1;
                          toast_show("Pages",g_pages_inspector?"Format inspector":"Inspector hidden",RGB(255,149,0));
                          dirty=1; goto end_left_press;
                      } }
                } else if (str_eq(w->title,"Keynote")) {
                    int cy_kn = w->y + TITLEBAR_H;
                    int bi_kn;
                    for (bi_kn=0; bi_kn<5; bi_kn++) {
                        int bx_kn = w->x + 6 + bi_kn * 54;
                        if (mx>=bx_kn && mx<bx_kn+52 && my>=cy_kn+4 && my<cy_kn+22) {
                            g_keynote_mode = bi_kn;
                            if (bi_kn == 0) {
                                g_keynote_editing = 0;
                                toast_show("Keynote","Slideshow started",RGB(52,199,89));
                            } else if (bi_kn == 2) {
                                if (g_keynote_slide_count < 99) g_keynote_slide_count++;
                                g_keynote_slide = g_keynote_slide_count - 1;
                                g_keynote_editing = 1;
                                toast_show("Keynote","Slide added",RGB(255,149,0));
                            } else {
                                toast_show("Keynote",bi_kn==1?"Slides view":(bi_kn==3?"Format inspector":"Insert menu"),RGB(255,149,0));
                            }
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int kn_sl_w=70;
                      int kn_content_y=w->y+TITLEBAR_H+28;
                      int shown_kn_ev=g_keynote_slide_count;
                      int si_kn_ev;
                      if (shown_kn_ev < 1) shown_kn_ev = 1;
                      if (shown_kn_ev > 5) shown_kn_ev = 5;
                      for (si_kn_ev=0; si_kn_ev<shown_kn_ev; si_kn_ev++) {
                          int sy_kn=kn_content_y+12+si_kn_ev*14;
                          if (mx>=w->x+4 && mx<w->x+kn_sl_w && my>=sy_kn-2 && my<sy_kn+12) {
                              g_keynote_slide=si_kn_ev;
                              g_keynote_editing=1;
                              toast_show("Keynote","Slide selected",RGB(255,149,0));
                              dirty=1; goto end_left_press;
                          }
                      }
                      int kn_content_h=w->h-TITLEBAR_H-2-28;
                      int cv_x=w->x+kn_sl_w+1;
                      int cv_w=w->w-kn_sl_w-2;
                      int cv_h=kn_content_h;
                      int sl_pad=12;
                      int sl_x=cv_x+sl_pad;
                      int sl_y=kn_content_y+sl_pad;
                      int sl_w=cv_w-sl_pad*2;
                      int sl_h=cv_h-sl_pad*2;
                      if (mx>=sl_x && mx<sl_x+sl_w && my>=sl_y && my<sl_y+sl_h) {
                          g_keynote_editing = 1;
                          if (g_keynote_mode == 0) g_keynote_mode = 3;
                          toast_show("Keynote","Editing slide",RGB(255,149,0));
                          dirty=1; goto end_left_press;
                      } }
                } else if (str_eq(w->title,"iMovie")) {
                    int cy_im = w->y + TITLEBAR_H;
                    int bi_im;
                    for (bi_im=0; bi_im<3; bi_im++) {
                        int bx_im = w->x + 6 + bi_im * 70;
                        if (mx>=bx_im && mx<bx_im+66 && my>=cy_im+3 && my<cy_im+21) {
                            g_imovie_tab = bi_im;
                            if (bi_im == 0) {
                                if (g_imovie_import_count < 99) g_imovie_import_count++;
                                toast_show("iMovie","Media imported",RGB(40,140,220));
                            } else {
                                toast_show("iMovie",bi_im==1?"Projects selected":"Theatre selected",RGB(40,140,220));
                            }
                            dirty=1; goto end_left_press;
                        }
                    }
                    if (mx>=w->x+w->w-72 && mx<w->x+w->w-8 && my>=cy_im+4 && my<cy_im+24) {
                        g_imovie_share_count++;
                        g_share_visible = 1;
                        gui_record_share_action("Sharing movie",RGB(40,140,220));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Xcode")) {
                    int cy_xc = w->y + TITLEBAR_H;
                    if (mx>=w->x+6 && mx<w->x+34 && my>=cy_xc+4 && my<cy_xc+22) {
                        g_xcode_running = 1;
                        g_xcode_run_count++;
                        toast_show("Xcode","Run started",RGB(52,199,89));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+36 && mx<w->x+70 && my>=cy_xc+4 && my<cy_xc+22) {
                        if (g_xcode_running) {
                            g_xcode_running = 0;
                            toast_show("Xcode","Run stopped",RGB(255,59,48));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Podcasts")) {
                    int ep_y=w->y+TITLEBAR_H+32;
                    int ctl_y=ep_y+64;
                    int cx_pc=w->x+w->w/2;
                    if (mx>=cx_pc-60 && mx<cx_pc-28 && my>=ctl_y-2 && my<ctl_y+18) {
                        if (g_podcasts_episode < 999) g_podcasts_episode++;
                        toast_show("Podcasts","Previous episode",RGB(147,44,246));
                        dirty=1; goto end_left_press;
                    }
                    if ((mx-cx_pc)*(mx-cx_pc)+(my-(ctl_y+8))*(my-(ctl_y+8)) <= 14*14) {
                        g_podcasts_playing ^= 1;
                        toast_show("Podcasts",g_podcasts_playing?"Playing":"Paused",RGB(147,44,246));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=cx_pc+28 && mx<cx_pc+64 && my>=ctl_y-2 && my<ctl_y+18) {
                        if (g_podcasts_episode > 1) g_podcasts_episode--;
                        toast_show("Podcasts","Next episode",RGB(147,44,246));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Freeform")) {
                    int tool_w=(w->w-12)/5;
                    int ti_ff;
                    for (ti_ff=0; ti_ff<5; ti_ff++) {
                        int tx=w->x+6+ti_ff*tool_w;
                        if (mx>=tx && mx<tx+tool_w-2 && my>=w->y+TITLEBAR_H+30 && my<w->y+TITLEBAR_H+46) {
                            g_freeform_tool=ti_ff;
                            toast_show("Freeform","Tool selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                    if (mx>=w->x+4 && mx<w->x+w->w-4 && my>=w->y+TITLEBAR_H+50 && my<w->y+w->h-22) {
                        if (g_freeform_added_items < 99) g_freeform_added_items++;
                        toast_show("Freeform","Content added",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Shortcuts")) {
                    int si_sc;
                    for (si_sc=0; si_sc<3; si_sc++) {
                        int sx=w->x+4+si_sc*(w->w-8)/3;
                        if (mx>=sx && mx<sx+(w->w-8)/3-2 && my>=w->y+TITLEBAR_H+27 && my<w->y+TITLEBAR_H+41) {
                            g_shortcuts_tab=si_sc;
                            toast_show("Shortcuts",si_sc==0?"My Shortcuts":(si_sc==1?"Gallery":"Automation"),RGB(255,149,0));
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int card_w=(w->w-18)/2, card_h=50, sc_i;
                      for (sc_i=0; sc_i<6; sc_i++) {
                          int cx=w->x+6+(sc_i%2)*(card_w+4);
                          int cy=w->y+TITLEBAR_H+48+(sc_i/2)*(card_h+4);
                          if (mx>=cx && mx<cx+card_w && my>=cy && my<cy+card_h) {
                              g_shortcuts_run_count++;
                              if (sc_i == 3) g_qn_visible = 1;
                              else if (sc_i == 2) g_pref_dnd ^= 1;
                              toast_show("Shortcuts","Shortcut run",RGB(255,149,0));
                              dirty=1; goto end_left_press;
                          }
                      } }
                } else if (str_eq(w->title,"Voice Memos")) {
                    int wave_y=w->y+TITLEBAR_H+36;
                    int rec_y=wave_y+86;
                    int cx_vm=w->x+w->w/2;
                    int cy_vm=rec_y+16;
                    if ((mx-cx_vm)*(mx-cx_vm)+(my-cy_vm)*(my-cy_vm) <= 22*22) {
                        if (!g_voice_memos_recording) {
                            g_voice_memos_recording=1;
                            g_voice_memos_start_tick=timer_ticks();
                            toast_show("Voice Memos","Recording started",RGB(255,59,48));
                        } else {
                            g_voice_memos_recording=0;
                            g_voice_memos_saved++;
                            toast_show("Voice Memos","Recording saved",RGB(255,149,0));
                        }
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Numbers")) {
                    int cy_nb=w->y+TITLEBAR_H;
                    int ti_nb;
                    for (ti_nb=0; ti_nb<4; ti_nb++) {
                        int tx=w->x+8+ti_nb*50;
                        if (mx>=tx && mx<tx+42 && my>=cy_nb+4 && my<cy_nb+20) {
                            g_numbers_tool=ti_nb;
                            toast_show("Numbers","Tool selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int sum_x=w->x+w->w-50;
                      if (mx>=sum_x && mx<sum_x+42 && my>=cy_nb+4 && my<cy_nb+20) {
                          g_numbers_sel_row=4; g_numbers_sel_col=4; g_numbers_tool=0;
                          toast_show("Numbers","Sum selected",RGB(52,199,89));
                          dirty=1; goto end_left_press;
                      } }
                    { int tb_h=24;
                      int fb_y=w->y+TITLEBAR_H+tb_h+2;
                      int grid_y=fb_y+20;
                      int row_hdr_w=28;
                      int col_hdr_h=18;
                      int col_w=(w->w-row_hdr_w-2)/5;
                      int row_h=18;
                      if (col_w < 1) col_w = 1;
                      if (mx>=w->x+row_hdr_w+2 && mx<w->x+row_hdr_w+2+col_w*5 &&
                          my>=grid_y+col_hdr_h && my<grid_y+col_hdr_h+row_h*5) {
                          g_numbers_sel_col=(mx-(w->x+row_hdr_w+2))/col_w;
                          g_numbers_sel_row=(my-(grid_y+col_hdr_h))/row_h;
                          if (g_numbers_sel_col<0) g_numbers_sel_col=0;
                          if (g_numbers_sel_col>4) g_numbers_sel_col=4;
                          if (g_numbers_sel_row<0) g_numbers_sel_row=0;
                          if (g_numbers_sel_row>4) g_numbers_sel_row=4;
                          toast_show("Numbers","Cell selected",RGB(0,122,255));
                          dirty=1; goto end_left_press;
                      } }
                } else if (str_eq(w->title,"GarageBand")) {
                    int cy_gb=w->y+TITLEBAR_H;
                    int bi_gb;
                    for (bi_gb=0; bi_gb<5; bi_gb++) {
                        int bx=w->x+6+bi_gb*36;
                        if (mx>=bx && mx<bx+30 && my>=cy_gb+4 && my<cy_gb+24) {
                            if (bi_gb==1) {
                                g_garageband_playing ^= 1;
                                toast_show("GarageBand",g_garageband_playing?"Playing":"Paused",RGB(220,20,60));
                            } else if (bi_gb==3) {
                                if (g_garageband_recording) g_garageband_take_count++;
                                g_garageband_playing=0; g_garageband_recording=0;
                                toast_show("GarageBand","Stopped",RGB(220,20,60));
                            } else if (bi_gb==4) {
                                g_garageband_recording ^= 1;
                                g_garageband_playing = g_garageband_recording;
                                if (!g_garageband_recording) g_garageband_take_count++;
                                toast_show("GarageBand",g_garageband_recording?"Recording":"Take saved",RGB(220,20,60));
                            } else {
                                toast_show("GarageBand",bi_gb==0?"Rewind":"Forward",RGB(220,20,60));
                            }
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Automator")) {
                    int cy_at=w->y+TITLEBAR_H;
                    int lib_w_at=w->w/3;
                    if (mx>=w->x+4 && mx<w->x+lib_w_at-2 && my>=cy_at+38 && my<cy_at+52) {
                        g_automator_search_focused=1; gui_search_focus(GUI_SEARCH_AUTOMATOR); dirty=1; goto end_left_press;
                    } else if (g_automator_search_focused) {
                        g_automator_search_focused=0; gui_search_blur(GUI_SEARCH_AUTOMATOR); dirty=1;
                    }
                    static const char *labels_at[]={"Run","Stop","Record","Variables"};
                    int bx_at=w->x+6;
                    int bi_at;
                    for (bi_at=0; bi_at<4; bi_at++) {
                        int bw_at=str_len(labels_at[bi_at])*8+8;
                        if (mx>=bx_at && mx<bx_at+bw_at && my>=cy_at+4 && my<cy_at+18) {
                            if (bi_at==0) g_automator_mode=1;
                            else if (bi_at==1) g_automator_mode=0;
                            else if (bi_at==2) g_automator_recording ^= 1;
                            else g_automator_mode=3;
                            toast_show("Automator",labels_at[bi_at],RGB(238,95,0));
                            dirty=1; goto end_left_press;
                        }
                        bx_at += bw_at+6;
                    }
                    if (mx>=w->x+2 && mx<w->x+lib_w_at && my>=cy_at+56 && my<cy_at+56+6*20) {
                        int cat_i=(my-(cy_at+56))/20;
                        if (cat_i>=0 && cat_i<6) {
                            g_automator_category=cat_i;
                            g_automator_search_focused=0; gui_search_blur(GUI_SEARCH_AUTOMATOR);
                            toast_show("Automator","Category selected",RGB(238,95,0));
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int wf_x=w->x+lib_w_at+4;
                      int wf_w=w->w-lib_w_at-6;
                      int step_y=cy_at+42;
                      int si_at;
                      for (si_at=0; si_at<4; si_at++) {
                          int sy_at=step_y+si_at*42;
                          if (mx>=wf_x && mx<wf_x+wf_w-4 && my>=sy_at && my<sy_at+34) {
                              g_automator_step=si_at;
                              toast_show("Automator","Step selected",RGB(238,95,0));
                              dirty=1; goto end_left_press;
                          }
                      } }
                } else if (str_eq(w->title,"Script Editor")) {
                    int cy_se=w->y+TITLEBAR_H;
                    if (mx>=w->x+4 && mx<w->x+36 && my>=cy_se+5 && my<cy_se+19) {
                        g_script_running=1;
                        g_script_run_count++;
                        toast_show("Script Editor","Script running",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+40 && mx<w->x+72 && my>=cy_se+5 && my<cy_se+19) {
                        g_script_running=0;
                        toast_show("Script Editor","Script stopped",RGB(255,59,48));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"AirPlay")) {
                    int cy_ap=w->y+TITLEBAR_H;
                    int di_ap;
                    for (di_ap=0; di_ap<4; di_ap++) {
                        int ay_ap=cy_ap+28+di_ap*28;
                        if (mx>=w->x+8 && mx<w->x+w->w-8 && my>=ay_ap && my<ay_ap+24) {
                            g_airplay_device=di_ap;
                            toast_show("AirPlay","Device selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"TestFlight")) {
                    int cy_tf=w->y+TITLEBAR_H;
                    int ai_tf;
                    if (mx>=w->x+w->w-64 && mx<w->x+w->w-4 && my>=cy_tf+6 && my<cy_tf+24) {
                        g_testflight_invites++;
                        toast_show("TestFlight","Invite created",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                    for (ai_tf=0; ai_tf<3; ai_tf++) {
                        int ay_tf=cy_tf+36+ai_tf*40;
                        if (mx>=w->x+4 && mx<w->x+w->w-8 && my>=ay_tf && my<ay_tf+36) {
                            g_testflight_selected=ai_tf;
                            if (mx>=w->x+w->w-58 && mx<w->x+w->w-8 && my>=ay_tf+10 && my<ay_tf+26) {
                                g_testflight_updated_mask |= (1 << ai_tf);
                                toast_show("TestFlight","App updated",RGB(52,199,89));
                            } else {
                                toast_show("TestFlight","App selected",RGB(0,122,255));
                            }
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Clips")) {
                    int cy_cl=w->y+TITLEBAR_H;
                    int vh_cl=w->h-TITLEBAR_H-40;
                    int rb_cl=cy_cl+vh_cl+4;
                    int cx_cl=w->x+w->w/2;
                    if ((mx-cx_cl)*(mx-cx_cl)+(my-(rb_cl+17))*(my-(rb_cl+17)) <= 16*16) {
                        if (g_clips_recording) g_clips_saved_count++;
                        g_clips_recording ^= 1;
                        toast_show("Clips",g_clips_recording?"Recording":"Clip saved",RGB(255,59,48));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Transmit")) {
                    int cy_tr=w->y+TITLEBAR_H;
                    int pw_tr=(w->w-3)/2;
                    int mid_x_tr=w->x+pw_tr-16;
                    int mid_y_tr=cy_tr+(w->h-TITLEBAR_H)/2-10;
                    if (mx>=mid_x_tr && mx<mid_x_tr+32 && my>=mid_y_tr && my<mid_y_tr+20) {
                        if (g_transmit_transfer_count < 999) g_transmit_transfer_count++;
                        toast_show("Transmit","Transfer queued",RGB(255,149,0));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Lasso")) {
                    int cy_la=w->y+TITLEBAR_H;
                    int li_la;
                    for (li_la=0; li_la<6; li_la++) {
                        int ly_la=cy_la+30+li_la*28;
                        if (mx>=w->x+2 && mx<w->x+w->w-2 && my>=ly_la && my<ly_la+26) {
                            g_lasso_selected=li_la;
                            toast_show("Lasso","App selected",RGB(100,80,200));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Bartender 4")) {
                    int cy_ba=w->y+TITLEBAR_H;
                    int bi_ba;
                    for (bi_ba=0; bi_ba<5; bi_ba++) {
                        int by_ba=cy_ba+68+bi_ba*24;
                        if (mx>=w->x+4 && mx<w->x+w->w-4 && my>=by_ba && my<by_ba+22) {
                            g_bartender_visible_mask ^= (1 << bi_ba);
                            toast_show("Bartender 4","Visibility toggled",RGB(52,199,89));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Scrobbles")) {
                    int cy_sc=w->y+TITLEBAR_H;
                    int si_sc;
                    for (si_sc=0; si_sc<5; si_sc++) {
                        int sy_sc=cy_sc+100+si_sc*28;
                        if (mx>=w->x+2 && mx<w->x+w->w-2 && my>=sy_sc && my<sy_sc+26) {
                            g_scrobbles_selected=si_sc;
                            toast_show("Scrobbles","Track selected",RGB(220,40,40));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Grapher")) {
                    int cy_gr=w->y+TITLEBAR_H;
                    if (my>=cy_gr+2 && my<cy_gr+20) {
                        if (mx>=w->x+8 && mx<w->x+80) {
                            g_grapher_curve = (g_grapher_curve == 1) ? 0 : 1;
                            toast_show("Grapher","Sine curve toggled",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                        if (mx>=w->x+88 && mx<w->x+160) {
                            g_grapher_curve = (g_grapher_curve == 2) ? 0 : 2;
                            toast_show("Grapher","Cosine curve toggled",RGB(255,59,48));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Health")) {
                    int cy_h=w->y+TITLEBAR_H;
                    int sy_h=cy_h+95+78+6;
                    int cw_h=(w->w-10)/2;
                    int ch_h=46;
                    int hi_h;
                    for (hi_h=0; hi_h<5; hi_h++) {
                        int hx_h=w->x+3+(hi_h%2)*(cw_h+4);
                        int hy_h=sy_h+(hi_h/2)*(ch_h+4);
                        int hw_h=hi_h==4?w->w-8:cw_h;
                        if (mx>=hx_h && mx<hx_h+hw_h && my>=hy_h && my<hy_h+ch_h) {
                            g_health_metric=hi_h;
                            toast_show("Health","Metric selected",RGB(255,45,85));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Reality Composer")) {
                    int cy_rc=w->y+TITLEBAR_H;
                    if (mx>=w->x+4 && mx<w->x+w->w-4 && my>=cy_rc+26 && my<cy_rc+w->h-TITLEBAR_H-34) {
                        g_reality_object = (g_reality_object + 1) % 3;
                        toast_show("Reality Composer","Object selected",RGB(147,44,246));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Tot")) {
                    int cy_to=w->y+TITLEBAR_H;
                    int di_to;
                    for (di_to=0; di_to<7; di_to++) {
                        int cell_to=(w->w-12)/7;
                        int dx_to=w->x+6+di_to*cell_to;
                        if (mx>=dx_to && mx<dx_to+cell_to && my>=cy_to+1 && my<cy_to+24) {
                            g_tot_dot=di_to;
                            toast_show("Tot","Dot selected",RGB(255,149,0));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Digital Color Meter")) {
                    int cy_dcm=w->y+TITLEBAR_H;
                    if (mx>=w->x+1 && mx<w->x+w->w-1 && my>=cy_dcm && my<w->y+w->h-1) {
                        uint8_t rr=(uint8_t)((mx * 3 + my) & 0xff);
                        uint8_t gg=(uint8_t)((mx + my * 5) & 0xff);
                        uint8_t bb=(uint8_t)((mx * 7 + my * 2) & 0xff);
                        g_dcm_sample_color=RGB(rr,gg,bb);
                        toast_show("Digital Color Meter","Sampled color",RGB(rr,gg,bb));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"Klokki")) {
                    int cy_kl=w->y+TITLEBAR_H;
                    int ki_kl;
                    if (mx>=w->x+w->w-38 && mx<w->x+w->w-12 && my>=cy_kl+38 && my<cy_kl+62) {
                        g_klokki_running ^= 1;
                        toast_show("Klokki",g_klokki_running?"Timer running":"Timer paused",RGB(52,199,89));
                        dirty=1; goto end_left_press;
                    }
                    for (ki_kl=0; ki_kl<4; ki_kl++) {
                        int ky_kl=cy_kl+78+ki_kl*26;
                        if (mx>=w->x+4 && mx<w->x+w->w-4 && my>=ky_kl && my<ky_kl+22) {
                            g_klokki_project=ki_kl;
                            g_klokki_running=1;
                            toast_show("Klokki","Project selected",RGB(52,199,89));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Overflow 3")) {
                    static const char *ov_apps_ev[]={"Xcode","Terminal","Sketch","Figma","Sublime","VS Code","Tower","Sequel","Paw","Dash"};
                    int cy_ov=w->y+TITLEBAR_H;
                    int oi_ov;
                    for (oi_ov=0; oi_ov<10; oi_ov++) {
                        int ox_ov=w->x+8+(oi_ov%4)*(w->w/4);
                        int oy_ov=cy_ov+28+(oi_ov/4)*56;
                        if (mx>=ox_ov-3 && mx<ox_ov+43 && my>=oy_ov-3 && my<oy_ov+59) {
                            g_overflow_selected=oi_ov;
                            (void)gui_open_basic_app(ov_apps_ev[oi_ov]);
                            toast_show("Overflow 3",ov_apps_ev[oi_ov],RGB(40,120,200));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Disk Utility")) {
                    int dsb_w=100;
                    int ci_du=w->x+dsb_w+8;
                    int cy_du=w->y+TITLEBAR_H+28;
                    int btn_w=(w->w-dsb_w-24)/2;
                    int ai_du;
                    static const char *du_actions[]={"First Aid","Partition","Erase","Mount"};
                    if (btn_w < 74) btn_w = 74;
                    for (ai_du=0; ai_du<4; ai_du++) {
                        int ax=ci_du+(ai_du%2)*(btn_w+4);
                        int ay=cy_du+96+(ai_du/2)*24;
                        if (mx>=ax && mx<ax+btn_w && my>=ay && my<ay+20) {
                            g_disk_utility_action=ai_du;
                            toast_show("Disk Utility",du_actions[ai_du],RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"About This Mac")) {
                    int btn_w=140, btn_h=20;
                    int btn_x=w->x+w->w/2-btn_w/2;
                    int cy_ab=w->y+TITLEBAR_H+1+96+10+6*17+14+16;
                    if (mx>=btn_x && mx<btn_x+btn_w && my>=cy_ab && my<cy_ab+btn_h) {
                        g_about_update_checks++;
                        g_update_visible=1;
                        toast_show("Software Update","Checking for updates",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                }
                break;
            }
            /* 2048: click New Game button */
            { int ci_2k;
              for (ci_2k=g_num_windows-1; ci_2k>=0; ci_2k--) {
                gui_window_t *w=&g_windows[ci_2k];
                if (!w->visible||!w->title||!str_eq(w->title,"2048")) continue;
                /* New Game button at wx+ww-70, cy+4, 62x28 */
                int cy_2k = w->y + TITLEBAR_H;
                if (mx>=w->x+w->w-70 && mx<w->x+w->w-8 && my>=cy_2k+4 && my<cy_2k+32) {
                    g2048_new_game(); dirty=1; goto end_left_press;
                }
                /* Arrow key moves via click on board quadrant */
                int bsz_2k = w->w-12; if(bsz_2k>w->h-TITLEBAR_H-44) bsz_2k=w->h-TITLEBAR_H-44;
                int bx0_2k = w->x+(w->w-bsz_2k)/2, by0_2k = cy_2k+38;
                if (mx>=bx0_2k && mx<bx0_2k+bsz_2k && my>=by0_2k && my<by0_2k+bsz_2k) {
                    if (g_2048_state==0) { g2048_new_game(); dirty=1; goto end_left_press; }
                }
                break;
              }
            }
            /* Chess: click square to select/move piece */
            { int ci_ch;
              for (ci_ch=g_num_windows-1; ci_ch>=0; ci_ch--) {
                gui_window_t *w=&g_windows[ci_ch];
                if (!w->visible||!w->title||!str_eq(w->title,"Chess")) continue;
                { int jj3,cov3=0;
                  for(jj3=ci_ch+1;jj3<g_num_windows;jj3++){
                      gui_window_t *wj3=&g_windows[jj3];
                      if(!wj3->visible)continue;
                      if(mx>=wj3->x&&mx<wj3->x+wj3->w&&my>=wj3->y&&my<wj3->y+wj3->h){cov3=1;break;}
                  }
                  if(cov3)break;
                }
                int bsz=w->h-TITLEBAR_H-32; if(bsz>w->w-16)bsz=w->w-16; if(bsz>168)bsz=168;
                int bx=w->x+(w->w-bsz)/2, by=w->y+TITLEBAR_H+6;
                int sq=bsz/8;
                if (sq < 1) break;
                if(mx>=bx&&mx<bx+bsz&&my>=by&&my<by+bsz){
                    int cr=(my-by)/sq, cc=(mx-bx)/sq;
                    if(cr>=0&&cr<8&&cc>=0&&cc<8){
                        int piece=g_chess_board[cr][cc];
                        if(g_chess_sel_r<0){
                            /* Select piece */
                            int is_white=(piece>=1&&piece<=6);
                            int is_black=(piece>=7&&piece<=12);
                            if((g_chess_white_turn&&is_white)||(!g_chess_white_turn&&is_black)){
                                g_chess_sel_r=cr; g_chess_sel_c=cc; dirty=1;
                            }
                        } else {
                            /* Move piece */
                            int from_piece=g_chess_board[g_chess_sel_r][g_chess_sel_c];
                            /* Don't move onto own piece */
                            int dest_piece=g_chess_board[cr][cc];
                            int own_white=(from_piece>=1&&from_piece<=6);
                            int dest_white=(dest_piece>=1&&dest_piece<=6);
                            int dest_black=(dest_piece>=7&&dest_piece<=12);
                            int bad=(own_white&&dest_white)||(!own_white&&dest_black);
                            if(!bad&&!(cr==g_chess_sel_r&&cc==g_chess_sel_c)){
                                g_chess_board[cr][cc]=from_piece;
                                g_chess_board[g_chess_sel_r][g_chess_sel_c]=0;
                                g_chess_white_turn=!g_chess_white_turn;
                                /* AI: find best black move (capture > advance) */
                                if(!g_chess_white_turn){
                                    static const int piece_val[13]={0,1,3,3,5,9,100, 1,3,3,5,9,100};
                                    int best_score=-1, br=-1, bc=-1, bdr=-1, bdc=-1;
                                    int ar2,ac2,dr2,dc2;
                                    /* Scan all black pieces and try moves */
                                    for(ar2=0;ar2<8;ar2++) for(ac2=0;ac2<8;ac2++){
                                        int p=g_chess_board[ar2][ac2];
                                        if(p<7||p>12) continue;
                                        /* Each piece type: pawn=7, knight=8, bishop=9, rook=10, queen=11, king=12 */
                                        int pt=p-6; /* 1=p,2=n,3=b,4=r,5=q,6=k */
                                        /* Try pawn moves */
                                        if(pt==1){
                                            /* Forward */
                                            if(ar2+1<8&&g_chess_board[ar2+1][ac2]==0){
                                                int sc=0; if(ar2+1>=4)sc+=1;
                                                if(sc>best_score){best_score=sc;br=ar2;bc=ac2;bdr=ar2+1;bdc=ac2;}
                                            }
                                            /* Diagonal captures */
                                            int dcc;
                                            for(dcc=-1;dcc<=1;dcc+=2){
                                                int nc=ac2+dcc;
                                                if(nc>=0&&nc<8&&ar2+1<8){
                                                    int tp=g_chess_board[ar2+1][nc];
                                                    if(tp>=1&&tp<=6){
                                                        int sc=piece_val[tp]+2;
                                                        if(sc>best_score){best_score=sc;br=ar2;bc=ac2;bdr=ar2+1;bdc=nc;}
                                                    }
                                                }
                                            }
                                        } else {
                                            /* Other pieces: try all reachable squares */
                                            for(dr2=0;dr2<8;dr2++) for(dc2=0;dc2<8;dc2++){
                                                if(dr2==ar2&&dc2==ac2) continue;
                                                int tp=g_chess_board[dr2][dc2];
                                                if(tp>=7&&tp<=12) continue; /* own piece */
                                                int sc=tp>=1&&tp<=6?piece_val[tp]+1:0;
                                                if(sc>best_score){best_score=sc;br=ar2;bc=ac2;bdr=dr2;bdc=dc2;}
                                            }
                                        }
                                    }
                                    if(br>=0){
                                        g_chess_board[bdr][bdc]=g_chess_board[br][bc];
                                        g_chess_board[br][bc]=0;
                                    }
                                    g_chess_white_turn=1;
                                }
                            }
                            g_chess_sel_r=-1; g_chess_sel_c=-1; dirty=1;
                        }
                        goto end_left_press;
                    }
                }
                break;
              }
            }
            /* Minesweeper: click cell or face button */
            { int ci_ms;
              for (ci_ms=g_num_windows-1; ci_ms>=0; ci_ms--) {
                gui_window_t *w=&g_windows[ci_ms];
                if (!w->visible||!w->title||!str_eq(w->title,"Minesweeper")) continue;
                /* Helper: count adjacent mines */
                #define MS_ADJ(rr,cc) ({ int _a=0,_dr,_dc; \
                    for(_dr=-1;_dr<=1;_dr++) for(_dc=-1;_dc<=1;_dc++) { \
                        int _nr=(rr)+_dr,_nc=(cc)+_dc; \
                        if((_dr||_dc)&&_nr>=0&&_nr<MINE_ROWS&&_nc>=0&&_nc<MINE_COLS) \
                            _a+=g_mine_board[_nr][_nc]; } _a; })
                int wx2=w->x, wy2=w->y, ww2=w->w;
                int hdr_h2=30, hdr_y2=wy2+TITLEBAR_H+1;
                int bx2=wx2+(ww2-MINE_COLS*20)/2;
                int by3=hdr_y2+hdr_h2+2;
                /* Face button click */
                int fx2=wx2+ww2/2-12, fy2=hdr_y2+4;
                if (mx>=fx2&&mx<fx2+24&&my>=fy2&&my<fy2+22) {
                    /* Reset game */
                    int _r2,_c2;
                    g_mine_state=0; g_mine_remaining=MINE_COUNT; g_mine_rng=1;
                    for(_r2=0;_r2<MINE_ROWS;_r2++)for(_c2=0;_c2<MINE_COLS;_c2++){
                        g_mine_board[_r2][_c2]=0;g_mine_vis[_r2][_c2]=0;g_mine_flag[_r2][_c2]=0;}
                    dirty=1; goto end_left_press;
                }
                /* Cell click */
                if (mx>=bx2&&mx<bx2+MINE_COLS*20&&my>=by3&&my<by3+MINE_ROWS*20) {
                    int cr2=(my-by3)/20, cc2=(mx-bx2)/20;
                    if (cr2<0||cr2>=MINE_ROWS||cc2<0||cc2>=MINE_COLS) break;
                    if (g_mine_state==2||g_mine_state==3) break;
                    if (g_mine_flag[cr2][cc2]) break;
                    /* First click: place mines avoiding this cell */
                    if (g_mine_state==0) {
                        /* LCG mine placement */
                        int placed=0;
                        while (placed < MINE_COUNT) {
                            g_mine_rng = g_mine_rng*1664525u+1013904223u;
                            int rr2=(int)((g_mine_rng>>16)%(unsigned)MINE_ROWS);
                            int rc2=(int)(((g_mine_rng>>8)&0xFF)%(unsigned)MINE_COLS);
                            if ((rr2==cr2&&rc2==cc2)||g_mine_board[rr2][rc2]) continue;
                            g_mine_board[rr2][rc2]=1; placed++;
                        }
                        g_mine_start_tick=timer_ticks(); g_mine_state=1;
                    }
                    if (g_mine_vis[cr2][cc2]) break;
                    /* BFS flood fill reveal */
                    if (g_mine_board[cr2][cc2]) {
                        /* Hit mine */
                        g_mine_vis[cr2][cc2]=1;
                        g_mine_state=3; g_mine_end_tick=timer_ticks();
                    } else {
                        /* BFS */
                        static unsigned char ms_q[MINE_ROWS*MINE_COLS][2];
                        int ms_head=0, ms_tail=0;
                        ms_q[ms_tail][0]=(unsigned char)cr2;
                        ms_q[ms_tail][1]=(unsigned char)cc2;
                        ms_tail++;
                        g_mine_vis[cr2][cc2]=1;
                        while (ms_head<ms_tail) {
                            int qr=ms_q[ms_head][0], qc=ms_q[ms_head][1]; ms_head++;
                            int adj2=MS_ADJ(qr,qc);
                            if (adj2==0) {
                                int dr3,dc3;
                                for(dr3=-1;dr3<=1;dr3++) for(dc3=-1;dc3<=1;dc3++) {
                                    if(!dr3&&!dc3) continue;
                                    int nr3=qr+dr3,nc3=qc+dc3;
                                    if(nr3<0||nr3>=MINE_ROWS||nc3<0||nc3>=MINE_COLS) continue;
                                    if(g_mine_vis[nr3][nc3]||g_mine_flag[nr3][nc3]||g_mine_board[nr3][nc3]) continue;
                                    g_mine_vis[nr3][nc3]=1;
                                    ms_q[ms_tail][0]=(unsigned char)nr3;
                                    ms_q[ms_tail][1]=(unsigned char)nc3;
                                    ms_tail++;
                                }
                            }
                        }
                        /* Check win */
                        int hidden=0,ri2,ci3;
                        for(ri2=0;ri2<MINE_ROWS;ri2++)for(ci3=0;ci3<MINE_COLS;ci3++)
                            if(!g_mine_vis[ri2][ci3]&&!g_mine_board[ri2][ci3]) hidden++;
                        if(hidden==0){g_mine_state=2;g_mine_end_tick=timer_ticks();toast_show("Minesweeper","You win! :)",RGB(52,199,89));}
                    }
                    dirty=1; goto end_left_press;
                }
                break;
              }
              #undef MS_ADJ
            }
            /* Journal: click entry or new button */
            { int ci_jn;
              for (ci_jn=g_num_windows-1; ci_jn>=0; ci_jn--) {
                gui_window_t *w=&g_windows[ci_jn];
                if (!w->visible||!w->title||!str_eq(w->title,"Journal")) continue;
                int wx3=w->x, wy3=w->y, ww3=w->w, wh3=w->h;
                int cy3=wy3+TITLEBAR_H+1, ch3=wh3-TITLEBAR_H-19;
                int sb_w3=120;
                /* Check sidebar entry clicks */
                if (mx>=wx3+1&&mx<wx3+sb_w3&&my>=cy3+32) {
                    int ei=(my-(cy3+32))/48;
                    if (ei>=0&&ei<g_journal_count&&ei<JOURNAL_MAX) {
                        g_journal_sel=ei; g_journal_focused=1; dirty=1;
                        goto end_left_press;
                    }
                }
                /* + button in sidebar header */
                if (mx>=wx3+sb_w3-14&&mx<wx3+sb_w3&&my>=cy3&&my<cy3+28) {
                    if (g_journal_count < JOURNAL_MAX) {
                        g_journal_count++;
                        /* clear new entry */
                        int ni=g_journal_count-1;
                        int ki;
                        for(ki=0;ki<JOURNAL_TLEN-1;ki++) g_journal_titles[ni][ki]=(ki<8)?("New Entry")[ki]:0;
                        g_journal_titles[ni][8]=0;
                        g_journal_bodies[ni][0]=0;
                        g_journal_sel=ni; g_journal_focused=1;
                    }
                    dirty=1; goto end_left_press;
                }
                /* "+ New Entry" button at bottom of content */
                int btn_y2=cy3+ch3-22;
                if (mx>=wx3+sb_w3+9&&mx<wx3+ww3-2&&my>=btn_y2&&my<btn_y2+18) {
                    if (g_journal_count < JOURNAL_MAX) {
                        g_journal_count++;
                        int ni=g_journal_count-1;
                        int ki;
                        for(ki=0;ki<JOURNAL_TLEN-1;ki++) g_journal_titles[ni][ki]=(ki<8)?("New Entry")[ki]:0;
                        g_journal_titles[ni][8]=0;
                        g_journal_bodies[ni][0]=0;
                        g_journal_sel=ni; g_journal_focused=1;
                    }
                    dirty=1; goto end_left_press;
                }
                break;
              }
            }
            /* Calendar: click day or nav arrows */
            { int ci_ca;
              for (ci_ca=g_num_windows-1; ci_ca>=0; ci_ca--) {
                gui_window_t *w=&g_windows[ci_ca];
                if(!w->visible||!w->title||!str_eq(w->title,"Calendar")) continue;
                { int jj4,cov4=0;
                  for(jj4=ci_ca+1;jj4<g_num_windows;jj4++){
                      gui_window_t *wj4=&g_windows[jj4];
                      if(!wj4->visible)continue;
                      if(mx>=wj4->x&&mx<wj4->x+wj4->w&&my>=wj4->y&&my<wj4->y+wj4->h){cov4=1;break;}
                  }
                  if(cov4)break;
                }
                int cx0=w->x+1, cy0=w->y+TITLEBAR_H+1;
                int cw0=w->w-2;
                /* Month nav arrows */
                if(my>=cy0&&my<cy0+28){
                    if(mx>=cx0&&mx<cx0+20){ g_cal_offset--; dirty=1; goto end_left_press; }
                    if(mx>=cx0+cw0-20&&mx<cx0+cw0){ g_cal_offset++; dirty=1; goto end_left_press; }
                }
                /* Day click */
                { int cal_m, cal_y;
                  int dim2;
                  int start_col2;
                  gui_calendar_month_from_offset(g_cal_offset, &cal_y, &cal_m);
                  dim2 = datetime_days_in_month(cal_y, cal_m);
                  start_col2 = datetime_day_of_week(cal_y, cal_m, 1);
                  int cell_w2=cw0/7, cell_h2=(w->h-TITLEBAR_H-1-28-18-20)/6;
                  if(cell_w2<1)cell_w2=1;
                  if(cell_h2<1)cell_h2=1;
                  int day_y0=cy0+28+18;
                  if(my>=day_y0&&my<day_y0+cell_h2*6){
                      int row3=(my-day_y0)/cell_h2;
                      int col3=(mx-cx0)/cell_w2;
                      if(col3>=0&&col3<7){
                          int day3=row3*7+col3-start_col2+1;
                          if(day3>=1&&day3<=dim2){
                              g_cal_sel_day=day3;
                              g_cal_popup=1;
                              g_cal_evt_input[0]=0;
                              g_cal_evt_input_len=0;
                              dirty=1; goto end_left_press;
                          }
                      }
                  }
                }
                break;
              }
            }
            /* Home app: device toggles and room tabs */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Home")) continue;
                if (i != top_win_idx) continue;
                int rm_y2 = w->y + TITLEBAR_H + 28;
                /* Room tab clicks */
                { int ri2;
                  for (ri2=0;ri2<4;ri2++) {
                      int rtx=w->x+4+ri2*(w->w-8)/4, rtw=(w->w-8)/4-2;
                      if (mx>=rtx && mx<rtx+rtw && my>=rm_y2 && my<rm_y2+16) {
                          g_home_room=ri2; dirty=1; goto end_left_press;
                      }
                  }
                }
                /* Device tile clicks */
                { int di2;
                  int cell_w2=(w->w-16)/3, cell_h2=58;
                  for (di2=0;di2<6;di2++) {
                      int dx2=w->x+8+(di2%3)*cell_w2, dy2=rm_y2+36+(di2/3)*cell_h2;
                      if (mx>=dx2 && mx<dx2+cell_w2-4 && my>=dy2 && my<dy2+cell_h2-4) {
                          g_home_dev_on[di2] ^= 1;
                          const char *dnames[6]={"Living Light","Smart TV","Thermostat","Speaker","Curtains","Door Lock"};
                          toast_show(dnames[di2], g_home_dev_on[di2]?"On":"Off", RGB(255,149,0));
                          dirty=1; goto end_left_press;
                      }
                  }
                }
                break;
            }
            /* Notes sidebar click: select note; content click: focus last note */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Notes")) continue;
                if (i != top_win_idx) continue;
                int sb_w2 = (w->w > 220) ? 110 : w->w/2;
                int content_y2 = w->y + TITLEBAR_H + 1;
                /* Click in sidebar area */
                if (mx >= w->x+1 && mx <= w->x+sb_w2 && my >= content_y2+22) {
                    int ni = (my - (content_y2+22)) / 36;
                    if (ni >= 0 && ni < NOTES_COUNT) {
                        g_notes_sel = ni;
                        g_notes_focused = (ni == NOTES_COUNT-1) ? 1 : 0;
                        dirty = 1; goto end_left_press;
                    }
                }
                /* Click in content area of the editable note */
                if (mx > w->x+sb_w2 && my >= content_y2 && g_notes_sel == NOTES_COUNT-1) {
                    g_notes_focused = 1; dirty = 1; goto end_left_press;
                } else if (mx > w->x+sb_w2) {
                    g_notes_focused = 0; dirty = 1;
                }
            }
            /* Desktop icon click (right column) */
            {
                static const struct { const char *app; const char *title; int x; int y; } desk_icons[] = {
                    { "MyOS",    "MyOS Finder", 0, 0   },
                    { "Notes",   "Notes",       0, 58  },
                    { "Trash",   NULL,          0, 116 },
                    { "Downlds", "MyOS Finder", 0, 174 },
                    { "Docs",    "MyOS Finder", 0, 232 },
                };
                int di_base_x = VGA_WIDTH - 58, di_base_y = DESKTOP_Y + 10;
                int di_n = 5, di_k;
                for (di_k = 0; di_k < di_n; di_k++) {
                    int dix2 = di_base_x + desk_icons[di_k].x;
                    int diy2 = di_base_y + desk_icons[di_k].y;
                    if (mx >= dix2 && mx < dix2+36 && my >= diy2 && my < diy2+50) {
                        if (di_k == 2) { /* Trash */
                            toast_show("Trash", "Trash is empty", RGB(140,140,140));
                        } else {
                            const char *wt = desk_icons[di_k].title;
                            int fnd4 = 0, j4;
                            for (j4=0; j4<g_num_windows; j4++) {
                                if (g_windows[j4].title && str_eq(g_windows[j4].title, wt)) {
                                    g_windows[j4].visible=1; g_win_minimized[j4]=0;
                                    win_bring_to_front(j4); fnd4=1; break;
                                }
                            }
                            if (!fnd4 && g_num_windows < MAX_WINDOWS) {
                                gui_window_t *nw4 = &g_windows[g_num_windows];
                                nw4->x=200;nw4->y=80;nw4->w=380;nw4->h=340;
                                nw4->title=wt; nw4->visible=1; nw4->focused=0; nw4->dragging=0; nw4->maximized=0;
                                nw4->space=g_current_space;
                                g_win_anim[g_num_windows]=OPEN_ANIM; g_num_windows++;
                                toast_show(desk_icons[di_k].app, "Opened", RGB(80,140,200));
                            }
                        }
                        dirty = 1;
                        goto end_left_press;
                    }
                }
            }
            /* Dock click: open/focus window by name */
            {
                int num_dock = NUM_DOCK_ICONS;
                int total_dw = num_dock * DOCK_ICON_SIZE + (num_dock-1)*DOCK_ICON_PAD + 24 + 24;
                int dix = (VGA_WIDTH - total_dw) / 2;
                int diy = DOCK_Y + (DOCK_H - DOCK_ICON_SIZE)/2 - 4;
                int di;
                int ix2 = dix;
                for (di = 0; di < num_dock; di++) {
                    if (mx >= ix2 && mx < ix2 + DOCK_ICON_SIZE &&
                        my >= diy && my < diy + DOCK_ICON_SIZE) {
                        g_dock_bounce[di] = 24;  /* start bounce animation */
                        const char *aname = s_dock_icons[di].name;
                        /* Launchpad: toggle overlay instead of opening a window */
                        if (str_eq(aname, "Launchpad")) {
                            g_lp_visible = !g_lp_visible;
                            g_lp_page=0; g_lp_slen=0; g_lp_search[0]=0;
                            dirty = 1;
                            goto end_left_press;
                        }
                        /* Finder is stored as "MyOS Finder" in windows array */
                        const char *wname = str_eq(aname,"Finder") ? "MyOS Finder" : aname;
                        int found = 0, j2;
                        for (j2 = 0; j2 < g_num_windows; j2++) {
                            if (g_windows[j2].title && str_eq(g_windows[j2].title, wname)) {
                                g_windows[j2].visible = 1;
                                g_win_minimized[j2] = 0;
                                win_bring_to_front(j2);
                                found = 1; break;
                            }
                        }
                        if (!found && g_num_windows < MAX_WINDOWS) {
                            gui_window_t *nw = &g_windows[g_num_windows];
                            nw->focused=0; nw->dragging=0; nw->visible=1; nw->maximized=0;
                            if (str_eq(aname,"Finder"))     { nw->x=200;nw->y=80; nw->w=380;nw->h=340; nw->title="MyOS Finder"; }
                            else if (str_eq(aname,"Terminal"))  { nw->x=490;nw->y=90; nw->w=290;nw->h=240; nw->title="Terminal"; }
                            else if (str_eq(aname,"TextEdit"))  { nw->x=120;nw->y=80; nw->w=310;nw->h=260; nw->title="TextEdit"; g_edit_focused=1; }
                            else if (str_eq(aname,"Settings"))  { nw->x=150;nw->y=50; nw->w=500;nw->h=400; nw->title="Settings"; }
                            else if (str_eq(aname,"Calculator")){ nw->x=180;nw->y=100;nw->w=220;nw->h=280; nw->title="Calculator"; }
                            else if (str_eq(aname,"Clock"))     { nw->x=18; nw->y=80; nw->w=160;nw->h=200; nw->title="Clock"; }
                            else if (str_eq(aname,"Notes"))     { nw->x=18; nw->y=270;nw->w=260;nw->h=240; nw->title="Notes"; }
                            else if (str_eq(aname,"Music"))     { nw->x=300;nw->y=120;nw->w=240;nw->h=200; nw->title="Music"; }
                            else if (str_eq(aname,"Photos"))    { nw->x=240;nw->y=130;nw->w=260;nw->h=200; nw->title="Photos"; }
                            else if (str_eq(aname,"Safari"))    { nw->x=60; nw->y=50; nw->w=480;nw->h=380; nw->title="Safari"; }
                            else if (str_eq(aname,"Maps"))      { nw->x=180;nw->y=80; nw->w=280;nw->h=240; nw->title="Maps"; }
                            else if (str_eq(aname,"App Store")) { nw->x=480;nw->y=70; nw->w=290;nw->h=240; nw->title="App Store"; }
                            else if (str_eq(aname,"Calendar")) { nw->x=200;nw->y=80; nw->w=300;nw->h=260; nw->title="Calendar"; }
                            else if (str_eq(aname,"Mail"))     { nw->x=100;nw->y=80; nw->w=360;nw->h=260; nw->title="Mail"; }
                            else { nw->x=100;nw->y=80;nw->w=240;nw->h=180; nw->title=aname; }
                            nw->space = g_current_space;
                            g_win_anim[g_num_windows] = OPEN_ANIM; /* open animation */
                            g_num_windows++;
                            toast_show(aname, "Opened", s_dock_icons[di].color);
                        }
                        dirty = 1;
                        goto end_left_press;
                    }
                    ix2 += DOCK_ICON_SIZE + DOCK_ICON_PAD;
                    if (di == 5 || di == 11 || di == 15) ix2 += 8;
                }
            }
            /* Alfred search field */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Alfred")) continue;
                if (i != top_win_idx) continue;
                if (mx>=w->x+8 && mx<w->x+w->w-8 &&
                    my>=w->y+TITLEBAR_H+10 && my<w->y+TITLEBAR_H+44) {
                    g_alfred_search_focused=1; gui_search_focus(GUI_SEARCH_ALFRED); dirty=1; goto end_left_press;
                } else if (g_alfred_search_focused) {
                    g_alfred_search_focused=0; gui_search_blur(GUI_SEARCH_ALFRED); dirty=1;
                }
                { int ai_al;
                  for (ai_al=0; ai_al<4; ai_al++) {
                      int ay_al=w->y+TITLEBAR_H+54+ai_al*28;
                      if (mx>=w->x+2 && mx<w->x+w->w-2 && my>=ay_al-2 && my<ay_al+28) {
                          g_alfred_result=ai_al;
                          toast_show("Alfred","Result selected",RGB(200,50,200));
                          dirty=1; goto end_left_press;
                      }
                  } }
                break;
            }
            /* Static search fields in utility apps */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title) continue;
                if (i != top_win_idx) continue;
                if (str_eq(w->title,"Font Book")) {
                    int cy_fb=w->y+TITLEBAR_H;
                    int sb_w_fb=w->w*2/5;
                    if (mx>=w->x+4 && mx<w->x+sb_w_fb-2 && my>=cy_fb+26 && my<cy_fb+40) {
                        g_fontbook_search_focused=1; gui_search_focus(GUI_SEARCH_FONTBOOK); dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+2 && mx<w->x+sb_w_fb && my>=cy_fb+44 && my<cy_fb+44+9*18) {
                        int font_i = (my - (cy_fb+44)) / 18;
                        if (font_i >= 0 && font_i < 9) {
                            g_fontbook_selected = font_i;
                            g_fontbook_search_focused=0; gui_search_blur(GUI_SEARCH_FONTBOOK);
                            toast_show("Font Book","Font selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    } else if (g_fontbook_search_focused) { g_fontbook_search_focused=0; gui_search_blur(GUI_SEARCH_FONTBOOK); dirty=1; }
                } else if (str_eq(w->title,"Console")) {
                    int cy_co=w->y+TITLEBAR_H;
                    int sf_x_co=w->x+w->w-100;
                    { static const char *levels_co[]={"All","Errors","Warnings","Info"};
                      int bx_co=w->x+4, li_co;
                      if (my>=cy_co+4 && my<cy_co+18) {
                          for (li_co=0; li_co<4; li_co++) {
                              int lw_co=str_len(levels_co[li_co])*8+8;
                              if (mx>=bx_co && mx<bx_co+lw_co) {
                                  g_console_level_filter=li_co;
                                  g_console_filter_focused=0; gui_search_blur(GUI_SEARCH_CONSOLE);
                                  toast_show("Console","Level filter changed",RGB(0,122,255));
                                  dirty=1; goto end_left_press;
                              }
                              bx_co += str_len(levels_co[li_co])*8+12;
                          }
                      }
                    }
                    { int dev_w_co=w->w/4;
                      if (mx>=w->x+2 && mx<w->x+dev_w_co && my>=cy_co+40 && my<cy_co+40+5*16) {
                          int dev_i=(my-(cy_co+40))/16;
                          if (dev_i>=0 && dev_i<5) {
                              g_console_device_filter=dev_i;
                              g_console_filter_focused=0; gui_search_blur(GUI_SEARCH_CONSOLE);
                              toast_show("Console","Device filter changed",RGB(0,122,255));
                              dirty=1; goto end_left_press;
                          }
                      }
                    }
                    if (mx>=sf_x_co && mx<sf_x_co+94 && my>=cy_co+4 && my<cy_co+18) {
                        g_console_filter_focused=1; gui_search_focus(GUI_SEARCH_CONSOLE); dirty=1; goto end_left_press;
                    } else if (g_console_filter_focused) { g_console_filter_focused=0; gui_search_blur(GUI_SEARCH_CONSOLE); dirty=1; }
                } else if (str_eq(w->title,"SF Symbols")) {
                    int cy_sf=w->y+TITLEBAR_H;
                    if (mx>=w->x+8 && mx<w->x+w->w-82 && my>=cy_sf+6 && my<cy_sf+22) {
                        g_sfsymbols_search_focused=1; gui_search_focus(GUI_SEARCH_SFSYMBOLS); dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+10 && mx<w->x+w->w-10 && my>=cy_sf+30 && my<cy_sf+130) {
                        int cell_w_sf=(w->w-20)/5;
                        int col_sf=cell_w_sf>0 ? (mx-(w->x+10))/cell_w_sf : 0;
                        int row_sf=(my-(cy_sf+30))/50;
                        int sym_i=row_sf*5+col_sf;
                        if (sym_i>=0 && sym_i<10) {
                            g_sfsymbols_selected=sym_i;
                            g_sfsymbols_search_focused=0; gui_search_blur(GUI_SEARCH_SFSYMBOLS);
                            toast_show("SF Symbols","Symbol selected",RGB(52,199,89));
                            dirty=1; goto end_left_press;
                        }
                    } else if (g_sfsymbols_search_focused) { g_sfsymbols_search_focused=0; gui_search_blur(GUI_SEARCH_SFSYMBOLS); dirty=1; }
                } else if (str_eq(w->title,"1Password")) {
                    int sy_pw=w->y+TITLEBAR_H+26;
                    { int vi_pw; for (vi_pw=0; vi_pw<5; vi_pw++) {
                        int vy_pw=w->y+TITLEBAR_H+24+vi_pw*22;
                        if (mx>=w->x+2 && mx<w->x+89 && my>=vy_pw-2 && my<vy_pw+18) {
                            g_onepassword_vault=vi_pw;
                            g_onepassword_search_focused=0; gui_search_blur(GUI_SEARCH_ONEPASSWORD);
                            toast_show("1Password","Vault selected",RGB(0,120,200));
                            dirty=1; goto end_left_press;
                        }
                    } }
                    if (mx>=w->x+92 && mx<w->x+w->w-4 && my>=sy_pw && my<sy_pw+20) {
                        g_onepassword_search_focused=1; gui_search_focus(GUI_SEARCH_ONEPASSWORD); dirty=1; goto end_left_press;
                    } else if (g_onepassword_search_focused) { g_onepassword_search_focused=0; gui_search_blur(GUI_SEARCH_ONEPASSWORD); dirty=1; }
                } else if (str_eq(w->title,"Raycast")) {
                    int sy_rc=w->y+TITLEBAR_H+10;
                    if (mx>=w->x+8 && mx<w->x+w->w-8 && my>=sy_rc && my<sy_rc+30) {
                        g_raycast_search_focused=1; gui_search_focus(GUI_SEARCH_RAYCAST); dirty=1; goto end_left_press;
                    } else if (g_raycast_search_focused) { g_raycast_search_focused=0; gui_search_blur(GUI_SEARCH_RAYCAST); dirty=1; }
                    { int ri_rc; for (ri_rc=0; ri_rc<6; ri_rc++) {
                        int ry_rc=w->y+TITLEBAR_H+52+ri_rc*26;
                        if (mx>=w->x+2 && mx<w->x+w->w-2 && my>=ry_rc-2 && my<ry_rc+26) {
                            g_raycast_result=ri_rc;
                            toast_show("Raycast","Command selected",RGB(255,90,30));
                            dirty=1; goto end_left_press;
                        }
                    } }
                } else if (str_eq(w->title,"Privacy")) {
                    int cy_pv=w->y+TITLEBAR_H, ci_pv;
                    for (ci_pv=0; ci_pv<9; ci_pv++) {
                        int y_pv=cy_pv+6+ci_pv*20;
                        if (mx>=w->x+1 && mx<w->x+100 && my>=y_pv-2 && my<y_pv+18) {
                            g_privacy_category=ci_pv; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Accessibility")) {
                    int cy_ac=w->y+TITLEBAR_H, ci_ac;
                    for (ci_ac=0; ci_ac<5; ci_ac++) {
                        int y_ac=cy_ac+8+ci_ac*28;
                        if (mx>=w->x+1 && mx<w->x+110 && my>=y_ac-4 && my<y_ac+22) {
                            g_accessibility_category=ci_ac; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Stocks")) {
                    int ns_st=7;
                    int row_h_st=(w->h-TITLEBAR_H-50)/ns_st;
                    int si_st;
                    if (row_h_st < 28) row_h_st = 28;
                    for (si_st=0; si_st<ns_st; si_st++) {
                        int ry_st=w->y+TITLEBAR_H+32+si_st*row_h_st;
                        if (ry_st+row_h_st>w->y+w->h-22) break;
                        if (mx>=w->x+2 && mx<w->x+w->w-2 &&
                            my>=ry_st && my<ry_st+row_h_st) {
                            g_stocks_selected=si_st;
                            toast_show("Stocks","Ticker selected",RGB(52,199,89));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Weather")) {
                    int by_w=w->y+TITLEBAR_H+1;
                    int hf_y_w=by_w+80;
                    int hw_w=(w->w-4)/RUNTIME_HOURLY_COUNT;
                    int wi_w;
                    if (hw_w < 1) hw_w = 1;
                    if (my>=hf_y_w+10 && my<hf_y_w+42) {
                        for (wi_w=0; wi_w<RUNTIME_HOURLY_COUNT; wi_w++) {
                            int hx_w=w->x+2+wi_w*hw_w;
                            if (mx>=hx_w && mx<hx_w+hw_w) {
                                g_weather_selection=wi_w;
                                toast_show("Weather","Hourly forecast selected",RGB(90,180,255));
                                dirty=1; goto end_left_press;
                            }
                        }
                    }
                    { int fc_y_w=by_w+128;
                      int dw_w=(w->w-4)/RUNTIME_WEATHER_DAYS;
                      int di_w;
                      if (dw_w < 1) dw_w = 1;
                      if (my>=fc_y_w+12 && my<fc_y_w+72) {
                          for (di_w=0; di_w<RUNTIME_WEATHER_DAYS; di_w++) {
                              int dx_w=w->x+2+di_w*dw_w;
                              if (mx>=dx_w && mx<dx_w+dw_w) {
                                  g_weather_selection=RUNTIME_HOURLY_COUNT+di_w;
                                  toast_show("Weather","Daily forecast selected",RGB(90,180,255));
                                  dirty=1; goto end_left_press;
                              }
                          }
                      } }
                } else if (str_eq(w->title,"Keyboard Shortcuts")) {
                    int col_count_ks=(w->w>=500)?2:1;
                    int col_w_ks=(w->w-20)/col_count_ks;
                    int ns_ks=51;
                    int rows_per_col_ks=(ns_ks+col_count_ks-1)/col_count_ks;
                    int ky_ks=w->y+TITLEBAR_H+38+16;
                    int i_ks;
                    if (col_w_ks < 1) col_w_ks = 1;
                    for (i_ks=0; i_ks<ns_ks; i_ks++) {
                        int col_ks=i_ks/rows_per_col_ks;
                        int row_ks=i_ks%rows_per_col_ks;
                        int base_x_ks=w->x+8+col_ks*col_w_ks;
                        int ry_ks=ky_ks+row_ks*14;
                        if (col_ks>=col_count_ks || ry_ks+13>w->y+w->h-22) break;
                        if (mx>=base_x_ks && mx<base_x_ks+col_w_ks-6 &&
                            my>=ry_ks-2 && my<ry_ks+13) {
                            g_keyboard_shortcut_selected=i_ks;
                            toast_show("Keyboard Shortcuts","Shortcut selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"System Info")) {
                    int sy_si=w->y+TITLEBAR_H+1+52+8;
                    int i_si;
                    for (i_si=0; i_si<7; i_si++) {
                        int ry_si=sy_si+i_si*18;
                        if (mx>=w->x+4 && mx<w->x+w->w-4 &&
                            my>=ry_si-2 && my<ry_si+16) {
                            g_system_info_selected=i_si;
                            toast_show("System Info","Row selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Stickies")) {
                    int cy_st=w->y+TITLEBAR_H;
                    int sw_st=(w->w-10)/2;
                    int sh_st=(w->h-TITLEBAR_H-10)/2;
                    int ni_st;
                    for (ni_st=0; ni_st<4; ni_st++) {
                        int sx_st=w->x+2+(ni_st%2)*(sw_st+4);
                        int sy_st=cy_st+2+(ni_st/2)*(sh_st+4);
                        if (mx>=sx_st && mx<sx_st+sw_st && my>=sy_st && my<sy_st+sh_st) {
                            g_stickies_selected=ni_st;
                            toast_show("Stickies","Note selected",RGB(255,204,0));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Fantastical")) {
                    datetime_t fa_ev_now;
                    int fa_len_ev, fa_first_ev, cell_w_fa, row_fa, col_fa;
                    get_current_datetime(&fa_ev_now);
                    fa_len_ev=datetime_days_in_month(fa_ev_now.year, fa_ev_now.month);
                    fa_first_ev=datetime_day_of_week(fa_ev_now.year, fa_ev_now.month, 1);
                    cell_w_fa=(w->w-2)/7;
                    if (cell_w_fa < 1) cell_w_fa = 1;
                    for (row_fa=0; row_fa<6; row_fa++) for (col_fa=0; col_fa<7; col_fa++) {
                        int day_fa=row_fa*7+col_fa-fa_first_ev+1;
                        int cx_fa=w->x+2+col_fa*cell_w_fa;
                        int cy_fa=w->y+TITLEBAR_H+46+row_fa*18;
                        if (day_fa<1 || day_fa>fa_len_ev) continue;
                        if (mx>=cx_fa && mx<cx_fa+cell_w_fa && my>=cy_fa && my<cy_fa+18) {
                            g_fantastical_selected_day=day_fa;
                            toast_show("Fantastical","Day selected",RGB(220,40,40));
                            dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Configurator")) {
                    int cy_cf=w->y+TITLEBAR_H, ci_cf;
                    for (ci_cf=0; ci_cf<6; ci_cf++) {
                        int y_cf=cy_cf+8+ci_cf*24;
                        if (mx>=w->x+1 && mx<w->x+100 && my>=y_cf-4 && my<y_cf+18) {
                            g_configurator_category=ci_cf; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Proxyman")) {
                    int cy_pm=w->y+TITLEBAR_H, hi_pm;
                    for (hi_pm=0; hi_pm<4; hi_pm++) {
                        int y_pm=cy_pm+22+hi_pm*20;
                        if (mx>=w->x+2 && mx<w->x+80 && my>=y_pm && my<y_pm+18) {
                            g_proxyman_session=hi_pm; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"iStudiez Pro")) {
                    int cy_is=w->y+TITLEBAR_H, ti_is;
                    for (ti_is=0; ti_is<4; ti_is++) {
                        int y_is=cy_is+10+ti_is*30;
                        if (mx>=w->x+1 && mx<w->x+80 && my>=y_is-4 && my<y_is+18) {
                            g_istudiez_tab=ti_is; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Things 3")) {
                    int cy_th=w->y+TITLEBAR_H, ti_th;
                    for (ti_th=0; ti_th<5; ti_th++) {
                        int y_th=cy_th+10+ti_th*24;
                        if (mx>=w->x+1 && mx<w->x+90 && my>=y_th-2 && my<y_th+20) {
                            g_things_section=ti_th; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Bear")) {
                    int cy_br=w->y+TITLEBAR_H, bi_br;
                    for (bi_br=0; bi_br<5; bi_br++) {
                        int y_br=cy_br+24+bi_br*22;
                        if (mx>=w->x+2 && mx<w->x+80 && my>=y_br-2 && my<y_br+18) {
                            g_bear_note=bi_br; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Reeder 5")) {
                    int cy_re=w->y+TITLEBAR_H, ri_re;
                    for (ri_re=0; ri_re<5; ri_re++) {
                        int y_re=cy_re+24+ri_re*22;
                        if (mx>=w->x+2 && mx<w->x+80 && my>=y_re-2 && my<y_re+18) {
                            g_reeder_feed=ri_re; dirty=1; goto end_left_press;
                        }
                    }
                } else if (str_eq(w->title,"Color Picker")) {
                    int cpt_x_ev=w->x+4, cpt_y_ev=w->y+TITLEBAR_H+6;
                    static const char *cp_tabs_ev[] = { "Wheel", "Sliders", "Palette" };
                    int ct_ev;
                    for (ct_ev=0; ct_ev<3; ct_ev++) {
                        int tw_ev=(int)(str_len(cp_tabs_ev[ct_ev])*8+8);
                        if (mx>=cpt_x_ev && mx<cpt_x_ev+tw_ev && my>=cpt_y_ev-2 && my<cpt_y_ev+14) {
                            g_color_picker_tab=ct_ev;
                            toast_show("Color Picker","Mode selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                        cpt_x_ev += tw_ev + 4;
                    }
                } else if (str_eq(w->title,"MyOS Finder")) {
                    int sfx_fn=w->x+w->w-112;
                    int sy_fn=w->y+TITLEBAR_H+6;
                    if (mx>=sfx_fn && mx<sfx_fn+72 && my>=sy_fn && my<sy_fn+16) {
                        g_finder_search_focused=1; gui_search_focus(GUI_SEARCH_FINDER); dirty=1; goto end_left_press;
                    } else if (g_finder_search_focused) { g_finder_search_focused=0; gui_search_blur(GUI_SEARCH_FINDER); dirty=1; }
                }
                break;
            }
            /* Handoff activity selection and continue */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Handoff")) continue;
                if (i != top_win_idx) continue;
                { int cy_hf = w->y + TITLEBAR_H;
                  int ai_hf;
                  static const char *apps_hf[] = {"Safari","Notes","Mail","Pages"};
                  for (ai_hf=0; ai_hf<4; ai_hf++) {
                      int ay_hf = cy_hf + 30 + ai_hf * 36;
                      if (ay_hf + 32 > cy_hf + w->h - TITLEBAR_H) break;
                      if (mx>=w->x+8 && mx<w->x+w->w-8 && my>=ay_hf && my<ay_hf+32) {
                          g_handoff_selected = ai_hf;
                          dirty=1; goto end_left_press;
                      }
                  }
                  if (mx>=w->x+8 && mx<w->x+w->w-8 &&
                      my>=cy_hf+w->h-TITLEBAR_H-24 && my<cy_hf+w->h-TITLEBAR_H) {
                      if (g_handoff_selected < 0) g_handoff_selected = 0;
                      if (g_handoff_selected > 3) g_handoff_selected = 3;
                      (void)gui_open_basic_app(apps_hf[g_handoff_selected]);
                      toast_show("Handoff","Continued on this Mac",RGB(52,199,89));
                      dirty=1; goto end_left_press;
                  }
                }
                break;
            }
            /* News article selection */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"News")) continue;
                if (i != top_win_idx) continue;
                { int feat_y_nw = w->y + TITLEBAR_H + 28;
                  int ai_nw;
                  for (ai_nw=0; ai_nw<5; ai_nw++) {
                      int ay_nw = feat_y_nw + 76 + ai_nw * 34;
                      if (ay_nw + 34 > w->y + w->h - 22) break;
                      if (mx>=w->x+4 && mx<w->x+w->w-4 && my>=ay_nw && my<ay_nw+32) {
                          g_news_selected = ai_nw;
                          toast_show("News","Article selected",RGB(255,59,48));
                          dirty=1; goto end_left_press;
                      }
                  }
                }
                break;
            }
            /* Find My, Time Machine, and AirDrop window actions */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title) continue;
                if (i != top_win_idx) continue;
                if (str_eq(w->title,"Find My")) {
                    int fm_h_ev = w->h - TITLEBAR_H - 18;
                    int fd_ev;
                    static const int frx_ev[] = {50,60,70};
                    static const int fry_ev[] = {40,60,30};
                    if (mx>=w->x+4 && mx<w->x+w->w-4 &&
                        my>=w->y+TITLEBAR_H+3 && my<w->y+TITLEBAR_H+23) {
                        g_findmy_search_focused = 1; gui_search_focus(GUI_SEARCH_FINDMY);
                        dirty=1; goto end_left_press;
                    }
                    if (g_findmy_search_focused) {
                        g_findmy_search_focused = 0; gui_search_blur(GUI_SEARCH_FINDMY);
                        dirty = 1;
                    }
                    for (fd_ev=0; fd_ev<3; fd_ev++) {
                        int fx_ev = w->x + 1 + frx_ev[fd_ev] * (w->w - 2) / 100;
                        int fy_ev = w->y + TITLEBAR_H + 1 + fry_ev[fd_ev] * fm_h_ev / 100;
                        int dx_ev = mx - fx_ev;
                        int dy_ev = my - fy_ev;
                        if (dx_ev*dx_ev + dy_ev*dy_ev <= 18*18) {
                            g_findmy_selected = fd_ev;
                            toast_show("Find My","Device selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                    { int bp_y_ev = w->y + TITLEBAR_H + fm_h_ev - 44;
                      for (fd_ev=0; fd_ev<3; fd_ev++) {
                          int dx3_ev = w->x + 8 + fd_ev * ((w->w - 16) / 3);
                          int bw_ev = (w->w - 16) / 3;
                          if (mx>=dx3_ev && mx<dx3_ev+bw_ev && my>=bp_y_ev && my<bp_y_ev+44) {
                              g_findmy_selected = fd_ev;
                              dirty=1; goto end_left_press;
                          }
                      }
                    }
                } else if (str_eq(w->title,"Time Machine")) {
                    int cy_tm_ev = w->y + TITLEBAR_H + 1;
                    int ch_tm_ev = w->h - TITLEBAR_H - 19;
                    int tl_x_ev = w->x + w->w - 28;
                    { static const int tl_offs_ev[] = {10, 30, 60, 90, 120, 150};
                      int ti_tm;
                      for (ti_tm=0; ti_tm<6; ti_tm++) {
                          int ty_tm=cy_tm_ev+tl_offs_ev[ti_tm];
                          if (ty_tm > cy_tm_ev+ch_tm_ev-20) break;
                          if (mx>=tl_x_ev && mx<tl_x_ev+24 && my>=ty_tm-6 && my<ty_tm+6) {
                              g_tm_selected_snapshot=ti_tm;
                              toast_show("Time Machine","Snapshot selected",RGB(0,122,255));
                              dirty=1; goto end_left_press;
                          }
                      } }
                    int bc_y_ev = cy_tm_ev + ch_tm_ev - 24;
                    if (mx>=w->x+8 && mx<w->x+68 && my>=bc_y_ev+3 && my<bc_y_ev+19) {
                        g_tm_cancelled++;
                        toast_show("Time Machine","Cancelled",RGB(120,120,128));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=w->x+w->w-72 && mx<w->x+w->w-8 && my>=bc_y_ev+3 && my<bc_y_ev+19) {
                        g_tm_restored++;
                        g_tm_snapshot_count++;
                        g_tm_last_snapshot_tick = timer_ticks();
                        toast_show("Time Machine","Snapshot restored",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                } else if (str_eq(w->title,"AirDrop")) {
                    int btn_y_ad = w->y + TITLEBAR_H + 182;
                    int btn1w_ad = (w->w - 20) / 2;
                    int btn2x_ad = w->x + w->w / 2 + 2;
                    if (mx>=w->x+8 && mx<w->x+8+btn1w_ad && my>=btn_y_ad && my<btn_y_ad+16) {
                        g_airdrop_mode = 0;
                        toast_show("AirDrop","Contacts Only",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                    if (mx>=btn2x_ad && mx<btn2x_ad+btn1w_ad && my>=btn_y_ad && my<btn_y_ad+16) {
                        g_airdrop_mode = 1;
                        toast_show("AirDrop","Everyone",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                }
                break;
            }
            /* Wallet: local Apple Pay authorization */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Wallet")) continue;
                if (i != top_win_idx) continue;
                { int btn_y_wl = w->y + TITLEBAR_H + 28 + 3*12 + 70;
                  int btn_x_wl = w->x + (w->w - 150) / 2;
                  if (mx>=btn_x_wl && mx<btn_x_wl+150 && my>=btn_y_wl && my<btn_y_wl+28) {
                      g_wallet_pay_count++;
                      toast_show("Wallet","Face ID payment authorized",RGB(52,199,89));
                      dirty=1; goto end_left_press;
                  } }
                break;
            }
            /* Mail: New compose button + message list click */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Mail")) continue;
                if (i != top_win_idx) continue;
                int cy_ml = w->y+TITLEBAR_H+1;
                if (!g_mail_compose && mx>=w->x+w->w-76&&mx<w->x+w->w-4&&my>=cy_ml+4&&my<cy_ml+22) {
                    g_mail_search_focused=1; gui_search_focus(GUI_SEARCH_MAIL); dirty=1; goto end_left_press;
                } else if (!g_mail_compose && g_mail_search_focused) {
                    g_mail_search_focused=0; gui_search_blur(GUI_SEARCH_MAIL); dirty=1;
                }
                /* New button (compose) at x+4..x+32, cy_ml+4..cy_ml+22 */
                if (!g_mail_compose && mx>=w->x+4&&mx<w->x+32&&my>=cy_ml+4&&my<cy_ml+22) {
                    g_mail_compose=1; g_mail_focused_field=1;
                    g_mail_to[0]=0; g_mail_to_len=0;
                    g_mail_subject[0]=0; g_mail_subject_len=0;
                    g_mail_body[0]=0; g_mail_body_len=0;
                    dirty=1; goto end_left_press;
                }
                /* Cancel button in compose view */
                if (g_mail_compose && mx>=w->x+8&&mx<w->x+56&&my>=cy_ml+9&&my<cy_ml+21) {
                    g_mail_compose=0; g_mail_focused_field=0; dirty=1; goto end_left_press;
                }
                /* Send button in compose view */
                if (g_mail_compose && mx>=w->x+w->w-58&&mx<w->x+w->w-8&&my>=cy_ml+5&&my<cy_ml+23) {
                    int sj;
                    if (g_mail_to_len <= 0) {
                        toast_show("Mail","Add a recipient",RGB(255,149,0));
                    } else {
                        for (sj=0; sj<63 && g_mail_subject[sj]; sj++)
                            g_mail_last_sent_subject[sj] = g_mail_subject[sj];
                        g_mail_last_sent_subject[sj] = 0;
                        if (g_mail_last_sent_subject[0] == 0) {
                            g_mail_last_sent_subject[0] = '(';
                            g_mail_last_sent_subject[1] = 'n';
                            g_mail_last_sent_subject[2] = 'o';
                            g_mail_last_sent_subject[3] = ' ';
                            g_mail_last_sent_subject[4] = 's';
                            g_mail_last_sent_subject[5] = 'u';
                            g_mail_last_sent_subject[6] = 'b';
                            g_mail_last_sent_subject[7] = 'j';
                            g_mail_last_sent_subject[8] = 'e';
                            g_mail_last_sent_subject[9] = 'c';
                            g_mail_last_sent_subject[10] = 't';
                            g_mail_last_sent_subject[11] = ')';
                            g_mail_last_sent_subject[12] = 0;
                        }
                        g_mail_sent_count++;
                        g_mail_compose=0;
                        g_mail_focused_field=0;
                        g_mail_to[0]=0; g_mail_to_len=0;
                        g_mail_subject[0]=0; g_mail_subject_len=0;
                        g_mail_body[0]=0; g_mail_body_len=0;
                        toast_show("Mail","Message saved to Sent",RGB(0,122,255));
                    }
                    dirty=1; goto end_left_press;
                }
                /* Field focus in compose */
                if (g_mail_compose) {
                    int fcy=cy_ml+30;
                    if (my>=fcy+2&&my<fcy+20) { g_mail_focused_field=1; dirty=1; goto end_left_press; }
                    fcy+=22;
                    if (my>=fcy+2&&my<fcy+20) { g_mail_focused_field=2; dirty=1; goto end_left_press; }
                    fcy+=22;
                    if (my>=fcy&&my<w->y+w->h-18) { g_mail_focused_field=3; dirty=1; goto end_left_press; }
                }
                /* Message list and preview action clicks (not composing) */
                if (!g_mail_compose) {
                    int sb_w_ml=68, list_w_ml=(w->w-2-68)*2/5;
                    int lx_ml=w->x+1+sb_w_ml, msg_y=cy_ml+26;
                    int fi_ml;
                    for (fi_ml=0; fi_ml<6; fi_ml++) {
                        int fy_ml=msg_y+6+fi_ml*22;
                        if (mx>=w->x+1 && mx<w->x+1+sb_w_ml && my>=fy_ml-2 && my<fy_ml+18) {
                            g_mail_folder=fi_ml;
                            g_mail_sel_msg=0;
                            toast_show("Mail","Folder selected",RGB(0,122,255));
                            dirty=1; goto end_left_press;
                        }
                    }
                    int px_ml = w->x + 1 + sb_w_ml + list_w_ml;
                    int pw_ml = w->w - 2 - sb_w_ml - list_w_ml;
                    int mi3;
                    for(mi3=0;mi3<5;mi3++) {
                        if(mx>=lx_ml&&mx<lx_ml+list_w_ml&&my>=msg_y+mi3*38&&my<msg_y+mi3*38+38) {
                            g_mail_sel_msg=mi3; dirty=1; goto end_left_press;
                        }
                    }
                    {
                        int ch_ml = w->h - TITLEBAR_H - 19;
                        int reply_y_ml = cy_ml + 182;
                        if (reply_y_ml + 20 < cy_ml + ch_ml - 30) {
                            if (mx>=px_ml+8 && mx<px_ml+52 && my>=reply_y_ml && my<reply_y_ml+18) {
                                (void)pw_ml;
                                gui_mail_start_compose("sender@example.com", "Re:", "");
                                toast_show("Mail","Reply draft ready",RGB(0,122,255));
                                dirty=1; goto end_left_press;
                            }
                            if (mx>=px_ml+58 && mx<px_ml+110 && my>=reply_y_ml && my<reply_y_ml+18) {
                                gui_mail_start_compose("", "Fwd:", "");
                                g_mail_focused_field = 1;
                                toast_show("Mail","Forward draft ready",RGB(0,122,255));
                                dirty=1; goto end_left_press;
                            }
                        }
                    }
                }
                break;
            }
            /* Calculator button clicks */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible) continue;
                if (w->title && str_eq(w->title, "Calculator")) {
                    char act = calc_hit(mx, my, w);
                    if (act) calc_press(act);
                }
            }
            /* TextEdit toolbar clicks */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"TextEdit")) continue;
                int tby2 = w->y + TITLEBAR_H + 1;
                if (my < tby2 || my > tby2+22) break;
                /* B button at x+6 */
                if (mx>=w->x+6 && mx<w->x+24 && my>=tby2+3 && my<tby2+19) {
                    g_edit_bold = !g_edit_bold; dirty=1; goto end_left_press;
                }
                /* I button at x+28 */
                if (mx>=w->x+28 && mx<w->x+46 && my>=tby2+3 && my<tby2+19) {
                    g_edit_italic = !g_edit_italic; dirty=1; goto end_left_press;
                }
                /* A- button at x+54 */
                if (mx>=w->x+54 && mx<w->x+72 && my>=tby2+3 && my<tby2+19) {
                    if (g_edit_font_size > 0) { g_edit_font_size--; } dirty=1; goto end_left_press;
                }
                /* A+ button at x+76 */
                if (mx>=w->x+76 && mx<w->x+96 && my>=tby2+3 && my<tby2+19) {
                    if (g_edit_font_size < 2) { g_edit_font_size++; } dirty=1; goto end_left_press;
                }
                /* Color at x+98 */
                if (mx>=w->x+98 && mx<w->x+126 && my>=tby2+3 && my<tby2+19) {
                    g_edit_color = (g_edit_color + 1) % 4; dirty=1; goto end_left_press;
                }
                /* Click on text area = focus */
                if (my > tby2+22) { g_edit_focused = 1; textedit_clear_selection(); dirty=1; goto end_left_press; }
            }
            /* Music window button clicks */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Music")) continue;
                if (i != top_win_idx) continue;
                int ww2 = w->w;
                { int tab_y2=w->y+TITLEBAR_H+2, tab_w2=(ww2-2)/3;
                  if (mx>=w->x+1 && mx<w->x+1+tab_w2*3 && my>=tab_y2 && my<tab_y2+20) {
                      g_music_tab=(mx-(w->x+1))/tab_w2;
                      if (g_music_tab < 0) g_music_tab = 0;
                      if (g_music_tab > 2) g_music_tab = 2;
                      dirty=1; goto end_left_press;
                  }
                  if (g_music_tab == 0) {
                      int ly=w->y+TITLEBAR_H+34, mt;
                      for (mt=0; mt<5; mt++) {
                          int ry=ly+mt*24;
                          if (mx>=w->x+8 && mx<w->x+ww2-8 && my>=ry && my<ry+22) {
                              g_music_track=mt; g_music_tab=1; g_music_playing=1;
                              toast_show("Music","Track selected",RGB(252,60,68));
                              dirty=1; goto end_left_press;
                          }
                      }
                  }
                  if (g_music_tab == 2) {
                      int ry0=w->y+TITLEBAR_H+38, ms;
                      for (ms=0; ms<4; ms++) {
                          int ry=ry0+ms*34;
                          if (mx>=w->x+12 && mx<w->x+ww2-12 && my>=ry && my<ry+28) {
                              g_music_track=ms; g_music_playing=1;
                              toast_show("Music","Station tuned",RGB(252,60,68));
                              dirty=1; goto end_left_press;
                          }
                      }
                  } }
                int art_sz2 = (ww2-2 > 120) ? 100 : ww2/2;
                if (art_sz2 > w->h/2-20) art_sz2 = w->h/2-20;
                int art_y2 = w->y + TITLEBAR_H + 28;
                int ty3 = art_y2 + art_sz2 + 10 + 44 + 24;
                int mid2 = w->x + ww2/2;
                /* Play/Pause button region */
                if (mx>=mid2-12 && mx<=mid2+14 && my>=ty3-4 && my<=ty3+18) {
                    g_music_playing = !g_music_playing;
                    toast_show("Music", g_music_playing ? "Playing" : "Paused", RGB(252,60,68));
                    dirty=1; goto end_left_press;
                }
                /* Prev button */
                if (mx>=mid2-52 && mx<=mid2-20 && my>=ty3-4 && my<=ty3+18) {
                    if (g_music_track > 0) g_music_track--; else g_music_track=4;
                    toast_show("Music", "Previous track", RGB(252,60,68));
                    dirty=1; goto end_left_press;
                }
                /* Next button */
                if (mx>=mid2+20 && mx<=mid2+52 && my>=ty3-4 && my<=ty3+18) {
                    g_music_track = (g_music_track + 1) % 5;
                    toast_show("Music", "Next track", RGB(252,60,68));
                    dirty=1; goto end_left_press;
                }
            }
            /* Clock tab clicks + Timer/Stopwatch buttons */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || !w->title || !str_eq(w->title,"Clock")) continue;
                int tby2 = w->y + TITLEBAR_H + 1;
                int tbw2 = w->w / 4;
                if (tbw2 < 1) tbw2 = 1;
                /* Tab bar click */
                if (my >= tby2 && my < tby2+16) {
                    int ti2 = (mx - w->x - 1) / tbw2;
                    if (ti2 >= 0 && ti2 < 4) { g_clock_tab = ti2; dirty=1; goto end_left_press; }
                }
                /* Alarm add action */
                if (g_clock_tab == 1) {
                    int content_y = w->y + TITLEBAR_H + 17;
                    int content_h = w->h - TITLEBAR_H - 17 - 18;
                    int add_y = content_y + 136 + (g_clock_extra_alarms > 0 ? 32 : 0);
                    if (add_y < content_y + content_h - 12 &&
                        mx>=w->x+8 && mx<w->x+110 && my>=add_y-2 && my<add_y+14) {
                        if (g_clock_extra_alarms < 99) g_clock_extra_alarms++;
                        toast_show("Clock","Alarm added",RGB(0,122,255));
                        dirty=1; goto end_left_press;
                    }
                }
                /* Timer start/stop */
                if (g_clock_tab == 2) {
                    int cy2b = w->y + TITLEBAR_H + 17 + (w->h - TITLEBAR_H - 17 - 18)/2;
                    int btn_y = cy2b - (w->h-TITLEBAR_H-17-18)/2 + 118;
                    if (mx>=w->x+w->w/2-28 && mx<=w->x+w->w/2+28 && my>=btn_y && my<btn_y+20) {
                        if (!g_timer_running) { g_timer_running=1; g_timer_start_tick=timer_ticks(); }
                        else g_timer_running=0;
                        dirty=1; goto end_left_press;
                    }
                }
                /* Stopwatch start/stop + reset */
                if (g_clock_tab == 3) {
                    int cy3 = w->y + TITLEBAR_H + 17;
                    int btn_y3 = cy3 + 88;
                    int rst_y3 = cy3 + 116;
                    if (mx>=w->x+w->w/2-28 && mx<=w->x+w->w/2+28 && my>=btn_y3 && my<btn_y3+20) {
                        if (!g_stopwatch_running) {
                            g_stopwatch_running=1; g_stopwatch_start=timer_ticks();
                        } else {
                            g_stopwatch_elapsed += timer_ticks() - g_stopwatch_start;
                            g_stopwatch_running=0;
                        }
                        dirty=1; goto end_left_press;
                    }
                    if (!g_stopwatch_running && mx>=w->x+w->w/2-20 && mx<=w->x+w->w/2+20 && my>=rst_y3 && my<rst_y3+18) {
                        g_stopwatch_elapsed=0; dirty=1; goto end_left_press;
                    }
                }
            }
            /* Activity Monitor tab clicks */
            { int ami;
              for (ami = 0; ami < g_num_windows; ami++) {
                  gui_window_t *w = &g_windows[ami];
                  if (!w->visible || !w->title || !str_eq(w->title,"Activity Monitor")) continue;
                  int am_taby = w->y + TITLEBAR_H + 1;
                  int am_tabw = (w->w-2)/4;
                  if (am_tabw < 1) am_tabw = 1;
                  if (my >= am_taby && my < am_taby+22) {
                      int ti_am = (mx - w->x - 1) / am_tabw;
                      if (ti_am >= 0 && ti_am < 4) { g_am_tab = ti_am; dirty=1; goto end_left_press; }
                  }
                  break;
              }
            }
            /* Finder folder click: navigate into folder (single click) */
            {
                static uint32_t last_click_tick = 0;
                static int last_click_fi = -1;
                for (i = 0; i < g_num_windows; i++) {
                    gui_window_t *w = &g_windows[i];
                    if (!w->visible || !w->title || !str_eq(w->title,"MyOS Finder")) continue;
                    if (i != top_win_idx) continue; /* only process if Finder is topmost */
                    int tbh2 = 28;
                    int sb_w = 90;
                    int fy0 = w->y + TITLEBAR_H + 1 + tbh2 + 1;
                    int fh2 = w->h - TITLEBAR_H - 19 - tbh2 - 1;
                    /* Back button in toolbar */
                    if (mx >= w->x+8 && mx < w->x+28 && my >= w->y+TITLEBAR_H+5 && my < w->y+TITLEBAR_H+23) {
                        if (g_finder_depth > 0) { g_finder_depth--; dirty=1; }
                        goto end_left_press;
                    }
                    /* Sidebar clicks */
                    if (mx >= w->x+2 && mx < w->x+sb_w && my >= fy0 && my < fy0+fh2) {
                        int rel = my - fy0;
                        if (rel>=28 && rel<40)  { /* Desktop */
                            g_finder_depth=0; dirty=1;
                        } else if (rel>=40 && rel<52) { /* Documents */
                            if (g_finder_depth==0) { g_finder_stack[0]="Documents"; g_finder_depth=1; dirty=1; }
                        } else if (rel>=52 && rel<64) { /* Downloads */
                            if (g_finder_depth==0) { g_finder_stack[0]="Downloads"; g_finder_depth=1; dirty=1; }
                        }
                        goto end_left_press;
                    }
                    /* Content area double-click detection */
                    int cx0 = w->x + sb_w + 1;
                    int cw2 = w->w - sb_w - 2;
                    if (mx >= cx0 && mx < cx0+cw2 && my >= fy0 && my < fy0+fh2) {
                        const char *finder_query_ev = gui_search_text(GUI_SEARCH_FINDER);
                        if (finder_query_ev && finder_query_ev[0]) {
                            int fcount_search = 0;
                            const folder_icon_t *cf_search = finder_current_folders(&fcount_search);
                            int row_search = (my - (fy0 + 28)) / 18;
                            int shown_search = 0;
                            int fi_search;
                            for (fi_search = 0; fi_search < fcount_search; fi_search++) {
                                if (!gui_search_matches(GUI_SEARCH_FINDER, cf_search[fi_search].name, 0))
                                    continue;
                                if (shown_search == row_search) {
                                    uint32_t click_now = timer_ticks();
                                    if (last_click_fi == fi_search && (click_now - last_click_tick) < 500 &&
                                        g_finder_depth < FINDER_DEPTH_MAX-1) {
                                        g_finder_stack[g_finder_depth] = cf_search[fi_search].name;
                                        g_finder_depth++;
                                        last_click_fi = -1;
                                    } else {
                                        last_click_fi = fi_search;
                                        last_click_tick = click_now;
                                    }
                                    dirty = 1;
                                    goto end_left_press;
                                }
                                shown_search++;
                            }
                            goto end_left_press;
                        }
                        int ic_w = (cw2 > 200) ? 100 : cw2/2;
                        int ic_h = 60;
                        int gx = cx0 + (cw2 - 2*ic_w)/2;
                        int gy = fy0 + 8;
                        int ri, ci2;
                        for (ri=0;ri<2;ri++) for (ci2=0;ci2<2;ci2++) {
                            int fi = ri*2+ci2;
                            int fx = gx + ci2*ic_w;
                            int fy = gy + ri*ic_h;
                            if (mx>=fx && mx<fx+ic_w && my>=fy && my<fy+ic_h) {
                                uint32_t click_now = timer_ticks();
                                /* Double-click: two clicks within 500ms on same item */
                                if (last_click_fi == fi && (click_now - last_click_tick) < 500) {
                                    int fcount2 = 0;
                                    const folder_icon_t *cf = finder_current_folders(&fcount2);
                                    if (fi < fcount2 && g_finder_depth < FINDER_DEPTH_MAX-1) {
                                        g_finder_stack[g_finder_depth] = cf[fi].name;
                                        g_finder_depth++;
                                        dirty = 1;
                                    }
                                    last_click_fi = -1;
                                } else {
                                    last_click_fi = fi;
                                    last_click_tick = click_now;
                                }
                                goto end_left_press;
                            }
                        }
                    }
                    break;
                }
            }
            /* Settings sidebar + toggle clicks */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible) continue;
                if (!w->title || !str_eq(w->title, "Settings")) continue;
                int cy_tab = w->y + TITLEBAR_H + 1;
                int sb_w3 = 130;
                /* Sidebar item clicks (below Apple ID block at +44px) */
                if (mx >= w->x+1 && mx < w->x+sb_w3) {
                    int rel = my - (cy_tab + 44);
                    if (rel >= 0) {
                        int ti3 = rel / 20;
                        if (ti3 >= 0 && ti3 < 14) { g_settings_tab = ti3; dirty=1; goto end_left_press; }
                    }
                }
                /* Content area toggles */
                int tx_r2 = w->x + w->w - 54;
                int th2 = 20, twid2 = 36;
                int cx3 = w->x + sb_w3 + 10;
                int cy3 = cy_tab + 10;
                if (g_settings_tab == 0) {
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+231&&my<cy3+231+th2){g_pref_wifi^=1;toast_show("Wi-Fi",g_pref_wifi?"On":"Off",RGB(0,122,255));dirty=1;}
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+251&&my<cy3+251+th2){g_pref_bt^=1;toast_show("Bluetooth",g_pref_bt?"On":"Off",RGB(0,122,255));dirty=1;}
                    {int wi3;for(wi3=0;wi3<5;wi3++){
                     int wx4=wi3<4?cx3+wi3*74:cx3, wy4=wi3<4?cy3+132:cy3+170;
                     if(mx>=wx4&&mx<wx4+62&&my>=wy4&&my<wy4+30){g_pref_wallpaper=wi3;dirty=1;}}}
                } else if (g_settings_tab == 1) {
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+41&&my<cy3+41+th2){g_pref_darkmode^=1;dirty=1;}
                    { int sai3; for (sai3=0; sai3<7; sai3++) {
                      int sax2=cx3+sai3*24+12, say2=cy3+126;
                      if (mx>=sax2-12 && mx<sax2+12 && my>=say2-12 && my<say2+12) {
                          g_pref_accent=sai3; toast_show("Appearance","Accent selected",RGB(0,122,255)); dirty=1; goto end_left_press;
                      } } }
                } else if (g_settings_tab == 2) {
                    { int sri2; for (sri2=0; sri2<4; sri2++) { int sry2=cy3+128+sri2*22;
                      if (mx>=cx3-2 && mx<cx3+w->w-sb_w3-20 && my>=sry2-2 && my<sry2+16) {
                          g_pref_resolution=sri2; toast_show("Display","Resolution selected",RGB(0,122,255)); dirty=1; goto end_left_press;
                      } } }
                } else if (g_settings_tab == 3) {
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+15&&my<cy3+15+th2){g_pref_notifs^=1;dirty=1;}
                } else if (g_settings_tab == 4) {
                    { int sai2; for (sai2=0; sai2<6; sai2++) { int sx2=cx3+(sai2%3)*86, sy2=cy3+76+(sai2/3)*28;
                      if (mx>=sx2 && mx<sx2+82 && my>=sy2 && my<sy2+24) {
                          g_pref_alert_sound=sai2; toast_show("Sound","Alert sound selected",RGB(0,122,255)); dirty=1; goto end_left_press;
                      } } }
                } else if (g_settings_tab == 5) {
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+15&&my<cy3+15+th2){g_pref_dnd^=1;dirty=1;}
                } else if (g_settings_tab == 6) {
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+227&&my<cy3+227+th2){g_pref_vpn^=1;toast_show("VPN",g_pref_vpn?"Connected":"Disconnected",RGB(0,122,255));dirty=1;}
                } else if (g_settings_tab == 7) {
                    if (mx>=tx_r2&&mx<tx_r2+twid2&&my>=cy3+15&&my<cy3+15+th2){g_pref_bt^=1;toast_show("Bluetooth",g_pref_bt?"On":"Off",RGB(0,122,255));dirty=1;}
                }
            }
            /* Button press */
            for (i = 0; i < g_num_buttons; i++) {
                if (g_buttons[i].win_idx < -1 ||
                    (g_buttons[i].win_idx >= 0 &&
                     (g_buttons[i].win_idx >= g_num_windows ||
                      !g_windows[g_buttons[i].win_idx].visible))) continue;
                if (gui_button_hit(&g_buttons[i], mx, my))
                    g_buttons[i].pressed = 1;
            }
            /* Window drag start (title bar area, avoid traffic lights x<62) */
            /* Also: resize start (bottom-right 16x16 corner) */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible || w->maximized) continue;
                /* Resize corner: bottom-right 16x16 */
                if (mx >= w->x + w->w - 16 && mx < w->x + w->w &&
                    my >= w->y + w->h - 16 && my < w->y + w->h) {
                    w->resizing = 1;
                    w->drag_x = mx; w->drag_y = my; /* store start pos */
                } else if (mx >= w->x + 62 && mx < w->x + w->w &&
                           my >= w->y      && my < w->y + TITLEBAR_H) {
                    w->dragging = 1;
                    w->drag_x   = mx - w->x;
                    w->drag_y   = my - w->y;
                }
            }
            /* Context menu item click */
            if (g_ctx_visible) {
                int item = ctx_menu_hit(mx, my);
                g_ctx_visible = 0;
                if (item >= 0) {
                    const char *lbl2 = g_ctx_labels[item];
                    if (str_eq(lbl2,"Get Info")) {
                        int fcount3 = 0;
                        char nbuf3[12];
                        char msg3[32];
                        int mp3 = 0;
                        int ni3 = 0;
                        (void)finder_current_folders(&fcount3);
                        int_to_str(fcount3, nbuf3);
                        while (nbuf3[ni3] && mp3 + 1 < (int)sizeof(msg3)) msg3[mp3++] = nbuf3[ni3++];
                        msg3[mp3++] = ' ';
                        msg3[mp3++] = 'i';
                        msg3[mp3++] = 't';
                        msg3[mp3++] = 'e';
                        msg3[mp3++] = 'm';
                        if (fcount3 != 1 && mp3 + 1 < (int)sizeof(msg3)) msg3[mp3++] = 's';
                        msg3[mp3] = 0;
                        toast_show("Finder", msg3, RGB(41,128,185));
                    } else if (str_eq(lbl2,"Change Wallpaper")) {
                        g_pref_wallpaper = (g_pref_wallpaper + 1) % 5;
                        const char *wnames[5]={"Blue","Sunset","Forest","Space","Sequoia"};
                        toast_show("Desktop",wnames[g_pref_wallpaper],RGB(0,122,255));
                    } else if (str_eq(lbl2,"Sort By Name")) {
                        toast_show("Finder","Icons sorted by name",RGB(41,128,185));
                    } else if (str_eq(lbl2,"Settings...")) {
                        int j2, found2=0;
                        for (j2=0; j2<g_num_windows; j2++) {
                            if (g_windows[j2].title && str_eq(g_windows[j2].title,"Settings"))
                                { g_windows[j2].visible=1; win_bring_to_front(j2); found2=1; break; }
                        }
                        if (!found2 && g_num_windows < MAX_WINDOWS) {
                            gui_window_t *sw2 = &g_windows[g_num_windows];
                            sw2->x=260;sw2->y=80;sw2->w=280;sw2->h=280;
                            sw2->title="Settings";sw2->visible=1;sw2->focused=0;
                            g_win_anim[g_num_windows]=OPEN_ANIM;
                            g_num_windows++;
                        }
                    } else if (str_eq(lbl2,"Widgets...")) {
                        g_widget_visible = !g_widget_visible;
                    } else if (str_eq(lbl2,"About MyOS")) {
                        /* Open About This Mac window */
                        int j_am, found_am=0;
                        for (j_am=0; j_am<g_num_windows; j_am++) {
                            if (g_windows[j_am].title && str_eq(g_windows[j_am].title,"About This Mac")) {
                                g_windows[j_am].visible=1; win_bring_to_front(j_am); found_am=1; break;
                            }
                        }
                        if (!found_am && g_num_windows < MAX_WINDOWS) {
                            gui_window_t *nw_am = &g_windows[g_num_windows++];
                            nw_am->x=VGA_WIDTH/2-130; nw_am->y=VGA_HEIGHT/2-130;
                            nw_am->w=260; nw_am->h=260;
                            nw_am->visible=1; nw_am->focused=0;
                            nw_am->title="About This Mac";
                            nw_am->space=g_current_space;
                        }
                    }
                    dirty = 1;
                }
            }
            /* Green maximize/restore button (x+48) */
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible) continue;
                int btn_cy = w->y + TITLEBAR_H / 2;
                int dx = mx - (w->x + 48), dy = my - btn_cy;
                if (dx*dx + dy*dy <= 36) {
                    if (!w->maximized) {
                        w->orig_x=w->x; w->orig_y=w->y; w->orig_w=w->w; w->orig_h=w->h;
                        w->x=0; w->y=MENUBAR_H; w->w=VGA_WIDTH; w->h=DOCK_Y-MENUBAR_H;
                        w->maximized=1;
                    } else {
                        w->x=w->orig_x; w->y=w->orig_y; w->w=w->orig_w; w->h=w->orig_h;
                        w->maximized=0;
                    }
                }
            }
            end_left_press:;
        }

        /* Left release */
        int released_drag_idx = -1;
        if (!(mb & MOUSE_LEFT) && (prev_btn & MOUSE_LEFT)) {
            for (i = 0; i < g_num_windows; i++) {
                if (g_windows[i].visible && g_windows[i].dragging && !g_windows[i].resizing) {
                    released_drag_idx = i;
                    break;
                }
            }
            for (i = 0; i < g_num_buttons; i++) {
                if (g_buttons[i].pressed) {
                    if (g_buttons[i].win_idx >= -1 &&
                        (g_buttons[i].win_idx < 0 ||
                         (g_buttons[i].win_idx < g_num_windows &&
                          g_windows[g_buttons[i].win_idx].visible)) &&
                        gui_button_hit(&g_buttons[i], mx, my) &&
                        g_buttons[i].on_click)
                        g_buttons[i].on_click(g_buttons[i].id);
                    g_buttons[i].pressed = 0;
                }
            }
            for (i = 0; i < g_num_windows; i++) {
                g_windows[i].dragging = 0;
                g_windows[i].resizing = 0;
            }
        }

        /* Window drag/resize motion */
        if (mb & MOUSE_LEFT) {
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                /* Resize motion */
                if (w->resizing) {
                    int dx = mx - w->drag_x;
                    int dy = my - w->drag_y;
                    int nw = w->w + dx, nh = w->h + dy;
                    int minw = w->min_w > 0 ? w->min_w : 150;
                    int minh = w->min_h > 0 ? w->min_h : 100;
                    if (nw < minw) nw = minw;
                    if (nh < minh) nh = minh;
                    if (w->x + nw > VGA_WIDTH)  nw = VGA_WIDTH  - w->x;
                    if (w->y + nh > DOCK_Y)     nh = DOCK_Y     - w->y;
                    w->w = nw; w->h = nh;
                    w->drag_x = mx; w->drag_y = my; /* update anchor */
                    dirty = 1;
                    continue;
                }
                if (!w->dragging) continue;
                int nx = mx - w->drag_x;
                int ny = my - w->drag_y;
                if (nx < 0)                 nx = 0;
                if (ny < MENUBAR_H)         ny = MENUBAR_H;
                if (nx + w->w > VGA_WIDTH)  nx = VGA_WIDTH  - w->w;
                if (ny + w->h > DOCK_Y)     ny = DOCK_Y     - w->h;
                if (nx != w->x || ny != w->y) {
                    w->x = nx; w->y = ny;
                    /* reposition buttons that belong to this window */
                    {
                        int bw2 = 82, bh2 = 22;
                        int total_btn2 = 4 * bw2 + 3 * 6;
                        int bx2 = nx + (w->w - total_btn2) / 2;
                        int by2 = ny + w->h - 18 - bh2 - 4;
                        int j;
                        for (j = 0; j < g_num_buttons; j++) {
                            if (g_buttons[j].win_idx == i) {
                                g_buttons[j].x = bx2 + j * (bw2 + 6);
                                g_buttons[j].y = by2;
                            }
                        }
                    }
                }
            }
        }

        /* Window snapping: cursor at screen edge on drag release → tile */
        if (!(mb & MOUSE_LEFT) && (prev_btn & MOUSE_LEFT) && released_drag_idx >= 0) {
            gui_window_t *w = &g_windows[released_drag_idx];
            if (w->visible && !w->maximized) {
                int snap_h = DOCK_Y - MENUBAR_H;
                if (mx < 28) {
                    /* Snap to left half */
                    w->orig_x=w->x; w->orig_y=w->y; w->orig_w=w->w; w->orig_h=w->h;
                    w->x=0; w->y=MENUBAR_H; w->w=VGA_WIDTH/2; w->h=snap_h;
                    w->maximized=0;
                    dirty=1;
                } else if (mx > VGA_WIDTH - 28) {
                    /* Snap to right half */
                    w->orig_x=w->x; w->orig_y=w->y; w->orig_w=w->w; w->orig_h=w->h;
                    w->x=VGA_WIDTH/2; w->y=MENUBAR_H; w->w=VGA_WIDTH/2; w->h=snap_h;
                    w->maximized=0;
                    dirty=1;
                } else if (my < MENUBAR_H + 8) {
                    /* Snap to full screen */
                    w->orig_x=w->x; w->orig_y=w->y; w->orig_w=w->w; w->orig_h=w->h;
                    w->x=0; w->y=MENUBAR_H; w->w=VGA_WIDTH; w->h=snap_h;
                    w->maximized=1; dirty=1;
                }
            }
        }

        /* Right press: context menu on desktop or close existing */
        if ((mb & MOUSE_RIGHT) && !(prev_btn & MOUSE_RIGHT)) {
            g_menu_open = -1; /* close any open dropdown */
            /* Check if click on window → don't show menu */
            int on_win = 0;
            for (i = 0; i < g_num_windows; i++) {
                gui_window_t *w = &g_windows[i];
                if (!w->visible) continue;
                if (mx>=w->x && mx<w->x+w->w && my>=w->y && my<w->y+w->h) { on_win=1; break; }
            }
            /* Minesweeper right-click: flag/unflag */
            { int ci_msr;
              for (ci_msr=g_num_windows-1; ci_msr>=0; ci_msr--) {
                gui_window_t *w=&g_windows[ci_msr];
                if (!w->visible||!w->title||!str_eq(w->title,"Minesweeper")) continue;
                if (g_mine_state==2||g_mine_state==3) break;
                int wxr=w->x, wyr=w->y, wwr=w->w;
                int hdr_yr=wyr+TITLEBAR_H+1, hdr_hr=30;
                int bxr=wxr+(wwr-MINE_COLS*20)/2;
                int byr=hdr_yr+hdr_hr+2;
                if (mx>=bxr&&mx<bxr+MINE_COLS*20&&my>=byr&&my<byr+MINE_ROWS*20) {
                    int cr3=(my-byr)/20, cc3=(mx-bxr)/20;
                    if (cr3<0||cr3>=MINE_ROWS||cc3<0||cc3>=MINE_COLS) break;
                    if (g_mine_vis[cr3][cc3]) break;
                    if (g_mine_flag[cr3][cc3]) {
                        g_mine_flag[cr3][cc3]=0; g_mine_remaining++;
                    } else {
                        g_mine_flag[cr3][cc3]=1; g_mine_remaining--;
                    }
                    on_win=1; dirty=1;
                }
                break;
              }
            }
            g_ctx_visible = 0;
            g_dock_ctx_visible = 0;
            /* Check dock right-click */
            if (my >= DOCK_Y && my < VGA_HEIGHT) {
                int num_dock2 = NUM_DOCK_ICONS;
                int total_dw2 = num_dock2*DOCK_ICON_SIZE+(num_dock2-1)*DOCK_ICON_PAD+24+24;
                int dix2 = (VGA_WIDTH - total_dw2) / 2;
                int diy2 = DOCK_Y + (DOCK_H - DOCK_ICON_SIZE)/2 - 4;
                int di2; int ix3 = dix2;
                for (di2 = 0; di2 < num_dock2; di2++) {
                    if (mx >= ix3 && mx < ix3 + DOCK_ICON_SIZE &&
                        my >= diy2 && my < diy2 + DOCK_ICON_SIZE) {
                        g_dock_ctx_icon    = di2;
                        g_dock_ctx_visible = 1;
                        dirty = 1;
                        goto end_right_press;
                    }
                    ix3 += DOCK_ICON_SIZE + DOCK_ICON_PAD;
                    if (di2 == 5 || di2 == 11 || di2 == 15) ix3 += 8;
                }
            }
            if (!on_win && my > MENUBAR_H && my < DOCK_Y) {
                g_ctx_x = mx; g_ctx_y = my;
                /* Clamp to screen */
                if (g_ctx_x + CTX_MENU_W > VGA_WIDTH)  g_ctx_x = VGA_WIDTH - CTX_MENU_W - 2;
                if (g_ctx_y + CTX_MENU_ITEMS*CTX_MENU_ITEM_H + 4 > DOCK_Y)
                    g_ctx_y = DOCK_Y - CTX_MENU_ITEMS*CTX_MENU_ITEM_H - 6;
                g_ctx_visible = 1;
            }
            end_right_press:;
        }
        /* Keyboard */
        {
            int ch = keyboard_poll();
            while (ch != KEY_NONE) {
                /* Any key wakes screensaver / lock */
                g_last_input_tick = timer_ticks();
                if (g_locked) {
                    if (ch == KEY_ENTER) {
                        g_locked = 0; g_lock_pw_len = 0; g_lock_pw[0] = 0;
                    } else if (ch == KEY_BACKSPACE && g_lock_pw_len > 0) {
                        g_lock_pw[--g_lock_pw_len] = 0;
                    } else if (ch >= 0x20 && ch < 0x7F && g_lock_pw_len < 8) {
                        g_lock_pw[g_lock_pw_len++] = (char)ch;
                        g_lock_pw[g_lock_pw_len] = 0;
                    }
                    dirty = 1; ch = keyboard_poll(); continue;
                }
                if (g_saver_on) {
                    if (ch == ' ') {
                        /* Space cycles screensaver mode instead of waking */
                        g_saver_mode = (g_saver_mode + 1) % 3;
                    } else {
                        g_saver_on = 0;
                    }
                    dirty = 1; ch = keyboard_poll(); continue;
                }
                /* Mail compose pre-chain */
                if (g_mail_compose && g_mail_focused_field > 0 && ch != KEY_ESC) {
                    char *mfld = NULL; int *mlen = NULL; int mmax = 0;
                    if      (g_mail_focused_field==1){mfld=g_mail_to;      mlen=&g_mail_to_len;      mmax=63;}
                    else if (g_mail_focused_field==2){mfld=g_mail_subject; mlen=&g_mail_subject_len; mmax=63;}
                    else if (g_mail_focused_field==3){mfld=g_mail_body;    mlen=&g_mail_body_len;    mmax=255;}
                    if (mfld) {
                        if (ch=='\b'||ch==0x7F) {
                            if(*mlen>0){(*mlen)--;mfld[*mlen]=0;dirty=1;}
                        } else if ((ch=='\r'||ch=='\n')&&g_mail_focused_field==3) {
                            if(*mlen<mmax){mfld[(*mlen)++]='\n';mfld[*mlen]=0;dirty=1;}
                        } else if ((ch=='\r'||ch=='\n')&&g_mail_focused_field<3) {
                            g_mail_focused_field++; dirty=1;
                        } else if (ch=='\t') {
                            if(g_mail_focused_field<3){g_mail_focused_field++;dirty=1;}
                        } else if (ch>=0x20&&ch<0x7F&&*mlen<mmax) {
                            mfld[(*mlen)++]=(char)ch; mfld[*mlen]=0; dirty=1;
                        }
                    }
                    ch=keyboard_poll(); continue;
                }
                /* Focused text fields intercept before global shortcuts (backspace etc.) */
                if (g_dict_focused && ch != KEY_ESC) {
                    if (ch == '\b' || ch == 0x7F) {
                        if (g_dict_input_len > 0) { g_dict_input_len--; g_dict_input[g_dict_input_len]=0; dirty=1; }
                    } else if (ch >= 0x20 && ch < 0x7F && g_dict_input_len < 30) {
                        g_dict_input[g_dict_input_len++] = (char)ch;
                        g_dict_input[g_dict_input_len] = 0;
                        dirty=1;
                    }
                    ch = keyboard_poll(); continue;
                }
                if (g_notes_focused && ch != KEY_ESC) {
                    if (g_notes_sel == NOTES_COUNT-1) {
                        int nb_len = 0;
                        while (g_notes_body[NOTES_COUNT-1][nb_len]) nb_len++;
                        if (ch == '\b' || ch == 0x7F) {
                            if (nb_len > 0) { g_notes_body[NOTES_COUNT-1][nb_len-1]=0; dirty=1; }
                        } else if (ch == '\r' || ch == '\n') {
                            if (nb_len < NOTES_MAXLEN-1) { g_notes_body[NOTES_COUNT-1][nb_len]='\n'; g_notes_body[NOTES_COUNT-1][nb_len+1]=0; dirty=1; }
                        } else if (ch >= 0x20 && ch < 0x7F && nb_len < NOTES_MAXLEN-1) {
                            g_notes_body[NOTES_COUNT-1][nb_len]=(char)ch;
                            g_notes_body[NOTES_COUNT-1][nb_len+1]=0;
                            dirty=1;
                        }
                    }
                    ch = keyboard_poll(); continue;
                }
                {
                    int search_owner = gui_focused_search_owner();
                    if (search_owner && ch == KEY_ESC) {
                        gui_clear_search_focus(search_owner);
                        dirty = 1;
                        ch = keyboard_poll();
                        continue;
                    }
                    if (search_owner && (ch == KEY_ENTER || ch == '\r' || ch == '\n')) {
                        const char *query = gui_search_text(search_owner);
                        toast_show(gui_search_owner_name(search_owner),
                                   query && query[0] ? query : "Search ready",
                                   RGB(0,122,255));
                        dirty = 1;
                        ch = keyboard_poll();
                        continue;
                    }
                    if (search_owner && gui_search_handle_key(search_owner, ch)) {
                        dirty = 1;
                        ch = keyboard_poll();
                        continue;
                    }
                }
                if (gui_top_window_named("Safari") && g_safari_form_focused >= 0 && !g_safari_url_focused && ch != KEY_ESC) {
                    int sf = g_safari_form_focused;
                    if (sf >= g_safari_form_count) {
                        g_safari_form_focused = -1;
                    } else if (ch == KEY_ENTER || ch == '\r' || ch == '\n') {
                        safari_submit_form(sf);
                        toast_show("Safari", g_safari_page_status, RGB(40,160,220));
                        dirty = 1;
                    } else if (ch == '\t') {
                        if (g_safari_form_count > 0)
                            g_safari_form_focused = (sf + 1) % g_safari_form_count;
                        dirty = 1;
                    } else if (ch == KEY_BACKSPACE || ch == '\b' || ch == 0x7F) {
                        int flen = str_len(g_safari_form_values[sf]);
                        if (flen > 0) {
                            g_safari_form_values[sf][flen - 1] = 0;
                            dirty = 1;
                        }
                    } else if (ch >= 0x20 && ch < 0x7F && g_safari_form_input_names[sf][0]) {
                        int flen = str_len(g_safari_form_values[sf]);
                        if (flen + 1 < SAFARI_FORM_VALUE_MAX) {
                            g_safari_form_values[sf][flen] = (char)ch;
                            g_safari_form_values[sf][flen + 1] = 0;
                            dirty = 1;
                        }
                    }
                    ch = keyboard_poll(); continue;
                }
                /* Wordle pre-chain: consume ALL input when Wordle window is topmost */
                g_wordle_focused = gui_top_window_named("Wordle") ? 1 : 0;
                if (g_wordle_focused && ch != KEY_ESC) {
                    if ((ch == 'r' || ch == 'R') && g_wordle_state != 0) {
                        /* Restart (only when game over: won or lost) */
                        int wi=0; while(g_wordle_words[wi])wi++;
                        if (wi > 0) {
                            g_wordle_answer_idx=(g_wordle_answer_idx+1)%wi;
                        } else {
                            g_wordle_answer_idx=0;
                        }
                        int i2,j2;
                        for(i2=0;i2<WORDLE_ROWS;i2++){
                            for(j2=0;j2<WORDLE_COLS;j2++){g_wordle_guesses[i2][j2]=0;g_wordle_results[i2][j2]=0;}
                            g_wordle_guesses[i2][WORDLE_COLS]=0;
                        }
                        for(i2=0;i2<26;i2++) g_wordle_kb_state[i2]=0;
                        g_wordle_cur_row=0; g_wordle_cur_col=0; g_wordle_state=0;
                        dirty=1;
                    } else if (g_wordle_state==0) {
                        if (((ch>='A'&&ch<='Z') || (ch>='a'&&ch<='z')) && g_wordle_cur_col<WORDLE_COLS) {
                            char lc2=(ch>='a'&&ch<='z')?(char)(ch-32):(char)ch;
                            g_wordle_guesses[g_wordle_cur_row][g_wordle_cur_col]=lc2;
                            g_wordle_cur_col++;
                            dirty=1;
                        } else if (ch=='\b'||ch==0x7F||ch==0x08) {
                            if (g_wordle_cur_col>0){g_wordle_cur_col--;g_wordle_guesses[g_wordle_cur_row][g_wordle_cur_col]=0;dirty=1;}
                        } else if ((ch==KEY_ENTER||ch=='\r'||ch=='\n') && g_wordle_cur_col==WORDLE_COLS) {
                            const char *ans2=g_wordle_words[g_wordle_answer_idx];
                            if (!ans2) ans2=g_wordle_words[0];
                            if (!ans2) { ch = keyboard_poll(); continue; }
                            char *guess2=g_wordle_guesses[g_wordle_cur_row];
                            int ci2, ci3;
                            for(ci2=0;ci2<WORDLE_COLS;ci2++) {
                                if(guess2[ci2]==ans2[ci2]) g_wordle_results[g_wordle_cur_row][ci2]=3;
                                else g_wordle_results[g_wordle_cur_row][ci2]=1;
                            }
                            for(ci2=0;ci2<WORDLE_COLS;ci2++) {
                                if(g_wordle_results[g_wordle_cur_row][ci2]==3) continue;
                                for(ci3=0;ci3<WORDLE_COLS;ci3++) {
                                    if(ci3==ci2) continue;
                                    if(g_wordle_results[g_wordle_cur_row][ci3]!=3&&guess2[ci2]==ans2[ci3]){
                                        g_wordle_results[g_wordle_cur_row][ci2]=2; break;
                                    }
                                }
                            }
                            for(ci2=0;ci2<WORDLE_COLS;ci2++) {
                                int ki2=guess2[ci2]-'A';
                                int rs2=g_wordle_results[g_wordle_cur_row][ci2];
                                if(ki2>=0&&ki2<26&&rs2>g_wordle_kb_state[ki2]) g_wordle_kb_state[ki2]=rs2;
                            }
                            int won2=1;
                            for(ci2=0;ci2<WORDLE_COLS;ci2++) if(g_wordle_results[g_wordle_cur_row][ci2]!=3){won2=0;break;}
                            g_wordle_cur_row++;
                            g_wordle_cur_col=0;
                            if(won2) g_wordle_state=1;
                            else if(g_wordle_cur_row>=WORDLE_ROWS) g_wordle_state=2;
                            dirty=1;
                        }
                    }
                    ch = keyboard_poll(); continue;
                }
                /* Spotlight takes priority over global shortcuts (handles Enter, chars, etc.) */
                if (g_spot_visible && ch != KEY_ESC &&
                    !(ch >= KEY_CTRL_DIGIT_BASE + 1 && ch <= KEY_CTRL_DIGIT_BASE + 9) &&
                    !(ch >= 0x01 && ch <= 0x07) &&
                    !(ch >= 0x0B && ch <= 0x1F && ch != KEY_ENTER && ch != KEY_BACKSPACE && ch != KEY_UP && ch != KEY_DOWN)) {
                    /* Spotlight input — all keys except ESC and ctrl shortcuts */
                    if (ch == KEY_UP) {
                        if (g_spot_sel > 0) g_spot_sel--;
                        dirty = 1; ch = keyboard_poll(); continue;
                    } else if (ch == KEY_DOWN) {
                        g_spot_sel++;
                        dirty = 1; ch = keyboard_poll(); continue;
                    } else if (ch == KEY_ENTER && g_spot_qlen > 0) {
                        /* Open selected or first matching app */
                        int ri_s = 0; int hit_idx = 0;
                        while (g_spot_apps[ri_s]) {
                            int ms2 = spot_substr_match(g_spot_apps[ri_s], g_spot_query, g_spot_qlen);
                            if (ms2) {
                                if (hit_idx == g_spot_sel) {
                                    const char *an2 = g_spot_apps[ri_s];
                                    int js2, fs2=0;
                                    for (js2=0;js2<g_num_windows;js2++) {
                                        const char *wt2 = g_windows[js2].title;
                                        if (wt2 && str_eq(wt2,an2)) {
                                            g_windows[js2].visible=1; win_bring_to_front(js2); fs2=1;
                                            if (str_eq(an2,"Wordle")) { g_wordle_focused=1; }
                                            if (str_eq(an2,"Dictionary")) { g_dict_focused=1; g_edit_focused=0; }
                                            if (str_eq(an2,"TextEdit")) { g_edit_focused=1; g_dict_focused=0; }
                                            break;
                                        }
                                    }
                                    if (!fs2 && g_num_windows < MAX_WINDOWS) {
                                        gui_window_t *nws2 = &g_windows[g_num_windows];
                                        nws2->focused=0; nws2->dragging=0; nws2->visible=1; nws2->maximized=0;
                                        if (str_eq(an2,"Clock"))            { nws2->x=50; nws2->y=80; nws2->w=180; nws2->h=220; }
                                        else if (str_eq(an2,"Calculator"))  { nws2->x=180;nws2->y=100;nws2->w=220;nws2->h=280; }
                                        else if (str_eq(an2,"Settings"))    { nws2->x=150;nws2->y=50; nws2->w=500;nws2->h=400; }
                                        else if (str_eq(an2,"TextEdit"))    { nws2->x=120;nws2->y=80; nws2->w=310;nws2->h=260; g_edit_focused=1; }
                                        else if (str_eq(an2,"Terminal"))    { nws2->x=100;nws2->y=100;nws2->w=290;nws2->h=220; }
                                        else if (str_eq(an2,"Safari"))      { nws2->x=60; nws2->y=50; nws2->w=480;nws2->h=380; }
                                        else if (str_eq(an2,"Music"))       { nws2->x=80; nws2->y=55; nws2->w=280;nws2->h=340; }
                                        else if (str_eq(an2,"Photos"))      { nws2->x=70; nws2->y=50; nws2->w=420;nws2->h=340; }
                                        else if (str_eq(an2,"Maps"))        { nws2->x=80; nws2->y=55; nws2->w=400;nws2->h=320; }
                                        else if (str_eq(an2,"App Store"))   { nws2->x=70; nws2->y=45; nws2->w=440;nws2->h=360; }
                                        else if (str_eq(an2,"Mail"))        { nws2->x=80; nws2->y=50; nws2->w=420;nws2->h=350; }
                                        else if (str_eq(an2,"Calendar"))    { nws2->x=90; nws2->y=55; nws2->w=400;nws2->h=340; }
                                        else if (str_eq(an2,"Notes"))       { nws2->x=90; nws2->y=60; nws2->w=300;nws2->h=320; }
                                        else if (str_eq(an2,"Finder"))      { nws2->x=80; nws2->y=50; nws2->w=420;nws2->h=320; }
                                        else if (str_eq(an2,"Activity Monitor")) { nws2->x=140;nws2->y=80; nws2->w=320;nws2->h=270; }
                                        else if (str_eq(an2,"System Info")) { nws2->x=180;nws2->y=100;nws2->w=280;nws2->h=220; }
                                        else if (str_eq(an2,"Color Picker"))        { nws2->x=220;nws2->y=80; nws2->w=220;nws2->h=310; }
                                        else if (str_eq(an2,"Script Editor"))       { nws2->x=130;nws2->y=70; nws2->w=340;nws2->h=290; }
                                        else if (str_eq(an2,"Migration Assistant")) { nws2->x=170;nws2->y=80; nws2->w=300;nws2->h=290; }
                                        else if (str_eq(an2,"Screen Time"))        { nws2->x=160;nws2->y=60; nws2->w=320;nws2->h=320; }
                                        else if (str_eq(an2,"Passwords"))          { nws2->x=120;nws2->y=60; nws2->w=380;nws2->h=300; }
                                        else if (str_eq(an2,"Numbers"))            { nws2->x=100;nws2->y=55; nws2->w=400;nws2->h=300; }
                                        else if (str_eq(an2,"Focus"))              { nws2->x=200;nws2->y=65; nws2->w=280;nws2->h=340; }
                                        else if (str_eq(an2,"1Password"))          { nws2->x=100;nws2->y=50; nws2->w=420;nws2->h=320; }
                                        else if (str_eq(an2,"Fantastical"))        { nws2->x=120;nws2->y=55; nws2->w=380;nws2->h=310; }
                                        else if (str_eq(an2,"Things 3"))           { nws2->x=110;nws2->y=55; nws2->w=400;nws2->h=310; }
                                        else if (str_eq(an2,"Raycast"))            { nws2->x=150;nws2->y=65; nws2->w=340;nws2->h=260; }
                                        else if (str_eq(an2,"Tot"))                { nws2->x=160;nws2->y=70; nws2->w=300;nws2->h=250; }
                                        else if (str_eq(an2,"Klokki"))             { nws2->x=140;nws2->y=65; nws2->w=320;nws2->h=280; }
                                        else if (str_eq(an2,"Bear"))               { nws2->x=100;nws2->y=50; nws2->w=420;nws2->h=310; }
                                        else if (str_eq(an2,"Reeder 5"))           { nws2->x=100;nws2->y=50; nws2->w=420;nws2->h=310; }
                                        else if (str_eq(an2,"CleanMyMac X"))       { nws2->x=160;nws2->y=55; nws2->w=300;nws2->h=310; }
                                        else if (str_eq(an2,"Bartender 4"))        { nws2->x=120;nws2->y=60; nws2->w=380;nws2->h=280; }
                                        else if (str_eq(an2,"Alfred"))             { nws2->x=130;nws2->y=65; nws2->w=360;nws2->h=280; }
                                        else if (str_eq(an2,"Scrobbles"))          { nws2->x=180;nws2->y=70; nws2->w=280;nws2->h=290; }
                                        else if (str_eq(an2,"Keyboard Shortcuts")) { nws2->x=90;nws2->y=40; nws2->w=620;nws2->h=500; }
                                        else if (str_eq(an2,"iStudiez Pro"))       { nws2->x=110;nws2->y=55; nws2->w=400;nws2->h=280; }
                                        else if (str_eq(an2,"Lasso"))              { nws2->x=150;nws2->y=60; nws2->w=340;nws2->h=270; }
                                        else if (str_eq(an2,"Dictionary"))         { nws2->x=180;nws2->y=80; nws2->w=300;nws2->h=220; g_dict_focused=1; g_dict_input_len=0; g_dict_input[0]=0; }
                                        else if (str_eq(an2,"Chess"))              { nws2->x=170;nws2->y=70; nws2->w=240;nws2->h=240; }
                                        else if (str_eq(an2,"2048"))               { nws2->x=150;nws2->y=60; nws2->w=240;nws2->h=280; g2048_new_game(); }
                                        else if (str_eq(an2,"Health"))             { nws2->x=100;nws2->y=45; nws2->w=280;nws2->h=380; }
                                        else if (str_eq(an2,"Sudoku"))             { nws2->x=90; nws2->y=40; nws2->w=260;nws2->h=320; g_sdk_started=0;g_sdk_errors=0;g_sdk_sel_r=-1;g_sdk_sel_c=-1; }
                                        else if (str_eq(an2,"Photo Booth"))        { nws2->x=140;nws2->y=60; nws2->w=320;nws2->h=280; }
                                        else if (str_eq(an2,"Stickies"))           { nws2->x=160;nws2->y=80; nws2->w=280;nws2->h=220; }
                                        else if (str_eq(an2,"Snake"))              { nws2->x=120;nws2->y=45; nws2->w=316;nws2->h=252; toast_show("Snake","Space=start, Arrows=move",RGB(52,199,89)); }
                                        else if (str_eq(an2,"Wordle"))             { nws2->x=150;nws2->y=40; nws2->w=280;nws2->h=380; { int i2,j2; for(i2=0;i2<WORDLE_ROWS;i2++){for(j2=0;j2<WORDLE_COLS;j2++){g_wordle_guesses[i2][j2]=0;g_wordle_results[i2][j2]=0;} g_wordle_guesses[i2][WORDLE_COLS]=0;} for(i2=0;i2<26;i2++)g_wordle_kb_state[i2]=0; g_wordle_cur_row=0;g_wordle_cur_col=0;g_wordle_state=0; g_wordle_answer_idx=0; g_wordle_focused=1; } toast_show("Wordle","Type 5-letter word, Enter to guess",RGB(108,169,100)); }
                                        else if (str_eq(an2,"Breakout"))           { nws2->x=100;nws2->y=40; nws2->w=320;nws2->h=280; toast_show("Breakout","Space=start, A/D=move",RGB(100,180,255)); }
                                        else if (str_eq(an2,"Pong"))               { nws2->x=120;nws2->y=40; nws2->w=300;nws2->h=260; toast_show("Pong","Space=start, W/S=move",RGB(255,180,50)); g_pong_active=0;g_pong_over=0;g_pong_score_p=0;g_pong_score_a=0; }
                                        else if (str_eq(an2,"Minesweeper")) { nws2->x=200;nws2->y=60;nws2->w=200;nws2->h=264; g_mine_state=0;g_mine_remaining=MINE_COUNT;g_mine_rng=1; { int _r,_c; for(_r=0;_r<MINE_ROWS;_r++)for(_c=0;_c<MINE_COLS;_c++){g_mine_board[_r][_c]=0;g_mine_vis[_r][_c]=0;g_mine_flag[_r][_c]=0;} } toast_show("Minesweeper","Left=reveal, Right=flag",RGB(80,80,200)); }
                                        else if (str_eq(an2,"Journal"))     { nws2->x=110;nws2->y=50; nws2->w=380;nws2->h=340; g_journal_sel=0; g_journal_focused=0; toast_show("Journal","Select an entry to read",RGB(255,149,0)); }
                                        else if (str_eq(an2,"Contacts"))   { nws2->x=150;nws2->y=50; nws2->w=420;nws2->h=320; g_ct_sel=0; toast_show("Contacts","Your address book",RGB(0,122,255)); }
                                        else if (str_eq(an2,"Preview"))    { nws2->x=160;nws2->y=60; nws2->w=380;nws2->h=300; g_preview_page=0; g_preview_zoom=100; g_preview_markup=0; toast_show("Preview","Open documents and images",RGB(170,50,170)); }
                                        else if (str_eq(an2,"Apple TV"))   { nws2->x=120;nws2->y=50; nws2->w=400;nws2->h=300; g_atv_sel=0; toast_show("Apple TV","Stream movies and TV shows",RGB(255,255,255)); }
                                        else { nws2->x=120;nws2->y=80;nws2->w=280;nws2->h=200; }
                                        nws2->title = an2;
                                        g_win_anim[g_num_windows] = OPEN_ANIM;
                                        g_num_windows++;
                                        toast_show(an2, "Opened via Spotlight", RGB(0,122,255));
                                    }
                                    break;
                                }
                                hit_idx++;
                            }
                            ri_s++;
                        }
                        g_spot_visible=0; g_spot_qlen=0; g_spot_query[0]=0; g_spot_sel=0;
                        dirty=1;
                    } else if (ch == KEY_BACKSPACE) {
                        if (g_spot_qlen > 0) { g_spot_query[--g_spot_qlen] = 0; g_spot_sel=0; }
                        dirty = 1;
                    } else if (ch >= 0x20 && ch < 0x7F && g_spot_qlen < SPOT_QUERY_MAX) {
                        g_spot_query[g_spot_qlen++] = (char)ch;
                        g_spot_query[g_spot_qlen] = 0;
                        g_spot_sel = 0; /* reset selection on new char */
                        dirty = 1;
                    }
                    ch = keyboard_poll(); continue;
                }
                if (ch == KEY_ESC) {
                    if (g_mail_compose) {
                        g_mail_compose = 0; g_mail_focused_field = 0; dirty = 1;
                    } else if (g_photos_fullscreen) {
                        g_photos_fullscreen = 0; dirty = 1;
                    } else if (g_safari_url_focused || g_safari_form_focused >= 0) {
                        g_safari_url_focused = 0; g_safari_form_focused = -1; dirty = 1;
                    } else if (g_ctx_visible) {
                        g_ctx_visible = 0; dirty = 1;
                    } else if (g_dock_ctx_visible) {
                        g_dock_ctx_visible = 0; dirty = 1;
                    } else if (g_menu_open >= 0) {
                        g_menu_open = -1; dirty = 1;
                    } else if (g_print_visible) {
                        g_print_visible = 0; dirty = 1;
                    } else if (g_share_visible) {
                        g_share_visible = 0; dirty = 1;
                    } else if (g_crash_visible) {
                        g_crash_visible = 0; dirty = 1;
                    } else if (g_update_visible) {
                        g_update_visible = 0; dirty = 1;
                    } else if (g_focus_filter_visible) {
                        g_focus_filter_visible = 0; dirty = 1;
                    } else if (g_icloud_visible) {
                        g_icloud_visible = 0; dirty = 1;
                    } else if (g_bt_visible) {
                        g_bt_visible = 0; dirty = 1;
                    } else if (g_kbshort_visible) {
                        g_kbshort_visible = 0; dirty = 1;
                    } else if (g_timemachine_visible) {
                        g_timemachine_visible = 0; dirty = 1;
                    } else if (g_colormeter_visible) {
                        g_colormeter_visible = 0; dirty = 1;
                    } else if (g_notifhist_visible) {
                        g_notifhist_visible = 0; dirty = 1;
                    } else if (g_wifi_visible) {
                        g_wifi_visible = 0; dirty = 1;
                    } else if (g_display_visible) {
                        g_display_visible = 0; dirty = 1;
                    } else if (g_sound_visible) {
                        g_sound_visible = 0; dirty = 1;
                    } else if (g_actmon_visible) {
                        g_actmon_visible = 0; dirty = 1;
                    } else if (g_facetime_visible) {
                        g_facetime_visible = 0; dirty = 1;
                    } else if (g_privacy_visible) {
                        g_privacy_visible = 0; dirty = 1;
                    } else if (g_reminders_visible) {
                        g_reminders_visible = 0; dirty = 1;
                    } else if (g_calendar_visible) {
                        g_calendar_visible = 0; dirty = 1;
                    } else if (g_airplay_visible) {
                        g_airplay_visible = 0; dirty = 1;
                    } else if (g_airdrop_visible) {
                        g_airdrop_visible = 0; dirty = 1;
                    } else if (g_ql_visible) {
                        g_ql_visible = 0; dirty = 1;
                    } else if (g_widget_visible) {
                        g_widget_visible = 0; dirty = 1;
                    } else if (g_switcher_visible) {
                        g_switcher_visible = 0; dirty = 1;
                    } else if (g_lp_visible) {
                        if (g_lp_slen > 0) { g_lp_slen=0; g_lp_search[0]=0; dirty=1; }
                        else { g_lp_visible=0; g_lp_page=0; dirty=1; }
                    } else if (g_expose_visible) {
                        g_expose_visible = 0; dirty = 1;
                    } else if (g_mc_visible) {
                        g_mc_visible = 0; dirty = 1;
                    } else if (g_spot_visible) {
                        g_spot_visible = 0; g_spot_qlen = 0; g_spot_query[0] = 0; g_spot_sel = 0;
                        dirty = 1;
                    } else if (g_wt_visible) {
                        g_wt_visible = 0; dirty = 1;
                    } else if (g_qn_visible) {
                        g_qn_visible = 0; dirty = 1;
                    } else if (g_nc_visible || g_cc_visible) {
                        g_nc_visible = 0; g_cc_visible = 0; dirty = 1;
                    } else {
                        /* Close topmost visible window, or do nothing */
                        int top = win_top_visible();
                        if (top >= 0) {
                            win_close(top);
                            dirty = 1;
                        }
                    }
                } else if (ch == 0x0B) { /* Ctrl+K = Lock screen */
                    g_locked = 1; g_lock_pw_len = 0; g_lock_pw[0] = 0; dirty = 1;
                } else if (ch == 0x0C) { /* Ctrl+L = toggle Launchpad */
                    if (!g_mc_visible) { g_lp_visible = !g_lp_visible; g_lp_page=0; g_lp_slen=0; g_lp_search[0]=0; dirty = 1; }
                } else if (ch == 0x0D) { /* Ctrl+M = toggle Mission Control (0x0D != KEY_ENTER=0x0A) */
                    if (!g_lp_visible) { g_mc_visible = !g_mc_visible; dirty = 1; }
                } else if (ch == 0x0E) { /* Ctrl+N = toggle Notification Center */
                    g_nc_visible = !g_nc_visible; g_cc_visible = 0; dirty = 1;
                } else if (ch == 0x0F) { /* Ctrl+O = Podcasts */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Podcasts"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwpc = &g_windows[g_num_windows];
                          nwpc->x=160;nwpc->y=70;nwpc->w=280;nwpc->h=260;
                          nwpc->title="Podcasts";nwpc->visible=1;nwpc->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x05) { /* Ctrl+E = Reminders */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Reminders"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwrm = &g_windows[g_num_windows];
                          nwrm->x=200;nwrm->y=80;nwrm->w=260;nwrm->h=240;
                          nwrm->title="Reminders";nwrm->visible=1;nwrm->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x17) { /* Ctrl+W = Weather */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Weather"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwwt = &g_windows[g_num_windows];
                          nwwt->x=180;nwwt->y=70;nwwt->w=260;nwwt->h=280;
                          nwwt->title="Weather";nwwt->visible=1;nwwt->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x06) { /* Ctrl+F = FaceTime */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"FaceTime"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwft = &g_windows[g_num_windows];
                          nwft->x=200;nwft->y=80;nwft->w=220;nwft->h=260;
                          nwft->title="FaceTime";nwft->visible=1;nwft->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                          g_facetime_active=0; g_facetime_contact=0;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x12) { /* Ctrl+R = AirDrop */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"AirDrop"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwad = &g_windows[g_num_windows];
                          nwad->x=240;nwad->y=100;nwad->w=240;nwad->h=220;
                          nwad->title="AirDrop";nwad->visible=1;nwad->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x07) { /* Ctrl+G = toggle Stage Manager */
                    g_stage_manager = !g_stage_manager; dirty = 1;
                } else if (ch == 0x04) { /* Ctrl+D = toggle Dark Mode */
                    g_pref_darkmode = !g_pref_darkmode; dirty = 1;
                    toast_show("Appearance", g_pref_darkmode?"Dark Mode On":"Light Mode On",
                               g_pref_darkmode?RGB(30,30,32):RGB(220,220,220));
                } else if (ch == 0x13) { /* Ctrl+S = open/raise Settings */
                    { int j2, found3=0;
                      for (j2=0;j2<g_num_windows;j2++) {
                          if (g_windows[j2].title && str_eq(g_windows[j2].title,"Settings"))
                              { g_windows[j2].visible=1; win_bring_to_front(j2); found3=1; break; }
                      }
                      if (!found3 && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *sw3 = &g_windows[g_num_windows];
                          sw3->x=150;sw3->y=50;sw3->w=500;sw3->h=400;
                          sw3->title="Settings";sw3->visible=1;sw3->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x02) { /* Ctrl+B = toggle Control Center */
                    g_cc_visible = !g_cc_visible; g_nc_visible = 0; dirty = 1;
                } else if (ch == 0x19) { /* Ctrl+Y = open/raise Calendar */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Calendar"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwc = &g_windows[g_num_windows];
                          nwc->x=200;nwc->y=80;nwc->w=300;nwc->h=260;
                          nwc->title="Calendar";nwc->visible=1;nwc->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x1A) { /* Ctrl+Z = open/raise Mail */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Mail"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwm = &g_windows[g_num_windows];
                          nwm->x=100;nwm->y=80;nwm->w=360;nwm->h=260;
                          nwm->title="Mail";nwm->visible=1;nwm->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x01) { /* Ctrl+A = open/raise Activity Monitor */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Activity Monitor"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwam = &g_windows[g_num_windows];
                          nwam->x=140;nwam->y=80;nwam->w=320;nwam->h=270;
                          nwam->title="Activity Monitor";nwam->visible=1;nwam->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x10) { /* Ctrl+P = open System Information */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"System Info"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwsi = &g_windows[g_num_windows];
                          nwsi->x=180;nwsi->y=100;nwsi->w=280;nwsi->h=220;
                          nwsi->title="System Info";nwsi->visible=1;nwsi->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0xF8) { /* F8 = Keyboard Shortcuts */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Keyboard Shortcuts"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwks2 = &g_windows[g_num_windows];
                          nwks2->x=90;nwks2->y=40;nwks2->w=620;nwks2->h=500;
                          nwks2->title="Keyboard Shortcuts";nwks2->visible=1;nwks2->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x14) { /* Ctrl+T = Stocks */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Stocks"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwst = &g_windows[g_num_windows];
                          nwst->x=140;nwst->y=60;nwst->w=320;nwst->h=280;
                          nwst->title="Stocks";nwst->visible=1;nwst->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x18) { /* Ctrl+X = News */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"News"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwnw = &g_windows[g_num_windows];
                          nwnw->x=150;nwnw->y=70;nwnw->w=340;nwnw->h=290;
                          nwnw->title="News";nwnw->visible=1;nwnw->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x16) { /* Ctrl+V = Maps */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Maps"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwmp = &g_windows[g_num_windows];
                          nwmp->x=120;nwmp->y=60;nwmp->w=360;nwmp->h=300;
                          nwmp->title="Maps";nwmp->visible=1;nwmp->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x03) { /* Ctrl+C = Messages */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Messages"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwmsg = &g_windows[g_num_windows];
                          nwmsg->x=130;nwmsg->y=60;nwmsg->w=340;nwmsg->h=290;
                          nwmsg->title="Messages";nwmsg->visible=1;nwmsg->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x08) { /* Ctrl+H = AirDrop */
                    g_airdrop_visible = !g_airdrop_visible;
                    if (g_airdrop_visible) g_airdrop_sending = 0;
                    dirty = 1;
                } else if (ch == 0x09) { /* Ctrl+I = iCloud */
                    g_icloud_visible = !g_icloud_visible; dirty = 1;
                } else if (ch == 0x02) { /* Ctrl+B = Bluetooth */
                    g_bt_visible = !g_bt_visible; dirty=1;
                } else if (ch == 0x15) { /* Ctrl+U = System Update */
                    g_update_visible = !g_update_visible; dirty=1;
                } else if (ch == 0x06) { /* Ctrl+F = Focus Filter */
                    g_focus_filter_visible = !g_focus_filter_visible; dirty=1;
                } else if (ch == 0x14) { /* Ctrl+T = Time Machine */
                    g_timemachine_visible = !g_timemachine_visible; dirty=1;
                } else if (ch == 0x0C) { /* Ctrl+L = Color Meter */
                    g_colormeter_visible = !g_colormeter_visible; dirty=1;
                } else if (ch == 0x0E) { /* Ctrl+N = Notif History */
                    g_notifhist_visible = !g_notifhist_visible; dirty=1;
                } else if (ch == 0xF8) { /* F8 = Keyboard Shortcuts */
                    g_kbshort_visible = !g_kbshort_visible; dirty=1;
                } else if (ch == 0x17) { /* Ctrl+W = WiFi */
                    g_wifi_visible = !g_wifi_visible; dirty=1;
                } else if (ch == 0x07) { /* Ctrl+G = Display */
                    g_display_visible = !g_display_visible; dirty=1;
                } else if (ch == 0x12) { /* Ctrl+R = Sound */
                    g_sound_visible = !g_sound_visible; dirty=1;
                } else if (ch == 0x0F) { /* Ctrl+O = Activity Monitor */
                    g_actmon_visible = !g_actmon_visible; dirty=1;
                } else if (ch == 0x1A) { /* Ctrl+Z = FaceTime */
                    g_facetime_visible = !g_facetime_visible;
                    if (g_facetime_visible) g_facetime_calling = 0;
                    dirty = 1;
                } else if (ch == 0x1C) { /* Ctrl+\ = Snake game */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Snake"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwsn = &g_windows[g_num_windows];
                          nwsn->x=120;nwsn->y=45;nwsn->w=316;nwsn->h=252;
                          nwsn->title="Snake";nwsn->visible=1;nwsn->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                          toast_show("Snake","Arrow keys to play, Space to start",RGB(52,199,89));
                      }
                      dirty=1;
                    }
                } else if (ch == 0x1D) { /* Ctrl+] = Breakout game */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Breakout"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwbk = &g_windows[g_num_windows];
                          nwbk->x=100;nwbk->y=40;nwbk->w=320;nwbk->h=280;
                          nwbk->title="Breakout";nwbk->visible=1;nwbk->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                          toast_show("Breakout","Space=start, A/D or arrows to move",RGB(100,180,255));
                      }
                      dirty=1;
                    }
                } else if (ch == 0x0A && !g_wordle_focused) { /* Ctrl+J = Home (blocked when Wordle is focused) */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Home"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwhm = &g_windows[g_num_windows];
                          nwhm->x=150;nwhm->y=65;nwhm->w=340;nwhm->h=290;
                          nwhm->title="Home";nwhm->visible=1;nwhm->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x11) { /* Ctrl+Q = Music */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Music"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwmu = &g_windows[g_num_windows];
                          nwmu->x=170;nwmu->y=70;nwmu->w=260;nwmu->h=320;
                          nwmu->title="Music";nwmu->visible=1;nwmu->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x15) { /* Ctrl+U = Books */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Books"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwbk = &g_windows[g_num_windows];
                          nwbk->x=160;nwbk->y=70;nwbk->w=300;nwbk->h=280;
                          nwbk->title="Books";nwbk->visible=1;nwbk->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0xFC) { /* F12 = Freeform */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Freeform"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwff = &g_windows[g_num_windows];
                          nwff->x=110;nwff->y=55;nwff->w=360;nwff->h=310;
                          nwff->title="Freeform";nwff->visible=1;nwff->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0xFB) { /* F11 = Voice Memos */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Voice Memos"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwvm = &g_windows[g_num_windows];
                          nwvm->x=160;nwvm->y=70;nwvm->w=240;nwvm->h=300;
                          nwvm->title="Voice Memos";nwvm->visible=1;nwvm->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0xF9) { /* F9 = Find My */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Find My"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwfm = &g_windows[g_num_windows];
                          nwfm->x=140;nwfm->y=65;nwfm->w=320;nwfm->h=270;
                          nwfm->title="Find My";nwfm->visible=1;nwfm->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0xFA) { /* F10 = Wallet */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Wallet"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwwl = &g_windows[g_num_windows];
                          nwwl->x=180;nwwl->y=70;nwwl->w=260;nwwl->h=300;
                          nwwl->title="Wallet";nwwl->visible=1;nwwl->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x1E) { /* Ctrl+^ = Translate */
                    { int jj, ff=0;
                      for (jj=0;jj<g_num_windows;jj++) {
                          if (g_windows[jj].title && str_eq(g_windows[jj].title,"Translate"))
                              { g_windows[jj].visible=1; win_bring_to_front(jj); ff=1; break; }
                      }
                      if (!ff && g_num_windows < MAX_WINDOWS) {
                          gui_window_t *nwtr = &g_windows[g_num_windows];
                          nwtr->x=160;nwtr->y=65;nwtr->w=310;nwtr->h=280;
                          nwtr->title="Translate";nwtr->visible=1;nwtr->focused=0;
                          g_win_anim[g_num_windows]=OPEN_ANIM;
                          g_num_windows++;
                      }
                      dirty=1;
                    }
                } else if (ch == 0x1F) { /* Ctrl+_ = Split View (tile top 2 windows) */
                    { /* Find the top two visible non-overlay windows */
                      int sv_i = -1, sv_j = -1, kk;
                      int av = MENUBAR_H, ah = DOCK_Y - MENUBAR_H;
                      for (kk=g_num_windows-1; kk>=0 && (sv_i<0||sv_j<0); kk--) {
                          if (!g_windows[kk].visible) continue;
                          if (sv_i < 0) sv_i = kk;
                          else if (sv_j < 0) sv_j = kk;
                      }
                      if (sv_i >= 0 && sv_j >= 0) {
                          int hw = VGA_WIDTH / 2;
                          g_windows[sv_j].x=0;    g_windows[sv_j].y=av;
                          g_windows[sv_j].w=hw-1; g_windows[sv_j].h=ah;
                          g_windows[sv_j].maximized=0;
                          g_windows[sv_i].x=hw+1; g_windows[sv_i].y=av;
                          g_windows[sv_i].w=hw-1; g_windows[sv_i].h=ah;
                          g_windows[sv_i].maximized=0;
                          toast_show("Split View","Two windows side by side",RGB(0,122,255));
                          dirty=1;
                      }
                    }
                } else if (ch == 0xF7) { /* F7 = Widget Bar */
                    g_widget_visible = !g_widget_visible; dirty = 1;
                } else if (gui_top_window_named("Photo Booth") &&
                           (ch>='1' && ch<='6')) {
                    /* Photo Booth: number keys 1-6 select filter */
                    g_pb_filter = (int)(ch - '1');
                    dirty = 1;
                } else if (gui_top_window_named("Photo Booth") &&
                           ch == ' ') {
                    /* Photo Booth: Space = take photo */
                    g_pb_flash_tick = timer_ticks();
                    g_pb_photos[g_pb_captured % 4] = g_pb_filter;
                    g_pb_captured++;
                    static const char *pbnames_kb[]={"Normal","Sepia","B&W","Vintage","Comic","X-Ray"};
                    toast_show("Photo Booth", pbnames_kb[g_pb_filter], RGB(220,50,50));
                    dirty = 1;
                } else if (g_cal_popup) {
                    /* Calendar event creation popup keyboard */
                    if (ch == 0xFF1B) { /* ESC */
                        g_cal_popup=0; dirty=1;
                    } else if (ch == '\r' || ch == '\n') {
                        /* Add event */
                        if (g_cal_evt_input_len > 0 && g_cal_evt_n < CAL_MAX_EVTS) {
                            g_cal_evt_day[g_cal_evt_n] = g_cal_sel_day;
                            int ei4;
                            for(ei4=0;ei4<g_cal_evt_input_len&&ei4<39;ei4++)
                                g_cal_evt_txt[g_cal_evt_n][ei4]=g_cal_evt_input[ei4];
                            g_cal_evt_txt[g_cal_evt_n][ei4]=0;
                            g_cal_evt_n++;
                            toast_show("Calendar","Event added",RGB(255,59,48));
                        }
                        g_cal_popup=0; dirty=1;
                    } else if (ch == '\b' || ch == 127) {
                        if(g_cal_evt_input_len>0){g_cal_evt_input_len--;g_cal_evt_input[g_cal_evt_input_len]=0;dirty=1;}
                    } else if (ch >= 0x20 && ch < 0x80 && g_cal_evt_input_len < 38) {
                        g_cal_evt_input[g_cal_evt_input_len++]=(char)ch;
                        g_cal_evt_input[g_cal_evt_input_len]=0;
                        dirty=1;
                    }
                } else if (gui_top_window_named("Contacts") &&
                           (ch==KEY_UP||ch==KEY_DOWN)) {
                    /* Contacts: arrow keys navigate contact list */
                    if (ch==KEY_UP && g_ct_sel > 0) g_ct_sel--;
                    else if (ch==KEY_DOWN && g_ct_sel < 7) g_ct_sel++;
                    dirty = 1;
                } else if (g_ms_focused && gui_top_window_named("Messages")) {
                    /* Messages: typing in input box */
                    static const char *ms_autoreplies[]={
                        "Haha that's funny!","Sounds good to me!","On my way!",
                        "Sure, see you then!","Thanks!","Let me check and get back to you",
                        "LOL yes exactly!","Absolutely!","Great idea!"
                    };
                    if (ch == '\r' || ch == '\n') {
                        /* Send message */
                        if (g_ms_input_len > 0 && g_ms_sent_n < MS_MAXSENT) {
                            int ii2;
                            for (ii2=0;ii2<g_ms_input_len&&ii2<71;ii2++)
                                g_ms_sent[g_ms_sent_n][ii2]=g_ms_input[ii2];
                            g_ms_sent[g_ms_sent_n][ii2]=0;
                            g_ms_sent_conv[g_ms_sent_n] = g_ms_sel;
                            g_ms_sent_n++;
                            /* Queue auto-reply */
                            if (g_ms_reply_n < MS_MAXSENT) {
                                static int ar_idx=0;
                                int n_ar=9;
                                const char *ar=ms_autoreplies[ar_idx%n_ar]; ar_idx++;
                                int ii3;
                                for(ii3=0;ar[ii3]&&ii3<71;ii3++) g_ms_reply[g_ms_reply_n][ii3]=ar[ii3];
                                g_ms_reply[g_ms_reply_n][ii3]=0;
                                g_ms_reply_conv[g_ms_reply_n]=g_ms_sel;
                                g_ms_reply_tick[g_ms_reply_n]=timer_ticks();
                                g_ms_reply_n++;
                            }
                            g_ms_input_len=0; g_ms_input[0]=0;
                            dirty=1;
                        }
                    } else if (ch == '\b' || ch == 127) {
                        if (g_ms_input_len > 0) { g_ms_input_len--; g_ms_input[g_ms_input_len]=0; dirty=1; }
                    } else if (ch >= 0x20 && ch < 0x80 && g_ms_input_len < 72) {
                        g_ms_input[g_ms_input_len++] = (char)ch;
                        g_ms_input[g_ms_input_len] = 0;
                        dirty=1;
                    }
                } else if (g_notes_focused) {
                    /* Notes: type into the editable note (last slot) */
                    if (g_notes_sel == NOTES_COUNT-1) {
                        int nb_len = 0;
                        while (g_notes_body[NOTES_COUNT-1][nb_len]) nb_len++;
                        if (ch == '\b' || ch == 127) {
                            if (nb_len > 0) { g_notes_body[NOTES_COUNT-1][nb_len-1]=0; dirty=1; }
                        } else if (ch == KEY_ESC || ch == 0x1B) {
                            g_notes_focused=0; dirty=1;
                        } else if (ch == '\r' || ch == '\n') {
                            if (nb_len < NOTES_MAXLEN-1) { g_notes_body[NOTES_COUNT-1][nb_len]='\n'; g_notes_body[NOTES_COUNT-1][nb_len+1]=0; dirty=1; }
                        } else if (ch >= 0x20 && ch < 0x7F && nb_len < NOTES_MAXLEN-1) {
                            g_notes_body[NOTES_COUNT-1][nb_len]=(char)ch;
                            g_notes_body[NOTES_COUNT-1][nb_len+1]=0;
                            dirty=1;
                        }
                    }
                } else if (g_dict_focused) {
                    /* Dictionary: typing in search bar */
                    if (ch == '\b' || ch == 127) {
                        if (g_dict_input_len > 0) { g_dict_input_len--; g_dict_input[g_dict_input_len]=0; dirty=1; }
                    } else if (ch == KEY_ESC || ch == 0x1B) {
                        g_dict_input_len=0; g_dict_input[0]=0; g_dict_focused=0; dirty=1;
                    } else if (ch >= 0x20 && ch < 0x7F && g_dict_input_len < 30) {
                        g_dict_input[g_dict_input_len++] = (char)ch;
                        g_dict_input[g_dict_input_len] = 0;
                        dirty=1;
                    }
                } else if (gui_top_window_named("Sudoku") &&
                           g_sdk_started && g_sdk_sel_r>=0 && g_sdk_sel_c>=0) {
                    /* Sudoku: number keys to place digit */
                    if (ch>='1'&&ch<='9' && !g_sdk_given[g_sdk_sel_r][g_sdk_sel_c]) {
                        int n=(int)(ch-'0');
                        g_sdk_board[g_sdk_sel_r][g_sdk_sel_c]=n;
                        if (g_sdk_puzzle[g_sdk_sel_r][g_sdk_sel_c]!=0 &&
                            g_sdk_puzzle[g_sdk_sel_r][g_sdk_sel_c]!=n) g_sdk_errors++;
                        dirty=1;
                    } else if ((ch=='0'||ch=='\b'||ch==127) && !g_sdk_given[g_sdk_sel_r][g_sdk_sel_c]) {
                        g_sdk_board[g_sdk_sel_r][g_sdk_sel_c]=0; dirty=1;
                    } else if (ch==KEY_UP&&g_sdk_sel_r>0) { g_sdk_sel_r--; dirty=1; }
                    else if (ch==KEY_DOWN&&g_sdk_sel_r<8) { g_sdk_sel_r++; dirty=1; }
                    else if (ch==KEY_LEFT&&g_sdk_sel_c>0) { g_sdk_sel_c--; dirty=1; }
                    else if (ch==KEY_RIGHT&&g_sdk_sel_c<8) { g_sdk_sel_c++; dirty=1; }
                } else if (gui_top_window_named("2048") &&
                           (ch==KEY_LEFT||ch==KEY_RIGHT||ch==KEY_UP||ch==KEY_DOWN||ch=='n'||ch=='N')) {
                    /* 2048 controls */
                    if (ch=='n'||ch=='N') { g2048_new_game(); dirty=1; }
                    else if (g_2048_state==0) { g2048_new_game(); dirty=1; }
                    else if (g_2048_state==1||g_2048_state==2) {
                        int dir=-1;
                        if (ch==KEY_LEFT)  dir=0;
                        else if (ch==KEY_RIGHT) dir=1;
                        else if (ch==KEY_UP)    dir=2;
                        else if (ch==KEY_DOWN)  dir=3;
                        if (dir>=0) { g2048_move(dir); dirty=1; }
                    } else if (g_2048_state==3) { g2048_new_game(); dirty=1; }
                } else if (gui_top_window_named("Breakout") &&
                           (ch==KEY_LEFT||ch==KEY_RIGHT||ch==' '||ch=='a'||ch=='d')) {
                    /* Breakout controls */
                    if (ch == ' ') {
                        if (!g_brk_active || g_brk_game_over || g_brk_won) {
                            /* Init/restart */
                            int ri, ci2;
                            for (ri=0;ri<BRK_ROWS;ri++) for (ci2=0;ci2<BRK_COLS;ci2++) g_brk_bricks[ri][ci2]=1;
                            g_brk_paddle_x=150; g_brk_ball_x=150; g_brk_ball_y=180;
                            g_brk_ball_vx=2; g_brk_ball_vy=-3;
                            g_brk_score=0; g_brk_lives=3;
                            g_brk_game_over=0; g_brk_won=0;
                            g_brk_last_tick=timer_ticks();
                            g_brk_active=1;
                        }
                    } else if ((ch==KEY_LEFT||ch=='a') && g_brk_paddle_x > 30) g_brk_paddle_x -= 18;
                    else if (ch==KEY_RIGHT||ch=='d') {
                        int top = win_top_visible();
                        if (top >= 0 && g_brk_paddle_x < g_windows[top].w - 32) g_brk_paddle_x += 18;
                    }
                    dirty = 1;
                } else if (gui_top_window_named("Pong") &&
                           (ch==' '||ch=='w'||ch=='s'||ch==KEY_UP||ch==KEY_DOWN)) {
                    /* Pong controls */
                    if (ch == ' ') {
                        if (!g_pong_active || g_pong_over) {
                            int top = win_top_visible();
                            g_pong_active=1; g_pong_over=0;
                            g_pong_score_p=0; g_pong_score_a=0;
                            if (top >= 0) {
                                gui_window_t *pw=&g_windows[top];
                                int pgh=pw->h-TITLEBAR_H-2;
                                g_pong_bx=pw->w/2; g_pong_by=pgh/2;
                                g_pong_vx=3; g_pong_vy=2;
                                g_pong_py=pgh/2; g_pong_ay=pgh/2;
                                g_pong_last=timer_ticks();
                            }
                        }
                    } else if ((ch=='w'||ch==KEY_UP) && g_pong_py > 20) g_pong_py -= 16;
                    else if ((ch=='s'||ch==KEY_DOWN)) {
                        int top = win_top_visible();
                        if (top >= 0) {
                            gui_window_t *pw=&g_windows[top];
                            int pgh=pw->h-TITLEBAR_H-2;
                            if (g_pong_py < pgh-20) g_pong_py += 16;
                        }
                    }
                    dirty = 1;
                } else if (gui_top_window_named("Snake") &&
                           (ch==KEY_UP||ch==KEY_DOWN||ch==KEY_LEFT||ch==KEY_RIGHT||ch==' ')) {
                    /* Snake game controls */
                    if (ch == ' ') {
                        if (!g_snake_active || g_snake_game_over) {
                            /* Start / restart */
                            g_snake_len=3; g_snake_score=0; g_snake_speed=200;
                            g_snake_dir=0; g_snake_next_dir=0; g_snake_game_over=0;
                            g_snake_x[0]=5; g_snake_y[0]=7;
                            g_snake_x[1]=4; g_snake_y[1]=7;
                            g_snake_x[2]=3; g_snake_y[2]=7;
                            g_snake_food_x=12; g_snake_food_y=7;
                            g_snake_last_tick=timer_ticks();
                            g_snake_active=1;
                        }
                    } else if (ch==KEY_UP    && g_snake_dir!=1) g_snake_next_dir=3;
                    else if (ch==KEY_DOWN    && g_snake_dir!=3) g_snake_next_dir=1;
                    else if (ch==KEY_LEFT    && g_snake_dir!=0) g_snake_next_dir=2;
                    else if (ch==KEY_RIGHT   && g_snake_dir!=2) g_snake_next_dir=0;
                    dirty = 1;
                } else if (g_photos_fullscreen && (ch == KEY_LEFT || ch == KEY_UP)) {
                    g_photos_sel = (g_photos_sel + 5) % 6; dirty = 1;
                } else if (g_photos_fullscreen && (ch == KEY_RIGHT || ch == KEY_DOWN)) {
                    g_photos_sel = (g_photos_sel + 1) % 6; dirty = 1;
                } else if (ch == KEY_PGUP) {
                    if (gui_top_window_named("Safari")) {
                        g_safari_page_scroll -= 5;
                        if (g_safari_page_scroll < 0) g_safari_page_scroll = 0;
                        dirty = 1;
                    } else if (gui_top_window_named("Terminal")) {
                        int max_sc = term_num_lines - TERM_LINES;
                        if (max_sc < 0) max_sc = 0;
                        g_term_scroll += 5;
                        if (g_term_scroll > max_sc) g_term_scroll = max_sc;
                        dirty = 1;
                    }
                } else if (ch == KEY_PGDN) {
                    if (gui_top_window_named("Safari")) {
                        g_safari_page_scroll += 5;
                        if (g_safari_page_scroll > SAFARI_PAGE_TEXT_MAX / 8) g_safari_page_scroll = SAFARI_PAGE_TEXT_MAX / 8;
                        dirty = 1;
                    } else if (gui_top_window_named("Terminal")) {
                        g_term_scroll -= 5;
                        if (g_term_scroll < 0) g_term_scroll = 0;
                        dirty = 1;
                    }
                } else if ((ch == KEY_UP || ch == KEY_DOWN) && gui_top_window_named("Safari") && !g_safari_url_focused) {
                    if (ch == KEY_UP && g_safari_page_scroll > 0) g_safari_page_scroll--;
                    if (ch == KEY_DOWN && g_safari_page_scroll < SAFARI_PAGE_TEXT_MAX / 8) g_safari_page_scroll++;
                    dirty = 1;
                } else if ((ch == KEY_UP || ch == KEY_DOWN) && gui_top_window_named("Settings")) {
                    if (ch == KEY_UP && g_settings_tab > 0)  { g_settings_tab--; dirty=1; }
                    if (ch == KEY_DOWN && g_settings_tab < 13){ g_settings_tab++; dirty=1; }
                } else if (ch == KEY_LEFT && gui_top_window_named("Calendar")) {
                    g_cal_offset--; dirty = 1;
                } else if (ch == KEY_RIGHT && gui_top_window_named("Calendar")) {
                    g_cal_offset++; dirty = 1;
                } else if ((ch==KEY_LEFT||ch==KEY_RIGHT||ch==KEY_UP||ch==KEY_DOWN) &&
                           gui_top_window_named("Maps")) {
                    /* Pan Maps with arrow keys */
                    int pan_step = 20 * g_maps_zoom;
                    if (ch==KEY_LEFT)  g_maps_pan_x -= pan_step;
                    if (ch==KEY_RIGHT) g_maps_pan_x += pan_step;
                    if (ch==KEY_UP)    g_maps_pan_y -= pan_step;
                    if (ch==KEY_DOWN)  g_maps_pan_y += pan_step;
                    dirty = 1;
                } else if (ch >= 0xC0 && ch <= 0xC3) {
                    /* Ctrl+Arrow = Window Tiling (macOS Sequoia) */
                    int wi = win_top_visible();
                    int aw = VGA_WIDTH, ah = DOCK_Y - MENUBAR_H;
                    if (wi >= 0 && ch == 0xC2) { /* Ctrl+Left = left half */
                        gui_window_t *tw = &g_windows[wi];
                        if (g_tile_saved_idx != wi) {
                            g_tile_saved_x=tw->x; g_tile_saved_y=tw->y;
                            g_tile_saved_w=tw->w; g_tile_saved_h=tw->h;
                            g_tile_saved_idx=wi;
                        }
                        tw->x=0; tw->y=MENUBAR_H; tw->w=aw/2; tw->h=ah;
                        g_tile_zone=1; g_tile_flash=12;
                        toast_show("Window Tile","Left Half",RGB(52,199,89));
                    } else if (wi >= 0 && ch == 0xC3) { /* Ctrl+Right = right half */
                        gui_window_t *tw = &g_windows[wi];
                        if (g_tile_saved_idx != wi) {
                            g_tile_saved_x=tw->x; g_tile_saved_y=tw->y;
                            g_tile_saved_w=tw->w; g_tile_saved_h=tw->h;
                            g_tile_saved_idx=wi;
                        }
                        tw->x=aw/2; tw->y=MENUBAR_H; tw->w=aw/2; tw->h=ah;
                        g_tile_zone=2; g_tile_flash=12;
                        toast_show("Window Tile","Right Half",RGB(52,199,89));
                    } else if (wi >= 0 && ch == 0xC0) { /* Ctrl+Up = maximize */
                        gui_window_t *tw = &g_windows[wi];
                        if (g_tile_saved_idx != wi) {
                            g_tile_saved_x=tw->x; g_tile_saved_y=tw->y;
                            g_tile_saved_w=tw->w; g_tile_saved_h=tw->h;
                            g_tile_saved_idx=wi;
                        }
                        tw->x=0; tw->y=MENUBAR_H; tw->w=aw; tw->h=ah;
                        g_tile_zone=3; g_tile_flash=12;
                        toast_show("Window Tile","Maximized",RGB(52,199,89));
                    } else if (wi >= 0 && ch == 0xC1) { /* Ctrl+Down = restore */
                        gui_window_t *tw = &g_windows[wi];
                        if (g_tile_saved_idx == wi) {
                            tw->x=g_tile_saved_x; tw->y=g_tile_saved_y;
                            tw->w=g_tile_saved_w; tw->h=g_tile_saved_h;
                            g_tile_saved_idx=-1;
                            toast_show("Window Tile","Restored",RGB(100,100,110));
                        } else {
                            tw->x=100; tw->y=60; tw->w=380; tw->h=300;
                            toast_show("Window Tile","Default Size",RGB(100,100,110));
                        }
                        g_tile_zone=0; g_tile_flash=0;
                    }
                    if (wi >= 0) dirty = 1;
                } else if (g_scr_visible) {
                    /* Screenshot tool input */
                    if (ch == 0x1B || ch == KEY_ESC) {
                        g_scr_visible = 0; dirty = 1;
                    } else if (ch == '	') {
                        g_scr_mode = (g_scr_mode + 1) % 3; dirty = 1;
                    } else if (ch == 's' || ch == 'S') {
                        g_scr_visible = 2; dirty = 1;
                        toast_show("Screenshot", "Preview ready", RGB(52,199,89));
                    }
                } else if (ch == ' ' && !g_lp_visible && !g_mc_visible && !g_spot_visible && !g_edit_focused && !g_safari_url_focused && !g_photos_fullscreen) {
                    /* Space = Quick Look (only when no text input is focused) */
                    g_ql_visible = !g_ql_visible; dirty = 1;
                } else if (ch == 0xF1) { /* F1 = Night Shift toggle */
                    g_night_shift = !g_night_shift; dirty = 1;
                    toast_show("Night Shift", g_night_shift?"Night Shift On":"Night Shift Off",
                               g_night_shift?RGB(255,160,40):RGB(100,100,110));
                } else if (ch == 0xF2) { /* F2 = Focus Mode cycle */
                    if (!g_pref_dnd) {
                        g_pref_dnd = 1;
                        g_focus_mode = 1;
                    } else {
                        g_focus_mode++;
                        if (g_focus_mode > 5) { g_focus_mode = 1; g_pref_dnd = 0; }
                    }
                    if (g_pref_dnd) {
                        const char *nm = focus_mode_name(g_focus_mode);
                        toast_show("Focus Mode", nm, RGB(100,60,200));
                    } else {
                        toast_show("Focus Mode", "Off", RGB(100,100,110));
                    }
                    dirty = 1;
                } else if (ch == 0xF3) { /* F3 = App Expose (windows of focused app) */
                    if (g_expose_visible) {
                        g_expose_visible = 0;
                    } else {
                        g_expose_visible = 1;
                        g_expose_app_idx = -1; /* show focused app's windows */
                        int top3 = win_top_visible();
                        if (top3 >= 0 && g_windows[top3].title) {
                            int di3;
                            for (di3 = 0; di3 < NUM_DOCK_ICONS; di3++) {
                                if (str_eq(s_dock_icons[di3].name, g_windows[top3].title) ||
                                    str_eq(s_dock_icons[di3].name, "Finder")) {
                                    g_expose_app_idx = di3; break;
                                }
                            }
                        }
                        g_mc_visible = 0;
                    }
                    dirty = 1;
                } else if (ch == 0xF4) { /* F4 = Launchpad */
                    g_lp_visible = !g_lp_visible; dirty = 1;
                } else if (ch == 0xF5) { /* F5 = App Switcher */
                    if (!g_switcher_visible) {
                        g_switcher_visible = 1;
                        g_switcher_sel = (g_num_windows > 0) ? g_num_windows - 1 : 0;
                    } else {
                        /* Cycle backward through windows */
                        if (g_num_windows > 0) {
                            g_switcher_sel = (g_switcher_sel - 1 + g_num_windows) % g_num_windows;
                        }
                    }
                    dirty = 1;
                } else if (ch == 0xF6) { /* F6 = confirm switcher selection */
                    if (g_switcher_visible && g_num_windows > 0) {
                        g_windows[g_switcher_sel].visible = 1;
                        win_bring_to_front(g_switcher_sel);
                        g_switcher_visible = 0;
                        dirty = 1;
                    }
                } else if (ch >= KEY_CTRL_DIGIT_BASE + 1 &&
                           ch <= KEY_CTRL_DIGIT_BASE + 4 &&
                           !g_lp_visible) { /* Ctrl+1-4 = switch spaces */
                    int sp = (int)(ch - KEY_CTRL_DIGIT_BASE);
                    if (sp <= g_num_spaces) { g_current_space = sp; dirty = 1; }
                } else if (ch == 'p' && !g_edit_focused && !g_safari_url_focused && !g_spot_visible && !g_lp_visible) {
                    /* p = Screenshot Tool (like Cmd+Shift+5) */
                    if (g_scr_visible) { g_scr_visible = 0; }
                    else { g_scr_visible = 1; g_scr_mode = 0; }
                    dirty = 1;
                } else if (ch == 0xFD) { /* Insert = Writing Tools */
                    g_wt_visible = !g_wt_visible;
                    if (g_wt_visible) { g_wt_sel=-1; g_wt_done=0; }
                    dirty = 1;
                } else if (ch == 0xFE) { /* Delete = Quick Note */
                    g_qn_visible = !g_qn_visible;
                    dirty = 1;
                } else if (g_wt_visible) {
                    /* Writing Tools: ESC closes, 1-6 selects tool */
                    if (ch == KEY_ESC || ch == 0x1B) { g_wt_visible=0; dirty=1; }
                    else if (ch>='1'&&ch<='6'&&g_wt_done==0){
                        (void)writing_tools_apply(ch-'1'); dirty=1;
                    }
                } else if (g_qn_visible) {
                    /* Quick Note input */
                    if (ch == KEY_ESC || ch == 0x1B) { g_qn_visible=0; dirty=1; }
                    else if (ch == KEY_BACKSPACE && g_qn_len > 0) { g_qn_text[--g_qn_len]=0; dirty=1; }
                    else if (ch == KEY_ENTER && g_qn_len < 126) { g_qn_text[g_qn_len++]='\n'; g_qn_text[g_qn_len]=0; dirty=1; }
                    else if (ch >= 0x20 && ch < 0x7F && g_qn_len < 126) { g_qn_text[g_qn_len++]=(char)ch; g_qn_text[g_qn_len]=0; dirty=1; }
                } else if (g_lp_visible) {
                    /* Launchpad: search + page navigation */
                    if (ch == KEY_BACKSPACE) {
                        if (g_lp_slen > 0) { g_lp_search[--g_lp_slen]=0; dirty=1; }
                    } else if (ch >= 0x20 && ch < 0x7F && g_lp_slen < 30) {
                        g_lp_search[g_lp_slen++]=(char)ch; g_lp_search[g_lp_slen]=0; dirty=1;
                    } else {
                        int ipp2 = LAUNCHPAD_COLS * LAUNCHPAD_ROWS;
                        int tp2  = (LP_ICON_COUNT + ipp2 - 1) / ipp2;
                        if (ch == KEY_RIGHT && g_lp_slen==0 && g_lp_page < tp2-1) { g_lp_page++; dirty=1; }
                        else if (ch == KEY_LEFT && g_lp_slen==0 && g_lp_page > 0) { g_lp_page--; dirty=1; }
                        else dirty = 1;
                    }
                } else if (ch == '\t') {
                    /* Tab cycles Raycast actions, completes Terminal input, or toggles Spotlight. */
                    if (!g_edit_focused && !g_safari_url_focused && gui_top_window_named("Raycast")) {
                        g_raycast_result = (g_raycast_result + 1) % 6;
                        toast_show("Raycast","Action selected",RGB(255,90,30));
                        dirty = 1;
                    } else if (!g_edit_focused && !g_safari_url_focused &&
                               gui_top_window_named("Terminal") && term_input_len > 0) {
                        /* Complete to first matching command. */
                        static const char *cmds[] = {
                            "ls","ls -la","ls -a","pwd","date","uptime","uname","uname -a",
                            "whoami","hostname","ps","top","htop","df","free","dmesg",
                            "sysinfo","neofetch","ifconfig","netstat","route","ip a",
                            "sw_vers","diskutil list","lsblk","lscpu","lscpu",
                            "git status","make","brew","python3","node","ssh","curl",
                            "caffeinate","launchctl list","arch","env","history","alias",
                            "help","clear","exit","gui","reboot","shutdown","banner","fortune",
                            NULL
                        };
                        int ci3;
                        for (ci3=0; cmds[ci3]; ci3++) {
                            int match=1, ti3;
                            for (ti3=0; ti3<term_input_len; ti3++) {
                                if (!cmds[ci3][ti3] || cmds[ci3][ti3]!=term_input[ti3]) { match=0; break; }
                            }
                            if (match && str_len(cmds[ci3]) > term_input_len) {
                                /* Complete to this command */
                                int fi3;
                                for (fi3=0; cmds[ci3][fi3]; fi3++) {
                                    term_input[fi3] = cmds[ci3][fi3];
                                }
                                term_input_len = fi3;
                                term_input[fi3] = 0;
                                dirty = 1;
                                break;
                            }
                        }
                    } else {
                        /* Tab = toggle Spotlight */
                        g_spot_visible = !g_spot_visible;
                        g_spot_qlen = 0; g_spot_query[0] = 0; g_spot_sel = 0;
                        dirty = 1;
                    }
                } else if (g_siri_visible) {
                    /* Siri input */
                    if (ch == KEY_ESC || ch == 0x1B) {
                        g_siri_visible=0; g_siri_qlen=0; g_siri_query[0]=0; g_siri_response=0; dirty=1;
                    } else if (ch == KEY_ENTER && g_siri_qlen > 0) {
                        g_siri_response=1; g_siri_resp_tick=timer_ticks(); dirty=1;
                    } else if (ch == KEY_BACKSPACE && g_siri_qlen > 0) {
                        g_siri_query[--g_siri_qlen]=0; dirty=1;
                    } else if (ch >= 0x20 && ch < 0x7F && g_siri_qlen < 47) {
                        g_siri_query[g_siri_qlen++]=(char)ch; g_siri_query[g_siri_qlen]=0; dirty=1;
                    }
                } else if (g_spot_visible) {
                    /* Spotlight input */
                    if (ch == KEY_UP) {
                        if (g_spot_sel > 0) g_spot_sel--;
                        dirty = 1;
                    } else if (ch == KEY_DOWN) {
                        g_spot_sel++;
                        dirty = 1;
                    } else if (ch == KEY_ENTER && g_spot_qlen > 0) {
                        /* Open selected matching app */
                        int ri2 = 0; int hit2 = 0;
                        while (g_spot_apps[ri2]) {
                            int m = spot_substr_match(g_spot_apps[ri2], g_spot_query, g_spot_qlen);
                            if (m) {
                                if (hit2 == g_spot_sel) {
                                const char *aname = g_spot_apps[ri2];
                                int j3, found3=0;
                                for (j3=0;j3<g_num_windows;j3++) {
                                    const char *wt = g_windows[j3].title;
                                    if (wt && (str_eq(wt,aname) ||
                                        (str_eq(aname,"Finder") && str_eq(wt,"MyOS Finder")))) {
                                        g_windows[j3].visible=1; win_bring_to_front(j3);
                                        if (str_eq(aname,"Wordle")) { g_wordle_focused=1; }
                                        if (str_eq(aname,"Dictionary")) { g_dict_focused=1; g_edit_focused=0; }
                                        if (str_eq(aname,"TextEdit")) { g_edit_focused=1; g_dict_focused=0; }
                                        found3=1; break;
                                    }
                                }
                                if (!found3 && g_num_windows < MAX_WINDOWS) {
                                    gui_window_t *nw3 = &g_windows[g_num_windows];
                                    nw3->focused=0; nw3->dragging=0; nw3->visible=1; nw3->maximized=0;
                                    if (str_eq(aname,"Clock"))      { nw3->x=50;  nw3->y=80;  nw3->w=180; nw3->h=220; }
                                    else if (str_eq(aname,"Calculator")){ nw3->x=180;nw3->y=100;nw3->w=220;nw3->h=280; }
                                    else if (str_eq(aname,"Settings")) { nw3->x=150;nw3->y=50;nw3->w=500;nw3->h=400; }
                                    else if (str_eq(aname,"TextEdit")) { nw3->x=120;nw3->y=80; nw3->w=310;nw3->h=260; g_edit_focused=1; }
                                    else if (str_eq(aname,"Safari"))   { nw3->x=60; nw3->y=50; nw3->w=480;nw3->h=380; }
                                    else if (str_eq(aname,"Music"))    { nw3->x=80; nw3->y=55; nw3->w=280;nw3->h=340; }
                                    else if (str_eq(aname,"Photos"))   { nw3->x=70; nw3->y=50; nw3->w=420;nw3->h=340; }
                                    else if (str_eq(aname,"Maps"))     { nw3->x=80; nw3->y=55; nw3->w=400;nw3->h=320; }
                                    else if (str_eq(aname,"App Store")){ nw3->x=70; nw3->y=45; nw3->w=440;nw3->h=360; }
                                    else if (str_eq(aname,"Mail"))     { nw3->x=80; nw3->y=50; nw3->w=420;nw3->h=350; }
                                    else if (str_eq(aname,"Calendar")) { nw3->x=90; nw3->y=55; nw3->w=400;nw3->h=340; }
                                    else if (str_eq(aname,"Notes"))    { nw3->x=90; nw3->y=60; nw3->w=300;nw3->h=320; }
                                    else if (str_eq(aname,"Finder"))   { nw3->x=80; nw3->y=50; nw3->w=420;nw3->h=320; }
                                    else if (str_eq(aname,"Terminal")) { nw3->x=100;nw3->y=100;nw3->w=290;nw3->h=220; }
                                    else if (str_eq(aname,"Activity Monitor")) { nw3->x=140;nw3->y=80;nw3->w=320;nw3->h=270; }
                                    else if (str_eq(aname,"System Info"))         { nw3->x=180;nw3->y=100;nw3->w=280;nw3->h=220; }
                                    else if (str_eq(aname,"Color Picker"))        { nw3->x=220;nw3->y=80; nw3->w=220;nw3->h=310; }
                                    else if (str_eq(aname,"Script Editor"))       { nw3->x=130;nw3->y=70; nw3->w=340;nw3->h=290; }
                                    else if (str_eq(aname,"Migration Assistant")) { nw3->x=170;nw3->y=80; nw3->w=300;nw3->h=290; }
                                    else if (str_eq(aname,"Screen Time"))        { nw3->x=160;nw3->y=60; nw3->w=320;nw3->h=320; }
                                    else if (str_eq(aname,"Passwords"))          { nw3->x=120;nw3->y=60; nw3->w=380;nw3->h=300; }
                                    else if (str_eq(aname,"Numbers"))            { nw3->x=100;nw3->y=55; nw3->w=400;nw3->h=300; }
                                    else if (str_eq(aname,"Focus"))              { nw3->x=200;nw3->y=65; nw3->w=280;nw3->h=340; }
                                    else if (str_eq(aname,"Keynote"))            { nw3->x=80; nw3->y=50; nw3->w=420;nw3->h=320; }
                                    else if (str_eq(aname,"Pages"))              { nw3->x=100;nw3->y=50; nw3->w=380;nw3->h=320; }
                                    else if (str_eq(aname,"GarageBand"))         { nw3->x=60; nw3->y=45; nw3->w=450;nw3->h=330; }
                                    else if (str_eq(aname,"iMovie"))             { nw3->x=70; nw3->y=45; nw3->w=440;nw3->h=330; }
                                    else if (str_eq(aname,"Xcode"))              { nw3->x=60; nw3->y=45; nw3->w=460;nw3->h=330; }
                                    else if (str_eq(aname,"GameCenter"))         { nw3->x=150;nw3->y=60; nw3->w=340;nw3->h=320; }
                                    else if (str_eq(aname,"Automator"))          { nw3->x=80; nw3->y=50; nw3->w=420;nw3->h=320; }
                                    else if (str_eq(aname,"Font Book"))          { nw3->x=120;nw3->y=60; nw3->w=360;nw3->h=300; }
                                    else if (str_eq(aname,"Console"))            { nw3->x=90; nw3->y=50; nw3->w=440;nw3->h=310; }
                                    else if (str_eq(aname,"iPhone Mirroring"))   { nw3->x=250;nw3->y=50; nw3->w=260;nw3->h=310; }
                                    else if (str_eq(aname,"Instruments"))        { nw3->x=70; nw3->y=45; nw3->w=450;nw3->h=320; }
                                    else if (str_eq(aname,"Network Utility"))    { nw3->x=100;nw3->y=55; nw3->w=400;nw3->h=300; }
                                    else if (str_eq(aname,"Math Notes"))         { nw3->x=160;nw3->y=50; nw3->w=300;nw3->h=340; }
                                    else if (str_eq(aname,"Keyboard Shortcuts")) { nw3->x=90;nw3->y=40; nw3->w=620;nw3->h=500; }
                                    else if (str_eq(aname,"Final Cut Pro"))      { nw3->x=50; nw3->y=35; nw3->w=500;nw3->h=340; }
                                    else if (str_eq(aname,"Logic Pro"))          { nw3->x=50; nw3->y=35; nw3->w=500;nw3->h=340; }
                                    else if (str_eq(aname,"Motion"))             { nw3->x=55; nw3->y=40; nw3->w=480;nw3->h=330; }
                                    else if (str_eq(aname,"MainStage"))          { nw3->x=50; nw3->y=40; nw3->w=490;nw3->h=320; }
                                    else if (str_eq(aname,"Compressor"))         { nw3->x=130;nw3->y=55; nw3->w=380;nw3->h=310; }
                                    else if (str_eq(aname,"Screen Recording"))   { nw3->x=200;nw3->y=70; nw3->w=300;nw3->h=260; }
                                    else if (str_eq(aname,"Sidecar"))            { nw3->x=180;nw3->y=60; nw3->w=280;nw3->h=280; }
                                    else if (str_eq(aname,"Universal Control"))  { nw3->x=130;nw3->y=55; nw3->w=360;nw3->h=260; }
                                    else if (str_eq(aname,"Handoff"))            { nw3->x=160;nw3->y=60; nw3->w=320;nw3->h=280; }
                                    else if (str_eq(aname,"Privacy"))            { nw3->x=110;nw3->y=50; nw3->w=400;nw3->h=300; }
                                    else if (str_eq(aname,"Accessibility"))      { nw3->x=110;nw3->y=50; nw3->w=400;nw3->h=300; }
                                    else if (str_eq(aname,"AirPlay"))            { nw3->x=200;nw3->y=60; nw3->w=280;nw3->h=260; }
                                    else if (str_eq(aname,"TestFlight"))         { nw3->x=150;nw3->y=55; nw3->w=340;nw3->h=300; }
                                    else if (str_eq(aname,"Reality Composer"))   { nw3->x=80; nw3->y=40; nw3->w=440;nw3->h=320; }
                                    else if (str_eq(aname,"Configurator"))       { nw3->x=110;nw3->y=50; nw3->w=400;nw3->h=300; }
                                    else if (str_eq(aname,"Stickies"))           { nw3->x=160;nw3->y=80; nw3->w=280;nw3->h=220; }
                                    else if (str_eq(aname,"Dictionary"))         { nw3->x=180;nw3->y=80; nw3->w=300;nw3->h=220; g_dict_focused=1; g_dict_input_len=0; g_dict_input[0]=0; }
                                    else if (str_eq(aname,"Chess"))              { nw3->x=170;nw3->y=70; nw3->w=240;nw3->h=240; }
                                    else if (str_eq(aname,"2048"))               { nw3->x=150;nw3->y=60; nw3->w=240;nw3->h=280; g2048_new_game(); }
                                    else if (str_eq(aname,"Health"))             { nw3->x=100;nw3->y=45; nw3->w=280;nw3->h=380; }
                                    else if (str_eq(aname,"Sudoku"))             { nw3->x=90; nw3->y=40; nw3->w=260;nw3->h=320; g_sdk_started=0;g_sdk_errors=0;g_sdk_sel_r=-1;g_sdk_sel_c=-1; }
                                    else if (str_eq(aname,"Grapher"))            { nw3->x=140;nw3->y=60; nw3->w=320;nw3->h=260; }
                                    else if (str_eq(aname,"Digital Color Meter")){ nw3->x=200;nw3->y=80; nw3->w=280;nw3->h=200; }
                                    else if (str_eq(aname,"Photo Booth"))        { nw3->x=140;nw3->y=60; nw3->w=320;nw3->h=280; }
                                    else if (str_eq(aname,"SF Symbols"))         { nw3->x=130;nw3->y=60; nw3->w=340;nw3->h=270; }
                                    else if (str_eq(aname,"Transporter"))        { nw3->x=170;nw3->y=70; nw3->w=300;nw3->h=240; }
                                    else if (str_eq(aname,"AR Quick Look"))      { nw3->x=130;nw3->y=55; nw3->w=340;nw3->h=280; }
                                    else if (str_eq(aname,"Feedback Assistant")) { nw3->x=150;nw3->y=60; nw3->w=340;nw3->h=260; }
                                    else if (str_eq(aname,"1Password"))          { nw3->x=100;nw3->y=50; nw3->w=420;nw3->h=320; }
                                    else if (str_eq(aname,"Fantastical"))        { nw3->x=120;nw3->y=55; nw3->w=380;nw3->h=310; }
                                    else if (str_eq(aname,"Things 3"))           { nw3->x=110;nw3->y=55; nw3->w=400;nw3->h=310; }
                                    else if (str_eq(aname,"Raycast"))            { nw3->x=150;nw3->y=65; nw3->w=340;nw3->h=260; }
                                    else if (str_eq(aname,"Tot"))                { nw3->x=160;nw3->y=70; nw3->w=300;nw3->h=250; }
                                    else if (str_eq(aname,"Klokki"))             { nw3->x=140;nw3->y=65; nw3->w=320;nw3->h=280; }
                                    else if (str_eq(aname,"Bear"))               { nw3->x=100;nw3->y=50; nw3->w=420;nw3->h=310; }
                                    else if (str_eq(aname,"Reeder 5"))           { nw3->x=100;nw3->y=50; nw3->w=420;nw3->h=310; }
                                    else if (str_eq(aname,"CleanMyMac X"))       { nw3->x=160;nw3->y=55; nw3->w=300;nw3->h=310; }
                                    else if (str_eq(aname,"Bartender 4"))        { nw3->x=120;nw3->y=60; nw3->w=380;nw3->h=280; }
                                    else if (str_eq(aname,"Alfred"))             { nw3->x=130;nw3->y=65; nw3->w=360;nw3->h=280; }
                                    else if (str_eq(aname,"Scrobbles"))          { nw3->x=180;nw3->y=70; nw3->w=280;nw3->h=290; }
                                    else if (str_eq(aname,"Snake"))              { nw3->x=120;nw3->y=45; nw3->w=316;nw3->h=252; toast_show("Snake","Space=start, Arrows=move",RGB(52,199,89)); }
                                    else if (str_eq(aname,"Wordle"))             { nw3->x=150;nw3->y=40; nw3->w=280;nw3->h=380; { int i2,j2; for(i2=0;i2<WORDLE_ROWS;i2++){for(j2=0;j2<WORDLE_COLS;j2++){g_wordle_guesses[i2][j2]=0;g_wordle_results[i2][j2]=0;} g_wordle_guesses[i2][WORDLE_COLS]=0;} for(i2=0;i2<26;i2++)g_wordle_kb_state[i2]=0; g_wordle_cur_row=0;g_wordle_cur_col=0;g_wordle_state=0; g_wordle_answer_idx=0; g_wordle_focused=1; } toast_show("Wordle","Type 5-letter word, Enter to guess",RGB(108,169,100)); }
                                    else if (str_eq(aname,"Breakout"))           { nw3->x=100;nw3->y=40; nw3->w=320;nw3->h=280; toast_show("Breakout","Space=start, A/D=move",RGB(100,180,255)); }
                                    else if (str_eq(aname,"Pong"))               { nw3->x=120;nw3->y=40; nw3->w=300;nw3->h=260; toast_show("Pong","Space=start, W/S=move",RGB(255,180,50)); g_pong_active=0;g_pong_over=0;g_pong_score_p=0;g_pong_score_a=0; }
                                    else if (str_eq(aname,"Minesweeper")) { nw3->x=200;nw3->y=60;nw3->w=200;nw3->h=264; g_mine_state=0;g_mine_remaining=MINE_COUNT;g_mine_rng=1; { int _r,_c; for(_r=0;_r<MINE_ROWS;_r++)for(_c=0;_c<MINE_COLS;_c++){g_mine_board[_r][_c]=0;g_mine_vis[_r][_c]=0;g_mine_flag[_r][_c]=0;} } toast_show("Minesweeper","Left=reveal, Right=flag",RGB(80,80,200)); }
                                    else if (str_eq(aname,"Journal"))     { nw3->x=110;nw3->y=50; nw3->w=380;nw3->h=340; g_journal_sel=0; g_journal_focused=0; toast_show("Journal","Select an entry to read",RGB(255,149,0)); }
                                    else if (str_eq(aname,"Contacts"))    { nw3->x=150;nw3->y=50; nw3->w=420;nw3->h=320; g_ct_sel=0; toast_show("Contacts","Your address book",RGB(0,122,255)); }
                                    else if (str_eq(aname,"Preview"))     { nw3->x=160;nw3->y=60; nw3->w=380;nw3->h=300; g_preview_page=0; g_preview_zoom=100; g_preview_markup=0; toast_show("Preview","Open documents and images",RGB(170,50,170)); }
                                    else if (str_eq(aname,"Apple TV"))    { nw3->x=120;nw3->y=50; nw3->w=400;nw3->h=300; g_atv_sel=0; toast_show("Apple TV","Stream movies and TV shows",RGB(255,255,255)); }
                                    else if (str_eq(aname,"iStudiez Pro"))       { nw3->x=110;nw3->y=55; nw3->w=400;nw3->h=280; }
                                    else if (str_eq(aname,"Lasso"))              { nw3->x=150;nw3->y=60; nw3->w=340;nw3->h=270; }
                                    else if (str_eq(aname,"Messages"))           { nw3->x=130;nw3->y=60; nw3->w=340;nw3->h=290; }
                                    else { nw3->x=120;nw3->y=80;nw3->w=280;nw3->h=200; }
                                    nw3->title = aname;
                                    g_num_windows++;
                                    toast_show(aname, "Opened via Spotlight", RGB(0,122,255));
                                }
                                break;
                                } /* if hit2==g_spot_sel */
                                hit2++;
                            }
                            ri2++;
                        }
                        g_spot_visible=0; g_spot_qlen=0; g_spot_query[0]=0; g_spot_sel=0;
                        dirty=1;
                    } else if (ch == KEY_BACKSPACE) {
                        if (g_spot_qlen > 0) { g_spot_query[--g_spot_qlen] = 0; g_spot_sel=0; }
                        dirty = 1;
                    } else if (ch >= 0x20 && ch < 0x7F && g_spot_qlen < SPOT_QUERY_MAX) {
                        g_spot_query[g_spot_qlen++] = (char)ch;
                        g_spot_query[g_spot_qlen] = 0;
                        g_spot_sel = 0;
                        dirty = 1;
                    }
                } else if (g_safari_url_focused) {
                    /* Safari URL bar input */
                    int url_len = str_len(g_safari_url);
                    safari_normalize_state();
                    if (ch == KEY_ENTER) {
                        g_safari_url_focused = 0;
                        g_safari_form_focused = -1;
                        safari_load_url(g_safari_url);
                        toast_show("Safari", g_safari_page_status, RGB(40,160,220));
                    } else if (ch == KEY_BACKSPACE) {
                        if (url_len > 0) g_safari_url[url_len-1] = 0;
                    } else if (ch >= 0x20 && ch < 0x7F && url_len + 1 < SAFARI_URL_MAX) {
                        g_safari_url[url_len] = (char)ch;
                        g_safari_url[url_len+1] = 0;
                    }
                    dirty = 1;
                } else if (g_edit_focused && gui_top_window_named("TextEdit")) {
                    /* TextEdit input */
                    if (ch == KEY_BACKSPACE) {
                        if (!textedit_delete_selection() && g_edit_len > 0)
                            g_edit_text[--g_edit_len] = 0;
                    } else if (ch == KEY_ENTER) {
                        textedit_delete_selection();
                        if (g_edit_len < TEXTEDIT_MAXCHARS - 1) {
                            g_edit_text[g_edit_len++] = '\n';
                            g_edit_text[g_edit_len] = 0;
                        }
                    } else if (ch >= 0x20 && ch < 0x7F) {
                        textedit_delete_selection();
                        if (g_edit_len < TEXTEDIT_MAXCHARS - 1) {
                            g_edit_text[g_edit_len++] = (char)ch;
                            g_edit_text[g_edit_len] = 0;
                        }
                    }
                    dirty = 1;
                } else if (gui_top_window_named("Calculator")) {
                    /* Calculator keyboard: digits, operators, Enter=, backspace=C */
                    char act = 0;
                    if (ch >= '0' && ch <= '9') act = (char)ch;
                    else if (ch == '+' || ch == '-' || ch == '*' || ch == '/') act = (char)ch;
                    else if (ch == KEY_ENTER || ch == '=') act = '=';
                    else if (ch == KEY_BACKSPACE) act = 'c';
                    else if (ch == '.') act = '.';
                    else if (ch == '%') act = 'p';
                    if (act) { calc_press(act); dirty = 1; }
                } else if (ch == KEY_ENTER) {
                    term_process_command();
                    dirty = 1;
                } else if (ch == KEY_BACKSPACE) {
                    if (term_input_len > 0) {
                        term_input_len--;
                        term_input[term_input_len] = 0;
                    }
                    dirty = 1;
                } else if (ch >= 0x20 && ch < 0x7F && term_input_len < INPUT_MAX) {
                    term_input[term_input_len++] = (char)ch;
                    term_input[term_input_len] = 0;
                    dirty = 1;
                }
                ch = keyboard_poll();
            }
        }

        /* Hot Corners: trigger on entering a 4px corner zone */
        {
            int in_tl = (mx < 4 && my < 4);
            int in_tr = (mx > VGA_WIDTH-5  && my < 4);
            int in_bl = (mx < 4 && my > VGA_HEIGHT-5);
            int in_br = (mx > VGA_WIDTH-5  && my > VGA_HEIGHT-5);
            int was_tl = (prev_mx < 4 && prev_my < 4);
            int was_tr = (prev_mx > VGA_WIDTH-5  && prev_my < 4);
            int was_bl = (prev_mx < 4 && prev_my > VGA_HEIGHT-5);
            int was_br = (prev_mx > VGA_WIDTH-5  && prev_my > VGA_HEIGHT-5);
            if (!g_saver_on && !g_locked) {
                /* Top-left  -> Mission Control */
                if (in_tl && !was_tl) { g_mc_visible = !g_mc_visible; dirty = 1; }
                /* Top-right -> Notification Center */
                if (in_tr && !was_tr) { g_nc_visible = !g_nc_visible; g_cc_visible = 0; dirty = 1; }
                /* Bottom-left -> Launchpad */
                if (in_bl && !was_bl) { g_lp_visible = !g_lp_visible; dirty = 1; }
                /* Bottom-right -> Screen saver */
                if (in_br && !was_br) { g_saver_on = 1; dirty = 1; }
            }
        }

        /* Screensaver idle check (must run BEFORE prev_mx/my update so movement is detected) */
        {
            uint32_t idle_now = timer_ticks();
            int moved = (mb != 0 || mx != prev_mx || my != prev_my);
            if (moved) g_last_input_tick = idle_now;
            if (!g_saver_on && (idle_now - g_last_input_tick) > SAVER_IDLE) {
                g_saver_on = 1; dirty = 1;
            }
            /* Bounce the clock position every ~500ms */
            if (g_saver_on) {
                static uint32_t last_bounce = 0;
                if (idle_now - last_bounce > 500) {
                    last_bounce = idle_now;
                    g_saver_x += g_saver_dx * 3;
                    g_saver_y += g_saver_dy * 2;
                    if (g_saver_x < 20 || g_saver_x > VGA_WIDTH - 130) g_saver_dx = -g_saver_dx;
                    if (g_saver_y < 40 || g_saver_y > VGA_HEIGHT - 60) g_saver_dy = -g_saver_dy;
                    dirty = 1;
                }
                /* Mouse/key wakes screensaver */
                if (moved) { g_saver_on = 0; g_last_input_tick = now; dirty = 1; }
            }
        }

        /* Periodic system notifications (skip while screensaver/locked) */
        if (!g_saver_on && !g_locked && g_pref_notifs) {
            if (now - last_notif_tick > NOTIF_INTERVAL) {
                last_notif_tick = now;
                toast_show_action(s_sys_notifs[notif_idx].app,
                              s_sys_notifs[notif_idx].msg,
                              s_sys_notifs[notif_idx].color,
                              s_sys_notifs[notif_idx].type);
                notif_idx = (notif_idx + 1) % SYS_NOTIF_COUNT;
                dirty = 1;
            }
        }
        prev_btn = mb;
        prev_mx = mx; prev_my = my;

        if (dirty) {
            draw_scene(mx, my);
            vga_flip();
        }
        scheduler_maybe_preempt();
    }
}
