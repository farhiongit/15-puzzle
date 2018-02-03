#pragma once
#ifndef LIB15PUZZLE_H
#  define LIB15PUZZLE_H

#  include <libintl.h>

#  define TBT(String) (String)  // To be translated
#  ifdef __USE_GNU_GETTEXT
#    define PACKAGE "lib15puzzle"
#    define _(String) dgettext (PACKAGE, String)
#    define dgettext_noop(String) String
#    define N_(String) dgettext_noop (String)

#  else

#    define _(String) (String)
#    define N_(String) String
#    define textdomain(Domain)
#    define bindtextdomain(Package, Directory)

#  endif

#  define CHECK_ALLOC(ptr) \
  ASSERT(ptr, "Memory allocation error")

#  define ASSERT(cond,msg) \
  do {\
    if (!(cond)) \
    {\
      const char* __str__ = (msg) ;\
      if (!__str__ || !*__str__)\
        __str__ = _("Fatal error") ;\
      fprintf(stderr, _("%1$s (condition '%2$s' is false in function %3$s at %4$s:%5$i)\n"),__str__,#cond,__func__,__FILE__,__LINE__);\
      pthread_exit(0) ;\
    }\
  } while(0)

#  define ASSERT_FALSE(cond,msg) \
  do {\
    int val; \
    if ((val = cond)) \
    { \
      fprintf(stderr, _("condition '%1$s' is true (%2$i)\n"),#cond,val);\
      ASSERT(0,msg);\
    } \
  } while(0)

#  define private static

#endif
