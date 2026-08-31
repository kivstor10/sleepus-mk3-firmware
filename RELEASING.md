# Publishing Sleepus MK3 firmware

The source repository is private. Published `.bin` files are copied to the public WebDFU updater by `.github/workflows/publish-firmware.yml`.

## Create a release

1. Build the firmware in AT32IDE.
2. Convert the ELF output to a raw binary:

   ```powershell
   arm-none-eabi-objcopy -O binary "project\AT32_IDE\Debug\SleeperCell_MK3 Bootloader.elf" "Sleepus-MK3.bin"
   ```

3. Commit and push all source changes.
4. Create and push a version tag:

   ```powershell
   git tag -a v1.0.1 -m "Sleepus MK3 v1.0.1"
   git push origin main v1.0.1
   ```

5. Create the private GitHub release with exactly one `.bin` asset:

   ```powershell
   gh release create v1.0.1 "Sleepus-MK3.bin#Sleepus-MK3.bin" --verify-tag --generate-notes
   ```

Publishing the release triggers the workflow. It verifies that exactly one `.bin` asset exists, generates a SHA-256 manifest, and pushes both files to `kivstor10/sleepus-mk3`.

## Verify

Check the **Publish firmware release** workflow in the private repository. The updater should then show the new version at:

https://kivstor10.github.io/sleepus-mk3/
