#ifndef DCFS_UTIL_H
#define DCFS_UTIL_H

#include "fs.h"

#include <stdint.h>
#include <stdlib.h>

#define STREQ(s1, s2) (strcmp((s1), (s2)) == 0)

void id_to_ctime(time_t *ctime, const char *id);
char *get_auth_token();
char *get_guild_id();

int b64encode(char *out, const char *in, size_t out_len);
int b64decode(char *out, const char *in, size_t out_len);

int count_char(const char *string, char c);
int last_index(const char *string, char c);
dcfs_hash string_hash(const char *string);
void string_normalize(char *out, const char *in, size_t out_len);

#endif
