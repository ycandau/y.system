/**
  @file
  y.filesys - A Max 7 object to rename and delete files.
  Yves Candau

  @ingroup  myExternals  
*/

// TO DO:
//   Check existence of folders and files

/* ========  HEADER FILES  ======== */

#include <stdio.h>
#include <errno.h>

#include "ext.h"       // Header file for all objects, should always be first
#include "ext_obex.h"  // Header file for all objects, required for new style Max object

/* ========  DEFINES  ======== */

#define TRACE(...)   do { if (false) object_post((t_object *)x, "TRACE:  " __VA_ARGS__); } while (0)
#define POST(...)    do { if (true) object_post((t_object *)x, __VA_ARGS__); } while (0)
#define WARNING(...) do { object_warn ((t_object *)x, __VA_ARGS__); } while (0)
#define MY_ERR(...)	 do { if (x->a_error_report) object_error((t_object *)x, __VA_ARGS__); } while (0)
#define MY_ASSERT(_test, _ret, ...) if (_test) { object_error((t_object *)x, __VA_ARGS__); return _ret; }

/* ========  TYPEDEF AND CONST GLOBAL VARIABLES  ======== */


/* ========  STRUCTURE DECLARATION  ======== */

typedef struct _filesys 
{
  t_object ob;
  t_symbol *name;

  char dir_s[MAX_PATH_CHARS];

  void *outl_mess;    // Outlet 0: messages
  void *outl_return;  // Outlet 1: 0 or 1 for failure or success (int)

  char a_verbose;
  char a_error_report;

} t_filesys;

/* ========  FUNCTION PROTOTYPES  ======== */

void *filesys_new   (t_symbol *sym, long argc, t_atom *argv);
void  filesys_free  (t_filesys *x);
void  filesys_assist(t_filesys *x, void *b, long msg, long arg, char *str);

void filesys_cd     (t_filesys *x, t_symbol *sym);
void filesys_getdir (t_filesys *x);
void filesys_postdir(t_filesys *x);

void filesys_rename(t_filesys *x, t_symbol *sym, long argc, t_atom *argv);
void filesys_delete(t_filesys *x, t_symbol *sym, long argc, t_atom *argv);

void _defer_rename(t_filesys *x, t_symbol *sym, long argc, t_atom *argv);
void _defer_delete(t_filesys *x, t_symbol *sym, long argc, t_atom *argv);

void filesys_anything (t_filesys *x, t_symbol *sym, long argc, t_atom *argv);

void _filesys_cd_patcher(t_filesys *x);
t_bool _filesys_is_abs(t_filesys *x, const char *str);
void _filesys_get_path(t_filesys *x, const char* str_src, char *str_dest);

/* ========  GLOBAL CLASS POINTER AND STATIC VARIABLES  ======== */

void *filesys_class;

/* ========  INITIALIZATION ROUTINE  ======== */

void C74_EXPORT ext_main(void *r)
{
  t_class *c;
  
  c = class_new("y.filesys", (method)filesys_new, (method)filesys_free, 
        (long)sizeof(t_filesys), (method)NULL, A_GIMME, 0);
  
  class_addmethod(c, (method)filesys_assist, "assist", A_CANT, 0);  

  class_addmethod(c, (method)filesys_cd, "cd", A_SYM, 0);
  class_addmethod(c, (method)filesys_getdir, "getdir", 0);
  class_addmethod(c, (method)filesys_postdir, "postdir", 0);

  class_addmethod(c, (method)filesys_rename, "rename", A_GIMME, 0);
  class_addmethod(c, (method)filesys_rename, "ren", A_GIMME, 0);
  class_addmethod(c, (method)filesys_rename, "mv", A_GIMME, 0);
  class_addmethod(c, (method)filesys_delete, "delete", A_GIMME, 0);
  class_addmethod(c, (method)filesys_delete, "del", A_GIMME, 0);
  class_addmethod(c, (method)filesys_delete, "rm", A_GIMME, 0);

  class_addmethod(c, (method)filesys_anything, "anything", A_GIMME, 0);

  CLASS_ATTR_CHAR(c, "verbose", 0, t_filesys, a_verbose);
	CLASS_ATTR_STYLE_LABEL(c, "verbose", 0, "onoff", "Verbose");
  CLASS_ATTR_SAVE(c, "verbose", 0);
  CLASS_ATTR_ORDER(c, "verbose", 0, "1");

  CLASS_ATTR_CHAR(c, "err_report", 0, t_filesys, a_error_report);
	CLASS_ATTR_STYLE_LABEL(c, "err_report", 0, "onoff", "Error reporting");
  CLASS_ATTR_SAVE(c, "err_report", 0);
  CLASS_ATTR_ORDER(c, "err_report", 0, "2");

  class_register(CLASS_BOX, c);
  filesys_class = c;
}

