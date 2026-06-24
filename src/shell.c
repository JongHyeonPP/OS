#include "shell.h"
#include "block.h"
#include "debug.h"
#include "driver.h"
#include "heap.h"
#include "net.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "process.h"
#include "runtime_info.h"
#include "selftest.h"
#include "simplefs.h"
#include "task.h"
#include "timer.h"
#include "tty.h"
#include "uts.h"
#include "vfs.h"
#include <stdint.h>

static char g_cwd[VFS_MAX_PATH];
static int g_redirect_fd = -1;
static int g_redirect_failed = 0;
static char g_pipe_capture[512];
static uint32_t g_pipe_capture_pos;
static int g_pipe_capture_failed;

static void append_ipv4(char *line, uint32_t *pos, uint32_t max, uint32_t ip);

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static uint32_t slen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void scopy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (!max) return;
    while (src && src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void append_char(char *buf, uint32_t *pos, uint32_t max, char ch) {
    if (*pos + 1 >= max) return;
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = 0;
}

static void append_str(char *buf, uint32_t *pos, uint32_t max, const char *s) {
    while (s && *s) append_char(buf, pos, max, *s++);
}

static int component_is(const char *s, uint32_t len, const char *lit) {
    uint32_t i = 0;
    while (i < len && lit[i] && s[i] == lit[i]) i++;
    return i == len && lit[i] == 0;
}

static void pop_path_component(char *buf, uint32_t *pos) {
    if (*pos <= 1) {
        *pos = 1;
        buf[0] = '/';
        buf[1] = 0;
        return;
    }
    while (*pos > 1 && buf[*pos - 1] != '/') (*pos)--;
    if (*pos > 1) (*pos)--;
    buf[*pos] = 0;
}

static int append_path_component(char *buf,
                                 uint32_t *pos,
                                 uint32_t max,
                                 const char *s,
                                 uint32_t len) {
    uint32_t i;
    if (*pos > 1) {
        if (*pos + 1 >= max) return -1;
        append_char(buf, pos, max, '/');
    }
    if (*pos + len >= max) return -1;
    for (i = 0; i < len; i++) append_char(buf, pos, max, s[i]);
    return 0;
}

static void append_dec(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    char tmp[11];
    uint32_t n = 0;
    if (v == 0) {
        append_char(buf, pos, max, '0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n) append_char(buf, pos, max, tmp[--n]);
}

static void append_octal_mode(char *buf, uint32_t *pos, uint32_t max, uint32_t mode) {
    uint32_t perm = mode & VFS_MODE_PERM_MASK;
    append_char(buf, pos, max, '0');
    append_char(buf, pos, max, (char)('0' + ((perm >> 6) & 7U)));
    append_char(buf, pos, max, (char)('0' + ((perm >> 3) & 7U)));
    append_char(buf, pos, max, (char)('0' + (perm & 7U)));
}

static void append_int(char *buf, uint32_t *pos, uint32_t max, int32_t v) {
    uint32_t mag;
    if (v >= 0) {
        append_dec(buf, pos, max, (uint32_t)v);
        return;
    }
    append_char(buf, pos, max, '-');
    mag = (uint32_t)(-(v + 1)) + 1U;
    append_dec(buf, pos, max, mag);
}

static void append_hex8(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    append_char(buf, pos, max, h[(v >> 4) & 0xF]);
    append_char(buf, pos, max, h[v & 0xF]);
}

static void append_hex16(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    append_hex8(buf, pos, max, (v >> 8) & 0xFF);
    append_hex8(buf, pos, max, v & 0xFF);
}

static void append_mac(char *buf, uint32_t *pos, uint32_t max, const uint8_t mac[6]) {
    uint32_t i;
    for (i = 0; i < 6; i++) {
        if (i) append_char(buf, pos, max, ':');
        append_hex8(buf, pos, max, mac ? mac[i] : 0);
    }
}

static const char *skip_spaces(const char *s);

static int parse_u32(const char *s, uint32_t *value, const char **rest) {
    uint32_t v = 0;
    int any = 0;
    s = skip_spaces(s);
    while (*s >= '0' && *s <= '9') {
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (0xFFFFFFFFU - digit) / 10U) return -1;
        v = v * 10U + digit;
        s++;
        any = 1;
    }
    if (!any) return -1;
    if (value) *value = v;
    if (rest) *rest = skip_spaces(s);
    return 0;
}

static int parse_u32_token(const char *s, uint32_t *value, const char **rest) {
    uint32_t v = 0;
    int any = 0;
    s = skip_spaces(s);
    while (*s >= '0' && *s <= '9') {
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (0xFFFFFFFFU - digit) / 10U) return -1;
        v = v * 10U + digit;
        s++;
        any = 1;
    }
    if (!any || (*s && *s != ' ')) return -1;
    if (value) *value = v;
    if (rest) *rest = skip_spaces(s);
    return 0;
}

static int parse_octal_mode_token(const char *s, uint32_t *value, const char **rest) {
    uint32_t v = 0;
    int any = 0;
    s = skip_spaces(s);
    while (*s >= '0' && *s <= '7') {
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (VFS_MODE_PERM_MASK - digit) / 8U) return -1;
        v = v * 8U + digit;
        s++;
        any = 1;
    }
    if (!any || v > VFS_MODE_PERM_MASK || (*s && *s != ' ')) return -1;
    if (value) *value = v;
    if (rest) *rest = skip_spaces(s);
    return 0;
}

static int parse_block_ref(const char *arg, uint32_t *dev, const char **rest) {
    char name[BLOCK_NAME_MAX];
    uint32_t i = 0;
    uint32_t index;
    arg = skip_spaces(arg);
    if (!*arg) {
        *dev = 0;
        *rest = arg;
        return block_count() ? 0 : -1;
    }
    if (*arg >= '0' && *arg <= '9') {
        if (parse_u32_token(arg, &index, rest) < 0) return -1;
        if (index >= block_count()) return -1;
        *dev = index;
        return 0;
    }
    while (arg[i] && arg[i] != ' ' && i + 1 < sizeof(name)) {
        name[i] = arg[i];
        i++;
    }
    name[i] = 0;
    {
        int dev_index = block_find(name);
        if (dev_index < 0) return -1;
        *dev = (uint32_t)dev_index;
    }
    while (arg[i] && arg[i] != ' ') i++;
    *rest = skip_spaces(arg + i);
    return 0;
}

static int parse_netif_ref(const char *arg, uint32_t *ifindex, const char **rest) {
    char name[NET_NAME_MAX];
    uint32_t i = 0;
    uint32_t index;
    arg = skip_spaces(arg);
    if (!*arg) return -1;
    if (*arg >= '0' && *arg <= '9') {
        if (parse_u32_token(arg, &index, rest) < 0) return -1;
        if (index >= netif_count()) return -1;
        *ifindex = index;
        return 0;
    }
    while (arg[i] && arg[i] != ' ' && i + 1 < sizeof(name)) {
        name[i] = arg[i];
        i++;
    }
    name[i] = 0;
    while (arg[i] && arg[i] != ' ') i++;
    *rest = skip_spaces(arg + i);
    for (i = 0; i < netif_count(); i++) {
        const netif_t *n = netif_at(i);
        if (n && n->name && streq(n->name, name)) {
            *ifindex = i;
            return 0;
        }
    }
    return -1;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static void first_token(const char *line, char *cmd, uint32_t max, const char **rest) {
    uint32_t i = 0;
    line = skip_spaces(line);
    while (line[i] && line[i] != ' ' && i + 1 < max) {
        cmd[i] = line[i];
        i++;
    }
    cmd[i] = 0;
    while (line[i] && line[i] != ' ') i++;
    *rest = skip_spaces(line + i);
}

static int split_redirection_line(const char *line,
                                  char *cmdline,
                                  uint32_t cmdmax,
                                  char *target,
                                  uint32_t target_max,
                                  int *append) {
    uint32_t i = 0;
    uint32_t cmd_len;
    uint32_t out_pos = 0;
    uint32_t target_len = 0;
    const char *p;
    if (!line || !cmdline || !cmdmax || !target || !target_max || !append) return -1;
    while (line[i] && line[i] != '>') i++;
    if (!line[i]) return 0;
    cmd_len = i;
    while (cmd_len > 0 && line[cmd_len - 1] == ' ') cmd_len--;
    if (cmd_len == 0 || cmd_len >= cmdmax) return -1;
    for (out_pos = 0; out_pos < cmd_len; out_pos++) cmdline[out_pos] = line[out_pos];
    cmdline[cmd_len] = 0;
    *append = 0;
    i++;
    if (line[i] == '>') {
        *append = 1;
        i++;
    }
    p = skip_spaces(line + i);
    if (!*p) return -1;
    while (p[target_len] && p[target_len] != ' ') {
        if (target_len + 1 >= target_max) return -1;
        target[target_len] = p[target_len];
        target_len++;
    }
    target[target_len] = 0;
    p = skip_spaces(p + target_len);
    if (*p) return -1;
    return 1;
}

static int split_pipeline_line(const char *line,
                               char *left,
                               uint32_t left_max,
                               char *right,
                               uint32_t right_max) {
    uint32_t i = 0;
    uint32_t left_len;
    uint32_t right_len = 0;
    const char *p;
    if (!line || !left || !left_max || !right || !right_max) return -1;
    while (line[i] && line[i] != '|') i++;
    if (!line[i]) return 0;
    left_len = i;
    while (left_len > 0 && line[left_len - 1] == ' ') left_len--;
    if (left_len == 0 || left_len >= left_max) return -1;
    for (i = 0; i < left_len; i++) left[i] = line[i];
    left[left_len] = 0;
    p = skip_spaces(line + left_len);
    if (*p != '|') return -1;
    p = skip_spaces(p + 1);
    if (!*p) return -1;
    while (p[right_len]) {
        if (p[right_len] == '|') return -1;
        if (right_len + 1 >= right_max) return -1;
        right[right_len] = p[right_len];
        right_len++;
    }
    while (right_len > 0 && right[right_len - 1] == ' ') right_len--;
    right[right_len] = 0;
    return right_len ? 1 : -1;
}

static void redirect_write_line(const char *line) {
    uint32_t len = slen(line);
    if (g_redirect_fd < 0) {
        g_redirect_failed = 1;
        return;
    }
    if (len && vfs_write(g_redirect_fd, line, len) != (int)len) {
        g_redirect_failed = 1;
        return;
    }
    if (vfs_write(g_redirect_fd, "\n", 1) != 1) g_redirect_failed = 1;
}

static void pipe_capture_reset(void) {
    g_pipe_capture_pos = 0;
    g_pipe_capture[0] = 0;
    g_pipe_capture_failed = 0;
}

static void pipe_capture_line(const char *line) {
    uint32_t len = slen(line);
    uint32_t i;
    if (g_pipe_capture_failed) return;
    if (g_pipe_capture_pos + len + 1 >= sizeof(g_pipe_capture)) {
        g_pipe_capture_failed = 1;
        return;
    }
    for (i = 0; i < len; i++) g_pipe_capture[g_pipe_capture_pos++] = line[i];
    g_pipe_capture[g_pipe_capture_pos++] = '\n';
    g_pipe_capture[g_pipe_capture_pos] = 0;
}

static int text_contains(const char *haystack, const char *needle) {
    uint32_t i;
    if (!haystack || !needle) return 0;
    if (!needle[0]) return 1;
    for (i = 0; haystack[i]; i++) {
        uint32_t j = 0;
        while (haystack[i + j] && needle[j] && haystack[i + j] == needle[j]) j++;
        if (needle[j] == 0) return 1;
    }
    return 0;
}

static void emit_text_lines(const char *text, shell_write_fn out) {
    char line[96];
    uint32_t lp = 0;
    uint32_t i;
    for (i = 0; text && text[i]; i++) {
        if (text[i] == '\n') {
            line[lp] = 0;
            out(line);
            lp = 0;
        } else if (lp + 1 < sizeof(line)) {
            line[lp++] = text[i];
        }
    }
    if (lp) {
        line[lp] = 0;
        out(line);
    }
}


static uint32_t text_line_total(const char *text) {
    uint32_t i;
    uint32_t lines = 0;
    int saw_char = 0;
    int last_was_newline = 0;
    if (!text) return 0;
    for (i = 0; text[i]; i++) {
        saw_char = 1;
        if (text[i] == '\n') {
            lines++;
            last_was_newline = 1;
        } else {
            last_was_newline = 0;
        }
    }
    if (saw_char && !last_was_newline) lines++;
    return lines;
}

static void emit_line_range_text(const char *text,
                                 uint32_t first_line,
                                 uint32_t max_lines,
                                 shell_write_fn out) {
    char line[96];
    uint32_t lp = 0;
    uint32_t i;
    uint32_t line_no = 0;
    uint32_t emitted = 0;
    if (!text || max_lines == 0) return;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            line[lp] = 0;
            if (line_no >= first_line && emitted < max_lines) {
                out(line);
                emitted++;
            }
            line_no++;
            lp = 0;
            if (emitted >= max_lines) return;
        } else if (lp + 1 < sizeof(line)) {
            line[lp++] = text[i];
        }
    }
    if (lp && emitted < max_lines && line_no >= first_line) {
        line[lp] = 0;
        out(line);
    }
}

