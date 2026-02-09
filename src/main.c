#include "discord/api.h"
#include "discord/discord.h"
#include "util.h"

#if __APPLE__
#define _FILE_OFFSET_BITS 64
#endif

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <zlib.h>

#define STREQ(s1, s2) (strcmp((s1), (s2)) == 0)
#define GET_CURRENT_CHANNEL(ch)                                                \
  json_array *_c = (state)->channels;                                          \
  json_array_for_each(_c, (ch)) {                                              \
    if (STREQ((ch)->name, (p).dir))                                            \
      break;                                                                   \
  }

#ifndef MAX_FILESIZE
#define MAX_FILESIZE 10485760
#endif

static const char *program_name;
static struct snowflake GUILD_ID;

struct path {
  char dir[128];
  char filename[256];
};

struct dcfs_state {
  json_array *channels;
};

static void path_init(const char *path, struct path *p) {
  memset(p, 0, sizeof(struct path));

  if (!STREQ(path, "/")) {
    int last_slash_idx = last_index(path, '/');

    if (last_slash_idx > 0) {
      memcpy(p->dir, path + 1, last_slash_idx - 1);
      memcpy(p->filename, path + (last_slash_idx + 1),
             strlen(path) - last_slash_idx);
    } else {
      memcpy(p->dir, path + 1, strlen(path) - 1);
    }
  }
}

static inline struct dcfs_state *get_state() {
  return (fuse_get_context())->private_data;
};

static inline struct channel *get_channel(json_array *channels,
                                          struct path *path) {
  struct channel *_ch;
  json_array_for_each(channels, _ch) {
    if (STREQ(_ch->name, path->dir)) {
      return _ch;
    }
  }
  return NULL;
}

static void print_err(const char *format, ...) {
  va_list list;
  va_start(list, format);

  fprintf(stderr, "%s: \033[31;1mERROR\033[0m ", program_name);
  vfprintf(stderr, format, list);

  va_end(list);
}

static void print_inf(const char *format, ...) {
  va_list list;
  va_start(list, format);

  printf("%s \033[34;1mINFO\033[0m ", program_name);
  vprintf(format, list);

  va_end(list);
}

static void print_op(const char *op, struct path *path) {
  if (!*path->dir && !*path->filename) {
    printf("\033[35;1mOPERATION \033[37m%s\033[0m: /\n", op);
  } else if (!*path->filename) {
    printf("\033[35;1mOPERATION \033[37m%s\033[0m: /%s\n", op, path->dir);
  } else {
    printf("\033[35;1mOPERATION \033[37m%s\033[0m: /%s/%s\n", op, path->dir,
           path->filename);
  }
}

static int process_files(struct channel *channel, struct message *head,
                         struct file *files, size_t files_n) {
  struct response resp = {0};
  discord_create_attachments(channel->id.value, files, files_n, &resp);

  if (resp.http_code != 200) {
    print_err("failed to upload file %s. error code: %d\n",
              head->attachment.filename, resp.http_code);
    json_array_remove_ptr(&channel->messages, head);
    free(resp.raw);

    return -EAGAIN;
  }

  json_object *json;
  json_load(resp.raw, (void **)&json);
  free(resp.raw);

  json_string message_id = json_object_get(json, "id");

  json_object *attachment;
  json_array *attachments = json_object_get(json, "attachments");
  json_array_for_each(attachments, attachment) {
    json_string filename = json_object_get(attachment, "filename");
    json_number *size = json_object_get(attachment, "size");
    json_string url = json_object_get(attachment, "url");

    if (STREQ(filename, head->attachment.filename)) {
      snowflake_init(message_id, &head->id);
      head->attachment.size = *size;
      head->attachment.url = strdup(url);

    } else {
      struct message part_message = {
          .is_part = 1,
          .content = NULL,
      };
      snowflake_init(message_id, &part_message.id);

      memset(part_message.attachment.filename, 0,
             sizeof(part_message.attachment.filename));
      assert(b64decode(filename, part_message.attachment.filename,
                       sizeof(part_message.attachment.filename)) == 0);
      part_message.attachment.size = *size;
      part_message.attachment.url = strdup(url);
      head->attachment.size += *size;

      int part_n_start = last_index(filename, 'T');
      size_t part_n = strtol(filename + part_n_start + 1, NULL, 10);

      head->parts[part_n - 1] =
          json_array_push(channel->messages, &part_message,
                          sizeof(struct message), JSON_UNKNOWN);
      head->parts_n++;
    }
  }

  json_object_destroy(json);
  return 0;
}

