#include "discord/discord.h"
#include "util.h"

#if __APPLE__
#define _FILE_OFFSET_BITS 64
#endif

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <curl/curl.h>
#include <errno.h>

#define DCFS_UNUSED __attribute__((unused))
#define STREQ(s1, s2) (strcmp((s1), (s2)) == 0)
#define CHECK_NULL(o, code)                                                    \
  if (!(o))                                                                    \
    return -(code);

#ifndef MAX_FILESIZE
#define MAX_FILESIZE 10485760
#endif

static const char *program_name;
static struct discord_snowflake GUILD_ID;

struct path {
  char dir[128];
  char filename[256];
};

struct dcfs_state {
  json_array *channels;
};

static void path_init(const char *path, struct path *p) {
  memset(p, 0, sizeof(struct path));

  char path_normalized[256];
  memset(path_normalized, 0, sizeof(path_normalized));
  string_normalize(path_normalized, path, sizeof(path_normalized));

  if (!STREQ(path_normalized, "/")) {
    int last_slash_idx = last_index(path_normalized, '/');

    if (last_slash_idx > 0) {
      memcpy(p->dir, path_normalized + 1, last_slash_idx - 1);
      memcpy(p->filename, path_normalized + (last_slash_idx + 1),
             strlen(path_normalized) - last_slash_idx);
    } else {
      memcpy(p->dir, path_normalized + 1, strlen(path_normalized) - 1);
    }
  }
}

static inline struct dcfs_state *get_state() {
  return (fuse_get_context())->private_data;
};

static inline struct dcfs_channel *get_channel(json_array *channels,
                                               struct path *path) {
  struct dcfs_channel *_ch;
  json_array_for_each(channels, _ch) {
    if (STREQ(_ch->name, path->dir)) {
      return _ch;
    }
  }
  return NULL;
}

static inline struct dcfs_message *get_message(json_array *messages,
                                               struct path *path) {
  struct dcfs_message *_m;
  json_array_for_each(messages, _m) {
    if (STREQ(_m->filename, path->filename)) {
      return _m;
    }
  }
  return NULL;
}

static void print_err(const char *format, ...) {
  va_list list;
  va_start(list, format);

  fprintf(stderr, "%s: \033[31;1mERR\033[0m ", program_name);
  vfprintf(stderr, format, list);

  va_end(list);
}

DCFS_UNUSED static void print_inf(const char *format, ...) {
  va_list list;
  va_start(list, format);

  printf("%s \033[34;1mINFO\033[0m ", program_name);
  vprintf(format, list);

  va_end(list);
}

