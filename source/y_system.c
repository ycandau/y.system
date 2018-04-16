//******************************************************************************
//  @file
//  y.command - A Max object to call command functions:
//  - cd in the directory structure
//  - rename and delete files
//  - launch an executable
//
//  Yves Candau
//
//  @ingroup  myExternals
//

// TO DO:
//   Check existence of folders and files

//******************************************************************************
//  Header files
//
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <process.h>

#include "ext.h"       // Header file for all objects, should always be first
#include "ext_obex.h"  // Header file for all objects, required for new style Max object

//******************************************************************************
//  Preprocessor
//
#define TRACE(...)   do { if (false) object_post((t_object*)x, "TRACE:  " __VA_ARGS__); } while (0)
#define POST(...)    do { object_post((t_object*)x, __VA_ARGS__); } while (0)
#define MY_ERR(...)  do { if (x->a_error_report) object_error((t_object*)x, __VA_ARGS__); } while (0)
#define MY_ASSERT(test, ret, ...) do { if (test) { object_error((t_object*)x, __VA_ARGS__); return ret; } } while (0)

//******************************************************************************
//  Typedef
//

//******************************************************************************
//  Max object structure
//
typedef struct _system {

  t_object obj;
  t_symbol* name;

  void* outl_any;     // Outlet 0: messages
  void* outl_return;  // Outlet 1: 0 or 1 for failure or success (int)

  char  dir_cur[MAX_PATH_CHARS];
  short dir_cur_len;

  char a_verbose;
  char a_error_report;

} t_system;

//******************************************************************************
//  Global class pointer
//
static t_class* system_class = NULL;

//******************************************************************************
//  Function declarations
//
void* system_new       (t_symbol* sym, long argc, t_atom* argv);
void  system_free      (t_system* x);
void  system_assist    (t_system* x, void* b, long msg, long arg, char* str);

void system_bang       (t_system* x);
void system_post       (t_system* x);
void system_cd_patcher (t_system* x);
void system_cd         (t_system* x, t_symbol* sym);

void system_rename     (t_system* x, t_symbol* sym, long argc, t_atom* argv);
void system_delete     (t_system* x, t_symbol* sym, long argc, t_atom* argv);
void system_command    (t_system* x, t_symbol* sym, long argc, t_atom* argv);

void system_rename_d   (t_system* x, t_symbol* sym, long argc, t_atom* argv);
void system_delete_d   (t_system* x, t_symbol* sym, long argc, t_atom* argv);
void system_command_d  (t_system* x, t_symbol* sym, long argc, t_atom* argv);

void system_anything   (t_system* x, t_symbol* sym, long argc, t_atom* argv);

t_bool path_is_abs     (t_system* x, const char* str);
void   get_path        (t_system* x, const char* str_src, char* str_dest);

