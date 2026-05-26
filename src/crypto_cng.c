/*
** SQLCipher
** http://sqlcipher.net
**
** Copyright (c) 2008 - 2013, ZETETIC LLC
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the ZETETIC LLC nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY ZETETIC LLC ''AS IS'' AND ANY
** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL ZETETIC LLC BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*/
/* BEGIN SQLCIPHER */
#ifdef SQLITE_HAS_CODEC
#ifdef SQLCIPHER_CRYPTO_CNG

/*
** Windows CNG (Cryptography Next Generation) provider for SQLCipher.
**
** Two compilation modes are supported:
**
**   Static link (default):
**     Compile with -DSQLCIPHER_CRYPTO_CNG.
**     bcrypt.lib is linked automatically on MSVC via #pragma comment(lib).
**     On MinGW/GCC add -lbcrypt to your linker flags.
**     Requires Windows 8 / Windows Server 2012 or later at runtime
**     (BCryptDeriveKeyPBKDF2).
**
**   Dynamic load:
**     Compile with -DSQLCIPHER_CRYPTO_CNG -DSQLCIPHER_CRYPTO_CNG_DYNAMIC.
**     bcrypt.dll is loaded at runtime via LoadLibraryW; no import library
**     is required at link time.  The provider fails to activate gracefully
**     on systems where bcrypt.dll does not export BCryptDeriveKeyPBKDF2
**     (i.e., Windows Vista / 7).
*/

#include "sqliteInt.h"
#include "sqlcipher.h"
#include <windows.h>
#include <string.h>

/* Ensure NTSTATUS and NT_SUCCESS are available.  They are provided by
   <ntdef.h> in some SDK configurations; define safe fallbacks here. */
#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

/* AES-256-CBC fixed constants */
#define CNG_AES_KEY_SZ    32  /* 256-bit key = 32 bytes  */
#define CNG_AES_IV_SZ     16  /* AES block size = IV size */
#define CNG_AES_BLOCK_SZ  16  /* AES block size            */

/* HMAC output sizes */
#define CNG_HMAC_SHA1_SZ    20
#define CNG_HMAC_SHA256_SZ  32
#define CNG_HMAC_SHA512_SZ  64

/* =========================================================================
** Mode-specific definitions
** =========================================================================
**
** Dynamic mode  — bcrypt.dll loaded at runtime; bcrypt.h NOT included.
** Static mode   — bcrypt.h included; unified pfn_* macros alias real names.
*/

#ifdef SQLCIPHER_CRYPTO_CNG_DYNAMIC

/* Handle typedefs (mirrors bcrypt.h; PVOID because bcrypt.h is not included) */
typedef PVOID BCRYPT_ALG_HANDLE;
typedef PVOID BCRYPT_HASH_HANDLE;
typedef PVOID BCRYPT_KEY_HANDLE;
typedef PVOID BCRYPT_HANDLE;

/* Algorithm identifiers */
#define BCRYPT_SHA1_ALGORITHM    L"SHA1"
#define BCRYPT_SHA256_ALGORITHM  L"SHA256"
#define BCRYPT_SHA512_ALGORITHM  L"SHA512"
#define BCRYPT_AES_ALGORITHM     L"AES"

/* Property names */
#define BCRYPT_CHAINING_MODE     L"ChainingMode"
#define BCRYPT_CHAIN_MODE_CBC    L"ChainingModeCBC"
#define BCRYPT_OBJECT_LENGTH     L"ObjectLength"

/* Flag constants */
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG  0x00000002UL
#define BCRYPT_ALG_HANDLE_HMAC_FLAG      0x00000008UL

