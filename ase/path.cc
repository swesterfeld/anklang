// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "path.hh"
#include "platform.hh"
#include "utils.hh"
#include "inifile.hh"
#include "internal.hh"
#include <unistd.h>     // getuid
#include <sys/stat.h>   // lstat
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <algorithm>
#include <cstring>      // strchr
#include <glob.h>       // glob
#include <mutex>

#if __has_include(<linux/fs.h>)
#include <linux/fs.h>
#endif

#include <filesystem>
namespace Fs = std::filesystem;


#define IS_DIRSEP(c)                    ((c) == ASE_DIRSEP || (c) == ASE_DIRSEP2)
#define IS_SEARCHPATH_SEPARATOR(c)      ((c) == ASE_SEARCHPATH_SEPARATOR || (c) == ';') // make ';' work under Windows and Unix
#define UPPER_ALPHA(L)                  (L >= 'A' && L <= 'Z')
#define LOWER_ALPHA(L)                  (L >= 'a' && L <= 'z')
#define ISALPHA(L)                      (LOWER_ALPHA (L) || UPPER_ALPHA (L))

// == CxxPasswd =
namespace { // Anon
struct CxxPasswd {
  String pw_name, pw_passwd, pw_gecos, pw_dir, pw_shell;
  uid_t pw_uid = -1;
  gid_t pw_gid = -1;
  CxxPasswd (std::string username = "");
};
} // Anon

