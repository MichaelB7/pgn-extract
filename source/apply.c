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
#include "map.h"
#include "apply.h"
#include "tokens.h"
#include "taglist.h"
#include "output.h"
#include "lex.h"
#include "grammar.h"
#include "moves.h"
#include "eco.h"
#include "decode.h"
#include "hashing.h"
#include "fenmatcher.h"
#include "zobrist.h"

/* Define a positional search depth that should look at the
 * full length of a game.  This is used in play_moves().
 */
#define DEFAULT_POSITIONAL_DEPTH 300

/* Prototypes of functions limited to this file. */
static Boolean position_matches(const Board *board);
static Boolean play_moves(Game *game_details, Board *board, Move *moves,
        unsigned max_depth, Boolean check_move_validity,
        Boolean mainline);
static Boolean apply_variations(const Game *game_details, const Board *board,
        Variation *variation, Boolean check_move_validity);
static Boolean rewrite_variations(const Board *board, Variation *variation);
static Boolean rewrite_moves(Game *game, Board *board, Move *move_details);
static void append_evaluation(Move *move_details, const Board *board);
static void append_FEN_comment(Move *move_details, const Board *board);
static void append_hashcode_comment(Move *move_details, const Board *board);
static double evaluate(const Board *board);
static double shannonEvaluation(const Board *board);
static void print_board(const Board *board, FILE *outfp);

/* The English SAN piece characters. These are
 * always used when building a FEN string, rather
 * than using any language-dependent user settings.
 */
static char SAN_piece_characters[NUM_PIECE_VALUES] = {
    '?', '?',
    'P', 'N', 'B', 'R', 'Q', 'K'
};


/* These letters may be changed via a call to set_output_piece_characters
 * with a string of the form "PNBRQK".
 * This would normally be done with the -Wsan argument.
 */
static const char *output_piece_characters[NUM_PIECE_VALUES] = {
    "?", "?",
    "P", "N", "B", "R", "Q", "K"
};

/* letters should contain a string of the form: "PNBRQK" */
void set_output_piece_characters(const char *letters)
{
    if (letters == NULL) {
        fprintf(GlobalState.logfile,
                "NULL string passed to set_output_piece_characters.\n");
    }
    else {
        Piece piece;
        int piece_index;
        for (piece_index = 0, piece = PAWN; piece <= KING &&
                letters[piece_index] != '\0'; piece++) {
            /* Check whether we have a single character piece, 
             * or one of the form X+Y, where the piece is represented
             * by the combination XY.
             */
            if (letters[piece_index + 1] == '+') {
                /* A two-char piece. */
                static char double_char_piece[] = "XY";
                double_char_piece[0] = letters[piece_index];
                piece_index++;
                /* Skip the plus. */
                piece_index++;
                if (letters[piece_index] != '\0') {
                    double_char_piece[1] = letters[piece_index];
                    output_piece_characters[piece] = copy_string(double_char_piece);
                    piece_index++;
                }
                else {
                    fprintf(GlobalState.logfile,
                            "Missing piece letter following + in -Wsan%s.\n",
                            letters);
                    exit(1);
                }
            }
            else {
                static char single_char_piece[] = "X";
                *single_char_piece = letters[piece_index];
                output_piece_characters[piece] = copy_string(single_char_piece);
                piece_index++;
            }
        }
        if (piece < NUM_PIECE_VALUES) {
            fprintf(GlobalState.logfile,
                    "Insufficient piece letters found with -Wsan%s.\n",
                    letters);
            fprintf(GlobalState.logfile,
                    "The argument should be of the form -Wsan%s.\n",
                    "PNBRQK");
            exit(1);
        }
        else if (letters[piece_index] != '\0') {
            fprintf(GlobalState.logfile,
                    "Too many piece letters found with -Wsan%s.\n",
                    letters);
            fprintf(GlobalState.logfile,
                    "The argument should be of the form -Wsan%s.\n",
                    "PNBRQK");
            exit(1);
        }
        else {
            /* Ok. */
        }
    }
}

/* Return a fresh copy of the given string. */
char *
copy_string(const char *str)
{
    char *result;
    size_t len = strlen(str);

    result = malloc_or_die(len + 1);
    strcpy(result, str);
    return result;
}

/* Allocate space for a new board. */
static Board *
allocate_new_board(void)
{
    return (Board *) malloc_or_die(sizeof (Board));
}

/* Free the board space. */
static void
free_board(Board *board)
{
    (void) free((void *) board);
}

Piece
convert_FEN_char_to_piece(char c)
{
    Piece piece = EMPTY;

    switch (c) {
        case 'K': case 'k':
            piece = KING;
            break;
        case 'Q': case 'q':
            piece = QUEEN;
            break;
        case 'R': case 'r':
            piece = ROOK;
            break;
        case 'N': case 'n':
            piece = KNIGHT;
            break;
        case 'B': case 'b':
            piece = BISHOP;
            break;
        case 'P': case 'p':
            piece = PAWN;
            break;
    }
    return piece;
}

/* Return the SAN letter associated with the given piece. */
char
SAN_piece_letter(Piece piece)
{
    if (piece < NUM_PIECE_VALUES) {
        return SAN_piece_characters[piece];
    }
    else {
        return '?';
    }
}

/* Return the SAN letter for the given Piece. */
char
coloured_piece_to_SAN_letter(Piece coloured_piece)
{
    Piece piece = EXTRACT_PIECE(coloured_piece);
    char letter = SAN_piece_letter(piece);
    if (EXTRACT_COLOUR(coloured_piece) == BLACK) {
        letter = tolower(letter);
    }
    return letter;
}

/* Find the position of the innermost or outermost rook for
 * the given castling move.
 */
static Col
find_rook_starting_position(const Board *board, Colour colour, MoveClass castling, Boolean outermost)
{
    Rank rank;
    Col col, boundary;
    int direction;
    Piece rook = MAKE_COLOURED_PIECE(colour, ROOK);
    /* Default search is outermost for the first rook and rook_count is 1.
     * If the innermost is required then the rook_count is 2.
     */
    int rook_count;
    if (outermost) {
        rook_count = 1;
    }
    else {
        rook_count = 2;
    }

    if (colour == WHITE) {
        rank = FIRSTRANK;
    }
    else {
        rank = LASTRANK;
    }
    if (castling == KINGSIDE_CASTLE) {
        direction = -1;
        col = LASTCOL;
        boundary = FIRSTCOL - 1;
    }
    else {
        direction = 1;
        col = FIRSTCOL;
        boundary = LASTCOL + 1;
    }
    while (col != boundary && rook_count > 0) {
        if (board->board[RankConvert(rank)][ColConvert(col)] != rook) {
            col += direction;
        }
        else {
            rook_count--;
            if (rook_count != 0) {
                col += direction;
            }
        }
    }
    if (col != boundary) {
        return colour == WHITE ? col : col;
    }
    else {
        return '\0';
    }
}

/* Set up the board from the FEN string passed as
 * argument.
 * This has the form:
 *        Forsythe string of the setup position.
 *        w/b - colour to move.
 *        castling permissions.
 *        en-passant square.
 *        half-moves since pawn move/piece capture.
 *        move number.
 */
