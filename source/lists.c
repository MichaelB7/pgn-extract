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
#include "defs.h"
#include "typedef.h"
#include "lists.h"
#include "taglist.h"

/* Define a type to permit tag strings to be associated with
 * a TagOperator for selecting relationships between them
 * and a game to be matched.
 */
typedef struct {
    char *tag_string;
    TagOperator operator;
} TagSelection;

/* Definitions for maintaining arrays of tag strings.
 * These arrays are used for various purposes:
 *        lists of white/black players to extract on.
 *        lists of other criteria to extract on.
 */

typedef struct {
    /* How many elements we have currently allocated for.
     * If this is > 0 then we should always have allocated exactly
     * one more than this number to simplify the (char **)NULL
     * termination of the list.
     */
    unsigned num_allocated_elements;
    /* num_used_elements should always be <= num_allocated elements. */
    unsigned num_used_elements;
    /* The list of elements.
     * Elements 0 .. num_used_elements point to null-terminated strings.
     * list[num_used_elements] == (char **)NULL once the list is complete.
     */
    TagSelection *tag_strings;
} StringArray;

/* Functions to allow creation of string lists. */
/* Allow a string list for every known tag.
 * It is important that these lists should be initialised to
 *         { 0, 0, (TagSelection *) NULL }
 * which happens by default, anyway.
 * This array, and its length (tag_list_length) are initialised
 * by calling init_tag_lists.
 */
static StringArray *TagLists;
static int tag_list_length = 0;

static char *soundex(const char *str);
static Boolean check_list(int tag, const char *tag_string, StringArray *list);

void init_tag_lists(void)
{
    int i;
    tag_list_length = ORIGINAL_NUMBER_OF_TAGS;
    TagLists = (StringArray *) malloc_or_die(tag_list_length * sizeof (*TagLists));
    for (i = 0; i < tag_list_length; i++) {
        TagLists[i].num_allocated_elements = 0;
        TagLists[i].num_used_elements = 0;
        TagLists[i].tag_strings = (TagSelection *) NULL;
    }
}

/*
 * Extend the tag list to the new length.
 */
static void
extend_tag_list_length(int new_length)
{
    if (new_length < tag_list_length) {
        fprintf(GlobalState.logfile,
                "Internal error: inappropriate call to extend_tag_list_length().\n");
        fprintf(GlobalState.logfile,
                "New length of %d is not greater than existing length of %d\n",
                new_length, tag_list_length);
        exit(1);
    }
    else {
        int i;
        TagLists = (StringArray *) realloc_or_die((void *) TagLists,
                new_length * sizeof (*TagLists));
        for (i = tag_list_length; i < new_length; i++) {
            TagLists[i].num_allocated_elements = 0;
            TagLists[i].num_used_elements = 0;
            TagLists[i].tag_strings = (TagSelection *) NULL;
        }
        tag_list_length = new_length;
    }
}

/* Add str to the list of strings in list.
 * List may be a new list, in which case space is allocated
 * for it.
 * Return the index on success, otherwise -1.
 */
static int
add_to_taglist(const char *str, StringArray *list)
{
    Boolean everything_ok = TRUE;

    if (list->num_allocated_elements == list->num_used_elements) {
        /* We need more space. */
        if (list->num_allocated_elements == 0) {
            /* No elements in the list. */
            list->tag_strings = (TagSelection *) malloc_or_die((INIT_LIST_SPACE + 1) *
                    sizeof (TagSelection));
            if (list->tag_strings != NULL) {
                list->num_allocated_elements = INIT_LIST_SPACE;
                list->num_used_elements = 0;
            }
            else {
                everything_ok = FALSE;
            }
        }
        else {
            list->tag_strings = (TagSelection *) realloc((void *) list->tag_strings,
                    (list->num_allocated_elements + MORE_LIST_SPACE + 1) *
                    sizeof (TagSelection));
            if (list->tag_strings != NULL) {
                list->num_allocated_elements += MORE_LIST_SPACE;
            }
            else {
                everything_ok = FALSE;
            }
        }
    }
    if (everything_ok) {
        /* There is space. */
        const unsigned len = strlen(str) + 1;
        unsigned ix = list->num_used_elements;

        list->tag_strings[ix].operator = NONE;
        list->tag_strings[ix].tag_string = (char *) malloc_or_die(len);
        if (list->tag_strings[ix].tag_string != NULL) {
            strcpy(list->tag_strings[ix].tag_string, str);
            list->num_used_elements++;
            /* Make sure that the list is properly terminated at all times. */
            list->tag_strings[ix + 1].tag_string = NULL;
            return (int) ix;
        }
        else {
            return -1;
        }
    }
    else {
        return -1;
    }
}

