# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

`AGENTS.md` above is upstream AzerothCore's file and is overwritten by upstream merges — keep fork-specific
guidance in this file instead.

## What this repo is

The **mod-playerbots fork** of AzerothCore (`mod-playerbots/azerothcore-wotlk`), not upstream AC.

- Default branch is `Playerbot` (the fork's "master"); `test-staging` is the pre-release branch. CI maps the core
  branch to the module branch: `Playerbot` → mod-playerbots `master`, `test-staging` → `test-staging`.
- `origin/master` tracks upstream AzerothCore and is merged in periodically. Most of `src/` is upstream code, so
  keep fork edits minimal and localized — every line changed is a future merge conflict.

## modules/ is not part of this repository

`.gitignore` excludes `/modules/*`. `mod-playerbots`, `mod-arac`, `mod-transmog`, `mod-ah-bot-plus`, and
`mod-dungeon-clear` are **separate git checkouts nested in this tree**. Editing files there does nothing to this
repo's index — run git inside the module directory. `git status` at the root will never show module changes.

`mod-playerbots` layout: `src/{Ai,Bot,Db,Mgr,Script,Util}` (~630 .cpp), `conf/playerbots.conf.dist`,
`data/sql/playerbots/{base,updates,create,custom}` plus its own `data/sql/{world,characters}` updates. It has no
`CMakeLists.txt` — AC's module collector picks it up via `include.sh`.

## Playerbots ↔ core coupling

`modules/CMakeLists.txt` detects `modules/mod-playerbots` and adds `-DMOD_PLAYERBOTS` to the `database`,
`game-interface`, and `modules` targets. Core code guarded by `#ifdef MOD_PLAYERBOTS`:

- `src/server/database/Database/` — `DatabaseEnv{,Fwd}.h/.cpp`, `DatabaseLoader`, `DatabaseWorkerPool`,
  `Implementation/PlayerbotsDatabase.{h,cpp}`, `Updater/DBUpdater.cpp`
- `src/server/game/` — `Server/WorldSession.cpp`, `Server/WorldSessionMgr.cpp`, `Movement/MotionMaster.{h,cpp}`,
  `World/IWorld.h`, `World/World.h`
- `src/server/scripts/Commands/cs_server.cpp`

There is a **fourth database** beyond the three AGENTS.md lists: `acore_playerbots` (`PlayerbotsDatabase`,
configured by `PlayerbotsDatabaseInfo` in `playerbots.conf`). Changes to any file above must compile **both** with
and without the define — `.github/workflows/core-build*.yml` build without the module,
`core-build-playerbots.yml` builds with it.

## Build and run

A configured build tree already exists at `build/` (install prefix `/opt/AzerothCore/env/dist`,
`SCRIPTS=static`, `MODULES=static`, `BUILD_TESTING=OFF`). Prefer it over creating a new one:

```bash
cmake --build build -j$(nproc)          # incremental
./acore.sh compiler build               # dashboard equivalent (aliases: ./acore.sh c b)
./acore.sh                              # interactive menu (db create, client data, restarter, …)
```

Compiler settings come from `conf/dist/config.sh`; copy it to `conf/config.sh` to override locally (gitignored).

Runtime configs live in `env/dist/etc/*.conf`, generated at install time from `src/server/apps/*/*.conf.dist` and
each module's `conf/*.conf.dist`. All of `env/` is gitignored — edit the `.dist` templates, not the generated files.

## Tests

`BUILD_TESTING` is OFF in the existing build tree, so configure a separate one to run tests.

```bash
cmake -B build-test -DBUILD_TESTING=ON -DSCRIPTS=static -DMODULES=static
cmake --build build-test -j$(nproc) --target unit_tests
./build-test/src/test/unit_tests --gtest_filter=SpellProcChanceTest.*   # single suite/case
ctest --test-dir build-test -R <name>
```

Tests mirror the source tree (`src/test/server/game/Spells/*Test.cpp`); shared fixtures and stubs are in
`src/test/mocks/`. Modules add tests by appending to the global CMake property `ACORE_MODULE_TEST_SOURCES`
(consumed in `src/test/CMakeLists.txt:35`).

## Agent skills

`.agents/` is the source of truth for repo skills and rules; `.claude/skills/<name>` are relative symlinks into
`.agents/skills/<name>`. Add new skills under `.agents/` and symlink, never directly in `.claude/`.

## Environment gotcha

Repo *files* are owned by `Fester`, but the repo root directory and `build/` are still owned by the
`AzerothCore` daemon user. Existing files can be edited normally; creating a new file at the repo root needs
`sudo -u AzerothCore`. Git also rejects commands with `detected dubious ownership` because the worktree root
has a different owner than the caller — work around it per-command with
`git -c safe.directory=/Development/AzerothCore-PlayerBots …` rather than writing global config.
