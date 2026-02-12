#include "util.h"

#include <assert.h>
#include <regex.h>
#include <sys/stat.h>

static struct {
  regex_t comp;
  regmatch_t matches[3];
} part_regex;

void dcfs_path_init(const char *path, struct dcfs_path *p) {
  memset(p, 0, sizeof(struct dcfs_path));

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

void dcfs_free_file(struct dcfs_file *file) {
  for (size_t i = 0; i < file->messages_n; i++) {
    struct dcfs_message *message = file->messages[i];
    discord_free_message(message);
    free(message);
  }

  if (file->content)
    free(file->content);
}

void dcfs_free_files(json_array *files) {
  struct dcfs_file *file;
  json_array *_f = files;
  json_array_for_each(_f, file) dcfs_free_file(file);
  json_array_destroy(files);
}

json_array *dcfs_get_files(const char *channel_id) {
  regcomp(&part_regex.comp, "(.+)\\.PART([0-9]+)", REG_EXTENDED);
  json_array *messages = discord_get_messages(channel_id);
  json_array *files = NULL;

  if (messages) {
    files = json_array_new();

    struct dcfs_message *message;
    json_array *_messages = messages;
    json_array_for_each(_messages, message) {
      int ret = regexec(&part_regex.comp, message->filename,
                        sizeof(part_regex.matches) / sizeof(regmatch_t),
                        part_regex.matches, 0);

      if (ret != 0) {
        struct dcfs_file file;
        memset(&file, 0, sizeof(struct dcfs_file));

        struct dcfs_message *head = calloc(sizeof(struct dcfs_message), 1);
        assert(head);

        id_to_ctime(&file.ctime, message->id);
        file.mode = S_IFREG | 0644;
        file.gid = getgid();
        file.uid = getuid();
        file.size = message->size;
        snprintf(file.filename, sizeof(file.filename), "%s", message->filename);

        head->size = message->size;
        head->url = message->url;

        snprintf(head->id, sizeof(head->id), "%s", message->id);
        snprintf(head->filename, sizeof(head->filename), "%s",
                 message->filename);

        file.messages[0] = head;
        file.messages_n++;

        json_array_push(files, &file, sizeof(struct dcfs_file), JSON_UNKNOWN);
      }
    }

    _messages = messages;
    json_array_for_each(_messages, message) {
      int ret = regexec(&part_regex.comp, message->filename,
                        sizeof(part_regex.matches) / sizeof(regmatch_t),
                        part_regex.matches, 0);

      if (ret == 0) {
        regmatch_t m_filename = part_regex.matches[1];
        regmatch_t m_part = part_regex.matches[2];
        size_t part_n = strtol(message->filename + m_part.rm_so, NULL, 10);

        if (part_n >= DISCORD_MAX_PARTS) {
          discord_free_messages(messages);
          return NULL;
        }

        size_t filename_size = m_filename.rm_eo - m_filename.rm_so;
        char filename[filename_size + 1];
        snprintf(filename, filename_size, "%s",
                 message->filename + m_filename.rm_so);
        // memcpy(filename, message->filename + m_filename.rm_so,
        // filename_size); filename[filename_size] = 0;

        json_array *_files = files;
        struct dcfs_file *file;
        json_array_for_each(_files, file) {
          if (strcmp(file->filename, filename) == 0) {
            struct dcfs_message *part = calloc(sizeof(struct dcfs_message), 1);
            assert(part);

            part->size = message->size;
            part->url = message->url;
            snprintf(part->filename, sizeof(part->filename), "%s",
                     message->filename);

            file->messages_n++;
            file->messages[part_n] = part;
            break;
          };
        }
      }
    }

    json_array_destroy(messages);
    goto out;
  }

out:
  regfree(&part_regex.comp);
  return files;
}

inline void dcfs_free_dir(struct dcfs_dir *dir) {
  dcfs_free_files(dir->files);
};

void dcfs_free_dirs(json_array *dirs) {
  json_array *_d = dirs;
  struct dcfs_dir *dir;
  json_array_for_each(_d, dir) dcfs_free_dir(dir);
  json_array_destroy(dirs);
};

json_array *dcfs_get_dirs(const char *guild_id) {
  json_array *channels = discord_get_channels(guild_id);
  json_array *dirs = NULL;

  if (channels) {
    dirs = json_array_new();
    assert(dirs);

    json_array *_c = channels;
    struct dcfs_channel *channel;
    json_array_for_each(_c, channel) {
      struct dcfs_dir dir;
      memset(&dir, 0, sizeof(struct dcfs_dir));

      id_to_ctime(&dir.ctime, channel->id);
      dir.mode = S_IFDIR | 0755;
      dir.gid = getgid();
      dir.uid = getuid();
      memcpy(&dir.channel, channel, sizeof(struct dcfs_channel));

      json_array_push(dirs, &dir, sizeof(struct dcfs_dir), JSON_UNKNOWN);
    }
  }

  json_array_destroy(channels);
  return dirs;
}
