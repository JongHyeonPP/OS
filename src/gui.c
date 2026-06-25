/*
 * gui.c  -  macOS-inspired GUI for MyOS at 800x600x32bpp
 *
 * Layout:
 *   y =   0 ..  21   Menu bar  (22 px)
 *   y =  22 .. 575   Desktop   (554 px)
 *   y = 576 .. 599   Dock      (24 px)
 */
#include "gui.h"
#include "debug.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "datetime.h"
#include "font.h"
#include "net.h"
#include "uts.h"
#include "vfs.h"
#include <stdint.h>
#include <stddef.h>
#include "gui_internal.h"

static void safari_html_decode_inline(char *s);

/* Integer cos/sin: angle in degrees [0,359], returns scaled by 100 */
int vga_cos_approx(int deg) {
    /* 90-step table scaled *100 */
    static const int ct[91] = {
        100, 99, 98, 97, 96, 94, 91, 88, 85, 82,
         78, 74, 71, 67, 62, 58, 53, 48, 44, 39,
         34, 29, 24, 19, 14,  9,  3, -3, -8,-13,
        -17,-22,-28,-33,-38,-43,-47,-52,-57,-62,
        -66,-71,-75,-79,-83,-87,-90,-93,-96,-98,
        -99,-100,-99,-98,-96,-93,-90,-87,-83,-79,
        -75,-71,-66,-62,-57,-52,-47,-43,-38,-33,
        -28,-22,-17,-13, -8, -3,  3,  9, 14, 19,
         24, 29, 34, 39, 44, 48, 53, 58, 62, 67,
         71
    };
    deg = ((deg % 360) + 360) % 360;
    if (deg <= 90)  return ct[deg];
    if (deg <= 180) return -ct[180-deg];
    if (deg <= 270) return -ct[deg-180];
    return ct[360-deg];
}
int vga_sin_approx(int deg) {
    return vga_cos_approx(deg - 90);
}

void gui_draw_circle_outline(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        vga_put_pixel(cx+x,cy+y,color); vga_put_pixel(cx+y,cy+x,color);
        vga_put_pixel(cx-y,cy+x,color); vga_put_pixel(cx-x,cy+y,color);
        vga_put_pixel(cx-x,cy-y,color); vga_put_pixel(cx-y,cy-x,color);
        vga_put_pixel(cx+y,cy-x,color); vga_put_pixel(cx+x,cy-y,color);
        y++; err += 1+2*y;
        if (2*(err-x)+1 > 0) { x--; err += 1-2*x; }
    }
}

/* =========================================================================
 * Layout constants
 * ======================================================================= */

/* Titlebar height for windows */

/* =========================================================================
 * Globals
 * ======================================================================= */
gui_window_t  g_windows[MAX_WINDOWS];
int           g_num_windows = 0;

gui_button_t  g_buttons[MAX_BUTTONS];
int           g_num_buttons = 0;

/* Status message shown inside main window */
char  g_status[80];

/* Dock notification badges: index matches s_dock_icons order */
int g_dock_badges[17] = {3, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 5, 1, 3, 0, 0, 0};
int g_dock_bounce[17] = {0};

/* App switcher overlay */
int g_switcher_visible = 0;
int g_switcher_sel = 0;

/* Music player state */
int g_music_playing = 1;
int g_music_track   = 0; /* 0-4 track index */
int g_music_vol     = 66; /* 0-100 volume % */
int g_music_tab     = 1;  /* 0=library, 1=now playing, 2=radio */

/* Widget bar (F7 toggle) */
int g_widget_visible = 0;

/* Quick Look (Space) */
int g_ql_visible = 0;
const char *g_ql_filename = "document.txt";
int g_scr_visible = 0;
int g_scr_mode    = 0;

/* Crash Reporter */
int g_crash_visible = 0;

/* System Update */
int g_update_visible = 0;

/* Focus Filter */
int g_focus_filter_visible = 0;
int g_focus_filter_mode = 0;

/* iCloud */
int g_icloud_visible = 0;

/* Bluetooth */
int g_bt_visible = 0;

/* Keyboard Shortcuts */
int g_kbshort_visible = 0;
int g_kbshort_page = 0;

/* Time Machine */
int g_timemachine_visible = 0;
int g_tm_snapshot_count = 0;
int g_tm_selected_snapshot = 0;
uint32_t g_tm_last_snapshot_tick = 0;
int g_tm_restored = 0;
int g_tm_cancelled = 0;

/* Digital Color Meter */
int g_colormeter_visible = 0;

/* Notification History */
int g_notifhist_visible = 0;
int g_notifhist_clear_count = 0;

/* WiFi */
int g_wifi_visible = 0;
int g_wifi_connecting = 0;

/* Display Settings */
int g_display_visible = 0;
int g_display_brightness = 80;

/* Sound Settings */
int g_sound_visible = 0;
int g_sound_volume = 70;
int g_sound_output_device = 0;
int g_sound_input_device = 0;

/* Activity Monitor */
int g_actmon_visible = 0;

/* FaceTime */
int g_facetime_visible = 0;
int g_facetime_calling = 0;

/* Privacy & Security */
int g_privacy_visible = 0;
int g_privacy_tab = 0;

/* Reminders */
int g_reminders_visible = 0;
int g_reminders_list = 0;

/* Calendar */
int g_calendar_visible = 0;
int g_calendar_month = 0;
int g_calendar_year = 0;

/* AirPlay */
int g_airplay_visible = 0;
int g_airplay_scan_count = 0;
uint32_t g_airplay_last_scan_tick = 0;
int g_airplay_selected = 0; /* 0=this Mac, 1=discovered receiver */


/* Siri overlay */
int      g_siri_visible  = 0;
uint32_t g_siri_birth    = 0;
char     g_siri_query[48] = {0};
int      g_siri_qlen     = 0;
int      g_siri_response = 0; /* 0=listening, 1=responding */
uint32_t g_siri_resp_tick = 0;

/* Control Center state */
int g_cc_brightness = 75; /* 0-100 */
int g_cc_volume     = 50; /* 0-100 */
int g_pref_dnd      = 0;  /* Do Not Disturb */
int g_focus_mode    = 1;  /* 1=Personal 2=Work 3=Sleep 4=Gaming 5=Driving */
int g_night_shift   = 0;  /* Night Shift warm tint */

/* Privacy indicators (macOS 12+ style dots in menu bar) */
int g_mic_in_use    = 0;  /* orange dot = mic active */
int g_cam_in_use    = 0;  /* green dot  = camera active */
int g_screen_shared = 0;  /* orange dot = screen being shared */

/* AirDrop panel */
int g_airdrop_visible  = 0;
int g_airdrop_sending  = 0;  /* 0=idle 1=scanning 2=sending */
int g_airdrop_progress = 0;
uint32_t g_airdrop_start_tick = 0;
int g_airdrop_mode = 0;      /* 0=Contacts Only, 1=Everyone */
/* Music lyrics */
/* Lock screen overlay */

/* Music lyrics */
/* Lock screen overlay */

/* Music lyrics */
/* Lock screen overlay */

/* Music lyrics */
/* Lock screen overlay */



/* Handoff state */
int g_handoff_visible = 0;
int g_handoff_tick    = 0;
int g_handoff_selected = 0;

/* Writing Tools overlay (macOS Sequoia) */
int      g_wt_visible = 0;
int      g_wt_sel     = -1;  /* -1=none, 0-5=tool selected */
int      g_wt_done    = 0;   /* 0=choosing, 1=processing, 2=done */
uint32_t g_wt_tick    = 0;
char     g_wt_result[128] = "Select editable text.";

/* Quick Note floating panel */
int  g_qn_visible = 0;
char g_qn_text[128] = "Quick Note\n----------\n";
int  g_qn_len = 22;

/* Clock window tabs: 0=Clock, 1=Alarm, 2=Timer, 3=Stopwatch */
int g_clock_tab = 0;

