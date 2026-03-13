# Comprehensive System Design: Secure Online Journal

## 1. System Overview & Architecture

### 1.1 Project Vision
The Journal server is a highly secure, private, and intimate online journaling application. Its primary mandate is data confidentiality: user entries and attachments must remain entirely unreadable to anyone lacking the user's specific Journal Passphrase, including system administrators with direct access to the database or storage volumes. 

### 1.2 Technology Stack
*   **Language:** C++23. Utilizes modern features (concepts, ranges, `std::expected` or equivalent custom error handling).
*   **Core Framework:** `libmw` (https://github.com/MetroWind/libmw).
    *   `mw::http-server`: Wrapper over `cpp-httplib` for handling HTTP/1.1 requests, routing, and OIDC middleware.
    *   `mw::crypto`: C++ wrapper around OpenSSL for hashing, RNG, key derivation, and symmetric encryption (AES-GCM).
    *   `mw::sqlite`: C++23 wrapper for SQLite3 C API for persistent storage.
    *   `mw::URL`: Dedicated class for robust URL parsing and manipulation. **All URL construction and manipulation MUST use this class.**
*   **YAML Parser:** `ryml` (RapidYAML), used for parsing the server configuration file.
*   **Markdown Engine:** `MacroDown` (https://git.xeno.darksair.org/macrodown/). A CommonMark-compatible processor with a TeX-like macro system, generating HTML from Abstract Syntax Trees (AST).
*   **Template Engine:** `inja` (https://github.com/pantor/inja). A template engine for modern C++ inspired by Jinja2, used for server-side HTML rendering.
*   **Build System:** CMake (v3.24+). Dependencies are managed via `FetchContent`.

### 1.3 Architectural Components
1.  **Web Server Layer:** Listens on a configured port or UNIX domain socket. Routes HTTP requests to specific handlers, accounting for the configured `root_url`.
2.  **Authentication Middleware:** Intercepts requests. Validates session cookies. If unauthenticated, redirects to the OIDC flow. If authenticated but locked, restricts access to the "Unlock" endpoints.
3.  **Application Handlers:** The core logic. Validates input, orchestrates database reads/writes, invokes the encryption/decryption layer, and formats output using Inja templates.
4.  **Encryption Layer:** A dedicated module handling Envelope Encryption. It manages the Data Encryption Key (DEK) lifecycle, Key Derivation Functions (KDF), and AES-256-GCM operations.
5.  **Data Layer:** Encapsulates SQLite interactions. Maps C++ objects to SQL queries.

---

## 2. Coding & Style Conventions

To maintain consistency across the project, the following conventions (derived from the global `GEMINI.md` standard) MUST be strictly adhered to:

### 2.1 Naming Conventions
*   **File Names:** `snake_case` (e.g., `journal_handler.cpp`, `encryption_engine.hpp`).
*   **Classes, Types, Enums, and Structs:** `CapCase` (e.g., `UserSession`, `JournalEntry`, `CryptoError`).
*   **Variables (Local and Member):** `snake_case` (e.g., `encrypted_body`, `user_id`).
*   **Global Constants & Enum Cases:** `UPPER_CASE` (e.g., `MAX_ATTACHMENT_SIZE`, `STATE_UNLOCKED`).
*   **Functions/Methods:**
    *   `camelCase` for multi-word names (e.g., `decryptEntry`, `handleLogin`).
    *   `lower case` for single-word names (e.g., `render()`, `save()`).

### 2.2 Formatting & Memory Management
*   **Indentation:** 4 spaces.
*   **Braces:** Left brace on a new line, unless the block is entirely empty.
*   **Parentheses:** No space before parentheses.
    ```cpp
    if(condition)
    {
        doSomething();
    }
    ```
*   **Pointers:** Prefer `std::unique_ptr` over `std::shared_ptr`. Ownership should be strictly single unless shared state is definitively required.
*   **Error Handling:** All functions that can fail MUST return a `mw::E<T>` (from `libmw`). Exceptions should be avoided for control flow.

---

## 3. Database Architecture & Schema Design

All application state is stored in a single SQLite database file. Since all sensitive fields are encrypted at the application layer, the SQLite database does not require database-level encryption (like SQLCipher).

### 3.1 Connection Management
*   The application maintains a single `mw::sqlite::Connection` pool or instance.
*   Foreign keys MUST be enabled upon connection: `PRAGMA foreign_keys = ON;`.
*   Write-Ahead Logging (WAL) MUST be enabled for performance: `PRAGMA journal_mode = WAL;`.

### 3.2 Schema Definitions

#### 3.2.1 `users` Table
Stores user identities linked to the external OIDC provider and the cryptographic material needed to unlock their data.

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    oidc_sub TEXT UNIQUE NOT NULL,    
    encrypted_dek BLOB NOT NULL,      
    salt BLOB NOT NULL,               
    dek_nonce BLOB NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

#### 3.2.2 `entries` Table
Stores the actual journal entries. A user can have at most one entry per day, but each entry is also given a unique slug so it has a distinct, permanent URL.

```sql
CREATE TABLE entries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    
    -- A globally unique, URL-safe string (e.g., 8 chars of base62) generated upon creation.
    -- This ensures every entry has a unique, persistent URL.
    slug TEXT UNIQUE NOT NULL,
    
    -- Stored as 'YYYY-MM-DD' to allow easy querying and uniqueness enforcement.
    date TEXT NOT NULL,               
    
    -- The Markdown string, encrypted using the user's plaintext DEK via AES-256-GCM.
    encrypted_body BLOB NOT NULL,     
    
    -- The 96-bit nonce used for AES-GCM encryption of this specific entry.
    nonce BLOB NOT NULL,              
    
    last_modified DATETIME DEFAULT CURRENT_TIMESTAMP,
    
    UNIQUE(user_id, date)
);
```
*Rationale:* Enforcing `UNIQUE(user_id, date)` prevents multiple entries per day. The `slug` guarantees that the entry has a unique URL routing identifier (e.g., `<root_url>/entry/<slug>`).

#### 3.2.3 `attachments` Table
Stores files uploaded by the user to be referenced in their journal entries.

```sql
CREATE TABLE attachments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    
    -- Random 8-character slug for the URL.
    slug TEXT UNIQUE NOT NULL,        

    encrypted_filename BLOB NOT NULL,
    filename_nonce BLOB NOT NULL,
    encrypted_mime_type BLOB NOT NULL,
    mime_type_nonce BLOB NOT NULL,
    encrypted_data BLOB NOT NULL,     
    data_nonce BLOB NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

---

## 4. Configuration

The application is configured via a YAML file (e.g., `config.yaml`), parsed at startup using `ryml` (RapidYAML).

### 4.1 YAML Structure
```yaml
# The base URL prefix for the entire application (e.g., if hosted in a subpath or specific domain).
# All generated internal links and API routes must be prefixed with this value.
root_url: "https://journal.example.com/my-journal"

# Network binding
bind_address: "127.0.0.1"
bind_port: 8080

# File paths
database_path: "/var/lib/journal/journal.db"

# OpenID Connect Settings
oidc_url_prefix: "https://auth.example.com/realms/journal/"
oidc_client_id: "journal-app"
oidc_client_secret: "secret-key-here"
```

### 4.2 `root_url` Routing & Templating
*   **Routing:** The internal `mw::http-server` router must be configured using `mw::URL` to prefix all its endpoint paths with the path component of the `root_url` (e.g., `/my-journal`).
*   **Templating:** The `root_url` string must be passed into every `inja` template context. All links (`<a href="...">`), resources (`<link rel="...">`), and API calls MUST be constructed using `mw::URL` (either server-side before rendering or via a helper if implemented) to ensure consistency.

---

## 5. Cryptography & Security Model

The security model relies on **Envelope Encryption**. Every user has a unique 256-bit symmetric key (DEK) that encrypts all their data. This DEK is itself encrypted by a Key Encryption Key (KEK) derived from a user-memorized passphrase.

### 5.1 Cryptographic Primitives (via `mw::crypto`)
*   **Symmetric Encryption:** AES-256-GCM (Galois/Counter Mode).
*   **Key Derivation Function (KDF):** Argon2id or PBKDF2-HMAC-SHA256.
*   **Hashing:** SHA-256 (used for session IDs). 
*   **Random Number Generation:** CSPRNG via `mw::crypto::rand`.

### 5.2 Session Reaper & Memory Safety
Since the decrypted DEK is stored in volatile memory and browser tab closures cannot be reliably detected by the server:
*   A background "Reaper" task must run periodically (e.g., every 5 minutes).
*   It must identify sessions that have been inactive (no requests) for a configurable duration (e.g., 30 minutes).
*   Expired sessions must be destroyed, and their associated DEKs must be securely zeroed in memory using `mw::crypto::secure_zero`.

*(The Setup, Unlock, and Encryption Lifecycle flows remain exactly as detailed in previous iterations, ensuring the DEK is only held in volatile session memory.)*

---

## 6. Identity & Access Management (OIDC)

Authentication relies exclusively on external OpenID Connect providers. The server acts as a Relying Party (RP).

### 6.1 OIDC Discovery Phase
1.  The server appends `/.well-known/openid-configuration` to the `oidc_url_prefix`.
2.  It parses the JSON response to extract `authorization_endpoint`, `token_endpoint`, and `jwks_uri`.

### 6.2 Session State Machine
A session is represented by a strictly unguessable session ID stored in a browser cookie. 
*   **Cookie Policy:** The session cookie MUST NOT have an `Expires` or `Max-Age` attribute, making it a transient session cookie that is deleted when the tab/browser is closed.

---

## 7. API Design & Request Handlers

All HTTP request processing flows through `mw::http-server`. Paths below are shown *relative* to the `root_url` path component. (e.g., if `root_url` is `https://domain.com/app`, then `/auth/login` maps to the absolute path `/app/auth/login`).

### 7.1 Authentication Handlers (`/auth/*`)

*   `GET /auth/login` -> Redirects to OIDC `authorization_endpoint`.
*   `GET /auth/callback` -> Exchanges code for token, creates locked session.
*   `GET /auth/setup` -> Renders passphrase setup UI.
*   `POST /auth/setup` -> Executes DEK generation and encryption.
*   `GET /auth/unlock` -> Renders passphrase input UI.
*   `POST /auth/unlock` -> Decrypts DEK, unlocks session.
*   `POST /auth/logout` -> Destroys session, zeroing DEK in memory.

### 7.2 Application Handlers

All these handlers require a session in the `UNLOCKED` state. **All returned URLs and redirects MUST be constructed via `mw::URL`.**

*   `GET /`
    *   **Logic:** Determines the current server local date (`YYYY-MM-DD`). Queries the DB to see if an entry exists for today. If it does, redirects to the URL for `<root_url>/entry/:slug`. If not, renders the editor for a new entry today.
*   `GET /entry/:slug`
    *   **Path Var:** `slug` (Unique entry identifier).
    *   **Logic:**
        1. Query `entries` table for `user_id` and `slug`.
        2. If exists, decrypt `encrypted_body` using `session.decrypted_dek`. Parse decrypted markdown using `MacroDown`, render to HTML.
        3. Fetch a summary/list of past entries (their dates and slugs) to render the navigation calendar.
        4. Pass HTML content, date string, `slug`, and navigation data (with slugs) to the `editor.html` template.
*   `POST /entry/:date`
    *   **Path Var:** `date`.
    *   **Payload:** `application/json` or `form-urlencoded` containing `markdown_body`.
    *   **Logic:**
        1. Check if an entry for this `date` exists. If not, generate a new unique 8-character `slug`.
        2. Generate new 12-byte `nonce`.
        3. Encrypt `markdown_body` using `session.decrypted_dek` and `nonce`.
        4. Perform an `UPSERT` into `entries`.
        5. Return JSON containing the `slug` and the full URL constructed via `mw::URL`.
*   `POST /api/attachments`
    *   **Logic:** Generates an 8-character random `slug`, encrypts file bytes/metadata, and inserts into `attachments`. Returns the full attachment URL constructed via `mw::URL`.
*   `GET /attachments/:slug`
    *   **Logic:** Decrypts file based on `slug` and serves it with the decrypted MIME type.
*   `GET /attachments/manage`
    *   **Logic:** Renders UI listing all attachments, providing copyable URLs constructed via `mw::URL`.

---

## 8. Markdown & Rendering Engine

### 8.1 Integration with MacroDown
1.  **Parsing:** `macrodown::MacroDown::parse()` generates an AST.
2.  **Transformation:** Ensure local Markdown links and image tags respect the `root_url`.
3.  **Rendering:** `macrodown::MacroDown::render(*root)` generates the final HTML string.

---

## 9. Web Frontend & UI Design

### 9.1 Styling Rules
*   **Frameworks:** Strictly prohibited. **Vanilla CSS only.**
*   **Theming:** Rely entirely on CSS Custom Properties defined in `:root`.

### 9.2 Layout Architecture
*   **Grid:** `display: grid; grid-template-columns: 250px 1fr;`
*   **Sidebar:** Displays calendar links to unique entry URLs (`<a href="{{root_url}}/entry/{{entry.slug}}">`).
*   **Main Editor:** The `<textarea>` must be styled to look seamless (`border: none; outline: none; resize: none;`).

### 9.3 Minimal JavaScript Requirements
1.  **Auto-Save:** Debounced `fetch` to `<root_url>/entry/` + currentDate. On the first save, it should receive the new `slug` and use `history.replaceState` to update the browser URL to the unique entry URL (`<root_url>/entry/<slug>`).
2.  **Drag-and-Drop:** `fetch` to `<root_url>/api/attachments`, injecting `![Attachment](url)` into the editor.
3.  **Clipboard Interaction:** Button to copy attachment URLs.

---

## 10. Build System & CMake Integration

### 10.1 Dependency Management
Uses `FetchContent` to pull `libmw`, `macrodown`, `inja`, and `ryml`.

```cmake
# CMakeLists.txt snippet highlighting ryml addition
FetchContent_Declare(ryml 
    GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(libmw macrodown inja ryml)

# ... target_link_libraries includes ryml::ryml
```