#include <string.h>

void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  *system_model = strdup("Unknown System Model");
  *cpu_model = strdup("Unknown CPU Model");
  *cpu_frequency = -1;
}