static void print_warn(const char *format, ...) {
  va_list list;
  va_start(list, format);

  printf("%s \033[33;1mWARN\033[0m ", program_name);
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

static int process_files(struct dcfs_channel *channel,
                         struct dcfs_message *head, struct file *files,
                         size_t files_n) {
  struct response resp = {0};
  discord_create_attachments(channel->id.value, files, files_n, &resp);

  if (resp.http_code != 200) {
    print_err("failed to upload file %s. error code: %d\n", head->filename,
              resp.http_code);
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
    char decoded_filename[256];
    memset(decoded_filename, 0, sizeof(decoded_filename));

    json_string filename = json_object_get(attachment, "filename");
    json_number *size = json_object_get(attachment, "size");
    json_string url = json_object_get(attachment, "url");

    assert(b64decode(decoded_filename, filename, sizeof(decoded_filename)) ==
           0);

    if (STREQ(decoded_filename, head->filename)) {
      discord_snowflake_init(message_id, &head->id);

      // head->size = *size;
      head->url = strdup(url);

    } else {
      struct dcfs_message part_message;
      memset(&part_message, 0, sizeof(struct dcfs_message));

      discord_snowflake_init(message_id, &part_message.id);

      strcpy(part_message.filename, decoded_filename);
      part_message.size = *size;
      part_message.url = strdup(url);
      part_message.is_part = 1;
      // head->size += *size;

      int part_n_start = last_index(decoded_filename, 'T');
      size_t part_n = strtol(decoded_filename + part_n_start + 1, NULL, 10);

      head->parts[part_n - 1] =
          json_array_push(channel->messages, &part_message,
                          sizeof(struct dcfs_message), JSON_UNKNOWN);
      head->parts_n++;
    }
  }

  json_object_destroy(json);
  return 0;
}

static int upload_file(struct dcfs_channel *channel, struct path *p) {
  struct dcfs_message *message = get_message(channel->messages, p);
  CHECK_NULL(message, ENOENT);

  int ret = -ENODATA;
  if (!message->url && message->content) {
    ret = 0;
    int files_n = 0;
    struct file files[10];
    memset(&files, 0, sizeof(files));

    if (message->size / MAX_FILESIZE >= DISCORD_MAX_PARTS) {
      ret = -EFBIG;
      goto out;
    }

    for (size_t offset = 0; offset < message->size;
         offset += MAX_FILESIZE, files_n++) {

      if (files_n > 0 && files_n % 10 == 0) {
        if ((ret = process_files(channel, message, files, 10)) != 0)
          goto out;
        memset(&files, 0, sizeof(files));
      }

      struct file *file = &files[files_n % 10];

      memset(file->filename, 0, sizeof(file->filename));
      if (offset == 0) {
        b64encode(file->filename, p->filename, sizeof(file->filename));
      } else {
        char tmp_filename[512];
        snprintf(tmp_filename, sizeof(tmp_filename), "%s.PART%d", p->filename,
                 files_n);
        b64encode(file->filename, tmp_filename, sizeof(file->filename));
      }

      file->buffer = message->content + offset;
      size_t remaining = message->size - offset;
      file->buffer_size = remaining < MAX_FILESIZE ? remaining : MAX_FILESIZE;
    }

    if (files_n > 0) {
      ret = process_files(channel, message, files, files_n % 10);
    }
  }
out:
  free(message->content);
  message->content = NULL;
  return ret;
}

static int delete_file(struct dcfs_channel *channel, struct path *p) {
  struct dcfs_message *message = get_message(channel->messages, p);
  CHECK_NULL(message, ENOENT);

  struct response resp = {0};
  discord_delete_messsage(channel->id.value, message->id.value, &resp);

  int last_deleted_message_id = string_hash(message->id.value);
  for (size_t i = 0; i < message->parts_n; i++) {
    struct dcfs_message *part = message->parts[i];
    if (string_hash(part->id.value) != last_deleted_message_id) {
      discord_delete_messsage(channel->id.value, part->id.value, &resp);
      last_deleted_message_id = string_hash(part->id.value);
    }
    discord_free_message(part);
    json_array_remove_ptr(&channel->messages, part);
  }

  discord_free_message(message);
  json_array_remove_ptr(&channel->messages, message);

  return 0;
};

int dcfs_rmdir(const char *path) {
  if (count_char(path, '/') != 1) {
    return -EPERM;
  }
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_rmdir", &p);

  struct dcfs_channel *channel;
  json_array *_ch = state->channels;
  json_array_for_each(_ch, channel) {
    if (STREQ(path + 1, channel->name)) {
      struct response resp = {0};
      discord_delete_channel(channel->id.value, &resp);

      if (resp.http_code != 200) {
        print_err("failed to delete %s channel. http code: %ld\n",
                  channel->name, resp.http_code);
        return -EAGAIN;
      }

      discord_free_messages(channel->messages);
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
    print_err("failed to create a new channel. http_code: %ld\n",
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

  struct dcfs_channel new_channel;

  discord_snowflake_init(id, &new_channel.id);
  strcpy(new_channel.name, name);
  new_channel.type = *type;
  new_channel.has_parent = *parent == JSON_NULL ? 0 : 1;
  new_channel.mode = mode;

  json_array_push(state->channels, &new_channel, sizeof(struct dcfs_channel),
                  JSON_UNKNOWN);
  json_object_destroy(json);

  return 0;
};

#ifdef __APPLE__
int dcfs_getxattr(const char *path, const char *name, char *value, size_t size,
                  uint32_t flags) {
  (void)path;
  (void)name;
  (void)value;
  (void)size;
  (void)flags;
  return 0;
}

int dcfs_setxattr(const char *path, const char *name, const char *value,
                  size_t size, int flags, uint32_t position) {
  (void)path;
  (void)name;
  (void)value;
  (void)size;
  (void)flags;
  (void)position;
  return 0;
}

int dcfs_setattr(const char *path, struct fuse_darwin_attr *attr, int to_set,
                 struct fuse_file_info *_) {

  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_chown", &p);

  struct dcfs_channel *channel;
  struct dcfs_message *message;

  if (*p.dir && !*p.filename) {
    channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);

    channel->gid = attr->gid & to_set ? attr->gid : channel->gid;
    channel->uid = attr->uid & to_set ? attr->uid : channel->uid;

    if (attr->mode & to_set && S_ISDIR(attr->mode))
      channel->mode = attr->mode;

  } else if (*p.dir && *p.filename) {
    channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);
    message = get_message(channel->messages, &p);
    CHECK_NULL(message, ENOENT);

    message->gid = attr->gid & to_set ? attr->gid : channel->gid;
    message->uid = attr->uid & to_set ? attr->uid : channel->uid;
    if (attr->mode & to_set && S_ISREG(attr->mode))
      message->mode = attr->mode;
  }

  return 0;
}

#endif /* __APPLE__ */

#ifdef __APPLE__
int dcfs_getattr(const char *path, struct fuse_darwin_attr *stbuf,
                 struct fuse_file_info *_)
#else
int dcfs_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *_)
#endif
{

  struct dcfs_state *state = get_state();

  struct path p = {0};
  path_init(path, &p);
  print_op("dcfs_getattr", &p);

  memset(stbuf, 0, sizeof(struct stat));

  if (STREQ(path, "/")) {
#ifdef __APPLE__
    stbuf->mode = S_IFDIR | 0755;
    stbuf->gid = getgid();
    stbuf->uid = getuid();
    stbuf->ctimespec.tv_sec = GUILD_ID.timestamp;
    stbuf->mtimespec.tv_sec = GUILD_ID.timestamp;
#else
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_gid = getgid();
    stbuf->st_uid = getuid();
    stbuf->st_ctim.tv_sec = GUILD_ID.timestamp;
    stbuf->st_mtim.tv_sec = GUILD_ID.timestamp;
#endif /* __APPLE__ */

  } else if (count_char(path, '/') == 1) {
    struct dcfs_channel *channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);

    if (!channel->messages)
      channel->messages = discord_get_messages(channel->id.value);

#ifdef __APPLE__
    stbuf->mode = channel->mode;
    stbuf->gid = channel->gid;
    stbuf->uid = channel->uid;
    stbuf->ctimespec.tv_sec = channel->id.timestamp;
    stbuf->mtimespec.tv_sec = channel->id.timestamp;
#else
    stbuf->st_mode = channel->mode;
    stbuf->st_gid = channel->gid;
    stbuf->st_uid = channel->uid;
    stbuf->st_ctim.tv_sec = channel->id.timestamp;
    stbuf->st_mtim.tv_sec = channel->id.timestamp;
#endif /* __APPLE__ */

  } else if (count_char(path, '/') == 2) {
    struct dcfs_channel *channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);
    struct dcfs_message *message = get_message(channel->messages, &p);
    CHECK_NULL(message, ENOENT);

#ifdef __APPLE__
    stbuf->mode = message->mode;
    stbuf->gid = message->gid;
    stbuf->uid = message->uid;
    stbuf->size = message->size;
    stbuf->ctimespec.tv_sec = message->id.timestamp;
    stbuf->mtimespec.tv_sec = message->id.timestamp;
#else
    stbuf->st_mode = message->mode;
    stbuf->st_gid = message->gid;
    stbuf->st_uid = message->uid;
    stbuf->st_size = message->size;
    stbuf->st_ctim.tv_sec = message->id.timestamp;
    stbuf->st_mtim.tv_sec = message->id.timestamp;
#endif /* __APPLE__ */
  }

  return 0;
}

