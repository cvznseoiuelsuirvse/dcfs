#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdlib.h>

void print_buffer(char *buffer, size_t buffer_len);
size_t get_filesize(const char *path);
char *get_auth_token();
char *get_guild_id();

void str_replace(const char *orig, char *out, size_t out_size, const char *old,
                 const char *new);

int str_to_int(const char *string, size_t string_size);
void print_size(float n);

int count_char(const char *string, char c);
int last_index(const char *string, char c);
int hash_string(const char *string);

#endif
