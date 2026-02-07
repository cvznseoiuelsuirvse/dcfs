#ifndef REQUEST_H
#define REQUEST_H

#include <stdio.h>

struct response {
  char *raw;
  size_t size;
  int http_code;
};

struct file {
  char filename[256];
  char *buffer;
  size_t buffer_size;
};

int request_get(const char *url, struct response *resp, char user_auth);
int request_post(const char *url, char *data, struct response *resp,
                 char user_auth);
int request_post_files(const char *url, const struct file *files,
                       size_t files_n, struct response *resp);
int request_patch(const char *url, char *data, struct response *resp,
                  char user_auth);
int request_delete(const char *url, struct response *resp, char user_auth);

#endif
