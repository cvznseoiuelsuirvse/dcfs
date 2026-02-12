#include "discord/discord.h"
#include "fs.h"
#include "util.h"

#if __APPLE__
#define _FILE_OFFSET_BITS 64
#endif

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 18)
#include <fuse3/fuse.h>
#include <fuse3/fuse_lowlevel.h>

#include <curl/curl.h>
#include <errno.h>

#define DCFS_UNUSED __attribute__((unused))
#define CHECK_NULL(o, code)                                                    \
  if (!(o))                                                                    \
    return -(code);

#ifndef MAX_FILESIZE
#define MAX_FILESIZE 10485760
#endif

static const char *program_name;
static const char *GUILD_ID;

struct dcfs_state {
  json_array *dirs;
};

static inline struct dcfs_state *get_state() {
  return (fuse_get_context())->private_data;
};

static inline struct dcfs_dir *get_dir(json_array *dirs,
                                       struct dcfs_path *path) {
  struct dcfs_dir *dir;
  json_array_for_each(dirs, dir) {
    if (STREQ(dir->channel.name, path->dir)) {
      return dir;
    }
  }
  return NULL;
}

static inline struct dcfs_file *get_file(json_array *messages,
                                         struct dcfs_path *path) {
  struct dcfs_file *_f;
  json_array_for_each(messages, _f) {
    if (STREQ(_f->filename, path->filename)) {
      return _f;
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

static void print_op(const char *op, struct dcfs_path *path) {
  if (!*path->dir && !*path->filename) {
    printf("\033[35;1mOPERATION \033[37m%s\033[0m: /\n", op);
  } else if (!*path->filename) {
    printf("\033[35;1mOPERATION \033[37m%s\033[0m: /%s\n", op, path->dir);
  } else {
    printf("\033[35;1mOPERATION \033[37m%s\033[0m: /%s/%s\n", op, path->dir,
           path->filename);
  }
}

static int process_files(struct dcfs_dir *dir, struct dcfs_file *dcfs_file,
                         struct file *files, size_t files_n) {
  struct response resp = {0};
  discord_create_attachments(dir->channel.id, files, files_n, &resp);

  if (resp.http_code != 200) {
    print_err("failed to upload file %s. error code: %d\n", dcfs_file->filename,
              resp.http_code);
    json_array_remove_ptr(&dir->files, dcfs_file);
    free(resp.raw);

    return -EAGAIN;
  }

  json_object *json;
  json_load(resp.raw, (void **)&json);
  free(resp.raw);

  json_string message_id = json_object_get(json, "id");
  id_to_ctime(&dcfs_file->ctime, message_id);

  struct dcfs_message *message;
  int part_n = 0;

  json_object *attachment;
  json_array *attachments = json_object_get(json, "attachments");
  json_array_for_each(attachments, attachment) {
    char decoded_filename[256];
    memset(decoded_filename, 0, sizeof(decoded_filename));

    json_string filename = json_object_get(attachment, "filename");
    json_number *size = json_object_get(attachment, "size");
    json_string url = json_object_get(attachment, "url");

    b64decode(decoded_filename, filename, sizeof(decoded_filename));

    message = calloc(sizeof(struct dcfs_message), 1);
    assert(message);

    snprintf(message->id, sizeof(message->id), "%s", message_id);
    snprintf(message->filename, sizeof(message->filename), "%s", filename);
    message->size = *size;
    message->url = strdup(url);

    if (!STREQ(decoded_filename, dcfs_file->filename)) {
      int part_n_start = last_index(decoded_filename, 'T');
      part_n = strtol(decoded_filename + part_n_start + 1, NULL, 10);
    }
    dcfs_file->messages[part_n] = message;
    dcfs_file->messages_n++;
  }

  json_object_destroy(json);
  return 0;
}

static int upload_file(struct dcfs_dir *dir, struct dcfs_path *p) {
  struct dcfs_file *dcfs_file = get_file(dir->files, p);
  CHECK_NULL(dcfs_file, ENOENT);

  int ret = -ENODATA;
  if (!*dcfs_file->messages) {
    ret = 0;
    int files_n = 0;
    struct file files[10];
    memset(&files, 0, sizeof(files));

    if (dcfs_file->size / MAX_FILESIZE >= DISCORD_MAX_PARTS) {
      ret = -EFBIG;
      goto out;
    }

    for (size_t offset = 0; offset < dcfs_file->size;
         offset += MAX_FILESIZE, files_n++) {

      if (files_n > 0 && files_n % 10 == 0) {
        if ((ret = process_files(dir, dcfs_file, files, 10)) != 0)
          goto out;
        memset(&files, 0, sizeof(files));
      }

      struct file *file = &files[files_n % 10];
      if (offset == 0) {
        b64encode(file->filename, p->filename, sizeof(file->filename));
      } else {
        char tmp_filename[512];
        snprintf(tmp_filename, sizeof(tmp_filename), "%s.PART%d", p->filename,
                 files_n);
        b64encode(file->filename, tmp_filename, sizeof(file->filename));
      }

      file->buffer = dcfs_file->content + offset;
      size_t remaining = dcfs_file->size - offset;
      file->buffer_size = remaining < MAX_FILESIZE ? remaining : MAX_FILESIZE;
    }

    if (files_n > 0) {
      ret = process_files(dir, dcfs_file, files, files_n % 10);
    }
  }
out:
  free(dcfs_file->content);
  dcfs_file->content = NULL;
  return ret;
}

static int delete_file(struct dcfs_dir *dir, struct dcfs_path *p) {
  struct dcfs_file *file = get_file(dir->files, p);
  CHECK_NULL(file, ENOENT);

  struct response resp = {0};
  dcfs_hash last_deleted_message_id = 0;

  for (size_t i = 0; i < file->messages_n; i++) {
    struct dcfs_message *message = file->messages[i];
    if (string_hash(message->id) != last_deleted_message_id) {
      discord_delete_messsage(dir->channel.id, message->id, &resp);
      last_deleted_message_id = string_hash(message->id);
    }
  }

  dcfs_free_file(file);
  json_array_remove_ptr(&dir->files, file);

  return 0;
};

int dcfs_rmdir(const char *path) {
  if (count_char(path, '/') != 1) {
    return -EPERM;
  }
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_rmdir", &p);

  struct dcfs_dir *dir;
  json_array *_d = state->dirs;
  json_array_for_each(_d, dir) {
    if (STREQ(path + 1, dir->channel.name)) {
      struct response resp = {0};
      discord_delete_channel(dir->channel.id, &resp);

      if (resp.http_code != 200) {
        print_err("failed to delete %s channel. http code: %ld\n",
                  dir->channel.name, resp.http_code);
        return -EAGAIN;
      }

      discord_free_messages(dir->files);
      json_array_remove_ptr(&state->dirs, dir);

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

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_mkdir", &p);

  struct response resp = {0};
  discord_create_channel(GUILD_ID, path + 1, &resp);

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

  struct dcfs_dir new_dir;
  memset(&new_dir, 0, sizeof(struct dcfs_dir));

  id_to_ctime(&new_dir.ctime, id);
  new_dir.mode = mode;
  new_dir.gid = getgid();
  new_dir.uid = getuid();

  snprintf(new_dir.channel.id, sizeof(new_dir.channel.id), "%s", id);
  snprintf(new_dir.channel.name, sizeof(new_dir.channel.name), "%s", name);
  new_dir.channel.type = *type;
  new_dir.channel.has_parent = 0;

  json_array_push(state->dirs, &new_dir, sizeof(struct dcfs_dir), JSON_UNKNOWN);
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

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_chown", &p);

  struct dcfs_dir *dir;
  struct dcfs_file *file;

  if (*p.dir && !*p.filename) {
    dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);

    dir->gid = attr->gid & to_set ? attr->gid : dir->gid;
    dir->uid = attr->uid & to_set ? attr->uid : dir->uid;

    if (attr->mode & to_set && S_ISDIR(attr->mode))
      dir->mode = attr->mode;

  } else if (*p.dir && *p.filename) {
    dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);
    file = get_file(dir->files, &p);
    CHECK_NULL(file, ENOENT);

    file->gid = attr->gid & to_set ? attr->gid : dir->gid;
    file->uid = attr->uid & to_set ? attr->uid : dir->uid;
    if (attr->mode & to_set && S_ISREG(attr->mode))
      file->mode = attr->mode;
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

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_getattr", &p);

  memset(stbuf, 0, sizeof(struct stat));

  if (STREQ(path, "/")) {
#ifdef __APPLE__
    stbuf->mode = S_IFDIR | 0755;
    stbuf->gid = getgid();
    stbuf->uid = getuid();
    id_to_ctime(&stbuf->ctimespec.tv_sec, GUILD_ID);
    id_to_ctime(&stbuf->mtimespec.tv_sec, GUILD_ID);
#else
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_gid = getgid();
    stbuf->st_uid = getuid();
    id_to_ctime(&stbuf->st_ctim.tv_sec, GUILD_ID);
    id_to_ctime(&stbuf->st_mtim.tv_sec, GUILD_ID);
#endif /* __APPLE__ */

  } else if (count_char(path, '/') == 1) {
    struct dcfs_dir *dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);

    if (!dir->files)
      dir->files = dcfs_get_files(dir->channel.id);

#ifdef __APPLE__
    stbuf->mode = dir->mode;
    stbuf->gid = dir->gid;
    stbuf->uid = dir->uid;
    stbuf->ctimespec.tv_sec = dir->ctime;
    stbuf->mtimespec.tv_sec = dir->ctime;
#else
    stbuf->st_mode = dir->mode;
    stbuf->st_gid = dir->gid;
    stbuf->st_uid = dir->uid;
    stbuf->st_ctim.tv_sec = dir->ctime;
    stbuf->st_mtim.tv_sec = dir->ctime;
#endif /* __APPLE__ */

  } else if (count_char(path, '/') == 2) {
    struct dcfs_dir *dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);
    struct dcfs_file *file = get_file(dir->files, &p);
    CHECK_NULL(file, ENOENT);

#ifdef __APPLE__
    stbuf->mode = file->mode;
    stbuf->gid = file->gid;
    stbuf->uid = file->uid;
    stbuf->size = file->size;
    stbuf->ctimespec.tv_sec = file->ctime;
    stbuf->mtimespec.tv_sec = file->ctime;
#else
    stbuf->st_mode = file->mode;
    stbuf->st_gid = file->gid;
    stbuf->st_uid = file->uid;
    stbuf->st_size = file->size;
    stbuf->st_ctim.tv_sec = file->ctime;
    stbuf->st_mtim.tv_sec = file->ctime;
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

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_readdir", &p);

  filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);

  if (STREQ(path, "/")) {
    struct dcfs_dir *dir;
    json_array *_d = state->dirs;
    json_array_for_each(_d, dir) {
      if (dir->channel.type == GUILD_TEXT && !dir->channel.has_parent) {
        filler(buf, dir->channel.name, NULL, 0, FUSE_FILL_DIR_DEFAULTS);
      }
    }
  } else {
    struct dcfs_dir *dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);

    struct dcfs_file *file;
    json_array *_f = dir->files;
    json_array_for_each(_f, file) {
      filler(buf, file->filename, NULL, 0, FUSE_FILL_DIR_DEFAULTS);
    }
  }

  return 0;
}

int dcfs_create(const char *path, mode_t mode, struct fuse_file_info *_) {
  if (count_char(path, '/') != 2) {
    return -EPERM;
  }
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_create", &p);

  struct dcfs_dir *dir = get_dir(state->dirs, &p);
  CHECK_NULL(dir, ENOENT);

  struct dcfs_file file;
  memset(&file, 0, sizeof(file));
  snprintf(file.filename, sizeof(file.filename), "%s", p.filename);
  file.mode = mode;

  json_array_push(dir->files, &file, sizeof(struct dcfs_file), JSON_UNKNOWN);
  return 0;
}

int dcfs_chown(const char *path, uid_t uid, gid_t gid,
               struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_chown", &p);

  struct dcfs_dir *dir;
  struct dcfs_file *file;

  if (*p.dir && !*p.filename) {
    dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);

    dir->gid = gid;
    dir->uid = uid;

  } else if (*p.dir && *p.filename) {
    dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);
    file = get_file(dir->files, &p);
    CHECK_NULL(file, ENOENT);

    file->gid = gid;
    file->uid = uid;
  }

  return 0;
};