Board *
new_fen_board(const char *fen)
{
    Board *new_board = allocate_new_board();
    /* Start with a clear board. */
    static const Board initial_board = {
        {
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF},
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF},
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF}
        },
        /* Who to move next. */
        WHITE,
        /* Move number. */
        1,
        /* Default castling rights. Support Chess960. */
        'h', 'a', 'h', 'a',
        /* Initial king positions. */
        'e', FIRSTRANK,
        'e', LASTRANK,
        /* En Passant rights. */
        FALSE, 0, 0,
        /* Initial hash value. */
        0ul,
        /* half-move_clock */
        0,
    };
    Rank rank = LASTRANK;
    Col col;
    const char *fen_char = fen;
    Boolean Ok = TRUE;
    /* In some circumstances we will try to parse the game data,
     * even if there are errors.
     */
    Boolean try_to_parse_game = FALSE;

    /* Reset the contents of the new board. */
    *new_board = initial_board;
    /* Extract the piece positions. */
    col = FIRSTCOL;
    while (Ok && (*fen_char != ' ') && (*fen_char != '\0') &&
            (rank >= FIRSTRANK)) {
        Piece piece;
        char ch = *fen_char;
        Colour colour;

        if ((piece = convert_FEN_char_to_piece(ch)) != EMPTY) {
            if (isupper((int) ch)) {
                colour = WHITE;
            }
            else {
                colour = BLACK;
            }
            if (col <= LASTCOL) {
                new_board->board[RankConvert(rank)][ColConvert(col)] =
                        MAKE_COLOURED_PIECE(colour, piece);
                if (piece == KING) {
                    if (colour == WHITE) {
                        new_board->WKingCol = col;
                        new_board->WKingRank = rank;
                    }
                    else {
                        new_board->BKingCol = col;
                        new_board->BKingRank = rank;
                    }
                }
                col++;
                fen_char++;
            }
            else {
                Ok = FALSE;
            }
        }
        else if (isdigit((int) ch)) {
            if (('1' <= ch) && (ch <= '8')) {
                col += ch - '0';
                /* In filling up the remaining columns of a rank we will
                 * temporarily exceed LASTCOL, but we expect the
                 * next character to be '/' so that we reset.
                 */
                if (col <= (LASTCOL + 1)) {
                    fen_char++;
                }
                else {
                    Ok = FALSE;
                }
            }
            else {
                Ok = FALSE;
            }
        }
        else if (ch == '/') {
            /* End of that rank. We should have completely filled the
             * previous rank.
             */
            if (col == (LASTCOL + 1)) {
                col = FIRSTCOL;
                rank--;
                fen_char++;
            }
            else {
                Ok = FALSE;
            }
        }
        else {
            /* Unknown character. */
            Ok = FALSE;
        }
    }
    /* As we don't print any error messages until the end of the function,
     * we don't need to guard everything with if(Ok).
     */
    if (*fen_char == ' ') {
        /* Find out who is to move. */
        fen_char++;
    }
    else {
        Ok = FALSE;
    }
    if (*fen_char == 'w') {
        new_board->to_move = WHITE;
        fen_char++;
    }
    else if (*fen_char == 'b') {
        new_board->to_move = BLACK;
        fen_char++;
    }
    else {
        Ok = FALSE;
    }
    if (*fen_char == ' ') {
        fen_char++;
    }
    else {
        Ok = FALSE;
    }
    /* Determine castling rights. */
    if (*fen_char == '-') {
        /* No castling rights -- default above. */
        new_board->WKingCastle = new_board->WQueenCastle =
                new_board->BKingCastle = new_board->BQueenCastle = '\0';
        fen_char++;
    }
    else {
        /* Check to make sure that this section isn't empty. */
        if (*fen_char == ' ') {
            Ok = FALSE;
        }

        /* Accommodate Chess960 notation for castling. */
        if (*fen_char == 'K') {
            new_board->WKingCastle = find_rook_starting_position(new_board, WHITE, KINGSIDE_CASTLE, TRUE);
            fen_char++;
        }
        else if (*fen_char >= 'A' && *fen_char <= 'H') {
            new_board->WKingCastle = tolower(*fen_char);
            fen_char++;
        }
        else {
            new_board->WKingCastle = '\0';
        }
        if (*fen_char == 'Q') {
            new_board->WQueenCastle = find_rook_starting_position(new_board, WHITE, QUEENSIDE_CASTLE, TRUE);
            fen_char++;
        }
        else if (*fen_char >= 'A' && *fen_char <= 'H') {
            new_board->WQueenCastle = tolower(*fen_char);
            fen_char++;
        }
        else {
            new_board->WQueenCastle = '\0';
        }
        if (*fen_char == 'k') {
            new_board->BKingCastle = find_rook_starting_position(new_board, BLACK, KINGSIDE_CASTLE, TRUE);
            fen_char++;
        }
        else if (*fen_char >= 'a' && *fen_char <= 'h') {
            new_board->BKingCastle = *fen_char;
            fen_char++;
        }
        else {
            new_board->BKingCastle = '\0';
        }
        if (*fen_char == 'q') {
            new_board->BQueenCastle = find_rook_starting_position(new_board, BLACK, QUEENSIDE_CASTLE, TRUE);
            fen_char++;
        }
        else if (*fen_char >= 'a' && *fen_char <= 'h') {
            new_board->BQueenCastle = *fen_char;
            fen_char++;
        }
        else {
            new_board->BQueenCastle = '\0';
        }
    }
    if (*fen_char == ' ') {
        fen_char++;
    }
    else {
        Ok = FALSE;
    }
    /* If we are ok to this point, try to make a best efforts approach
     * to handle the game, even if there are subsequent errors.
     */
    if (Ok) {
        try_to_parse_game = TRUE;
    }
    /* Check for an en-passant square. */
    if (*fen_char == '-') {
        /* None. */
        fen_char++;
    }
    else if (is_col(*fen_char)) {
        col = *fen_char;
        fen_char++;
        if (is_rank(*fen_char)) {
            rank = *fen_char;
            fen_char++;
            /* Make sure that the en-passant indicator is consistent
             * with whose move it is.
             */
            if (((new_board->to_move == WHITE) && (rank == '6')) ||
                    ((new_board->to_move == BLACK) && (rank == '3'))) {
                /* Consistent. */
                new_board->EnPassant = TRUE;
                new_board->ep_rank = rank;
                new_board->ep_col = col;
            }
            else {
                Ok = FALSE;
            }
        }
        else {
            Ok = FALSE;
        }
    }
    else {
        Ok = FALSE;
    }
    if (*fen_char == ' ') {
        fen_char++;
    }
    else {
        Ok = FALSE;
    }
    /* Check for half-move count since last pawn move
     * or capture.
     */
    if (isdigit((int) *fen_char)) {
        unsigned halfmove_clock = *fen_char - '0';
        fen_char++;
        while (isdigit((int) *fen_char)) {
            halfmove_clock = (halfmove_clock * 10)+(*fen_char - '0');
            fen_char++;
        }
        new_board->halfmove_clock = halfmove_clock;
    }
    else {
        Ok = FALSE;
    }
    if (*fen_char == ' ') {
        fen_char++;
    }
    else {
        Ok = FALSE;
    }
    /* Check for current move number. */
    if (isdigit((int) *fen_char)) {
        unsigned move_number = 0;

        move_number = *fen_char - '0';
        fen_char++;
        while (isdigit((int) *fen_char)) {
            move_number = (move_number * 10)+(*fen_char - '0');
            fen_char++;
        }
        if (move_number < 1) {
            move_number = 1;
        }
        new_board->move_number = move_number;
    }
    else {
        Ok = FALSE;
    }
    /* Allow trailing space. */
    while (isspace((int) *fen_char)) {
        fen_char++;
    }
    if (*fen_char != '\0') {
        Ok = FALSE;
    }
    if (!Ok) {
        fprintf(GlobalState.logfile, "Illegal FEN string %s at %s", fen, fen_char);
        if (try_to_parse_game) {
            fprintf(GlobalState.logfile, " Attempting to parse the game, anyway.");
        }
        else {
            (void) free_board((void *) new_board);
            new_board = NULL;
        }
        putc('\n', GlobalState.logfile);
        report_details(GlobalState.logfile);
    }
    return new_board;
}

/* Set up a board structure for a new game.
 * This involves placing the pieces in their initial positions,
 * setting up castling and en-passant rights, and initialising
 * the hash positions.
 * If the fen argument is NULL then a completely new board is
 * setup, otherwise the indicated FEN position is returned.
 */
static Board *
new_game_board(const char *fen)
{
    Board *new_board;
    static const Board initial_board ={
        {
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF},
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF},
            { OFF, OFF, W(ROOK), W(KNIGHT), W(BISHOP), W(QUEEN),
                W(KING), W(BISHOP), W(KNIGHT), W(ROOK), OFF, OFF},
            { OFF, OFF, W(PAWN), W(PAWN), W(PAWN), W(PAWN),
                W(PAWN), W(PAWN), W(PAWN), W(PAWN), OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, OFF, OFF},
            { OFF, OFF, B(PAWN), B(PAWN), B(PAWN), B(PAWN),
                B(PAWN), B(PAWN), B(PAWN), B(PAWN), OFF, OFF},
            { OFF, OFF, B(ROOK), B(KNIGHT), B(BISHOP), B(QUEEN),
                B(KING), B(BISHOP), B(KNIGHT), B(ROOK), OFF, OFF},
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF},
            { OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF, OFF}
        },
        /* Who to move next. */
        WHITE,
        /* Move number. */
        1,
        /* Castling rights. Support Chess960. */
        'h', 'a', 'h', 'a',
        /* Initial king positions. */
        'e', FIRSTRANK,
        'e', LASTRANK,
        /* En Passant rights. */
        FALSE, 0, 0,
        /* Initial hash value. */
        0ul,
        /* half-move_clock */
        0,
    };
    /* Iterate over the columns. */
    Col col;

    if (fen != NULL) {
        new_board = new_fen_board(fen);
    }
    /* Guard against failure of new_fen_board as well as the
     * normal game situation.
     */
    if ((fen == NULL) || (new_board == NULL)) {
        /* Use the initial board setup. */
        new_board = allocate_new_board();
        *new_board = initial_board;
    }

    /* Generate the hash value for the initial position. */
    for (col = FIRSTCOL; col <= LASTCOL; col++) {
        Rank rank;

        for (rank = FIRSTRANK; rank <= LASTRANK; rank++) {
            /* Find the basic components. */
            Piece coloured_piece = new_board->board[
                    RankConvert(rank)][ColConvert(col)];
            Piece piece = EXTRACT_PIECE(coloured_piece);
            Colour colour = EXTRACT_COLOUR(coloured_piece);

            if (coloured_piece != EMPTY) {
                new_board->hash_value ^= hash_lookup(col, rank, piece, colour);
            }
        }
    }
    return new_board;
}

