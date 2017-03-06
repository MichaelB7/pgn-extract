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
#if defined(__BORLANDC__) || defined(_MSC_VER)
#include <io.h>
#ifndef R_OK
#define R_OK 0
#endif
#else
#include <unistd.h>
#endif
#include "bool.h"
#include "mymalloc.h"
#include "defs.h"
#include "typedef.h"
#include "tokens.h"
#include "taglist.h"
#include "lex.h"
#include "moves.h"
#include "lists.h"
#include "decode.h"
#include "lines.h"
#include "grammar.h"
#include "apply.h"
#include "output.h"

/* Prototypes for the functions in this file. */
static void save_string(const char *result);
/* When a move is saved, what is known of its source and destination coordinates
 * should also be saved.
 */
static void save_move(const unsigned char *move);
static void save_q_castle(void);
static void save_k_castle(void);
static void terminate_input(void);
static Boolean extract_yytext(const unsigned char *symbol_start,
        const unsigned char *linep);
static Boolean open_input(const char *infile);
static Boolean open_input_file(int file_number);

static unsigned long line_number = 0;
/* Keep track of the Recursive Annotation Variation level. */
static unsigned RAV_level = 0;
/* Keep track of the last move found. */
static unsigned char last_move[MAX_MOVE_LEN + 1];
/* How many games we have extracted from this file. */
static unsigned games_in_file = 0;

/* Provide an input file pointer.
 * This is intialised in init_lex_tables.
 */
static FILE *yyin = NULL;

/* Define space for holding matched tokens. */
#define MAX_YYTEXT 100
static unsigned char yytext[MAX_YYTEXT + 1];
YYSTYPE yylval;

#define MAX_CHAR 256
#define ALPHA_DIST ('a'-'A')
/* Table of symbol classifications. */
static TokenType ChTab[MAX_CHAR];
/* A boolean array as to whether a character is allowed in a move or not. */
static short MoveChars[MAX_CHAR];

/* Define a table to hold the list of tag strings and the corresponding
 * TagName index. This is initialised in init_list_of_known_tags().
 */
static const char **TagList;
static unsigned tag_list_length = 0;

/* Initialise the TagList. This should be stored in alphabetical order,
 * by virtue of the order in which the _TAG values are defined.
 */
static void
init_list_of_known_tags(void)
{
    unsigned i;
    tag_list_length = ORIGINAL_NUMBER_OF_TAGS;
    TagList = (const char **) malloc_or_die(tag_list_length * sizeof (*TagList));
    /* Be paranoid and put a string in every entry. */
    for (i = 0; i < tag_list_length; i++) {
        TagList[i] = "";
    }
    TagList[ANNOTATOR_TAG] = "Annotator";
    TagList[BLACK_TAG] = "Black";
    TagList[BLACK_ELO_TAG] = "BlackElo";
    TagList[BLACK_NA_TAG] = "BlackNA";
    TagList[BLACK_TITLE_TAG] = "BlackTitle";
    TagList[BLACK_TYPE_TAG] = "BlackType";
    TagList[BLACK_USCF_TAG] = "BlackUSCF";
    TagList[BOARD_TAG] = "Board";
    TagList[DATE_TAG] = "Date";
    TagList[ECO_TAG] = "ECO";
    TagList[PSEUDO_ELO_TAG] = "Elo";
    TagList[EVENT_TAG] = "Event";
    TagList[EVENT_DATE_TAG] = "EventDate";
    TagList[EVENT_SPONSOR_TAG] = "EventSponsor";
    TagList[FEN_TAG] = "FEN";
    TagList[PSEUDO_FEN_PATTERN_TAG] = "FENPattern";
    TagList[HASHCODE_TAG] = "HashCode";
    TagList[LONG_ECO_TAG] = "LongECO";
    TagList[MODE_TAG] = "Mode";
    TagList[NIC_TAG] = "NIC";
    TagList[OPENING_TAG] = "Opening";
    TagList[PSEUDO_PLAYER_TAG] = "Player";
    TagList[PLY_COUNT_TAG] = "PlyCount";
    TagList[RESULT_TAG] = "Result";
    TagList[ROUND_TAG] = "Round";
    TagList[SECTION_TAG] = "Section";
    TagList[SETUP_TAG] = "SetUp";
    TagList[SITE_TAG] = "Site";
    TagList[STAGE_TAG] = "Stage";
    TagList[SUB_VARIATION_TAG] = "SubVariation";
    TagList[TERMINATION_TAG] = "Termination";
    TagList[TIME_TAG] = "Time";
    TagList[TIME_CONTROL_TAG] = "TimeControl";
    TagList[TOTAL_PLY_COUNT_TAG] = "TotalPlyCount";
    TagList[UTC_DATE_TAG] = "UTCDate";
    TagList[UTC_TIME_TAG] = "UTCTime";
    TagList[VARIATION_TAG] = "Variation";
    TagList[WHITE_TAG] = "White";
    TagList[WHITE_ELO_TAG] = "WhiteElo";
    TagList[WHITE_NA_TAG] = "WhiteNA";
    TagList[WHITE_TITLE_TAG] = "WhiteTitle";
    TagList[WHITE_TYPE_TAG] = "WhiteType";
    TagList[WHITE_USCF_TAG] = "WhiteUSCF";
}

