
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
#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#include "lib15puzzle.h"
#include "sp_solve.h"

// Declare a Aho-Corasick Finite State Machine for states holding chars
#include "../aho_corasick/aho_corasick_template_impl.h"
// *INDENT-OFF*
ACM_DECLARE (char)
ACM_DEFINE (char)
// *INDENT-ON*

/**************************************************/

/** Macros - BEGIN **/
#ifdef TEST_CANCELLATION_POINT
pthread_t *thread_cancellation_point_test = 0;
#endif

//#define TRACE 1
#if TRACE == 2

#  define PUZZLE_DEBUG(puzzle, ...) \
  do {\
    FILE* stream = (puzzle)->stream;\
    if (!stream)\
      break;\
    if (!stream)\
      stream = stderr;\
    fprintf(stream, "[%s] ", PACKAGE);\
    if (puzzle) fprintf(stream, "[%p] ", (void *)puzzle);\
    fprintf(stream, __VA_ARGS__);\
    fflush(stream);\
  } while(0)

#  define PUZZLE_PRINT(puzzle, ...) PUZZLE_DEBUG(puzzle, __VA_ARGS__)

#elif TRACE == 1

#  define PUZZLE_DEBUG(puzzle, ...) (void)((void)(puzzle), (void)(__VA_ARGS__))

#  define PUZZLE_PRINT(puzzle, ...) \
  do {\
    if ((puzzle)->stream)\
    {\
      fprintf((puzzle)->stream, __VA_ARGS__);\
      fflush((puzzle)->stream);\
    }\
  } while(0)

#else

#  define PUZZLE_DEBUG(puzzle, ...) (void)((void)(puzzle), (void)(__VA_ARGS__))
#  define PUZZLE_PRINT(puzzle, ...)  PUZZLE_DEBUG(puzzle, __VA_ARGS__)

#endif

#define SLIDING_PUZZLE_SOLVE_VERSION "2.0.0.1"
#define SLIDING_PUZZLE_SOLVE_AUTHOR "Laurent FARHI"
#define SLIDING_PUZZLE_SOLVE_COPYRIGHT "(c) 2011-2017"
const char *
sliding_puzzle_solver_version ()
{
  return "V" SLIDING_PUZZLE_SOLVE_VERSION "\n" SLIDING_PUZZLE_SOLVE_AUTHOR "\n" __DATE__ ", " __TIME__ "\n"
    SLIDING_PUZZLE_SOLVE_COPYRIGHT;
}

/**************************************************/

/** Modules - BEGIN **/

/** USING RANDOM GENERATOR MODULE **/
#define _XOPEN_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <limits.h>
#include "random_generator.m"

/** USING RW_LOCK MODULE **/
#include <pthread.h>
#include <errno.h>
#include "rw_lock.m"

#include <inttypes.h>
#include <string.h>

typedef struct sZoneExtension
{
  int lmin, lmax, cmin, cmax;
} ZoneExtension;

#if TRACE == 2
static void
print_keyword (Keyword (char) kw, void *value)
{
  printf ("Register keyword: ");
  for (size_t i = 0; i < ACM_MATCH_LENGTH (kw); i++)
    printf ("%c", ACM_MATCH_SYMBOLS (kw)[i]);
  if (value)
  {
    ZoneExtension *z = value;
    printf (" %i %i %i %i", z->lmin, z->lmax, z->cmin, z->cmax);
  }
  printf ("\n");
}

static void
print_match (MatchHolder (char) kw, void *value)
{
  printf ("Keyword #%zu: ", ACM_MATCH_UID (kw));
  for (size_t i = 0; i < ACM_MATCH_LENGTH (kw); i++)
    printf ("%c", ACM_MATCH_SYMBOLS (kw)[i]);
  ZoneExtension *z = value;
  printf (" %i %i %i %i\n", z->lmin, z->lmax, z->cmin, z->cmax);
}
#endif

/**************************************************/

/** Objects - BEGIN **/
struct sHeuristicData
{
  int nb_tiles;
  int *tiles;
  int8_t *database;
};

struct sHeuristicDatabase
{
  struct sHeuristicData *database_sol;
  int size_sol;
  int *mirror_sol, *mirror_pos;
  int nbUsers;
};

typedef struct sHeuristicDatabase *HeuristicDatabase;

struct sCycleDatabase
{
  ACMachine (char) * cycles;
  int nbUsers;
};

typedef struct sCycleDatabase *CycleDatabase;

struct sPuzzle
{
  int width, height;
  int *grid;                    // Array of Tiles at poisiton i
  int *pos;                     // Array of Position of tile i
  int parity;
  int d2sol;                    // Minimal distances to solution
  int orient;

  int *grid_sol;                // Tiles of target solution
  int *pos_sol;                 // Position of tiles of target solution

  int *pos_perm;                // depends on width and height
  int nb_perms;                 // depends on width and height
  int *upper_nb_perms;          // depends on width and height

  CycleDatabase cycle_database;
  const ACState (char) * cycle_state;
  HeuristicDatabase heuristic_database;

  FILE *stream;
  Puzzle_move_handler solution_shower;

  rw_access_control_t *accessControl;

  int solved;
  int solution_length;
  int *solution;
};

typedef struct sPuzzle const *constPuzzle;

struct BufferIDA
{
  int move;
  int *grid, *pos;
  uintmax_t nbGeneratedNodes;
};

struct BuffersIDA
{
  struct BufferIDA **pBuffer;
  int *pBufferLength;
};

typedef struct
{
  struct sPuzzle node;          // configuration of this node
  int F;

  int move;                     // move to get to this node
} NodeRBFS;

struct BufferRBFS
{
  int move;
  int *grid[4], *pos[4];
  uintmax_t nbGeneratedNodes;
};

struct BuffersRBFS
{
  struct BufferRBFS **pBuffer;
  int *pBufferLength;
};

/** Objects - END **/

/** Destructors - BEGIN **/
static int
sliding_puzzle_cycle_bank_release (Puzzle puzzle)
{
  if (!puzzle->cycle_database)
    return 0;

  if (puzzle->cycle_database->nbUsers > 1)
  {
    puzzle->cycle_database->nbUsers--;
    puzzle->cycle_database = 0;
    puzzle->cycle_state = 0;
    return 0;
  }

  ACM_release (puzzle->cycle_database->cycles);
  free (puzzle->cycle_database);
  puzzle->cycle_database = 0;
  puzzle->cycle_state = 0;
  return 1;
}

static int
sliding_puzzle_heuristic_database_release (Puzzle puzzle)
{
  if (!puzzle->heuristic_database)
    return 0;

  if (puzzle->heuristic_database->nbUsers > 1)
  {
    puzzle->heuristic_database->nbUsers--;
    puzzle->heuristic_database = 0;
    return 0;
  }

  for (int i = 0; i < puzzle->heuristic_database->size_sol; i++)
  {
    free (puzzle->heuristic_database->database_sol[i].tiles);
    free (puzzle->heuristic_database->database_sol[i].database);
  }
  free (puzzle->heuristic_database->database_sol);
  free (puzzle->heuristic_database->mirror_sol);
  free (puzzle->heuristic_database->mirror_pos);

  free (puzzle->heuristic_database);
  puzzle->heuristic_database = 0;
  return 1;
}

int
sliding_puzzle_release (Puzzle puzzle)
{
  if (!puzzle)
    return 1;

  PUZZLE_DEBUG (puzzle, _("Releasing puzzle...\n"));

  int rw_ac_destroy_status = rw_ac_destroy (puzzle->accessControl);
  ASSERT (rw_ac_destroy_status == 0 || rw_ac_destroy_status == EBUSY, _("POSIX thread finalization error"));
  if (rw_ac_destroy_status == EBUSY)
  {
    PUZZLE_DEBUG (puzzle,
                  _
                  ("WARNING: Puzzle is still in use (presumably locked by sliding_puzzle_solve_IDA or sliding_puzzle_solve_RBFS) and can not be released.\n"));
    return 0;
  }

  free (puzzle->grid_sol);
  free (puzzle->pos_sol);
  free (puzzle->upper_nb_perms);
  free (puzzle->pos_perm);
  free (puzzle->grid);
  free (puzzle->pos);
  free (puzzle->solution);

  if (sliding_puzzle_heuristic_database_release (puzzle))
    PUZZLE_PRINT (puzzle, _("Heuristic database released.\n"));
  if (sliding_puzzle_cycle_bank_release (puzzle))
    PUZZLE_PRINT (puzzle, _("Cycle bank released.\n"));

  PUZZLE_DEBUG (puzzle, _("Puzzle released.\n"));
  free (puzzle);

  return 1;
}

/** Destructors - END **/

/**************************************************/

/** Cleaners for canceled thread - BEGIN **/
static void
cycle_database_cleanup (void *arg)
{
  CycleDatabase c = arg;
  ACM_release (c->cycles);
  free (c);
}

static void
sliding_puzzle_cancellation_msg (void *arg)
{
  Puzzle p = arg;
  PUZZLE_DEBUG (p, _("Processing canceled.\n"));
}

static void
sliding_puzzle_buffer_IDA_cleanup (void *arg)
{
  struct BuffersIDA *pb = arg;
  for (int i = 0; i < *pb->pBufferLength; i++)
  {
    free ((*pb->pBuffer)[i].grid);
    free ((*pb->pBuffer)[i].pos);
  }
  free (*pb->pBuffer);
}

static void
sliding_puzzle_buffer_RBFS_cleanup (void *arg)
{
  struct BuffersRBFS *pb = arg;
  for (int i = 0; i < *pb->pBufferLength; i++)
    for (int c = 0; c < 4; c++)
    {
      free ((*pb->pBuffer)[i].grid[c]);
      free ((*pb->pBuffer)[i].pos[c]);
    }
  free (*pb->pBuffer);
}