namespace Ase {

namespace Path {

[[maybe_unused]] static bool
startswith_dosdrive (const char *p)
{
  return ASE_DOS_PATHS && p && ISALPHA (p[0]) && p[1] == ':';
}

static bool
startswith_dosdrive (const String &s)
{
  return ASE_DOS_PATHS && s.size() >= 2 && ISALPHA (s[0]) && s[1] == ':';
}

/// Retrieve the directory part of the filename `path`.
String
dirname (const String &path)
{
  const String dir = Fs::path (path).parent_path();
  return dir.empty() ? "." : dir;
}

/// Strips all directory components from @a path and returns the resulting file name.
String
basename (const String &path)
{
  const char *base = strrchr (path.c_str(), ASE_DIRSEP);
  if (ASE_DIRSEP2 != ASE_DIRSEP)
    {
      const char *base2 = strrchr (path.c_str(), ASE_DIRSEP2);
      if (base2 > base || !base)
        base = base2;
    }
  if (base)
    return base + 1;
  if (startswith_dosdrive (path))
    return path.substr (2);
  return path;
}

/// Convert `path` to normal form.
String
normalize (const String &path)
{
  return Fs::path (path).lexically_normal();
}

/// Resolve links and directory references in @a path and provide a canonicalized absolute pathname.
String
realpath (const String &path)
{
  char *const cpath = ::realpath (path.c_str(), NULL);
  if (cpath)
    {
      const String result = cpath;
      free (cpath);
      errno = 0;
      return result;
    }
  // error case
  return path;
}

/// Append trailing slash to `path`, unless it's present.
String
dir_terminate (const String &path)
{
  if (path.empty() || !IS_DIRSEP (path.back()))
    return path + ASE_DIRSEP;
  return path;
}

/// Strip trailing directory terminators.
String
strip_slashes (const String &path)
{
  String s = path;
  while (s.size() > 1 && IS_DIRSEP (s.back()))
    s.resize (s.size() - 1);
  return s;
}

/**
 * @param path  a filename path
 * @param incwd optional current working directory
 *
 * Complete @a path to become an absolute file path. If neccessary, @a incwd or
 * the real current working directory is prepended.
 */
String
abspath (const String &path, const String &incwd)
{
  if (isabs (path))
    return path;
  if (!incwd.empty())
    return abspath (join (incwd, path), "");
  String pcwd = program_cwd();
  if (!pcwd.empty())
    return join (pcwd, path);
  return join (cwd(), path);
}

/// Return wether @a path is an absolute pathname.
bool
isabs (const String &path)
{
  return_unless (path.size(), false);
  if (IS_DIRSEP (path[0]))
    return true;
  if (startswith_dosdrive (path) && IS_DIRSEP (path[2]))
    return true;
  return false;
}

/// Return wether @a path is an absolute pathname which identifies the root directory.
bool
isroot (const String &path, bool dos_drives)
{
  const char *c = path.data();
  // skip drive letter
  if (dos_drives &&
      ((c[0] >= 'A' && c[0] <= 'Z') ||
       (c[0] >= 'a' && c[0] <= 'z')) &&
      c[1] == ':')
    c += 2; // skip drive letter like "C:"
  // path MUST begin with a slash
  if (!IS_DIRSEP (c[0]))
    return false;
  // path MAY contain "./" or more slashes
  while (IS_DIRSEP (c[0]) || (c[1] == '.' && IS_DIRSEP (c[1])))
    c += 1 + (c[1] == '.');                     // skip slash and possibly leading dot
  // path MUST NOT contain other entries
  return c[0] == 0;
}

/// Return wether @a path is pointing to a directory component.
bool
isdirname (const String &path)
{
  uint l = path.size();
  if (path == "." || path == "..")
    return true;
  if (l >= 1 && IS_DIRSEP (path[l-1]))
    return true;
  if (l >= 2 && IS_DIRSEP (path[l-2]) && path[l-1] == '.')
    return true;
  if (l >= 3 && IS_DIRSEP (path[l-3]) && path[l-2] == '.' && path[l-1] == '.')
    return true;
  return false;
}

/// Create the directories in `dirpath` with `mode`, check errno on false returns.
bool
mkdirs (const String &dirpath, uint mode)
{
  Fs::path target = dirpath;
  if (check (target, "d"))
    return true;                // IS_DIR
  if (check (target, "e"))
    {
      errno = ENOTDIR;
      return false;             // !IS_DIR
    }
  if (target.has_relative_path() &&
      !mkdirs (target.parent_path(), mode))
    return false;               // !IS_DIR
  if (mkdir (target.native().c_str(), mode) == 0)
    return true;                // IS_DIR
  const int saved_errno = errno;
  if (check (target, "d"))
    return true;                // IS_DIR
  errno = saved_errno;
  return false;                 // !IS_DIR
}

/// Check if `descendant` belongs to the directory hierarchy under `dirpath`.
bool
dircontains (const String &dirpath, const String &descendant, String *relpath)
{
  String child = realpath (descendant);
  String dir = realpath (dirpath) + ASE_DIRSEP;
  if (0 == child.compare (0, dir.size(), dir))
    {
      if (relpath)
        *relpath = child.substr (dir.size(), String::npos);
      return true;
    }
  return false;
}

/// Recursively delete directory tree.
void
rmrf (const String &dir)
{
  std::error_code ec;
  std::filesystem::remove_all (dir, ec);
}

/// Copy a file to a new non-existing location, sets errno and returns false on error.
bool
copy_file (const String &src, const String &dest)
{
  unsigned long ficlone = 0;
#ifdef FICLONE
  ficlone = FICLONE;
#endif
  // try cloning a file, supported on XFS & BTRFS
  if (ficlone)
    {
      const int srcfd = open (src.c_str(), O_RDONLY | O_NOCTTY);
      if (srcfd >= 0)
        {
          const int dstfd = open (dest.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
          bool cloned = dstfd >= 0 ? 0 == ioctl (dstfd, ficlone, srcfd) : false; // FICLONE
          close (srcfd);
          if (dstfd >= 0)
            {
              cloned &= 0 == close (dstfd);
              if (cloned)
                return true;
              // cleanup failed cloning attempt
              unlink (dest.c_str());
            }
        }
    }
  // attempt regular file copy
  std::error_code ec = {};
  std::filesystem::copy_file (src, dest, ec); // [out] ec
  errno = ec ? ec.value() : 0;
  return !ec;
}

bool
rename (const String &src, const String &dest)
{
  std::error_code ec = {};
  std::filesystem::rename (src, dest, ec); // [out] ec
  errno = ec ? ec.value() : 0;
  return !ec;
}

/// Get a @a user's home directory, uses $HOME if no @a username is given.
String
user_home (const String &username)
{
  if (username.empty())
    {
      // $HOME gets precedence over getpwnam(3), like '~/' vs '~username/' expansion
      const char *homedir = getenv ("HOME");
      if (homedir && isabs (homedir))
        return homedir;
    }
  CxxPasswd pwn (username);
  return pwn.pw_dir;
}

/// Get the $XDG_DATA_HOME directory, see: https://specifications.freedesktop.org/basedir-spec/latest
String
data_home ()
{
  const char *var = getenv ("XDG_DATA_HOME");
  if (var && isabs (var))
    return var;
  return expand_tilde ("~/.local/share");
}

/// Get the $XDG_CONFIG_HOME directory, see: https://specifications.freedesktop.org/basedir-spec/latest
String
config_home ()
{
  const char *var = getenv ("XDG_CONFIG_HOME");
  if (var && isabs (var))
    return var;
  return expand_tilde ("~/.config");
}

/// Get the $XDG_CACHE_HOME directory, see: https://specifications.freedesktop.org/basedir-spec/latest
String
cache_home ()
{
  const char *var = getenv ("XDG_CACHE_HOME");
  if (var && isabs (var))
    return var;
  return expand_tilde ("~/.cache");
}

/// Get the $XDG_RUNTIME_DIR directory, see: https://specifications.freedesktop.org/basedir-spec/latest
String
runtime_dir ()
{
  const char *var = getenv ("XDG_RUNTIME_DIR");
  if (var && isabs (var))
    return var;
  return string_format ("/run/user/%u", getuid());
}

using StringStringM = std::map<String,String>;

static StringStringM
xdg_user_dirs()
{
  StringStringM defs = {
    { "XDG_DESKTOP_DIR",        "$HOME/Desktop" },
    { "XDG_DOWNLOAD_DIR",       "$HOME/Downloads" },
    { "XDG_TEMPLATES_DIR",      "$HOME/Templates" },
    { "XDG_PUBLICSHARE_DIR",    "$HOME/Public" },
    { "XDG_DOCUMENTS_DIR",      "$HOME/Documents" },
    { "XDG_MUSIC_DIR",          "$HOME/Music" },
    { "XDG_PICTURES_DIR",       "$HOME/Pictures" },
    { "XDG_VIDEOS_DIR",         "$HOME/Videos" },
  };
  String udirs = join (config_home(), "user-dirs.dirs"); // https://wiki.archlinux.org/title/XDG_user_directories
  String data = stringread (udirs);
  if (!data.empty())
    {
      IniFile ff { udirs, data };
      const String global = "";
      for (String key : ff.attributes (global))
        {
          const String v = ff.value_as_string (global + "." + key);
          if (!key.empty() && !v.empty())
            defs[key] = v;
        }
    }
  const String uhome = user_home();
  for (auto &it : defs)
    if (string_startswith (it.second, "$HOME/"))
      it.second = uhome + it.second.substr (5);
  if (0)
    for (const auto &pair : defs)
      printerr ("XDG: %s = %s\n", pair.first, pair.second);
  return defs;
}

String
xdg_dir (const String &xdgdir)
{
  const String udir = string_toupper (xdgdir);
  if (udir == "HOME")
    return user_home();
  if (udir == "DATA")
    return data_home();
  if (udir == "CONFIG")
    return config_home();
  if (udir == "CACHE")
    return cache_home();
  if (udir == "RUNTIME")
    return runtime_dir();
  static const StringStringM defs = xdg_user_dirs();
  const auto it = defs.find ("XDG_" + string_toupper (xdgdir) + "_DIR");
  return it != defs.end() ? it->second : "";
}

/// Get the $XDG_CONFIG_DIRS directory list, see: https://specifications.freedesktop.org/basedir-spec/latest
String
config_dirs ()
{
  const char *var = getenv ("XDG_CONFIG_DIRS");
  if (var && var[0])
    return var;
  else
    return "/etc/xdg";
}

/// Get the $XDG_DATA_DIRS directory list, see: https://specifications.freedesktop.org/basedir-spec/latest
String
data_dirs ()
{
  const char *var = getenv ("XDG_DATA_DIRS");
  if (var && var[0])
    return var;
  else
    return "/usr/local/share:/usr/share";
}

static String
access_config_names (const String *newval)
{
  static std::mutex mutex;
  static std::lock_guard<std::mutex> locker (mutex);
  static String cfg_names;
  if (newval)
    cfg_names = *newval;
  if (cfg_names.empty())
    {
      String names = Path::basename (program_alias());
      if (program_alias() != names)
        names = searchpath_join (names, program_alias());
      return names;
    }
  else
    return cfg_names;
}

/// Get config names as set with config_names(), if unset defaults to program_alias().
String
config_names ()
{
  return access_config_names (NULL);
}

/// Set a colon separated list of names for this application to find configuration settings and files.
void
config_names (const String &names)
{
  access_config_names (&names);
}

StringPair
split_extension (const std::string &filepath, const bool lastdot)
{
  const char *const fullpath = filepath.c_str();
  const char *const slash1 = strrchr (fullpath, '/'), *const slash2 = strrchr (fullpath, '\\');
  const char *const slash = slash2 > slash1 ? slash2 : slash1;
  const char *const dot = lastdot ? strrchr (slash ? slash : fullpath, '.') : strchr (slash ? slash : fullpath, '.');
  if (dot)
    return std::make_pair (filepath.substr (0, dot - fullpath), filepath.substr (dot - fullpath));
  return std::make_pair (filepath, "");
}

/// Expand a "~/" or "~user/" @a path which refers to user home directories.
String
expand_tilde (const String &path)
{
  if (path[0] != '~')
    return path;
  const size_t dir1 = path.find (ASE_DIRSEP);
  const size_t dir2 = ASE_DIRSEP == ASE_DIRSEP2 ? String::npos : path.find (ASE_DIRSEP2);
  const size_t dir = std::min (dir1, dir2);
  String username;
  if (dir != String::npos)
    username = path.substr (1, dir - 1);
  else
    username = path.substr (1);
  const String userhome = user_home (username);
  if (userhome.empty())
    return path;
  return join (userhome, dir == String::npos ? "" : path.substr (dir));
}

String
skip_root (const String &path)
{
  return_unless (!path.empty(), path);
#ifdef  _WIN32          // strip C:/
  if (path.size() >= 3 &&
      ( (path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z') ) &&
      path[1] == ':' && IS_DIRSEP (path[2]))
    return path.substr (3);
#endif
#ifdef  _WIN32          // strip //server/
  if (path.size() >= 3 && IS_DIRSEP (path[0]) && IS_DIRSEP (path[1]) && !IS_DIRSEP (path[2]))
    {
      const char *p = &path[3];
      while (*p && !IS_DIRSEP (*p))
        p++;
      if (IS_DIRSEP (*p))
        return path.substr (++p - &path[0]);
    }
#endif
  const char *p = &path[0];
  while (*p && IS_DIRSEP (*p))
    p++;
  return path.substr (p - &path[0]);
}

/// Retrieve the on-disk size in bytes of `path`.
size_t
file_size (const String &path)
{
  std::error_code ec = {};
  size_t size = std::filesystem::file_size (path, ec); // [out] ec
  return ec ? 0 : size;
}

static int
errno_check_file (const char *file_name, const char *mode)
{
  uint access_mask = 0, nac = 0;

  if (strchr (mode, 'e'))       // exists
    nac++, access_mask |= F_OK;
  if (strchr (mode, 'r'))       // readable
    nac++, access_mask |= R_OK;
  if (strchr (mode, 'w'))       // writable
    nac++, access_mask |= W_OK;
  bool check_exec = strchr (mode, 'x') != NULL;
  if (check_exec)               // executable
    nac++, access_mask |= X_OK;

  /* on some POSIX systems, X_OK may succeed for root without any
   * executable bits set, so we also check via stat() below.
   */
  if (nac && access (file_name, access_mask) < 0)
    return -errno;

  const bool check_size0 = strchr (mode, 'z') != NULL;  // zero size
  const bool check_size1 = strchr (mode, 's') != NULL;  // non-zero size
  const bool check_file = strchr (mode, 'f') != NULL;   // open as file
  const bool check_dir  = strchr (mode, 'd') != NULL;   // open as directory
  const bool check_link = strchr (mode, 'L') != NULL || strchr (mode, 'h') != NULL;   // open as link
  const bool check_char = strchr (mode, 'c') != NULL;   // open as character device
  const bool check_block = strchr (mode, 'b') != NULL;  // open as block device
  const bool check_pipe = strchr (mode, 'p') != NULL;   // open as pipe
  const bool check_socket = strchr (mode, 'S') != NULL; // open as socket

  if (check_exec || check_size0 || check_size1 || check_file || check_dir ||
      check_link || check_char || check_block || check_pipe || check_socket)
    {
      struct stat st;

      if (check_link)
        {
          if (lstat (file_name, &st) < 0)
            return -errno;
        }
      else if (stat (file_name, &st) < 0)
        return -errno;

      if (0)
        printerr ("file-check(\"%s\",\"%s\"): %u %s%s%s%s%s%s%s\n",
                  file_name, mode,
                  st.st_size,
                  S_ISREG (st.st_mode) ? "f" : "",
                  S_ISDIR (st.st_mode) ? "d" : "",
                  S_ISLNK (st.st_mode) ? "L" : "",
                  S_ISCHR (st.st_mode) ? "c" : "",
                  S_ISBLK (st.st_mode) ? "b" : "",
                  S_ISFIFO (st.st_mode) ? "p" : "",
                  S_ISSOCK (st.st_mode) ? "S" : "");

      if (check_size0 && st.st_size != 0)
        return -EFBIG;
      if (check_size1 && st.st_size == 0)
        return -ENODATA;
      if (S_ISDIR (st.st_mode) && (check_file || check_link || check_char || check_block || check_pipe))
        return -EISDIR;
      if (check_file && !S_ISREG (st.st_mode))
        return -EINVAL;
      if (check_dir && !S_ISDIR (st.st_mode))
        return -ENOTDIR;
      if (check_link && !S_ISLNK (st.st_mode))
        return -EINVAL;
      if (check_char && !S_ISCHR (st.st_mode))
        return -ENODEV;
      if (check_block && !S_ISBLK (st.st_mode))
        return -ENOTBLK;
      if (check_pipe && !S_ISFIFO (st.st_mode))
        return -ENXIO;
      if (check_socket && !S_ISSOCK (st.st_mode))
        return -ENOTSOCK;
      if (check_exec && !(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
        return -EACCES; // for root executable, any +x bit is good enough
    }

  return 0;
}

/**
 * @param file  possibly relative filename
 * @param mode  feature string
 * @return      true if @a file adhears to @a mode
 *
 * Perform various checks on @a file and return whether all
 * checks passed. On failure, errno is set appropriately, and
 * FALSE is returned. Available features to be checked for are:
 * @li @c e - @a file must exist
 * @li @c r - @a file must be readable
 * @li @c w - @a file must be writable
 * @li @c x - @a file must be executable
 * @li @c f - @a file must be a regular file
 * @li @c d - @a file must be a directory
 * @li @c l - @a file must be a symbolic link
 * @li @c c - @a file must be a character device
 * @li @c b - @a file must be a block device
 * @li @c p - @a file must be a named pipe
 * @li @c s - @a file must be a socket.
 */
bool
check (const String &file, const String &mode)
{
  const int err = file.size() && mode.size() ? errno_check_file (file.c_str(), mode.c_str()) : -ENOENT;
  errno = err < 0 ? -err : 0;
  return errno == 0;
}

/**
 * @param file1  possibly relative filename
 * @param file2  possibly relative filename
 * @return       TRUE if @a file1 and @a file2 are equal
 *
 * Check whether @a file1 and @a file2 are pointing to the same inode
 * in the same file system on the same device.
 */
bool
equals (const String &file1, const String &file2)
{
  if (!file1.size() || !file2.size())
    return file1.size() == file2.size();
  struct stat st1 = { 0, }, st2 = { 0, };
  int err1 = 0, err2 = 0;
  errno = 0;
  if (stat (file1.c_str(), &st1) < 0 && stat (file1.c_str(), &st1) < 0)
    err1 = errno;
  errno = 0;
  if (stat (file2.c_str(), &st2) < 0 && stat (file2.c_str(), &st2) < 0)
    err2 = errno;
  if (err1 || err2)
    return false;
  return (st1.st_dev  == st2.st_dev &&
          st1.st_ino  == st2.st_ino &&
          st1.st_rdev == st2.st_rdev);
}

/// Return the current working directoy, including symlinks used in $PWD if available.
String
cwd ()
{
#ifdef  _GNU_SOURCE
  {
    char *dir = get_current_dir_name();
    if (dir)
      {
        const String result = dir;
        free (dir);
        return result;
      }
  }
#endif
  size_t size = 512;
  do
    {
      char *buf = (char*) malloc (size);
      if (!buf)
        break;
      const char *const dir = getcwd (buf, size);
      if (dir)
        {
          const String result = dir;
          free (buf);
          return result;
        }
      free (buf);
      size *= 2;
    }
  while (errno == ERANGE);
  // system must be in a bad shape if we get here...
  return "./";
}

StringS
searchpath_split (const String &searchpath)
{
  StringS sv;
  uint i, l = 0;
  for (i = 0; i < searchpath.size(); i++)
    if (IS_SEARCHPATH_SEPARATOR (searchpath[i]))
      {
        if (i > l)
          sv.push_back (searchpath.substr (l, i - l));
        l = i + 1;
      }
  if (i > l)
    sv.push_back (searchpath.substr (l, i - l));
  return sv;
}

/// Check if @a searchpath contains @a element, a trailing slash searches for directories.
bool
searchpath_contains (const String &searchpath, const String &element)
{
  const bool dirsearch = element.size() > 0 && IS_DIRSEP (element[element.size() - 1]);
  const String needle = dirsearch && element.size() > 1 ? element.substr (0, element.size() - 1) : element; // strip trailing slash
  size_t pos = searchpath.find (needle);
  while (pos != String::npos)
    {
      size_t end = pos + needle.size();
      if (pos == 0 || IS_SEARCHPATH_SEPARATOR (searchpath[pos - 1]))
        {
          if (dirsearch && IS_DIRSEP (searchpath[end]))
            end++; // skip trailing slash in searchpath segment
          if (searchpath[end] == 0 || IS_SEARCHPATH_SEPARATOR (searchpath[end]))
            return true;
        }
      pos = searchpath.find (needle, end);
    }
  return false;
}

/// Find the first @a file in @a searchpath which matches @a mode (see check()).
String
searchpath_find (const String &searchpath, const String &file, const String &mode)
{
  if (isabs (file))
    return check (file, mode) ? file : "";
  StringS sv = searchpath_split (searchpath);
  for (size_t i = 0; i < sv.size(); i++)
    if (check (join (sv[i], file), mode))
      return join (sv[i], file);
  return "";
}

/// Find all @a searchpath entries matching @a mode (see check()).
StringS
searchpath_list (const String &searchpath, const String &mode)
{
  StringS v;
  for (const auto &file : searchpath_split (searchpath))
    if (check (file, mode))
      v.push_back (file);
  return v;
}

/// Construct `head + joiner + tail` avoiding duplicates of `joiner`.
String
join_with (const String &head, char joiner, const String &tail)
{
  return_unless (head.size(), tail);
  return_unless (tail.size(), head);
  if (head.back() == joiner)
    {
      if (tail[0] == joiner)
        return head + tail.substr (1);
      return head + tail;
    }
  if (tail[0] == joiner)
    return head + tail;
  return head + joiner + tail;
}

/// Yield a new searchpath by combining each element of @a searchpath with each element of @a postfixes.
String
searchpath_multiply (const String &searchpath, const String &postfixes)
{
  String newpath;
  for (const auto &e : searchpath_split (searchpath))
    for (const auto &p : searchpath_split (postfixes))
      newpath = join_with (newpath, ASE_SEARCHPATH_SEPARATOR, join (e, p));
  return newpath;
}

String
searchpath_join (const StringS &string_vector)
{
  const char searchsep[2] = { ASE_SEARCHPATH_SEPARATOR, 0 };
  return string_join (searchsep, string_vector);
}

String
vpath_find (const String &file, const String &mode)
{
  String result = searchpath_find (".", file, mode);
  if (!result.empty())
    return result;
  const char *vpath = getenv ("VPATH");
  if (vpath)
    {
      result = searchpath_find (vpath, file, mode);
      if (!result.empty())
        return result;
    }
  return file;
}

/// Create list with directories and filenames matching `pathpattern` with shell wildcards.
void
glob (const String &pathpattern, StringS &dirs, StringS &files)
{
  glob_t iglob = { 0, };
  const int ir = ::glob (pathpattern.c_str(), GLOB_TILDE | GLOB_MARK, nullptr, &iglob);
  if (ir != 0)
    return;
  for (size_t i = 0; i < iglob.gl_pathc; i++) {
    const char *const p = iglob.gl_pathv[i];
    size_t l = strlen (p);
    if (IS_DIRSEP (p[l-1]))
      dirs.push_back (p);
    else
      files.push_back (p);
  }
  globfree (&iglob);
}

/// Create list with filenames matching `pathpattern` with shell wildcards.
void
glob (const String &pathpattern, StringS &matches)
{
  glob_t iglob = { 0, };
  const int ir = ::glob (pathpattern.c_str(), GLOB_TILDE, nullptr, &iglob);
  if (ir != 0)
    return;
  for (size_t i = 0; i < iglob.gl_pathc; i++)
    matches.push_back (iglob.gl_pathv[i]);
  globfree (&iglob);
}

/// Recursively match files with glob `pattern` under `basedir`.
void
rglob (const String &basedir, const String &pattern, StringS &matches)
{
  glob_t iglob = { 0, };
  const int ir = ::glob (basedir.c_str(), GLOB_TILDE_CHECK | GLOB_NOSORT | GLOB_MARK | GLOB_ONLYDIR, nullptr, &iglob);
  if (ir != 0)
    return;
  for (size_t i = 0; i < iglob.gl_pathc; i++)
    {
      std::string subdir = iglob.gl_pathv[i];
      if (subdir[subdir.size()-1] != ASE_DIRSEP && subdir[subdir.size()-1] != ASE_DIRSEP2)
        continue;
      rglob (subdir + "*", pattern, matches);
      glob_t jglob = { 0, };
      const int jr = ::glob ((subdir + pattern).c_str(), GLOB_NOSORT, nullptr, &jglob);
      if (jr != 0)
        continue;
      for (size_t j = 0; j < jglob.gl_pathc; j++)
        matches.push_back (jglob.gl_pathv[j]);
      globfree (&jglob);
    }
  globfree (&iglob);
}

/// Convert all `pathnames` via realpath() and eliminate duplicates.
void
unique_realpaths (StringS &pathnames)
{
  size_t j = 0;
  for (ssize_t i = 0; i < pathnames.size(); i++) {
    char *rpath = ::realpath (pathnames[i].c_str(), nullptr);
    if (rpath) {
      pathnames[j++] = rpath;
      free (rpath);
    }
  }
  pathnames.resize (j);
  strings_version_sort (&pathnames);
  pathnames.erase (std::unique (pathnames.begin(), pathnames.end()), pathnames.end());
}

/// Remove extra slashes, './' and '../' from `abspath_expression`.
String
simplify_abspath (const std::string &abspath_expression)
{
  std::vector<std::string> dirs = Ase::string_split (abspath_expression, "/");
  for (ssize_t i = 0; i < ssize_t (dirs.size()); i++)
    if (dirs[i].empty() || dirs[i] == ".")
      dirs.erase (dirs.begin() + i--);
    else if (dirs[i] == "..")
      {
        dirs.erase (dirs.begin() + i--);
        if (i >= 0)
          dirs.erase (dirs.begin() + i--);
      }
  return "/" + string_join ("/", dirs);
}

static char* // return malloc()-ed buffer containing a full read of FILE
file_memread (FILE *stream, size_t *lengthp, ssize_t maxlength)
{
  size_t sz = maxlength <= 0 || maxlength > 1048576 ? 1048576 : maxlength;
  char *buffer = (char*) malloc (sz);
  if (!buffer)
    return NULL;
  char *current = buffer;
  errno = 0;
  while (!feof (stream))
    {
      ssize_t bytes = fread (current, 1, sz - (current - buffer), stream);
      if (bytes <= 0 && ferror (stream) && errno != EAGAIN)
        {
          current = buffer; // error/0-data
          break;
        }
      current += bytes;
      if (maxlength >= 0 && current - buffer >= maxlength)
        {
          current = buffer + maxlength; // shorten if needed
          break;
        }
      if (current == buffer + sz)
        {
          bytes = current - buffer;
          sz *= 2;
          char *newstring = (char*) realloc (buffer, sz);
          if (!newstring)
            {
              current = buffer; // error/0-data
              break;
            }
          buffer = newstring;
          current = buffer + bytes;
        }
    }
  int savederr = errno;
  *lengthp = current - buffer;
  if (!*lengthp)
    {
      free (buffer);
      buffer = NULL;
    }
  errno = savederr;
  return buffer;
}

char*
memread (const String &filename, size_t *lengthp, ssize_t maxlength)
{
  FILE *file = fopen (filename.c_str(), "r");
  if (!file)
    {
      *lengthp = 0;
      return strdup ("");
    }
  char *contents = file_memread (file, lengthp, maxlength);
  int savederr = errno;
  fclose (file);
  contents = (char*) realloc (contents, *lengthp);
  errno = savederr;
  return contents;
}

void
memfree (char *memread_mem)
{
  if (memread_mem)
    free (memread_mem);
}

bool
memwrite (const String &filename, size_t len, const uint8 *bytes, bool append)
{
  FILE *file = fopen (filename.c_str(), append ? "a" : "w");
  if (!file)
    return false;
  const size_t nbytes = fwrite (bytes, 1, len, file);
  bool success = ferror (file) == 0 && nbytes == len;
  success = fclose (file) == 0 && success;
  if (!success)
    unlink (filename.c_str());
  return success;
}

// Read `filename` into a std::string, check `errno` for empty returns.
String
stringread (const String &filename, ssize_t maxlength)
{
  String s;
  size_t length = 0;
  errno = 0;
  char *data = memread (filename, &length, maxlength);
  if (data)
    {
      s = String (data, length);
      memfree (data);
      errno = 0;
    }
  return s;
}

// Write `data` into `filename`, check `errno` for false returns.
bool
stringwrite (const String &filename, const String &data, bool mkdirs_)
{
  if (mkdirs_)
    mkdirs (dirname (filename), 0750);
  return memwrite (filename, data.size(), (const uint8*) data.data());
}

bool
stringappend (const String &filename, const String &data, bool mkdirs_)
{
  if (mkdirs_)
    mkdirs (dirname (filename), 0750);
  return memwrite (filename, data.size(), (const uint8*) data.data(), true);
}

} // Path
} // Ase

#include <pwd.h>        // getpwuid

// == CxxPasswd =
namespace { // Anon
CxxPasswd::CxxPasswd (std::string username) :
  pw_uid (-1), pw_gid (-1)
{
  const int strbuf_size = 5 * 1024;
  char strbuf[strbuf_size + 256]; // work around Darwin getpwnam_r overwriting buffer boundaries
  struct passwd pwnambuf, *p = NULL;
  if (username.empty())
    {
      int ret = 0;
      errno = 0;
      do
        {
          if (1) // HAVE_GETPWUID_R
            ret = getpwuid_r (getuid(), &pwnambuf, strbuf, strbuf_size, &p);
          else   // HAVE_GETPWUID
            p = getpwuid (getuid());
        }
      while ((ret != 0 || p == NULL) && errno == EINTR);
      if (ret != 0)
        p = NULL;
    }
  else // !username.empty()
    {
      int ret = 0;
      errno = 0;
      do
        ret = getpwnam_r (username.c_str(), &pwnambuf, strbuf, strbuf_size, &p);
      while ((ret != 0 || p == NULL) && errno == EINTR);
      if (ret != 0)
        p = NULL;
    }
  if (p)
    {
      pw_name = p->pw_name;
      pw_passwd = p->pw_passwd;
      pw_uid = p->pw_uid;
      pw_gid = p->pw_gid;
      pw_gecos = p->pw_gecos;
      pw_dir = p->pw_dir;
      pw_shell = p->pw_shell;
    }
}
} // Anon

// == Testing ==
#include "testing.hh"
#include "internal.hh"

namespace { // Anon
using namespace Ase;

TEST_INTEGRITY (path_tests);
static void
path_tests()
{
  String p, s;
  // Path::join
  s = Path::join ("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f");
#if ASE_DIRSEP == '/'
  p = "0/1/2/3/4/5/6/7/8/9/a/b/c/d/e/f";
#else
  p = "0\\1\\2\\3\\4\\5\\6\\7\\8\\9\\a\\b\\c\\d\\e\\f";
#endif
  TCMP (s, ==, p);
  // Path::searchpath_join
  s = Path::searchpath_join ("0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f");
#if ASE_SEARCHPATH_SEPARATOR == ';'
  p = "0;1;2;3;4;5;6;7;8;9;a;b;c;d;e;f";
#else
  p = "0:1:2:3:4:5:6:7:8:9:a:b:c:d:e:f";
#endif
  TCMP (s, ==, p);
  // Path
  bool b = Path::isabs (p);
  TCMP (b, ==, false);
  const char dirsep[2] = { ASE_DIRSEP, 0 };
#if ASE_DIRSEP == '/'
  s = Path::join (dirsep, s);
#else
  s = Path::join ("C:\\", s);
#endif
  b = Path::isabs (s);
  TCMP (b, ==, true);
  s = Path::skip_root (s);
  TCMP (s, ==, p);
  // TASSERT (Path::dir_separator == "/" || Path::dir_separator == "\\");
  TASSERT (ASE_SEARCHPATH_SEPARATOR == ':' || ASE_SEARCHPATH_SEPARATOR == ';');
  TCMP (Path::basename ("simple"), ==, "simple");
  TCMP (Path::basename ("skipthis" + String (dirsep) + "file"), ==, "file");
  TCMP (Path::basename (String (dirsep) + "skipthis" + String (dirsep) + "file"), ==, "file");
  TCMP (Path::dirname ("file"), ==, ".");
  TCMP (Path::dirname ("dir" + String (dirsep)), ==, "dir");
  TCMP (Path::dirname ("dir" + String (dirsep) + "file"), ==, "dir");
  TCMP (Path::cwd(), !=, "");
  TCMP (Path::check (Path::join (Path::cwd(), "..", Path::basename (Path::cwd())), "rd"), ==, true); // ../. should be a readable directory
  TASSERT (Path::isroot ("/") == true);
  TASSERT (Path::isroot ("//") == true);
  TASSERT (Path::isroot ("//////////") == true);
  TASSERT (Path::isroot ("/.") == true);
  TASSERT (Path::isroot ("./") == false);
  TASSERT (Path::isroot (".////") == false);
  TASSERT (Path::isroot ("/./") == true);
  TASSERT (Path::isroot ("/./././././") == true);
  TASSERT (Path::isroot ("/././././.") == true);
  TASSERT (Path::isroot ("///././././//.///") == true);
  TASSERT (Path::isroot ("///././././//.///.") == true);
  TASSERT (Path::isroot ("abc") == false);
  TASSERT (Path::isroot ("C:/", true) == true);
  TASSERT (Path::isroot ("C:/.", true) == true);
  TASSERT (Path::isroot ("8:/", true) == false);
  TASSERT (Path::isroot ("8:/..", true) == false);
  TASSERT (Path::isroot ("C:/D", true) == false);
  TCMP (Path::isdirname (""), ==, false);
  TCMP (Path::isdirname ("foo"), ==, false);
  TCMP (Path::isdirname ("foo/"), ==, true);
  TCMP (Path::isdirname ("/foo"), ==, false);
  TCMP (Path::isdirname ("foo/."), ==, true);
  TCMP (Path::isdirname ("foo/.."), ==, true);
  TCMP (Path::isdirname ("foo/..."), ==, false);
  TCMP (Path::isdirname ("foo/..../"), ==, true);
  TCMP (Path::isdirname ("/."), ==, true);
  TCMP (Path::isdirname ("/.."), ==, true);
  TCMP (Path::isdirname ("/"), ==, true);
  TCMP (Path::isdirname ("."), ==, true);
  TCMP (Path::isdirname (".."), ==, true);
  TCMP (Path::expand_tilde (""), ==, "");
  const char *env_home = getenv ("HOME");
  if (env_home)
    TCMP (Path::expand_tilde ("~"), ==, env_home);
  const char *env_logname = getenv ("LOGNAME");
  if (env_home && env_logname)
    TCMP (Path::expand_tilde ("~" + String (env_logname)), ==, env_home);
  TCMP (Path::expand_tilde ("~:unknown/"), ==, "~:unknown/");
  TCMP (Path::searchpath_multiply ("/:/tmp", "foo:bar"), ==, "/foo:/bar:/tmp/foo:/tmp/bar");
  const String abs_basedir = Path::abspath (anklang_runpath (RPath::PREFIXDIR));
  TCMP (Path::searchpath_list ("/:" + abs_basedir, "e"), ==, StringS ({ "/", abs_basedir }));
  TCMP (Path::searchpath_contains ("/foo/:/bar", "/"), ==, false);
  TCMP (Path::searchpath_contains ("/foo/:/bar", "/foo"), ==, false); // false because "/foo" is file search
  TCMP (Path::searchpath_contains ("/foo/:/bar", "/foo/"), ==, true); // true because "/foo/" is dir search
  TCMP (Path::searchpath_contains ("/foo/:/bar", "/bar"), ==, true); // file search matches /bar
  TCMP (Path::searchpath_contains ("/foo/:/bar", "/bar/"), ==, true); // dir search matches /bar
  TCMP (Path::skip_root ("foo/"), ==, "foo/");
  TCMP (Path::skip_root ("/foo/"), ==, "foo/");
  TCMP (Path::skip_root ("///foo/"), ==, "foo/");
#if 0
  TCMP (Path::check ("/tmp/empty", "z"), ==, true);
  TCMP (Path::check ("/tmp/empty", "s"), ==, false);
#endif
  TCMP (Path::check ("/etc/os-release", "s"), ==, true);
  TCMP (Path::check ("/etc/os-release", "z"), ==, false);
#ifdef  _WIN32
  TCMP (Path::skip_root ("//foo/."), ==, ".");
  TCMP (Path::skip_root ("C:/foo/."), ==, "foo/.");
  TCMP (Path::skip_root ("\\\\foo\\."), ==, ".");
  TCMP (Path::skip_root ("C:\\foo\\."), ==, "foo\\.");
#else
  TCMP (Path::skip_root ("//foo/."), ==, "foo/.");
  TCMP (Path::skip_root ("C:/foo/."), ==, "C:/foo/.");
#endif
}

} // Anon
