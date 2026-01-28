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

int aes_decrypt(unsigned char *ciphertext, int ciphertext_len,
                unsigned char *key, unsigned char *plaintext);
int b64decode(const char *input, int input_len, char *output, int max_out_len);
int _pow(int n, int po);
int str_to_int(const char *string, size_t string_size);
void print_size(float n);

int filename_decode(char *filename, size_t max_out_len);
int filename_decrypt(uint8_t *KEY, char *filename, size_t decoded_size);
void filename_encode_encrypt(const char *old, char *new, size_t new_size);
int count_char(const char *string, char c);

#endif
