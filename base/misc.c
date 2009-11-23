#include "misc.h"

size_t string_v_printf(char* dst, size_t dst_len, const char* fmt, ...) {
  size_t new_len;
  va_list args_ptr;
  int outsize = 0;

  va_start(args_ptr, fmt);
  outsize = vsnprintf_s(dst, dst_len, dst_len - 1, fmt, args_ptr);
  va_end(args_ptr);
  new_len = outsize > 0 ? outsize : 0;
  return new_len;
}
  