//******************************************************************************
//  Initialization
//
void ext_main(void* r) {

  t_class* c;

  c = class_new("y.system",
    (method)system_new,
    (method)system_free,
    (long)sizeof(t_system),
    (method)NULL,
    A_GIMME, 0);

  class_addmethod(c, (method)system_assist,    "assist",   A_CANT,  0);

  class_addmethod(c, (method)system_bang,      "bang",              0);
  class_addmethod(c, (method)system_post,      "post",              0);
  class_addmethod(c, (method)system_cd,        "cd",       A_SYM,   0);
  class_addmethod(c, (method)system_rename_d,  "rename",   A_GIMME, 0);
  class_addmethod(c, (method)system_rename_d,  "ren",      A_GIMME, 0);
  class_addmethod(c, (method)system_rename_d,  "mv",       A_GIMME, 0);
  class_addmethod(c, (method)system_delete_d,  "delete",   A_GIMME, 0);
  class_addmethod(c, (method)system_delete_d,  "del",      A_GIMME, 0);
  class_addmethod(c, (method)system_delete_d,  "rm",       A_GIMME, 0);
  class_addmethod(c, (method)system_command_d, "command",  A_GIMME, 0);
  class_addmethod(c, (method)system_command_d, "cmd",      A_GIMME, 0);

  class_addmethod(c, (method)system_anything,  "anything", A_GIMME, 0);

  CLASS_ATTR_CHAR(c, "verbose", 0, t_system, a_verbose);
  CLASS_ATTR_ORDER(c, "verbose", 0, "1");
  CLASS_ATTR_LABEL(c, "verbose", 0, "Verbose");
  CLASS_ATTR_STYLE(c, "verbose", 0, "onoff");
  CLASS_ATTR_SAVE(c, "verbose", 0);
  CLASS_ATTR_SELFSAVE(c, "verbose", 0);

  CLASS_ATTR_CHAR(c, "err_report", 0, t_system, a_error_report);
  CLASS_ATTR_ORDER(c, "err_report", 0, "2");
  CLASS_ATTR_LABEL(c, "err_report", 0, "Error reporting");
  CLASS_ATTR_STYLE(c, "err_report", 0, "onoff");
  CLASS_ATTR_SAVE(c, "err_report", 0);
  CLASS_ATTR_SELFSAVE(c, "err_report", 0);

  class_register(CLASS_BOX, c);
  system_class = c;
}

//******************************************************************************
//  Constructor
//
void* system_new(t_symbol* sym, long argc, t_atom* argv) {

  t_system* x = NULL;

  // Allocate the object and test
  x = (t_system*)object_alloc(system_class);
  MY_ASSERT(!x, NULL, "Allocation failed.");

  // Set inlets, outlets and proxies
  x->outl_return = intout((t_object*)x);            // Outlet 1: 0 or 1 for failure or success (int)
  x->outl_any    = outlet_new((t_object*)x, NULL);  // Outlet 0: messages

  // Set the current directory to the patcher's directory
  system_cd_patcher(x);

  POST("New object:  directory:  %s", x->dir_cur);

  return(x);
}

//******************************************************************************
//  Destructor
//
void system_free(t_system* x) {

  POST("Object freed");
}

//******************************************************************************
//  Assist
//
void system_assist(t_system* x, void* b, long msg, long arg, char* str) {

  switch (msg) {
  case ASSIST_INLET:
    switch (arg) {
    case 0: sprintf(str, "Inlet 0: All purpose (any)"); break;
    default: break;
    }
    break;
  case ASSIST_OUTLET:
    switch (arg) {
    case 0: sprintf(str, "Outlet 0: All purpose (any)"); break;
    case 1: sprintf(str, "Outlet 1: Failure or success (0 / 1)"); break;
    default: break;
    }
    break;
  }
}

//******************************************************************************
//  Output the current directory
//
void system_bang(t_system* x) {

  TRACE("system_bang");

  outlet_anything(x->outl_any, gensym(x->dir_cur), 0, NULL);
}

//******************************************************************************
//  Post the current directory in a variety of styles
//
void system_post(t_system* x) {

  TRACE("system_post");

  char str_tmp[MAX_PATH_CHARS];
  char str_cur[MAX_PATH_CHARS];

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_MAX, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_MAX / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_COLON, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_COLON / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_SLASH, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_SLASH / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE_WIN, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_NATIVE_WIN / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_IGNORE);
  POST("PATH_STYLE_MAX / PATH_TYPE_IGNORE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_RELATIVE);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_RELATIVE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_BOOT);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_BOOT:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_C74);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_C74:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_PATH);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_PATH:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_DESKTOP);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_DESKTOP:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE_WIN, PATH_TYPE_TILDE);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_TILDE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE_WIN, PATH_TYPE_TEMPFOLDER);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_TEMPFOLDER:  %s", str_cur);

  POST("Current directory:  %s", x->dir_cur);
}