static void emit_head_text(const char *text, uint32_t count, shell_write_fn out) {
    emit_line_range_text(text, 0, count, out);
}

static void emit_tail_text(const char *text, uint32_t count, shell_write_fn out) {
    uint32_t total = text_line_total(text);
    uint32_t start = total > count ? total - count : 0;
    emit_line_range_text(text, start, count, out);
}

static void emit_grep_text(const char *text, const char *pattern, shell_write_fn out) {
    char line[96];
    uint32_t lp = 0;
    uint32_t i;
    if (!pattern || !pattern[0]) {
        out("grep: missing pattern");
        return;
    }
    for (i = 0; text && text[i]; i++) {
        if (text[i] == '\n') {
            line[lp] = 0;
            if (text_contains(line, pattern)) out(line);
            lp = 0;
        } else if (lp + 1 < sizeof(line)) {
            line[lp++] = text[i];
        }
    }
    if (lp) {
        line[lp] = 0;
        if (text_contains(line, pattern)) out(line);
    }
}

static void count_text(const char *text, uint32_t *lines, uint32_t *words, uint32_t *bytes) {
    uint32_t i;
    int in_word = 0;
    *lines = 0;
    *words = 0;
    *bytes = 0;
    for (i = 0; text && text[i]; i++) {
        char ch = text[i];
        (*bytes)++;
        if (ch == '\n') (*lines)++;
        if (ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r') {
            in_word = 0;
        } else if (!in_word) {
            (*words)++;
            in_word = 1;
        }
    }
}

static void emit_wc_text(const char *text, shell_write_fn out) {
    char line[64];
    uint32_t pos = 0;
    uint32_t lines;
    uint32_t words;
    uint32_t bytes;
    count_text(text, &lines, &words, &bytes);
    append_dec(line, &pos, sizeof(line), lines);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), words);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), bytes);
    out(line);
}

static int shell_line_cmp(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
}

static int collect_text_lines(const char *text, char lines[][96], uint32_t max_lines, uint32_t *count_out) {
    uint32_t i;
    uint32_t line_count = 0;
    uint32_t lp = 0;
    if (!count_out || !lines) return -1;
    if (!text) text = "";
    for (i = 0; text[i]; i++) {
        if (text[i] == '\n') {
            if (line_count >= max_lines) return -1;
            lines[line_count][lp] = 0;
            line_count++;
            lp = 0;
        } else {
            if (line_count >= max_lines) return -1;
            if (lp + 1 >= 96) return -1;
            lines[line_count][lp++] = text[i];
        }
    }
    if (lp) {
        if (line_count >= max_lines) return -1;
        lines[line_count][lp] = 0;
        line_count++;
    }
    *count_out = line_count;
    return 0;
}

static void emit_sort_text(const char *text, shell_write_fn out) {
    char lines[32][96];
    uint32_t count = 0;
    uint32_t i;
    if (collect_text_lines(text, lines, 32, &count) < 0) {
        out("sort: input too large");
        return;
    }
    for (i = 0; i < count; i++) {
        uint32_t j;
        uint32_t best = i;
        for (j = i + 1U; j < count; j++) {
            if (shell_line_cmp(lines[j], lines[best]) < 0) best = j;
        }
        if (best != i) {
            char tmp[96];
            scopy(tmp, lines[i], sizeof(tmp));
            scopy(lines[i], lines[best], sizeof(lines[i]));
            scopy(lines[best], tmp, sizeof(lines[best]));
        }
        out(lines[i]);
    }
}

static void emit_uniq_text(const char *text, shell_write_fn out) {
    char lines[32][96];
    uint32_t count = 0;
    uint32_t i;
    if (collect_text_lines(text, lines, 32, &count) < 0) {
        out("uniq: input too large");
        return;
    }
    for (i = 0; i < count; i++) {
        if (i == 0 || !streq(lines[i], lines[i - 1U])) out(lines[i]);
    }
}

static int next_word(const char *line, char *word, uint32_t max, const char **rest) {
    uint32_t i = 0;
    int overflow = 0;
    line = skip_spaces(line);
    while (line[i] && line[i] != ' ') {
        if (i + 1 < max) word[i] = line[i];
        else overflow = 1;
        i++;
    }
    if (max) {
        if (overflow) word[0] = 0;
        else word[i] = 0;
    }
    while (line[i] && line[i] != ' ') i++;
    *rest = skip_spaces(line + i);
    if (overflow) return -1;
    return i ? 1 : 0;
}

static void normalize_path(const char *arg, char *out) {
    char tmp[VFS_MAX_PATH];
    const char *src;
    uint32_t i = 0;
    uint32_t pos = 0;
    uint32_t tmp_pos = 0;
    arg = skip_spaces(arg ? arg : "");
    if (!*arg) {
        scopy(out, g_cwd, VFS_MAX_PATH);
        return;
    }
    if (arg[0] == '/') {
        if (slen(arg) >= VFS_MAX_PATH) {
            out[0] = 0;
            return;
        }
        scopy(tmp, arg, VFS_MAX_PATH);
    } else {
        uint32_t arg_len = slen(arg);
        uint32_t cwd_len = slen(g_cwd);
        if (cwd_len + (streq(g_cwd, "/") ? 0U : 1U) + arg_len >= VFS_MAX_PATH) {
            out[0] = 0;
            return;
        }
        scopy(tmp, g_cwd, sizeof(tmp));
        tmp_pos = slen(tmp);
        if (tmp_pos > 1) append_char(tmp, &tmp_pos, sizeof(tmp), '/');
        append_str(tmp, &tmp_pos, sizeof(tmp), arg);
    }
    out[0] = '/';
    out[1] = 0;
    pos = 1;
    src = tmp;
    while (src[i]) {
        uint32_t start;
        uint32_t len;
        while (src[i] == '/') i++;
        start = i;
        while (src[i] && src[i] != '/') i++;
        len = i - start;
        if (len == 0 || component_is(src + start, len, ".")) continue;
        if (component_is(src + start, len, "..")) {
            pop_path_component(out, &pos);
            continue;
        }
        if (append_path_component(out, &pos, VFS_MAX_PATH, src + start, len) < 0) {
            out[0] = 0;
            return;
        }
    }
}

static void print_type(char *buf, uint32_t *pos, uint32_t max, vfs_node_type_t type) {
    if (type == VFS_NODE_DIR) append_str(buf, pos, max, "dir ");
    else if (type == VFS_NODE_DEV) append_str(buf, pos, max, "dev ");
    else if (type == VFS_NODE_SYMLINK) append_str(buf, pos, max, "link");
    else append_str(buf, pos, max, "file");
}

static void cmd_ls(const char *arg, shell_write_fn out) {
    char path[VFS_MAX_PATH];
    const char *p = arg;
    uint32_t idx = 0;
    int any = 0;
    while (*p == '-') {
        while (*p && *p != ' ') p++;
        p = skip_spaces(p);
    }
    normalize_path(p, path);
    for (;;) {
        vfs_dirent_t ent;
        char line[96];
        uint32_t pos = 0;
        if (vfs_readdir(path, idx, &ent) < 0) break;
        print_type(line, &pos, sizeof(line), ent.type);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), ent.size);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), ent.name);
        if (ent.type == VFS_NODE_DIR) append_char(line, &pos, sizeof(line), '/');
        out(line);
        idx++;
        any = 1;
    }
    if (!any) {
        vfs_dirent_t ent;
        if (vfs_stat(path, &ent) == 0) out(ent.name);
        else out("ls: no such file or directory");
    }
}

static void cmd_cat(const char *arg, shell_write_fn out) {
    char path[VFS_MAX_PATH];
    char buf[128];
    char line[96];
    uint32_t lp = 0;
    int fd;
    int n;
    uint32_t i;
    if (!*skip_spaces(arg)) {
        out("cat: missing operand");
        return;
    }
    normalize_path(arg, path);
    if (streq(path, "/etc/hosts")) {
        runtime_system_info_t sys;
        const netif_t *netif;
        char loop_line[32];
        uint32_t loop_pos = 0;
        runtime_get_system_info(&sys);
        append_ipv4(loop_line, &loop_pos, sizeof(loop_line), 0x7F000001U);
        append_str(loop_line, &loop_pos, sizeof(loop_line), " localhost");
        out(loop_line);
        netif = runtime_primary_netif();
        if (netif && netif->ipv4) {
            char host_line[96];
            uint32_t pos = 0;
            append_ipv4(host_line, &pos, sizeof(host_line), netif->ipv4);
            append_char(host_line, &pos, sizeof(host_line), ' ');
            append_str(host_line, &pos, sizeof(host_line), sys.nodename);
            out(host_line);
        }
        return;
    }
    if (streq(path, "/etc/hostname")) {
        out(uts_nodename());
        return;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        out("cat: no such file");
        return;
    }
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < (uint32_t)n; i++) {
            if (buf[i] == '\n') {
                line[lp] = 0;
                out(line);
                lp = 0;
            } else if (lp + 1 < sizeof(line)) {
                line[lp++] = buf[i];
            }
        }
    }
    if (lp) {
        line[lp] = 0;
        out(line);
    }
    vfs_close(fd);
}

static int read_text_file_arg(const char *arg, char *text, uint32_t max) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *tail;
    uint32_t pos = 0;
    int fd;
    int n;
    if (!text || max == 0) return -1;
    first_token(arg, path_arg, sizeof(path_arg), &tail);
    if (!path_arg[0] || *tail) return -1;
    normalize_path(path_arg, path);
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    while ((n = vfs_read(fd, text + pos, max - 1U - pos)) > 0) {
        pos += (uint32_t)n;
        if (pos + 1 >= max) {
            vfs_close(fd);
            text[max - 1] = 0;
            return -2;
        }
    }
    vfs_close(fd);
    text[pos] = 0;
    return n < 0 ? -1 : (int)pos;
}

static void cmd_grep(const char *arg, shell_write_fn out) {
    char pattern[64];
    char text[512];
    const char *rest;
    int n;
    first_token(arg, pattern, sizeof(pattern), &rest);
    if (!pattern[0]) {
        out("grep: missing pattern");
        return;
    }
    n = read_text_file_arg(rest, text, sizeof(text));
    if (n == -2) {
        out("grep: input too large");
        return;
    }
    if (n < 0) {
        out("grep: missing file");
        return;
    }
    emit_grep_text(text, pattern, out);
}