#ifdef __APPLE__
int dcfs_readdir(const char *path, void *buf, fuse_darwin_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *_,
                 enum fuse_readdir_flags flags)

#else
int dcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *_,
                 enum fuse_readdir_flags flags)
#endif /* __APPLE__ */
{

  (void)offset;
  (void)flags;
  struct dcfs_state *state = get_state();

  struct path p = {0};
  path_init(path, &p);
  print_op("dcfs_readdir", &p);

  filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);

  if (STREQ(path, "/")) {
    struct dcfs_channel *channel;
    json_array *_c = state->channels;
    json_array_for_each(_c, channel) {
      if (channel->type == GUILD_TEXT && !channel->has_parent) {
        filler(buf, channel->name, NULL, 0, FUSE_FILL_DIR_DEFAULTS);
      }
    }
  } else {
    struct dcfs_channel *channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);

    if (!channel->messages) {
      channel->messages = discord_get_messages(channel->id.value);
      if (!channel->messages) {
        print_err("failed to get %s channel messages\n", channel->id.value);
        return -EAGAIN;
      }
    }

    struct dcfs_message *message;
    json_array *_m = channel->messages;
    json_array_for_each(_m, message) {
      if (!message->is_part) {
        filler(buf, message->filename, NULL, 0, FUSE_FILL_DIR_DEFAULTS);
      }
    }
  }

  return 0;
}

