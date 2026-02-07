#include "api.h"
#include "request.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int discord_create_channel(const char *guild_id, const char *name,
                           struct response *resp) {
  int res = 0;
  char *payload =
      "{\"name\": \"%s\", \"type\": 0, \"permission_overwrites\": [{\"id\": "
      "\"%s\", \"type\": 0, \"allow\": \"0\", \"deny\": \"1024\"}]}";
  char new_payload[SIZE];
  snprintf(new_payload, SIZE, payload, name, guild_id);

  char new_url[SIZE];
  snprintf(new_url, SIZE, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
           guild_id, "channels");

  if (request_post(new_url, new_payload, resp, 1) != 0)
    res = 1;

  return res;
};

int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp) {

  int res = 0;

  char new_payload[SIZE];
  snprintf(new_payload, SIZE, "{\"name\": \"%s\"}", name);

  char new_url[SIZE];
  snprintf(new_url, SIZE, "%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_patch(new_url, new_payload, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}

int discord_delete_channel(const char *channel_id, struct response *resp) {
  int res = 0;

  char new_url[SIZE];
  snprintf(new_url, SIZE, "%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_delete(new_url, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}

int discord_create_attachments(const char *channel_id, const struct file *files,
                               size_t files_n, struct response *resp) {
  int res = 0;

  char new_url[SIZE];
  snprintf(new_url, SIZE, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id, "messages");

  if (request_post_files(new_url, files, files_n, resp) != 0)
    ;
  res = 1;

  return res;
}

int discord_delete_messsage(const char *channel_id, const char *message_id,
                            struct response *resp) {
  int res = 0;

  char new_url[SIZE];
  snprintf(new_url, SIZE, "%s/%s/%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id, "messages", message_id);

  if (request_delete(new_url, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}
