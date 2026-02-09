#include "util.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct repl_t {
  const char *old;
  const char *new;
};

static const char b64charset[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64inv[] = {
    17,  0,   0,   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
    0,   0,   0,   0,   0,   0,   0,   -65, -65, -65, -65, -65, -65,
    -65, -65, -65, -65, -65, -65, -65, -65, -65, -65, -65, -65, -65,
    -65, -65, -65, -65, -65, -65, -65, 0,   0,   0,   0,   -32, 0,
    -71, -71, -71, -71, -71, -71, -71, -71, -71, -71, -71, -71, -71,
    -71, -71, -71, -71, -71, -71, -71, -71, -71, -71, -71, -71, -71,
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

static int is_b64_valid(char c) {
  if (c == 45 || c == 95 || (47 < c && c < 58) || (64 < c && c < 91) ||
      (96 < c && c < 123))
    return 1;
  return 0;
}

int b64encode(const char *in, char *out, size_t out_len) {
  size_t in_offset = 0;
  size_t out_offset = 0;
  size_t in_len = strlen(in);

  for (; in_offset < in_len; in_offset += 3, out_offset += 4) {
    if (out_offset + 3 >= out_len)
      return 1;

    int remaining = in_len - in_offset;
    uint32_t v = (uint8_t)in[in_offset] << 16 |
                 (remaining > 1 ? (uint8_t)in[in_offset + 1] << 8 : 0) |
                 (remaining > 2 ? (uint8_t)in[in_offset + 2] : 0);

    for (int i = 0; i < 4; i++) {
      if (remaining > (i - 1)) {
        int b64idx = (v >> ((3 - i) * 6)) & 0x3f;
        out[out_offset + i] = b64charset[b64idx];
      }
    }
  }
  return 0;
}

int b64decode(const char *in, char *out, size_t out_len) {
  size_t in_offset = 0;
  size_t out_offset = 0;
  size_t in_len = strlen(in);

  for (; in_offset < in_len; in_offset += 4, out_offset += 3) {
    if (out_offset + 2 >= out_len) {
      return 1;
    }

    int remaining = in_len - in_offset;
    uint32_t v = 0;

    for (int i = 0; i < 4; i++) {
      if (remaining > i) {
        uint8_t c = (uint8_t)in[in_offset + i];
        if (!is_b64_valid(c))
          return 1;
        v |= (uint8_t)(c + b64inv[c - 45]) << ((3 - i) * 6);
      }
    }

    for (int i = 0; i < 3; i++) {
      if (remaining > (i - 1)) {
        uint8_t c = (v >> ((2 - i) * 8));
        out[out_offset + i] = c;
      }
    }
  }
  return 0;
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
