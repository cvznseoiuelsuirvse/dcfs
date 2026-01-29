#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void discord_create_channel(const char *guild_id, const char *name,
                            struct response *resp) {
  int res = 0;
  char *payload =
      "{\"name\": \"%s\", \"type\": 0, \"permission_overwrites\": [{\"id\": "
      "\"%s\", \"type\": 0, \"allow\": \"0\", \"deny\": \"1024\"}]}";
  size_t payload_size =
      (strlen(payload) - 4) + strlen(name) + strlen(guild_id) + 1;

  char new_payload[payload_size];
  snprintf(new_payload, payload_size, payload, name, guild_id);

  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("guilds") +
                        strlen(guild_id) + strlen("channels") + 5;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
           guild_id, "channels");

  if (request_post(new_url, new_payload, resp, 1) != 0)
    res = 1;

  free(resp->raw);
};

int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp) {

  int res = 0;
  char *payload = "{\"name\": \"%s\"}";
  size_t payload_size = (strlen(payload) - 2) + strlen(name) + 1;
  char new_payload[payload_size];
  snprintf(new_payload, payload_size, payload, name);

  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                        strlen(channel_id) + 3;

  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_patch(new_url, new_payload, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}

int discord_delete_channel(const char *channel_id, struct response *resp) {
  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                        strlen(channel_id) + 3;

  int res = 0;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_delete(new_url, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}

int discord_create_message(const char *channel_id, const char *filename,
                           char *buffer, size_t size, struct response *resp) {
  int res = 0;
  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                        strlen(channel_id) + strlen("messages") + 5;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s/%s", DISCORD_API_BASE_URL,
           "channels", channel_id, "messages");

  if (request_post_file(new_url, filename, buffer, size, resp) != 0)
    ;
  res = 1;

  return res;
}

int discord_delete_messsage(const char *channel_id, const char *message_id,
                            struct response *resp) {
  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                        strlen(channel_id) + strlen("messages") +
                        strlen(message_id) + 6;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s/%s/%s", DISCORD_API_BASE_URL,
           "channels", channel_id, "messages", message_id);

  request_delete(new_url, resp, 1);
  return 0;
}
