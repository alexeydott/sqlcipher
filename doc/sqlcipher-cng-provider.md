# Windows CNG Crypto Provider for SQLCipher

SQLCipher ships with a native Windows cryptographic provider built on the
**Windows CNG API** (`bcrypt.dll`).  It delivers the same AES-256-CBC
encryption, HMAC, and PBKDF2 key-derivation as the OpenSSL provider, but
relies entirely on the operating system's built-in crypto — no third-party
libraries required.

---

## Requirements

| Requirement | Minimum version |
|-------------|----------------|
| Windows OS  | **Windows 8 / Windows Server 2012** (for `BCryptDeriveKeyPBKDF2`) |
| Compiler    | MSVC (any modern version) or MinGW/GCC |
| SDK         | Windows SDK 8.0+ when building in static-link mode |

> **Note:** The provider activates gracefully on Windows 7 in static-link
> mode at compile time, but calling any KDF operation will fail at runtime
> because `BCryptDeriveKeyPBKDF2` is not available on that OS version.  In
> dynamic-load mode the provider refuses to activate on Windows 7 and
> SQLCipher falls back cleanly with an error.

---

## Two Linking Modes

### Static link (default)

The Windows SDK import library `bcrypt.lib` is linked at build time.
MSVC picks it up automatically via `#pragma comment(lib, "bcrypt.lib")`.

```bat
# MSVC — nmake
nmake /f Makefile.msc CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_CNG"

# MinGW — add -lbcrypt explicitly
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_CNG" \
  LDFLAGS="-lbcrypt"
make
```

### Dynamic load

`bcrypt.dll` is loaded at runtime via `LoadLibraryW`.
No import library is needed at link time.  Useful when distributing a
single binary that also runs on platforms where `bcrypt.lib` may not be
present in the SDK.

```bat
# MSVC — nmake
nmake /f Makefile.msc CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_CNG -DSQLCIPHER_CRYPTO_CNG_DYNAMIC"

# MinGW
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_CNG -DSQLCIPHER_CRYPTO_CNG_DYNAMIC"
make
```

---

## Provider Capabilities

| Property | Value |
|----------|-------|
| `PRAGMA cipher_provider`         | `cng`          |
| `PRAGMA cipher_provider_version` | `Windows CNG`  |
| `PRAGMA cipher`                  | `AES-256-CBC`  |
| Key size                         | 32 bytes       |
| IV size                          | 16 bytes       |
| Block size                       | 16 bytes       |
| HMAC algorithms                  | SHA1, SHA256, SHA512 |
| KDF algorithms (PBKDF2)          | SHA1, SHA256, SHA512 |
| FIPS status                      | 0 (reported)   |

---

## How It Works

The provider implements all 17 function pointers of the `sqlcipher_provider`
interface (`src/sqlcipher.h`) using the following CNG primitives:

| SQLCipher function | CNG API |
|--------------------|---------|
| `random`           | `BCryptGenRandom(NULL, …, BCRYPT_USE_SYSTEM_PREFERRED_RNG)` |
| `add_random`       | no-op — CNG manages its own entropy pool |
| `hmac`             | `BCryptOpenAlgorithmProvider` + `BCRYPT_ALG_HANDLE_HMAC_FLAG`, `BCryptCreateHash`, `BCryptHashData`, `BCryptFinishHash` |
| `kdf`              | `BCryptDeriveKeyPBKDF2` |
| `cipher`           | `BCryptOpenAlgorithmProvider`(AES) + `BCryptSetProperty`(CBC) + `BCryptGenerateSymmetricKey` + `BCryptEncrypt` / `BCryptDecrypt` |

**IV copy:** `BCryptEncrypt` and `BCryptDecrypt` modify the IV buffer
in-place.  The provider copies the IV before each call so that SQLCipher's
original IV is never changed.

---

## CMake Integration (example)

```cmake
if(WIN32)
  target_compile_definitions(sqlcipher PRIVATE
    SQLITE_HAS_CODEC
    SQLCIPHER_CRYPTO_CNG          # or add SQLCIPHER_CRYPTO_CNG_DYNAMIC for runtime load
  )
  # Static link — bcrypt.lib is added automatically by #pragma comment(lib) on MSVC.
  # MinGW requires an explicit link:
  if(MINGW)
    target_link_libraries(sqlcipher PRIVATE bcrypt)
  endif()
endif()
```

---

## Running the Tests

The test suite is guarded: all CNG-specific tests are skipped automatically
when a different provider is active.

```bat
# Build with CNG and run the test suite
nmake /f Makefile.msc CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_CNG"
tclsh test\sqlcipher-cng-provider.test
```

Test coverage includes:
- Provider name and version PRAGMAs
- Encrypt/decrypt round-trip (passphrase and raw hex key)
- Wrong-key rejection
- Rekey operation
- All HMAC algorithm variants (SHA1, SHA256, SHA512)
- All PBKDF2 KDF algorithm variants
- Cipher property PRAGMAs (cipher name, key size, IV size)

---

## Source Files

| File | Role |
|------|------|
| `src/crypto_cng.c`                     | Provider implementation |
| `src/sqlcipher.c` (lines 86–91, 517–)  | Default-provider guard + dispatch |
| `Makefile.msc` (SRC00 list)            | MSVC build inclusion |
| `test/sqlcipher-cng-provider.test`     | Tcl test suite |
