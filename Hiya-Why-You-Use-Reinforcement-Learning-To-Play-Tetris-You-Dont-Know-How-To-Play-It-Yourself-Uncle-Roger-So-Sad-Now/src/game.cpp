#include "game.h"

using namespace Constants;

#include <map>
#include <thread>
#include "window.h"

bool Game::textures_loaded = false;
bool Game::client_running = false;

void server_process(Server *server) {
    while (server && server->running)
        server->handle();
}

void client_process(Client *client, Game *game) {
    while (client && client->running) {
        if (!client->handle()) {
            if (game->status == GameStatus::PLAYING)
               game->EndGame(GameResult::DISCONNECT);
            break;
        }
    }
    delete client;
}

//void *server_process(ALLEGRO_THREAD *t, void *arg) {
//    auto *server = (Server *)arg;
//    while (server->running) {
//        server->handle();
//        usleep(1000);
//    }
//    delete server;
//
//    return nullptr;
//}
//
//void *client_process(ALLEGRO_THREAD *t, void *arg) {
//    auto *client = (Client *) arg;
//    while (client->running){
//        client->handle();
//        usleep(1000);
//    }
//    delete client;
//
//    return nullptr;
//}

Game::Game(GameType type, ALLEGRO_DISPLAY *display, ALLEGRO_TIMER *tick, char name[], char host[]): name(std::string(name)) {
    eventQueue = al_create_event_queue();
    if (!eventQueue)
        FATAL("Failed to create event queue!");

    if (!al_init_primitives_addon())
        FATAL("Primitives addon init failed!");

    fall = al_create_timer(FALL_TIME);
    if (!fall)
        FATAL("Fall timer create failed");

    das = al_create_timer(DAS_INTERVAL_SECONDS);
    if (!das)
        FATAL("DAS timer create failed");

    init_colors();

    al_register_event_source(eventQueue, al_get_display_event_source(display));
    al_register_event_source(eventQueue, al_get_timer_event_source(tick));
    al_register_event_source(eventQueue, al_get_timer_event_source(fall));
    al_register_event_source(eventQueue, al_get_timer_event_source(das));
    al_register_event_source(eventQueue, al_get_keyboard_event_source());

    gameType = type;
    is_multi = (type == GameType::MULTI_HOST || type == GameType::MULTI_CLIENT);

    if (type == GameType::MULTI_HOST) {
        server = new Server(SERVER_PORT, *this);
        //        ALLEGRO_THREAD *server_thread = al_create_thread(server_process, server);
        //        al_start_thread(server_thread);
        server_thread = std::thread(server_process, server);
    }
    //
    if (is_multi) {
        if (type == GameType::MULTI_HOST)
            client = new Client(server->master_fd, *this);
        else {
//            char host[] = "192.168.89.68";
            client = new Client(host, 7122, *this);
            client_thread = std::thread(client_process, client, this);
        }
        //        ALLEGRO_THREAD *client_thread = al_create_thread(client_process, client);
        //        al_start_thread(client_thread);
    }

    tc = new TetrisController(fall, *this);

}

Game::~Game() {
    al_destroy_event_queue(eventQueue);
    delete tc;

    if (client != nullptr) {
        client->Stop();
        if (gameType == GameType::MULTI_CLIENT)
            client_thread.detach();
        else if (gameType == GameType::MULTI_HOST)
            delete client, client = nullptr;
    }

    if (server != nullptr) {
        server->Stop();
        while (server);
        server_thread.detach();
    }
}

