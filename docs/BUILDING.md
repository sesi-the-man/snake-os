# Building the image

The build runs inside a Docker container, so the only thing you need installed
is [Docker Desktop](https://www.docker.com/products/docker-desktop/) (macOS,
Windows) or Docker Engine (Linux).

## Quick start

```bash
git clone --recursive https://github.com/sesi-the-man/snake-os.git
cd snake-os
docker compose run --rm build
```

When it finishes, the terminal prints the image's SHA-256 and the files are in
`images/`:

```
images/snake_os.<version>.pi0.img        # flash this
images/snake_os.<version>.sha256.txt     # its checksum
```

`<version>` is the git tag you built (e.g. `v1.1`), or the commit hash if the
commit isn't tagged.

If you cloned without `--recursive`, fetch Buildroot first:

```bash
git submodule update --init
```

## How long it takes

The first build compiles a complete cross-compiler toolchain, the Linux kernel
and BusyBox from source. Rough times:

| Machine | First build | Rebuild |
|---|---|---|
| Linux x86-64, 8+ cores | 30–60 min | 10–20 min |
| Apple Silicon, native (see below) | 30–60 min | 10–20 min |
| Apple Silicon, `linux/amd64` emulation | 1–2 hours | 20–40 min |

Downloaded sources (`.buildroot_dl/`) and compiler caches (`.buildroot-ccache/`,
`.ccache/`) are kept in the repository folder between runs, which is what makes
rebuilds faster. These folders are git-ignored.

Give Docker most of your CPU cores and at least 8 GB of memory in Docker
Desktop's settings.

## Apple Silicon Macs

There are two ways to build:

**Fast, for testing:** just run `docker compose run --rm build`. The build runs
natively on the ARM chip. The image works, but its SHA-256 won't match official
releases.

**Reproducible, for releases or verifying a release:** build in an x86-64
container, which is how official images are made:

```bash
DOCKER_DEFAULT_PLATFORM=linux/amd64 docker compose run --rm build
```

Turn on "Use Rosetta for x86_64/amd64 emulation" in Docker Desktop's settings
first, or this build can take many hours.

Don't switch between the two modes back and forth: the caches from one don't
help the other.

## Verifying a release by rebuilding it

```bash
git checkout v1.1               # the release tag
git submodule update --init
DOCKER_DEFAULT_PLATFORM=linux/amd64 docker compose run --rm build
```

Compare the printed SHA-256 with the `.sha256.txt` file attached to the GitHub
release. They should be identical.

## Options

Pass options after `build`:

```bash
docker compose run --rm build --no-clean      # reuse the last build's output
docker compose run --rm build --legal-info    # also collect licenses and sources
docker compose run --rm build --help
```

`--no-clean` only helps if the previous build's container still exists; with
`--rm` the build output is discarded, so a normal run always does a clean build
(using the caches above).

## Building the game alone (no SD card image)

For testing the game on a Pi Zero that already runs Raspberry Pi OS, you can
build just the program. On a Mac, with [Zig](https://ziglang.org) installed
(`brew install zig`):

```bash
zig cc -target arm-linux-musleabihf -mcpu=arm1176jzf_s -static -Os -s -o snake src/snake.c
```

Copy `snake` to the Pi (enable SPI in `raspi-config` first) and run it with
`sudo ./snake`.

To run the game's self-test on your own computer (no hardware needed):

```bash
cc -DSIM -O2 -Wall -o sim src/snake.c && ./sim
```

It plays a few games with a simple AI, checks the menu and game logic, prints
`ok`, and writes screenshots as `.ppm` files.

## Troubleshooting

- **"The buildroot submodule is missing"**: run `git submodule update --init`.
- **Build fails while downloading**: re-run the same command. Downloads resume
  from `.buildroot_dl/`.
- **Out of disk space**: the build needs about 10 GB free inside Docker.
  Increase Docker Desktop's disk limit or run `docker system prune`.
