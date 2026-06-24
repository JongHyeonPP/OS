#ifndef SHELL_H
#define SHELL_H

typedef void (*shell_write_fn)(const char *line);

void shell_init(void);
int shell_execute_line(const char *line, shell_write_fn write_line);
const char *shell_cwd(void);

#endif
