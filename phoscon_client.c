#include <glib.h>
#include <jansson.h>

#include "phoscon_client.h"
#include "debug.h"
#include "util.h"

#define DEFAULT_PHOSCON_PORT  8080

typedef struct phoscon_client {
  struct phoscon_client_cfg cfg;
  conn_handle_t *handle;
  gchar *base_url;
  GHashTable *schedules;
} phoscon_client_t;

DEFINE_GQUARK("phoscon_client");

static phoscon_client_t *pclient;

static void
free_phoscon_client(phoscon_client_t *pc)
{
  g_assert(pc);

  g_free(pc->cfg.api_key);
  g_free(pc->cfg.host);
  g_free(pc->base_url);
  g_clear_pointer(&pc->handle, util_cleanup_handle);
  g_clear_pointer(&pc->schedules, g_hash_table_destroy);
  g_free(pc);
}

static void
free_schedule_entry(gpointer data)
{
  struct phoscon_schedule_ent *ent = (struct phoscon_schedule_ent *) data;

  g_free(ent->name);
  g_free(ent->descr);
  g_free(ent->status);
  g_clear_pointer(&ent->created, g_date_time_unref);
  g_free(ent->timestr);
  g_free(ent->local_timestr);
  g_free(ent);
}

static gchar *
build_phoscon_base_url(const struct phoscon_client_cfg *cfg, gboolean use_https)
{
  g_assert(cfg);

  return g_strdup_printf("http%s://%s:%u/api/%s",
                         use_https ? "s" : "",
                         cfg->host, cfg->port, cfg->api_key);
}

static struct phoscon_schedule_ent *
dup_phoscon_schedule(struct phoscon_schedule_ent *src)
{
  struct phoscon_schedule_ent *dst;

  g_assert(src);
  g_assert(src->created);

  dst = g_malloc0(sizeof(*dst));
  dst->id = src->id;
  dst->name = g_strdup(src->name);
  dst->descr = g_strdup(src->descr);
  dst->status = g_strdup(src->status);
  dst->created = g_date_time_ref(src->created);
  dst->timestr = g_strdup(src->timestr);
  dst->local_timestr = g_strdup(src->local_timestr);

  return dst;
}

static struct phoscon_schedule_ent *
parse_phoscon_schedule(const gchar *id, json_t *jobj, GError **err)
{
  struct phoscon_schedule_ent *nsched = NULL;
  json_error_t jerr = { 0, };
  json_t *jdescr = NULL;
  struct phoscon_schedule_ent sent;
  const gchar *created_str;
  gchar *tmp = NULL;

  g_assert(jobj);
  g_assert(id);

  sent.id = g_ascii_strtoll(id, NULL, 10);

  if (sent.id < 0 || !json_is_object(jobj)) {
    SET_GERROR(err, -1, "invalid phoscon schedule");
    return NULL;
  }

  /* TODO: Add proper parsing of time strings into structs */
  if (json_unpack_ex(jobj, &jerr, 0, "{s:s,s:s,s:s,s:s,s?:s}",
                     "created",     &created_str,
                     "status",      &sent.status,
                     "name",        &sent.name,
                     "time",        &sent.timestr,
                     "localtime",   &sent.local_timestr) != 0) {
    SET_GERROR(err, -1, "invalid JSON response (%s)", jerr.text);
    return NULL;
  }
  /* Try to find the description. It can be NULL, so we can't use the
   * above unpack_ex code.
   */
  if ((jdescr = json_object_get(jobj, "description")) != NULL) {
    sent.descr = (gchar *) json_string_value(jdescr);
  }

  /* Pointless workaround to get the string returned by phoscon to be
   * accepted by GLibs ISO8601 parsing function
   */
  tmp = g_strdup_printf("%sZ", created_str);
  sent.created = g_date_time_new_from_iso8601(tmp, NULL);
  g_clear_pointer(&tmp, g_free);

  if (sent.created == NULL) {
    SET_GERROR(err, -1, "could not parse creation timestamp");
    return NULL;
  }

  nsched = dup_phoscon_schedule(&sent);
  g_date_time_unref(sent.created);

  g_message("Schedule [%d] '%s' Created: %s Status: %s Time: %s (Local: %s)",
            nsched->id, nsched->name,
            util_dt_format(nsched->created),
            nsched->status, nsched->timestr,
            nsched->local_timestr);

  return nsched;
}

