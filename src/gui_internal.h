#ifndef GUI_INTERNAL_H
#define GUI_INTERNAL_H

#include "gui.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "datetime.h"
#include "runtime_info.h"
#include "font.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Layout constants
 * ======================================================================= */
#define MENUBAR_Y   0
#define MENUBAR_H   22
#define DESKTOP_Y   0
#define DESKTOP_H   576
#define DOCK_Y      576
#define DOCK_H      24
#define TITLEBAR_H  28

/* =========================================================================
 * Toast constants
 * ======================================================================= */
#define TOAST_W      240
#define TOAST_H       74
#define TOAST_LIFE  5000
#define TOAST_TYPE_DEFAULT  0
#define TOAST_TYPE_REPLY    1
#define TOAST_TYPE_SNOOZE   2

/* =========================================================================
 * Notification Center constants
 * ======================================================================= */
#define NC_W         220
#define NC_MAX_ENTRIES 8

/* =========================================================================
 * Control Center constants
 * ======================================================================= */
#define CC_W   240
#define CC_H   400

/* =========================================================================
 * Spotlight constants
 * ======================================================================= */
#define SPOTLIGHT_W    400
#define SPOTLIGHT_H    40
#define SPOT_QUERY_MAX 32

/* =========================================================================
 * Launchpad constants
 * ======================================================================= */
#define LAUNCHPAD_COLS   5
#define LAUNCHPAD_ROWS   7
#define LP_ICON_SZ      44
#define LP_ICON_PAD      6
#define LP_ICON_COUNT   105

/* =========================================================================
 * Terminal constants
 * ======================================================================= */
#define TERM_LINES   14
#define TERM_HIST    80
#define TERM_MAX_COL 32
#define INPUT_MAX    25

/* =========================================================================
 * TextEdit constants
 * ======================================================================= */
#define TEXTEDIT_MAXCHARS 1024
#define TEXTEDIT_LINE_W   36

/* =========================================================================
 * Notes constants
 * ======================================================================= */
#define NOTES_COUNT  5
#define NOTES_MAXLEN 256

/* =========================================================================
 * Safari constants
 * ======================================================================= */
#define SAFARI_MAX_TABS 4
#define SAFARI_URL_MAX 192
#define SAFARI_TITLE_MAX 48
#define SAFARI_PAGE_TEXT_MAX 8192
#define SAFARI_RESPONSE_MAX 16384
#define SAFARI_STATUS_MAX 128
#define SAFARI_HISTORY_MAX 12
#define SAFARI_REDIRECT_MAX 4
#define SAFARI_MAX_LINKS 32
#define SAFARI_LINK_TITLE_MAX 40
#define SAFARI_MAX_FORMS 16
#define SAFARI_FORM_NAME_MAX 32
#define SAFARI_FORM_VALUE_MAX 64
#define SAFARI_FORM_QUERY_MAX 160
#define SAFARI_COOKIE_MAX 16
#define SAFARI_COOKIE_NAME_MAX 32
#define SAFARI_COOKIE_VALUE_MAX 96

/* =========================================================================
 * Calendar constants
 * ======================================================================= */
#define CAL_MAX_EVTS 20

/* =========================================================================
 * Breakout constants
 * ======================================================================= */
#define BRK_COLS   10
#define BRK_ROWS    5

/* =========================================================================
 * Snake constants
 * ======================================================================= */
#define SNAKE_GRID_W  20
#define SNAKE_GRID_H  15
#define SNAKE_MAX_LEN 200

/* =========================================================================
 * Wordle constants
 * ======================================================================= */
#define WORDLE_ROWS  6
#define WORDLE_COLS  5

/* =========================================================================
 * Screensaver / lock constants
 * ======================================================================= */
#define SAVER_IDLE   300000
#define OPEN_ANIM    6

/* =========================================================================
 * Messages constants
 * ======================================================================= */
#define MS_MAXSENT 12

/* =========================================================================
 * Sudoku constants
 * ======================================================================= */
#define SDK_SZ 9

/* =========================================================================
 * Struct types (forward declared here; defined via gui.h)
 * ======================================================================= */
