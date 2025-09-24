// Build-only probe TU to pull Emerald headers under host config.
// We do *not* include this into your C++ targets.
#include <global.h>
int pe_headers_probe(void) { return 0; }
