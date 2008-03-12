#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <kstat.h>

static kstat_ctl_t *kc = NULL;
static kid_t kcid = 0;

static void
find_cpu_model_freq(char **cpu_model, int *frequency) {

  kstat_t *ksp;
  kid_t nkcid;
  kstat_named_t *knp;
  int i,found_brand,found_freq;


  found_brand = 0;
  found_freq = 0;

  kc = kstat_open();

  if (NULL == kc) {
    *cpu_model = strdup("kstat_open");
    *frequency = -1;
    return;
  }

  ksp = kstat_lookup(kc, "cpu_info", 0, NULL);

  if ((NULL == ksp) ||
      ((ksp) && (KSTAT_TYPE_NAMED != ksp->ks_type))) {
    *cpu_model = strdup("kstat_lookup");
    *frequency = -1;
    kstat_close(kc);
    return;
  }

  nkcid = kstat_read(kc, ksp, NULL);

  if (-1 == nkcid) {
    *cpu_model = strdup("kstat_read");
    *frequency = -1;
    kstat_close(kc);
    return;
  }

  for (i = ksp->ks_ndata, knp = ksp->ks_data;
       i > 0;
       knp++, i--) {
    if (!strcmp("brand", knp->name)) {
      *cpu_model = strdup(KSTAT_NAMED_STR_PTR(knp));
      found_brand = 1;
    }
    else if (!strcmp("clock_MHz",knp->name)) {
      *frequency = (int)knp->value.ui32;
      found_freq = 1;
    }
  }
  if (!found_brand)
    *cpu_model = strdup("CPU Not Found");
  if (!found_freq)
    *frequency = -1;

  kstat_close(kc);
}


void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  int ret;

  *system_model = strdup("Teach Me DMI");
  find_cpu_model_freq(cpu_model,cpu_frequency);

}
