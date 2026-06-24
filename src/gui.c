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
#include "font.h"
#include <stdint.h>
#include <stddef.h>
#include "gui_internal.h"

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

/* Widget bar (F7 toggle) */
int g_widget_visible = 0;

/* Quick Look (Space) */
int g_ql_visible = 0;
const char *g_ql_filename = "document.txt";
int g_scr_visible = 0;
int g_scr_mode    = 0;

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
int g_airdrop_progress = 0;  /* 0-100 */

/* Handoff state */
int g_handoff_visible = 0;
int g_handoff_tick    = 0;

/* Universal Control */
int g_uc_active = 0;

/* Writing Tools overlay (macOS Sequoia) */
int      g_wt_visible = 0;
int      g_wt_sel     = -1;  /* -1=none, 0-5=tool selected */
int      g_wt_done    = 0;   /* 0=choosing, 1=processing, 2=done */
uint32_t g_wt_tick    = 0;

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
char g_safari_url[64] = "about:home";
/* Multi-tab state */
int  g_safari_tab_count  = 1;
int  g_safari_active_tab = 0;
char g_safari_tab_urls[SAFARI_MAX_TABS][64] = {
    "about:home", "", "", ""
};
char g_safari_tab_titles[SAFARI_MAX_TABS][24] = {
    "MyOS Home", "", "", ""
};

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
int g_photos_fullscreen = 0;
int g_photos_edit_mode  = 0; /* 0=view, 1=edit tools visible */
int g_photos_brightness = 50; /* 0-100 */
int g_photos_contrast   = 50;
int g_photos_saturation = 50;
int g_photos_edit_tool  = 0; /* 0=Adjust, 1=Crop, 2=Filter */

/* =========================================================================
 * App Store download state: bitmask of downloading apps (bits 0-3)
 * ======================================================================= */
int      g_appstore_downloading = 0; /* bitmask */
uint32_t g_appstore_dl_tick[4]  = {0,0,0,0};

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

/* Music: show equalizer (always on) */
int g_music_eq_visible = 1;

/* Dark mode toggle (declared here for use in toast_draw before full decl) */
int g_pref_darkmode  = 0;

/* Screen Time state */
int g_screen_time_tab = 0; /* 0=App Usage, 1=Downtime */

/* Passwords state */
int g_passwords_sel = 0; /* selected sidebar category */
int g_passwords_entry = 0; /* selected password entry */

/* Numbers state */
int g_numbers_sel_row = 0;
int g_numbers_sel_col = 0;

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
    { "Music",            RGB(252, 60, 68), 'M' },
    { "Reminders",        RGB(255, 59, 48), 'R' },
    { "Messages",         RGB(52, 199, 89), 'M' },
    { "Find My",          RGB(0,  122,255), 'F' },
    { "Wallet",           RGB(0,   80,200), 'W' },
    { "Voice Memos",      RGB(255, 59, 48), 'V' },
    { "Shortcuts",        RGB(255,149,  0), 'S' },
    { "Freeform",         RGB(0,  122,255), 'F' },
    { "Disk Utility",     RGB(100,100,200), 'D' },
    { "TextEdit",         RGB(255,140, 40), 'E' },
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
    { "Keynote Remote",   RGB(255,149,  0), 'K' },
    { "Numbers Remote",   RGB(52, 199, 89), 'N' },
    { "Pages Remote",     RGB(255,149,  0), 'P' },
    { "iStudiez Pro",     RGB(0,  122,255), 'i' },
    { "Lasso",            RGB(100, 80,200), 'L' },
    { "Transmit",         RGB(255,149,  0), 'T' },
    { "Proxyman",         RGB(52, 199, 89), 'X' },
    { "Retcon",           RGB(200, 40, 40), 'R' },
    { "Overflow 3",       RGB(40, 120,200), 'O' },
    { "1Password",        RGB(0,  120,200), '1' },
    { "Fantastical",      RGB(220, 40, 40), 'F' },
    { "Things 3",         RGB(100, 60,200), 'T' },
    { "Raycast",          RGB(255, 90, 30), 'R' },
    { "Lungo",            RGB(200, 80, 20), 'L' },
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
            vga_draw_string_trans(ix2+(LP_ICON_SZ-8)/2, iy2+(LP_ICON_SZ-8)/2, &s_lp_icons[fi3].letter, RGB(255,255,255));
            vga_draw_string_trans(ix2+(LP_ICON_SZ-8)/2+1, iy2+(LP_ICON_SZ-8)/2, &s_lp_icons[fi3].letter, RGB(255,255,255));
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
            vga_draw_string_trans(ix+(LP_ICON_SZ-8)/2,   iy+(LP_ICON_SZ-8)/2, &s_lp_icons[fi].letter, RGB(255,255,255));
            vga_draw_string_trans(ix+(LP_ICON_SZ-8)/2+1, iy+(LP_ICON_SZ-8)/2, &s_lp_icons[fi].letter, RGB(255,255,255));
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
/* g_pref_darkmode declared earlier (before toast_draw) */
int g_pref_notifs    = 1;
int g_pref_sound     __attribute__((unused)) = 1;
int g_pref_wallpaper = 0; /* 0=blue gradient, 1=sunset, 2=forest, 3=space */

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

/* Print Dialog overlay */
int g_print_visible = 0;
int g_print_copies  = 1;
int g_print_page_from = 1;
int g_print_page_to   = 4;
int g_print_color     = 1;
int g_print_quality   = 1; /* 0=Draft 1=Normal 2=Best */

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
int g_contacts_sel = 0;

/* ====== Preview ====== */
int g_preview_page = 0;
int g_preview_zoom = 100;  /* percent */

/* ====== Apple TV ====== */
int g_atv_sel = 0;

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

/* =========================================================================
 * gui_run  -  event loop (~60fps frame-limited, with close-button support)
 * ======================================================================= */