/* Function pointer typedefs */
typedef NTSTATUS (WINAPI *pfnBCryptOpenAlgorithmProvider_t)(BCRYPT_ALG_HANDLE *, LPCWSTR, LPCWSTR, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptCloseAlgorithmProvider_t)(BCRYPT_ALG_HANDLE, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptGenRandom_t)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptCreateHash_t)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptHashData_t)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptFinishHash_t)(BCRYPT_HASH_HANDLE, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptDestroyHash_t)(BCRYPT_HASH_HANDLE);
typedef NTSTATUS (WINAPI *pfnBCryptGetProperty_t)(BCRYPT_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG *, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptGenerateSymmetricKey_t)(BCRYPT_ALG_HANDLE, BCRYPT_KEY_HANDLE *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptEncrypt_t)(BCRYPT_KEY_HANDLE, PUCHAR, ULONG, VOID *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptDecrypt_t)(BCRYPT_KEY_HANDLE, PUCHAR, ULONG, VOID *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG *, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptDestroyKey_t)(BCRYPT_KEY_HANDLE);
typedef NTSTATUS (WINAPI *pfnBCryptSetProperty_t)(BCRYPT_HANDLE, LPCWSTR, PUCHAR, ULONG, ULONG);
typedef NTSTATUS (WINAPI *pfnBCryptDeriveKeyPBKDF2_t)(BCRYPT_ALG_HANDLE, PUCHAR, ULONG, PUCHAR, ULONG, ULONGLONG, PUCHAR, ULONG, ULONG);

/* Global DLL handle and function pointers (zero-initialised by C runtime) */
static HMODULE                            cng_bcrypt_dll                   = NULL;
static pfnBCryptOpenAlgorithmProvider_t   pfn_BCryptOpenAlgorithmProvider  = NULL;
static pfnBCryptCloseAlgorithmProvider_t  pfn_BCryptCloseAlgorithmProvider = NULL;
static pfnBCryptGenRandom_t               pfn_BCryptGenRandom              = NULL;
static pfnBCryptCreateHash_t              pfn_BCryptCreateHash             = NULL;
static pfnBCryptHashData_t                pfn_BCryptHashData               = NULL;
static pfnBCryptFinishHash_t              pfn_BCryptFinishHash             = NULL;
static pfnBCryptDestroyHash_t             pfn_BCryptDestroyHash            = NULL;
static pfnBCryptGetProperty_t             pfn_BCryptGetProperty            = NULL;
static pfnBCryptGenerateSymmetricKey_t    pfn_BCryptGenerateSymmetricKey   = NULL;
static pfnBCryptEncrypt_t                 pfn_BCryptEncrypt                = NULL;
static pfnBCryptDecrypt_t                 pfn_BCryptDecrypt                = NULL;
static pfnBCryptDestroyKey_t              pfn_BCryptDestroyKey             = NULL;
static pfnBCryptSetProperty_t             pfn_BCryptSetProperty            = NULL;
static pfnBCryptDeriveKeyPBKDF2_t         pfn_BCryptDeriveKeyPBKDF2        = NULL;

#else /* ---- static link ---- */

#include <bcrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

/* Unified pfn_* aliases so all implementation code compiles identically
   in both static and dynamic modes. */
#define pfn_BCryptOpenAlgorithmProvider   BCryptOpenAlgorithmProvider
#define pfn_BCryptCloseAlgorithmProvider  BCryptCloseAlgorithmProvider
#define pfn_BCryptGenRandom               BCryptGenRandom
#define pfn_BCryptCreateHash              BCryptCreateHash
#define pfn_BCryptHashData                BCryptHashData
#define pfn_BCryptFinishHash              BCryptFinishHash
#define pfn_BCryptDestroyHash             BCryptDestroyHash
#define pfn_BCryptGetProperty             BCryptGetProperty
#define pfn_BCryptGenerateSymmetricKey    BCryptGenerateSymmetricKey
#define pfn_BCryptEncrypt                 BCryptEncrypt
#define pfn_BCryptDecrypt                 BCryptDecrypt
#define pfn_BCryptDestroyKey              BCryptDestroyKey
#define pfn_BCryptSetProperty             BCryptSetProperty
#define pfn_BCryptDeriveKeyPBKDF2         BCryptDeriveKeyPBKDF2

#endif /* SQLCIPHER_CRYPTO_CNG_DYNAMIC */

/* Reference count — protected by SQLCIPHER_MUTEX_PROVIDER_ACTIVATE */
static unsigned int cng_init_count = 0;

/* =========================================================================
** Activate / Deactivate
** =========================================================================
*/

