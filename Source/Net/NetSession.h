// NetSession.h - the multiplayer session: a lobby, then a party.
//
// The model is the ordinary one for a strategy game of this shape: deterministic lockstep,
// as in Age of Empires, StarCraft and the grand strategies. Every machine runs the whole
// simulation, in fixed ticks, identically. Only orders cross the wire. The host puts each
// order on a tick and says how far everybody may advance; the clients follow to the tick.
// Every so often the machines compare a fingerprint of the world, and a machine that has
// drifted is sent the world whole and carries on from there.
//
// A lobby is found by a five-character code rather than by an address: the host shouts the
// code over the local network once a second, and a joiner shouts the code it is looking for
// and waits for the answer. Once the two know about each other everything else is TCP.
//
// Every player listens as well as connects. That is what makes host migration possible: if
// the host disappears, the survivors agree - by a rule, with no negotiation - on which of
// them takes over, and they already know where to reach him.
#pragma once

#include "Socket.h"
#include "../Core/Json.h"
#include "../Core/Singleton.h"
#include "../Game/WorldGenerator.h"

#include <deque>
#include <future>
#include <string>
#include <vector>

namespace woc
{
    enum class NetRole : u8 { Offline, Host, Client };

    /// One person at the table. The seat is an index into the party's realms; -1 means the
    /// player has not taken one yet.
    struct LobbyPlayer
    {
        std::string id;          // eight hex characters, drawn once per process
        std::string name;
        i32 seat = -1;
        bool ready = false;
        bool host = false;

        /// Where this player listens, so that anybody can become the host later.
        u32 address = 0;
        u16 port = 0;

        Json ToJson() const;
        static LobbyPlayer FromJson(const Json& node);
    };

    /// Everything the lobby agrees on. The host owns it; the clients receive it whole
    /// whenever it changes, which is often enough to be simple and rare enough to be free.
    /// A lobby heard on the local network, for the list a joiner picks from.
    struct LobbyListing
    {
        std::string code;
        std::string name;
        std::string hostName;
        i32 players = 0;
        i32 seats = 0;
        NetAddress address;      // where its host listens
        f32 age = 0.0f;          // seconds since it was last heard
    };

    struct LobbyState
    {
        std::string code;        // five characters, the thing a player types to join
        std::string name;        // what the host called it
        std::string hostId;
        PartySettings party;
        /// Non-empty to carry on from a save instead of starting a new world.
        std::string saveFile;
        std::vector<LobbyPlayer> players;

        const LobbyPlayer* Find(const std::string& id) const;
        LobbyPlayer* Find(const std::string& id);
        /// How many realms are not held by a person - the AI count the lobby shows.
        i32 AiCount() const;

        Json ToJson() const;
        static LobbyState FromJson(const Json& node);
    };

    class NetSession final : public Singleton<NetSession>
    {
        friend class Singleton<NetSession>;
    public:
        /// Pumps the sockets. Called once a frame from the application loop, wherever the
        /// player happens to be - the lobby keeps running while the menu is on screen.
        void Update(f32 realSeconds);
        void Shutdown();

        // --- joining and leaving ------------------------------------------------------------
        /// Opens a lobby and starts advertising it. Returns the code, or empty on failure.
        std::string HostLobby(const std::string& playerName, const std::string& lobbyName = std::string());
        /// Starts looking for `code` on the local network. The answer arrives in Update.
        /// An IPv4 address ("192.168.1.20") instead of a code asks that machine directly,
        /// which works where broadcasts do not.
        bool JoinLobby(const std::string& code, const std::string& playerName);
        /// Joins a lobby from the list, going straight to the address it was heard from.
        bool JoinListing(const LobbyListing& listing, const std::string& playerName);

        /// Keeps the list of lobbies on the network fresh. Call every frame while the list
        /// is on screen; it stops by itself a moment after the calls stop.
        void Browse();
        const std::vector<LobbyListing>& Listings() const { return m_listings; }

        /// Host-only: renames the lobby for everybody.
        void SetLobbyName(const std::string& name);
        void Leave();

        NetRole Role() const { return m_role; }
        bool Active() const { return m_role != NetRole::Offline; }
        bool IsHost() const { return m_role == NetRole::Host; }
        /// The clock belongs to the host. Offline, everybody is the host of themselves.
        bool MayControlSpeed() const { return m_role != NetRole::Client; }

        const LobbyState& Lobby() const { return m_lobby; }
        LobbyState& MutableLobby() { return m_lobby; }
        const std::string& LocalPeerId() const { return m_localId; }
        const std::string& PlayerName() const { return m_playerName; }
        /// A line for the panel: what the session is doing, or why it stopped.
        const std::string& Status() const { return m_status; }

        // --- what the local player decides about himself -------------------------------------
        void SetLocalSeat(i32 seat);
        void SetLocalReady(bool ready);
        /// Host-only: everything about the party that is not a seat.
        void SetParty(const PartySettings& party);
        void SetSaveFile(const std::string& file);

        bool EveryoneReady() const;
        /// Host-only: tells everybody to load the world and start playing.
        bool StartParty();

        /// True once for the frame on which the party should begin on this machine.
        bool ConsumeStart(PartySettings& outSettings, std::string& outSaveFile);

        // --- the party itself: deterministic lockstep ------------------------------------------
        // Every machine runs the same simulation. What crosses the wire is only what the
        // players ordered and when: the host puts every order - its own and everybody else's -
        // on a tick, and tells everybody how far the world may now be advanced. Nobody sends
        // the world itself, except to repair a machine that has fallen out of step.

        /// An order, stamped with who gave it and the tick at which it takes effect.
        struct TurnOrder
        {
            u64 tick = 0;
            std::string peer;
            Json command;
        };