//******************************************************************************
//  Change the current directory to the patcher directory
//
void system_cd_patcher(t_system* x) {

  TRACE("system_cd_patcher");

  // Get the top patcher containing the object
  t_object* patcher = NULL;
  object_obex_lookup(x, gensym("#P"), &patcher);
  patcher = jpatcher_get_toppatcher(patcher);

  // Get the patcher's filepath, and test its existence
  t_symbol* path_sym = jpatcher_get_filepath(patcher);
  if (path_sym != gensym("")) {
    path_nameconform(path_sym->s_name, x->dir_cur, PATH_STYLE_MAX, PATH_TYPE_PATH);

  // ... otherwise use the application path
  } else {
    char path_str[MAX_PATH_CHARS];
    path_toabsolutesystempath(path_getapppath(), "", path_str);
    path_nameconform(path_str, x->dir_cur, PATH_STYLE_MAX, PATH_TYPE_PATH);
    strncat_zero(path_str, "/", MAX_PATH_CHARS);
  }

  // Remove the patcher file name
  char* iter = x->dir_cur;
  char* last_sep = x->dir_cur;
  while (*iter) { if (*iter++ == '/') { last_sep = iter; } };
  *last_sep = '\0';

  // Set the length of the path string
  x->dir_cur_len = (short)(last_sep - x->dir_cur);
}

//******************************************************************************
//  Change the current directory
//
void system_cd(t_system* x, t_symbol* sym) {

  TRACE("system_cd");

  // If the symbol is "~", change the directory to the patcher directory
  if (sym == gensym("~")) { system_cd_patcher(x); }

  // ... if the symbol is "..", change the directory to the parent directory
  else if (sym == gensym("..")) {

    // Test if the current directory is the root
    if (x->dir_cur[x->dir_cur_len - 2] == ':') { return; }

    // Backtrack to previous '/'
    char* iter = x->dir_cur + x->dir_cur_len - 2;
    while ((iter > x->dir_cur) && (*iter != '/')) { iter--; }
    *(iter + 1) = '\0';
    x->dir_cur_len = (short)(iter - x->dir_cur + 1);
  }

  // ... or else change to the path provided as an argument
  else {
    char test[MAX_PATH_CHARS + 1];
    post("cd:  >%s<", sym->s_name);

    path_nameconform(sym->s_name, test, PATH_STYLE_MAX, PATH_TYPE_ABSOLUTE);
    post("conformed:  >%s<", test);

    if (path_is_abs(x, test)) {
      post("absolute");

      // If necessary add a closing slash character
      //char* pc = test + strlen(test) - 1;
      //if (*pc != '/') { *++pc = '/'; *++pc = '\0'; }



      struct _stat file_stat;
      int res = _stat(test, &file_stat);
      if (res < 0) { post("file not found"); }
      post("directory:  %i", _S_IFDIR & file_stat.st_mode);
    }
  }
}



void system_rename(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_rename");

  char str_cur[MAX_PATH_CHARS];
  char str_new[MAX_PATH_CHARS];

  t_symbol* sym_cur = atom_getsym(argv);
  t_symbol* sym_new = atom_getsym(argv + 1);

  get_path(x, sym_cur->s_name, str_cur);
  get_path(x, sym_new->s_name, str_new);

  // Try renaming the file
  int err = 0;

#ifdef WIN_VERSION
  err = rename(str_cur, str_new);
#endif

#ifdef MAC_VERSION
  err = rename(str_cur, str_new);
#endif

  // In case of success
  if (!err) { if (x->a_verbose) { POST("Renaming:  %s  to  %s", sym_cur->s_name, sym_new->s_name); } }

  // ... otherwise post an error message
  else {
    switch (errno) {
    case ENOENT: MY_ERR("rename:  File or destination folder not found:  \"%s\"  \"%s\"",
      sym_cur->s_name, sym_new->s_name); break;
    case EEXIST: MY_ERR("rename:  Existing file with name:  \"%s\"", sym_new->s_name); break;
    case EACCES: MY_ERR("rename:  Access error:  \"%s\"", sym_new->s_name); break;
    case EINVAL: MY_ERR("rename:  Invalid characters:  \"%s\"  \"%s\"",
      sym_cur->s_name, sym_new->s_name); break;
    default: MY_ERR("rename:  Unknown error:  %i", errno); break;
  }
}

  // Send a 0 or 1 message to indicate failure or success
  outlet_int(x->outl_return, err ? 0 : 1);
}