typedef struct { const char *name; uint32_t color; char letter; } lp_icon_t;
typedef struct { const char *word; const char *phonetic; const char *pos; const char *def1; const char *def2; } dict_entry_t;
typedef struct { const char *name; const char *url; uint32_t color; char letter; } safari_home_site_t;

/* =========================================================================
 * Extern declarations for all shared global variables
 * ======================================================================= */

/* Windows / buttons */
extern gui_window_t  g_windows[MAX_WINDOWS];
extern int           g_num_windows;
extern gui_button_t  g_buttons[MAX_BUTTONS];
extern int           g_num_buttons;
extern char          g_status[80];
extern int           g_dock_badges[17];
extern int           g_dock_bounce[17];  /* bounce frames remaining per icon */

/* App switcher */
extern int g_switcher_visible;
extern int g_switcher_sel;

/* Music */
extern int g_music_playing;
extern int g_music_track;
extern int g_music_vol;
extern int g_music_tab;

/* Widget / QuickLook */
extern int g_widget_visible;
extern int g_ql_visible;
extern const char *g_ql_filename;
/* Screenshot tool */
extern int g_scr_visible;
extern int g_scr_mode;

extern int g_crash_visible;
extern int g_update_visible;
extern int g_focus_filter_visible;
extern int g_focus_filter_mode;
extern int g_icloud_visible;
extern int g_bt_visible;
extern int g_kbshort_visible;
extern int g_kbshort_page;
extern int g_timemachine_visible;
extern int g_tm_snapshot_count;
extern int g_tm_selected_snapshot;
extern uint32_t g_tm_last_snapshot_tick;
extern int g_tm_restored;
extern int g_tm_cancelled;
extern int g_colormeter_visible;
extern int g_notifhist_visible;
extern int g_notifhist_clear_count;

extern int g_wifi_visible;
extern int g_wifi_connecting;
extern int g_display_visible;
extern int g_display_brightness;
extern int g_sound_visible;
extern int g_sound_volume;
extern int g_sound_output_device;
extern int g_sound_input_device;
extern int g_actmon_visible;
extern int g_facetime_visible;
extern int g_facetime_calling;

extern int g_privacy_visible;
extern int g_privacy_tab;
extern int g_reminders_visible;
extern int g_reminders_list;
extern int g_calendar_visible;
extern int g_calendar_month;
extern int g_calendar_year;
extern int g_airplay_visible;
extern int g_airplay_scan_count;
extern uint32_t g_airplay_last_scan_tick;
extern int g_airplay_selected;

/* Siri */
extern int      g_siri_visible;
extern uint32_t g_siri_birth;
extern char     g_siri_query[48];
extern int      g_siri_qlen;
extern int      g_siri_response;
extern uint32_t g_siri_resp_tick;

/* Control Center */
extern int g_cc_brightness;
extern int g_cc_volume;
extern int g_pref_dnd;
extern int g_focus_mode;
extern int g_night_shift;

/* Privacy indicators */
extern int g_mic_in_use;
extern int g_cam_in_use;
extern int g_screen_shared;

/* AirDrop */
extern int g_airdrop_visible;
extern int g_airdrop_sending;
extern int g_airdrop_progress;
extern uint32_t g_airdrop_start_tick;
extern int g_airdrop_mode;
extern int g_photos_edit_tool;

/* Handoff */
extern int g_handoff_visible;
extern int g_handoff_tick;
extern int g_handoff_selected;

/* Writing Tools */
extern int      g_wt_visible;
extern int      g_wt_sel;
extern int      g_wt_done;
extern uint32_t g_wt_tick;
extern char     g_wt_result[128];

/* Quick Note */
extern int  g_qn_visible;
extern char g_qn_text[128];
extern int  g_qn_len;

/* Clock */
extern int g_clock_tab;

/* Calendar */
extern int  g_cal_offset;
extern int  g_cal_sel_day;
extern int  g_cal_popup;
extern char g_cal_evt_input[64];
extern int  g_cal_evt_input_len;
extern int  g_cal_evt_day[CAL_MAX_EVTS];
extern char g_cal_evt_txt[CAL_MAX_EVTS][40];
extern int  g_cal_evt_n;
extern uint32_t g_stopwatch_start;
extern uint32_t g_stopwatch_elapsed;
extern int      g_stopwatch_running;
extern int g_timer_set;
extern uint32_t g_timer_start_tick;
extern int g_timer_running;

