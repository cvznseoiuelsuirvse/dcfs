#ifndef DCFS_DISCORD_API_H
#define DCFS_DISCORD_API_H

#include "request.h"

#define DISCORD_API_BASE_URL "https://discord.com/api/v9"
#define SIZE 256

typedef enum channel_types {
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
