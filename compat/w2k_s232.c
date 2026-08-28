#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

/*
 * Windows 2000 Winsock bridge for Supermium.
 *
 * Supermium's stock p_s232.dll forwards APIs present in the platform's
 * ws2_32.dll, but its generated fallbacks for getaddrinfo/freeaddrinfo are
 * diagnostic stubs.  On x86 the getaddrinfo stub also returns with the wrong
 * stack discipline, which makes Chromium execute data on its DNS thread.
 *
 * This bridge forwards every Winsock entry statically imported by chrome.dll
 * and implements the missing IPv4 resolver APIs with Windows 2000 primitives.
 * A post-link tool rebuilds the export table using p_s232.dll's original
 * ordinals, so both name and ordinal imports remain ABI-compatible.
 */

#define AF_UNSPEC 0
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

#define AI_PASSIVE 0x00000001
#define AI_CANONNAME 0x00000002
#define AI_NUMERICHOST 0x00000004
#define AI_NUMERICSERV 0x00000008
#define AI_ALL 0x00000100
#define AI_ADDRCONFIG 0x00000400
#define AI_V4MAPPED 0x00000800

#ifndef WSAEFAULT
#define WSAEFAULT 10014
#endif
#ifndef WSAEINVAL
#define WSAEINVAL 10022
#endif
#ifndef WSAESOCKTNOSUPPORT
#define WSAESOCKTNOSUPPORT 10044
#endif
#ifndef WSAEAFNOSUPPORT
#define WSAEAFNOSUPPORT 10047
#endif
#ifndef WSAHOST_NOT_FOUND
#define WSAHOST_NOT_FOUND 11001
#endif
#ifndef WSATRY_AGAIN
#define WSATRY_AGAIN 11002
#endif
#ifndef WSANO_RECOVERY
#define WSANO_RECOVERY 11003
#endif
#ifndef WSATYPE_NOT_FOUND
#define WSATYPE_NOT_FOUND 10109
#endif
#ifndef WSA_NOT_ENOUGH_MEMORY
#define WSA_NOT_ENOUGH_MEMORY 8
#endif

typedef unsigned int W2K_SOCKET;

typedef struct W2K_SOCKADDR {
    unsigned short sa_family;
    char sa_data[14];
} W2K_SOCKADDR;

typedef struct W2K_IN_ADDR {
    unsigned long s_addr;
} W2K_IN_ADDR;

typedef struct W2K_SOCKADDR_IN {
    short sin_family;
    unsigned short sin_port;
    W2K_IN_ADDR sin_addr;
    char sin_zero[8];
} W2K_SOCKADDR_IN;

typedef struct W2K_HOSTENT {
    char *h_name;
    char **h_aliases;
    short h_addrtype;
    short h_length;
    char **h_addr_list;
} W2K_HOSTENT;

typedef struct W2K_SERVENT {
    char *s_name;
    char **s_aliases;
    short s_port;
    char *s_proto;
} W2K_SERVENT;

typedef struct W2K_ADDRINFO {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    unsigned int ai_addrlen;
    char *ai_canonname;
    W2K_SOCKADDR *ai_addr;
    struct W2K_ADDRINFO *ai_next;
} W2K_ADDRINFO;

typedef W2K_HOSTENT *(WINAPI *W2K_GETHOSTBYNAME)(const char *name);
typedef W2K_SERVENT *(WINAPI *W2K_GETSERVBYNAME)(
    const char *name, const char *protocol);
typedef unsigned short (WINAPI *W2K_HTONS)(unsigned short value);
typedef void (WINAPI *W2K_WSASETLASTERROR)(int error);
typedef int (WINAPI *W2K_WSAGETLASTERROR)(void);

static HMODULE g_system_ws2;
static W2K_GETHOSTBYNAME g_gethostbyname;
static W2K_GETSERVBYNAME g_getservbyname;
static W2K_HTONS g_htons;
static W2K_WSASETLASTERROR g_wsa_set_last_error;
static W2K_WSAGETLASTERROR g_wsa_get_last_error;

