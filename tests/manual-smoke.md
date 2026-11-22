# Manual Smoke Tests

These cases cover Windows UI and installer paths that cannot be exercised
reliably from the command-line test suite.

## Smart Extract UI

- [ ] Extract an archive whose target directory already exists. Verify
      Rename, Overwrite, and Cancel each produce the documented result.
- [ ] Use Smart Extract to Desktop and verify the archive root is created
      directly under the Desktop.
- [ ] Smart extract an archive with no files and verify one warning is shown.

## File Manager

- [ ] Open a ZIP containing exactly one root-level `.tar` file. Verify the
      File Manager shows the tar contents instead of the outer ZIP entry.
- [ ] Open a ZIP containing a `.tar` file inside a subdirectory. Verify the
      tar is not expanded automatically.
- [ ] Open a ZIP containing a root-level `.tar` and another root file. Verify
      the tar is not expanded automatically.
- [ ] Open `.tar.zst`, `.tar.br`, `.tar.gz`, `.tar.lz4`, `.tar.lz5`,
      `.tar.liz`, and `.tar.7z` files directly in the File Manager. Verify the
      tar contents open without a memory-allocation error, including archives
      with only one root-level tar member.

## Explorer Context Menu

- [ ] For each format in `CompressFormats.h`, enable only that item in
      Settings, use it from the context menu, and verify the expected
      extension and a successful round trip.
- [ ] Configure a non-default level, dictionary, thread count, and memory
      limit, then create an archive through the direct context menu. Verify
      the resulting archive method properties with `7z l -slt`.
- [ ] Verify single-stream formats are used with one file and that tar/wim
      work with multiple selected files or a folder.
- [ ] Verify Smart Extract and Smart Extract to Desktop are present according
      to the configured flag defaults.
- [ ] Start with a fresh settings key and with a pre-existing
      `ContextMenu` value. Verify the intended defaults for both cases.

## Restart-Safe Installer

- [ ] Upgrade while no installed file is locked. Verify files are replaced
      immediately and no restart prompt is shown.
- [ ] Hold the installed shell DLL open, accept the Restart Manager prompt,
      and verify Explorer is restarted and the upgrade completes.
- [ ] Repeat the locked-file scenario and choose Cancel. Verify replacement is
      deferred until reboot and the installed version still reports success.
- [ ] Lock a file used by a non-restartable application. Verify the prompt
      lists the application name and PID, then verify Retry and Cancel paths.
- [ ] End the listed application while the prompt is open, click Retry, and
      verify the exited PID disappears and installation continues.
- [ ] Install to a new directory while an older installation exists. Verify
      the migration prompt, shell registration, and old-directory cleanup.
- [ ] Run the above installer checks on a system without Restart Manager to
      verify the delayed-replacement fallback.

## Low-Memory And Error Paths

- [ ] In a debug build, force failure of the Restart Manager resource-array
      allocation. Verify Setup does not crash and either schedules reboot or
      exits with an error.
- [ ] Verify an unresolved language insertion point makes the language patch
      script fail instead of silently producing a partial file.