void system_delete(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_delete");

  char str_cur[MAX_PATH_CHARS];

  t_symbol* sym_cur = atom_getsym(argv);

  get_path(x, sym_cur->s_name, str_cur);

  // Try deleting the file
  int err = 0;

#ifdef WIN_VERSION
  err = remove(str_cur);
#endif

#ifdef MAC_VERSION
  err = remove(str_cur);
#endif

  // In case of success
  if (!err) { if (x->a_verbose) { POST("Removing:  %s", sym_cur->s_name); } }

  // ... otherwise post an error message
  else {
    switch (errno) {
    case EACCES: MY_ERR("delete:  File is open or read-only:  \"%s\"",
      sym_cur->s_name); break;
    case ENOENT: MY_ERR("delete:  File not found:  \"%s\"", sym_cur->s_name); break;
    default: MY_ERR("delete:  Unknown error:  %i", errno); break;
  }
}

  // Send a 0 or 1 message to indicate failure or success
  outlet_int(x->outl_return, err ? 0 : 1);
}

void system_command(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_command");

  /*char temp[MAX_PATH_CHARS];
  strcpy(temp, atom_getsym(argv)->s_name);

  post("cmd:  %s", temp);*/

  _spawnl(_P_NOWAIT, "cmd.exe", "cmd.exe", NULL);
  //_spawnl(_P_NOWAIT, "c:\\test b\\run.cmd", "c:\\test b\\run.cmd", NULL);

  //system("c:\\test\\run.cmd");
  //system("c:\\test\" \"b\\run.cmd");

  /*char  buff[128];
  FILE* pipe = _popen("c:\\test\\run.cmd", "rt");*/

  /*while (fgets(buff, 128, pipe)) {
    post("%s", buff);
  } */

  //_pclose(pipe);
}

//******************************************************************************
//  Rename a file, using a deferred low call
//
void system_rename_d(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_rename_d");

  defer_low((t_object*)x, (method)system_rename, sym, (short)argc, argv);
}

//******************************************************************************
//  Delete a file, using a deferred low call
//
void system_delete_d(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_delete_d");

  defer_low((t_object*)x, (method)system_delete, sym, (short)argc, argv);
}

//******************************************************************************
//  Launch a shell command, using a deferred low call
//
void system_command_d(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_command_d");

  defer_low((t_object*)x, (method)system_command, sym, (short)argc, argv);
}

//******************************************************************************
//  Process any other message
//
void system_anything(t_system* x, t_symbol* sym, long argc, t_atom* argv) {

  TRACE("system_anything");

  POST("Message not recognized:  \"%s\"", sym->s_name);
}

t_bool path_is_abs(t_system* x, const char* str) {

#ifdef WIN_VERSION
  return (t_bool)strstr(str, ":/");
#endif

#ifdef MAC_VERSION
  return ((str[0] == '/') ||
    ((str[0] >= 'A') && (str[0] <= 'Z') && (strchr(str, ':'))));
#endif
}

void get_path(t_system* x, const char* str_src, char* str_dest) {

  char str_tmp[MAX_PATH_CHARS];

  if (path_is_abs(x, str_src)) {
    strncpy_zero(str_tmp, str_src, MAX_PATH_CHARS);
  } else {
    strncpy_zero(str_tmp, x->dir_cur, MAX_PATH_CHARS);
    strncat_zero(str_tmp, str_src, MAX_PATH_CHARS);
  }

  path_nameconform(str_tmp, str_dest, PATH_STYLE_NATIVE, PATH_TYPE_PATH);
}

