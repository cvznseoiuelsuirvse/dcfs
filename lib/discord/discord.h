#ifndef DCFS_DISCORD_H
#define DCFS_DISCORD_H

#include "discord/api.h"
#include "json/json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_PARTS 256

struct snowflake {
  size_t timestamp;
  char value[64];
};

inline static void snowflake_init(const char *ts, struct snowflake *snowflake) {
  memset(snowflake, 0, sizeof(struct snowflake));
  uint64_t n = strtoll(ts, NULL, 10);
  snowflake->timestamp = ((n >> 22) + 1420070400000) / 1000;
  strcpy(snowflake->value, ts);
}

struct attachment {
  char filename[256];
  char *url;
  size_t size;
};

struct message {
  struct snowflake id;
  struct attachment attachment;
  char *content;
  size_t content_size;
  struct message *parts[MAX_PARTS];
  size_t parts_n;
  int is_part;
};

struct channel {
  struct snowflake id;
  char name[128];
  enum channel_types type;
  char has_parent;
  json_array *messages;
};

void discord_free_channels(json_array *channels);
void discord_free_channel(struct channel *channel);
json_array *discord_get_channels(const char *guild_id);

void discord_free_messages(json_array *messages);
void discord_free_message(struct message *message);
json_array *discord_get_messages(const char *channel_id);

#endif
