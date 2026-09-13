#include "Socket.h"
#include "../Core/Log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace woc
{
    namespace
    {
        bool s_started = false;

        sockaddr_in ToSockAddr(const NetAddress& address)
        {
            sockaddr_in out{};
            out.sin_family = AF_INET;
            out.sin_port = htons(address.port);
            out.sin_addr.s_addr = htonl(address.ipv4);
            return out;
        }

        NetAddress FromSockAddr(const sockaddr_in& in)
        {
            NetAddress out;
            out.ipv4 = ntohl(in.sin_addr.s_addr);
            out.port = ntohs(in.sin_port);
            return out;
        }

        void SetNonBlocking(SOCKET handle)
        {
            u_long mode = 1;
            ioctlsocket(handle, FIONBIO, &mode);
        }

        bool WouldBlock()
        {
            const int error = WSAGetLastError();
            return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
        }
    }

    std::string NetAddress::ToString() const
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u:%u",
                      (ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF, (ipv4 >> 8) & 0xFF, ipv4 & 0xFF,
                      static_cast<unsigned>(port));
        return buffer;
    }

    NetAddress NetAddress::Parse(const std::string& text, u16 port)
    {
        NetAddress out;
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (std::sscanf(text.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return out;
        out.ipv4 = (a << 24) | (b << 16) | (c << 8) | d;
        out.port = port;
        return out;
    }

    bool NetStartup::Ensure()
    {
        if (s_started) return true;

        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
        {
            WOC_LOG_ERROR("WSAStartup failed: ", result);
            return false;
        }
        s_started = true;
        return true;
    }

    void NetStartup::Shutdown()
    {
        if (!s_started) return;
        WSACleanup();
        s_started = false;
    }

    // =========================================================================================
    // TCP
    // =========================================================================================

    TcpSocket::~TcpSocket() { Close(); }

    TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    {
        *this = std::move(other);
    }

    TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
    {
        if (this == &other) return *this;
        Close();
        m_handle = other.m_handle;
        m_peer = other.m_peer;
        m_outgoing = std::move(other.m_outgoing);
        m_connected = other.m_connected;
        m_broken = other.m_broken;
        other.m_handle = 0;
        other.m_connected = false;
        other.m_broken = false;
        return *this;
    }

    void TcpSocket::Adopt(u64 handle, const NetAddress& peer)
    {
        Close();
        m_handle = handle;
        m_peer = peer;
        m_connected = true;
        m_broken = false;
        SetNonBlocking(static_cast<SOCKET>(m_handle));
    }

    bool TcpSocket::Connect(const NetAddress& address)
    {
        if (!NetStartup::Ensure()) return false;
        Close();

        const SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (handle == INVALID_SOCKET) return false;

        SetNonBlocking(handle);

        // Nagle would hold a fifty-byte order back waiting for company. The traffic here is
        // small and occasional, and latency is the only thing about it that matters.
        int flag = 1;
        setsockopt(handle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

        const sockaddr_in target = ToSockAddr(address);
        const int result = connect(handle, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        if (result == SOCKET_ERROR && !WouldBlock())
        {
            closesocket(handle);
            return false;
        }

        m_handle = static_cast<u64>(handle);
        m_peer = address;
        m_connected = result == 0;
        m_broken = false;
        return true;
    }

    void TcpSocket::Poll()
    {
        if (m_handle == 0 || m_broken) return;

        if (!m_connected)
        {
            // A connecting socket becomes writable when it is through, and exceptional when
            // it has been refused. select with a zero timeout answers both in one call.
            fd_set writable, failed;
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            FD_SET(static_cast<SOCKET>(m_handle), &writable);
            FD_SET(static_cast<SOCKET>(m_handle), &failed);

            timeval immediate{ 0, 0 };
            if (select(0, nullptr, &writable, &failed, &immediate) > 0)
            {
                if (FD_ISSET(static_cast<SOCKET>(m_handle), &failed)) m_broken = true;
                else m_connected = true;
            }
        }

        if (m_connected) Flush();
    }

    void TcpSocket::Send(const void* data, size_t bytes)
    {
        if (m_handle == 0 || m_broken || bytes == 0) return;
        const u8* begin = static_cast<const u8*>(data);
        m_outgoing.insert(m_outgoing.end(), begin, begin + bytes);
        if (m_connected) Flush();
    }

    void TcpSocket::Flush()
    {
        if (m_handle == 0 || m_broken || m_outgoing.empty()) return;

        size_t sent = 0;
        while (sent < m_outgoing.size())
        {
            const int wrote = send(static_cast<SOCKET>(m_handle),
                                   reinterpret_cast<const char*>(m_outgoing.data() + sent),
                                   static_cast<int>(m_outgoing.size() - sent), 0);
            if (wrote == SOCKET_ERROR)
            {
                if (!WouldBlock()) m_broken = true;
                break;
            }
            sent += static_cast<size_t>(wrote);
        }

        if (sent > 0) m_outgoing.erase(m_outgoing.begin(), m_outgoing.begin() + static_cast<i64>(sent));
    }

    bool TcpSocket::Receive(std::vector<u8>& out)
    {
        if (m_handle == 0 || m_broken) return false;

        char buffer[16384];
        for (;;)
        {
            const int read = recv(static_cast<SOCKET>(m_handle), buffer, sizeof(buffer), 0);
            if (read > 0)
            {
                out.insert(out.end(), buffer, buffer + read);
                continue;
            }
            if (read == 0)
            {
                // An orderly close from the other end.
                m_broken = true;
                return false;
            }
            if (!WouldBlock())
            {
                m_broken = true;
                return false;
            }
            break;
        }
        return true;
    }

    void TcpSocket::Close()
    {
        if (m_handle != 0)
        {
            closesocket(static_cast<SOCKET>(m_handle));
            m_handle = 0;
        }
        m_outgoing.clear();
        m_connected = false;
    }

    // =========================================================================================
    // Listening
    // =========================================================================================

    TcpListener::~TcpListener() { Close(); }

    bool TcpListener::Listen(u16 port)
    {
        if (!NetStartup::Ensure()) return false;
        Close();

        const SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (handle == INVALID_SOCKET) return false;

        int reuse = 1;
        setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_port = htons(port);

        if (bind(handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR ||
            listen(handle, 8) == SOCKET_ERROR)
        {
            closesocket(handle);
            return false;
        }

        // Whatever port we actually ended up on is what the beacon has to advertise.
        sockaddr_in bound{};
        int length = sizeof(bound);
        if (getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length) == 0)
        {
            m_port = ntohs(bound.sin_port);
        }
        else
        {
            m_port = port;
        }

        SetNonBlocking(handle);
        m_handle = static_cast<u64>(handle);
        return true;
    }

    bool TcpListener::Accept(TcpSocket& out)
    {
        if (m_handle == 0) return false;

        sockaddr_in from{};
        int length = sizeof(from);
        const SOCKET accepted = accept(static_cast<SOCKET>(m_handle),
                                       reinterpret_cast<sockaddr*>(&from), &length);
        if (accepted == INVALID_SOCKET) return false;

        int flag = 1;
        setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));
        out.Adopt(static_cast<u64>(accepted), FromSockAddr(from));
        return true;
    }

    void TcpListener::Close()
    {
        if (m_handle == 0) return;
        closesocket(static_cast<SOCKET>(m_handle));
        m_handle = 0;
        m_port = 0;
    }

    // =========================================================================================
    // Datagrams
    // =========================================================================================

    UdpSocket::~UdpSocket() { Close(); }

    bool UdpSocket::Open(u16 port)
    {
        if (!NetStartup::Ensure()) return false;
        Close();

        const SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == INVALID_SOCKET) return false;

        int reuse = 1;
        setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        int broadcast = 1;
        setsockopt(handle, SOL_SOCKET, SO_BROADCAST,
                   reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_port = htons(port);
        if (bind(handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR)
        {
            closesocket(handle);
            return false;
        }

        SetNonBlocking(handle);
        m_handle = static_cast<u64>(handle);
        return true;
    }

    void UdpSocket::Close()
    {
        if (m_handle == 0) return;
        closesocket(static_cast<SOCKET>(m_handle));
        m_handle = 0;
    }

    void UdpSocket::SendTo(const NetAddress& address, const void* data, size_t bytes)
    {
        if (m_handle == 0) return;
        const sockaddr_in target = ToSockAddr(address);
        sendto(static_cast<SOCKET>(m_handle), static_cast<const char*>(data), static_cast<int>(bytes), 0,
               reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    }

    void UdpSocket::Broadcast(u16 port, const void* data, size_t bytes)
    {
        if (m_handle == 0) return;

        // The all-ones broadcast reaches the local segment; the loopback is sent to
        // separately so that two copies of the game on one machine can find each other,
        // which is how this is usually tried first.
        NetAddress target;
        target.ipv4 = 0xFFFFFFFFu;
        target.port = port;
        SendTo(target, data, bytes);

        // And to each network by name, so the shout goes out of every adapter and not only
        // the one the routing table happens to prefer.
        for (u32 address : BroadcastAddresses())
        {
            target.ipv4 = address;
            SendTo(target, data, bytes);
        }

        target.ipv4 = 0x7F000001u;
        SendTo(target, data, bytes);
    }

    bool UdpSocket::ReceiveFrom(std::vector<u8>& out, NetAddress& from)
    {
        if (m_handle == 0) return false;

        char buffer[2048];
        sockaddr_in sender{};
        int length = sizeof(sender);
        const int read = recvfrom(static_cast<SOCKET>(m_handle), buffer, sizeof(buffer), 0,
                                  reinterpret_cast<sockaddr*>(&sender), &length);
        if (read <= 0) return false;

        out.assign(buffer, buffer + read);
        from = FromSockAddr(sender);
        return true;
    }

    std::vector<u32> BroadcastAddresses()
    {
        static std::vector<u32> cached;
        static ULONGLONG cachedAt = 0;
        // Adapters come and go (a laptop joins the Wi-Fi), but not every second.
        const ULONGLONG now = GetTickCount64();
        if (cachedAt != 0 && now - cachedAt < 10000) return cached;
        cachedAt = now;
        cached.clear();

        ULONG size = 16 * 1024;
        std::vector<u8> buffer(size);
        const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(size);
            result = GetAdaptersAddresses(AF_INET, flags, nullptr,
                                          reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
        }
        if (result != NO_ERROR) return cached;

        for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp) continue;
            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
            {
                if (unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
                const auto* in = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
                const u32 ip = ntohl(in->sin_addr.s_addr);
                const u32 prefix = unicast->OnLinkPrefixLength;
                if (prefix == 0 || prefix >= 32) continue;
                if ((ip >> 16) == 0xA9FE) continue;             // 169.254: no network at all

                const u32 mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
                const u32 broadcast = ip | ~mask;
                if (std::find(cached.begin(), cached.end(), broadcast) == cached.end())
                {
                    cached.push_back(broadcast);
                }
            }
        }
        return cached;
    }

    std::vector<u32> LocalAddresses()
    {
        std::vector<u32> addresses;
        if (!NetStartup::Ensure()) return addresses;

        char name[256]{};
        if (gethostname(name, sizeof(name)) != 0) return addresses;

        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* results = nullptr;
        if (getaddrinfo(name, nullptr, &hints, &results) == 0)
        {
            for (addrinfo* it = results; it != nullptr; it = it->ai_next)
            {
                const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
                const u32 ip = ntohl(in->sin_addr.s_addr);
                if (std::find(addresses.begin(), addresses.end(), ip) == addresses.end())
                {
                    addresses.push_back(ip);
                }
            }
            freeaddrinfo(results);
        }

        addresses.push_back(0x7F000001u);   // loopback, last: a real address is preferred
        return addresses;
    }
}
