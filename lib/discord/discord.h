#ifndef DCFS_DISCORD_H
#define DCFS_DISCORD_H

#include "discord/api.h"
#include "json/json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct snowflake {
  time_t timestamp;
  uint8_t internal_worker_id;
  uint8_t internal_process_id;
  uint16_t increment;
  char value[40];
};

inline static void snowflake_init(const char *ts, struct snowflake *snowflake) {
  uint64_t n = strtoll(ts, NULL, 10);
  snowflake->timestamp = ((n >> 22) + 1420070400000) / 1000;
  snowflake->internal_worker_id = (n & 0x3E0000) >> 17;
  snowflake->internal_process_id = (n & 0x1F000) >> 12;
  snowflake->increment = n & 0xFFF;
  memset(snowflake->value, 0, sizeof(snowflake->value));
  memcpy(snowflake->value, ts, strlen(ts));
}

struct attachment {
  char *filename;
  char *url;
  size_t size;
};

struct message {
  struct snowflake id;
  struct attachment attachment;
  char *content;
  size_t content_size;
  struct message **parts;
  size_t parts_n;
  int is_part;
};

struct channel {
  struct snowflake id;
  char *name;
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
