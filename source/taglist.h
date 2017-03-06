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
        /* Define indices for the set of pre-defined tags.
         * Higher values are created dynamically as new
         * tags are recognised in the source files.
         */

    /* List the tags so that the strings that they represent
     * would be in alphabetical order. E.g. note that EVENT_TAG and
     * EVENT_DATE_TAG should be in this order because the strings are
     * "Event" and "EventDate".
     */
#ifndef TAGLIST_H
#define TAGLIST_H

typedef enum {
    ANNOTATOR_TAG,
    BLACK_TAG,
    BLACK_ELO_TAG,
    BLACK_NA_TAG,
    BLACK_TITLE_TAG,
    BLACK_TYPE_TAG,
    BLACK_USCF_TAG,
    BOARD_TAG,
    DATE_TAG,
    ECO_TAG,
    /* The PSEUDO_ELO_TAG is not a real PGN one.  It is used with the -t
     * argument so that it becomes possible to indicate a rating of either colour.
     */
    PSEUDO_ELO_TAG,
    EVENT_TAG,
    EVENT_DATE_TAG,
    EVENT_SPONSOR_TAG,
    FEN_TAG,
    /* The PSEUDO_FEN_PATTERN_TAG is not a real PGN one.  It is used with the -t
     * argument so that it becomes possible to indicate a FEN-based board pattern.
     */
    PSEUDO_FEN_PATTERN_TAG,
    HASHCODE_TAG,
    LONG_ECO_TAG,
    MODE_TAG,
    NIC_TAG,
    OPENING_TAG,
    /* The PSEUDO_PLAYER_TAG is not a real PGN one.  It is used with the -t
     * argument so that it becomes possible to indicate a player of either colour.
     */
    PSEUDO_PLAYER_TAG,
    PLY_COUNT_TAG,
    /* The TOTAL_PLY_COUNT_TAG is used with the --totalplycount argument
     * so record the total number of plies in a game.
     */
    TOTAL_PLY_COUNT_TAG,
    RESULT_TAG,
    ROUND_TAG,
    SECTION_TAG,
    SETUP_TAG,
    SITE_TAG,
    STAGE_TAG,
    SUB_VARIATION_TAG,
    TERMINATION_TAG,
    TIME_TAG,
    TIME_CONTROL_TAG,
    UTC_DATE_TAG,
    UTC_TIME_TAG,
    VARIATION_TAG,
    WHITE_TAG,
    WHITE_ELO_TAG,
    WHITE_NA_TAG,
    WHITE_TITLE_TAG,
    WHITE_TYPE_TAG,
    WHITE_USCF_TAG,
    /* The following should always be last. It should not be used
     * as a tag identification.
     */
    ORIGINAL_NUMBER_OF_TAGS
} TagName;

#endif	// TAGLIST_H

