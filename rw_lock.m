#pragma once
#ifndef RW_LOCK_MODULE
#define RW_LOCK_MODULE

/*
SYNOPSIS
#include <pthread.h>
#include <errno.h>
#include "rw_lock.m"
*/
/*****************************************************************
* MULTI-THREAD ACCESS CONTROL IMPLEMENTATION                     *
*****************************************************************/
/** SINGLE-WRITE MULTIPLE-READ LOCK      **/
typedef struct sSWMRLock
{
  pthread_mutex_t read;
  pthread_mutex_t write;
  pthread_cond_t reading;
  int nb_readers;
} rw_access_control_t;

private void
_mutex_cancel_cleanup (void *arg)
{
  rw_access_control_t *pac = arg;
  pthread_mutex_unlock (&pac->write);
  pthread_mutex_unlock (&pac->read);
}

private rw_access_control_t *
rw_ac_create ()
{
  rw_access_control_t *pac = malloc (sizeof (*pac));
  if (!pac)
    return 0;
  if (pthread_mutex_init (&pac->write, 0))
    return 0;
  if (pthread_mutex_init (&pac->read, 0))
  {
    pthread_mutex_destroy (&pac->write);
    return 0;
  }
  if (pthread_cond_init (&pac->reading, 0))
  {
    pthread_mutex_destroy (&pac->write);
    pthread_mutex_destroy (&pac->read);
    return 0;
  }

  pac->nb_readers = 0;
  return pac;
}

private int
rw_ac_write_begin (rw_access_control_t * pac)
{
  int status;
  if ((status = pthread_mutex_lock (&pac->write)))
    return status;
  if ((status = pthread_mutex_lock (&pac->read)))
  {
    pthread_mutex_unlock (&pac->write);
    return status;
  }

  pthread_cleanup_push (_mutex_cancel_cleanup, pac);

  while (pac->nb_readers)
  {
#ifdef TEST_CANCELLATION_POINT
    if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
        && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
      pthread_cancel (*thread_cancellation_point_test);
#endif

    // Cancellation point
    if ((status = pthread_cond_wait (&pac->reading, &pac->read)))
      return status;
  }

  pthread_cleanup_pop (0);      // _mutex_cancel_cleanup

  if ((status = pthread_mutex_unlock (&pac->read)))
    return status;

  return 0;
}

private int
rw_ac_write_try_begin (rw_access_control_t * pac)
{
  int status;
  if ((status = pthread_mutex_trylock (&pac->write)))
    return status;
  if ((status = pthread_mutex_lock (&pac->read)))
  {
    pthread_mutex_unlock (&pac->write);
    return status;
  }

  if (pac->nb_readers)
  {
    if ((status = pthread_mutex_unlock (&pac->read)))
    {
      pthread_mutex_unlock (&pac->write);
      return status;
    }
    else if ((status = pthread_mutex_unlock (&pac->write)))
      return status;
    else
      return EBUSY;
  }
  else if ((status = pthread_mutex_unlock (&pac->read)))
    return status;

  return 0;
}

private int
rw_ac_write_end (rw_access_control_t * pac)
{
  return pthread_mutex_unlock (&pac->write);
}

private int
rw_ac_read_begin (rw_access_control_t * pac)
{
  int status;
  if ((status = pthread_mutex_lock (&pac->write)))
    return status;
  if ((status = pthread_mutex_lock (&pac->read)))
  {
    pthread_mutex_unlock (&pac->write);
    return status;
  }

  pthread_cleanup_push (_mutex_cancel_cleanup, pac);

#ifdef TEST_CANCELLATION_POINT
  if (!strcmp (TEST_CANCELLATION_POINT, __func__) && thread_cancellation_point_test
      && pthread_equal (*thread_cancellation_point_test, pthread_self ()))
    pthread_cancel (*thread_cancellation_point_test);
#endif

  // Cancellation point
  pthread_testcancel ();

  pthread_cleanup_pop (0);      // _mutex_cancel_cleanup

  pac->nb_readers++;

  if ((status = pthread_mutex_unlock (&pac->read)))
  {
    pthread_mutex_unlock (&pac->write);
    return status;
  }
  else
    return pthread_mutex_unlock (&pac->write);
}

private int
rw_ac_read_try_begin (rw_access_control_t * pac)
{
  int status;
  if ((status = pthread_mutex_trylock (&pac->write)))
    return status;
  if ((status = pthread_mutex_lock (&pac->read)))
  {
    pthread_mutex_unlock (&pac->write);
    return status;
  }

  pac->nb_readers++;

  if ((status = pthread_mutex_unlock (&pac->read)))
    return status;
  if ((status = pthread_mutex_unlock (&pac->write)))
    return status;
  return 0;
}

private int
rw_ac_read_end (rw_access_control_t * pac)
{
  int status;
  if (!pac->nb_readers)
    return 0;
  if ((status = pthread_mutex_lock (&pac->read)))
    return status;

  pac->nb_readers--;
  if (!pac->nb_readers)
    if ((status = pthread_cond_signal (&pac->reading)))
      return status;

  if ((status = pthread_mutex_unlock (&pac->read)))
    return status;

  return 0;
}

private int
rw_ac_destroy (rw_access_control_t * pac)
{
  if (!pac)
    return 0;

  int status;
  if ((status = rw_ac_write_try_begin (pac)))
    return status;
  else if ((status = rw_ac_write_end (pac)))
    return status;
  if ((status = pthread_cond_destroy (&pac->reading)))
    return status;
  if ((status = pthread_mutex_destroy (&pac->read)))
    return status;
  if ((status = pthread_mutex_destroy (&pac->write)))
    return status;
  free (pac);

  return 0;
}

#endif
