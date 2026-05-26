# AGENTS.md

> This file is maintained by AI Factory. Update it when the project structure changes significantly.

## Project Overview

SQLCipher is an open-source, standalone fork of SQLite that adds transparent 256-bit AES encryption to database files, along with HMAC authentication, PBKDF2 key derivation, and memory sanitization. Maintained by Zetetic LLC.

## Tech Stack

- **Language:** C (C99/C11)
- **Build System:** Autoconf + GNU Make; MSVC via `Makefile.msc`
- **Crypto Providers:** OpenSSL, LibTomCrypt, CommonCrypto (Apple), Windows CNG
- **Test Suite:** Tcl-based (SQLCipher subset)

## Project Structure

```
sqlcipher/
├── src/                  # All C source files
│   ├── sqlcipher.h       # SQLCipher public API & crypto provider interface
│   ├── sqlcipher.c       # Core encryption logic, codec context, PRAGMAs
│   ├── crypto_openssl.c  # OpenSSL provider
│   ├── crypto_cc.c       # Apple CommonCrypto provider
│   ├── crypto_libtomcrypt.c  # LibTomCrypt provider
│   ├── crypto_cng.c      # Windows CNG (bcrypt.dll) provider
│   ├── sqlite3.h.in      # SQLite upstream header template
│   ├── shell.c.in        # SQLite shell template
│   └── *.c               # SQLite core source (upstream, minimize changes)
├── ext/                  # SQLite extensions
│   ├── fts3/             # Full-Text Search v3
│   ├── fts5/             # Full-Text Search v5
│   ├── rtree/            # R-Tree spatial index
│   ├── json/             # JSON support
│   └── wasm/             # WebAssembly support
├── test/                 # Tcl-based test suite
│   └── sqlcipher.test    # Main SQLCipher-specific tests
├── tool/                 # Build tools and scripts
│   └── mksqlite3c.tcl    # Amalgamation builder
├── doc/                  # Technical documentation
├── autoconf/             # Autoconf build infrastructure
├── sqlite3.c             # Amalgamation (generated)
├── sqlite3.h             # Amalgamation header (generated)
├── configure             # Autoconf configure script
├── Makefile.in           # GNU Make template
├── main.mk               # Core make rules
├── Makefile.msc          # MSVC makefile
├── make.bat              # Windows build script
├── build_amalgamation.bat # Amalgamation build script
├── VERSION               # Current version string
└── CHANGELOG.md          # Release history
```

## Key Entry Points

| File | Purpose |
|------|---------|
| `src/sqlcipher.h` | SQLCipher API declarations and `sqlcipher_provider` interface |
| `src/sqlcipher.c` | Core implementation — codec context lifecycle, page encryption, PRAGMAs |
| `src/crypto_openssl.c` | Default crypto provider for Linux/Windows |
| `src/crypto_cc.c` | Default crypto provider for Apple platforms |
| `src/crypto_cng.c` | Windows CNG provider (Win8+, static or dynamic bcrypt.dll) |
| `configure` | Autoconf build entry point |
| `Makefile.in` | Main build rules template |
| `test/sqlcipher.test` | SQLCipher-specific Tcl test suite |
| `sqlite3.c` / `sqlite3.h` | Amalgamation distribution files |

## Documentation

| Document | Path | Description |
|----------|------|-------------|
| README | README.md | Project overview, build instructions, testing |
| Changelog | CHANGELOG.md | Release history and notable changes |
| CNG provider | docs/cng-provider.md | Windows CNG provider: build modes, requirements, CMake example |
| SQLite license | SQLITE_LICENSE.md | Upstream SQLite license |
| BSD license | LICENSE.md | SQLCipher BSD license |

## AI Context Files

| File | Purpose |
|------|---------|
| AGENTS.md | Project structure map for AI agents and new contributors |
| .ai-factory/DESCRIPTION.md | Detailed project specification and tech stack |
| .ai-factory/ARCHITECTURE.md | Architecture guidelines and patterns |
| .ai-factory/rules/base.md | Detected coding conventions for this codebase |

## Agent Rules

- Decompose shell commands — run steps sequentially rather than chaining with `&&`
  - ❌ Incorrect (combined): `./configure --with-tempstore=yes CFLAGS="-DSQLITE_HAS_CODEC" && make`
  - ✅ Correct (decomposed): First `./configure --with-tempstore=yes CFLAGS="-DSQLITE_HAS_CODEC"`, then `make`
- All SQLCipher-specific code changes must be inside `#ifdef SQLITE_HAS_CODEC` guards
- Never modify SQLite upstream source files directly — only SQLCipher-specific files (`sqlcipher.h`, `sqlcipher.c`, `crypto_*.c`)
- Always run `make sqlcipher-test` after changes to crypto or PRAGMA logic
- Base branch for contributions is `master` (not `main`)
