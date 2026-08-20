# Upgrading a site that runs the Faces block

This file is for Moodle **administrators**. It covers what happens to
`block_faces` when the site it is installed on is upgraded, and in particular
the code tree restructure introduced in Moodle 5.1.

For the plugin's own change history see `CHANGES.txt`.

---

## Do I need to read this?

| Your upgrade | Action required |
| --- | --- |
| Anything up to Moodle 5.0 | None. Upgrade normally. |
| Moodle 5.0 or earlier → **Moodle 5.1 or later** | **Yes.** The block must be moved by hand. See below. |
| Moodle 5.1 → 5.2 | None for the block; it is already under `public/`. |

The plugin code itself needs no change for any Moodle release from 4.1 to
5.2. What changes is **where the files live**.

---

## What changed in Moodle 5.1

Moodle 5.1 moved every web-servable file into a `public/` directory, so that
`config.php` and the CLI scripts sit outside the web root.

Three consequences matter here:

1. **`$CFG->dirroot` now points at `<moodleroot>/public`.**
   Core derives it from its own location (`public/lib/setup.php`), so there is
   no legacy layout to fall back on — the code tree itself has the new shape.

2. **The block directory moved.** `lib/components.json` maps the `block`
   plugin type to `public/blocks`, resolved against the repository root.

   ```
   Moodle 5.0 and earlier   <moodleroot>/blocks/faces
   Moodle 5.1 and later     <moodleroot>/public/blocks/faces
   ```

3. **The CLI scripts moved out of `public/`.** `admin/cli/` is now at the
   repository root, so upgrade commands are run from there.

`config.php` stays at the repository root. The `public/config.php` that ships
with core is only a shim that loads it.

---

## Upgrade procedure

Moodle does **not** relocate add-on plugins during a core upgrade, and no
migration script exists for this. The move is a manual step.

### 1. Check the platform

Moodle 5.2 requires:

- PHP **8.3.0** or later, 64-bit, with the `sodium` extension enabled
- `memory_limit` of at least 96M
- PostgreSQL 16+, MariaDB 10.11+, MySQL 8.4+, Aurora MySQL 8.0+, or
  SQL Server 2019+
- A source site already on Moodle **4.4 or later**

### 2. Inventory your add-on plugins

*Site administration → Plugins → Plugins overview*, filtered on "Additional".
Take this list **before** replacing the code tree: plugins that are not
carried over disappear from that page afterwards, which makes the list hard
to reconstruct.

### 3. Back up

Code tree, `moodledata`, and database. Repointing the web server at `public/`
is not a change you want to reverse under pressure.

### 4. Deploy the new code tree

Unpack Moodle 5.2 and point the web server's `DocumentRoot` at
`<moodleroot>/public`. Keep `config.php` at the repository root.

The router (`public/r.php`) sees more use from 5.2 onwards. On Apache, make
sure unknown paths fall through to it, for example with
`FallbackResource /r.php` in the `public/` directory configuration.

### 5. Move the block into `public/`

```bash
cp -a /path/to/old/moodle/blocks/faces \
      /path/to/new/moodle/public/blocks/faces
```

Repeat for every add-on from step 2.

### 6. Run the upgrade from the repository root

```bash
cd /path/to/new/moodle
php admin/cli/upgrade.php
```

Note the working directory: `admin/cli/` is at the root, not under `public/`.

### 7. Verify

*Site administration → Plugins → Plugins overview*. No plugin should be
reported as missing, and `block_faces` should appear with its version number.
Cross-check against the inventory from step 2.

### 8. Purge caches

```bash
php admin/cli/purge_caches.php
```

Then open a course that has the Faces block and confirm the block, the
"Show All Faces" page and the print view all render.

---

## Troubleshooting

### "Installed but missing from disk"

The block was left at the pre-5.1 path. Moodle still has its record in the
database but cannot find the files, so it offers to uninstall it — **do not
accept**, that would drop the block instances from your courses. Copy the
directory to `public/blocks/faces` as in step 5 and reload the page instead.

### The block renders but no groups are listed

Unrelated to the upgrade: group visibility depends on the course group mode
and on the viewer's `moodle/site:accessallgroups` capability.

### Very large group selections

The group selection form submits one value per checked group. On a course
with a very large number of groups this can exceed PHP's `max_input_vars`
(default 1000); Moodle recommends raising it to 5000 or more.

---

## What did not change

For the record, and to save the next person the audit: no API used by this
plugin was deprecated or removed between Moodle 4.1 and 5.2. `block_base`,
`get_enrolled_users()`, the `groups_*` functions, `core_user\fields`,
`single_select`, `user_picture`, the privacy provider and `styles.css`
loading are all unchanged, the legacy class names (`moodle_url`,
`html_writer`, `renderable`, …) still resolve through core's aliases, and
AMD modules still load through RequireJS.

The plugin's own `require_once(__DIR__ . '/../../../config.php')` resolves
correctly under both layouts, which is why no code change was needed.
