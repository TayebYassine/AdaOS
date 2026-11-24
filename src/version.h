#ifndef ADAOS_VERSION_H
#define ADAOS_VERSION_H

// The version string the build stamps into the kernel with -DADAOS_VERSION.
// A kernel compiled by hand gets the plain name instead.

#ifndef ADAOS_VERSION
#define ADAOS_VERSION "AdaOS"  // fallback when the build passed no version
#endif

#endif /* ADAOS_VERSION_H */
