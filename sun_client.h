/* Client for accessing Sunrise/sunset times provided by sunrise-sunset.org */

#ifndef SUN_CLIENT_H__
#define SUN_CLIENT_H__

#include <glib.h>

gboolean
sun_client_init(gdouble lat, gdouble lon, GError **err);

void
sun_client_cleanup(void);

gboolean
sun_client_lookup(GDateTime **sunrise, GDateTime **sunset, GError **err);

void
sun_client_print_tdiff(GDateTime *orig, GDateTime *latest,
                       const gchar *descr);

#endif /* SUN_CLIENT_H__ */