/* Simple soundex code supplied by John Brogan
 * (jwbrogan@unix2.netaxs.com), 26th Aug 1994.
 * John writes:
 * "In recognition of the large number of strong players from countries
 * with Slavic based languages, I tried to tailor the soundex code
 * to match any reasonable transliteration of a Slavic name into
 * English.  Thus, Nimzovich will match Nimsowitsch, Tal will match
 * Talj, etc.  Unfortunately, in order to be sure not to miss any
 * valid matches, I had to make the code so tolerant that it sometimes
 * comes up with some wildly false matches.  This, to me, is better
 * than missing some games, but your mileage may differ."
 *
 * This looks like it was originally derived from the public domain
 * version released by N. Dean Pentcheff, 1989, which was, itself,
 * based on that in D.E. Knuth's "The art of computer programming.",
 * Volume 3: Sorting and searching.  Addison-Wesley Publishing Company:
 * Reading, Mass. Page 392.
 * Amended by David Barnes, 2nd Sep 1994.
 */

/* Define a maximum length for the soundex result. */
#define MAXSOUNDEX 50

/* Calculate a soundex string for instr.
 * The space used is statically allocated, so the caller
 * will have to allocate its own for the result if it
 * to be retained across different calls.
 */
static char *
soundex(const char *str)
{
    static char sbuf[MAXSOUNDEX + 1];
    /* An index into sbuf. */
    unsigned sindex = 0;
    /* Keep track of the last character to compress repeated letters. */
    char lastc = ' ';
    /*                     ABCDEFGHIJKLMNOPQRSTUVWXYZ */
    const char *mapping = "01230120002455012622011202";
    char initial_letter = *str;

    /* Special case for names that begin with 'J',
     * otherwise Janosevic == Nimzovich.
     * In addition, we really want Yusupov to match Jusupov.
     */
    if (islower((int) initial_letter)) {
        initial_letter = toupper(initial_letter);
    }
    if ((initial_letter == 'Y') || (initial_letter == 'J')) {
        sbuf[sindex] = '7';
        str++;
        sindex++;
    }

    while ((*str != '\0') && (sindex < MAXSOUNDEX)) {
        char ch = *str;

        /* We are only interested in alphabetics, and duplicate
         * characters are reduced to singletons.
         */
        if (isalpha((int) ch) && (ch != lastc)) {
            char translation;

            if (islower((int) ch)) {
                ch = toupper(ch);
            }
            /* Pick up the translation. */
            translation = mapping[ch - 'A'];
            if ((translation != '0') && (translation != lastc)) {
                sbuf[sindex] = translation;
                sindex++;
                lastc = translation;
            }
        }
        str++;
    }
    sbuf[sindex] = '\0';
    return (sbuf);
}

/* Return TRUE if tag is one on which soundex matching should
 * be used, if requested.
 */
static Boolean
soundex_tag(int tag)
{
    Boolean use_soundex = FALSE;

    switch (tag) {
        case WHITE_TAG:
        case BLACK_TAG:
        case PSEUDO_PLAYER_TAG:
        case EVENT_TAG:
        case SITE_TAG:
        case ANNOTATOR_TAG:
            use_soundex = TRUE;
            break;
    }
    return use_soundex;
}

/* Add tagstr to the list of tags to be matched.
 * If we are using soundex matching, then store
 * its soundex version rather than its plain text.
 */
