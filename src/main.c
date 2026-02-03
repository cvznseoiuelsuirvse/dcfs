#include "discord/api.h"
#include "discord/discord.h"
#include "util.h"
#include <sys/xattr.h>

#if __APPLE__
#define _FILE_OFFSET_BITS 64
#endif

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <curl/curl.h>
#include <errno.h>
#include <regex.h>
#include <stdarg.h>
#include <unistd.h>

#define FILENAME (path) + (last_index((path), '/') + 1)

#ifndef MAX_FILESIZE
#define MAX_FILESIZE 8388608
#endif

struct snowflake GUILD_ID;

struct {
  regex_t comp;
  regmatch_t matches[3];
} part_regex;

void print_err(const char *format, ...) {
  va_list list;
  va_start(list, format);

  fprintf(stderr, "\033[31;1mERROR\033[0m ");
  vfprintf(stderr, format, list);

  va_end(list);
}

void print_inf(const char *format, ...) {
  va_list list;
  va_start(list, format);

  printf("\033[34;1mINFO\033[0m ");
  vprintf(format, list);

  va_end(list);
}

json_array *get_messages(const char *channel_id) {
  regcomp(&part_regex.comp, "(.+)\\.PART([0-9]+)", REG_EXTENDED);

  json_array *messages = discord_get_messages(channel_id);
  if (messages) {
    struct message *message;

    json_array *_messages = messages;
    int i = 0;
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

            message_head->parts[message_head->parts_n - 1].idx = part;
            message_head->parts[message_head->parts_n - 1].guild_idx = i;
            message_head->parts[message_head->parts_n - 1].message = message;

            message_head->attachment.size += message->attachment.size;
            break;
          };
        }
      }
      i++;
    }

    regfree(&part_regex.comp);
    return messages;
  }

  regfree(&part_regex.comp);
  return NULL;
}

struct dcfs_state {
  json_array *messages;
  json_array *channels;
  struct channel *current_channel;
};

int dcfs_rmdir(const char *path) {
  if (count_char(path, '/') != 1) {
    return -EPERM;
  }
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  struct channel *channel;
  json_array *_ch = state->channels;
  int i = 0;
  json_array_for_each(_ch, channel) {
    if (strcmp(path + 1, channel->name) == 0) {
      struct response resp = {0};
      discord_delete_channel(channel->id.value, &resp);

      if (resp.http_code != 200) {
        fprintf(stderr, "failed to delete %s channel. http code: %d\n",
                channel->name, resp.http_code);
        return -EAGAIN;
      }

      discord_free_channel(channel);
      json_array_remove(&state->channels, i);

      return 0;
    }
    i++;
  }
  return -ENOENT;
}

