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

#ifndef OUTPUT_H
#define OUTPUT_H

void format_game(Game *current_game,FILE *outputfile);
void print_str(FILE *fp, const char *str);
void terminate_line(FILE *fp);
OutputFormat which_output_format(const char *arg);
const char *output_file_suffix(OutputFormat format);
void add_to_output_tag_order(TagName tag);
void set_output_line_length(unsigned max);
void add_plycount(const Game *game);
void add_total_plycount(const Game *game, Boolean count_variations);
/* Provide enough static space to build FEN string. */
#define FEN_SPACE 100

#endif	// OUTPUT_H

