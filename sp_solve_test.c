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
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "sp_solve.h"

static void
solution_shower (Puzzle puzzle, int move, int tile, int direction)
{
  static Puzzle p = 0;

  if (puzzle != p)
  {
    printf ("[%p]\n", (void *) puzzle);
    p = puzzle;
  }
  printf (" %2i: %2i(%c)\n", move, tile, direction ? direction : '0');
}

int
sliding_puzzle_TU ()
{
  // Test
  printf ("%s\n", sliding_puzzle_solver_version ());

  struct UnitTest
  {
    char name[20];
    int grid[16];
    int estimate;
    int actual;
    long long int totalNodes;

    double cpuSeconds[2];
  };

  struct UnitTest Korf[] = {
    {{"Solution 1"}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0}, 0, 0, 0},
    {{"Solution 2"}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}, 0, 0, 0},
    {{"Alt1"}, {13, 14, 15, 7, 11, 12, 9, 5, 6, 0, 2, 1, 4, 8, 10, 3}, -1, 63, -1},
    {{"Korf1"}, {14, 13, 15, 7, 11, 12, 9, 5, 6, 0, 2, 1, 4, 8, 10, 3}, 41, 57, 276361933},
    {{"Korf2"}, {13, 5, 4, 10, 9, 12, 8, 14, 2, 3, 7, 1, 0, 15, 11, 6}, 43, 55, 15300442},
    {{"Korf3"}, {14, 7, 8, 2, 13, 11, 10, 4, 9, 12, 5, 0, 3, 6, 1, 15}, 41, 59, 565994203},
    {{"Korf4"}, {5, 12, 10, 7, 15, 11, 14, 0, 8, 2, 1, 13, 3, 4, 9, 6}, 42, 56, 62643179},
    {{"Korf5"}, {4, 7, 14, 13, 10, 3, 9, 12, 11, 5, 6, 15, 1, 2, 8, 0}, 42, 56, 11020325},
    {{"Korf6"}, {14, 7, 1, 9, 12, 3, 6, 15, 8, 11, 2, 5, 10, 0, 4, 13}, 36, 52, 32201660},
    {{"Korf7"}, {2, 11, 15, 5, 13, 4, 6, 7, 12, 8, 10, 1, 9, 3, 14, 0}, 30, 52, 387138094},
    {{"Korf8"}, {12, 11, 15, 3, 8, 0, 4, 2, 6, 13, 9, 5, 14, 1, 10, 7}, 32, 50, 39118937},
    {{"Korf9"}, {3, 14, 9, 11, 5, 4, 8, 2, 13, 12, 6, 7, 10, 1, 15, 0}, 32, 46, 1650696},
    {{"Korf10"}, {13, 11, 8, 9, 0, 15, 7, 10, 4, 3, 6, 14, 5, 12, 2, 1}, 43, 59, 198758703},
    {{"Korf11"}, {5, 9, 13, 14, 6, 3, 7, 12, 10, 8, 4, 0, 15, 2, 11, 1}, 43, 57, 150346072},
    {{"Korf12"}, {14, 1, 9, 6, 4, 8, 12, 5, 7, 2, 3, 0, 10, 11, 13, 15}, 35, 45, 546344},
    {{"Korf13"}, {3, 6, 5, 2, 10, 0, 15, 14, 1, 4, 13, 12, 9, 8, 11, 7}, 36, 46, 11861705},
    {{"Korf14"}, {7, 6, 8, 1, 11, 5, 14, 10, 3, 4, 9, 13, 15, 2, 0, 12}, 41, 59, 1369596778},
    {{"Korf15"}, {13, 11, 4, 12, 1, 8, 9, 15, 6, 5, 14, 2, 7, 3, 10, 0}, 44, 62, 543598067},
    {{"Korf16"}, {1, 3, 2, 5, 10, 9, 15, 6, 8, 14, 13, 11, 12, 4, 7, 0}, 24, 42, 17984051},
    {{"Korf17"}, {15, 14, 0, 4, 11, 1, 6, 13, 7, 5, 8, 9, 3, 2, 10, 12}, 46, 66, 607399560},
    {{"Korf18"}, {6, 0, 14, 12, 1, 15, 9, 10, 11, 4, 7, 2, 8, 3, 5, 13}, 43, 55, 23711067},
    {{"Korf19"}, {7, 11, 8, 3, 14, 0, 6, 15, 1, 4, 13, 9, 5, 12, 2, 10}, 36, 46, 1280495},
    {{"Korf20"}, {6, 12, 11, 3, 13, 7, 9, 15, 2, 14, 8, 10, 4, 1, 5, 0}, 36, 52, 17954870},
    {{"Korf21"}, {12, 8, 14, 6, 11, 4, 7, 0, 5, 1, 10, 15, 3, 13, 9, 2}, 34, 54, 257064810},
    {{"Korf22"}, {14, 3, 9, 1, 15, 8, 4, 5, 11, 7, 10, 13, 0, 2, 12, 6}, 41, 59, 750746755},
    {{"Korf23"}, {10, 9, 3, 11, 0, 13, 2, 14, 5, 6, 4, 7, 8, 15, 1, 12}, 33, 49, 15971319},
    {{"Korf24"}, {7, 3, 14, 13, 4, 1, 10, 8, 5, 12, 9, 11, 2, 15, 6, 0}, 34, 54, 42693209},
    {{"Korf25"}, {11, 4, 2, 7, 1, 0, 10, 15, 6, 9, 14, 8, 3, 13, 5, 12}, 32, 52, 100734844},
    {{"Korf26"}, {5, 7, 3, 12, 15, 13, 14, 8, 0, 10, 9, 6, 1, 4, 2, 11}, 40, 58, 226668645},
    {{"Korf27"}, {14, 1, 8, 15, 2, 6, 0, 3, 9, 12, 10, 13, 4, 7, 5, 11}, 33, 53, 306123421},
    {{"Korf28"}, {13, 14, 6, 12, 4, 5, 1, 0, 9, 3, 10, 2, 15, 11, 8, 7}, 36, 52, 5934442},
    {{"Korf29"}, {9, 8, 0, 2, 15, 1, 4, 14, 3, 10, 7, 5, 11, 13, 6, 12}, 38, 54, 117076111},
    {{"Korf30"}, {12, 15, 2, 6, 1, 14, 4, 8, 5, 3, 7, 0, 10, 13, 9, 11}, 35, 47, 2196593},
    {{"Korf31"}, {12, 8, 15, 13, 1, 0, 5, 4, 6, 3, 2, 11, 9, 7, 14, 10}, 38, 50, 2351811},
    {{"Korf32"}, {14, 10, 9, 4, 13, 6, 5, 8, 2, 12, 7, 0, 1, 3, 11, 15}, 43, 59, 661041936},
    {{"Korf33"}, {14, 3, 5, 15, 11, 6, 13, 9, 0, 10, 2, 12, 4, 1, 7, 8}, 42, 60, 480637867},
    {{"Korf34"}, {6, 11, 7, 8, 13, 2, 5, 4, 1, 10, 3, 9, 14, 0, 12, 15}, 36, 52, 20671552},
    {{"Korf35"}, {1, 6, 12, 14, 3, 2, 15, 8, 4, 5, 13, 9, 0, 7, 11, 10}, 39, 55, 47506056},
    {{"Korf36"}, {12, 6, 0, 4, 7, 3, 15, 1, 13, 9, 8, 11, 2, 14, 5, 10}, 36, 52, 59802602},
    {{"Korf37"}, {8, 1, 7, 12, 11, 0, 10, 5, 9, 15, 6, 13, 14, 2, 3, 4}, 40, 58, 280078791},
    {{"Korf38"}, {7, 15, 8, 2, 13, 6, 3, 12, 11, 0, 4, 10, 9, 5, 1, 14}, 41, 53, 24492852},
    {{"Korf39"}, {9, 0, 4, 10, 1, 14, 15, 3, 12, 6, 5, 7, 11, 13, 8, 2}, 35, 49, 19355806},
    {{"Korf40"}, {11, 5, 1, 14, 4, 12, 10, 0, 2, 7, 13, 3, 9, 15, 6, 8}, 36, 54, 63276188},
    {{"Korf41"}, {8, 13, 10, 9, 11, 3, 15, 6, 0, 1, 2, 14, 12, 5, 4, 7}, 36, 54, 51501544},
    {{"Korf42"}, {4, 5, 7, 2, 9, 14, 12, 13, 0, 3, 6, 11, 8, 1, 15, 10}, 30, 42, 877823},
    {{"Korf43"}, {11, 15, 14, 13, 1, 9, 10, 4, 3, 6, 2, 12, 7, 5, 8, 0}, 48, 64, 41124767},
    {{"Korf44"}, {12, 9, 0, 6, 8, 3, 5, 14, 2, 4, 11, 7, 10, 1, 15, 13}, 32, 50, 95733125},
    {{"Korf45"}, {3, 14, 9, 7, 12, 15, 0, 4, 1, 8, 5, 6, 11, 10, 2, 13}, 39, 51, 6158733},
    {{"Korf46"}, {8, 4, 6, 1, 14, 12, 2, 15, 13, 10, 9, 5, 3, 7, 0, 11}, 35, 49, 22119320},
    {{"Korf47"}, {6, 10, 1, 14, 15, 8, 3, 5, 13, 0, 2, 7, 4, 9, 11, 12}, 35, 47, 1411294},
    {{"Korf48"}, {8, 11, 4, 6, 7, 3, 10, 9, 2, 12, 15, 13, 0, 1, 5, 14}, 39, 49, 1905023},
    {{"Korf49"}, {10, 0, 2, 4, 5, 1, 6, 12, 11, 13, 9, 7, 15, 3, 14, 8}, 33, 59, 1809933698},
    {{"Korf50"}, {12, 5, 13, 11, 2, 10, 0, 9, 7, 8, 4, 3, 14, 6, 15, 1}, 39, 53, 63036422},
    {{"Korf51"}, {10, 2, 8, 4, 15, 0, 1, 14, 11, 13, 3, 6, 9, 7, 5, 12}, 44, 56, 26622863},
    {{"Korf52"}, {10, 8, 0, 12, 3, 7, 6, 2, 1, 14, 4, 11, 15, 13, 9, 5}, 38, 56, 377141881},
    {{"Korf53"}, {14, 9, 12, 13, 15, 4, 8, 10, 0, 2, 1, 7, 3, 11, 5, 6}, 50, 64, 465225698},
    {{"Korf54"}, {12, 11, 0, 8, 10, 2, 13, 15, 5, 4, 7, 3, 6, 9, 14, 1}, 40, 56, 220374385},
    {{"Korf55"}, {13, 8, 14, 3, 9, 1, 0, 7, 15, 5, 4, 10, 12, 2, 6, 11}, 29, 41, 927212},
    {{"Korf56"}, {3, 15, 2, 5, 11, 6, 4, 7, 12, 9, 1, 0, 13, 14, 10, 8}, 29, 55, 1199487996},
    {{"Korf57"}, {5, 11, 6, 9, 4, 13, 12, 0, 8, 2, 15, 10, 1, 7, 3, 14}, 36, 50, 8841527},
    {{"Korf58"}, {5, 0, 15, 8, 4, 6, 1, 14, 10, 11, 3, 9, 7, 12, 2, 13}, 37, 51, 12955404},
    {{"Korf59"}, {15, 14, 6, 7, 10, 1, 0, 11, 12, 8, 4, 9, 2, 5, 13, 3}, 35, 57, 1207520464},
    {{"Korf60"}, {11, 14, 13, 1, 2, 3, 12, 4, 15, 7, 9, 5, 10, 6, 8, 0}, 48, 66, 3337690331},
    {{"Korf61"}, {6, 13, 3, 2, 11, 9, 5, 10, 1, 7, 12, 14, 8, 4, 0, 15}, 31, 45, 7096850},
    {{"Korf62"}, {4, 6, 12, 0, 14, 2, 9, 13, 11, 8, 3, 15, 7, 10, 1, 5}, 43, 57, 23540413},
    {{"Korf63"}, {8, 10, 9, 11, 14, 1, 7, 15, 13, 4, 0, 12, 6, 2, 5, 3}, 40, 56, 995472712},
    {{"Korf64"}, {5, 2, 14, 0, 7, 8, 6, 3, 11, 12, 13, 15, 4, 10, 9, 1}, 31, 51, 260054152},
    {{"Korf65"}, {7, 8, 3, 2, 10, 12, 4, 6, 11, 13, 5, 15, 0, 1, 9, 14}, 31, 47, 18997681},
    {{"Korf66"}, {11, 6, 14, 12, 3, 5, 1, 15, 8, 0, 10, 13, 9, 7, 4, 2}, 41, 61, 1957191378},
    {{"Korf67"}, {7, 1, 2, 4, 8, 3, 6, 11, 10, 15, 0, 5, 14, 12, 13, 9}, 28, 50, 252783878},
    {{"Korf68"}, {7, 3, 1, 13, 12, 10, 5, 2, 8, 0, 6, 11, 14, 15, 4, 9}, 31, 51, 64367799},
    {{"Korf69"}, {6, 0, 5, 15, 1, 14, 4, 9, 2, 13, 8, 10, 11, 12, 7, 3}, 37, 53, 109562359},
    {{"Korf70"}, {15, 1, 3, 12, 4, 0, 6, 5, 2, 8, 14, 9, 13, 10, 7, 11}, 30, 52, 151042571},
    {{"Korf71"}, {5, 7, 0, 11, 12, 1, 9, 10, 15, 6, 2, 3, 8, 4, 13, 14}, 30, 44, 8885972},
    {{"Korf72"}, {12, 15, 11, 10, 4, 5, 14, 0, 13, 7, 1, 2, 9, 8, 3, 6}, 38, 56, 1031641140},
    {{"Korf73"}, {6, 14, 10, 5, 15, 8, 7, 1, 3, 4, 2, 0, 12, 9, 11, 13}, 37, 49, 3222276},
    {{"Korf74"}, {14, 13, 4, 11, 15, 8, 6, 9, 0, 7, 3, 1, 2, 10, 12, 5}, 46, 56, 1897728},
    {{"Korf75"}, {14, 4, 0, 10, 6, 5, 1, 3, 9, 2, 13, 15, 12, 7, 8, 11}, 30, 48, 42772589},
    {{"Korf76"}, {15, 10, 8, 3, 0, 6, 9, 5, 1, 14, 13, 11, 7, 2, 12, 4}, 41, 57, 126638417},
    {{"Korf77"}, {0, 13, 2, 4, 12, 14, 6, 9, 15, 1, 10, 3, 11, 5, 8, 7}, 34, 54, 18918269},
    {{"Korf78"}, {3, 14, 13, 6, 4, 15, 8, 9, 5, 12, 10, 0, 2, 7, 1, 11}, 41, 53, 10907150},
    {{"Korf79"}, {0, 1, 9, 7, 11, 13, 5, 3, 14, 12, 4, 2, 8, 6, 10, 15}, 28, 42, 540860},
    {{"Korf80"}, {11, 0, 15, 8, 13, 12, 3, 5, 10, 1, 4, 6, 14, 9, 7, 2}, 43, 57, 132945856},
    {{"Korf81"}, {13, 0, 9, 12, 11, 6, 3, 5, 15, 8, 1, 10, 4, 14, 2, 7}, 39, 53, 9982569},
    {{"Korf82"}, {14, 10, 2, 1, 13, 9, 8, 11, 7, 3, 6, 12, 15, 5, 4, 0}, 40, 62, 5506801123},
    {{"KorfB3"}, {12, 3, 9, 1, 4, 5, 10, 2, 6, 11, 15, 0, 14, 7, 13, 8}, 31, 49, 65533432},
    {{"Korf84"}, {15, 8, 10, 7, 0, 12, 14, 1, 5, 9, 6, 3, 13, 11, 4, 2}, 37, 55, 106074303},
    {{"Korf85"}, {4, 7, 13, 10, 1, 2, 9, 6, 12, 8, 14, 5, 3, 0, 11, 15}, 32, 44, 2725456},
    {{"Korf86"}, {6, 0, 5, 10, 11, 12, 9, 2, 1, 7, 4, 3, 14, 8, 13, 15}, 35, 45, 2304426},
    {{"Korf87"}, {9, 5, 11, 10, 13, 0, 2, 1, 8, 6, 14, 12, 4, 7, 3, 15}, 34, 52, 64926494},
    {{"Korf88"}, {15, 2, 12, 11, 14, 13, 9, 5, 1, 3, 8, 7, 0, 10, 6, 4}, 43, 65, 6009130748},
    {{"Korf89"}, {11, 1, 7, 4, 10, 13, 3, 8, 9, 14, 0, 15, 6, 5, 2, 12}, 36, 54, 166571097},
    {{"Korf90"}, {5, 4, 7, 1, 11, 12, 14, 15, 10, 13, 8, 6, 2, 0, 9, 3}, 36, 50, 7171137},
    {{"Korf91"}, {9, 7, 5, 2, 14, 15, 12, 10, 11, 3, 6, 1, 8, 13, 0, 4}, 41, 57, 602886858},
    {{"Korf92"}, {3, 2, 7, 9, 0, 15, 12, 4, 6, 11, 5, 14, 8, 13, 10, 1}, 37, 57, 1101072541},
    {{"Korf93"}, {13, 9, 14, 6, 12, 8, 1, 2, 3, 4, 0, 7, 5, 10, 11, 15}, 34, 46, 1599909},
    {{"Korf94"}, {5, 7, 11, 8, 0, 14, 9, 13, 10, 12, 3, 15, 6, 1, 4, 2}, 45, 53, 1337340},
    {{"Korf95"}, {4, 3, 6, 13, 7, 15, 9, 0, 10, 5, 8, 11, 2, 12, 1, 14}, 34, 50, 7115967},
    {{"Korf96"}, {1, 7, 15, 14, 2, 6, 4, 9, 12, 11, 13, 3, 0, 8, 5, 10}, 35, 49, 12808564},
    {{"Korf97"}, {9, 14, 5, 7, 8, 15, 1, 2, 10, 4, 13, 6, 12, 0, 11, 3}, 32, 44, 1002927},
    {{"Korf98"}, {0, 11, 3, 12, 5, 2, 1, 9, 8, 10, 14, 15, 7, 4, 13, 6}, 34, 54, 103526883},
    {{"Korf99"}, {7, 15, 4, 0, 10, 9, 2, 5, 12, 11, 13, 6, 1, 3, 14, 8}, 39, 57, 83477694},
    {{"Korf100"}, {11, 4, 0, 8, 6, 10, 5, 13, 12, 7, 14, 3, 1, 2, 9, 15}, 38, 54, 67880056}
  };

  int nbKorf = sizeof (Korf) / sizeof (Korf[0]);