int dcfs_chmod(const char *path, mode_t mode, struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_chown", &p);

  struct dcfs_dir *dir;
  struct dcfs_file *file;

  if (*p.dir && !*p.filename) {
    dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);

    if (S_ISREG(mode))
      return -ENOTSUP;

    dir->mode = mode;

  } else if (*p.dir && *p.filename) {
    dir = get_dir(state->dirs, &p);
    CHECK_NULL(dir, ENOENT);
    file = get_file(dir->files, &p);
    CHECK_NULL(file, ENOENT);

    if (S_ISDIR(mode))
      return -ENOTSUP;

    file->mode = mode;
  }

  return 0;
};

int dcfs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *_) {

  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_write", &p);

  struct dcfs_dir *dir = get_dir(state->dirs, &p);
  CHECK_NULL(dir, ENOENT);
  struct dcfs_file *file = get_file(dir->files, &p);
  CHECK_NULL(file, ENOENT);

  file->size += size;

  if (!file->content) {
    file->content = malloc(size);

    if (!file->content) {
      print_err("dcfs_write: failed to malloc\n");
      return -ENOBUFS;
    }
  } else {
    file->content = realloc(file->content, file->size);
  }

  if (offset < file->size) {
    if (offset + size > file->size) {
      size = file->size - offset;
    }

    memcpy(file->content + offset, buf, size);

  } else {
    size = 0;
  }

  return size;
}