int dcfs_create(const char *path, mode_t mode, struct fuse_file_info *_) {
  if (count_char(path, '/') != 2) {
    return -EPERM;
  }
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_create", &p);

  struct dcfs_channel *channel = get_channel(state->channels, &p);
  CHECK_NULL(channel, ENOENT);

  struct dcfs_message message;
  memset(&message, 0, sizeof(struct dcfs_message));
  strcpy(message.filename, p.filename);
  message.mode = mode;

  json_array_push(channel->messages, &message, sizeof(struct dcfs_message),
                  JSON_UNKNOWN);
  return 0;
}

int dcfs_chown(const char *path, uid_t uid, gid_t gid,
               struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_chown", &p);

  struct dcfs_channel *channel;
  struct dcfs_message *message;

  if (*p.dir && !*p.filename) {
    channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);

    channel->gid = gid;
    channel->uid = uid;

  } else if (*p.dir && *p.filename) {
    channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);
    message = get_message(channel->messages, &p);
    CHECK_NULL(message, ENOENT);

    message->gid = gid;
    message->uid = uid;
  }

  return 0;
};

int dcfs_chmod(const char *path, mode_t mode, struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_chown", &p);

  struct dcfs_channel *channel;
  struct dcfs_message *message;

  if (*p.dir && !*p.filename) {
    channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);

    if (S_ISREG(mode))
      return -ENOTSUP;

    channel->mode = mode;

  } else if (*p.dir && *p.filename) {
    channel = get_channel(state->channels, &p);
    CHECK_NULL(channel, ENOENT);
    message = get_message(channel->messages, &p);
    CHECK_NULL(message, ENOENT);

    if (S_ISDIR(mode))
      return -ENOTSUP;

    message->mode = mode;
  }

  return 0;
};

