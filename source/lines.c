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
#include <string.h>
#include <ctype.h>
#include "bool.h"
#include "mymalloc.h"
#include "lines.h"

/* Read a single line of input. */
#define INIT_LINE_LENGTH 80
#define LINE_INCREMENT 20

/* Define a character that may be used to comment a line in, e.g.
 * the variations files.
 * Use the PGN escape mechanism character, for consistency.
 */
#define COMMENT_CHAR '%'

char *
read_line(FILE *fpin)
{
    char *line = NULL;
    unsigned len = 0;
    unsigned max_length;
    int ch;

    ch = getc(fpin);
    if (ch != EOF) {
        line = (char *) malloc_or_die(INIT_LINE_LENGTH + 1);
        max_length = INIT_LINE_LENGTH;
        while ((ch != '\n') && (ch != '\r') && (ch != EOF)) {
            /* Another character to add. */
            if (len == max_length) {
                line = (char *) realloc_or_die((void *) line,
                        max_length + LINE_INCREMENT + 1);
                if (line == NULL) {
                    return NULL;
                }
                max_length += LINE_INCREMENT;
            }
            line[len] = ch;
            len++;
            ch = getc(fpin);
        }
        line[len] = '\0';
        if (ch == '\r') {
            /* Try to avoid double counting lines in dos-format files. */
            ch = getc(fpin);
            if (ch != '\n' && ch != EOF) {
                ungetc(ch, fpin);
            }
        }
    }
    return line;
}

/* Return TRUE if line contains a non-space character, but
 * is not a comment line.
 */
Boolean
non_blank_line(const char *line)
{
    Boolean blank = TRUE;

    if (line != NULL) {
        if (comment_line(line)) {
            /* Comment lines count as blanks. */
        }
        else {
            while (blank && (*line != '\0')) {
                if (!isspace((int) *line)) {
                    blank = FALSE;
                }
                else {
                    line++;
                }
            }
        }
    }
    return !blank;
}

Boolean
blank_line(const char *line)
{
    return !non_blank_line(line);
}

/* Should the given line be regarded as a comment line? */
Boolean
comment_line(const char *line)
{
    return *line == COMMENT_CHAR;
}
