#include <getopt.h>
#include <glib.h>
#include <glib-unix.h>

#include "sun_client.h"
#include "phoscon_client.h"
#include "debug.h"
#include "cfg.h"

#define DEFAULT_POLL_PERIOD_SEC   3600
#define MIN_POLL_PERIOD_SEC       10 * 60

static gchar *prog_name;

struct prog_cfg {
  struct phoscon_client_cfg phoscon;
  gdouble latitude;
  gdouble longitude;
  guint poll_period_secs;
  gint sunset_sid;
  gint sunrise_sid;
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
  { "sunsetID",  CFG_TYPE_INT, GOFFS(sunset_sid),  FALSE,  "Sunset schedule ID"  },
  { "sunriseID", CFG_TYPE_INT, GOFFS(sunrise_sid), FALSE,  "Sunrise schedule ID" }
};

static gboolean
fetch_and_update_sun_times(struct prog_state *state, GError **err)
{
  GDateTime *srt = NULL;
  GDateTime *sst = NULL;
  gboolean ret = FALSE;

  g_assert(state);

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

  if (state->cfg.sunrise_sid > 0) {
    if (!phoscon_client_update_schedule_time(state->cfg.sunrise_sid,
                                             srt, err)) {
      g_prefix_error(err, "update sunrise schedule: ");
      goto out;
    }
  } else {
    g_debug("No specified schedule ID for sunrise");
  }

  if (state->cfg.sunset_sid > 0) {
    if (!phoscon_client_update_schedule_time(state->cfg.sunset_sid,
                                             sst, err)) {
      g_prefix_error(err, "update sunset schedule: ");
      goto out;
    }
  } else {
    g_debug("No specified schedule ID for sunset");
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
parse_config(const gchar *cfgfile, struct prog_cfg *cfg, GError **err)
{
  GList *grp_list = NULL;
  gboolean ret;
  gint i;
  struct cfg_group grps[] = {
    { "phoscon",  TRUE,   &cfg->phoscon, ARRAY_SIZE(phoscon_cfg_ents), phoscon_cfg_ents },
    { "general",  TRUE,   cfg,           ARRAY_SIZE(general_cfg_ents), general_cfg_ents },
    { "schedules", FALSE, cfg,           ARRAY_SIZE(sched_cfg_ents),   sched_cfg_ents },
    { NULL, },
  };

  for (i = 0; grps[i].grp_name; i++) {
    grp_list = g_list_append(grp_list, &grps[i]);
  }
  ret = cfg_parse_file(cfgfile, grp_list, err);
  g_list_free(grp_list);

  if (cfg->poll_period_secs < MIN_POLL_PERIOD_SEC) {
    g_warning("Invalid sun service poll period, using default");
    cfg->poll_period_secs = DEFAULT_POLL_PERIOD_SEC;
  }

  return ret;
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
             "  --config   -c     Configuration file to parse\n"
             "  --once     -o     Fetch and update once, then exit\n"
             "  --help     -h     Show help options\n\n",
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
  gint retval = EXIT_FAILURE;
  gint opt;

  static const struct option opts[] = {
    { "help",   no_argument,        NULL, 'h' },
    { "config", required_argument,  NULL, 'c' },
    { "once",   no_argument,        NULL, 'o' },
    { NULL,    0,                   NULL,  0  }
  };

  while ((opt = getopt_long(argc, argv, "hc:o", opts, NULL)) != -1) {
    switch (opt) {
    case 'h':
      usage(NULL, EXIT_SUCCESS);
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

  if (!cfgfile) {
    usage("Missing configuration file", EXIT_FAILURE);
  }

  /* Parse the configuration from the keyfile */
  if (!parse_config(cfgfile, cfg, &err)) {
    g_printerr("Could not parse config file '%s': %s\n",
               cfgfile, GERROR_MSG(err));
    goto out;
  }

  /* Initialise the sunrise client */
  if (!sun_client_init(cfg->latitude, cfg->longitude, &err)) {
    g_printerr("Could initialise sunrise/set client: %s\n",
               GERROR_MSG(err));
    goto out;
  }

  /* Initialise the phoscon client */
  if (!phoscon_client_init(&cfg->phoscon, &err)) {
    g_printerr("Could initialise phoscon client: %s\n",
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