/* Extend TagList to accomodate a new tag string.
 * Return the current value of tag_list_length as its
 * index, having incremented its value.
 */
static TagName
make_new_tag(const char *tag)
{
    unsigned tag_index = tag_list_length;
    tag_list_length++;
    TagList = (const char **) realloc_or_die((void *) TagList,
            tag_list_length * sizeof (*TagList));
    TagList[tag_index] = copy_string(tag);
    /* Ensure that the game header's tags array can accommodate
     * the new tag.
     */
    increase_game_header_tags_length(tag_list_length);
    return tag_index;
}

const char *
tag_header_string(TagName tag)
{
    if (tag < tag_list_length) {
        return TagList[tag];
    }
    else {
        fprintf(GlobalState.logfile, "Internal error in tag_header_string(%d)\n",
                tag);
        return NULL;
    }
}

/* Initialise ChTab[], the classification of the initial characters
 * of symbols.
 * Initialise MoveChars, the classification of secondary characters
 * of moves.
 */
void
init_lex_tables(void)
{
    int i;

    /* Assume standard input will be used, until we know otherwise. */
    yyin = stdin;
    init_list_of_known_tags();
    /* Initialise ChTab[]. */
    for (i = 0; i < MAX_CHAR; i++) {
        ChTab[i] = ERROR_TOKEN;
    }
    ChTab[' '] = WHITESPACE;
    ChTab['\t'] = WHITESPACE;
    /* Take account of DOS line-ends. */
    ChTab['\r'] = WHITESPACE;
    ChTab['['] = TAG_START;
    ChTab[']'] = TAG_END;
    ChTab['"'] = DOUBLE_QUOTE;
    ChTab['{'] = COMMENT_START;
    ChTab['}'] = COMMENT_END;
    ChTab['$'] = NAG;
    ChTab['!'] = ANNOTATE;
    ChTab['?'] = ANNOTATE;
    ChTab['+'] = CHECK_SYMBOL;
    ChTab['#'] = CHECK_SYMBOL;
    ChTab['.'] = DOT;
    ChTab['('] = RAV_START;
    ChTab[')'] = RAV_END;
    ChTab['%'] = PERCENT;
    ChTab['\\'] = ESCAPE;
    ChTab['\0'] = EOS;
    ChTab['*'] = STAR;
    ChTab['-'] = DASH;

    /* Operators allowed only in the tag file. */
    ChTab['<'] = OPERATOR;
    ChTab['>'] = OPERATOR;
    ChTab['='] = OPERATOR; /* Overloaded in MoveChars. */

    for (i = '0'; i <= '9'; i++) {
        ChTab[i] = DIGIT;
    }
    for (i = 'A'; i <= 'Z'; i++) {
        ChTab[i] = ALPHA;
        ChTab[i + ALPHA_DIST] = ALPHA;
    }
    ChTab['_'] = ALPHA;

    /* Classify the Russian piece letters as ALPHA. */
    ChTab[RUSSIAN_KNIGHT_OR_KING] = ALPHA; /* King and Knight. */
    ChTab[RUSSIAN_KING_SECOND_LETTER] = ALPHA; /* King (second character). */
    ChTab[RUSSIAN_QUEEN] = ALPHA; /* Queen. */
    ChTab[RUSSIAN_ROOK] = ALPHA; /* Rook. */
    ChTab[RUSSIAN_BISHOP] = ALPHA; /* Bishop. */

    /* Initialise MoveChars[]. */
    for (i = 0; i < MAX_CHAR; i++) {
        MoveChars[i] = 0;
    }
    /* Files. */
    for (i = 'a'; i <= 'h'; i++) {
        MoveChars[i] = 1;
    }
    /* Ranks. */
    for (i = '1'; i <= '8'; i++) {
        MoveChars[i] = 1;
    }
    /* Upper-case pieces. */
    MoveChars['K'] = 1;
    MoveChars['Q'] = 1;
    MoveChars['R'] = 1;
    MoveChars['N'] = 1;
    MoveChars['B'] = 1;
    /* Lower-case pieces. */
    MoveChars['k'] = 1;
    MoveChars['q'] = 1;
    MoveChars['r'] = 1;
    MoveChars['n'] = 1;
    MoveChars['b'] = 1;
    /* Other u-c Dutch/German characters. */
    MoveChars['D'] = 1; /* Queen. */
    MoveChars['T'] = 1; /* Rook. */
    MoveChars['S'] = 1; /* Knight. */
    MoveChars['P'] = 1; /* Knight. */
    MoveChars['L'] = 1; /* Bishop. */
    /* Russian characters. */
    MoveChars[RUSSIAN_KNIGHT_OR_KING] = 1; /* King and Knight. */
    MoveChars[RUSSIAN_KING_SECOND_LETTER] = 1; /* King (second character). */
    MoveChars[RUSSIAN_QUEEN] = 1; /* Queen. */
    MoveChars[RUSSIAN_ROOK] = 1; /* Rook. */
    MoveChars[RUSSIAN_BISHOP] = 1; /* Bishop. */

    /* Capture and square separators. */
    MoveChars['x'] = 1;
    MoveChars['X'] = 1;
    MoveChars[':'] = 1;
    MoveChars['-'] = 1;
    /* Promotion character. */
    MoveChars['='] = 1;
    /* Castling. */
    MoveChars['O'] = 1;
    MoveChars['o'] = 1;
    MoveChars['0'] = 1;
    /* Allow a trailing p for ep. */
    MoveChars['p'] = 1;
}

