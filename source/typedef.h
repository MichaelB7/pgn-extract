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

        /* Type definitions required by multiple files. */

    /* Define a type to represent different output formats.
     * Currently represented are:
     *     SOURCE: the original source notation.
     *           SAN: SAN.
     *           CM: Chess Master input format.
     *     LALG: Long-algebraic, e.g. e2e4.
     *     HALG: Hyphenated long-algebraic, e.g. e2-e4.
     *     ELALG: Enhanced long-algebraic. Includes piece names, e.g. Ng1f3,
     *            and en-passant notation.
     *     XLALG: Enhanced long-algebraic. Includes piece names, e.g. Ng1f3,
     *            en-passant notation and either - or x between squares for
     *            non-capture and capture moves respectively.
     *     UCI: UCI-compatible format - actually LALG.
     */
#ifndef TYPEDEF_H
#define TYPEDEF_H

typedef enum { SOURCE, SAN, EPD, CM, LALG, HALG, ELALG, XLALG, UCI } OutputFormat;

    /* Define a type to specify whether a move gives check, checkmate,
     * or nocheck.
     * checkmate implies check, but check does not imply that a move
     * is not checkmate.
     */
typedef enum { NOCHECK, CHECK, CHECKMATE } CheckStatus;

        /* Permit lists of strings, e.g. lists of comments,
         * list of NAGs, etc.
         */
typedef struct string_list {
    const char *str;
    struct string_list *next;
} StringList;

/* The following function is used for linking list items together. */
StringList *save_string_list_item(StringList *list,const char *str);

typedef struct comment_list{
    StringList *comment;
    struct comment_list *next;
} CommentList;

typedef struct variation{
    CommentList *prefix_comment;
    struct move *moves;
    CommentList *suffix_comment;
    struct variation *next;
} Variation;

/* Define a maximum length for the text of moves.
 * This is generous.
 */
#define MAX_MOVE_LEN 15

        /* Retain the text of a move and any associated 
         * NAGs and comments.
         */
typedef struct move {
    /* @@@ This array is of type unsigned char,
     * in order to accommodate full 8-bit letters without
     * sign extension.
     */
    unsigned char move[MAX_MOVE_LEN+1];
    /* Class of move, e.g. PAWN_MOVE, PIECE_MOVE. */
    MoveClass class;
    Col from_col;
    Rank from_rank;
    Col to_col;
    Rank to_rank;
    Piece piece_to_move;
    /* captured_piece is EMPTY if there is no capture. */
    Piece captured_piece;
    /* promoted_piece is EMPTY if class is not PAWN_MOVE_WITH_PROMOTION. */
    Piece promoted_piece;
    /* Whether this move gives check. */
    CheckStatus check_status;
    /* An EPD representation of the board immediately after this move
     * has been played.
     */
    char *epd;
    StringList *Nags;
    CommentList *comment_list;
    /* terminating_result hold the result of the current list of moves. */
    char *terminating_result;
    Variation *Variants;
    /* Pointers to the previous and next move.
     * The extraction program does not need the prev field, but my
     * intention is to build other interfaces that might need it.
     * For instance, a game viewer would need to be able to move backwards
     * and forwards through a game.
     */
    struct move *prev, *next;
} Move;

typedef struct {
    /* Tags for this game. */
    char **tags;
    /* The maximum number of strings in tags. */
    int tags_length;
    /* Any comment prefixing the game, between
     * the tags and the moves.
     */
    CommentList *prefix_comment;
    /* The hash value of the final position. */
    HashCode final_hash_value;
    /* An accumulated hash value, used to disambiguate false clashes
     * of final_hash_value.
     */
    HashCode cumulative_hash_value;
    /* Board hash value at fuzzy_move_depth, if required. */
    HashCode fuzzy_duplicate_hash;
    /* The move list of the game. */
    Move *moves;
    /* Whether the moves have been checked, or not. */
    Boolean moves_checked;
    /* Whether the moves are ok, or not. */
    Boolean moves_ok;
    /* if !moves_ok, the first ply at which an error was found.
     * 0 => no error found.
     */
    int error_ply;
    /* Counts of the number of times each position has been reached.
     * Used for repetition detection, if required.
     */
    struct PositionCount *position_counts;
} Game;

