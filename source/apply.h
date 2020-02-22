/*
 *  This file is part of pgn-extract: a Portable Game Notation (PGN) extractor.
 *  Copyright (C) 1994-2019 David J. Barnes
 *
 *  pgn-extract is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  pgn-extract is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with pgn-extract. If not, see <http://www.gnu.org/licenses/>.
 *
 *  David J. Barnes may be contacted as d.j.barnes@kent.ac.uk
 *  https://www.cs.kent.ac.uk/people/staff/djb/
 */

#ifndef APPLY_H
#define APPLY_H

void store_hash_value(Move *move_details,const char *fen);
Boolean save_polyglot_hashcode(const char *value);
Boolean apply_move_list(Game *game_details,unsigned *plycount, unsigned max_depth);
Boolean apply_move(Move *move_details, Board *board);
Boolean apply_eco_move_list(Game *game_details,unsigned *number_of_half_moves);
Board *rewrite_game(Game *game_details);
char *copy_string(const char *str);
void free_board(Board *board);
Board *new_game_board(const char *fen);
Board *new_fen_board(const char *fen);
/* letters should contain a string of the form: "PNBRQK" */
void set_output_piece_characters(const char *letters);
char coloured_piece_to_SAN_letter(Piece coloured_piece);
void build_basic_EPD_string(const Board *board,char *fen);
Piece convert_FEN_char_to_piece(char c);
char SAN_piece_letter(Piece piece);
const char *piece_str(Piece piece);
char *get_FEN_string(const Board *board);
CommentList *create_match_comment(const Board *board);

#endif	// APPLY_H

