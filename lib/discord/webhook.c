#include "discord/webhook.h"
#include "request.h"
#include "util.h"
#include "json/json.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char *WEBHOOK_URL = NULL;

void webhook_set_url(const char *url) { WEBHOOK_URL = (char *)url; };

int webhook_get_info(webhook_info *info) {
  if (WEBHOOK_URL == NULL) {
    fprintf(stderr, "webhook url isn't set\n");
    return 1;
  }

  struct data userdata = {.json = NULL, .size = 0};
  if (request_get(WEBHOOK_URL, &userdata, 0) != 0)
    return 1;

  if (userdata.size == 0) {
    fprintf(stderr, "failed to request '%s'\n", WEBHOOK_URL);
    return 1;
  }
  if (userdata.json == NULL) {
    fprintf(stderr, "failed to get info from '%s'\n", WEBHOOK_URL);
    return 1;
  }

  json_string id = json_object_get(userdata.json, "id");
  json_string name = json_object_get(userdata.json, "name");
  json_string guild_id = json_object_get(userdata.json, "guild_id");
  json_string channel_id = json_object_get(userdata.json, "channel_id");

  strcpy(info->id, id);
  strcpy(info->name, name);
  strcpy(info->guild_id, guild_id);
  strcpy(info->channel_id, channel_id);

  json_object_destroy(userdata.json);
  return 0;
}

int webhook_send_file(const char *path, json_object **json) {
  int ret = 0;
  if (WEBHOOK_URL == NULL) {
    fprintf(stderr, "webhook url isn't set\n");
    return 1;
  }

  const char *filename = basename((char *)path);

  size_t fsize = get_filesize(path);
  if (fsize == 0) {
    fprintf(stderr, "failed to get size of '%s'\n", filename);
    return 1;
  }

  FILE *fp = fopen(path, "rb");

  if (fp == NULL) {
    fprintf(stderr, "failed to open '%s': ", path);
    perror("fopen");
    return 1;
  }

  char buffer[fsize];
  size_t read_size = fread(buffer, sizeof(char), fsize, fp);
  fclose(fp);

  if (read_size != fsize) {
    fprintf(stderr, "error reading '%s'\n", filename);
    return 1;
  }

  char *params = "wait=true";
  size_t new_url_size = strlen(WEBHOOK_URL) + strlen(params) + 2;

  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s?%s", WEBHOOK_URL, params);

  struct data userdata = {.json = NULL, .size = 0};
  if (request_post_file(new_url, filename, buffer, sizeof(buffer), &userdata) !=
      0)
    ret = 1;

  // if json wan't meant to be returned
  if (json == NULL && userdata.json != NULL) {
    json_object_destroy(userdata.json);
  } else {
    *json = userdata.json;
  }

  return ret;
};

int webhook_send(char *data, json_object **json) {
  int ret = 0;
  if (WEBHOOK_URL == NULL) {
    fprintf(stderr, "webhook url isn't set\n");
    return 1;
  }

  char *params = "wait=true";
  size_t new_url_size = strlen(WEBHOOK_URL) + strlen(params) + 2;

  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s?%s", WEBHOOK_URL, params);

  struct data userdata = {.json = NULL, .size = 0};
  if (request_post(new_url, data, &userdata, 0) != 0)
    ret = 1;

  // if json wan't meant to be returned
  if (json == NULL && userdata.json != NULL) {
    json_object_destroy(userdata.json);
  } else {
    *json = userdata.json;
  }

  return ret;
}

int webhook_delete(const char *message_id) {
  int ret = 0;
  if (WEBHOOK_URL == NULL) {
    fprintf(stderr, "webhook url isn't set\n");
    return 1;
  }

  if (strlen(message_id) > 20) {
    fprintf(stderr, "message_id is too long (>20)\n");
    return 1;
  }

  size_t new_url_size =
      strlen(WEBHOOK_URL) + strlen("messages") + strlen(message_id) + 3;

  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s", WEBHOOK_URL, "messages",
           message_id);

  struct data userdata = {.json = NULL, .size = 0};
  if (request_delete(new_url, &userdata, 0) != 0)
    ret = 1;

  if (userdata.json != NULL)
    json_object_destroy(userdata.json);

  return ret;
}
