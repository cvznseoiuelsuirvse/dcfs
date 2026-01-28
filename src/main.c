#include "discord/api.h"
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
#include <getopt.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint8_t KEY[] = {
    0x4f, 0x16, 0x1b, 0x34, 0x68, 0x84, 0xde, 0x65, 0x0e, 0x96, 0xe0,
    0x8c, 0xd4, 0xf7, 0x07, 0xea, 0x82, 0x68, 0x7f, 0x47, 0xb5, 0xa3,
    0xa9, 0xc3, 0x38, 0x4e, 0xe2, 0x5f, 0x6b, 0x5f, 0x5f, 0x4e,
};

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
};
void format_time(char *formatted, time_t n) {
  struct tm *t = localtime(&n);
  strftime(formatted, 40, " %b %e  %Y", t);
}

void print_help() {
  printf("  -h, --help      show this message\n");
  printf("  -c, --channel   select channel\n");
  printf("  -g, --guild     select guild\n");
  printf("  -l, --ls        list guild or specified channel\n");
}

int dcfs_getattr(const char *path, struct stat *stbuf,
                 struct fuse_file_info *fi) {
  int res = 0;
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;
  memset(stbuf, 0, sizeof(struct stat));
  stbuf->st_uid = getuid();
  stbuf->st_gid = getgid();

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_size = 4096;
    stbuf->st_ctim.tv_sec = GUILD_ID.timestamp;
    stbuf->st_mtim.tv_sec = GUILD_ID.timestamp;

  } else if (count_char(path, '/') == 1) {
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_size = 4096;
    struct channel *channel;
    json_array *_ch = state->channels;
    json_array_for_each(_ch, channel) {
      if (strcmp(path + 1, channel->name) == 0) {
        stbuf->st_ctim.tv_sec = channel->id.timestamp;
        stbuf->st_mtim.tv_sec = channel->id.timestamp;
        break;
      }
    }
  }
  return res;
}

int dcfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi,
                 enum fuse_readdir_flags flags) {
  filler(buf, ".", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  filler(buf, "..", NULL, 0, FUSE_FILL_DIR_DEFAULTS);
  struct fuse_context *ctx = fuse_get_context();
  struct dcfs_state *state = ctx->private_data;

  if (strcmp(path, "/") == 0) {
    if (!state->channels) {
      state->channels = discord_get_channels(GUILD_ID.value);
      if (!state->channels) {
        fprintf(stderr, "failed to get channels\n");
        return -ENOENT;
      }
    }

    struct channel *channel;
    json_array *_c = state->channels;
    json_array_for_each(_c, channel) {
      if (channel->type == GUILD_TEXT && !channel->has_parent) {
        struct stat st;
        st.st_mode = S_IFDIR;
        filler(buf, channel->name, NULL, 0, FUSE_FILL_DIR_DEFAULTS);
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
