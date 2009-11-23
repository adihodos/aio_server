#ifndef _HTTP_CODES_H
#define _HTTP_CODES_H

struct http_response {
  const char* http_string_spec;
  size_t      http_code;
  size_t      string_spec_len;
};

extern struct http_response kHTTPResponseArray[];

typedef enum {
  /*!
   * Expects a 64 bit unsigned integer arguments for the content-length field.
   */
  kHTTPCode200Ok,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode201Created,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode202Accepted,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode204NoContent,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode301MovedPerm,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode302MovedTemp,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode304NotModified,
  /*!
   * No arguments needed.
   */
  kHTTPCode400BadRequest,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode401Unauthorized,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode403Forbidden,
  /*!
   * No arguments needed.
   */
  kHTTPCode404NotFound,
  /*!
   * No arguments needed.
   */
  kHTTPCode500InternalError,
  /*!
   * Not implemented. Do not use. Ironic, isn't it ? :))
   */
  kHTTPCode501NotImplemented,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode502BadGateway,
  /*!
   * Not implemented. Do not use.
   */
  kHTTPCode503ServiceUnavailable
} kHTTPCodes;

/*!
 * @brief Call this once before using the kHTTPResponseArray.
 */
void setup_http_response_array(void);


#endif /* !_HTTP_CODES_H */
