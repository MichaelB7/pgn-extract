# pgn-extract
Chess utility to extract data/games from a pgn file by David J. Barnes

An open-source program, pgn-extract, which is a command-line program for searching, manipulating and formatting chess games recorded in the Portable Game Notation (PGN) or something close. It is capable of handling files containing millions of games. It also recognises Chess960 encodings.

There are several ways to specify the criteria on which to extract; for instance: textual move sequences, the position reached after a sequence of moves, information in the tag fields, and material balance in the ending. Full ANSI C source and a 32-bit Windows binary for the program are available under the terms of the GNU General Public License. The program includes a semantic analyser which will report errors in game scores and it is also able to detect duplicate games found in one or more of its input files.

The range of input move formats accepted is fairly wide and includes recognition of lower-case piece letters for English and upper-case piece letters for Dutch and German. The default output is in English Standard Algebraic Notation (SAN), although there is some support for output in different notations.

Extracted games may be written out either including or excluding comments, NAGs, and variations. Games may be given ECO classifications derived from the accompanying file eco.pgn, or a customised version provided by the user.

Plus, lots of other useful features that have gradually found their way into what was once a relatively simple program!