static void cmd_wc(const char *arg, shell_write_fn out) {
    char text[512];
    int n = read_text_file_arg(arg, text, sizeof(text));
    if (n == -2) {
        out("wc: input too large");
        return;
    }
    if (n < 0) {
        out("wc: missing file");
        return;
    }
    emit_wc_text(text, out);
}

static void cmd_sort_uniq(const char *arg, int uniq_mode, shell_write_fn out) {
    char text[512];
    int n = read_text_file_arg(arg, text, sizeof(text));
    if (n == -2) {
        out(uniq_mode ? "uniq: input too large" : "sort: input too large");
        return;
    }
    if (n < 0) {
        out(uniq_mode ? "uniq: missing file" : "sort: missing file");
        return;
    }
    if (uniq_mode) emit_uniq_text(text, out);
    else emit_sort_text(text, out);
}


static void cmd_head_tail(const char *arg, int tail_mode, shell_write_fn out) {
    char text[512];
    char first[16];
    const char *rest;
    const char *tail;
    const char *file_arg = arg;
    uint32_t count = 10;
    uint32_t parsed;
    int n;
    if (next_word(arg, first, sizeof(first), &rest) <= 0) {
        out(tail_mode ? "tail: missing file" : "head: missing file");
        return;
    }
    if (parse_u32(first, &parsed, &tail) == 0 && *tail == 0 && *rest) {
        count = parsed;
        file_arg = rest;
    }
    n = read_text_file_arg(file_arg, text, sizeof(text));
    if (n == -2) {
        out(tail_mode ? "tail: input too large" : "head: input too large");
        return;
    }
    if (n < 0) {
        out(tail_mode ? "tail: missing file" : "head: missing file");
        return;
    }
    if (tail_mode) emit_tail_text(text, count, out);
    else emit_head_text(text, count, out);
}

static void run_pipeline_right(const char *right, shell_write_fn out) {
    char cmd[24];
    char pattern[64];
    const char *arg;
    const char *tail;
    first_token(right, cmd, sizeof(cmd), &arg);
    if (streq(cmd, "cat")) {
        if (*arg) out("cat: unexpected operand");
        else emit_text_lines(g_pipe_capture, out);
        return;
    }
    if (streq(cmd, "grep")) {
        first_token(arg, pattern, sizeof(pattern), &tail);
        if (!pattern[0]) out("grep: missing pattern");
        else if (*tail) out("grep: too many operands");
        else emit_grep_text(g_pipe_capture, pattern, out);
        return;
    }
    if (streq(cmd, "wc")) {
        if (*arg) out("wc: too many operands");
        else emit_wc_text(g_pipe_capture, out);
        return;
    }
    if (streq(cmd, "head")) {
        uint32_t count = 10;
        if (*arg && (parse_u32_token(arg, &count, &tail) < 0 || *tail)) out("head: usage [N]");
        else emit_head_text(g_pipe_capture, count, out);
        return;
    }
    if (streq(cmd, "tail")) {
        uint32_t count = 10;
        if (*arg && (parse_u32_token(arg, &count, &tail) < 0 || *tail)) out("tail: usage [N]");
        else emit_tail_text(g_pipe_capture, count, out);
        return;
    }
    if (streq(cmd, "sort")) {
        if (*arg) out("sort: too many operands");
        else emit_sort_text(g_pipe_capture, out);
        return;
    }
    if (streq(cmd, "uniq")) {
        if (*arg) out("uniq: too many operands");
        else emit_uniq_text(g_pipe_capture, out);
        return;
    }
    out("pipe: unsupported command");
}

static void cmd_stat_common(const char *arg, const char *cmd_name, int follow, shell_write_fn out) {
    char path[VFS_MAX_PATH];
    vfs_dirent_t ent;
    char line[160];
    uint32_t pos = 0;
    normalize_path(arg, path);
    if ((follow ? vfs_stat(path, &ent) : vfs_lstat(path, &ent)) < 0) {
        if (streq(cmd_name, "lstat")) out("lstat: no such file");
        else out("stat: no such file");
        return;
    }
    append_str(line, &pos, sizeof(line), ent.name);
    append_str(line, &pos, sizeof(line), " type=");
    print_type(line, &pos, sizeof(line), ent.type);
    append_str(line, &pos, sizeof(line), " size=");
    append_dec(line, &pos, sizeof(line), ent.size);
    append_str(line, &pos, sizeof(line), " mode=");
    append_octal_mode(line, &pos, sizeof(line), ent.mode);
    append_str(line, &pos, sizeof(line), " uid=");
    append_dec(line, &pos, sizeof(line), ent.uid);
    append_str(line, &pos, sizeof(line), " gid=");
    append_dec(line, &pos, sizeof(line), ent.gid);
    append_str(line, &pos, sizeof(line), " mtime=");
    append_dec(line, &pos, sizeof(line), ent.modified_ms);
    out(line);
}

static void cmd_stat(const char *arg, shell_write_fn out) {
    cmd_stat_common(arg, "stat", 1, out);
}

static void cmd_lstat(const char *arg, shell_write_fn out) {
    cmd_stat_common(arg, "lstat", 0, out);
}

static void cmd_chmod(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    const char *tail;
    uint32_t mode;
    if (parse_octal_mode_token(arg, &mode, &rest) < 0) {
        out("chmod: usage MODE PATH");
        return;
    }
    first_token(rest, path_arg, sizeof(path_arg), &tail);
    if (!path_arg[0] || *skip_spaces(tail)) {
        out("chmod: usage MODE PATH");
        return;
    }
    normalize_path(path_arg, path);
    if (vfs_chmod(path, mode) < 0) out("chmod: failed");
}

static void cmd_chown(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    const char *tail;
    uint32_t uid;
    uint32_t gid;
    if (parse_u32_token(arg, &uid, &rest) < 0 ||
        parse_u32_token(rest, &gid, &rest) < 0) {
        out("chown: usage UID GID PATH");
        return;
    }
    first_token(rest, path_arg, sizeof(path_arg), &tail);
    if (!path_arg[0] || *skip_spaces(tail)) {
        out("chown: usage UID GID PATH");
        return;
    }
    normalize_path(path_arg, path);
    if (vfs_chown(path, uid, gid) < 0) out("chown: failed");
}

static void cmd_utime(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    const char *tail;
    uint32_t msec;
    first_token(arg, path_arg, sizeof(path_arg), &rest);
    if (!path_arg[0] || parse_u32_token(rest, &msec, &tail) < 0 || *tail) {
        out("utime: usage PATH MSEC");
        return;
    }
    normalize_path(path_arg, path);
    if (vfs_utime(path, msec, msec) < 0) out("utime: failed");
}

static int parse_access_mask(const char *s, uint32_t *mask) {
    uint32_t m = 0;
    if (!mask) return -1;
    s = skip_spaces(s);
    if (!*s) {
        *mask = 0;
        return 0;
    }
    while (*s && *s != ' ') {
        if (*s == 'r') m |= VFS_ACCESS_READ;
        else if (*s == 'w') m |= VFS_ACCESS_WRITE;
        else if (*s == 'x') m |= VFS_ACCESS_EXEC;
        else return -1;
        s++;
    }
    if (*skip_spaces(s)) return -1;
    *mask = m;
    return 0;
}

static void cmd_access(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    uint32_t mask;
    first_token(arg, path_arg, sizeof(path_arg), &rest);
    if (!path_arg[0] || parse_access_mask(rest, &mask) < 0) {
        out("access: usage PATH [rwx]");
        return;
    }
    normalize_path(path_arg, path);
    out(vfs_access(path, mask) == 0 ? "ok" : "denied");
}

static void cmd_umask(const char *arg, shell_write_fn out) {
    uint32_t mode;
    const char *rest;
    char line[16];
    uint32_t pos = 0;
    if (!*skip_spaces(arg)) {
        append_octal_mode(line, &pos, sizeof(line), process_umask_get(process_current()));
        out(line);
        return;
    }
    if (parse_octal_mode_token(arg, &mode, &rest) < 0 || *rest) {
        out("umask: usage [MODE]");
        return;
    }
    if (process_umask_set(process_current(), mode) < 0) out("umask: failed");
}

static int split_env_assignment(const char *arg,
                                char *name,
                                uint32_t name_max,
                                char *value,
                                uint32_t value_max) {
    const char *p = skip_spaces(arg);
    uint32_t name_len = 0;
    uint32_t value_len = 0;
    if (!name || !name_max || !value || !value_max || !p[0]) return -1;
    while (p[name_len] && p[name_len] != '=' && p[name_len] != ' ') {
        if (name_len + 1U >= name_max) return -1;
        name[name_len] = p[name_len];
        name_len++;
    }
    if (name_len == 0 || p[name_len] != '=') return -1;
    name[name_len] = 0;
    p += name_len + 1U;
    while (p[value_len]) {
        if (value_len + 1U >= value_max) return -1;
        value[value_len] = p[value_len];
        value_len++;
    }
    value[value_len] = 0;
    return 0;
}

static void cmd_env(const char *arg, shell_write_fn out) {
    process_t *proc = process_current();
    uint32_t i;
    if (*skip_spaces(arg)) {
        out("env: too many operands");
        return;
    }
    for (i = 0; i < (uint32_t)process_env_count(proc); i++) {
        const char *entry = process_env_entry(proc, i);
        if (entry) out(entry);
    }
}

static void cmd_getenv(const char *arg, shell_write_fn out) {
    char name[PROCESS_ENV_MAX];
    const char *tail;
    const char *value;
    first_token(arg, name, sizeof(name), &tail);
    if (!name[0] || *tail) {
        out("getenv: usage NAME");
        return;
    }
    value = process_env_get(process_current(), name);
    if (!value) out("getenv: not set");
    else out(value);
}

static void cmd_export(const char *arg, shell_write_fn out) {
    char name[PROCESS_ENV_MAX];
    char value[PROCESS_ENV_MAX];
    if (split_env_assignment(arg, name, sizeof(name), value, sizeof(value)) < 0) {
        out("export: usage NAME=VALUE");
        return;
    }
    if (process_env_set(process_current(), name, value, 1) < 0) out("export: failed");
}

static void cmd_unsetenv(const char *arg, shell_write_fn out) {
    char name[PROCESS_ENV_MAX];
    const char *tail;
    first_token(arg, name, sizeof(name), &tail);
    if (!name[0] || *tail) {
        out("unset: usage NAME");
        return;
    }
    if (process_env_unset(process_current(), name) < 0) out("unset: failed");
}

static void cmd_df(const char *arg, shell_write_fn out) {
    char path[VFS_MAX_PATH];
    vfs_fsinfo_t fs;
    char line[128];
    uint32_t pos = 0;
    if (*skip_spaces(arg)) normalize_path(arg, path);
    else scopy(path, g_cwd, sizeof(path));
    if (vfs_statfs(path, &fs) < 0) {
        out("df: failed");
        return;
    }
    out("FS BS BLOCKS FREE FILES FFREE");
    append_str(line, &pos, sizeof(line), fs.name);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), fs.block_size);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), fs.total_blocks);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), fs.free_blocks);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), fs.total_files);
    append_char(line, &pos, sizeof(line), ' ');
    append_dec(line, &pos, sizeof(line), fs.free_files);
    out(line);
}

static void split_path_text(const char *arg, char *path_arg, uint32_t path_max, const char **text) {
    uint32_t i = 0;
    int overflow = 0;
    arg = skip_spaces(arg);
    while (arg[i] && arg[i] != ' ') {
        if (i + 1 < path_max) path_arg[i] = arg[i];
        else overflow = 1;
        i++;
    }
    if (overflow) path_arg[0] = 0;
    else path_arg[i] = 0;
    while (arg[i] && arg[i] != ' ') i++;
    *text = skip_spaces(arg + i);
}