static void
sliding_puzzle_cycle_cleanup (void *arg)
{
  Puzzle cycling = (Puzzle) arg;

  free (cycling->grid_sol);
  free (cycling->pos_sol);
  free (cycling->upper_nb_perms);
  free (cycling->pos_perm);
  free (cycling->grid);
  free (cycling->pos);
  ASSERT_FALSE (rw_ac_destroy (cycling->accessControl), _("POSIX thread finalization error"));

  PUZZLE_DEBUG (cycling, _("Puzzle released.\n"));

  free (cycling);
}

static void
sliding_puzzle_heuristic_database_cleanup (void *arg)
{
  HeuristicDatabase heuristic_database = arg;
  for (int i = 0; i < heuristic_database->size_sol; i++)
  {
    free (heuristic_database->database_sol[i].tiles);
    free (heuristic_database->database_sol[i].database);
  }
  free (heuristic_database->database_sol);
  free (heuristic_database->mirror_sol);
  free (heuristic_database->mirror_pos);

  free (heuristic_database);
}

static void sliding_puzzle_read_end (Puzzle puzzle);
static void sliding_puzzle_write_end (Puzzle puzzle);

static void
sliding_puzzle_solve_write_clean (void *arg)
{
  Puzzle p = arg;
  sliding_puzzle_write_end (p);
}

static void
sliding_puzzle_solve_read_clean (void *arg)
{
  Puzzle p = arg;
  sliding_puzzle_read_end (p);
}

/** Cleaners for ca,canceled thread - END **/

/** Multi-threading access control toolbox - BEGIN **/
// Makes use of the "rw_lock.m" module (rw_ac_read_begin, rw_ac_read_end, rw_ac_write_begin, rw_ac_write_end)
// Pthread read-write locks (pthread_rwlock_t) would do as well.
static void
sliding_puzzle_read_begin (Puzzle puzzle)
{
  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
  PUZZLE_DEBUG (puzzle, _("Start reading puzzle data.\n"));
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  ASSERT_FALSE (rw_ac_read_begin (puzzle->accessControl), _("POSIX thread error"));
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
}

static void
sliding_puzzle_read_end (Puzzle puzzle)
{
  ASSERT_FALSE (rw_ac_read_end (puzzle->accessControl), _("POSIX thread error"));
  PUZZLE_DEBUG (puzzle, _("Finished reading puzzle data.\n"));
}

static void
sliding_puzzle_write_begin (Puzzle puzzle)
{
  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
  PUZZLE_DEBUG (puzzle, _("Start writing puzzle data.\n"));
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  ASSERT_FALSE (rw_ac_write_begin (puzzle->accessControl), _("POSIX thread error"));
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
}

static void
sliding_puzzle_write_end (Puzzle puzzle)
{
  ASSERT_FALSE (rw_ac_write_end (puzzle->accessControl), _("POSIX thread error"));
  PUZZLE_DEBUG (puzzle, _("Finished writing puzzle data.\n"));
}

/** Multi-threading access control toolbox - END **/

/** Sliding puzzle toolbox - BEGIN **/

/** Cycles - BEGIN **/
// Searching for cycles makes use of DFRS
static int sliding_puzzle_depth_first_recursive_search (Puzzle puzzle, int depth, int last, struct BufferIDA *buffer);

static Puzzle
sliding_puzzle_for_cycling_init (int width, int height)
{
  if (width <= 0 || height <= 0)
    return 0;

  Puzzle cycling = malloc (sizeof (*cycling));
  cycling->width = 2 * width - 1;
  cycling->height = 2 * height - 1;

  cycling->grid = malloc (cycling->width * cycling->height * sizeof (*cycling->grid));
  cycling->pos = malloc (cycling->width * cycling->height * sizeof (*cycling->pos));
  cycling->grid_sol = malloc (cycling->width * cycling->height * sizeof (*cycling->grid_sol));
  cycling->pos_sol = malloc (cycling->width * cycling->height * sizeof (*cycling->pos_sol));

  cycling->pos[0] = (cycling->width * cycling->height - 1) / 2;
  for (int i = 0; i < cycling->width * cycling->height; i++)
  {
    if (i < cycling->pos[0])
      cycling->grid_sol[i] = cycling->grid[i] = i + 1;
    else if (i > cycling->pos[0])
      cycling->grid_sol[i] = cycling->grid[i] = i;
    else
      cycling->grid_sol[i] = cycling->grid[i] = 0;
  }
  cycling->grid[cycling->pos[0]] = cycling->grid[cycling->pos[0] + 1];
  cycling->pos[0]++;
  cycling->grid[cycling->pos[0]] = 0;

  for (int i = 0; i < cycling->width * cycling->height; i++)
    cycling->pos[cycling->grid[i]] = i;

  for (int i = 0; i < cycling->width * cycling->height; i++)
    cycling->pos_sol[cycling->grid_sol[i]] = i;

  cycling->d2sol = 1;
  cycling->solved = 0;
  cycling->orient = 1;          // for cycle search
  cycling->cycle_database = 0;
  cycling->cycle_state = 0;
  cycling->heuristic_database = 0;
  cycling->stream = 0;
  cycling->solution_shower = 0;
  cycling->accessControl = 0;

  cycling->pos_perm =
    malloc ((4 * cycling->width * cycling->height -
             2 * (cycling->width + cycling->height)) * sizeof (*cycling->pos_perm));
  cycling->nb_perms = 0;
  cycling->upper_nb_perms = malloc (cycling->width * cycling->height * sizeof (*cycling->upper_nb_perms));
  for (int i = 0; i < cycling->width * cycling->height; i++)
  {
    if (i - cycling->width >= 0)
    {
      cycling->pos_perm[cycling->nb_perms] = i - cycling->width;
      cycling->nb_perms++;
    }
    if (i + cycling->width < cycling->width * cycling->height)
    {
      cycling->pos_perm[cycling->nb_perms] = i + cycling->width;
      cycling->nb_perms++;
    }
    if (i - 1 >= 0 && (i - 1) / cycling->width == i / cycling->width)
    {
      cycling->pos_perm[cycling->nb_perms] = i - 1;
      cycling->nb_perms++;
    }
    if (i + 1 < cycling->width * cycling->height && (i + 1) / cycling->width == i / cycling->width)
    {
      cycling->pos_perm[cycling->nb_perms] = i + 1;
      cycling->nb_perms++;
    }
    cycling->upper_nb_perms[i] = cycling->nb_perms;
  }

  ASSERT (cycling->accessControl = rw_ac_create (), _("POSIX thread initialization error"));

  PUZZLE_DEBUG (cycling, _("Puzzle created.\n"));

  return cycling;
}

static void
sliding_puzzle_for_cycling_register_cycle (ACMachine (char) * sm, size_t length, char *moves)
{
  if (!sm || !length || !moves)
    return;

  char *key = malloc (length * sizeof (*key));
  char *revkey = malloc (length * sizeof (*revkey));

  // 16 combinations from one path
  // x-mirror (x 2)
  // y-mirror (x 2)
  // pi/2 rotation (x 2)
  // time-reversal (x 2)

  for (int lsign = 0; lsign <= 1; lsign++)
  {
    for (int csign = 0; csign <= 1; csign++)
    {
      for (int lcperm = 0; lcperm <= 1; lcperm++)
      {
        for (size_t i = 0; i < length; i++)
        {
          key[i] = moves[i];

          if (lsign)
            switch (key[i])
            {
              case 'r':
                key[i] = 'l';
                break;
              case 'l':
                key[i] = 'r';
                break;
            }

          if (csign)
            switch (key[i])
            {
              case 'u':
                key[i] = 'd';
                break;
              case 'd':
                key[i] = 'u';
                break;
            }

          if (lcperm)
            switch (key[i])
            {
              case 'u':
                key[i] = 'l';
                break;
              case 'd':
                key[i] = 'r';
                break;
              case 'l':
                key[i] = 'u';
                break;
              case 'r':
                key[i] = 'd';
                break;
            }

          switch (key[i])
          {
            case 'u':
              revkey[length - i - 1] = 'd';
              break;
            case 'd':
              revkey[length - i - 1] = 'u';
              break;
            case 'l':
              revkey[length - i - 1] = 'r';
              break;
            case 'r':
              revkey[length - i - 1] = 'l';
              break;
          }
        }

        int diff = 0;           // lexicographic comparison
        for (size_t i = 0; i < length; i++)
        {
          if (!diff)
          {
            if (key[i] > revkey[i])
              diff = 1;
            else if (key[i] < revkey[i])
              diff = -1;
          }
        }

        size_t key_length = length / 2 + 1;     // first integer larger than mid-length

        ZoneExtension *zcycle = calloc (1, sizeof (*zcycle));
        ZoneExtension *zrevcycle = calloc (1, sizeof (*zrevcycle));
        ZoneExtension *zmid = calloc (1, sizeof (*zmid));

        int dl, dc;

        dl = dc = 0;
        if (diff == 1)          // key > revkey, keep key
        {
          for (size_t i = key_length - 1; i < length; i++)
          {
            dl += key[i] == 'd' ? -1 : key[i] == 'u' ? 1 : 0;
            dc += key[i] == 'r' ? -1 : key[i] == 'l' ? 1 : 0;

            if (dl < zmid->lmin)
              zmid->lmin = dl;
            if (dl > zmid->lmax)
              zmid->lmax = dl;
            if (dc < zmid->cmin)
              zmid->cmin = dc;
            if (dc > zmid->cmax)
              zmid->cmax = dc;
          }
        }
        dl = dc = 0;
        for (size_t i = key_length; i < length; i++)
        {
          dl += key[i] == 'd' ? -1 : key[i] == 'u' ? 1 : 0;
          dc += key[i] == 'r' ? -1 : key[i] == 'l' ? 1 : 0;

          if (dl < zcycle->lmin)
            zcycle->lmin = dl;
          if (dl > zcycle->lmax)
            zcycle->lmax = dl;
          if (dc < zcycle->cmin)
            zcycle->cmin = dc;
          if (dc > zcycle->cmax)
            zcycle->cmax = dc;
        }

        dl = dc = 0;
        if (diff == -1)         // key < revkey, keep revkey
        {
          for (size_t i = key_length - 1; i < length; i++)
          {
            dl += revkey[i] == 'd' ? -1 : revkey[i] == 'u' ? 1 : 0;
            dc += revkey[i] == 'r' ? -1 : revkey[i] == 'l' ? 1 : 0;

            if (dl < zmid->lmin)
              zmid->lmin = dl;
            if (dl > zmid->lmax)
              zmid->lmax = dl;
            if (dc < zmid->cmin)
              zmid->cmin = dc;
            if (dc > zmid->cmax)
              zmid->cmax = dc;
          }
        }

        dl = dc = 0;
        for (size_t i = key_length; i < length; i++)
        {
          dl += revkey[i] == 'd' ? -1 : revkey[i] == 'u' ? 1 : 0;
          dc += revkey[i] == 'r' ? -1 : revkey[i] == 'l' ? 1 : 0;

          if (dl < zrevcycle->lmin)
            zrevcycle->lmin = dl;
          if (dl > zrevcycle->lmax)
            zrevcycle->lmax = dl;
          if (dc < zrevcycle->cmin)
            zrevcycle->cmin = dc;
          if (dc > zrevcycle->cmax)
            zrevcycle->cmax = dc;
        }

        Keyword (char) kw;
        ACM_KEYWORD_SET (kw, key, key_length);
#if TRACE == 2
        print_keyword (kw, zcycle);
#endif
        if (!ACM_register_keyword (sm, kw, zcycle))
#if TRACE == 3
          printf (" already registered\n")
#endif
            ;

        if (diff)
        {
          ACM_KEYWORD_SET (kw, revkey, key_length);
#if TRACE == 2
          print_keyword (kw, zrevcycle);
#endif
          if (!ACM_register_keyword (sm, kw, zrevcycle))
#if TRACE == 3
            printf (" already registered\n")
#endif
              ;
        }
        else
          free (zrevcycle);

        if (diff == 1)          // key > revkey, keep key
        {
          ACM_KEYWORD_SET (kw, key, key_length - 1);
#if TRACE == 2
          print_keyword (kw, zmid);
#endif
          if (!ACM_register_keyword (sm, kw, zmid))
#if TRACE == 3
            printf (" already registered\n")
#endif
              ;
        }
        else if (diff == -1)    // key < revkey, keep revkey
        {
          ACM_KEYWORD_SET (kw, revkey, key_length - 1);
#if TRACE == 2
          print_keyword (kw, zmid);
#endif
          if (!ACM_register_keyword (sm, kw, zmid))
#if TRACE == 3
            printf (" already registered\n")
#endif
              ;
        }
        else
          free (zmid);
      }
    }
  }

  free (key);
  free (revkey);
}