#if DEBUG
  const int CYCLES_MAX_LENGTH = 12;
  const int PATTERN_MAX_LENGTH = 5;

  nbKorf = 3;
#else
  const int CYCLES_MAX_LENGTH = 28;
  const int PATTERN_MAX_LENGTH = 7 /*20 */ ;
#endif

  size_t sizeKorf = 0;

  while ((sizeKorf + 1) * (sizeKorf + 1) <= sizeof (Korf[0].grid) / sizeof (Korf[0].grid[0]))
    sizeKorf++;

  double preparationTime = 0;
  clock_t t0 = clock ();

  Puzzle puzzleOld, puzzle;

  puzzleOld = 0;

  double subTotalTime[2];

  for (int strategy = 0; strategy < 2; strategy++)
  {
    subTotalTime[strategy] = 0;
    int (*SOLVING_STRATEGY) (Puzzle) = 0;

    switch (strategy)
    {
      case 0:
        SOLVING_STRATEGY = sliding_puzzle_solve_IDA;
        break;
      case 1:
        SOLVING_STRATEGY = sliding_puzzle_solve_RBFS;
        break;
    }

    for (int i = 0; i < nbKorf; i++)
    {
#if DEBUG
      if (2 == i && nbKorf > 100)
        i += 98;
      else if (102 == i && nbKorf > 200)
        i += 98;
#endif
      printf ("*****************************************\n");
      printf (" INITIALIZING PUZZLE '%s' (%i / %i)\n", Korf[i].name, i + 1, nbKorf);
      printf ("*****************************************\n");
      puzzle =
        sliding_puzzle_init (sizeKorf, sizeof (Korf[i].grid) / sizeof (Korf[i].grid[0]) / sizeKorf, Korf[i].grid,
                             stdout);
      sliding_puzzle_move_handler_set (puzzle, solution_shower);
      sliding_puzzle_stream_set (puzzle, stdout);

      if (puzzle)
      {
        if (CYCLES_MAX_LENGTH)
        {
          if (!puzzleOld)
          {
            t0 = clock ();
            sliding_puzzle_cycle_database_attach (puzzle, CYCLES_MAX_LENGTH);
            printf ("Elapsed CPU time is %.2fs.\n", 1. * (clock () - t0) / CLOCKS_PER_SEC);
            preparationTime += 1. * (clock () - t0) / CLOCKS_PER_SEC;
          }
          else if (!sliding_puzzle_cycle_database_share (puzzleOld, puzzle))
            return -1;
        }

        if (PATTERN_MAX_LENGTH)
        {
          if (!puzzleOld)
          {
            t0 = clock ();
            sliding_puzzle_heuristic_database_attach (puzzle, PATTERN_MAX_LENGTH);
            printf ("Elapsed CPU time is %.2fs.\n", 1. * (clock () - t0) / CLOCKS_PER_SEC);
            preparationTime += 1. * (clock () - t0) / CLOCKS_PER_SEC;
          }
          else if (!sliding_puzzle_heuristic_database_share (puzzleOld, puzzle))
            return -1;
        }

        if (!puzzleOld)
          printf ("Total elapsed CPU time for preparation is %.2fs.\n", preparationTime);

        sliding_puzzle_release (puzzleOld);
        puzzleOld = puzzle;

        t0 = clock ();
        printf ("*****************************************\n");
        printf (" SOLVING PUZZLE '%s' (%i / %i)\n", Korf[i].name, i + 1, nbKorf);
        printf ("*****************************************\n");

        if (SOLVING_STRATEGY (puzzle) != Korf[i].actual && Korf[i].actual >= 0)
          return -1;

        printf ("Elapsed CPU time is %.2fs.\n", Korf[i].cpuSeconds[strategy] = 1. * (clock () - t0) / CLOCKS_PER_SEC);
        subTotalTime[strategy] += Korf[i].cpuSeconds[strategy];
      }
    }
    printf ("Sub-total elapsed CPU time for solving is %.2fs.\n", subTotalTime[strategy]);
  }

  printf ("Total elapsed CPU time for solving is %.2fs.\n", subTotalTime[0] + subTotalTime[1]);
  printf ("Total elapsed CPU time is %.2fs.\n", preparationTime + subTotalTime[0] + subTotalTime[1]);

  int nbRandom = 50;