static void cmd_write_file(const char *arg, int append, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *text;
    uint32_t len;
    int fd;
    int wrote;
    split_path_text(arg, path_arg, sizeof(path_arg), &text);
    if (!path_arg[0]) {
        out("write: missing file");
        return;
    }
    normalize_path(path_arg, path);
    fd = vfs_open(path,
                  VFS_O_CREAT | VFS_O_RDWR |
                  (append ? VFS_O_APPEND : VFS_O_TRUNC));
    if (fd < 0) {
        out("write: open failed");
        return;
    }
    if (append) vfs_seek(fd, 0, VFS_SEEK_END);
    len = slen(text);
    wrote = vfs_write(fd, text, len);
    if (wrote != (int)len || vfs_write(fd, "\n", 1) != 1) {
        out("write: failed");
        vfs_close(fd);
        return;
    }
    vfs_close(fd);
}

static void cmd_truncate_file(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    const char *tail;
    uint32_t size;
    first_token(arg, path_arg, sizeof(path_arg), &rest);
    if (!path_arg[0] || parse_u32_token(rest, &size, &tail) < 0 || *tail) {
        out("truncate: usage PATH SIZE");
        return;
    }
    normalize_path(path_arg, path);
    if (vfs_truncate(path, size) < 0) out("truncate: failed");
}

static void cmd_pread_file(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    char text[128];
    const char *rest;
    const char *tail;
    uint32_t offset;
    uint32_t len;
    int fd;
    int n;
    first_token(arg, path_arg, sizeof(path_arg), &rest);
    if (!path_arg[0] ||
        parse_u32_token(rest, &offset, &rest) < 0 ||
        parse_u32_token(rest, &len, &tail) < 0 ||
        *tail) {
        out("pread: usage PATH OFFSET LEN");
        return;
    }
    if (len >= sizeof(text)) {
        out("pread: length too large");
        return;
    }
    normalize_path(path_arg, path);
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        out("pread: open failed");
        return;
    }
    n = vfs_pread(fd, text, len, offset);
    vfs_close(fd);
    if (n < 0) {
        out("pread: failed");
        return;
    }
    text[n] = 0;
    emit_text_lines(text, out);
}

static void cmd_pwrite_file(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    const char *text;
    uint32_t offset;
    uint32_t len;
    int fd;
    first_token(arg, path_arg, sizeof(path_arg), &rest);
    if (!path_arg[0] || parse_u32_token(rest, &offset, &text) < 0) {
        out("pwrite: usage PATH OFFSET TEXT");
        return;
    }
    normalize_path(path_arg, path);
    fd = vfs_open(path, VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) {
        out("pwrite: open failed");
        return;
    }
    len = slen(text);
    if (vfs_pwrite(fd, text, len, offset) != (int)len) out("pwrite: failed");
    vfs_close(fd);
}

static void cmd_sync_path(const char *arg, int data_only, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *rest;
    int fd;
    int rc;
    first_token(arg, path_arg, sizeof(path_arg), &rest);
    if (!path_arg[0] || *skip_spaces(rest)) {
        out(data_only ? "fdatasync: usage PATH" : "fsync: usage PATH");
        return;
    }
    normalize_path(path_arg, path);
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        out(data_only ? "fdatasync: open failed" : "fsync: open failed");
        return;
    }
    rc = data_only ? vfs_fdatasync(fd) : vfs_fsync(fd);
    vfs_close(fd);
    if (rc < 0) out(data_only ? "fdatasync: failed" : "fsync: failed");
}

static void cmd_random(const char *arg, shell_write_fn out) {
    uint8_t bytes[16];
    char line[40];
    const char *tail;
    uint32_t len;
    uint32_t i;
    uint32_t pos = 0;
    if (parse_u32_token(arg, &len, &tail) < 0 || *tail || len > sizeof(bytes)) {
        out("random: usage LEN");
        return;
    }
    if (vfs_getrandom(bytes, len) != (int)len) {
        out("random: failed");
        return;
    }
    for (i = 0; i < len; i++) append_hex8(line, &pos, sizeof(line), bytes[i]);
    out(line);
}

static void cmd_mv(const char *arg, shell_write_fn out) {
    char old_arg[VFS_MAX_PATH];
    char new_arg[VFS_MAX_PATH];
    char old_path[VFS_MAX_PATH];
    char new_path[VFS_MAX_PATH];
    const char *rest;
    const char *tail;
    first_token(arg, old_arg, sizeof(old_arg), &rest);
    first_token(rest, new_arg, sizeof(new_arg), &tail);
    if (!old_arg[0] || !new_arg[0]) {
        out("mv: missing operand");
        return;
    }
    if (*skip_spaces(tail)) {
        out("mv: too many operands");
        return;
    }
    normalize_path(old_arg, old_path);
    normalize_path(new_arg, new_path);
    if (vfs_rename(old_path, new_path) < 0) out("mv: failed");
}

static void cmd_ln(const char *arg, shell_write_fn out) {
    char opt[8];
    char target_arg[VFS_MAX_PATH];
    char link_arg[VFS_MAX_PATH];
    char target[VFS_MAX_PATH];
    char link_path[VFS_MAX_PATH];
    const char *rest;
    const char *tail;
    first_token(arg, opt, sizeof(opt), &rest);
    first_token(rest, target_arg, sizeof(target_arg), &rest);
    first_token(rest, link_arg, sizeof(link_arg), &tail);
    if (!streq(opt, "-s") || !target_arg[0] || !link_arg[0] || *skip_spaces(tail)) {
        out("ln: usage -s TARGET LINK");
        return;
    }
    normalize_path(target_arg, target);
    normalize_path(link_arg, link_path);
    if (!target[0] || !link_path[0] || vfs_symlink(target, link_path) < 0) out("ln: failed");
}

static void cmd_readlink(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    char target[VFS_MAX_PATH];
    const char *tail;
    int n;
    first_token(arg, path_arg, sizeof(path_arg), &tail);
    if (!path_arg[0] || *skip_spaces(tail)) {
        out("readlink: usage PATH");
        return;
    }
    normalize_path(path_arg, path);
    n = vfs_readlink(path, target, sizeof(target));
    if (n < 0) out("readlink: failed");
    else out(target);
}

static int parse_exec_args(const char *arg,
                           char *path,
                           char argbuf[PROCESS_MAX_ARGS][PROCESS_ARG_MAX],
                           const char *argv[PROCESS_MAX_ARGS],
                           uint32_t *argc_out) {
    char token[VFS_MAX_PATH];
    const char *rest;
    uint32_t argc = 0;
    int r = next_word(arg, token, sizeof(token), &rest);
    if (r <= 0) return r;
    normalize_path(token, path);
    if (!path[0] || slen(path) >= PROCESS_ARG_MAX) return -1;
    scopy(argbuf[argc], path, PROCESS_ARG_MAX);
    argv[argc] = argbuf[argc];
    argc++;
    while (*rest) {
        r = next_word(rest, token, PROCESS_ARG_MAX, &rest);
        if (r < 0) return -1;
        if (r == 0) break;
        if (argc >= PROCESS_MAX_ARGS) return -2;
        scopy(argbuf[argc], token, PROCESS_ARG_MAX);
        argv[argc] = argbuf[argc];
        argc++;
    }
    *argc_out = argc;
    return 1;
}

static void cmd_ps(shell_write_fn out) {
    uint32_t i;
    out("PID PPID PGID SID UID GID STATE ARGC CMD");
    for (i = 0; i < PROCESS_MAX; i++) {
        const process_t *p = process_at(i);
        char line[112];
        uint32_t pos = 0;
        if (!p) continue;
        append_dec(line, &pos, sizeof(line), p->pid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->ppid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->pgid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->sid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->uid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->gid);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), process_state_name(p->state));
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->argc);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), p->name);
        out(line);
    }
}

static void cmd_jobs(shell_write_fn out) {
    uint32_t i;
    int any = 0;
    out("PID PGID STATE CMD");
    for (i = 0; i < PROCESS_MAX; i++) {
        const process_t *p = process_at(i);
        char line[96];
        uint32_t pos = 0;
        if (!p || p->pid == 0) continue;
        any = 1;
        append_dec(line, &pos, sizeof(line), p->pid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), p->pgid);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), process_state_name(p->state));
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), p->name);
        out(line);
    }
    if (!any) out("jobs: none");
}

static void cmd_pgrp(const char *arg, shell_write_fn out) {
    uint32_t pid;
    uint32_t pgid;
    const char *rest;
    const char *tail;
    int current_pgid;
    int current_sid;
    if (parse_u32_token(arg, &pid, &rest) < 0) {
        out("pgrp: usage PID [PGID]");
        return;
    }
    if (!*rest) {
        char line[48];
        uint32_t pos = 0;
        current_pgid = process_getpgid(pid);
        current_sid = process_getsid(pid);
        if (current_pgid < 0 || current_sid < 0) {
            out("pgrp: bad pid");
            return;
        }
        append_dec(line, &pos, sizeof(line), pid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), (uint32_t)current_pgid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), (uint32_t)current_sid);
        out(line);
        return;
    }
    if (parse_u32_token(rest, &pgid, &tail) < 0 || *tail) {
        out("pgrp: usage PID [PGID]");
        return;
    }
    if (process_setpgid(pid, pgid) < 0) out("pgrp: failed");
}

static int proc_leaf_allowed(const char *leaf) {
    return streq(leaf, "status") ||
           streq(leaf, "stat") ||
           streq(leaf, "cmdline") ||
           streq(leaf, "children") ||
           streq(leaf, "exe") ||
           streq(leaf, "cwd") ||
           streq(leaf, "fds") ||
           streq(leaf, "maps") ||
           streq(leaf, "environ");
}

static void cmd_proc(const char *arg, shell_write_fn out) {
    uint32_t pid;
    const char *rest;
    const char *tail;
    char token[VFS_MAX_PATH];
    char leaf[16];
    char path[VFS_MAX_PATH];
    uint32_t pos = 0;
    first_token(arg, token, sizeof(token), &rest);
    if (streq(token, "self")) {
        first_token(rest, leaf, sizeof(leaf), &tail);
        if (!leaf[0]) scopy(leaf, "status", sizeof(leaf));
        if (*tail || !proc_leaf_allowed(leaf)) {
            out("proc: usage PID [status|stat|cmdline|children|exe|cwd|fds|maps|environ]");
            return;
        }
        append_str(path, &pos, sizeof(path), "/proc/self/");
        append_str(path, &pos, sizeof(path), leaf);
        cmd_cat(path, out);
        return;
    }
    if (parse_u32_token(arg, &pid, &rest) < 0) {
        out("proc: bad pid");
        return;
    }
    first_token(rest, leaf, sizeof(leaf), &tail);
    if (!leaf[0]) scopy(leaf, "status", sizeof(leaf));
    if (*tail || !proc_leaf_allowed(leaf)) {
        out("proc: usage PID [status|stat|cmdline|children|exe|cwd|fds|maps|environ]");
        return;
    }
    append_str(path, &pos, sizeof(path), "/proc/");
    append_dec(path, &pos, sizeof(path), pid);
    append_char(path, &pos, sizeof(path), '/');
    append_str(path, &pos, sizeof(path), leaf);
    cmd_cat(path, out);
}

static void append_hex32(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    int shift;
    append_str(buf, pos, max, "0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        static const char h[] = "0123456789ABCDEF";
        append_char(buf, pos, max, h[(v >> shift) & 0xF]);
    }
}

static uint32_t sat_mul_u32(uint32_t a, uint32_t b) {
    if (a != 0 && b > 0xFFFFFFFFU / a) return 0xFFFFFFFFU;
    return a * b;
}