// Thread cancellable
static int
sliding_puzzle_for_cycling_search (Puzzle cycling, int depth_max)
{
  pthread_cleanup_push (sliding_puzzle_cancellation_msg, cycling);

  int prev_depth = 1;
  int next_depth = cycling->d2sol + 1;
  struct BufferIDA *buffer = malloc (sizeof (*buffer));
  buffer[0].move = 1;
  buffer[0].grid = 0;
  buffer[0].pos = 0;

  struct BuffersIDA b;
  b.pBuffer = &buffer;
  b.pBufferLength = &prev_depth;
  pthread_cleanup_push (sliding_puzzle_buffer_IDA_cleanup, &b);

#if DEBUG
  printf (_("Cycle search depth: "));
#endif
  if (!cycling->solved)
    while (next_depth - 1 < depth_max)
    {
#if DEBUG
      printf ("%i.", next_depth);
#endif

#ifdef TEST_CANCELLATION_POINT
      if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
          && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
        pthread_cancel (*thread_cancellation_point_test);
#endif
      // Cancellation point
      pthread_testcancel ();

      buffer = realloc (buffer, next_depth * sizeof (*buffer));

      for (int i = prev_depth; i < next_depth; i++)
      {
        buffer[i].grid = malloc (cycling->height * cycling->width * sizeof (*buffer[i].grid));
        buffer[i].pos = malloc (cycling->height * cycling->width * sizeof (*buffer[i].pos));
      }

      if (cycling->cycle_database)
      {
        cycling->cycle_state = ACM_reset (cycling->cycle_database->cycles);
        ACM_match (cycling->cycle_state, 'l');
      }
      else
        cycling->cycle_state = 0;
      prev_depth = next_depth;
      next_depth = sliding_puzzle_depth_first_recursive_search (cycling, next_depth - 1, 0, buffer + 1) + 1;

      if (cycling->solved == 0)
        /* nothing */ ;
      else if (cycling->solved < 0)
        break;
      else                      //  if (cycling->solved > 0)
      {
        // A cycle has been found
        // A cycle is a set of moves that leaves the grid unchanged, such as lr or ldruldruldru
        char *cycle = malloc (prev_depth * sizeof (*cycle));
        for (int i = 0; i < prev_depth; i++)
        {
          cycle[i] =
            buffer[i].move == -cycling->width ? 'd' : buffer[i].move == cycling->width ? 'u' : buffer[i].move ==
            -1 ? 'r' : 'l';
        }

#if DEBUG
        printf (_("\nCycle found: "));
        for (int i = 0; i < prev_depth; i++)
          printf ("%c", cycle[i]);
        printf ("\n");
#endif

        // Add this path to the list of deprecated paths
        Keyword (char) kw;
        ACM_KEYWORD_SET (kw, cycle, prev_depth);
#if TRACE == 2
        print_keyword (kw, 0);
#endif
        sliding_puzzle_for_cycling_register_cycle (cycling->cycle_database->cycles, prev_depth, cycle);

        free (cycle);

        // Try to solve the puzzle again, without using the registered cycle
        cycling->solved = 0;
      }
    }                           // end while

#if DEBUG
  printf ("\n");
#endif

  pthread_cleanup_pop (1);      // sliding_puzzle_solve_IDA_cleanup
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg

  PUZZLE_DEBUG (cycling, _("Cycles database done.\n"));

  return 0;
}

// Thread cancellable
static CycleDatabase
sliding_puzzle_cycle_bank_create (int width, int height, int max_length)
{
  if (max_length <= 0)
    return 0;

  Puzzle cycling = sliding_puzzle_for_cycling_init (width, height);
  if (!cycling)
    return 0;
  pthread_cleanup_push (sliding_puzzle_cycle_cleanup, cycling);

  CycleDatabase cb = calloc (1, sizeof (*cb));
  cb->cycles = ACM_create (char);
  cb->nbUsers = 1;
  pthread_cleanup_push (cycle_database_cleanup, cb);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif

  cycling->cycle_database = cb;
  // Cancellation point
  sliding_puzzle_for_cycling_search (cycling, max_length);

  pthread_cleanup_pop (0);      // sm_cleanup
  pthread_cleanup_pop (1);      // cycle_cleanup

  return cycling->cycle_database;
}

/** Cycles - END **/

/** Distance of tiles to solution - BEGIN **/
static int
sliding_puzzle_distances_breadth_first_search (int8_t * database, int width, int height, int pattern_size,
                                               int *init_pos)
{
  uintmax_t space_size = 1;
  int puzzle_size = width * height;
  int *pos = malloc (pattern_size * sizeof (*pos));
  int *next_pos = malloc (pattern_size * sizeof (*next_pos));

  uint32_t index = 0;
  uint32_t next_index = 0;
  for (int j = 0; j < pattern_size; j++)
  {
    space_size *= puzzle_size;
    index *= puzzle_size;
    index += init_pos[j];
  }

  uint32_t *queue;
  CHECK_ALLOC (queue = malloc (space_size * sizeof (*queue)));
  uint32_t *write = queue;
  uint32_t *read = queue;

  // Distance to the initial position is 0.
  database[index] = 0;
  *write = index;               // index of the initial configuration pos, means *read = index as well
  write++;

  int delta = 0;                // maximum difference between heuristic distance and Manhattan distance
  int distance = 0;

  // Calculating distances from the initial position 'pos', moving away from the initial position one move at a time:
  // First for positions one move away from the initial position, then 2 moves away, and so on.
  for (; read < write; read++)
  {
    index = *read;
    distance = database[index];
    for (int j = pattern_size - 1; j >= 0; j--)
    {
      pos[j] = index % puzzle_size;
      index /= puzzle_size;
    }

    // Make one more move of any tile in any direction.
    for (int move = 0; move < 4 * pattern_size; move++)
    {
      int tile = move / 4;      // tile to move
      int dir = move % 4;       // move
      if (dir == 0 && pos[tile] >= width)
        next_pos[tile] = pos[tile] - width;
      else if (dir == 1 && pos[tile] < puzzle_size - width)
        next_pos[tile] = pos[tile] + width;
      else if (dir == 2 && pos[tile] % width)
        next_pos[tile] = pos[tile] - 1;
      else if (dir == 3 && (pos[tile] + 1) % width)
        next_pos[tile] = pos[tile] + 1;
      else
      {
        //printf ("f");
        continue;               // forbidden move (out of the grid)
      }

      next_index = 0;
      for (int j = 0; j < pattern_size; j++)
      {
        if (j != tile)
          next_pos[j] = pos[j];
        next_index *= puzzle_size;
        next_index += next_pos[j];
      }

      // Do not update database[index]
      // - either it has already been calculated (theses positions of tiles are reachable with less moves)
      // - or it is a forbidden position (two tiles at the same position, see sliding_puzzle_heuristic_database_create).
      if (database[next_index] >= 0)
      {
        //printf ("i");
        continue;
      }
      // Compares the distance of the block of tiles to the its (higher) Manhattan distance (md)
      int md = 0;
      for (int j = 0; j < pattern_size; j++)
      {
        int delta_line = next_pos[j] / width - init_pos[j] / width;
        if (delta_line < 0)
          delta_line = -delta_line;

        int delta_col = next_pos[j] % width - init_pos[j] % width;
        if (delta_col < 0)
          delta_col = -delta_col;

        md += delta_line + delta_col;
      }

      if (distance + 1 - md > delta)
        delta = distance + 1 - md;

      database[next_index] = distance + 1;
      *write = next_index;
      write++;
    }
  }

#if TRACE == 2
  printf ("[%ti / %ju = %f%%]", write - queue, space_size, 100. * (write - queue) / space_size);
#endif

  free (queue);
  free (pos);
  free (next_pos);

  return delta;
}

