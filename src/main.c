#include "../key.h"
#include "discord/api.h"
#include "discord/discord.h"
#include "util.h"

#if __APPLE__
#include <sys/xattr.h>
#define _FILE_OFFSET_BITS 64
#endif

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <curl/curl.h>
#include <errno.h>
#include <regex.h>
#include <unistd.h>

struct snowflake GUILD_ID;

struct {
  regex_t comp;
  regmatch_t matches[3];
} part_regex;

enum flags {
  CHANNEL = 1 << 0,
  GUILD = 1 << 1,
  LIST = 1 << 2,
  DOWNLOAD = 1 << 3,
};

struct dcfs_state {
  json_array *messages;
  json_array *channels;
  const char *current_channel;
};

int dcfs_rmdir(const char *path) {
  if (count_char(path, '/') != 1) {
    return -EPERM;
  }
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  struct channel *channel;
  json_array *_ch = state->channels;
  json_array_for_each(_ch, channel) {
    if (strcmp(path + 1, channel->name) == 0) {
      struct response resp = {0};
      discord_delete_channel(channel->id.value, &resp);

      if (resp.http_code != 201) {
        fprintf(stderr, "failed to delete %s channel. http code: %d\n",
                channel->name, resp.http_code);
        return -EAGAIN;
      }
      return 0;
    }
  }
  return -ENOENT;
}

int dcfs_mkdir(const char *path, mode_t mode) {
  if (count_char(path, '/') != 1) {
    fprintf(stderr, "can't create directory in a directory\n");
    return -EPERM;
  }

  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  struct response resp = {0};
  discord_create_channel(GUILD_ID.value, path + 1, &resp);

  if (resp.http_code != 201) {
    fprintf(stderr, "failed to create a new channel. http_code: %d\n",
            resp.http_code);
    return -EAGAIN;
  }

  json_object *json = resp.json;

  json_string id = json_object_get(json, "id");
  json_string name = json_object_get(json, "name");
  json_number *type = json_object_get(json, "type");
  json_word *parent = json_object_get(json, "parent_id");
  struct channel new_channel = {
      .type = *type,
      .has_parent = *parent == JSON_NULL ? 0 : 1,
      .name = strdup(name),
  };
  snowflake_init(id, &new_channel.id);

  json_array_push(state->channels, &new_channel, sizeof(struct channel),
                  JSON_UNKNOWN);
  json_object_destroy(json);

  return 0;
};

#ifdef __APPLE__
int dcfs_getxattr(const char *path, const char *name, char *value, size_t size,
                  uint32_t flags) {
  return -ENOTSUP;
}
#endif

#ifdef __APPLE__
int dcfs_getattr(const char *path, struct fuse_darwin_attr *stbuf,
                 struct fuse_file_info *fi)
#else
int dcfs_getattr(const char *path, struct stat *stbuf,
                 struct fuse_file_info *fi)
#endif
{
  int res = 0;
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;
  memset(stbuf, 0, sizeof(struct stat));
#ifdef __APPLE__
  stbuf->uid = getuid();
  stbuf->gid = getgid();
#else
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
#endif

  if (strcmp(path, "/") == 0) {
#ifdef __APPLE__
    stbuf->mode = S_IFDIR | 0755;
    stbuf->size = 4096;
#else
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_size = 4096;
#endif

#ifdef __APPLE__
    stbuf->ctimespec.tv_sec = GUILD_ID.timestamp;
    stbuf->mtimespec.tv_sec = GUILD_ID.timestamp;
#else
    stbuf->st_ctim.tv_sec = GUILD_ID.timestamp;
    stbuf->st_mtim.tv_sec = GUILD_ID.timestamp;
#endif

  } else if (count_char(path, '/') == 1) {

    struct channel *channel;
    json_array *_ch = state->channels;
    json_array_for_each(_ch, channel) {
      if (strcmp(path + 1, channel->name) == 0) {
#ifdef __APPLE__
        stbuf->mode = S_IFDIR | 0755;
        stbuf->size = 0;
        stbuf->ctimespec.tv_sec = channel->id.timestamp;
        stbuf->mtimespec.tv_sec = channel->id.timestamp;
#else
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_size = 0;
        stbuf->st_ctim.tv_sec = channel->id.timestamp;
        stbuf->st_mtim.tv_sec = channel->id.timestamp;
#endif
        return res;
      }
    }
    res = -ENOENT;
  }

  return res;
}

