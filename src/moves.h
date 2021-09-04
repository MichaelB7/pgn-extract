/*
 *  This file is part of pgn-extract: a Portable Game Notation (PGN) extractor.
 *  Copyright (C) 1994-2021 David J. Barnes
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

#ifndef MOVES_H
#define MOVES_H

void add_positional_variations_from_file(FILE *fpin);
void add_positional_variation_from_line(char *line);
void add_textual_variations_from_file(FILE *fpin);
void add_textual_variation_from_line(char *line);
Boolean check_textual_variations(const Game *game_details);
Boolean check_move_bounds(unsigned plycount);
void add_fen_positional_match(const char *fen_string);
void add_fen_pattern_match(const char *fen_pattern, Boolean add_reverse, const char *label);
Boolean check_for_only_checkmate(const Game *game_details);
Boolean check_for_only_stalemate(const Board *board, const Move *moves);
Boolean is_stalemate(const Board *board, const Move *moves);

#endif	// MOVES_H