static void cmd_tasks(shell_write_fn out) {
    uint32_t i;
    out("TID PID STATE PRIO CR3 NAME");
    for (i = 0; i < task_table_size(); i++) {
        const task_t *t = task_at(i);
        char line[112];
        uint32_t pos = 0;
        if (!t) continue;
        append_dec(line, &pos, sizeof(line), t->id);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), t->process_id);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), task_state_name(t->state));
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), t->priority);
        append_char(line, &pos, sizeof(line), ' ');
        append_hex32(line, &pos, sizeof(line), t->regs.cr3);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), t->name ? t->name : "-");
        out(line);
    }
}

static void cmd_prio(const char *arg, shell_write_fn out) {
    uint32_t tid;
    uint32_t prio;
    const char *rest;
    const char *tail;
    const task_t *task;
    if (parse_u32_token(arg, &tid, &rest) < 0) {
        out("prio: usage TID [VALUE]");
        return;
    }
    task = task_at(tid);
    if (!task) {
        out("prio: bad tid");
        return;
    }
    if (!*rest) {
        char line[32];
        uint32_t pos = 0;
        append_dec(line, &pos, sizeof(line), tid);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), task->priority);
        out(line);
        return;
    }
    if (parse_u32_token(rest, &prio, &tail) < 0 || *tail) {
        out("prio: usage TID [VALUE]");
        return;
    }
    if (task_set_priority((int)tid, prio) < 0) out("prio: failed");
}

static void cmd_stty(const char *arg, shell_write_fn out) {
    uint32_t mode = tty_get_mode();
    const char *rest = arg;
    char word[16];
    int r;
    if (!*skip_spaces(arg)) {
        char line[48];
        uint32_t pos = 0;
        append_str(line, &pos, sizeof(line), "echo ");
        append_str(line, &pos, sizeof(line), (mode & TTY_MODE_ECHO) ? "on " : "off ");
        append_str(line, &pos, sizeof(line), "canon ");
        append_str(line, &pos, sizeof(line), (mode & TTY_MODE_CANON) ? "on" : "off");
        out(line);
        return;
    }
    if (streq(skip_spaces(arg), "size")) {
        char line[16];
        uint32_t pos = 0;
        append_dec(line, &pos, sizeof(line), TTY_ROWS);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), TTY_COLUMNS);
        out(line);
        return;
    }
    while ((r = next_word(rest, word, sizeof(word), &rest)) > 0) {
        if (streq(word, "raw")) mode = 0;
        else if (streq(word, "sane")) mode = TTY_MODE_ECHO | TTY_MODE_CANON;
        else if (streq(word, "echo")) mode |= TTY_MODE_ECHO;
        else if (streq(word, "-echo")) mode &= ~TTY_MODE_ECHO;
        else if (streq(word, "canon")) mode |= TTY_MODE_CANON;
        else if (streq(word, "-canon")) mode &= ~TTY_MODE_CANON;
        else {
            out("stty: usage [size|raw|sane|echo|-echo|canon|-canon]");
            return;
        }
    }
    if (r < 0 || tty_set_mode(mode) < 0) out("stty: failed");
}

static void cmd_wait(const char *arg, shell_write_fn out) {
    uint32_t pid;
    const char *rest;
    uint32_t reaped = 0;
    if (*skip_spaces(arg)) {
        int code = 0;
        int r;
        if (parse_u32_token(arg, &pid, &rest) < 0 || *rest) {
            out("wait: bad pid");
            return;
        }
        (void)rest;
        r = process_wait(pid, &code);
        if (r == 1) {
            char line[64];
            uint32_t pos = 0;
            append_str(line, &pos, sizeof(line), "pid ");
            append_dec(line, &pos, sizeof(line), pid);
            append_str(line, &pos, sizeof(line), " exit ");
            append_int(line, &pos, sizeof(line), code);
            out(line);
        } else if (r == 0) {
            out("wait: not exited");
        } else {
            out("wait: no such pid");
        }
        return;
    }
    (void)pid;
    while (1) {
        int code = 0;
        int zombie_pid = process_wait_any(&code);
        if (zombie_pid <= 0) break;
        {
            char line[64];
            uint32_t pos = 0;
            append_str(line, &pos, sizeof(line), "reaped pid ");
            append_dec(line, &pos, sizeof(line), (uint32_t)zombie_pid);
            append_str(line, &pos, sizeof(line), " exit ");
            append_int(line, &pos, sizeof(line), code);
            out(line);
            reaped++;
        }
    }
    if (reaped == 0) out("wait: no zombies");
}

static void cmd_kill(const char *arg, shell_write_fn out) {
    uint32_t pid;
    uint32_t sig = 9;
    const char *rest;
    int group = 0;
    char token[24];
    first_token(arg, token, sizeof(token), &rest);
    if (token[0] == '-' && token[1]) {
        const char *sig_rest;
        if (parse_u32(token + 1, &sig, &sig_rest) < 0 ||
            *sig_rest ||
            sig == 0 ||
            sig > 31U) {
            out("kill: bad signal");
            return;
        }
        rest = skip_spaces(rest);
        if (rest[0] == '-' && rest[1]) {
            group = 1;
            if (parse_u32(rest + 1, &pid, &rest) < 0 || *rest) {
                out("kill: bad pid");
                return;
            }
        } else if (parse_u32(rest, &pid, &rest) < 0 || *rest) {
            out("kill: bad pid");
            return;
        }
    } else if (parse_u32(arg, &pid, &rest) < 0 || *rest) {
        out("kill: bad pid");
        return;
    }
    if ((group ? process_kill_group(pid, -(int)sig) : process_kill(pid, -(int)sig)) == 0)
        out("kill: terminated");
    else out("kill: failed");
}

static void cmd_lspci(shell_write_fn out) {
    uint32_t i;
    if (pci_count() == 0) {
        out("no pci devices");
        return;
    }
    for (i = 0; i < pci_count(); i++) {
        const pci_device_t *d = pci_at(i);
        char line[96];
        uint32_t pos = 0;
        append_hex8(line, &pos, sizeof(line), d->bus);
        append_char(line, &pos, sizeof(line), ':');
        append_hex8(line, &pos, sizeof(line), d->slot);
        append_char(line, &pos, sizeof(line), '.');
        append_dec(line, &pos, sizeof(line), d->function);
        append_char(line, &pos, sizeof(line), ' ');
        append_hex16(line, &pos, sizeof(line), d->vendor_id);
        append_char(line, &pos, sizeof(line), ':');
        append_hex16(line, &pos, sizeof(line), d->device_id);
        append_str(line, &pos, sizeof(line), " class ");
        append_hex8(line, &pos, sizeof(line), d->class_code);
        append_char(line, &pos, sizeof(line), '/');
        append_hex8(line, &pos, sizeof(line), d->subclass);
        out(line);
    }
}

static void cmd_lsblk(shell_write_fn out) {
    uint32_t i;
    out("NAME SECTORS SIZE");
    for (i = 0; i < block_count(); i++) {
        const block_device_t *b = block_at(i);
        char line[96];
        uint32_t pos = 0;
        append_str(line, &pos, sizeof(line), b->name);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), b->sector_count);
        append_char(line, &pos, sizeof(line), ' ');
        append_dec(line, &pos, sizeof(line), sat_mul_u32(b->sector_count, b->sector_size) / 1024);
        append_str(line, &pos, sizeof(line), "K");
        out(line);
    }
}

static void cmd_blkread(const char *arg, shell_write_fn out) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t dev;
    uint32_t lba = 0;
    const char *rest;
    uint32_t row;
    if (parse_block_ref(arg, &dev, &rest) < 0) {
        out("blkread: unknown device");
        return;
    }
    if (*rest && parse_u32(rest, &lba, &rest) < 0) {
        out("blkread: bad lba");
        return;
    }
    if (*rest) {
        out("blkread: bad lba");
        return;
    }
    if (block_read(dev, lba, sector, 1) != 1) {
        out("blkread: read failed");
        return;
    }
    for (row = 0; row < 4; row++) {
        char line[96];
        uint32_t pos = 0;
        uint32_t i;
        append_dec(line, &pos, sizeof(line), row * 16);
        append_str(line, &pos, sizeof(line), ": ");
        for (i = 0; i < 16; i++) {
            append_hex8(line, &pos, sizeof(line), sector[row * 16 + i]);
            append_char(line, &pos, sizeof(line), ' ');
        }
        out(line);
    }
}

static void cmd_blkwrite(const char *arg, shell_write_fn out) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t dev;
    uint32_t lba = 0;
    const char *rest;
    uint32_t i;
    if (parse_block_ref(arg, &dev, &rest) < 0) {
        out("blkwrite: unknown device");
        return;
    }
    if (parse_u32(rest, &lba, &rest) < 0) {
        out("blkwrite: bad lba");
        return;
    }
    for (i = 0; i < sizeof(sector); i++) sector[i] = 0;
    for (i = 0; rest[i] && i + 1 < sizeof(sector); i++) sector[i] = (uint8_t)rest[i];
    sector[i] = '\n';
    if (block_write(dev, lba, sector, 1) != 1) {
        out("blkwrite: write failed");
        return;
    }
    out("blkwrite: ok");
}

static void cmd_mkfs(const char *arg, shell_write_fn out) {
    uint32_t dev;
    const char *rest;
    if (parse_block_ref(arg, &dev, &rest) < 0) {
        out("mkfs: unknown block device");
        return;
    }
    if (*rest) {
        out("mkfs: too many operands");
        return;
    }
    out(simplefs_format(dev) == 0 ? "mkfs: simplefs formatted" : "mkfs: failed");
}

static void cmd_mountfs(const char *arg, shell_write_fn out) {
    uint32_t dev;
    const char *rest;
    char mount_arg[VFS_MAX_PATH];
    char mount_path[VFS_MAX_PATH];
    const char *tail;
    char line[96];
    uint32_t pos = 0;
    vfs_dirent_t ent;
    if (parse_block_ref(arg, &dev, &rest) < 0) {
        out("mountfs: unknown block device");
        return;
    }
    if (*rest) {
        first_token(rest, mount_arg, sizeof(mount_arg), &tail);
        if (*tail) {
            out("mountfs: too many operands");
            return;
        }
        normalize_path(mount_arg, mount_path);
    } else {
        scopy(mount_path, "/disk", sizeof(mount_path));
    }
    if (!mount_path[0] || mount_path[0] != '/') {
        out("mountfs: bad mountpoint");
        return;
    }
    if (vfs_stat(mount_path, &ent) < 0) {
        if (vfs_mkdir(mount_path) < 0) {
            out("mountfs: mountpoint failed");
            return;
        }
    } else if (ent.type != VFS_NODE_DIR) {
        out("mountfs: mountpoint failed");
        return;
    }
    if (simplefs_mount(dev, mount_path) < 0) {
        out("mountfs: failed");
        return;
    }
    append_str(line, &pos, sizeof(line), "mountfs: mounted on ");
    append_str(line, &pos, sizeof(line), mount_path);
    out(line);
}

static void cmd_umount(const char *arg, shell_write_fn out) {
    char path_arg[VFS_MAX_PATH];
    char path[VFS_MAX_PATH];
    const char *tail;
    first_token(arg, path_arg, sizeof(path_arg), &tail);
    if (!path_arg[0] || *skip_spaces(tail)) {
        out("umount: usage PATH");
        return;
    }
    normalize_path(path_arg, path);
    if (!simplefs_mounted() || !streq(path, simplefs_mount_path())) {
        out("umount: not mounted");
        return;
    }
    if (simplefs_unmount() < 0) out("umount: failed");
}