/* Starting from linep in line, gather up the string until
 * the closing quote.  Skip over the closing quote.
 */
LinePair
gather_string(char *line, unsigned char *linep)
{
    LinePair resulting_line;
    char ch;
    unsigned len = 0;
    char *str;
    Boolean end_of_string = FALSE;

    do {
        ch = *linep++;
        len++;
        if (ch == '\\') {
            /* Escape the next character. */
            ch = *linep++;
            len++;
            if(ch == '\0') {
                fprintf(GlobalState.logfile, "Missing escaped character in string.\n");
                print_error_context(GlobalState.logfile);
                end_of_string = TRUE;
            }
        }
        else if(ch == '"' || ch == '\0') {
            end_of_string = TRUE;
        }
        else {
            /* Ordinary character. */
        }
    } while (!end_of_string);
    /* The last one doesn't belong in the string. */
    len--;
    /* Allocate space for the result. */
    str = malloc_or_die(len + 1);
    strncpy(str, (const char *) (linep - len - 1), len);
    str[len] = '\0';
    /* Store it in yylval. */
    yylval.token_string = str;

    /* Make sure that the string was properly terminated, by
     * looking at the last character examined.
     */
    if (ch == '\0') {
        /* Too far. */
        if (!GlobalState.skipping_current_game) {
            fprintf(GlobalState.logfile, "Missing closing quote in %s\n", line);
        }
        if (len > 1) {
            /* Move back to the null. */
            linep--;
            str[len - 1] = '\0';
        }
    }
    else {
        /* We have already skipped over the closing quote. */
    }
    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = STRING;
    return resulting_line;
}

/*
 * Is ch of the given character class?
 * External access to ChTab.
 */
Boolean
is_character_class(unsigned char ch, TokenType character_class)
{
    return ChTab[ch] == character_class;
}

/* Starting from linep in line, gather up a comment until
 * the END_COMMENT.  Skip over the END_COMMENT.
 */
static LinePair
gather_comment(char *line, unsigned char *linep)
{
    LinePair resulting_line;
    char ch;
    unsigned len = 0;
    /* The string list in which the current comment will be gathered. */
    StringList *current_comment = NULL;
    /* The pointer to be returned. */
    CommentList *comment;

    do {
        /* Restart a new segment. */
        len = 0;
        do {
            ch = *linep++;
            len++;
        } while ((ch != '}') && (ch != '\0'));
        /* The last one doesn't belong in the comment. */
        len--;
        if (GlobalState.keep_comments) {
            char *comment_str;

            /* Allocate space for the result. */
            comment_str = (char *) malloc_or_die(len + 1);
            strncpy(comment_str, (const char *) (linep - len - 1), len);
            comment_str[len] = '\0';
            current_comment = save_string_list_item(current_comment, comment_str);
        }
        if (ch == '\0') {
            line = next_input_line(yyin);
            linep = (unsigned char *) line;
        }
    } while ((ch != '}') && (line != NULL));

    /* Set up the structure to be returned. */
    comment = malloc_or_die(sizeof (*comment));
    comment->comment = current_comment;
    comment->next = NULL;
    yylval.comment = comment;

    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = COMMENT;
    return resulting_line;
}

/* Remember that 0 can start 0-1 and 0-0.
 * Remember that 1 can start 1-0 and 1/2.
 */
