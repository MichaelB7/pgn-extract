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

#include <stdio.h>
#include <stdlib.h>
#include "bool.h"
#include "mymalloc.h"
#include "defs.h"
#include "typedef.h"
#include "apply.h"
#include "fenmatcher.h"

/* Character on an encoded board representing an empty square. */
#define EMPTY_SQUARE '_'
/* Pattern meta characters. */
#define NON_EMPTY_SQUARE '!'
#define ANY_SQUARE_STATE '?'
#define ZERO_OR_MORE_OF_ANYTHING '*'
#define ANY_WHITE_PIECE 'A'
#define ANY_BLACK_PIECE 'a'

/* Symbols for closures. */
#define CCL_START '['
#define CCL_END ']'
#define NCCL '^'

/**
 * Based on original pattern matching code by Rob Pike.
 * Taken from:
 *     http://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html
 * and ideas from Kernighan and Plauger's "Software Tools".
 */

static Boolean matchhere(const char *regexp, const char *text);
static Boolean matchstar(const char *regexp, const char *text);
static Boolean matchccl(const char *regexp, const char *text);
static Boolean matchnccl(const char *regexp, const char *text);
static Boolean matchone(char regchar, char textchar);
static char *convert_board_to_text(const Board *board);

/* The list of FEN-based patterns to match. */
static StringList *fen_patterns = NULL;

void
add_fen_pattern(const char *fen_pattern)
{
    /* Check the pattern a reasonable syntax. */
    /* Count the number of rank dividers. */
    int dividors = 0;
    /* Count the number of symbols in each rank - must be
     * at least one.
     */
    int rankSymbols = 0;
    Boolean ok = TRUE;
    const char *p = fen_pattern;
    Boolean in_closure = FALSE;
    while (*p != '\0' && ok) {
        if (*p == '/') {
            dividors++;
            if (rankSymbols == 0) {
                /* Nothing on the previous rank. */
                ok = FALSE;
            }
            rankSymbols = 0;
        }
        else if (*p == CCL_START) {
            if (!in_closure) {
                in_closure = TRUE;
            }
            else {
                ok = FALSE;
                fprintf(GlobalState.logfile,
                        "Nested closures not allowed: %s\n",
                        fen_pattern);
            }
        }
        else if (*p == CCL_END) {
            if (in_closure) {
                in_closure = FALSE;
            }
            else {
                ok = FALSE;
                fprintf(GlobalState.logfile,
                        "Missing %c to match %c: %s\n",
                        CCL_START, CCL_END,
                        fen_pattern);
            }
        }
        else if (*p == NCCL) {
            if (!in_closure) {
                ok = FALSE;
                fprintf(GlobalState.logfile,
                        "%c not allowed outside %c...%c: %s\n",
                        NCCL,
                        CCL_START, CCL_END,
                        fen_pattern);
            }
        }
        else {
            rankSymbols++;
        }
        p++;
    }
    if (dividors != 7) {
        ok = FALSE;
    }
    else if (rankSymbols == 0) {
        ok = FALSE;
    }
    if (ok) {
        const char *pattern = copy_string(fen_pattern);
        fen_patterns = save_string_list_item(fen_patterns, pattern);
    }
    else {
        fprintf(GlobalState.logfile, "FEN Pattern: %s badly formed.\n",
                fen_pattern);
    }
}

/*
 * Try to match the board against one of the FEN patterns.
 * Return the matching pattern, if there is one, otherwise NULL.
 */
const char *
pattern_match_board(const Board *board)
{
    Boolean match = FALSE;
    const char *pattern = NULL;
    if (fen_patterns != NULL) {
        const char *text = convert_board_to_text(board);

        StringList *item = fen_patterns;

        while (item != NULL && !match) {
            if (0) printf("Try %s against %s\n", item->str, text);
            pattern = item->str;
            if (matchhere(pattern, text)) {
                if (0) fprintf(stdout, "%s matches\n%s\n", pattern, text);
                match = TRUE;
            }
            else {
                item = item->next;
            }
        }
        (void) free((void *) text);
        if (match) {
            return pattern;
        }
        else {
            return (const char *) NULL;
        }
    }
    else {
        return (const char *) NULL;
    }
}

/**
 * matchhere: search for regexp at beginning of text
 */
