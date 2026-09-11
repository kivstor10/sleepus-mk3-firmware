# Sleepus MK3 WebDFU Updater

Static Chrome/Edge WebUSB updater for the AT32 factory DFU bootloader (`2E3C:DF11`). Normal updates erase only the 2 KiB sectors covered by the raw `.bin` image and preserve the settings slots at `0x080FE000` through `0x080FFFFF`.

Full-chip recovery is an explicit option for a device protected with RDP Level 1. It issues a mass erase to remove protection and permanently deletes saved settings.

## Publish with GitHub Pages

1. Commit the `webdfu-updater` directory to the repository.
2. In GitHub, open **Settings > Pages**.
3. Under **Build and deployment**, choose **Deploy from a branch**.
4. Select the branch containing these files and the `/webdfu-updater` folder if GitHub offers it.

GitHub Pages branch publishing only offers `/` or `/docs`. If `/webdfu-updater` is not available, either rename this directory to `docs` or copy its contents into an existing `docs/webdfu-updater` directory and link to that path.

WebUSB requires a secure context. Use the generated `https://<account>.github.io/<repository>/...` URL; opening `index.html` directly from disk will not work.

## Local preview

```powershell
python -m http.server 8765 --directory webdfu-updater
```

Open `http://localhost:8765` in Chrome or Edge. Localhost is treated as a secure context for WebUSB development.