/* Calendar: month offset from current RTC month (-N = past, +N = future) */
int g_cal_offset  = 0;
int g_cal_sel_day = 0;      /* selected day (1-31, 0=none) */
int g_cal_popup   = 0;      /* event creation popup visible */
char g_cal_evt_input[64]={0}; /* new event text */
int  g_cal_evt_input_len=0;
/* Stored events: up to 5 per day, as "day:text" */
int  g_cal_evt_day[CAL_MAX_EVTS] = {3,10,15,22,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
char g_cal_evt_txt[CAL_MAX_EVTS][40]={
    "Team Meeting","Deadline","Birthday Party","Dentist",
    "","","","","","","","","","","","","","","","" };
int  g_cal_evt_n = 4;
/* Stopwatch: ticks at start (0 = not running) */
uint32_t g_stopwatch_start = 0;
uint32_t g_stopwatch_elapsed = 0;
int      g_stopwatch_running = 0;
/* Timer: countdown in seconds */
int g_timer_set = 300; /* 5 min default */
uint32_t g_timer_start_tick = 0;
int g_timer_running = 0;

/* =========================================================================
 * Terminal state
 * ======================================================================= */

char term_history[TERM_HIST][TERM_MAX_COL + 1];
int  term_num_lines = 0;
int  g_term_scroll  = 0; /* scroll offset from bottom (0=newest) */
char term_input[INPUT_MAX + 1];
int  term_input_len = 0;

/* =========================================================================
 * Terminal helpers
 * ======================================================================= */
void term_println(const char *s) {
    int i, j;
    if (!s) s = "";
    if (term_num_lines < TERM_HIST) {
        for (i = 0; s[i] && i < TERM_MAX_COL; i++)
            term_history[term_num_lines][i] = s[i];
        term_history[term_num_lines][i] = 0;
        term_num_lines++;
    } else {
        /* scroll: shift all lines up */
        for (i = 0; i < TERM_HIST - 1; i++) {
            for (j = 0; j <= TERM_MAX_COL; j++)
                term_history[i][j] = term_history[i+1][j];
        }
        for (i = 0; s[i] && i < TERM_MAX_COL; i++)
            term_history[TERM_HIST-1][i] = s[i];
        term_history[TERM_HIST-1][i] = 0;
    }
    /* Auto-scroll to bottom when new output arrives */
    g_term_scroll = 0;
}

/* =========================================================================
 * Calculator state
 * ======================================================================= */
int32_t g_calc_a    = 0;
int32_t g_calc_cur  = 0;
char    g_calc_op   = 0;
int     g_calc_fresh = 1;
char    g_calc_disp[16] = {'0', 0};

/* =========================================================================
 * TextEdit state
 * ======================================================================= */
char g_edit_text[TEXTEDIT_MAXCHARS] = "Welcome to TextEdit!\nStart typing...\n";
int  g_edit_len = 0;     /* computed at init */
int  g_edit_focused = 0; /* 1 = keyboard goes to TextEdit */
int  g_edit_bold = 0;
int  g_edit_italic = 0;
int  g_edit_font_size = 1; /* 0=small, 1=normal, 2=large */
uint32_t g_edit_color = 0; /* 0=black */
int  g_edit_sel_start = 0;
int  g_edit_sel_end = 0;

/* =========================================================================
 * Notes state
 * ======================================================================= */
int g_notes_sel      = 0;
int g_notes_focused  = 0;
char g_notes_titles[NOTES_COUNT][32] = {
    "Shopping list", "Work tasks", "Ideas", "Reminders", "New Note"
};
char g_notes_body[NOTES_COUNT][NOTES_MAXLEN] = {
    "- Apples\n- Bread\n- Milk\n- Coffee\nRemember:\n- MyOS demo\n- Ship it!",
    "Tasks:\n- Polish UI\n- Write tests\n- Review PR\n- Update notes",
    "Ideas:\n- Dark mode UI\n- New icons\n- Voice control\n- Cloud sync\n- Plugins",
    "Reminders:\n- Meeting 10am\n- Call dentist\n- Pay bills\n- Dog food\n- Groceries",
    ""
};
int g_notes_body_len[NOTES_COUNT] = {0, 0, 0, 0, 0};

/* =========================================================================
 * Safari state
 * ======================================================================= */
int  g_safari_url_focused = 0;
char g_safari_url[SAFARI_URL_MAX] = "about:home";
/* Multi-tab state */
int  g_safari_tab_count  = 1;
int  g_safari_active_tab = 0;
char g_safari_tab_urls[SAFARI_MAX_TABS][SAFARI_URL_MAX] = {
    "about:home", "", "", ""
};
char g_safari_tab_titles[SAFARI_MAX_TABS][SAFARI_TITLE_MAX] = {
    "MyOS Home", "", "", ""
};
char g_safari_hist_urls[SAFARI_MAX_TABS][SAFARI_HISTORY_MAX][SAFARI_URL_MAX] = {
    {{"about:home"}}
};
int  g_safari_hist_count[SAFARI_MAX_TABS] = {1, 0, 0, 0};
int  g_safari_hist_index[SAFARI_MAX_TABS] = {0, 0, 0, 0};
int  g_safari_page_state = 0; /* 0=start, 1=loaded, 2=error */
char g_safari_page_url[SAFARI_URL_MAX] = "about:home";
char g_safari_page_title[SAFARI_TITLE_MAX] = "MyOS Home";
char g_safari_page_status[SAFARI_STATUS_MAX] = "Ready";
char g_safari_page_text[SAFARI_PAGE_TEXT_MAX] = "";
static char g_safari_response_buf[SAFARI_RESPONSE_MAX];
static char g_safari_decoded_body_buf[SAFARI_RESPONSE_MAX];
static char g_safari_local_response_buf[SAFARI_RESPONSE_MAX];
static char g_safari_cookie_hosts[SAFARI_COOKIE_MAX][64];
static char g_safari_cookie_names[SAFARI_COOKIE_MAX][SAFARI_COOKIE_NAME_MAX];
static char g_safari_cookie_values[SAFARI_COOKIE_MAX][SAFARI_COOKIE_VALUE_MAX];
static int  g_safari_cookie_next = 0;
int  g_safari_page_scroll = 0;
int  g_safari_link_count = 0;
char g_safari_link_urls[SAFARI_MAX_LINKS][SAFARI_URL_MAX];
char g_safari_link_titles[SAFARI_MAX_LINKS][SAFARI_LINK_TITLE_MAX];
int  g_safari_form_count = 0;
int  g_safari_form_focused = -1;
char g_safari_form_actions[SAFARI_MAX_FORMS][SAFARI_URL_MAX];
char g_safari_form_methods[SAFARI_MAX_FORMS][8];
char g_safari_form_input_names[SAFARI_MAX_FORMS][SAFARI_FORM_NAME_MAX];
char g_safari_form_values[SAFARI_MAX_FORMS][SAFARI_FORM_VALUE_MAX];
char g_safari_form_query_prefix[SAFARI_MAX_FORMS][SAFARI_FORM_QUERY_MAX];
char g_safari_form_titles[SAFARI_MAX_FORMS][SAFARI_LINK_TITLE_MAX];
uint32_t g_safari_page_status_code = 0;
uint32_t g_safari_loaded_tick = 0;

static const safari_home_site_t g_safari_home_sites[] = {
    { "Example",     "http://example.com/",              RGB(66,133,244), 'E' },
    { "NeverSSL",    "http://neverssl.com/",             RGB(255,149,0),  'N' },
    { "CERN",        "http://info.cern.ch/",             RGB(36,41,46),   'C' },
    { "Example.org", "http://example.org/",              RGB(52,199,89),  'O' },
    { "Localhost",   "http://localhost/",                RGB(0,122,255),  'L' },
    { "Hosts",       "http://localhost/etc/hosts",       RGB(88,86,214),  'H' },
    { "Routes",      "http://localhost/proc/net/route",  RGB(255,45,85),  'R' },
    { "MyOS",        "about:home",                       RGB(80,80,80),   'M' },
};

int safari_home_site_count(void) {
    return (int)(sizeof(g_safari_home_sites) / sizeof(g_safari_home_sites[0]));
}

const safari_home_site_t *safari_home_site_at(int index) {
    if (index < 0 || index >= safari_home_site_count())
        return 0;
    return &g_safari_home_sites[index];
}

static void safari_copy(char *dst, int max, const char *src);
static void safari_history_reset_tab(int tab, const char *url);
static void safari_load_url_internal(const char *url, const char *method,
                                     const char *request_body,
                                     int record_history, int redirect_depth);

void safari_normalize_state(void) {
    if (g_safari_tab_count < 1) {
        g_safari_tab_count = 1;
        g_safari_active_tab = 0;
        g_safari_tab_urls[0][0] = 0;
        g_safari_tab_titles[0][0] = 'N';
        g_safari_tab_titles[0][1] = 'e';
        g_safari_tab_titles[0][2] = 'w';
        g_safari_tab_titles[0][3] = ' ';
        g_safari_tab_titles[0][4] = 'T';
        g_safari_tab_titles[0][5] = 'a';
        g_safari_tab_titles[0][6] = 'b';
        g_safari_tab_titles[0][7] = 0;
    }
    if (g_safari_tab_count > SAFARI_MAX_TABS)
        g_safari_tab_count = SAFARI_MAX_TABS;
    if (g_safari_active_tab < 0)
        g_safari_active_tab = 0;
    if (g_safari_active_tab >= g_safari_tab_count)
        g_safari_active_tab = g_safari_tab_count - 1;
    if (g_safari_hist_count[g_safari_active_tab] < 1) {
        safari_history_reset_tab(g_safari_active_tab,
                                 g_safari_tab_urls[g_safari_active_tab][0] ?
                                 g_safari_tab_urls[g_safari_active_tab] : "about:home");
    }
    if (g_safari_hist_count[g_safari_active_tab] > SAFARI_HISTORY_MAX)
        g_safari_hist_count[g_safari_active_tab] = SAFARI_HISTORY_MAX;
    if (g_safari_hist_index[g_safari_active_tab] < 0)
        g_safari_hist_index[g_safari_active_tab] = 0;
    if (g_safari_hist_index[g_safari_active_tab] >= g_safari_hist_count[g_safari_active_tab])
        g_safari_hist_index[g_safari_active_tab] = g_safari_hist_count[g_safari_active_tab] - 1;
    if (g_safari_form_focused >= g_safari_form_count)
        g_safari_form_focused = -1;
}


typedef struct {
    char scheme[6];
    char host[64];
    char path[192];
    uint16_t port;
    uint32_t ipv4;
    int local;
} safari_request_t;

static int safari_char_lower(int ch) {
    return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

static int safari_ieq_char(char a, char b) {
    return safari_char_lower((unsigned char)a) == safari_char_lower((unsigned char)b);
}

static int safari_starts_with_ci(const char *s, const char *prefix) {
    int i = 0;
    if (!s || !prefix) return 0;
    while (prefix[i]) {
        if (!s[i] || !safari_ieq_char(s[i], prefix[i])) return 0;
        i++;
    }
    return 1;
}

static int safari_eq_ci(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (!safari_ieq_char(a[i], b[i])) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

int safari_url_has_scheme(const char *url, const char *scheme) {
    int i = 0;
    if (!url || !scheme) return 0;
    while (scheme[i]) {
        if (!url[i] || !safari_ieq_char(url[i], scheme[i])) return 0;
        i++;
    }
    return url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/';
}

static void safari_copy(char *dst, int max, const char *src) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void safari_append(char *dst, int *pos, int max, const char *src) {
    int i = 0;
    if (!dst || !pos || max <= 0 || !src) return;
    while (src[i] && *pos + 1 < max) {
        dst[*pos] = src[i];
        (*pos)++;
        i++;
    }
    dst[*pos] = 0;
}

static void safari_append_uint(char *dst, int *pos, int max, uint32_t value) {
    char nbuf[12];
    runtime_format_uint(value, nbuf, sizeof(nbuf));
    safari_append(dst, pos, max, nbuf);
}

static void safari_append_ipv4(char *dst, int *pos, int max, uint32_t ipv4) {
    char ipbuf[18];
    runtime_format_ipv4(ipv4, ipbuf, sizeof(ipbuf));
    safari_append(dst, pos, max, ipbuf);
}

static int safari_is_hex_digit_char(char ch) {
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static int safari_url_char_needs_escape(char ch) {
    unsigned char u = (unsigned char)ch;
    return u <= 32U || u >= 127U ||
           ch == '"' || ch == '<' || ch == '>' || ch == '\\' ||
           ch == '^' || ch == '`' || ch == '{' || ch == '|' || ch == '}';
}

static void safari_append_pct_encoded_byte(char *dst, int *pos, int max, unsigned char value) {
    static const char hex[] = "0123456789ABCDEF";
    if (!dst || !pos || *pos + 3 >= max) return;
    dst[(*pos)++] = '%';
    dst[(*pos)++] = hex[(value >> 4) & 0x0F];
    dst[(*pos)++] = hex[value & 0x0F];
    dst[*pos] = 0;
}

static void safari_url_encode_component(const char *src, char *dst, int max) {
    int pos = 0;
    int i = 0;
    if (!dst || max <= 0) return;
    dst[0] = 0;
    if (!src) return;
    while (src[i] && pos + 1 < max) {
        char ch = src[i++];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            dst[pos++] = ch;
            dst[pos] = 0;
        } else if (ch == ' ') {
            if (pos + 1 >= max) break;
            dst[pos++] = '+';
            dst[pos] = 0;
        } else {
            safari_append_pct_encoded_byte(dst, &pos, max, (unsigned char)ch);
        }
    }
}

static int safari_text_has_whitespace(const char *s) {
    int i;
    if (!s) return 0;
    for (i = 0; s[i]; i++) {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
            return 1;
    }
    return 0;
}

static void safari_make_search_url(const char *query, char *out, int max) {
    char encoded[SAFARI_URL_MAX];
    int pos = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    safari_url_encode_component(query, encoded, sizeof(encoded));
    safari_append(out, &pos, max, "http://frogfind.com/?q=");
    safari_append(out, &pos, max, encoded);
}

static void safari_normalize_path_segments(const char *path, char *out, int max) {
    const char *suffix = 0;
    int pos = 0;
    int i = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    if (!path || !path[0]) {
        safari_copy(out, max, "/");
        return;
    }
    while (path[i] && path[i] != '?' && path[i] != '#') i++;
    if (path[i]) suffix = path + i;
    i = 0;
    out[pos++] = '/';
    out[pos] = 0;
    while (path[i] && path + i != suffix) {
        const char *start;
        int len = 0;
        while (path[i] == '/') i++;
        start = path + i;
        while (path[i] && path + i != suffix && path[i] != '/') {
            i++;
            len++;
        }
        if (len == 0 || (len == 1 && start[0] == '.')) {
            continue;
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (pos > 1) {
                if (out[pos - 1] == '/') pos--;
                while (pos > 1 && out[pos - 1] != '/') pos--;
                out[pos] = 0;
            }
            continue;
        }
        if (pos > 1 && out[pos - 1] != '/' && pos + 1 < max)
            out[pos++] = '/';
        while (len-- > 0 && pos + 1 < max)
            out[pos++] = *start++;
        out[pos] = 0;
    }
    if (pos == 0) {
        out[pos++] = '/';
        out[pos] = 0;
    }
    if (suffix)
        safari_append(out, &pos, max, suffix);
}

static void safari_append_host_header(char *dst, int *pos, int max, const safari_request_t *req) {
    if (!req) return;
    safari_append(dst, pos, max, req->host);
    if ((str_eq(req->scheme, "http") && req->port != 80) ||
        (str_eq(req->scheme, "https") && req->port != 443)) {
        safari_append(dst, pos, max, ":");
        safari_append_uint(dst, pos, max, req->port);
    }
}

static int safari_parse_ipv4_text(const char *s, uint32_t *out) {
    uint32_t octets[4];
    int i;
    if (!s || !out) return -1;
    for (i = 0; i < 4; i++) {
        uint32_t value = 0;
        int any = 0;
        while (*s >= '0' && *s <= '9') {
            value = value * 10U + (uint32_t)(*s - '0');
            if (value > 255U) return -1;
            any = 1;
            s++;
        }
        if (!any) return -1;
        octets[i] = value;
        if (i < 3) {
            if (*s != '.') return -1;
            s++;
        }
    }
    if (*s) return -1;
    *out = (octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3];
    return 0;
}

static int safari_host_token_eq(const char *buf, int start, int end, const char *host) {
    int i = 0;
    if (!buf || !host || start >= end) return 0;
    while (start + i < end && host[i]) {
        if (!safari_ieq_char(buf[start + i], host[i])) return 0;
        i++;
    }
    return start + i == end && host[i] == 0;
}

static int safari_resolve_hosts_file(const char *host, uint32_t *out) {
    int fd;
    int n;
    int i = 0;
    char buf[512];
    if (!host || !out) return -1;
    fd = vfs_open("/etc/hosts", VFS_O_RDONLY);
    if (fd < 0) return -1;
    n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    while (i < n) {
        char ipbuf[18];
        int ip_pos = 0;
        uint32_t ip = 0;
        while (i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++;
        if (i >= n) break;
        if (buf[i] == '#') {
            while (i < n && buf[i] != '\n') i++;
            continue;
        }
        while (i < n && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\r' && buf[i] != '\n' && ip_pos + 1 < (int)sizeof(ipbuf))
            ipbuf[ip_pos++] = buf[i++];
        ipbuf[ip_pos] = 0;
        if (safari_parse_ipv4_text(ipbuf, &ip) < 0) {
            while (i < n && buf[i] != '\n') i++;
            continue;
        }
        while (i < n && buf[i] != '\n') {
            int start;
            int end;
            while (i < n && (buf[i] == ' ' || buf[i] == '\t')) i++;
            start = i;
            while (i < n && buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\r' && buf[i] != '\n') i++;
            end = i;
            if (safari_host_token_eq(buf, start, end, host)) {
                *out = ip;
                return 0;
            }
        }
    }
    return -1;
}

static void safari_dns_put16(uint8_t *buf, int off, uint16_t value) {
    buf[off] = (uint8_t)(value >> 8);
    buf[off + 1] = (uint8_t)(value & 0xFFU);
}

static uint16_t safari_dns_get16(const uint8_t *buf, int off) {
    return (uint16_t)(((uint16_t)buf[off] << 8) | buf[off + 1]);
}

static int safari_dns_encode_name(const char *host, uint8_t *packet, int *pos, int max) {
    const char *p = host;
    if (!host || !packet || !pos) return -1;
    while (*p) {
        int len = 0;
        const char *label = p;
        while (p[len] && p[len] != '.') len++;
        if (len <= 0 || len > 63 || *pos + len + 2 >= max) return -1;
        packet[(*pos)++] = (uint8_t)len;
        while (len--) packet[(*pos)++] = (uint8_t)*label++;
        p = label;
        if (*p == '.') p++;
    }
    if (*pos + 1 >= max) return -1;
    packet[(*pos)++] = 0;
    return 0;
}

static int safari_dns_read_name(const uint8_t *packet, int len, int *off, char *out, int max) {
    int cur;
    int pos = 0;
    int jumped = 0;
    int jumps = 0;
    if (!packet || !off || !out || max <= 0) return -1;
    out[0] = 0;
    cur = *off;
    while (cur < len && jumps < 16) {
        uint8_t label = packet[cur];
        if ((label & 0xC0U) == 0xC0U) {
            int ptr;
            if (cur + 1 >= len) return -1;
            ptr = ((label & 0x3FU) << 8) | packet[cur + 1];
            if (ptr < 0 || ptr >= len) return -1;
            if (!jumped) *off = cur + 2;
            cur = ptr;
            jumped = 1;
            jumps++;
            continue;
        }
        if (label & 0xC0U) return -1;
        cur++;
        if (label == 0) {
            if (!jumped) *off = cur;
            out[pos] = 0;
            return 0;
        }
        if (cur + label > len) return -1;
        if (pos > 0 && pos + 1 < max) out[pos++] = '.';
        {
            int label_len = label;
            int i;
            for (i = 0; i < label_len; i++) {
                char ch = (char)packet[cur + i];
                if (pos + 1 < max) out[pos++] = (char)safari_char_lower((unsigned char)ch);
            }
            cur += label_len;
        }
    }
    return -1;
}

static int safari_dns_skip_name(const uint8_t *packet, int len, int *off) {
    char tmp[SAFARI_URL_MAX];
    return safari_dns_read_name(packet, len, off, tmp, sizeof(tmp));
}

static int safari_dns_parse_a_response(const uint8_t *packet, int len, uint16_t id,
                                       const char *host, uint32_t *out, char *cname, int cname_max) {
    int off = 12;
    uint16_t qd;
    uint16_t an;
    uint16_t ns;
    uint16_t ar;
    uint16_t flags;
    uint32_t i;
    uint32_t rr_count;
    int have_cname = 0;
    if (!packet || len < 12 || !out || !host) return -1;
    if (cname && cname_max > 0) cname[0] = 0;
    if (safari_dns_get16(packet, 0) != id) return -1;
    flags = safari_dns_get16(packet, 2);
    if ((flags & 0x8000U) == 0 || (flags & 0x000FU) != 0) return -1;
    qd = safari_dns_get16(packet, 4);
    an = safari_dns_get16(packet, 6);
    ns = safari_dns_get16(packet, 8);
    ar = safari_dns_get16(packet, 10);
    for (i = 0; i < qd; i++) {
        if (safari_dns_skip_name(packet, len, &off) < 0 || off + 4 > len) return -1;
        off += 4;
    }
    rr_count = (uint32_t)an + (uint32_t)ns + (uint32_t)ar;
    for (i = 0; i < rr_count; i++) {
        char rr_name[SAFARI_URL_MAX];
        uint16_t type;
        uint16_t cls;
        uint16_t rdlen;
        if (safari_dns_read_name(packet, len, &off, rr_name, sizeof(rr_name)) < 0 || off + 10 > len)
            return -1;
        type = safari_dns_get16(packet, off);
        cls = safari_dns_get16(packet, off + 2);
        rdlen = safari_dns_get16(packet, off + 8);
        off += 10;
        if (off + rdlen > len) return -1;
        if (type == 5U && cls == 1U && cname && cname_max > 0 && str_eq(rr_name, host)) {
            int name_off = off;
            if (safari_dns_read_name(packet, len, &name_off, cname, cname_max) == 0 && cname[0])
                have_cname = 1;
        }
        if (type == 1U && cls == 1U && rdlen == 4U &&
            (str_eq(rr_name, host) || (cname && cname[0] && str_eq(rr_name, cname)))) {
            *out = ((uint32_t)packet[off] << 24) | ((uint32_t)packet[off + 1] << 16) |
                   ((uint32_t)packet[off + 2] << 8) | packet[off + 3];
            return 0;
        }
        off += rdlen;
    }
    return have_cname ? 1 : -1;
}

static int safari_dns_query4_depth(const char *host, uint32_t *out, int depth) {
    uint8_t query[NET_UDP_PAYLOAD_MAX];
    uint8_t response[NET_UDP_PAYLOAD_MAX];
    uint32_t dns = runtime_dns_server4();
    uint32_t src_ip = 0;
    uint16_t src_port = 0;
    uint16_t id;
    int fd;
    int pos = 12;
    int attempt;
    if (!host || !out || !dns || depth > 4) return -1;
    id = (uint16_t)(0x5A00U ^ (timer_ticks() & 0xFFFFU));
    for (pos = 0; pos < (int)sizeof(query); pos++) query[pos] = 0;
    pos = 12;
    safari_dns_put16(query, 0, id);
    safari_dns_put16(query, 2, 0x0100U);
    safari_dns_put16(query, 4, 1U);
    if (safari_dns_encode_name(host, query, &pos, sizeof(query)) < 0 || pos + 4 > (int)sizeof(query))
        return -1;
    safari_dns_put16(query, pos, 1U); pos += 2;
    safari_dns_put16(query, pos, 1U); pos += 2;
    fd = vfs_socket_udp4();
    if (fd < 0) return -1;
    for (attempt = 0; attempt < 4; attempt++) {
        int wait;
        (void)vfs_socket_sendto_udp4(fd, dns, 53, query, (uint32_t)pos);
        for (wait = 0; wait < 80; wait++) {
            int n;
            net_poll();
            n = vfs_socket_recvfrom_udp4(fd, &src_ip, &src_port, response, sizeof(response));
            if (n <= 0) continue;
            if (src_ip != dns || src_port != 53) continue;
            {
                char cname[SAFARI_URL_MAX];
                int parsed_dns = safari_dns_parse_a_response(response, n, id, host, out, cname, sizeof(cname));
                if (parsed_dns == 0) {
                    vfs_close(fd);
                    return 0;
                }
                if (parsed_dns == 1 && cname[0]) {
                    vfs_close(fd);
                    return safari_dns_query4_depth(cname, out, depth + 1);
                }
            }
        }
    }
    vfs_close(fd);
    return -1;
}

static int safari_dns_query4(const char *host, uint32_t *out) {
    return safari_dns_query4_depth(host, out, 0);
}

static int safari_host_resolve4(const char *host, uint32_t *out) {
    uint32_t i;
    if (!host || !out || !host[0]) return -1;
    if (safari_parse_ipv4_text(host, out) == 0) return 0;
    if (str_eq(host, "localhost")) {
        *out = 0x7F000001U;
        return 0;
    }
    if (str_eq(host, uts_nodename())) {
        *out = 0x7F000101U;
        return 0;
    }
    if (safari_resolve_hosts_file(host, out) == 0) return 0;
    for (i = 0; i < netif_count(); i++) {
        const netif_t *iface = netif_at(i);
        if (iface && iface->name && iface->ipv4 && str_eq(host, iface->name)) {
            *out = iface->ipv4;
            return 0;
        }
    }
    return safari_dns_query4(host, out);
}

int safari_is_home_url(const char *url) {
    return !url || !url[0] || str_eq(url, "about:home") || str_eq(url, "about:blank");
}

static void safari_normalize_url_text(const char *input, char *out, int max) {
    int pos = 0;
    const char *p = input;
    char trimmed[SAFARI_URL_MAX];
    int len = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    while (p && (*p == ' ' || *p == '\t')) p++;
    if (!p || !*p) {
        safari_copy(out, max, "about:home");
        return;
    }
    while (p[len] && len + 1 < (int)sizeof(trimmed)) {
        trimmed[len] = p[len];
        len++;
    }
    while (len > 0 && (trimmed[len - 1] == ' ' || trimmed[len - 1] == '\t' ||
                       trimmed[len - 1] == '\r' || trimmed[len - 1] == '\n'))
        len--;
    trimmed[len] = 0;
    if (!trimmed[0]) {
        safari_copy(out, max, "about:home");
        return;
    }
    p = trimmed;
    if (safari_starts_with_ci(p, "about:") ||
        safari_starts_with_ci(p, "http://") ||
        safari_starts_with_ci(p, "https://")) {
        safari_copy(out, max, p);
        return;
    }
    if (p[0] == '/') {
        safari_append(out, &pos, max, "http://localhost");
        safari_append(out, &pos, max, p);
        return;
    }
    if (safari_text_has_whitespace(p)) {
        safari_make_search_url(p, out, max);
        return;
    }
    safari_append(out, &pos, max, "http://");
    safari_append(out, &pos, max, p);
}

static void safari_set_tab_title(const char *title) {
    safari_normalize_state();
    safari_copy(g_safari_tab_titles[g_safari_active_tab], SAFARI_TITLE_MAX,
                title && title[0] ? title : "New Tab");
}

static void safari_history_reset_tab(int tab, const char *url) {
    int i;
    char normalized[SAFARI_URL_MAX];
    if (tab < 0 || tab >= SAFARI_MAX_TABS) return;
    safari_normalize_url_text(url, normalized, sizeof(normalized));
    safari_copy(g_safari_hist_urls[tab][0], SAFARI_URL_MAX, normalized);
    for (i = 1; i < SAFARI_HISTORY_MAX; i++) g_safari_hist_urls[tab][i][0] = 0;
    g_safari_hist_count[tab] = 1;
    g_safari_hist_index[tab] = 0;
}

void safari_reset_tab_state(int tab, const char *url) {
    char normalized[SAFARI_URL_MAX];
    if (tab < 0 || tab >= SAFARI_MAX_TABS) return;
    safari_normalize_url_text(url, normalized, sizeof(normalized));
    safari_copy(g_safari_tab_urls[tab], SAFARI_URL_MAX, normalized);
    safari_copy(g_safari_tab_titles[tab], SAFARI_TITLE_MAX,
                safari_is_home_url(normalized) ? "MyOS Home" : "New Tab");
    safari_history_reset_tab(tab, normalized);
}

void safari_copy_tab_state(int dst, int src) {
    int i;
    if (dst < 0 || dst >= SAFARI_MAX_TABS || src < 0 || src >= SAFARI_MAX_TABS) return;
    safari_copy(g_safari_tab_urls[dst], SAFARI_URL_MAX, g_safari_tab_urls[src]);
    safari_copy(g_safari_tab_titles[dst], SAFARI_TITLE_MAX, g_safari_tab_titles[src]);
    g_safari_hist_count[dst] = g_safari_hist_count[src];
    g_safari_hist_index[dst] = g_safari_hist_index[src];
    for (i = 0; i < SAFARI_HISTORY_MAX; i++)
        safari_copy(g_safari_hist_urls[dst][i], SAFARI_URL_MAX, g_safari_hist_urls[src][i]);
}

static void safari_history_push_url(const char *url) {
    int tab;
    int i;
    char normalized[SAFARI_URL_MAX];
    safari_normalize_state();
    tab = g_safari_active_tab;
    safari_normalize_url_text(url, normalized, sizeof(normalized));
    if (g_safari_hist_count[tab] < 1) safari_history_reset_tab(tab, normalized);
    if (str_eq(g_safari_hist_urls[tab][g_safari_hist_index[tab]], normalized)) return;
    if (g_safari_hist_index[tab] + 1 < g_safari_hist_count[tab])
        g_safari_hist_count[tab] = g_safari_hist_index[tab] + 1;
    if (g_safari_hist_count[tab] >= SAFARI_HISTORY_MAX) {
        for (i = 1; i < SAFARI_HISTORY_MAX; i++)
            safari_copy(g_safari_hist_urls[tab][i - 1], SAFARI_URL_MAX, g_safari_hist_urls[tab][i]);
        g_safari_hist_count[tab] = SAFARI_HISTORY_MAX - 1;
        if (g_safari_hist_index[tab] > 0) g_safari_hist_index[tab]--;
    }
    safari_copy(g_safari_hist_urls[tab][g_safari_hist_count[tab]], SAFARI_URL_MAX, normalized);
    g_safari_hist_index[tab] = g_safari_hist_count[tab];
    g_safari_hist_count[tab]++;
}

int safari_can_go_back(void) {
    safari_normalize_state();
    return g_safari_hist_index[g_safari_active_tab] > 0;
}

int safari_can_go_forward(void) {
    safari_normalize_state();
    return g_safari_hist_index[g_safari_active_tab] + 1 < g_safari_hist_count[g_safari_active_tab];
}

void safari_go_back(void) {
    safari_normalize_state();
    if (!safari_can_go_back()) return;
    g_safari_hist_index[g_safari_active_tab]--;
    safari_load_url_internal(g_safari_hist_urls[g_safari_active_tab][g_safari_hist_index[g_safari_active_tab]], 0, 0, 0, 0);
}

void safari_go_forward(void) {
    safari_normalize_state();
    if (!safari_can_go_forward()) return;
    g_safari_hist_index[g_safari_active_tab]++;
    safari_load_url_internal(g_safari_hist_urls[g_safari_active_tab][g_safari_hist_index[g_safari_active_tab]], 0, 0, 0, 0);
}

static int safari_parse_url(const char *url, safari_request_t *req) {
    char normalized[SAFARI_URL_MAX];
    const char *p;
    int path_pos = 0;
    int host_len = 0;
    uint32_t ip = 0;
    if (!req) return -1;
    req->scheme[0] = 0;
    req->host[0] = 0;
    req->path[0] = '/';
    req->path[1] = 0;
    req->port = 80;
    req->ipv4 = 0;
    req->local = 0;
    safari_normalize_url_text(url, normalized, sizeof(normalized));
    if (safari_is_home_url(normalized)) return 1;
    if (safari_starts_with_ci(normalized, "https://")) {
        safari_copy(req->scheme, sizeof(req->scheme), "https");
        p = normalized + 8;
        req->port = 443;
    } else if (safari_starts_with_ci(normalized, "http://")) {
        safari_copy(req->scheme, sizeof(req->scheme), "http");
        p = normalized + 7;
    } else {
        return -1;
    }
    while (p[host_len] && p[host_len] != '/' && p[host_len] != ':' &&
           p[host_len] != '?' && p[host_len] != '#') {
        if (host_len + 1 >= (int)sizeof(req->host)) return -1;
        req->host[host_len] = (char)safari_char_lower((unsigned char)p[host_len]);
        host_len++;
    }
    req->host[host_len] = 0;
    p += host_len;
    if (*p == ':') {
        uint32_t port = 0;
        int any = 0;
        p++;
        while (*p >= '0' && *p <= '9') {
            port = port * 10U + (uint32_t)(*p - '0');
            if (port > 65535U) return -1;
            any = 1;
            p++;
        }
        if (!any || port == 0) return -1;
        req->port = (uint16_t)port;
    }
    if (*p == '/' || *p == '?' || *p == '#') {
        if (*p != '/') {
            req->path[path_pos++] = '/';
        }
        while (*p && *p != '#' && path_pos + 1 < (int)sizeof(req->path)) {
            if (*p == '%' && safari_is_hex_digit_char(p[1]) && safari_is_hex_digit_char(p[2]) &&
                path_pos + 3 < (int)sizeof(req->path)) {
                req->path[path_pos++] = *p++;
                req->path[path_pos++] = *p++;
                req->path[path_pos++] = *p++;
                req->path[path_pos] = 0;
            } else if (safari_url_char_needs_escape(*p)) {
                if (path_pos + 3 >= (int)sizeof(req->path)) break;
                safari_append_pct_encoded_byte(req->path, &path_pos, sizeof(req->path), (unsigned char)*p++);
            } else {
                req->path[path_pos++] = *p++;
                req->path[path_pos] = 0;
            }
        }
        req->path[path_pos] = 0;
    }
    if (!req->host[0]) return -1;
    if (safari_host_resolve4(req->host, &ip) < 0) return -2;
    req->ipv4 = ip;
    req->local = ((ip & 0xFF000000U) == 0x7F000000U);
    return 0;
}

static void safari_make_local_body(const safari_request_t *req, const char *request, char *out, int max) {
    int pos = 0;
    int fd;
    char filebuf[512];
    int n = -1;
    const char *path = req ? req->path : "/";
    (void)request;
    if (!out || max <= 0) return;
    out[0] = 0;
    if (!path || !path[0] || str_eq(path, "/") || str_eq(path, "/index.html")) {
        runtime_system_info_t sys;
        runtime_storage_info_t storage;
        net_stats_t stats;
        runtime_get_system_info(&sys);
        runtime_get_storage_info("/", &storage);
        net_stats(&stats);
        safari_append(out, &pos, max, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n");
        safari_append(out, &pos, max, "<html><title>MyOS Local Web</title><body>");
        safari_append(out, &pos, max, "<h1>MyOS Local Web</h1>");
        safari_append(out, &pos, max, "<p>Hostname: ");
        safari_append(out, &pos, max, sys.nodename);
        safari_append(out, &pos, max, "</p><p>Kernel: ");
        safari_append(out, &pos, max, sys.sysname);
        safari_append(out, &pos, max, " ");
        safari_append(out, &pos, max, sys.release);
        safari_append(out, &pos, max, "</p><p>Network packets: tx=");
        safari_append_uint(out, &pos, max, stats.tx_packets);
        safari_append(out, &pos, max, " rx=");
        safari_append_uint(out, &pos, max, stats.rx_packets);
        safari_append(out, &pos, max, "</p><p>Storage free: ");
        {
            char bbuf[18];
            runtime_format_bytes(storage.free_bytes, bbuf, sizeof(bbuf));
            safari_append(out, &pos, max, bbuf);
        }
        safari_append(out, &pos, max, "</p><p>Try /etc/hosts or /proc/net/hosts.</p></body></html>");
        return;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd >= 0) {
        n = vfs_read(fd, filebuf, sizeof(filebuf) - 1);
        vfs_close(fd);
    }
    if (n >= 0) {
        filebuf[n] = 0;
        safari_append(out, &pos, max, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
        safari_append(out, &pos, max, "<html><title>");
        safari_append(out, &pos, max, path);
        safari_append(out, &pos, max, "</title><body><pre>");
        safari_append(out, &pos, max, filebuf);
        safari_append(out, &pos, max, "</pre></body></html>");
        return;
    }
    safari_append(out, &pos, max, "HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n");
    safari_append(out, &pos, max, "<html><title>Not Found</title><body><h1>Not Found</h1><p>");
    safari_append(out, &pos, max, path);
    safari_append(out, &pos, max, "</p></body></html>");
}



static int safari_header_name_eq(const char *line, const char *name);

static void safari_append_cookie_header(char *request, int *pos, int max, const char *host) {
    int i;
    int wrote = 0;
    if (!request || !pos || !host || !host[0]) return;
    for (i = 0; i < SAFARI_COOKIE_MAX; i++) {
        if (!g_safari_cookie_hosts[i][0] || !g_safari_cookie_names[i][0]) continue;
        if (!safari_eq_ci(g_safari_cookie_hosts[i], host)) continue;
        if (!wrote) {
            safari_append(request, pos, max, "\r\nCookie: ");
            wrote = 1;
        } else {
            safari_append(request, pos, max, "; ");
        }
        safari_append(request, pos, max, g_safari_cookie_names[i]);
        safari_append(request, pos, max, "=");
        safari_append(request, pos, max, g_safari_cookie_values[i]);
    }
}

static void safari_cookie_clear_slot(int slot) {
    if (slot < 0 || slot >= SAFARI_COOKIE_MAX) return;
    g_safari_cookie_hosts[slot][0] = 0;
    g_safari_cookie_names[slot][0] = 0;
    g_safari_cookie_values[slot][0] = 0;
}

static int safari_cookie_slot_for(const char *host, const char *name) {
    int i;
    int empty = -1;
    if (!host || !name || !host[0] || !name[0]) return -1;
    for (i = 0; i < SAFARI_COOKIE_MAX; i++) {
        if (g_safari_cookie_hosts[i][0] && g_safari_cookie_names[i][0] &&
            safari_eq_ci(g_safari_cookie_hosts[i], host) &&
            str_eq(g_safari_cookie_names[i], name))
            return i;
        if (empty < 0 && !g_safari_cookie_names[i][0]) empty = i;
    }
    if (empty >= 0) return empty;
    i = g_safari_cookie_next++ % SAFARI_COOKIE_MAX;
    if (g_safari_cookie_next < 0) g_safari_cookie_next = 0;
    return i;
}

static void safari_store_cookie_pair(const char *host, const char *name,
                                     const char *value, int clear_cookie) {
    int slot = safari_cookie_slot_for(host, name);
    if (slot < 0) return;
    if (clear_cookie) {
        safari_cookie_clear_slot(slot);
        return;
    }
    safari_copy(g_safari_cookie_hosts[slot], sizeof(g_safari_cookie_hosts[slot]), host);
    safari_copy(g_safari_cookie_names[slot], SAFARI_COOKIE_NAME_MAX, name);
    safari_copy(g_safari_cookie_values[slot], SAFARI_COOKIE_VALUE_MAX, value ? value : "");
}

static int safari_cookie_attr_has_max_age_zero(const char *line, int len) {
    int i;
    for (i = 0; i + 8 < len; i++) {
        if (safari_starts_with_ci(line + i, "max-age")) {
            const char *p = line + i + 7;
            while (p < line + len && (*p == ' ' || *p == '\t')) p++;
            if (p < line + len && *p == '=') p++;
            while (p < line + len && (*p == ' ' || *p == '\t')) p++;
            return p < line + len && *p == '0';
        }
    }
    return 0;
}

static void safari_store_set_cookie_line(const char *host, const char *line, int len) {
    char name[SAFARI_COOKIE_NAME_MAX];
    char value[SAFARI_COOKIE_VALUE_MAX];
    int ni = 0;
    int vi = 0;
    int i = 0;
    int clear_cookie;
    if (!host || !host[0] || !line || len <= 0) return;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    while (i < len && line[i] && line[i] != '=' && line[i] != ';' &&
           line[i] != '\r' && line[i] != '\n' && ni + 1 < (int)sizeof(name)) {
        name[ni++] = line[i++];
    }
    name[ni] = 0;
    while (ni > 0 && (name[ni - 1] == ' ' || name[ni - 1] == '\t')) name[--ni] = 0;
    if (i >= len || line[i] != '=' || !name[0]) return;
    i++;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    while (i < len && line[i] && line[i] != ';' && line[i] != '\r' &&
           line[i] != '\n' && vi + 1 < (int)sizeof(value)) {
        value[vi++] = line[i++];
    }
    while (vi > 0 && (value[vi - 1] == ' ' || value[vi - 1] == '\t')) vi--;
    value[vi] = 0;
    clear_cookie = (vi == 0) || safari_cookie_attr_has_max_age_zero(line, len);
    safari_store_cookie_pair(host, name, value, clear_cookie);
}

static void safari_store_cookies_from_response(const safari_request_t *req, const char *response) {
    const char *p = response;
    if (!req || !req->host[0] || !response) return;
    while (*p) {
        if ((p[0] == '\r' && p[1] == '\n') || p[0] == '\n') break;
        if (safari_header_name_eq(p, "Set-Cookie")) {
            const char *v = p;
            int len = 0;
            while (*v && *v != ':') v++;
            if (*v == ':') v++;
            while (*v == ' ' || *v == '\t') v++;
            while (v[len] && v[len] != '\r' && v[len] != '\n') len++;
            safari_store_set_cookie_line(req->host, v, len);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

static void safari_append_http_request(char *request, int *pos, int max,
                                       const safari_request_t *req,
                                       const char *method,
                                       const char *body,
                                       int raw_network) {
    const char *verb = (method && method[0]) ? method : "GET";
    int body_len = body ? str_len(body) : 0;
    safari_append(request, pos, max, verb);
    safari_append(request, pos, max, " ");
    safari_append(request, pos, max, req->path);
    safari_append(request, pos, max, " HTTP/1.1\r\nHost: ");
    safari_append_host_header(request, pos, max, req);
    if (raw_network)
        safari_append(request, pos, max, "\r\nUser-Agent: MyOS-Safari/1.0\r\nAccept: text/html,text/plain,*/*");
    safari_append_cookie_header(request, pos, max, req->host);
    safari_append(request, pos, max, "\r\nAccept-Encoding: identity\r\nConnection: close");
    if (safari_eq_ci(verb, "POST")) {
        safari_append(request, pos, max, "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: ");
        safari_append_uint(request, pos, max, (uint32_t)body_len);
    }
    safari_append(request, pos, max, "\r\n\r\n");
    if (safari_eq_ci(verb, "POST") && body_len > 0)
        safari_append(request, pos, max, body);
}

static int safari_fetch_local_http(const safari_request_t *req, const char *method,
                                   const char *request_body, char *response, int max) {
    int server = -1;
    int client = -1;
    int accepted = -1;
    int total = 0;
    int off = 0;
    int n;
    char request[512];
    char server_req[512];
    char *local_response = g_safari_local_response_buf;
    int rp = 0;
    if (!req || !response || max <= 0) return -1;
    response[0] = 0;
    server = vfs_socket_tcp4();
    client = vfs_socket_tcp4();
    if (server < 0 || client < 0) goto fail;
    if (vfs_socket_bind_tcp4(server, req->port) < 0 ||
        vfs_socket_listen_tcp4(server) < 0) goto fail;
    if (vfs_socket_connect_tcp4(client, req->ipv4, req->port) < 0) goto fail;
    accepted = vfs_socket_accept_tcp4(server);
    if (accepted < 0) goto fail;
    request[0] = 0;
    safari_append_http_request(request, &rp, sizeof(request), req, method, request_body, 0);
    if (vfs_write(client, request, (uint32_t)str_len(request)) <= 0) goto fail;
    n = vfs_read(accepted, server_req, sizeof(server_req) - 1);
    if (n < 0) goto fail;
    server_req[n] = 0;
    safari_make_local_body(req, server_req, local_response, SAFARI_RESPONSE_MAX);
    while (local_response[off] && total + 1 < max) {
        n = vfs_write(accepted, local_response + off, (uint32_t)str_len(local_response + off));
        if (n <= 0) break;
        off += n;
        n = vfs_read(client, response + total, (uint32_t)(max - total - 1));
        if (n > 0) total += n;
    }
    while (total + 1 < max && (n = vfs_read(client, response + total, (uint32_t)(max - total - 1))) > 0)
        total += n;
    response[total] = 0;
    vfs_close(accepted);
    vfs_close(client);
    vfs_close(server);
    return total;
fail:
    if (accepted >= 0) vfs_close(accepted);
    if (client >= 0) vfs_close(client);
    if (server >= 0) vfs_close(server);
    return -1;
}

static void safari_tls_put8(uint8_t *buf, int *pos, int max, uint8_t value) {
    if (!buf || !pos || *pos >= max) return;
    buf[(*pos)++] = value;
}

static void safari_tls_put16(uint8_t *buf, int *pos, int max, uint16_t value) {
    if (!buf || !pos || *pos + 1 >= max) return;
    buf[(*pos)++] = (uint8_t)(value >> 8);
    buf[(*pos)++] = (uint8_t)(value & 0xFFU);
}

static void safari_tls_put24_at(uint8_t *buf, int off, uint32_t value) {
    if (!buf || off < 0) return;
    buf[off] = (uint8_t)((value >> 16) & 0xFFU);
    buf[off + 1] = (uint8_t)((value >> 8) & 0xFFU);
    buf[off + 2] = (uint8_t)(value & 0xFFU);
}

static void safari_tls_put16_at(uint8_t *buf, int off, uint16_t value) {
    if (!buf || off < 0) return;
    buf[off] = (uint8_t)(value >> 8);
    buf[off + 1] = (uint8_t)(value & 0xFFU);
}

static void safari_tls_append_bytes(uint8_t *buf, int *pos, int max, const void *src, int len) {
    int i;
    if (!buf || !pos || !src || len <= 0) return;
    for (i = 0; i < len && *pos < max; i++)
        buf[(*pos)++] = ((const uint8_t *)src)[i];
}

static void safari_tls_random(uint8_t *out, int len) {
    uint32_t seed = timer_ticks() ^ 0xA5A55A5AU;
    int i;
    if (!out || len <= 0) return;
    if (vfs_getrandom(out, (uint32_t)len) == len) return;
    for (i = 0; i < len; i++) {
        seed = seed * 1664525U + 1013904223U + (uint32_t)i;
        out[i] = (uint8_t)(seed >> 24);
    }
}



typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t buf[64];
    uint32_t used;
} safari_sha256_ctx_t;

static uint32_t safari_rotr32(uint32_t v, int n) {
    return (v >> n) | (v << (32 - n));
}

static uint32_t safari_load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void safari_store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void safari_store_be64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 7; i >= 0; i--) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void safari_sha256_transform(safari_sha256_ctx_t *ctx, const uint8_t block[64]) {
    static const uint32_t k[64] = {
        0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
        0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
        0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
        0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
        0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
        0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
        0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
        0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
        0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
        0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
        0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
        0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
        0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
        0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
        0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
        0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
    };
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = safari_load_be32(block + i * 4);
    for (i = 16; i < 64; i++) {
        uint32_t s0 = safari_rotr32(w[i - 15], 7) ^ safari_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = safari_rotr32(w[i - 2], 17) ^ safari_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = ctx->h[0]; b = ctx->h[1]; c = ctx->h[2]; d = ctx->h[3];
    e = ctx->h[4]; f = ctx->h[5]; g = ctx->h[6]; h = ctx->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t s1 = safari_rotr32(e, 6) ^ safari_rotr32(e, 11) ^ safari_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = safari_rotr32(a, 2) ^ safari_rotr32(a, 13) ^ safari_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

static void safari_sha256_init(safari_sha256_ctx_t *ctx) {
    if (!ctx) return;
    ctx->h[0] = 0x6A09E667U; ctx->h[1] = 0xBB67AE85U;
    ctx->h[2] = 0x3C6EF372U; ctx->h[3] = 0xA54FF53AU;
    ctx->h[4] = 0x510E527FU; ctx->h[5] = 0x9B05688CU;
    ctx->h[6] = 0x1F83D9ABU; ctx->h[7] = 0x5BE0CD19U;
    ctx->bytes = 0;
    ctx->used = 0;
}

static void safari_sha256_update(safari_sha256_ctx_t *ctx, const void *data, int len) {
    const uint8_t *p = (const uint8_t *)data;
    int i;
    if (!ctx || !p || len <= 0) return;
    ctx->bytes += (uint64_t)len;
    for (i = 0; i < len; i++) {
        ctx->buf[ctx->used++] = p[i];
        if (ctx->used == 64) {
            safari_sha256_transform(ctx, ctx->buf);
            ctx->used = 0;
        }
    }
}

static void safari_sha256_final(safari_sha256_ctx_t *ctx, uint8_t out[32]) {
    uint64_t bit_len;
    int i;
    if (!ctx || !out) return;
    bit_len = ctx->bytes * 8U;
    ctx->buf[ctx->used++] = 0x80U;
    if (ctx->used > 56) {
        while (ctx->used < 64) ctx->buf[ctx->used++] = 0;
        safari_sha256_transform(ctx, ctx->buf);
        ctx->used = 0;
    }
    while (ctx->used < 56) ctx->buf[ctx->used++] = 0;
    safari_store_be64(ctx->buf + 56, bit_len);
    safari_sha256_transform(ctx, ctx->buf);
    for (i = 0; i < 8; i++)
        safari_store_be32(out + i * 4, ctx->h[i]);
}

static void safari_sha256_hash(const void *data, int len, uint8_t out[32]) {
    safari_sha256_ctx_t ctx;
    safari_sha256_init(&ctx);
    safari_sha256_update(&ctx, data, len);
    safari_sha256_final(&ctx, out);
}

typedef struct {
    safari_sha256_ctx_t inner;
    uint8_t opad[64];
} safari_hmac_sha256_ctx_t;

static void safari_hmac_sha256_init(safari_hmac_sha256_ctx_t *ctx, const uint8_t *key, int key_len) {
    uint8_t k0[64];
    uint8_t kh[32];
    int i;
    if (!ctx) return;
    for (i = 0; i < 64; i++) k0[i] = 0;
    if (key && key_len > 64) {
        safari_sha256_hash(key, key_len, kh);
        for (i = 0; i < 32; i++) k0[i] = kh[i];
    } else if (key && key_len > 0) {
        for (i = 0; i < key_len && i < 64; i++) k0[i] = key[i];
    }
    for (i = 0; i < 64; i++) {
        ctx->opad[i] = (uint8_t)(k0[i] ^ 0x5CU);
        k0[i] ^= 0x36U;
    }
    safari_sha256_init(&ctx->inner);
    safari_sha256_update(&ctx->inner, k0, 64);
}

static void safari_hmac_sha256_update(safari_hmac_sha256_ctx_t *ctx, const void *data, int len) {
    if (!ctx) return;
    safari_sha256_update(&ctx->inner, data, len);
}

static void safari_hmac_sha256_final(safari_hmac_sha256_ctx_t *ctx, uint8_t out[32]) {
    uint8_t inner_hash[32];
    safari_sha256_ctx_t outer;
    if (!ctx || !out) return;
    safari_sha256_final(&ctx->inner, inner_hash);
    safari_sha256_init(&outer);
    safari_sha256_update(&outer, ctx->opad, 64);
    safari_sha256_update(&outer, inner_hash, 32);
    safari_sha256_final(&outer, out);
}

static void safari_hmac_sha256(const uint8_t *key, int key_len,
                               const void *data, int len,
                               uint8_t out[32]) {
    safari_hmac_sha256_ctx_t ctx;
    safari_hmac_sha256_init(&ctx, key, key_len);
    safari_hmac_sha256_update(&ctx, data, len);
    safari_hmac_sha256_final(&ctx, out);
}

static void safari_hkdf_extract(const uint8_t *salt, int salt_len,
                                const uint8_t *ikm, int ikm_len,
                                uint8_t out[32]) {
    static const uint8_t zero_salt[32] = { 0 };
    if (!salt || salt_len <= 0) {
        salt = zero_salt;
        salt_len = 32;
    }
    safari_hmac_sha256(salt, salt_len, ikm, ikm_len, out);
}

static int safari_hkdf_expand(const uint8_t *prk, int prk_len,
                              const uint8_t *info, int info_len,
                              uint8_t *out, int out_len) {
    uint8_t t[32];
    int t_len = 0;
    int pos = 0;
    uint8_t counter = 1;
    int i;
    if (!prk || prk_len <= 0 || !out || out_len < 0) return -1;
    while (pos < out_len) {
        safari_hmac_sha256_ctx_t hctx;
        safari_hmac_sha256_init(&hctx, prk, prk_len);
        if (t_len > 0) safari_hmac_sha256_update(&hctx, t, t_len);
        if (info && info_len > 0) safari_hmac_sha256_update(&hctx, info, info_len);
        safari_hmac_sha256_update(&hctx, &counter, 1);
        safari_hmac_sha256_final(&hctx, t);
        t_len = 32;
        for (i = 0; i < t_len && pos < out_len; i++)
            out[pos++] = t[i];
        counter++;
        if (counter == 0 && pos < out_len) return -1;
    }
    return 0;
}

static int safari_tls13_hkdf_expand_label(const uint8_t secret[32],
                                          const char *label,
                                          const uint8_t *context,
                                          int context_len,
                                          uint8_t *out,
                                          int out_len) {
    uint8_t info[128];
    const char prefix[] = "tls13 ";
    int pos = 0;
    int i;
    int label_len = str_len(label);
    int full_label_len = 6 + label_len;
    if (!secret || !label || !out || out_len < 0 || full_label_len > 64 ||
        context_len < 0 || context_len > 64)
        return -1;
    safari_tls_put16(info, &pos, sizeof(info), (uint16_t)out_len);
    safari_tls_put8(info, &pos, sizeof(info), (uint8_t)full_label_len);
    for (i = 0; i < 6; i++) safari_tls_put8(info, &pos, sizeof(info), (uint8_t)prefix[i]);
    for (i = 0; i < label_len; i++) safari_tls_put8(info, &pos, sizeof(info), (uint8_t)label[i]);
    safari_tls_put8(info, &pos, sizeof(info), (uint8_t)context_len);
    if (context && context_len > 0) safari_tls_append_bytes(info, &pos, sizeof(info), context, context_len);
    return safari_hkdf_expand(secret, 32, info, pos, out, out_len);
}

static int safari_bytes_equal(const uint8_t *a, const uint8_t *b, int len) {
    uint8_t diff = 0;
    int i;
    if (!a || !b || len < 0) return 0;
    for (i = 0; i < len; i++) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

static int safari_tls13_hash_selftest(void) {
    static int checked = 0;
    static int ok = 0;
    static const uint8_t sha_abc[32] = {
        0xBA,0x78,0x16,0xBF,0x8F,0x01,0xCF,0xEA,
        0x41,0x41,0x40,0xDE,0x5D,0xAE,0x22,0x23,
        0xB0,0x03,0x61,0xA3,0x96,0x17,0x7A,0x9C,
        0xB4,0x10,0xFF,0x61,0xF2,0x00,0x15,0xAD
    };
    static const uint8_t hmac_quick[32] = {
        0xF7,0xBC,0x83,0xF4,0x30,0x53,0x84,0x24,
        0xB1,0x32,0x98,0xE6,0xAA,0x6F,0xB1,0x43,
        0xEF,0x4D,0x59,0xA1,0x49,0x46,0x17,0x59,
        0x97,0x47,0x9D,0xBC,0x2D,0x1A,0x3C,0xD8
    };
    static const uint8_t hkdf_prk_expected[32] = {
        0x07,0x77,0x09,0x36,0x2C,0x2E,0x32,0xDF,
        0x0D,0xDC,0x3F,0x0D,0xC4,0x7B,0xBA,0x63,
        0x90,0xB6,0xC7,0x3B,0xB5,0x0F,0x9C,0x31,
        0x22,0xEC,0x84,0x4A,0xD7,0xC2,0xB3,0xE5
    };
    static const uint8_t hkdf_okm_expected[42] = {
        0x3C,0xB2,0x5F,0x25,0xFA,0xAC,0xD5,0x7A,
        0x90,0x43,0x4F,0x64,0xD0,0x36,0x2F,0x2A,
        0x2D,0x2D,0x0A,0x90,0xCF,0x1A,0x5A,0x4C,
        0x5D,0xB0,0x2D,0x56,0xEC,0xC4,0xC5,0xBF,
        0x34,0x00,0x72,0x08,0xD5,0xB8,0x87,0x18,
        0x58,0x65
    };
    uint8_t digest[32];
    uint8_t hmac[32];
    uint8_t ikm[22];
    uint8_t salt[13];
    uint8_t info[10];
    uint8_t prk[32];
    uint8_t okm[42];
    uint8_t label_out[16];
    int i;
    if (checked) return ok;
    checked = 1;
    safari_sha256_hash("abc", 3, digest);
    if (!safari_bytes_equal(digest, sha_abc, 32)) return 0;
    safari_hmac_sha256((const uint8_t *)"key", 3,
                       "The quick brown fox jumps over the lazy dog", 43,
                       hmac);
    if (!safari_bytes_equal(hmac, hmac_quick, 32)) return 0;
    for (i = 0; i < 22; i++) ikm[i] = 0x0B;
    for (i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    for (i = 0; i < 10; i++) info[i] = (uint8_t)(0xF0 + i);
    safari_hkdf_extract(salt, sizeof(salt), ikm, sizeof(ikm), prk);
    if (!safari_bytes_equal(prk, hkdf_prk_expected, 32)) return 0;
    if (safari_hkdf_expand(prk, 32, info, sizeof(info), okm, sizeof(okm)) != 0) return 0;
    if (!safari_bytes_equal(okm, hkdf_okm_expected, sizeof(okm))) return 0;
    if (safari_tls13_hkdf_expand_label(prk, "test", info, 3, label_out, sizeof(label_out)) != 0)
        return 0;
    ok = 1;
    return 1;
}


static uint32_t safari_load_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void safari_store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void safari_store_le64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 0; i < 8; i++) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

static void safari_chacha_qr(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);
    *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);
    *a += *b; *d ^= *a; *d = (*d << 8) | (*d >> 24);
    *c += *d; *b ^= *c; *b = (*b << 7) | (*b >> 25);
}

static void safari_chacha20_block(const uint8_t key[32], uint32_t counter,
                                  const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t x[16];
    uint32_t w[16];
    int i;
    x[0] = 0x61707865U; x[1] = 0x3320646EU; x[2] = 0x79622D32U; x[3] = 0x6B206574U;
    for (i = 0; i < 8; i++) x[4 + i] = safari_load_le32(key + i * 4);
    x[12] = counter;
    x[13] = safari_load_le32(nonce + 0);
    x[14] = safari_load_le32(nonce + 4);
    x[15] = safari_load_le32(nonce + 8);
    for (i = 0; i < 16; i++) w[i] = x[i];
    for (i = 0; i < 10; i++) {
        safari_chacha_qr(&w[0], &w[4], &w[8], &w[12]);
        safari_chacha_qr(&w[1], &w[5], &w[9], &w[13]);
        safari_chacha_qr(&w[2], &w[6], &w[10], &w[14]);
        safari_chacha_qr(&w[3], &w[7], &w[11], &w[15]);
        safari_chacha_qr(&w[0], &w[5], &w[10], &w[15]);
        safari_chacha_qr(&w[1], &w[6], &w[11], &w[12]);
        safari_chacha_qr(&w[2], &w[7], &w[8], &w[13]);
        safari_chacha_qr(&w[3], &w[4], &w[9], &w[14]);
    }
    for (i = 0; i < 16; i++)
        safari_store_le32(out + i * 4, w[i] + x[i]);
}

static void safari_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                                uint32_t counter, const uint8_t *in,
                                uint8_t *out, int len) {
    uint8_t block[64];
    int i;
    int pos = 0;
    if (!key || !nonce || !in || !out || len < 0) return;
    while (pos < len) {
        safari_chacha20_block(key, counter++, nonce, block);
        for (i = 0; i < 64 && pos < len; i++, pos++)
            out[pos] = (uint8_t)(in[pos] ^ block[i]);
    }
}

static void safari_poly1305_mac(const uint8_t *m, int bytes,
                                const uint8_t key[32], uint8_t out[16]) {
    const uint32_t mask26 = 0x3FFFFFFU;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t h0 = 0;
    uint32_t h1 = 0;
    uint32_t h2 = 0;
    uint32_t h3 = 0;
    uint32_t h4 = 0;
    uint8_t block[16];
    int i;
    t0 = safari_load_le32(key + 0);
    t1 = safari_load_le32(key + 4);
    t2 = safari_load_le32(key + 8);
    t3 = safari_load_le32(key + 12);
    r0 = t0 & mask26;
    r1 = ((t0 >> 26) | (t1 << 6)) & 0x3FFFF03U;
    r2 = ((t1 >> 20) | (t2 << 12)) & 0x3FFC0FFU;
    r3 = ((t2 >> 14) | (t3 << 18)) & 0x3F03FFFU;
    r4 = (t3 >> 8) & 0x00FFFFFU;
    s1 = r1 * 5U; s2 = r2 * 5U; s3 = r3 * 5U; s4 = r4 * 5U;
    while (bytes > 0) {
        int n = bytes >= 16 ? 16 : bytes;
        uint32_t hibit = (n == 16) ? (1U << 24) : 0;
        uint64_t d0;
        uint64_t d1;
        uint64_t d2;
        uint64_t d3;
        uint64_t d4;
        uint32_t c;
        for (i = 0; i < 16; i++) block[i] = 0;
        for (i = 0; i < n; i++) block[i] = m[i];
        if (n < 16) block[n] = 1U;
        t0 = safari_load_le32(block + 0);
        t1 = safari_load_le32(block + 4);
        t2 = safari_load_le32(block + 8);
        t3 = safari_load_le32(block + 12);
        h0 += t0 & mask26;
        h1 += ((t0 >> 26) | (t1 << 6)) & mask26;
        h2 += ((t1 >> 20) | (t2 << 12)) & mask26;
        h3 += ((t2 >> 14) | (t3 << 18)) & mask26;
        h4 += (t3 >> 8) | hibit;
        d0 = ((uint64_t)h0 * r0) + ((uint64_t)h1 * s4) + ((uint64_t)h2 * s3) + ((uint64_t)h3 * s2) + ((uint64_t)h4 * s1);
        d1 = ((uint64_t)h0 * r1) + ((uint64_t)h1 * r0) + ((uint64_t)h2 * s4) + ((uint64_t)h3 * s3) + ((uint64_t)h4 * s2);
        d2 = ((uint64_t)h0 * r2) + ((uint64_t)h1 * r1) + ((uint64_t)h2 * r0) + ((uint64_t)h3 * s4) + ((uint64_t)h4 * s3);
        d3 = ((uint64_t)h0 * r3) + ((uint64_t)h1 * r2) + ((uint64_t)h2 * r1) + ((uint64_t)h3 * r0) + ((uint64_t)h4 * s4);
        d4 = ((uint64_t)h0 * r4) + ((uint64_t)h1 * r3) + ((uint64_t)h2 * r2) + ((uint64_t)h3 * r1) + ((uint64_t)h4 * r0);
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & mask26; d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & mask26; d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & mask26; d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & mask26; d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & mask26; h0 += c * 5U;
        c = h0 >> 26; h0 &= mask26; h1 += c;
        m += n;
        bytes -= n;
    }
    {
        uint32_t c;
        uint32_t g0;
        uint32_t g1;
        uint32_t g2;
        uint32_t g3;
        uint32_t g4;
        uint32_t mask;
        uint64_t f;
        c = h1 >> 26; h1 &= mask26; h2 += c;
        c = h2 >> 26; h2 &= mask26; h3 += c;
        c = h3 >> 26; h3 &= mask26; h4 += c;
        c = h4 >> 26; h4 &= mask26; h0 += c * 5U;
        c = h0 >> 26; h0 &= mask26; h1 += c;
        g0 = h0 + 5U; c = g0 >> 26; g0 &= mask26;
        g1 = h1 + c; c = g1 >> 26; g1 &= mask26;
        g2 = h2 + c; c = g2 >> 26; g2 &= mask26;
        g3 = h3 + c; c = g3 >> 26; g3 &= mask26;
        g4 = h4 + c - (1U << 26);
        mask = (g4 >> 31) - 1U;
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0;
        h1 = (h1 & mask) | g1;
        h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3;
        h4 = (h4 & mask) | g4;
        f = ((uint64_t)h0 | ((uint64_t)h1 << 26)) + safari_load_le32(key + 16);
        safari_store_le32(out + 0, (uint32_t)f);
        f = ((uint64_t)(h1 >> 6) | ((uint64_t)h2 << 20)) + safari_load_le32(key + 20) + (f >> 32);
        safari_store_le32(out + 4, (uint32_t)f);
        f = ((uint64_t)(h2 >> 12) | ((uint64_t)h3 << 14)) + safari_load_le32(key + 24) + (f >> 32);
        safari_store_le32(out + 8, (uint32_t)f);
        f = ((uint64_t)(h3 >> 18) | ((uint64_t)h4 << 8)) + safari_load_le32(key + 28) + (f >> 32);
        safari_store_le32(out + 12, (uint32_t)f);
    }
}

static int safari_chacha20_poly1305_tag(const uint8_t key[32], const uint8_t nonce[12],
                                        const uint8_t *aad, int aad_len,
                                        const uint8_t *cipher, int cipher_len,
                                        uint8_t tag[16]) {
    static uint8_t mac_buf[SAFARI_RESPONSE_MAX + 128];
    uint8_t block0[64];
    int pos = 0;
    int i;
    if (!key || !nonce || !tag || aad_len < 0 || cipher_len < 0 ||
        aad_len + cipher_len + 32 > (int)sizeof(mac_buf))
        return -1;
    safari_chacha20_block(key, 0, nonce, block0);
    if (aad && aad_len > 0) {
        for (i = 0; i < aad_len; i++) mac_buf[pos++] = aad[i];
    }
    while (pos & 15) mac_buf[pos++] = 0;
    if (cipher && cipher_len > 0) {
        for (i = 0; i < cipher_len; i++) mac_buf[pos++] = cipher[i];
    }
    while (pos & 15) mac_buf[pos++] = 0;
    safari_store_le64(mac_buf + pos, (uint64_t)aad_len); pos += 8;
    safari_store_le64(mac_buf + pos, (uint64_t)cipher_len); pos += 8;
    safari_poly1305_mac(mac_buf, pos, block0, tag);
    return 0;
}

static int safari_chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                            const uint8_t *aad, int aad_len,
                                            const uint8_t *plain, int plain_len,
                                            uint8_t *cipher, uint8_t tag[16]) {
    if (!plain || !cipher || !tag || plain_len < 0) return -1;
    safari_chacha20_xor(key, nonce, 1, plain, cipher, plain_len);
    return safari_chacha20_poly1305_tag(key, nonce, aad, aad_len, cipher, plain_len, tag);
}

static int safari_chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                            const uint8_t *aad, int aad_len,
                                            const uint8_t *cipher, int cipher_len,
                                            const uint8_t tag[16], uint8_t *plain) {
    uint8_t calc[16];
    if (!cipher || !tag || !plain || cipher_len < 0) return -1;
    if (safari_chacha20_poly1305_tag(key, nonce, aad, aad_len, cipher, cipher_len, calc) != 0)
        return -1;
    if (!safari_bytes_equal(calc, tag, 16)) return -1;
    safari_chacha20_xor(key, nonce, 1, cipher, plain, cipher_len);
    return cipher_len;
}

static int safari_tls13_aead_selftest(void) {
    static int checked = 0;
    static int ok = 0;
    static const uint8_t chacha_expected[64] = {
        0x10,0xF1,0xE7,0xE4,0xD1,0x3B,0x59,0x15,
        0x50,0x0F,0xDD,0x1F,0xA3,0x20,0x71,0xC4,
        0xC7,0xD1,0xF4,0xC7,0x33,0xC0,0x68,0x03,
        0x04,0x22,0xAA,0x9A,0xC3,0xD4,0x6C,0x4E,
        0xD2,0x82,0x64,0x46,0x07,0x9F,0xAA,0x09,
        0x14,0xC2,0xD7,0x05,0xD9,0x8B,0x02,0xA2,
        0xB5,0x12,0x9C,0xD1,0xDE,0x16,0x4E,0xB9,
        0xCB,0xD0,0x83,0xE8,0xA2,0x50,0x3C,0x4E
    };
    static const uint8_t poly_key[32] = {
        0x85,0xD6,0xBE,0x78,0x57,0x55,0x6D,0x33,
        0x7F,0x44,0x52,0xFE,0x42,0xD5,0x06,0xA8,
        0x01,0x03,0x80,0x8A,0xFB,0x0D,0xB2,0xFD,
        0x4A,0xBF,0xF6,0xAF,0x41,0x49,0xF5,0x1B
    };
    static const uint8_t poly_expected[16] = {
        0xA8,0x06,0x1D,0xC1,0x30,0x51,0x36,0xC6,
        0xC2,0x2B,0x8B,0xAF,0x0C,0x01,0x27,0xA9
    };
    uint8_t key[32];
    uint8_t nonce[12];
    uint8_t block[64];
    uint8_t poly[16];
    uint8_t aad[5] = { 1, 2, 3, 4, 5 };
    uint8_t plain[17] = "MyOS TLS record";
    uint8_t cipher[17];
    uint8_t decoded[17];
    uint8_t tag[16];
    int i;
    if (checked) return ok;
    checked = 1;
    for (i = 0; i < 32; i++) key[i] = (uint8_t)i;
    nonce[0] = 0; nonce[1] = 0; nonce[2] = 0; nonce[3] = 9;
    nonce[4] = 0; nonce[5] = 0; nonce[6] = 0; nonce[7] = 0x4A;
    nonce[8] = 0; nonce[9] = 0; nonce[10] = 0; nonce[11] = 0;
    safari_chacha20_block(key, 1, nonce, block);
    if (!safari_bytes_equal(block, chacha_expected, 64)) return 0;
    safari_poly1305_mac((const uint8_t *)"Cryptographic Forum Research Group", 34, poly_key, poly);
    if (!safari_bytes_equal(poly, poly_expected, 16)) return 0;
    for (i = 0; i < 12; i++) nonce[i] = (uint8_t)(0xA0 + i);
    if (safari_chacha20_poly1305_encrypt(key, nonce, aad, sizeof(aad),
                                         plain, sizeof(plain), cipher, tag) != 0)
        return 0;
    if (safari_chacha20_poly1305_decrypt(key, nonce, aad, sizeof(aad),
                                         cipher, sizeof(cipher), tag, decoded) != sizeof(decoded))
        return 0;
    if (!safari_bytes_equal(decoded, plain, sizeof(plain))) return 0;
    ok = 1;
    return 1;
}

typedef int64_t safari_x25519_fe_t[16];

static void safari_x25519_set(safari_x25519_fe_t out, const safari_x25519_fe_t in) {
    int i;
    for (i = 0; i < 16; i++) out[i] = in[i];
}

static void safari_x25519_carry(safari_x25519_fe_t out) {
    int i;
    int64_t c;
    for (i = 0; i < 16; i++) {
        out[i] += 1LL << 16;
        c = out[i] >> 16;
        if (i < 15)
            out[i + 1] += c - 1;
        else
            out[0] += 38 * (c - 1);
        out[i] -= c << 16;
    }
}

static void safari_x25519_select(safari_x25519_fe_t p, safari_x25519_fe_t q, int bit) {
    int i;
    int64_t c = ~(int64_t)(bit - 1);
    for (i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void safari_x25519_add(safari_x25519_fe_t out,
                              const safari_x25519_fe_t a,
                              const safari_x25519_fe_t b) {
    int i;
    for (i = 0; i < 16; i++) out[i] = a[i] + b[i];
}

static void safari_x25519_sub(safari_x25519_fe_t out,
                              const safari_x25519_fe_t a,
                              const safari_x25519_fe_t b) {
    int i;
    for (i = 0; i < 16; i++) out[i] = a[i] - b[i];
}

static void safari_x25519_mul(safari_x25519_fe_t out,
                              const safari_x25519_fe_t a,
                              const safari_x25519_fe_t b) {
    int i;
    int j;
    int64_t t[31];
    for (i = 0; i < 31; i++) t[i] = 0;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    }
    for (i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; i++)
        out[i] = t[i];
    safari_x25519_carry(out);
    safari_x25519_carry(out);
}

static void safari_x25519_square(safari_x25519_fe_t out, const safari_x25519_fe_t a) {
    safari_x25519_mul(out, a, a);
}

static void safari_x25519_inv(safari_x25519_fe_t out, const safari_x25519_fe_t in) {
    safari_x25519_fe_t c;
    int a;
    safari_x25519_set(c, in);
    for (a = 253; a >= 0; a--) {
        safari_x25519_square(c, c);
        if (a != 2 && a != 4)
            safari_x25519_mul(c, c, in);
    }
    safari_x25519_set(out, c);
}

static void safari_x25519_unpack(safari_x25519_fe_t out, const uint8_t in[32]) {
    int i;
    for (i = 0; i < 16; i++)
        out[i] = (int64_t)in[2 * i] + ((int64_t)in[2 * i + 1] << 8);
    out[15] &= 0x7FFF;
}

static void safari_x25519_pack(uint8_t out[32], const safari_x25519_fe_t in) {
    safari_x25519_fe_t t;
    safari_x25519_fe_t m;
    int i;
    int j;
    int b;
    safari_x25519_set(t, in);
    safari_x25519_carry(t);
    safari_x25519_carry(t);
    safari_x25519_carry(t);
    for (j = 0; j < 2; j++) {
        m[0] = t[0] - 0xFFED;
        for (i = 1; i < 15; i++) {
            m[i] = t[i] - 0xFFFF - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xFFFF;
        }
        m[15] = t[15] - 0x7FFF - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xFFFF;
        safari_x25519_select(t, m, 1 - b);
    }
    for (i = 0; i < 16; i++) {
        out[2 * i] = (uint8_t)(t[i] & 0xFF);
        out[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xFF);
    }
}

static void safari_x25519_scalarmult(uint8_t out[32],
                                     const uint8_t scalar[32],
                                     const uint8_t point[32]) {
    static const safari_x25519_fe_t fe_121665 = { 0xDB41, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t e[32];
    safari_x25519_fe_t x1;
    safari_x25519_fe_t a = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    safari_x25519_fe_t b;
    safari_x25519_fe_t c = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    safari_x25519_fe_t d = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    safari_x25519_fe_t e_tmp;
    safari_x25519_fe_t f;
    int i;
    for (i = 0; i < 32; i++) e[i] = scalar[i];
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;
    safari_x25519_unpack(x1, point);
    safari_x25519_set(b, x1);
    for (i = 254; i >= 0; i--) {
        int bit = (e[i >> 3] >> (i & 7)) & 1;
        safari_x25519_select(a, b, bit);
        safari_x25519_select(c, d, bit);
        safari_x25519_add(e_tmp, a, c);
        safari_x25519_sub(a, a, c);
        safari_x25519_add(c, b, d);
        safari_x25519_sub(b, b, d);
        safari_x25519_square(d, e_tmp);
        safari_x25519_square(f, a);
        safari_x25519_mul(a, c, a);
        safari_x25519_mul(c, b, e_tmp);
        safari_x25519_add(e_tmp, a, c);
        safari_x25519_sub(a, a, c);
        safari_x25519_square(b, a);
        safari_x25519_sub(c, d, f);
        safari_x25519_mul(a, c, fe_121665);
        safari_x25519_add(a, a, d);
        safari_x25519_mul(c, c, a);
        safari_x25519_mul(a, d, f);
        safari_x25519_mul(d, b, x1);
        safari_x25519_square(b, e_tmp);
        safari_x25519_select(a, b, bit);
        safari_x25519_select(c, d, bit);
    }
    safari_x25519_inv(c, c);
    safari_x25519_mul(a, a, c);
    safari_x25519_pack(out, a);
}

static void safari_x25519_public_from_private(const uint8_t private_key[32], uint8_t public_key[32]) {
    static const uint8_t basepoint[32] = { 9 };
    safari_x25519_scalarmult(public_key, private_key, basepoint);
}


typedef struct {
    uint8_t private_key[32];
    uint8_t public_key[32];
    uint8_t client_random[32];
    uint8_t client_hello[512];
    int client_hello_len;
    uint8_t client_hs_secret[32];
    uint8_t server_hs_secret[32];
    uint8_t handshake_secret[32];
    uint8_t client_hs_key[32];
    uint8_t client_hs_iv[12];
    uint8_t server_hs_key[32];
    uint8_t server_hs_iv[12];
    uint8_t client_app_key[32];
    uint8_t client_app_iv[12];
    uint8_t server_app_key[32];
    uint8_t server_app_iv[12];
    uint32_t client_hs_seq;
    uint32_t server_hs_seq;
    uint32_t client_app_seq;
    uint32_t server_app_seq;
    int ready;
    int handshake_ready;
    int app_ready;
    char cert_subject[96];
    char cert_issuer[96];
    char cert_name[96];
    int cert_seen;
    int cert_host_ok;
    int cert_time_ok;
} safari_tls13_probe_state_t;

typedef struct {
    uint16_t version;
    uint16_t cipher;
    uint16_t key_share_group;
    uint8_t key_share[32];
    int key_share_len;
    int msg_off;
    int msg_len;
} safari_tls13_server_hello_t;

static int safari_make_tls_client_hello(const safari_request_t *req, uint8_t *out, int max,
                                        safari_tls13_probe_state_t *tls_state) {
    static const uint16_t ciphers[] = {
        0x1303U, /* TLS_CHACHA20_POLY1305_SHA256 */
        0xC02FU, /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */
        0xC02BU, /* TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 */
        0xC030U, /* TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 */
        0xCCA8U, /* TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 */
        0x009CU, /* TLS_RSA_WITH_AES_128_GCM_SHA256 */
        0x002FU  /* TLS_RSA_WITH_AES_128_CBC_SHA */
    };
    static const uint16_t groups[] = { 0x001DU, 0x0017U, 0x0018U };
    static const uint16_t sigalgs[] = { 0x0403U, 0x0503U, 0x0401U, 0x0501U, 0x0804U, 0x0805U };
    uint8_t rnd[32];
    uint8_t x25519_private[32];
    uint8_t x25519_public[32];
    int pos = 0;
    int rec_len_pos;
    int hs_len_pos;
    int ext_len_pos;
    int ext_start;
    int i;
    int host_len;
    if (tls_state) {
        tls_state->client_hello_len = 0;
        tls_state->client_hs_seq = 0;
        tls_state->server_hs_seq = 0;
        tls_state->client_app_seq = 0;
        tls_state->server_app_seq = 0;
        tls_state->ready = 0;
        tls_state->handshake_ready = 0;
        tls_state->app_ready = 0;
        tls_state->cert_subject[0] = 0;
        tls_state->cert_issuer[0] = 0;
        tls_state->cert_name[0] = 0;
        tls_state->cert_seen = 0;
        tls_state->cert_host_ok = 0;
        tls_state->cert_time_ok = 0;
    }
    if (!req || !req->host[0] || !out || max < 128) return -1;
    host_len = str_len(req->host);
    if (host_len <= 0 || host_len > 253) return -1;
    if (!safari_tls13_hash_selftest()) return -1;
    if (!safari_tls13_aead_selftest()) return -1;
    safari_tls_random(rnd, sizeof(rnd));
    safari_tls_random(x25519_private, sizeof(x25519_private));
    safari_x25519_public_from_private(x25519_private, x25519_public);
    if (tls_state) {
        for (i = 0; i < 32; i++) {
            tls_state->client_random[i] = rnd[i];
            tls_state->private_key[i] = x25519_private[i];
            tls_state->public_key[i] = x25519_public[i];
        }
    }
    safari_tls_put8(out, &pos, max, 22U);       /* Handshake record */
    safari_tls_put16(out, &pos, max, 0x0301U);  /* TLS record legacy version */
    rec_len_pos = pos;
    safari_tls_put16(out, &pos, max, 0U);
    safari_tls_put8(out, &pos, max, 1U);        /* ClientHello */
    hs_len_pos = pos;
    safari_tls_put8(out, &pos, max, 0U);
    safari_tls_put8(out, &pos, max, 0U);
    safari_tls_put8(out, &pos, max, 0U);
    safari_tls_put16(out, &pos, max, 0x0303U);  /* TLS 1.2 client version */
    safari_tls_append_bytes(out, &pos, max, rnd, sizeof(rnd));
    safari_tls_put8(out, &pos, max, 0U);        /* empty session id */
    safari_tls_put16(out, &pos, max, (uint16_t)(sizeof(ciphers) / sizeof(ciphers[0]) * 2U));
    for (i = 0; i < (int)(sizeof(ciphers) / sizeof(ciphers[0])); i++)
        safari_tls_put16(out, &pos, max, ciphers[i]);
    safari_tls_put8(out, &pos, max, 1U);
    safari_tls_put8(out, &pos, max, 0U);        /* null compression */
    ext_len_pos = pos;
    safari_tls_put16(out, &pos, max, 0U);
    ext_start = pos;

    /* server_name */
    safari_tls_put16(out, &pos, max, 0U);
    safari_tls_put16(out, &pos, max, (uint16_t)(host_len + 5));
    safari_tls_put16(out, &pos, max, (uint16_t)(host_len + 3));
    safari_tls_put8(out, &pos, max, 0U);
    safari_tls_put16(out, &pos, max, (uint16_t)host_len);
    safari_tls_append_bytes(out, &pos, max, req->host, host_len);

    /* supported_groups */
    safari_tls_put16(out, &pos, max, 10U);
    safari_tls_put16(out, &pos, max, (uint16_t)(2 + sizeof(groups)));
    safari_tls_put16(out, &pos, max, (uint16_t)sizeof(groups));
    for (i = 0; i < (int)(sizeof(groups) / sizeof(groups[0])); i++)
        safari_tls_put16(out, &pos, max, groups[i]);

    /* supported_versions: TLS 1.3, with TLS 1.2 fallback */
    safari_tls_put16(out, &pos, max, 43U);
    safari_tls_put16(out, &pos, max, 5U);
    safari_tls_put8(out, &pos, max, 4U);
    safari_tls_put16(out, &pos, max, 0x0304U);
    safari_tls_put16(out, &pos, max, 0x0303U);

    /* key_share: X25519 */
    safari_tls_put16(out, &pos, max, 51U);
    safari_tls_put16(out, &pos, max, 38U);
    safari_tls_put16(out, &pos, max, 36U);
    safari_tls_put16(out, &pos, max, 0x001DU);
    safari_tls_put16(out, &pos, max, 32U);
    safari_tls_append_bytes(out, &pos, max, x25519_public, sizeof(x25519_public));

    /* ec_point_formats */
    safari_tls_put16(out, &pos, max, 11U);
    safari_tls_put16(out, &pos, max, 2U);
    safari_tls_put8(out, &pos, max, 1U);
    safari_tls_put8(out, &pos, max, 0U);

    /* signature_algorithms */
    safari_tls_put16(out, &pos, max, 13U);
    safari_tls_put16(out, &pos, max, (uint16_t)(2 + sizeof(sigalgs)));
    safari_tls_put16(out, &pos, max, (uint16_t)sizeof(sigalgs));
    for (i = 0; i < (int)(sizeof(sigalgs) / sizeof(sigalgs[0])); i++)
        safari_tls_put16(out, &pos, max, sigalgs[i]);

    /* ALPN: http/1.1 */
    safari_tls_put16(out, &pos, max, 16U);
    safari_tls_put16(out, &pos, max, 11U);
    safari_tls_put16(out, &pos, max, 9U);
    safari_tls_put8(out, &pos, max, 8U);
    safari_tls_append_bytes(out, &pos, max, "http/1.1", 8);

    if (pos >= max) return -1;
    safari_tls_put16_at(out, rec_len_pos, (uint16_t)(pos - 5));
    safari_tls_put24_at(out, hs_len_pos, (uint32_t)(pos - hs_len_pos - 3));
    safari_tls_put16_at(out, ext_len_pos, (uint16_t)(pos - ext_start));
    if (tls_state && pos - 5 <= (int)sizeof(tls_state->client_hello)) {
        tls_state->client_hello_len = pos - 5;
        for (i = 0; i < tls_state->client_hello_len; i++)
            tls_state->client_hello[i] = out[5 + i];
        tls_state->ready = 1;
    }
    return pos;
}

static void safari_append_hex16(char *dst, int *pos, int max, uint16_t value) {
    static const char hx[] = "0123456789ABCDEF";
    if (!dst || !pos || *pos + 6 >= max) return;
    safari_append(dst, pos, max, "0x");
    dst[(*pos)++] = hx[(value >> 12) & 0x0F];
    dst[(*pos)++] = hx[(value >> 8) & 0x0F];
    dst[(*pos)++] = hx[(value >> 4) & 0x0F];
    dst[(*pos)++] = hx[value & 0x0F];
    dst[*pos] = 0;
}

static const char *safari_tls_version_name(uint16_t version) {
    if (version == 0x0304U) return "TLS 1.3";
    if (version == 0x0303U) return "TLS 1.2";
    if (version == 0x0302U) return "TLS 1.1";
    if (version == 0x0301U) return "TLS 1.0";
    return "TLS";
}

static const char *safari_tls_cipher_name(uint16_t cipher) {
    if (cipher == 0x1303U) return "CHACHA20_POLY1305_SHA256";
    if (cipher == 0xC02FU) return "ECDHE_RSA_AES_128_GCM_SHA256";
    if (cipher == 0xC02BU) return "ECDHE_ECDSA_AES_128_GCM_SHA256";
    if (cipher == 0xC030U) return "ECDHE_RSA_AES_256_GCM_SHA384";
    if (cipher == 0xCCA8U) return "ECDHE_RSA_CHACHA20_POLY1305_SHA256";
    if (cipher == 0x009CU) return "RSA_AES_128_GCM_SHA256";
    if (cipher == 0x002FU) return "RSA_AES_128_CBC_SHA";
    return "";
}

static const char *safari_tls_group_name(uint16_t group) {
    if (group == 0x001DU) return "X25519";
    if (group == 0x0017U) return "secp256r1";
    if (group == 0x0018U) return "secp384r1";
    return "";
}


static int safari_tls_complete_record_count(const uint8_t *buf, int len) {
    int off = 0;
    int count = 0;
    if (!buf || len <= 0) return 0;
    while (off + 5 <= len) {
        uint16_t rec_len = (uint16_t)(((uint16_t)buf[off + 3] << 8) | buf[off + 4]);
        int end = off + 5 + rec_len;
        if (end > len) break;
        count++;
        off = end;
    }
    return count;
}


static int safari_tls_probe_has_followup_record(const uint8_t *buf, int len) {
    int off = 0;
    int index = 0;
    int saw_server_handshake = 0;
    if (!buf || len <= 0) return 0;
    while (off + 5 <= len) {
        uint8_t rec_type = buf[off];
        uint16_t rec_len = (uint16_t)(((uint16_t)buf[off + 3] << 8) | buf[off + 4]);
        int end = off + 5 + rec_len;
        if (end > len) break;
        if (rec_type == 21U) return 1;
        if (index == 0 && rec_type == 22U) {
            saw_server_handshake = 1;
        } else if (saw_server_handshake && (rec_type == 22U || rec_type == 23U)) {
            return 1;
        }
        off = end;
        index++;
    }
    return 0;
}

static const char *safari_tls13_handshake_name(uint8_t htype) {
    if (htype == 2U) return "ServerHello";
    if (htype == 8U) return "EncryptedExtensions";
    if (htype == 11U) return "Certificate";
    if (htype == 15U) return "CertificateVerify";
    if (htype == 20U) return "Finished";
    if (htype == 4U) return "NewSessionTicket";
    return "Handshake";
}

static int safari_tls13_parse_server_hello(const uint8_t *buf, int len,
                                           safari_tls13_server_hello_t *out) {
    int off = 0;
    if (!buf || !out) return -1;
    out->version = 0;
    out->cipher = 0;
    out->key_share_group = 0;
    out->key_share_len = 0;
    out->msg_off = 0;
    out->msg_len = 0;
    while (off + 5 <= len) {
        uint8_t rec_type = buf[off];
        uint16_t rec_len = (uint16_t)(((uint16_t)buf[off + 3] << 8) | buf[off + 4]);
        int rec_start = off + 5;
        int rec_end = rec_start + rec_len;
        if (rec_end > len) break;
        if (rec_type == 22U) {
            int h = rec_start;
            while (h + 4 <= rec_end) {
                uint8_t htype = buf[h];
                uint32_t hlen = ((uint32_t)buf[h + 1] << 16) |
                                ((uint32_t)buf[h + 2] << 8) |
                                (uint32_t)buf[h + 3];
                int body = h + 4;
                int hend = body + (int)hlen;
                if (hend > rec_end) break;
                if (htype == 2U && body + 38 <= hend) {
                    int sid_len = buf[body + 34];
                    int cpos = body + 35 + sid_len;
                    out->msg_off = h;
                    out->msg_len = (int)hlen + 4;
                    out->version = (uint16_t)(((uint16_t)buf[body] << 8) | buf[body + 1]);
                    if (cpos + 2 <= hend)
                        out->cipher = (uint16_t)(((uint16_t)buf[cpos] << 8) | buf[cpos + 1]);
                    if (cpos + 5 <= hend) {
                        int ext_len = (int)(((uint16_t)buf[cpos + 3] << 8) | buf[cpos + 4]);
                        int ext = cpos + 5;
                        int ext_end = ext + ext_len;
                        if (ext_end > hend) ext_end = hend;
                        while (ext + 4 <= ext_end) {
                            uint16_t etype = (uint16_t)(((uint16_t)buf[ext] << 8) | buf[ext + 1]);
                            int elen = (int)(((uint16_t)buf[ext + 2] << 8) | buf[ext + 3]);
                            int ebody = ext + 4;
                            int enext = ebody + elen;
                            if (enext > ext_end) break;
                            if (etype == 43U && elen >= 2)
                                out->version = (uint16_t)(((uint16_t)buf[ebody] << 8) | buf[ebody + 1]);
                            if (etype == 51U && elen >= 4) {
                                int klen = (int)(((uint16_t)buf[ebody + 2] << 8) | buf[ebody + 3]);
                                int i;
                                out->key_share_group = (uint16_t)(((uint16_t)buf[ebody] << 8) | buf[ebody + 1]);
                                out->key_share_len = klen;
                                if (klen > 32) klen = 32;
                                if (ebody + 4 + klen <= enext) {
                                    for (i = 0; i < klen; i++)
                                        out->key_share[i] = buf[ebody + 4 + i];
                                }
                            }
                            ext = enext;
                        }
                    }
                    return 0;
                }
                h = hend;
            }
        }
        off = rec_end;
    }
    return -1;
}

static int safari_tls13_first_encrypted_record(const uint8_t *buf, int len,
                                               const safari_tls13_server_hello_t *sh,
                                               int *rec_off, int *rec_len) {
    int off = 0;
    if (!buf || !sh || !rec_off || !rec_len || sh->msg_len <= 0) return -1;
    while (off + 5 <= len) {
        uint8_t rec_type = buf[off];
        uint16_t len16 = (uint16_t)(((uint16_t)buf[off + 3] << 8) | buf[off + 4]);
        int end = off + 5 + len16;
        if (end > len) break;
        if (rec_type == 23U && off > sh->msg_off) {
            *rec_off = off;
            *rec_len = (int)len16;
            return 0;
        }
        off = end;
    }
    return -1;
}


static void safari_tls13_make_nonce(const uint8_t iv[12], uint32_t seq, uint8_t nonce[12]) {
    int i;
    for (i = 0; i < 12; i++) nonce[i] = iv[i];
    nonce[8] ^= (uint8_t)(seq >> 24);
    nonce[9] ^= (uint8_t)(seq >> 16);
    nonce[10] ^= (uint8_t)(seq >> 8);
    nonce[11] ^= (uint8_t)seq;
}

static int safari_tls13_open_record(const uint8_t key[32], const uint8_t iv[12], uint32_t seq,
                                    const uint8_t *record, int record_len,
                                    uint8_t *plain, int plain_max,
                                    uint8_t *inner_type) {
    uint16_t enc_len;
    int cipher_len;
    int plain_len;
    int inner_len;
    uint8_t nonce[12];
    if (!key || !iv || !record || !plain || !inner_type || record_len < 22) return -1;
    if (record[0] != 23U) return -1;
    enc_len = (uint16_t)(((uint16_t)record[3] << 8) | record[4]);
    if ((int)enc_len + 5 > record_len || enc_len <= 16) return -1;
    cipher_len = (int)enc_len - 16;
    if (cipher_len > plain_max) return -1;
    safari_tls13_make_nonce(iv, seq, nonce);
    plain_len = safari_chacha20_poly1305_decrypt(key, nonce,
                                                 record, 5,
                                                 record + 5, cipher_len,
                                                 record + 5 + cipher_len,
                                                 plain);
    if (plain_len <= 0) return -1;
    inner_len = plain_len;
    while (inner_len > 0 && plain[inner_len - 1] == 0) inner_len--;
    if (inner_len <= 0) return -1;
    *inner_type = plain[inner_len - 1];
    return inner_len - 1;
}

static int safari_tls13_seal_record(const uint8_t key[32], const uint8_t iv[12], uint32_t seq,
                                    uint8_t inner_type,
                                    const uint8_t *plain, int plain_len,
                                    uint8_t *record, int max) {
    static uint8_t inner[SAFARI_RESPONSE_MAX];
    uint8_t nonce[12];
    uint8_t tag[16];
    int enc_len = plain_len + 1 + 16;
    int i;
    if (!key || !iv || !record || plain_len < 0 || plain_len + 1 > (int)sizeof(inner) ||
        max < enc_len + 5)
        return -1;
    for (i = 0; i < plain_len; i++) inner[i] = plain ? plain[i] : 0;
    inner[plain_len] = inner_type;
    record[0] = 23U;
    record[1] = 0x03U;
    record[2] = 0x03U;
    safari_tls_put16_at(record, 3, (uint16_t)enc_len);
    safari_tls13_make_nonce(iv, seq, nonce);
    if (safari_chacha20_poly1305_encrypt(key, nonce,
                                         record, 5,
                                         inner, plain_len + 1,
                                         record + 5, tag) != 0)
        return -1;
    for (i = 0; i < 16; i++) record[5 + plain_len + 1 + i] = tag[i];
    return enc_len + 5;
}

static int safari_tls13_append_handshake(uint8_t *dst, int *pos, int max,
                                         uint8_t htype, const uint8_t *body, int body_len) {
    if (!dst || !pos || body_len < 0 || *pos + 4 + body_len > max) return -1;
    dst[(*pos)++] = htype;
    dst[(*pos)++] = (uint8_t)((body_len >> 16) & 0xFF);
    dst[(*pos)++] = (uint8_t)((body_len >> 8) & 0xFF);
    dst[(*pos)++] = (uint8_t)(body_len & 0xFF);
    safari_tls_append_bytes(dst, pos, max, body, body_len);
    return 0;
}


static int safari_tls13_record_selftest(void) {
    static int checked = 0;
    static int ok = 0;
    uint8_t key[32];
    uint8_t iv[12];
    uint8_t rec[96];
    uint8_t plain[32];
    uint8_t opened[64];
    uint8_t inner = 0;
    int rec_len;
    int open_len;
    int i;
    if (checked) return ok;
    checked = 1;
    for (i = 0; i < 32; i++) key[i] = (uint8_t)(0x20 + i);
    for (i = 0; i < 12; i++) iv[i] = (uint8_t)(0xA0 + i);
    for (i = 0; i < 17; i++) plain[i] = (uint8_t)('A' + i);
    rec_len = safari_tls13_seal_record(key, iv, 7U, 23U, plain, 17, rec, sizeof(rec));
    if (rec_len <= 0) return 0;
    open_len = safari_tls13_open_record(key, iv, 7U, rec, rec_len, opened, sizeof(opened), &inner);
    if (open_len != 17 || inner != 23U) return 0;
    if (!safari_bytes_equal(opened, plain, 17)) return 0;
    rec[rec_len - 1] ^= 1U;
    if (safari_tls13_open_record(key, iv, 7U, rec, rec_len, opened, sizeof(opened), &inner) >= 0)
        return 0;
    ok = 1;
    return 1;
}

static int safari_tls13_derive_handshake_keys(safari_tls13_probe_state_t *state,
                                              const safari_tls13_server_hello_t *sh,
                                              const uint8_t *buf) {
    uint8_t shared[32];
    uint8_t zeros[32] = { 0 };
    uint8_t empty_hash[32];
    uint8_t early_secret[32];
    uint8_t derived_secret[32];
    uint8_t handshake_secret[32];
    uint8_t transcript_hash[32];
    uint8_t client_secret[32];
    uint8_t server_secret[32];
    safari_sha256_ctx_t transcript;
    int i;
    int nonzero = 0;
    if (!state || !sh || !buf || sh->key_share_len != 32) return -1;
    safari_x25519_scalarmult(shared, state->private_key, sh->key_share);
    for (i = 0; i < 32; i++) {
        if (shared[i]) nonzero = 1;
    }
    if (!nonzero) return -1;
    safari_sha256_hash("", 0, empty_hash);
    safari_hkdf_extract(zeros, sizeof(zeros), zeros, sizeof(zeros), early_secret);
    if (safari_tls13_hkdf_expand_label(early_secret, "derived", empty_hash, 32,
                                       derived_secret, 32) != 0)
        return -1;
    safari_hkdf_extract(derived_secret, 32, shared, 32, handshake_secret);
    for (i = 0; i < 32; i++) state->handshake_secret[i] = handshake_secret[i];
    safari_sha256_init(&transcript);
    safari_sha256_update(&transcript, state->client_hello, state->client_hello_len);
    safari_sha256_update(&transcript, buf + sh->msg_off, sh->msg_len);
    safari_sha256_final(&transcript, transcript_hash);
    if (safari_tls13_hkdf_expand_label(handshake_secret, "c hs traffic",
                                       transcript_hash, 32, client_secret, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(handshake_secret, "s hs traffic",
                                       transcript_hash, 32, server_secret, 32) != 0)
        return -1;
    for (i = 0; i < 32; i++) {
        state->client_hs_secret[i] = client_secret[i];
        state->server_hs_secret[i] = server_secret[i];
    }
    if (safari_tls13_hkdf_expand_label(client_secret, "key", 0, 0, state->client_hs_key, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(client_secret, "iv", 0, 0, state->client_hs_iv, 12) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(server_secret, "key", 0, 0, state->server_hs_key, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(server_secret, "iv", 0, 0, state->server_hs_iv, 12) != 0)
        return -1;
    state->client_hs_seq = 0;
    state->server_hs_seq = 0;
    state->handshake_ready = 1;
    return 0;
}

static void safari_tls13_append_decrypt_note(const safari_tls13_probe_state_t *state,
                                             const uint8_t *buf, int len,
                                             char *out, int *pos, int max) {
    static uint8_t plain[SAFARI_RESPONSE_MAX];
    safari_tls13_server_hello_t sh;
    uint8_t shared[32];
    uint8_t zeros[32] = { 0 };
    uint8_t empty_hash[32];
    uint8_t early_secret[32];
    uint8_t derived_secret[32];
    uint8_t handshake_secret[32];
    uint8_t transcript_hash[32];
    uint8_t server_secret[32];
    uint8_t key[32];
    uint8_t iv[12];
    uint8_t nonce[12];
    safari_sha256_ctx_t transcript;
    int rec_off;
    int rec_len;
    int cipher_len;
    int plain_len;
    int inner_len;
    int i;
    int nonzero = 0;
    if (!state || !state->ready || !buf || len <= 0 || !out || !pos) return;
    if (safari_tls13_parse_server_hello(buf, len, &sh) != 0) return;
    if (sh.version != 0x0304U || sh.cipher != 0x1303U ||
        sh.key_share_group != 0x001DU || sh.key_share_len != 32)
        return;
    if (safari_tls13_first_encrypted_record(buf, len, &sh, &rec_off, &rec_len) != 0) {
        safari_append(out, pos, max, "TLS 1.3 keys can be derived after the encrypted handshake record arrives.\n");
        return;
    }
    if (rec_len <= 16 || rec_off + 5 + rec_len > len) return;
    safari_x25519_scalarmult(shared, state->private_key, sh.key_share);
    for (i = 0; i < 32; i++) {
        if (shared[i]) nonzero = 1;
    }
    if (!nonzero) {
        safari_append(out, pos, max, "TLS 1.3 X25519 shared secret was all zero; aborting decrypt.\n");
        return;
    }
    safari_sha256_hash("", 0, empty_hash);
    safari_hkdf_extract(zeros, sizeof(zeros), zeros, sizeof(zeros), early_secret);
    if (safari_tls13_hkdf_expand_label(early_secret, "derived", empty_hash, 32,
                                       derived_secret, 32) != 0)
        return;
    safari_hkdf_extract(derived_secret, 32, shared, 32, handshake_secret);
    safari_sha256_init(&transcript);
    safari_sha256_update(&transcript, state->client_hello, state->client_hello_len);
    safari_sha256_update(&transcript, buf + sh.msg_off, sh.msg_len);
    safari_sha256_final(&transcript, transcript_hash);
    if (safari_tls13_hkdf_expand_label(handshake_secret, "s hs traffic",
                                       transcript_hash, 32, server_secret, 32) != 0)
        return;
    if (safari_tls13_hkdf_expand_label(server_secret, "key", 0, 0, key, 32) != 0)
        return;
    if (safari_tls13_hkdf_expand_label(server_secret, "iv", 0, 0, iv, 12) != 0)
        return;
    for (i = 0; i < 12; i++) nonce[i] = iv[i];
    cipher_len = rec_len - 16;
    plain_len = safari_chacha20_poly1305_decrypt(key, nonce,
                                                 buf + rec_off, 5,
                                                 buf + rec_off + 5, cipher_len,
                                                 buf + rec_off + 5 + cipher_len,
                                                 plain);
    if (plain_len <= 0) {
        safari_append(out, pos, max, "TLS 1.3 encrypted handshake record was present but AEAD verify failed.\n");
        return;
    }
    inner_len = plain_len;
    while (inner_len > 0 && plain[inner_len - 1] == 0) inner_len--;
    if (inner_len <= 0) return;
    safari_append(out, pos, max, "TLS 1.3 encrypted handshake decrypted: ");
    if (plain[inner_len - 1] == 22U) {
        int hp = 0;
        int wrote = 0;
        int content_len = inner_len - 1;
        while (hp + 4 <= content_len) {
            uint8_t htype = plain[hp];
            uint32_t hlen = ((uint32_t)plain[hp + 1] << 16) |
                            ((uint32_t)plain[hp + 2] << 8) |
                            (uint32_t)plain[hp + 3];
            int hend = hp + 4 + (int)hlen;
            if (hend > content_len) break;
            if (wrote) safari_append(out, pos, max, ", ");
            safari_append(out, pos, max, safari_tls13_handshake_name(htype));
            wrote = 1;
            hp = hend;
        }
        if (!wrote) safari_append(out, pos, max, "handshake fragment");
    } else {
        safari_append(out, pos, max, "inner content type ");
        safari_append_uint(out, pos, max, plain[inner_len - 1]);
    }
    safari_append(out, pos, max, "\n");
}

static void safari_describe_tls_probe(const safari_request_t *req, const uint8_t *buf, int len,
                                      const safari_tls13_probe_state_t *tls_state,
                                      char *out, int max) {
    int pos = 0;
    int off = 0;
    int saw_server_hello = 0;
    int saw_certificate = 0;
    uint16_t server_version = 0;
    uint16_t cipher = 0;
    uint16_t key_share_group = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    safari_append(out, &pos, max, "TLS TCP connection established.\nClientHello sent with SNI: ");
    safari_append(out, &pos, max, req ? req->host : "");
    safari_append(out, &pos, max, "\n");
    while (buf && off + 5 <= len) {
        uint8_t rec_type = buf[off];
        uint16_t rec_len = (uint16_t)(((uint16_t)buf[off + 3] << 8) | buf[off + 4]);
        int rec_start = off + 5;
        int rec_end = rec_start + rec_len;
        if (rec_end > len) break;
        if (rec_type == 21U && rec_start + 2 <= rec_end) {
            safari_append(out, &pos, max, "TLS alert received. level=");
            safari_append_uint(out, &pos, max, buf[rec_start]);
            safari_append(out, &pos, max, " description=");
            safari_append_uint(out, &pos, max, buf[rec_start + 1]);
            safari_append(out, &pos, max, "\n");
        } else if (rec_type == 22U) {
            int h = rec_start;
            while (h + 4 <= rec_end) {
                uint8_t htype = buf[h];
                uint32_t hlen = ((uint32_t)buf[h + 1] << 16) |
                                ((uint32_t)buf[h + 2] << 8) |
                                (uint32_t)buf[h + 3];
                int body = h + 4;
                int hend = body + (int)hlen;
                if (hend > rec_end) break;
                if (htype == 2U && body + 38 <= hend) {
                    int sid_len;
                    int cpos;
                    saw_server_hello = 1;
                    server_version = (uint16_t)(((uint16_t)buf[body] << 8) | buf[body + 1]);
                    sid_len = buf[body + 34];
                    cpos = body + 35 + sid_len;
                    if (cpos + 2 <= hend)
                        cipher = (uint16_t)(((uint16_t)buf[cpos] << 8) | buf[cpos + 1]);
                    if (cpos + 5 <= hend) {
                        int ext_len = (int)(((uint16_t)buf[cpos + 3] << 8) | buf[cpos + 4]);
                        int ext = cpos + 5;
                        int ext_end = ext + ext_len;
                        if (ext_end > hend) ext_end = hend;
                        while (ext + 4 <= ext_end) {
                            uint16_t etype = (uint16_t)(((uint16_t)buf[ext] << 8) | buf[ext + 1]);
                            int elen = (int)(((uint16_t)buf[ext + 2] << 8) | buf[ext + 3]);
                            int ebody = ext + 4;
                            int enext = ebody + elen;
                            if (enext > ext_end) break;
                            if (etype == 43U && elen >= 2)
                                server_version = (uint16_t)(((uint16_t)buf[ebody] << 8) | buf[ebody + 1]);
                            if (etype == 51U && elen >= 4)
                                key_share_group = (uint16_t)(((uint16_t)buf[ebody] << 8) | buf[ebody + 1]);
                            ext = enext;
                        }
                    }
                } else if (htype == 11U) {
                    saw_certificate = 1;
                }
                h = hend;
            }
        }
        off = rec_end;
    }
    if (saw_server_hello) {
        safari_append(out, &pos, max, "ServerHello received: ");
        safari_append(out, &pos, max, safari_tls_version_name(server_version));
        safari_append(out, &pos, max, " cipher ");
        safari_append_hex16(out, &pos, max, cipher);
        if (safari_tls_cipher_name(cipher)[0]) {
            safari_append(out, &pos, max, " ");
            safari_append(out, &pos, max, safari_tls_cipher_name(cipher));
        }
        if (key_share_group) {
            safari_append(out, &pos, max, " key_share ");
            if (safari_tls_group_name(key_share_group)[0])
                safari_append(out, &pos, max, safari_tls_group_name(key_share_group));
            else
                safari_append_hex16(out, &pos, max, key_share_group);
        }
        safari_append(out, &pos, max, "\n");
        safari_tls13_append_decrypt_note(tls_state, buf, len, out, &pos, max);
    } else {
        safari_append(out, &pos, max, "No ServerHello was decoded from the first TLS response.\n");
    }
    safari_append(out, &pos, max, saw_certificate ? "Certificate handshake was visible in plaintext.\n" :
                  "Certificate is not yet decoded; TLS 1.3 encrypts it after ServerHello.\n");
    safari_append(out, &pos, max,
                  "\nIf this fallback is shown, full HTTPS rendering still needs this site's TLS variant to complete application traffic, plus certificate chain trust and broader TLS compatibility.");
}

static int safari_fetch_raw_http(const safari_request_t *req, const char *method,
                                 const char *request_body,
                                 char *response, int max, char *err, int err_max) {
    uint32_t ifindex = 0;
    uint32_t seq;
    uint32_t ack = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint32_t expected_seq = 0;
    uint16_t rflags = 0;
    uint16_t src_port;
    uint16_t srcp = 0;
    uint32_t srcip = 0;
    int tries;
    int total = 0;
    int req_pos = 0;
    char request[512];
    char chunk[NET_TCP_PAYLOAD_MAX + 1];
    if (!req || !response || max <= 0) return -1;
    response[0] = 0;
    if (net_route_lookup4(req->ipv4, &ifindex, 0) < 0) {
        safari_copy(err, err_max, "No IPv4 route to host");
        return -1;
    }
    src_port = (uint16_t)(40000U + ((timer_ticks() / 17U) % 20000U));
    seq = 0x53414641U ^ timer_ticks();
    for (tries = 0; tries < 4; tries++) {
        if (net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, 0, NET_TCP_SYN, 0, 0) >= 0)
            break;
        net_poll();
    }
    if (tries >= 4) {
        safari_copy(err, err_max, "TCP SYN send failed");
        return -1;
    }
    for (tries = 0; tries < 64; tries++) {
        int n;
        srcip = 0;
        srcp = 0;
        rseq = 0;
        rack = 0;
        rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk) - 1, &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (srcip == req->ipv4 && srcp == req->port && (rflags & NET_TCP_SYN) && (rflags & NET_TCP_ACK)) {
            ack = rseq + 1U;
            break;
        }
        if ((tries % 16) == 15)
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port,
                                seq, 0, NET_TCP_SYN, 0, 0);
    }
    if (ack == 0) {
        safari_copy(err, err_max, "TCP handshake timed out");
        return -1;
    }
    expected_seq = ack;
    request[0] = 0;
    safari_append_http_request(request, &req_pos, sizeof(request), req, method, request_body, 1);
    {
        int sent = 0;
        int req_len = str_len(request);
        while (sent < req_len) {
            int part = req_len - sent;
            if (part > NET_TCP_PAYLOAD_MAX) part = NET_TCP_PAYLOAD_MAX;
            if (net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq + 1U + (uint32_t)sent, ack,
                              NET_TCP_ACK | NET_TCP_PSH, request + sent, (uint32_t)part) < 0) {
                safari_copy(err, err_max, "HTTP request send failed");
                return -1;
            }
            sent += part;
        }
        seq += 1U + (uint32_t)req_len;
    }
    for (tries = 0; tries < 220 && total + 1 < max; tries++) {
        int n;
        srcip = 0;
        srcp = 0;
        rseq = 0;
        rack = 0;
        rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk) - 1, &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (n == 0 && srcip == 0 && srcp == 0 && rflags == 0) continue;
        if (srcip != req->ipv4 || srcp != req->port) continue;
        if (rflags & NET_TCP_RST) {
            safari_copy(err, err_max, "Connection reset by peer");
            return -1;
        }
        if (n > 0) {
            if (rseq != expected_seq) {
                (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                    NET_TCP_ACK, 0, 0);
                continue;
            }
            {
                int recv_len = n;
                int copy_len = n;
                int i;
                if (copy_len > max - total - 1) copy_len = max - total - 1;
                for (i = 0; i < copy_len; i++) response[total + i] = chunk[i];
                total += copy_len;
                expected_seq += (uint32_t)recv_len;
                ack = expected_seq;
                (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                    NET_TCP_ACK, 0, 0);
            }
        }
        if (n == 0 && (rflags & NET_TCP_FIN) && rseq != expected_seq) {
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                NET_TCP_ACK, 0, 0);
            continue;
        }
        if (rflags & NET_TCP_FIN) {
            ack = expected_seq + 1U;
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                NET_TCP_ACK, 0, 0);
            break;
        }
    }
    response[total] = 0;
    if (total <= 0) {
        safari_copy(err, err_max, "No HTTP response bytes received");
        return -1;
    }
    return total;
}

static int safari_ci_contains(const char *haystack, const char *needle);


static int safari_tcp_send_buffer4(uint32_t ifindex, uint32_t dst_ipv4,
                                   uint16_t src_port, uint16_t dst_port,
                                   uint32_t *seq, uint32_t ack,
                                   const uint8_t *data, int len,
                                   char *err, int err_max,
                                   const char *err_text) {
    int sent = 0;
    if (!seq || !data || len < 0) return -1;
    while (sent < len) {
        int part = len - sent;
        if (part > NET_TCP_PAYLOAD_MAX) part = NET_TCP_PAYLOAD_MAX;
        if (net_tcp_send4(ifindex, dst_ipv4, src_port, dst_port,
                          *seq + (uint32_t)sent, ack,
                          NET_TCP_ACK | NET_TCP_PSH,
                          data + sent, (uint32_t)part) < 0) {
            safari_copy(err, err_max, err_text ? err_text : "TCP send failed");
            return -1;
        }
        sent += part;
    }
    *seq += (uint32_t)len;
    return 0;
}


typedef struct {
    uint8_t tag;
    int val;
    int len;
    int end;
} safari_der_tlv_t;

static int safari_der_read_tlv(const uint8_t *buf, int len, int off,
                               safari_der_tlv_t *tlv) {
    uint8_t len_byte;
    int llen;
    int value_len = 0;
    int i;
    if (!buf || !tlv || off < 0 || off + 2 > len) return -1;
    tlv->tag = buf[off++];
    if ((tlv->tag & 0x1FU) == 0x1FU) return -1;
    len_byte = buf[off++];
    if ((len_byte & 0x80U) == 0) {
        value_len = (int)len_byte;
    } else {
        llen = (int)(len_byte & 0x7FU);
        if (llen <= 0 || llen > 3 || off + llen > len) return -1;
        for (i = 0; i < llen; i++)
            value_len = (value_len << 8) | (int)buf[off + i];
        off += llen;
    }
    if (value_len < 0 || value_len > len - off) return -1;
    tlv->val = off;
    tlv->len = value_len;
    tlv->end = off + value_len;
    return 0;
}

static int safari_der_oid_eq(const uint8_t *buf, const safari_der_tlv_t *tlv,
                             const uint8_t *oid, int oid_len) {
    int i;
    if (!buf || !tlv || !oid || tlv->tag != 0x06U || tlv->len != oid_len)
        return 0;
    for (i = 0; i < oid_len; i++) {
        if (buf[tlv->val + i] != oid[i]) return 0;
    }
    return 1;
}

static int safari_x509_parse_digits(const uint8_t *buf, int len, int off,
                                    int digits, int *value) {
    int i;
    int v = 0;
    if (!buf || !value || off < 0 || digits <= 0 || off + digits > len)
        return 0;
    for (i = 0; i < digits; i++) {
        uint8_t ch = buf[off + i];
        if (ch < '0' || ch > '9') return 0;
        v = v * 10 + (int)(ch - '0');
    }
    *value = v;
    return 1;
}

static int safari_x509_parse_time(const uint8_t *buf, int len, uint8_t tag,
                                  datetime_t *dt) {
    int year;
    int pos;
    int tmp;
    if (!buf || !dt) return 0;
    if (tag == 0x17U) {
        if (!safari_x509_parse_digits(buf, len, 0, 2, &tmp)) return 0;
        year = (tmp >= 50) ? 1900 + tmp : 2000 + tmp;
        pos = 2;
    } else if (tag == 0x18U) {
        if (!safari_x509_parse_digits(buf, len, 0, 4, &year)) return 0;
        pos = 4;
    } else {
        return 0;
    }
    if (!safari_x509_parse_digits(buf, len, pos, 2, &dt->month)) return 0;
    pos += 2;
    if (!safari_x509_parse_digits(buf, len, pos, 2, &dt->day)) return 0;
    pos += 2;
    if (!safari_x509_parse_digits(buf, len, pos, 2, &dt->hour)) return 0;
    pos += 2;
    if (!safari_x509_parse_digits(buf, len, pos, 2, &dt->minute)) return 0;
    pos += 2;
    dt->second = 0;
    if (pos + 2 <= len && buf[pos] >= '0' && buf[pos] <= '9' &&
        buf[pos + 1] >= '0' && buf[pos + 1] <= '9') {
        if (!safari_x509_parse_digits(buf, len, pos, 2, &dt->second)) return 0;
    }
    dt->year = year;
    if (dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > datetime_days_in_month(dt->year, dt->month) ||
        dt->hour > 23 || dt->minute > 59 || dt->second > 59)
        return 0;
    dt->weekday = datetime_day_of_week(dt->year, dt->month, dt->day);
    return 1;
}

static int safari_x509_time_cmp(const datetime_t *a, const datetime_t *b) {
    if (a->year != b->year) return (a->year < b->year) ? -1 : 1;
    if (a->month != b->month) return (a->month < b->month) ? -1 : 1;
    if (a->day != b->day) return (a->day < b->day) ? -1 : 1;
    if (a->hour != b->hour) return (a->hour < b->hour) ? -1 : 1;
    if (a->minute != b->minute) return (a->minute < b->minute) ? -1 : 1;
    if (a->second != b->second) return (a->second < b->second) ? -1 : 1;
    return 0;
}

static void safari_x509_copy_der_string(const uint8_t *buf, int len, uint8_t tag,
                                        char *out, int max) {
    int i;
    int pos = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    if (!buf || len <= 0) return;
    if (tag == 0x1EU) {
        for (i = 0; i + 1 < len && pos + 1 < max; i += 2) {
            unsigned char ch = (buf[i] == 0) ? buf[i + 1] : '?';
            out[pos++] = (ch >= 32U && ch < 127U) ? (char)ch : '?';
        }
    } else {
        for (i = 0; i < len && pos + 1 < max; i++) {
            unsigned char ch = buf[i];
            out[pos++] = (ch >= 32U && ch < 127U) ? (char)ch : '?';
        }
    }
    out[pos] = 0;
}

static int safari_x509_find_name_attr_inner(const uint8_t *buf, int len,
                                            int start, int end,
                                            const uint8_t *oid, int oid_len,
                                            char *out, int max, int depth) {
    int pos = start;
    if (!buf || !oid || !out || depth > 8) return 0;
    while (pos < end) {
        safari_der_tlv_t tlv;
        if (safari_der_read_tlv(buf, len, pos, &tlv) != 0 || tlv.end > end)
            return 0;
        if (tlv.tag == 0x30U) {
            safari_der_tlv_t first;
            if (safari_der_read_tlv(buf, len, tlv.val, &first) == 0 &&
                first.end <= tlv.end && safari_der_oid_eq(buf, &first, oid, oid_len)) {
                safari_der_tlv_t value;
                if (safari_der_read_tlv(buf, len, first.end, &value) == 0 &&
                    value.end <= tlv.end) {
                    safari_x509_copy_der_string(buf + value.val, value.len,
                                                value.tag, out, max);
                    return out[0] != 0;
                }
            }
            if (safari_x509_find_name_attr_inner(buf, len, tlv.val, tlv.end,
                                                 oid, oid_len, out, max, depth + 1))
                return 1;
        } else if (tlv.tag == 0x31U) {
            if (safari_x509_find_name_attr_inner(buf, len, tlv.val, tlv.end,
                                                 oid, oid_len, out, max, depth + 1))
                return 1;
        }
        pos = tlv.end;
    }
    return 0;
}

static int safari_x509_find_name_attr(const uint8_t *buf, int len,
                                      const safari_der_tlv_t *name,
                                      const uint8_t *oid, int oid_len,
                                      char *out, int max) {
    if (!name || name->tag != 0x30U) return 0;
    return safari_x509_find_name_attr_inner(buf, len, name->val, name->end,
                                           oid, oid_len, out, max, 0);
}

static int safari_x509_name_len(const char *s) {
    int len = s ? str_len(s) : 0;
    while (len > 0 && s[len - 1] == '.') len--;
    return len;
}

static int safari_x509_match_n_ci(const char *a, const char *b, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if (!safari_ieq_char(a[i], b[i])) return 0;
    }
    return 1;
}

static int safari_x509_dns_name_match(const char *host, const char *pattern) {
    int hlen = safari_x509_name_len(host);
    int plen = safari_x509_name_len(pattern);
    int i;
    if (!host || !pattern || hlen <= 0 || plen <= 0) return 0;
    if (plen > 2 && pattern[0] == '*' && pattern[1] == '.') {
        int suffix_len = plen - 1;
        int label_len = hlen - suffix_len;
        if (label_len <= 0) return 0;
        if (!safari_x509_match_n_ci(host + label_len, pattern + 1, suffix_len))
            return 0;
        for (i = 0; i < label_len; i++) {
            if (host[i] == '.') return 0;
        }
        return 1;
    }
    return hlen == plen && safari_x509_match_n_ci(host, pattern, hlen);
}

static int safari_x509_parse_san_names(safari_tls13_probe_state_t *state,
                                       const safari_request_t *req,
                                       const uint8_t *buf, int len,
                                       int *san_count, int *san_host_ok,
                                       char *first_san, int first_max) {
    safari_der_tlv_t seq;
    int pos;
    if (!state || !req || !buf || !san_count || !san_host_ok)
        return 0;
    if (safari_der_read_tlv(buf, len, 0, &seq) != 0 || seq.tag != 0x30U)
        return 0;
    pos = seq.val;
    while (pos < seq.end) {
        safari_der_tlv_t gn;
        char name[96];
        if (safari_der_read_tlv(buf, len, pos, &gn) != 0 || gn.end > seq.end)
            return 0;
        if (gn.tag == 0x82U) {
            safari_x509_copy_der_string(buf + gn.val, gn.len, gn.tag,
                                        name, sizeof(name));
            if (name[0]) {
                (*san_count)++;
                if (first_san && first_max > 0 && !first_san[0])
                    safari_copy(first_san, first_max, name);
                if (safari_x509_dns_name_match(req->host, name))
                    *san_host_ok = 1;
            }
        }
        pos = gn.end;
    }
    return *san_count > 0;
}

static int safari_x509_parse_extensions(safari_tls13_probe_state_t *state,
                                        const safari_request_t *req,
                                        const uint8_t *cert, int cert_len,
                                        const safari_der_tlv_t *ext_field,
                                        int *san_count, int *san_host_ok,
                                        char *first_san, int first_max) {
    static const uint8_t oid_subject_alt_name[] = { 0x55U, 0x1DU, 0x11U };
    safari_der_tlv_t seq;
    int pos;
    if (!ext_field || ext_field->tag != 0xA3U) return 0;
    if (safari_der_read_tlv(cert, cert_len, ext_field->val, &seq) != 0 ||
        seq.tag != 0x30U)
        return 0;
    pos = seq.val;
    while (pos < seq.end) {
        safari_der_tlv_t ext;
        safari_der_tlv_t oid;
        safari_der_tlv_t value;
        int p;
        if (safari_der_read_tlv(cert, cert_len, pos, &ext) != 0 ||
            ext.end > seq.end)
            return 0;
        p = ext.val;
        if (safari_der_read_tlv(cert, cert_len, p, &oid) != 0 ||
            oid.end > ext.end)
            return 0;
        p = oid.end;
        if (p < ext.end) {
            safari_der_tlv_t maybe_critical;
            if (safari_der_read_tlv(cert, cert_len, p, &maybe_critical) == 0 &&
                maybe_critical.end <= ext.end && maybe_critical.tag == 0x01U)
                p = maybe_critical.end;
        }
        if (safari_der_read_tlv(cert, cert_len, p, &value) == 0 &&
            value.end <= ext.end && value.tag == 0x04U &&
            safari_der_oid_eq(cert, &oid, oid_subject_alt_name,
                              (int)sizeof(oid_subject_alt_name))) {
            safari_x509_parse_san_names(state, req, cert + value.val, value.len,
                                        san_count, san_host_ok,
                                        first_san, first_max);
        }
        pos = ext.end;
    }
    return *san_count > 0;
}

static int safari_tls13_parse_leaf_certificate(safari_tls13_probe_state_t *state,
                                               const safari_request_t *req,
                                               const uint8_t *cert, int cert_len) {
    static const uint8_t oid_common_name[] = { 0x55U, 0x04U, 0x03U };
    safari_der_tlv_t cert_seq;
    safari_der_tlv_t tbs;
    safari_der_tlv_t field;
    safari_der_tlv_t issuer;
    safari_der_tlv_t validity;
    safari_der_tlv_t subject;
    datetime_t not_before;
    datetime_t not_after;
    datetime_t now;
    char first_san[96];
    int san_count = 0;
    int san_host_ok = 0;
    int pos;
    int p;
    if (!state || !req || !cert || cert_len <= 0) return -1;
    state->cert_seen = 1;
    state->cert_subject[0] = 0;
    state->cert_issuer[0] = 0;
    state->cert_name[0] = 0;
    state->cert_host_ok = 0;
    state->cert_time_ok = 0;
    first_san[0] = 0;
    if (safari_der_read_tlv(cert, cert_len, 0, &cert_seq) != 0 ||
        cert_seq.tag != 0x30U)
        return -1;
    pos = cert_seq.val;
    if (safari_der_read_tlv(cert, cert_len, pos, &tbs) != 0 ||
        tbs.tag != 0x30U || tbs.end > cert_seq.end)
        return -1;
    pos = tbs.val;
    if (safari_der_read_tlv(cert, cert_len, pos, &field) != 0)
        return -1;
    if (field.tag == 0xA0U)
        pos = field.end;
    if (safari_der_read_tlv(cert, cert_len, pos, &field) != 0) return -1;
    pos = field.end; /* serialNumber */
    if (safari_der_read_tlv(cert, cert_len, pos, &field) != 0) return -1;
    pos = field.end; /* signature */
    if (safari_der_read_tlv(cert, cert_len, pos, &issuer) != 0 ||
        issuer.tag != 0x30U)
        return -1;
    safari_x509_find_name_attr(cert, cert_len, &issuer, oid_common_name,
                               (int)sizeof(oid_common_name),
                               state->cert_issuer, sizeof(state->cert_issuer));
    pos = issuer.end;
    if (safari_der_read_tlv(cert, cert_len, pos, &validity) != 0 ||
        validity.tag != 0x30U)
        return -1;
    p = validity.val;
    if (safari_der_read_tlv(cert, cert_len, p, &field) == 0 &&
        field.end <= validity.end &&
        safari_x509_parse_time(cert + field.val, field.len, field.tag, &not_before)) {
        p = field.end;
        if (safari_der_read_tlv(cert, cert_len, p, &field) == 0 &&
            field.end <= validity.end &&
            safari_x509_parse_time(cert + field.val, field.len, field.tag, &not_after) &&
            datetime_now_utc(&now)) {
            state->cert_time_ok =
                safari_x509_time_cmp(&now, &not_before) >= 0 &&
                safari_x509_time_cmp(&now, &not_after) <= 0;
        }
    }
    pos = validity.end;
    if (safari_der_read_tlv(cert, cert_len, pos, &subject) != 0 ||
        subject.tag != 0x30U)
        return -1;
    safari_x509_find_name_attr(cert, cert_len, &subject, oid_common_name,
                               (int)sizeof(oid_common_name),
                               state->cert_subject, sizeof(state->cert_subject));
    pos = subject.end;
    if (safari_der_read_tlv(cert, cert_len, pos, &field) != 0) return -1;
    pos = field.end; /* subjectPublicKeyInfo */
    while (pos < tbs.end) {
        if (safari_der_read_tlv(cert, cert_len, pos, &field) != 0 ||
            field.end > tbs.end)
            break;
        if (field.tag == 0xA3U)
            safari_x509_parse_extensions(state, req, cert, cert_len, &field,
                                         &san_count, &san_host_ok,
                                         first_san, sizeof(first_san));
        pos = field.end;
    }
    if (san_count > 0) {
        safari_copy(state->cert_name, sizeof(state->cert_name), first_san);
        state->cert_host_ok = san_host_ok;
    } else if (state->cert_subject[0]) {
        safari_copy(state->cert_name, sizeof(state->cert_name), state->cert_subject);
        state->cert_host_ok = safari_x509_dns_name_match(req->host, state->cert_subject);
    }
    return 0;
}

static int safari_tls13_parse_certificate_message(safari_tls13_probe_state_t *state,
                                                  const safari_request_t *req,
                                                  const uint8_t *body,
                                                  int body_len) {
    int ctx_len;
    int pos;
    int list_len;
    int list_end;
    int cert_len;
    if (!state || !req || !body || body_len < 4) return -1;
    state->cert_seen = 0;
    state->cert_subject[0] = 0;
    state->cert_issuer[0] = 0;
    state->cert_name[0] = 0;
    state->cert_host_ok = 0;
    state->cert_time_ok = 0;
    ctx_len = (int)body[0];
    pos = 1 + ctx_len;
    if (pos + 3 > body_len) return -1;
    list_len = ((int)body[pos] << 16) | ((int)body[pos + 1] << 8) | (int)body[pos + 2];
    pos += 3;
    list_end = pos + list_len;
    if (list_len <= 0 || list_end > body_len || pos + 3 > list_end)
        return -1;
    cert_len = ((int)body[pos] << 16) | ((int)body[pos + 1] << 8) | (int)body[pos + 2];
    pos += 3;
    if (cert_len <= 0 || pos + cert_len > list_end)
        return -1;
    return safari_tls13_parse_leaf_certificate(state, req, body + pos, cert_len);
}


static int safari_tls13_complete_server_handshake(safari_tls13_probe_state_t *state,
                                                  const safari_request_t *req,
                                                  const uint8_t *buf, int len,
                                                  uint8_t *client_finished_record,
                                                  int client_finished_max,
                                                  int *client_finished_len) {
    static uint8_t plain[SAFARI_RESPONSE_MAX];
    safari_tls13_probe_state_t work;
    safari_tls13_server_hello_t sh;
    safari_sha256_ctx_t transcript;
    uint8_t finished_key[32];
    uint8_t verify[32];
    uint8_t expected[32];
    uint8_t transcript_hash[32];
    uint8_t empty_hash[32];
    uint8_t zeros[32] = { 0 };
    uint8_t derived_secret[32];
    uint8_t master_secret[32];
    uint8_t client_app_secret[32];
    uint8_t server_app_secret[32];
    uint8_t fin_msg[36];
    int off = 0;
    int found_finished = 0;
    int fin_msg_len;
    int i;
    if (!state || !state->ready || !buf || len <= 0 ||
        !client_finished_record || !client_finished_len)
        return -1;
    if (safari_tls13_parse_server_hello(buf, len, &sh) != 0) return -1;
    if (sh.version != 0x0304U || sh.cipher != 0x1303U ||
        sh.key_share_group != 0x001DU || sh.key_share_len != 32)
        return -1;
    work = *state;
    if (safari_tls13_derive_handshake_keys(&work, &sh, buf) != 0) return -1;
    safari_sha256_init(&transcript);
    safari_sha256_update(&transcript, work.client_hello, work.client_hello_len);
    safari_sha256_update(&transcript, buf + sh.msg_off, sh.msg_len);
    while (off + 5 <= len) {
        uint8_t rec_type = buf[off];
        uint16_t rec_len = (uint16_t)(((uint16_t)buf[off + 3] << 8) | buf[off + 4]);
        int rec_end = off + 5 + rec_len;
        if (rec_end > len) return -1;
        if (rec_type == 20U) {
            off = rec_end;
            continue;
        }
        if (rec_type == 23U && off > sh.msg_off) {
            uint8_t inner_type = 0;
            int plain_len = safari_tls13_open_record(work.server_hs_key, work.server_hs_iv,
                                                     work.server_hs_seq,
                                                     buf + off, rec_end - off,
                                                     plain, SAFARI_RESPONSE_MAX,
                                                     &inner_type);
            if (plain_len < 0) return -1;
            work.server_hs_seq++;
            if (inner_type != 22U) return -1;
            {
                int hp = 0;
                while (hp + 4 <= plain_len) {
                    uint8_t htype = plain[hp];
                    uint32_t hlen = ((uint32_t)plain[hp + 1] << 16) |
                                    ((uint32_t)plain[hp + 2] << 8) |
                                    (uint32_t)plain[hp + 3];
                    int hend = hp + 4 + (int)hlen;
                    safari_sha256_ctx_t tmp;
                    if (hend > plain_len) return -1;
                    if (htype == 11U)
                        (void)safari_tls13_parse_certificate_message(&work, req,
                                                                     plain + hp + 4,
                                                                     (int)hlen);
                    if (htype == 20U) {
                        tmp = transcript;
                        safari_sha256_final(&tmp, transcript_hash);
                        if (safari_tls13_hkdf_expand_label(work.server_hs_secret, "finished",
                                                           0, 0, finished_key, 32) != 0)
                            return -1;
                        safari_hmac_sha256(finished_key, 32, transcript_hash, 32, expected);
                        if ((int)hlen != 32 || !safari_bytes_equal(expected, plain + hp + 4, 32))
                            return -1;
                        safari_sha256_update(&transcript, plain + hp, 4 + (int)hlen);
                        found_finished = 1;
                        break;
                    }
                    safari_sha256_update(&transcript, plain + hp, 4 + (int)hlen);
                    hp = hend;
                }
            }
            if (found_finished) break;
        }
        off = rec_end;
    }
    if (!found_finished) return -1;
    {
        safari_sha256_ctx_t tmp = transcript;
        safari_sha256_final(&tmp, transcript_hash);
        if (safari_tls13_hkdf_expand_label(work.client_hs_secret, "finished",
                                           0, 0, finished_key, 32) != 0)
            return -1;
        safari_hmac_sha256(finished_key, 32, transcript_hash, 32, verify);
    }
    fin_msg_len = 0;
    if (safari_tls13_append_handshake(fin_msg, &fin_msg_len, sizeof(fin_msg),
                                      20U, verify, 32) != 0)
        return -1;
    *client_finished_len = safari_tls13_seal_record(work.client_hs_key, work.client_hs_iv,
                                                    work.client_hs_seq++,
                                                    22U, fin_msg, fin_msg_len,
                                                    client_finished_record,
                                                    client_finished_max);
    if (*client_finished_len <= 0) return -1;
    safari_sha256_update(&transcript, fin_msg, fin_msg_len);
    {
        safari_sha256_ctx_t tmp = transcript;
        safari_sha256_final(&tmp, transcript_hash);
    }
    safari_sha256_hash("", 0, empty_hash);
    if (safari_tls13_hkdf_expand_label(work.handshake_secret, "derived",
                                       empty_hash, 32, derived_secret, 32) != 0)
        return -1;
    safari_hkdf_extract(derived_secret, 32, zeros, sizeof(zeros), master_secret);
    if (safari_tls13_hkdf_expand_label(master_secret, "c ap traffic",
                                       transcript_hash, 32, client_app_secret, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(master_secret, "s ap traffic",
                                       transcript_hash, 32, server_app_secret, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(client_app_secret, "key", 0, 0, work.client_app_key, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(client_app_secret, "iv", 0, 0, work.client_app_iv, 12) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(server_app_secret, "key", 0, 0, work.server_app_key, 32) != 0)
        return -1;
    if (safari_tls13_hkdf_expand_label(server_app_secret, "iv", 0, 0, work.server_app_iv, 12) != 0)
        return -1;
    for (i = 0; i < 32; i++) {
        state->client_app_key[i] = work.client_app_key[i];
        state->server_app_key[i] = work.server_app_key[i];
    }
    for (i = 0; i < 12; i++) {
        state->client_app_iv[i] = work.client_app_iv[i];
        state->server_app_iv[i] = work.server_app_iv[i];
    }
    state->client_hs_seq = work.client_hs_seq;
    state->server_hs_seq = work.server_hs_seq;
    state->client_app_seq = 0;
    state->server_app_seq = 0;
    state->handshake_ready = 1;
    state->app_ready = 1;
    safari_copy(state->cert_subject, sizeof(state->cert_subject), work.cert_subject);
    safari_copy(state->cert_issuer, sizeof(state->cert_issuer), work.cert_issuer);
    safari_copy(state->cert_name, sizeof(state->cert_name), work.cert_name);
    state->cert_seen = work.cert_seen;
    state->cert_host_ok = work.cert_host_ok;
    state->cert_time_ok = work.cert_time_ok;
    return 0;
}

static int safari_tls13_append_received_record(safari_tls13_probe_state_t *state,
                                               const uint8_t *record, int record_len,
                                               char *response, int max,
                                               int *total,
                                               int *closed) {
    static uint8_t plain[SAFARI_RESPONSE_MAX];
    uint8_t inner_type = 0;
    int plain_len;
    int i;
    if (!state || !state->app_ready || !record || !response || !total || !closed)
        return -1;
    plain_len = safari_tls13_open_record(state->server_app_key, state->server_app_iv,
                                         state->server_app_seq,
                                         record, record_len,
                                         plain, SAFARI_RESPONSE_MAX,
                                         &inner_type);
    if (plain_len < 0) return -1;
    state->server_app_seq++;
    if (inner_type == 23U) {
        for (i = 0; i < plain_len && *total + 1 < max; i++)
            response[(*total)++] = (char)plain[i];
        response[*total] = 0;
    } else if (inner_type == 21U) {
        *closed = 1;
    }
    return 0;
}

static int safari_fetch_https_tls13(const safari_request_t *req, const char *method,
                                    const char *request_body,
                                    char *response, int max,
                                    char *err, int err_max,
                                    safari_tls13_probe_state_t *out_state) {
    static uint8_t tls_in[SAFARI_RESPONSE_MAX];
    uint8_t chunk[NET_TCP_PAYLOAD_MAX];
    uint8_t hello[512];
    uint8_t outrec[1024];
    char request[512];
    safari_tls13_probe_state_t state;
    uint32_t ifindex = 0;
    uint32_t seq;
    uint32_t ack = 0;
    uint32_t expected_seq = 0;
    uint32_t srcip = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint16_t src_port;
    uint16_t srcp = 0;
    uint16_t rflags = 0;
    int hello_len;
    int tries;
    int tls_total = 0;
    int app_scan = 0;
    int req_pos = 0;
    int total = 0;
    int closed = 0;
    if (!req || !response || max <= 0) return -1;
    if (!safari_tls13_record_selftest()) {
        safari_copy(err, err_max, "TLS record selftest failed");
        return -1;
    }
    response[0] = 0;
    state.ready = 0;
    state.handshake_ready = 0;
    state.app_ready = 0;
    state.client_hello_len = 0;
    hello_len = safari_make_tls_client_hello(req, hello, sizeof(hello), &state);
    if (hello_len <= 0) {
        safari_copy(err, err_max, "TLS ClientHello build failed");
        return -1;
    }
    if (net_route_lookup4(req->ipv4, &ifindex, 0) < 0) {
        safari_copy(err, err_max, "No IPv4 route to TLS host");
        return -1;
    }
    src_port = (uint16_t)(43000U + ((timer_ticks() / 11U) % 20000U));
    seq = 0x48545450U ^ timer_ticks();
    for (tries = 0; tries < 4; tries++) {
        if (net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, 0, NET_TCP_SYN, 0, 0) >= 0)
            break;
        net_poll();
    }
    if (tries >= 4) {
        safari_copy(err, err_max, "HTTPS TCP SYN send failed");
        return -1;
    }
    for (tries = 0; tries < 80; tries++) {
        int n;
        srcip = 0; srcp = 0; rseq = 0; rack = 0; rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk), &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (srcip == req->ipv4 && srcp == req->port &&
            (rflags & NET_TCP_SYN) && (rflags & NET_TCP_ACK)) {
            ack = rseq + 1U;
            break;
        }
        if ((tries % 20) == 19)
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port,
                                seq, 0, NET_TCP_SYN, 0, 0);
    }
    if (ack == 0) {
        safari_copy(err, err_max, "HTTPS TCP handshake timed out");
        return -1;
    }
    expected_seq = ack;
    seq += 1U;
    if (safari_tcp_send_buffer4(ifindex, req->ipv4, src_port, req->port,
                                &seq, ack, hello, hello_len,
                                err, err_max, "TLS ClientHello send failed") != 0)
        return -1;
    for (tries = 0; tries < 420 && !state.app_ready; tries++) {
        int n;
        srcip = 0; srcp = 0; rseq = 0; rack = 0; rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk), &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (n == 0 && srcip == 0 && srcp == 0 && rflags == 0) continue;
        if (srcip != req->ipv4 || srcp != req->port) continue;
        if (rflags & NET_TCP_RST) {
            safari_copy(err, err_max, "HTTPS connection reset by peer");
            return -1;
        }
        if (n > 0) {
            int i;
            if (rseq != expected_seq) {
                (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                    NET_TCP_ACK, 0, 0);
                continue;
            }
            if (tls_total + n > (int)sizeof(tls_in)) {
                safari_copy(err, err_max, "TLS handshake response too large");
                return -1;
            }
            for (i = 0; i < n; i++) tls_in[tls_total + i] = chunk[i];
            tls_total += n;
            expected_seq += (uint32_t)n;
            ack = expected_seq;
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                NET_TCP_ACK, 0, 0);
            {
                int fin_len = 0;
                if (safari_tls13_complete_server_handshake(&state, req, tls_in, tls_total,
                                                           outrec, sizeof(outrec), &fin_len) == 0) {
                    if (safari_tcp_send_buffer4(ifindex, req->ipv4, src_port, req->port,
                                                &seq, ack, outrec, fin_len,
                                                err, err_max, "TLS Finished send failed") != 0)
                        return -1;
                }
            }
        }
        if (rflags & NET_TCP_FIN) break;
    }
    if (!state.app_ready) {
        safari_copy(err, err_max, "TLS 1.3 application keys were not established");
        return -1;
    }
    request[0] = 0;
    safari_append_http_request(request, &req_pos, sizeof(request), req, method, request_body, 1);
    {
        int rec_len = safari_tls13_seal_record(state.client_app_key, state.client_app_iv,
                                               state.client_app_seq++,
                                               23U, (const uint8_t *)request, req_pos,
                                               outrec, sizeof(outrec));
        if (rec_len <= 0) {
            safari_copy(err, err_max, "HTTPS request encrypt failed");
            return -1;
        }
        if (safari_tcp_send_buffer4(ifindex, req->ipv4, src_port, req->port,
                                    &seq, ack, outrec, rec_len,
                                    err, err_max, "HTTPS request send failed") != 0)
            return -1;
    }
    app_scan = tls_total;
    for (tries = 0; tries < 520 && total + 1 < max && !closed; tries++) {
        int n;
        srcip = 0; srcp = 0; rseq = 0; rack = 0; rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk), &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (n == 0 && srcip == 0 && srcp == 0 && rflags == 0) continue;
        if (srcip != req->ipv4 || srcp != req->port) continue;
        if (rflags & NET_TCP_RST) break;
        if (n > 0) {
            int i;
            if (rseq != expected_seq) {
                (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                    NET_TCP_ACK, 0, 0);
                continue;
            }
            if (tls_total + n > (int)sizeof(tls_in)) break;
            for (i = 0; i < n; i++) tls_in[tls_total + i] = chunk[i];
            tls_total += n;
            expected_seq += (uint32_t)n;
            ack = expected_seq;
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                NET_TCP_ACK, 0, 0);
            while (app_scan + 5 <= tls_total) {
                uint16_t rec_len = (uint16_t)(((uint16_t)tls_in[app_scan + 3] << 8) | tls_in[app_scan + 4]);
                int rec_end = app_scan + 5 + (int)rec_len;
                if (rec_end > tls_total) break;
                if (tls_in[app_scan] == 23U) {
                    if (safari_tls13_append_received_record(&state,
                                                            tls_in + app_scan,
                                                            rec_end - app_scan,
                                                            response, max,
                                                            &total, &closed) != 0) {
                        safari_copy(err, err_max, "HTTPS response decrypt failed");
                        return -1;
                    }
                    if (total > 0 && safari_ci_contains(response, "\r\n\r\n")) {
                        if (safari_ci_contains(response, "Connection: close") && closed) break;
                    }
                } else if (tls_in[app_scan] == 21U) {
                    closed = 1;
                }
                app_scan = rec_end;
            }
        }
        if (n == 0 && (rflags & NET_TCP_FIN) && rseq != expected_seq) {
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                NET_TCP_ACK, 0, 0);
            continue;
        }
        if (rflags & NET_TCP_FIN) {
            ack = expected_seq + 1U;
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                NET_TCP_ACK, 0, 0);
            break;
        }
    }
    response[total] = 0;
    if (total <= 0) {
        safari_copy(err, err_max, "No HTTPS response bytes decrypted");
        return -1;
    }
    if (out_state)
        *out_state = state;
    return total;
}