static int upload_file(struct channel *channel, const char *filename) {
  int ret = -ENOENT;
  struct message *message;
  json_array *_m = channel->messages;

  json_array_for_each(_m, message) {
    if (STREQ(message->attachment.filename, filename)) {
      ret = -ENODATA;
      if (!message->attachment.url && message->content) {
        ret = 0;
        int files_n = 0;
        struct file files[10];
        memset(&files, 0, sizeof(files));

        if (message->content_size / MAX_FILESIZE >= MAX_PARTS) {
          ret = -EFBIG;
          goto out;
        }

        for (size_t offset = 0; offset < message->content_size;
             offset += MAX_FILESIZE, files_n++) {

          if (files_n > 0 && files_n % 10 == 0) {
            if ((ret = process_files(channel, message, files, 10)) != 0)
              goto out;
            memset(&files, 0, sizeof(files));
          }

          struct file *file = &files[files_n % 10];

          print_inf("uploading file %s\n", filename);
          memset(file->filename, 0, sizeof(file->filename));
          if (offset == 0) {
            b64encode(filename, file->filename, sizeof(file->filename));
          } else {
            char tmp_filename[256];
            snprintf(tmp_filename, sizeof(tmp_filename), "%s.PART%d", filename,
                     files_n);
            b64encode(tmp_filename, file->filename, sizeof(file->filename));
          }
          print_inf("encoded %s\n", file->filename);

          file->buffer = message->content + offset;
          size_t remaining = message->content_size - offset;
          file->buffer_size =
              remaining < MAX_FILESIZE ? remaining : MAX_FILESIZE;
        }

        if (files_n > 0) {
          ret = process_files(channel, message, files, files_n % 10);
        }

      out:
        free(message->content);
        message->content = NULL;
        message->content_size = 0;
      }
      break;
    }
  }
  return ret;
}

static int delete_file(struct channel *channel, const char *filename) {
  json_array *_m = channel->messages;
  struct message *message;

  json_array_for_each(_m, message) {
    if (STREQ(filename, message->attachment.filename)) {
      struct response resp = {0};
      discord_delete_messsage(channel->id.value, message->id.value, &resp);

      int last_deleted_message_id = hash_string(message->id.value);
      for (size_t i = 0; i < message->parts_n; i++) {
        struct message *part = message->parts[i];
        if (hash_string(part->id.value) != last_deleted_message_id) {
          discord_delete_messsage(channel->id.value, part->id.value, &resp);
          last_deleted_message_id = hash_string(part->id.value);
        }
        discord_free_message(part);
        json_array_remove_ptr(&channel->messages, part);
      }

      discord_free_message(message);
      json_array_remove_ptr(&channel->messages, message);

      return 0;
    }
  }

  return -EAGAIN;
};

int dcfs_rmdir(const char *path) {
  if (count_char(path, '/') != 1) {
    return -EPERM;
  }
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_rmdir", &p);

  struct channel *channel;
  json_array *_ch = state->channels;
  json_array_for_each(_ch, channel) {
    if (STREQ(path + 1, channel->name)) {
      struct response resp = {0};
      discord_delete_channel(channel->id.value, &resp);

      if (resp.http_code != 200) {
        fprintf(stderr, "failed to delete %s channel. http code: %ld\n",
                channel->name, resp.http_code);
        return -EAGAIN;
      }

      discord_free_channel(channel);
      json_array_remove_ptr(&state->channels, channel);

      return 0;
    }
  }
  return -ENOENT;
}

