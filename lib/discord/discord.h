#ifndef DCFS_DISCORD_H
#define DCFS_DISCORD_H

#include "request.h"
#include "json/json.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DISCORD_API_BASE_URL "https://discord.com/api/v9"
#define DISCORD_MAX_PARTS 256
#define DISCORD_SIZE 256

struct discord_snowflake {
  size_t timestamp;
  char value[64];
};

enum channel_types {
  GUILD_TEXT,
  DM,
  GUILD_VOICE,
  GROUP_DM,
  GUILD_CATEGORY,
  GUILD_ANNOUNCEMENT,
  ANNOUNCEMENT_THREAD,
  PUBLIC_THREAD,
  PRIVATE_THREAD,
  GUILD_STAGE_VOICE,
  GUILD_DIRECTORY,
  GUILD_FORUM,
  GUILD_MEDIA,
} discord_channel_types;

struct dcfs_message {
  mode_t mode;
  gid_t gid;
  uid_t uid;
  struct discord_snowflake id;
  char filename[256];
  char *url;
  char *content;
  size_t size;
  struct dcfs_message *parts[DISCORD_MAX_PARTS];
  size_t parts_n;
  int is_part;
};

struct dcfs_channel {
  mode_t mode;
  gid_t gid;
  uid_t uid;
  struct discord_snowflake id;
  char name[128];
  enum channel_types type;
  char has_parent;
  json_array *messages;
};
inline static void discord_snowflake_init(const char *id,
                                          struct discord_snowflake *snowflake) {
  memset(snowflake, 0, sizeof(struct discord_snowflake));
  uint64_t n = strtoll(id, NULL, 10);
  snowflake->timestamp = ((n >> 22) + 1420070400000) / 1000;
  strcpy(snowflake->value, id);
}

void discord_free_channels(json_array *channels);
void discord_free_channel(struct dcfs_channel *channel);
json_array *discord_get_channels(const char *guild_id);

void discord_free_messages(json_array *messages);
void discord_free_message(struct dcfs_message *message);
json_array *discord_get_messages(const char *channel_id);

int discord_create_channel(const char *guild_id, const char *name,
                           struct response *resp);
int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp);
int discord_delete_channel(const char *channel_id, struct response *resp);
int discord_create_attachments(const char *channel_id, const struct file *files,
                               size_t files_n, struct response *resp);
int discord_delete_messsage(const char *channel_id, const char *message_id,
                            struct response *resp);

#endif
