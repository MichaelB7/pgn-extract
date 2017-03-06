/*
 *  Program: pgn-extract: a Portable Game Notation (PGN) extractor.
 *  Copyright (C) 1994-2017 David Barnes
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  David Barnes may be contacted as D.J.Barnes@kent.ac.uk
 *  https://www.cs.kent.ac.uk/people/staff/djb/
 *
 */

#ifndef OUTPUT_H
#define OUTPUT_H

void output_game(Game current_game,FILE *outputfile);
void print_str(FILE *fp, const char *str);
void terminate_line(FILE *fp);
OutputFormat which_output_format(const char *arg);
const char *output_file_suffix(OutputFormat format);
void add_to_output_tag_order(TagName tag);
void set_output_line_length(unsigned max);

/* Provide enough static space to build FEN string. */
#define FEN_SPACE 100

#endif	// OUTPUT_H

