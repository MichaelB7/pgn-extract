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
/* For unlink() */
#include <io.h>
#else
/* For unlink() */
#include <unistd.h>
#endif
#include "bool.h"
#include "mymalloc.h"
#include "defs.h"
#include "typedef.h"
#include "tokens.h"
#include "taglist.h"
#include "lex.h"
#include "hashing.h"

/* Routines, similar in nature to those in apply.c
 * to implement a duplicate hash-table lookup using
 * an external file, rather than malloc'd memory.
 * The only limit should be a file system limit.
 * NB: Using an external, virtual file seems obsolete now.
 *
 * This version should be slightly more accurate than
 * the alternative because the final_ and cumulative_
 * hash values are both stored, rather than the XOR
 * of them.
 */

/*
 * The name of the file used.
 * This is overwritten each time, and removed on normal
 * program exit.
 */
static char VIRTUAL_FILE[] = "virtual.tmp";

/* Define the size of the hash table.
 */
#define LOG_TABLE_SIZE 100003

/* Define a table to hold hash values of the extracted games.
 * This is used to enable duplicate detection.
 */
typedef struct {
    /* Record the file offset of the first and last entries
     * for an index. (head == -1) => empty.
     */
    long head, tail;
} LogHeaderEntry;

/* If use_virtual_hash_table */
static LogHeaderEntry *VirtualLogTable = NULL;

/* Define a table to hold hash values of the extracted games.
 * This is used to enable duplicate detection when not using
 * the virtual hash table.
 */
static HashLog **LogTable = NULL;

/* Define a type to hold hash values of interest.
 * This is used both to aid in duplicate detection
 * and in finding positional variations.
 */
typedef struct VirtualHashLog {
    /* Store the final position hash value and
     * the cumulative hash value for a game.
     */
    HashCode final_hash_value, cumulative_hash_value;
    /* Record the file list index for the file this game was first found in. */
    int file_number;
    /* Record the file offset of the next element
     * in this list. -1 => end-of-list.
     */
    long next;
} VirtualHashLog;

static FILE *hash_file = NULL;

static const char *previous_virtual_occurance(Game game_details);

/*
 * Check whether the position counts indicate a three-fold repetition.
 * If we are checking for repetition return TRUE if it does and FALSE otherwise.
 * If we are not then return TRUE.
 */
Boolean check_for_only_repetition(PositionCount *position_counts)
{
    if (GlobalState.check_for_repetition) {
        PositionCount *entry = position_counts;
        while (entry != NULL && entry->count < 3) {
            entry = entry->next;
        }
        return entry != NULL;
    }
    else {
        return TRUE;
    }
}

/*
 * Add hash_value as a position in the current game.
 * Return TRUE if a match is made, false otherwise.
 * NB: The assumption is that position_counts is not NULL.
 */
Boolean
update_position_counts(PositionCount *position_counts, HashCode hash_value)
{
    PositionCount *entry = position_counts;
    if (position_counts == NULL) {
        /* Don't try to match in variations. */
        return FALSE;
    }
    /* Try to find an existing entry. */
    while (entry != NULL && entry->hash_value != hash_value) {
        entry = entry->next;
    }
    if (entry == NULL) {
        /* New position. */
        entry = (PositionCount *) malloc_or_die(sizeof (*entry));
        entry->hash_value = hash_value;
        entry->count = 0;
        /* Insert just after the head of the list. */
        entry->next = position_counts->next;
        position_counts->next = entry;
    }
    /* Increment the count. */
    entry->count++;
    return entry->count >= 3;
}

/*
 * Free the list of position counts.
 */
void
free_position_count_list(PositionCount *position_counts)
{
    PositionCount *entry = position_counts;
    while (entry != NULL) {
        PositionCount *next = entry->next;
        (void) free((void *) entry);
        entry = next;
    }
}

/*
 * Create a new position count list.
 * This will have a single entry at its head.
 */
PositionCount *
new_position_count_list(HashCode hash_value)
{
    PositionCount *head = (PositionCount *) malloc_or_die(sizeof (*head));
    head->hash_value = hash_value;
    head->count = 1;
    head->next = NULL;
    return head;
}

/*
 * Return an identical copy of the given list.
 */
PositionCount *copy_position_count_list(PositionCount *original)
{
    PositionCount *copy = NULL;
    PositionCount *tail = NULL;
    while (original != NULL) {
        PositionCount *entry = (PositionCount *) malloc_or_die(sizeof (*entry));
        entry->hash_value = original->hash_value;
        entry->count = original->count;
        entry->next = NULL;

        if (copy == NULL) {
            copy = entry;
        }
        else {
            tail->next = entry;
        }
        tail = entry;
        original = original->next;
    }
    return copy;
}

