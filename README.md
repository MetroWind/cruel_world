# Secure Online Journal

A private, secure, and intimate online journaling application built with C++23.

## Features

- **End-to-End Privacy:** Journal entries and attachments are encrypted using AES-256-GCM.
- **Envelope Encryption:** Each user has a unique Data Encryption Key (DEK), which is itself encrypted by a Key Encryption Key (KEK) derived from a user-provided passphrase.
- **Secure Sessions:** The decrypted DEK is held only in volatile memory and is securely zeroed when the session expires or the server restarts.
- **Background Session Reaper:** Periodically wipes expired sessions to minimize the window of vulnerability.
- **Modern Web Interface:** A fast, responsive, vanilla CSS-based UI with minimal JavaScript.
- **Markdown Support:** Write entries in Markdown with live preview and automatic rendering via MacroDown.
- **Attachments:** Securely upload and manage files referenced in your journal.
- **OIDC Authentication:** Integrates with external OpenID Connect providers (e.g., Keycloak).

## Tech Stack

- **Language:** C++23
- **Frameworks:** [libmw](https://github.com/MetroWind/libmw) (HTTP, SQLite, Crypto, URL)
- **Markdown:** [MacroDown](https://git.xeno.darksair.org/macrodown/)
- **Templating:** [Inja](https://github.com/pantor/inja)
- **Config:** [RapidYAML (ryml)](https://github.com/biojppm/rapidyaml)
- **Build System:** CMake (v3.24+)

## Prerequisites

- CMake (v3.24 or higher)
- A modern C++ compiler supporting C++23 (e.g., GCC 13+, Clang 16+)
- OpenSSL development libraries
- SQLite3 development libraries
- Zlib and Libcurl (required by libmw dependencies)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . -j $(nproc)
```

## Running

1. Create a `config.yaml` based on the configuration guide below.
2. Run the application:
   ```bash
   ./journal_app config.yaml
   ```

## Configuration

The application uses a YAML configuration file. Example:

```yaml
# Network settings
bind_address: "127.0.0.1"
bind_port: 8080

# The base URL for the application. All redirects and links are based on this.
root_url: "http://localhost:8080/"

# Storage path for the SQLite database
database_path: "journal.db"

# OIDC Settings
oidc_url_prefix: "https://auth.example.com/realms/myrealm/"
oidc_client_id: "journal-client"
oidc_client_secret: "your-client-secret"
```

## Running Tests

To build and run unit tests:

```bash
cmake -DJOURNAL_BUILD_TESTS=ON ..
cmake --build .
ctest --test-dir . --output-on-failure
```

*Note: In debug or test builds, AddressSanitizer and UndefinedBehaviorSanitizer are enabled by default for improved safety during development.*

## Security Model

The journal uses **Envelope Encryption**. Your journal passphrase is never stored. It is used with a unique salt and a strong KDF to derive a KEK. This KEK decrypts your master DEK stored in the database. The decrypted DEK is only ever stored in the server's RAM and is wiped using `mw::crypto::secure_zero` upon logout or session timeout.