static LinePair
gather_possible_numeric(char *line, unsigned char *linep, char initial_digit)
{
    LinePair resulting_line;
    TokenType token = MOVE_NUMBER;
    /* Keep a record of where this token started. */
    const unsigned char *symbol_start = linep - 1;

    if (initial_digit == '0') {
        /* Could be castling or a result. */
        if (strncmp((const char *) linep, "-1", 2) == 0) {
            token = TERMINATING_RESULT;
            save_string("0-1");
            linep += 2;
        }
        else if (strncmp((const char *) linep, "-0-0", 4) == 0) {
            token = MOVE;
            save_q_castle();
            linep += 4;
        }
        else if (strncmp((const char *) linep, "-0", 2) == 0) {
            token = MOVE;
            save_k_castle();
            linep += 2;
        }
        else {
            /* MOVE_NUMBER */
        }
    }
    else if (initial_digit == '1') {
        if (strncmp((const char *) linep, "-0", 2) == 0) {
            token = TERMINATING_RESULT;
            save_string("1-0");
            linep += 2;
        }
        else if (strncmp((const char *) linep, "/2", 2) == 0) {
            token = TERMINATING_RESULT;
            linep += 2;
            /* Check for the full form. */
            if (strncmp((const char *) linep, "-1/2", 4) == 0) {
                token = TERMINATING_RESULT;
                linep += 4;
            }
            /* Make sure that the full form of the draw result
             * is saved. 
             */
            save_string("1/2-1/2");
        }
        else {
            /* MOVE_NUMBER */
        }
    }
    else {
        /* MOVE_NUMBER */
    }
    if (token == MOVE_NUMBER) {
        /* Gather the remaining digits. */
        while (isdigit((unsigned) *linep)) {
            linep++;
        }
    }
    if (token == MOVE_NUMBER) {
        /* Fill out the fields of yylval. */
        if (extract_yytext(symbol_start, linep)) {
            yylval.move_number = 0;
            (void) sscanf((const char *) yytext, "%u", &yylval.move_number);
            /* Skip any trailing dots. */
            while (*linep == '.') {
                linep++;
            }
        }
        else {
            token = NO_TOKEN;
        }
    }
    else {
        /* TERMINATING_RESULT and MOVE have already been dealt with. */
    }
    resulting_line.line = line;
    resulting_line.linep = linep;
    resulting_line.token = token;
    return resulting_line;
}

/* Look up tag_string in TagList[] and return its _TAG
 * value or -1 if it isn't there.
 * Although the strings are sorted initially, further
 * tags identified in the source files will be appended
 * without further sorting. So we cannot use a binary
 * search on the list.
 */
static int
identify_tag(const char *tag_string)
{
    unsigned tag_index;

    for (tag_index = 0; tag_index < tag_list_length; tag_index++) {
        if (strcmp(tag_string, TagList[tag_index]) == 0) {
            return tag_index;
        }
    }
    /* Not found. */
    return -1;
}

/* Starting from linep in line, gather up the tag name.
 * Skip over any preceding white space.
 */
LinePair
gather_tag(char *line, unsigned char *linep)
{
    LinePair resulting_line;
    char ch;
    unsigned len = 0;

    do {
        /* Check for end of line while skipping white space. */
        if (*linep == '\0') {
            line = next_input_line(yyin);
            linep = (unsigned char *) line;
        }
        if (line != NULL) {
            while (ChTab[(unsigned) *linep] == WHITESPACE) {
                linep++;
            }
        }
    }    while ((line != NULL) && (ChTab[(unsigned) *linep] == '\0'));

    if (line != NULL) {
        ch = *linep++;
        while (isalpha((unsigned) ch) || isdigit((unsigned) ch) || (ch == '_')) {
            len++;
            ch = *linep++;
        }
        /* The last one wasn't part of the tag. */
        linep--;
        if (len > 0) {
            int tag_item;
            char *tag_string;

            /* Allocate space for the result. */
            tag_string = malloc_or_die(len + 1);
            strncpy((char *) tag_string, (const char *) (linep - len), len);
            tag_string[len] = '\0';
            tag_item = identify_tag(tag_string);
            if (tag_item < 0) {
                tag_item = make_new_tag(tag_string);
            }
            if (tag_item >= 0 && ((unsigned) tag_item) < tag_list_length) {
                yylval.tag_index = tag_item;
                resulting_line.token = TAG;
                (void) free((void *) tag_string);
            }
            else {
                fprintf(GlobalState.logfile,
                        "Internal error: invalid tag index %d in gather_tag.\n",
                        tag_item);
                exit(1);
            }
        }
        else {
            resulting_line.token = NO_TOKEN;
        }
    }
    else {
        resulting_line.token = NO_TOKEN;
    }
    resulting_line.line = line;
    resulting_line.linep = linep;
    return resulting_line;
}