/* Determine which table to initialise, depending
 * on whether use_virtual_hash_table is set or not.
 */
void
init_duplicate_hash_table(void)
{
    int i;

    if (GlobalState.use_virtual_hash_table) {
        VirtualLogTable = (LogHeaderEntry *)
                malloc_or_die(LOG_TABLE_SIZE * sizeof (*VirtualLogTable));
        for (i = 0; i < LOG_TABLE_SIZE; i++) {
            VirtualLogTable[i].head = VirtualLogTable[i].tail = -1;
        }
        hash_file = fopen(VIRTUAL_FILE, "w+b");
        if (hash_file == NULL) {
            fprintf(GlobalState.logfile, "Unable to open %s\n",
                    VIRTUAL_FILE);
        }
    }
    else {
        LogTable = (HashLog**) malloc_or_die(LOG_TABLE_SIZE * sizeof (*LogTable));
        for (i = 0; i < LOG_TABLE_SIZE; i++) {
            LogTable[i] = NULL;
        }
    }
}

/* Close and remove the temporary file if in use. */
void
clear_duplicate_hash_table(void)
{
    if (GlobalState.use_virtual_hash_table) {
        if (hash_file != NULL) {
            (void) fclose(hash_file);
            unlink(VIRTUAL_FILE);
            hash_file = NULL;
        }
    }
}

/* Retrieve a duplicate table entry from the hash file. */
static int
retrieve_virtual_entry(long ix, VirtualHashLog *entry)
{
    if (hash_file == NULL) {
        return 0;
    }
    else if (fseek(hash_file, ix, SEEK_SET) != 0) {
        fprintf(GlobalState.logfile,
                "Fseek error to %ld in retrieve_virtual_entry\n", ix);
        return 0;
    }
    else if (fread((void *) entry, sizeof (*entry), 1, hash_file) != 1) {
        fprintf(GlobalState.logfile,
                "Fread error from %ld in retrieve_virtual_entry\n", ix);
        return 0;
    }
    else {
        return 1;
    }
}

/* Write a duplicate table entry to the hash file. */
static int
write_virtual_entry(long where, const VirtualHashLog *entry)
{
    if (fseek(hash_file, where, SEEK_SET) != 0) {
        fprintf(GlobalState.logfile,
                "Fseek error to %ld in write_virtual_entry\n", where);
        return 0;
    }
    else if (fwrite((void *) entry, sizeof (*entry), 1, hash_file) != 1) {
        fprintf(GlobalState.logfile,
                "Fwrite error from %ld in write_virtual_entry\n", where);
        fflush(hash_file);
        return 0;
    }
    else {
        /* Written ok. */
        return 1;
    }
}

/* Return the name of the original file if it looks like we
 * have met the moves in game_details before, otherwise return
 * NULL.  A match is assumed to be so if both
 * the final_ and cumulative_ hash values in game_details
 * are already present in VirtualLogTable.
 */
static const char *
previous_virtual_occurance(Game game_details)
{
    unsigned ix = game_details.final_hash_value % LOG_TABLE_SIZE;
    VirtualHashLog entry;
    Boolean duplicate = FALSE;
    const char *original_filename = NULL;


    /* Are we keeping this information? */
    if (GlobalState.suppress_duplicates || GlobalState.suppress_originals ||
            GlobalState.duplicate_file != NULL) {
        if (VirtualLogTable[ix].head < 0l) {
            /* First occurrence. */
        }
        else {
            int keep_going =
                    retrieve_virtual_entry(VirtualLogTable[ix].head, &entry);

            while (keep_going && !duplicate) {
                if ((entry.final_hash_value == game_details.final_hash_value) &&
                        (entry.cumulative_hash_value == game_details.cumulative_hash_value)) {
                    /* We have a match.
                     * Determine where it first occured.
                     */
                    original_filename = input_file_name(entry.file_number);
                    duplicate = TRUE;
                }
                else if (entry.next >= 0l) {
                    keep_going = retrieve_virtual_entry(entry.next, &entry);
                }
                else {
                    keep_going = 0;
                }
            }
        }

        if (!duplicate) {
            /* Write an entry for it. */
            /* Where to write the next VirtualHashLog entry. */
            static long next_free_entry = 0l;

            /* Store the XOR of the two hash values. */
            entry.final_hash_value = game_details.final_hash_value;
            entry.cumulative_hash_value = game_details.cumulative_hash_value;
            entry.file_number = current_file_number();
            entry.next = -1l;

            /* Write out these details. */
            if (write_virtual_entry(next_free_entry, &entry)) {
                long where_written = next_free_entry;
                /* Move on ready for next time. */
                next_free_entry += sizeof (entry);

                /* Now update the index table. */
                if (VirtualLogTable[ix].head < 0l) {
                    /* First occurrence. */
                    VirtualLogTable[ix].head =
                            VirtualLogTable[ix].tail = where_written;
                }
                else {
                    VirtualHashLog tail;

                    if (retrieve_virtual_entry(VirtualLogTable[ix].tail, &tail)) {
                        tail.next = where_written;
                        (void) write_virtual_entry(VirtualLogTable[ix].tail, &tail);
                        /* Store the new tail address. */
                        VirtualLogTable[ix].tail = where_written;
                    }
                }
            }
        }
    }
    return original_filename;
}

