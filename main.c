#include <getopt.h>
#include <glib.h>
#include <glib-unix.h>

#include "sun_client.h"
#include "phoscon_client.h"
#include "debug.h"
#include "cfg.h"

#define DEFAULT_POLL_PERIOD_SEC   3600
#define MIN_POLL_PERIOD_SEC       10 * 60

#define MAX_SUNX_IDS  10   /* Maximum number of sunset/sunrise entries */

static gchar *prog_name;

struct prog_cfg {
  struct phoscon_client_cfg phoscon;
  gdouble latitude;
  gdouble longitude;
  guint poll_period_secs;
  gchar *sunset_id_strs;
  gchar *sunrise_id_strs;
  gint sunset_ids[MAX_SUNX_IDS];
  gint sunrise_ids[MAX_SUNX_IDS];
};

struct prog_state {
  struct prog_cfg cfg;
  GMainLoop *loop;
  GDateTime *sunset;
  GDateTime *sunrise;
  guint poll_src_id;
  gulong poll_cntr;
};

#define POFFS(m) (offsetof(struct phoscon_client_cfg, m))
#define GOFFS(m) (offsetof(struct prog_cfg, m))
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof(struct cfg_ent_descr))

DEFINE_GQUARK("phoscon_sunmon_main");

const struct cfg_ent_descr phoscon_cfg_ents[] = {
  { "hostname",  CFG_TYPE_STRING, POFFS(host),        TRUE,  "Hostname of phoscon gateway" },
  { "port",      CFG_TYPE_INT,    POFFS(port),        FALSE, "Port of phoscon gateway"     },
  { "apiKey",    CFG_TYPE_STRING, POFFS(api_key),     TRUE,  "Phoscon API key"             }
};

const struct cfg_ent_descr general_cfg_ents[] = {
  { "pollPeriod", CFG_TYPE_INT,    GOFFS(poll_period_secs), TRUE,  "Sunrise/set poll period" },
  { "latitude",   CFG_TYPE_DOUBLE, GOFFS(latitude),         TRUE,  "Location latitude"  },
  { "longitude",  CFG_TYPE_DOUBLE, GOFFS(longitude),        TRUE,  "Location longitude" }
};

const struct cfg_ent_descr sched_cfg_ents[] = {
  { "sunsetID",  CFG_TYPE_VALUE, GOFFS(sunset_id_strs),  FALSE,  "Sunset schedule IDs"  },
  { "sunriseID", CFG_TYPE_VALUE, GOFFS(sunrise_id_strs), FALSE,  "Sunrise schedule IDs" }
};

static gboolean
fetch_and_update_sun_times(struct prog_state *state, GError **err)
{
  struct prog_cfg *cfg;
  GDateTime *srt = NULL;
  GDateTime *sst = NULL;
  gboolean ret = FALSE;
  gint i;

  g_assert(state);

  cfg = &state->cfg;

  /* Fetch the times */
  if (!sun_client_lookup(&srt, &sst, err)) {
    return FALSE;
  }

  if (state->sunrise) {
    sun_client_print_tdiff(state->sunrise, srt, "sunrise");
  }
  if (state->sunset) {
    sun_client_print_tdiff(state->sunset, sst, "sunset");
  }

  for (i = 0; i < MAX_SUNX_IDS; i++) {
    if (cfg->sunrise_ids[i] < 0) {
      continue;
    } else if (!phoscon_client_update_schedule_time(cfg->sunrise_ids[i],
                                                    srt, err)) {
      g_prefix_error(err, "update sunrise schedule ID=%d: ",
                     cfg->sunrise_ids[i]);
      goto out;
    }
  }

  for (i = 0; i < MAX_SUNX_IDS; i++) {
    if (cfg->sunset_ids[i] < 0) {
      continue;
    } else if (!phoscon_client_update_schedule_time(cfg->sunset_ids[i],
                                                    sst, err)) {
      g_prefix_error(err, "update sunset schedule ID=%d: ",
                     cfg->sunset_ids[i]);
      goto out;
    }
  }

  ret = TRUE;
  g_clear_pointer(&state->sunrise, g_date_time_unref);
  g_clear_pointer(&state->sunset, g_date_time_unref);
  state->sunrise = g_date_time_ref(srt);
  state->sunset = g_date_time_ref(sst);

out:
  g_date_time_unref(srt);
  g_date_time_unref(sst);

  return ret;
}

static gboolean
handle_sigint(gpointer data)
{
  struct prog_state *state = (struct prog_state *) data;

  g_message("Caught signal, shutting down");
  g_main_loop_quit(state->loop);

  return FALSE;
}