// Create and initialize a database
static int8_t *
sliding_puzzle_heuristic_database_create (int width, int height, int size, int *start_pos)
{
  // Space of 'size' dimensions, for all possible positions for 'size' tiles.
  int *positions = malloc (size * sizeof (*positions));
  // Space size for 'size' tiles with 'width * height' positions on each dimension
  uintmax_t space_size = 1;
  uintmax_t forbidden = 0;

  // Calculate space size, and initialize all the 'size' tiles at the initial position (position 0).
  for (int j = 0; j < size; j++)
  {
    space_size *= width * height;
    positions[j] = 0;
  }

  // Database of distances from initial positions 'target_pos'
  int8_t *database;
  CHECK_ALLOC (database = malloc (space_size * sizeof (*database)));

  // Initialize the database:
  // Loop on all possible positions of 'size' tiles reachable from every possible moves.
  // Reachable positions are marked with -1, unreachable positions with 0.
  for (uintmax_t ii = 0; ii < space_size; ii++)
  {
    database[ii] = -1;

    // Invalidate forbidden positions of tiles (if two tiles are on the same position).
    for (int j1 = 0; j1 < size; j1++)
      for (int j2 = j1 + 1; j2 < size; j2++)
        if (positions[j1] == positions[j2])
        {
          database[ii] = 0;     // forbiden position (two tiles at the same position)
          j1 = j2 = size;       // stop the loop
          forbidden++;
        }

    // Move one tile
    for (int j = 0; j < size; j++)
    {
      positions[j]++;
      if (positions[j] >= width * height)
        positions[j] = 0;
      else
        break;
    }
  }
  free (positions);

#if TRACE == 2
  printf ("[%ti / %ju = %f%%]", (space_size - forbidden), space_size, 100. * (space_size - forbidden) / space_size);
#endif

#if DEBUG
  int delta_manhattan =
#endif
    sliding_puzzle_distances_breadth_first_search (database, width, height, size, start_pos);
#if DEBUG
  printf (_(" (heuristic distance is %i moves longer than Manhattan distance)"), delta_manhattan);
#endif
  return database;
}

/** Distance of tiles to solution - END **/

// Computes the distance to solutions of puzzle adding distance of blocks (patterns) of tiles
// to solution rather than the Manhattan distance.
inline static void
sliding_puzzle_compute_heuristic_distances_to_solutions (Puzzle puzzle)
{
  HeuristicDatabase hdb = puzzle->heuristic_database;
  int size = puzzle->width * puzzle->height;

  // Distances to final solutions (Heuristic database patterns)
  // Distance to solution
  puzzle->d2sol = 0;
  for (int i = 0; i < hdb->size_sol; i++)
  {
    uintmax_t index = 0;
    struct sHeuristicData db = hdb->database_sol[i];
    for (int j = 0; j < db.nb_tiles; j++)
    {
      index *= size;
      index += puzzle->pos[db.tiles[j]];
    }
    puzzle->d2sol += db.database[index];
  }
  if (hdb->mirror_sol)
  {
    int d2sol = 0;
    for (int i = 0; i < hdb->size_sol; i++)
    {
      uintmax_t index = 0;
      struct sHeuristicData db = hdb->database_sol[i];
      for (int j = 0; j < db.nb_tiles; j++)
      {
        index *= size;
        index += hdb->mirror_pos[puzzle->pos[hdb->mirror_sol[db.tiles[j]]]];
      }
      d2sol += db.database[index];
    }
    if (d2sol > puzzle->d2sol)
      puzzle->d2sol = d2sol;
  }
}

// Computes the distance to solution using the Manhattan distance.
static int
sliding_puzzle_initialize_distances_to_solutions (Puzzle puzzle)
{
  puzzle->d2sol = 0;
  if (puzzle->heuristic_database)
    sliding_puzzle_compute_heuristic_distances_to_solutions (puzzle);
  else
  {
    // Manhattan distance to solution = sum of differences of rows and differences of columns.
    // The distance of a tile to its taarget position (when the puzzle is ordered) is
    // the number of rows between initial and final position plus
    // the number of columns between initial and final position
    // The distance of a puzzle grid to its solution is the sum of the distance of each tile to its final position.
    for (int i = 0; i < puzzle->width * puzzle->height; i++)
      if (puzzle->grid_sol[i] != 0)
        for (int j = 0; j < puzzle->width * puzzle->height; j++)
          if (puzzle->grid[j] == puzzle->grid_sol[i])
          {
            int delta_line = j / puzzle->width - i / puzzle->width;     // number of rows between initial and final position
            if (delta_line < 0)
              delta_line *= -1;

            int delta_col = j % puzzle->width - i % puzzle->width;      // number of columns between initial and final position
            if (delta_col < 0)
              delta_col *= -1;

            puzzle->d2sol += delta_line + delta_col;
            break;
          }
  }

  PUZZLE_PRINT (puzzle, _("Distance to target: %i\n"), puzzle->d2sol);

  puzzle->solved = puzzle->d2sol ? 0 : 1;

  return puzzle->d2sol;
}

/** Optimized solution searches algorithms - BEGIN **/

/** DFRS **/
static int
sliding_puzzle_depth_first_recursive_search (Puzzle puzzle, int depth, int last, struct BufferIDA *buffer)
{
  // Solved ?
  if (puzzle->d2sol == 0)
  {
    puzzle->solved = 1;
    return 0;
  }

  // puzzle->pos[0] is the position of tile 0, that is the empty position.
  int first_move = 0;
  if (puzzle->pos[0] > 0)
    first_move = puzzle->upper_nb_perms[puzzle->pos[0] - 1];
  int last_move = puzzle->upper_nb_perms[puzzle->pos[0]];

  int next_depth = INT_MAX;
  puzzle->solved = 0;

  // Loop on authorized moves.
  // Try every authorized move for tile 0 (empty position).
  // upper_nb_perms[i] is is the number of possible moves for tiles for which position is between 0 and position i.
  // pos_perm, with index from 0 to nb_perms[i] (excluded) is the list of those authorized moves.
  for (int move = first_move; move < last_move; move++)
  {
    int initpos = puzzle->pos_perm[move];       // initial tile position = blank final position
    int finalpos = puzzle->pos[0];      // blank initial position = final tile position

    buffer->move = initpos - finalpos;  // blank tile move

    int tile = puzzle->grid[initpos];   // Tile to move

    int li = initpos / puzzle->width;   // line of initial tile position
    int ci = initpos % puzzle->width;   // column ...

    int orient = 0;
    const ACState (char) * s = puzzle->cycle_state;
    if (s)                      // If a cycle bank is defined
    {
      if ((orient = puzzle->orient))    // assignation here, not comparison.
        // True only for cycling puzzles, not standard puzzles.
      {
        if (initpos - finalpos == puzzle->width)
          continue;
        orient = 0;
      }

      // Check if the last moves would be a cycle (non efficient moves).
      // Update state machine with blank tile move
      ZoneExtension *z = 0;
      if (ACM_match (s,
                     buffer->move == puzzle->width ? 'u' :
                     buffer->move == -puzzle->width ? 'd' : buffer->move == 1 ? 'l' : 'r'))
      {
        void *v;
        ACM_get_match (s, 0, 0, &v);
        z = v;
      }

      // If the last moves describe a cycle not hitting edges of the puzzle,
      // then the last move is useless and not tried further.
      if (z && (z->lmin + li >= 0) && (z->lmax + li < puzzle->height) && (z->cmin + ci >= 0)
          && (z->cmax + ci < puzzle->width))
        continue;
    }
    // If the last moves is the opposite of the previous one,
    // then the last move is useless and not tried further.
    else if (buffer->move == -last)
      continue;

    buffer->nbGeneratedNodes++;

    // Copy constructor
    struct sPuzzle successor = *puzzle;
    successor.orient = orient;
    successor.grid = buffer->grid;
    memcpy (buffer->grid, puzzle->grid, successor.width * successor.height * sizeof (*buffer->grid));
    successor.pos = buffer->pos;
    memcpy (buffer->pos, puzzle->pos, successor.width * successor.height * sizeof (*buffer->pos));
    successor.cycle_state = s;
    successor.accessControl = 0;

    // Move blank tile
    successor.grid[initpos] = 0;
    successor.grid[finalpos] = tile;
    successor.pos[0] = initpos;
    successor.pos[tile] = finalpos;

    if (successor.heuristic_database)
      sliding_puzzle_compute_heuristic_distances_to_solutions (&successor);
    else
    {
      // Recompute Manhattan distance
      int lf = finalpos / puzzle->width;
      int cf = finalpos % puzzle->width;

      int delta_line, delta_col;
      int vs1 = successor.pos_sol[tile];
      int ls1 = vs1 / successor.width;
      int cs1 = vs1 % successor.width;

      delta_line = li > ls1 ? li - ls1 : ls1 - li;
      delta_col = ci > cs1 ? ci - cs1 : cs1 - ci;
      successor.d2sol -= delta_line + delta_col;
      delta_line = lf > ls1 ? lf - ls1 : ls1 - lf;
      delta_col = cf > cs1 ? cf - cs1 : cs1 - cf;
      successor.d2sol += delta_line + delta_col;
    }

    // If the minimal distance to solution is lower than the maximum length of searched solutions (depth) then
    // make an extra move, calling sliding_puzzle_depth_first_recursive_search one step further.
    int b = successor.d2sol;
    if (b < depth)
      // recursive call, returns the minimal distance of successor to solution.
      b = sliding_puzzle_depth_first_recursive_search (&successor, depth - 1, buffer->move, buffer + 1);
    // else don't try further because we have reached the depth limit.

    // The minimal distance of puzzle to solution is the minimal distance of successor to solution plus one.
    if (b >= 0 && b < INT_MAX)
      b++;

    if (successor.solved < 0)
    {
      puzzle->solved = successor.solved;
      return INT_MAX;
    }
    else if (successor.solved > 0)      // If solved
    {
      puzzle->solved = successor.solved;
      return b;
    }

    // In case finding a solution for successor would require more than INT_MAX moves (most unlikely)
    // give up for this try and try other another move.
    if (b < 0)
      continue;

    if (b < next_depth)
      next_depth = b;
    // Not solved yet. Continue the loop for the next authorized move.
  }

  if (next_depth < INT_MAX)
    return next_depth;          // Returns the smallest minimal distance to solution for all possible moves of tile
  else
    return -1;                  // In case finding a solution for puzzle would require more than INT_MAX moves (most unlikely)
}

