# Advanced Settings

In the same directory as the `compose.yml` file, create a `config.ini` file and write the following content to modify as needed:

```ini
; API token, default empty
CORE_API_TOKEN=your_token
; Internal port, default 8989
CORE_PORT=8989
; Log level, default WARNING
CORE_LOG_LEVEL=WARNING
; Number of worker threads, default automatically set
CORE_NUM_WORKERS=
; Request timeout, default 30000ms
CORE_REQUEST_TIMEOUT=
; Maximum number of parallel translations, default automatically set
CORE_MAX_PARALLEL_TRANSLATIONS=
```

> You can also set the configuration using the same name in the environment variable.
>
> The entries in the `config.ini` file will override the environment variable settings.