static void append_ipv4(char *line, uint32_t *pos, uint32_t max, uint32_t ip) {
    append_dec(line, pos, max, (ip >> 24) & 0xFF);
    append_char(line, pos, max, '.');
    append_dec(line, pos, max, (ip >> 16) & 0xFF);
    append_char(line, pos, max, '.');
    append_dec(line, pos, max, (ip >> 8) & 0xFF);
    append_char(line, pos, max, '.');
    append_dec(line, pos, max, ip & 0xFF);
}

static int parse_ipv4(const char *arg, uint32_t *out) {
    uint32_t oct[4];
    uint32_t i;
    const char *p = skip_spaces(arg);
    for (i = 0; i < 4; i++) {
        if (parse_u32(p, &oct[i], &p) < 0 || oct[i] > 255) return -1;
        if (i < 3) {
            if (*p != '.') return -1;
            p++;
        }
    }
    if (*skip_spaces(p) != 0) return -1;
    *out = (oct[0] << 24) | (oct[1] << 16) | (oct[2] << 8) | oct[3];
    return 0;
}

static int parse_ipv4_word(const char *arg, uint32_t *out, const char **rest) {
    char word[32];
    int r = next_word(arg, word, sizeof(word), rest);
    if (r <= 0) return -1;
    return parse_ipv4(word, out);
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int parse_mac(const char *arg, uint8_t mac[6]) {
    uint32_t i;
    const char *p = arg;
    if (!arg || !mac) return -1;
    for (i = 0; i < 6; i++) {
        int hi = hex_value(p[0]);
        int lo = hex_value(p[1]);
        if (hi < 0 || lo < 0) return -1;
        mac[i] = (uint8_t)((hi << 4) | lo);
        p += 2;
        if (i < 5) {
            if (*p != ':') return -1;
            p++;
        }
    }
    return *p == 0 ? 0 : -1;
}

static int parse_mac_word(const char *arg, uint8_t mac[6], const char **rest) {
    char word[24];
    int r = next_word(arg, word, sizeof(word), rest);
    if (r <= 0) return -1;
    return parse_mac(word, mac);
}

static void print_netif(uint32_t i, shell_write_fn out) {
    const netif_t *n = netif_at(i);
    char line[96];
    uint32_t pos = 0;
    if (!n) return;
    append_str(line, &pos, sizeof(line), n->name);
    append_str(line, &pos, sizeof(line), " ether ");
    append_mac(line, &pos, sizeof(line), n->mac);
    append_str(line, &pos, sizeof(line), " inet ");
    append_ipv4(line, &pos, sizeof(line), n->ipv4);
    append_str(line, &pos, sizeof(line), n->up ? " up" : " down");
    append_str(line, &pos, sizeof(line), " mtu=");
    append_dec(line, &pos, sizeof(line), n->mtu);
    append_str(line, &pos, sizeof(line), " tx=");
    append_dec(line, &pos, sizeof(line), n->tx_packets);
    append_str(line, &pos, sizeof(line), " rx=");
    append_dec(line, &pos, sizeof(line), n->rx_packets);
    out(line);
}

static void cmd_ifconfig(const char *arg, shell_write_fn out) {
    uint32_t i;
    const char *rest;
    uint32_t ip;
    if (!*skip_spaces(arg)) {
        for (i = 0; i < netif_count(); i++) print_netif(i, out);
        return;
    }
    if (parse_netif_ref(arg, &i, &rest) < 0) {
        out("ifconfig: unknown interface");
        return;
    }
    if (!*rest) {
        print_netif(i, out);
    } else if (streq(rest, "up")) {
        out(netif_set_up(i, 1) == 0 ? "ifconfig: up" : "ifconfig: failed");
    } else if (streq(rest, "down")) {
        out(netif_set_up(i, 0) == 0 ? "ifconfig: down" : "ifconfig: failed");
    } else if (rest[0] == 'a' && rest[1] == 'd' && rest[2] == 'd' &&
               rest[3] == 'r' && rest[4] == ' ') {
        if (parse_ipv4(rest + 5, &ip) < 0) out("ifconfig: bad address");
        else out(netif_set_ipv4(i, ip) == 0 ? "ifconfig: address set" : "ifconfig: failed");
    } else {
        out("ifconfig: usage IFACE [up|down|addr A.B.C.D]");
    }
}

static void cmd_route(const char *arg, shell_write_fn out) {
    char op[16];
    const char *rest;
    uint32_t dest;
    uint32_t mask;
    uint32_t gateway;
    uint32_t ifindex;
    if (!*skip_spaces(arg)) {
        cmd_cat("/proc/routes", out);
        return;
    }
    first_token(arg, op, sizeof(op), &rest);
    if (!streq(op, "add") && !streq(op, "del")) {
        out("route: usage add|del DEST MASK GATEWAY IFACE");
        return;
    }
    if (parse_ipv4_word(rest, &dest, &rest) < 0 ||
        parse_ipv4_word(rest, &mask, &rest) < 0 ||
        parse_ipv4_word(rest, &gateway, &rest) < 0 ||
        parse_netif_ref(rest, &ifindex, &rest) < 0 ||
        *rest) {
        out(streq(op, "add") ?
            "route: usage add DEST MASK GATEWAY IFACE" :
            "route: usage del DEST MASK GATEWAY IFACE");
        return;
    }
    if (streq(op, "add")) out(net_route_add(dest, mask, gateway, ifindex) == 0 ? "route: added" : "route: failed");
    else out(net_route_del(dest, mask, gateway, ifindex) == 0 ? "route: deleted" : "route: failed");
}

static void cmd_ip(const char *arg, shell_write_fn out) {
    char sub[16];
    const char *tail;
    first_token(arg, sub, sizeof(sub), &tail);
    if (!sub[0] || (streq(sub, "addr") && !*tail) || (streq(sub, "a") && !*tail)) {
        uint32_t i;
        for (i = 0; i < netif_count(); i++) {
            const netif_t *n = netif_at(i);
            char line[96];
            uint32_t pos = 0;
            if (!n) continue;
            append_dec(line, &pos, sizeof(line), i);
            append_str(line, &pos, sizeof(line), ": ");
            append_str(line, &pos, sizeof(line), n->name);
            append_str(line, &pos, sizeof(line), n->up ? ": up" : ": down");
            out(line);
            pos = 0;
            line[0] = 0;
            append_str(line, &pos, sizeof(line), "    inet ");
            append_ipv4(line, &pos, sizeof(line), n->ipv4);
            out(line);
            pos = 0;
            line[0] = 0;
            append_str(line, &pos, sizeof(line), "    link/ether ");
            append_mac(line, &pos, sizeof(line), n->mac);
            out(line);
        }
        return;
    }
    if (streq(sub, "route")) {
        cmd_route(tail, out);
        return;
    }
    out("ip: usage [addr|a|route]");
}

static void cmd_arp(const char *arg, shell_write_fn out) {
    char op[16];
    const char *rest;
    uint32_t ifindex;
    uint32_t ip;
    uint8_t mac[6];
    if (!*skip_spaces(arg)) {
        cmd_cat("/proc/arp", out);
        return;
    }
    first_token(arg, op, sizeof(op), &rest);
    if (streq(op, "add")) {
        if (parse_netif_ref(rest, &ifindex, &rest) < 0 ||
            parse_ipv4_word(rest, &ip, &rest) < 0 ||
            parse_mac_word(rest, mac, &rest) < 0 ||
            *rest) {
            out("arp: usage add IFACE IP MAC");
            return;
        }
        out(net_arp_learn(ifindex, ip, mac) == 0 ? "arp: added" : "arp: failed");
        return;
    }
    if (streq(op, "del")) {
        if (parse_netif_ref(rest, &ifindex, &rest) < 0 ||
            parse_ipv4_word(rest, &ip, &rest) < 0 ||
            *rest) {
            out("arp: usage del IFACE IP");
            return;
        }
        out(net_arp_delete(ifindex, ip) == 0 ? "arp: deleted" : "arp: failed");
        return;
    }
    out("arp: usage add IFACE IP MAC | del IFACE IP");
}

static void cmd_netstat(const char *arg, shell_write_fn out) {
    char opt[8];
    const char *tail;
    first_token(arg, opt, sizeof(opt), &tail);
    if (!opt[0]) {
        cmd_cat("/proc/net/dev", out);
        return;
    }
    if (*tail) {
        out("netstat: usage [-i|-r|-u|-s|-a]");
        return;
    }
    if (streq(opt, "-i")) {
        cmd_cat("/proc/net/dev", out);
    } else if (streq(opt, "-r")) {
        cmd_cat("/proc/net/route", out);
    } else if (streq(opt, "-u")) {
        cmd_cat("/proc/net/udp", out);
    } else if (streq(opt, "-s")) {
        cmd_cat("/proc/net/snmp", out);
    } else if (streq(opt, "-a")) {
        cmd_cat("/proc/net/dev", out);
        cmd_cat("/proc/net/route", out);
        cmd_cat("/proc/net/arp", out);
        cmd_cat("/proc/net/udp", out);
        cmd_cat("/proc/net/snmp", out);
    } else {
        out("netstat: usage [-i|-r|-u|-s|-a]");
    }
}

static void cmd_udp(const char *arg, shell_write_fn out) {
    char first[32];
    uint32_t port;
    uint32_t len;
    uint32_t dst = 0x7F000001U;
    uint32_t ifindex = 0;
    uint16_t src_port;
    uint16_t from_port = 0;
    uint32_t from_ip = 0;
    const char *rest;
    const char *payload;
    char buf[72];
    char line[96];
    uint32_t pos = 0;
    int n;
    if (next_word(arg, first, sizeof(first), &rest) <= 0) {
        out("udp: usage [IP] PORT MESSAGE");
        return;
    }
    if (text_contains(first, ".")) {
        if (parse_ipv4(first, &dst) < 0 ||
            parse_u32_token(rest, &port, &payload) < 0) {
            out("udp: usage [IP] PORT MESSAGE");
            return;
        }
    } else {
        const char *tail;
        if (parse_u32(first, &port, &tail) < 0 || *tail) {
            out("udp: usage [IP] PORT MESSAGE");
            return;
        }
        payload = rest;
    }
    if (port == 0 || port > 65535U || !*payload) {
        out("udp: usage [IP] PORT MESSAGE");
        return;
    }
    if (net_route_lookup4(dst, &ifindex, 0) < 0) {
        out("udp: no route");
        return;
    }
    len = slen(payload);
    if (len > NET_UDP_PAYLOAD_MAX) {
        out("udp: message too long");
        return;
    }
    src_port = (uint16_t)(port == 65535U ? 65534U : port + 1U);
    if (net_udp_send4(ifindex, dst, src_port, (uint16_t)port, payload, len) != (int)len) {
        out("udp: send failed");
        return;
    }
    n = net_udp_recv4((uint16_t)port, buf, sizeof(buf) - 1, &from_ip, &from_port);
    if (n <= 0) {
        out("udp: no packet");
        return;
    }
    buf[n] = 0;
    append_str(line, &pos, sizeof(line), "udp ");
    append_dec(line, &pos, sizeof(line), from_port);
    append_str(line, &pos, sizeof(line), ": ");
    append_str(line, &pos, sizeof(line), buf);
    out(line);
}

static void cmd_drivers(shell_write_fn out) {
    uint32_t i;
    for (i = 0; i < driver_count(); i++) {
        const driver_t *d = driver_at(i);
        char line[96];
        uint32_t pos = 0;
        append_dec(line, &pos, sizeof(line), i);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), driver_bus_name(d->bus));
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), d->name);
        append_str(line, &pos, sizeof(line), d->loaded ? " loaded" : " off");
        out(line);
    }
}

