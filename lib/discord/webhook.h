#ifndef DISCORD_WEBHOOK_H
#define DISCORD_WEBHOOK_H

#include "json/json.h"

typedef struct webhook_info {
  char id[21];
  char name[33];
  char guild_id[21];
  char channel_id[21];
} webhook_info;

typedef struct webhook_message {
  char message[2000];
  char *buffer;
} webhook_message;

void webhook_set_url(const char *url);
int webhook_get_info(webhook_info *info);
int webhook_send_file(const char *path, json_object **json);
int webhook_send(char *data, json_object **json);
int webhook_delete(const char *message_id);

#endif
