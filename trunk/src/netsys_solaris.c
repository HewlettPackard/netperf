#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
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
find_system_model_sysinfo(char **system_model) {

#include <sys/systeminfo.h>
  char model_str[37];
  char *token1,*token2;
  long  ret;
  /* sysinfo is kind enough to zero-terminate for us. we will be
     ignoring the leading SUNW, if present so use 37 instead of 35 in
     case the platform name is long */
  ret = sysinfo(SI_PLATFORM,model_str,37);
  if (-1 != ret) {
    /* however, it seems to shove potentially redundant information at
       us and include a comma, which we have no desire to include, so
       we will ass-u-me we can do a couple strtok calls to be rid of
       that */
    token1 = strtok(model_str,",");
    token2 = strtok(NULL,",");
    if (token2)
      *system_model = strdup(token2);
    else
      *system_model = strdup(model_str);
  }
  else
    *system_model = strdup("sysinfo");

}

static void
find_system_model(char **system_model) {

  /* the .h file will be there even on a SPARC system, so we have to
     check for both the .h and the libarary... */
#if defined(HAVE_SYS_SMBIOS_H) && defined(HAVE_LIBSMBIOS)
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
  if (NULL == smbios_handle) {
    /* fall-back on sysinfo for the system model info, we don't really
       care why we didn't get a handle, just that we didn't get one */
#if defined(NETPERF_STANDALONE_DEBUG)
    printf("smbios_open returned NULL, error %d errno %d %s\n",
	   error,errno,strerror(errno));
#endif
    find_system_model_sysinfo(system_model);
    return;
  }
  ret = smbios_info_common(smbios_handle,256,&info);
  if (0 == ret)
    *system_model = strdup(info.smbi_product);
  else {
    /* we ass-u-me that while there was smbios on the system it didn't
       have the smbi_product information we seek, so once again we
       fallback to sysinfo.  this is getting tiresome isn't it?-) raj
       2008-03-12 */
#if defined(NETPERF_STANDALONE_DEBUG)
    printf("smbios_info_common returned %d errno %d %s\n",
	   ret,errno,strerror(errno));
#endif
    find_system_model_sysinfo(system_model);
  }
  smbios_close(smbios_handle);

#else

  find_system_model_sysinfo(system_model);

#endif

  return;
}

void
find_system_info(char **system_model, char **cpu_model, int *cpu_frequency) {
  int ret;

  find_system_model(system_model);
  find_cpu_model_freq(cpu_model,cpu_frequency);

}

#if defined(NETPERF_STANDALONE_DEBUG)
int
main(int argc, char *argv[]) {
  char *system_model;
  char *cpu_model;
  int  frequency;

  find_system_info(&system_model,&cpu_model,&frequency);
  printf("system_model %s, cpu_model %s, frequency %d\n",
	 system_model,
	 cpu_model,
	 frequency);
}
#endif

