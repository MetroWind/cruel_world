# Journal

A private, secure, intimate online journal server in modern C++23.

## Requirements

* All data in sqlite database
* Use libmw for HTTP server, HTTP requests, cryptography, database,
  and misc utility. All URL manipulation should be through the
  `mw::URL` class. You should check https://github.com/MetroWind/libmw
  to see what it can do.
* If a function can fail, it should return a `mw::E<>`.
* A journal entry is just a date with a Markdown body.
* Each journal entry should have a unique URL.
* There is at most one journal entry each day for each user.
* Use the MacroDown library to render markdown
  https://git.xeno.darksair.org/macrodown/
* Authentication is through an external OpenID Connect service (e.g.
  Keycloak). The way this works is the server will read a OIDC url
  prefix from the config (e.g.
  `https://auth.xeno.darksair.org/realms/{realm-name}/`). The server
  should then find the openid-configuration endpoint from that by
  appending `.well-known/openid-configuration` to the URL prefix. All
  OpenID endpoints are discovered in the response of the
  openid-configuration endpoint. See
  https://openid.net/specs/openid-connect-discovery-1_0.html#ProviderConfigurationResponse
  for details.
* Journal entries are encrypted with AES-256-GCM. Each user has their
  own key.
* One should not be able to read the journals by simply having access
  to the database file.
* Implement Envelope Encryption: each user has a unique Data Encryption
  Key (DEK) for AES-256 encryption of their journal entries.
* Users must provide a separate "Journal Passphrase" (distinct from their
  OIDC login) to unlock their journal.
* The Journal Passphrase is used to derive a Key Encryption Key (KEK)
  using a strong Key Derivation Function.
* The database stores the user's DEK encrypted by their KEK (also
  AES-256-GCM). The plaintext DEK is never written to disk.
* The decrypted DEK is kept only in volatile session memory and is wiped
  when the user's session expires or the server restarts.
* A user’s session should immediately expire when they close the
  browser tab.
* UX Flow: Users authenticate via OIDC first (Login), then enter their
  Journal Passphrase to decrypt their DEK (Unlock) before reading or
  writing entries.
* Support attachment uploading. Uploaded attachments are also stored
  in the database, and are encrypted in the same way as the journals.
* Each attachment will have a unique URL using a random slug.
* Provide an interface to manage attachments. There should be a way to
  easily copy the URL of the attachments.
* The web UI should be rendered in the C++ server. There should be
  minimal JavaScript (but also don’t be afraid to use JS if needed).
* Use the inja template library.
* All URL manipulation (routing, link generation, etc.) MUST be
  performed through the `mw::URL` class.
* Cmake example:
  https://github.com/MetroWind/shrt/raw/refs/heads/master/CMakeLists.txt
* Configuration will be a YAML file. Among others, one of the config
  parameter will be `root_url`, which would be the URL prefix for all
  URLs.
* All public functions should have unit test coverage.