void
add_tag_to_list(int tag, const char *tagstr, TagOperator operator)
{
    if (tag >= tag_list_length) {
        /* A new tag - one without a pre-defined _TAG #define.
         * Make sure there is room for it in TagList.
         */
        extend_tag_list_length(tag + 1);
    }
    if ((tag >= 0) && (tag < tag_list_length)) {
        const char *string_to_store = tagstr;
        int ix;

        if (GlobalState.use_soundex) {
            if (soundex_tag(tag)) {
                string_to_store = soundex(tagstr);
            }
        }
        ix = add_to_taglist(string_to_store, &TagLists[tag]);
        if (ix >= 0) {
            TagLists[tag].tag_strings[ix].operator = operator;
        }
        /* Ensure that we know we are checking tags. */
        GlobalState.check_tags = TRUE;
    }
    else {
        fprintf(GlobalState.logfile,
                "Illegal tag number %d in add_tag_to_list.\n", tag);
    }
}

/* Argstr is an extraction argument.
 * The type of argument is indicated by the first letter of
 * argstr:
 *        a -- annotator of the game
 *        b -- player of the black pieces
 *        d -- date of the game
 *        e -- ECO code
 *        p -- player of either colour
 *        r -- result
 *        w -- player of the white pieces
 * The remainder of argstr is the argument string to be entered
 * into the appropriate list.
 */
void
extract_tag_argument(const char *argstr)
{
    const char *arg = &(argstr[1]);

    switch (*argstr) {
        case 'a':
            add_tag_to_list(ANNOTATOR_TAG, arg, NONE);
            break;
        case 'b':
            add_tag_to_list(BLACK_TAG, arg, NONE);
            break;
        case 'd':
            add_tag_to_list(DATE_TAG, arg, NONE);
            break;
        case 'e':
            add_tag_to_list(ECO_TAG, arg, NONE);
            break;
        case 'h':
            add_tag_to_list(HASHCODE_TAG, arg, NONE);
            break;
        case 'p':
            add_tag_to_list(PSEUDO_PLAYER_TAG, arg, NONE);
            break;
        case 'r':
            add_tag_to_list(RESULT_TAG, arg, NONE);
            break;
        case 'w':
            add_tag_to_list(WHITE_TAG, arg, NONE);
            break;
        default:
            fprintf(GlobalState.logfile,
                    "Unknown type of tag extraction argument: %s\n",
                    argstr);
            exit(1);
            break;
    }
}

/* Check for one of list->strings matching the date_string.
 * Return TRUE on match, FALSE on failure.
 * It is only necessary for a prefix of tag to match
 * the string.
 */

/* Set limits on the allowable ranges for dates extracted from games.
 * Because of century changes, it is difficult to know what
 * best to do with two digit year numbers; so exclude them.
 */
#define MINDATE 100
#define MAXDATE 3000

static Boolean
check_date(const char *date_string, StringArray *list)
{
    unsigned list_index;

    /* Care needed because dates with operators must be ANDed and
     * dates without operators must be ORed.
     * The first match is used to set wanted properly for the first time.
     */
    Boolean wanted = FALSE;
    for (list_index = 0; list_index < list->num_used_elements; list_index++) {
        const char *list_string = list->tag_strings[list_index].tag_string;
        TagOperator operator = list->tag_strings[list_index].operator;

        if (*list_string == 'b') {
            operator = LESS_THAN;
            list_string++;
        }
        else if (*list_string == 'a') {
            operator = GREATER_THAN;
            list_string++;
        }
        else {
            /* No prefix. */
        }
        if (operator != NONE) {
            /* We have a relational comparison. */
            unsigned game_date, list_date;
            /* Try to extract dates from both strings. */
            if ((sscanf(list_string, "%u", &list_date) == 1) &&
                    (sscanf(date_string, "%u", &game_date) == 1) &&
                    (game_date > MINDATE) && (game_date < MAXDATE)) {
                Boolean matches;
                switch (operator) {
                    case LESS_THAN:
                        matches = game_date < list_date;
                        break;
                    case LESS_THAN_OR_EQUAL_TO:
                        matches = game_date <= list_date;
                        break;
                    case GREATER_THAN:
                        matches = game_date > list_date;
                        break;
                    case GREATER_THAN_OR_EQUAL_TO:
                        matches = game_date >= list_date;
                        break;
                    case EQUAL_TO:
                        matches = game_date == list_date;
                        break;
                    case NOT_EQUAL_TO:
                        matches = game_date != list_date;
                        break;
                    case NONE:
                    default:
                        /* Should have been covered above. */
                        matches = FALSE;
                        break;
                }
                if (list_index == 0) {
                    wanted = matches;
                }
                else {
                    wanted = wanted && matches;
                }
            }
            else {
                /* Bad format, or out of range. Assume not wanted. */
                wanted = FALSE;
		fprintf(GlobalState.logfile, "Failed to extract to ints.\n");
            }
        }
        else {
            /* No need to check if we already have a match. */
            if (list_index == 0 || !wanted) {
                /* Just a straight prefix match. */
                wanted = strncmp(date_string, list_string, strlen(list_string)) == 0;
            }
        }
    }
    return wanted;
}