/** BFRS **/
static int
sliding_puzzle_best_first_recursive_search (NodeRBFS * N, int depth, int V, int max_depth, struct BufferRBFS **pBuffer,
                                            int *pBufferLength)
{
  Puzzle node = &N->node;
  // Solved ?
  int h_node = node->d2sol;
  if (h_node == 0)
  {
    node->solved = 1;
    return depth;
  }

  // Minimal distance (depth + h_node) is larger than INT_MAX : abort (most unlikely)
  if (h_node > INT_MAX - depth)
  {
    node->solved = -1;
    return INT_MAX;
  }

  // If the minimal distance to solution (depth + h_node) is higher than the searched solution length
  // return the minimal distance to solution.
  if (h_node > max_depth - depth)
    return depth + h_node;

  if (depth >= *pBufferLength)
  {
    *pBuffer = realloc (*pBuffer, (depth + 1) * sizeof (**pBuffer));
    for (int i = *pBufferLength; i < depth + 1; i++)
      for (int c = 0; c < 4; c++)
      {
        (*pBuffer)[i].grid[c] = malloc (node->height * node->width * sizeof (*(*pBuffer)[i].grid[c]));  // needs cancel cleanup
        (*pBuffer)[i].pos[c] = malloc (node->height * node->width * sizeof (*(*pBuffer)[i].pos[c]));    // needs cancel cleanup
        (*pBuffer)[i].nbGeneratedNodes = 0;
      }
    *pBufferLength = depth + 1;
    PUZZLE_PRINT (node, "%i.", depth + 1);

#ifdef TEST_CANCELLATION_POINT
    if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
        && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
      pthread_cancel (*thread_cancellation_point_test);
#endif
    // Cancellation point
    pthread_testcancel ();
  }

  int first_move = 0;
  if (node->pos[0] > 0)
    first_move = node->upper_nb_perms[node->pos[0] - 1];
  int last_move = node->upper_nb_perms[node->pos[0]];

  NodeRBFS children[4];
  int nbChildren = 0;

  // Loop on authorized moves.
  // Try every authorized move for tile 0 (empty position).
  // upper_nb_perms[i] is is the number of possible moves for tiles for which position is between 0 and position i.
  // pos_perm, with index from 0 to nb_perms[i] (excluded) is the list of those authorized moves.
  for (int move = first_move; move < last_move; move++)
  {
    int initpos = node->pos_perm[move]; // blank final position
    int tile = node->grid[initpos];

    int finalpos = node->pos[0];        // blank initial position
    int m = initpos - finalpos; // blank tile move

    int li = initpos / node->width;
    int ci = initpos % node->width;

    int orient = 0;

    const ACState (char) * s = node->cycle_state;
    if (s)                      // If a cycle bank is defined
    {
      if ((orient = node->orient))      // assignation here, not comparison.
        // True only for cycling puzzles, not standard puzzles.
      {
        if (initpos - finalpos == node->width)
          continue;
        orient = 0;
      }

      // Check if the last moves would be a cycle (non efficient moves).
      // Update state machine with blank tile move
      ZoneExtension *z = 0;
      if (ACM_match (s, m == node->width ? 'u' : m == -node->width ? 'd' : m == 1 ? 'l' : 'r'))
      {
        void *v;
        ACM_get_match (s, 0, 0, &v);
        z = v;
      }

      // If the last moves describe a cycle not hitting edges of the puzzle,
      // then the last move is useless and not tried further.
      if (z && (z->lmin + li >= 0) && (z->lmax + li < node->height) && (z->cmin + ci >= 0)
          && (z->cmax + ci < node->width))
        continue;
    }
    // If the last moves is the opposite of the previous one,
    // then the last move is useless and not tried further.
    else if (m == -N->move)
      continue;

    nbChildren++;
    children[nbChildren - 1].node = *node;
    children[nbChildren - 1].move = m;

    (*pBuffer)[depth].nbGeneratedNodes++;

    // Copy constructor
    Puzzle successor = &children[nbChildren - 1].node;
    successor->orient = orient;
    successor->grid = (*pBuffer)[depth].grid[nbChildren - 1];
    memcpy (successor->grid, node->grid, successor->width * successor->height * sizeof (*successor->grid));
    successor->pos = (*pBuffer)[depth].pos[nbChildren - 1];
    memcpy (successor->pos, node->pos, successor->width * successor->height * sizeof (*successor->pos));
    successor->cycle_state = s;

    // Move blank tile
    successor->grid[initpos] = 0;
    successor->grid[finalpos] = tile;
    successor->pos[0] = initpos;
    successor->pos[tile] = finalpos;
    successor->accessControl = 0;

    if (successor->heuristic_database)
      sliding_puzzle_compute_heuristic_distances_to_solutions (successor);
    else
    {
      // Recompute Manhattan distance
      int lf = finalpos / node->width;
      int cf = finalpos % node->width;

      int delta_line, delta_col;
      int vs1 = successor->pos_sol[tile];
      int ls1 = vs1 / successor->width;
      int cs1 = vs1 % successor->width;

      delta_line = li > ls1 ? li - ls1 : ls1 - li;
      delta_col = ci > cs1 ? ci - cs1 : cs1 - ci;
      successor->d2sol -= delta_line + delta_col;
      delta_line = lf > ls1 ? lf - ls1 : ls1 - lf;
      delta_col = cf > cs1 ? cf - cs1 : cs1 - cf;
      successor->d2sol += delta_line + delta_col;
    }

    // Minimal distance of initial puzzle to solution after the extra move
    children[nbChildren - 1].F = depth + 1 + successor->d2sol;

    // Minimal distance of initial puzzle to solution before the extra move
    int f = depth + h_node;

    // If both minimal distance (before and after move) are lower than V, then
    // the minimal is kept identical to the previous depth of iteration.
    if (children[nbChildren - 1].F < V && f < V)
      children[nbChildren - 1].F = V;
  }

  if (nbChildren == 0)
    return INT_MAX;

  NodeRBFS *first = 0;
  if (nbChildren == 1)
  {
    first = &children[0];
    while (first->node.solved == 0 && first->F <= max_depth)
    {
#ifdef TEST_CANCELLATION_POINT
      if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
          && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
        pthread_cancel (*thread_cancellation_point_test);
#endif
      first->F =
        // Cancellation point
        // recursive call
        sliding_puzzle_best_first_recursive_search (first, /* depth = */ depth + 1, /* V = */ first->F, max_depth,
                                                    pBuffer, pBufferLength);
    }
  }
  else
  {
    // Loop until puzzle is solved
    while (1)
    {
      // Find first and second node, sorted by increasing F
      first = &children[0];
      NodeRBFS *second = &children[1];
      if (children[1].F < children[0].F)
      {
        first = &children[1];
        second = &children[0];
      }
      for (int i = 2; i < nbChildren; i++)
        if (children[i].F < first->F)
        {
          second = first;
          first = &children[i];
        }
        else if (children[i].F < second->F)
          second = &children[i];

      if (first->F > max_depth)
        break;

#ifdef TEST_CANCELLATION_POINT
      if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
          && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
        pthread_cancel (*thread_cancellation_point_test);
#endif
      // max_depth = min (second->F, max_depth)
      if (second->F < max_depth)
        first->F =
          // Cancellation point
          // recursive call
          sliding_puzzle_best_first_recursive_search (first, /* depth = */ depth + 1, /* V = */ first->F,
                                                      /* max_depth = */ second->F, pBuffer, pBufferLength);
      else
        first->F =
          // Cancellation point
          // recursive call
          sliding_puzzle_best_first_recursive_search (first, /* depth = */ depth + 1, /* V = */ first->F, max_depth,
                                                      pBuffer, pBufferLength);

      if (first->node.solved)
        break;
    }
  }

  // Solved
  N->node.solved = first->node.solved;
  (*pBuffer)[depth].move = first->move;

  // Returns the distance of puzzle to solution.
  int ret = first->F;

  return ret;
}

/** Optimized solution searches algorithms - END **/

/** Sliding puzzle toolbox - END **/

/** User interface - BEGIN **/