        /// Gives an order. A client sends it to the host; the host keeps its own in the same
        /// queue as everybody else's, so that its orders take effect at a tick too.
        void SubmitOrder(const Json& command);
        /// Host-only: every order waiting to be put on a tick, each stamped with its sender.
        void TakeOrders(std::vector<TurnOrder>& out);
        /// Host-only: the world may be advanced to `upTo`; these orders are on these ticks;
        /// these are the fingerprints of the world at those ticks. Sent every frame.
        void SendTurn(u64 upTo, i32 speedIndex, const std::vector<TurnOrder>& orders,
                      const std::vector<std::pair<u64, u64>>& checksums);

        /// Client-only: how far the host has allowed the world to go.
        u64 ConfirmedTick() const { return m_confirmedTick; }
        i32 HostSpeed() const { return m_hostSpeed; }
        /// Client-only: removes and returns the orders due at `tick`, in the host's order.
        void TakeOrdersAt(u64 tick, std::vector<TurnOrder>& out);
        /// Client-only: the host's fingerprint for `tick`, if it has sent one.
        bool ChecksumAt(u64 tick, u64& out) const;

        /// Client-only: asks the host to send the world whole, after a desynchronisation.
        void RequestResync();
        /// Host-only: true once for each round of resynchronisation requests (and after a
        /// host migration, when every machine must be brought onto the new host's world).
        bool ConsumeResyncRequest();
        /// Host-only: the whole world, to everybody.
        void SendResync(const std::string& document);
        /// Client-only: a whole world from the host, if one has arrived.
        bool ConsumeResync(Json& outDocument);

        /// True while a client is still waiting for the map - or the save - to come down
        /// the wire. Nobody may declare themselves ready until it has.
        bool WaitingForMap() const { return !m_awaitedMap.empty() || !m_awaitedSave.empty(); }
        /// What this machine is waiting on, in words, for the lobby to show.
        std::string WaitingFor() const;

    private:
        NetSession() = default;
        ~NetSession() = default;

        /// One connected player, from the host's side or the client's.
        struct Peer
        {
            TcpSocket socket;
            std::string id;
            std::vector<u8> incoming;
            bool greeted = false;
        };

        // --- plumbing ---------------------------------------------------------------------
        void PumpDiscovery(f32 realSeconds);
        /// The announcement a host shouts and answers with.
        Json Announcement() const;
        /// Records a lobby heard on the network in the browse list.
        void NoteListing(const Json& message, const NetAddress& from);
        /// Sends `text` to every discovery port: broadcast, and to the typed address if any.
        void Shout(const std::string& text);
        bool OpenBeacon();
        void PumpHost();
        void PumpClient();
        void HandleMessage(Peer& peer, u16 type, const std::vector<u8>& payload);
        static bool NextMessage(std::vector<u8>& buffer, u16& type, std::vector<u8>& payload);

        void SendTo(TcpSocket& socket, u16 type, const std::string& payload);
        void SendTo(TcpSocket& socket, u16 type, const std::vector<u8>& payload);
        void Broadcast(u16 type, const std::string& payload);

        void PublishLobby();
        void BeginHosting(const std::string& code);
        /// The host is gone: work out who takes over and go there.
        void MigrateHost();

        // --- the map ------------------------------------------------------------------------
        /// True when this machine already has the map the lobby is using.
        bool HasMap(const std::string& folder) const;
        /// Host-only: sends every file of a map folder to one peer.
        void SendMap(Peer& peer, const std::string& folder);
        void ReceiveMapFile(const std::vector<u8>& payload);
        /// The same for the save a lobby is carrying on from: a client cannot load a party
        /// out of a file only the host has.
        bool HasSave(const std::string& fileName) const;
        void SendSave(Peer& peer, const std::string& fileName);
        void ReceiveSaveFile(const std::vector<u8>& payload);
        /// Asks the host for whatever this machine is missing. Safe to call repeatedly.
        void RequestMissingFiles(Peer& host);

        NetRole m_role = NetRole::Offline;
        LobbyState m_lobby;
        std::string m_localId;
        std::string m_playerName = "Гравець";
        std::string m_status;

        TcpListener m_listener;
        UdpSocket m_beacon;
        std::vector<Peer> m_peers;      // host: the clients. client: one entry, the host.

        /// Client, while looking: the code it is hunting for and how long it has hunted.
        std::string m_seeking;
        /// Set when the player typed an address: the seek is also sent there directly.
        NetAddress m_seekAddress;
        /// How long the pending TCP connection has been trying.
        f32 m_connectTimer = 0.0f;
        /// Seconds of browsing left; Browse() tops it up.
        f32 m_browseTimer = 0.0f;
        f32 m_browseBeacon = 0.0f;
        std::vector<LobbyListing> m_listings;
        f32 m_seekTimer = 0.0f;
        f32 m_beaconTimer = 0.0f;

        /// Set by a Start message; drained by the menu, which then loads the world.
        bool m_startPending = false;
        PartySettings m_startSettings;
        std::string m_startSave;

        // --- lockstep state -------------------------------------------------------------------
        std::vector<TurnOrder> m_orders;            // host: waiting to be put on a tick
        std::deque<TurnOrder> m_scheduled;          // client: on a tick, not yet carried out
        std::vector<std::pair<u64, u64>> m_checksums;
        u64 m_confirmedTick = 0;
        i32 m_hostSpeed = 2;
        bool m_resyncRequested = false;             // host: somebody asked
        bool m_resyncAwaited = false;               // client: we asked and are waiting
        Json m_resync;
        bool m_resyncPending = false;

        /// The map and the save this machine is waiting to receive.
        std::string m_awaitedMap;
        std::string m_awaitedSave;
        i32 m_awaitedFiles = 0;
    };
}