static Boolean
extract_yytext(const unsigned char *symbol_start, const unsigned char *linep)
{ /* Whether the string fitted. */
    Boolean Ok = TRUE;
    long len = linep - symbol_start;

    if (len < MAX_YYTEXT) {
        strncpy((char *) yytext, (const char *) symbol_start, len);
        yytext[len] = '\0';
    }
    else {
        strncpy((char *) yytext, (const char *) symbol_start, MAX_YYTEXT);
        yytext[MAX_YYTEXT] = '\0';
        if (!GlobalState.skipping_current_game)
            fprintf(GlobalState.logfile, "Symbol %s exceeds length of %u.\n",
                yytext, MAX_YYTEXT);
        Ok = FALSE;
    }
    return Ok;
}

/* Identify the next symbol.
 * Don't take any action on EOF -- leave that to next_token.
 */
static TokenType
get_next_symbol(void)
{
    static char *line = NULL;
    static unsigned char *linep = NULL;
    /* The token to be returned. */
    TokenType token;
    LinePair resulting_line;

    do {
        /* Remember where in line the current symbol starts. */
        const unsigned char *symbol_start;

        /* Clear any remaining symbol. */
        *yytext = '\0';
        if (line == NULL) {
            line = next_input_line(yyin);
            linep = (unsigned char *) line;
            if (line != NULL) {
                token = NO_TOKEN;
            }
            else {
                token = EOF_TOKEN;
            }
        }
        else {
            int next_char = *linep & 0x0ff;

            /* Remember where we start. */
            symbol_start = linep;
            linep++;
            token = ChTab[next_char];

            switch (token) {
                case WHITESPACE:
                    while (ChTab[(unsigned) *linep] == WHITESPACE)
                        linep++;
                    token = NO_TOKEN;
                    break;
                case TAG_START:
                    resulting_line = gather_tag(line, linep);
                    /* Pick up where we are now. */
                    line = resulting_line.line;
                    linep = resulting_line.linep;
                    token = resulting_line.token;
                    break;
                case TAG_END:
                    token = NO_TOKEN;
                    break;
                case DOUBLE_QUOTE:
                    resulting_line = gather_string(line, linep);
                    /* Pick up where we are now. */
                    line = resulting_line.line;
                    linep = resulting_line.linep;
                    token = resulting_line.token;
                    break;
                case COMMENT_START:
                    resulting_line = gather_comment(line, linep);
                    /* Pick up where we are now. */
                    line = resulting_line.line;
                    linep = resulting_line.linep;
                    token = resulting_line.token;
                    break;
                case COMMENT_END:
                    if (!GlobalState.skipping_current_game) {
                        fprintf(GlobalState.logfile, "Unmatched comment end on line %lu.\n", line_number);
                    }
                    token = NO_TOKEN;
                    break;
                case NAG:
                    while (isdigit((unsigned) *linep)) {
                        linep++;
                    }
                    if (extract_yytext(symbol_start, linep)) {
                        save_string((const char *) yytext);
                    }
                    else {
                        token = NO_TOKEN;
                    }
                    break;
                case ANNOTATE:
                    /* Don't return anything in case of error. */
                    token = NO_TOKEN;
                    while (ChTab[(unsigned) *linep] == ANNOTATE) {
                        linep++;
                    }
                    if (extract_yytext(symbol_start, linep)) {
                        switch (yytext[0]) {
                            case '!':
                                switch (yytext[1]) {
                                    case '!':
                                        save_string("$3");
                                        break;
                                    case '?':
                                        save_string("$5");
                                        break;
                                    default:
                                        save_string("$1");
                                        break;
                                }
                                token = NAG;
                                break;
                            case '?':
                                switch (yytext[1]) {
                                    case '!':
                                        save_string("$6");
                                        break;
                                    case '?':
                                        save_string("$4");
                                        break;
                                    default:
                                        save_string("$2");
                                        break;
                                }
                                token = NAG;
                                break;
                        }
                    }
                    break;
                case CHECK_SYMBOL:
                    /* Allow ++ */
                    while (ChTab[(unsigned) *linep] == CHECK_SYMBOL) {
                        linep++;
                    }
                    break;
                case DOT:
                    while (ChTab[(unsigned) *linep] == DOT)
                        linep++;
                    token = NO_TOKEN;
                    break;
                case PERCENT:
                    /* Trash the rest of the line. */
                    line = next_input_line(yyin);
                    linep = (unsigned char *) line;
                    token = NO_TOKEN;
                    break;
                case ESCAPE:
                    /* @@@ What to do about this? */
                    if (*linep != '\0') {
                        linep++;
                    }
                    token = NO_TOKEN;
                    break;
                case ALPHA:
                    /* Not all ALPHAs are move characters. */
                    if (MoveChars[next_char]) {
                        /* Scan through the possible move characters. */
                        while (MoveChars[*linep & 0x0ff]) {
                            linep++;
                        }
                        if (extract_yytext(symbol_start, linep)) {
                            /* Only classify it as a move if it
                             * seems to be a complete move.
                             */
                            if (move_seems_valid(yytext)) {
                                save_move(yytext);
                                token = MOVE;
                            }
                            else {
                                if (!GlobalState.skipping_current_game) {
                                    print_error_context(GlobalState.logfile);
                                    fprintf(GlobalState.logfile,
                                            "Unknown move text %s.\n", yytext);
                                }
                                token = NO_TOKEN;
                            }
                        }
                        else {
                            token = NO_TOKEN;
                        }
                    }
                    else {
                        if (!GlobalState.skipping_current_game) {
                            print_error_context(GlobalState.logfile);
                            fprintf(GlobalState.logfile,
                                    "Unknown character %c (Hex: %x).\n",
                                    next_char, next_char);
                        }
                        /* Skip any sequence of them. */
                        while (ChTab[(unsigned) *linep] == ERROR_TOKEN)
                            linep++;
                    }
                    break;
                case DIGIT:
                    /* Remember that 0 can start 0-1 and 0-0.
                     * Remember that 1 can start 1-0 and 1/2.
                     */
                    resulting_line = gather_possible_numeric(
                            line, linep, next_char);
                    /* Pick up where we are now. */
                    line = resulting_line.line;
                    linep = resulting_line.linep;
                    token = resulting_line.token;
                    break;
                case EOF_TOKEN:
                    break;
                case RAV_START:
                    RAV_level++;
                    break;
                case RAV_END:
                    if (RAV_level > 0) {
                        RAV_level--;
                    }
                    else {
                        if (!GlobalState.skipping_current_game) {
                            print_error_context(GlobalState.logfile);
                            fprintf(GlobalState.logfile, "Too many ')' found.\n");
                        }
                        token = NO_TOKEN;
                    }
                    break;
                case STAR:
                    save_string("*");
                    token = TERMINATING_RESULT;
                    break;
                case DASH:
                    if (ChTab[(unsigned) *linep] == DASH) {
                        linep++;
                        save_move((const unsigned char *) NULL_MOVE_STRING);
                        token = MOVE;
                    }
                    else {
                        fprintf(GlobalState.logfile, "Single '-' not allowed.\n");
                        print_error_context(GlobalState.logfile);
                        token = NO_TOKEN;
                    }
                    break;
                case EOS:
                    /* End of the string. */
                    line = next_input_line(yyin);
                    linep = (unsigned char *) line;
                    token = NO_TOKEN;
                    break;
                case ERROR_TOKEN:
                    if (!GlobalState.skipping_current_game) {
                        print_error_context(GlobalState.logfile);
                        fprintf(GlobalState.logfile,
                                "Unknown character %c (Hex: %x).\n",
                                next_char, next_char);
                    }
                    /* Skip any sequence of them. */
                    while (ChTab[(unsigned) *linep] == ERROR_TOKEN)
                        linep++;
                    break;
                case OPERATOR:
                    print_error_context(GlobalState.logfile);
                    fprintf(GlobalState.logfile,
                            "Operator in illegal context: %c.\n", *symbol_start);
                    /* Skip any sequence of them. */
                    while (ChTab[(unsigned) *linep] == OPERATOR)
                        linep++;
                    token = NO_TOKEN;
                    break;
                default:
                    if (!GlobalState.skipping_current_game) {
                        print_error_context(GlobalState.logfile);
                        fprintf(GlobalState.logfile,
                                "Internal error: Missing case for %d on char %x.\n",
                                token, next_char);
                    }
                    token = NO_TOKEN;
                    break;
            }
        }
    } while (token == NO_TOKEN);
    return token;
}

