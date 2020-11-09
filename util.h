/* Utilities for common use */

#ifndef UTIL_H__
#define UTIL_H__

#include <glib.h>
#include <curl/curl.h>

typedef struct conn_handle conn_handle_t;

conn_handle_t *
util_init_handle(GError **err);

void
util_cleanup_handle(conn_handle_t *handle);

gboolean
util_perform_http_get(conn_handle_t *handle, const gchar *url, GError **err);

gboolean
util_perform_http_put(conn_handle_t *handle, const gchar *url,
                      const gchar *data, GError **err);

GString *
util_get_handle_buffer(conn_handle_t *handle);

GTimeSpan
util_dt_diff_time_only(GDateTime *start, GDateTime *end);

const gchar *
util_dt_format(GDateTime *dt);

#endif /* UTIL_H__ */