static int safari_fetch_tls_probe(const safari_request_t *req, uint8_t *response, int max,
                                  char *err, int err_max,
                                  safari_tls13_probe_state_t *tls_state) {
    uint8_t hello[512];
    uint8_t chunk[NET_TCP_PAYLOAD_MAX];
    uint32_t ifindex = 0;
    uint32_t seq;
    uint32_t ack = 0;
    uint32_t expected_seq = 0;
    uint16_t src_port;
    uint16_t srcp = 0;
    uint32_t srcip = 0;
    uint32_t rseq = 0;
    uint32_t rack = 0;
    uint16_t rflags = 0;
    int hello_len;
    int tries;
    int sent = 0;
    int total = 0;
    if (!req || !response || max <= 0) return -1;
    hello_len = safari_make_tls_client_hello(req, hello, sizeof(hello), tls_state);
    if (hello_len <= 0) {
        safari_copy(err, err_max, "TLS ClientHello build failed");
        return -1;
    }
    if (net_route_lookup4(req->ipv4, &ifindex, 0) < 0) {
        safari_copy(err, err_max, "No IPv4 route to TLS host");
        return -1;
    }
    src_port = (uint16_t)(42000U + ((timer_ticks() / 13U) % 20000U));
    seq = 0x544C5300U ^ timer_ticks();
    for (tries = 0; tries < 4; tries++) {
        if (net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, 0, NET_TCP_SYN, 0, 0) >= 0)
            break;
        net_poll();
    }
    if (tries >= 4) {
        safari_copy(err, err_max, "TLS TCP SYN send failed");
        return -1;
    }
    for (tries = 0; tries < 80; tries++) {
        int n;
        srcip = 0; srcp = 0; rseq = 0; rack = 0; rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk), &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (srcip == req->ipv4 && srcp == req->port &&
            (rflags & NET_TCP_SYN) && (rflags & NET_TCP_ACK)) {
            ack = rseq + 1U;
            break;
        }
        if ((tries % 20) == 19)
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port,
                                seq, 0, NET_TCP_SYN, 0, 0);
    }
    if (ack == 0) {
        safari_copy(err, err_max, "TLS TCP handshake timed out");
        return -1;
    }
    expected_seq = ack;
    while (sent < hello_len) {
        int part = hello_len - sent;
        if (part > NET_TCP_PAYLOAD_MAX) part = NET_TCP_PAYLOAD_MAX;
        if (net_tcp_send4(ifindex, req->ipv4, src_port, req->port,
                          seq + 1U + (uint32_t)sent, ack,
                          NET_TCP_ACK | NET_TCP_PSH, hello + sent, (uint32_t)part) < 0) {
            safari_copy(err, err_max, "TLS ClientHello send failed");
            return -1;
        }
        sent += part;
    }
    seq += 1U + (uint32_t)hello_len;
    for (tries = 0; tries < 240 && total + 1 < max; tries++) {
        int n;
        srcip = 0; srcp = 0; rseq = 0; rack = 0; rflags = 0;
        net_poll();
        n = net_tcp_recv4(src_port, chunk, sizeof(chunk), &srcip, &srcp, &rseq, &rack, &rflags);
        if (n < 0) break;
        if (n == 0 && srcip == 0 && srcp == 0 && rflags == 0) continue;
        if (srcip != req->ipv4 || srcp != req->port) continue;
        if (rflags & NET_TCP_RST) {
            safari_copy(err, err_max, "TLS connection reset by peer");
            return -1;
        }
        if (n > 0) {
            if (rseq != expected_seq) {
                (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                    NET_TCP_ACK, 0, 0);
                continue;
            }
            {
                int recv_len = n;
                int copy_len = n;
                int i;
                if (copy_len > max - total - 1) copy_len = max - total - 1;
                for (i = 0; i < copy_len; i++) response[total + i] = chunk[i];
                total += copy_len;
                expected_seq += (uint32_t)recv_len;
                ack = expected_seq;
                (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                    NET_TCP_ACK, 0, 0);
            }
        }
        if (total >= 5 && safari_tls_probe_has_followup_record(response, total)) break;
        if (n == 0 && (rflags & NET_TCP_FIN) && rseq != expected_seq) {
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, expected_seq,
                                NET_TCP_ACK, 0, 0);
            continue;
        }
        if (rflags & NET_TCP_FIN) {
            ack = expected_seq + 1U;
            (void)net_tcp_send4(ifindex, req->ipv4, src_port, req->port, seq, ack,
                                NET_TCP_ACK, 0, 0);
            break;
        }
    }
    if (total <= 0) {
        safari_copy(err, err_max, "No TLS response bytes received");
        return -1;
    }
    if (total < max) response[total] = 0;
    return total;
}

