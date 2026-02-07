#include "discord.h"
#include "api.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
  regex_t comp;
  regmatch_t matches[3];
} part_regex;

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

static json_array *discord_get_all_messages(const char *channel_id) {
  json_array *messages = json_array_new();

  int messages_n = -1;
  struct response resp = {0};

  while (messages_n == -1 || messages_n == 100) {
    if (resp.raw) {
      free(resp.raw);
      resp.raw = NULL;
    }

    char new_url[SIZE];
    if (messages_n == -1) {
      snprintf(new_url, SIZE, "%s/%s/%s/%s?limit=100", DISCORD_API_BASE_URL,
               "channels", channel_id, "messages");

    } else {
      int idx = json_array_size(messages) - 1;
      struct message *last_message = json_array_get(messages, idx);

      if (!last_message) {
        printf("WARNING: no message found at index %d\n", idx);
        free(resp.raw);
        json_array_destroy(messages);
        return NULL;
      }

      snprintf(new_url, SIZE, "%s/%s/%s/%s?limit=100&before=%s",
               DISCORD_API_BASE_URL, "channels", channel_id, "messages",
               last_message->id.value);
    }

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

    json_array *json;
    json_load(resp.raw, (void **)&json);
    free(resp.raw);

    messages_n = json_array_size(json);

    json_object *o;
    json_array_for_each(json, o) {
      json_string message_id = json_object_get(o, "id");

      json_object *attachment;
      json_array *attachments = json_object_get(o, "attachments");
      json_array_for_each(attachments, attachment) {
        struct message message;
        snowflake_init(message_id, &message.id);
        message.content = NULL;
        message.is_part = 0;
        message.parts_n = 0;
        message.parts = NULL;

        json_string filename = json_object_get(attachment, "filename");
        json_number *size = json_object_get(attachment, "size");
        json_string url = json_object_get(attachment, "url");

        message.attachment.filename = strdup(filename);
        message.attachment.size = *size;
        message.attachment.url = strdup(url);
        json_array_push(messages, &message, sizeof(struct message),
                        JSON_UNKNOWN);
      }
    }
    json_array_destroy(json);
  }

  return messages;
}

json_array *discord_get_messages(const char *channel_id) {
  regcomp(&part_regex.comp, "(.+)\\.PART([0-9]+)", REG_EXTENDED);

  json_array *messages = discord_get_all_messages(channel_id);
  if (messages) {
    struct message *message;

    json_array *_messages = messages;
    size_t parts_cap;
    json_array_for_each(_messages, message) {
      int ret = regexec(&part_regex.comp, message->attachment.filename,
                        sizeof(part_regex.matches) / sizeof(regmatch_t),
                        part_regex.matches, 0);

      if (ret == 0) {
        regmatch_t m_body = part_regex.matches[1];
        regmatch_t m_part = part_regex.matches[2];
        size_t part =
            strtol(message->attachment.filename + m_part.rm_so, NULL, 10);

        size_t body_size = m_body.rm_eo - m_body.rm_so;
        char body[body_size + 1];
        memcpy(body, message->attachment.filename + m_body.rm_so, body_size);
        body[body_size] = 0;
        message->is_part = 1;

        json_array *_messages1 = messages;
        struct message *message_head;
        json_array_for_each(_messages1, message_head) {
          if (strcmp(message_head->attachment.filename, body) == 0) {
            message_head->parts_n++;

            if (message_head->parts && part > parts_cap) {
              parts_cap = ((part / 10) + 1) * 10;
              message_head->parts =
                  realloc(message_head->parts, parts_cap * sizeof(struct part));
            } else if (!message_head->parts) {
              parts_cap = ((part / 10) + 1) * 10;
              message_head->parts = malloc(parts_cap * sizeof(struct part));
              if (!message_head->parts) {
                discord_free_messages(messages);
                return NULL;
              }
            }

            message_head->parts[part - 1].idx = part;
            message_head->parts[part - 1].message = message;

            message_head->attachment.size += message->attachment.size;
            break;
          };
        }
      }
    }

    regfree(&part_regex.comp);
    return messages;
  }

  regfree(&part_regex.comp);
  return NULL;
}

void discord_free_channel(struct channel *channel) {
  discord_free_messages(channel->messages);
  free(channel->name);
}

void discord_free_channels(json_array *channels) {
  struct channel *channel;
  json_array *_c = channels;
  json_array_for_each(_c, channel) { discord_free_channel(channel); }
  json_array_destroy(channels);
}

json_array *discord_get_channels(const char *guild_id) {
  char new_url[SIZE];
  snprintf(new_url, SIZE, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
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
