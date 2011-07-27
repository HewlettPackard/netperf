#include <string.h>

#ifdef WIN32
#define strdup _strdup
#endif

void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  *system_model = strdup("Unknown System Model");
  *cpu_model = strdup("Unknown CPU Model");
  *cpu_frequency = -1;
}
