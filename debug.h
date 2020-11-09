#ifndef DEBUG_H__
/*
 * debug.h - Helper macros for GError
 *
 * GERROR_MSG   Safe access to GError message
 * DEFINE_QUARK Quick definition for GQuark. Catalog should be unique
 * SET_GERROR   Quick macro to set gerror. DEFINE_QUARK is required
 */
#define GERROR_MSG(err) (err ? err->message : "Unknown error")

#define DEFINE_GQUARK(catalog) \
static GQuark error_quark(void)\
{\
  static GQuark quark;\
  if (!quark)\
    quark = g_quark_from_static_string(catalog);\
  return quark;\
}

#define SET_GERROR(error, code, ...) \
G_STMT_START \
{ \
  g_set_error (error, error_quark(), code, __VA_ARGS__); \
} \
G_STMT_END

#endif /* DEBUG_H_ */
