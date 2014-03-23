#ifndef _RPMFC_INTERNAL_H
#define _RPMFC_INTERNAL_H

#include <rpm/argv.h>
#include <rpm/rpmfc.h>
#include <rpm/rpmstrpool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rpmcf_s {
    ARGV_t fattrs;	/*!< file attribute tokens */
    rpm_color_t fcolor;	/*!< file color */
    char *ftype;	/*!< file type (class) */
};

typedef struct rpmcf_s * rpmcf;	    /*!< classified file */

rpmcf rpmfcClassifyFile(rpmfc fc, const char *fn, rpm_mode_t mode);

rpm_color_t rpmcfColor(rpmcf cf);

const ARGV_t rpmcfAttrs(rpmcf cf);

const char *rpmcfType(rpmcf cf);

rpmcf rpmcfFree(rpmcf cf);

#ifdef __cplusplus
}
#endif

#endif /* _RPMFC_INTERNAL_H */