static Boolean
check_elo(const char *elo_string, StringArray *list)
{
    unsigned list_index;
    Boolean wanted = TRUE;

    for (list_index = 0; (list_index < list->num_used_elements) && wanted;
            list_index++) {
        const char *list_string = list->tag_strings[list_index].tag_string;
        TagOperator operator = list->tag_strings[list_index].operator;

        if (operator != NONE) {
            /* We have a relational comparison. */
            unsigned game_elo, list_elo;
            /* Try to extract elos from both strings. */
            if ((sscanf(list_string, "%u", &list_elo) == 1) &&
                    (sscanf(elo_string, "%u", &game_elo) == 1)) {
                switch (operator) {
                    case LESS_THAN:
                        wanted = game_elo < list_elo;
                        break;
                    case LESS_THAN_OR_EQUAL_TO:
                        wanted = game_elo <= list_elo;
                        break;
                    case GREATER_THAN:
                        wanted = game_elo > list_elo;
                        break;
                    case GREATER_THAN_OR_EQUAL_TO:
                        wanted = game_elo >= list_elo;
                        break;
                    case EQUAL_TO:
                        wanted = game_elo == list_elo;
                        break;
                    case NOT_EQUAL_TO:
                        wanted = game_elo != list_elo;
                        break;
                    case NONE:
                        /* Already covered above. */
                        break;
                }
            }
            else {
                /* Bad format, or out of range. Assume not wanted. */
                wanted = FALSE;
            }
        }
        else {
            /* Just a straight prefix match. */
            if (strncmp(elo_string, list_string, strlen(list_string)) == 0) {
                wanted = TRUE;
            }
        }
    }
    return wanted;
}

/* Check for one of list->strings matching the tag.
 * Return TRUE on match, FALSE on failure.
 * It is only necessary for a prefix of tag to match
 * the string.
 */
static Boolean
check_list(int tag, const char *tag_string, StringArray *list)
{
    unsigned list_index;
    Boolean wanted = FALSE;
    const char *search_str;

    if (GlobalState.use_soundex && soundex_tag(tag)) {
        search_str = soundex(tag_string);
    }
    else {
        search_str = tag_string;
    }
    for (list_index = 0; (list_index < list->num_used_elements) && !wanted;
            list_index++) {
        const char *list_string = list->tag_strings[list_index].tag_string;

        if (GlobalState.tag_match_anywhere) {
            /* Match anywhere in the tag. */
            if (strstr(search_str, list_string) != NULL) {
                wanted = TRUE;
            }
        }
        else {
            /* Match only at the beginning of the tag. */
            if (strncmp(search_str, list_string, strlen(list_string)) == 0) {
                wanted = TRUE;
            }
        }
    }
    return wanted;
}

/* Check the Tag Details of this current game against those in
 * the wanted lists. Check all details apart from any ECO
 * tag as this is checked separately by CheckECOTag.
 * An empty list implies that there is no restriction on
 * the values in the corresponding tag field.
 * In consequence, completely empty lists imply that all
 * games reaching this far are wanted.
 * Return TRUE if wanted, FALSE otherwise.
 */