static gboolean
handle_poll_timeout(gpointer data)
{
  GError *err = NULL;
  struct prog_state *state = (struct prog_state *) data;

  if (!fetch_and_update_sun_times(state, &err)) {
    g_warning("Poll update #%lu failed: %s",
              state->poll_cntr, GERROR_MSG(err));
    g_clear_error(&err);
  }

  state->poll_cntr++;

  /* As this is glib callback, always return TRUE */
  return TRUE;
}

static void
clear_prog_cfg(struct prog_cfg *cfg)
{
  struct phoscon_client_cfg *pclient;

  g_assert(cfg);

  pclient = &cfg->phoscon;
  g_free(pclient->host);
  g_free(pclient->api_key);
  g_free(cfg->sunrise_id_strs);
  g_free(cfg->sunset_id_strs);

  memset(cfg, 0, sizeof(*cfg));
}

static void
clear_prog_state(struct prog_state *state)
{
  g_assert(state);

  if (state->poll_src_id) {
    g_source_remove(state->poll_src_id);
    state->poll_src_id = 0;
  }

  clear_prog_cfg(&state->cfg);
  g_clear_pointer(&state->sunrise, g_date_time_unref);
  g_clear_pointer(&state->sunset, g_date_time_unref);
  g_clear_pointer(&state->loop, g_main_loop_unref);
}

static gboolean
parse_sunx_ids(const gchar *str, const gchar *actstr, gint *ids, GError **err)
{
  gboolean ret = FALSE;
  gchar **splits = FALSE;
  gint i;

  if (!str || !strlen(str)) {
    return TRUE;
  }

  splits = g_strsplit(str, ",", MAX_SUNX_IDS + 1);
  if (g_strv_length(splits) > MAX_SUNX_IDS) {
    g_warning("Too many %s IDs specified, only first %d will be used",
              actstr, MAX_SUNX_IDS);
  }

  for (i = 0; splits[i] && i < MAX_SUNX_IDS; i++) {
    gchar *eptr = NULL;
    gint val;

    if (strlen(splits[i]) == 0) {
      SET_GERROR(err, -1, "empty %s ID at position #%d", actstr, i + 1);
      goto out;
    } else if ((val = g_ascii_strtoll(splits[i], &eptr, 10)) < 0 ||
               (eptr && strlen(eptr))) {
      SET_GERROR(err, -1, "unable to parse %s '%s' (ID #%d in list)",
                 actstr, splits[i], i + 1);
      goto out;
    }

    ids[i] = val;
  }

  ret = TRUE;
  /* fall through */

out:
  g_strfreev(splits);

  return ret;
}

static gboolean
parse_config(const gchar *cfgfile, struct prog_cfg *cfg, GError **err)
{
  GList *grp_list = NULL;
  gboolean ret = FALSE;
  gint i;
  struct cfg_group grps[] = {
    { "phoscon",  TRUE,   &cfg->phoscon, ARRAY_SIZE(phoscon_cfg_ents), phoscon_cfg_ents },
    { "general",  TRUE,   cfg,           ARRAY_SIZE(general_cfg_ents), general_cfg_ents },
    { "schedules", FALSE, cfg,           ARRAY_SIZE(sched_cfg_ents),   sched_cfg_ents },
    { NULL, },
  };

  /* Initialise all IDs to -1 (uninitialised) */
  for (i = 0; i < MAX_SUNX_IDS; i++) {
    cfg->sunset_ids[i] =  -1;
    cfg->sunrise_ids[i] =  -1;
  }

  for (i = 0; grps[i].grp_name; i++) {
    grp_list = g_list_append(grp_list, &grps[i]);
  }

  if (!cfg_parse_file(cfgfile, grp_list, err)) {
    goto out;
  }

  if (!parse_sunx_ids(cfg->sunrise_id_strs, "sunrise", cfg->sunrise_ids, err) ||
      !parse_sunx_ids(cfg->sunset_id_strs,  "sunset",  cfg->sunset_ids, err)) {
    goto out;
  }

  if (cfg->poll_period_secs < MIN_POLL_PERIOD_SEC) {
    g_warning("Invalid sun service poll period, using default");
    cfg->poll_period_secs = DEFAULT_POLL_PERIOD_SEC;
  }

  ret = TRUE;
  /* fall through */
out:
  g_list_free(grp_list);

  return ret;
}