int dcfs_mkdir(const char *path, mode_t mode) {
  if (count_char(path, '/') != 1) {
    return -EPERM;
  }

  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_mkdir", &p);

  struct response resp = {0};
  discord_create_channel(GUILD_ID.value, path + 1, &resp);

  if (resp.http_code != 201) {
    fprintf(stderr, "failed to create a new channel. http_code: %ld\n",
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
  };

  snowflake_init(id, &new_channel.id);
  strcpy(new_channel.name, name);

  json_array_push(state->channels, &new_channel, sizeof(struct channel),
                  JSON_UNKNOWN);
  json_object_destroy(json);

  return 0;
};

#ifdef __APPLE__
int dcfs_getxattr(const char *path, const char *name, char *value, size_t size,
                  uint32_t flags) {
  return 0;
}
#else
int dcfs_getxattr(const char *path, const char *name, char *value,
                  size_t size) {
  return 0;
}

#endif

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

#ifdef __APPLE__
int dcfs_getattr(const char *path, struct fuse_darwin_attr *stbuf,
                 struct fuse_file_info *fi)
#else
int dcfs_getattr(const char *path, struct stat *stbuf,
                 struct fuse_file_info *fi)
#endif
{
  int res = 0;

  struct dcfs_state *state = get_state();

  struct path p = {0};
  path_init(path, &p);
  print_op("dcfs_getattr", &p);

  memset(stbuf, 0, sizeof(struct stat));

#ifdef __APPLE__
  stbuf->uid = getuid();
  stbuf->gid = getgid();
#else
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();
#endif /* __APPLE__ */

  if (STREQ(path, "/")) {
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

    return res;

  } else if (count_char(path, '/') == 1) {
    struct channel *channel;
    json_array *_ch = state->channels;
    json_array_for_each(_ch, channel) {
      if (STREQ(p.dir, channel->name)) {
        if (!channel->messages)
          channel->messages = discord_get_messages(channel->id.value);

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
    struct channel *channel;
    GET_CURRENT_CHANNEL(channel);

    json_array *_m = channel->messages;
    struct message *message;
    json_array_for_each(_m, message) {
      if (STREQ(p.filename, message->attachment.filename)) {
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
  struct dcfs_state *state = get_state();

  struct path p = {0};
  path_init(path, &p);
  print_op("dcfs_readdir", &p);

  filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);

  if (STREQ(path, "/")) {
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
    GET_CURRENT_CHANNEL(channel);

    if (!channel->messages) {
      channel->messages = discord_get_messages(channel->id.value);
      if (!channel->messages) {
        fprintf(stderr, "failed to get %s channel messages\n",
                channel->id.value);
        return -ENOENT;
      }
    }

    struct message *message;
    json_array *_m = channel->messages;
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
  if (count_char(path, '/') != 2) {
    return -EPERM;
  }
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_create", &p);

  struct channel *channel;
  GET_CURRENT_CHANNEL(channel);

  struct message message;

  message.content = NULL;
  message.content_size = 0;
  message.parts_n = 0;
  message.is_part = 0;

  strcpy(message.attachment.filename, p.filename);
  message.attachment.size = 0;
  message.attachment.url = NULL;

  json_array_push(channel->messages, &message, sizeof(struct message),
                  JSON_UNKNOWN);
  return 0;
}

int dcfs_chown(const char *path, uid_t uid, gid_t gid,
               struct fuse_file_info *fi) {
  // print_inf("dcfs_chown: %s\n", filename);

  return 0;
};

int dcfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
  // const char *filename = FILENAME;
  // print_inf("dcfs_chmod: %s\n", filename);
  return 0;
};

int dcfs_open(const char *path, struct fuse_file_info *fi) {
  // const char *filename = FILENAME;
  // print_inf("dcfs_open: %s\n", filename);

  return 0;
}

int dcfs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {

  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_write", &p);

  struct channel *channel;
  GET_CURRENT_CHANNEL(channel);

  struct message *message;
  json_array *_m = channel->messages;
  json_array_for_each(_m, message) {
    if (STREQ(p.filename, message->attachment.filename)) {
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
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_read", &p);

  struct channel *channel;
  GET_CURRENT_CHANNEL(channel);

  struct message *message;
  json_array *_m = channel->messages;
  json_array_for_each(_m, message) {
    if (STREQ(message->attachment.filename, p.filename)) {
      if (!message->content) {
        struct response resp = {0};
        request_get(message->attachment.url, &resp, 0);
        message->content = resp.raw;
        message->content_size = resp.size;

        for (size_t i = 0; i < message->parts_n; i++) {
          struct message *part = message->parts[i];
          print_inf("reading part: %s", part->attachment.filename);
          resp = (struct response){0};

          request_get(part->attachment.url, &resp, 0);

          message->content =
              realloc(message->content, message->content_size + resp.size);
          memcpy(message->content + message->content_size, resp.raw, resp.size);
          message->content_size += resp.size;
          free(resp.raw);
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
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_release", &p);

  struct channel *channel;
  GET_CURRENT_CHANNEL(channel);

  return upload_file(channel, p.filename);
}

int dcfs_unlink(const char *path) {
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_unlink", &p);

  struct channel *channel;
  GET_CURRENT_CHANNEL(channel);

  return delete_file(channel, p.filename);
}

int dcfs_rename(const char *from, const char *to, unsigned int flags) {
  int ret = 0;
  struct dcfs_state *state = get_state();

  struct path p_from;
  path_init(from, &p_from);
  struct channel *old_channel = get_channel(state->channels, &p_from);

  struct path p_to;
  path_init(to, &p_to);
  struct channel *new_channel = get_channel(state->channels, &p_to);

  print_op("dcfs_rename", &p_from);
  print_op("dcfs_rename", &p_to);

  if (*p_from.dir && !*p_from.filename && *p_to.dir && !*p_to.filename) {
    struct response resp = {0};
    discord_rename_channel(old_channel->id.value, p_to.dir, &resp);

    if (resp.http_code != 200) {
      ret = -EAGAIN;
      goto out;
    }

    memset(old_channel->name, 0, sizeof(old_channel->name));
    strcpy(old_channel->name, p_to.dir);

  } else if (*p_from.dir && *p_from.filename && *p_to.dir && *p_to.filename) {
    if (STREQ(p_from.dir, p_to.dir)) {
      ret = -ENOSYS;
      goto out;
    }

    if (!old_channel || !new_channel) {
      ret = -ENOENT;
      goto out;
    }

    struct message new_message;
    new_message.parts_n = 0;
    new_message.is_part = 0;

    strcpy(new_message.attachment.filename, p_to.filename);
    new_message.attachment.size = 0;
    new_message.attachment.url = NULL;

    struct message *old_message;
    json_array *_old_m = old_channel->messages;
    json_array_for_each(_old_m, old_message) {
      if (STREQ(old_message->attachment.filename, p_from.filename)) {
        new_message.content = malloc(old_message->attachment.size);
        if (!new_message.content) {
          ret = -1;
          goto out;
        }

        int offset = 0;
        int bytes_read = 0;
        while ((bytes_read = dcfs_read(from, new_message.content + offset, 4096,
                                       offset, NULL)) > 0)
          offset += bytes_read;
        ;
        new_message.content_size = old_message->content_size;
      }
    }

    if ((ret = delete_file(old_channel, p_from.filename)) != 0)
      goto out;

    struct message *tmp = json_array_push(new_channel->messages, &new_message,
                                          sizeof(struct message), JSON_UNKNOWN);
    print_inf("new_message %p\n", tmp);

    ret = upload_file(new_channel, p_to.filename);

  } else {
    ret = -ENOTSUP;
  }

out:
  return ret;
}

void dcfs_destroy(void *data) {
  struct dcfs_state *state = data;
  print_inf("dcfs_destroy\n");

  if (state->channels)
    discord_free_channels(state->channels);
}

int main(int argc, char *argv[]) {
  program_name = argv[0];

  if (get_auth_token() == NULL) {
    print_err("DCFS_TOKEN isn't set\n");
    return 1;
  }

  char *guild_id = get_guild_id();
  if (!guild_id) {
    print_err("DCFS_GUILD_ID isn't set\n");
    return 1;
  }
  snowflake_init(guild_id, &GUILD_ID);

  struct fuse_operations operations = {
      .readdir = dcfs_readdir,
      .getattr = dcfs_getattr,
      .chown = dcfs_chown,
      .chmod = dcfs_chmod,
      .getxattr = dcfs_getxattr,
      .setxattr = dcfs_setxattr,
      .mkdir = dcfs_mkdir,
      .rmdir = dcfs_rmdir,
      .unlink = dcfs_unlink,
      .create = dcfs_create,
      .open = dcfs_open,
      .read = dcfs_read,
      .write = dcfs_write,
      .release = dcfs_release,
      .rename = dcfs_rename,
      .destroy = dcfs_destroy,

  };

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct stat stbuf;
  struct fuse_cmdline_opts opts;

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

  struct dcfs_state state = {0};
  state.channels = discord_get_channels(GUILD_ID.value);
  if (!state.channels) {
    print_err("failed to get channels of guild %s\n", GUILD_ID.value);
    return 1;
  }

  return fuse_main(argc, argv, &operations, &state);
}