/* Terminal */
extern char term_history[TERM_HIST][TERM_MAX_COL + 1];
extern int  term_num_lines;
extern int  g_term_scroll;
extern char term_input[INPUT_MAX + 1];
extern int  term_input_len;

/* Calculator */
extern int32_t g_calc_a;
extern int32_t g_calc_cur;
extern char    g_calc_op;
extern int     g_calc_fresh;
extern char    g_calc_disp[16];

/* TextEdit */
extern char g_edit_text[TEXTEDIT_MAXCHARS];
extern int  g_edit_len;
extern int  g_edit_focused;
extern int  g_edit_bold;
extern int  g_edit_italic;
extern int  g_edit_font_size;
extern uint32_t g_edit_color;
extern int  g_edit_sel_start;
extern int  g_edit_sel_end;

/* Notes */
extern int  g_notes_sel;
extern int  g_notes_focused;
extern char g_notes_titles[NOTES_COUNT][32];
extern char g_notes_body[NOTES_COUNT][NOTES_MAXLEN];
extern int  g_notes_body_len[NOTES_COUNT];

/* Safari */
extern int  g_safari_url_focused;
extern char g_safari_url[SAFARI_URL_MAX];
extern int  g_safari_tab_count;
extern int  g_safari_active_tab;
extern char g_safari_tab_urls[SAFARI_MAX_TABS][SAFARI_URL_MAX];
extern char g_safari_tab_titles[SAFARI_MAX_TABS][SAFARI_TITLE_MAX];
extern char g_safari_hist_urls[SAFARI_MAX_TABS][SAFARI_HISTORY_MAX][SAFARI_URL_MAX];
extern int  g_safari_hist_count[SAFARI_MAX_TABS];
extern int  g_safari_hist_index[SAFARI_MAX_TABS];
extern int  g_safari_page_state;
extern char g_safari_page_url[SAFARI_URL_MAX];
extern char g_safari_page_title[SAFARI_TITLE_MAX];
extern char g_safari_page_status[SAFARI_STATUS_MAX];
extern char g_safari_page_text[SAFARI_PAGE_TEXT_MAX];
extern int  g_safari_page_scroll;
extern int  g_safari_link_count;
extern char g_safari_link_urls[SAFARI_MAX_LINKS][SAFARI_URL_MAX];
extern char g_safari_link_titles[SAFARI_MAX_LINKS][SAFARI_LINK_TITLE_MAX];
extern int  g_safari_form_count;
extern int  g_safari_form_focused;
extern char g_safari_form_actions[SAFARI_MAX_FORMS][SAFARI_URL_MAX];
extern char g_safari_form_methods[SAFARI_MAX_FORMS][8];
extern char g_safari_form_input_names[SAFARI_MAX_FORMS][SAFARI_FORM_NAME_MAX];
extern char g_safari_form_values[SAFARI_MAX_FORMS][SAFARI_FORM_VALUE_MAX];
extern char g_safari_form_query_prefix[SAFARI_MAX_FORMS][SAFARI_FORM_QUERY_MAX];
extern char g_safari_form_titles[SAFARI_MAX_FORMS][SAFARI_LINK_TITLE_MAX];
extern uint32_t g_safari_page_status_code;
extern uint32_t g_safari_loaded_tick;

extern int g_maps_view;

/* Finder */
extern int g_finder_view;

/* Photos */
extern int g_photos_sel;
extern int g_photos_section;
extern int g_photos_fullscreen;
extern int g_photos_edit_mode;
extern int g_photos_brightness;
extern int g_photos_contrast;
extern int g_photos_saturation;


/* Dictionary */
extern char g_dict_input[32];
extern int  g_dict_input_len;
extern int  g_dict_focused;
extern int  g_wordle_focused;
extern const dict_entry_t g_dict_words[];

