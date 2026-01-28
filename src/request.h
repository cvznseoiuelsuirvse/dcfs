#ifndef REQUEST_H
#define REQUEST_H

#include "json/json.h"

struct data {
  void *json;
  json_value_type json_type;
  size_t size;
  int http_code;
  const char *http_error_message;
  char *__raw;
};

int request_get(const char *url, struct data *userdata, char user_auth);
int request_post(const char *url, char *data, struct data *userdata,
                 char user_auth);
int request_post_file(const char *url, const char *filename, char *buffer,
                      size_t buffer_size, struct data *userdata);
int request_patch(const char *url, char *data, struct data *userdata,
                  char user_auth);
int request_delete(const char *url, struct data *userdata, char user_auth);

#endif
