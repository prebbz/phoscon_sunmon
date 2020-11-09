#include <glib.h>

#include "cfg.h"
#include "debug.h"

DEFINE_GQUARK("cfg");

static gboolean
parse_config_group(GKeyFile *kf, struct cfg_group *grp, GError **err)
{
  guint i;
  gint parsed = 0;
  const gchar *grpname;

  g_assert(kf);
  g_assert(grp);
  g_assert(grp->member);

  grpname = grp->grp_name;
  g_debug("Processing config group '%s'", grpname);

  for (i = 0; i < grp->count; i++) {
    GError *lerr = NULL;
    const struct cfg_ent_descr *d = grp->descrs + i;
    gpointer ptr = (gpointer) grp->member + d->mbr_offs;

    if (!g_key_file_has_key(kf, grpname, d->key, NULL)) {
      if (d->required) {
        SET_GERROR(err, -1, "missing required key '%s'", d->key);
        return FALSE;
      }
      g_message("Missing config entry for key '%s'", d->key);
      continue;
    }

    switch(d->type) {
      case CFG_TYPE_STRING: {
        gchar **sptr = (gchar **) ptr;

        *sptr = g_key_file_get_string(kf, grpname, d->key, &lerr);
        break;
      }
      case CFG_TYPE_BOOLEAN: {
        gboolean *b = (gboolean *) ptr;

        *b = g_key_file_get_boolean(kf, grpname, d->key, &lerr);
        break;
      }
      case CFG_TYPE_INT: {
        gint *i = (gint *) ptr;

        *i = g_key_file_get_integer(kf, grpname, d->key, &lerr);
        break;
      }
      case CFG_TYPE_DOUBLE: {
        gdouble *dv = (gdouble *) ptr;

        *dv = g_key_file_get_double(kf, grpname, d->key, &lerr);
        break;
      }
      default: {
        g_assert_not_reached();
      }
    }

    if (lerr) {
      g_propagate_prefixed_error(err, lerr, "could not parse key '%s': ",
                                 d->key);
      return FALSE;
    }
    parsed++;
  }

  g_message("Parsed %d key(s) from group '%s'", parsed, grpname);

  return TRUE;
}

/**** Exposed functions begin here **************************************/

gboolean
cfg_parse_file(const gchar *cfgfile, GList *groups, GError **err)
{
  GKeyFile *kf;
  GList *node;
  gint grp_count = 0;
  gboolean ret = FALSE;

  g_return_val_if_fail(cfgfile != NULL, FALSE);

  kf = g_key_file_new();
  if (!g_key_file_load_from_file(kf, cfgfile, G_KEY_FILE_NONE, err)) {
    goto out;
  }

  for (node = groups; node; node = node->next) {
    struct cfg_group *grp = (struct cfg_group *) node->data;

    /* Check the group exists */
    if (!g_key_file_has_group(kf, grp->grp_name)) {
      if (grp->required) {
        SET_GERROR(err, -1, "missing required group '%s'", grp->grp_name);
        goto out;
      }
      g_message("No such group '%s' in config file", grp->grp_name);
      continue;
    } else if (!grp->member || !grp->descrs) {
      SET_GERROR(err, -1, "missing destination member struct or no entries");
      goto out;
    } else if (!parse_config_group(kf, grp, err)) {
      g_prefix_error(err, "failed to parse group '%s': ", grp->grp_name);
      goto out;
    }
    grp_count++;
  }
  g_message("Parsed %d group(s) from config file '%s'", grp_count, cfgfile);
  ret = TRUE;

  /* fall through */

out:
  g_key_file_unref(kf);

  return ret;
}