static gboolean
dump_schedule_list(struct prog_cfg *cfg, GError **err)
{
  GList *res = NULL;
  GList *node;
  gint rc;

  if ((rc = phoscon_client_list_all_schedules(&res)) < 0) {
    SET_GERROR(err, -1, "schedule fetch failed");
    return FALSE;
  }

  g_message("Phoscon schedule list (%d entr%s)", rc, rc == 1 ? "y" : "ies");

  g_print("+-----+--------------------+------------+---------------------+-------------------+\n"
          "| ID  | Name               | Status     | Created             | Schedule (local)  |\n"
          "+-----+--------------------+------------+---------------------+-------------------+\n");

  for (node = res; node; node = node->next) {
    const struct phoscon_schedule_ent *ent = node->data;
    gchar *cstr = ent->created ? g_date_time_format(ent->created, "%F %T") :
                                 g_strdup("invalid");

    g_print("| %03d | %-18s | %-10s | %-19s | %-17s |\n",
            ent->id, ent->name, ent->status, cstr, ent->local_timestr);
    g_free(cstr);
  }

  if (rc) {
    g_print("+-----+--------------------+------------+---------------------+-------------------+\n");
  }

  return TRUE;
}

static void
usage(const gchar *errstr, gint exit_code)
{
  g_printerr("Phoscon Schedule Sunset/Sunrise monitor v0.1\n");
  if (errstr) {
    g_printerr("\nError: %s\n", errstr);
  }
  g_printerr("\nUsage: %s [options] -c <cfg_file>\n"
             "Options:\n"
             "  --config          -c    Configuration file to parse\n"
             "  --once            -o    Fetch and update once, then exit\n"
             "  --list-schedules  -l    List all Phoscon schedules then exit\n"
             "  --help            -h    Show help options\n\n",
             prog_name);
  exit(exit_code);
}

gint
main(gint argc, gchar **argv)
{
  GError *err = NULL;
  struct prog_state state = { 0, };
  struct prog_cfg *cfg = &state.cfg;
  gchar *cfgfile = NULL;
  gboolean one_shot = FALSE;
  gboolean do_list = FALSE;
  gint retval = EXIT_FAILURE;
  gint opt;

  static const struct option opts[] = {
    { "help",           no_argument,        NULL, 'h' },
    { "config",         required_argument,  NULL, 'c' },
    { "once",           no_argument,        NULL, 'o' },
    { "list-schedules", no_argument,        NULL, 'l' },
    { NULL, 0, NULL,  0  }
  };

  prog_name = argv[0];

  while ((opt = getopt_long(argc, argv, "hc:ol", opts, NULL)) != -1) {
    switch (opt) {
    case 'h':
      usage(NULL, EXIT_SUCCESS);
      break;
    case 'l':
      do_list = TRUE;
      break;
    case 'c':
      cfgfile = optarg;
      break;
    case 'o':
      one_shot = TRUE;
      break;
    default:
      usage("Illegal argument", EXIT_FAILURE);
    }
  }

  if (one_shot && do_list) {
    usage("Illegal argument combination", EXIT_FAILURE);
  } else if (!cfgfile) {
    usage("Missing configuration file", EXIT_FAILURE);
  }

  /* Parse the configuration from the keyfile */
  if (!parse_config(cfgfile, cfg, &err)) {
    g_printerr("Could not parse config file '%s': %s\n",
               cfgfile, GERROR_MSG(err));
    goto out;
  }

  /* Initialise the phoscon client */
  if (!phoscon_client_init(&cfg->phoscon, &err)) {
    g_printerr("Could initialise phoscon client: %s\n",
               GERROR_MSG(err));
    goto out;
  }

  if (do_list) {
    if (!dump_schedule_list(cfg, &err)) {
      g_printerr("Could not list schedules: %s\n", GERROR_MSG(err));
    }
    goto out;
  }

  /* Initialise the sunrise client */
  if (!sun_client_init(cfg->latitude, cfg->longitude, &err)) {
    g_printerr("Could initialise sunrise/set client: %s\n",
               GERROR_MSG(err));
    goto out;
  }

  /* Perform initial update before doing the periodic ones */
  if (!fetch_and_update_sun_times(&state, &err)) {
    g_printerr("Perform initial update failed: %s\n", GERROR_MSG(err));
    goto out;
  }

  if (one_shot) {
    /* We are doneskys */
    g_message("One-shot mode, exit with success code");
    retval = EXIT_SUCCESS;
    goto out;
  }

  g_message("Sunrise/sunset poll period is %u seconds",
            cfg->poll_period_secs);
  state.poll_src_id = g_timeout_add_seconds(cfg->poll_period_secs,
                                            handle_poll_timeout, &state);
  g_unix_signal_add(SIGINT, handle_sigint, &state);
  g_unix_signal_add(SIGTERM, handle_sigint, &state);
  state.loop = g_main_loop_new(NULL, FALSE);

  /* Start the main loop */
  g_message("Entering main loop...");
  g_main_loop_run(state.loop);
  g_message("Shutting down after %lu poll(s)", state.poll_cntr);

  retval = EXIT_SUCCESS;
out:
  clear_prog_state(&state);
  g_clear_error(&err);

  return retval;
}