/* Print out the current occupant of the given square. */
static void
print_square(Col col, Rank rank, const Board *board, FILE *outfp)
{
    int r = RankConvert(rank);
    int c = ColConvert(col);

    Piece coloured_piece = board->board[r][c];
    switch ((int) coloured_piece) {
        case W(PAWN):
        case W(KNIGHT):
        case W(BISHOP):
        case W(ROOK):
        case W(QUEEN):
        case W(KING):
        case B(PAWN):
        case B(KNIGHT):
        case B(BISHOP):
        case B(ROOK):
        case B(QUEEN):
        case B(KING):
            putc(coloured_piece_to_SAN_letter(coloured_piece), outfp);
            break;
        case EMPTY:
            putc('.', outfp);
            break;
        case OFF:
            putc('?', outfp);
            break;
        default:
            fprintf(GlobalState.logfile,
                    "Attempt to print illegal square %c%c in print_square.\n",
                    col, rank);
            break;
    }
}

/* Print out the contents of the given board. */
static void
print_board(const Board *board, FILE *outfp)
{
    Rank rank;
    Col col;

    for (rank = LASTRANK; rank >= FIRSTRANK; rank--) {
        for (col = FIRSTCOL; col <= LASTCOL; col++) {
            print_square(col, rank, board, outfp);
        }
        putc('\n', outfp);
    }
    putc('\n', outfp);
}

#if INCLUDE_UNUSED_FUNCTIONS

/* Check the consistency of the board. */
static void
check_board(const Board *board, const char *where)
{
    Rank rank;
    Col col;

    for (rank = LASTRANK; rank >= FIRSTRANK; rank--) {
        for (col = FIRSTCOL; col <= LASTCOL; col++) {
            int r = RankConvert(rank);
            int c = ColConvert(col);

            switch (board->board[r][c]) {
                case W(PAWN):
                case W(KNIGHT):
                case W(BISHOP):
                case W(ROOK):
                case W(QUEEN):
                case W(KING):
                case B(PAWN):
                case B(KNIGHT):
                case B(BISHOP):
                case B(ROOK):
                case B(QUEEN):
                case B(KING):
                case EMPTY:
                    break;
                default:
                    fprintf(GlobalState.logfile,
                            "%s: Illegal square %c%c (%u %u) contains %d.\n",
                            where, col, rank, r, c, board->board[r][c]);
                    report_details(GlobalState.logfile);
                    abort();
                    break;
            }
        }
    }
}
#endif

/* Return the number of half moves that have been completed
 * on the board.
 */
static int
half_moves_played(const Board *board)
{
    int half_moves = 2 * (board->move_number - 1);
    if (board->to_move == BLACK) {
        half_moves++;
    }
    return half_moves;
}

/* Implement move_details on the board.
 * Return TRUE if the move is ok, FALSE otherwise.
 * move_details is completed by the call to determine_move_details.
 * Thereafter, it is safe to make the move on board.
 */
static Boolean
apply_move(Colour colour, Move *move_details, Board *board)
{ /* Assume success. */
    Boolean Ok = TRUE;

    if (determine_move_details(colour, move_details, board)) {
        Piece piece_to_move = move_details->piece_to_move;

        if (GlobalState.output_format == EPD) {
            move_details->epd = (char *) malloc_or_die(FEN_SPACE);
            build_basic_EPD_string(board, move_details->epd);
        }

        if (move_details->class != NULL_MOVE) {
            make_move(move_details->class, move_details->from_col, move_details->from_rank,
                    move_details->to_col, move_details->to_rank,
                    piece_to_move, colour, board);
        }
        /* See if there are any subsidiary actions. */
        switch (move_details->class) {
            case PAWN_MOVE:
            case PIECE_MOVE:
            case ENPASSANT_PAWN_MOVE:
                /* Nothing more to do. */
                break;
            case PAWN_MOVE_WITH_PROMOTION:
                if (move_details->promoted_piece != EMPTY) {
                    /* Now make the promotion. */
                    make_move(move_details->class, move_details->to_col, move_details->to_rank,
                            move_details->to_col, move_details->to_rank,
                            move_details->promoted_piece, colour, board);
                }
                else {
                    Ok = FALSE;
                }
                break;
            case KINGSIDE_CASTLE:
                break;
            case QUEENSIDE_CASTLE:
                break;
            case NULL_MOVE:
                /* Nothing more to do. */
                break;
            case UNKNOWN_MOVE:
            default:
                Ok = FALSE;
                break;
        }
        /* Determine whether or not this move gives check. */
        if (Ok) {
            move_details->check_status =
                    king_is_in_check(board, OPPOSITE_COLOUR(colour));
            if (move_details->check_status == CHECK) {
                /* See whether it is checkmate. */
                if (king_is_in_checkmate(OPPOSITE_COLOUR(colour), board)) {
                    move_details->check_status = CHECKMATE;
                }
            }
            /* Get ready for the next move. */
            board->to_move = OPPOSITE_COLOUR(board->to_move);
            if (board->to_move == WHITE) {
                board->move_number++;
            }
        }
    }
    else {
        Ok = FALSE;
    }
    return Ok;
}

/* Play out the moves on the given board.
 * game_details is updated with the final_ and cumulative_ hash
 * values.
 * Check move validity unless a NULL_MOVE has been found in this
 * variation.
 * Return TRUE if the game is valid and matches all matching criteria,
 * FALSE otherwise.
 */