static void cmd_sysinfo(shell_write_fn out) {
    char line[96];
    uint32_t pos = 0;
    append_str(line, &pos, sizeof(line), "ticks=");
    append_dec(line, &pos, sizeof(line), timer_ticks());
    append_str(line, &pos, sizeof(line), " free_pages=");
    append_dec(line, &pos, sizeof(line), pmm_free_pages());
    append_str(line, &pos, sizeof(line), " managed_pages=");
    append_dec(line, &pos, sizeof(line), pmm_managed_pages());
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "heap_free=");
    append_dec(line, &pos, sizeof(line), (uint32_t)heap_free_bytes());
    append_str(line, &pos, sizeof(line), " heap_used=");
    append_dec(line, &pos, sizeof(line), (uint32_t)heap_used_bytes());
    append_str(line, &pos, sizeof(line), " heap_blocks=");
    append_dec(line, &pos, sizeof(line), heap_block_count());
    append_str(line, &pos, sizeof(line), " heap_free_blocks=");
    append_dec(line, &pos, sizeof(line), heap_free_block_count());
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "proc=");
    append_dec(line, &pos, sizeof(line), process_count());
    append_str(line, &pos, sizeof(line), " irqsw=");
    append_dec(line, &pos, sizeof(line), scheduler_irq_switches());
    append_str(line, &pos, sizeof(line), " coopsw=");
    append_dec(line, &pos, sizeof(line), scheduler_coop_switches());
    append_str(line, &pos, sizeof(line), " quantum=");
    append_dec(line, &pos, sizeof(line), scheduler_quantum_ticks());
    append_str(line, &pos, sizeof(line), " pci=");
    append_dec(line, &pos, sizeof(line), pci_count());
    append_str(line, &pos, sizeof(line), " block=");
    append_dec(line, &pos, sizeof(line), block_count());
    append_str(line, &pos, sizeof(line), " net=");
    append_dec(line, &pos, sizeof(line), netif_count());
    out(line);
}

static void cmd_free(shell_write_fn out) {
    runtime_system_info_t sys;
    uint32_t used;
    char value[24];
    char line[96];
    uint32_t pos;
    runtime_get_system_info(&sys);
    used = sys.pmm_total_bytes >= sys.pmm_free_bytes ? sys.pmm_total_bytes - sys.pmm_free_bytes : 0;
    out("Memory:");
    runtime_format_bytes(sys.pmm_total_bytes, value, sizeof(value));
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  total: ");
    append_str(line, &pos, sizeof(line), value);
    out(line);
    runtime_format_bytes(used, value, sizeof(value));
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  used:  ");
    append_str(line, &pos, sizeof(line), value);
    out(line);
    runtime_format_bytes(sys.pmm_free_bytes, value, sizeof(value));
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  free:  ");
    append_str(line, &pos, sizeof(line), value);
    out(line);
}

static void cmd_sw_vers(shell_write_fn out) {
    runtime_system_info_t sys;
    char line[96];
    uint32_t pos;
    runtime_get_system_info(&sys);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "ProductName: ");
    append_str(line, &pos, sizeof(line), sys.sysname);
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "ProductVersion: ");
    append_str(line, &pos, sizeof(line), sys.release);
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "BuildVersion: ");
    append_str(line, &pos, sizeof(line), sys.version);
    out(line);
}

static void cmd_system_profiler(const char *arg, shell_write_fn out) {
    runtime_system_info_t sys;
    runtime_storage_info_t storage;
    const netif_t *n;
    char line[96];
    char value[24];
    uint32_t pos;
    (void)arg;
    runtime_get_system_info(&sys);
    n = runtime_primary_netif();
    out("Hardware:");
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  Model Name: ");
    append_str(line, &pos, sizeof(line), sys.sysname);
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  Model Identifier: ");
    append_str(line, &pos, sizeof(line), sys.machine);
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  Processor: ");
    append_str(line, &pos, sizeof(line), sys.cpu_model);
    out(line);
    runtime_format_bytes(sys.pmm_total_bytes, value, sizeof(value));
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  Memory: ");
    append_str(line, &pos, sizeof(line), value);
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  Display: ");
    append_dec(line, &pos, sizeof(line), sys.display_width);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), sys.display_height);
    append_char(line, &pos, sizeof(line), 'x');
    append_dec(line, &pos, sizeof(line), sys.display_bpp);
    out(line);
    if (runtime_get_storage_info("/", &storage) == 0) {
        runtime_format_bytes(storage.total_bytes, value, sizeof(value));
        pos = 0;
        line[0] = 0;
        append_str(line, &pos, sizeof(line), "  Storage: ");
        append_str(line, &pos, sizeof(line), storage.name);
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), value);
        out(line);
    }
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "  Network: ");
    append_str(line, &pos, sizeof(line), n ? n->name : "none");
    out(line);
}

static void cmd_uname(const char *arg, shell_write_fn out) {
    char opt[8];
    const char *tail;
    first_token(arg, opt, sizeof(opt), &tail);
    if (!opt[0]) {
        char line[64];
        uint32_t pos = 0;
        append_str(line, &pos, sizeof(line), uts_sysname());
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), uts_machine());
        out(line);
        return;
    }
    if (streq(opt, "-a") && !*tail) {
        char line[128];
        uint32_t pos = 0;
        append_str(line, &pos, sizeof(line), uts_sysname());
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), uts_nodename());
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), uts_release());
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), uts_version());
        append_char(line, &pos, sizeof(line), ' ');
        append_str(line, &pos, sizeof(line), uts_machine());
        out(line);
        return;
    }
    out("uname: usage [-a]");
}

static void cmd_hostname(const char *arg, shell_write_fn out) {
    char name[UTS_FIELD_MAX];
    const char *tail;
    first_token(arg, name, sizeof(name), &tail);
    if (!name[0]) {
        out(uts_nodename());
        return;
    }
    if (*tail) {
        out("hostname: usage [NAME]");
        return;
    }
    if (uts_set_nodename(name) < 0) {
        out("hostname: invalid name");
        return;
    }
    (void)process_env_set(process_current(), "HOSTNAME", uts_nodename(), 1);
}

static void cmd_limits(shell_write_fn out) {
    char line[128];
    uint32_t pos = 0;
    append_str(line, &pos, sizeof(line), "page=");
    append_dec(line, &pos, sizeof(line), PAGE_SIZE);
    append_str(line, &pos, sizeof(line), " process=");
    append_dec(line, &pos, sizeof(line), PROCESS_MAX);
    append_str(line, &pos, sizeof(line), " task=");
    append_dec(line, &pos, sizeof(line), MAX_TASKS);
    append_str(line, &pos, sizeof(line), " fd=");
    append_dec(line, &pos, sizeof(line), PROCESS_MAX_FDS);
    out(line);
    pos = 0;
    line[0] = 0;
    append_str(line, &pos, sizeof(line), "path=");
    append_dec(line, &pos, sizeof(line), VFS_MAX_PATH);
    append_str(line, &pos, sizeof(line), " name=");
    append_dec(line, &pos, sizeof(line), VFS_MAX_NAME);
    append_str(line, &pos, sizeof(line), " netif=");
    append_dec(line, &pos, sizeof(line), NET_MAX_IFACES);
    append_str(line, &pos, sizeof(line), " block=");
    append_dec(line, &pos, sizeof(line), BLOCK_MAX_DEVICES);
    append_str(line, &pos, sizeof(line), " driver=");
    append_dec(line, &pos, sizeof(line), DRIVER_MAX);
    append_str(line, &pos, sizeof(line), " hz=");
    append_dec(line, &pos, sizeof(line), timer_hz());
    append_str(line, &pos, sizeof(line), " uts=");
    append_dec(line, &pos, sizeof(line), UTS_FIELD_MAX);
    out(line);
}

static void cmd_dmesg(shell_write_fn out) {
    uint32_t pos = 0;
    char line[96];
    uint32_t line_pos = 0;
    char c;
    int seen = 0;
    while (debug_log_read(&pos, &c, 1) == 1) {
        seen = 1;
        if (c == '\r') continue;
        if (c == '\n') {
            line[line_pos] = 0;
            if (line_pos) out(line);
            line_pos = 0;
        } else if (line_pos + 1 < sizeof(line)) {
            line[line_pos++] = c;
        }
    }
    if (!seen) {
        out("dmesg: empty");
    } else if (line_pos) {
        line[line_pos] = 0;
        out(line);
    }
}

void shell_init(void) {
    scopy(g_cwd, "/home/root", sizeof(g_cwd));
    debug_puts("[shell] initialized\n");
}

const char *shell_cwd(void) {
    return g_cwd;
}