/** Helpers - BEGIN **/
Puzzle
sliding_puzzle_init4 (int width, int height, int *grid, FILE * f)
{
  // Check grid validity
  if (width <= 0 || height <= 0 || width * height < 2)
    return 0;

  if (grid)
    for (int i = 0; i < width * height; i++)
    {
      if (grid[i] < 0 || grid[i] >= width * height)
        return 0;
      else
        for (int j = i + 1; j < width * height; j++)
          if (grid[i] == grid[j])
            return 0;
    }

  Puzzle puzzle = malloc (sizeof (*puzzle));
  puzzle->width = width;
  puzzle->height = height;
  puzzle->orient = 0;
  puzzle->cycle_database = 0;
  puzzle->cycle_state = 0;
  puzzle->heuristic_database = 0;
  puzzle->stream = f;
  puzzle->solution_shower = 0;

  // Target solution for an odd grid :
  // - ascending from 0 to (width * height - 1)
  // - the empty tile is on the upper left corner
  puzzle->grid_sol = malloc (width * height * sizeof (*puzzle->grid_sol));
  puzzle->pos_sol = malloc (width * height * sizeof (*puzzle->pos_sol));
  for (int i = 0; i < width * height; i++)
    puzzle->grid_sol[i] = i;    // Tile at position i

  for (int i = 0; i < width * height; i++)
    puzzle->pos_sol[puzzle->grid_sol[i]] = i;   // Position of tile i

  // Loop on authorized moves.
  // Initialize valid permutations : valid moves, considering grid borders
  // upper_nb_perms[i] is is the number of possible moves for tiles for which position is between 0 and position i.
  // pos_perm, with index from 0 to nb_perms[i] (excluded) is the list of those authorized moves.
  puzzle->pos_perm = malloc ((4 * width * height - 2 * (width + height)) * sizeof (*puzzle->pos_perm));
  puzzle->nb_perms = 0;
  puzzle->upper_nb_perms = malloc (width * height * sizeof (*puzzle->upper_nb_perms));
  for (int i = 0; i < width * height; i++)
  {
    if (i - width >= 0)         // not the upper border
    {
      puzzle->pos_perm[puzzle->nb_perms] = i - width;
      puzzle->nb_perms++;
    }
    if (i + width < width * height)     // not the lower border
    {
      puzzle->pos_perm[puzzle->nb_perms] = i + width;
      puzzle->nb_perms++;
    }
    if (i - 1 >= 0 && (i - 1) / width == i / width)     // not the leftmost border
    {
      puzzle->pos_perm[puzzle->nb_perms] = i - 1;
      puzzle->nb_perms++;
    }
    if (i + 1 < width * height && (i + 1) / width == i / width) // not the rightmost border
    {
      puzzle->pos_perm[puzzle->nb_perms] = i + 1;
      puzzle->nb_perms++;
    }
    puzzle->upper_nb_perms[i] = puzzle->nb_perms;
  }

  // Initialisze puzzle->grid either from argument 'grid' or randmoly
  puzzle->grid = malloc (width * height * sizeof (*puzzle->grid));
  if (grid)                     // initialization tiles from 'grid'
    memcpy (puzzle->grid, grid, width * height * sizeof (*puzzle->grid));
  else                          // random initialization
  {
    for (int i = 0; i < width * height; i++)
      while (1)
      {
        int tile = alea (width * height - 1);
        for (int j = 0; j < i; j++)
          if (puzzle->grid[j] == tile)
          {
            tile = -1;
            break;
          }
        if (tile != -1)
        {
          puzzle->grid[i] = tile;
          break;
        }
      }
  }

  // Compute grid parity
  for (int i = 0; i < width * height; i++)
    if (puzzle->grid[i] == 0)
    {
      puzzle->parity = i / puzzle->width;
      break;
    }

  for (int pos1 = 0; pos1 < width * height; pos1++)
    for (int pos2 = pos1 + 1; pos2 < width * height; pos2++)
      if (puzzle->grid[pos1] != 0 && puzzle->grid[pos2] != 0
          && puzzle->pos_sol[puzzle->grid[pos1]] > puzzle->pos_sol[puzzle->grid[pos2]])
        puzzle->parity++;

  PUZZLE_PRINT (puzzle, _("Puzzle (%s):\n"), puzzle->parity % 2 ? _("odd") : _("even"));

  // Display puzzle->grid
  for (int i = 0; i < height; i++)
  {
    for (int j = 0; j < width; j++)
      if (puzzle->grid[i * height + j])
        PUZZLE_PRINT (puzzle, " %2i", puzzle->grid[i * height + j]);
      else
        PUZZLE_PRINT (puzzle, "  .");
    PUZZLE_PRINT (puzzle, "\n");
  }

  // Switch an odd parity grid to an even one applying a central symetry on position and tile number
  if (puzzle->parity % 2)       // odd parity
    for (int pos = 0; pos < width * height / 2; pos++)
    {
      int tmp = puzzle->grid[pos];
      if (puzzle->grid[width * height - 1 - pos])
        puzzle->grid[pos] = width * height - puzzle->grid[width * height - 1 - pos];
      else
        puzzle->grid[pos] = 0;
      if (tmp)
        puzzle->grid[width * height - 1 - pos] = width * height - tmp;
      else
        puzzle->grid[width * height - 1 - pos] = 0;
    }

  puzzle->pos = malloc (width * height * sizeof (*puzzle->pos));
  for (int i = 0; i < width * height; i++)
    puzzle->pos[puzzle->grid[i]] = i;   // Position of tiles

  // Display target solution
  PUZZLE_PRINT (puzzle, _("Target is:\n"));
  for (int i = 0; i < height; i++)
  {
    for (int j = 0; j < width; j++)
      if (puzzle->parity % 2 == 0)
      {
        if (puzzle->grid_sol[i * height + j])
          PUZZLE_PRINT (puzzle, " %2i", puzzle->grid_sol[i * height + j]);
        else
          PUZZLE_PRINT (puzzle, "  .");
      }
      else
      {
        if (puzzle->grid_sol[width * height - 1 - i * height - j])
          PUZZLE_PRINT (puzzle, " %2i", width * height - puzzle->grid_sol[width * height - 1 - i * height - j]);
        else
          PUZZLE_PRINT (puzzle, "  .");
      }
    PUZZLE_PRINT (puzzle, "\n");
  }

  ASSERT (puzzle->accessControl = rw_ac_create (), _("POSIX thread initialization error"));

  PUZZLE_DEBUG (puzzle, _("Puzzle created.\n"));

  puzzle->solved = 0;
  puzzle->solution_length = 0;
  puzzle->solution = 0;

  return puzzle;
}

Puzzle
sliding_puzzle_init2 (int width, int height)
{
  // Random grid, standard output stream.
  return sliding_puzzle_init4 (width, height, 0, 0);
}

int
sliding_puzzle_parity_get (Puzzle puzzle)
{
  return puzzle->parity % 2;
}

// Get 'puzzle' grid into 'grid'
void
sliding_puzzle_grid_get (Puzzle puzzle, int grid[])
{
  for (int pos = 0; pos < puzzle->width * puzzle->height; pos++)
    if (!(puzzle->parity % 2))
      grid[pos] = puzzle->grid[pos];
    else if (puzzle->grid[puzzle->width * puzzle->height - 1 - pos])
      grid[pos] = puzzle->width * puzzle->height - puzzle->grid[puzzle->width * puzzle->height - 1 - pos];
    else
      grid[pos] = 0;
}

// Get the optimal (shortest) sequence of moves to order the puzzle.
void
sliding_puzzle_solution_get (Puzzle puzzle, int solution[])
{
  for (int i = 0; i < puzzle->solution_length; i++)
    solution[i] = puzzle->solution[i];
}

// Thread safe
Puzzle_move_handler
sliding_puzzle_move_handler_set (Puzzle puzzle, Puzzle_move_handler mh)
{
  Puzzle_move_handler ret = 0;
  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_write_begin (puzzle);

  ret = puzzle->solution_shower;
  puzzle->solution_shower = mh;

  sliding_puzzle_write_end (puzzle);
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
  return ret;
}

// Thread safe
FILE *
sliding_puzzle_stream_set (Puzzle puzzle, FILE * f)
{
  FILE *ret = 0;
  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_write_begin (puzzle);

  ret = puzzle->stream;
  puzzle->stream = f;

  sliding_puzzle_write_end (puzzle);
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
  return ret;
}

/** Cycles database creation for puzzle - BEGIN **/

// Thread cancelable, thread safe
void
sliding_puzzle_cycle_database_attach (Puzzle puzzle, int cycle_size)
{
  if (!puzzle || puzzle->width * puzzle->height == 0 || cycle_size <= 0)
    return;

  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
  PUZZLE_PRINT (puzzle, _("Search for cycles and record cycles in bank using IDA* (up to %i moves)...\n"), cycle_size);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  // Create a cycle database for the size of the puzzle
  CycleDatabase s = sliding_puzzle_cycle_bank_create (puzzle->width, puzzle->height, cycle_size);       // allocation

  pthread_cleanup_push (cycle_database_cleanup, s);

#if TRACE == 2
  ACM_foreach_keyword (s->cycles, print_match);
#endif

  PUZZLE_PRINT (puzzle, _("%lu forbidden sequences of moves have been found.\n"), ACM_nb_keywords (s->cycles));
  PUZZLE_PRINT (puzzle, _("Search for cycles and record cycles in bank using IDA* (up to %i moves)...DONE\n"),
                cycle_size);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif

  // Cancellation point
  sliding_puzzle_write_begin (puzzle);

  // The puzzle has already been previously attached.
  if (sliding_puzzle_cycle_bank_release (puzzle))
    PUZZLE_PRINT (puzzle, _("Cycle bank released.\n"));

  puzzle->cycle_database = s;
  puzzle->cycle_database->nbUsers = 1;

  PUZZLE_PRINT (puzzle, _("Cycle bank attached.\n"));
  sliding_puzzle_write_end (puzzle);

  pthread_cleanup_pop (0);      // state_cleanup
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
}