static int sqlcipher_cng_activate(void *ctx) {
  int rc = SQLITE_OK;

  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_activate: entering SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");
  sqlite3_mutex_enter(sqlcipher_mutex(SQLCIPHER_MUTEX_PROVIDER_ACTIVATE));
  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_activate: entered SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");

#ifdef SQLCIPHER_CRYPTO_CNG_DYNAMIC
  if(cng_init_count == 0) {
    sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_activate: loading bcrypt.dll dynamically");
    cng_bcrypt_dll = LoadLibraryW(L"bcrypt.dll");
    if(cng_bcrypt_dll == NULL) {
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_activate: LoadLibraryW(\"bcrypt.dll\") failed with error %lu",
        GetLastError());
      rc = SQLITE_ERROR;
      goto done;
    }

    pfn_BCryptOpenAlgorithmProvider  = (pfnBCryptOpenAlgorithmProvider_t) GetProcAddress(cng_bcrypt_dll, "BCryptOpenAlgorithmProvider");
    pfn_BCryptCloseAlgorithmProvider = (pfnBCryptCloseAlgorithmProvider_t)GetProcAddress(cng_bcrypt_dll, "BCryptCloseAlgorithmProvider");
    pfn_BCryptGenRandom              = (pfnBCryptGenRandom_t)             GetProcAddress(cng_bcrypt_dll, "BCryptGenRandom");
    pfn_BCryptCreateHash             = (pfnBCryptCreateHash_t)            GetProcAddress(cng_bcrypt_dll, "BCryptCreateHash");
    pfn_BCryptHashData               = (pfnBCryptHashData_t)              GetProcAddress(cng_bcrypt_dll, "BCryptHashData");
    pfn_BCryptFinishHash             = (pfnBCryptFinishHash_t)            GetProcAddress(cng_bcrypt_dll, "BCryptFinishHash");
    pfn_BCryptDestroyHash            = (pfnBCryptDestroyHash_t)           GetProcAddress(cng_bcrypt_dll, "BCryptDestroyHash");
    pfn_BCryptGetProperty            = (pfnBCryptGetProperty_t)           GetProcAddress(cng_bcrypt_dll, "BCryptGetProperty");
    pfn_BCryptGenerateSymmetricKey   = (pfnBCryptGenerateSymmetricKey_t)  GetProcAddress(cng_bcrypt_dll, "BCryptGenerateSymmetricKey");
    pfn_BCryptEncrypt                = (pfnBCryptEncrypt_t)               GetProcAddress(cng_bcrypt_dll, "BCryptEncrypt");
    pfn_BCryptDecrypt                = (pfnBCryptDecrypt_t)               GetProcAddress(cng_bcrypt_dll, "BCryptDecrypt");
    pfn_BCryptDestroyKey             = (pfnBCryptDestroyKey_t)            GetProcAddress(cng_bcrypt_dll, "BCryptDestroyKey");
    pfn_BCryptSetProperty            = (pfnBCryptSetProperty_t)           GetProcAddress(cng_bcrypt_dll, "BCryptSetProperty");
    pfn_BCryptDeriveKeyPBKDF2        = (pfnBCryptDeriveKeyPBKDF2_t)       GetProcAddress(cng_bcrypt_dll, "BCryptDeriveKeyPBKDF2");

    if( !pfn_BCryptOpenAlgorithmProvider  || !pfn_BCryptCloseAlgorithmProvider ||
        !pfn_BCryptGenRandom              || !pfn_BCryptCreateHash              ||
        !pfn_BCryptHashData               || !pfn_BCryptFinishHash              ||
        !pfn_BCryptDestroyHash            || !pfn_BCryptGetProperty             ||
        !pfn_BCryptGenerateSymmetricKey   || !pfn_BCryptEncrypt                 ||
        !pfn_BCryptDecrypt                || !pfn_BCryptDestroyKey              ||
        !pfn_BCryptSetProperty            || !pfn_BCryptDeriveKeyPBKDF2) {
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_activate: failed to resolve one or more BCrypt function "
        "pointers from bcrypt.dll (Windows 8+ required for BCryptDeriveKeyPBKDF2)");
      FreeLibrary(cng_bcrypt_dll);
      cng_bcrypt_dll = NULL;
      rc = SQLITE_ERROR;
      goto done;
    }
    sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_activate: bcrypt.dll loaded successfully, all 14 function pointers resolved");
  }
#endif /* SQLCIPHER_CRYPTO_CNG_DYNAMIC */

  cng_init_count++;
  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_activate: activated, init count is now %u", cng_init_count);