int dcfs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_read", &p);

  struct dcfs_dir *dir = get_dir(state->dirs, &p);
  CHECK_NULL(dir, ENOENT);
  struct dcfs_file *file = get_file(dir->files, &p);
  CHECK_NULL(file, ENOENT);

  // size_t part_size = file->size;
  // int part_n = offset / part_size;

  if (!file->content) {
    struct response resp = {0};
    off_t content_offset = 0;

    for (size_t i = 0; i < file->messages_n; i++) {
      struct dcfs_message *part = file->messages[i];

      resp = (struct response){0};
      request_get(part->url, &resp, 0);

      if (i == 0)
        file->content = malloc(resp.size);
      else {
        file->content = realloc(file->content, file->size + resp.size);
      }

      print_inf("%s %d\n", file->filename, file->size);
      memcpy(file->content + content_offset, resp.raw, resp.size);
      part->content = resp.raw;
      content_offset += resp.size;
    }
  }

  if (offset < file->size) {
    if (offset + size > file->size) {
      size = file->size - offset;
    }

    memcpy(buf, file->content + offset, size);

  } else {
    size = 0;
  }

  return size;
}

int dcfs_release(const char *path, struct fuse_file_info *_) {
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_release", &p);

  struct dcfs_dir *dir = get_dir(state->dirs, &p);
  CHECK_NULL(dir, ENOENT);

  return upload_file(dir, &p);
}