TokenType
next_token(void)
{
    TokenType token = get_next_symbol();

    /* Don't call yywrap if parsing the ECO file. */
    while ((token == EOF_TOKEN) && !GlobalState.parsing_ECO_file &&
            !yywrap()) {
        token = get_next_symbol();
    }
    return token;
}

/* Return TRUE if token is one to skip when looking for
 * the start or end of a game.
 */
static Boolean
skip_token(TokenType token)
{
    switch (token) {
        case TERMINATING_RESULT:
        case TAG:
        case MOVE:
        case EOF_TOKEN:
            return FALSE;
        default:
            return TRUE;
    }
}

/* Skip tokens until the next game looks like it is
 * about to start. This is signalled by
 * a tag section a terminating result from the
 * previous game, or a move.
 */
TokenType
skip_to_next_game(TokenType token)
{
    if (skip_token(token)) {
        GlobalState.skipping_current_game = TRUE;
        do {
            if (token == COMMENT) {
                /* Free the space. */
                if ((yylval.comment != NULL) &&
                        (yylval.comment->comment != NULL)) {
                    free_string_list(yylval.comment->comment);
                    free((void *) yylval.comment);
                    yylval.comment = NULL;
                }
            }
            token = next_token();
        } while (skip_token(token));
        GlobalState.skipping_current_game = FALSE;
    }
    return token;
}

