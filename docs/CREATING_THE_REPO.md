# Creating the GitHub repository (one time)

These steps turn the downloaded `snake-os` folder into a GitHub repository.
You only do this once.

## 1. Prerequisites

- A GitHub account to publish under, and a personal access token for it
  (classic, with the **repo** and **workflow** scopes) to use as the password
  when pushing.
- `git` set up with your name and email (or pass them to setup-repo.sh, see step 2):
  ```bash
  git config --global user.name "Your Name"
  git config --global user.email "you@example.com"
  ```

## 2. Create the local repository

Unzip the folder, then:

```bash
cd snake-os
./setup-repo.sh
```

This initializes git, adds Buildroot as a submodule pinned to the exact commit
the build was tested with, makes the first commit, and deletes the setup script.

## 3. Create the empty repository on GitHub

1. On github.com, click **+** (top right), then **New repository**.
2. Owner: your account. Name: `snake-os` (or another name; see below).
3. Visibility: Public.
4. Do **not** add a README, .gitignore or license (the folder already has them).
5. Click **Create repository**.

## 4. Push

```bash
git remote add origin https://sesi-the-man@github.com/sesi-the-man/snake-os.git
git push -u origin main
```

## 5. Recommended repository settings

- **Settings → General**: add a short description, e.g. "Tiny bootable snake
  game for Raspberry Pi Zero + SPI LCD".
- **Settings → Branches**: protect `main` (require pull requests) if more than
  one person will work on it.
- **Settings → Actions → General**: allow GitHub Actions if you want the
  automatic test builds (see docs/RELEASING.md).

## Renaming

If you pick a different repository name, update the clone URL in
`README.md` and `docs/BUILDING.md`. Nothing in the build depends on the name.
The image file name (`snake_os.<version>.pi0.img`) is set in `build.sh`.
