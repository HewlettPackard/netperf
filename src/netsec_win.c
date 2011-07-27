#include <string.h>
#define strdup _strdup

void
find_security_info(int *enabled, int *type, char **specific){
  *enabled = -1;
  *type    = -1;
  *specific = strdup("N/A");
  return;
}