done:
  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_activate: leaving SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");
  sqlite3_mutex_leave(sqlcipher_mutex(SQLCIPHER_MUTEX_PROVIDER_ACTIVATE));
  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_activate: left SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");
  return rc;
}

static int sqlcipher_cng_deactivate(void *ctx) {
  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_deactivate: entering SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");
  sqlite3_mutex_enter(sqlcipher_mutex(SQLCIPHER_MUTEX_PROVIDER_ACTIVATE));
  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_deactivate: entered SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");

  cng_init_count--;
  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_deactivate: deactivated, init count is now %u", cng_init_count);

#ifdef SQLCIPHER_CRYPTO_CNG_DYNAMIC
  if(cng_init_count == 0 && cng_bcrypt_dll != NULL) {
    sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_deactivate: unloading bcrypt.dll");
    FreeLibrary(cng_bcrypt_dll);
    cng_bcrypt_dll                   = NULL;
    pfn_BCryptOpenAlgorithmProvider  = NULL;
    pfn_BCryptCloseAlgorithmProvider = NULL;
    pfn_BCryptGenRandom              = NULL;
    pfn_BCryptCreateHash             = NULL;
    pfn_BCryptHashData               = NULL;
    pfn_BCryptFinishHash             = NULL;
    pfn_BCryptDestroyHash            = NULL;
    pfn_BCryptGetProperty            = NULL;
    pfn_BCryptGenerateSymmetricKey   = NULL;
    pfn_BCryptEncrypt                = NULL;
    pfn_BCryptDecrypt                = NULL;
    pfn_BCryptDestroyKey             = NULL;
    pfn_BCryptSetProperty            = NULL;
    pfn_BCryptDeriveKeyPBKDF2        = NULL;
  }
#endif /* SQLCIPHER_CRYPTO_CNG_DYNAMIC */

  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_deactivate: leaving SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");
  sqlite3_mutex_leave(sqlcipher_mutex(SQLCIPHER_MUTEX_PROVIDER_ACTIVATE));
  sqlcipher_log(SQLCIPHER_LOG_TRACE, SQLCIPHER_LOG_MUTEX,
    "sqlcipher_cng_deactivate: left SQLCIPHER_MUTEX_PROVIDER_ACTIVATE");
  return SQLITE_OK;
}

/* =========================================================================
** Provider name and version
** =========================================================================
*/

static const char* sqlcipher_cng_get_provider_name(void *ctx) {
  return "cng";
}

static const char* sqlcipher_cng_get_provider_version(void *ctx) {
  return "Windows CNG";
}

/* =========================================================================
** Random number generation
** =========================================================================
*/

/* Windows CNG manages its own entropy pool; external seeding is a no-op. */
static int sqlcipher_cng_add_random(void *ctx, const void *buffer, int length) {
  return SQLITE_OK;
}

