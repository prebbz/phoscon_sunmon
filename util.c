#include <glib.h>
#include <curl/curl.h>

#include "debug.h"
#include "util.h"

struct conn_handle {
  CURL *curl;
  GString *buffer;
  glong http_code;
};

DEFINE_GQUARK("util");

static size_t
write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  GString *gs = (GString *) userdata;
  gssize sz = size * nmemb;

  g_string_append_len(gs, ptr, sz);

  return sz;
}

static size_t
read_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
  GString *gs = (GString *) userdata;
  gssize sz = MIN(gs->len, size * nmemb);

  memcpy(ptr, gs->str, sz);
  return sz;
}

static gboolean
check_http_code(conn_handle_t *handle, GError **err)
{
  CURLcode cret;

  g_assert(handle);

  if ((cret = curl_easy_getinfo(handle->curl, CURLINFO_RESPONSE_CODE,
                                &handle->http_code)) != CURLE_OK) {
    SET_GERROR(err, -1, "could not get HTTP response code (%s)",
               curl_easy_strerror(cret));
    return FALSE;
  }

  g_debug("HTTP code: %ld", handle->http_code);

  if (handle->http_code >= 200 && handle->http_code <= 299) {
    return TRUE;
  }

  SET_GERROR(err, -1, "request failed with HTTP code %ld",
             handle->http_code);

  return FALSE;
}


/**** Exposed functions begin here **************************************/

conn_handle_t *
util_init_handle(GError **err)
{
  conn_handle_t *handle;
  CURLcode cret;

  handle = g_malloc0(sizeof(*handle));
  handle->buffer = g_string_new(NULL);

  if ((handle->curl = curl_easy_init()) == NULL) {
    SET_GERROR(err, -1, "unable to setup libCURL backend");
    goto out_fail;
  }

  cret = curl_easy_setopt(handle->curl, CURLOPT_READFUNCTION, read_callback);
  cret |= curl_easy_setopt(handle->curl, CURLOPT_WRITEFUNCTION, write_callback);
  cret |= curl_easy_setopt(handle->curl, CURLOPT_WRITEDATA, handle->buffer);

  if (cret != CURLE_OK) {
    SET_GERROR(err, -1, "failed to set curl options");
    goto out_fail;
  }


  return handle;

out_fail:
  g_clear_pointer(&handle, curl_easy_cleanup);

  return FALSE;
}

gboolean
util_perform_http_get(conn_handle_t *handle, const gchar *url, GError **err)
{
  CURLcode cret;
  gboolean ret;

  g_return_val_if_fail(handle != NULL, FALSE);
  g_return_val_if_fail(url != NULL, FALSE);

  cret = curl_easy_setopt(handle->curl, CURLOPT_URL, url);

  if (cret != CURLE_OK) {
    SET_GERROR(err, -1, "could not set curl options");
    goto out;
  }

  g_string_truncate(handle->buffer, 0);

  if ((cret = curl_easy_perform(handle->curl)) != CURLE_OK) {
    SET_GERROR(err, -1, "GET request failed: %s", curl_easy_strerror(cret));
    return FALSE;
  }

  if ((ret = check_http_code(handle, err)) == FALSE) {
    g_prefix_error(err, "HTTP GET ");
  }

out:
  return ret;
}

gboolean
util_perform_http_put(conn_handle_t *handle, const gchar *url,
                      const gchar *data, GError **err)
{
  GString *gs;
  CURLcode cret;
  gboolean ret = FALSE;

  g_return_val_if_fail(handle != NULL, FALSE);
  g_return_val_if_fail(url != NULL, FALSE);
  g_return_val_if_fail(data != NULL, FALSE);

  gs = g_string_new(data);

  cret = curl_easy_setopt(handle->curl, CURLOPT_UPLOAD, 1L);
  cret |= curl_easy_setopt(handle->curl, CURLOPT_PUT, 1L);
  cret |= curl_easy_setopt(handle->curl, CURLOPT_URL, url);
  cret |= curl_easy_setopt(handle->curl, CURLOPT_READDATA, gs);
  cret |= curl_easy_setopt(handle->curl, CURLOPT_INFILESIZE_LARGE,
                           (curl_off_t) gs->len);
  if (cret != CURLE_OK) {
    SET_GERROR(err, -1, "failed to set curl upload options");
    goto out;
  }

  g_string_truncate(handle->buffer, 0);

  if ((cret = curl_easy_perform(handle->curl)) != CURLE_OK) {
    SET_GERROR(err, -1, "PUT request failed: %s", curl_easy_strerror(cret));
    goto out;
  }

  if ((ret = check_http_code(handle, err)) == FALSE) {
    g_prefix_error(err, "HTTP PUT ");
  }

  g_string_free(gs, TRUE);

out:
  curl_easy_setopt(handle->curl, CURLOPT_UPLOAD, 0L);
  curl_easy_setopt(handle->curl, CURLOPT_PUT, 0L);

  return ret;
}

void
util_cleanup_handle(conn_handle_t *handle)
{
  g_return_if_fail(handle != NULL);

  curl_easy_cleanup(handle->curl);
  g_string_free(handle->buffer, TRUE);
  g_free(handle);
}

GString *
util_get_handle_buffer(conn_handle_t *handle)
{
  g_return_val_if_fail(handle != NULL, NULL);

  return handle->buffer;
}

const gchar *
util_dt_format(GDateTime *dt)
{
  static gchar buf[64];

  if (!dt) {
    return "<invalid>";
  }

  g_snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
             g_date_time_get_year(dt),
             g_date_time_get_month(dt),
             g_date_time_get_day_of_month(dt),
             g_date_time_get_hour(dt),
             g_date_time_get_minute(dt),
             g_date_time_get_second(dt));

  return buf;
}

GTimeSpan
util_dt_diff_time_only(GDateTime *begin, GDateTime *end)
{
   /* Ignores the date component */
  gint hrdiff;
  gint mindiff;
  gint secdiff;

  g_assert(end);
  g_assert(begin);

  hrdiff = g_date_time_get_hour(end) - g_date_time_get_hour(begin);
  mindiff = g_date_time_get_minute(end) - g_date_time_get_minute(begin);
  secdiff = g_date_time_get_second(end) - g_date_time_get_second(begin);

  return ((hrdiff * G_TIME_SPAN_HOUR) + (mindiff * G_TIME_SPAN_MINUTE) +
          (secdiff * G_TIME_SPAN_SECOND));
}

