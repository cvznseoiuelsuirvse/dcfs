#include "discord.h"
#include "util.h"

#include <assert.h>
#include <regex.h>
#include <sys/stat.h>

static struct {
  regex_t comp;
  regmatch_t matches[3];
} part_regex;

void discord_free_message(struct dcfs_message *message) {
  free(message->url);
  if (message->content)
    free(message->content);
}
void discord_free_messages(json_array *messages) {
  struct dcfs_message *message;
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

    char new_url[DISCORD_SIZE];
    if (messages_n == -1) {
      snprintf(new_url, DISCORD_SIZE, "%s/%s/%s/%s?limit=100",
               DISCORD_API_BASE_URL, "channels", channel_id, "messages");

    } else {
      int idx = json_array_size(messages) - 1;
      struct dcfs_message *last_message = json_array_get(messages, idx);

      if (!last_message) {
        printf("WARNING: no message found at index %d\n", idx);
        free(resp.raw);
        json_array_destroy(messages);
        return NULL;
      }

      snprintf(new_url, DISCORD_SIZE, "%s/%s/%s/%s?limit=100&before=%s",
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

    if (!json)
      return NULL;

    messages_n = json_array_size(json);

    json_object *o;
    json_array_for_each(json, o) {
      json_string message_id = json_object_get(o, "id");

      json_object *attachment;
      json_array *attachments = json_object_get(o, "attachments");
      json_array_for_each(attachments, attachment) {
        struct dcfs_message message;
        memset(&message, 0, sizeof(struct dcfs_message));

        discord_snowflake_init(message_id, &message.id);

        json_string filename = json_object_get(attachment, "filename");
        json_number *size = json_object_get(attachment, "size");
        json_string url = json_object_get(attachment, "url");

        assert(b64decode(message.filename, filename,
                         sizeof(message.filename)) == 0);

        message.size = *size;
        message.url = strdup(url);
        message.mode = S_IFREG | 0644;
        message.gid = getgid();
        message.uid = getuid();

        json_array_push(messages, &message, sizeof(struct dcfs_message),
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
    struct dcfs_message *message;
    json_array *_messages = messages;
    json_array_for_each(_messages, message) {
      int ret = regexec(&part_regex.comp, message->filename,
                        sizeof(part_regex.matches) / sizeof(regmatch_t),
                        part_regex.matches, 0);

      if (ret == 0) {
        regmatch_t m_body = part_regex.matches[1];
        regmatch_t m_part = part_regex.matches[2];
        size_t part = strtol(message->filename + m_part.rm_so, NULL, 10);

        if (part >= DISCORD_MAX_PARTS) {
          discord_free_messages(messages);
          return NULL;
        }

        size_t body_size = m_body.rm_eo - m_body.rm_so;
        char body[body_size + 1];
        memcpy(body, message->filename + m_body.rm_so, body_size);
        body[body_size] = 0;
        message->is_part = 1;

        json_array *_messages1 = messages;
        struct dcfs_message *message_head;
        json_array_for_each(_messages1, message_head) {
          if (strcmp(message_head->filename, body) == 0) {
            message_head->parts_n++;
            message_head->parts[part - 1] = message;
            message_head->size += message->size;
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

void discord_free_channel(struct dcfs_channel *channel) {
  discord_free_messages(channel->messages);
}

void discord_free_channels(json_array *channels) {
  struct dcfs_channel *channel;
  json_array *_c = channels;
  json_array_for_each(_c, channel) { discord_free_channel(channel); }
  json_array_destroy(channels);
}

json_array *discord_get_channels(const char *guild_id) {
  char new_url[DISCORD_SIZE];
  snprintf(new_url, DISCORD_SIZE, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
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

  if (!json)
    return NULL;

  json_object *o;
  json_array_for_each(json, o) {
    json_string id = json_object_get(o, "id");
    json_string name = json_object_get(o, "name");
    json_number *type = json_object_get(o, "type");
    json_word *parent = json_object_get(o, "parent_id");

    struct dcfs_channel channel;
    memset(&channel, 0, sizeof(struct dcfs_channel));

    discord_snowflake_init(id, &channel.id);
    strcpy(channel.name, name);

    channel.type = *type;
    channel.has_parent = *parent == JSON_NULL ? 0 : 1;
    channel.mode = S_IFDIR | 0755;
    channel.gid = getgid();
    channel.uid = getuid();

    json_array_push(channels, &channel, sizeof(struct dcfs_channel),
                    JSON_UNKNOWN);
  }

  json_array_destroy(json);
  return channels;
}

int discord_create_channel(const char *guild_id, const char *name,
                           struct response *resp) {
  int res = 0;
  char *payload =
      "{\"name\": \"%s\", \"type\": 0, \"permission_overwrites\": [{\"id\": "
      "\"%s\", \"type\": 0, \"allow\": \"0\", \"deny\": \"1024\"}]}";
  char new_payload[DISCORD_SIZE];
  snprintf(new_payload, DISCORD_SIZE, payload, name, guild_id);

  char new_url[DISCORD_SIZE];
  snprintf(new_url, DISCORD_SIZE, "%s/%s/%s/%s", DISCORD_API_BASE_URL, "guilds",
           guild_id, "channels");

  if (request_post(new_url, new_payload, resp, 1) != 0)
    res = 1;

  return res;
};

int discord_rename_channel(const char *channel_id, const char *name,
                           struct response *resp) {

  int res = 0;

  char new_payload[DISCORD_SIZE];
  snprintf(new_payload, DISCORD_SIZE, "{\"name\": \"%s\"}", name);

  char new_url[DISCORD_SIZE];
  snprintf(new_url, DISCORD_SIZE, "%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_patch(new_url, new_payload, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}

int discord_delete_channel(const char *channel_id, struct response *resp) {
  int res = 0;

  char new_url[DISCORD_SIZE];
  snprintf(new_url, DISCORD_SIZE, "%s/%s/%s", DISCORD_API_BASE_URL, "channels",
           channel_id);

  if (request_delete(new_url, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}

int discord_create_attachments(const char *channel_id, const struct file *files,
                               size_t files_n, struct response *resp) {
  int res = 0;

  char new_url[DISCORD_SIZE];
  snprintf(new_url, DISCORD_SIZE, "%s/%s/%s/%s", DISCORD_API_BASE_URL,
           "channels", channel_id, "messages");

  if (request_post_files(new_url, files, files_n, resp) != 0)
    res = 1;

  return res;
}

int discord_delete_messsage(const char *channel_id, const char *message_id,
                            struct response *resp) {
  int res = 0;

  char new_url[DISCORD_SIZE];
  snprintf(new_url, DISCORD_SIZE, "%s/%s/%s/%s/%s", DISCORD_API_BASE_URL,
           "channels", channel_id, "messages", message_id);

  if (request_delete(new_url, resp, 1) != 0)
    res = 1;

  free(resp->raw);
  return res;
}