int shell_execute_line(const char *line, shell_write_fn out) {
    char cmd[24];
    const char *arg;
    char path[VFS_MAX_PATH];
    vfs_dirent_t ent;
    if (!out) return 0;
    {
        char redirected_line[160];
        char redirect_target[VFS_MAX_PATH];
        char redirect_path[VFS_MAX_PATH];
        int redirect_append = 0;
        int redirect = split_redirection_line(line ? line : "",
                                              redirected_line,
                                              sizeof(redirected_line),
                                              redirect_target,
                                              sizeof(redirect_target),
                                              &redirect_append);
        if (redirect < 0) {
            out("redirect: bad syntax");
            return 1;
        }
        if (redirect > 0) {
            int fd;
            int old_fd;
            int old_failed;
            int result;
            normalize_path(redirect_target, redirect_path);
            if (!redirect_path[0]) {
                out("redirect: bad target");
                return 1;
            }
            fd = vfs_open(redirect_path,
                          VFS_O_CREAT | VFS_O_WRONLY |
                          (redirect_append ? VFS_O_APPEND : VFS_O_TRUNC));
            if (fd < 0) {
                out("redirect: open failed");
                return 1;
            }
            if (redirect_append) (void)vfs_seek(fd, 0, VFS_SEEK_END);
            old_fd = g_redirect_fd;
            old_failed = g_redirect_failed;
            g_redirect_fd = fd;
            g_redirect_failed = 0;
            result = shell_execute_line(redirected_line, redirect_write_line);
            vfs_close(fd);
            if (g_redirect_failed) out("redirect: write failed");
            g_redirect_fd = old_fd;
            g_redirect_failed = old_failed;
            return result;
        }
    }
    {
        char pipe_left[160];
        char pipe_right[96];
        int pipe = split_pipeline_line(line ? line : "",
                                       pipe_left,
                                       sizeof(pipe_left),
                                       pipe_right,
                                       sizeof(pipe_right));
        if (pipe < 0) {
            out("pipe: bad syntax");
            return 1;
        }
        if (pipe > 0) {
            int result;
            pipe_capture_reset();
            result = shell_execute_line(pipe_left, pipe_capture_line);
            if (!result) {
                out("pipe: command not found");
                return 1;
            }
            if (g_pipe_capture_failed) {
                out("pipe: buffer full");
                return 1;
            }
            run_pipeline_right(pipe_right, out);
            return 1;
        }
    }
    first_token(line ? line : "", cmd, sizeof(cmd), &arg);
    if (cmd[0] == 0) return 1;
    if (streq(cmd, "help")) {
        out("Commands: ls cat cd pwd stat lstat df echo");
        out("touch mkdir rmdir rm mv ln readlink write append pread pwrite truncate chmod chown utime fsync fdatasync sync");
        out("access umask env export getenv unset");
        out("grep wc sort uniq head tail pipe: |");
        out("ps jobs pgrp proc tasks prio stty sched exec wait kill drivers");
        out("lspci lsblk blkread blkwrite");
        out("mount mkfs mountfs umount");
        out("redirection: > >>");
        out("ifconfig ip netstat route arp ping udp hostname uname uptime clock time random limits interrupts exceptions vm sysinfo free sw_vers system_profiler dmesg selftest");
        return 1;
    }
    if (streq(cmd, "pwd")) {
        out(g_cwd);
        return 1;
    }
    if (streq(cmd, "cd")) {
        normalize_path(arg, path);
        if (vfs_stat(path, &ent) == 0 && ent.type == VFS_NODE_DIR) scopy(g_cwd, path, sizeof(g_cwd));
        else out("cd: no such directory");
        return 1;
    }
    if (streq(cmd, "ls")) {
        cmd_ls(arg, out);
        return 1;
    }
    if (streq(cmd, "cat")) {
        cmd_cat(arg, out);
        return 1;
    }
    if (streq(cmd, "grep")) {
        cmd_grep(arg, out);
        return 1;
    }
    if (streq(cmd, "wc")) {
        cmd_wc(arg, out);
        return 1;
    }
    if (streq(cmd, "sort")) {
        cmd_sort_uniq(arg, 0, out);
        return 1;
    }
    if (streq(cmd, "uniq")) {
        cmd_sort_uniq(arg, 1, out);
        return 1;
    }
    if (streq(cmd, "head")) {
        cmd_head_tail(arg, 0, out);
        return 1;
    }
    if (streq(cmd, "tail")) {
        cmd_head_tail(arg, 1, out);
        return 1;
    }
    if (streq(cmd, "echo")) {
        out(arg);
        return 1;
    }
    if (streq(cmd, "touch")) {
        normalize_path(arg, path);
        if (!*skip_spaces(arg)) out("touch: missing operand");
        else if (vfs_create(path) < 0) {
            uint32_t now = timer_ms();
            if (vfs_stat(path, &ent) < 0 || vfs_utime(path, now, now) < 0) out("touch: failed");
        }
        return 1;
    }
    if (streq(cmd, "mkdir")) {
        normalize_path(arg, path);
        if (!*skip_spaces(arg)) out("mkdir: missing operand");
        else if (vfs_mkdir(path) < 0) out("mkdir: failed");
        return 1;
    }
    if (streq(cmd, "rmdir")) {
        normalize_path(arg, path);
        if (!*skip_spaces(arg)) out("rmdir: missing operand");
        else if (vfs_rmdir(path) < 0) out("rmdir: failed");
        return 1;
    }
    if (streq(cmd, "rm")) {
        normalize_path(arg, path);
        if (!*skip_spaces(arg)) out("rm: missing operand");
        else if (vfs_unlink(path) < 0) out("rm: failed");
        return 1;
    }
    if (streq(cmd, "mv")) {
        cmd_mv(arg, out);
        return 1;
    }
    if (streq(cmd, "ln")) {
        cmd_ln(arg, out);
        return 1;
    }
    if (streq(cmd, "readlink")) {
        cmd_readlink(arg, out);
        return 1;
    }
    if (streq(cmd, "stat")) {
        cmd_stat(arg, out);
        return 1;
    }
    if (streq(cmd, "lstat")) {
        cmd_lstat(arg, out);
        return 1;
    }
    if (streq(cmd, "df")) {
        cmd_df(arg, out);
        return 1;
    }
    if (streq(cmd, "write")) {
        cmd_write_file(arg, 0, out);
        return 1;
    }
    if (streq(cmd, "append")) {
        cmd_write_file(arg, 1, out);
        return 1;
    }
    if (streq(cmd, "truncate")) {
        cmd_truncate_file(arg, out);
        return 1;
    }
    if (streq(cmd, "pread")) {
        cmd_pread_file(arg, out);
        return 1;
    }
    if (streq(cmd, "pwrite")) {
        cmd_pwrite_file(arg, out);
        return 1;
    }
    if (streq(cmd, "fsync")) {
        cmd_sync_path(arg, 0, out);
        return 1;
    }
    if (streq(cmd, "fdatasync")) {
        cmd_sync_path(arg, 1, out);
        return 1;
    }
    if (streq(cmd, "sync")) {
        if (*skip_spaces(arg)) out("sync: usage");
        else if (vfs_sync() < 0) out("sync: failed");
        return 1;
    }
    if (streq(cmd, "chmod")) {
        cmd_chmod(arg, out);
        return 1;
    }
    if (streq(cmd, "chown")) {
        cmd_chown(arg, out);
        return 1;
    }
    if (streq(cmd, "utime")) {
        cmd_utime(arg, out);
        return 1;
    }
    if (streq(cmd, "access")) {
        cmd_access(arg, out);
        return 1;
    }
    if (streq(cmd, "umask")) {
        cmd_umask(arg, out);
        return 1;
    }
    if (streq(cmd, "env")) {
        cmd_env(arg, out);
        return 1;
    }
    if (streq(cmd, "getenv")) {
        cmd_getenv(arg, out);
        return 1;
    }
    if (streq(cmd, "export")) {
        cmd_export(arg, out);
        return 1;
    }
    if (streq(cmd, "unset")) {
        cmd_unsetenv(arg, out);
        return 1;
    }
    if (streq(cmd, "exec")) {
        process_t *p;
        char argbuf[PROCESS_MAX_ARGS][PROCESS_ARG_MAX];
        const char *argv[PROCESS_MAX_ARGS];
        uint32_t argc = 0;
        int parsed = parse_exec_args(arg, path, argbuf, argv, &argc);
        if (parsed == 0) out("exec: missing file");
        else if (parsed == -2) out("exec: too many args");
        else if (parsed < 0) out("exec: argument too long");
        else {
            p = process_spawn_path_args(path, argc, argv);
            if (p) {
                char msg[64];
                uint32_t pos = 0;
                append_str(msg, &pos, sizeof(msg), "spawned pid ");
                append_dec(msg, &pos, sizeof(msg), p->pid);
                out(msg);
            } else {
                out("exec: not an ELF user image");
            }
        }
        return 1;
    }
    if (streq(cmd, "wait")) {
        cmd_wait(arg, out);
        return 1;
    }
    if (streq(cmd, "kill")) {
        cmd_kill(arg, out);
        return 1;
    }
    if (streq(cmd, "mount")) {
        out("ramfs on / type ramfs rw");
        out("devfs on /dev type devfs rw");
        if (simplefs_mounted()) {
            char mount_line[96];
            uint32_t pos = 0;
            append_str(mount_line, &pos, sizeof(mount_line), "simplefs on ");
            append_str(mount_line, &pos, sizeof(mount_line), simplefs_mount_path());
            append_str(mount_line, &pos, sizeof(mount_line), " type simplefs rw");
            out(mount_line);
        }
        return 1;
    }
    if (streq(cmd, "ps") || streq(cmd, "top")) {
        cmd_ps(out);
        return 1;
    }
    if (streq(cmd, "jobs")) {
        cmd_jobs(out);
        return 1;
    }
    if (streq(cmd, "pgrp")) {
        cmd_pgrp(arg, out);
        return 1;
    }
    if (streq(cmd, "proc")) {
        cmd_proc(arg, out);
        return 1;
    }
    if (streq(cmd, "tasks")) {
        cmd_tasks(out);
        return 1;
    }
    if (streq(cmd, "prio")) {
        cmd_prio(arg, out);
        return 1;
    }
    if (streq(cmd, "stty")) {
        cmd_stty(arg, out);
        return 1;
    }
    if (streq(cmd, "sched")) {
        cmd_cat("/proc/sched", out);
        return 1;
    }
    if (streq(cmd, "drivers")) {
        cmd_drivers(out);
        return 1;
    }
    if (streq(cmd, "lspci")) {
        cmd_lspci(out);
        return 1;
    }
    if (streq(cmd, "lsblk")) {
        cmd_lsblk(out);
        return 1;
    }
    if (streq(cmd, "blkread")) {
        cmd_blkread(arg, out);
        return 1;
    }
    if (streq(cmd, "blkwrite")) {
        cmd_blkwrite(arg, out);
        return 1;
    }
    if (streq(cmd, "mkfs")) {
        cmd_mkfs(arg, out);
        return 1;
    }
    if (streq(cmd, "mountfs")) {
        cmd_mountfs(arg, out);
        return 1;
    }
    if (streq(cmd, "umount")) {
        cmd_umount(arg, out);
        return 1;
    }
    if (streq(cmd, "ifconfig")) {
        cmd_ifconfig(arg, out);
        return 1;
    }
    if (streq(cmd, "ip")) {
        cmd_ip(arg, out);
        return 1;
    }
    if (streq(cmd, "netstat")) {
        cmd_netstat(arg, out);
        return 1;
    }
    if (streq(cmd, "route")) {
        cmd_route(arg, out);
        return 1;
    }
    if (streq(cmd, "arp")) {
        cmd_arp(arg, out);
        return 1;
    }
    if (streq(cmd, "ping")) {
        uint32_t dst = 0x7F000001U;
        uint32_t ifindex = 0;
        uint32_t gateway = 0;
        if (*skip_spaces(arg) && parse_ipv4(arg, &dst) < 0) {
            out("ping: bad address");
            return 1;
        }
        if (net_route_lookup4(dst, &ifindex, &gateway) < 0) {
            out("ping: no route");
            return 1;
        }
        out(net_ping4(ifindex, dst) == 0 ? "icmp_seq=1 reply" : "ping failed");
        return 1;
    }
    if (streq(cmd, "udp")) {
        cmd_udp(arg, out);
        return 1;
    }
    if (streq(cmd, "hostname")) {
        cmd_hostname(arg, out);
        return 1;
    }
    if (streq(cmd, "uname")) {
        cmd_uname(arg, out);
        return 1;
    }
    if (streq(cmd, "uptime")) {
        char msg[80];
        uint32_t pos = 0;
        append_str(msg, &pos, sizeof(msg), "ticks ");
        append_dec(msg, &pos, sizeof(msg), timer_ticks());
        append_str(msg, &pos, sizeof(msg), " ms ");
        append_dec(msg, &pos, sizeof(msg), timer_ms());
        out(msg);
        return 1;
    }
    if (streq(cmd, "clock")) {
        char msg[96];
        uint32_t pos = 0;
        append_str(msg, &pos, sizeof(msg), "hz ");
        append_dec(msg, &pos, sizeof(msg), timer_hz());
        append_str(msg, &pos, sizeof(msg), " ticks ");
        append_dec(msg, &pos, sizeof(msg), timer_ticks());
        append_str(msg, &pos, sizeof(msg), " ms ");
        append_dec(msg, &pos, sizeof(msg), timer_ms());
        out(msg);
        return 1;
    }
    if (streq(cmd, "time")) {
        char msg[96];
        uint32_t pos = 0;
        uint32_t ms = timer_ms();
        append_str(msg, &pos, sizeof(msg), "sec ");
        append_dec(msg, &pos, sizeof(msg), ms / 1000U);
        append_str(msg, &pos, sizeof(msg), " usec ");
        append_dec(msg, &pos, sizeof(msg), (ms % 1000U) * 1000U);
        out(msg);
        return 1;
    }
    if (streq(cmd, "random")) {
        cmd_random(arg, out);
        return 1;
    }
    if (streq(cmd, "limits")) {
        cmd_limits(out);
        return 1;
    }
    if (streq(cmd, "interrupts")) {
        cmd_cat("/proc/interrupts", out);
        return 1;
    }
    if (streq(cmd, "exceptions")) {
        cmd_cat("/proc/exceptions", out);
        return 1;
    }
    if (streq(cmd, "vm")) {
        cmd_cat("/proc/vm", out);
        return 1;
    }
    if (streq(cmd, "sysinfo") || streq(cmd, "neofetch")) {
        cmd_sysinfo(out);
        return 1;
    }
    if (streq(cmd, "free")) {
        cmd_free(out);
        return 1;
    }
    if (streq(cmd, "sw_vers")) {
        cmd_sw_vers(out);
        return 1;
    }
    if (streq(cmd, "system_profiler")) {
        cmd_system_profiler(arg, out);
        return 1;
    }
    if (streq(cmd, "dmesg")) {
        cmd_dmesg(out);
        return 1;
    }
    if (streq(cmd, "selftest")) {
        (void)selftest_run(out);
        return 1;
    }
    return 0;
}
