/****************************************************************************
 * sk_csri_ext.h - Saiki's CSRI extension definitions
 *****************************************************************************
 * This extension widens the CSRI API with helper callbacks for feeding
 * subtitle data incrementally. The definitions are kept platform-neutral so
 * renderers that are not tied to specific multimedia frameworks can still
 * participate.
 ****************************************************************************/

#ifndef _SK_CSRI_EXT_H
#define _SK_CSRI_EXT_H

#include "csri.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Media subtype identifiers understood by the extension. These values mirror
 * the textual subtitle subtypes we care about inside VSFilter without relying
 * on platform specific GUIDs.
 */
enum sk_csri_subtype {
    SK_CSRI_SUBTYPE_UNKNOWN = 0,
    SK_CSRI_SUBTYPE_UTF8,
    SK_CSRI_SUBTYPE_SSA,
    SK_CSRI_SUBTYPE_ASS,
    SK_CSRI_SUBTYPE_ASS2,
    SK_CSRI_SUBTYPE_SSF,
    SK_CSRI_SUBTYPE_VOBSUB,
    SK_CSRI_SUBTYPE_HDMV,
    SK_CSRI_SUBTYPE_DVB,
};

typedef void (*sk_csri_process_data_fn)(csri_inst *inst,
    const void *data,
    size_t length,
    double time_start,
    double time_stop,
    enum sk_csri_subtype subtype);

struct sk_csri_ext_impl {
    sk_csri_process_data_fn process_data;
};

#define SK_CSRI_EXT_ID "sk.csri_ext"

#ifdef __cplusplus
}
#endif

#endif /* _SK_CSRI_EXT_H */
