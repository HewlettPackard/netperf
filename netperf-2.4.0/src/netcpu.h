/* This should define all the common routines etc exported by the
   various netcpu_mumble.c files raj 2005-01-26 */

extern void  cpu_util_init(void);
extern void  cpu_util_terminate(void);
extern int   get_cpu_method();
extern void  get_cpu_idle(uint64_t *res);
extern float calibrate_idle_rate(int iterations, int interval);
extern float calc_cpu_util_internal(float elapsed);
extern void  cpu_start_internal(void);
extern void  cpu_stop_internal(void);