/* Settings */
extern int g_settings_tab;
extern int g_widgets_visible;
extern int g_am_tab;
extern int g_reminders_done;
extern int g_reminders_sel_list;
extern int g_reminders_extra_lists;
extern int g_reminders_extra_items;
extern int g_appstore_tab;
extern int g_appstore_downloads;
extern int g_appstore_search_focused;
extern int g_migration_source;
extern int g_migration_step;
extern int g_books_tab;
extern int g_books_reading;
extern int g_books_selected;
extern int g_findmy_selected;
extern int g_findmy_search_focused;
extern int g_news_selected;
extern int g_contacts_search_focused;
extern int g_music_eq_visible;
extern int g_pref_darkmode;
extern int g_screen_time_tab;
extern int g_passwords_sel;
extern int g_passwords_entry;
extern int g_passwords_added;
extern int g_passwords_search_focused;
extern int g_numbers_sel_row;
extern int g_numbers_sel_col;
extern int g_calendar_added_events;
extern int g_calendar_added_day;

/* Toast */
extern char     g_toast_msg[64];
extern char     g_toast_sub[64];
extern uint32_t g_toast_color;
extern uint32_t g_toast_birth;
extern int      g_toast_visible;
extern int      g_toast_type;

/* Launchpad */
extern int  g_lp_visible;
extern int  g_lp_page;
extern char g_lp_search[32];
extern int  g_lp_slen;
extern const lp_icon_t s_lp_icons[LP_ICON_COUNT];

extern int g_mc_visible;

/* App Exposé */
extern int g_expose_visible;
extern int g_expose_app_idx;   /* dock icon index of target app */

/* Notification Center */
extern int g_nc_visible;
extern char g_nc_msgs[NC_MAX_ENTRIES][48];
extern char g_nc_subs[NC_MAX_ENTRIES][48];
extern uint32_t g_nc_colors[NC_MAX_ENTRIES];
extern int g_nc_count;

/* Spaces */
extern int g_current_space;
extern int g_num_spaces;

/* System prefs */
extern int g_pref_wifi;
extern int g_pref_bt;
extern int g_pref_vpn;
extern int g_pref_notifs;
extern int g_pref_wallpaper;
extern int g_pref_accent;
extern int g_pref_resolution;
extern int g_pref_alert_sound;

/* Control Center state */
extern int g_cc_visible;
extern int g_stage_manager;

/* Window tiling */
extern int g_tile_zone;
extern int g_tile_flash;
extern int g_tile_saved_x;
extern int g_tile_saved_y;
extern int g_tile_saved_w;
extern int g_tile_saved_h;
extern int g_tile_saved_idx;

/* Photo Booth */
extern int      g_pb_filter;
extern int      g_pb_captured;
extern uint32_t g_pb_flash_tick;
extern int      g_pb_photos[4];

/* Contacts */
extern int g_ct_sel;

/* Messages */
extern int  g_ms_sel;
extern char g_ms_input[80];
extern int  g_ms_input_len;
extern int  g_ms_focused;
extern int  g_ms_search_focused;
extern char g_ms_sent[MS_MAXSENT][72];
extern int  g_ms_sent_conv[MS_MAXSENT];
extern int  g_ms_sent_n;
extern char g_ms_reply[MS_MAXSENT][72];
extern int  g_ms_reply_conv[MS_MAXSENT];
extern uint32_t g_ms_reply_tick[MS_MAXSENT];
extern int  g_ms_reply_n;

/* Home */
extern int g_home_dev_on[6];
extern int g_home_temp;
extern int g_home_room;
extern int g_home_brightness;

/* Breakout */
extern int g_brk_active;
extern int g_brk_game_over;
extern int g_brk_won;
extern int g_brk_paddle_x;
extern int g_brk_ball_x;
extern int g_brk_ball_y;
extern int g_brk_ball_vx;
extern int g_brk_ball_vy;
extern int g_brk_bricks[BRK_ROWS][BRK_COLS];
extern int g_brk_score;
extern int g_brk_lives;
extern uint32_t g_brk_last_tick;

/* 2048 */
extern int g_2048_board[4][4];
extern int g_2048_score;
extern int g_2048_best;
extern int g_2048_state;

/* Chess */
extern int g_chess_board[8][8];
extern int g_chess_sel_r;
extern int g_chess_sel_c;
extern int g_chess_white_turn;
extern int g_chess_moved;