int dcfs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *_) {

  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_write", &p);

  struct dcfs_channel *channel = get_channel(state->channels, &p);
  CHECK_NULL(channel, ENOENT);
  struct dcfs_message *message = get_message(channel->messages, &p);
  CHECK_NULL(message, ENOENT);

  message->size += size;

  if (!message->content) {
    message->content = malloc(size);

    if (!message->content) {
      print_err("dcfs_write: failed to malloc\n");
      return -1;
    }
  } else {
    message->content = realloc(message->content, message->size);
  }

  if (offset < message->size) {
    if (offset + size > message->size) {
      size = message->size - offset;
    }

    memcpy(message->content + offset, buf, size);

  } else {
    size = 0;
  }

  return size;
}

int dcfs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_read", &p);

  struct dcfs_channel *channel = get_channel(state->channels, &p);
  CHECK_NULL(channel, ENOENT);
  struct dcfs_message *message = get_message(channel->messages, &p);
  CHECK_NULL(message, ENOENT);

  if (!message->content) {
    struct response resp = {0};
    request_get(message->url, &resp, 0);
    message->content = resp.raw;
    message->size = resp.size;

    for (size_t i = 0; i < message->parts_n; i++) {
      struct dcfs_message *part = message->parts[i];
      resp = (struct response){0};

      request_get(part->url, &resp, 0);

      message->content = realloc(message->content, message->size + resp.size);
      memcpy(message->content + message->size, resp.raw, resp.size);
      message->size += resp.size;
      free(resp.raw);
    }
  }

  if (offset < message->size) {
    if (offset + size > message->size) {
      size = message->size - offset;
    }

    memcpy(buf, message->content + offset, size);

  } else {
    size = 0;
  }

  return size;
}

int dcfs_release(const char *path, struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_release", &p);

  struct dcfs_channel *channel = get_channel(state->channels, &p);
  CHECK_NULL(channel, ENOENT);

  return upload_file(channel, &p);
}

int dcfs_unlink(const char *path) {
  struct dcfs_state *state = get_state();

  struct path p;
  path_init(path, &p);
  print_op("dcfs_unlink", &p);

  struct dcfs_channel *channel = get_channel(state->channels, &p);
  CHECK_NULL(channel, ENOENT);

  return delete_file(channel, &p);
}

int dcfs_rename(const char *from, const char *to, unsigned int flags) {
  if (flags)
    return -EINVAL;

  int ret = 0;
  struct dcfs_state *state = get_state();

  struct path p_from;
  path_init(from, &p_from);
  struct dcfs_channel *old_channel = get_channel(state->channels, &p_from);
  CHECK_NULL(old_channel, ENOENT);

  struct path p_to;
  path_init(to, &p_to);
  struct dcfs_channel *new_channel = get_channel(state->channels, &p_to);
  CHECK_NULL(new_channel, ENOENT);

  print_op("dcfs_rename", &p_from);
  print_op("dcfs_rename", &p_to);

  if (*p_from.dir && !*p_from.filename && *p_to.dir && !*p_to.filename) {
    struct response resp = {0};
    discord_rename_channel(old_channel->id.value, p_to.dir, &resp);
    if (resp.http_code != 200)
      return -EAGAIN;

    memset(old_channel->name, 0, sizeof(old_channel->name));
    strcpy(old_channel->name, p_to.dir);

  } else if (*p_from.dir && *p_from.filename && *p_to.dir && *p_to.filename) {
    if (STREQ(p_from.dir, p_to.dir))
      return -ENOSYS;

    if (!old_channel || !new_channel)
      return -ENOENT;

    struct dcfs_message new_message;
    memset(&new_message, 0, sizeof(struct dcfs_message));
    strcpy(new_message.filename, p_to.filename);

    struct dcfs_message *old_message =
        get_message(old_channel->messages, &p_from);
    CHECK_NULL(old_message, ENOENT);

    new_message.content = malloc(old_message->size);
    if (!new_message.content)
      return -1;

    int offset = 0;
    int bytes_read = 0;
    while ((bytes_read = dcfs_read(from, new_message.content + offset, 4096,
                                   offset, NULL)) > 0)
      offset += bytes_read;
    ;
    new_message.size = old_message->size;

    if ((ret = delete_file(old_channel, &p_from)) != 0)
      return -EAGAIN;

    json_array_push(new_channel->messages, &new_message,
                    sizeof(struct dcfs_message), JSON_UNKNOWN);

    ret = upload_file(new_channel, &p_to);

  } else {
    ret = -ENOTSUP;
  }

  return ret;
}

