#ifndef __INI_H__
#define __INI_H__

int ini_parse(const char *filename,
              int (*handler)(void *user, const char *section,
                             const char *name, const char *value),
              void *user);

#endif