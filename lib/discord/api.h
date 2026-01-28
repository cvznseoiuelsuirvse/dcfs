#ifndef DISCORD_API_H
#define DISCORD_API_H

#include "request.h"
#include "json/json.h"

int discord_create_channel(const char *guild_id, const char *name,
                           struct response *resp);
int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp);
int discord_delete_channel(const char *channel_id, struct response *resp);

json_array *discord_get_channels(const char *guild_id);
void discord_free_channels(json_array *channels);

json_array *discord_get_messages(const char *channel_id);
void discord_free_messages(json_array *messages);

#endif
