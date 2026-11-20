# KyPost source ae383bf3e238

Source: https://github.com/Busnes-app/KyPost-for-Linux/commit/ae383bf3e238c105803271ddf4fb1e0387baeb79

[Full release history](https://github.com/Busnes-app/KyPost-for-Linux/blob/ae383bf3e238c105803271ddf4fb1e0387baeb79/CHANGELOG.md)

### Changes

- When the KyPost server holds an account's mail until email encryption setup is finished on the web (KyPost-Server #666), the mail section shows "Finish encryption setup" with "Open KyPost on the web" and "Check again" instead of a refresh error. Mail sync pauses rather than retrying, and resumes after a successful check or when the app returns to the foreground. The pairing, keys, cached mail and drafts are kept.

### Upgrade impact

- Rebuild or install the updated Flatpak. No data, account, key or configuration migration is required. Older servers never send this answer, so nothing changes against them.