static int safari_http_status_code(const char *response) {
    int code = 0;
    if (!response || !safari_starts_with_ci(response, "HTTP/")) return 0;
    while (*response && *response != ' ') response++;
    while (*response == ' ') response++;
    while (*response >= '0' && *response <= '9') {
        code = code * 10 + (*response - '0');
        response++;
    }
    return code;
}

static int safari_header_name_eq(const char *line, const char *name) {
    int i = 0;
    if (!line || !name) return 0;
    while (name[i]) {
        if (!line[i] || !safari_ieq_char(line[i], name[i])) return 0;
        i++;
    }
    return line[i] == ':';
}

static int safari_header_value(const char *response, const char *name, char *out, int max) {
    const char *p = response;
    int i;
    if (!response || !name || !out || max <= 0) return -1;
    out[0] = 0;
    while (*p) {
        if ((p[0] == '\r' && p[1] == '\n') || p[0] == '\n') break;
        if (safari_header_name_eq(p, name)) {
            const char *v = p;
            int len = 0;
            while (*v && *v != ':') v++;
            if (*v == ':') v++;
            while (*v == ' ' || *v == '\t') v++;
            while (v[len] && v[len] != '\r' && v[len] != '\n') len++;
            while (len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t')) len--;
            for (i = 0; i < len && i + 1 < max; i++) out[i] = v[i];
            out[i] = 0;
            return out[0] ? 0 : -1;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return -1;
}

static int safari_is_redirect_status(uint32_t code) {
    return code == 301U || code == 302U || code == 303U || code == 307U || code == 308U;
}

static void safari_resolve_location(const safari_request_t *req, const char *location, char *out, int max) {
    const char *p = location;
    int pos = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    while (p && (*p == ' ' || *p == '\t')) p++;
    if (!p || !*p) return;
    if (safari_starts_with_ci(p, "about:") || safari_starts_with_ci(p, "http://") ||
        safari_starts_with_ci(p, "https://")) {
        safari_copy(out, max, p);
        return;
    }
    if (req && p[0] == '/' && p[1] == '/') {
        safari_append(out, &pos, max, req->scheme[0] ? req->scheme : "http");
        safari_append(out, &pos, max, ":");
        safari_append(out, &pos, max, p);
        return;
    }
    if (!req || !req->host[0]) {
        safari_copy(out, max, p);
        return;
    }
    safari_append(out, &pos, max, req->scheme[0] ? req->scheme : "http");
    safari_append(out, &pos, max, "://");
    safari_append(out, &pos, max, req->host);
    if ((str_eq(req->scheme, "http") && req->port != 80) ||
        (str_eq(req->scheme, "https") && req->port != 443)) {
        safari_append(out, &pos, max, ":");
        safari_append_uint(out, &pos, max, req->port);
    }
    if (p[0] == '/') {
        char normalized_path[192];
        safari_normalize_path_segments(p, normalized_path, sizeof(normalized_path));
        safari_append(out, &pos, max, normalized_path);
    } else {
        char dir[192];
        char combined[192];
        char normalized_path[192];
        int i;
        int last_slash = 0;
        int dpos = 0;
        int cpos = 0;
        const char *base = req->path[0] ? req->path : "/";
        if (p[0] == '?' || p[0] == '#') {
            safari_append(out, &pos, max, base);
            safari_append(out, &pos, max, p);
            return;
        }
        for (i = 0; base[i] && base[i] != '?' && base[i] != '#'; i++) if (base[i] == '/') last_slash = i;
        for (i = 0; i <= last_slash && base[i] && dpos + 1 < (int)sizeof(dir); i++) dir[dpos++] = base[i];
        if (dpos == 0) dir[dpos++] = '/';
        dir[dpos] = 0;
        combined[0] = 0;
        safari_append(combined, &cpos, sizeof(combined), dir);
        safari_append(combined, &cpos, sizeof(combined), p);
        safari_normalize_path_segments(combined, normalized_path, sizeof(normalized_path));
        safari_append(out, &pos, max, normalized_path);
    }
}

static int safari_ci_contains(const char *haystack, const char *needle) {
    int i;
    if (!haystack || !needle || !needle[0]) return 0;
    for (i = 0; haystack[i]; i++) {
        int j = 0;
        while (needle[j] && haystack[i + j] && safari_ieq_char(haystack[i + j], needle[j])) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

static int safari_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int safari_html_entity_char(const char *p, char *out_ch, int *consumed) {
    uint32_t code = 0;
    int i = 0;
    int base = 10;
    if (!p || !out_ch || !consumed) return 0;
    if (safari_starts_with_ci(p, "amp;")) { *out_ch = '&'; *consumed = 4; return 1; }
    if (safari_starts_with_ci(p, "lt;")) { *out_ch = '<'; *consumed = 3; return 1; }
    if (safari_starts_with_ci(p, "gt;")) { *out_ch = '>'; *consumed = 3; return 1; }
    if (safari_starts_with_ci(p, "nbsp;")) { *out_ch = ' '; *consumed = 5; return 1; }
    if (safari_starts_with_ci(p, "quot;")) { *out_ch = '"'; *consumed = 5; return 1; }
    if (safari_starts_with_ci(p, "apos;")) { *out_ch = '\''; *consumed = 5; return 1; }
    if (p[0] != '#') return 0;
    i = 1;
    if (p[i] == 'x' || p[i] == 'X') {
        base = 16;
        i++;
    }
    if (safari_hex_value(p[i]) < 0) return 0;
    while (p[i] && p[i] != ';' && i < 10) {
        int digit = base == 16 ? safari_hex_value(p[i]) :
                    ((p[i] >= '0' && p[i] <= '9') ? p[i] - '0' : -1);
        if (digit < 0 || digit >= base) return 0;
        code = code * (uint32_t)base + (uint32_t)digit;
        i++;
    }
    if (p[i] != ';') return 0;
    if (code == 8216U || code == 8217U) code = '\'';
    else if (code == 8220U || code == 8221U) code = '"';
    else if (code == 8211U || code == 8212U) code = '-';
    else if (code == 8230U) code = '.';
    if (code == 160U) code = ' ';
    if (code < 32U || code > 126U) return 0;
    *out_ch = (char)code;
    *consumed = i + 1;
    return 1;
}

static void safari_html_decode_inline(char *s) {
    int r = 0;
    int w = 0;
    if (!s) return;
    while (s[r]) {
        if (s[r] == '&') {
            char decoded = 0;
            int consumed = 0;
            if (safari_html_entity_char(s + r + 1, &decoded, &consumed)) {
                s[w++] = decoded;
                r += consumed + 1;
                continue;
            }
        }
        s[w++] = s[r++];
    }
    s[w] = 0;
}

static int safari_decode_chunked_body(const char *body, char *out, int max) {
    const char *p = body;
    int pos = 0;
    if (!body || !out || max <= 0) return -1;
    out[0] = 0;
    while (*p) {
        uint32_t chunk_size = 0;
        int any = 0;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        while (safari_hex_value(*p) >= 0) {
            chunk_size = (chunk_size << 4) | (uint32_t)safari_hex_value(*p);
            any = 1;
            p++;
        }
        if (!any) return -1;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
        if (chunk_size == 0) break;
        {
            uint32_t remaining = chunk_size;
            while (remaining > 0 && *p && pos + 1 < max) {
                out[pos++] = *p++;
                remaining--;
            }
            while (remaining > 0 && *p) { p++; remaining--; }
            if (remaining > 0) return -1;
        }
        if (p[0] == '\r' && p[1] == '\n') p += 2;
        else if (*p == '\n') p++;
    }
    out[pos] = 0;
    return pos > 0 ? 0 : -1;
}

static const char *safari_http_body(const char *response) {
    const char *p = response;
    if (!p) return "";
    while (*p) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') return p + 4;
        if (p[0] == '\n' && p[1] == '\n') return p + 2;
        p++;
    }
    return response;
}

static void safari_extract_title(const char *body, char *out, int max) {
    const char *p = body;
    int pos = 0;
    safari_copy(out, max, "");
    if (!body || !out || max <= 0) return;
    while (*p) {
        if (safari_starts_with_ci(p, "<title")) {
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            while (*p && !safari_starts_with_ci(p, "</title>") && pos + 1 < max) {
                char ch = *p++;
                if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
                out[pos++] = ch;
            }
            out[pos] = 0;
            safari_html_decode_inline(out);
            return;
        }
        p++;
    }
}

static void safari_text_from_body(const char *body, char *out, int max) {
    int pos = 0;
    int in_tag = 0;
    int skip_script = 0;
    int skip_style = 0;
    int space_pending = 0;
    int line_start = 1;
    const char *p = body;
    if (!out || max <= 0) return;
    out[0] = 0;
    if (!body) return;
    while (*p && pos + 1 < max) {
        char ch = *p++;
        if (skip_script || skip_style) {
            if (ch == '<' &&
                ((skip_script && safari_starts_with_ci(p, "/script")) ||
                 (skip_style && safari_starts_with_ci(p, "/style")))) {
                skip_script = 0;
                skip_style = 0;
                in_tag = 1;
            }
            continue;
        }
        if (ch == '<') {
            in_tag = 1;
            if (safari_starts_with_ci(p, "script")) {
                skip_script = 1;
            } else if (safari_starts_with_ci(p, "style")) {
                skip_style = 1;
            } else if (safari_starts_with_ci(p, "br") ||
                safari_starts_with_ci(p, "/p") ||
                safari_starts_with_ci(p, "p") ||
                safari_starts_with_ci(p, "/div") ||
                safari_starts_with_ci(p, "div") ||
                safari_starts_with_ci(p, "/h") ||
                safari_starts_with_ci(p, "h") ||
                safari_starts_with_ci(p, "/li") ||
                safari_starts_with_ci(p, "li")) {
                if (!line_start && pos + 1 < max) {
                    out[pos++] = '\n';
                    line_start = 1;
                    space_pending = 0;
                }
            }
            continue;
        }
        if (in_tag) {
            if (ch == '>') in_tag = 0;
            continue;
        }
        if (ch == '&') {
            char decoded = 0;
            int consumed = 0;
            if (safari_html_entity_char(p, &decoded, &consumed)) {
                ch = decoded;
                p += consumed;
            }
        }
        if (ch == '\r') continue;
        if (ch == '\n') {
            if (!line_start) {
                out[pos++] = '\n';
                line_start = 1;
                space_pending = 0;
            }
            continue;
        }
        if (ch == '\t' || ch == ' ') {
            if (!line_start) space_pending = 1;
            continue;
        }
        if ((unsigned char)ch < 32 || (unsigned char)ch > 126) continue;
        if (space_pending && pos + 1 < max) {
            out[pos++] = ' ';
            space_pending = 0;
        }
        out[pos++] = ch;
        line_start = 0;
    }
    out[pos] = 0;
    if (pos == 0) safari_copy(out, max, "(empty response)");
}

static void safari_clear_links(void) {
    int i;
    g_safari_link_count = 0;
    for (i = 0; i < SAFARI_MAX_LINKS; i++) {
        g_safari_link_urls[i][0] = 0;
        g_safari_link_titles[i][0] = 0;
    }
}

static void safari_clear_forms(void) {
    int i;
    g_safari_form_count = 0;
    g_safari_form_focused = -1;
    for (i = 0; i < SAFARI_MAX_FORMS; i++) {
        g_safari_form_actions[i][0] = 0;
        g_safari_form_methods[i][0] = 0;
        g_safari_form_input_names[i][0] = 0;
        g_safari_form_values[i][0] = 0;
        g_safari_form_query_prefix[i][0] = 0;
        g_safari_form_titles[i][0] = 0;
    }
}

static void safari_reset_page_view(void) {
    g_safari_page_scroll = 0;
    safari_clear_links();
    safari_clear_forms();
}

static int safari_tag_is(const char *p, const char *name) {
    int i = 0;
    if (!p || *p != '<' || !name) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    while (name[i]) {
        if (!p[i] || !safari_ieq_char(p[i], name[i])) return 0;
        i++;
    }
    return p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n' ||
           p[i] == '>' || p[i] == '/';
}

static int safari_attr_boundary(char ch) {
    return ch == '<' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '/';
}

static int safari_attr_name_matches(const char *p, const char *name) {
    int i = 0;
    if (!p || !name) return 0;
    while (name[i]) {
        if (!p[i] || !safari_ieq_char(p[i], name[i])) return 0;
        i++;
    }
    return p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n' || p[i] == '=';
}

static int safari_tag_attr(const char *tag_start, const char *tag_end, const char *name, char *out, int max) {
    const char *p = tag_start;
    if (!tag_start || !tag_end || !name || !out || max <= 0) return -1;
    out[0] = 0;
    while (p < tag_end && *p) {
        if (safari_attr_boundary(p == tag_start ? '<' : p[-1]) && safari_attr_name_matches(p, name)) {
            int i = 0;
            char quote = 0;
            while (p < tag_end && *p && *p != '=' && *p != '>' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
            while (p < tag_end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p >= tag_end || *p != '=') return -1;
            p++;
            while (p < tag_end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p < tag_end && (*p == '\"' || *p == '\'')) quote = *p++;
            while (p < tag_end && *p && i + 1 < max) {
                if ((quote && *p == quote) || (!quote && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '>'))) break;
                out[i++] = *p++;
            }
            out[i] = 0;
            safari_html_decode_inline(out);
            return out[0] ? 0 : -1;
        }
        p++;
    }
    return -1;
}

static int safari_anchor_href(const char *tag_start, const char *tag_end, char *out, int max) {
    return safari_tag_attr(tag_start, tag_end, "href", out, max);
}

static void safari_anchor_text(const char *start, const char *end, char *out, int max) {
    int pos = 0;
    int in_tag = 0;
    const char *p = start;
    if (!out || max <= 0) return;
    out[0] = 0;
    while (p && p < end && *p && pos + 1 < max) {
        char ch = *p++;
        if (ch == '<') { in_tag = 1; continue; }
        if (in_tag) { if (ch == '>') in_tag = 0; continue; }
        if (ch == '&') {
            char decoded = 0;
            int consumed = 0;
            if (safari_html_entity_char(p, &decoded, &consumed)) {
                ch = decoded;
                p += consumed;
            }
        }
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if ((unsigned char)ch >= 32 && (unsigned char)ch < 127) out[pos++] = ch;
    }
    while (pos > 0 && out[pos - 1] == ' ') pos--;
    out[pos] = 0;
}

static void safari_add_link(const safari_request_t *req, const char *href, const char *label) {
    char resolved[SAFARI_URL_MAX];
    int idx;
    if (!href || !href[0] || g_safari_link_count >= SAFARI_MAX_LINKS) return;
    safari_resolve_location(req, href, resolved, sizeof(resolved));
    if (!resolved[0]) return;
    idx = g_safari_link_count++;
    safari_copy(g_safari_link_urls[idx], SAFARI_URL_MAX, resolved);
    safari_copy(g_safari_link_titles[idx], SAFARI_LINK_TITLE_MAX,
                label && label[0] ? label : resolved);
}

static void safari_extract_links(const safari_request_t *req, const char *body) {
    const char *p = body;
    safari_clear_links();
    while (p && *p && g_safari_link_count < SAFARI_MAX_LINKS) {
        if (safari_starts_with_ci(p, "<a") && (p[2] == ' ' || p[2] == '\t' || p[2] == '>')) {
            const char *tag_end = p;
            const char *text_start;
            const char *text_end;
            char href[SAFARI_URL_MAX];
            char label[SAFARI_LINK_TITLE_MAX];
            while (*tag_end && *tag_end != '>') tag_end++;
            if (!*tag_end) break;
            if (safari_anchor_href(p, tag_end, href, sizeof(href)) == 0) {
                text_start = tag_end + 1;
                text_end = text_start;
                while (*text_end && !safari_starts_with_ci(text_end, "</a")) text_end++;
                safari_anchor_text(text_start, text_end, label, sizeof(label));
                safari_add_link(req, href, label);
                p = *text_end ? text_end : tag_end + 1;
                continue;
            }
            p = tag_end + 1;
            continue;
        }
        p++;
    }
}


static int safari_form_text_type(const char *type) {
    return !type || !type[0] ||
           safari_eq_ci(type, "text") || safari_eq_ci(type, "search") || safari_eq_ci(type, "url") ||
           safari_eq_ci(type, "email") || safari_eq_ci(type, "number") || safari_eq_ci(type, "password");
}

static void safari_request_current_url(const safari_request_t *req, char *out, int max) {
    int pos = 0;
    if (!out || max <= 0) return;
    out[0] = 0;
    if (!req || !req->host[0]) return;
    safari_append(out, &pos, max, req->scheme[0] ? req->scheme : "http");
    safari_append(out, &pos, max, "://");
    safari_append(out, &pos, max, req->host);
    if ((str_eq(req->scheme, "http") && req->port != 80) ||
        (str_eq(req->scheme, "https") && req->port != 443)) {
        safari_append(out, &pos, max, ":");
        safari_append_uint(out, &pos, max, req->port);
    }
    safari_append(out, &pos, max, req->path[0] ? req->path : "/");
}

static void safari_append_form_pair(char *query, int *pos, int max, const char *name, const char *value) {
    char enc_name[SAFARI_FORM_NAME_MAX * 3];
    char enc_value[SAFARI_FORM_VALUE_MAX * 3];
    if (!query || !pos || !name || !name[0] || max <= 0) return;
    safari_url_encode_component(name, enc_name, sizeof(enc_name));
    safari_url_encode_component(value ? value : "", enc_value, sizeof(enc_value));
    if (*pos > 0) safari_append(query, pos, max, "&");
    safari_append(query, pos, max, enc_name);
    safari_append(query, pos, max, "=");
    safari_append(query, pos, max, enc_value);
}

static void safari_extract_forms(const safari_request_t *req, const char *body) {
    const char *p = body;
    safari_clear_forms();
    while (p && *p && g_safari_form_count < SAFARI_MAX_FORMS) {
        if (safari_tag_is(p, "form")) {
            const char *tag_end = p;
            const char *form_end;
            const char *q;
            char action[SAFARI_URL_MAX];
            char method[8];
            char resolved[SAFARI_URL_MAX];
            char input_name[SAFARI_FORM_NAME_MAX];
            char input_value[SAFARI_FORM_VALUE_MAX];
            char input_hint[SAFARI_LINK_TITLE_MAX];
            char query[SAFARI_FORM_QUERY_MAX];
            int query_pos = 0;
            while (*tag_end && *tag_end != '>') tag_end++;
            if (!*tag_end) break;
            form_end = tag_end + 1;
            while (*form_end && !safari_starts_with_ci(form_end, "</form")) form_end++;
            if (!*form_end) form_end = tag_end + 1;
            action[0] = 0;
            method[0] = 0;
            input_name[0] = 0;
            input_value[0] = 0;
            input_hint[0] = 0;
            query[0] = 0;
            if (safari_tag_attr(p, tag_end, "action", action, sizeof(action)) == 0)
                safari_resolve_location(req, action, resolved, sizeof(resolved));
            else
                safari_request_current_url(req, resolved, sizeof(resolved));
            if (safari_tag_attr(p, tag_end, "method", method, sizeof(method)) < 0)
                safari_copy(method, sizeof(method), "get");
            for (q = tag_end + 1; q < form_end && *q; q++) {
                if (safari_tag_is(q, "input")) {
                    const char *input_end = q;
                    char type[16];
                    char name[SAFARI_FORM_NAME_MAX];
                    char value[SAFARI_FORM_VALUE_MAX];
                    char placeholder[SAFARI_LINK_TITLE_MAX];
                    while (*input_end && input_end < form_end && *input_end != '>') input_end++;
                    if (!*input_end || input_end > form_end) break;
                    type[0] = 0;
                    name[0] = 0;
                    value[0] = 0;
                    placeholder[0] = 0;
                    (void)safari_tag_attr(q, input_end, "type", type, sizeof(type));
                    (void)safari_tag_attr(q, input_end, "name", name, sizeof(name));
                    (void)safari_tag_attr(q, input_end, "value", value, sizeof(value));
                    (void)safari_tag_attr(q, input_end, "placeholder", placeholder, sizeof(placeholder));
                    if (safari_eq_ci(type, "hidden")) {
                        safari_append_form_pair(query, &query_pos, sizeof(query), name, value);
                    } else if (safari_form_text_type(type) && !input_name[0] && name[0]) {
                        safari_copy(input_name, sizeof(input_name), name);
                        safari_copy(input_value, sizeof(input_value), value);
                        safari_copy(input_hint, sizeof(input_hint), placeholder[0] ? placeholder : name);
                    } else if (safari_eq_ci(type, "submit") && value[0] && !input_hint[0]) {
                        safari_copy(input_hint, sizeof(input_hint), value);
                    }
                    q = input_end;
                }
            }
            if (resolved[0]) {
                int idx = g_safari_form_count++;
                safari_copy(g_safari_form_actions[idx], SAFARI_URL_MAX, resolved);
                safari_copy(g_safari_form_methods[idx], sizeof(g_safari_form_methods[idx]), method);
                safari_copy(g_safari_form_input_names[idx], SAFARI_FORM_NAME_MAX, input_name);
                safari_copy(g_safari_form_values[idx], SAFARI_FORM_VALUE_MAX, input_value);
                safari_copy(g_safari_form_query_prefix[idx], SAFARI_FORM_QUERY_MAX, query);
                safari_copy(g_safari_form_titles[idx], SAFARI_LINK_TITLE_MAX,
                            input_hint[0] ? input_hint : (input_name[0] ? input_name : "Submit form"));
            }
            p = form_end;
            continue;
        }
        p++;
    }
}

static int safari_url_contains_query(const char *url) {
    int i;
    if (!url) return 0;
    for (i = 0; url[i]; i++) {
        if (url[i] == '?') return 1;
        if (url[i] == '#') return 0;
    }
    return 0;
}

void safari_focus_form(int index) {
    safari_normalize_state();
    if (index >= 0 && index < g_safari_form_count)
        g_safari_form_focused = index;
    else
        g_safari_form_focused = -1;
}

void safari_submit_form(int index) {
    char target[SAFARI_URL_MAX];
    char encoded_pair[SAFARI_FORM_QUERY_MAX];
    int pos;
    int qpos = 0;
    if (index < 0 || index >= g_safari_form_count) return;
    safari_copy(target, sizeof(target), g_safari_form_actions[index]);
    pos = str_len(target);
    encoded_pair[0] = 0;
    if (g_safari_form_query_prefix[index][0])
        safari_append(encoded_pair, &qpos, sizeof(encoded_pair), g_safari_form_query_prefix[index]);
    if (g_safari_form_input_names[index][0])
        safari_append_form_pair(encoded_pair, &qpos, sizeof(encoded_pair),
                                g_safari_form_input_names[index],
                                g_safari_form_values[index]);
    g_safari_form_focused = -1;
    if (safari_eq_ci(g_safari_form_methods[index], "post")) {
        safari_load_url_internal(target, "POST", encoded_pair, 1, 0);
        return;
    }
    if (encoded_pair[0]) {
        safari_append(target, &pos, sizeof(target), safari_url_contains_query(target) ? "&" : "?");
        safari_append(target, &pos, sizeof(target), encoded_pair);
    }
    safari_load_url(target);
}

static void safari_load_url_internal(const char *url, const char *method,
                                     const char *request_body,
                                     int record_history, int redirect_depth) {
    safari_request_t req;
    char normalized[SAFARI_URL_MAX];
    char *response = g_safari_response_buf;
    char *decoded_body = g_safari_decoded_body_buf;
    char header_value[SAFARI_STATUS_MAX];
    char err[SAFARI_STATUS_MAX];
    char title[SAFARI_TITLE_MAX];
    int parsed;
    int n;
    int sp = 0;
    int https_tls13_loaded = 0;
    safari_tls13_probe_state_t https_tls_state;
    const char *http_method = (method && method[0]) ? method : "GET";
    const char *http_body = request_body ? request_body : "";
    safari_normalize_state();
    safari_normalize_url_text(url, normalized, sizeof(normalized));
    safari_copy(g_safari_url, SAFARI_URL_MAX, normalized);
    safari_copy(g_safari_tab_urls[g_safari_active_tab], SAFARI_URL_MAX, normalized);
    if (safari_is_home_url(normalized)) {
        safari_reset_page_view();
        g_safari_page_state = 0;
        g_safari_page_status_code = 0;
        g_safari_loaded_tick = timer_ticks();
        safari_copy(g_safari_page_url, SAFARI_URL_MAX, "about:home");
        safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, "MyOS Home");
        safari_copy(g_safari_page_status, SAFARI_STATUS_MAX, "Ready");
        safari_copy(g_safari_page_text, SAFARI_PAGE_TEXT_MAX, "");
        safari_set_tab_title("MyOS Home");
        if (record_history) safari_history_push_url("about:home");
        return;
    }
    parsed = safari_parse_url(normalized, &req);
    if (parsed != 0) {
        safari_reset_page_view();
        g_safari_page_state = 2;
        g_safari_page_status_code = 0;
        safari_copy(g_safari_page_url, SAFARI_URL_MAX, normalized);
        safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, "Cannot Open Page");
        safari_copy(g_safari_page_text, SAFARI_PAGE_TEXT_MAX,
                    parsed == -2 ? "DNS lookup failed for this host." : "The address is not a valid HTTP URL.");
        safari_copy(g_safari_page_status, SAFARI_STATUS_MAX,
                    parsed == -2 ? "DNS lookup failed" : "Bad address");
        safari_set_tab_title("Load Error");
        if (record_history) safari_history_push_url(normalized);
        return;
    }
    if (str_eq(req.scheme, "https")) {
        char http_alt[SAFARI_URL_MAX];
        char tls_err[SAFARI_STATUS_MAX];
        safari_tls13_probe_state_t tls_state;
        int tls_n;
        int ap = 0;
        err[0] = 0;
        n = safari_fetch_https_tls13(&req, http_method, http_body,
                                     response, SAFARI_RESPONSE_MAX,
                                     err, sizeof(err), &https_tls_state);
        if (n >= 0) {
            https_tls13_loaded = 1;
            goto have_response;
        }
        safari_reset_page_view();
        g_safari_page_state = 2;
        g_safari_page_status_code = 0;
        safari_copy(g_safari_page_url, SAFARI_URL_MAX, normalized);
        tls_err[0] = 0;
        tls_state.ready = 0;
        tls_state.client_hello_len = 0;
        tls_n = safari_fetch_tls_probe(&req, (uint8_t *)response, SAFARI_RESPONSE_MAX,
                                       tls_err, sizeof(tls_err), &tls_state);
        if (tls_n > 0) {
            safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, "TLS Handshake");
            safari_describe_tls_probe(&req, (const uint8_t *)response, tls_n, &tls_state,
                                      g_safari_page_text, SAFARI_PAGE_TEXT_MAX);
            safari_copy(g_safari_page_status, SAFARI_STATUS_MAX,
                        err[0] ? err : "TLS handshake reached server");
        } else {
            safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, "TLS Connection Failed");
            safari_copy(g_safari_page_text, SAFARI_PAGE_TEXT_MAX,
                        err[0] ? err : (tls_err[0] ? tls_err : "TLS handshake did not complete."));
            safari_copy(g_safari_page_status, SAFARI_STATUS_MAX,
                        err[0] ? err : (tls_err[0] ? tls_err : "TLS handshake failed"));
        }
        http_alt[0] = 0;
        safari_append(http_alt, &ap, sizeof(http_alt), "http://");
        safari_append(http_alt, &ap, sizeof(http_alt), req.host);
        safari_append(http_alt, &ap, sizeof(http_alt), req.path[0] ? req.path : "/");
        if (http_alt[0] && g_safari_link_count < SAFARI_MAX_LINKS) {
            safari_copy(g_safari_link_urls[g_safari_link_count], SAFARI_URL_MAX, http_alt);
            safari_copy(g_safari_link_titles[g_safari_link_count], SAFARI_LINK_TITLE_MAX, "Open HTTP version");
            g_safari_link_count++;
        }
        safari_set_tab_title(tls_n > 0 ? "TLS Handshake" : "TLS Error");
        if (record_history) safari_history_push_url(normalized);
        return;
    }
    err[0] = 0;
    n = req.local ? safari_fetch_local_http(&req, http_method, http_body, response, SAFARI_RESPONSE_MAX)
                  : safari_fetch_raw_http(&req, http_method, http_body, response, SAFARI_RESPONSE_MAX, err, sizeof(err));
have_response:
    g_safari_loaded_tick = timer_ticks();
    safari_copy(g_safari_page_url, SAFARI_URL_MAX, normalized);
    if (n < 0) {
        safari_reset_page_view();
        g_safari_page_state = 2;
        g_safari_page_status_code = 0;
        safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, "Cannot Open Page");
        safari_copy(g_safari_page_text, SAFARI_PAGE_TEXT_MAX, err[0] ? err : "Connection failed.");
        safari_copy(g_safari_page_status, SAFARI_STATUS_MAX, err[0] ? err : "Connection failed");
        safari_set_tab_title("Load Error");
        if (record_history) safari_history_push_url(normalized);
        return;
    }
    g_safari_page_status_code = (uint32_t)safari_http_status_code(response);
    safari_store_cookies_from_response(&req, response);
    if (safari_is_redirect_status(g_safari_page_status_code)) {
        char location[SAFARI_URL_MAX];
        char resolved[SAFARI_URL_MAX];
        if (safari_header_value(response, "Location", location, sizeof(location)) == 0) {
            if (redirect_depth >= SAFARI_REDIRECT_MAX) {
                safari_reset_page_view();
                g_safari_page_state = 2;
                safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, "Too Many Redirects");
                safari_copy(g_safari_page_status, SAFARI_STATUS_MAX, "Redirect limit reached");
                safari_copy(g_safari_page_text, SAFARI_PAGE_TEXT_MAX, "The page redirected too many times.");
                safari_set_tab_title("Redirect Error");
                if (record_history) safari_history_push_url(normalized);
                return;
            }
            safari_resolve_location(&req, location, resolved, sizeof(resolved));
            if (resolved[0]) {
                safari_load_url_internal(resolved,
                                         g_safari_page_status_code == 303U ? "GET" : http_method,
                                         g_safari_page_status_code == 303U ? "" : http_body,
                                         record_history, redirect_depth + 1);
                return;
            }
        }
    }
    safari_reset_page_view();
    {
        const char *body = safari_http_body(response);
        if (safari_header_value(response, "Transfer-Encoding", header_value, sizeof(header_value)) == 0 &&
            safari_ci_contains(header_value, "chunked") &&
            safari_decode_chunked_body(body, decoded_body, SAFARI_RESPONSE_MAX) == 0) {
            body = decoded_body;
        }
        title[0] = 0;
        safari_extract_title(body, title, sizeof(title));
        if (!title[0]) safari_copy(title, sizeof(title), req.host);
        safari_text_from_body(body, g_safari_page_text, SAFARI_PAGE_TEXT_MAX);
        safari_extract_links(&req, body);
        safari_extract_forms(&req, body);
    }
    g_safari_page_state = (g_safari_page_status_code >= 400U) ? 2 : 1;
    safari_copy(g_safari_page_title, SAFARI_TITLE_MAX, title);
    g_safari_page_status[0] = 0;
    safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX,
                  str_eq(req.scheme, "https") ? "HTTPS " : "HTTP ");
    safari_append_uint(g_safari_page_status, &sp, SAFARI_STATUS_MAX, g_safari_page_status_code);
    safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX, " from ");
    safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX, req.host);
    safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX, " (");
    safari_append_ipv4(g_safari_page_status, &sp, SAFARI_STATUS_MAX, req.ipv4);
    safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX, ")");
    if (https_tls13_loaded) {
        if (https_tls_state.cert_seen &&
            https_tls_state.cert_host_ok &&
            https_tls_state.cert_time_ok) {
            safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX,
                          " TLS1.3 cert host/time ok; chain not trusted");
        } else if (https_tls_state.cert_seen) {
            safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX,
                          " TLS1.3 cert warning; chain not trusted");
        } else {
            safari_append(g_safari_page_status, &sp, SAFARI_STATUS_MAX,
                          " TLS1.3 cert unparsed; chain not trusted");
        }
    }
    safari_set_tab_title(title);
    if (record_history) safari_history_push_url(g_safari_page_url);
}