/* Save castling moves in a standard way. */
static void
save_q_castle(void)
{
    save_move((const unsigned char *) "O-O-O");
}

/* Save castling moves in a standard way. */
static void
save_k_castle(void)
{
    save_move((const unsigned char *) "O-O");
}

/* Make a copy of the matched text of the move. */
static void
save_move(const unsigned char *move)
{
    /* Decode the move into its components. */
    yylval.move_details = decode_move(move);
    /* Remember the last move. */
    strcpy((char *) last_move, (const char *) move);
}

void
restart_lex_for_new_game(void)
{
    *last_move = '\0';
    RAV_level = 0;
}

/* Make it possible to read multiple input files.
 * These are held in list_of_files. The list
 * is built up from the program's arguments.
 */
static int current_file_num = 0;
/* Keep track of the list of PGN files.  These will either be the
 * remaining arguments once flags have been dealt with, or
 * those read from -c and -f arguments.
 */
static FILE_LIST list_of_files = {
    (const char **) NULL,
    (SourceFileType *) NULL,
    0, 0
};

/* Return the index number of the current input file in list_of_files. */
unsigned
current_file_number(void)
{
    return current_file_num;
}

/* Read a list of lines from fp. These are the names of files
 * to be added to the existing list_of_files.
 * list_of_files.list must have a (char *)NULL on the end.
 */
void
add_filename_list_from_file(FILE *fp, SourceFileType file_type)
{
    if ((list_of_files.files == NULL) || (list_of_files.max_files == 0)) {
        /* Allocate an initial number of pointers for the lines.
         * This must always include an extra one for terminating NULL.
         */
        list_of_files.files = (const char **) malloc_or_die((INIT_LIST_SPACE + 1) *
                sizeof (const char *));
        list_of_files.file_type = (SourceFileType *) malloc_or_die((INIT_LIST_SPACE + 1) *
                sizeof (SourceFileType));
        list_of_files.max_files = INIT_LIST_SPACE;
        list_of_files.num_files = 0;
    }
    if (list_of_files.files != NULL) {
        /* Find the first line. */
        char *line = read_line(fp);

        while (line != NULL) {
            if (non_blank_line(line)) {
                add_filename_to_source_list(line, file_type);
            }
            else {
                (void) free((void *) line);
            }
            line = read_line(fp);
        }
    }
}

void
add_filename_to_source_list(const char *filename, SourceFileType file_type)
{ /* Where to put it. */
    unsigned location = list_of_files.num_files;

    if (access(filename, R_OK) != 0) {
        fprintf(GlobalState.logfile, "Unable to find %s\n", filename);
        exit(1);
    }
    else {
        /* Ok. */
    }
    /* See if there is room. */
    if (list_of_files.num_files == list_of_files.max_files) {
        /* There isn't, so increase the amount of available space,
         * ensuring that there is always an extra slot for the terminating
         * NULL.
         */
        if ((list_of_files.files == NULL) || (list_of_files.max_files == 0)) {
            /* Allocate an initial number of pointers for the lines.
             * This must always include an extra one for terminating NULL.
             */
            list_of_files.files = (const char **) malloc_or_die((INIT_LIST_SPACE + 1) *
                    sizeof (const char *));
            list_of_files.file_type = (SourceFileType *)
                    malloc_or_die((INIT_LIST_SPACE + 1) *
                    sizeof (SourceFileType));
            list_of_files.max_files = INIT_LIST_SPACE;
            list_of_files.num_files = 0;
        }
        else {
            list_of_files.files = (const char **) realloc((void *) list_of_files.files,
                    (list_of_files.max_files + MORE_LIST_SPACE + 1) *
                    sizeof (const char *));
            list_of_files.file_type = (SourceFileType *)
                    realloc((void *) list_of_files.file_type,
                    (list_of_files.max_files + MORE_LIST_SPACE + 1) *
                    sizeof (SourceFileType));
            list_of_files.max_files += MORE_LIST_SPACE;
            if ((list_of_files.files == NULL) && (list_of_files.file_type == NULL)) {
                perror("");
                abort();
            }
        }
    }
    /* We know that there is space. Ensure that CHECKFILEs are all
     * stored before NORMALFILEs.
     */
    if (file_type == CHECKFILE) {

        for (location = 0; (location < list_of_files.num_files) &&
                (list_of_files.file_type[location] == CHECKFILE); location++) {
            /* Do nothing. */
        }
        if (location < list_of_files.num_files) {
            /* Put the new one here.
             * Move the rest down.
             */
            unsigned j;

            for (j = list_of_files.num_files; j > location; j--) {
                list_of_files.files[j] = list_of_files.files[j - 1];
                list_of_files.file_type[j] = list_of_files.file_type[j - 1];
            }
        }
    }
    list_of_files.files[location] = copy_string(filename);
    list_of_files.file_type[location] = file_type;
    list_of_files.num_files++;
    /* Keep the list properly terminated. */
    list_of_files.files[list_of_files.num_files] = (char *) NULL;
}

