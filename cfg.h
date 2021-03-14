/* Program configuration parsing */

#ifndef CFG_H__
#define CFG_H__

#include <glib.h>

enum cfg_ent_type {
  CFG_TYPE_NONE = 0,
  CFG_TYPE_INT,
  CFG_TYPE_STRING,
  CFG_TYPE_BOOLEAN,
  CFG_TYPE_DOUBLE,
  CFG_TYPE_VALUE,
  CFG_TYPE_LAST,
};

struct cfg_ent_descr {
  gchar *key;
  enum cfg_ent_type type;
  goffset mbr_offs;
  gboolean required;
  gchar *descr;
};

struct cfg_group {
  gchar *grp_name;
  gboolean required;
  gpointer member;
  guint count;
  const struct cfg_ent_descr *descrs;
};

gboolean
cfg_parse_file(const gchar *cfgfile, GList *groups, GError **err);

#endif /* CFG_H */