/* Pong */
extern int  g_pong_active;
extern int  g_pong_over;
extern int  g_pong_py;
extern int  g_pong_ay;
extern int  g_pong_bx;
extern int  g_pong_by;
extern int  g_pong_vx;
extern int  g_pong_vy;
extern int  g_pong_score_p;
extern int  g_pong_score_a;
extern uint32_t g_pong_last;

/* Snake */
extern int g_snake_active;
extern int g_snake_len;
extern int g_snake_x[SNAKE_MAX_LEN];
extern int g_snake_y[SNAKE_MAX_LEN];
extern int g_snake_dir;
extern int g_snake_next_dir;
extern int g_snake_food_x;
extern int g_snake_food_y;
extern int g_snake_score;
extern uint32_t g_snake_last_tick;
extern int g_snake_speed;
extern int g_snake_game_over;

/* Wordle */
extern const char *g_wordle_words[];
extern int  g_wordle_answer_idx;
extern char g_wordle_guesses[WORDLE_ROWS][WORDLE_COLS+1];
extern int  g_wordle_results[WORDLE_ROWS][WORDLE_COLS];
extern int  g_wordle_cur_row;
extern int  g_wordle_cur_col;
extern int  g_wordle_state;
extern int  g_wordle_kb_state[26];

/* Window state arrays */
extern int g_win_minimized[MAX_WINDOWS];
extern int g_win_anim[MAX_WINDOWS];
extern int g_win_close_anim[MAX_WINDOWS];  /* minimize scale-down: OPEN_ANIM..0 */

/* Screensaver / lock */
extern uint32_t g_last_input_tick;
extern int      g_saver_on;
extern int      g_saver_mode;
extern int      g_saver_x;
extern int      g_saver_y;
extern int      g_saver_dx;
extern int      g_saver_dy;
extern int  g_locked;
extern char g_lock_pw[9];
extern int  g_lock_pw_len;

/* Share / Print */
extern int g_share_visible;
extern int g_share_action_count;
extern const char *g_share_last_action;
extern int g_print_visible;
extern int g_print_copies;
extern int g_print_page_from;
extern int g_print_page_to;
extern int g_print_color;
extern int g_print_quality;
extern int g_print_jobs;

/* Per-space wallpaper */
extern int g_space_wallpaper[4];

/* Mail compose */
extern int  g_mail_compose;
extern int  g_mail_focused_field;
extern char g_mail_to[64];
extern int  g_mail_to_len;
extern char g_mail_subject[64];
extern int  g_mail_subject_len;
extern char g_mail_body[256];
extern int  g_mail_body_len;
extern int  g_mail_sel_msg;
extern int  g_mail_folder;
extern int  g_mail_sent_count;
extern int  g_mail_search_focused;
extern char g_mail_last_sent_subject[64];

/* Maps interactive */
extern int g_maps_zoom;
extern int g_maps_pan_x;
extern int g_maps_pan_y;

/* FaceTime */
extern int g_facetime_active;
extern int g_facetime_contact;

/* Spotlight */
extern int  g_spot_visible;
extern char g_spot_query[SPOT_QUERY_MAX + 1];
extern int  g_spot_qlen;
extern int  g_spot_sel;

/* Finder depth */
extern int g_finder_depth;

/* Context menu */
extern int g_ctx_visible;
extern int g_ctx_x;
extern int g_ctx_y;
extern int g_ctx_type;

/* Menu open state */
extern int g_menu_open;

/* Sudoku */
extern int g_sdk_puzzle[SDK_SZ][SDK_SZ];
extern int g_sdk_board[SDK_SZ][SDK_SZ];
extern int g_sdk_given[SDK_SZ][SDK_SZ];
extern int g_sdk_sel_r;
extern int g_sdk_sel_c;
extern int g_sdk_started;
extern int g_sdk_errors;

/* Finder navigation (defined in gui_draw.c) */
#define FINDER_DEPTH_MAX 4
extern int   g_finder_depth;
extern const char *g_finder_stack[FINDER_DEPTH_MAX];

/* Notification Center (defined in gui_overlays.c) */
extern char g_nc_msgs[NC_MAX_ENTRIES][48];
extern char g_nc_subs[NC_MAX_ENTRIES][48];
extern uint32_t g_nc_colors[NC_MAX_ENTRIES];

