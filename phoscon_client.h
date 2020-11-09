/* Simple Phoscon client */

#ifndef PHOSCON_CLIENT_H__
#define PHOSCON_CLIENT_H__

#include <glib.h>

struct phoscon_client_cfg {
  gchar *host;
  guint port;
  gchar *api_key;
};

struct phoscon_schedule_ent {
  gint id;
  gchar *name;
  gchar *descr;
  gchar *status;
  GDateTime *created;
  gchar *timestr;          /* "time": "W127/T15:30:00" */
  gchar *local_timestr;
};

gboolean
phoscon_client_init(const struct phoscon_client_cfg *cfg, GError **err);

void
phoscon_client_release(void);

const struct phoscon_schedule_ent *
phoscon_client_lookup_schedule(gint id);

gboolean
phoscon_client_update_schedule_time(gint id, GDateTime *utc, GError **err);

#endif /* PHOSCON_CLIENT_H__ */