static Boolean
play_moves(Game *game_details, Board *board, Move *moves, unsigned max_depth,
        Boolean check_move_validity,
        Boolean mainline)
{
    Boolean game_ok = TRUE;
    /* Ply number at which any error was found. */
    int error_ply = 0;
    /* Force a match if we aren't looking for positional variations. */
    Boolean game_matches = !GlobalState.positional_variations;
    Move *next_move = moves;
    /* Keep track of the final ECO match. */
    EcoLog *eco_match = NULL;
    /* Whether a NULL_MOVE was found in the main line. */
    Boolean null_move_in_main_line = FALSE;
    /* Whether the fifty-move rule was available in the main line. */
    Boolean fifty_move_rule_applies = FALSE;
    
    /* Try the initial board position for a match.
     * This is required because the game might have been set up
     * from a FEN string, rather than being the normal starting
     * position.
     */
    if (!game_matches && position_matches(board)) {
        game_matches = TRUE;
        if (GlobalState.add_position_match_comments) {
            CommentList *comment = create_match_comment(next_move);
            comment->next = game_details->prefix_comment;
            game_details->prefix_comment = comment;
        }
    }
    /* Keep going while the game is ok, and we have some more
     * moves and we haven't exceeded the search depth without finding
     * a match.
     */
    while (game_ok && (next_move != NULL) &&
            (game_matches || (board->move_number <= max_depth))) {
        if (*(next_move->move) != '\0') {
            /* See if there are any variations associated with this move. */
            if ((next_move->Variants != NULL) && GlobalState.keep_variations) {
                game_matches |= apply_variations(game_details, board,
                        next_move->Variants,
                        check_move_validity);
            }
            /* Now try the main move. */
            if (next_move->class == NULL_MOVE) {
                null_move_in_main_line = TRUE;
                /* We might not be able to check the validity of
                 * subsequent moves.
                 */
#if 0
                check_move_validity = FALSE;
#endif
            }
            if (check_move_validity) {
                if (apply_move(board->to_move, next_move, board)) {
                    /* Don't try for a positional match if we already have one. */
                    if (!game_matches && position_matches(board)) {
                        game_matches = TRUE;
                        if (GlobalState.add_position_match_comments) {
                            CommentList *comment = create_match_comment(next_move);
                            append_comments_to_move(next_move, comment);
                        }
                    }
                    /* Combine this hash value with the cumulative one. */
                    game_details->cumulative_hash_value += board->hash_value;
                    if (GlobalState.fuzzy_match_duplicates) {
                        int plies = 2 * (board->move_number - 1);
                        if (board->to_move == BLACK) {
                            plies++;
                        }
                        /* Consider remembering this hash value for fuzzy matches. */
                        if (GlobalState.fuzzy_match_depth == plies) {
                            /* Remember it. */
                            game_details->fuzzy_duplicate_hash = board->hash_value;
                        }
                    }

                    if (GlobalState.check_for_repetition) {
                        Boolean repetition = update_position_counts(game_details->position_counts, board->hash_value);
                        if (repetition && GlobalState.add_position_match_comments) {
                            CommentList *comment = create_match_comment(next_move);
                            append_comments_to_move(next_move, comment);
                        }
                    }

                    if (GlobalState.check_for_fifty_move_rule && mainline) {
                        if (board->halfmove_clock >= 100) {
                            /* Fifty moves by both players with no pawn move or capture. */
                            fifty_move_rule_applies = TRUE;
                            if (GlobalState.add_position_match_comments) {
                                CommentList *comment = create_match_comment(next_move);
                                append_comments_to_move(next_move, comment);
                            }
                        }
                    }

                    if (next_move->next == NULL && mainline) {
                        if (GlobalState.fuzzy_match_duplicates &&
                                GlobalState.fuzzy_match_depth == 0) {
                            game_details->fuzzy_duplicate_hash = board->hash_value;
                        }
                        /* Ensure that the result tag is consistent with the
                         * final status of the game.
                         */
                        const char *result = game_details->tags[RESULT_TAG];
                        const char *corrected_result = NULL;
                        if (result != NULL) {
                            if (next_move->check_status == CHECKMATE) {
                                if (board->to_move == BLACK) {
                                    if (strncmp(result, "1-0", 3) != 0) {
                                        fprintf(GlobalState.logfile,
                                                "Warning: Result of %s is inconsistent with checkmate by white in\n", result);
                                        report_details(GlobalState.logfile);
                                        if (GlobalState.fix_result_tags) {
                                            corrected_result = "1-0";
                                        }
                                    }
                                }
                                else {
                                    if (strncmp(result, "0-1", 3) != 0) {
                                        fprintf(GlobalState.logfile,
                                                "Warning: Result of %s is inconsistent with checkmate by black in\n", result);
                                        report_details(GlobalState.logfile);
                                        if (GlobalState.fix_result_tags) {
                                            corrected_result = "0-1";
                                        }
                                    }
                                }
                            }
                            else {
                                if (is_stalemate(board, NULL)) {
                                    if (strncmp(result, "1/2", 3) != 0) {
                                        fprintf(GlobalState.logfile,
                                                "Warning: Result of %s is inconsistent with stalemate in\n", result);
                                        report_details(GlobalState.logfile);
                                        if (GlobalState.fix_result_tags) {
                                            corrected_result = "1/2-1/2";
                                        }
                                    }
                                }
                            }
                            if (corrected_result != NULL) {
                                free((void *) result);
                                game_details->tags[RESULT_TAG] = copy_string(corrected_result);
                            }
                        }
                    }

                    if (GlobalState.add_ECO && !GlobalState.parsing_ECO_file) {
                        int half_moves = half_moves_played(board);
                        EcoLog *entry = eco_matches(
                                board->hash_value,
                                game_details->cumulative_hash_value,
                                half_moves);
                        if (entry != NULL) {
                            /* Consider keeping the match.
                             * Could try to avoid spurious matches which become
                             * more likely with larger ECO files and
                             * the longer a game goes on.
                             * Could be mitigated partly by preferring
                             * an ECO line of exactly the same length as
                             * the current game line.
                             * Not currently implemented.
                             */
                            if (eco_match == NULL) {
                                /* We don't have one yet. */
                                eco_match = entry;
                            }
                            else {
                                /* Keep it anyway.
                                 * This logic always prefers a longer match
                                 * to a inter, irrespective of whether
                                 * either match is exact or not.
                                 * This logic was followed in versions
                                 * up to and including v13.8.
                                 */
                                eco_match = entry;
                            }
                        }
                    }
                    next_move = next_move->next;
                }
                else {
                    print_error_context(GlobalState.logfile);
                    fprintf(GlobalState.logfile,
                            "Failed to make move %u%s %s in the game:\n",
                            board->move_number,
                            (board->to_move == WHITE) ? "." : "...",
                            next_move->move);
                    print_board(board, GlobalState.logfile);
                    report_details(GlobalState.logfile);
                    game_ok = FALSE;
                    /* Work out where the error was. */
                    error_ply = 2 * board->move_number - 1;
                    /* Check who has just moved. */
                    if (board->to_move == BLACK) {
                        error_ply++;
                    }
                }
            }
            else {
                /* Go through the motions as if the move were checked. */
                board->to_move = OPPOSITE_COLOUR(board->to_move);
                if (board->to_move == WHITE) {
                    board->move_number++;
                }
                next_move = next_move->next;
            }
        }
        else {
            /* An empty move. */
            fprintf(GlobalState.logfile,
                    "Internal error: Empty move in play_moves.\n");
            report_details(GlobalState.logfile);
            game_ok = FALSE;
            /* Work out where the error was. */
            error_ply = 2 * board->move_number - 1;
            /* Check who has just moved. */
            if (board->to_move == BLACK) {
                error_ply++;
            }
        }
    }
    /* Record whether the full game was checked or not. */
    game_details->moves_checked = next_move == NULL;

    if (null_move_in_main_line) {
        game_ok = FALSE;
    }
    if (game_ok) {
        if (eco_match != NULL) {
            /* Free any details of the old one. */
            if (game_details->tags[ECO_TAG] != NULL) {
                (void) free((void *) game_details->tags[ECO_TAG]);
                game_details->tags[ECO_TAG] = NULL;
            }
            if (game_details->tags[OPENING_TAG] != NULL) {
                (void) free((void *) game_details->tags[OPENING_TAG]);
                game_details->tags[OPENING_TAG] = NULL;
            }
            if (game_details->tags[VARIATION_TAG] != NULL) {
                (void) free((void *) game_details->tags[VARIATION_TAG]);
                game_details->tags[VARIATION_TAG] = NULL;
            }
            if (game_details->tags[SUB_VARIATION_TAG] != NULL) {
                (void) free((void *) game_details->tags[SUB_VARIATION_TAG]);
                game_details->tags[SUB_VARIATION_TAG] = NULL;
            }
            /* Add in the new one. */
            if (eco_match->ECO_tag != NULL) {
                game_details->tags[ECO_TAG] = copy_string(eco_match->ECO_tag);
            }
            if (eco_match->Opening_tag != NULL) {
                game_details->tags[OPENING_TAG] = copy_string(eco_match->Opening_tag);
            }
            if (eco_match->Variation_tag != NULL) {
                game_details->tags[VARIATION_TAG] =
                        copy_string(eco_match->Variation_tag);
            }
            if (eco_match->Sub_Variation_tag != NULL) {
                game_details->tags[SUB_VARIATION_TAG] =
                        copy_string(eco_match->Sub_Variation_tag);
            }
        }

        if (GlobalState.check_for_fifty_move_rule && mainline) {
            game_matches = fifty_move_rule_applies;
        }
    }
    /* Fill in the hash value of the final position reached. */
    game_details->final_hash_value = board->hash_value;
    game_details->moves_ok = game_ok;
    game_details->error_ply = error_ply;
    if (!game_ok) {
        /* Decide whether to keep it anyway. */
        if (GlobalState.keep_broken_games) {
        }
        else if (GlobalState.positional_variations) {
        }
        else {
            /* Only return a match if it genuinely matched a variation
             * in which we were interested.
             */
            /* We can't have found a genuine match. */
            game_matches = FALSE;
        }
    }
    return game_matches;
}

/* Play out the moves of an ECO line on the given board.
 * game_details is updated with the final_ and cumulative_ hash
 * values.
 */

static void
play_eco_moves(Game *game_details, Board *board, Move *moves)
{
    Boolean game_ok = TRUE;
    /* Ply number at which any error was found. */
    int error_ply = 0;
    Move *next_move = moves;

    /* Keep going while the game is ok, and we have some more
     * moves and we haven't exceeded the search depth without finding
     * a match.
     */
    while (game_ok && (next_move != NULL)) {
        if (*(next_move->move) != '\0') {
            /* Ignore variations. */
            if (apply_move(board->to_move, next_move, board)) {
                /* Combine this hash value to the cumulative one. */
                game_details->cumulative_hash_value += board->hash_value;
                next_move = next_move->next;
            }
            else {
                print_error_context(GlobalState.logfile);
                fprintf(GlobalState.logfile,
                        "Failed to make move %u%s %s in the game:\n",
                        board->move_number,
                        (board->to_move == WHITE) ? "." : "...",
                        next_move->move);
                print_board(board, GlobalState.logfile);
                report_details(GlobalState.logfile);
                game_ok = FALSE;
                /* Work out where the error was. */
                error_ply = 2 * board->move_number - 1;
                /* Check who has just moved. */
                if (board->to_move == BLACK) {
                    error_ply++;
                }
            }
        }
        else {
            /* An empty move. */
            fprintf(GlobalState.logfile,
                    "Internal error: Empty move in play_eco_moves.\n");
            report_details(GlobalState.logfile);
            game_ok = FALSE;
            /* Work out where the error was. */
            error_ply = 2 * board->move_number - 1;
            /* Check who has just moved. */
            if (board->to_move == BLACK) {
                error_ply++;
            }
        }
    }
    /* Record whether the full game was checked or not. */
    game_details->moves_checked = next_move == NULL;
    /* Fill in the hash value of the final position reached. */
    game_details->final_hash_value = board->hash_value;
    game_details->moves_ok = game_ok;
    game_details->error_ply = error_ply;
}

/* Play out a variation.
 * Check move validity unless a NULL_MOVE has been found in this
 * variation.
 * Return TRUE if the variation matches a position that
 * we are looking for.
 */