static gboolean
fetch_all_schedules(phoscon_client_t *pc, GError **err)
{
  GString *buff;
  json_t *jobj = NULL;
  json_error_t jerr = { 0, };
  json_t *jent = NULL;
  const gchar *key = NULL;
  gchar *url;
  gboolean ret = FALSE;

  g_assert(pc);
  g_assert(pc->handle);
  g_assert(pc->base_url);

  url = g_strdup_printf("%s/schedules", pc->base_url);
  if (!util_perform_http_get(pc->handle, url, err)) {
    g_prefix_error(err, "connection to phoscon failed: ");
    goto out;
  }

  buff = util_get_handle_buffer(pc->handle);
  g_assert(buff);

  /* Parse the JSON */
  if ((jobj = json_loads(buff->str, 0, &jerr)) == NULL) {
    SET_GERROR(err, -1, "could not parse phoscon JSON response");
    goto out;
  }

  g_debug("buffer: %s", buff->str);

  json_object_foreach(jobj, key, jent) {
    struct phoscon_schedule_ent *se = parse_phoscon_schedule(key, jent, err);

    if (se == NULL) {
      g_prefix_error(err, "parse schedule '%s': ", key);
      goto out;
    }
    g_hash_table_insert(pc->schedules, &se->id, se);
  }

  ret = TRUE;

out:
  g_free(url);
  if (jobj) {
    json_decref(jobj);
  }

  return ret;
}

static gboolean
update_phoscon_schedule(phoscon_client_t *pc,
                        struct phoscon_schedule_ent *sent,
                        GError **err)
{
  GString *buff;
  json_t *jresp = NULL;
  json_t *jreq = NULL;
  gchar *jreq_str = NULL;
  json_t *jtmp = NULL;
  json_error_t jerr = { 0, };
  gboolean ret = FALSE;
  gchar *url;

  g_assert(pc);
  g_assert(sent);

  url = g_strdup_printf("%s/schedules/%d", pc->base_url, sent->id);
  /* Only update the time string for now */
  if ((jreq = json_pack_ex(&jerr, 0, "{s:s,s:s*}",
                           "time", sent->timestr,
                           "localtime", sent->local_timestr)) == NULL) {
    SET_GERROR(err, -1, "could not pack JSON request: %s", jerr.text);
    goto out;
  } else if ((jreq_str = json_dumps(jreq, JSON_INDENT(2))) == NULL) {
    SET_GERROR(err, -1, "could not pack get JSON string");
    goto out;
  }

  /* Update the remote schedule */
  g_debug("URL: %s\n"
            "Data: %s", url, jreq_str);
  if (!util_perform_http_put(pc->handle, url, jreq_str, err)) {
    goto out;
  }
  buff = util_get_handle_buffer(pc->handle);

  g_debug("response buff: %s", buff->str);
  /* Parse the JSON */
  if ((jresp = json_loads(buff->str, 0, &jerr)) == NULL) {
    SET_GERROR(err, -1, "could not parse phoscon JSON response");
    goto out;
  }

  /* TODO: Properly parse the JSON jtmp and verify the time matches what
   * we sent in.
   */
  if ((jtmp = json_array_get(jresp, 0)) == NULL ||
       json_object_get(jtmp, "success") == NULL) {
    SET_GERROR(err, -1, "unexpected response from server: %s",
               jtmp ? "missing success string" : "not an array");

    goto out;
  }

  ret = TRUE;

out:
  g_free(jreq_str);
  g_free(url);
  if (jresp) {
    json_decref(jresp);
  }
  if (jreq) {
    json_decref(jreq);
  }

  return ret;
}