static int sqlcipher_cng_random(void *ctx, void *buffer, int length) {
  NTSTATUS status;
  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_random: requesting %d random bytes via BCryptGenRandom", length);
  status = pfn_BCryptGenRandom(NULL, (PUCHAR)buffer, (ULONG)length,
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_random: BCryptGenRandom for %d bytes returned 0x%08lx",
      length, (unsigned long)status);
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

/* =========================================================================
** HMAC
** =========================================================================
*/

static int sqlcipher_cng_hmac(
  void *ctx, int algorithm,
  const unsigned char *hmac_key, int key_sz,
  const unsigned char *in,  int in_sz,
  const unsigned char *in2, int in2_sz,
  unsigned char *out
) {
  NTSTATUS status;
  BCRYPT_ALG_HANDLE  hAlg        = NULL;
  BCRYPT_HASH_HANDLE hHash       = NULL;
  PUCHAR             pbHashObj   = NULL;
  ULONG              cbHashObj   = 0, cbResult = 0;
  LPCWSTR            alg_id;
  int                hmac_sz, rc = SQLITE_OK;

  if(in == NULL) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: input buffer is NULL");
    return SQLITE_ERROR;
  }

  switch(algorithm) {
    case SQLCIPHER_HMAC_SHA1:
      alg_id  = BCRYPT_SHA1_ALGORITHM;
      hmac_sz = CNG_HMAC_SHA1_SZ;
      break;
    case SQLCIPHER_HMAC_SHA256:
      alg_id  = BCRYPT_SHA256_ALGORITHM;
      hmac_sz = CNG_HMAC_SHA256_SZ;
      break;
    case SQLCIPHER_HMAC_SHA512:
      alg_id  = BCRYPT_SHA512_ALGORITHM;
      hmac_sz = CNG_HMAC_SHA512_SZ;
      break;
    default:
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_hmac: unsupported algorithm %d", algorithm);
      return SQLITE_ERROR;
  }

  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_hmac: algorithm %d, key_sz %d, in_sz %d, in2 %s",
    algorithm, key_sz, in_sz, (in2 != NULL) ? "present" : "absent");

  status = pfn_BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL,
                                           BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: BCryptOpenAlgorithmProvider for algorithm %d "
      "returned 0x%08lx", algorithm, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  status = pfn_BCryptGetProperty((BCRYPT_HANDLE)hAlg, BCRYPT_OBJECT_LENGTH,
                                 (PUCHAR)&cbHashObj, sizeof(ULONG), &cbResult, 0);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: BCryptGetProperty(BCRYPT_OBJECT_LENGTH) for algorithm %d "
      "returned 0x%08lx", algorithm, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  pbHashObj = (PUCHAR)sqlcipher_malloc(cbHashObj);
  if(pbHashObj == NULL) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: sqlcipher_malloc failed for hash object of %lu bytes",
      (unsigned long)cbHashObj);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  status = pfn_BCryptCreateHash(hAlg, &hHash, pbHashObj, cbHashObj,
                                (PUCHAR)hmac_key, (ULONG)key_sz, 0);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: BCryptCreateHash for algorithm %d, key_sz %d "
      "returned 0x%08lx", algorithm, key_sz, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  status = pfn_BCryptHashData(hHash, (PUCHAR)in, (ULONG)in_sz, 0);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: BCryptHashData on first buffer of %d bytes "
      "for algorithm %d returned 0x%08lx",
      in_sz, algorithm, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  if(in2 != NULL) {
    status = pfn_BCryptHashData(hHash, (PUCHAR)in2, (ULONG)in2_sz, 0);
    if(!NT_SUCCESS(status)) {
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_hmac: BCryptHashData on second buffer of %d bytes "
        "for algorithm %d returned 0x%08lx",
        in2_sz, algorithm, (unsigned long)status);
      rc = SQLITE_ERROR;
      goto cleanup;
    }
  }

  status = pfn_BCryptFinishHash(hHash, out, (ULONG)hmac_sz, 0);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_hmac: BCryptFinishHash for algorithm %d "
      "returned 0x%08lx", algorithm, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

cleanup:
  if(hHash)     pfn_BCryptDestroyHash(hHash);
  if(pbHashObj) sqlcipher_free(pbHashObj, cbHashObj);
  if(hAlg)      pfn_BCryptCloseAlgorithmProvider(hAlg, 0);
  return rc;
}

/* =========================================================================
** Key Derivation Function — PBKDF2
** Requires Windows 8 / Windows Server 2012 or later (BCryptDeriveKeyPBKDF2).
** =========================================================================
*/

static int sqlcipher_cng_kdf(
  void *ctx, int algorithm,
  const unsigned char *pass, int pass_sz,
  const unsigned char *salt, int salt_sz,
  int workfactor,
  int key_sz, unsigned char *key
) {
  NTSTATUS          status;
  BCRYPT_ALG_HANDLE hAlg   = NULL;
  LPCWSTR           alg_id;
  int               rc     = SQLITE_OK;

  switch(algorithm) {
    case SQLCIPHER_PBKDF2_HMAC_SHA1:   alg_id = BCRYPT_SHA1_ALGORITHM;   break;
    case SQLCIPHER_PBKDF2_HMAC_SHA256: alg_id = BCRYPT_SHA256_ALGORITHM; break;
    case SQLCIPHER_PBKDF2_HMAC_SHA512: alg_id = BCRYPT_SHA512_ALGORITHM; break;
    default:
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_kdf: unsupported algorithm %d", algorithm);
      return SQLITE_ERROR;
  }

  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_kdf: algorithm %d, pass_sz %d, salt_sz %d, "
    "workfactor %d, key_sz %d",
    algorithm, pass_sz, salt_sz, workfactor, key_sz);

  status = pfn_BCryptOpenAlgorithmProvider(&hAlg, alg_id, NULL,
                                           BCRYPT_ALG_HANDLE_HMAC_FLAG);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_kdf: BCryptOpenAlgorithmProvider for algorithm %d "
      "returned 0x%08lx", algorithm, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  status = pfn_BCryptDeriveKeyPBKDF2(
    hAlg,
    (PUCHAR)pass, (ULONG)pass_sz,
    (PUCHAR)salt, (ULONG)salt_sz,
    (ULONGLONG)workfactor,
    (PUCHAR)key, (ULONG)key_sz,
    0
  );
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_kdf: BCryptDeriveKeyPBKDF2 algorithm %d, "
      "workfactor %d, key_sz %d returned 0x%08lx",
      algorithm, workfactor, key_sz, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