/* Spotlight (defined in gui_overlays.c) */
extern int  g_spot_visible;
extern char g_spot_query[SPOT_QUERY_MAX + 1];
extern int  g_spot_qlen;
extern int  g_spot_sel;
extern const char *g_spot_apps[];

/* Minesweeper game */
#define MINE_ROWS    9
#define MINE_COLS    9
#define MINE_COUNT  10
extern int      g_mine_board[MINE_ROWS][MINE_COLS];
extern int      g_mine_vis[MINE_ROWS][MINE_COLS];
extern int      g_mine_flag[MINE_ROWS][MINE_COLS];
extern int      g_mine_state;        /* 0=ready 1=playing 2=won 3=lost */
extern uint32_t g_mine_start_tick;
extern uint32_t g_mine_end_tick;
extern int      g_mine_remaining;
extern uint32_t g_mine_rng;

/* Journal app */
#define JOURNAL_MAX   5
#define JOURNAL_BLEN 160
#define JOURNAL_TLEN  40
extern int  g_journal_sel;
extern int  g_journal_edit;
extern int  g_journal_focused;
extern int  g_journal_count;
extern char g_journal_titles[JOURNAL_MAX][JOURNAL_TLEN];
extern char g_journal_bodies[JOURNAL_MAX][JOURNAL_BLEN];

extern int g_preview_page;
extern int g_preview_zoom;
extern int g_preview_markup;
extern int g_atv_sel;
extern int g_atv_bottom_tab;
extern int g_atv_playing;
extern int g_wallet_pay_count;
extern int g_compressor_added_files;
extern int g_compressor_submitted;
extern int g_transporter_uploading;
extern uint32_t g_transporter_upload_start_tick;
extern int g_transporter_upload_count;
extern int g_screen_recording_active;
extern int g_screen_recording_count;
extern int g_sidecar_connected;
extern int g_universal_connected;
extern int g_translate_favorites;
extern int g_math_notes_created;
extern int g_feedback_submissions;
extern int g_cleanmymac_scan_count;
extern int g_keynote_mode;
extern int g_keynote_slide_count;
extern int g_keynote_slide;
extern int g_keynote_editing;
extern int g_imovie_tab;
extern int g_imovie_import_count;
extern int g_imovie_share_count;
extern int g_xcode_running;
extern int g_xcode_run_count;
extern int g_clock_extra_alarms;
extern int g_motion_playing;
extern int g_motion_tool;
extern int g_motion_layer;
extern int g_podcasts_playing;
extern int g_podcasts_episode;
extern int g_freeform_tool;
extern int g_freeform_added_items;
extern int g_netutil_tab;
extern int g_netutil_ping_count;
extern int g_netutil_ping_ok;
extern uint32_t g_netutil_ping_target;
extern uint32_t g_netutil_ping_last_ms;
extern uint32_t g_netutil_ping_last_tick;
extern int g_gamecenter_tab;
extern int g_fontbook_selected;
extern int g_pages_bold;
extern int g_pages_italic;
extern int g_pages_underline;
extern int g_pages_align;
extern int g_pages_inspector;
extern int g_pages_insert_count;
extern int g_finalcut_transport;
extern int g_finalcut_library;
extern int g_shortcuts_tab;
extern int g_shortcuts_run_count;
extern int g_voice_memos_recording;
extern uint32_t g_voice_memos_start_tick;
extern int g_voice_memos_saved;
extern int g_garageband_playing;
extern int g_garageband_recording;
extern int g_garageband_take_count;
extern int g_automator_mode;
extern int g_automator_recording;
extern int g_automator_search_focused;
extern int g_automator_category;
extern int g_automator_step;
extern int g_fontbook_search_focused;
extern int g_console_filter_focused;
extern int g_console_level_filter;
extern int g_console_device_filter;
extern int g_sfsymbols_search_focused;
extern int g_sfsymbols_selected;
extern int g_privacy_category;
extern int g_accessibility_category;
extern int g_configurator_category;
extern int g_transmit_transfer_count;
extern int g_overflow_selected;
extern int g_airplay_device;
extern int g_testflight_selected;
extern int g_testflight_updated_mask;
extern int g_testflight_invites;
extern int g_clips_recording;
extern int g_clips_saved_count;
extern int g_mainstage_zone;
extern int g_mainstage_performing;
extern int g_instruments_recording;
extern int g_instruments_track;
extern int g_klokki_project;
extern int g_klokki_running;
extern int g_lasso_selected;
extern int g_bartender_visible_mask;
extern int g_scrobbles_selected;
extern int g_logic_transport;
extern int g_logic_track;
extern int g_tot_dot;
extern uint32_t g_dcm_sample_color;
extern int g_grapher_curve;
extern int g_health_metric;
extern int g_reality_object;
extern int g_iphone_mirroring_connected;
extern int g_iphone_mirroring_taps;
extern int g_stocks_selected;
extern int g_weather_selection;
extern int g_keyboard_shortcut_selected;
extern int g_system_info_selected;
extern int g_stickies_selected;
extern int g_fantastical_selected_day;
extern int g_proxyman_session;
extern int g_istudiez_tab;
extern int g_onepassword_vault;
extern int g_things_section;
extern int g_raycast_result;
extern int g_bear_note;
extern int g_reeder_feed;
extern int g_alfred_result;
extern int g_onepassword_search_focused;
extern int g_raycast_search_focused;
extern int g_finder_search_focused;
extern int g_photos_search_focused;
extern int g_color_picker_tab;
extern int g_maps_search_focused;
extern int g_alfred_search_focused;
extern int g_script_running;
extern int g_script_run_count;
extern int g_numbers_tool;
extern int g_disk_utility_action;
extern int g_about_update_checks;
extern int g_contacts_added;
extern int g_maps_route_started;

