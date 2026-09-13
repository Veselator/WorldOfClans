#include "NetSession.h"

#include "../Core/Log.h"
#include "../Core/Paths.h"
#include "../Core/Random.h"
#include "../Game/Map/MapLoader.h"
#include "../Game/SaveGame.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace woc
{
    namespace
    {
        /// What goes over the wire. Everything but the map files is a JSON document, which
        /// costs a few bytes nobody will ever notice and saves writing a parser twice.
        enum MessageType : u16
        {
            MsgHello = 1,        // client -> host: who I am and where I listen
            MsgLobby = 2,        // host -> all: the whole lobby, whenever it changes
            MsgSeat = 3,         // client -> host: my seat, my people, my banner, my readiness
            MsgStart = 4,        // host -> all: load this and begin
            MsgCommand = 5,      // client -> host: an order the player has given
            // 6 was the world snapshot of the old host-authoritative model; unused.
            MsgMapRequest = 7,   // client -> host: I do not have this map
            MsgMapFile = 8,      // host -> client: one file of it (binary)
            MsgMapDone = 9,      // host -> client: that was the last of them
            MsgSaveRequest = 10, // client -> host: I do not have the save we are continuing
            MsgSaveFile = 11,    // host -> client: here it is (binary)
            MsgTurn = 12,        // host -> all: advance to tick N; these orders on these ticks
            MsgResyncRequest = 13, // client -> host: I have fallen out of step
            MsgResync = 14,      // host -> all: the whole world, to start again from
        };

        /// The discovery ports. Four of them, so that four copies of the game on one
        /// machine can each hold one and still hear each other - which is how anybody
        /// actually tries a multiplayer build for the first time.
        constexpr u16 kDiscoveryPorts[] = { 45789, 45790, 45791, 45792 };
        constexpr f32 kBeaconSeconds = 1.0f;
        constexpr i32 kProtocolVersion = 1;

        std::string MakeCode()
        {
            // No I and no O: a code is read aloud and typed in by hand.
            static const char* kAlphabet = "0123456789ABCDEFGHJKLMNPQRSTUVWXYZ";
            const size_t span = std::strlen(kAlphabet);

            Random random;
            std::string code;
            for (int i = 0; i < 5; ++i)
            {
                code += kAlphabet[static_cast<size_t>(random.Range(0, static_cast<i32>(span) - 1))];
            }
            return code;
        }

        std::string MakePeerId()
        {
            static const char* kHex = "0123456789abcdef";
            Random random;
            std::string id;
            for (int i = 0; i < 8; ++i) id += kHex[random.Range(0, 15)];
            return id;
        }

        void WriteU32(std::vector<u8>& out, u32 value)
        {
            out.push_back(static_cast<u8>(value & 0xFF));
            out.push_back(static_cast<u8>((value >> 8) & 0xFF));
            out.push_back(static_cast<u8>((value >> 16) & 0xFF));
            out.push_back(static_cast<u8>((value >> 24) & 0xFF));
        }

        u32 ReadU32(const std::vector<u8>& in, size_t& cursor)
        {
            if (cursor + 4 > in.size()) { cursor = in.size(); return 0; }
            const u32 value = static_cast<u32>(in[cursor]) |
                              (static_cast<u32>(in[cursor + 1]) << 8) |
                              (static_cast<u32>(in[cursor + 2]) << 16) |
                              (static_cast<u32>(in[cursor + 3]) << 24);
            cursor += 4;
            return value;
        }

        std::string ReadString(const std::vector<u8>& in, size_t& cursor)
        {
            const u32 length = ReadU32(in, cursor);
            if (cursor + length > in.size()) { cursor = in.size(); return std::string(); }
            const std::string text(reinterpret_cast<const char*>(in.data() + cursor), length);
            cursor += length;
            return text;
        }
    }

    // =========================================================================================
    // The lobby document
    // =========================================================================================

    Json LobbyPlayer::ToJson() const
    {
        Json node = Json::MakeObject();
        node["id"] = id;
        node["name"] = name;
        node["seat"] = seat;
        node["ready"] = ready;
        node["host"] = host;
        node["address"] = static_cast<i64>(address);
        node["port"] = static_cast<i64>(port);
        return node;
    }

    LobbyPlayer LobbyPlayer::FromJson(const Json& node)
    {
        LobbyPlayer player;
        player.id = node["id"].AsString();
        player.name = node["name"].AsString();
        player.seat = node["seat"].AsInt(-1);
        player.ready = node["ready"].AsBool(false);
        player.host = node["host"].AsBool(false);
        player.address = static_cast<u32>(node["address"].AsNumber(0.0));
        player.port = static_cast<u16>(node["port"].AsInt(0));
        return player;
    }

    const LobbyPlayer* LobbyState::Find(const std::string& id) const
    {
        for (const LobbyPlayer& player : players)
        {
            if (player.id == id) return &player;
        }
        return nullptr;
    }

    LobbyPlayer* LobbyState::Find(const std::string& id)
    {
        for (LobbyPlayer& player : players)
        {
            if (player.id == id) return &player;
        }
        return nullptr;
    }

    i32 LobbyState::AiCount() const
    {
        i32 taken = 0;
        for (const LobbyPlayer& player : players)
        {
            if (player.seat >= 0 && player.seat < party.stateCount) ++taken;
        }
        return std::max(0, party.stateCount - taken);
    }

    Json LobbyState::ToJson() const
    {
        Json node = Json::MakeObject();
        node["code"] = code;
        node["name"] = name;
        node["hostId"] = hostId;
        node["party"] = party.ToJson();
        node["save"] = saveFile;

        Json list = Json::MakeArray();
        for (const LobbyPlayer& player : players) list.Push(player.ToJson());
        node["players"] = list;
        return node;
    }

    LobbyState LobbyState::FromJson(const Json& node)
    {
        LobbyState state;
        state.code = node["code"].AsString();
        state.name = node["name"].AsString();
        state.hostId = node["hostId"].AsString();
        state.party = PartySettings::FromJson(node["party"]);
        state.saveFile = node["save"].AsString();
        for (const Json& player : node["players"].AsArray())
        {
            state.players.push_back(LobbyPlayer::FromJson(player));
        }
        return state;
    }

    // =========================================================================================
    // Opening and closing a session
    // =========================================================================================

    std::string NetSession::HostLobby(const std::string& playerName, const std::string& lobbyName)
    {
        Leave();

        if (!NetStartup::Ensure())
        {
            m_status = "Мережу запустити не вдалося";
            return std::string();
        }

        m_playerName = playerName.empty() ? std::string("Князь") : playerName;
        m_localId = MakePeerId();
        m_lobby.name = lobbyName.empty() ? "Лобі " + m_playerName : lobbyName;
        BeginHosting(MakeCode());
        return m_lobby.code;
    }

    void NetSession::SetLobbyName(const std::string& name)
    {
        if (m_role != NetRole::Host || name == m_lobby.name) return;
        m_lobby.name = name;
        PublishLobby();
    }

    bool NetSession::OpenBeacon()
    {
        if (m_beacon.IsOpen()) return true;
        // One of the discovery ports, whichever is free: four copies of the game on one
        // machine each get their own and still hear one another's broadcasts.
        for (u16 port : kDiscoveryPorts)
        {
            if (m_beacon.Open(port))
            {
                WOC_LOG_INFO("Discovery: listening on UDP ", port);
                return true;
            }
        }
        if (m_beacon.Open(0))
        {
            WOC_LOG_WARN("Discovery: every discovery port is taken; only direct answers will be heard");
            return true;
        }
        WOC_LOG_ERROR("Discovery: no UDP socket could be opened");
        return false;
    }

    Json NetSession::Announcement() const
    {
        Json announce = Json::MakeObject();
        announce["woc"] = "lobby";
        announce["code"] = m_lobby.code;
        announce["name"] = m_lobby.name;
        announce["host"] = m_playerName;
        announce["port"] = static_cast<i64>(m_listener.Port());
        announce["players"] = static_cast<i64>(m_lobby.players.size());
        announce["seats"] = static_cast<i64>(m_lobby.party.stateCount);
        announce["version"] = kProtocolVersion;
        return announce;
    }

    void NetSession::Shout(const std::string& text)
    {
        for (u16 port : kDiscoveryPorts)
        {
            m_beacon.Broadcast(port, text.data(), text.size());
            if (m_seekAddress.ipv4 != 0)
            {
                NetAddress direct = m_seekAddress;
                direct.port = port;
                m_beacon.SendTo(direct, text.data(), text.size());
            }
        }
    }

    void NetSession::NoteListing(const Json& message, const NetAddress& from)
    {
        if (message["version"].AsInt(0) != kProtocolVersion) return;

        LobbyListing heard;
        heard.code = message["code"].AsString();
        heard.name = message["name"].AsString();
        heard.hostName = message["host"].AsString();
        heard.players = message["players"].AsInt(0);
        heard.seats = message["seats"].AsInt(0);
        heard.address = from;
        heard.address.port = static_cast<u16>(message["port"].AsInt(0));
        if (heard.code.empty() || !heard.address.Valid()) return;

        for (LobbyListing& known : m_listings)
        {
            if (known.code != heard.code) continue;
            // The same lobby heard over loopback and over the network: keep the real address.
            const bool loopback = (heard.address.ipv4 >> 24) == 127;
            if (loopback && (known.address.ipv4 >> 24) != 127) heard.address = known.address;
            known = heard;
            return;
        }
        m_listings.push_back(heard);
        WOC_LOG_INFO("Discovery: heard lobby ", heard.code, " at ", heard.address.ToString());
    }

    void NetSession::Browse()
    {
        if (m_role != NetRole::Offline) return;
        if (!NetStartup::Ensure()) return;
        if (!OpenBeacon()) return;
        if (m_browseTimer <= 0.0f) m_browseBeacon = 0.0f;   // ask at once when the list opens
        m_browseTimer = 1.5f;
    }

    bool NetSession::JoinListing(const LobbyListing& listing, const std::string& playerName)
    {
        if (!JoinLobby(listing.code, playerName)) return false;

        // We already know where it is: no need to shout for it.
        Peer peer;
        if (!peer.socket.Connect(listing.address))
        {
            m_status = "Не вдалося під'єднатися до " + listing.address.ToString();
            return true;   // still seeking by code; the answer may yet come
        }
        m_peers.clear();
        m_peers.push_back(std::move(peer));
        m_connectTimer = 0.0f;
        m_status = "З'єднання з " + listing.address.ToString() + "...";
        WOC_LOG_INFO("Joining listed lobby ", listing.code, " at ", listing.address.ToString());
        return true;
    }

    void NetSession::BeginHosting(const std::string& code)
    {
        if (!m_listener.Open() && !m_listener.Listen(0))
        {
            m_status = "Не вдалося відкрити порт";
            m_role = NetRole::Offline;
            return;
        }

        OpenBeacon();

        m_role = NetRole::Host;
        m_lobby.code = code;
        m_lobby.hostId = m_localId;
        m_seeking.clear();
        m_beaconTimer = 0.0f;

        // The host always holds the first seat of his own lobby.
        LobbyPlayer* me = m_lobby.Find(m_localId);
        if (!me)
        {
            LobbyPlayer player;
            player.id = m_localId;
            player.name = m_playerName;
            player.seat = 0;
            player.host = true;
            m_lobby.players.push_back(player);
            me = &m_lobby.players.back();
        }
        me->host = true;
        me->port = m_listener.Port();

        for (LobbyPlayer& player : m_lobby.players) player.host = player.id == m_localId;

        // The code is already in the panel's title and in the players' column.
        m_status.clear();
        WOC_LOG_INFO("Hosting lobby ", m_lobby.code, " on port ", m_listener.Port());
        PublishLobby();
    }

    bool NetSession::JoinLobby(const std::string& code, const std::string& playerName)
    {
        Leave();

        if (!NetStartup::Ensure())
        {
            m_status = "Мережу запустити не вдалося";
            return false;
        }

        m_playerName = playerName.empty() ? std::string("Гравець") : playerName;
        m_localId = MakePeerId();

        // A joiner listens too - that is what lets him become the host later.
        if (!m_listener.Listen(0))
        {
            m_status = "Не вдалося відкрити порт";
            return false;
        }
        OpenBeacon();

        // An address rather than a code: ask that machine directly, and take whatever lobby
        // it answers with.
        m_seekAddress = NetAddress{};
        m_seeking.clear();
        const NetAddress typed = NetAddress::Parse(code, 1);
        if (typed.ipv4 != 0 && code.find('.') != std::string::npos)
        {
            m_seekAddress = typed;
            m_seeking = "*";
        }
        else
        {
            for (char c : code) m_seeking += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        m_role = NetRole::Client;
        m_seekTimer = 0.0f;
        m_beaconTimer = 0.0f;
        m_connectTimer = 0.0f;
        m_status = m_seekAddress.ipv4 != 0 ? "Стукаємо до " + code + "..."
                                           : "Шукаємо лобі " + m_seeking + "...";
        WOC_LOG_INFO("Discovery: seeking ", m_seeking, m_seekAddress.ipv4 ? " at " + code : std::string());
        return true;
    }

    void NetSession::Leave()
    {
        m_peers.clear();
        m_listener.Close();
        m_beacon.Close();
        m_role = NetRole::Offline;
        m_lobby = LobbyState{};
        m_seeking.clear();
        m_seekAddress = NetAddress{};
        m_connectTimer = 0.0f;
        m_orders.clear();
        m_scheduled.clear();
        m_checksums.clear();
        m_confirmedTick = 0;
        m_resyncRequested = false;
        m_resyncAwaited = false;
        m_resyncPending = false;
        m_startPending = false;
        m_awaitedMap.clear();
        m_awaitedSave.clear();
        m_awaitedFiles = 0;
        m_status.clear();
    }

    void NetSession::Shutdown()
    {
        Leave();
        NetStartup::Shutdown();
    }

    // =========================================================================================
    // The frame pump
    // =========================================================================================

    void NetSession::Update(f32 realSeconds)
    {
        // Listings age whether or not anybody is looking at them.
        for (LobbyListing& listing : m_listings) listing.age += realSeconds;
        m_listings.erase(std::remove_if(m_listings.begin(), m_listings.end(),
                                        [](const LobbyListing& l) { return l.age > 4.5f; }),
                         m_listings.end());

        if (m_role == NetRole::Offline)
        {
            if (m_browseTimer <= 0.0f) return;
            m_browseTimer -= realSeconds;
            if (m_browseTimer <= 0.0f)
            {
                m_beacon.Close();
                return;
            }
            PumpDiscovery(realSeconds);
            return;
        }

        PumpDiscovery(realSeconds);

        if (m_role == NetRole::Host) PumpHost();
        else PumpClient();
    }

    void NetSession::PumpDiscovery(f32 realSeconds)
    {
        if (!m_beacon.IsOpen()) return;

        // --- what we shout ---------------------------------------------------------------
        if (m_role == NetRole::Offline)
        {
            // Browsing: ask who is out there. Hosts answer to the asker directly, and an
            // answer to something we sent gets through a firewall that would drop a
            // stranger's broadcast.
            m_browseBeacon -= realSeconds;
            if (m_browseBeacon <= 0.0f)
            {
                m_browseBeacon = 1.5f;
                Json seek = Json::MakeObject();
                seek["woc"] = "seek";
                seek["code"] = "*";
                Shout(seek.Dump(0));
            }
        }
        else
        {
            m_beaconTimer -= realSeconds;
            if (m_beaconTimer <= 0.0f)
            {
                m_beaconTimer = kBeaconSeconds;

                Json announce;
                if (m_role == NetRole::Host)
                {
                    announce = Announcement();
                }
                else if (!m_seeking.empty() && m_peers.empty())
                {
                    announce = Json::MakeObject();
                    announce["woc"] = "seek";
                    announce["code"] = m_seeking;
                }

                if (!announce.IsNull()) Shout(announce.Dump(0));
            }
        }

        // A connection that never completes - a firewall dropping the packets on the floor -
        // is abandoned after a few seconds and the search starts again.
        if (m_role == NetRole::Client && !m_peers.empty() && m_lobby.code.empty())
        {
            m_connectTimer += realSeconds;
            if (!m_peers.front().socket.Connected() && m_connectTimer > 6.0f)
            {
                const std::string where = m_peers.front().socket.Peer().ToString();
                WOC_LOG_WARN("Discovery: no TCP connection to ", where, " - blocked or unreachable");
                m_peers.clear();
                m_connectTimer = 0.0f;
                m_status = "Хост " + where + " не відповідає. Перевірте, чи дозволено гру "
                           "в брандмауері Windows на його комп'ютері.";
            }
        }

        // --- what we hear ------------------------------------------------------------------
        std::vector<u8> datagram;
        NetAddress from;
        while (m_beacon.ReceiveFrom(datagram, from))
        {
            const std::string text(reinterpret_cast<const char*>(datagram.data()), datagram.size());
            const Json message = Json::Parse(text);
            if (message.IsNull()) continue;

            const std::string kind = message["woc"].AsString();

            if (kind == "lobby") NoteListing(message, from);

            if (m_role == NetRole::Host && kind == "seek" &&
                (message["code"].AsString() == m_lobby.code || message["code"].AsString() == "*"))
            {
                // Somebody is looking for exactly us, or for anyone at all: answer at once,
                // to the address the question came from, rather than making them wait for
                // the next beacon.
                const std::string reply = Announcement().Dump(0);
                m_beacon.SendTo(from, reply.data(), reply.size());
                continue;
            }

            const bool wanted = m_seeking == "*"
                ? (m_seekAddress.ipv4 != 0 && from.ipv4 == m_seekAddress.ipv4)
                : message["code"].AsString() == m_seeking;
            if (m_role == NetRole::Client && !m_seeking.empty() && m_peers.empty() &&
                kind == "lobby" && wanted)
            {
                if (message["version"].AsInt(0) != kProtocolVersion)
                {
                    m_status = "Інша версія гри в цього лобі";
                    continue;
                }

                NetAddress target = from;
                target.port = static_cast<u16>(message["port"].AsInt(0));
                if (!target.Valid()) continue;

                Peer peer;
                if (!peer.socket.Connect(target))
                {
                    m_status = "Не вдалося під'єднатися до " + target.ToString();
                    continue;
                }

                m_peers.clear();
                m_peers.push_back(std::move(peer));
                m_connectTimer = 0.0f;
                m_status = "З'єднання з " + target.ToString() + "...";
                WOC_LOG_INFO("Joining lobby at ", target.ToString());
            }
        }
    }

    void NetSession::PumpHost()
    {
        // --- new arrivals -------------------------------------------------------------------
        for (;;)
        {
            Peer peer;
            if (!m_listener.Accept(peer.socket)) break;
            WOC_LOG_INFO("Lobby: a player is connecting from ", peer.socket.Peer().ToString());
            m_peers.push_back(std::move(peer));
        }

        // --- traffic -------------------------------------------------------------------------
        bool lost = false;
        for (Peer& peer : m_peers)
        {
            peer.socket.Poll();
            if (!peer.socket.Receive(peer.incoming))
            {
                lost = true;
                continue;
            }

            u16 type = 0;
            std::vector<u8> payload;
            while (NextMessage(peer.incoming, type, payload)) HandleMessage(peer, type, payload);
        }

        if (lost)
        {
            // Whoever has gone leaves the table; the rest are told about it.
            for (const Peer& peer : m_peers)
            {
                if (peer.socket.Alive() || peer.id.empty()) continue;
                m_lobby.players.erase(
                    std::remove_if(m_lobby.players.begin(), m_lobby.players.end(),
                                   [&peer](const LobbyPlayer& player) { return player.id == peer.id; }),
                    m_lobby.players.end());
                WOC_LOG_INFO("Lobby: ", peer.id, " has left");
            }
            m_peers.erase(std::remove_if(m_peers.begin(), m_peers.end(),
                                         [](const Peer& peer) { return !peer.socket.Alive(); }),
                          m_peers.end());
            PublishLobby();
        }
    }

    void NetSession::PumpClient()
    {
        if (m_peers.empty()) return;

        Peer& host = m_peers.front();
        host.socket.Poll();

        if (host.socket.Connected() && !host.greeted)
        {
            host.greeted = true;

            Json hello = Json::MakeObject();
            hello["id"] = m_localId;
            hello["name"] = m_playerName;
            hello["port"] = static_cast<i64>(m_listener.Port());
            hello["version"] = kProtocolVersion;
            SendTo(host.socket, MsgHello, hello.Dump(0));
        }

        if (!host.socket.Receive(host.incoming))
        {
            WOC_LOG_WARN("The host has gone; looking for a successor");
            MigrateHost();
            return;
        }

        u16 type = 0;
        std::vector<u8> payload;
        while (NextMessage(host.incoming, type, payload)) HandleMessage(host, type, payload);
    }

    // =========================================================================================
    // Framing
    // =========================================================================================

    bool NetSession::NextMessage(std::vector<u8>& buffer, u16& type, std::vector<u8>& payload)
    {
        // [u32 payload length][u16 type][payload]
        if (buffer.size() < 6) return false;

        size_t cursor = 0;
        const u32 length = ReadU32(buffer, cursor);
        if (buffer.size() < 6 + length) return false;

        type = static_cast<u16>(buffer[4]) | (static_cast<u16>(buffer[5]) << 8);
        payload.assign(buffer.begin() + 6, buffer.begin() + 6 + static_cast<i64>(length));
        buffer.erase(buffer.begin(), buffer.begin() + 6 + static_cast<i64>(length));
        return true;
    }

    void NetSession::SendTo(TcpSocket& socket, u16 type, const std::string& payload)
    {
        std::vector<u8> bytes(payload.begin(), payload.end());
        SendTo(socket, type, bytes);
    }

    void NetSession::SendTo(TcpSocket& socket, u16 type, const std::vector<u8>& payload)
    {
        std::vector<u8> frame;
        frame.reserve(payload.size() + 6);
        WriteU32(frame, static_cast<u32>(payload.size()));
        frame.push_back(static_cast<u8>(type & 0xFF));
        frame.push_back(static_cast<u8>((type >> 8) & 0xFF));
        frame.insert(frame.end(), payload.begin(), payload.end());
        socket.Send(frame.data(), frame.size());
    }

    void NetSession::Broadcast(u16 type, const std::string& payload)
    {
        for (Peer& peer : m_peers)
        {
            if (peer.socket.Alive()) SendTo(peer.socket, type, payload);
        }
    }

    // =========================================================================================
    // What each message means
    // =========================================================================================

    void NetSession::HandleMessage(Peer& peer, u16 type, const std::vector<u8>& payload)
    {
        const auto asJson = [&payload]()
        {
            return Json::Parse(std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
        };

        switch (type)
        {
        case MsgHello:
        {
            if (m_role != NetRole::Host) break;
            const Json hello = asJson();
            if (hello["version"].AsInt(0) != kProtocolVersion) break;

            peer.id = hello["id"].AsString();
            peer.greeted = true;

            LobbyPlayer player;
            player.id = peer.id;
            player.name = hello["name"].AsString("Гравець");
            player.port = static_cast<u16>(hello["port"].AsInt(0));
            player.address = peer.socket.Peer().ipv4;
            player.seat = -1;

            // The first free realm, so a joiner is playing rather than watching.
            for (i32 seat = 0; seat < m_lobby.party.stateCount; ++seat)
            {
                const bool taken = std::any_of(m_lobby.players.begin(), m_lobby.players.end(),
                    [seat](const LobbyPlayer& other) { return other.seat == seat; });
                if (!taken) { player.seat = seat; break; }
            }

            if (LobbyPlayer* existing = m_lobby.Find(peer.id)) *existing = player;
            else m_lobby.players.push_back(player);

            WOC_LOG_INFO("Lobby: ", player.name, " joined as ", peer.id);
            PublishLobby();
            break;
        }

        case MsgLobby:
        {
            if (m_role != NetRole::Client) break;
            m_lobby = LobbyState::FromJson(asJson());
            m_seeking.clear();
            m_seekAddress = NetAddress{};
            m_status.clear();   // the code is in the title; saying it twice helps nobody

            // The map, and the save if the lobby is carrying one on, may be things this
            // machine has never seen. Ask for them now, so they are on disk long before
            // anybody presses "ready".
            RequestMissingFiles(peer);
            break;
        }

        case MsgSeat:
        {
            if (m_role != NetRole::Host) break;
            const Json seat = asJson();
            LobbyPlayer* player = m_lobby.Find(seat["id"].AsString());
            if (!player) break;

            player->seat = seat["seat"].AsInt(player->seat);
            player->ready = seat["ready"].AsBool(player->ready);
            if (seat["name"].IsString()) player->name = seat["name"].AsString();

            // A player's people and banner belong to the seat he is holding.
            if (player->seat >= 0 && player->seat < static_cast<i32>(m_lobby.party.seats.size()))
            {
                SeatSettings& target = m_lobby.party.seats[static_cast<size_t>(player->seat)];
                if (seat["race"].IsString()) target.raceId = seat["race"].AsString();
                if (!seat["color"].IsNull()) target.color = static_cast<u32>(seat["color"].AsNumber(target.color));
                target.human = true;
                target.playerName = player->name;
                target.peerId = player->id;
            }
            PublishLobby();
            break;
        }

        case MsgStart:
        {
            if (m_role != NetRole::Client) break;
            const Json start = asJson();
            m_startSettings = PartySettings::FromJson(start["party"]);
            m_startSave = start["save"].AsString();

            // Each machine plays its own seat, whatever the host's document says.
            if (const LobbyPlayer* me = m_lobby.Find(m_localId))
            {
                m_startSettings.humanSeat = std::max(0, me->seat);
            }
            m_startPending = true;
            break;
        }

        case MsgCommand:
        {
            if (m_role != NetRole::Host) break;
            Json command = asJson();
            // Stamped by the host from the socket it arrived on, never by the sender: an
            // order can therefore only ever be about the realm of whoever actually sent it.
            command["peer"] = peer.id;
            TurnOrder order;
            order.peer = peer.id;
            order.command = std::move(command);
            m_orders.push_back(std::move(order));
            break;
        }

        case MsgTurn:
        {
            if (m_role != NetRole::Client) break;
            const Json turn = asJson();
            m_confirmedTick = std::max(m_confirmedTick, static_cast<u64>(turn["to"].AsNumber(0.0)));
            m_hostSpeed = turn["speed"].AsInt(m_hostSpeed);
            for (const Json& entry : turn["orders"].AsArray())
            {
                TurnOrder order;
                order.tick = static_cast<u64>(entry["t"].AsNumber(0.0));
                order.peer = entry["p"].AsString();
                order.command = entry["c"];
                m_scheduled.push_back(std::move(order));
            }
            for (const Json& entry : turn["sums"].AsArray())
            {
                m_checksums.emplace_back(static_cast<u64>(entry[static_cast<size_t>(0)].AsNumber(0.0)),
                                         static_cast<u64>(std::stoull(entry[static_cast<size_t>(1)].AsString("0"))));
            }
            // Fingerprints for ticks long past are of no more use.
            if (m_checksums.size() > 64) m_checksums.erase(m_checksums.begin(), m_checksums.end() - 64);
            break;
        }

        case MsgResyncRequest:
        {
            if (m_role != NetRole::Host) break;
            WOC_LOG_WARN("Lockstep: ", peer.id, " asks for the whole world");
            m_resyncRequested = true;
            break;
        }

        case MsgResync:
        {
            if (m_role != NetRole::Client) break;
            m_resync = asJson();
            m_resyncPending = !m_resync.IsNull();
            break;
        }

        case MsgMapRequest:
        {
            if (m_role != NetRole::Host) break;
            SendMap(peer, asJson()["folder"].AsString());
            break;
        }

        case MsgMapFile:
            ReceiveMapFile(payload);
            break;

        case MsgMapDone:
        {
            if (m_role != NetRole::Client) break;
            WOC_LOG_INFO("Map received: ", m_awaitedMap, " (", m_awaitedFiles, " files)");
            m_status = "Карту " + m_awaitedMap + " отримано";
            m_awaitedMap.clear();
            break;
        }

        case MsgSaveRequest:
        {
            if (m_role != NetRole::Host) break;
            SendSave(peer, asJson()["file"].AsString());
            break;
        }

        case MsgSaveFile:
        {
            if (m_role != NetRole::Client) break;
            ReceiveSaveFile(payload);
            m_status = "Збереження отримано";
            m_awaitedSave.clear();
            break;
        }

        default:
            break;
        }
    }

    // =========================================================================================
    // The lobby, as the host keeps it
    // =========================================================================================

    void NetSession::PublishLobby()
    {
        if (m_role != NetRole::Host) return;

        // The seats a person is holding are marked as such; the rest belong to the machine.
        for (size_t i = 0; i < m_lobby.party.seats.size(); ++i)
        {
            SeatSettings& seat = m_lobby.party.seats[i];
            const bool held = std::any_of(m_lobby.players.begin(), m_lobby.players.end(),
                [i](const LobbyPlayer& player) { return player.seat == static_cast<i32>(i); });
            if (!held)
            {
                seat.human = false;
                seat.playerName.clear();
                seat.peerId.clear();
            }
        }

        Broadcast(MsgLobby, m_lobby.ToJson().Dump(0));
    }

    void NetSession::SetLocalSeat(i32 seat)
    {
        if (m_role == NetRole::Host)
        {
            if (LobbyPlayer* me = m_lobby.Find(m_localId)) me->seat = seat;
            PublishLobby();
            return;
        }
        if (m_peers.empty()) return;

        Json update = Json::MakeObject();
        update["id"] = m_localId;
        update["seat"] = seat;
        SendTo(m_peers.front().socket, MsgSeat, update.Dump(0));
    }

    void NetSession::SetLocalReady(bool ready)
    {
        if (m_role == NetRole::Host)
        {
            if (LobbyPlayer* me = m_lobby.Find(m_localId)) me->ready = ready;
            PublishLobby();
            return;
        }
        if (m_peers.empty()) return;

        // A client sends its whole seat with its readiness: the people and the banner it
        // picked are decided locally and the host only records them.
        Json update = Json::MakeObject();
        update["id"] = m_localId;
        update["ready"] = ready;
        update["name"] = m_playerName;

        if (const LobbyPlayer* me = m_lobby.Find(m_localId))
        {
            update["seat"] = me->seat;
            if (me->seat >= 0 && me->seat < static_cast<i32>(m_lobby.party.seats.size()))
            {
                const SeatSettings& seat = m_lobby.party.seats[static_cast<size_t>(me->seat)];
                update["race"] = seat.raceId;
                update["color"] = static_cast<i64>(seat.color);
            }
        }
        SendTo(m_peers.front().socket, MsgSeat, update.Dump(0));
    }

    void NetSession::SetParty(const PartySettings& party)
    {
        m_lobby.party = party;
        if (m_role == NetRole::Host) PublishLobby();
    }

    void NetSession::SetSaveFile(const std::string& file)
    {
        m_lobby.saveFile = file;

        // Carrying a save on means playing the map it was played on; anything else would be
        // a save of one world laid over another. The two choices are therefore one choice.
        if (!file.empty())
        {
            for (const SaveSlot& slot : SaveGame::List())
            {
                if (slot.fileName != file) continue;
                if (!slot.mapFolder.empty()) m_lobby.party.mapFolder = slot.mapFolder;
                m_lobby.party.generateMap = false;
                break;
            }
        }

        if (m_role == NetRole::Host) PublishLobby();
    }

    bool NetSession::EveryoneReady() const
    {
        if (m_lobby.players.empty()) return false;
        return std::all_of(m_lobby.players.begin(), m_lobby.players.end(),
                           [](const LobbyPlayer& player) { return player.ready; });
    }

    bool NetSession::StartParty()
    {
        if (m_role != NetRole::Host || !EveryoneReady()) return false;

        // Every seat a person is holding is human; the seat each machine plays is decided
        // on that machine, from the lobby it already has.
        for (size_t i = 0; i < m_lobby.party.seats.size(); ++i)
        {
            m_lobby.party.seats[i].human = false;
        }
        for (const LobbyPlayer& player : m_lobby.players)
        {
            if (player.seat < 0 || player.seat >= static_cast<i32>(m_lobby.party.seats.size())) continue;
            SeatSettings& seat = m_lobby.party.seats[static_cast<size_t>(player.seat)];
            seat.human = true;
            seat.playerName = player.name;
            seat.peerId = player.id;
        }

        // Every machine must generate the same world, so a seed of "draw one" is drawn
        // here, once, and written into the document everybody receives.
        if (m_lobby.party.seed == 0)
        {
            Random random;
            m_lobby.party.seed = static_cast<u32>(random.Range(1, 2000000000));
        }

        Json start = Json::MakeObject();
        start["party"] = m_lobby.party.ToJson();
        start["save"] = m_lobby.saveFile;
        Broadcast(MsgStart, start.Dump(0));

        m_startSettings = m_lobby.party;
        if (const LobbyPlayer* me = m_lobby.Find(m_localId)) m_startSettings.humanSeat = std::max(0, me->seat);
        m_startSave = m_lobby.saveFile;
        m_startPending = true;
        return true;
    }

    bool NetSession::ConsumeStart(PartySettings& outSettings, std::string& outSaveFile)
    {
        if (!m_startPending) return false;
        m_startPending = false;
        outSettings = m_startSettings;
        outSaveFile = m_startSave;
        return true;
    }

    // =========================================================================================
    // The party
    // =========================================================================================

    void NetSession::SubmitOrder(const Json& command)
    {
        if (m_role == NetRole::Host)
        {
            TurnOrder order;
            order.peer = m_localId;
            order.command = command;
            m_orders.push_back(std::move(order));
            return;
        }
        if (m_role != NetRole::Client || m_peers.empty()) return;
        SendTo(m_peers.front().socket, MsgCommand, command.Dump(0));
    }

    void NetSession::TakeOrders(std::vector<TurnOrder>& out)
    {
        out.clear();
        out.swap(m_orders);
    }

    void NetSession::SendTurn(u64 upTo, i32 speedIndex, const std::vector<TurnOrder>& orders,
                              const std::vector<std::pair<u64, u64>>& checksums)
    {
        if (m_role != NetRole::Host || m_peers.empty()) return;

        Json turn = Json::MakeObject();
        turn["to"] = static_cast<i64>(upTo);
        turn["speed"] = speedIndex;
        Json list = Json::MakeArray();
        for (const TurnOrder& order : orders)
        {
            Json entry = Json::MakeObject();
            entry["t"] = static_cast<i64>(order.tick);
            entry["p"] = order.peer;
            entry["c"] = order.command;
            list.Push(entry);
        }
        turn["orders"] = list;
        if (!checksums.empty())
        {
            Json sums = Json::MakeArray();
            for (const auto& [tick, sum] : checksums)
            {
                Json pair = Json::MakeArray();
                pair.Push(static_cast<i64>(tick));
                // As text: a 64-bit fingerprint does not survive a trip through a double.
                pair.Push(std::to_string(sum));
                sums.Push(pair);
            }
            turn["sums"] = sums;
        }
        Broadcast(MsgTurn, turn.Dump(0));
    }

    void NetSession::TakeOrdersAt(u64 tick, std::vector<TurnOrder>& out)
    {
        out.clear();
        while (!m_scheduled.empty() && m_scheduled.front().tick <= tick)
        {
            if (m_scheduled.front().tick == tick) out.push_back(std::move(m_scheduled.front()));
            m_scheduled.pop_front();   // an order for a tick already gone cannot be carried out
        }
    }

    bool NetSession::ChecksumAt(u64 tick, u64& out) const
    {
        for (const auto& [at, sum] : m_checksums)
        {
            if (at == tick) { out = sum; return true; }
        }
        return false;
    }

    void NetSession::RequestResync()
    {
        if (m_role != NetRole::Client || m_peers.empty() || m_resyncAwaited) return;
        m_resyncAwaited = true;
        SendTo(m_peers.front().socket, MsgResyncRequest, std::string("{}"));
    }

    bool NetSession::ConsumeResyncRequest()
    {
        if (!m_resyncRequested) return false;
        m_resyncRequested = false;
        return true;
    }

    void NetSession::SendResync(const std::string& document)
    {
        if (m_role != NetRole::Host || m_peers.empty()) return;
        Broadcast(MsgResync, document);
    }

    bool NetSession::ConsumeResync(Json& outDocument)
    {
        if (!m_resyncPending) return false;
        m_resyncPending = false;
        m_resyncAwaited = false;
        outDocument = std::move(m_resync);
        m_resync = Json();
        // Everything scheduled before the new world is void: the new world already has it.
        const u64 tick = static_cast<u64>(outDocument["sim"]["tick"].AsNumber(0.0));
        while (!m_scheduled.empty() && m_scheduled.front().tick < tick) m_scheduled.pop_front();
        m_checksums.clear();
        m_confirmedTick = std::max(m_confirmedTick, tick);
        return true;
    }

    // =========================================================================================
    // When the host disappears
    // =========================================================================================

    void NetSession::MigrateHost()
    {
        // Everybody has the same list and applies the same rule, so nobody has to agree
        // with anybody: the survivor with the lowest id takes the table over.
        m_peers.clear();
        m_lobby.players.erase(
            std::remove_if(m_lobby.players.begin(), m_lobby.players.end(),
                           [this](const LobbyPlayer& player) { return player.id == m_lobby.hostId; }),
            m_lobby.players.end());

        if (m_lobby.players.empty())
        {
            m_status = "Хост зник, і більше нікого не лишилося";
            m_role = NetRole::Offline;
            return;
        }

        const LobbyPlayer* successor = &m_lobby.players.front();
        for (const LobbyPlayer& player : m_lobby.players)
        {
            if (player.id < successor->id) successor = &player;
        }

        if (successor->id == m_localId)
        {
            WOC_LOG_INFO("Taking over as host of lobby ", m_lobby.code);
            for (LobbyPlayer& player : m_lobby.players) player.ready = false;
            BeginHosting(m_lobby.code);
            m_status = "Хост зник — тепер хостом є ви";
            // The survivors may stand at different ticks; the new host's world is the one
            // everybody carries on from.
            m_resyncRequested = true;
            return;
        }

        NetAddress target;
        target.ipv4 = successor->address;
        target.port = successor->port;
        if (!target.Valid())
        {
            m_status = "Хост зник, і до наступника не достукатися";
            m_role = NetRole::Offline;
            return;
        }

        Peer peer;
        if (!peer.socket.Connect(target))
        {
            m_status = "Хост зник, і під'єднатися до наступника не вдалося";
            m_role = NetRole::Offline;
            return;
        }

        m_lobby.hostId = successor->id;
        m_peers.push_back(std::move(peer));
        m_status = "Хост змінився: " + successor->name;
    }

    // =========================================================================================
    // Sending a map down the wire
    // =========================================================================================

    bool NetSession::HasMap(const std::string& folder) const
    {
        if (folder.empty()) return true;
        return Paths::FileExists(Paths::Get().Map(folder, "Map.json"));
    }

    void NetSession::SendMap(Peer& peer, const std::string& folder)
    {
        if (folder.empty()) return;

        const std::string directory = Paths::Get().MapsDirectory() + "/" + folder;
        if (!Paths::DirectoryExists(directory))
        {
            WOC_LOG_WARN("A player asked for a map we do not have: ", folder);
            SendTo(peer.socket, MsgMapDone, std::string("{}"));
            return;
        }

        // Everything in the folder, whatever it happens to be: the layers a map is made of
        // are a data decision and the wire should not have an opinion about them.
        std::vector<std::string> files;
        for (const char* extension : { ".json", ".png" })
        {
            for (const std::string& name : Paths::ListFiles(directory, extension)) files.push_back(name);
        }

        i32 sent = 0;
        for (const std::string& name : files)
        {
            std::ifstream input(directory + "/" + name, std::ios::binary);
            if (!input) continue;

            const std::vector<u8> data((std::istreambuf_iterator<char>(input)),
                                       std::istreambuf_iterator<char>());

            std::vector<u8> message;
            WriteU32(message, static_cast<u32>(folder.size()));
            message.insert(message.end(), folder.begin(), folder.end());
            WriteU32(message, static_cast<u32>(name.size()));
            message.insert(message.end(), name.begin(), name.end());
            WriteU32(message, static_cast<u32>(data.size()));
            message.insert(message.end(), data.begin(), data.end());

            SendTo(peer.socket, MsgMapFile, message);
            ++sent;
        }

        SendTo(peer.socket, MsgMapDone, std::string("{}"));
        WOC_LOG_INFO("Sent map ", folder, " (", sent, " files) to ", peer.id);
    }

    std::string NetSession::WaitingFor() const
    {
        if (!m_awaitedMap.empty()) return "Отримуємо карту " + m_awaitedMap + "...";
        if (!m_awaitedSave.empty()) return "Отримуємо збереження...";
        return std::string();
    }

    void NetSession::RequestMissingFiles(Peer& host)
    {
        if (m_role != NetRole::Client) return;

        if (!m_lobby.party.mapFolder.empty() && !HasMap(m_lobby.party.mapFolder) &&
            m_awaitedMap != m_lobby.party.mapFolder)
        {
            m_awaitedMap = m_lobby.party.mapFolder;
            m_awaitedFiles = 0;
            Json request = Json::MakeObject();
            request["folder"] = m_awaitedMap;
            SendTo(host.socket, MsgMapRequest, request.Dump(0));
            m_status = "Отримуємо карту " + m_awaitedMap + "...";
        }

        if (!m_lobby.saveFile.empty() && !HasSave(m_lobby.saveFile) &&
            m_awaitedSave != m_lobby.saveFile)
        {
            m_awaitedSave = m_lobby.saveFile;
            Json request = Json::MakeObject();
            request["file"] = m_awaitedSave;
            SendTo(host.socket, MsgSaveRequest, request.Dump(0));
            m_status = "Отримуємо збереження...";
        }
    }

    bool NetSession::HasSave(const std::string& fileName) const
    {
        if (fileName.empty()) return true;
        return Paths::FileExists(Paths::Get().SavesDirectory() + "/" + fileName);
    }

    void NetSession::SendSave(Peer& peer, const std::string& fileName)
    {
        if (fileName.empty()) return;

        const std::string path = Paths::Get().SavesDirectory() + "/" + fileName;
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            WOC_LOG_WARN("A player asked for a save we do not have: ", fileName);
            return;
        }

        const std::vector<u8> data((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());

        std::vector<u8> message;
        WriteU32(message, static_cast<u32>(fileName.size()));
        message.insert(message.end(), fileName.begin(), fileName.end());
        WriteU32(message, static_cast<u32>(data.size()));
        message.insert(message.end(), data.begin(), data.end());

        SendTo(peer.socket, MsgSaveFile, message);
        WOC_LOG_INFO("Sent save ", fileName, " (", data.size(), " bytes) to ", peer.id);
    }

    void NetSession::ReceiveSaveFile(const std::vector<u8>& payload)
    {
        size_t cursor = 0;
        const std::string name = ReadString(payload, cursor);
        const u32 length = ReadU32(payload, cursor);
        if (name.empty() || cursor + length > payload.size()) return;

        const std::string directory = Paths::Get().SavesDirectory();
        if (!Paths::EnsureDirectory(directory))
        {
            WOC_LOG_ERROR("Could not make room for the received save");
            return;
        }

        std::ofstream output(directory + "/" + name, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            WOC_LOG_ERROR("Could not write the received save ", name);
            return;
        }
        output.write(reinterpret_cast<const char*>(payload.data() + cursor), length);
        WOC_LOG_INFO("Received save ", name, " (", length, " bytes)");
    }

    void NetSession::ReceiveMapFile(const std::vector<u8>& payload)
    {
        size_t cursor = 0;
        const std::string folder = ReadString(payload, cursor);
        const std::string name = ReadString(payload, cursor);
        const u32 length = ReadU32(payload, cursor);
        if (folder.empty() || name.empty() || cursor + length > payload.size()) return;

        const std::string directory = Paths::Get().MapsDirectory() + "/" + folder;
        if (!Paths::EnsureDirectory(Paths::Get().MapsDirectory()) || !Paths::EnsureDirectory(directory))
        {
            WOC_LOG_ERROR("Could not make room for the received map ", folder);
            return;
        }

        std::ofstream output(directory + "/" + name, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            WOC_LOG_ERROR("Could not write ", name, " of the received map");
            return;
        }
        output.write(reinterpret_cast<const char*>(payload.data() + cursor), length);
        ++m_awaitedFiles;
    }
}