static Boolean
apply_variations(const Game *game_details, const Board *board, Variation *variation,
        Boolean check_move_validity)
{ /* Force a match if we aren't looking for positional variations. */
    Boolean variation_matches = GlobalState.positional_variations ? FALSE : TRUE;
    /* Allocate space for the copies.
     * Allocation is done, rather than relying on local copies in the body
     * of the loop because the recursive nature of this function has
     * resulted in stack overflow on the PC version.
     */
    Game *copy_game = (Game *) malloc_or_die(sizeof (*copy_game));
    Board *copy_board = allocate_new_board();

    while (variation != NULL) {
        /* Work on the copies. */
        *copy_game = *game_details;
        *copy_board = *board;
        /* Don't look for repetitions. */
        copy_game->position_counts = NULL;

        /* We only need one variation to match to declare a match.
         * Play out the variation to its full depth, because we
         * will want the full move information if the main line
         * later matches.
         */
        variation_matches |= play_moves(copy_game, copy_board, variation->moves,
                DEFAULT_POSITIONAL_DEPTH,
                check_move_validity, FALSE);
        variation = variation->next;
    }
    (void) free((void *) copy_game);
    (void) free_board((void *) copy_board);
    return variation_matches;
}

/* game_details contains a complete move score.
 * Try to apply each move on a new board.
 * Store in plycount the number of ply played.
 * Return TRUE if the game matches a variation that we are
 * looking for.
 */
Boolean
apply_move_list(Game *game_details, unsigned *plycount, unsigned max_depth)
{
    Move *moves = game_details->moves;
    Board *board = new_game_board(game_details->tags[FEN_TAG]);
    Boolean game_matches;

    /* Ensure that we have a sensible search depth. */
    if (max_depth == 0) {
        /* No positional variations specified. */
        max_depth = DEFAULT_POSITIONAL_DEPTH;
    }

    /* Start off the cumulative hash value. */
    game_details->cumulative_hash_value = 0;

    if (GlobalState.check_for_repetition && game_details->position_counts == NULL) {
        game_details->position_counts = new_position_count_list(board->hash_value);
    }

    /* Play through the moves and see if we have a match.
     * Check move validity.
     */
    game_matches = play_moves(game_details, board, moves, max_depth, TRUE, TRUE);

    /* Record how long the game was. */
    if (board->to_move == BLACK) {
        *plycount = 2 * (board->move_number - 1) + 1;
    }
    else {
        /* This move number hasn't been played. */
        *plycount = 2 * (board->move_number - 1);
    }

    if (game_matches) {
        game_matches = check_for_only_stalemate(board, moves);
    }

    (void) free_board((void *) board);
    return game_matches;
}

/* game_details contains a complete move score.
 * Try to apply each move on a new board.
 * Store in number_of_moves the length of the game.
 * Return TRUE if the game is ok.
 */
Boolean
apply_eco_move_list(Game *game_details, unsigned *number_of_half_moves)
{
    Move *moves = game_details->moves;
    Board *board = new_game_board(game_details->tags[FEN_TAG]);

    /* Start off the cumulative hash value. */
    game_details->cumulative_hash_value = 0;
    play_eco_moves(game_details, board, moves);
    //game_details->moves_checked = TRUE;
    /* Record how long the game was. */
    *number_of_half_moves = half_moves_played(board);
    (void) free_board((void *) board);
    return game_details->moves_ok;
}

/* Return the string associated with the given piece. */
const char *
piece_str(Piece piece)
{
    if (piece < NUM_PIECE_VALUES) {
        return output_piece_characters[piece];
    }
    else {
        return "?";
    }
}

/* Rewrite move_details->move according to the details held
 * within the structure and the current state of the board.
 */
static Boolean
rewrite_SAN_string(Colour colour, Move *move_details, Board *board)
{
    Boolean Ok = TRUE;

    if (move_details == NULL) {
        /* Shouldn't happen. */
        fprintf(GlobalState.logfile,
                "Internal error: NULL move details in rewrite_SAN_string.\n");
        Ok = FALSE;
    }
    else if (move_details->move[0] == '\0') {
        /* Shouldn't happen. */
        fprintf(GlobalState.logfile, "Empty move in rewrite_SAN_string.\n");
        Ok = FALSE;
    }
    else {
        const unsigned char *move = move_details->move;
        MoveClass class = move_details->class;
        MovePair *move_list = NULL;
        Col to_col = move_details->to_col;
        Rank to_rank = move_details->to_rank;
        unsigned char new_move_str[MAX_MOVE_LEN + 1] = "";

        switch (class) {
            case PAWN_MOVE:
            case ENPASSANT_PAWN_MOVE:
            case PAWN_MOVE_WITH_PROMOTION:
                move_list = find_pawn_moves(move_details->from_col,
                        '0', to_col, to_rank,
                        colour, board);
                break;
            case PIECE_MOVE:
                switch (move_details->piece_to_move) {
                    case KING:
                        move_list = find_king_moves(to_col, to_rank, colour, board);
                        break;
                    case QUEEN:
                        move_list = find_queen_moves(to_col, to_rank, colour, board);
                        break;
                    case ROOK:
                        move_list = find_rook_moves(to_col, to_rank, colour, board);
                        break;
                    case KNIGHT:
                        move_list = find_knight_moves(to_col, to_rank, colour, board);
                        break;
                    case BISHOP:
                        move_list = find_bishop_moves(to_col, to_rank, colour, board);
                        break;
                    default:
                        fprintf(GlobalState.logfile, "Unknown piece move %s\n", move);
                        Ok = FALSE;
                        break;
                }
                break;
            case KINGSIDE_CASTLE:
            case QUEENSIDE_CASTLE:
                /* No move list to prepare. */
                break;
            case NULL_MOVE:
                /* No move list to prepare. */
                break;
            case UNKNOWN_MOVE:
            default:
                fprintf(GlobalState.logfile,
                        "Unknown move class in rewrite_SAN_string(%d).\n",
                        move_details->class);
                Ok = FALSE;
                break;
        }
        if (move_list != NULL) {
            move_list = exclude_checks(move_details->piece_to_move, colour,
                    move_list, board);
        }
        if ((move_list == NULL) && (class != KINGSIDE_CASTLE) &&
                (class != QUEENSIDE_CASTLE) && (class != NULL_MOVE)) {
            Ok = FALSE;
        }
        /* We should now have enough information in move_details to compose a
         * SAN string.
         */
        if (Ok) {
            size_t new_move_index = 0;

            switch (class) {
                case PAWN_MOVE:
                case ENPASSANT_PAWN_MOVE:
                case PAWN_MOVE_WITH_PROMOTION:
                    /* See if we need to give the source column. */
                    if (move_details->captured_piece != EMPTY) {
                        new_move_str[new_move_index] = move_details->from_col;
                        new_move_index++;
                        new_move_str[new_move_index] = 'x';
                        new_move_index++;
                    }
                    else if (move_list->next != NULL) {
                        new_move_str[new_move_index] = move_details->from_col;
                        new_move_index++;
                    }
                    /* Add in the destination. */
                    new_move_str[new_move_index] = to_col;
                    new_move_index++;
                    new_move_str[new_move_index] = to_rank;
                    new_move_index++;
                    if (class == PAWN_MOVE_WITH_PROMOTION) {
                        const char *promoted_piece =
                                piece_str(move_details->promoted_piece);
                        new_move_str[new_move_index] = '=';
                        new_move_index++;
                        strcpy((char *) &new_move_str[new_move_index],
                                promoted_piece);
                        new_move_index += strlen(promoted_piece);
                    }
                    new_move_str[new_move_index] = '\0';
                    break;
                case PIECE_MOVE:
                {
                    const char *piece = piece_str(move_details->piece_to_move);
                    strcpy((char *) &new_move_str[0], piece);
                    new_move_index += strlen(piece);
                    /* Check for the need to disambiguate. */
                    if (move_list->next != NULL) {
                        /* It is necessary.  Count how many times
                         * the from_ col and rank occur in the list
                         * of possibles in order to determine which to use
                         * for this purpose.
                         */
                        int col_times = 0, rank_times = 0;
                        MovePair *possible;
                        Col from_col = move_details->from_col;
                        Rank from_rank = move_details->from_rank;

                        for (possible = move_list; possible != NULL;
                                possible = possible->next) {
                            if (possible->from_col == from_col) {
                                col_times++;
                            }
                            if (possible->from_rank == from_rank) {
                                rank_times++;
                            }
                        }
                        if (col_times == 1) {
                            /* Use the col. */
                            new_move_str[new_move_index] = from_col;
                            new_move_index++;
                        }
                        else if (rank_times == 1) {
                            /* Use the rank. */
                            new_move_str[new_move_index] = from_rank;
                            new_move_index++;
                        }
                        else {
                            /* Use both. */
                            new_move_str[new_move_index] = from_col;
                            new_move_index++;
                            new_move_str[new_move_index] = from_rank;
                            new_move_index++;
                        }
                    }
                    /* See if a capture symbol is needed. */
                    if (move_details->captured_piece != EMPTY) {
                        new_move_str[new_move_index] = 'x';
                        new_move_index++;
                    }
                    /* Add in the destination. */
                    new_move_str[new_move_index] = to_col;
                    new_move_index++;
                    new_move_str[new_move_index] = to_rank;
                    new_move_index++;
                    new_move_str[new_move_index] = '\0';
                }
                    break;
                case KINGSIDE_CASTLE:
                    strcpy((char *) new_move_str, "O-O");
                    break;
                case QUEENSIDE_CASTLE:
                    strcpy((char *) new_move_str, "O-O-O");
                    break;
                case NULL_MOVE:
                    strcpy((char *) new_move_str, (char *) NULL_MOVE_STRING);
                    break;
                case UNKNOWN_MOVE:
                default:
                    Ok = FALSE;
                    break;
            }
            if (Ok) {
                if (move_details->check_status != NOCHECK) {
                    if (move_details->check_status == CHECK) {
                        /* It isn't mate. */
                        strcat((char *) new_move_str, "+");
                    }
                    else {
                        if (GlobalState.output_format == CM) {
                            strcat((char *) new_move_str, "++");
                        }
                        else {
                            strcat((char *) new_move_str, "#");
                        }
                    }
                }
            }
            /* Update the move_details structure with the new string. */
            strcpy((char *) move_details->move,
                    (const char *) new_move_str);
        }
        if (move_list != NULL) {
            free_move_pair_list(move_list);
        }
    }
    return Ok;
}

