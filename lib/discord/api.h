#ifndef DISCORD_API_H
#define DISCORD_API_H

#include "request.h"
#include "json/json.h"

int discord_create_channel(const char *guild_id, const char *name,
                           struct data *userdata);
int discord_rename_channel(const char *channel_id, const char *name,
                           struct data *userdata);
int discord_delete_channel(const char *channel_id, struct data *userdata);

int discord_create_webhook(const char *channel_id, const char *name,
                           struct data *userdata);

json_array *discord_get_channels(const char *guild_id);
void discord_free_channels(json_array *channels);

json_array *discord_get_messages(const char *channel_id);
void discord_free_messages(json_array *messages);

json_array *discord_get_webhooks(const char *channel_id);

#endif
