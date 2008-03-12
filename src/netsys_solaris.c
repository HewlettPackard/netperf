#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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

static void
find_system_model(char **system_model) {

#if defined(HAVE_SYS_SMBIOS_H)
#include <sys/smbios.h>
  smbios_hdl_t *smbios_handle;
  smbios_info_t info; 

  int error;
  int  ret;
  id_t ret_id_t;

  /* much of this is wild guessing based on web searches, sys/smbios.h, and
     experimentation.  my thanks to a helpful person familiar with libsmbios
     who got me started.  feel free to make yourself known as you see fit :)
     rick jones 2008-03-12 */
  smbios_handle = smbios_open(NULL,SMB_VERSION,0,&error);
  ret = smbios_info_common(smbios_handle,256,&info);
  if (0 == ret) 
    *system_model = strdup(info.smbi_product);
  else
    *system_model = strdup("smbios_info_common");
  smbios_close(smbios_handle);

#else
  *system_model = strdup("Teach me for SPARC");
#endif

  return;
}

void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  int ret;

  find_system_model(system_model);
  find_cpu_model_freq(cpu_model,cpu_frequency);

}