/* Define a type to distinguish between CHECK files, NORMAL files,
 * and ECO files.
 * CHECKFILEs are those whose contents are not output.
 * Their contents are used to check for duplicates in NORMALFILEs.
 * An ECOFILE consists of ECO lines for classification.
 */
typedef enum { NORMALFILE, CHECKFILE, ECOFILE } SourceFileType;

/*    0 = don't divide on ECO code.
 *    1 = divide by letter.
 *    2 = divide by letter and single digit.
 *    N > 1 = divide by letter and N-1 digits.
 *    In principle, it should be possible to expand the ECO classification
 *    with an arbitrary number of digits.
 */
typedef enum {
    DONT_DIVIDE = 0, MIN_ECO_LEVEL = 1, MAX_ECO_LEVEL = 10
} EcoDivision;

/* Define a type to describe which tags are to be output.
 */
typedef enum {
    ALL_TAGS = 0, SEVEN_TAG_ROSTER = 1, NO_TAGS = 2
} TagOutputForm;

/* Whether games with a SETUP_TAG should be kept. */
typedef enum {
    SETUP_TAG_OK, NO_SETUP_TAG, SETUP_TAG_ONLY,
} SetupOutputStatus;

/* A type to support the storing of a list of game numbers.
 * Used to support the --selectonly and --skip arguments.
 */
typedef struct game_number {
    unsigned long min, max;
    struct game_number *next;
} game_number;

/* This structure holds details of the program state.
 * Most of these fields are set from the program's arguments.
 */