/* ========  NEW INSTANCE ROUTINE: MAX_TEMPLATE_NEW  ======== */

void *filesys_new(t_symbol *sym, long argc, t_atom *argv)
{
  t_filesys *x = NULL;

  x = (t_filesys *)object_alloc(filesys_class);

  MY_ASSERT(!x, NULL, "Object allocation failed.");

  x->outl_return = bangout((t_object*)x);         // Outlet 1: 0 or 1 for failure or success (int)
  x->outl_mess = outlet_new((t_object*)x, NULL);  // Outlet 0: General messages

  _filesys_cd_patcher(x);

  POST("New object created:  directory:  %s", x->dir_s);

  return(x);
}

/* ========  PROCEDURE: MAX_TEMPLATE_FREE  ======== */

void filesys_free(t_filesys *x)
{
  POST("Object freed.");
}

/* ====  PROCEDURE: MAX_TEMPLATE_ASSIST  ==== */

void filesys_assist(t_filesys *x, void *b, long msg, long arg, char *str)
{
  if (msg == ASSIST_INLET) {
    switch (arg) {
    case 0: sprintf(str, "Inlet 0: All purpose (list)"); break;
    default: break; } }
  
  else if (msg == ASSIST_OUTLET) {
    switch (arg) {
    case 0: sprintf(str, "Outlet 0: All purpose messages"); break;
    case 1: sprintf(str, "Outlet 1: 0 or 1 for failure or success (int)"); break;
    default: break; } }
}

void _filesys_cd_patcher(t_filesys *x)
{
  // Get the top patcher containing the object
  t_object *patcher = NULL;
  object_obex_lookup(x, gensym("#P"), &patcher);
  patcher = jpatcher_get_toppatcher(patcher);
  
  // Initialize the current directory to the patcher filepath
  t_symbol *path = jpatcher_get_filepath(patcher);
  path_nameconform(path->s_name, x->dir_s, PATH_STYLE_MAX, PATH_TYPE_PATH);

  // Remove the patcher file name
  char *iter = x->dir_s;
  char *marker = x->dir_s;
  do { if (*iter == '/') { marker = iter; } } while (*iter++);
  *(marker + 1) = '\0';
}

t_bool _filesys_is_abs(t_filesys *x, const char *str)
{
#ifdef WIN_VERSION
  return ((str[0] >= 'A') && (str[0] <= 'Z') && (strchr(str, ':')));
#endif
  
#ifdef MAC_VERSION
  return ((str[0] == '/') ||
    ((str[0] >= 'A') && (str[0] <= 'Z') && (strchr(str, ':'))));
#endif
}

void _filesys_get_path(t_filesys *x, const char* str_src, char *str_dest)
{
  char str_tmp[MAX_PATH_CHARS];

  if (_filesys_is_abs(x, str_src)) {
    strncpy_zero(str_tmp, str_src, MAX_PATH_CHARS); }
  else {
    strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
    strncat_zero(str_tmp, str_src, MAX_PATH_CHARS); }

  path_nameconform(str_tmp, str_dest, PATH_STYLE_NATIVE, PATH_TYPE_PATH);
}

