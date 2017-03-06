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

        /* Define values for the amount of space to initially malloc
         * and incrementally realloc in a list.
         */
#ifndef LISTS_H
#define LISTS_H

#define INIT_LIST_SPACE 10
#define MORE_LIST_SPACE 5

        /* Tags to be sought may have an operator to specify the
         * relationship between value in the tag list and that in
         * the game. For instance, in order to find games before 1962
         * use Date < "1962". The < turns into a LESS_THAN operator.
         * Potentially any tag may have an operator, but not all make
         * sense in all circumstances.
         */
typedef enum {
    NONE,
    LESS_THAN, GREATER_THAN, EQUAL_TO, NOT_EQUAL_TO,
    LESS_THAN_OR_EQUAL_TO, GREATER_THAN_OR_EQUAL_TO
} TagOperator;

void extract_tag_argument(const char *argstr);
void add_tag_to_list(int tag,const char *tagstr,TagOperator operator);
Boolean check_tag_details_not_ECO(char *Details[],int num_details);
Boolean check_ECO_tag(char *Details[]);
void init_tag_lists(void);
Boolean check_setup_tag(char *Details[]);

#endif	// LISTS_H

