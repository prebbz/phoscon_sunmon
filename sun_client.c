/* Client for accessing Sunrise/sunset times */

#include <glib.h>
#include <jansson.h>

#include "sun_client.h"
#include "util.h"
#include "debug.h"

/* Sunrise/sunset times provided by sunrise-sunset.org */
#define SUNRISE_SERVER_URL       "https://api.sunrise-sunset.org/json"
#define DATA_STALE_PERIOD_SECS   120

struct sun_client {
  GDateTime *sunrise;
  GDateTime *sunset;
  conn_handle_t *handle;
  gdouble lat;
  gdouble lon;
  gchar *req_str;
  gint64 last_fetch;
  gulong fetch_counter;
};

DEFINE_GQUARK("sun_client");


static struct sun_client *sclient;

static void
free_sun_client(struct sun_client *sc)
{
  if (!sc) {
    return;
  }

  if (sc->handle) {
    util_cleanup_handle(sc->handle);
  }
  g_clear_pointer(&sc->sunrise, g_date_time_unref);
  g_clear_pointer(&sc->sunset, g_date_time_unref);
  g_free(sc->req_str);
  g_free(sc);
}

static const gchar *
print_time_only(GDateTime *dt)
{
  static gchar buf[32];

  if (!dt) {
    return "<invalid>";
  }

  g_snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             g_date_time_get_hour(dt),
             g_date_time_get_minute(dt),
             g_date_time_get_second(dt));

  return buf;
}

static gchar *
format_duration_str(GTimeSpan tdiff)
{
  static gchar buf[256];
  const gchar *tstr = NULL;
  glong ltdf = labs(tdiff);
  guint disp = 0;
  gboolean before = (tdiff < 0);

  if (ltdf < G_TIME_SPAN_MINUTE) {
    tstr = "second";
    disp = ltdf / G_TIME_SPAN_SECOND;
  } else if (ltdf < G_TIME_SPAN_HOUR) {
    tstr = "minute";
    disp = ltdf / G_TIME_SPAN_MINUTE;
  } else {
    tstr = "hour";
    disp = ltdf / G_TIME_SPAN_HOUR;
  }

  g_snprintf(buf, sizeof(buf),
             "%u %s%s %s",
             disp, tstr, disp == 1 ? "" : "s",
             before ? "earlier" : "later");

  return buf;
}

static gboolean
sclient_lookup_internal(struct sun_client *sc, GError **err)
{
  GString *buff;
  GDateTime *tmp1 = NULL;
  GDateTime *tmp2 = NULL;
  gboolean ret = FALSE;
  json_t *jobj = NULL;
  json_t *jents = NULL;
  json_error_t jerr = { 0, };
  const gchar *srise_time = NULL;
  const gchar *sset_time = NULL;
  const gchar *status_str = NULL;

  g_return_val_if_fail(sc != NULL, FALSE);


  g_debug("req: %s", sc->req_str);
  if (!util_perform_http_get(sc->handle, sc->req_str, err)) {
    g_prefix_error(err, "lookup failed: ");
    return FALSE;
  }

  buff = util_get_handle_buffer(sc->handle);
  g_debug("result buffer: %s", buff->str);

  if ((jobj = json_loads(buff->str, 0, &jerr)) == NULL) {
    SET_GERROR(err, -1, "could not parse JSON response: %s", jerr.text);
    goto out;
  }

  if (json_unpack_ex(jobj, &jerr, 0, "{s:o,s:s}",
                     "results",  &jents,
                     "status",   &status_str) != 0) {
    SET_GERROR(err, -1, "could not parse JSON: %s", jerr.text);
    goto out;
  }

  /* Check the status is OK */
  if (g_strcmp0(status_str, "OK") != 0) {
    SET_GERROR(err, -1, "server returned invalid status '%s'", status_str);
    goto out;
  }

  if (json_unpack_ex(jents, &jerr, 0, "{s:s,s:s}",
                     "sunrise", &srise_time,
                     "sunset",  &sset_time) != 0) {
    SET_GERROR(err, -1, "unexpected JSON response: %s", jerr.text);
    goto out;
  }

  if ((tmp1 = g_date_time_new_from_iso8601(srise_time, NULL)) == NULL) {
    SET_GERROR(err, -1, "could not parse sunrise time string '%s'",
               srise_time);
    goto out;
  }

  if ((tmp2 = g_date_time_new_from_iso8601(sset_time, NULL)) == NULL) {
    SET_GERROR(err, -1, "could not parse sunset time string '%s'",
               sset_time);
    goto out;
  }

  g_clear_pointer(&sc->sunrise, g_date_time_unref);
  g_clear_pointer(&sc->sunset, g_date_time_unref);
  sc->last_fetch = g_get_monotonic_time();
  sc->sunrise = g_date_time_ref(tmp1);
  sc->sunset = g_date_time_ref(tmp2);
  sc->fetch_counter++;

  ret = TRUE;

out:
  if (jobj) {
    json_decref(jobj);
  }
  g_clear_pointer(&tmp1, g_date_time_unref);
  g_clear_pointer(&tmp2, g_date_time_unref);

  return ret;
}