cleanup:
  if(hAlg) pfn_BCryptCloseAlgorithmProvider(hAlg, 0);
  return rc;
}

/* =========================================================================
** Cipher — AES-256-CBC
** =========================================================================
*/

static int sqlcipher_cng_cipher(
  void *ctx, int mode,
  const unsigned char *key, int key_sz,
  const unsigned char *iv,
  const unsigned char *in, int in_sz,
  unsigned char *out
) {
  NTSTATUS          status;
  BCRYPT_ALG_HANDLE hAlg      = NULL;
  BCRYPT_KEY_HANDLE hKey      = NULL;
  PUCHAR            pbKeyObj  = NULL;
  ULONG             cbKeyObj  = 0, cbResult = 0, cbOutput = 0;
  /* BCryptEncrypt/BCryptDecrypt update the IV buffer in-place; copy it first */
  UCHAR             iv_copy[CNG_AES_IV_SZ];
  int               rc        = SQLITE_OK;

  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_cipher: mode %d, key_sz %d, in_sz %d", mode, key_sz, in_sz);

  status = pfn_BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_cipher: BCryptOpenAlgorithmProvider(AES) "
      "returned 0x%08lx", (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  /* Set CBC chaining mode before generating the key object */
  status = pfn_BCryptSetProperty(
    (BCRYPT_HANDLE)hAlg, BCRYPT_CHAINING_MODE,
    (PUCHAR)BCRYPT_CHAIN_MODE_CBC, (ULONG)sizeof(BCRYPT_CHAIN_MODE_CBC), 0
  );
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_cipher: BCryptSetProperty(BCRYPT_CHAINING_MODE=CBC) "
      "returned 0x%08lx", (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  status = pfn_BCryptGetProperty(
    (BCRYPT_HANDLE)hAlg, BCRYPT_OBJECT_LENGTH,
    (PUCHAR)&cbKeyObj, sizeof(ULONG), &cbResult, 0
  );
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_cipher: BCryptGetProperty(BCRYPT_OBJECT_LENGTH) for AES "
      "returned 0x%08lx", (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  pbKeyObj = (PUCHAR)sqlcipher_malloc(cbKeyObj);
  if(pbKeyObj == NULL) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_cipher: sqlcipher_malloc failed for key object of %lu bytes",
      (unsigned long)cbKeyObj);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  status = pfn_BCryptGenerateSymmetricKey(
    hAlg, &hKey, pbKeyObj, cbKeyObj,
    (PUCHAR)key, (ULONG)key_sz, 0
  );
  if(!NT_SUCCESS(status)) {
    sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
      "sqlcipher_cng_cipher: BCryptGenerateSymmetricKey key_sz %d "
      "returned 0x%08lx", key_sz, (unsigned long)status);
    rc = SQLITE_ERROR;
    goto cleanup;
  }

  memcpy(iv_copy, iv, CNG_AES_IV_SZ);

  if(mode == SQLCIPHER_ENCRYPT) {
    status = pfn_BCryptEncrypt(hKey, (PUCHAR)in, (ULONG)in_sz, NULL,
                               iv_copy, CNG_AES_IV_SZ,
                               out, (ULONG)in_sz, &cbOutput, 0);
    if(!NT_SUCCESS(status)) {
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_cipher: BCryptEncrypt for %d bytes "
        "returned 0x%08lx", in_sz, (unsigned long)status);
      rc = SQLITE_ERROR;
      goto cleanup;
    }
  } else {
    status = pfn_BCryptDecrypt(hKey, (PUCHAR)in, (ULONG)in_sz, NULL,
                               iv_copy, CNG_AES_IV_SZ,
                               out, (ULONG)in_sz, &cbOutput, 0);
    if(!NT_SUCCESS(status)) {
      sqlcipher_log(SQLCIPHER_LOG_ERROR, SQLCIPHER_LOG_PROVIDER,
        "sqlcipher_cng_cipher: BCryptDecrypt for %d bytes "
        "returned 0x%08lx", in_sz, (unsigned long)status);
      rc = SQLITE_ERROR;
      goto cleanup;
    }
  }

  sqlcipher_log(SQLCIPHER_LOG_DEBUG, SQLCIPHER_LOG_PROVIDER,
    "sqlcipher_cng_cipher: completed mode %d, in_sz %d, cbOutput %lu",
    mode, in_sz, (unsigned long)cbOutput);

cleanup:
  if(hKey)    pfn_BCryptDestroyKey(hKey);
  if(pbKeyObj) sqlcipher_free(pbKeyObj, cbKeyObj);
  if(hAlg)    pfn_BCryptCloseAlgorithmProvider(hAlg, 0);
  return rc;
}

