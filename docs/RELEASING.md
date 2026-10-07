# Releasing a new version

Each release is a git tag (e.g. `v1.1`) plus a GitHub Release with these files
attached:

| File | What it is |
|---|---|
| `snake_os.v1.1.pi0.img` | The SD card image |
| `snake_os.v1.1.sha256.txt` | SHA-256 checksum of the image |
| `snake_os.v1.1.sha256.txt.sig` | GPG signature of the checksum file (optional but recommended) |
| `snake_os.v1.1.legal-info.tar.gz` | Licenses and source code for everything in the image |

## 1. Prepare

1. Make sure `main` has everything you want in the release and the game's
   self-test passes:
   ```bash
   cc -DSIM -O2 -Wall -o sim src/snake.c && ./sim    # must print "ok"
   ```
2. Bump `SNAKE_VERSION` in `br2-external/package/snake/snake.mk` to match the
   new version, and commit.

## 2. Tag

```bash
git tag -a v1.1 -m "v1.1"
git push origin v1.1
```

If GitHub Actions is enabled, pushing the tag starts a build and creates a
**draft** release with the image attached (see below). Treat that build as a
convenience and a cross-check, not as the trusted release.

## 3. Build the release image locally

On a clean checkout of the tag, using the reproducible x86-64 build:

```bash
git checkout v1.1
git submodule update --init
DOCKER_DEFAULT_PLATFORM=linux/amd64 docker compose run --rm build --legal-info
```

## 4. Cross-check the hash

The SHA-256 printed by your build must match:
- the one from the GitHub Actions build (in the draft release), and ideally
- one more independent build by a second person.

If the hashes don't match, don't publish. Find out why first (usually a dirty
working tree: check that `git status` is clean and the version has no `-dirty`
suffix).

## 5. Sign the checksum file (recommended)

```bash
cd images
gpg --detach-sign --armor -o snake_os.v1.1.sha256.txt.sig snake_os.v1.1.sha256.txt
```

Use a key whose public half is published where users can find it (e.g. in the
repository or on keys.openpgp.org) and mention which key in the release notes.

## 6. Publish

1. Open the draft release on GitHub (or create one for the tag).
2. Attach (or replace) the four files from step 1's table, using your local
   build's files.
3. Paste the release notes from `docs/release-notes-template.md` and fill them in.
4. Click **Publish release**.

## Automatic builds (GitHub Actions)

`.github/workflows/build.yml` builds the image:
- when a `v*` tag is pushed, and then creates a draft release with the files;
- on demand from the **Actions** tab ("Run workflow"), with the files available
  as a downloadable artifact.

A full build takes about 1–2 hours on GitHub's free runners.

## GPL note

The image contains GPL-licensed software (Linux, BusyBox). When distributing
the image you must also make the corresponding source code available. The
`legal-info` archive produced by `--legal-info` contains it, which is why it is
attached to every release.