int dcfs_mkdir(const char *path, mode_t mode) {
  // if (count_char(path, '/') != 1) {
  //   fprintf(stderr, "can't create directory in a directory\n");
  //   return -EPERM;
  // }

  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  struct response resp = {0};
  discord_create_channel(GUILD_ID.value, path + 1, &resp);

  if (resp.http_code != 201) {
    fprintf(stderr, "failed to create a new channel. http_code: %d\n",
            resp.http_code);
    return -EAGAIN;
  }

  json_object *json;
  json_load(resp.raw, (void **)&json);
  free(resp.raw);

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

#ifdef HAVE_SETXATTR
int dcfs_getxattr(const char *path, const char *name, char *value, size_t size,
                  uint32_t flags) {
  return -ENOTSUP;
}

#ifdef __APPLE__
int dcfs_setxattr(const char *path, const char *name, const char *value,
                  size_t size, int flags, uint32_t position) {
  return 0;
}
#else
int dcfs_setxattr(const char *path, const char *name, const char *value,
                  size_t size, int flags) {
  return 0;
}
#endif /* __APPLE__ */

#endif /* HAVE_SETXATTR */

#ifdef __APPLE__
int dcfs_setattr(const char *path, struct fuse_darwin_attr *stbuf, int valid,
                 struct fuse_file_info *fi) {
  print_inf("%d\n", valid);
  return -ENOSYS;
};
#endif /* __APPLE__ */

#ifdef __APPLE__
int dcfs_getattr(const char *path, struct fuse_darwin_attr *stbuf,
                 struct fuse_file_info *fi)
#else
int dcfs_getattr(const char *path, struct stat *stbuf,
                 struct fuse_file_info *fi)
#endif
{
  int res = 0;
  const char *filename = FILENAME;
  if (filename[0] == '.' && filename[1] == '_') {
    return -ENOENT;
  }

  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;
  memset(stbuf, 0, sizeof(struct stat));
#ifdef __APPLE__
  stbuf->uid = getuid();
  stbuf->gid = getgid();
#else
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
#endif /* __APPLE__ */

  if (strcmp(path, "/") == 0) {
#ifdef __APPLE__
    stbuf->mode = S_IFDIR | 0755;
    stbuf->size = 4096;
#else
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_size = 4096;
#endif /* __APPLE__ */

#ifdef __APPLE__
    stbuf->ctimespec.tv_sec = GUILD_ID.timestamp;
    stbuf->mtimespec.tv_sec = GUILD_ID.timestamp;
#else
    stbuf->st_ctim.tv_sec = GUILD_ID.timestamp;
    stbuf->st_mtim.tv_sec = GUILD_ID.timestamp;
#endif /* __APPLE__ */

    return 0;

  } else if (count_char(path, '/') == 1) {
    struct channel *channel;
    json_array *_ch = state->channels;
    json_array_for_each(_ch, channel) {
      if (strcmp(filename, channel->name) == 0) {
        if (!state->current_channel ||
            strcmp(filename, state->current_channel->name) != 0) {

          if (state->messages)
            discord_free_messages(state->messages);

          state->messages = get_messages(channel->id.value);
          state->current_channel = channel;
        }

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
#endif /* __APPLE__ */

        return res;
      }
    }
    res = -ENOENT;

  } else if (count_char(path, '/') == 2) {
    struct message *message;

    json_array *_m = state->messages;
    json_array_for_each(_m, message) {
      if (strcmp(filename, message->attachment.filename) == 0) {
#ifdef __APPLE__
        stbuf->mode = S_IFREG | 0644;
        stbuf->size = message->attachment.size;
        stbuf->ctimespec.tv_sec = message->id.timestamp;
        stbuf->mtimespec.tv_sec = message->id.timestamp;
#else
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_size = message->attachment.size;
        stbuf->st_ctim.tv_sec = message->id.timestamp;
        stbuf->st_mtim.tv_sec = message->id.timestamp;
#endif /* __APPLE__ */
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
#endif /* __APPLE__ */
{

  filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  if (strcmp(path, "/") == 0) {
    if (!state->channels) {
      state->channels = discord_get_channels(GUILD_ID.value);
      if (!state->channels) {
        print_err("failed to get channels of guild %s\n", GUILD_ID.value);
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
#endif /* __APPLE__ */
        filler(buf, channel->name, &st, 0, FUSE_FILL_DIR_DEFAULTS);
      }
    }
  } else {

    struct channel *channel;
    json_array *_c = state->channels;

    json_array_for_each(_c, channel) {
      if (strcmp(path + 1, channel->name) == 0) {
        if (state->messages)
          discord_free_messages(state->messages);

        state->messages = get_messages(channel->id.value);
      }
    }

    if (!state->messages) {
      fprintf(stderr, "failed to get messages\n");
      return -ENOENT;
    }

    json_array *_m = state->messages;
    struct message *message;
    json_array_for_each(_m, message) {
      if (!message->is_part) {
#ifdef __APPLE__
        struct fuse_darwin_attr st;
        st.mode = S_IFREG;
#else
        struct stat st;
        st.st_mode = S_IFREG;
#endif /* __APPLE__ */
        filler(buf, message->attachment.filename, &st, 0,
               FUSE_FILL_DIR_DEFAULTS);
      }
    }
  }

  return 0;
}

int dcfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  const char *filename = FILENAME;
  if (strcmp(path, "/") == 0 || (filename[0] == '.' && filename[1] == '_')) {
    return -EPERM;
  }

  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  struct message message;
  print_inf("trying to create %s\n", filename);

  message.content = NULL;
  message.content_size = 0;
  message.is_part = 0;
  message.parts = NULL;
  message.parts_n = 0;

  message.attachment.filename = strdup(filename);
  message.attachment.size = 0;
  message.attachment.url = NULL;

  json_array_push(state->messages, &message, sizeof(struct message),
                  JSON_UNKNOWN);

  return 0;
}

int dcfs_chown(const char *path, uid_t uid, gid_t gid,
               struct fuse_file_info *fi) {
  const char *filename = FILENAME;
  print_inf("dcfs_chown: %s\n", filename);

  return 0;
};

int dcfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
  const char *filename = FILENAME;
  print_inf("dcfs_chmod: %s\n", filename);
  return 0;
};

int dcfs_open(const char *path, struct fuse_file_info *fi) {
  const char *filename = FILENAME;
  print_inf("dcfs_open: %s\n", filename);

  return 0;
}

int dcfs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {

  const char *filename = FILENAME;
  print_inf("dcfs_write: %s\n", filename);

  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  struct message *message;
  json_array *_m = state->messages;
  json_array_for_each(_m, message) {
    if (strcmp(filename, message->attachment.filename) == 0) {
      if (message->content_size > MAX_FILESIZE) {
        return -EFBIG;
      }

      message->content_size += size;

      if (!message->content) {
        message->content = malloc(size);

        if (!message->content) {
          print_err("dcfs_write: failed to malloc\n");
          return -1;
        }
      } else {
        message->content = realloc(message->content, message->content_size);
      }

      if (offset < message->content_size) {
        if (offset + size > message->content_size) {
          size = message->content_size - offset;
        }

        memcpy(message->content + offset, buf, size);

      } else {
        size = 0;
      }

      return size;
    }
  }

  return -ENOENT;
}

int dcfs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  if (!state->messages)
    return -ENODATA;

  const char *filename = FILENAME;
  print_inf("dcfs_read: %s\n", filename);

  struct message *message;
  json_array *_m = state->messages;
  json_array_for_each(_m, message) {
    if (strcmp(message->attachment.filename, filename) == 0) {
      if (!message->content) {
        struct response resp = {0};
        request_get(message->attachment.url, &resp, 0);
        message->content = resp.raw;
        message->content_size = resp.size;

        for (size_t i = 0; i < message->parts_n; i++) {
          struct part part = message->parts[i];
          resp = (struct response){0};

          request_get(part.message->attachment.url, &resp, 0);

          message->content =
              realloc(message->content, message->content_size + resp.size);
          memcpy(message->content + message->content_size, resp.raw, resp.size);
          message->content_size += resp.size;
        }
      }

      if (offset < message->content_size) {
        if (offset + size > message->content_size) {
          size = message->content_size - offset;
        }

        memcpy(buf, message->content + offset, size);

      } else {
        size = 0;
      }

      return size;
    }
  }
  return -1;
}

int dcfs_release(const char *path, struct fuse_file_info *fi) {
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;
  const char *filename = FILENAME;

  struct message *message;
  json_array *_m = state->messages;
  print_inf("dcfs_release: %s\n", filename);

  int i = 0;
  json_array_for_each(_m, message) {
    if (strcmp(message->attachment.filename, filename) == 0) {
      if (!message->attachment.url) {
        print_inf("trying to upload new file: %s\n", filename);

        struct response resp = {0};
        discord_create_message(state->current_channel->id.value, filename,
                               message->content, message->content_size, &resp);

        if (message->content) {
          free(message->content);
          message->content = NULL;
          message->content_size = 0;
        }

        if (resp.http_code != 200) {
          print_err("failed to upload file %s. error code: %d\n", filename,
                    resp.http_code);
          json_array_remove(&state->messages, i);

        } else {
          json_object *json;
          json_load(resp.raw, (void **)&json);
          free(resp.raw);

          json_string message_id = json_object_get(json, "id");
          snowflake_init(message_id, &message->id);
          message->is_part = 0;
          message->parts_n = 0;
          message->parts = NULL;

          json_array *attachments = json_object_get(json, "attachments");
          json_object *attachment = json_array_get(attachments, 0);

          json_string filename = json_object_get(attachment, "filename");
          json_number *size = json_object_get(attachment, "size");
          json_string url = json_object_get(attachment, "url");

          message->attachment.filename = strdup(filename);
          message->attachment.size = *size;
          message->attachment.url = strdup(url);

          json_object_destroy(json);
        }
      }
    }
    i++;
  }
  return 0;
}
int dcfs_unlink(const char *path) {
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;
  const char *filename = FILENAME;

  print_inf("dcfs_unlink: %s\n", filename);
  json_array *_m = state->messages;
  struct message *message;
  int i = 0;
  json_array_for_each(_m, message) {
    if (strcmp(filename, message->attachment.filename) == 0) {
      struct response resp = {0};
      discord_delete_messsage(state->current_channel->id.value,
                              message->id.value, &resp);

      if (resp.http_code != 204) {
        print_err("failed to delete message %s\n", message->id.value);
        return -EAGAIN;
      }

      discord_free_message(message);
      json_array_remove(&state->messages, i);
      return 0;
    }
    i++;
  }

  return -EAGAIN;
}

void dcfs_destroy(void *data) {
  struct dcfs_state *state = data;

  if (state->channels)
    discord_free_channels(state->channels);

  if (state->messages)
    discord_free_messages(state->messages);
}

int main(int argc, char *argv[]) {
  struct fuse_operations operations = {
      .readdir = dcfs_readdir,
      .getattr = dcfs_getattr,
      .chown = dcfs_chown,
      .chmod = dcfs_chmod,
#ifdef HAVE_SETXATTR
      .getxattr = dcfs_getxattr,
      .setxattr = dcfs_setxattr,
#endif /* HAVE_SETXATTR */
      .mkdir = dcfs_mkdir,
      .rmdir = dcfs_rmdir,
      .unlink = dcfs_unlink,
      .create = dcfs_create,
      .open = dcfs_open,
      .read = dcfs_read,
      .write = dcfs_write,
      .release = dcfs_release,
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
