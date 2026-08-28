#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *HCERTSTORE_W2K;
typedef struct _CERT_CONTEXT_W2K {
    DWORD dwCertEncodingType;
    BYTE *pbCertEncoded;
    DWORD cbCertEncoded;
    void *pCertInfo;
    HCERTSTORE_W2K hCertStore;
} CERT_CONTEXT_W2K, *PCERT_CONTEXT_W2K;
typedef const CERT_CONTEXT_W2K *PCCERT_CONTEXT_W2K;

typedef HCERTSTORE_W2K (WINAPI *PFN_CERT_OPEN_STORE)(
    LPCSTR, DWORD, ULONG_PTR, DWORD, const void *);
typedef BOOL (WINAPI *PFN_CERT_ADD_ENCODED)(
    HCERTSTORE_W2K, DWORD, const BYTE *, DWORD, DWORD,
    PCCERT_CONTEXT_W2K *);
typedef PCCERT_CONTEXT_W2K (WINAPI *PFN_CERT_ENUM)(
    HCERTSTORE_W2K, PCCERT_CONTEXT_W2K);
typedef BOOL (WINAPI *PFN_CERT_CLOSE_STORE)(HCERTSTORE_W2K, DWORD);

typedef struct _CRYPTO_API {
    HMODULE module;
    PFN_CERT_OPEN_STORE open_store;
    PFN_CERT_ADD_ENCODED add_encoded;
    PFN_CERT_ENUM enum_certificates;
    PFN_CERT_CLOSE_STORE close_store;
} CRYPTO_API;

#define W2K_X509_ASN_ENCODING                 0x00000001UL
#define W2K_CERT_STORE_PROV_SYSTEM_A          ((LPCSTR)9)
#define W2K_CERT_SYSTEM_STORE_LOCAL_MACHINE   0x00020000UL
#define W2K_CERT_STORE_OPEN_EXISTING_FLAG     0x00004000UL
#define W2K_CERT_STORE_ADD_REPLACE_EXISTING   3UL

static void usage(const char *program)
{
    fprintf(stderr,
            "Windows 2000 Modern Root Installer 1.0\n"
            "Usage:\n"
            "  %s count\n"
            "  %s verify CERT_DIRECTORY\n"
            "  %s install CERT_DIRECTORY\n",
            program, program, program);
}

static int load_crypto(CRYPTO_API *api)
{
    memset(api, 0, sizeof(*api));
    api->module = LoadLibraryA("crypt32.dll");
    if (!api->module) {
        fprintf(stderr, "ERROR load crypt32.dll win32=%lu\n",
                (unsigned long)GetLastError());
        return 0;
    }
    api->open_store = (PFN_CERT_OPEN_STORE)GetProcAddress(
        api->module, "CertOpenStore");
    api->add_encoded = (PFN_CERT_ADD_ENCODED)GetProcAddress(
        api->module, "CertAddEncodedCertificateToStore");
    api->enum_certificates = (PFN_CERT_ENUM)GetProcAddress(
        api->module, "CertEnumCertificatesInStore");
    api->close_store = (PFN_CERT_CLOSE_STORE)GetProcAddress(
        api->module, "CertCloseStore");
    if (!api->open_store || !api->add_encoded ||
        !api->enum_certificates || !api->close_store) {
        fprintf(stderr, "ERROR required crypt32 export missing win32=%lu\n",
                (unsigned long)GetLastError());
        FreeLibrary(api->module);
        memset(api, 0, sizeof(*api));
        return 0;
    }
    return 1;
}

static HCERTSTORE_W2K open_machine_root_store(CRYPTO_API *api)
{
    HCERTSTORE_W2K store = api->open_store(
        W2K_CERT_STORE_PROV_SYSTEM_A,
        0,
        0,
        W2K_CERT_SYSTEM_STORE_LOCAL_MACHINE |
            W2K_CERT_STORE_OPEN_EXISTING_FLAG,
        "ROOT");
    if (!store) {
        fprintf(stderr, "ERROR open LocalMachine\\ROOT win32=%lu\n",
                (unsigned long)GetLastError());
    }
    return store;
}

static unsigned long count_store(CRYPTO_API *api, HCERTSTORE_W2K store)
{
    unsigned long count = 0;
    PCCERT_CONTEXT_W2K current = NULL;
    while ((current = api->enum_certificates(store, current)) != NULL) {
        ++count;
    }
    return count;
}

static int read_file(const char *path, BYTE **bytes_out, DWORD *size_out)
{
    HANDLE file;
    DWORD size;
    DWORD got;
    BYTE *bytes;

    *bytes_out = NULL;
    *size_out = 0;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR open file=%s win32=%lu\n", path,
                (unsigned long)GetLastError());
        return 0;
    }
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 1024UL * 1024UL) {
        fprintf(stderr, "ERROR invalid certificate size file=%s size=%lu\n",
                path, (unsigned long)size);
        CloseHandle(file);
        return 0;
    }
    bytes = (BYTE *)malloc(size);
    if (!bytes) {
        fprintf(stderr, "ERROR out of memory file=%s size=%lu\n", path,
                (unsigned long)size);
        CloseHandle(file);
        return 0;
    }
    if (!ReadFile(file, bytes, size, &got, NULL) || got != size) {
        fprintf(stderr, "ERROR read file=%s win32=%lu got=%lu expected=%lu\n",
                path, (unsigned long)GetLastError(), (unsigned long)got,
                (unsigned long)size);
        free(bytes);
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    *bytes_out = bytes;
    *size_out = size;
    return 1;
}