void filesys_cd(t_filesys *x, t_symbol *sym)
{
  TRACE("filesys_cd");

  // Reset to the path containing the patcher file
  if (sym == gensym("~")) { _filesys_cd_patcher(x); }

  // ... or else set the path provided as an argument
  else {
    path_nameconform(sym->s_name, x->dir_s, PATH_STYLE_MAX, PATH_TYPE_ABSOLUTE);

    // If necessary add a closing slash character
    char *iter = x->dir_s + strlen(x->dir_s) - 1;
    if (*iter != '/') { *++iter = '/'; *++iter = '\0'; } }
}

void filesys_getdir(t_filesys *x)
{
  TRACE("filesys_getdir");

  t_atom ato;
  atom_setsym(&ato, gensym(x->dir_s));
  outlet_anything(x->outl_mess, gensym("dir"), 1, &ato); 
}

void filesys_postdir(t_filesys *x)
{
  TRACE("filesys_posttdir");

  char str_tmp[MAX_PATH_CHARS];
  char str_cur[MAX_PATH_CHARS];

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_MAX, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_MAX / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_COLON, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_COLON / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_SLASH, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_SLASH / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE_WIN, PATH_TYPE_ABSOLUTE);
  POST("PATH_STYLE_NATIVE_WIN / PATH_TYPE_ABSOLUTE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_IGNORE);
  POST("PATH_STYLE_MAX / PATH_TYPE_IGNORE:  %s", str_cur);
  
  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_RELATIVE);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_RELATIVE:  %s", str_cur);
  
  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_BOOT);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_BOOT:  %s", str_cur);
  
  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_C74);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_C74:  %s", str_cur);
  
  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_PATH);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_PATH:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE, PATH_TYPE_DESKTOP);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_DESKTOP:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE_WIN, PATH_TYPE_TILDE);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_TILDE:  %s", str_cur);

  strncpy_zero(str_tmp, x->dir_s, MAX_PATH_CHARS);
  path_nameconform(str_tmp, str_cur, PATH_STYLE_NATIVE_WIN, PATH_TYPE_TEMPFOLDER);
  POST("PATH_STYLE_NATIVE / PATH_TYPE_TEMPFOLDER:  %s", str_cur);
  
  POST("Current directory:  %s", x->dir_s);
}

void filesys_rename(t_filesys *x, t_symbol *sym, long argc, t_atom *argv)
{
  TRACE("filesys_rename");

  defer_low((t_object *)x, (method)_defer_rename, sym, (short)argc, argv);
}

void _defer_rename(t_filesys *x, t_symbol *sym, long argc, t_atom *argv)
{
  TRACE("_defer_rename");

  char str_cur[MAX_PATH_CHARS];
  char str_new[MAX_PATH_CHARS];

  t_symbol *sym_cur = atom_getsym(argv);
  t_symbol *sym_new = atom_getsym(argv + 1);

  _filesys_get_path(x, sym_cur->s_name, str_cur);
  _filesys_get_path(x, sym_new->s_name, str_new);

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
    default: MY_ERR("rename:  Unknown error:  %i", errno); break; } }

  // Send a 0 or 1 message to indicate failure or success
  outlet_int(x->outl_return, err ? 0 : 1);
}

void filesys_delete(t_filesys *x, t_symbol *sym, long argc, t_atom *argv)
{
  TRACE("filesys_delete");

  defer_low((t_object *)x, (method)_defer_delete, sym, (short)argc, argv);
}

void _defer_delete(t_filesys *x, t_symbol *sym, long argc, t_atom *argv)
{
  TRACE("_defer_delete");

  char str_cur[MAX_PATH_CHARS];

  t_symbol *sym_cur = atom_getsym(argv);

  _filesys_get_path(x, sym_cur->s_name, str_cur);
  
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
    default: MY_ERR("delete:  Unknown error:  %i", errno); break; } }

  // Send a 0 or 1 message to indicate failure or success
  outlet_int(x->outl_return, err ? 0 : 1);
}

/* ====  PROCEDURE: MAX_TEMPLATE_ANYTHING  ==== */

void filesys_anything(t_filesys *x, t_symbol *sym, long argc, t_atom *argv)
{
  TRACE("filesys_anything");
  WARNING("The message \"%s\" is not recognized.", sym->s_name);
}
