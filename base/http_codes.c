#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "basetypes.h"
#include "http_codes.h"

struct http_response kHTTPResponseArray[] = {
  { 
    "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-length %I64u\r\n",
    200,
    (size_t) -1 /* not valid for this one, call strlen on the resulting string */
  },
  {
    NULL, /* not implemented */
    201,
    0
  },
  {
    NULL, /* not implemented */
    202,
    0
  },
  {
    NULL, /* not implemented */
    204,
    0
  },
  {
    NULL, /* not implemented */
    301,
    0
  },
  {
    NULL, /* not implemented */
    302,
    0
  },
  {
    NULL, /* not implemented */
    304,
    0
  },
  {
    "HTTP/1.1 400 Bad request\r\nConnection: close\r\n"
     "Content-Length: 0\r\n\r\n",
    400,
    0
  },
  {
    NULL, /* not implemented */
    401,
    0
  },
  {
    NULL, /* not implemented */
    403,
    0
  },
  {
    "HTTP/1.1 404 Not found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
    404,
    0
  },
  {
    "HTTP/1.1 500 Internal server error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
    500,
    0
  },
  {
    NULL, /* not implemented */
    501,
    0
  },
  {
    NULL, /* not implemented */
    502,
    0
  },
  {
    NULL, /* not implemented */
    503,
    0
  }
};

void setup_http_response_array(void) {
  size_t i;
  for (i = 0; i < _countof(kHTTPResponseArray); ++i) {
    if (kHTTPResponseArray[i].http_string_spec) {
      kHTTPResponseArray[i].string_spec_len = 
        strlen(kHTTPResponseArray[i].http_string_spec);
    }
  }
}