Boolean
check_tag_details_not_ECO(char *Details[], int num_details)
{
    Boolean wanted = TRUE;
    int tag;

    if (GlobalState.check_tags) {
        /* Sanity check. */
        if (num_details < tag_list_length) {
            fprintf(GlobalState.logfile,
                    "Internal error: mismatch in tag set lengths in ");
            fprintf(GlobalState.logfile,
                    "CheckTagDetailsNotECO: %d vs %d\n",
                    num_details, tag_list_length);
            exit(1);
        }

        /* PSEUDO_PLAYER_TAG and PSEUDO_ELO_TAG are treated differently,
         * since they have the effect of or-ing together the WHITE_ and BLACK_ lists.
         * Otherwise, different tag lists are and-ed.
         */
        if (TagLists[PSEUDO_PLAYER_TAG].num_used_elements != 0) {
            /* Check both the White and Black lists. */
            if (Details[WHITE_TAG] != NULL) {
                wanted = check_list(WHITE_TAG, Details[WHITE_TAG],
                        &TagLists[PSEUDO_PLAYER_TAG]);
                /* If we didn't find a player there, try for the opponent. */
                if (!wanted && (Details[BLACK_TAG] != NULL)) {
                    wanted = check_list(BLACK_TAG, Details[BLACK_TAG],
                            &TagLists[PSEUDO_PLAYER_TAG]);
                }
            }
            else if (Details[BLACK_TAG] != NULL) {
                wanted = check_list(BLACK_TAG, Details[BLACK_TAG],
                        &TagLists[PSEUDO_PLAYER_TAG]);
            }
            else {
                wanted = FALSE;
            }
        }
        else if (TagLists[PSEUDO_ELO_TAG].num_used_elements != 0) {
            /* Check both the White and Black lists. */
            if (Details[WHITE_ELO_TAG] != NULL) {
                wanted = check_elo(Details[WHITE_ELO_TAG],
                        &TagLists[PSEUDO_ELO_TAG]);
                /* If we didn't find a player there, try for the opponent. */
                if (!wanted && (Details[BLACK_ELO_TAG] != NULL)) {
                    wanted = check_elo(Details[BLACK_ELO_TAG],
                            &TagLists[PSEUDO_ELO_TAG]);
                }
            }
            else if (Details[BLACK_ELO_TAG] != NULL) {
                wanted = check_elo(Details[BLACK_ELO_TAG],
                        &TagLists[PSEUDO_ELO_TAG]);
            }
            else {
                wanted = FALSE;
            }
        }
        else {
            /* No PSEUDO_*_TAG info to check. */
        }

        /* Check the remaining tags in turn as long as we still have a match. */
        for (tag = 0; (tag < tag_list_length) && wanted; tag++) {
            if (tag == PSEUDO_PLAYER_TAG) {
            }
            else if (tag == PSEUDO_ELO_TAG) {
            }
            else if (tag == ECO_TAG) {
                /* This is handled separately. */
            }
            else if (TagLists[tag].num_used_elements != 0) {
                if (Details[tag] != NULL) {
                    if (tag == DATE_TAG) {
                        wanted = check_date(Details[tag], &TagLists[tag]);
                    }
                    else if ((tag == WHITE_ELO_TAG) || (tag == BLACK_ELO_TAG)) {
                        wanted = check_elo(Details[tag], &TagLists[tag]);
                    }
                    else {
                        wanted = check_list(tag, Details[tag], &TagLists[tag]);
                    }
                }
                else {
                    /* Required tag not present. */
                    wanted = FALSE;
                }
            }
            else {
                /* Not used. */
            }
        }
    }
    return wanted;
}

/* Check just the ECO tag from the game's tag details. */
Boolean
check_ECO_tag(char *Details[])
{
    Boolean wanted = TRUE;

    if (GlobalState.check_tags) {
        if (TagLists[ECO_TAG].num_used_elements != 0) {
            if (Details[ECO_TAG] != NULL) {
                wanted = check_list(ECO_TAG, Details[ECO_TAG], &TagLists[ECO_TAG]);
            }
            else {
                /* Required tag not present. */
                wanted = FALSE;
            }
        }
    }
    return wanted;
}

/* Check whether the tags fit with the setting of GlobalState.setup_status.
 * Return TRUE if they do, false otherwise.
 */
Boolean
check_setup_tag(char *Details[])
{
    switch (GlobalState.setup_status) {
        case SETUP_TAG_OK:
            return TRUE;
        case NO_SETUP_TAG:
            return Details[SETUP_TAG] == NULL;
        case SETUP_TAG_ONLY:
            return Details[SETUP_TAG] != NULL;
        default:
            fprintf(GlobalState.logfile, "Internal error: setup status %u not recognised.",
                    GlobalState.setup_status);
            exit(1);
    }
}