int dcfs_unlink(const char *path) {
  struct dcfs_state *state = get_state();

  struct dcfs_path p;
  dcfs_path_init(path, &p);
  print_op("dcfs_unlink", &p);

  struct dcfs_dir *dir = get_dir(state->dirs, &p);
  CHECK_NULL(dir, ENOENT);

  return delete_file(dir, &p);
}

int dcfs_rename(const char *from, const char *to, unsigned int flags) {
  if (flags)
    return -EINVAL;

  int ret = 0;
  struct dcfs_state *state = get_state();

  struct dcfs_path p_from;
  dcfs_path_init(from, &p_from);
  struct dcfs_dir *old_dir = get_dir(state->dirs, &p_from);
  CHECK_NULL(old_dir, ENOENT);

  struct dcfs_path p_to;
  dcfs_path_init(to, &p_to);
  struct dcfs_dir *new_dir = get_dir(state->dirs, &p_to);

  print_op("dcfs_rename", &p_from);
  print_op("dcfs_rename", &p_to);

  if (*p_from.dir && !*p_from.filename && *p_to.dir && !*p_to.filename) {
    struct response resp = {0};
    discord_rename_channel(old_dir->channel.id, p_to.dir, &resp);
    if (resp.http_code != 200)
      return -EAGAIN;

    snprintf(old_dir->channel.name, sizeof(old_dir->channel.name), "%s",
             p_to.dir);

  } else if (*p_from.dir && *p_from.filename && *p_to.dir && *p_to.filename) {
    if (STREQ(p_from.dir, p_to.dir))
      return -ENOSYS;

    if (!old_dir || !new_dir)
      return -ENOENT;

    struct dcfs_file new_file;
    memset(&new_file, 0, sizeof(new_file));

    snprintf(new_file.filename, sizeof(new_file.filename), "%s", p_to.filename);

    struct dcfs_file *old_file = get_file(old_dir->files, &p_from);
    CHECK_NULL(old_file, ENOENT);

    for (size_t i = 0; i < old_file->messages_n; i++) {
    }
    new_file.content = malloc(old_file->size);
    if (!new_file.content) {
      print_err("dcfs_rename: failed to malloc\n");
      return -ENOBUFS;
    }

    int offset = 0;
    int bytes_read = 0;
    while ((bytes_read = dcfs_read(from, new_file.content + offset, 4096,
                                   offset, NULL)) > 0)
      offset += bytes_read;
    ;
    new_file.size = old_file->size;
    new_file.mode = old_file->mode;
    new_file.gid = old_file->gid;
    new_file.uid = old_file->uid;

    if ((ret = delete_file(old_dir, &p_from)) != 0)
      return -EAGAIN;

    json_array_remove_ptr(&old_dir->files, old_file);
    json_array_push(new_dir->files, &new_file, sizeof(struct dcfs_file),
                    JSON_UNKNOWN);

    ret = upload_file(new_dir, &p_to);

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

  GUILD_ID = get_guild_id();
  if (!GUILD_ID) {
    print_err("DCFS_GUILD_ID isn't set\n");
    return 1;
  }

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

  state.dirs = dcfs_get_dirs(GUILD_ID);
  if (!state.dirs) {
    print_err("failed to get dirs\n");
    goto out1;
  }

  if (opts.singlethread)
    res = fuse_session_loop(se);
  else {
    struct fuse_loop_config *config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
    fuse_loop_cfg_set_max_threads(config, opts.max_threads);
    res = fuse_session_loop_mt(se, config);
  }

out1:
  curl_global_cleanup();
  fuse_unmount(fuse);
  fuse_remove_signal_handlers(se);

out2:
  free(real_mountpoint);

out3:
  dcfs_free_dirs(state.dirs);
  fuse_destroy(fuse);

out4:
  fuse_opt_free_args(&args);
  free(opts.mountpoint);

  return res;
}