/* Context menu (defined in gui_overlays.c) */
#define CTX_MENU_W       150
#define CTX_MENU_ITEM_H   20
#define CTX_MENU_ITEMS     8
extern int g_ctx_visible;
extern int g_ctx_x;
extern int g_ctx_y;
extern int g_ctx_type;
extern const char *g_ctx_labels[CTX_MENU_ITEMS];

/* Dock context menu (right-click popover above dock icon) */
extern int g_dock_ctx_visible;
extern int g_dock_ctx_icon;   /* index into s_dock_icons */
#define DOCK_CTX_ITEMS  6

/* Menu open state (defined in gui_overlays.c) */
extern int g_menu_open;

/* Menubar titles and items (defined in gui_overlays.c) */
#define N_MENUS         6
#define MAX_MENU_ITEMS  13
extern const char *g_menubar_titles[N_MENUS];
extern const char *g_menu_items[N_MENUS][MAX_MENU_ITEMS];

/* =========================================================================
 * Function prototypes for shared helpers
 * ======================================================================= */

/* String helpers */
static inline int min_int(int a, int b) { return a < b ? a : b; }
int   str_len(const char *s);
int   str_eq(const char *a, const char *b);
void  str_cpy(char *dst, const char *src);
void  int_to_str(int n, char *buf);

/* GUI helpers */
void win_bring_to_front(int idx);
void win_send_to_back(int idx);
void win_minimize(int idx);
void win_close(int idx);
int  win_top_visible(void);
int  dropdown_hit(int mx, int my);
void get_current_datetime(datetime_t *dt);
void get_clock_str(char *buf);
void get_date_long_str(char *buf);
void get_date_short_str(char *buf);
void get_month_day_str(char *buf);
void get_file_modified_str(char *buf);
void get_menu_date_str(char *buf);
void get_weekday_upper_str(char *buf);
void get_month_year_str(int year, int month, char *buf);
void get_current_month_year_str(char *buf);
void get_week_of_year_str(char *buf);
void gui_calendar_month_from_offset(int offset, int *year, int *month);
int  gui_calendar_today_day_for_month(int year, int month);
void toast_show(const char *msg, const char *sub, uint32_t color);
void toast_show_action(const char *msg, const char *sub, uint32_t color, int type);
void nc_add(const char *msg, const char *sub, uint32_t color);
void term_println(const char *s);

/* Calculator helpers */
void calc_update_disp(int32_t v);
void calc_press(char action);
char calc_hit(int mx, int my, const gui_window_t *win);

/* Focus mode helper (in gui_draw.c) */
const char *focus_mode_name(int mode);

/* Terminal */
void term_process_command(void);

