#ifndef DCFS_UTIL_H
#define DCFS_UTIL_H

#include <stdint.h>
#include <stdlib.h>

void print_buffer(char *buffer, size_t buffer_len);
size_t get_filesize(const char *path);
char *get_auth_token();
char *get_guild_id();

int b64encode(char *out, const char *in, size_t out_len);
int b64decode(char *out, const char *in, size_t out_len);

int count_char(const char *string, char c);
int last_index(const char *string, char c);
unsigned int string_hash(const char *string);
void string_normalize(char *out, const char *in, size_t out_len);

#endif