void safari_load_url(const char *url) {
    safari_load_url_internal(url, 0, 0, 1, 0);
}

void safari_load_current_tab(void) {
    const char *target;
    safari_normalize_state();
    target = g_safari_tab_urls[g_safari_active_tab][0] ? g_safari_tab_urls[g_safari_active_tab] : g_safari_url;
    safari_load_url_internal(target, 0, 0, 0, 0);
}

void safari_reload(void) {
    safari_load_current_tab();
}

/* =========================================================================
 * Maps view mode: 0=Standard, 1=Satellite, 2=Hybrid
 * ======================================================================= */
int g_maps_view = 0;

/* =========================================================================
 * Finder view mode (0=icon, 1=list)
 * ======================================================================= */
int g_finder_view = 0;

/* =========================================================================
 * Photos state
 * ======================================================================= */
int g_photos_sel        = 0;
int g_photos_section    = 0;
int g_photos_fullscreen = 0;
int g_photos_edit_mode  = 0; /* 0=view, 1=edit tools visible */
int g_photos_brightness = 50; /* 0-100 */
int g_photos_contrast   = 50;
int g_photos_saturation = 50;
int g_photos_edit_tool  = 0; /* 0=Adjust, 1=Crop, 2=Filter */

/* =========================================================================
 * ======================================================================= */