// Thread cancellable, thread safe
int
sliding_puzzle_cycle_database_share (Puzzle orig, Puzzle dest)
{
  if (!orig || !dest)
    return 0;

  if (orig == dest)
    return 1;

  // Check for compliant puzzles (same size)
  if (orig->width != dest->width || orig->height != dest->height)
    return 0;

  int ret = 0;

  pthread_cleanup_push (sliding_puzzle_cancellation_msg, dest);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_read_begin (orig);
  pthread_cleanup_push (sliding_puzzle_solve_read_clean, orig);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_write_begin (dest);

  if (orig->cycle_database == 0 || dest->cycle_database == 0 || orig->cycle_database != dest->cycle_database)
  {
    if (sliding_puzzle_cycle_bank_release (dest))
      PUZZLE_PRINT (dest, _("Cycle bank released.\n"));

    if (orig->cycle_database)
    {
      dest->cycle_database = orig->cycle_database;
      dest->cycle_database->nbUsers++;
      ret = 1;
      PUZZLE_PRINT (dest, _("Cycle bank shared with puzzle [%p].\n"), (void *) orig);
    }
  }
  sliding_puzzle_write_end (dest);
  pthread_cleanup_pop (1);      //sliding_puzzle_read_end (orig);
  pthread_cleanup_pop (0);      //sliding_puzzle_cancellation_msg

  return ret;
}

/** Cycles database creation for puzzle - END **/

/** Heuristic database creation for puzzle - BEGIN **/

// Thread cancellable, thread safe
void
sliding_puzzle_heuristic_database_attach (Puzzle puzzle, int pattern_size)
{
  if (!puzzle || puzzle->width * puzzle->height == 0 || pattern_size <= 0)
    return;

  int size_max = 0;
  for (uintmax_t max = 1;
       max <= UINTMAX_MAX / puzzle->width / puzzle->height && max * puzzle->width * puzzle->height - 1 <= UINT32_MAX
       && max <= SIZE_MAX / puzzle->width / puzzle->height; size_max++)
    max *= puzzle->width * puzzle->height;

  if (pattern_size > size_max)
    pattern_size = size_max;

  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
  PUZZLE_PRINT (puzzle, _("Create heuristic database using breadth-first search (pattern max size is %1$i%2$s)...\n"),
                pattern_size, pattern_size == size_max ? _(", restricted by hardware capabilities") : "");

  // Create a heuristic database for the size of the puzzle
  HeuristicDatabase database = malloc (sizeof (*database));
  database->mirror_pos = database->mirror_sol = 0;
  database->database_sol = 0;
  database->nbUsers = 0;

  pthread_cleanup_push (sliding_puzzle_heuristic_database_cleanup, database);

  // If the puzzle is a square then it can be mirrored along its diagonal.
  if (puzzle->width == puzzle->height)
  {
    // mirror_pos[i] is the mirrored position of position i.
    database->mirror_pos = malloc (puzzle->width * puzzle->height * sizeof (*database->mirror_pos));
    for (int i = 0; i < puzzle->width * puzzle->height; i++)
      database->mirror_pos[i] = puzzle->width * (i % puzzle->width) + (i / puzzle->width);

    // If, moreover, the empty spot is on the diagonal, then the target solution can be mirrored along the diagonal.
    if (puzzle->pos_sol[0] % (puzzle->width + 1) == 0)
    {
      database->mirror_sol = malloc (puzzle->width * puzzle->height * sizeof (*database->mirror_sol));
      for (int i = 0; i < puzzle->width * puzzle->height; i++)
        database->mirror_sol[puzzle->grid_sol[i]] = puzzle->grid_sol[database->mirror_pos[i]];
    }
  }

  // The number of blocks of adjacent 'pattern_size' tiles in the ouzzle
  int nb_pattern = (puzzle->width * puzzle->height - 2 + pattern_size) / pattern_size;

  database->size_sol = nb_pattern;
  database->database_sol = malloc (database->size_sol * sizeof (*database->database_sol));
  for (int l = 0; l < nb_pattern; l++)
  {
    database->database_sol[l].database = 0;
    database->database_sol[l].tiles = 0;
  }

  // Create 'nb_pattern' blocks of 'pattern_size' adjacent tiles
  PUZZLE_PRINT (puzzle, _("Patterns for target:\n"));
  int p_start, p;
  p_start = 0;
  for (int l = 0; l < nb_pattern; l++)
  {
    int *tiles = 0;
    int *positions = 0;
    int nb_tiles = 0;

    // Create a block of 'pattern_size' adjacent tiles
    PUZZLE_PRINT (puzzle, _("Tiles"));
    for (p = p_start; nb_tiles < pattern_size && p < puzzle->width * puzzle->height; p++)
    {
      int pos = p / puzzle->width;
      if (pos % 2)
        pos = (pos + 1) * puzzle->width - p % puzzle->width - 1;
      else
        pos = p;
      if (pattern_size % 2 == 0)
        pos = puzzle->width * puzzle->height - 1 - pos;
      if (puzzle->grid_sol[pos])        // not the blank
      {
        nb_tiles++;
        tiles = realloc (tiles, nb_tiles * sizeof (*tiles));
        tiles[nb_tiles - 1] = puzzle->grid_sol[pos];
        if (puzzle->parity % 2)
          PUZZLE_PRINT (puzzle, " %2i", puzzle->width * puzzle->height - tiles[nb_tiles - 1]);
        else
          PUZZLE_PRINT (puzzle, " %2i", tiles[nb_tiles - 1]);
        positions = realloc (positions, nb_tiles * sizeof (*positions));
        positions[nb_tiles - 1] = pos;
      }
    }

    if (nb_tiles)
    {
      if (database->mirror_sol)
      {
        PUZZLE_PRINT (puzzle, _(", (mirrored along first diagonal)"));
        for (int t = 0; t < nb_tiles; t++)
          if (puzzle->parity % 2)
            PUZZLE_PRINT (puzzle, " %2i", puzzle->width * puzzle->height - database->mirror_sol[tiles[t]]);
          else
            PUZZLE_PRINT (puzzle, " %2i", database->mirror_sol[tiles[t]]);
      }

      // For each block, calculate distances of any positions of the tiles of this block to the target solution.
      database->database_sol[l].database =
        sliding_puzzle_heuristic_database_create (puzzle->width, puzzle->height, nb_tiles, positions);
      database->database_sol[l].tiles = tiles;
      database->database_sol[l].nb_tiles = nb_tiles;
    }
    free (positions);

    PUZZLE_PRINT (puzzle, "\n");

    if (p == puzzle->width * puzzle->height)
      break;
    else
      p_start = p;

#ifdef TEST_CANCELLATION_POINT
    if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
        && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
      pthread_cancel (*thread_cancellation_point_test);
#endif
    // Cancellation point
    pthread_testcancel ();
  }

  PUZZLE_PRINT (puzzle, _("Create heuristic database using breadth-first search (pattern max size is %i)...DONE\n"),
                pattern_size);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif

  // Cancellation point
  sliding_puzzle_write_begin (puzzle);
  if (sliding_puzzle_heuristic_database_release (puzzle))
    PUZZLE_PRINT (puzzle, _("Heuristic database released.\n"));
  puzzle->heuristic_database = database;
  puzzle->heuristic_database->nbUsers = 1;
  PUZZLE_PRINT (puzzle, _("Heuristic database attached.\n"));
  sliding_puzzle_write_end (puzzle);

  pthread_cleanup_pop (0);      // database_cleanup
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
}

// Thread cancellable, thread safe
int
sliding_puzzle_heuristic_database_share (Puzzle orig, Puzzle dest)
{
  if (!orig || !dest)
    return 0;

  if (orig == dest)
    return 1;

  // Check for compliant puzzles (same size and target)
  if (orig->width != dest->width || orig->height != dest->height)
    return 0;
  if (memcmp (dest->grid_sol, orig->grid_sol, orig->width * orig->height * sizeof (*orig->grid_sol)))
    return 0;

  int ret = 0;

  pthread_cleanup_push (sliding_puzzle_cancellation_msg, dest);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_read_begin (orig);
  pthread_cleanup_push (sliding_puzzle_solve_read_clean, orig);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_write_begin (dest);

  if (orig->heuristic_database != dest->heuristic_database)
  {
    if (sliding_puzzle_heuristic_database_release (dest))
      PUZZLE_PRINT (dest, _("Heuristic database released.\n"));

    dest->heuristic_database = orig->heuristic_database;
    if (dest->heuristic_database)
    {
      dest->heuristic_database->nbUsers++;
      ret = 1;
      PUZZLE_PRINT (dest, _("Heuristic database shared with puzzle [%p].\n"), (void *) orig);
    }
  }
  sliding_puzzle_write_end (dest);
  pthread_cleanup_pop (1);      // sliding_puzzle_read_end (orig);
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg

  return ret;
}

/** Heuristic database creation for puzzle - END **/

/** Helpers - END **/

