/*
* Copyright 2018 Laurent Farhi
* Contact: lfarhi@sfr.fr
*
*  This file is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This file is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this file.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#ifndef SP_SOLVE_H
#  define SP_SOLVE_H

#  include <stdio.h>

/*
 * Sliding puzzle solver
 */

/********************************
- Thread-safe
- Thread re-entrant
- Thread cancellable
********************************/

// BEGIN VFUNC
// Credits: VFUNC is a macro for overloading on number (but not types) of arguments.
// See https://stackoverflow.com/questions/11761703/overloading-macro-on-number-of-arguments
#  define __NARG__(...)  __NARG_I_(__VA_ARGS__,__RSEQ_N())
#  define __NARG_I_(...) __ARG_N(__VA_ARGS__)
#  define __ARG_N( \
      _1, _2, _3, _4, _5, _6, _7, _8, _9,_10, \
     _11,_12,_13,_14,_15,_16,_17,_18,_19,_20, \
     _21,_22,_23,_24,_25,_26,_27,_28,_29,_30, \
     _31,_32,_33,_34,_35,_36,_37,_38,_39,_40, \
     _41,_42,_43,_44,_45,_46,_47,_48,_49,_50, \
     _51,_52,_53,_54,_55,_56,_57,_58,_59,_60, \
     _61,_62,_63,N,...) N
#  define __RSEQ_N() \
     63,62,61,60,                   \
     59,58,57,56,55,54,53,52,51,50, \
     49,48,47,46,45,44,43,42,41,40, \
     39,38,37,36,35,34,33,32,31,30, \
     29,28,27,26,25,24,23,22,21,20, \
     19,18,17,16,15,14,13,12,11,10, \
     9,8,7,6,5,4,3,2,1,0

#  define _VFUNC_(name, n) name##n
#  define _VFUNC(name, n) _VFUNC_(name, n)
#  define VFUNC(func, ...) _VFUNC(func, __NARG__(__VA_ARGS__)) (__VA_ARGS__)
// END VFUNC

/*****************************************************
* PUBLIC INTERFACE                                   *
*****************************************************/

/** Get library version **/
const char *sliding_puzzle_solver_version ();

/** Initialize and release a Puzzle instance **/
typedef struct sPuzzle *Puzzle;
Puzzle sliding_puzzle_init2 (int width, int height);
Puzzle sliding_puzzle_init4 (int width, int height, int *grid, FILE * f);
#  define sliding_puzzle_init(w, h, ...)            VFUNC(sliding_puzzle_init, w, h, __VA_ARGS__)
int sliding_puzzle_release (Puzzle puzzle);

/** Set and get properties **/
int sliding_puzzle_parity_get (Puzzle puzzle);
void sliding_puzzle_grid_get (Puzzle puzzle, int grid[]);
void sliding_puzzle_solution_get (Puzzle puzzle, int solution[]);

typedef void (*Puzzle_move_handler) (Puzzle puzzle, int move_number, int tile, int move);
Puzzle_move_handler sliding_puzzle_move_handler_set (Puzzle puzzle, Puzzle_move_handler mh);
FILE *sliding_puzzle_stream_set (Puzzle puzzle, FILE * f);

/** Solve puzzle using either IDA* or RBFS algorithm **/
int sliding_puzzle_solve_IDA (Puzzle puzzle);
int sliding_puzzle_solve_RBFS (Puzzle puzzle);

/** Optionally create and share a cycle detection database **/
void sliding_puzzle_cycle_database_attach (Puzzle puzzle, int cycle_size);
int sliding_puzzle_cycle_database_share (Puzzle orig, Puzzle dest);

/** Optionally create and share a heuristic distance to solution database **/
void sliding_puzzle_heuristic_database_attach (Puzzle puzzle, int pattern_size);
int sliding_puzzle_heuristic_database_share (Puzzle orig, Puzzle dest);

/*****************************************************
* FOR UNIT TEST PURPOSES                             *
*****************************************************/
//#define TEST_CANCELLATION_POINT "rw_ac_write_begin"
#  ifdef TEST_CANCELLATION_POINT
extern pthread_t *thread_cancellation_point_test;
#  endif

#endif

/* TODO list *******************
- Rendre la machine d'état multi-thread en rattachant l'état au puzzle - DONE
- Modifier l'interface des banques de cycles à l'image de celle des bases d'heuristique (attach/share) - DONE
- Rendre les puzzle solubles plusieurs fois - DONE
- OpenCL parallelization - TODO
********************************/
