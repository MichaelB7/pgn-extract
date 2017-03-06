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

        /* Define a type to hold hash values of interest. */
#ifndef ECO_H
#define ECO_H

typedef struct EcoLog {
    HashCode required_hash_value;
    /* cumulative_hash_value is used to disambiguate clashing
     * final hash values in duplicate detection.
     */
    HashCode cumulative_hash_value;
    /* How deep the line is, from the half_moves associated with
     * the board when the line is played out.
     */
    unsigned half_moves;
    const char *ECO_tag;
    const char *Opening_tag;
    const char *Variation_tag;
    const char *Sub_Variation_tag;
    struct EcoLog *next;
} EcoLog;

EcoLog *eco_matches(HashCode current_hash_value, HashCode cumulative_hash_value,
                    unsigned half_moves_played);
Boolean add_ECO(Game game_details);
FILE *open_eco_output_file(EcoDivision ECO_level,const char *eco);
void initEcoTable(void);
void save_eco_details(Game game_details,unsigned number_of_moves);

#endif	// ECO_H