typedef struct {
    /* Whether we are skipping the current game - typically because
     * of an error in its text.
     */
    Boolean skipping_current_game;
    /* Whether to check, but not write the converted output. */
    Boolean check_only;
    /* Verbosity level.
     * 0 -> nothing at all.
     * 1 -> only the number of games processed.
     * 2 -> a running commentary to logfile.
     */
    int verbosity;
    /* Whether to keep NAGs along with moves. */
    Boolean keep_NAGs;
    /* Whether to keep comments along with moves. */
    Boolean keep_comments;
    /* Whether to keep variations along with moves. */
    Boolean keep_variations;
    /* Which tags are to be output. */
    TagOutputForm tag_output_format;
    /* Whether to match permutations of textual variations or not. */
    Boolean match_permutations;
    /* Whether we are matching positional variations or not. */
    Boolean positional_variations;
    /* Whether we are using Soundex matching or not. */
    Boolean use_soundex;
    /* Whether to suppress duplicate game scores. */
    Boolean suppress_duplicates;
    /* Whether to suppress unique game scores. */
    Boolean suppress_originals;
    /* Whether to use fuzzy matching for duplicates. */
    Boolean fuzzy_match_duplicates;
    /* At what depth to use fuzzy matching. */
    int fuzzy_match_depth;
    /* Whether to check the tags for matches. */
    Boolean check_tags;
    /* Whether to add ECO codes. */
    Boolean add_ECO;
    /* Whether an ECO file is currently being parsed. */
    Boolean parsing_ECO_file;
    /* Which level to divide the output. */
    EcoDivision ECO_level;
    /* What form to write the output in. */
    OutputFormat output_format;
    /* Maximum output line length. */
    unsigned max_line_length;
    /* Whether to use a virtual hash table or not. */
    Boolean use_virtual_hash_table;
    /* Whether to match on the number of moves in a game. */
    Boolean check_move_bounds;
    /* Whether to match only games ending in checkmate. */
    Boolean match_only_checkmate;
    /* Whether to match only games ending in stalemate. */
    Boolean match_only_stalemate;
    /* Whether to output move numbers in the output. */
    Boolean keep_move_numbers;
    /* Whether to output results in the output. */
    Boolean keep_results;
    /* Whether to keep check and mate characters in the output. */
    Boolean keep_checks;
    /* Whether to output an evaluation value after each move. */
    Boolean output_evaluation;
    /* Whether to keep games which have incorrect moves. */
    Boolean keep_broken_games;
    /* Whether to suppress irrelevant ep info in EPD and FEN output. */
    Boolean suppress_redundant_ep_info;
    /* Whether the output should be in JSON format. */
    Boolean json_format;
    /* Whether to check for three-fold repetition. */
    Boolean check_for_repetition;
    /* Whether to check for 50-move draw games. */
    Boolean check_for_fifty_move_rule;
    /* Whether tag matches can be made other than at the start of the tag. */
    Boolean tag_match_anywhere;
    /* Maximum depth to which to search for positional variations.
     * This is picked up from the length of variations in the positional
     * variations file.
     */
    unsigned depth_of_positional_search;
    unsigned long num_games_processed;
    unsigned long num_games_matched;
    /* How many games to store in each file. */
    unsigned games_per_file;
    /* Which is the next file number. */
    unsigned next_file_number;
    /* Lower and upper bounds for moves if check_move_bounds.
     * From v17-33 these values are ply rather than moves.
     */
    unsigned lower_move_bound, upper_move_bound;
    /* Limit to the number of plies to appear in the output. */
    int output_ply_limit;
    /* How quiescent the game needs to be for it to be output. */
    unsigned quiescence_threshold;
    /* Maximum number to output (maximum_matches > 0) */
    unsigned long maximum_matches;
    /* Whether to output a FEN string. Either at the end of the game
     * or replacing a matching comment (see FEN_comment_pattern). */
    Boolean output_FEN_string;
    /* Whether to add a FEN comment after every move. */
    Boolean add_FEN_comments;
    /* Whether to add a hashcode comment after every move. */
    Boolean add_hashcode_comments;
    /* Whether to add a 'matching position' comment. */
    Boolean add_position_match_comments;
    /* Whether to include a tag with the total ply count of the game. */
    Boolean output_total_plycount;
    /* Whether to add a HashCode tag. */
    Boolean add_hashcode_tag;
    /* Whether to fix a Result tag that does not match the game outcome. */
    Boolean fix_result_tags;
    /* Whether this is a CHECKFILE or a NORMALFILE. */
    SourceFileType current_file_type;
    /* Whether SETUP_TAGs are ok in extracted games. */
    SetupOutputStatus setup_status;
    /* The comment to use for position matches, if required. */
    const char *position_match_comment;
    /* The comment pattern to match for FEN comments (see output_FEN_string) */
    const char *FEN_comment_pattern;
    /* Current input file name. */
    const char *current_input_file;
    /* File of ECO lines. */
    const char *eco_file;
    /* Where to write the extracted games. */
    FILE *outputfile;
    /* Output file name. */
    const char *output_filename;
    /* Where to write errors and running commentary. */
    FILE *logfile;
    /* Where to write duplicate games. */
    FILE *duplicate_file;
    /* Where to write games that don't match the criteria. */
    FILE *non_matching_file;
    /* Which game numbers to output (matching_game_numbers != NULL) */
    game_number *matching_game_numbers;
    /* Which game number to output next (matching_game_numbers != NULL) */
    game_number *next_game_number_to_output;
    /* Which game numbers to skip (skip_game_numbers != NULL) */
    game_number *skip_game_numbers;
    /* Which game number to skip next (skip_game_numbers != NULL) */
    game_number *next_game_number_to_skip;
} StateInfo;

/* Provide access to the global state that has been set
 * through command line arguments.
 */
extern StateInfo GlobalState;
FILE *must_open_file(const char *filename,const char *mode);

#endif	// TYPEDEF_H

