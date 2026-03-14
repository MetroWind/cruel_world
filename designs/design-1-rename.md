# Design Document 1: Project Rebranding to "Cruel World"

## 1. Overview and Rationale
The "Secure Online Journal" project is being rebranded to "Cruel World". This document outlines the technical steps required to transition the codebase, build system, and user interface to reflect this new identity.

The primary goal is to rename all occurrences of "Journal" to "Cruel World" (or `cruel_world` where appropriate for file paths and variable names) while maintaining full system functionality and code integrity.

### 1.1 Target Binary
The primary executable produced by the build system will be renamed from `journal_app` to `cruel_world`.

### 1.2 Target Project Name
The CMake project name will be changed from `Journal` to `CruelWorld`.

---

## 2. Structural Changes

### 2.1 Header Consolidation
The current structure places header files in a dedicated `include/journal/` subdirectory. To simplify the project structure, all headers will be moved directly into the `src/` directory alongside their implementation files.

*   **Action:** Move all `.hpp` files from `include/journal/` to `src/` and remove the `include/` directory entirely.
*   **Rationale:** For smaller projects or projects that do not export a public library API, keeping headers and source files co-located in the `src/` directory reduces directory traversal overhead and simplifies include paths.

### 2.2 Internal Logic and Class Names
The internal C++ class names, variable names, and namespaces (e.g., `JournalApp`) will remain unchanged. These are internal implementation details that are not visible to the end-user and do not affect the public branding of the application.

*   **Action:** No changes to internal C++ identifiers.
*   **Rationale:** Minimizes the risk of introducing bugs during the rebranding process and acknowledges that internal code names do not impact the user-facing product identity.

---

## 3. Build System Updates (`CMakeLists.txt`)

The `CMakeLists.txt` file is the blueprint for the build system. Multiple updates are required here to reflect the rebranding and the new header location.

### 3.1 Project Declaration
*   **Change:** `project(Journal)` -> `project(CruelWorld)`
*   **Explanation:** The `project()` command sets several variables, such as `PROJECT_NAME`, which are used throughout the build process.

### 3.2 Build Options
*   **Change:** `option(JOURNAL_BUILD_TESTS "Build unit tests" ON)` -> `option(CRUEL_WORLD_BUILD_TESTS "Build unit tests" ON)`
*   **Explanation:** Renaming user-facing options ensures that developers configuring the project see the correct project name in their configuration tools (like `cmake-gui` or `ccmake`).

### 3.3 Target Definitions
*   **Primary Executable:** Change `journal_app` to `cruel_world`.
    ```cmake
    add_executable(cruel_world ${SOURCE_FILES})
    ```
*   **Test Executable:** Change `journal_test` to `cruel_world_test`.
    ```cmake
    add_executable(cruel_world_test ${SOURCE_FILES_NO_MAIN} ${TEST_FILES})
    ```
*   **Explanation:** These names determine the final filenames of the binaries produced after compilation.

### 3.4 Include Directories
*   **Change:** Remove `include` from the `INCLUDES` variable (or `target_include_directories`). Add `src` if necessary, though files in `src/` can typically include other files in `src/` using relative paths like `#include "app.hpp"`.
*   **Explanation:** Since the `include/` directory is being removed, the build system must not look for it.

---

## 4. Source Code and Template Refactoring

### 4.1 Header Include Updates
All files that currently include headers from the `journal/` directory must be updated to reflect that the headers are now co-located in the `src/` directory.

*   **Example:** `#include "journal/app.hpp"` becomes `#include "app.hpp"`.
*   **Implementation Strategy:** A global search and replace for `#include "journal/` to `#include "` should be performed across all `.cpp` and `.hpp` files in the `src/` directory.

### 4.2 UI Text and Template Updates
The user interface should reflect the "Cruel World" branding. This involves updating HTML templates in the `templates/` directory.

*   **`templates/layout.html`:** Update the `<title>` and main header `<h2>`.
*   **`templates/setup.html`:** Update the `<h1>` and labels referring to the "Journal Passphrase".
*   **`templates/unlock.html`:** Update headers and labels.
*   **Explanation:** These changes ensure the end-user sees the new project name throughout their experience.

### 4.3 Log and Console Messages
Any strings printed to the console or logs that mention "Journal" should be updated.

*   **Location:** `src/main.cpp`
*   **Change:** `"Starting Journal server..."` -> `"Starting Cruel World server..."`

---

## 5. Documentation Updates

All project documentation must be updated to maintain consistency.

*   **`README.md`:** Update title and any project descriptions.
*   **`prd.md`:** Update the product name.
*   **`plan.md`:** Update any remaining references.

---

## 6. Verification and Validation Plan

After implementing the changes, the following steps must be taken to ensure the system remains stable:

1.  **CMake Reconfiguration:** Run `cmake -B build` and verify that no errors occur during the generation of the build files.
2.  **Clean Build:** Run `cmake --build build --clean-first` to compile the entire project from scratch. This ensures no stale artifacts from the previous project name interfere with the build.
3.  **Unit Test Execution:** Run the newly named `cruel_world_test` binary. All tests must pass.
    *   Command: `ctest --test-dir build`
4.  **Functional Verification:** Manually start the `cruel_world` server and navigate through the web interface (Setup, Unlock, Index) to ensure all templates render correctly and the branding is consistent.

---

## 7. Step-by-Step Implementation Guide

1.  **Move Headers:** Use the `mv` command to move headers into the `src` directory: `mv include/journal/*.hpp src/`, then remove the old directories `rm -r include`.
2.  **Update CMakeLists.txt:** Open the file, remove `include` from the include directories list, and replace all instances of `Journal` (case-sensitive) and `journal_app`/`journal_test` with `CruelWorld` and `cruel_world`/`cruel_world_test`.
3.  **Global Replace in Source:** Use a search-and-replace tool (or editor feature) within the `src/` directory to replace `#include "journal/` with `#include "`. Do not rename class or variable names.
4.  **Update Templates:** Manually edit files in `templates/` to replace "Journal" with "Cruel World".
5.  **Build and Test:** Follow the steps in Section 6.