/* Apply the move to board and rewrite move_details->move unless
 * the output format is the original source.
 * Return TRUE if the move is ok, FALSE otherwise.
 */
static Boolean
rewrite_move(Colour colour, Move *move_details, Board *board)
{ /* Assume success. */
    Boolean Ok = TRUE;
    
    if (GlobalState.output_format == SOURCE || 
            rewrite_SAN_string(colour, move_details, board)) {
        Piece piece_to_move = move_details->piece_to_move;

        if (move_details->class != NULL_MOVE) {
            make_move(move_details->class, move_details->from_col, move_details->from_rank,
                    move_details->to_col, move_details->to_rank,
                    piece_to_move, colour, board);
        }
        /* See if there are any subsidiary actions. */
        switch (move_details->class) {
            case PAWN_MOVE:
            case PIECE_MOVE:
            case ENPASSANT_PAWN_MOVE:
            case KINGSIDE_CASTLE:
            case QUEENSIDE_CASTLE:
                /* Nothing more to do. */
                break;
            case PAWN_MOVE_WITH_PROMOTION:
                if (move_details->class == PAWN_MOVE_WITH_PROMOTION) {
                    if (move_details->promoted_piece != EMPTY) {
                        /* @@@ Do promoted moves have '+' properly appended? */
                        /* Now make the promotion. */
                        make_move(move_details->class, move_details->to_col, move_details->to_rank,
                                move_details->to_col, move_details->to_rank,
                                move_details->promoted_piece, colour, board);
                    }
                    else {
                        Ok = FALSE;
                    }
                }
                break;
            case NULL_MOVE:
                /* Nothing more. */
                break;
            case UNKNOWN_MOVE:
            default:
                Ok = FALSE;
                break;
        }
    }
    else {
        Ok = FALSE;
    }
    return Ok;
}

/* Rewrite the list of moves by playing through the game.
 * If game is NULL then the moves are a variation rather than
 * the main line.
 */
static Boolean
rewrite_moves(Game *game, Board *board, Move *moves)
{
    Boolean game_ok = TRUE;
    Move *move_details = moves;

    while (game_ok && (move_details != NULL)) {
        if (*(move_details->move) != '\0') {
            /* See if there are any variations associated with this move. */
            if ((move_details->Variants != NULL) &&
                    GlobalState.keep_variations &&
                    !rewrite_variations(board, move_details->Variants)) {
                /* Something wrong with the variations. */
                game_ok = FALSE;
            }
            /* NULL_MOVE not allowed in the main line. */
            if (rewrite_move(board->to_move, move_details, board) &&
                    !(move_details->class == NULL_MOVE && game != NULL)) {
                board->to_move = OPPOSITE_COLOUR(board->to_move);
                if (board->to_move == WHITE) {
                    board->move_number++;
                }

                if (GlobalState.output_evaluation) {
                    /* Append an evaluation of the new state of the board
                     * with the move having been played.
                     */
                    append_evaluation(move_details, board);
                }

                if (GlobalState.add_FEN_comments) {
                    /* Append a FEN comment with the new state of the board
                     * with the move having been played.
                     */
                    append_FEN_comment(move_details, board);
                }
                
                if (GlobalState.add_hashcode_comments) {
                    /* Append a hashcode comment using the new state of the board
                     * with the move having been played.
                     */
                    append_hashcode_comment(move_details, board);
                }
                
                /* See if a comment should be replaced by a FEN comment. */
                if(move_details->comment_list != NULL &&
                        GlobalState.FEN_comment_pattern != NULL) {
                    CommentList *comment = move_details->comment_list;
                    Boolean match = FALSE;
                    while(!match && comment != NULL) {
                        StringList *string_list = comment->comment;
                        while(!match && string_list != NULL) {
                            if(strcmp(GlobalState.FEN_comment_pattern, string_list->str) == 0) {
                                match = TRUE;
                            }
                            else {
                                string_list = string_list->next;
                            }
                        }
                        if(!match) {
                            comment = comment->next;
                        }
                        else {
                            /* Crudely replace it. */
                            (void) free((void *) string_list->str);
                            char *FEN_comment = malloc_or_die(FEN_SPACE);
                            build_FEN_string(board, FEN_comment);
                            string_list->str = FEN_comment;
                        }
                    }
                }

                move_details = move_details->next;
            }
            else {
                /* Broken games that are being kept have probably already been
                 * reported, so don't repeat the error.
                 */
                if (!GlobalState.keep_broken_games) {
                    fprintf(GlobalState.logfile,
                            "Failed to rewrite move %u%s %s in the game:\n",
                            board->move_number,
                            (board->to_move == WHITE) ? "." : "...",
                            move_details->move);
                    report_details(GlobalState.logfile);
                    print_board(board, GlobalState.logfile);
                }
                game_ok = FALSE;
            }
        }
        else {
            /* An empty move. */
            fprintf(GlobalState.logfile,
                    "Internal error: Empty move in rewrite_moves.\n");
            report_details(GlobalState.logfile);
            game_ok = FALSE;
        }
    }
    if (!game_ok && GlobalState.keep_broken_games && move_details != NULL) {
        /* Try to place the remaining moves into a comment. */
        CommentList *comment = (CommentList*) malloc_or_die(sizeof (*comment));
        /* Break the link from the previous move. */
        Move *prev;
        StringList *commented_move_list = NULL;

        /* Find the move before the current one, if any. */
        if (move_details == moves) {
            /* Broken from the first move. */
            prev = NULL;
        }
        else {
            prev = moves;
            while (prev != NULL && prev->next != move_details) {
                prev = prev->next;
            }
            if (prev != NULL) {
                prev->next = NULL;
            }
        }
        /* Build the comment string. */
        char *terminating_result = NULL;
        while (move_details != NULL) {
            commented_move_list = save_string_list_item(commented_move_list, (char *) move_details->move);
            if (move_details->next == NULL) {
                /* Remove the terminating result. */
                terminating_result = move_details->terminating_result;
                move_details->terminating_result = NULL;
            }
            move_details = move_details->next;
        }
        comment->comment = commented_move_list;
        comment->next = NULL;

        if (prev != NULL) {
            prev->terminating_result = terminating_result;
            append_comments_to_move(prev, comment);
        }
        else {
            /* The whole game is broken. */
            if (game != NULL) {
                if (terminating_result != NULL) {
                    commented_move_list = save_string_list_item(commented_move_list, terminating_result);
                }
                game->prefix_comment = comment;
                game->moves = NULL;
            }
        }
    }
    return game_ok;
}

/* Rewrite the list of variations.
 * Return TRUE if the variation are ok. a position that
 */
static Boolean
rewrite_variations(const Board *board, Variation *variation)
{
    Board *copy_board = allocate_new_board();
    Boolean variations_ok = TRUE;

    while ((variation != NULL) && variations_ok) {
        /* Work on the copy. */
        *copy_board = *board;

        variations_ok = rewrite_moves((Game *) NULL, copy_board, variation->moves);
        variation = variation->next;
    }
    (void) free_board((void *) copy_board);
    return variations_ok;
}

/* moves contains a complete game score.
 * Try to rewrite each move into SAN as it is played on a new board.
 * Return the final Board position if the game was rewritten alright,
 * NULL otherwise.
 */
Board *
rewrite_game(Game *current_game)
{
    const char *fen = current_game->tags[FEN_TAG];
    Board *board = new_game_board(fen);
    Boolean game_ok;

    /* No null-move found at the start of the game. */
    game_ok = rewrite_moves(current_game, board, current_game->moves);
    if (game_ok) {
    }
    else if (GlobalState.keep_broken_games) {
    }
    else {
        (void) free_board((void *) board);
        board = NULL;
    }
    return board;
}