GameResult Game::Start() {
    ALLEGRO_EVENT event;

    INFO(name)
    INFO(name.length());
    if (is_multi)
        client->SendRegister(name);

    // keycode -> holding, last_hold_time
    std::map<int, std::pair<bool, double>> das_map;


    for (;;) {
        al_wait_for_event(eventQueue, &event);
        switch (event.type) {
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                return GameResult::EXIT;

            case ALLEGRO_EVENT_KEY_DOWN:
                if (status == GameStatus::END && event.keyboard.keycode == ALLEGRO_KEY_ENTER) {
                    if (gameType == GameType::MULTI_HOST && client->players_alive.size() > 1)
                        break;
                    return result;
                }
                handleKeyPress(event.keyboard.keycode);

                switch (event.keyboard.keycode) {
                    case ALLEGRO_KEY_LEFT:
                    case ALLEGRO_KEY_RIGHT:
                    case ALLEGRO_KEY_DOWN:
                        das_map[event.keyboard.keycode] = {true, al_get_time()};
                }
                break;

            case ALLEGRO_EVENT_KEY_UP:
                switch (event.keyboard.keycode) {
                    case ALLEGRO_KEY_LEFT:
                    case ALLEGRO_KEY_RIGHT:
                    case ALLEGRO_KEY_DOWN:
                        das_map[event.keyboard.keycode].first = false;
                }
                break;

            case ALLEGRO_EVENT_TIMER:
                if (event.timer.source == fall) {
                    tc->Next();
                } else if (event.timer.source == das) {
                    for (auto &[keycode, val] : das_map) {
                        auto &[holding, last_hold_time] = val;
                        if (holding) {
                            if (al_get_time() - last_hold_time >= DAS_HOLD_SECONDS) {
                                handleKeyPress(keycode);
                            }
                        }
                    }
                } else {// tick
                    updateScreen();
                }
                break;
        }
    }
}

void Game::handleKeyPress(int keycode) {
    if (status == GameStatus::PLAYING) {
        if (keycode == ALLEGRO_KEY_RIGHT)
            tc->Move(false);
        else if (keycode == ALLEGRO_KEY_LEFT)
            tc->Move(true);
        else if (keycode == ALLEGRO_KEY_UP)
            tc->Rotate(false);
        else if (keycode == ALLEGRO_KEY_DOWN) {
            al_play_sample(Window::se_move, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, nullptr);
            tc->Fall();
        } else if (keycode == ALLEGRO_KEY_SPACE)
            tc->HardFall();
        else if (keycode == ALLEGRO_KEY_C)
            tc->Hold();
        else if (keycode == ALLEGRO_KEY_Z)
            tc->Rotate(true);
    } else if (status == GameStatus::PENDING) {
        if (keycode == ALLEGRO_KEY_ENTER) {
            if (gameType == GameType::MULTI_HOST)
                server->SendGameStart();
            else if (gameType == GameType::SINGLE)
                StartGame();
        }
    }
}

void Game::StartGame() {
    al_start_timer(fall);
    al_start_timer(das);
    status = GameStatus::PLAYING;
    al_play_sample(Window::gameplay_bgm, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_LOOP, &Window::gameplay_sampid);

    //send_line_animations.emplace_back(new SendLineAnimation(WINDOW_WIDTH, WINDOW_HEIGHT, 0, 0));
}

void Game::updateScreen() {
    drawBackground();
    if (status == GameStatus::PLAYING)
        tc->Draw();

    if (is_multi && client->id != -1)
        drawMulti();

    drawTexts();

    drawAnimations();

    al_flip_display();
}

