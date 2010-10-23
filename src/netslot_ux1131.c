#include <sys/libIO.h>
#include <stdlib.h>
#include <sys/types.h>

/*
 * No OLAR headerfile on target system! ;-(
 */
typedef uint64_t olar_io_slot_t;

typedef struct olar_error_info {
        int  oe_err;
        char oe_hwpath[MAX_HW_PATH_STR];
} olar_err_t;

#define MAX_SLOT_ID_LEN 30

char * get_hw_path_from_if(char *if_name);

char *
find_interface_slot(char *interface_name) {

  char *hw_str;
  olar_io_slot_t slot_id;
  olar_err_t oe;
  char slot_str[MAX_SLOT_ID_LEN];
  
  /* Open dev_config for libIO communication */
  if ( io_init(O_RDONLY) == IO_ERROR ) {
    return strdup("io_init");
  }

  hw_str = get_hw_path_from_if(interface_name);

  /* close dev_config */
  io_end();

  slot_id = 0;
  if ( olar_path_to_id(hw_str,&slot_id,&oe) == -1 ) {
    free(hw_str);
    return strdup("olar_path_to_id");
  }

  if ( olar_slot_id_to_string(slot_id,slot_str,30) == -1 ) {
    free(hw_str);
    return strdup("slot_to_string");
  }

  free(hw_str);
  
  return strdup(slot_str);
}

/*
 * Returns the H/W path string corresponding to the lan if name
 *
 * Assumption: if_name is of type lan%ppid
 *             Not known to work with Logical,Vlan etc..
 *
 */
char * get_hw_path_from_if(char *if_name)
{
  int instance;
  io_token_t tok;
  hw_path_t hw;
  char *hwstr;

  sscanf(if_name,"lan%d",&instance);

  if ( (tok = io_search(NULL,S_IOTREE_EXT,0,"class", "lan",
                        "instance", &instance, NULL)) == NULL ) {
    /* we don't want extraneous output on netserver side - at some
       point we can teach it about "where" */
    /* io_error("Could not find H/w path"); */
    return(NULL);
  }

  if ( io_node_to_hw_path(tok, &hw) != IO_SUCCESS ) {
    /* io_error("io_node_to_hw_path failed"); */
    return NULL;
  }

  hwstr = (char *) malloc (MAX_HW_PATH_STR * sizeof(char));
  if (hwstr == NULL ) {
    /* perror("malloc failed in get_hw_path_from_if"); */
    return NULL;
  }

  if  (io_hw_path_to_str(hwstr,&hw) == IO_ERROR ) {
    /* io_error("io_hw_path_to_str failed"); */
    free(hwstr);
    return NULL;
  }

  return hwstr;
}

void
find_interface_ids(char *interface_name, int *vendor, int *device, int *sub_vend, int *sub_dev) {
  *vendor = 0;
  *device = 0;
  *sub_vend = 0;
  *sub_dev = 0;
  return;
}