/**** Exposed functions begin here **************************************/

gboolean
sun_client_init(gdouble lat, gdouble lon, GError **err)
{
  struct sun_client *sc;

  g_return_val_if_fail(sclient == NULL, FALSE);

  /* Use GLib memory allocators */
  json_set_alloc_funcs(g_malloc, g_free);
  sc = g_malloc0(sizeof(*sc));
  if ((sc->handle = util_init_handle(err)) == NULL) {
    g_prefix_error(err, "setup handle: ");
    goto out_fail;
  }

  sc->lat = lat;
  sc->lon = lon;
  sc->req_str = g_strdup_printf("%s?lat=%.7f&lng=%.7f&formatted=0",
                                SUNRISE_SERVER_URL,
                                sc->lat, sc->lon);

  if (!sclient_lookup_internal(sc, err)) {
    g_prefix_error(err, "fetch initial times failed: ");
    goto out_fail;
  }

  g_message("Sunrise/Sunset client initialised with location lat=%.6f long=%.6f",
            lat, lon);
  g_message("Attribution of API to sunrise-sunset.org");
  g_message("Initial sunrise time (UTC): %s", print_time_only(sc->sunrise));
  g_message("Initial sunset time (UTC) : %s", print_time_only(sc->sunset));
  sclient = sc;

  return TRUE;

out_fail:
  free_sun_client(sc);

  return FALSE;
}

void
sun_client_cleanup(void)
{
  struct sun_client *sc = sclient;

  if (!sc) {
    return;
  }

  g_message("Tearing down sun client, total lookups: %lu", sc->fetch_counter);
  g_clear_pointer(&sclient, free_sun_client);
}

gboolean
sun_client_lookup(GDateTime **sunrise, GDateTime **sunset, GError **err)
{
  struct sun_client *sc = sclient;
  gboolean use_cached = FALSE;

  g_return_val_if_fail(sc != NULL, FALSE);

  if (sc->last_fetch) {
    gint64 mt = (g_get_monotonic_time() - sc->last_fetch) / G_TIME_SPAN_SECOND;
    use_cached = mt < DATA_STALE_PERIOD_SECS;

    g_assert(sc->sunrise);
    g_assert(sc->sunset);
    g_debug("Sunrise/sunset data is %ld seconds old, cache use: %s",
            mt, use_cached ? "yes" : "no");
  }

  if (!use_cached && sclient_lookup_internal(sc, err) == FALSE) {
    return FALSE;
  }

  if (sunset) {
    *sunset = g_date_time_ref(sc->sunset);
  }
  if (sunrise) {
    *sunrise = g_date_time_ref(sc->sunrise);
  }

  return TRUE;
}

void
sun_client_print_tdiff(GDateTime *orig, GDateTime *latest,
                       const gchar *descr)
{
  GTimeSpan ts;

  g_return_if_fail(orig != NULL);
  g_return_if_fail(latest != NULL);

  if (!descr) {
    descr = "specified";
  }

  ts = util_dt_diff_time_only(latest, orig);
  if (!ts) {
    g_message("No difference in %s time (%s)", descr, print_time_only(orig));
    return;
  }

  g_message("The %s time is %s (%s)", descr,
            format_duration_str(ts), print_time_only(latest));
}