/* Define a table to hold the positional hash codes of interest.
 * Size should be a prime number for collision avoidance.
 */
#define MAX_NON_POLYGLOT_CODE 541
static HashLog *non_polyglot_codes_of_interest[MAX_NON_POLYGLOT_CODE];
/* Whether or not the non-polyglot hashcodes are in use. */
Boolean using_non_polyglot = FALSE;

/* move_details is either the start of a variation in which we are interested
 * or it is NULL.
 * fen is either a position we are interested in or it is NULL.
 * Generate and store the hash value for the variation, or the FEN
 * position in non_polyglot_codes_of_interest.
 */
void
store_hash_value(Move *move_details, const char *fen)
{
    Move *move = move_details;
    Board *board = new_game_board(fen);
    Boolean Ok = TRUE;

    while ((move != NULL) && Ok) {
        /* Reset print_move number if a variation was printed. */
        if (*(move->move) == '\0') {
            /* A comment node, not a proper move. */
            move = move->next;
        }
        else if (apply_move(board->to_move, move, board)) {
            move = move->next;
        }
        else {
            print_error_context(GlobalState.logfile);
            fprintf(GlobalState.logfile, "Failed to make move %u%s %s\n",
                    board->move_number,
                    (board->to_move == WHITE) ? "." : "...",
                    move->move);
            Ok = FALSE;
        }
    }

    if (Ok) {
        HashLog *entry = (HashLog *) malloc_or_die(sizeof (*entry));
        unsigned ix = board->hash_value % MAX_NON_POLYGLOT_CODE;

        /* We don't include the cumulative hash value as the sequence
         * of moves to reach this position is not important.
         */
        entry->cumulative_hash_value = 0;
        entry->final_hash_value = board->hash_value;
        /* Link it into the head at this index. */
        entry->next = non_polyglot_codes_of_interest[ix];
        non_polyglot_codes_of_interest[ix] = entry;
        using_non_polyglot = TRUE;
    }
    else {
        exit(1);
    }
    (void) free_board((void *) board);
}

/* Define a table to hold the positional hash codes of interest.
 * Size should be a prime number for collision avoidance.
 */
#define MAX_POLYGLOT_CODE 541
static HashLog *polyglot_codes_of_interest[MAX_POLYGLOT_CODE];
/* Whether or not the polyglot hashcodes are in use. */
Boolean using_polyglot = FALSE;

/**
 * Convert the given hex string to an int and save it
 * for position matching. 
 * @param value A hexadecimal string of up to 16 characters.
 * @return TRUE if the value is decoded ok; FALSE otherwise.
 */
Boolean save_polyglot_hashcode(const char *value)
{
    uint64_t hash;
    Boolean Ok;
    
    if(value != NULL && *value != '\0') {
        if(value[strspn(value, "0123456789abcdefABCDEF")] == '\0') {
            Ok = sscanf(value, "%llx", &hash) == 1;
            if (Ok) {
                HashLog *entry = (HashLog *) malloc_or_die(sizeof (*entry));
                unsigned ix = hash % MAX_POLYGLOT_CODE;

                /* We don't include the cumulative hash value as the sequence
                 * of moves to reach this position is not important.
                 */
                entry->cumulative_hash_value = 0;
                entry->final_hash_value = hash;
                /* Link it into the head at this index. */
                entry->next = polyglot_codes_of_interest[ix];
                polyglot_codes_of_interest[ix] = entry;
                using_polyglot = TRUE;
            }
        }
        else {
            Ok = FALSE;
        }
    }
    else {
        Ok = FALSE;
    }
    return Ok;
}

/* Does the current board match a position of interest.
 * Look in codes_of_interest for current_hash_value.
 */
static Boolean
position_matches(const Board *board)
{
    Boolean found = FALSE;
    
    if(using_non_polyglot) {
        HashCode current_hash_value = board->hash_value;
        unsigned ix = current_hash_value % MAX_NON_POLYGLOT_CODE;
        for (HashLog *entry = non_polyglot_codes_of_interest[ix]; !found && (entry != NULL);
                entry = entry->next) {
            /* We can test against just the position value. */
            if (entry->final_hash_value == current_hash_value) {
                found = TRUE;
            }
        }
    }
    if(!found && using_polyglot) {
        uint64_t current_hash_value = generate_zobrist_hash_from_board(board);
        unsigned ix = current_hash_value % MAX_POLYGLOT_CODE;
        for (HashLog *entry = polyglot_codes_of_interest[ix]; !found && (entry != NULL);
                entry = entry->next) {
            /* We can test against just the position value. */
            if (entry->final_hash_value == current_hash_value) {
                found = TRUE;
            }
        }
    }
    if (!found) {
        const char *matching_pattern = pattern_match_board(board);
        found = matching_pattern != NULL;
    }
    return found;
}

/* Build a basic EPD string from the given board. */
void
build_basic_EPD_string(const Board *board, char *epd)
{
    Rank rank;
    int ix = 0;
#if 0
    Boolean castling_allowed;
#endif

    /* The board. */
    for (rank = LASTRANK; rank >= FIRSTRANK; rank--) {
        Col col;
        int consecutive_spaces = 0;
        for (col = FIRSTCOL; col <= LASTCOL; col++) {
            int coloured_piece = 
                board->board[RankConvert(rank)][ColConvert(col)];
            if (coloured_piece != EMPTY) {
                if (consecutive_spaces > 0) {
                    epd[ix] = '0' + consecutive_spaces;
                    ix++;
                    consecutive_spaces = 0;
                }
                epd[ix] = coloured_piece_to_SAN_letter(coloured_piece);
                ix++;
            }
            else {
                consecutive_spaces++;
            }
        }
        if (consecutive_spaces > 0) {
            epd[ix] = '0' + consecutive_spaces;
            ix++;
        }
        /* Terminate the row. */
        if (rank != FIRSTRANK) {
            epd[ix] = '/';
            ix++;
        }
    }
    epd[ix] = ' ';
    ix++;
    epd[ix] = board->to_move == WHITE ? 'w' : 'b';
    ix++;
    epd[ix] = ' ';
    ix++;

    /* Castling details. */
#if 1
    /* Try to cover Chess960 requirements. */
    if (board->WKingCastle == '\0' && board->WQueenCastle == '\0' &&
            board->BKingCastle == '\0' && board->BQueenCastle == '\0') {
        epd[ix] = '-';
        ix++;
    }
    else {
        if (board->WKingCastle != '\0' || board->WQueenCastle != '\0') {
            /* At least one White castling right. */
            if (board->WKingCastle != '\0' && board->WQueenCastle != '\0') {
                /* Both possible. */
                if (board->WKingCastle > board->WQueenCastle) {
                    /* Regular notation.
                     * NB: This loses square specifics, but that seems to be ok in
                     * X-FEN according to https://en.wikipedia.org/wiki/X-FEN
                     */
                    epd[ix] = 'K';
                    ix++;
                    epd[ix] = 'Q';
                    ix++;
                }
                else {
                    /* Out of order so store Rook cols. */
                    epd[ix] = toupper(board->WKingCastle);
                    ix++;
                    epd[ix] = toupper(board->WQueenCastle);
                    ix++;
                }
            }
            else {
                /* Only one. */
                if (board->WKingCastle != '\0') {
                    if (board->WKingCastle != LASTCOL) {
                        epd[ix] = toupper(board->WKingCastle);
                        ix++;
                    }
                    else {
                        /* Only kingside, from the regular position. */
                        epd[ix] = 'K';
                        ix++;
                    }
                }
                else {
                    if (board->WQueenCastle != FIRSTCOL) {
                        epd[ix] = toupper(board->WQueenCastle);
                        ix++;
                    }
                    else {
                        /* Only queenside, from the regular position. */
                        epd[ix] = 'Q';
                        ix++;
                    }
                }
            }
        }
        else {
            /* Only Black castling. */
        }
        /* Same for the queenside. */
        if (board->BKingCastle != '\0' || board->BQueenCastle != '\0') {
            /* At least one Black castling right. */
            if (board->BKingCastle != '\0' && board->BQueenCastle != '\0') {
                /* Both possible. */
                if (board->BKingCastle > board->BQueenCastle) {
                    /* Regular notation. */
                    epd[ix] = 'k';
                    ix++;
                    epd[ix] = 'q';
                    ix++;
                }
                else {
                    /* Out of order. */
                    epd[ix] = board->BKingCastle;
                    ix++;
                    epd[ix] = board->BQueenCastle;
                    ix++;
                }
            }
            else {
                /* Only one. */
                if (board->BKingCastle != '\0') {
                    if (board->BKingCastle != LASTCOL) {
                        epd[ix] = board->BKingCastle;
                        ix++;
                    }
                    else {
                        /* Only kingside, from the regular position. */
                        epd[ix] = 'k';
                        ix++;
                    }
                }
                else {
                    if (board->BQueenCastle != FIRSTCOL) {
                        epd[ix] = board->BQueenCastle;
                        ix++;
                    }
                    else {
                        /* Only queenside, from the regular position. */
                        epd[ix] = 'q';
                        ix++;
                    }
                }
            }
        }
        else {
            /* Only White castling. */
        }
    }
#else
    castling_allowed = FALSE;
    if (board->WKingCastle != '\0') {
        epd[ix] = 'K';
        ix++;
        castling_allowed = TRUE;
    }
    if (board->WQueenCastle != '\0') {
        epd[ix] = 'Q';
        ix++;
        castling_allowed = TRUE;
    }
    if (board->BKingCastle != '\0') {
        epd[ix] = 'k';
        ix++;
        castling_allowed = TRUE;
    }
    if (board->BQueenCastle != '\0') {
        epd[ix] = 'q';
        ix++;
        castling_allowed = TRUE;
    }
    if (!castling_allowed) {
        /* There are no castling rights. */
        epd[ix] = '-';
        ix++;
    }
#endif
    epd[ix] = ' ';
    ix++;

    /* Enpassant. */
    if (board->EnPassant) {
        /* It might be required to suppress redundant ep info. */
        Boolean suppress = FALSE;
        if (GlobalState.suppress_redundant_ep_info) {
            /* Determine whether the ep indication is redundant or not.
             * Assume that it is unless there is a pawn in position to
             * take advantage of it.
             */
            Boolean redundant = TRUE;
            Col ep_col = board->ep_col;
            Rank from_rank;
            Piece pawn;
            if (board->to_move == WHITE) {
                /* White pawn on the fifth rank capturing a black pawn. */
                from_rank = '5';
                pawn = W(PAWN);
            }
            else {
                /* Black pawn on the fourth rank capturing a white pawn. */
                from_rank = '4';
                pawn = B(PAWN);
            }
            if ((ep_col > FIRSTCOL) && (board->board[RankConvert(from_rank)][ColConvert(ep_col - 1)] == pawn)) {
                /* Check that the move does not leave the king in check. */
                Board copy_board = *board;
                make_move(UNKNOWN_MOVE, ep_col - 1, from_rank,
                        board->ep_col, board->ep_rank, PAWN, board->to_move, &copy_board);
                if (king_is_in_check(&copy_board, copy_board.to_move) == NOCHECK) {
                    redundant = FALSE;
                }
            }
            if (redundant && (ep_col < LASTCOL) &&
                    (board->board[RankConvert(from_rank)][ColConvert(ep_col + 1)] == pawn)) {
                /* Check that the move does not leave the king in check. */
                Board copy_board = *board;
                make_move(UNKNOWN_MOVE, ep_col + 1, from_rank,
                        board->ep_col, board->ep_rank, PAWN, board->to_move, &copy_board);
                if (king_is_in_check(&copy_board, copy_board.to_move) == NOCHECK) {
                    redundant = FALSE;
                }
            }
            suppress = redundant;
        }
        if (suppress) {
            epd[ix] = '-';
            ix++;
        }
        else {
            epd[ix] = board->ep_col;
            ix++;
            epd[ix] = board->ep_rank;
            ix++;
        }
    }
    else {
        epd[ix] = '-';
        ix++;
    }
    epd[ix] = '\0';
}