/* =========================================================================
** Capability getters
** =========================================================================
*/

static const char* sqlcipher_cng_get_cipher(void *ctx) {
  return "AES-256-CBC";
}

static int sqlcipher_cng_get_key_sz(void *ctx) {
  return CNG_AES_KEY_SZ;
}

static int sqlcipher_cng_get_iv_sz(void *ctx) {
  return CNG_AES_IV_SZ;
}

static int sqlcipher_cng_get_block_sz(void *ctx) {
  return CNG_AES_BLOCK_SZ;
}

static int sqlcipher_cng_get_hmac_sz(void *ctx, int algorithm) {
  switch(algorithm) {
    case SQLCIPHER_HMAC_SHA1:   return CNG_HMAC_SHA1_SZ;
    case SQLCIPHER_HMAC_SHA256: return CNG_HMAC_SHA256_SZ;
    case SQLCIPHER_HMAC_SHA512: return CNG_HMAC_SHA512_SZ;
    default:                    return 0;
  }
}

/* =========================================================================
** Context init / free
** =========================================================================
*/

static int sqlcipher_cng_ctx_init(void **ctx) {
  return sqlcipher_cng_activate(*ctx);
}

static int sqlcipher_cng_ctx_free(void **ctx) {
  return sqlcipher_cng_deactivate(NULL);
}

/* =========================================================================
** FIPS status
** =========================================================================
*/

static int sqlcipher_cng_fips_status(void *ctx) {
  return 0;
}

/* =========================================================================
** Provider setup — entry point called from sqlcipher_extra_init()
** =========================================================================
*/

int sqlcipher_cng_setup(sqlcipher_provider *p) {
  p->init               = NULL;
  p->shutdown           = NULL;
  p->get_provider_name  = sqlcipher_cng_get_provider_name;
  p->random             = sqlcipher_cng_random;
  p->hmac               = sqlcipher_cng_hmac;
  p->kdf                = sqlcipher_cng_kdf;
  p->cipher             = sqlcipher_cng_cipher;
  p->get_cipher         = sqlcipher_cng_get_cipher;
  p->get_key_sz         = sqlcipher_cng_get_key_sz;
  p->get_iv_sz          = sqlcipher_cng_get_iv_sz;
  p->get_block_sz       = sqlcipher_cng_get_block_sz;
  p->get_hmac_sz        = sqlcipher_cng_get_hmac_sz;
  p->ctx_init           = sqlcipher_cng_ctx_init;
  p->ctx_free           = sqlcipher_cng_ctx_free;
  p->add_random         = sqlcipher_cng_add_random;
  p->fips_status        = sqlcipher_cng_fips_status;
  p->get_provider_version = sqlcipher_cng_get_provider_version;
  return SQLITE_OK;
}

#endif /* SQLCIPHER_CRYPTO_CNG */
#endif /* SQLITE_HAS_CODEC */
/* END SQLCIPHER */