#if DEBUG
  nbRandom = 2;
#endif
  double subTotalTimeRandom = 0;

  for (int i = 0; i < nbRandom; i++)
  {
    printf ("*****************************************\n");
    printf (" INITIALIZING RANDOM PUZZLE (%i / %i)\n", i + 1, nbRandom);
    printf ("*****************************************\n");
    puzzle = sliding_puzzle_init (4, 4, 0, stdout);
    sliding_puzzle_move_handler_set (puzzle, solution_shower);
    sliding_puzzle_stream_set (puzzle, stdout);

    sliding_puzzle_cycle_database_share (puzzleOld, puzzle);
    sliding_puzzle_heuristic_database_share (puzzleOld, puzzle);
    sliding_puzzle_release (puzzleOld);
    puzzleOld = puzzle;

    for (int strategy = 0; strategy < 2; strategy++)
    {
      int (*SOLVING_STRATEGY) (Puzzle) = 0;

      switch (strategy)
      {
        case 0:
          SOLVING_STRATEGY = sliding_puzzle_solve_IDA;
          break;
        case 1:
          SOLVING_STRATEGY = sliding_puzzle_solve_RBFS;
          break;
      }

      printf ("*****************************************\n");
      printf (" SOLVING RANDOM PUZZLE (%i / %i)\n", i + 1, nbRandom);
      printf ("*****************************************\n");
      t0 = clock ();
      SOLVING_STRATEGY (puzzle);
      printf ("Elapsed CPU time is %.2fs.\n", 1. * (clock () - t0) / CLOCKS_PER_SEC);
      subTotalTimeRandom += 1. * (clock () - t0) / CLOCKS_PER_SEC;
    }
  }
  printf ("Sub-total elapsed CPU time for solving is %.2fs.\n", subTotalTimeRandom);

  sliding_puzzle_release (puzzleOld);

  return 0;
}

int
main (void)
{
  sliding_puzzle_TU ();
}