#ifdef __APPLE__
int dcfs_readdir(const char *path, void *buf, fuse_darwin_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi,
                 enum fuse_readdir_flags flags)

#else
int dcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi,
                 enum fuse_readdir_flags flags)
#endif
{

  filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  if (strcmp(path, "/") == 0) {
    if (!state->channels) {
      state->channels = discord_get_channels(GUILD_ID.value);
      if (!state->channels) {
        fprintf(stderr, "failed to get channels of guild %s\n", GUILD_ID.value);
        return -ENOENT;
      }
    }

    struct channel *channel;
    json_array *_c = state->channels;
    json_array_for_each(_c, channel) {
      if (channel->type == GUILD_TEXT && !channel->has_parent) {
#ifdef __APPLE__
        struct fuse_darwin_attr st;
        st.mode = S_IFDIR;
#else
        struct stat st;
        st.st_mode = S_IFDIR;
#endif
        filler(buf, channel->name, &st, 0, FUSE_FILL_DIR_DEFAULTS);
      }
    }
  }

  return 0;
}

void dcfs_destroy(void *data) {
  struct dcfs_state *state = data;

  if (state->channels)
    discord_free_channels(state->channels);

  if (state->messages)
    discord_free_messages(state->messages);
}

json_array *get_messages(const char *channel_id) {
  regcomp(&part_regex.comp, "(.+)\\.PART([0-9]+)", REG_EXTENDED);

  json_array *messages = discord_get_messages(channel_id);
  if (messages) {
    struct message *message;

    // decrypt filenames
    json_array *_messages = messages;
    json_array_for_each(_messages, message) {
      char new_filename[512];
      strcpy(new_filename, message->attachment.filename);

      size_t decoded_size = filename_decode(new_filename, 512);
      filename_decrypt(KEY, new_filename, decoded_size);

      free(message->attachment.filename);
      message->attachment.filename = strdup(new_filename);
    }

    // find parts
    _messages = messages;
    json_array_for_each(_messages, message) {
      int ret = regexec(&part_regex.comp, message->attachment.filename,
                        sizeof(part_regex.matches) / sizeof(regmatch_t),
                        part_regex.matches, 0);

      if (ret == 0) {
        regmatch_t m_body = part_regex.matches[1];
        regmatch_t m_part = part_regex.matches[2];
        int part = str_to_int(message->attachment.filename + m_part.rm_so,
                              m_part.rm_eo - m_part.rm_so);

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

            if (message_head->parts) {
              message_head->parts = realloc(
                  message->parts, message->parts_n * sizeof(struct part));
            } else {
              message_head->parts = malloc(sizeof(struct part));
              if (!message_head->parts) {
                discord_free_messages(messages);
                return NULL;
              }
            }

            message_head->parts[message_head->parts_n - 1].id = part;
            message_head->parts[message_head->parts_n - 1].message = message;

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

int main(int argc, char *argv[]) {
  struct fuse_operations operations = {
      .readdir = dcfs_readdir,
      .getattr = dcfs_getattr,
#ifdef __APPLE__
      .getxattr = dcfs_getxattr,
#endif
      .mkdir = dcfs_mkdir,
      .rmdir = dcfs_rmdir,
      .destroy = dcfs_destroy,

  };

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct stat stbuf;
  struct fuse_cmdline_opts opts;

  if (get_auth_token() == NULL) {
    fprintf(stderr, "DCFS_TOKEN isn't set\n");
    return 1;
  }

  char *guild_id = get_guild_id();
  if (!guild_id) {
    fprintf(stderr, "DCFS_GUILD_ID isn't set\n");
    return 1;
  }
  snowflake_init(guild_id, &GUILD_ID);

  if (fuse_parse_cmdline(&args, &opts) != 0)
    return 1;
  fuse_opt_free_args(&args);

  if (!opts.mountpoint) {
    fprintf(stderr, "missing mountpoint parameter\n");
    return 1;
  }

  if (stat(opts.mountpoint, &stbuf) == -1) {
    fprintf(stderr, "failed to access mountpoint %s: %s\n", opts.mountpoint,
            strerror(errno));
    free(opts.mountpoint);
    return 1;
  }
  free(opts.mountpoint);

  struct dcfs_state state = {
      .channels = NULL,
      .messages = NULL,
  };

  return fuse_main(argc, argv, &operations, &state);
}