int main(int argc, char *argv[]) {
  int res;
  struct fuse *fuse;
  struct fuse_session *se;
  struct stat stbuf;
  struct fuse_cmdline_opts opts;

  program_name = argv[0];

  if (!get_auth_token()) {
    print_err("DCFS_TOKEN isn't set\n");
    return 1;
  }

  char *guild_id = get_guild_id();
  if (!guild_id) {
    print_err("DCFS_GUILD_ID isn't set\n");
    return 1;
  }
  discord_snowflake_init(guild_id, &GUILD_ID);

  struct fuse_operations operations = {
      .readdir = dcfs_readdir,
      .mkdir = dcfs_mkdir,
      .rmdir = dcfs_rmdir,
      .getattr = dcfs_getattr,
      .create = dcfs_create,
      .unlink = dcfs_unlink,
      .chown = dcfs_chown,
      .chmod = dcfs_chmod,
      .read = dcfs_read,
      .write = dcfs_write,
      .release = dcfs_release,
      .rename = dcfs_rename,
#ifdef __APPLE__
      .getxattr = dcfs_getxattr,
      .setxattr = dcfs_setxattr,
      .setattr = dcfs_setattr,
#endif /* __APPLE__ */

  };

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  if (fuse_parse_cmdline(&args, &opts) != 0)
    return 1;

  if (opts.show_help) {
    fuse_lib_help(&args);
    return 0;
  }

  if (!opts.mountpoint) {
    print_err("missing mountpoint parameter\n");
    return 1;
  }

  if (stat(opts.mountpoint, &stbuf) == -1) {
    print_err("failed to access mountpoint %s: %s\n", opts.mountpoint,
              strerror(errno));
    free(opts.mountpoint);
    return 1;
  }

  if ((res = fuse_daemonize(opts.foreground)) == -1) {
    print_err("failed to fuse_daemonize\n");
    goto out4;
  }

  struct dcfs_state state = {0};
  fuse = fuse_new(&args, &operations, sizeof(struct fuse_operations), &state);
  se = fuse_get_session(fuse);

  if ((res = fuse_set_signal_handlers(se)) != 0)
    goto out3;

  char *real_mountpoint = realpath(opts.mountpoint, NULL);
  if ((res = fuse_mount(fuse, real_mountpoint)) != 0) {
    print_err("failed to fuse_mount\n");
    goto out2;
  }

  if ((res = fcntl(fuse_session_fd(se), F_SETFD, FD_CLOEXEC)) == -1) {
    perror("fcntl");
    print_warn("failed to set FD_CLOEXEC on fuse device\n");
  };

  if ((res = curl_global_init(CURL_GLOBAL_ALL)) != 0) {
    print_err("failed to curl_global_init\n");
    goto out1;
  }

  state.channels = discord_get_channels(GUILD_ID.value);
  if (!state.channels) {
    print_err("failed to get channels\n");
    goto out1;
  }

  if (opts.singlethread)
    res = fuse_loop(fuse);
  else
    res = fuse_loop_mt(fuse, 0);

out1:
  curl_global_cleanup();
  fuse_unmount(fuse);
  fuse_remove_signal_handlers(se);

out2:
  free(real_mountpoint);

out3:
  discord_free_channels(state.channels);
  fuse_destroy(fuse);

out4:
  fuse_opt_free_args(&args);
  free(opts.mountpoint);

  return res;
}