/* =========================================================================
 * Dictionary interactive search state
 * ======================================================================= */
char g_dict_input[32]  = {0};
int  g_dict_input_len  = 0;
int  g_dict_focused    = 0;
int  g_wordle_focused  = 0;  /* 1 when Wordle window is topmost */

const dict_entry_t g_dict_words[] = {
    {"serendipity",  "/ser-en-DIP-i-tee/",  "noun",       "the occurrence of events",    "by chance in a happy way."},
    {"ephemeral",    "/ih-FEM-er-ul/",       "adjective",  "lasting for a very short",    "time; transitory."},
    {"algorithm",    "/AL-go-rith-um/",      "noun",       "a set of rules to be",        "followed in calculations."},
    {"nostalgia",    "/nuh-STAL-juh/",       "noun",       "a longing for the past;",     "sentimental remembrance."},
    {"euphoria",     "/yoo-FOR-ee-uh/",      "noun",       "a feeling of intense",        "happiness or excitement."},
    {"melancholy",   "/MEL-un-kol-ee/",      "noun",       "a deep, pensive sadness;",    "thoughtful depression."},
    {"ubiquitous",   "/yoo-BIK-wih-tus/",   "adjective",  "present everywhere;",         "found all around."},
    {"pragmatic",    "/prag-MAT-ik/",        "adjective",  "dealing with things",         "practically; realistic."},
    {"eloquent",     "/EL-oh-kwent/",        "adjective",  "fluent and persuasive",       "in speaking or writing."},
    {"innovation",   "/in-oh-VAY-shun/",     "noun",       "introduction of new",         "ideas, methods, or things."},
    {"serenity",     "/suh-REN-ih-tee/",     "noun",       "the state of being calm,",    "peaceful and untroubled."},
    {"luminous",     "/LOO-mih-nus/",        "adjective",  "bright, shining; radiant;",   "glowing softly with light."},
    {"tenacious",    "/tuh-NAY-shus/",       "adjective",  "holding firmly to purpose;",  "persistent; not giving up."},
    {"wanderlust",   "/WON-der-lust/",       "noun",       "a strong desire to travel",   "and explore the world."},
    {"resilient",    "/rih-ZIL-yent/",       "adjective",  "able to recover quickly",     "from difficulties; tough."},
    {"sycophant",    "/SIK-oh-fant/",        "noun",       "a person who flatters",       "to gain advantage."},
    {"superfluous",  "/soo-PER-floo-us/",    "adjective",  "more than is needed;",        "unnecessary; excessive."},
    {"philosophy",   "/fih-LAH-suh-fee/",    "noun",       "the study of the nature",     "of knowledge and reality."},
    {"kernel",       "/KER-nel/",            "noun",       "the central or most",         "essential part of a thing."},
    {"binary",       "/BY-nair-ee/",         "noun",       "relating to two choices;",    "base-2 number system."},
    {"abstract",     "/AB-strakt/",          "adjective",  "relating to ideas rather",    "than physical objects."},
    {"altruism",     "/AL-troo-iz-um/",      "noun",       "selfless concern for others;","putting others first."},
    {"ambiguous",    "/am-BIG-yoo-us/",      "adjective",  "open to more than one",       "interpretation; unclear."},
    {"anachronism",  "/uh-NAK-ruh-niz-um/",  "noun",       "a thing out of its",          "proper historical period."},
    {"anomaly",      "/uh-NOM-uh-lee/",      "noun",       "something that deviates",     "from what is standard."},
    {"benevolent",   "/buh-NEV-uh-lent/",    "adjective",  "well-meaning and kindly;",    "generous and charitable."},
    {"clandestine",  "/klan-DES-tin/",       "adjective",  "kept secret or hidden;",      "done in a stealthy way."},
    {"cogent",       "/KOH-jent/",           "adjective",  "clear, logical, and",         "convincing in argument."},
    {"conundrum",    "/kuh-NUN-drum/",       "noun",       "a confusing and difficult",   "problem or question."},
    {"cryptic",      "/KRIP-tik/",           "adjective",  "having a mysterious or",      "obscure meaning."},
    {"diligent",     "/DIL-ih-jent/",        "adjective",  "having steady, careful",      "effort; hardworking."},
    {"discern",      "/dih-SURN/",           "verb",       "to perceive or recognize",    "something with the mind."},
    {"elusive",      "/ih-LOO-siv/",         "adjective",  "difficult to find, catch,",   "or achieve; evasive."},
    {"empathy",      "/EM-puh-thee/",        "noun",       "the ability to understand",   "others' feelings as your own."},
    {"enigma",       "/ih-NIG-muh/",         "noun",       "a mysterious person or",      "thing; a puzzle or riddle."},
    {"fortuitous",   "/for-TYOO-ih-tus/",    "adjective",  "happening by chance,",        "especially good fortune."},
    {"gregarious",   "/grih-GAIR-ee-us/",    "adjective",  "fond of company; sociable;",  "living in groups."},
    {"hubris",       "/HYOO-bris/",          "noun",       "excessive pride or",          "self-confidence; arrogance."},
    {"immutable",    "/ih-MYOO-tuh-bul/",    "adjective",  "unchanging over time;",       "cannot be changed."},
    {"ineffable",    "/in-EF-uh-bul/",       "adjective",  "too great to be expressed",   "in words; indescribable."},
    {"juxtapose",    "/JUK-stuh-pohz/",      "verb",       "place two things close",      "together to contrast them."},
    {"laconic",      "/luh-KON-ik/",         "adjective",  "using very few words;",       "brief and concise."},
    {"lethargic",    "/luh-THAR-jik/",       "adjective",  "sluggish and apathetic;",     "lacking energy or enthusiasm."},
    {"mellifluous",  "/muh-LIF-loo-us/",     "adjective",  "sweet or musical; pleasant",  "to hear; smooth and rich."},
    {"mendacious",   "/men-DAY-shus/",       "adjective",  "not telling the truth;",      "lying; untruthful."},
    {"meticulous",   "/muh-TIK-yuh-lus/",    "adjective",  "showing great attention",     "to detail; very precise."},
    {"nonchalant",   "/non-shuh-LAHNT/",     "adjective",  "appearing casually calm",     "and relaxed; indifferent."},
    {"oblivious",    "/uh-BLIV-ee-us/",      "adjective",  "not aware or conscious",      "of what is happening around."},
    {"opaque",       "/oh-PAYK/",            "adjective",  "not transparent; hard",       "to understand; unclear."},
    {"paradigm",     "/PAIR-uh-dym/",        "noun",       "a typical example or",        "model; a pattern or standard."},
    {"pensive",      "/PEN-siv/",            "adjective",  "engaged in deep thought,",    "often with sadness."},
    {"quintessential","/kwin-tuh-SEN-shul/", "adjective",  "representing the perfect",    "or most typical example."},
    {"recalcitrant", "/rih-KAL-sih-trant/",  "adjective",  "uncooperative; resistant",    "to authority or control."},
    {"sagacious",    "/suh-GAY-shus/",       "adjective",  "having good judgment;",       "wise and perceptive."},
    {"solitude",     "/SOL-ih-tood/",        "noun",       "the state of being alone;",   "peaceful isolation."},
    {"stoic",        "/STOH-ik/",            "adjective",  "enduring pain without",       "complaint; emotionally calm."},
    {"sublime",      "/suh-BLYM/",           "adjective",  "of great beauty or",          "excellence; awe-inspiring."},
    {"synthesis",    "/SIN-thuh-sis/",       "noun",       "the combination of parts",    "to form a unified whole."},
    {"tacit",        "/TAS-it/",             "adjective",  "understood without being",    "stated; implied; silent."},
    {"transcend",    "/tran-SEND/",          "verb",       "to be or go beyond the",      "range of normal limits."},
    {"verbose",      "/ver-BOHS/",           "adjective",  "using or expressed in",       "more words than needed."},
    {"vivacious",    "/vih-VAY-shus/",       "adjective",  "attractively lively",         "and animated; spirited."},
    {"whimsical",    "/WIM-zih-kul/",        "adjective",  "playfully quaint or",         "fanciful; not serious."},
    {"zealous",      "/ZEL-us/",             "adjective",  "having great energy and",     "enthusiasm for a cause."},
    {0, 0, 0, 0, 0}
};

/* =========================================================================
 * Settings active tab
 * ======================================================================= */
int g_settings_tab = 0;

/* Desktop widgets visibility */
int g_widgets_visible = 1;

/* Activity Monitor active tab: 0=CPU, 1=Memory, 2=Disk, 3=Network */
int g_am_tab = 0;

/* Reminders: which items are checked (bitmask for 3 items) */
int g_reminders_done = 0x01; /* item 0 checked by default */
int g_reminders_sel_list = 0; /* selected list in sidebar */
int g_reminders_extra_lists = 0;
int g_reminders_extra_items = 0;
int g_appstore_tab = 0;
int g_appstore_downloads = 0;
int g_appstore_search_focused = 0;
int g_migration_source = 0;
int g_migration_step = 0;
int g_books_tab = 0;
int g_books_reading = 0;
int g_books_selected = 0;
int g_findmy_selected = 0;
int g_findmy_search_focused = 0;
int g_news_selected = 0;
int g_contacts_search_focused = 0;

/* Music: show equalizer (always on) */
int g_music_eq_visible = 1;

/* Dark mode toggle (declared here for use in toast_draw before full decl) */
int g_pref_darkmode  = 0;

/* Screen Time state */
int g_screen_time_tab = 0; /* 0=App Usage, 1=Downtime */

/* Passwords state */
int g_passwords_sel = 0; /* selected sidebar category */
int g_passwords_entry = 0; /* selected password entry */
int g_passwords_added = 0;
int g_passwords_search_focused = 0;

/* Numbers state */
int g_numbers_sel_row = 0;
int g_numbers_sel_col = 0;
int g_calendar_added_events = 0;
int g_calendar_added_day = 0;

/* =========================================================================
 * Notification toast state
 * ======================================================================= */

char     g_toast_msg[64]  = {0};
char     g_toast_sub[64]  = {0};
uint32_t g_toast_color    = 0;
uint32_t g_toast_birth    = 0;  /* timer_ticks() when shown */
int      g_toast_visible  = 0;
int      g_toast_type     = 0;


void toast_show(const char *msg, const char *sub, uint32_t color) {
    int i;
    if (!msg) msg = "";
    if (!sub) sub = "";
    for (i = 0; msg[i] && i < 63; i++) g_toast_msg[i] = msg[i];
    g_toast_msg[i] = 0;
    for (i = 0; sub[i] && i < 63; i++) g_toast_sub[i] = sub[i];
    g_toast_sub[i] = 0;
    g_toast_color   = color;
    g_toast_birth   = timer_ticks();
    g_toast_visible = 1;
    g_toast_type    = TOAST_TYPE_DEFAULT;
    nc_add(msg, sub, color);
}

void toast_show_action(const char *msg, const char *sub, uint32_t color, int type) {
    toast_show(msg, sub, color);
    g_toast_type = type;
}

void toast_draw(void) {
    uint32_t age = timer_ticks() - g_toast_birth;
    if (age > TOAST_LIFE) { g_toast_visible = 0; return; }

    /* Slide-in / slide-out animation */
    int slide_ms = 280;
    int offset = 0;
    if ((int)age < slide_ms) {
        /* Slide in from right */
        offset = (TOAST_W + 10) * (slide_ms - (int)age) / slide_ms;
    } else if (age > (uint32_t)(TOAST_LIFE - slide_ms)) {
        /* Slide out to right */
        int fade_age = (int)(age - (TOAST_LIFE - slide_ms));
        offset = (TOAST_W + 10) * fade_age / slide_ms;
    }

    int tx = VGA_WIDTH - TOAST_W - 10 + offset;
    int ty = MENUBAR_H + 8;

    /* Soft multi-layer shadow */
    vga_fill_rect_alpha(tx+6, ty+6, TOAST_W, TOAST_H, RGB(0,0,0), 50);
    vga_fill_rect_alpha(tx+3, ty+3, TOAST_W, TOAST_H, RGB(0,0,0), 30);
    /* Frosted-glass background: fill semi-transparent then overlay tint */
    uint32_t toast_bg  = g_pref_darkmode ? RGB(44,44,48)    : RGB(248,248,252);
    uint32_t toast_bd  = g_pref_darkmode ? RGB(65,65,70)    : RGB(200,200,210);
    uint32_t toast_tx  = g_pref_darkmode ? RGB(220,220,224) : COLOR_TEXT;
    uint32_t toast_st  = g_pref_darkmode ? RGB(140,140,148) : RGB(100,100,110);
    gui_draw_rounded_rect(tx, ty, TOAST_W, TOAST_H, 10, toast_bg);
    /* Subtle frosted inner highlight */
    vga_fill_rect_alpha(tx+2, ty+2, TOAST_W-4, 4, RGB(255,255,255), 80);
    vga_draw_rect_outline(tx, ty, TOAST_W, TOAST_H, toast_bd);
    /* App icon rounded square */
    gui_draw_rounded_rect(tx+8, ty+TOAST_H/2-14, 28, 28, 6, g_toast_color);
    vga_fill_rect_alpha(tx+8, ty+TOAST_H/2-14, 28, 8, RGB(255,255,255), 50);
    char ic[2]; ic[0]=g_toast_msg[0]; ic[1]=0;
    vga_draw_string_trans(tx+18, ty+TOAST_H/2-4, ic, RGB(255,255,255));
    /* App name (bold-ish by drawing twice) */
    vga_draw_string_trans(tx+42, ty+8,  g_toast_msg, toast_tx);
    vga_draw_string_trans(tx+43, ty+8,  g_toast_msg, toast_tx);
    /* Subtitle */
    vga_draw_string_trans(tx+42, ty+22, g_toast_sub, toast_st);
    /* Time indicator — right-aligned */
    vga_draw_string_trans(tx+TOAST_W-30, ty+8, "now", toast_st);
    /* Thin colored accent line at left edge */
    vga_fill_rect(tx, ty+10, 3, TOAST_H-20, g_toast_color);

    /* Action buttons row */
    {
        int btn_y   = ty + 52;
        int btn_h   = 18;
        uint32_t btn_bg  = g_pref_darkmode ? RGB(60,60,66)    : RGB(230,230,240);
        uint32_t btn_tx2 = g_pref_darkmode ? RGB(180,200,255) : RGB(0,80,200);
        /* Divider */
        vga_fill_rect_alpha(tx+6, btn_y-2, TOAST_W-12, 1, toast_bd, 200);

        if (g_toast_type == TOAST_TYPE_REPLY) {
            /* [Reply]  [Dismiss] */
            int bw = (TOAST_W - 18) / 2;
            gui_draw_rounded_rect(tx+6,       btn_y, bw, btn_h, 5, btn_bg);
            gui_draw_rounded_rect(tx+6+bw+4,  btn_y, bw, btn_h, 5, btn_bg);
            vga_draw_string_trans(tx+6+bw/2-14,     btn_y+5, "Reply",   btn_tx2);
            vga_draw_string_trans(tx+6+bw+4+bw/2-18,btn_y+5, "Dismiss", toast_st);
        } else if (g_toast_type == TOAST_TYPE_SNOOZE) {
            /* [Snooze]  [Dismiss] */
            int bw = (TOAST_W - 18) / 2;
            gui_draw_rounded_rect(tx+6,       btn_y, bw, btn_h, 5, btn_bg);
            gui_draw_rounded_rect(tx+6+bw+4,  btn_y, bw, btn_h, 5, btn_bg);
            vga_draw_string_trans(tx+6+bw/2-17,      btn_y+5, "Snooze",  btn_tx2);
            vga_draw_string_trans(tx+6+bw+4+bw/2-18, btn_y+5, "Dismiss", toast_st);
        } else {
            /* [   Dismiss   ] */
            gui_draw_rounded_rect(tx+6, btn_y, TOAST_W-12, btn_h, 5, btn_bg);
            vga_draw_string_trans(tx+TOAST_W/2-18, btn_y+5, "Dismiss", toast_st);
        }
    }
}

/* =========================================================================
 * Launchpad state
 * ======================================================================= */

int g_lp_visible = 0;
int g_lp_page   = 0;  /* current Launchpad page (0-based) */
char g_lp_search[32] = {0};
int  g_lp_slen   = 0;

const lp_icon_t s_lp_icons[LP_ICON_COUNT] = {
    { "Finder",           RGB(41, 128,185), 'F' },
    { "Terminal",         RGB(30,  30, 30), 'T' },
    { "TextEdit",         RGB(255,140, 40), 'E' },
    { "Settings",         RGB(142,142,147), 'S' },
    { "Calculator",       RGB(200, 50, 50), 'C' },
    { "Clock",            RGB(80,  80,240), 'K' },
    { "Notes",            RGB(255,204,  0), 'N' },
    { "Photos",           RGB(240, 80,160), 'P' },
    { "Music",            RGB(252, 60, 68), 'M' },
    { "Maps",             RGB(60, 200, 80), 'G' },
    { "Safari",           RGB(40, 160,220), 'W' },
    { "App Store",        RGB(30, 120,255), 'A' },
    { "Calendar",         RGB(255, 59, 48), 'L' },
    { "Mail",             RGB(0,  140,255), '@' },
    { "Activity Monitor", RGB(100, 50,200), 'X' },
    { "System Info",      RGB(80, 140,200), 'I' },
    { "Weather",          RGB(30, 130,255), 'W' },
    { "FaceTime",         RGB(0,  200,100), 'F' },
    { "AirDrop",          RGB(0,  122,255), 'D' },
    { "Keyboard Shortcuts",RGB(100,80,180),'H' },
    { "Stocks",           RGB(52, 199, 89), '$' },
    { "News",             RGB(255, 59, 48), 'N' },
    { "Books",            RGB(255,149,  0), 'B' },
    { "Podcasts",         RGB(147, 44,246), 'P' },
    { "Home",             RGB(255,149,  0), 'H' },
    { "Reminders",        RGB(255, 59, 48), 'R' },
    { "Messages",         RGB(52, 199, 89), 'M' },
    { "Find My",          RGB(0,  122,255), 'F' },
    { "Wallet",           RGB(0,   80,200), 'W' },
    { "Voice Memos",      RGB(255, 59, 48), 'V' },
    { "Shortcuts",        RGB(255,149,  0), 'S' },
    { "Freeform",         RGB(0,  122,255), 'F' },
    { "Disk Utility",     RGB(100,100,200), 'D' },
    { "Screen Time",      RGB(52, 199, 89), 'T' },
    { "Passwords",        RGB(0,  122,255), 'P' },
    { "Numbers",          RGB(52, 199, 89), '#' },
    { "Focus",            RGB(100, 60,200), 'F' },
    { "Keynote",          RGB(255,149,  0), 'K' },
    { "Pages",            RGB(255,149,  0), 'P' },
    { "GarageBand",       RGB(220, 20, 60), 'G' },
    { "iMovie",           RGB(40, 140,220), 'V' },
    { "Xcode",            RGB(30, 120,255), 'X' },
    { "GameCenter",       RGB(52, 199, 89), 'G' },
    { "Automator",        RGB(238, 95,  0), 'A' },
    { "Font Book",        RGB(90,  90,100), 'f' },
    { "Console",          RGB(20,  20, 25), 'C' },
    { "iPhone Mirroring", RGB(0,  122,255), 'i' },
    { "Instruments",      RGB(220, 50, 50), 'I' },
    { "Network Utility",  RGB(40, 130,220), 'N' },
    { "Math Notes",       RGB(255,149,  0), '=' },
    { "Final Cut Pro",    RGB(220, 40, 40), 'F' },
    { "Logic Pro",        RGB(40,  40, 40), 'L' },
    { "Motion",           RGB(30, 200,220), 'M' },
    { "MainStage",        RGB(200, 30, 30), 'S' },
    { "Compressor",       RGB(80,  80, 90), 'C' },
    { "Screen Recording", RGB(255, 59, 48), 'R' },
    { "Sidecar",          RGB(0,  122,255), 'S' },
    { "Universal Control",RGB(100,100,200), 'U' },
    { "Handoff",          RGB(52, 199, 89), 'H' },
    { "Privacy",          RGB(60,  60, 65), 'P' },
    { "Accessibility",    RGB(0,  122,255), 'A' },
    { "AirPlay",          RGB(52, 199, 89), 'a' },
    { "TestFlight",       RGB(0,  122,255), 'T' },
    { "Reality Composer", RGB(80,  40,160), 'R' },
    { "Configurator",     RGB(40, 120,200), 'C' },
    { "Health",           RGB(255, 45, 85), 'H' },
    { "Sudoku",           RGB(88,  86,214), 'S' },
    { "Stickies",         RGB(255,220, 50), 'K' },
    { "Dictionary",       RGB(60, 120,200), 'D' },
    { "Chess",            RGB(40,  40, 40), 'C' },
    { "2048",             RGB(237,194, 46), '2' },
    { "Grapher",          RGB(40, 170,100), 'G' },
    { "Digital Color Meter",RGB(200,60,60),'M' },
    { "Feedback Assistant",RGB(0, 122,255), 'F' },
    { "Photo Booth",      RGB(220, 40, 80), 'P' },
    { "SF Symbols",       RGB(52, 199, 89), 'S' },
    { "Transporter",      RGB(40, 120,200), 'T' },
    { "AR Quick Look",    RGB(100,200,255), 'Q' },
    { "Clips",            RGB(255, 59, 48), 'C' },
    { "iStudiez Pro",     RGB(0,  122,255), 'i' },
    { "Lasso",            RGB(100, 80,200), 'L' },
    { "Transmit",         RGB(255,149,  0), 'T' },
    { "Proxyman",         RGB(52, 199, 89), 'X' },
    { "Overflow 3",       RGB(40, 120,200), 'O' },
    { "1Password",        RGB(0,  120,200), '1' },
    { "Fantastical",      RGB(220, 40, 40), 'F' },
    { "Things 3",         RGB(100, 60,200), 'T' },
    { "Raycast",          RGB(255, 90, 30), 'R' },
    { "Tot",              RGB(255,149,  0), '.' },
    { "Klokki",           RGB(52, 199, 89), 'K' },
    { "Bear",             RGB(220, 80, 40), 'B' },
    { "Reeder 5",         RGB(220, 50, 50), 'R' },
    { "CleanMyMac X",     RGB(0,  200,120), 'C' },
    { "Bartender 4",      RGB(60,  60, 80), 'B' },
    { "Alfred",           RGB(200, 50,200), 'A' },
    { "Scrobbles",        RGB(220, 40, 40), 'S' },
    { "Snake",            RGB(52,  199, 89), 'S' },
    { "Wordle",           RGB(108, 169,100), 'W' },
    { "Breakout",         RGB(100, 180,255), 'B' },
    { "Pong",             RGB(255, 180, 50), 'P' },
    { "Minesweeper",      RGB(80,  80, 200), 'M' },
    { "Journal",          RGB(255, 149,  0), 'J' },
    { "Contacts",         RGB(  0, 122,255), 'C' },
    { "Preview",          RGB(170,  50,170), 'P' },
    { "Apple TV",         RGB( 30,  30, 35), 'v' },
};