/* Return the name of the original file if it looks like we
 * have met the moves in game_details before, otherwise return
 * NULL.
 * For non-fuzzy comparison, a match is assumed to be so if both
 * final_ and cumulative_ hash values are already present 
 * as a pair in LogTable.
 * Fuzzy matches depend on the match depth and do not use the
 * cumulative hash value.
 */
const char *
previous_occurance(Game game_details, int plycount)
{
    const char *original_filename = NULL;
    if (GlobalState.use_virtual_hash_table) {
        original_filename = previous_virtual_occurance(game_details);
    }
    else {

        /* Are we keeping this information? */
        if (GlobalState.suppress_duplicates ||
                GlobalState.suppress_originals ||
                GlobalState.fuzzy_match_duplicates ||
                GlobalState.duplicate_file != NULL) {
            Boolean duplicate = FALSE;
            // Entry index.
            unsigned ix;
            HashLog *entry;

            ix = game_details.final_hash_value % LOG_TABLE_SIZE;
            entry = LogTable[ix];
            /* Check for non-fuzzy matches first. */
            while (entry != NULL && !duplicate) {
                if (entry->final_hash_value == game_details.final_hash_value &&
                        entry->cumulative_hash_value == game_details.cumulative_hash_value) {
                    /* An exact match. */
                    duplicate = TRUE;
                    /* Determine where it first occurred. */
                    original_filename = input_file_name(entry->file_number);
                }
                else {
                    entry = entry->next;
                }
            }
            if (!duplicate && GlobalState.fuzzy_match_duplicates) {
                ix = game_details.fuzzy_duplicate_hash % LOG_TABLE_SIZE;
                entry = LogTable[ix];
                while (entry != NULL && !duplicate) {
                    if (GlobalState.fuzzy_match_depth == 0 &&
                            entry->final_hash_value == game_details.final_hash_value) {
                        /* Accept positional match at the end of the game. */
                        duplicate = TRUE;
                    }
                    else {
                        /* Need to check at the fuzzy_match_depth. */
                        if (entry->final_hash_value == game_details.fuzzy_duplicate_hash) {
                            duplicate = TRUE;
                        }
                    }
                    if (duplicate) {
                        /* We have a match.
                         * Determine where it first occurred.
                         */
                        original_filename = input_file_name(entry->file_number);
                    }
                    else {
                        entry = entry->next;
                    }
                }
            }

            if (!duplicate) {
                /* First occurrence, so add it to the log. */
                entry = (HashLog *) malloc_or_die(sizeof (*entry));

                if (!GlobalState.fuzzy_match_duplicates) {
                    /* Store the two hash values. */
                    entry->final_hash_value = game_details.final_hash_value;
                    entry->cumulative_hash_value = game_details.cumulative_hash_value;
                }
                else if (GlobalState.fuzzy_match_depth > 0 &&
                        plycount >= GlobalState.fuzzy_match_depth) {
                    /* Store just the hash value from the fuzzy depth. */
                    entry->final_hash_value = game_details.fuzzy_duplicate_hash;
                    entry->cumulative_hash_value = 0;
                }
                else {
                    /* Store the two hash values. */
                    entry->final_hash_value = game_details.final_hash_value;
                    entry->cumulative_hash_value = game_details.cumulative_hash_value;
                }
                entry->file_number = current_file_number();
                /* Link it into the head at this index. */
                entry->next = LogTable[ix];
                LogTable[ix] = entry;
            }
        }
    }
    return original_filename;
}