#define W2K_FORWARD_LIST(X) \
    X(WSACloseEvent) \
    X(WSACreateEvent) \
    X(WSADuplicateSocketW) \
    X(WSAEnumNameSpaceProvidersW) \
    X(WSAEnumNetworkEvents) \
    X(WSAEnumProtocolsW) \
    X(WSAEventSelect) \
    X(WSAGetLastError) \
    X(WSAGetOverlappedResult) \
    X(WSAIoctl) \
    X(WSALookupServiceBeginW) \
    X(WSALookupServiceEnd) \
    X(WSALookupServiceNextW) \
    X(WSARecv) \
    X(WSARecvFrom) \
    X(WSAResetEvent) \
    X(WSASend) \
    X(WSASendTo) \
    X(WSASetEvent) \
    X(WSASetServiceW) \
    X(WSASocketW) \
    X(WSAStartup) \
    X(WSACleanup) \
    X(WSAWaitForMultipleEvents) \
    X(WSCEnumProtocols) \
    X(WSCGetProviderPath) \
    X(accept) \
    X(bind) \
    X(closesocket) \
    X(connect) \
    X(gethostname) \
    X(getpeername) \
    X(getsockname) \
    X(getsockopt) \
    X(htonl) \
    X(htons) \
    X(ioctlsocket) \
    X(listen) \
    X(ntohl) \
    X(ntohs) \
    X(recv) \
    X(recvfrom) \
    X(send) \
    X(sendto) \
    X(setsockopt) \
    X(shutdown) \
    X(socket)

/*
 * TCC emits a conventional EBP prologue even for its naked attribute.  Undo
 * that prologue before tail-jumping so the real ws2_32 function sees the
 * original caller's stack and performs its own stdcall cleanup.
 */
#define DECLARE_FORWARD(name) \
    static FARPROC g_forward_##name; \
    __declspec(dllexport) __declspec(naked) void WINAPI name(void) { \
        __asm__("mov %ebp,%esp\n\tpop %ebp\n\tjmp *g_forward_" #name); \
    }

W2K_FORWARD_LIST(DECLARE_FORWARD)

static void set_wsa_error(int error)
{
    if (g_wsa_set_last_error) g_wsa_set_last_error(error);
    SetLastError((DWORD)error);
}

static int parse_decimal_port(const char *text, unsigned short *port)
{
    unsigned long value = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    if (!cursor || !*cursor) return 0;
    while (*cursor) {
        if (*cursor < '0' || *cursor > '9') return 0;
        value = value * 10 + (*cursor - '0');
        if (value > 65535UL) return 0;
        ++cursor;
    }
    *port = g_htons ? g_htons((unsigned short)value) : (unsigned short)value;
    return 1;
}

static int parse_ipv4(const char *text, unsigned long *address)
{
    unsigned long parts[4];
    unsigned long value;
    const unsigned char *cursor = (const unsigned char *)text;
    int index;
    unsigned char *bytes = (unsigned char *)&value;

    if (!cursor || !*cursor) return 0;
    for (index = 0; index < 4; ++index) {
        unsigned long part = 0;
        int digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            part = part * 10 + (*cursor - '0');
            if (part > 255UL) return 0;
            ++cursor;
            ++digits;
        }
        if (!digits) return 0;
        parts[index] = part;
        if (index != 3) {
            if (*cursor != '.') return 0;
            ++cursor;
        }
    }
    if (*cursor) return 0;
    bytes[0] = (unsigned char)parts[0];
    bytes[1] = (unsigned char)parts[1];
    bytes[2] = (unsigned char)parts[2];
    bytes[3] = (unsigned char)parts[3];
    *address = value;
    return 1;
}

static int resolve_service(
    const char *service,
    int protocol,
    int numeric_only,
    unsigned short *port)
{
    W2K_SERVENT *entry;
    const char *protocol_name;

    *port = 0;
    if (!service) return 1;
    if (parse_decimal_port(service, port)) return 1;
    if (numeric_only || !g_getservbyname) return 0;
    protocol_name = protocol == IPPROTO_UDP ? "udp" : "tcp";
    entry = g_getservbyname(service, protocol_name);
    if (!entry) return 0;
    *port = (unsigned short)entry->s_port;
    return 1;
}

static void free_addrinfo_chain(W2K_ADDRINFO *head)
{
    HANDLE heap = GetProcessHeap();
    while (head) {
        W2K_ADDRINFO *next = head->ai_next;
        HeapFree(heap, 0, head);
        head = next;
    }
}