void launchpad_draw(void) {
    /* Dark blur background */
    vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(10,10,30), 210);

    /* Search bar */
    {
        int sb_w=200, sb_h=22, sb_x=(VGA_WIDTH-sb_w)/2, sb_y=12;
        vga_fill_rect_alpha(sb_x, sb_y, sb_w, sb_h, RGB(255,255,255), 50);
        gui_draw_rounded_rect_outline(sb_x, sb_y, sb_w, sb_h, 8, RGB(200,200,220));
        /* Search icon */
        gui_draw_circle(sb_x+14, sb_y+11, 5, RGB(0,0,0));
        gui_draw_circle(sb_x+14, sb_y+11, 4, RGB(200,200,220));
        vga_draw_line(sb_x+18, sb_y+15, sb_x+22, sb_y+19, RGB(160,160,180));
        if (g_lp_slen > 0)
            vga_draw_string_trans(sb_x+26, sb_y+7, g_lp_search, RGB(255,255,255));
        else
            vga_draw_string_trans(sb_x+26, sb_y+7, "Search...", RGB(160,160,180));
        if (g_lp_slen > 0) {
            /* Blinking cursor */
            if ((timer_ticks()/400)%2==0)
                vga_fill_rect(sb_x+26+g_lp_slen*8, sb_y+7, 2, 10, RGB(255,255,255));
        }
    }

    /* If searching: show filtered flat list */
    if (g_lp_slen > 0) {
        int gx2=60, gy2=44, ix2, iy2=gy2, count2=0;
        ix2 = gx2;
        int fi3;
        for (fi3=0; fi3<LP_ICON_COUNT; fi3++) {
            const char *nm = s_lp_icons[fi3].name;
            int k, match=1;
            for (k=0; k<g_lp_slen; k++) {
                char a=g_lp_search[k], b=nm[k];
                if (a>='A'&&a<='Z') a+=32;
                if (b>='A'&&b<='Z') b+=32;
                if (!b||a!=b) { match=0; break; }
            }
            if (!match) continue;
            /* Draw icon */
            vga_fill_rect_alpha(ix2+3, iy2+3, LP_ICON_SZ, LP_ICON_SZ, RGB(0,0,0), 60);
            gui_draw_rounded_rect(ix2, iy2, LP_ICON_SZ, LP_ICON_SZ, 14, s_lp_icons[fi3].color);
            vga_fill_rect_alpha(ix2+2, iy2+2, LP_ICON_SZ-4, LP_ICON_SZ/3, RGB(255,255,255), 40);
            { char icon_ch[2]; icon_ch[0] = s_lp_icons[fi3].letter; icon_ch[1] = 0;
              vga_draw_string_trans(ix2+(LP_ICON_SZ-8)/2, iy2+(LP_ICON_SZ-8)/2, icon_ch, RGB(255,255,255));
              vga_draw_string_trans(ix2+(LP_ICON_SZ-8)/2+1, iy2+(LP_ICON_SZ-8)/2, icon_ch, RGB(255,255,255)); }
            int nl2=str_len(nm); int lx2=ix2+(LP_ICON_SZ-nl2*8)/2; if(lx2<gx2)lx2=gx2;
            vga_draw_string_trans(lx2, iy2+LP_ICON_SZ+4, nm, RGB(230,230,230));
            count2++;
            ix2 += LP_ICON_SZ + LP_ICON_PAD;
            if (ix2 + LP_ICON_SZ > VGA_WIDTH - gx2) { ix2=gx2; iy2+=LP_ICON_SZ+20+LP_ICON_PAD; }
            if (iy2 + LP_ICON_SZ > VGA_HEIGHT - 50) break;
        }
        if (count2 == 0)
            vga_draw_string_trans(VGA_WIDTH/2-40, VGA_HEIGHT/2, "No Results", RGB(160,160,180));
        vga_draw_string_trans(10, VGA_HEIGHT-20, "ESC to clear search", RGB(160,160,160));
        return;
    }

    /* Page math */
    int icons_per_page = LAUNCHPAD_COLS * LAUNCHPAD_ROWS;
    int total_pages    = (LP_ICON_COUNT + icons_per_page - 1) / icons_per_page;
    if (g_lp_page >= total_pages) g_lp_page = total_pages - 1;
    if (g_lp_page < 0) g_lp_page = 0;
    int page_start = g_lp_page * icons_per_page;

    /* Title */
    {
        const char *title = "Launchpad";
        vga_draw_string_trans((VGA_WIDTH - str_len(title)*8)/2, 30, title, RGB(220,220,220));
    }

    int total_w = LAUNCHPAD_COLS * LP_ICON_SZ + (LAUNCHPAD_COLS-1) * LP_ICON_PAD;
    int total_h = LAUNCHPAD_ROWS * (LP_ICON_SZ + 20) + (LAUNCHPAD_ROWS-1) * LP_ICON_PAD;
    int gx = (VGA_WIDTH - total_w) / 2;
    int gy = (VGA_HEIGHT - total_h) / 2 - 10;
    int r, c;
    for (r = 0; r < LAUNCHPAD_ROWS; r++) {
        for (c = 0; c < LAUNCHPAD_COLS; c++) {
            int slot = r * LAUNCHPAD_COLS + c;
            int fi = page_start + slot;
            if (fi >= LP_ICON_COUNT) break;
            int ix = gx + c * (LP_ICON_SZ + LP_ICON_PAD);
            int iy = gy + r * (LP_ICON_SZ + 20 + LP_ICON_PAD);
            /* Icon shadow */
            vga_fill_rect_alpha(ix+3, iy+3, LP_ICON_SZ, LP_ICON_SZ, RGB(0,0,0), 60);
            /* Icon */
            gui_draw_rounded_rect(ix, iy, LP_ICON_SZ, LP_ICON_SZ, 14, s_lp_icons[fi].color);
            /* Shine */
            vga_fill_rect_alpha(ix+2, iy+2, LP_ICON_SZ-4, LP_ICON_SZ/3, RGB(255,255,255), 40);
            /* Letter centered */
            { char icon_ch[2]; icon_ch[0] = s_lp_icons[fi].letter; icon_ch[1] = 0;
              vga_draw_string_trans(ix+(LP_ICON_SZ-8)/2,   iy+(LP_ICON_SZ-8)/2, icon_ch, RGB(255,255,255));
              vga_draw_string_trans(ix+(LP_ICON_SZ-8)/2+1, iy+(LP_ICON_SZ-8)/2, icon_ch, RGB(255,255,255)); }
            /* Label */
            {
                const char *nm = s_lp_icons[fi].name;
                int nl = str_len(nm); if (nl > 10) nl = 10;
                int lx = ix + (LP_ICON_SZ - nl*8)/2;
                if (lx < gx) lx = gx;
                vga_draw_string_trans(lx, iy + LP_ICON_SZ + 4, nm, RGB(230,230,230));
            }
        }
    }

    /* Page dots */
    {
        int dot_r = 4, dot_gap = 12;
        int dots_w = total_pages * dot_gap;
        int dot_x = (VGA_WIDTH - dots_w) / 2;
        int dot_y = VGA_HEIGHT - 40;
        int pi;
        for (pi = 0; pi < total_pages; pi++) {
            if (pi == g_lp_page) {
                gui_draw_circle(dot_x + pi*dot_gap + dot_r, dot_y, dot_r, RGB(255,255,255));
            } else {
                gui_draw_circle(dot_x + pi*dot_gap + dot_r, dot_y, dot_r, RGB(100,100,100));
                gui_draw_circle(dot_x + pi*dot_gap + dot_r, dot_y, dot_r-1, RGB(10,10,30));
            }
        }
    }
    /* Page nav arrows */
    if (g_lp_page > 0)
        vga_draw_string_trans(20, VGA_HEIGHT/2 - 4, "<", RGB(200,200,200));
    if (g_lp_page < total_pages - 1)
        vga_draw_string_trans(VGA_WIDTH-24, VGA_HEIGHT/2 - 4, ">", RGB(200,200,200));

    /* Hint */
    vga_draw_string_trans(10, VGA_HEIGHT - 20, "ESC or L to close", RGB(160,160,160));
    { char pg_str[20]; pg_str[0]='P';pg_str[1]='g';pg_str[2]=' ';
      pg_str[3]='0'+g_lp_page+1;pg_str[4]='/';pg_str[5]='0'+total_pages;pg_str[6]=0;
      vga_draw_string_trans(VGA_WIDTH/2-24, VGA_HEIGHT-20, pg_str, RGB(140,140,140)); }
}

int launchpad_hit(int mx, int my) {
    int icons_per_page = LAUNCHPAD_COLS * LAUNCHPAD_ROWS;
    int total_w = LAUNCHPAD_COLS * LP_ICON_SZ + (LAUNCHPAD_COLS-1) * LP_ICON_PAD;
    int total_h = LAUNCHPAD_ROWS * (LP_ICON_SZ + 20) + (LAUNCHPAD_ROWS-1) * LP_ICON_PAD;
    int gx = (VGA_WIDTH - total_w) / 2;
    int gy = (VGA_HEIGHT - total_h) / 2 - 10;
    /* Left arrow area */
    if (mx < 40 && my > VGA_HEIGHT/2 - 20 && my < VGA_HEIGHT/2 + 20) {
        if (g_lp_page > 0) { g_lp_page--; return -2; }
    }
    /* Right arrow area */
    if (mx > VGA_WIDTH - 40 && my > VGA_HEIGHT/2 - 20 && my < VGA_HEIGHT/2 + 20) {
        int total_pages2 = (LP_ICON_COUNT + icons_per_page - 1) / icons_per_page;
        if (g_lp_page < total_pages2 - 1) { g_lp_page++; return -2; }
    }
    int r, c;
    for (r = 0; r < LAUNCHPAD_ROWS; r++) {
        for (c = 0; c < LAUNCHPAD_COLS; c++) {
            int slot = r * LAUNCHPAD_COLS + c;
            int fi2 = g_lp_page * icons_per_page + slot;
            if (fi2 >= LP_ICON_COUNT) break;
            int ix = gx + c * (LP_ICON_SZ + LP_ICON_PAD);
            int iy = gy + r * (LP_ICON_SZ + 20 + LP_ICON_PAD);
            if (mx >= ix && mx < ix+LP_ICON_SZ && my >= iy && my < iy+LP_ICON_SZ)
                return fi2;
        }
    }
    return -1;
}

/* =========================================================================
 * Mission Control state (window spread overview)
 * ======================================================================= */
int g_mc_visible      = 0;
int g_expose_visible  = 0;
int g_expose_app_idx  = -1;

/* =========================================================================
 * Notification Center panel
 * ======================================================================= */
int g_nc_visible = 0;

/* =========================================================================
 * Virtual Spaces (multiple desktops)
 * ======================================================================= */
int g_current_space = 1;
int g_num_spaces    = 2;

/* =========================================================================
 * System toggle state (for Settings panel + Control Center)
 * ======================================================================= */
int g_pref_wifi      = 1;
int g_pref_bt        = 1;
int g_pref_vpn       = 0;
/* g_pref_darkmode declared earlier (before toast_draw) */
int g_pref_notifs    = 1;
int g_pref_sound     __attribute__((unused)) = 1;
int g_pref_wallpaper = 0; /* 0=blue gradient, 1=sunset, 2=forest, 3=space */
int g_pref_accent = 0;
int g_pref_resolution = 1;
int g_pref_alert_sound = 0;

/* =========================================================================
 * Control Center (quick settings dropdown from top-right menu bar)
 * ======================================================================= */
int g_cc_visible = 0;
int g_stage_manager = 0; /* Stage Manager window arrangement mode */

/* Window tiling (macOS Sequoia style) */
int g_tile_zone   = 0; /* 0=none 1=left 2=right 3=full 4=top-L 5=top-R 6=bot-L 7=bot-R */
int g_tile_flash  = 0; /* countdown for tiling zone overlay animation (frames) */
int g_tile_saved_x = 100, g_tile_saved_y = 80;
int g_tile_saved_w = 360, g_tile_saved_h = 280;
int g_tile_saved_idx = -1; /* which window was tiled */

/* Photo Booth state */
int      g_pb_filter       = 0;   /* 0=Normal,1=Sepia,2=B&W,3=Vintage,4=Comic,5=X-Ray */
int      g_pb_captured     = 0;   /* number of captures taken */
uint32_t g_pb_flash_tick   = 0;   /* timer tick when last photo taken */
int      g_pb_photos[4]    = {-1,-1,-1,-1}; /* filter used for each thumbnail slot */

/* Contacts app state */
int g_ct_sel = 0; /* selected contact index (0-7) */

/* Messages app state */
int  g_ms_sel        = 0;          /* selected conversation 0-5 */
char g_ms_input[80]  = {0};        /* current input text */
int  g_ms_input_len  = 0;
int  g_ms_focused    = 0;          /* 1 = input box focused */
int  g_ms_search_focused = 0;
char g_ms_sent[MS_MAXSENT][72];   /* sent messages (all convs combined) */
int  g_ms_sent_conv[MS_MAXSENT];  /* which conv each sent msg belongs to */
int  g_ms_sent_n    = 0;
char g_ms_reply[MS_MAXSENT][72];  /* auto-replies */
int  g_ms_reply_conv[MS_MAXSENT];
uint32_t g_ms_reply_tick[MS_MAXSENT];
int  g_ms_reply_n   = 0;

/* Home app interactive device states (6 devices) */
int g_home_dev_on[6] = {1,1,1,1,0,0}; /* Living Light,TV,Thermostat,Speaker,Curtains,Door Lock */
int g_home_temp = 22; /* thermostat temperature */
int g_home_room = 0;  /* selected room: 0=Living,1=Bedroom,2=Kitchen,3=Office */
int g_home_brightness = 80; /* light brightness 0-100 */

/* Breakout game state */
int g_brk_active   = 0;
int g_brk_game_over= 0;
int g_brk_won      = 0;
int g_brk_paddle_x = 100; /* center of paddle */
int g_brk_ball_x   = 100, g_brk_ball_y = 180;
int g_brk_ball_vx  = 2,   g_brk_ball_vy = -3;
int g_brk_bricks[BRK_ROWS][BRK_COLS]; /* 1=alive */
int g_brk_score    = 0;
int g_brk_lives    = 3;
uint32_t g_brk_last_tick = 0;

/* 2048 game state */
int g_2048_board[4][4] = {{0}};
int g_2048_score  = 0;
int g_2048_best   = 0;
int g_2048_state  = 0; /* 0=idle, 1=playing, 2=won, 3=over */

/* Chess game state */
/* board[r][c]: 0=empty, 1-6=white(P,N,B,R,Q,K), 7-12=black(p,n,b,r,q,k) */
int g_chess_board[8][8] = {
    {10,8,9,11,12,9,8,10},  /* black back rank (r=0): r,n,b,q,k,b,n,r */
    { 7, 7, 7, 7, 7, 7, 7, 7}, /* black pawns */
    { 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0},
    { 0, 0, 0, 0, 0, 0, 0, 0},
    { 1, 1, 1, 1, 1, 1, 1, 1}, /* white pawns */
    { 4, 2, 3, 5, 6, 3, 2, 4}, /* white back rank: R,N,B,Q,K,B,N,R */
};
int g_chess_sel_r = -1, g_chess_sel_c = -1; /* selected square */
int g_chess_white_turn = 1; /* 1=white, 0=black */
int g_chess_moved = 0; /* for animation */

/* Pong game state */
int  g_pong_active  = 0;
int  g_pong_over    = 0;
int  g_pong_py      = 80;  /* player (right) paddle y center */
int  g_pong_ay      = 80;  /* AI (left) paddle y center */
int  g_pong_bx      = 150, g_pong_by = 80;
int  g_pong_vx      = 3,   g_pong_vy = 2;
int  g_pong_score_p = 0,   g_pong_score_a = 0;
uint32_t g_pong_last = 0;

/* Snake game state */
int g_snake_active = 0;     /* 1=playing, 0=not started/game over */
int g_snake_len    = 3;
int g_snake_x[SNAKE_MAX_LEN];
int g_snake_y[SNAKE_MAX_LEN];
int g_snake_dir    = 0;     /* 0=right 1=down 2=left 3=up */
int g_snake_next_dir = 0;
int g_snake_food_x = 10, g_snake_food_y = 7;
int g_snake_score  = 0;
uint32_t g_snake_last_tick = 0;
int g_snake_speed  = 200;   /* ms per step */
int g_snake_game_over = 0;

/* Wordle game state */
const char *g_wordle_words[] = {
    "CRANE","SLATE","AUDIO","PIANO","PRIZE","BRAVE","FLAME","GHOST","TIGER","BLAZE",
    "CRISP","DWARF","ELBOW","FANCY","GRASP","HONEY","JUICE","KNEEL","LODGE","MAGIC",
    "NERVE","OCEAN","PLUMB","QUEEN","RAPID","SCOUT","TRUST","ULTRA","VIVID","WINDY",
    "XEROX","YIELD","ZIPPY","APPLE","BAKER","CABLE","DAISY","EAGLE","FAIRY","GROAN",
    "HEART","IVORY","JOKER","KARMA","LEMON","MUSIC","NICER","OLIVE","PIXEL","QUIRK",
    "RIVER","SOLAR","TITAN","UMBRA","VAPOR","WALTZ","EXTRA","YOUTH","ZEBRA","ABBEY",
    "BRAND","CHOIR","DRINK","EIGHT","FLOSS","GLOOM","HAIKU","INDEX","JEWEL","KNACK",
    "LIGHT","MANOR","NIGHT","OASIS","PLAID","QUOTA","RELAY","SPORT","THEME","UNIFY",
    NULL
};
int  g_wordle_answer_idx = 0;
char g_wordle_guesses[WORDLE_ROWS][WORDLE_COLS+1]; /* each guess word */
int  g_wordle_results[WORDLE_ROWS][WORDLE_COLS];   /* 0=empty,1=gray,2=yellow,3=green */
int  g_wordle_cur_row = 0;
int  g_wordle_cur_col = 0;
int  g_wordle_state  = 0; /* 0=playing,1=won,2=lost */
int  g_wordle_kb_state[26];  /* 0=unused,1=gray,2=yellow,3=green */

/* Window minimized state (separate from visible to distinguish close vs minimize) */
int g_win_minimized[MAX_WINDOWS];

/* Window open animation frames (counts down from OPEN_ANIM to 0) */
int g_win_anim[MAX_WINDOWS];
/* Window minimize animation frames (OPEN_ANIM..0, then hide) */
int g_win_close_anim[MAX_WINDOWS];

/* =========================================================================
 * Screensaver (activates after SAVER_IDLE ms without input)
 * g_saver_mode: 0=Clock+Stars  1=Matrix  2=Warp
 * ======================================================================= */
uint32_t g_last_input_tick = 0;
int      g_saver_on   = 0;
int      g_saver_mode = 0;
int      g_saver_x  = 300;
int      g_saver_y  = 200;
int      g_saver_dx = 1;
int      g_saver_dy = 1;

/* Lock screen */
int  g_locked = 0;
char g_lock_pw[9]  = {0};   /* password input buffer */
int  g_lock_pw_len = 0;

/* Share Sheet overlay */
int g_share_visible = 0;
int g_share_action_count = 0;
const char *g_share_last_action = "Ready";

/* Print Dialog overlay */
int g_print_visible = 0;
int g_print_copies  = 1;
int g_print_page_from = 1;
int g_print_page_to   = 4;
int g_print_color     = 1;
int g_print_quality   = 1; /* 0=Draft 1=Normal 2=Best */
int g_print_jobs      = 0;

/* Per-space wallpaper (index 0-4 for each of spaces 1-4) */
int g_space_wallpaper[4] = {4, 1, 2, 3}; /* space1=Sequoia, 2=sunset, 3=forest, 4=space */

/* Mail compose state */
int  g_mail_compose       = 0;
int  g_mail_focused_field = 0;  /* 1=To, 2=Subject, 3=Body */
char g_mail_to[64]        = {0};
int  g_mail_to_len        = 0;
char g_mail_subject[64]   = {0};
int  g_mail_subject_len   = 0;
char g_mail_body[256]     = {0};
int  g_mail_body_len      = 0;
int  g_mail_sel_msg       = 0;
int  g_mail_folder        = 0;
int  g_mail_sent_count    = 0;
int  g_mail_search_focused = 0;
char g_mail_last_sent_subject[64] = {0};

/* Maps interactive state */
int g_maps_zoom  = 1;
int g_maps_pan_x = 0;
int g_maps_pan_y = 0;

/* FaceTime animated state */
int g_facetime_active  = 0;  /* 0=idle, 1=ringing, 2=connected */
int g_facetime_contact = 0;

/* Minesweeper game */
int      g_mine_board[MINE_ROWS][MINE_COLS];
int      g_mine_vis[MINE_ROWS][MINE_COLS];
int      g_mine_flag[MINE_ROWS][MINE_COLS];
int      g_mine_state       = 0;
uint32_t g_mine_start_tick  = 0;
uint32_t g_mine_end_tick    = 0;
int      g_mine_remaining   = MINE_COUNT;
uint32_t g_mine_rng         = 1;

/* Journal app */
int  g_journal_sel     = 0;
int  g_journal_edit    = 0;
int  g_journal_focused = 0;
int  g_journal_count   = 3;
char g_journal_titles[JOURNAL_MAX][JOURNAL_TLEN] = {
    "Productive Monday", "Weekend Hike", "Book Review", "", ""
};
char g_journal_bodies[JOURNAL_MAX][JOURNAL_BLEN] = {
    "Great day on the OS project!\nMade real progress on the GUI layer.",
    "Hiked the mountain trail today.\nThe views were absolutely breathtaking.",
    "Finished The Pragmatic Programmer.\nExcellent insights on software craft.",
    "", ""
};

/* ====== Contacts ====== */

/* ====== Preview ====== */
int g_preview_page = 0;
int g_preview_zoom = 100;
int g_preview_markup = 0;

/* ====== Apple TV ====== */
int g_atv_sel = 0;
int g_atv_bottom_tab = 0;
int g_atv_playing = 0;

/* ====== Wallet ====== */
int g_wallet_pay_count = 0;

/* ====== Misc app action state ====== */
int g_compressor_added_files = 0;
int g_compressor_submitted = 0;
int g_transporter_uploading = 0;
uint32_t g_transporter_upload_start_tick = 0;
int g_transporter_upload_count = 0;
int g_screen_recording_active = 0;
int g_screen_recording_count = 0;
int g_sidecar_connected = 0;
int g_universal_connected = 0;
int g_translate_favorites = 0;
int g_math_notes_created = 0;
int g_feedback_submissions = 0;
int g_cleanmymac_scan_count = 0;
int g_keynote_mode = 0;
int g_keynote_slide_count = 1;
int g_keynote_slide = 0;
int g_keynote_editing = 0;
int g_imovie_tab = 1;
int g_imovie_import_count = 0;
int g_imovie_share_count = 0;
int g_xcode_running = 0;
int g_xcode_run_count = 0;
int g_clock_extra_alarms = 0;
int g_motion_playing = 0;
int g_motion_tool = 0;
int g_motion_layer = 1;
int g_podcasts_playing = 0;
int g_podcasts_episode = 247;
int g_freeform_tool = 0;
int g_freeform_added_items = 0;
int g_netutil_tab = 0;
int g_netutil_ping_count = 0;
int g_gamecenter_tab = 0;
int g_fontbook_selected = 3;
int g_pages_bold = 1;
int g_pages_italic = 0;
int g_pages_underline = 0;
int g_pages_align = 0;
int g_pages_inspector = 0;
int g_pages_insert_count = 0;
int g_finalcut_transport = 1;
int g_finalcut_library = 0;
int g_shortcuts_tab = 0;
int g_shortcuts_run_count = 0;
int g_voice_memos_recording = 0;
uint32_t g_voice_memos_start_tick = 0;
int g_voice_memos_saved = 0;
int g_garageband_playing = 0;
int g_garageband_recording = 0;
int g_garageband_take_count = 0;
int g_automator_mode = 0;
int g_automator_recording = 0;
int g_automator_search_focused = 0;
int g_automator_category = 0;
int g_automator_step = 2;
int g_fontbook_search_focused = 0;
int g_console_filter_focused = 0;
int g_console_level_filter = 0;
int g_console_device_filter = 0;
int g_sfsymbols_search_focused = 0;
int g_sfsymbols_selected = 0;
int g_privacy_category = 0;
int g_accessibility_category = 0;
int g_configurator_category = 1;
int g_transmit_transfer_count = 0;
int g_overflow_selected = 0;
int g_airplay_device = 0;
int g_testflight_selected = 0;
int g_testflight_updated_mask = 0;
int g_testflight_invites = 0;
int g_clips_recording = 0;
int g_clips_saved_count = 0;
int g_mainstage_zone = 0;
int g_mainstage_performing = 0;
int g_instruments_recording = 1;
int g_instruments_track = 0;
int g_klokki_project = 0;
int g_klokki_running = 1;
int g_lasso_selected = 0;
int g_bartender_visible_mask = 20;
int g_scrobbles_selected = 0;
int g_logic_transport = 2;
int g_logic_track = 0;
int g_tot_dot = 1;
uint32_t g_dcm_sample_color = RGB(0,122,255);
int g_grapher_curve = 0;
int g_health_metric = 0;
int g_reality_object = 0;
int g_iphone_mirroring_connected = 1;
int g_iphone_mirroring_taps = 0;
int g_stocks_selected = 0;
int g_weather_selection = 0;
int g_keyboard_shortcut_selected = 0;
int g_system_info_selected = 0;
int g_stickies_selected = 0;
int g_fantastical_selected_day = 0;
int g_proxyman_session = 0;
int g_istudiez_tab = 0;
int g_onepassword_vault = 0;
int g_things_section = 1;
int g_raycast_result = 0;
int g_bear_note = 0;
int g_reeder_feed = 0;
int g_alfred_result = 0;
int g_onepassword_search_focused = 0;
int g_raycast_search_focused = 0;
int g_finder_search_focused = 0;
int g_photos_search_focused = 0;
int g_color_picker_tab = 0;
int g_maps_search_focused = 0;
int g_alfred_search_focused = 0;
int g_script_running = 0;
int g_script_run_count = 0;
int g_numbers_tool = 0;
int g_disk_utility_action = -1;
int g_about_update_checks = 0;
int g_contacts_added = 0;
int g_maps_route_started = 0;

/* ====== Stage Manager side strip ====== */

/* =========================================================================
 * draw_window_content - dispatches to app group drawing functions
 * ======================================================================= */
static void draw_window_content(int idx) {
    if (draw_apps_group1(idx)) return;
    if (draw_apps_group2(idx)) return;
    if (draw_apps_group3(idx)) return;
    draw_apps_group4(idx);
}

