# Implementation Plan: Secure Online Journal

This document outlines the step-by-step implementation plan for the Secure Online Journal server, based on the requirements in `prd.md` and the architecture detailed in `designs/design-0.md`.

## Phase 1: Project Setup & Core Infrastructure
**Goal:** Establish the build system, dependencies, directory structure, and basic application configuration.

1. **CMake Initialization:**
   - Create `CMakeLists.txt` using modern CMake (v3.24+).
   - Use `FetchContent` to download and integrate dependencies: `libmw`, `macrodown`, `inja`, `ryml`, and a testing framework (e.g., `Catch2` or `GTest`).
2. **Directory Structure:**
   - Scaffold directories: `src/`, `include/journal/`, `tests/`, `templates/`, `static/`, and `database/` (for local dev DB).
3. **Configuration Module:**
   - Create `Config` struct to hold parsed YAML data (`root_url`, `database_path`, OIDC settings, etc.).
   - Implement configuration parser using `ryml`.
   - Write unit tests for configuration parsing.

## Phase 2: Database Layer
**Goal:** Establish persistent storage schemas and access methods.

1. **Connection Management:**
   - Implement a singleton or pool for `mw::sqlite::Connection`.
   - Ensure `PRAGMA foreign_keys = ON;` and `PRAGMA journal_mode = WAL;` are executed on connect.
2. **Schema Initialization:**
   - Write C++ logic to execute `CREATE TABLE IF NOT EXISTS` for `users`, `entries`, and `attachments` based on the design document.
3. **Data Access Objects (DAOs):**
   - Create CRUD functions/classes for each table. All functions must return `mw::E<T>` to handle potential SQLite errors gracefully.
   - Example: `mw::E<User> getUser(int id);`, `mw::E<void> insertEntry(const Entry& entry);`.
4. **Unit Tests:**
   - Write tests for the DB layer using an in-memory SQLite database (`:memory:`).

## Phase 3: Cryptography & Security
**Goal:** Implement envelope encryption, key derivation, and volatile memory protection.

1. **Crypto Utilities (Wrapping `mw::crypto`):**
   - Implement `CryptoEngine` class.
   - Implement Key Derivation Function (Argon2 or PBKDF2) to derive the Key Encryption Key (KEK) from the Journal Passphrase.
   - Implement AES-256-GCM encryption/decryption routines for the Data Encryption Key (DEK).
   - Implement AES-256-GCM encryption/decryption routines for entry bodies and attachments using the plaintext DEK and a 96-bit nonce.
2. **Session Management:**
   - Implement `SessionManager` using secure, random session IDs (SHA-256).
   - Store sessions in memory with states: `LOCKED` (no DEK) and `UNLOCKED` (holding decrypted DEK).
   - Ensure the DEK is wiped from memory using `mw::crypto::secure_zero` when destroyed.
3. **The Reaper Task:**
   - Implement a background thread or asynchronous task that periodically scans for inactive sessions and destroys them, ensuring DEKs are wiped.
4. **Unit Tests:**
   - Write extensive tests for key derivation, encryption/decryption roundtrips, and secure memory erasure.

## Phase 4: Identity & OIDC
**Goal:** Implement the OpenID Connect discovery and authentication flow.

1. **OIDC Discovery:**
   - Implement HTTP client logic (via `libmw`) to fetch and parse `/.well-known/openid-configuration` on startup.
2. **Authentication Flow:**
   - Implement `/auth/login` (Redirect to provider).
   - Implement `/auth/callback` (Exchange code for token, extract `sub`, find/create user in DB, initialize `LOCKED` session).
3. **Setup & Unlock Handlers:**
   - Implement `/auth/setup` to accept a new passphrase, generate a DEK, encrypt it with the derived KEK, and save it to the database.
   - Implement `/auth/unlock` to accept the passphrase, derive the KEK, decrypt the DEK, and transition the session to `UNLOCKED`.

## Phase 5: Application Core & Routing
**Goal:** Implement the main journal functionality, markdown parsing, and attachment handling.

1. **Routing Initialization:**
   - Setup `mw::http-server` router. Ensure all routes account for the configured `root_url` path component using `mw::URL`.
2. **Markdown Integration:**
   - Integrate `MacroDown` parser and renderer. Create a wrapper function that safely parses user input and renders HTML.
3. **Journal Entry Endpoints:**
   - `GET /`: Resolve today's date, redirect to `/entry/:slug` if exists, or render blank editor.
   - `GET /entry/:slug`: Read from DB, decrypt using session DEK, render HTML.
   - `POST /entry/:date`: Encrypt payload with session DEK, generate slug if new, `UPSERT` into DB. Return new slug.
4. **Attachment Endpoints:**
   - `POST /api/attachments`: Receive file, encrypt metadata and payload, save to DB, return slug.
   - `GET /attachments/:slug`: Decrypt and serve file.
   - `GET /attachments/manage`: Render management UI.
5. **Unit Tests:**
   - Write tests for handler logic (mocking the HTTP server where possible) and URL manipulations.

## Phase 6: Frontend & Templating
**Goal:** Build a secure, JavaScript-light, vanilla CSS user interface.

1. **Base Templates (`inja`):**
   - Create `layout.html` including the 2-column grid structure (Sidebar + Editor).
   - Ensure the `root_url` is passed into every template context for asset and link generation.
2. **Styling:**
   - Write `static/style.css` using Vanilla CSS and CSS variables. Achieve the seamless `<textarea>` look.
3. **Frontend Interactions (Minimal JS):**
   - Implement auto-save (debounced POST to `/entry/:date`).
   - Implement `history.replaceState` to update the URL upon first save (when a slug is assigned).
   - Implement drag-and-drop file upload to `/api/attachments` and inject markdown image tags into the textarea.
   - Implement copy-to-clipboard buttons in the attachment manager.

## Phase 7: Polish, QA, and Delivery
**Goal:** Ensure robustness, security, and conformance to requirements.

1. **Security Audit:**
   - Review all DB queries for parameterization.
   - Verify that plaintext DEKs are never written to disk or logged.
   - Verify session cookies lack `Expires` and `Max-Age` (transient).
2. **Comprehensive Testing:**
   - Complete unit test coverage for all public C++ functions.
   - Run memory sanitizers (`-fsanitize=address,undefined`) to ensure no leaks or dangling pointers.
3. **Documentation:**
   - Create a `README.md` detailing build instructions, configuration, and runtime deployment.
