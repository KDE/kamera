include(CheckCXXSourceCompiles)

set(VA_COPY_TEST_SOURCE
"
#include <stdarg.h>
#include <stdlib.h>
void f (int i, ...) {
va_list args1, args2;
va_start (args1, i);
va_copy (args2, args1);
if (va_arg (args2, int) != 42 || va_arg (args1, int) != 42)
  exit (1);
va_end (args1); va_end (args2);
}
int main() {
  f (0, 42);
  return 0;
}
")

set(__VA_COPY_TEST_SOURCE
"
#include <stdarg.h>
#include <stdlib.h>
void f (int i, ...) {
va_list args1, args2;
va_start (args1, i);
__va_copy (args2, args1);
if (va_arg (args2, int) != 42 || va_arg (args1, int) != 42)
  exit (1);
va_end (args1); va_end (args2);
}
int main() {
  f (0, 42);
  return 0;
}
")

check_cxx_source_compiles("${VA_COPY_TEST_SOURCE}" HAVE_VA_COPY)
if(HAVE_VA_COPY)
  set(VA_COPY va_copy CACHE STRING "va_copy function")
else(HAVE_VA_COPY)
  check_cxx_source_compiles("${__VA_COPY_TEST_SOURCE}" HAVE___VA_COPY)
  if(HAVE___VA_COPY)
    set(VA_COPY __va_copy CACHE STRING "__va_copy function")
  else(HAVE___VA_COPY)
    message(FATAL_ERROR "Either va_copy or __va_copy must be present. Please check your libc.")
  endif(HAVE___VA_COPY)
endif(HAVE_VA_COPY)