/* Build a FEN string from the given board. */
void
build_FEN_string(const Board *board, char *fen)
{
    size_t ix;

    build_basic_EPD_string(board, fen);
    /* Append the (pseudo) half move count and the full move count. */
    ix = strlen(fen);
    fen[ix] = ' ';
    ix++;

    /* Half moves since the last capture or pawn move. */
    sprintf(&fen[ix], "%u", board->halfmove_clock);
    ix = strlen(fen);
    fen[ix] = ' ';
    ix++;

    /* The full move number. */
    sprintf(&fen[ix], "%u", board->move_number);
}

/* Append to move_details a FEN comment of the board.
 * The board state is immediately following application of the
 * given move.
 */
static void
append_FEN_comment(Move *move_details, const Board *board)
{
    char *FEN_comment = malloc_or_die(FEN_SPACE);
    CommentList *comment = (CommentList*) malloc_or_die(sizeof (*comment));
    StringList *current_comment = save_string_list_item(NULL, FEN_comment);

    build_FEN_string(board, FEN_comment);
    comment->comment = current_comment;
    comment->next = NULL;
    append_comments_to_move(move_details, comment);
}

/* Maximum length of a 64-bit unsigned int in decimal. 
 * NB: At the moment, the output is hex, which requires
 * only 16 characters.
 */
#define HASH_64_BIT_SPACE 20

/* Append to move_details a hashcode comment from the state of the board.
 * The board state is immediately following application of the
 * given move.
 */
static void
append_hashcode_comment(Move *move_details, const Board *board)
{
    char *hashcode_comment = malloc_or_die(HASH_64_BIT_SPACE + 1);
    CommentList *comment = (CommentList*) malloc_or_die(sizeof (*comment));
    StringList *current_comment = save_string_list_item(NULL, hashcode_comment);
    
    uint64_t hash = generate_zobrist_hash_from_board(board);
    sprintf(hashcode_comment, "%llx", hash);

    comment->comment = current_comment;
    comment->next = NULL;
    append_comments_to_move(move_details, comment);
}

/* Append to move_details an evaluation value for board.
 * The board state is immediately following application of the
 * given move.
 */
static void
append_evaluation(Move *move_details, const Board *board)
{
    CommentList *comment = (CommentList*) malloc_or_die(sizeof (*comment));
    /* Space template for the value.
     * @@@ There is a buffer-overflow risk here if the evaluation value
     * is too large.
     */
    const char valueSpace[] = "-012456789.00";
    char *evaluation = (char *) malloc_or_die(sizeof (valueSpace));
    StringList *current_comment;

    double value = evaluate(board);

    /* @@@ Overflow possible here if the value is too big to fit. */
    sprintf(evaluation, "%.2f", value);
    if (strlen(evaluation) > strlen(valueSpace)) {
        fprintf(GlobalState.logfile,
                "Overflow in evaluation space in append_evaluation()\n");
        exit(1);
    }

    current_comment = save_string_list_item(NULL, evaluation);
    comment->comment = current_comment;
    comment->next = NULL;
    append_comments_to_move(move_details, comment);
}

/* Append to move_details a comment indicating that this
 * move resulted in a positional match.
 */
CommentList *
create_match_comment(Move *move_details)
{
    /* The comment string. */
    char *match_comment = copy_string(GlobalState.position_match_comment);
    StringList *current_comment = save_string_list_item(NULL, match_comment);
    CommentList *comment = (CommentList*) malloc_or_die(sizeof (*comment));

    comment->comment = current_comment;
    comment->next = NULL;
    return comment;
}

/* Return an evaluation of board. */
static double
evaluate(const Board *board)
{
    return shannonEvaluation(board);
}

/* Return an evaluation of board based on
 * Claude Shannon's technique.
 */
static double
shannonEvaluation(const Board *board)
{
    MovePair *white_moves, *black_moves;
    int whiteMoveCount = 0, blackMoveCount = 0;
    int whitePieceCount = 0, blackPieceCount = 0;
    double shannonValue = 0.0;

    Rank rank;
    Col col;

    /* Determine the mobilities. */
    white_moves = find_all_moves(board, WHITE);
    if (white_moves != NULL) {
        MovePair *m;
        for (m = white_moves; m != NULL; m = m->next) {
            whiteMoveCount++;
        }
        free_move_pair_list(white_moves);
    }

    black_moves = find_all_moves(board, BLACK);
    if (black_moves != NULL) {
        MovePair *m;
        for (m = black_moves; m != NULL; m = m->next) {
            blackMoveCount++;
        }
        free_move_pair_list(black_moves);
    }


    /* Pick up each piece of the required colour. */
    for (rank = LASTRANK; rank >= FIRSTRANK; rank--) {
        int r = RankConvert(rank);
        for (col = FIRSTCOL; col <= LASTCOL; col++) {
            int c = ColConvert(col);
            int pieceValue = 0;
            Piece occupant = board->board[r][c];
            if (occupant != EMPTY) {
                /* This square is occupied by a piece of the required colour. */
                Piece piece = EXTRACT_PIECE(occupant);

                switch (piece) {
                    case PAWN:
                        pieceValue = 1;
                        break;
                    case BISHOP:
                    case KNIGHT:
                        pieceValue = 3;
                        break;
                    case ROOK:
                        pieceValue = 5;
                        break;
                    case QUEEN:
                        pieceValue = 9;
                        break;
                    case KING:
                        break;
                    default:
                        fprintf(GlobalState.logfile,
                                "Internal error: unknown piece %d in append_evaluation().\n",
                                piece);
                }
                if (EXTRACT_COLOUR(occupant) == WHITE) {
                    whitePieceCount += pieceValue;
                }
                else {
                    blackPieceCount += pieceValue;
                }
            }
        }
    }

    shannonValue = (whitePieceCount - blackPieceCount) +
            (whiteMoveCount - blackMoveCount) * 0.1;
    return shannonValue;
}