/* Use infile as the input source. */
static Boolean
open_input(const char *infile)
{
    yyin = fopen(infile, "r");
    if (yyin != NULL) {
        GlobalState.current_input_file = infile;
        if (GlobalState.verbosity > 1) {
            fprintf(GlobalState.logfile, "Processing %s\n",
                    GlobalState.current_input_file);
        }
    }
    return yyin != NULL;
}

/* Simple interface to open_input for the ECO file. */
Boolean
open_eco_file(const char *eco_file)
{
    return open_input(eco_file);
}

/* Open the input file whose number is the argument. */
static Boolean
open_input_file(int file_number)
{
    /* Depending on the type of file, ensure that the
     * current_file_type is set correctly.
     */
    if (open_input(list_of_files.files[file_number])) {
        GlobalState.current_file_type = list_of_files.file_type[file_number];
        return TRUE;
    }
    else {
        return FALSE;
    }
}

/* Open the first input file. */
Boolean
open_first_file(void)
{
    Boolean ok = TRUE;

    if (list_of_files.num_files == 0) {
        /* Use standard input. */
        yyin = stdin;
        GlobalState.current_input_file = "stdin";
        /* @@@ Should this be set?
        GlobalState.current_file_type = NORMALFILE;
         */
        if (GlobalState.verbosity > 1) {
            fprintf(GlobalState.logfile, "Processing %s\n",
                    GlobalState.current_input_file);
        }
    }
    else if (open_input_file(0)) {
    }
    else {
        fprintf(GlobalState.logfile,
                "Unable to open the PGN file: %s\n", input_file_name(0));
        ok = FALSE;
    }
    return ok;
}

/* Return the name of the file corresponding to the given
 * file number.
 */
const char *
input_file_name(unsigned file_number)
{
    if (file_number >= list_of_files.num_files) {
        return NULL;
    }
    else {
        return list_of_files.files[file_number];
    }
}

/* Give some error information. */
void
print_error_context(FILE *fp)
{
    if (GlobalState.current_input_file != NULL) {
        fprintf(fp, "File %s: ", GlobalState.current_input_file);
    }
    fprintf(fp, "Line number: %lu\n", line_number);
}

/* Make the given str accessible. */
static void
save_string(const char *str)
{
    const size_t len = strlen(str);
    char *token;

    token = malloc_or_die(len + 1);
    strcpy(token, str);
    yylval.token_string = token;
}

/* Return the next line of input from fp. */
char *
next_input_line(FILE *fp)
{ /* Retain each line in turn, so as to be able to free it. */
    static char *line = NULL;

    if (line != NULL) {
        (void) free((void *) line);
    }

    line = read_line(fp);

    if (line != NULL) {
        line_number++;
    }
    return line;
}

/* Handle the end of a file. */
int
yywrap(void)
{
    int time_to_exit;

    /* Beware of this being called in inappropriate circumstances. */
    if (list_of_files.files == NULL) {
        /* There are no files. */
        time_to_exit = 1;
    }
    else if (input_file_name(current_file_num) == NULL) {
        /* There was no last file! */
        time_to_exit = 1;
    }
    else {
        /* Close the input files.  */
        terminate_input();
        /* See if there is another. */
        current_file_num++;
        if (input_file_name(current_file_num) == NULL) {
            /* We have processed the last file. */
            time_to_exit = 1;
        }
        else if (!open_input_file(current_file_num)) {
            fprintf(GlobalState.logfile, "Unable to open the PGN file: %s\n",
                    input_file_name(current_file_num));
            time_to_exit = 1;
        }
        else {
            /* Ok, we opened it. */
            time_to_exit = 0;
            /* Set everything up for a new file. */
            /* Depending on the type of file, ensure that the
             * current_file_type is set correctly.
             */
            GlobalState.current_file_type =
                    list_of_files.file_type[current_file_num];
            restart_lex_for_new_game();
            games_in_file = 0;
            reset_line_number();
        }
    }
    return time_to_exit;
}

/* Reset the file's line number. */
void
reset_line_number(void)
{
    line_number = 0;
}

static void
terminate_input(void)
{
    if ((yyin != stdin) && (yyin != NULL)) {
        (void) fclose(yyin);
        yyin = NULL;
    }
}