/* Safari */
void safari_normalize_state(void);
int safari_is_home_url(const char *url);
void safari_load_url(const char *url);
void safari_load_current_tab(void);
void safari_reload(void);
int safari_home_site_count(void);
const safari_home_site_t *safari_home_site_at(int index);
int safari_url_has_scheme(const char *url, const char *scheme);
int safari_can_go_back(void);
int safari_can_go_forward(void);
void safari_go_back(void);
void safari_go_forward(void);
void safari_focus_form(int index);
void safari_submit_form(int index);
void safari_reset_tab_state(int tab, const char *url);
void safari_copy_tab_state(int dst, int src);
int writing_tools_apply(int tool);

/* Drawing helpers called across files */
void draw_toggle(int x, int y, int on);
void draw_ring_arc(int cx, int cy, int r_out, int r_in, int pct, uint32_t ring_col, uint32_t track_col);

/* 2048 helpers */
void g2048_spawn(void);
void g2048_new_game(void);
int  g2048_move(int dir);

/* Finder helpers (in gui_draw.c) */
typedef struct { const char *name; uint32_t color; } folder_icon_t;
const folder_icon_t *finder_current_folders(int *count);
void draw_folder(int x, int y, int cell_w, const folder_icon_t *fi);

/* Dock icon type (in gui_draw.c) */
typedef struct { const char *name; uint32_t color; char letter; } dock_icon_t;
extern const dock_icon_t s_dock_icons[17];
#define DOCK_ICON_SIZE  36
#define DOCK_ICON_PAD    6
#define DOCK_ICON_R      6
#define NUM_DOCK_ICONS  17

/* App group draw functions (split from draw_window_content) */
int draw_apps_group1(int idx);
int draw_apps_group2(int idx);
int draw_apps_group3(int idx);
int draw_apps_group4(int idx);

/* Overlay draw functions (in gui_overlays.c) */
void stage_manager_draw(void);
void cc_draw(void);
int  cc_click(int mx, int my);
void nc_draw(void);
int  nc_click(int mx, int my);
void mission_control_draw(void);
void app_expose_draw(void);
int  mission_control_hit(int mx, int my);
void spotlight_draw(void);
void ctx_menu_draw(void);
int  ctx_menu_hit(int mx, int my);
void dock_ctx_draw(void);
int  dock_ctx_hit(int mx, int my);
int  menubar_hit(int mx, int my);
void draw_dropdown(int menu_idx);
void dropdown_action(int menu_idx, int item_idx);
void menubar_update_for_active_app(void);
void lock_screen_draw(void);
void launchpad_draw(void);
int  launchpad_hit(int mx, int my);
int  spot_substr_match(const char *haystack, const char *needle, int nlen);
int  menubar_item_x(int idx);

/* Additional overlay functions (in gui_overlays.c) */
void widget_bar_draw(void);
void siri_draw(void);
void writing_tools_draw(void);
void quick_note_draw(void);
void quick_look_draw(void);
void screenshot_tool_draw(void);
void share_sheet_draw(void);
void print_dialog_draw(void);
void airdrop_draw(void);
void handoff_draw(void);
void app_switcher_draw(void);
void crash_reporter_draw(void);
void system_update_draw(void);
void focus_filter_draw(void);
void icloud_panel_draw(void);
void bluetooth_dialog_draw(void);
void keyboard_shortcuts_draw(void);
void time_machine_draw(void);
void color_meter_draw(void);
void notif_history_draw(void);
void wifi_panel_draw(void);
void display_settings_draw(void);
void sound_settings_draw(void);
void activity_monitor_draw(void);
void facetime_draw(void);
void privacy_panel_draw(void);
void reminders_draw(void);
void calendar_draw(void);
void airplay_draw(void);
int  new_overlays_click(int mx, int my);

/* Scene and draw functions (in gui.c) */
void draw_scene(int mx, int my);
void gui_draw_circle_outline(int cx, int cy, int r, uint32_t color);
void toast_draw(void);
int  vga_cos_approx(int deg);
int  vga_sin_approx(int deg);

/* Cursor helpers (in gui_overlays.c) */
void cursor_save(int x, int y);
void cursor_restore(void);

#endif /* GUI_INTERNAL_H */
