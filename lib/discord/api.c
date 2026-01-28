#include "api.h"
#include "discord.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *DISCORD_API_BASE_URL = "https://discord.com/api/v9/";

int discord_create_channel(const char *guild_id, const char *name,
                           struct response *resp) {
  char *payload =
      "{\"name\": \"%s\", \"type\": 0, \"permission_overwrites\": [{\"id\": "
      "\"%s\", \"type\": 0, \"allow\": \"0\", \"deny\": \"1024\"}]}";
  size_t payload_size =
      (strlen(payload) - 4) + strlen(name) + strlen(guild_id) + 1;

  char new_payload[payload_size];
  snprintf(new_payload, payload_size, payload, name, guild_id);

  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("guilds") +
                        strlen(guild_id) + strlen("channels") + 3;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
           guild_id, "channels");

  if (request_post(new_url, new_payload, resp, 1) != 0)
    return 1;

  return 0;
};

int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp) {

  char *payload = "{\"name\": \"%s\"}";
  size_t payload_size = (strlen(payload) - 2) + strlen(name) + 1;
  char new_payload[payload_size];
  snprintf(new_payload, payload_size, payload, name);

  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                        strlen(channel_id) + 2;

  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_patch(new_url, new_payload, resp, 1) != 0)
    return 1;

  return 0;
}

int discord_delete_channel(const char *channel_id, struct response *resp) {
  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                        strlen(channel_id) + 2;

  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_delete(new_url, resp, 1) != 0)
    return 1;

  return 0;
}

void discord_free_channels(json_array *channels) {
  struct channel *channel;
  json_array *_c = channels;
  json_array_for_each(_c, channel) { free(channel->name); }
  json_array_destroy(channels);
}

json_array *discord_get_channels(const char *guild_id) {
  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("guilds") +
                        strlen(guild_id) + strlen("channels") + 3;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
           guild_id, "channels");

  json_array *channels = json_array_new();
  struct response resp = {.json = NULL};

  if (request_get(new_url, &resp, 1) != 0) {
    json_array_destroy(channels);
    return NULL;
  }

  printf("http_code: %d\n", resp.http_code);
  if (resp.http_code != 200) {
    json_array_destroy(channels);
    return NULL;
  }

  json_array *json = resp.json;
  json_object *o;
  json_array_for_each(json, o) {
    json_string id = json_object_get(o, "id");
    json_string name = json_object_get(o, "name");
    json_number *type = json_object_get(o, "type");
    json_word *parent = json_object_get(o, "parent_id");

    struct channel ch = {
        .type = *type,
        .has_parent = *parent == JSON_NULL ? 0 : 1,
        .name = strdup(name),
    };
    snowflake_init(id, &ch.id);
    json_array_push(channels, &ch, sizeof(struct channel), JSON_UNKNOWN);
  }

  json_array_destroy(json);
  return channels;
}

void discord_free_messages(json_array *messages) {
  struct message *message;
  json_array *_m = messages;
  json_array_for_each(_m, message) {
    free(message->attachment.filename);
    free(message->attachment.url);
    if (message->parts)
      free(message->parts);
  }
  json_array_destroy(messages);
}

json_array *discord_get_messages(const char *channel_id) {
  json_array *messages = json_array_new();

  int messages_n = -1;
  size_t url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                    strlen(channel_id) + strlen("messages") + 4;

  while (messages_n == -1 || messages_n == 100) {
    struct response resp = {0};
    size_t new_url_size = url_size;
    if (messages_n == -1) {
      char *params = "limit=100";
      new_url_size += strlen(params);

      char new_url[url_size + strlen(params)];
      snprintf(new_url, new_url_size, "%s%s/%s/%s?%s", DISCORD_API_BASE_URL,
               "channels", channel_id, "messages", params);

      request_get(new_url, &resp, 1);
      if (resp.http_code != 200 && resp.http_code != 201) {
        json_string error_message = json_object_get(resp.json, "message");
        fprintf(stderr, "ERROR: %s\n", error_message);
        return NULL;
      }

    } else {
      char *params_s = "limit=100&before=%s";

      int idx = json_array_size(messages) - 1;
      struct message *last_message = json_array_get(messages, idx);
      if (!last_message) {
        printf("WARNING: no message found at index %d\n", idx);
        json_array_destroy(messages);
        return NULL;
      }

      size_t params_size = strlen(params_s) - 2 +
                           strlen(last_message->id.value) + 1; /* - 2 -> "%s" */

      char params[params_size];

      snprintf(params, params_size, params_s, last_message->id.value);
      new_url_size += strlen(params);

      char new_url[url_size + strlen(params)];
      snprintf(new_url, new_url_size, "%s%s/%s/%s?%s", DISCORD_API_BASE_URL,
               "channels", channel_id, "messages", params);

      request_get(new_url, &resp, 1);
      if (resp.http_code != 200 && resp.http_code != 201) {
        json_string error_message = json_object_get(resp.json, "message");
        fprintf(stderr, "ERROR: %s\n", error_message);
        json_array_destroy(messages);
        return NULL;
      }
    }

    json_array *json = resp.json;
    messages_n = json_array_size(json);

    json_object *o;
    json_array_for_each(json, o) {
      json_string message_id = json_object_get(o, "id");

      struct message message;
      snowflake_init(message_id, &message.id);
      message.is_part = 0;
      message.parts_n = 0;
      message.parts = NULL;

      json_array *attachments = json_object_get(o, "attachments");
      if (json_array_size(attachments) > 0) {
        json_object *attachment = json_array_get(attachments, 0);

        json_string filename = json_object_get(attachment, "filename");
        json_number *size = json_object_get(attachment, "size");
        json_string url = json_object_get(attachment, "url");

        message.attachment.filename = strdup(filename);
        message.attachment.size = *size;
        message.attachment.url = strdup(url);

      } else {
        printf("WARNING: no attachments found in %s\n", message_id);
      }
      json_array_push(messages, &message, sizeof(struct message), JSON_UNKNOWN);
    }
    json_array_destroy(json);
  }

  return messages;
}
