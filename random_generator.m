/// @file
/// This private module implements a uniform random generator of integers below an arbitrary limit othe than RAND_MAX.
#pragma once
#ifndef RANDOM_GENERATOR_MODULE
#define RANDOM_GENERATOR_MODULE

/*
SYNOPSIS
#define _XOPEN_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <limits.h>
#include "random_generator.m"
*/

/*****************************************************************
* RANDOM NUMBER GENERATOR                                        *
*****************************************************************/
static pthread_once_t random_init_once = PTHREAD_ONCE_INIT;
static unsigned int rand_seed = 0;

/// This function initializes the generator from the clock of the computer.
static void
random_init ()
{
#if TRACE
  fprintf (stderr, _("RANDOM GENERATOR INITIALIZATION"));
  fprintf (stderr, "\n");
#endif
  rand_seed = (unsigned int) time (NULL) + (unsigned int) (intptr_t) (&rand_seed);      //Cast from int* to unsigned int
}

/// This function draws a random integer uniformaly between 0 and n included.
///
/// @param [in] n Upper included limit.
/// @returns A random integer drawn uniformaly between 0 and n included.
static int
alea (int n)
{
  // Reproductible test.
#ifdef RANDOMIZE_SEED
  pthread_once (&random_init_once, random_init);
#endif

  // Draw a random integer number between 0 and n included ([0, n]).
  if (n == 0)
    return 0;
  else if (n < 0 || RAND_MAX < n || n == INT_MAX)
  {
    ASSERT (0 <= n && n <= RAND_MAX, 0);
    abort ();
  }

  // Here, n != 0 and n != INT_MAX and n <= RAND_MAX
  // The idea here is not to overflow integer range or random generator range
  // (avoiding the use of RAND_MAX + 1.)
  int blockSize = n == RAND_MAX ? 1 : (1 + (RAND_MAX - n) / (n + 1));      // blockSize = (RAND_MAX + 1)/(n + 1)
  int upperLimit = blockSize * n + (blockSize - 1);       // upperLimit = (n + 1) * blockSize - 1
  int draw;
  while ((draw = rand_r (&rand_seed)) > upperLimit)
    /* nothing here */ ;
  return draw / blockSize;
}

#endif