static int append_result(
    W2K_ADDRINFO **head,
    W2K_ADDRINFO **tail,
    unsigned long address,
    unsigned short port,
    int socktype,
    int protocol,
    const char *canonical_name)
{
    SIZE_T canonical_bytes = canonical_name ? lstrlenA(canonical_name) + 1 : 0;
    SIZE_T bytes = sizeof(W2K_ADDRINFO) + sizeof(W2K_SOCKADDR_IN) +
        canonical_bytes;
    W2K_ADDRINFO *entry = (W2K_ADDRINFO *)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    W2K_SOCKADDR_IN *socket_address;

    if (!entry) return 0;
    socket_address = (W2K_SOCKADDR_IN *)(entry + 1);
    socket_address->sin_family = AF_INET;
    socket_address->sin_port = port;
    socket_address->sin_addr.s_addr = address;
    entry->ai_family = AF_INET;
    entry->ai_socktype = socktype;
    entry->ai_protocol = protocol;
    entry->ai_addrlen = sizeof(*socket_address);
    entry->ai_addr = (W2K_SOCKADDR *)socket_address;
    if (canonical_bytes) {
        entry->ai_canonname = (char *)(socket_address + 1);
        lstrcpyA(entry->ai_canonname, canonical_name);
    }
    if (*tail) (*tail)->ai_next = entry;
    else *head = entry;
    *tail = entry;
    return 1;
}

__declspec(dllexport) int WINAPI getaddrinfo(
    const char *node,
    const char *service,
    const W2K_ADDRINFO *hints,
    W2K_ADDRINFO **result)
{
    W2K_ADDRINFO *head = NULL;
    W2K_ADDRINFO *tail = NULL;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int requested_socktype = hints ? hints->ai_socktype : 0;
    int requested_protocol = hints ? hints->ai_protocol : 0;
    int flags = hints ? hints->ai_flags : 0;
    int allowed_flags = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
        AI_NUMERICSERV | AI_ALL | AI_ADDRCONFIG | AI_V4MAPPED;
    int socktypes[2];
    int protocols[2];
    unsigned short ports[2];
    int variant_count = 0;
    unsigned long numeric_address = 0;
    W2K_HOSTENT *host = NULL;
    int numeric_host = 0;
    int address_index;
    int variant;

    if (!result) {
        set_wsa_error(WSAEFAULT);
        return WSAEFAULT;
    }
    *result = NULL;
    if (!node && !service) {
        set_wsa_error(WSAHOST_NOT_FOUND);
        return WSAHOST_NOT_FOUND;
    }
    if (flags & ~allowed_flags) {
        set_wsa_error(WSAEINVAL);
        return WSAEINVAL;
    }
    if (family != AF_UNSPEC && family != AF_INET) {
        set_wsa_error(WSAEAFNOSUPPORT);
        return WSAEAFNOSUPPORT;
    }
    if (requested_socktype != 0 && requested_socktype != SOCK_STREAM &&
        requested_socktype != SOCK_DGRAM && requested_socktype != SOCK_RAW) {
        set_wsa_error(WSAESOCKTNOSUPPORT);
        return WSAESOCKTNOSUPPORT;
    }

    if (requested_socktype == 0) {
        if (requested_protocol == 0 || requested_protocol == IPPROTO_TCP) {
            socktypes[variant_count] = SOCK_STREAM;
            protocols[variant_count++] = IPPROTO_TCP;
        }
        if (requested_protocol == 0 || requested_protocol == IPPROTO_UDP) {
            socktypes[variant_count] = SOCK_DGRAM;
            protocols[variant_count++] = IPPROTO_UDP;
        }
    } else {
        socktypes[0] = requested_socktype;
        protocols[0] = requested_protocol;
        if (!protocols[0]) {
            if (requested_socktype == SOCK_STREAM) protocols[0] = IPPROTO_TCP;
            else if (requested_socktype == SOCK_DGRAM) protocols[0] = IPPROTO_UDP;
        }
        variant_count = 1;
    }
    if (!variant_count) {
        set_wsa_error(WSAESOCKTNOSUPPORT);
        return WSAESOCKTNOSUPPORT;
    }
    for (variant = 0; variant < variant_count; ++variant) {
        if (!resolve_service(service, protocols[variant],
                             (flags & AI_NUMERICSERV) != 0,
                             &ports[variant])) {
            set_wsa_error(WSATYPE_NOT_FOUND);
            return WSATYPE_NOT_FOUND;
        }
    }

    if (!node) {
        unsigned char *bytes = (unsigned char *)&numeric_address;
        bytes[0] = (flags & AI_PASSIVE) ? 0 : 127;
        bytes[1] = 0;
        bytes[2] = 0;
        bytes[3] = (flags & AI_PASSIVE) ? 0 : 1;
        numeric_host = 1;
    } else if (parse_ipv4(node, &numeric_address)) {
        numeric_host = 1;
    } else {
        if (flags & AI_NUMERICHOST) {
            set_wsa_error(WSAHOST_NOT_FOUND);
            return WSAHOST_NOT_FOUND;
        }
        if (!g_gethostbyname) {
            set_wsa_error(WSANO_RECOVERY);
            return WSANO_RECOVERY;
        }
        host = g_gethostbyname(node);
        if (!host) {
            int error = WSAHOST_NOT_FOUND;
            if (g_wsa_get_last_error) error = g_wsa_get_last_error();
            if (!error) error = WSAHOST_NOT_FOUND;
            set_wsa_error(error);
            return error;
        }
        if (host->h_addrtype != AF_INET || host->h_length != 4 ||
            !host->h_addr_list || !host->h_addr_list[0]) {
            set_wsa_error(WSAEAFNOSUPPORT);
            return WSAEAFNOSUPPORT;
        }
    }

    for (address_index = 0; ; ++address_index) {
        unsigned long address;
        const char *canonical = NULL;
        if (numeric_host) {
            if (address_index) break;
            address = numeric_address;
            if ((flags & AI_CANONNAME) && node) canonical = node;
        } else {
            if (!host->h_addr_list[address_index]) break;
            memcpy(&address, host->h_addr_list[address_index], 4);
            if ((flags & AI_CANONNAME) && address_index == 0)
                canonical = host->h_name ? host->h_name : node;
        }
        for (variant = 0; variant < variant_count; ++variant) {
            if (!append_result(&head, &tail, address, ports[variant],
                               socktypes[variant], protocols[variant],
                               canonical)) {
                free_addrinfo_chain(head);
                set_wsa_error(WSA_NOT_ENOUGH_MEMORY);
                return WSA_NOT_ENOUGH_MEMORY;
            }
            canonical = NULL;
        }
    }
    if (!head) {
        set_wsa_error(WSAHOST_NOT_FOUND);
        return WSAHOST_NOT_FOUND;
    }
    *result = head;
    set_wsa_error(0);
    return 0;
}