void Game::drawBackground() {

    /// Background
    //al_draw_filled_rectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, BACKGROUND_COLOR);
    al_draw_scaled_bitmap(Window::gameplay_bg, 0, 0,
                          al_get_bitmap_width(Window::gameplay_bg), al_get_bitmap_height(Window::gameplay_bg),
                          0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
                          0);

    /// Gameplay Area
    // Background
    al_draw_filled_rectangle(GAMEPLAY_X, GAMEPLAY_Y,
                             GAMEPLAY_X + GAMEPLAY_WIDTH, GAMEPLAY_Y + GAMEPLAY_HEIGHT,
                             GAMEPLAY_BG_COLOR);
    // Girds
    for (int i = 0; i < TILE_COUNT_V; i++)
        al_draw_line(GAMEPLAY_X, GAMEPLAY_Y + i * TILE_SIZE,
                     GAMEPLAY_X + GAMEPLAY_WIDTH, GAMEPLAY_Y + i * TILE_SIZE,
                     GIRD_COLOR, GIRD_WIDTH);
    for (int i = 0; i < TILE_COUNT_H; i++)
        al_draw_line(GAMEPLAY_X + i * TILE_SIZE, GAMEPLAY_Y,
                     GAMEPLAY_X + i * TILE_SIZE, GAMEPLAY_Y + GAMEPLAY_HEIGHT,
                     GIRD_COLOR, GIRD_WIDTH);
    // Border
    al_draw_rounded_rectangle(GAMEPLAY_X - BORDER_OUTER_WIDTH / 2.0, GAMEPLAY_Y - BORDER_OUTER_WIDTH / 2.0,
                              GAMEPLAY_X + GAMEPLAY_WIDTH + BORDER_OUTER_WIDTH / 2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT + BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_WIDTH / 2.0, BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_COLOR, BORDER_OUTER_WIDTH);
    al_draw_rounded_rectangle(GAMEPLAY_X - BORDER_INNER_WIDTH / 2.0, GAMEPLAY_Y - BORDER_INNER_WIDTH / 2.0,
                              GAMEPLAY_X + GAMEPLAY_WIDTH + BORDER_INNER_WIDTH / 2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT + BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_WIDTH / 2.0, BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_COLOR, BORDER_INNER_WIDTH);

    /// Hold Area
    // Background
    al_draw_filled_rectangle(HOLDAREA_X, HOLDAREA_Y,
                             HOLDAREA_X + HOLDAREA_SIZE, HOLDAREA_Y + HOLDAREA_SIZE,
                             GAMEPLAY_BG_COLOR);
    // Border
    al_draw_rounded_rectangle(HOLDAREA_X - BORDER_OUTER_WIDTH / 2.0, HOLDAREA_Y - BORDER_OUTER_WIDTH / 2.0,
                              HOLDAREA_X + HOLDAREA_SIZE + BORDER_OUTER_WIDTH / 2.0, HOLDAREA_Y + HOLDAREA_SIZE + BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_WIDTH / 2.0, BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_COLOR, BORDER_OUTER_WIDTH);
    al_draw_rounded_rectangle(HOLDAREA_X - BORDER_INNER_WIDTH / 2.0, HOLDAREA_Y - BORDER_INNER_WIDTH / 2.0,
                              HOLDAREA_X + HOLDAREA_SIZE + BORDER_INNER_WIDTH / 2.0, HOLDAREA_Y + HOLDAREA_SIZE + BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_WIDTH / 2.0, BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_COLOR, BORDER_INNER_WIDTH);

    /// Garbage buffer
    // Background
    al_draw_filled_rectangle(GARBAGE_BUFFER_X, GARBAGE_BUFFER_Y,
                             GARBAGE_BUFFER_X + GARBAGE_BUFFER_WIDTH, GARBAGE_BUFFER_Y + GARBAGE_BUFFER_HEIGHT,
                             GAMEPLAY_BG_COLOR);
    // Border
    al_draw_rounded_rectangle(GARBAGE_BUFFER_X - BORDER_OUTER_WIDTH / 2.0, GARBAGE_BUFFER_Y - BORDER_OUTER_WIDTH / 2.0,
                              GARBAGE_BUFFER_X + GARBAGE_BUFFER_WIDTH + BORDER_OUTER_WIDTH / 2.0, GARBAGE_BUFFER_Y + GARBAGE_BUFFER_HEIGHT + BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_WIDTH / 2.0, BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_COLOR, BORDER_OUTER_WIDTH);
    al_draw_rounded_rectangle(GARBAGE_BUFFER_X - BORDER_INNER_WIDTH / 2.0, GARBAGE_BUFFER_Y - BORDER_INNER_WIDTH / 2.0,
                              GARBAGE_BUFFER_X + GARBAGE_BUFFER_WIDTH + BORDER_INNER_WIDTH / 2.0, GARBAGE_BUFFER_Y + GARBAGE_BUFFER_HEIGHT + BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_WIDTH / 2.0, BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_COLOR, BORDER_INNER_WIDTH);

    //    for (int y = GARBAGE_BUFFER_Y + GARBAGE_BUFFER_HEIGHT; y >= GARBAGE_BUFFER_Y; y -= TILE_SIZE)
    //        al_draw_line(GARBAGE_BUFFER_X, y,
    //                     GARBAGE_BUFFER_X + GARBAGE_BUFFER_WIDTH, y,
    //                     GIRD_COLOR, GIRD_WIDTH);

    /// Preview area
    // Background
    al_draw_filled_rectangle(PREVIEW_AREA_X, PREVIEW_AREA_Y,
                             PREVIEW_AREA_X + PREVIEW_AREA_WIDTH, PREVIEW_AREA_Y + PREVIEW_AREA_HEIGHT,
                             GAMEPLAY_BG_COLOR);
    // Border
    al_draw_rounded_rectangle(PREVIEW_AREA_X - BORDER_OUTER_WIDTH / 2.0, PREVIEW_AREA_Y - BORDER_OUTER_WIDTH / 2.0,
                              PREVIEW_AREA_X + PREVIEW_AREA_WIDTH + BORDER_OUTER_WIDTH / 2.0, PREVIEW_AREA_Y + PREVIEW_AREA_HEIGHT + BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_WIDTH / 2.0, BORDER_OUTER_WIDTH / 2.0,
                              BORDER_OUTER_COLOR, BORDER_OUTER_WIDTH);
    al_draw_rounded_rectangle(PREVIEW_AREA_X - BORDER_INNER_WIDTH / 2.0, PREVIEW_AREA_Y - BORDER_INNER_WIDTH / 2.0,
                              PREVIEW_AREA_X + PREVIEW_AREA_WIDTH + BORDER_INNER_WIDTH / 2.0, PREVIEW_AREA_Y + PREVIEW_AREA_HEIGHT + BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_WIDTH / 2.0, BORDER_INNER_WIDTH / 2.0,
                              BORDER_INNER_COLOR, BORDER_INNER_WIDTH);

}

