#pragma once
#include "globals.h"

using namespace Constants;

#include "tetromino.h"


class TetrisController {
 public:
    explicit TetrisController(ALLEGRO_TIMER *fall);

    TetrisState Next();

    void Draw();

    bool Rotate(bool ccw);
    bool Hold();
    bool Move(bool left);
    void Fall();
    void HardFall();

    void Place();

    void NextTetromino();

    void CheckLines();
    void CheckDeath();
    void ClearLines();

    void Dying();

    static ALLEGRO_BITMAP *hold_text;
    static ALLEGRO_BITMAP *next_text;

    static ALLEGRO_BITMAP *tetrimino_textures[9];


 private:
    Board board;

    TetrisState state;

    Tetromino *falling = nullptr;
    std::deque<Tile> next_queue;
    Tile hold = Tile::NONE;
    bool last_hold = false;

    bool clearing_line = false;
    std::vector<int> lines_to_clear;

    bool dying = false;


    static bool textures_loaded;

    ALLEGRO_TIMER *fall;
    int remaining_regret_times;
};