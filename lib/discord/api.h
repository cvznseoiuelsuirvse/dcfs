#ifndef DCFS_DISCORD_API_H
#define DCFS_DISCORD_API_H

#include "request.h"

#define DISCORD_API_BASE_URL "https://discord.com/api/v9"

int discord_create_channel(const char *guild_id, const char *name,
                           struct response *resp);
int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp);
int discord_delete_channel(const char *channel_id, struct response *resp);
int discord_create_message(const char *channel_id, const char *filename,
                           char *buffer, size_t size, struct response *resp);
int discord_delete_messsage(const char *channel_id, const char *message_id,
                            struct response *resp);

#endif