void Game::drawMulti() const {
    if (!client_running)
        return;
    auto &players = client->players;
    auto &player_list = client->player_list;

    for (int p = 0; p < player_list.size(); p++) {
        const int fd = player_list[p];
        auto &[player_name, player_board, player_alive] = players[fd];
        al_draw_filled_rectangle(MULTI_X[p], MULTI_Y[p],
                                 MULTI_X[p] + MULTI_WIDTH, MULTI_Y[p] + MULTI_HEIGHT,
                                 MULTI_BG_COLOR);

        for (int i = 0; i < TILE_COUNT_V; i++) {
            for (int j = 0; j < TILE_COUNT_H; j++) {
                if (player_board[i][j] == Tile::NONE)
                    continue;
                al_draw_tinted_scaled_bitmap(TetrisController::tetrimino_textures[int(player_board[i][j])],
                                             al_map_rgba_f(TETROMINO_BOARD_ALPHA, TETROMINO_BOARD_ALPHA, TETROMINO_BOARD_ALPHA, TETROMINO_BOARD_ALPHA),
                                             0, 0, TETROMINO_BLOCK_TEXTURE_SIZE, TETROMINO_BLOCK_TEXTURE_SIZE,
                                             MULTI_X[p] + MULTI_TILE_SIZE * j, MULTI_Y[p] + MULTI_TILE_SIZE * (TILE_COUNT_V - i - 1),
                                             MULTI_TILE_SIZE, MULTI_TILE_SIZE,
                                             0);
            }
        }

        if (!player_alive) {
            al_draw_filled_rectangle(MULTI_X[p], MULTI_Y[p],
                                     MULTI_X[p] + MULTI_WIDTH, MULTI_Y[p] + MULTI_HEIGHT,
                                     al_map_rgba(20, 20, 20, 150));
        }
        al_draw_text(Window::AirStrike30, al_map_rgb(210, 210, 210),
                     MULTI_X[p] + MULTI_WIDTH / 2.0,
                     MULTI_Y[p] + MULTI_HEIGHT,
                     ALLEGRO_ALIGN_CENTER, player_name.c_str());
    }
}