static int store_contains(CRYPTO_API *api, HCERTSTORE_W2K store,
                          const BYTE *bytes, DWORD size)
{
    int found = 0;
    PCCERT_CONTEXT_W2K current = NULL;
    while ((current = api->enum_certificates(store, current)) != NULL) {
        if (current->cbCertEncoded == size &&
            memcmp(current->pbCertEncoded, bytes, size) == 0) {
            found = 1;
        }
    }
    return found;
}

static int build_pattern(const char *directory, char *pattern, DWORD capacity)
{
    size_t length = strlen(directory);
    if (length + 7 >= capacity) {
        fprintf(stderr, "ERROR certificate directory path is too long\n");
        return 0;
    }
    strcpy(pattern, directory);
    if (length != 0 && directory[length - 1] != '\\' &&
        directory[length - 1] != '/') {
        strcat(pattern, "\\");
    }
    strcat(pattern, "*.cer");
    return 1;
}

static int build_file_path(const char *directory, const char *name,
                           char *path, DWORD capacity)
{
    size_t length = strlen(directory);
    if (length + strlen(name) + 2 >= capacity) {
        return 0;
    }
    strcpy(path, directory);
    if (length != 0 && directory[length - 1] != '\\' &&
        directory[length - 1] != '/') {
        strcat(path, "\\");
    }
    strcat(path, name);
    return 1;
}

static int process_directory(CRYPTO_API *api, HCERTSTORE_W2K store,
                             const char *directory, int install)
{
    char pattern[MAX_PATH];
    char path[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    unsigned long files = 0;
    unsigned long added = 0;
    unsigned long present = 0;
    unsigned long failed = 0;

    if (!build_pattern(directory, pattern, sizeof(pattern))) {
        return 2;
    }
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR no .cer files found pattern=%s win32=%lu\n",
                pattern, (unsigned long)GetLastError());
        return 2;
    }
    do {
        BYTE *bytes;
        DWORD size;
        int is_present;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        ++files;
        if (!build_file_path(directory, found.cFileName, path, sizeof(path)) ||
            !read_file(path, &bytes, &size)) {
            ++failed;
            continue;
        }
        if (install) {
            if (api->add_encoded(store, W2K_X509_ASN_ENCODING, bytes, size,
                                 W2K_CERT_STORE_ADD_REPLACE_EXISTING, NULL)) {
                ++added;
                printf("ADD OK file=%s bytes=%lu\n", found.cFileName,
                       (unsigned long)size);
            } else {
                ++failed;
                fprintf(stderr, "ADD FAILED file=%s win32=%lu\n",
                        found.cFileName, (unsigned long)GetLastError());
            }
        }
        is_present = store_contains(api, store, bytes, size);
        if (is_present) {
            ++present;
            printf("VERIFY PRESENT file=%s\n", found.cFileName);
        } else {
            ++failed;
            fprintf(stderr, "VERIFY MISSING file=%s\n", found.cFileName);
        }
        free(bytes);
    } while (FindNextFileA(search, &found));
    FindClose(search);

    printf("SUMMARY mode=%s files=%lu added=%lu present=%lu failed=%lu "
           "store_count=%lu\n",
           install ? "install" : "verify", files, added, present, failed,
           count_store(api, store));
    return files != 0 && failed == 0 && present == files ? 0 : 1;
}

int main(int argc, char **argv)
{
    CRYPTO_API api;
    HCERTSTORE_W2K store;
    int result;

    if (argc < 2 ||
        (strcmp(argv[1], "count") != 0 && argc != 3)) {
        usage(argv[0]);
        return 2;
    }
    if (!load_crypto(&api)) {
        return 3;
    }
    store = open_machine_root_store(&api);
    if (!store) {
        FreeLibrary(api.module);
        return 4;
    }
    if (strcmp(argv[1], "count") == 0) {
        printf("STORE LocalMachine\\ROOT count=%lu\n", count_store(&api, store));
        result = 0;
    } else if (strcmp(argv[1], "verify") == 0) {
        result = process_directory(&api, store, argv[2], 0);
    } else if (strcmp(argv[1], "install") == 0) {
        result = process_directory(&api, store, argv[2], 1);
    } else {
        usage(argv[0]);
        result = 2;
    }
    if (!api.close_store(store, 0)) {
        fprintf(stderr, "WARNING close store failed win32=%lu\n",
                (unsigned long)GetLastError());
    }
    FreeLibrary(api.module);
    return result;
}