/** Puzzle solvers - BEGIN **/
// Thread cancellable, thread safe
int
sliding_puzzle_solve_RBFS (Puzzle puzzle)
{
  int depth = -1;
  NodeRBFS root;

  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_write_begin (puzzle);
  pthread_cleanup_push (sliding_puzzle_solve_write_clean, puzzle);

  puzzle->solved = 0;
  puzzle->solution_length = 0;
  free (puzzle->solution);
  puzzle->solution = 0;

  PUZZLE_PRINT (puzzle, _("Solve puzzle...\n"));
  PUZZLE_PRINT (puzzle, _("  Using RBFS...\n"));
  if (puzzle->heuristic_database)
    PUZZLE_PRINT (puzzle, _("  Using heuristic database.\n"));
  if (puzzle->cycle_database)
  {
    PUZZLE_PRINT (puzzle, _("  Using cycle detection.\n"));
    puzzle->cycle_state = ACM_reset (puzzle->cycle_database->cycles);
  }

  // Initial distance to solutions
  depth = sliding_puzzle_initialize_distances_to_solutions (puzzle);

  // Buffering to avoid allocations during recursion
  struct BufferRBFS *buffer = 0;
  int bufferLength = 0;

  struct BuffersRBFS b;
  b.pBuffer = &buffer;
  b.pBufferLength = &bufferLength;
  pthread_cleanup_push (sliding_puzzle_buffer_RBFS_cleanup, &b);

  PUZZLE_PRINT (puzzle, _("Depth: "));
  root.node = *puzzle;
  root.F = depth;
  root.move = 0;
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);

  // Call to BFRS
  depth =
    sliding_puzzle_best_first_recursive_search (&root, /* depth = */ 0, /* V = */ depth, /* max_depth = */ INT_MAX,
                                                &buffer, &bufferLength);

  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg
  PUZZLE_PRINT (puzzle, "\n");

  if (root.node.solved > 0)
  {
    if (bufferLength && depth)
    {
      PUZZLE_PRINT (puzzle, _("Solved:\n Depth: %i\n Path:\n"), depth);
      int *grid = malloc (puzzle->width * puzzle->height * sizeof (*grid));
      int *pos = malloc (puzzle->width * puzzle->height * sizeof (*pos));
      memcpy (grid, puzzle->grid, puzzle->width * puzzle->height * sizeof (*grid));
      memcpy (pos, puzzle->pos, puzzle->width * puzzle->height * sizeof (*pos));

      puzzle->solution_length = depth;
      puzzle->solution = malloc (depth * sizeof (*puzzle->solution));

      for (int i = 0; i < depth; i++)
      {
        int tile, move;
        if (puzzle->parity % 2 == 0)
        {
          tile = grid[pos[0] + buffer[i].move];
          move =
            buffer[i].move == -puzzle->width ? 'd' : buffer[i].move == puzzle->width ? 'u' :
            buffer[i].move == -1 ? 'r' : 'l';
        }
        else
        {
          tile = puzzle->width * puzzle->height - grid[pos[0] + buffer[i].move];
          move =
            buffer[i].move == -puzzle->width ? 'u' : buffer[i].move == puzzle->width ? 'd' :
            buffer[i].move == -1 ? 'l' : 'r';
        }
        if (puzzle->solution_shower)
          puzzle->solution_shower (puzzle, i + 1, tile, move);
        else
          PUZZLE_PRINT (puzzle, " %2i(%c)", tile, move);
        grid[pos[0]] = grid[pos[0] + buffer[i].move];
        grid[pos[0] + buffer[i].move] = 0;
        pos[0] += buffer[i].move;

        puzzle->solution[i] = tile;
      }

      free (pos);
      free (grid);

      // Display statistical data
      uintmax_t nbGeneratedNodes = 0;
      PUZZLE_PRINT (puzzle, _("\n Generated nodes:"));
      for (int i = 0; i < bufferLength; i++)
      {
        PUZZLE_PRINT (puzzle, " %i:%" PRIuMAX, i + 1, buffer[i].nbGeneratedNodes);
        nbGeneratedNodes += buffer[i].nbGeneratedNodes;
      }
      PUZZLE_PRINT (puzzle, _(" TOTAL:%" PRIuMAX "\n"), nbGeneratedNodes);
    }
    if (puzzle->solution_shower)
      puzzle->solution_shower (puzzle, depth + 1, 0, 0);
  }

  pthread_cleanup_pop (1);      // sliding_puzzle_solve_RBFS_cleanup
  pthread_cleanup_pop (1);      // sliding_puzzle_solve_wr_clean
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg

  if (root.node.solved > 0)
    return depth;
  else                          // should not happen
    return -1;
}

// Thread cancellable, thread safe
int
sliding_puzzle_solve_IDA (Puzzle puzzle)
{
  int prev_depth = 0;

  pthread_cleanup_push (sliding_puzzle_cancellation_msg, puzzle);
#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif
  // Cancellation point
  sliding_puzzle_write_begin (puzzle);
  pthread_cleanup_push (sliding_puzzle_solve_write_clean, puzzle);

  puzzle->solved = 0;
  puzzle->solution_length = 0;
  free (puzzle->solution);
  puzzle->solution = 0;

  PUZZLE_PRINT (puzzle, _("Solve puzzle...\n"));
  PUZZLE_PRINT (puzzle, _("  Using IDA*...\n"));
  if (puzzle->heuristic_database)
    PUZZLE_PRINT (puzzle, _("  Using heuristic database.\n"));
  if (puzzle->cycle_database)
  {
    PUZZLE_PRINT (puzzle, _("  Using cycle detection.\n"));
    puzzle->cycle_state = ACM_reset (puzzle->cycle_database->cycles);
  }

  int next_depth = sliding_puzzle_initialize_distances_to_solutions (puzzle);

  // Buffering to avoid allocations during recursion
  struct BufferIDA *buffer = 0;

  struct BuffersIDA b;
  b.pBuffer = &buffer;
  b.pBufferLength = &prev_depth;
  pthread_cleanup_push (sliding_puzzle_buffer_IDA_cleanup, &b);

  PUZZLE_PRINT (puzzle, _("Depth: "));
  // Try to solve the puzzle with a solution not longer than 'next_depth'.
  // Increase next_depth as long as the puzzle is not solved.
  while (!puzzle->solved && next_depth < INT_MAX)
  {
    PUZZLE_PRINT (puzzle, "%i.", next_depth);

#ifdef TEST_CANCELLATION_POINT
    if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
        && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
      pthread_cancel (*thread_cancellation_point_test);
#endif
    // Cancellation point
    //pthread_testcancel ();

    buffer = realloc (buffer, next_depth * sizeof (*buffer));

    for (int i = prev_depth; i < next_depth; i++)
    {
      buffer[i].grid = malloc (puzzle->height * puzzle->width * sizeof (*buffer[i].grid));      // needs cancel cleanup
      buffer[i].pos = malloc (puzzle->height * puzzle->width * sizeof (*buffer[i].pos));        // needs cancel cleanup
      buffer[i].nbGeneratedNodes = 0;
    }

    if (puzzle->cycle_database)
      puzzle->cycle_state = ACM_reset (puzzle->cycle_database->cycles);
    prev_depth = next_depth;
    // Call to DFRS
    // Returns the smallest minimal distance to solution for all possible moves of tile.
    // This distance will be higher than prev_depth if not solutions were found.
    next_depth = sliding_puzzle_depth_first_recursive_search (puzzle, prev_depth, 0, buffer);

    // Cancellation point
    pthread_testcancel ();
  }                             // end while
  PUZZLE_PRINT (puzzle, "\n");

  if (puzzle->solved > 0)
  {
    if (prev_depth)
    {
      PUZZLE_PRINT (puzzle, _("Solved:\n Depth: %i\n Path:\n"), prev_depth);

      int *grid = malloc (puzzle->width * puzzle->height * sizeof (*grid));
      int *pos = malloc (puzzle->width * puzzle->height * sizeof (*pos));
      memcpy (grid, puzzle->grid, puzzle->width * puzzle->height * sizeof (*grid));
      memcpy (pos, puzzle->pos, puzzle->width * puzzle->height * sizeof (*pos));

      puzzle->solution_length = prev_depth;
      puzzle->solution = malloc (prev_depth * sizeof (*puzzle->solution));

      for (int i = 0; i < prev_depth; i++)
      {
        int tile, move;
        if (puzzle->parity % 2 == 0)
        {
          tile = grid[pos[0] + buffer[i].move];
          move =
            buffer[i].move == -puzzle->width ? 'd' : buffer[i].move == puzzle->width ? 'u' :
            buffer[i].move == -1 ? 'r' : 'l';
        }
        else
        {
          tile = puzzle->width * puzzle->height - grid[pos[0] + buffer[i].move];
          move =
            buffer[i].move == -puzzle->width ? 'u' : buffer[i].move == puzzle->width ? 'd' :
            buffer[i].move == -1 ? 'l' : 'r';
        }
        if (puzzle->solution_shower)
          puzzle->solution_shower (puzzle, i + 1, tile, move);
        else
          PUZZLE_PRINT (puzzle, " %2i(%c)", tile, move);
        grid[pos[0]] = grid[pos[0] + buffer[i].move];
        grid[pos[0] + buffer[i].move] = 0;
        pos[0] += buffer[i].move;

        puzzle->solution[i] = tile;
      }

      free (pos);
      free (grid);

      // Display some statistical data
      PUZZLE_PRINT (puzzle, _("\n Generated nodes:"));
      uintmax_t nbGeneratedNodes = 0;
      for (int i = 0; i < prev_depth; i++)
      {
        PUZZLE_PRINT (puzzle, " %i:%" PRIuMAX, i + 1, buffer[i].nbGeneratedNodes);
        nbGeneratedNodes += buffer[i].nbGeneratedNodes;
      }
      PUZZLE_PRINT (puzzle, _(" TOTAL:%" PRIuMAX "\n"), nbGeneratedNodes);
    }
    if (puzzle->solution_shower)
      puzzle->solution_shower (puzzle, prev_depth + 1, 0, 0);
  }
  else                          // should not happen
    prev_depth = -1;

  pthread_cleanup_pop (1);      // sliding_puzzle_solve_IDA_cleanup
  pthread_cleanup_pop (1);      // sliding_puzzle_solve_wr_clean
  pthread_cleanup_pop (0);      // sliding_puzzle_cancellation_msg

  return prev_depth;
}

/** Puzzle solvers - END **/

/** User interface - END **/
