// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#pragma once

#include <ase/strings.hh>

namespace Ase {

/// Write a string_format() message to the log file (or possibly stderr), using the POSIX/C locale.
template<class... A> void loginf    (const char *format, const A &...args) ASE_PRINTF (1, 0);

/// Format and send a log message to the user, stderr and log file, using the POSIX/C locale.
template<class... A> void logerr    (const String &dept, const char *format, const A &...args) ASE_PRINTF (2, 0);

/// Open log file.
void                      log_setup (bool inf2stderr, bool log2file);

// == Impl ==
void logmsg (const char c, const String &dept, const String &msg);
template<class... A> void
loginf (const char *format, const A &...args)
{
  logmsg ('I', "", string_format (format, args...).c_str());
}
template<class... A> void
logerr (const String &dept, const char *format, const A &...args)
{
  logmsg ('E', dept, string_format (format, args...).c_str());
}

} // Ase