static Boolean
matchhere(const char *regexp, const char *text)
{
    if (regexp[0] == '\0' && text[0] == '\0') {
        return TRUE;
    }
    if (regexp[0] == ZERO_OR_MORE_OF_ANYTHING) {
        return matchstar(regexp + 1, text);
    }
    if (*text != '\0') {
        switch (*regexp) {
            case ANY_SQUARE_STATE:
                return matchhere(regexp + 1, text + 1);
                break;
            case NON_EMPTY_SQUARE:
            case ANY_WHITE_PIECE:
            case ANY_BLACK_PIECE:
                if (matchone(*regexp, *text)) {
                    return matchhere(regexp + 1, text + 1);
                }
                break;
            case CCL_START:
                /* Closure */
                if (regexp[1] == NCCL) {
                    return matchnccl(regexp + 2, text);
                }
                else {
                    return matchccl(regexp + 1, text);
                }
                break;
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8':
            {
                /* The number of empty squares required. */
                int empty = regexp[0] - '0';
                Boolean matches = TRUE;
                /* The number matched. */
                int match_count = 0;
                while (matches && match_count < empty) {
                    if (text[match_count] == EMPTY_SQUARE) {
                        match_count++;
                    }
                    else {
                        matches = FALSE;
                    }
                }
                if (matches) {
                    return matchhere(regexp + 1, text + match_count);
                }
            }
                break;
            default:
                if (*regexp == *text) {
                    return matchhere(regexp + 1, text + 1);
                }
                break;
        }
    }
    /* No match. */
    return FALSE;
}

/**
 * matchstar: leftmost longest search on a single rank.
 */
static Boolean
matchstar(const char *regexp, const char *text)
{
    const char *t;

    /* Find the end of this rank. */
    for (t = text; *t != '\0' && *t != '/'; t++) {
        ;
    }
    /* Try from the longest match to the shortest until success. */
    do {
        /* * matches zero or more */
        if (matchhere(regexp, t)) {
            return TRUE;
        }
    } while (t-- > text);
    return FALSE;
}

/*
 * Return TRUE if regchar matches textchar, FALSE otherwise.
 */
static Boolean
matchone(char regchar, char textchar)
{
    if (regchar == textchar) {
        return TRUE;
    }
    else {
        switch (regchar) {
            case NON_EMPTY_SQUARE:
                return textchar != EMPTY_SQUARE;
            case ANY_WHITE_PIECE:
                /* Match any white piece. */
                switch (textchar) {
                    case 'K':
                    case 'Q':
                    case 'R':
                    case 'N':
                    case 'B':
                    case 'P':
                        return TRUE;
                    default:
                        return FALSE;
                }
            case ANY_BLACK_PIECE:
                /* Match any black piece. */
                switch (textchar) {
                    case 'k':
                    case 'q':
                    case 'r':
                    case 'n':
                    case 'b':
                    case 'p':
                        return TRUE;
                    default:
                        return FALSE;
                }
            case ANY_SQUARE_STATE:
                return TRUE;
            default:
                return FALSE;
        }
    }
}

/*
 * Match any of the character closure.
 */
static Boolean
matchccl(const char *regexp, const char *text)
{
    while (*regexp != CCL_END &&
            !matchone(*regexp, *text) && *regexp != '\0') {
        regexp++;
    }
    if (matchone(*regexp, *text)) {
        do {
            regexp++;
        } while (*regexp != CCL_END && *regexp != '\0');
        return matchhere(regexp + 1, text + 1);
    }
    else {
        return FALSE;
    }
}

/*
 * Match any of the characters not in the closure.
 */
static Boolean
matchnccl(const char *regexp, const char *text)
{
    while (*regexp != CCL_END &&
            !matchone(*regexp, *text) && *regexp != '\0') {
        regexp++;
    }
    if (*regexp == CCL_END) {
        return matchhere(regexp + 1, text + 1);
    }
    else {
        return FALSE;
    }
}

/* Build a basic EPD string from the given board. */
static char *
convert_board_to_text(const Board *board)
{
    Rank rank;
    int ix = 0;
    /* Allow space for a full board and '/' separators in between. */
    char *text = (char *) malloc_or_die(8 * 8 + 8);
    for (rank = LASTRANK; rank >= FIRSTRANK; rank--) {
        Col col;
        for (col = FIRSTCOL; col <= LASTCOL; col++) {
            int coloured_piece = board->board[RankConvert(rank)]
                    [ColConvert(col)];
            if (coloured_piece != EMPTY) {
                text[ix] = coloured_piece_to_SAN_letter(coloured_piece);
            }
            else {
                text[ix] = EMPTY_SQUARE;
            }
            ix++;
        }
        if (rank != FIRSTRANK) {
            text[ix] = '/';
            ix++;
        }
    }
    text[ix] = '\0';
    return text;
}
