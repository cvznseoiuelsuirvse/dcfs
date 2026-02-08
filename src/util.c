#include "util.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct repl_t {
  const char *old;
  const char *new;
};

void print_buffer(char *buffer, size_t buffer_len) {
  for (size_t i = 0; i < buffer_len; i++) {
    uint8_t c = buffer[i];
    if (c < 32 || c > 126) {
      printf("%02x ", c);
    } else {
      printf("%c ", c);
    }
  }
  printf("\n");
}

size_t get_filesize(const char *path) {
  struct stat s;
  int ret = stat(path, &s);
  if (ret != 0) {
    perror("stat");
    return 0;
  }
  return s.st_size;
}

char *get_auth_token() {
  char *value = getenv("DCFS_TOKEN");
  if (value != NULL && strlen(value) > 100) {
    printf("DCFS_TOKEN is too long\n");
    return NULL;
  }

  return value;
}

char *get_guild_id() {
  char *value = getenv("DCFS_GUILD_ID");

  if (value != NULL && strlen(value) > 20) {
    printf("DCFS_GUILD_ID is too long\n");
    return NULL;
  }

  return value;
}

void str_replace(const char *orig, char *out, size_t out_size, const char *old,
                 const char *new) {

  if (out_size <= strlen(orig)) {
    fprintf(stderr, "out must be longer than orig\n");
    return;
  }

  char string[out_size];
  strcpy(string, orig);

  const char *pos;
  while ((pos = strstr(string, old))) {
    size_t offset = pos - string;
    size_t new_i = 0;

    for (size_t old_i = 0; old_i < strlen(string);) {
      char c = string[old_i];

      if (old_i == offset) {
        strcpy(out + new_i, new);

        old_i += strlen(old);
        new_i += strlen(new);

      } else {
        out[new_i] = c;
        new_i++;
        old_i++;
      }
    }

    out[new_i] = 0;
    strcpy(string, out);
  }
};

void print_size(float n) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  for (size_t i = 0; i < 5; i++) {
    if (n < 1024) {
      const char *unit = units[i];
      char buffer[9];
      snprintf(buffer, 9, "%.2f%s", n, unit);
      printf("%9s", buffer);
      return;
    }
    n /= 1024;
  }
}

int count_char(const char *string, char c) {
  int n = 0;
  for (size_t i = 0; i < strlen(string); i++) {
    if (string[i] == c)
      n++;
  }
  return n;
}

int last_index(const char *string, char c) {
  int idx = -1;
  for (size_t i = 0; i < strlen(string); i++)
    if (string[i] == c)
      idx = i;
  return idx;
}

int hash_string(const char *string) {
  int hash = 5381;
  int c;
  while ((c = *string++))
    hash = ((hash << 5) + hash) + c;

  return hash;
}
