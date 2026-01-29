#include "discord.h"
#include "api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void discord_free_channel(struct channel *channel) {
  free(channel->name);
}

void discord_free_channels(json_array *channels) {
  struct channel *channel;
  json_array *_c = channels;
  json_array_for_each(_c, channel) { discord_free_channel(channel); }
  json_array_destroy(channels);
}

json_array *discord_get_channels(const char *guild_id) {
  size_t new_url_size = strlen(DISCORD_API_BASE_URL) + strlen("guilds") +
                        strlen(guild_id) + strlen("channels") + 5;
  char new_url[new_url_size];
  snprintf(new_url, new_url_size, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
           guild_id, "channels");

  json_array *channels = json_array_new();
  struct response resp = {0};

  if (request_get(new_url, &resp, 1) != 0) {
    free(resp.raw);
    json_array_destroy(channels);
    return NULL;
  }

  if (resp.http_code != 200) {
    json_array_destroy(channels);
    return NULL;
  }

  json_array *json;
  json_load(resp.raw, (void **)&json);
  free(resp.raw);

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

void discord_free_message(struct message *message) {
  free(message->attachment.filename);
  free(message->attachment.url);
  if (message->parts)
    free(message->parts);

  if (message->content)
    free(message->content);
}
void discord_free_messages(json_array *messages) {
  struct message *message;
  json_array *_m = messages;
  json_array_for_each(_m, message) { discord_free_message(message); }
  json_array_destroy(messages);
}

json_array *discord_get_messages(const char *channel_id) {
  json_array *messages = json_array_new();

  int messages_n = -1;
  size_t url_size = strlen(DISCORD_API_BASE_URL) + strlen("channels") +
                    strlen(channel_id) + strlen("messages") + 6;

  struct response resp = {0};
  while (messages_n == -1 || messages_n == 100) {
    if (resp.raw) {
      free(resp.raw);
      resp.raw = NULL;
    }

    size_t new_url_size = url_size;
    if (messages_n == -1) {
      char *params = "limit=100";
      new_url_size += strlen(params);

      char new_url[url_size + strlen(params)];
      snprintf(new_url, new_url_size, "%s/%s/%s/%s?%s", DISCORD_API_BASE_URL,
               "channels", channel_id, "messages", params);

      request_get(new_url, &resp, 1);
      if (resp.http_code != 200 && resp.http_code != 201) {
        json_object *error;
        json_load(resp.raw, (void **)&error);

        json_string error_message = json_object_get(error, "message");
        fprintf(stderr, "ERROR: %s\n", error_message);

        free(resp.raw);
        json_object_destroy(error);
        return NULL;
      }

    } else {
      char *params_s = "limit=100&before=%s";

      int idx = json_array_size(messages) - 1;
      struct message *last_message = json_array_get(messages, idx);
      if (!last_message) {
        printf("WARNING: no message found at index %d\n", idx);
        free(resp.raw);
        json_array_destroy(messages);
        return NULL;
      }

      size_t params_size = strlen(params_s) - 2 +
                           strlen(last_message->id.value) + 1; /* - 2 -> "%s" */

      char params[params_size];

      snprintf(params, params_size, params_s, last_message->id.value);
      new_url_size += strlen(params);

      char new_url[url_size + strlen(params)];
      snprintf(new_url, new_url_size, "%s/%s/%s/%s?%s", DISCORD_API_BASE_URL,
               "channels", channel_id, "messages", params);

      request_get(new_url, &resp, 1);
      if (resp.http_code != 200 && resp.http_code != 201) {
        json_object *error;
        json_load(resp.raw, (void **)&error);

        json_string error_message = json_object_get(error, "message");
        fprintf(stderr, "ERROR: %s\n", error_message);

        free(resp.raw);
        json_array_destroy(messages);
        json_object_destroy(error);
        return NULL;
      }
    }

    json_array *json;
    json_load(resp.raw, (void **)&json);
    free(resp.raw);

    messages_n = json_array_size(json);

    json_object *o;
    json_array_for_each(json, o) {
      json_string message_id = json_object_get(o, "id");

      struct message message;
      snowflake_init(message_id, &message.id);
      message.content = NULL;
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
