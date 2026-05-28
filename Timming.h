#ifndef TIMMING_H
#define TIMMING_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#include <sys/resource.h>
#include <sys/time.h>

void uswtime(double *usertime, double *systime, double *walltime);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* TIMMING_H */
