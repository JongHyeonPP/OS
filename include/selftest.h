#ifndef SELFTEST_H
#define SELFTEST_H

typedef void (*selftest_write_fn)(const char *line);

int selftest_run(selftest_write_fn out);

#endif