static gboolean
update_time_str(struct phoscon_schedule_ent *sent, GDateTime *utc,
                gboolean *updated, GError **err)
{
  gchar *tstr;
  gchar *ntstr = NULL;
  gboolean upd = FALSE;

  g_assert(sent);
  g_assert(utc);

  if ((tstr = g_strstr_len(sent->timestr, -1, "/T")) == NULL) {
    SET_GERROR(err, -1, "could not find timestamp identifier");
    return FALSE;
  }

  ntstr = g_strdup_printf("%.*s/T%02d:%02d:%02d",
                          (gint) (tstr - sent->timestr), sent->timestr,
                          g_date_time_get_hour(utc),
                          g_date_time_get_minute(utc),
                          g_date_time_get_second(utc));

  if (g_strcmp0(sent->timestr, ntstr) != 0) {
    g_message("Updated UTC time for '%s' from '%s' -> '%s'",
              sent->name, sent->timestr, ntstr);

    /* Update the local time so it gets updated in phoscon */
    if (sent->local_timestr) {
      GDateTime *lt = g_date_time_to_local(utc);
      gchar *nlstr = g_strdup_printf("%.*s/T%02d:%02d:%02d",
                                     (gint) (tstr - sent->timestr),
                                     sent->timestr,
                                     g_date_time_get_hour(lt),
                                     g_date_time_get_minute(lt),
                                     g_date_time_get_second(lt));
      g_message("Updated local time for '%s' from '%s' -> '%s'",
                sent->name, sent->local_timestr, nlstr);
      g_free(sent->local_timestr);
      sent->local_timestr = nlstr;
      g_free(sent->timestr);
      sent->timestr = ntstr;
      upd = TRUE;
    }
  } else {
    g_debug("No time update (%s)", ntstr);
    g_free(ntstr);
  }

  if (updated) {
    *updated = upd;
  }

  return TRUE;
}

/**** Exposed functions begin here **************************************/

gboolean
phoscon_client_init(const struct phoscon_client_cfg *cfg, GError **err)
{
  phoscon_client_t *pc;

  g_return_val_if_fail(pclient == NULL, FALSE);
  g_return_val_if_fail(cfg != NULL, FALSE);
  g_return_val_if_fail(cfg->host != NULL, FALSE);
  g_return_val_if_fail(cfg->api_key != NULL, FALSE);

  /* Use GLib memory allocators */
  json_set_alloc_funcs(g_malloc, g_free);
  pc = g_malloc0(sizeof(*pc));
  pc->cfg.port = cfg->port > 0 ? cfg->port : DEFAULT_PHOSCON_PORT;
  pc->cfg.api_key = g_strdup(cfg->api_key);
  pc->cfg.host = g_strdup(cfg->host);
  pc->base_url = build_phoscon_base_url(cfg, FALSE);
  if ((pc->handle = util_init_handle(err)) == NULL) {
    goto out_fail;
  }
  pc->schedules = g_hash_table_new_full(g_int_hash, g_int_equal,
                                        NULL, free_schedule_entry);

  /* Try to fetch all the schedules */
  if (!fetch_all_schedules(pc, err)) {
    g_prefix_error(err, "fetch initial schedules failed, ");
    goto out_fail;
  }

  g_message("Phoscon simple client initialised, found %u schedules",
            g_hash_table_size(pc->schedules));

  pclient = pc;
  return TRUE;

out_fail:
  free_phoscon_client(pc);

  return FALSE;
}

void
phoscon_client_release(void)
{
  g_clear_pointer(&pclient, free_phoscon_client);
}

const struct phoscon_schedule_ent *
phoscon_client_lookup_schedule(gint id)
{
  phoscon_client_t *pc = pclient;

  g_return_val_if_fail(pc != NULL, NULL);

  return g_hash_table_lookup(pc->schedules, &id);
}

gboolean
phoscon_client_update_schedule_time(gint id, GDateTime *utc, GError **err)
{
  phoscon_client_t *pc = pclient;
  struct phoscon_schedule_ent *sent;
  gboolean did_update = FALSE;

  g_return_val_if_fail(pc != NULL, FALSE);
  g_return_val_if_fail(utc != NULL, FALSE);

  if ((sent = g_hash_table_lookup(pc->schedules, &id)) == NULL) {
    SET_GERROR(err, -1, "no schedule matching ID=%d", id);
    return FALSE;
  }

  if (!update_time_str(sent, utc, &did_update, err)) {
    g_prefix_error(err, "could not update time string: ");
    return FALSE;
  }

  if (!did_update) {
    g_message("No update of schedule time for '%s'", sent->name);
    return TRUE;
  }

  return update_phoscon_schedule(pc, sent, err);
}