void draw_scene(int mx, int my) {
    int i;

    /* Auto-detect privacy indicators based on open windows */
    {
        int pi, cam=0, mic=0, scr=0;
        for (pi=0; pi<g_num_windows; pi++) {
            const char *t = g_windows[pi].title;
            if (!t || !g_windows[pi].visible) continue;
            if (str_eq(t,"FaceTime"))   { cam=1; mic=1; }
            if (str_eq(t,"Voice Memos")) mic=1;
            if (str_eq(t,"Photo Booth")) cam=1;
            if (str_eq(t,"Clips"))       cam=1;
        }
        if (g_screen_shared) scr=1;
        g_cam_in_use = cam;
        g_mic_in_use = mic;
        g_screen_shared = scr;
    }

    /* 1. Desktop gradient */
    gui_draw_desktop();

    /* Screensaver: darken the wallpaper we just drew, then show effect */
    if (g_saver_on) {
        uint32_t t_sv = timer_ticks();
        int saver_mode = g_saver_mode;
        if (saver_mode < 0 || saver_mode > 2) saver_mode = 0;
        vga_fill_rect(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0));

        if (saver_mode == 1) {
            /* Matrix rain */
            static uint8_t col_pos[80]  = {0};
            static uint8_t col_spd[80]  = {0};
            static uint8_t col_len[80]  = {0};
            static uint8_t col_init[80] = {0};
            static uint32_t last_mt = 0;
            int mi;
            if (!col_init[0]) {
                for (mi=0;mi<80;mi++) { col_pos[mi]=(uint8_t)(mi*7%75); col_spd[mi]=(uint8_t)(1+(mi*3%3)); col_len[mi]=(uint8_t)(8+mi%12); col_init[mi]=1; }
            }
            if (t_sv - last_mt > 80) {
                last_mt = t_sv;
                for (mi=0;mi<80;mi++) {
                    col_pos[mi] = (uint8_t)((col_pos[mi] + col_spd[mi]) % 75);
                }
            }
            for (mi=0;mi<80;mi++) {
                int cx6 = mi*10, cy6_head = col_pos[mi]*(VGA_HEIGHT/75);
                int li;
                for (li=0;li<(int)col_len[mi];li++) {
                    int py = cy6_head - li*8;
                    if (py < 0) py += VGA_HEIGHT;
                    if (py >= VGA_HEIGHT) continue;
                    unsigned char mc = (unsigned char)('A' + (mi+li+t_sv/200)%26);
                    uint32_t gc = (li==0) ? RGB(200,255,200) : RGB(0,(uint8_t)(180-li*10>60?180-li*10:60),0);
                    vga_draw_string_trans(cx6, py, (const char[]){(char)mc,0}, gc);
                }
            }
        } else if (saver_mode == 2) {
            /* Warp starfield */
            static struct { int16_t x,y; uint8_t z; } stars3[120];
            static int stars3_init = 0;
            if (!stars3_init) {
                int si3;
                for (si3=0;si3<120;si3++) {
                    stars3[si3].x=(int16_t)((si3*73)%800-400);
                    stars3[si3].y=(int16_t)((si3*137)%600-300);
                    stars3[si3].z=(uint8_t)(1+(si3%32));
                }
                stars3_init=1;
            }
            static uint32_t last_wt3 = 0;
            if (t_sv - last_wt3 > 40) {
                int si3;
                last_wt3 = t_sv;
                for (si3=0;si3<120;si3++) {
                    stars3[si3].z = (uint8_t)(stars3[si3].z + 1);
                    if (stars3[si3].z > 32) {
                        stars3[si3].x = (int16_t)((si3*73+t_sv)%800-400);
                        stars3[si3].y = (int16_t)((si3*137+t_sv)%600-300);
                        stars3[si3].z = 1;
                    }
                }
            }
            { int si3;
              for (si3=0;si3<120;si3++) {
                  int fz = stars3[si3].z;
                  int sx3 = VGA_WIDTH/2  + stars3[si3].x * 32 / (fz+1);
                  int sy3 = VGA_HEIGHT/2 + stars3[si3].y * 32 / (fz+1);
                  if (sx3<0||sx3>=VGA_WIDTH||sy3<0||sy3>=VGA_HEIGHT) continue;
                  uint8_t bri3=(uint8_t)(50+fz*6);
                  int sz3=(fz>20)?2:1;
                  vga_fill_rect(sx3,sy3,sz3,sz3,RGB(bri3,bri3,bri3));
              }
            }
        } else {
            /* Mode 0: Starfield + bouncing clock (original) */
            { static const uint16_t sx2[48]={
                  12,34,67,89,120,145,200,230,45,78,110,160,190,22,55,180,
                  300,350,400,250,175,220,310,420,500,550,600,650,700,750,50,100,
                  740,20,380,460,132,280,590,710,90,440,330,580,640,170,260,790};
              static const uint8_t sy2[48]={
                  8,25,42,60,15,38,22,50,70,35,55,12,45,68,30,58,
                  20,44,80,15,55,70,36,22,60,44,15,30,52,10,66,48,
                  18,90,32,75,48,6,88,64,38,24,58,14,72,46,100,26};
              int si5;
              for(si5=0;si5<48;si5++) {
                  int xs=(int)sx2[si5]%(VGA_WIDTH-4), ys=(int)sy2[si5]%(VGA_HEIGHT-4);
                  uint8_t bri=(uint8_t)(100+((t_sv/300+si5*11)%8)*18);
                  int sz2=((si5*7)%3)+1;
                  vga_fill_rect(xs, ys, sz2, sz2, RGB(bri,bri,(uint8_t)(bri+20)));
              }
            }
            /* Bouncing clock */
            { int cx5 = g_saver_x, cy5 = g_saver_y;
              char clk2[12]; get_clock_str(clk2);
              int cw=str_len(clk2);
              int ci, cx3=cx5-cw*12; int row2,col2;
              for(ci=0;clk2[ci];ci++) {
                  unsigned char ch2=(unsigned char)clk2[ci];
                  int bx=cx3+ci*24;
                  for(row2=0;row2<8;row2++) for(col2=0;col2<8;col2++) {
                      if(font8x8[ch2][row2]&(1u<<col2))
                          vga_fill_rect(bx+col2*3, cy5+row2*3, 3, 3, RGB(255,255,255));
                  }
              }
              { char dstr[32]; int dl;
                get_date_long_str(dstr);
                dl=str_len(dstr)*8;
                vga_draw_string_trans(cx5-dl/2, cy5+30, dstr, RGB(180,190,220));
              }
            }
        }

        /* Mode indicator (bottom-right) — tap Space while screensaver runs to cycle */
        { static const char *mode_names[3]={"Clock","Matrix","Warp"};
          vga_draw_string_trans(VGA_WIDTH-52, VGA_HEIGHT-12, mode_names[saver_mode], RGB(60,60,70));
        }
        /* Unlock hint — pulsing */
        { const char *hint="Click or key wakes | Space changes mode"; int hl=str_len(hint)*8;
          uint8_t ha=(uint8_t)(80+((int)(t_sv/400)%5)*20);
          vga_draw_string_trans((VGA_WIDTH-hl)/2, VGA_HEIGHT-30, hint, RGB(ha,ha,(uint8_t)(ha+30)));
        }
        /* MyOS brand */
        vga_draw_string_trans(VGA_WIDTH/2-20, 15, "MyOS", RGB(120,130,160));
        { int display_brightness = g_cc_brightness;
          if (display_brightness < 0) display_brightness = 0;
          if (display_brightness > 100) display_brightness = 100;
          if (display_brightness < 100) {
            uint8_t alpha_dim = (uint8_t)((100 - display_brightness) * 200 / 100);
            vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), alpha_dim);
          }
        }
        cursor_restore();
        cursor_save(mx, my);
        gui_draw_cursor(mx, my);
        return;
    }

    /* 2. Windows back to front (filtered by current space) */
    { int top_vis = win_top_visible();
      for (i = 0; i < g_num_windows; i++) {
        if (!g_windows[i].visible) continue;
        if (g_windows[i].space != 0 && g_windows[i].space != g_current_space) continue;
        gui_draw_window(i);
        draw_window_content(i);
        /* Subtle dim overlay for unfocused windows (content area only) */
        if (i != top_vis && !g_mc_visible && !g_expose_visible) {
            vga_fill_rect_alpha(g_windows[i].x, g_windows[i].y + TITLEBAR_H,
                                g_windows[i].w, g_windows[i].h - TITLEBAR_H,
                                RGB(0,0,0), 18);
        }
      }
    }

    /* 2.5 Window snap-zone preview (Sequoia tiling) */
    {
        int any_dragging = 0;
        for (i = 0; i < g_num_windows; i++) {
            if (g_windows[i].dragging) { any_dragging = 1; break; }
        }
        if (any_dragging) {
            uint32_t snap_col = RGB(0,122,255);
            int zone_h = DOCK_Y - MENUBAR_H;
            if (mx < 28) {
                /* Left half snap preview */
                vga_fill_rect_alpha(0, MENUBAR_H, VGA_WIDTH/2, zone_h, snap_col, 60);
                vga_draw_rect_outline(1, MENUBAR_H+1, VGA_WIDTH/2-2, zone_h-2, snap_col);
            } else if (mx > VGA_WIDTH - 28) {
                /* Right half snap preview */
                vga_fill_rect_alpha(VGA_WIDTH/2, MENUBAR_H, VGA_WIDTH/2, zone_h, snap_col, 60);
                vga_draw_rect_outline(VGA_WIDTH/2+1, MENUBAR_H+1, VGA_WIDTH/2-2, zone_h-2, snap_col);
            } else if (my < MENUBAR_H + 8) {
                /* Full-screen snap preview */
                vga_fill_rect_alpha(0, MENUBAR_H, VGA_WIDTH, zone_h, snap_col, 45);
                vga_draw_rect_outline(1, MENUBAR_H+1, VGA_WIDTH-2, zone_h-2, snap_col);
            }
        }
    }

    /* 3. Action buttons */
    for (i = 0; i < g_num_buttons; i++) {
        if (g_buttons[i].win_idx < -1) continue;
        if (g_buttons[i].win_idx >= 0 &&
            (g_buttons[i].win_idx >= g_num_windows ||
             !g_windows[g_buttons[i].win_idx].visible)) continue;
        gui_draw_button(&g_buttons[i]);
    }

    /* 4. Dock (above desktop, below menu bar) */
    gui_draw_dock();

    /* 5. Menu bar (always topmost) */
    gui_draw_menubar();

    /* 6. Cursor last */
    /* Context menu on top of everything except cursor */
    if (g_ctx_visible) ctx_menu_draw();
    /* Dock right-click popover */
    if (g_dock_ctx_visible) dock_ctx_draw();
    /* Menu bar dropdown */
    if (g_menu_open >= 0) draw_dropdown(g_menu_open);
    /* Spotlight */
    if (g_spot_visible) spotlight_draw();

    /* Stage Manager side strip */
    if (g_stage_manager) stage_manager_draw();

    /* Control Center panel */
    if (g_cc_visible) cc_draw();

    /* Notification Center side panel */
    if (g_nc_visible) nc_draw();

    /* Toast notifications */
    if (g_toast_visible) toast_draw();

    /* Mission Control overlay */
    if (g_mc_visible) mission_control_draw();

    /* App Exposé overlay */
    if (g_expose_visible) app_expose_draw();

    /* Launchpad overlay (topmost of all) */
    if (g_lp_visible) launchpad_draw();

    /* Widget bar overlay */
    if (g_widget_visible) widget_bar_draw();

    /* Quick Look overlay */
    if (g_ql_visible) quick_look_draw();
    if (g_scr_visible) screenshot_tool_draw();

    /* Writing Tools overlay */
    if (g_wt_visible) writing_tools_draw();

    /* Quick Note panel */
    if (g_qn_visible) quick_note_draw();

    /* Share Sheet overlay */
    if (g_share_visible) share_sheet_draw();

    /* Print Dialog overlay */
    if (g_print_visible) print_dialog_draw();

    /* AirDrop panel */
    if (g_airdrop_visible) airdrop_draw();

    /* Handoff notification */
    if (g_handoff_visible) handoff_draw();

    if (g_crash_visible) crash_reporter_draw();
    if (g_update_visible) system_update_draw();
    if (g_focus_filter_visible) focus_filter_draw();
    if (g_icloud_visible) icloud_panel_draw();
    if (g_bt_visible) bluetooth_dialog_draw();
    if (g_kbshort_visible) keyboard_shortcuts_draw();
    if (g_timemachine_visible) time_machine_draw();
    if (g_colormeter_visible) color_meter_draw();
    if (g_notifhist_visible) notif_history_draw();
    if (g_wifi_visible) wifi_panel_draw();
    if (g_display_visible) display_settings_draw();
    if (g_sound_visible) sound_settings_draw();
    if (g_actmon_visible) activity_monitor_draw();
    if (g_facetime_visible) facetime_draw();
    if (g_privacy_visible) privacy_panel_draw();
    if (g_reminders_visible) reminders_draw();
    if (g_calendar_visible) calendar_draw();
    if (g_airplay_visible) airplay_draw();

    /* Siri overlay */
    if (g_siri_visible) siri_draw();

    /* Photos fullscreen overlay */
    if (g_photos_fullscreen) {
        /* Gradient background based on photo type */
        static const uint32_t ph_tops[6]  = {RGB(100,160,200),RGB(15,15,30),RGB(255,140,50),RGB(20,60,20),RGB(80,20,20),RGB(30,20,60)};
        static const uint32_t ph_bots[6]  = {RGB(34,100,60), RGB(30,30,60),RGB(100,20,80),RGB(10,40,20),RGB(180,100,60),RGB(20,20,80)};
        int edit_panel_w = g_photos_edit_mode ? 140 : 0;
        int photo_w = VGA_WIDTH - edit_panel_w;
        int ph_y;
        for (ph_y=0;ph_y<VGA_HEIGHT;ph_y++) {
            int fi = g_photos_sel % 6;
            uint32_t t5 = ph_tops[fi], b5 = ph_bots[fi];
            uint8_t r5=(uint8_t)(((t5>>16)&0xFF)*(VGA_HEIGHT-ph_y)/VGA_HEIGHT + ((b5>>16)&0xFF)*ph_y/VGA_HEIGHT);
            uint8_t g5=(uint8_t)(((t5>>8)&0xFF)*(VGA_HEIGHT-ph_y)/VGA_HEIGHT  + ((b5>>8)&0xFF)*ph_y/VGA_HEIGHT);
            uint8_t b5c=(uint8_t)((t5&0xFF)*(VGA_HEIGHT-ph_y)/VGA_HEIGHT       + (b5&0xFF)*ph_y/VGA_HEIGHT);
            vga_draw_hline(0, ph_y, photo_w, RGB(r5,g5,b5c));
        }
        /* Top bar */
        vga_fill_rect_alpha(0, 0, photo_w, 28, RGB(0,0,0), 120);
        /* Photo counter */
        { char pc[8]; pc[0]='0'+g_photos_sel+1; pc[1]='/'; pc[2]='6'; pc[3]=0;
          vga_draw_string_trans(photo_w/2-12, 10, pc, RGB(255,255,255)); }
        /* Close button */
        vga_fill_rect(photo_w-28, 4, 20, 20, RGB(80,80,80));
        vga_draw_string_trans(photo_w-24, 8, "X", RGB(255,255,255));
        /* Edit button */
        { uint32_t ebc = g_photos_edit_mode ? RGB(0,122,255) : RGB(60,60,60);
          vga_fill_rect(photo_w-72, 4, 40, 20, ebc);
          vga_draw_string_trans(photo_w-68, 8, "Edit", RGB(255,255,255)); }
        /* Share button */
        vga_fill_rect(photo_w-116, 4, 40, 20, RGB(60,60,60));
        vga_draw_string_trans(photo_w-112, 8, "Share", RGB(255,255,255));
        /* Left/Right arrows */
        vga_fill_rect_alpha(4, VGA_HEIGHT/2-20, 24, 40, RGB(0,0,0), 80);
        vga_draw_string_trans(8, VGA_HEIGHT/2-4, "<", RGB(255,255,255));
        if (!g_photos_edit_mode) {
            vga_fill_rect_alpha(photo_w-28, VGA_HEIGHT/2-20, 24, 40, RGB(0,0,0), 80);
            vga_draw_string_trans(photo_w-22, VGA_HEIGHT/2-4, ">", RGB(255,255,255));
        }
        /* Photo label */
        static const char *ph_names[6] = {"Mountain","City Night","Sunset","Beach","Forest","Flowers"};
        const char *pname = ph_names[g_photos_sel%6];
        int pnl = str_len(pname)*8;
        vga_fill_rect_alpha((photo_w-pnl-16)/2, VGA_HEIGHT-32, pnl+16, 20, RGB(0,0,0), 120);
        vga_draw_string_trans((photo_w-pnl)/2, VGA_HEIGHT-26, pname, RGB(255,255,255));
        /* Edit panel (right sidebar) */
        if (g_photos_edit_mode) {
            int ep_x = photo_w;
            vga_fill_rect(ep_x, 0, edit_panel_w, VGA_HEIGHT, RGB(28,28,32));
            vga_draw_vline(ep_x, 0, VGA_HEIGHT, RGB(60,60,65));
            /* Edit tool tabs */
            static const char *et_names[] = {"Adjust","Crop","Filter"};
            int eti;
            for (eti=0;eti<3;eti++) {
                int etx = ep_x + eti*(edit_panel_w/3);
                uint32_t et_bg = (eti==g_photos_edit_tool) ? RGB(50,50,58) : RGB(28,28,32);
                vga_fill_rect(etx, 0, edit_panel_w/3, 28, et_bg);
                int etl = str_len(et_names[eti])*8;
                vga_draw_string_trans(etx+(edit_panel_w/3-etl)/2, 8, et_names[eti],
                    eti==g_photos_edit_tool ? RGB(255,255,255) : RGB(130,130,140));
                if (eti==g_photos_edit_tool) vga_fill_rect(etx, 26, edit_panel_w/3, 2, RGB(0,122,255));
            }
            vga_draw_hline(ep_x, 28, edit_panel_w, RGB(60,60,65));
            if (g_photos_edit_tool == 0) {
                /* Adjust: Brightness, Contrast, Saturation sliders */
                int sy_base = 40;
                static const char *adj_names[] = {"Brightness","Contrast","Saturation"};
                int *adj_vals[3]; adj_vals[0]=&g_photos_brightness; adj_vals[1]=&g_photos_contrast; adj_vals[2]=&g_photos_saturation;
                int adj_i;
                for (adj_i=0; adj_i<3; adj_i++) {
                    int sy2 = sy_base + adj_i*44;
                    vga_draw_string_trans(ep_x+8, sy2, adj_names[adj_i], RGB(180,180,190));
                    /* Slider track */
                    int sl_x = ep_x+8, sl_w = edit_panel_w-16;
                    vga_fill_rect(sl_x, sy2+14, sl_w, 4, RGB(60,60,68));
                    /* Slider fill */
                    int sl_fill = sl_w * (*adj_vals[adj_i]) / 100;
                    vga_fill_rect(sl_x, sy2+14, sl_fill, 4, RGB(0,122,255));
                    /* Slider thumb */
                    gui_draw_circle(sl_x+sl_fill, sy2+16, 6, RGB(255,255,255));
                    /* Value */
                    char vbuf[5]; int vv = *adj_vals[adj_i];
                    vbuf[0]='0'+vv/100; vbuf[1]='0'+(vv%100)/10; vbuf[2]='0'+vv%10; vbuf[3]=0;
                    vga_draw_string_trans(ep_x+edit_panel_w-26, sy2+12, vbuf, RGB(130,130,140));
                }
                /* Reset button */
                vga_fill_rect(ep_x+10, sy_base+140, edit_panel_w-20, 20, RGB(50,50,58));
                gui_draw_rounded_rect_outline(ep_x+10, sy_base+140, edit_panel_w-20, 20, 4, RGB(80,80,88));
                { int rl=str_len("Reset")*8;
                  vga_draw_string_trans(ep_x+(edit_panel_w-rl)/2, sy_base+144, "Reset", RGB(200,200,210)); }
            } else if (g_photos_edit_tool == 1) {
                /* Crop tool */
                vga_draw_string_trans(ep_x+8, 40, "Aspect Ratio", RGB(180,180,190));
                static const char *ratios[] = {"Free","1:1","4:3","16:9"};
                int ri2;
                for (ri2=0;ri2<4;ri2++) {
                    int ry2=56+ri2*24;
                    uint32_t rb = (ri2==0) ? RGB(0,122,255) : RGB(50,50,58);
                    vga_fill_rect(ep_x+8, ry2, edit_panel_w-16, 20, rb);
                    int rl2=str_len(ratios[ri2])*8;
                    vga_draw_string_trans(ep_x+(edit_panel_w-rl2)/2, ry2+6, ratios[ri2], RGB(255,255,255));
                }
            } else {
                /* Filter tool */
                vga_draw_string_trans(ep_x+8, 40, "Filters", RGB(180,180,190));
                static const char *filters[] = {"Original","Vivid","Warm","Cool","Drama","Fade"};
                static const uint32_t fcols[] = {
                    RGB(60,60,68),RGB(50,100,200),RGB(200,100,50),RGB(50,150,200),RGB(80,40,80),RGB(120,120,130)
                };
                int fi2;
                for (fi2=0;fi2<6;fi2++) {
                    int fy2=56+fi2*20;
                    uint32_t fb = (fi2==0)?RGB(0,122,255):fcols[fi2];
                    vga_fill_rect(ep_x+8, fy2, edit_panel_w-16, 18, fb);
                    vga_draw_string_trans(ep_x+14, fy2+5, filters[fi2], RGB(255,255,255));
                }
            }
            /* Done button */
            vga_fill_rect(ep_x+10, VGA_HEIGHT-36, edit_panel_w-20, 24, RGB(0,122,255));
            { int dl=str_len("Done")*8;
              vga_draw_string_trans(ep_x+(edit_panel_w-dl)/2, VGA_HEIGHT-28, "Done", RGB(255,255,255)); }
        }
    }

    /* Window tiling zone overlay (macOS Sequoia style) */
    if (g_tile_flash > 0) {
        g_tile_flash--;
        /* Compute zone rect */
        int tz_x=0, tz_y=MENUBAR_H, tz_w=VGA_WIDTH, tz_h=DOCK_Y-MENUBAR_H;
        if      (g_tile_zone==1) { tz_w=VGA_WIDTH/2; }
        else if (g_tile_zone==2) { tz_x=VGA_WIDTH/2; tz_w=VGA_WIDTH/2; }
        else if (g_tile_zone==4) { tz_w=VGA_WIDTH/2; tz_h=(DOCK_Y-MENUBAR_H)/2; }
        else if (g_tile_zone==5) { tz_x=VGA_WIDTH/2; tz_w=VGA_WIDTH/2; tz_h=(DOCK_Y-MENUBAR_H)/2; }
        else if (g_tile_zone==6) { tz_w=VGA_WIDTH/2; tz_y=MENUBAR_H+(DOCK_Y-MENUBAR_H)/2; tz_h=(DOCK_Y-MENUBAR_H)/2; }
        else if (g_tile_zone==7) { tz_x=VGA_WIDTH/2; tz_w=VGA_WIDTH/2; tz_y=MENUBAR_H+(DOCK_Y-MENUBAR_H)/2; tz_h=(DOCK_Y-MENUBAR_H)/2; }
        uint8_t fade = (uint8_t)(g_tile_flash * 140 / 12);
        vga_fill_rect_alpha(tz_x, tz_y, tz_w, tz_h, RGB(52,199,89), fade);
        gui_draw_rounded_rect_outline(tz_x+4, tz_y+4, tz_w-8, tz_h-8, 8, RGB(52,199,89));
    }

    /* App switcher overlay */
    if (g_switcher_visible) app_switcher_draw();

    /* Lock screen overlay (above everything) */
    if (g_locked) {
        lock_screen_draw();
        cursor_restore();
        cursor_save(mx, my);
        gui_draw_cursor(mx, my);
        return;
    }

    /* Window snap preview: show blue half-screen outline when dragging near edge */
    {
        int si2;
        for (si2=0; si2<g_num_windows; si2++) {
            gui_window_t *sw = &g_windows[si2];
            if (!sw->visible || !sw->dragging || sw->maximized) continue;
            if (sw->x <= 2 && sw->y <= MENUBAR_H + 8) {
                vga_fill_rect_alpha(0, MENUBAR_H, VGA_WIDTH/2, DOCK_Y-MENUBAR_H, RGB(0,122,255), 40);
                vga_draw_rect_outline(0, MENUBAR_H, VGA_WIDTH/2, DOCK_Y-MENUBAR_H, RGB(0,122,255));
            } else if (sw->x + sw->w >= VGA_WIDTH - 2 && sw->y <= MENUBAR_H + 8) {
                vga_fill_rect_alpha(VGA_WIDTH/2, MENUBAR_H, VGA_WIDTH/2, DOCK_Y-MENUBAR_H, RGB(0,122,255), 40);
                vga_draw_rect_outline(VGA_WIDTH/2, MENUBAR_H, VGA_WIDTH/2, DOCK_Y-MENUBAR_H, RGB(0,122,255));
            }
        }
    }

    /* Night Shift warm tint overlay */
    if (g_night_shift) {
        vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(255,150,40), 55);
    }

    /* Brightness dim overlay: 100%=no overlay, 0%=black screen */
    { int display_brightness = g_cc_brightness;
      if (display_brightness < 0) display_brightness = 0;
      if (display_brightness > 100) display_brightness = 100;
      if (display_brightness < 100) {
        uint8_t alpha_dim = (uint8_t)((100 - display_brightness) * 200 / 100);
        vga_fill_rect_alpha(0, 0, VGA_WIDTH, VGA_HEIGHT, RGB(0,0,0), alpha_dim);
      }
    }

    cursor_restore();
    cursor_save(mx, my);
    gui_draw_cursor(mx, my);
}

/* =========================================================================
 * Button callbacks
 * ======================================================================= */
/* =========================================================================
 * gui_init
 * ======================================================================= */
void gui_init(void) {

    /* Main Finder window - center of screen */
    {
        gui_window_t *mw = &g_windows[0];
        mw->x = 190; mw->y = 70; mw->w = 390; mw->h = 350;
        mw->title    = "MyOS Finder";
        mw->visible  = 1;
        mw->focused  = 1;
        mw->dragging = 0;
        g_num_windows = 1;
    }

    g_num_buttons = 0;
    str_cpy(g_status, "Ready");

    /* Terminal window - right side, no overlap with Finder */
    {
        gui_window_t *tw = &g_windows[1];
        tw->x = 596; tw->y = 70; tw->w = 188; tw->h = 230;
        tw->title    = "Terminal";
        tw->visible  = 1;
        tw->focused  = 0;
        tw->dragging = 0;
        g_num_windows = 2;
    }
    /* Clock window - top left */
    {
        gui_window_t *cw = &g_windows[2];
        cw->x = 14; cw->y = 70; cw->w = 165; cw->h = 210;
        cw->title    = "Clock";
        cw->visible  = 1;
        cw->focused  = 0;
        cw->dragging = 0;
        cw->maximized = 0;
        g_num_windows = 3;
    }

    /* Notes window - bottom left */
    {
        gui_window_t *nw = &g_windows[3];
        nw->x = 14; nw->y = 296; nw->w = 165; nw->h = 220;
        nw->title    = "Notes";
        nw->visible  = 1;
        nw->focused  = 0;
        nw->dragging = 0;
        nw->maximized = 0;
        g_num_windows = 4;
    }

    /* Initialize TextEdit buffer length */
    { int k = 0; while (g_edit_text[k]) k++; g_edit_len = k; }
    term_println("MyOS Terminal");
    term_println("Type 'help' for commands");
    term_println("---");
    g_last_input_tick = timer_ticks();

    /* Pre-populate NC with realistic notifications */
    nc_add("MyOS",    "Welcome! Fully bare-metal.",      RGB(0,122,255));
    nc_add("Mail",    "3 new messages from team",        RGB(0,122,255));
    nc_add("Calendar","Stand-up meeting",                RGB(255,59,48));
    nc_add("Notes",   "Shopping list updated",           RGB(255,204,0));
    nc_add("Music",   "Now playing: Midnight Drive",     RGB(252,60,68));
    nc_add("App Store","Updates available (3 apps)",     RGB(0,122,255));
    /* Welcome toast */
    toast_show("MyOS", "Welcome! Fully bare-metal.", RGB(0,122,255));
}

/* Bring window at idx to top (last in array = rendered last = visually on top).
   Updates g_buttons win_idx references accordingly. */
void win_bring_to_front(int idx) {
    int j;
    gui_window_t tmp;
    int minimized_tmp;
    int anim_tmp, close_anim_tmp;
    if (idx < 0 || idx >= g_num_windows) return;
    if (g_windows[idx].visible) g_win_minimized[idx] = 0;
    if (idx >= g_num_windows - 1) return;
    tmp = g_windows[idx];
    minimized_tmp = g_win_minimized[idx];
    anim_tmp = g_win_anim[idx];
    close_anim_tmp = g_win_close_anim[idx];
    for (j = idx; j < g_num_windows - 1; j++)
        g_windows[j] = g_windows[j + 1];
    for (j = idx; j < g_num_windows - 1; j++) {
        g_win_minimized[j] = g_win_minimized[j + 1];
        g_win_anim[j] = g_win_anim[j + 1];
        g_win_close_anim[j] = g_win_close_anim[j + 1];
    }
    g_windows[g_num_windows - 1] = tmp;
    g_win_minimized[g_num_windows - 1] = minimized_tmp;
    g_win_anim[g_num_windows - 1] = anim_tmp;
    g_win_close_anim[g_num_windows - 1] = close_anim_tmp;
    /* Fix button window index references */
    for (j = 0; j < g_num_buttons; j++) {
        if (g_buttons[j].win_idx == idx)
            g_buttons[j].win_idx = g_num_windows - 1;
        else if (g_buttons[j].win_idx > idx)
            g_buttons[j].win_idx--;
    }
}

void win_send_to_back(int idx) {
    int j;
    gui_window_t tmp;
    int minimized_tmp;
    int anim_tmp, close_anim_tmp;
    if (idx <= 0 || idx >= g_num_windows) return;
    tmp = g_windows[idx];
    minimized_tmp = g_win_minimized[idx];
    anim_tmp = g_win_anim[idx];
    close_anim_tmp = g_win_close_anim[idx];
    for (j = idx; j > 0; j--) {
        g_windows[j] = g_windows[j - 1];
        g_win_minimized[j] = g_win_minimized[j - 1];
        g_win_anim[j] = g_win_anim[j - 1];
        g_win_close_anim[j] = g_win_close_anim[j - 1];
    }
    g_windows[0] = tmp;
    g_win_minimized[0] = minimized_tmp;
    g_win_anim[0] = anim_tmp;
    g_win_close_anim[0] = close_anim_tmp;
    for (j = 0; j < g_num_buttons; j++) {
        if (g_buttons[j].win_idx == idx)
            g_buttons[j].win_idx = 0;
        else if (g_buttons[j].win_idx >= 0 && g_buttons[j].win_idx < idx)
            g_buttons[j].win_idx++;
    }
}

static void win_clear_focus_for_title(const char *title) {
    if (!title) return;
    if (str_eq(title, "Wordle")) g_wordle_focused = 0;
    if (str_eq(title, "Dictionary")) g_dict_focused = 0;
    if (str_eq(title, "TextEdit")) {
        g_dict_focused = 0;
        g_edit_focused = 0;
    }
    if (str_eq(title, "Messages")) g_ms_focused = 0;
}

int win_top_visible(void) {
    int i;
    for (i = g_num_windows - 1; i >= 0; i--) {
        if (g_windows[i].visible &&
            (g_windows[i].space == 0 || g_windows[i].space == g_current_space))
            return i;
    }
    return -1;
}

void win_minimize(int idx) {
    if (idx < 0 || idx >= g_num_windows) return;
    win_clear_focus_for_title(g_windows[idx].title);
    g_windows[idx].dragging = 0;
    g_windows[idx].resizing = 0;
    /* Start scale-down animation; actual hide happens when animation ends */
    g_win_close_anim[idx] = OPEN_ANIM;
    win_send_to_back(idx);
}

void win_close(int idx) {
    int j;
    if (idx < 0 || idx >= g_num_windows) return;
    win_clear_focus_for_title(g_windows[idx].title);
    for (j = 0; j < g_num_buttons; j++) {
        if (g_buttons[j].win_idx == idx) {
            g_buttons[j].win_idx = -2;
            g_buttons[j].pressed = 0;
            g_buttons[j].hover = 0;
        } else if (g_buttons[j].win_idx > idx) {
            g_buttons[j].win_idx--;
        }
    }
    for (j = idx; j < g_num_windows - 1; j++) {
        g_windows[j] = g_windows[j + 1];
        g_win_minimized[j] = g_win_minimized[j + 1];
        g_win_anim[j] = g_win_anim[j + 1];
        g_win_close_anim[j] = g_win_close_anim[j + 1];
    }
    g_num_windows--;
    if (g_num_windows >= 0 && g_num_windows < MAX_WINDOWS) {
        g_windows[g_num_windows].title = 0;
        g_windows[g_num_windows].focused = 0;
        g_windows[g_num_windows].visible = 0;
        g_windows[g_num_windows].dragging = 0;
        g_windows[g_num_windows].resizing = 0;
        g_windows[g_num_windows].maximized = 0;
        g_windows[g_num_windows].orig_x = 0;
        g_windows[g_num_windows].orig_y = 0;
        g_windows[g_num_windows].orig_w = 0;
        g_windows[g_num_windows].orig_h = 0;
        g_windows[g_num_windows].min_w = 0;
        g_windows[g_num_windows].min_h = 0;
        g_windows[g_num_windows].space = 0;
        g_win_minimized[g_num_windows] = 0;
        g_win_anim[g_num_windows] = 0;
        g_win_close_anim[g_num_windows] = 0;
    }
    if (g_switcher_sel >= g_num_windows)
        g_switcher_sel = g_num_windows > 0 ? g_num_windows - 1 : 0;
}


static int wt_is_space(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r';
}

static int wt_is_alpha(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static char wt_upper(char ch) {
    return (ch >= 'a' && ch <= 'z') ? (char)(ch - ('a' - 'A')) : ch;
}

static char wt_lower(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + ('a' - 'A')) : ch;
}

static void wt_copy(char *dst, int max, const char *src) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void wt_append_char(char *dst, int *pos, int max, char ch) {
    if (!dst || !pos || max <= 0 || *pos + 1 >= max) return;
    dst[*pos] = ch;
    (*pos)++;
    dst[*pos] = 0;
}

static void wt_append_text(char *dst, int *pos, int max, const char *src) {
    int i = 0;
    if (!dst || !pos || max <= 0 || !src) return;
    while (src[i] && *pos + 1 < max) {
        dst[*pos] = src[i];
        (*pos)++;
        i++;
    }
    dst[*pos] = 0;
}

static int wt_word_match(const char *src, int at, const char *word) {
    int i = 0;
    if (at > 0 && wt_is_alpha(src[at - 1])) return 0;
    while (word[i]) {
        if (!src[at + i] || wt_lower(src[at + i]) != word[i]) return 0;
        i++;
    }
    return !wt_is_alpha(src[at + i]);
}

static int wt_skip_filler(const char *src, int at) {
    static const char *fillers[] = { "very", "really", "just", "basically", "actually", 0 };
    int i;
    for (i = 0; fillers[i]; i++) {
        if (wt_word_match(src, at, fillers[i])) {
            int n = str_len(fillers[i]);
            while (src[at + n] == ' ') n++;
            return n;
        }
    }
    return 0;
}

static void wt_transform_text(int tool, const char *src, char *out, int max) {
    int i = 0;
    int pos = 0;
    int cap_next = 1;
    int prev_space = 1;
    if (!out || max <= 0) return;
    out[0] = 0;
    if (!src) return;
    if (tool == 5) {
        wt_append_text(out, &pos, max, "Summary: ");
        while (src[i] && pos + 1 < max && pos < 88) {
            char ch = src[i++];
            if (ch == '\n' || ch == '\t') ch = ' ';
            wt_append_char(out, &pos, max, ch);
            if (ch == '.' || ch == '!' || ch == '?') break;
        }
        return;
    }
    while (src[i] && pos + 1 < max) {
        char ch = src[i];
        int skip;
        if (tool == 4 && (skip = wt_skip_filler(src, i)) > 0) {
            i += skip;
            continue;
        }
        i++;
        if (wt_is_space(ch)) {
            if (!prev_space) {
                wt_append_char(out, &pos, max, ' ');
                prev_space = 1;
            }
            continue;
        }
        if (tool == 3 && ch == '!') ch = '.';
        if (cap_next && wt_is_alpha(ch)) {
            ch = wt_upper(ch);
            cap_next = 0;
        }
        wt_append_char(out, &pos, max, ch);
        prev_space = 0;
        if (ch == '.' || ch == '?' || ch == '!') cap_next = 1;
    }
    while (pos > 0 && out[pos - 1] == ' ') out[--pos] = 0;
    if (tool == 2 && pos + 9 < max) wt_append_text(out, &pos, max, " Thanks!");
}

static void wt_set_result(const char *text) {
    wt_copy(g_wt_result, sizeof(g_wt_result), text);
}

static int wt_replace_textedit_range(int start, int end, const char *replacement) {
    int repl_len = str_len(replacement);
    int old_len;
    int new_len;
    int i;
    if (g_edit_len == 0) {
        while (g_edit_len < TEXTEDIT_MAXCHARS - 1 && g_edit_text[g_edit_len]) g_edit_len++;
    }
    if (start < 0) start = 0;
    if (end < start) end = start;
    if (end > g_edit_len) end = g_edit_len;
    old_len = end - start;
    new_len = g_edit_len - old_len + repl_len;
    if (new_len >= TEXTEDIT_MAXCHARS) {
        repl_len -= new_len - (TEXTEDIT_MAXCHARS - 1);
        if (repl_len < 0) repl_len = 0;
        new_len = g_edit_len - old_len + repl_len;
    }
    for (i = g_edit_len; i >= end; i--)
        g_edit_text[start + repl_len + i - end] = g_edit_text[i];
    for (i = 0; i < repl_len; i++)
        g_edit_text[start + i] = replacement[i];
    g_edit_len = new_len;
    g_edit_text[g_edit_len] = 0;
    g_edit_sel_start = start;
    g_edit_sel_end = start + repl_len;
    g_edit_focused = 1;
    return repl_len;
}

int writing_tools_apply(int tool) {
    int top = win_top_visible();
    int start = 0;
    int end;
    int src_pos = 0;
    int i;
    char src[512];
    char out[512];
    if (tool < 0 || tool > 5) return 0;
    g_wt_sel = tool;
    g_wt_done = 2;
    g_wt_tick = timer_ticks();
    if (top < 0 || !g_windows[top].title || !str_eq(g_windows[top].title, "TextEdit")) {
        wt_set_result("Open TextEdit, select text, then choose a tool.");
        return 0;
    }
    if (g_edit_len == 0) {
        while (g_edit_len < TEXTEDIT_MAXCHARS - 1 && g_edit_text[g_edit_len]) g_edit_len++;
    }
    if (g_edit_len <= 0) {
        wt_set_result("TextEdit has no text to transform.");
        return 0;
    }
    end = g_edit_len;
    if (g_edit_sel_end != g_edit_sel_start) {
        start = g_edit_sel_start;
        end = g_edit_sel_end;
        if (start > end) {
            int tmp = start;
            start = end;
            end = tmp;
        }
        if (start < 0) start = 0;
        if (end > g_edit_len) end = g_edit_len;
    }
    for (i = start; i < end && src_pos + 1 < (int)sizeof(src); i++)
        src[src_pos++] = g_edit_text[i];
    src[src_pos] = 0;
    if (src_pos <= 0) {
        wt_set_result("No text selected.");
        return 0;
    }
    wt_transform_text(tool, src, out, sizeof(out));
    (void)wt_replace_textedit_range(start, end, out);
    wt_set_result((g_edit_sel_end - g_edit_sel_start) > 0 ? "Applied to TextEdit selection." : "Applied to TextEdit document.");
    return 1;
}


/* =========================================================================
 * gui_run  -  event loop (~60fps frame-limited, with close-button support)
 * ======================================================================= */
