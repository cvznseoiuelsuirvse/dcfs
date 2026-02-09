# dcfs (Discord Filesystem)

`dcfs` is a FUSE-based filesystem that allows you to use a Discord server as a storage backend. Channels are treated as directories, and messages with attachments are treated as files.

## Prerequisites

- `libcurl`
- `fuse3`
- A Discord Bot Token
- A Discord Guild (Server) ID

## Building

```bash
meson setup build
meson compile -C build
```

## Usage

1. Set the required environment variables:
   ```bash
   export DCFS_TOKEN="your_discord_bot_token"
   export DCFS_GUILD_ID="your_guild_id"
   ```

2. Mount the filesystem:
   ```bash
   ./bin/dcfs MOUNTPOINT OPTIONS
   ```
or (Macos)
   ```bash
   ./bin/dcfs MOUNTPOINT -o noappledouble OPTIONS
   ```

## Features

- Channels as directories
- Attachments as files
- Support for large files (split into parts)
- Basic file operations (read, write, rename, delete)

## Demo
[![Demo]](https://raw.githubusercontent.com/cvznseoiuelsuirvse/dcfs/asset/demo.mp4)

Chunked file handling (MAX_FILESIZE 256)
[![Demo1]](https://raw.githubusercontent.com/cvznseoiuelsuirvse/dcfs/asset/demo1.mp4)