void Game::drawTexts() const {
    if (status == GameStatus::PENDING) {
        if (gameType == GameType::MULTI_CLIENT) {
            al_draw_multiline_text(Window::AirStrike40, TEXT_COLOR,
                                   GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/2.0,
                                   GAMEPLAY_WIDTH, 40,
                                   ALLEGRO_ALIGN_CENTER, "Waiting host to start game");
        } else {
            al_draw_multiline_text(Window::AirStrike40, TEXT_COLOR,
                                   GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/2.0,
                                   GAMEPLAY_WIDTH, 40,
                                   ALLEGRO_ALIGN_CENTER, "Press ENTER to\nstart game");
        }
    } else if (status == GameStatus::END) {
        if (is_multi) {
            if (result == GameResult::LOSE) {
                al_draw_multiline_textf(Window::AirStrike40, TEXT_COLOR,
                                        GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/2.0,
                                        GAMEPLAY_WIDTH, 40,
                                        ALLEGRO_ALIGN_CENTER, "You died!\nYou are %d%s place!",
                                        place, place == 2? "nd": place == 3? "th": "rd");
            } else if (result == GameResult::WIN) {
                al_draw_multiline_text(Window::AirStrike40, TEXT_COLOR,
                                        GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/2.0,
                                        GAMEPLAY_WIDTH, 40,
                                        ALLEGRO_ALIGN_CENTER, "You WIN!\nYou are 1st place!");
            } else if (result == GameResult::DISCONNECT) {
                al_draw_multiline_text(Window::AirStrike40, TEXT_COLOR,
                                       GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/2.0,
                                       GAMEPLAY_WIDTH, 40,
                                       ALLEGRO_ALIGN_CENTER, "Disconnected from server!");
            }
        } else {
            al_draw_multiline_text(Window::AirStrike40, TEXT_COLOR,
                                    GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/2.0,
                                    GAMEPLAY_WIDTH, 40,
                                    ALLEGRO_ALIGN_CENTER, "Game over!");
        }

        if (gameType == GameType::MULTI_HOST && client->players_alive.size() > 1) {
            al_draw_multiline_text(Window::AirStrike30, TEXT_COLOR,
                                   GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/4.0*3,
                                   GAMEPLAY_WIDTH, 40,
                                   ALLEGRO_ALIGN_CENTER, "Waiting for game to finish...");
        } else {
            al_draw_multiline_text(Window::AirStrike30, TEXT_COLOR,
                                   GAMEPLAY_X + GAMEPLAY_WIDTH/2.0, GAMEPLAY_Y + GAMEPLAY_HEIGHT/4.0*3,
                                   GAMEPLAY_WIDTH, 40,
                                   ALLEGRO_ALIGN_CENTER, "Press ENTER to return to menu");
        }
    }
}

void Game::ReceiveAttack(int attacker, int lines) {
    if (status == GameStatus::PLAYING) {
        const int nth = std::find(client->player_list.begin(), client->player_list.end(), attacker) - client->player_list.begin();
        const int dx = GAMEPLAY_X + GAMEPLAY_WIDTH / 2;
        const int dy = GAMEPLAY_Y + GAMEPLAY_HEIGHT / 2;
        const int sx = MULTI_X[nth] + MULTI_WIDTH/2;
        const int sy = MULTI_Y[nth] + MULTI_HEIGHT/2;
        send_line_animations.emplace_back(new SendLineAnimation(sx, sy, dx, dy));
        tc->ReceiveAttack(lines);
    }
}

void Game::drawAnimations() {
    for (auto it = send_line_animations.begin(); it != send_line_animations.end(); ) {
        INFO("Start  animation");

        if ((*it)->NextFrame()) {
            delete *it;
            al_play_sample(Window::se_attack, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, nullptr);
            it = send_line_animations.erase(it);
        } else
            it++;
    }
}

void Game::EndGame(GameResult res, int pl) {
    result = res;
    place = pl;

    status = GameStatus::END;

    if (res == GameResult::WIN) {
        al_stop_sample(&Window::gameplay_sampid);
        al_play_sample(Window::me_win, 1.0, 0.0, 1.0, ALLEGRO_PLAYMODE_ONCE, nullptr);
    }

    al_stop_timer(das);
    al_stop_timer(fall);
}
