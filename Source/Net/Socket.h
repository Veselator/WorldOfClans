// Socket.h - the thinnest Winsock wrapper the game needs, and nothing more.
//
// Two kinds of socket and no abstraction over them: a TCP stream for the link between the
// host and each player, and a UDP socket for finding a lobby on the local network by its
// code. Everything is non-blocking and polled from the frame loop, because the game has a
// frame loop already and a networking thread would only buy it a class of bug it does not
// currently have.
#pragma once

#include "../Core/Types.h"

#include <string>
#include <vector>

namespace woc
{
    /// One address, in the only form this game ever needs it: dotted IPv4 and a port.
    struct NetAddress
    {
        u32 ipv4 = 0;        // host byte order
        u16 port = 0;

        bool Valid() const { return ipv4 != 0 && port != 0; }
        std::string ToString() const;
        static NetAddress Parse(const std::string& text, u16 port);
        bool operator==(const NetAddress& other) const
        {
            return ipv4 == other.ipv4 && port == other.port;
        }
    };

    /// Starts Winsock on the first use and shuts it down with the process. Every socket
    /// below holds one, so nothing has to remember to call it.
    class NetStartup
    {
    public:
        static bool Ensure();
        static void Shutdown();
    };

    /// A connected, non-blocking TCP stream. Moves, never copies: a socket has one owner.
    class TcpSocket
    {
    public:
        TcpSocket() = default;
        ~TcpSocket();
        TcpSocket(const TcpSocket&) = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;
        TcpSocket(TcpSocket&& other) noexcept;
        TcpSocket& operator=(TcpSocket&& other) noexcept;

        /// Begins a connection. Returns false only when the socket could not be made at
        /// all; a connection still in progress is a success here and settles in Poll.
        bool Connect(const NetAddress& address);
        /// True once the handshake has finished. False while it is still in flight.
        bool Connected() const { return m_connected; }
        /// Advances a pending connection and notices a dropped one. Call once a frame.
        void Poll();

        /// Queues bytes; they go out as the socket allows. Never blocks.
        void Send(const void* data, size_t bytes);
        /// Drains whatever has arrived into `out`, appending. False when the link is gone.
        bool Receive(std::vector<u8>& out);
        /// Pushes as much of the outgoing queue as the socket will take.
        void Flush();

        bool Alive() const { return m_handle != 0 && !m_broken; }
        void Close();

        const NetAddress& Peer() const { return m_peer; }
        void SetPeer(const NetAddress& address) { m_peer = address; }

        /// Adopts an accepted handle. Used by TcpListener and by nothing else.
        void Adopt(u64 handle, const NetAddress& peer);

    private:
        u64 m_handle = 0;
        NetAddress m_peer;
        std::vector<u8> m_outgoing;
        bool m_connected = false;
        bool m_broken = false;
    };

    /// A listening socket. Hands out accepted streams one at a time.
    class TcpListener
    {
    public:
        ~TcpListener();

        /// Binds and listens. `port` 0 asks the system for any free port; the one actually
        /// taken is then readable from Port().
        bool Listen(u16 port);
        /// Takes the next pending connection, or returns false when there is none waiting.
        bool Accept(TcpSocket& out);
        void Close();

        bool Open() const { return m_handle != 0; }
        u16 Port() const { return m_port; }

    private:
        u64 m_handle = 0;
        u16 m_port = 0;
    };

    /// A datagram socket, used for one thing: shouting a lobby's code across the network
    /// and listening for the answer.
    class UdpSocket
    {
    public:
        ~UdpSocket();

        /// Binds to `port` (0 for any) and enables broadcast.
        bool Open(u16 port);
        void Close();
        bool IsOpen() const { return m_handle != 0; }

        void SendTo(const NetAddress& address, const void* data, size_t bytes);
        /// Sends to the broadcast address on `port`.
        void Broadcast(u16 port, const void* data, size_t bytes);
        /// Reads one waiting datagram. False when there is nothing to read.
        bool ReceiveFrom(std::vector<u8>& out, NetAddress& from);

    private:
        u64 m_handle = 0;
    };

    /// This machine's addresses on the local networks, so a lobby can tell a joiner where
    /// to knock. The loopback is included last, for two copies of the game on one desk.
    std::vector<u32> LocalAddresses();
    /// The broadcast address of every network this machine is actually on (192.168.1.255
    /// and the like). The all-ones broadcast only leaves by whichever adapter Windows likes
    /// best - often a VPN or a virtual switch - so a lobby shouted only there is never heard
    /// by the laptop across the room.
    std::vector<u32> BroadcastAddresses();
}
