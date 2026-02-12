#ifndef DCFS_FS_H
#define DCFS_FS_H

#include "discord/discord.h"
#include <stdio.h>
#include <sys/stat.h>

typedef unsigned int dcfs_hash;
struct dcfs_file {
  char filename[256];
  size_t size;
  mode_t mode;
  gid_t gid;
  uid_t uid;
  time_t ctime;
  char *content;
  struct dcfs_message *messages[DISCORD_MAX_PARTS];
  size_t messages_n;
};

struct dcfs_dir {
  mode_t mode;
  gid_t gid;
  uid_t uid;
  time_t ctime;
  struct dcfs_channel channel;
  json_array *files;
};

struct dcfs_path {
  char dir[128];
  char filename[256];
};

void dcfs_path_init(const char *path, struct dcfs_path *p);

void dcfs_free_file(struct dcfs_file *file);
void dcfs_free_files(json_array *files);
json_array *dcfs_get_files(const char *channel_id);

void dcfs_free_dir(struct dcfs_dir *dir);
void dcfs_free_dirs(json_array *dirs);
json_array *dcfs_get_dirs(const char *guild_id);

#endif