__declspec(dllexport) void WINAPI freeaddrinfo(W2K_ADDRINFO *result)
{
    free_addrinfo_chain(result);
}

static char *append_decimal_byte(char *output, unsigned int value)
{
    if (value >= 100) {
        *output++ = (char)('0' + value / 100);
        value %= 100;
        *output++ = (char)('0' + value / 10);
    } else if (value >= 10) {
        *output++ = (char)('0' + value / 10);
    }
    *output++ = (char)('0' + value % 10);
    return output;
}

__declspec(dllexport) const char *WINAPI inet_ntop(
    int family,
    const void *address,
    char *buffer,
    unsigned int buffer_bytes)
{
    const unsigned char *bytes = (const unsigned char *)address;
    char rendered[16];
    char *cursor = rendered;
    int index;

    if (family != AF_INET) {
        set_wsa_error(WSAEAFNOSUPPORT);
        return NULL;
    }
    if (!address || !buffer) {
        set_wsa_error(WSAEFAULT);
        return NULL;
    }
    for (index = 0; index < 4; ++index) {
        cursor = append_decimal_byte(cursor, bytes[index]);
        if (index != 3) *cursor++ = '.';
    }
    *cursor++ = 0;
    if (buffer_bytes < (unsigned int)(cursor - rendered)) {
        set_wsa_error(WSAEFAULT);
        return NULL;
    }
    lstrcpyA(buffer, rendered);
    set_wsa_error(0);
    return buffer;
}

#define RESOLVE_FORWARD(name) \
    g_forward_##name = GetProcAddress(g_system_ws2, #name);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    char system_path[MAX_PATH];
    UINT length;
    (void)instance;
    (void)reserved;

    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    length = GetSystemDirectoryA(system_path, sizeof(system_path));
    if (!length || length + sizeof("\\ws2_32.dll") > sizeof(system_path))
        return FALSE;
    lstrcatA(system_path, "\\ws2_32.dll");
    g_system_ws2 = LoadLibraryA(system_path);
    if (!g_system_ws2) return FALSE;
    W2K_FORWARD_LIST(RESOLVE_FORWARD)
    g_gethostbyname = (W2K_GETHOSTBYNAME)GetProcAddress(
        g_system_ws2, "gethostbyname");
    g_getservbyname = (W2K_GETSERVBYNAME)GetProcAddress(
        g_system_ws2, "getservbyname");
    g_htons = (W2K_HTONS)GetProcAddress(g_system_ws2, "htons");
    g_wsa_set_last_error = (W2K_WSASETLASTERROR)GetProcAddress(
        g_system_ws2, "WSASetLastError");
    g_wsa_get_last_error = (W2K_WSAGETLASTERROR)GetProcAddress(
        g_system_ws2, "WSAGetLastError");
    return g_gethostbyname && g_htons && g_wsa_set_last_error &&
        g_wsa_get_last_error;
}
