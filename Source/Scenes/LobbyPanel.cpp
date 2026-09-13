#include "LobbyPanel.h"
#include "SceneManager.h"

#include "../Core/Config.h"
#include "../Core/Log.h"
#include "../Core/Random.h"
#include "../Game/Map/MapGenerator.h"
#include "../Game/World/RaceDatabase.h"
#include "../Net/NetSession.h"
#include "../Platform/Input.h"
#include "../Platform/Window.h"
#include "../Render/Renderer.h"
#include "../UI/UI.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace woc
{
    void LobbyPanel::Open()
    {
        m_open = true;
        m_error.clear();
        m_inRoom = NetSession::Get().Active();
        RefreshCatalogue();

        m_palette.clear();
        for (const Json& entry : ConfigManager::Get().Game()["clanColors"].AsArray())
        {
            m_palette.push_back(static_cast<u32>(std::strtoul(entry.AsString("c8452d").c_str(), nullptr, 16)));
        }
        if (m_palette.empty()) m_palette.push_back(0xC8452D);

        UI::Get().RestartTransition("menu.lobby");
    }

    void LobbyPanel::Close()
    {
        if (!m_open) return;
        m_open = false;
        // Leaving the panel leaves the table: a lobby nobody is looking at is a lobby
        // whose host is about to be reported missing anyway.
        NetSession::Get().Leave();
        m_inRoom = false;
    }

    void LobbyPanel::RefreshCatalogue()
    {
        m_maps = MapLoader::ListMaps();
        m_saves = SaveGame::List();
    }

    void LobbyPanel::Update(f32 deltaTime)
    {
        if (m_copiedTimer > 0.0f)
        {
            m_copiedTimer -= deltaTime;
            if (m_copiedTimer <= 0.0f) m_codeCopied = false;
        }
        if (!m_open) return;

        NetSession& session = NetSession::Get();
        if (session.Active() && !session.Lobby().code.empty()) m_inRoom = true;
        if (!session.Active()) m_inRoom = false;

        if (Input::Get().WasKeyPressed(Key::Escape) && !UI::Get().WantsKeyboard()) Close();
    }

    void LobbyPanel::Draw()
    {
        Renderer& renderer = Renderer::Get();
        UI& ui = UI::Get();
        const Theme& theme = Theme::Get();

        const f32 fade = ui.Transition("menu.lobby", m_open, 0.16f);
        if (fade <= 0.001f) return;

        const Vec2 viewport = renderer.ViewportSize();
        renderer.UIRect({ 0.0f, 0.0f, viewport.x, viewport.y }, theme.shadow.WithAlpha(0.68f * fade));
        ui.BlockMouse({ 0.0f, 0.0f, viewport.x, viewport.y });

        const bool room = m_inRoom;
        const f32 width = std::min(viewport.x - 60.0f, room ? 1180.0f : 620.0f);
        const f32 height = std::min(viewport.y - 60.0f, room ? 720.0f : 700.0f);
        const Rect panel{ (viewport.x - width) * 0.5f,
                          (viewport.y - height) * 0.5f + (1.0f - fade) * 30.0f, width, height };

        const LobbyState& lobby = NetSession::Get().Lobby();
        ui.Panel(panel, room ? (lobby.name.empty() ? "Лобі" : lobby.name) + "  ·  " + lobby.code
                             : std::string("Мультиплеєр"));

        if (room) DrawRoom(panel);
        else DrawPorch(panel);
    }

    // =========================================================================================
    // The porch
    // =========================================================================================

    void LobbyPanel::DrawPorch(const Rect& panel)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        NetSession& session = NetSession::Get();

        const f32 margin = 24.0f;
        const f32 innerX = panel.x + margin;
        const f32 innerW = panel.w - margin * 2.0f;
        f32 y = panel.y + theme.headerHeight + margin;

        auto row = [&](f32 height)
        {
            const Rect r{ innerX, y, innerW, height };
            y += height + 8.0f;
            return r;
        };

        ui.Label(row(20.0f), "ВАШЕ ІМ'Я", theme.accent);
        ui.TextField(row(28.0f), "playerName", m_playerName, 24);

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 12.0f;

        ui.Label(row(20.0f), "НАЗВА ЛОБІ", theme.accent);
        {
            const Rect r = row(30.0f);
            ui.TextField({ r.x, r.y, r.w * 0.58f, r.h }, "lobbyName", m_lobbyName, 32);
            if (m_lobbyName.empty())
            {
                renderer.UIText("Лобі " + m_playerName, { r.x + 6.0f, r.y + (r.h - renderer.TextHeight()) * 0.5f },
                                theme.textDim.WithAlpha(0.6f));
            }
            if (ui.Button({ r.x + r.w * 0.61f, r.y, r.w * 0.39f, r.h }, "Створити лобі"))
            {
                const std::string code = session.HostLobby(m_playerName, m_lobbyName);
                if (code.empty()) m_error = session.Status();
                else
                {
                    // A fresh lobby starts on whatever map this machine has first, five realms
                    // deep, with the host in the first seat.
                    PartySettings party;
                    party.stateCount = 5;
                    party.humanSeat = 0;
                    party.seats.clear();
                    for (i32 i = 0; i < party.stateCount; ++i)
                    {
                        SeatSettings seat;
                        seat.color = m_palette[static_cast<size_t>(i) % m_palette.size()];
                        party.seats.push_back(seat);
                    }
                    if (!m_maps.empty()) party.mapFolder = m_maps.front().folder;
                    session.SetParty(party);
                    m_inRoom = true;
                }
            }
        }

        y += 6.0f;
        renderer.UIRect({ innerX, y, innerW, 1.0f }, theme.border);
        y += 12.0f;

        // --- what the network has to offer -------------------------------------------------
        if (!session.Active()) session.Browse();
        const std::vector<LobbyListing>& listings = session.Listings();

        ui.Label(row(20.0f), "ЛОБІ У ВАШІЙ МЕРЕЖІ", theme.accent);
        {
            const f32 listHeight = 190.0f;
            const Rect area{ innerX, y, innerW, listHeight };
            y += listHeight + 8.0f;
            renderer.UIRect(area, theme.panel.Scaled(0.85f));
            renderer.UIRectOutline(area, theme.border, 1.0f);

            const f32 entry = 44.0f;
            const Rect content = ui.BeginScroll(area.Inset(4.0f), listings.size() * entry, m_listScroll);
            if (listings.empty())
            {
                ui.LabelCentered({ content.x, content.y + 60.0f, content.w, 20.0f },
                                 "Шукаємо лобі...", theme.textDim);
            }
            for (size_t i = 0; i < listings.size(); ++i)
            {
                const LobbyListing& listing = listings[i];
                const Rect r{ content.x, content.y + i * entry, content.w, entry - 4.0f };
                if (ui.ListItem(r, "", false))
                {
                    if (!session.JoinListing(listing, m_playerName)) m_error = session.Status();
                }
                renderer.UIText(listing.name.empty() ? "Лобі " + listing.hostName : listing.name,
                                { r.x + 10.0f, r.y + 4.0f }, theme.textStrong);
                renderer.UIText("хост: " + listing.hostName + "   ·   гравців: " +
                                std::to_string(listing.players) + " / " + std::to_string(listing.seats),
                                { r.x + 10.0f, r.y + 22.0f }, theme.textDim, 0.85f);
                ui.LabelRight({ r.x, r.y + 4.0f, r.w - 10.0f, 18.0f }, listing.code, theme.accent);
                ui.TooltipIfHovered(r, "Приєднатися до лобі " + listing.code + " (" +
                                       listing.address.ToString() + ")");
            }
            ui.EndScroll(content.y + listings.size() * entry);
        }

        ui.Label(row(20.0f), "АБО ЗА КОДОМ ЧИ IP-АДРЕСОЮ ХОСТА", theme.accent);
        {
            const Rect r = row(30.0f);
            ui.TextField({ r.x, r.y, r.w * 0.58f, r.h }, "lobbyCode", m_codeText, 21);
            const bool looksLikeAddress = m_codeText.find('.') != std::string::npos;
            if (ui.Button({ r.x + r.w * 0.61f, r.y, r.w * 0.39f, r.h }, "Приєднатися",
                          m_codeText.size() == 5 || looksLikeAddress))
            {
                if (!session.JoinLobby(m_codeText, m_playerName)) m_error = session.Status();
            }
        }

        y += ui.Paragraph({ innerX, y, innerW, 0.0f },
            "Код — п'ять цифр і латинських літер. Якщо лобі не видно: обидва комп'ютери мають "
            "бути в одній мережі (не гостьовій), а гра — дозволена в брандмауері Windows для "
            "приватних мереж (вікно з запитом з'являється при першому запуску). Тоді можна "
            "ввести IP хоста — його видно в «Параметри → Мережа».",
            theme.textDim) + 4.0f;

        y += 6.0f;
        if (!session.Status().empty()) ui.Label(row(20.0f), session.Status(), theme.textDim);
        if (!m_error.empty()) ui.Label(row(20.0f), m_error, theme.negative);

        const Rect back{ panel.x + margin, panel.Bottom() - 44.0f, 180.0f, 32.0f };
        if (ui.Button(back, "Назад")) Close();
    }

    // =========================================================================================
    // The room
    // =========================================================================================

    void LobbyPanel::DrawRoom(const Rect& panel)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        NetSession& session = NetSession::Get();

        const f32 margin = 14.0f;
        const f32 top = panel.y + theme.headerHeight + margin;
        const f32 bottom = panel.Bottom() - 56.0f;
        const f32 columnWidth = (panel.w - margin * 4.0f) / 3.0f;

        DrawPlayers({ panel.x + margin, top, columnWidth, bottom - top });
        DrawParty({ panel.x + margin * 2.0f + columnWidth, top, columnWidth, bottom - top });
        DrawMySeat({ panel.x + margin * 3.0f + columnWidth * 2.0f, top, columnWidth, bottom - top });

        // --- footer ---------------------------------------------------------------------------
        const f32 buttonY = panel.Bottom() - 44.0f;
        const Rect leave{ panel.x + margin, buttonY, 160.0f, 32.0f };
        if (ui.Button(leave, "Покинути")) Close();

        const LobbyPlayer* me = session.Lobby().Find(session.LocalPeerId());
        const bool ready = me && me->ready;

        const Rect readyRect{ leave.Right() + 10.0f, buttonY, 200.0f, 32.0f };
        if (ui.HighlightButton(readyRect, ready ? "Не готовий" : "Готовий",
                               ready ? theme.warning : theme.positive,
                               !session.WaitingForMap()))
        {
            session.SetLocalReady(!ready);
        }
        if (session.WaitingForMap())
        {
            ui.TooltipIfHovered(readyRect, session.WaitingFor().empty()
                ? std::string("Спершу треба отримати файли від хоста.")
                : session.WaitingFor());
        }

        if (session.IsHost())
        {
            const bool everyone = session.EveryoneReady();
            const Rect start{ panel.Right() - margin - 240.0f, buttonY, 240.0f, 32.0f };
            if (ui.Button(start, "Почати партію", everyone))
            {
                session.StartParty();
            }
            ui.TooltipIfHovered(start, everyone
                ? "Усі за столом. Можна починати."
                : "Хтось іще не натиснув «Готовий».");
        }
        else
        {
            renderer.UIText("Чекаємо, поки хост почне партію",
                            { panel.Right() - margin - 340.0f, buttonY + 8.0f }, theme.textDim);
        }

        if (!session.Status().empty())
        {
            renderer.UIText(session.Status(), { panel.x + margin, buttonY - 22.0f }, theme.textDim);
        }
    }

    void LobbyPanel::DrawPlayers(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        NetSession& session = NetSession::Get();
        const LobbyState& lobby = session.Lobby();

        ui.Panel(area, "За столом");

        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);
        const Rect content = ui.BeginScroll(body, body.h, m_playerScroll);
        f32 y = content.y;

        const RaceDatabase& races = RaceDatabase::Get();

        for (const LobbyPlayer& player : lobby.players)
        {
            const Rect row{ content.x, y, content.w, 40.0f };
            y += 44.0f;

            renderer.UIRect(row, player.id == session.LocalPeerId() ? theme.panelAlt : theme.panel);
            renderer.UIRectOutline(row, player.ready ? theme.positive : theme.border, 1.0f);

            const std::string name = player.name + (player.host ? "  (хост)" : "");
            renderer.UIText(name, { row.x + 8.0f, row.y + 5.0f },
                            player.ready ? theme.textStrong : theme.text);

            std::string seatText = player.seat < 0
                ? std::string("без держави")
                : "держава " + std::to_string(player.seat + 1);
            if (player.seat >= 0 && player.seat < static_cast<i32>(lobby.party.seats.size()))
            {
                const SeatSettings& seat = lobby.party.seats[static_cast<size_t>(player.seat)];
                seatText += " · " + (seat.raceId.empty() ? std::string("випадковий народ")
                                                         : races.Race(seat.raceId).name);
                renderer.UIRect({ row.Right() - 26.0f, row.y + 8.0f, 22.0f, 22.0f },
                                Color::FromRGB(seat.color));
            }
            renderer.UIText(seatText, { row.x + 8.0f, row.y + 21.0f }, theme.textDim, 0.85f);
        }

        y += 8.0f;
        ui.KeyValue({ content.x, y, content.w, 22.0f }, "AI-опоненти",
                    std::to_string(lobby.AiCount()), theme.text);
        y += 26.0f;

        // The host names the table; everybody else sees the name.
        ui.Label({ content.x, y, content.w, 20.0f }, "Назва лобі", theme.textDim);
        y += 22.0f;
        if (session.IsHost())
        {
            std::string name = lobby.name;
            if (ui.TextField({ content.x, y, content.w, 26.0f }, "roomLobbyName", name, 32))
            {
                session.SetLobbyName(name);
            }
        }
        else
        {
            ui.Label({ content.x, y, content.w, 26.0f }, lobby.name, theme.textStrong);
        }
        y += 32.0f;

        // The code is the only thing a player has to get out of this screen and into
        // somebody else's ear, so it is also the only thing here with a button on it.
        ui.KeyValue({ content.x, y, content.w, 22.0f }, "Код лобі", lobby.code, theme.accent);
        y += 26.0f;
        {
            const Rect r{ content.x, y, content.w, 26.0f };
            y += 32.0f;
            if (ui.Button(r, m_codeCopied ? "Скопійовано!" : "Скопіювати код", !lobby.code.empty()))
            {
                m_codeCopied = Window::SetClipboardText(lobby.code);
                m_copiedTimer = 2.5f;
            }
            ui.TooltipIfHovered(r, "Код піде в буфер обміну — лишиться тільки надіслати.");
        }

        ui.EndScroll(y);
    }

    void LobbyPanel::DrawParty(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        NetSession& session = NetSession::Get();

        ui.Panel(area, "Умови партії");

        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);
        const Rect content = ui.BeginScroll(body, body.h * 2.0f, m_partyScroll);
        f32 y = content.y;

        const bool host = session.IsHost();
        PartySettings party = session.Lobby().party;
        bool changed = false;

        auto row = [&](f32 height)
        {
            const Rect r{ content.x, y, content.w, height };
            y += height + 6.0f;
            return r;
        };

        if (!host)
        {
            ui.Label(row(20.0f), "Умови встановлює хост", theme.textDim);
        }

        // --- realms --------------------------------------------------------------------------
        {
            const Rect r = row(28.0f);
            i32 count = party.stateCount;
            if (host && ui.Stepper(r, "Держав на карті", count, 2, 12))
            {
                party.stateCount = count;
                while (static_cast<i32>(party.seats.size()) < count)
                {
                    SeatSettings seat;
                    seat.color = m_palette[party.seats.size() % m_palette.size()];
                    party.seats.push_back(seat);
                }
                party.seats.resize(static_cast<size_t>(count));
                changed = true;
            }
            else if (!host)
            {
                ui.KeyValue(r, "Держав на карті", std::to_string(party.stateCount), theme.text);
            }
        }

        {
            const Rect r = row(26.0f);
            bool fog = party.fogOfWar;
            if (host && ui.Toggle(r, "Туман війни", fog)) { party.fogOfWar = fog; changed = true; }
            else if (!host) ui.KeyValue(r, "Туман війни", fog ? "так" : "ні", theme.text);
        }

        {
            const Rect r = row(26.0f);
            bool bandits = party.bandits.enabled;
            if (host && ui.Toggle(r, "Розбійники", bandits))
            {
                party.bandits.enabled = bandits;
                changed = true;
            }
            else if (!host) ui.KeyValue(r, "Розбійники", bandits ? "так" : "ні", theme.text);

            if (host && party.bandits.enabled &&
                ui.Slider(row(26.0f), "Скільки їх", party.bandits.density, 0.0f, 1.0f))
            {
                changed = true;
            }
        }

        y += 6.0f;
        renderer.UIRect({ content.x, y, content.w, 1.0f }, theme.border);
        y += 10.0f;

        // --- the map --------------------------------------------------------------------------
        ui.Label(row(20.0f), "КАРТА", theme.accent);
        ui.KeyValue(row(22.0f), "Обрано",
                    party.mapFolder.empty() ? "немає" : party.mapFolder, theme.textStrong);

        if (host)
        {
            // A party is either a new world or an old one carried on - never a bit of each.
            // The switch is up here so the two lists below can never both be live.
            const bool continuing = !session.Lobby().saveFile.empty();
            {
                const Rect r = row(26.0f);
                const f32 half = (r.w - 6.0f) * 0.5f;
                if (ui.ListItem({ r.x, r.y, half, r.h }, "Нова партія", !continuing) && continuing)
                {
                    session.SetSaveFile(std::string());
                }
                if (ui.ListItem({ r.x + half + 6.0f, r.y, half, r.h }, "Зі збереження", continuing) &&
                    !continuing && !m_saves.empty())
                {
                    session.SetSaveFile(m_saves.front().fileName);
                }
            }

            if (!continuing)
            {
                // Which world the button will make. The host picks it here and everybody
                // plays on whatever comes out, because the map itself is then sent round.
                ui.Label(row(18.0f), "Який світ генерувати:", theme.textDim);
                const std::vector<MapGenPreset>& presets = MapGenerator::Presets();
                for (size_t i = 0; i < presets.size(); ++i)
                {
                    const Rect r = row(24.0f);
                    if (ui.ListItem(r, presets[i].name, static_cast<i32>(i) == m_selectedPreset))
                    {
                        m_selectedPreset = static_cast<i32>(i);
                    }
                    ui.TooltipIfHovered(r, presets[i].description);
                }

                const Rect generate = row(28.0f);
                if (ui.Button(generate, "Згенерувати нову карту"))
                {
                    GenerateMapNow();
                    return;   // the lobby was republished; the rest is redrawn next frame
                }
                ui.TooltipIfHovered(generate,
                    "Світ буде створено одразу й записано до теки Maps — і лишиться там, "
                    "тож на ньому можна буде й далі грати, і завантажити з нього збереження.\n"
                    "Гравці, у яких його немає, отримають його від вас самі.");

                ui.Label(row(18.0f), "Або одна з ваших карт:", theme.textDim);
                for (const MapDescription& map : m_maps)
                {
                    const Rect r = row(24.0f);
                    if (ui.ListItem(r, map.name, map.folder == party.mapFolder))
                    {
                        party.mapFolder = map.folder;
                        party.generateMap = false;
                        changed = true;
                    }
                }
            }
            else
            {
                ui.Label(row(18.0f), "Карту визначає збереження.", theme.textDim);

                if (m_saves.empty())
                {
                    ui.Label(row(22.0f), "Збережень немає", theme.negative);
                }
                for (const SaveSlot& slot : m_saves)
                {
                    const Rect r = row(24.0f);
                    // A save whose map is gone cannot be carried on by anybody, so it is
                    // shown as unavailable rather than as a trap.
                    const bool playable = MapLoader::ReadDescription(slot.mapFolder, m_probe);
                    if (ui.ListItem(r, playable ? slot.name : slot.name + "  (карти немає)",
                                    slot.fileName == session.Lobby().saveFile) && playable)
                    {
                        session.SetSaveFile(slot.fileName);
                    }
                    ui.TooltipIfHovered(r, playable
                        ? slot.dateText + "\n" + slot.realmName + "\nКарта: " + slot.mapFolder
                        : "Карти «" + slot.mapFolder + "» більше немає — це збереження не відкрити.");
                }
            }
        }
        else if (!session.Lobby().saveFile.empty())
        {
            ui.KeyValue(row(22.0f), "Продовження", session.Lobby().saveFile, theme.text);
        }

        if (changed) session.SetParty(party);
        ui.EndScroll(y);
    }

    void LobbyPanel::DrawMySeat(const Rect& area)
    {
        UI& ui = UI::Get();
        Renderer& renderer = Renderer::Get();
        const Theme& theme = Theme::Get();
        NetSession& session = NetSession::Get();

        ui.Panel(area, "Ваша держава");

        const Rect body = Rect{ area.x, area.y + theme.headerHeight, area.w, area.h - theme.headerHeight }
                              .Inset(theme.padding);
        const Rect content = ui.BeginScroll(body, body.h * 1.4f, m_seatScroll);
        f32 y = content.y;

        auto row = [&](f32 height)
        {
            const Rect r{ content.x, y, content.w, height };
            y += height + 6.0f;
            return r;
        };

        const LobbyPlayer* me = session.Lobby().Find(session.LocalPeerId());
        if (!me)
        {
            ui.Label(row(22.0f), "Чекаємо на хоста...", theme.textDim);
            ui.EndScroll(y);
            return;
        }

        // --- which realm ------------------------------------------------------------------
        ui.Label(row(20.0f), "ЯКУ ДЕРЖАВУ БЕРЕТЕ", theme.accent);
        for (i32 i = 0; i < session.Lobby().party.stateCount; ++i)
        {
            const Rect r = row(24.0f);
            const LobbyPlayer* holder = nullptr;
            for (const LobbyPlayer& player : session.Lobby().players)
            {
                if (player.seat == i) { holder = &player; break; }
            }

            const bool mine = holder && holder->id == me->id;
            const std::string label = "Держава " + std::to_string(i + 1) +
                (holder && !mine ? "  — " + holder->name : "");
            const auto& seats = session.Lobby().party.seats;
            const Color banner = i < static_cast<i32>(seats.size())
                ? Color::FromRGB(seats[static_cast<size_t>(i)].color) : theme.textDim;
            // The banner beside the name, as in the single-player screen: a colour is how
            // a player recognises his realm on the map, so it is how he picks it here too.
            if (ui.ListItem({ r.x, r.y, r.w - 30.0f, r.h }, label, mine, banner) && !holder)
            {
                session.SetLocalSeat(i);
            }
            renderer.UIRect({ r.Right() - 24.0f, r.y + 3.0f, 20.0f, r.h - 6.0f }, banner);
            renderer.UIRectOutline({ r.Right() - 24.0f, r.y + 3.0f, 20.0f, r.h - 6.0f }, theme.border, 1.0f);
            if (holder && !mine) ui.TooltipIfHovered(r, "Цю державу вже взяв інший гравець.");
        }

        if (me->seat < 0 || me->seat >= static_cast<i32>(session.Lobby().party.seats.size()))
        {
            ui.Label(row(22.0f), "Оберіть державу, щоб налаштувати народ.", theme.textDim);
            ui.EndScroll(y);
            return;
        }

        // Edited locally and sent to the host together with the readiness, so a client's
        // people and banner are its own decision and nobody else's.
        PartySettings party = session.Lobby().party;
        SeatSettings& seat = party.seats[static_cast<size_t>(me->seat)];
        bool changed = false;

        y += 6.0f;
        renderer.UIRect({ content.x, y, content.w, 1.0f }, theme.border);
        y += 10.0f;

        ui.Label(row(20.0f), "НАРОД", theme.accent);
        {
            const Rect r = row(26.0f);
            if (ui.ListItem(r, "Випадковий", seat.raceId.empty()))
            {
                seat.raceId.clear();
                changed = true;
            }
        }
        for (const RaceInfo& race : RaceDatabase::Get().Races())
        {
            const Rect r = row(32.0f);
            if (ui.ListItem(r, race.name, seat.raceId == race.id, race.color))
            {
                seat.raceId = race.id;
                changed = true;
            }
            renderer.UISprite(race.sprite, { r.Right() - 30.0f, r.y + 2.0f, 28.0f, 28.0f }, race.color);
            ui.TooltipIfHovered(r, race.description);
        }

        y += 6.0f;
        ui.Label(row(20.0f), "ПРАПОР", theme.accent);

        const f32 swatch = 28.0f;
        const f32 gap = 4.0f;
        const size_t columns = 6;
        const size_t rows = (m_palette.size() + columns - 1) / columns;
        for (size_t i = 0; i < m_palette.size(); ++i)
        {
            const Rect box{ content.x + (i % columns) * (swatch + gap),
                            y + (i / columns) * (swatch + gap), swatch, swatch };
            if (ui.ColorSwatch(box, Color::FromRGB(m_palette[i]), m_palette[i] == seat.color,
                               "lobbySwatch" + std::to_string(i)))
            {
                seat.color = m_palette[i];
                changed = true;
            }
        }
        y += rows * (swatch + gap) + 10.0f;

        if (changed)
        {
            // The host owns the document, so a client's choice travels as a seat update;
            // the host's own choice is simply written in.
            session.MutableLobby().party = party;
            if (session.IsHost()) session.SetParty(party);
            else session.SetLocalReady(me->ready);
        }

        ui.EndScroll(y);
    }

    void LobbyPanel::GenerateMapNow()
    {
        NetSession& session = NetSession::Get();
        if (!session.IsHost()) return;

        // Whatever the host last shaped in the party screen or the editor is the ground
        // this stands on; the preset then overrides the shape but not the size.
        const std::vector<MapGenPreset>& presets = MapGenerator::Presets();
        MapGenSettings settings = MapGenerator::Shared();
        if (m_selectedPreset >= 0 && m_selectedPreset < static_cast<i32>(presets.size()))
        {
            const u32 keptWidth = settings.width;
            const u32 keptHeight = settings.height;
            settings = presets[static_cast<size_t>(m_selectedPreset)].settings;
            settings.width = keptWidth;
            settings.height = keptHeight;
        }

        settings.seed = 0;   // a fresh world every time the button is pressed

        // One island per realm means per realm in *this* lobby.
        if (settings.islands >= 2)
        {
            settings.islands = std::max(2, NetSession::Get().Lobby().party.stateCount);
        }

        MapDescription made;
        const std::string folder = MapGenerator::GenerateAndStore(settings, made);
        if (folder.empty())
        {
            m_error = "Карту згенерувати не вдалося";
            return;
        }

        PartySettings party = session.Lobby().party;
        party.mapFolder = folder;
        party.generateMap = false;   // it is a real map now, and travels like one
        session.SetParty(party);

        RefreshCatalogue();
        WOC_LOG_INFO("Lobby map generated: ", folder);
    }
}
