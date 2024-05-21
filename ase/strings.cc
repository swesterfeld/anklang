// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "strings.hh"
#include "internal.hh"
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <libintl.h>
#include <iconv.h>
#include <errno.h>
#include <algorithm>

#include <glib.h>       // g_unichar_*

namespace Ase {

namespace Unicode {
extern inline unichar   tolower (unichar uc)    { return g_unichar_tolower (uc); }
extern inline unichar   toupper (unichar uc)    { return g_unichar_toupper (uc); }
extern inline unichar   totitle (unichar uc)    { return g_unichar_totitle (uc); }
} // Unicode

// === String ===
/// Reproduce a string @a s for @a count times.
String
string_multiply (const String &s,
                 uint64        count)
{
  if (count == 1)
    return s;
  else if (count & 1)
    {
      String tmp = string_multiply (s, count - 1);
      tmp += s;
      return tmp;
    }
  else if (count)
    {
      String tmp = string_multiply (s, count / 2);
      tmp += tmp;
      return tmp;
    }
  else
    return "";
}

/// Force lower case, alphanumerics + underscore and non-digit start.
String
string_to_identifier (const String &input)
{
  static const String validset = ASE_STRING_SET_LOWER_ALNUM "_";
  String ident = string_tolower (input);
  ident = string_canonify (ident, validset, "_");
  if (!ident.empty() && ident[0] <= '9')
    ident = "_" + ident;
  return ident;
}

/** Enforce a canonical charset for a string.
 * Convert all chars in `string` that are not listed as `valid_chars` with @a substitute.
 */
String
string_canonify (const String &string, const String &valid_chars, const String &substitute)
{
  const size_t l = string.size();
  const char *valids = valid_chars.c_str(), *p = string.c_str();
  size_t i;
  for (i = 0; i < l; i++)
    if (!strchr (valids, p[i]))
      goto rewrite_string;
  return string; // only ref increment
 rewrite_string:
  String d = string.substr (0, i);
  d += substitute;
  for (++i; i < l; i++)
    if (strchr (valids, p[i]))
      d += p[i];
    else
      d += substitute;
  return d;
}

/// Check if string_canonify() would modify @a string.
bool
string_is_canonified (const String &string, const String &valid_chars)
{
  const size_t l = string.size();
  const char *valids = valid_chars.c_str(), *p = string.c_str();
  for (size_t i = 0; i < l; i++)
    if (!strchr (valids, p[i]))
      return false;
  return true;
}

/// Returns a string containing all of a-z.
const String&
string_set_a2z ()
{
  static const String cached_a2z = "abcdefghijklmnopqrstuvwxyz";
  return cached_a2z;
}

/// Returns a string containing all of A-Z.
const String&
string_set_A2Z ()
{
  static const String cached_A2Z = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  return cached_A2Z;
}

/// Returns a string containing all of 0-9, A-Z and a-z.
const String&
string_set_ascii_alnum ()
{
  static const String cached_alnum = ASE_STRING_SET_ASCII_ALNUM;
  return cached_alnum;
}

bool
string_is_ascii_alnum (const String &str)
{
  const String &alnum = string_set_ascii_alnum();
  for (size_t i = 0; i < str.size(); i++)
    if (!strchr (alnum.c_str(), str[i]))
      return false;
  return true;
}

/// Convert all string characters into Unicode lower case characters.
String
string_tolower (const String &str)
{
  String s (str);
  for (size_t i = 0; i < s.size(); i++)
    s[i] = Unicode::tolower (s[i]);
  return s;
}

/// Check if all string characters are Unicode lower case characters.
bool
string_islower (const String &str)
{
  for (size_t i = 0; i < str.size(); i++)
    if (str[i] != Unicode::tolower (str[i]))
      return false;
  return true;
}

/// Convert all string characters into Unicode upper case characters.
String
string_toupper (const String &str)
{
  String s (str);
  for (size_t i = 0; i < s.size(); i++)
    s[i] = Unicode::toupper (s[i]);
  return s;
}

/// Check if all string characters are Unicode upper case characters.
bool
string_isupper (const String &str)
{
  for (size_t i = 0; i < str.size(); i++)
    if (str[i] != Unicode::toupper (str[i]))
      return false;
  return true;
}

/// Convert all string characters into Unicode title characters.
String
string_totitle (const String &str)
{
  String s (str);
  for (size_t i = 0; i < s.size(); i++)
    s[i] = Unicode::totitle (s[i]);
  return s;
}

/// Capitalize words, so the first letter is upper case, the rest lower case.
String
string_capitalize (const String &str, size_t maxn, bool rest_tolower)
{
  String s (str);
  bool wasalpha = false;
  for (size_t i = 0; i < s.size(); i++)
    {
      const bool atalpha = isalpha (s[i]);
      if (!wasalpha && atalpha)
        {
          if (maxn == 0)
            break;
          s[i] = Unicode::toupper (s[i]);
          maxn--;
        }
      else if (rest_tolower)
        s[i] = Unicode::tolower (s[i]);
      wasalpha = atalpha;
    }
  return s;
}

/// Yield normalized composed UTF-8 string.
String
string_normalize_nfc (const String &src)
{
  gchar *result = g_utf8_normalize (src.c_str(), src.size(), G_NORMALIZE_NFC);
  const String ret { result ? result : "" };
  g_free (result);
  return ret;
}

/// Yield normalized decomposed UTF-8 string.
String
string_normalize_nfd (const String &src)
{
  gchar *result = g_utf8_normalize (src.c_str(), src.size(), G_NORMALIZE_NFD);
  const String ret { result ? result : "" };
  g_free (result);
  return ret;
}

/// Formatting stripped normalized composed UTF-8 string.
String
string_normalize_nfkc (const String &src)
{
  gchar *result = g_utf8_normalize (src.c_str(), src.size(), G_NORMALIZE_NFKC);
  const String ret { result ? result : "" };
  g_free (result);
  return ret;
}

/// Formatting stripped normalized decomposed UTF-8 string.
String
string_normalize_nfkd (const String &src)
{
  gchar *result = g_utf8_normalize (src.c_str(), src.size(), G_NORMALIZE_NFKD);
  const String ret { result ? result : "" };
  g_free (result);
  return ret;
}

/// Yield UTF-8 string useful for case insensitive comparisons.
String
string_casefold (const String &src)
{
  gchar *result = g_utf8_casefold (src.c_str(), src.size());
  const String ret { result ? result : "" };
  g_free (result);
  return ret;
}

/// Like strcmp(3) for UTF-8 strings.
int
string_cmp (const String &s1, const String &s2)
{
  return g_utf8_collate (s1.c_str(), s2.c_str());
}

/// Like strcasecmp(3) for UTF-8 strings.
int
string_casecmp (const String &s1, const String &s2)
{
  const String cf1 = string_casefold (s1);
  const String cf2 = string_casefold (s2);
  return string_cmp (cf1, cf2);
}

#define STACK_BUFFER_SIZE       3072

static inline String
current_locale_vprintf (const char *format, va_list vargs)
{
  va_list pargs;
  char buffer[STACK_BUFFER_SIZE];
  buffer[0] = 0;
  va_copy (pargs, vargs);
  const int l = vsnprintf (buffer, sizeof (buffer), format, pargs);
  va_end (pargs);
  std::string string;
  if (l < 0)
    string = format; // error?
  else if (size_t (l) < sizeof (buffer))
    string = String (buffer, l);
  else
    {
      string.resize (l + 1);
      va_copy (pargs, vargs);
      const int j = vsnprintf (&string[0], string.size(), format, pargs);
      va_end (pargs);
      string.resize (std::min (l, std::max (j, 0)));
    }
  return string;
}

static inline String
posix_locale_vprintf (const char *format, va_list vargs)
{
  Lib::ScopedPosixLocale posix_locale_scope; // pushes POSIX/C locale for this scope
  return current_locale_vprintf (format, vargs);
}

/// Formatted printing ala vprintf() into a String, using the POSIX/C locale.
String
string_vprintf (const char *format, va_list vargs)
{
  return posix_locale_vprintf (format, vargs);
}

/// Formatted printing like string_vprintf using the current locale.
String
string_locale_vprintf (const char *format, va_list vargs)
{
  return current_locale_vprintf (format, vargs);
}

static StringS
string_whitesplit (const String &string, size_t maxn)
{
  static const char whitespaces[] = " \t\n\r\f\v";
  StringS sv;
  size_t i, l = 0;
  for (i = 0; i < string.size() && sv.size() < maxn; i++)
    if (strchr (whitespaces, string[i]))
      {
        if (i > l)
          sv.push_back (string.substr (l, i - l));
        l = i + 1;
      }
  i = string.size();
  if (i > l)
    sv.push_back (string.substr (l, i - l));
  return sv;
}

/// Split a string, using @a splitter as delimiter.
/// Passing "" as @a splitter will split the string at whitespace positions.
StringS
string_split (const String &string, const String &splitter, size_t maxn)
{
  if (splitter == "")
    return string_whitesplit (string, maxn);
  StringS sv;
  size_t i, l = 0, k = splitter.size();
  for (i = 0; i < string.size() && sv.size() < maxn; i++)
    if (string.substr (i, k) == splitter)
      {
        if (i >= l)
          sv.push_back (string.substr (l, i - l));
        l = i + k;
      }
  i = string.size();
  if (i >= l)
    sv.push_back (string.substr (l, i - l));
  return sv;
}

/// Split a string, using any of the @a splitchars as delimiter.
/// Passing "" as @a splitter will split the string between all position.
StringS
string_split_any (const String &string, const String &splitchars, size_t maxn)
{
  StringS sv;
  size_t i, l = 0;
  if (splitchars.empty())
    {
      for (i = 0; i < string.size() && sv.size() < maxn; i++)
        sv.push_back (string.substr (i, 1));
      if (i < string.size())
        sv.push_back (string.substr (i, string.size() - i));
    }
  else
    {
      const char *schars = splitchars.c_str();
      l = 0;
      for (i = 0; i < string.size() && sv.size() < maxn; i++)
        if (strchr (schars, string[i]))
          {
            if (i >= l)
              sv.push_back (string.substr (l, i - l));
            l = i + 1;
          }
      i = string.size();
      if (i >= l)
        sv.push_back (string.substr (l, i - l));
    }
  return sv;
}

StringS
strings_version_sort (const StringS &strings, bool reverse)
{
  StringS dest = strings;
  strings_version_sort (&dest, reverse);
  return dest;
}

void
strings_version_sort (StringS *strings, bool reverse)
{
  return_unless (strings);
  std::sort (strings->begin(), strings->end(), [reverse] (auto &a, auto &b) -> bool {
    const int c = strverscmp (a.c_str(), b.c_str());
    return reverse ? c > 0 : c < 0;
  });
}

/// Remove empty elements from a string vector.
void
string_vector_erase_empty (StringS &svector)
{
  for (size_t i = svector.size(); i; i--)
    {
      const size_t idx = i - 1;
      if (svector[idx].empty())
        svector.erase (svector.begin() + idx);
    }
}

/// Left-strip all elements of a string vector, see string_lstrip().
void
string_vector_lstrip (StringS &svector)
{
  for (auto &s : svector)
    s = string_lstrip (s);
}

/// Right-strip all elements of a string vector, see string_rstrip().
void
string_vector_rstrip (StringS &svector)
{
  for (auto &s : svector)
    s = string_rstrip (s);
}

/// Strip all elements of a string vector, see string_strip().
void
string_vector_strip (StringS &svector)
{
  for (auto &s : svector)
    s = string_strip (s);
}

/** Join a number of strings.
 * Join a string vector into a single string, using @a junctor inbetween each pair of strings.
 */
String
string_join (const String &junctor, const StringS &strvec)
{
  String s;
  if (strvec.size())
    s = strvec[0];
  for (uint i = 1; i < strvec.size(); i++)
    s += junctor + strvec[i];
  return s;
}

/** Interpret a string as boolean value.
 * Interpret the string as number, "ON"/"OFF" or distinguish "false"/"true" or "yes"/"no" by starting letter.
 * For empty strings, @a fallback is returned.
 */
bool
string_to_bool (const String &string, bool fallback)
{
  return cstring_to_bool (string.c_str(), fallback);
}

bool
cstring_to_bool (const char *string, bool fallback)
{
  if (!string)
    return fallback;
  const char *p = string;
  // skip spaces
  while (*p && isspace (*p))
    p++;
  // ignore signs
  if (p[0] == '-' || p[0] == '+')
    {
      p++;
      // skip spaces
      while (*p && isspace (*p))
        p++;
    }
  // handle numbers
  if (p[0] >= '0' && p[0] <= '9')
    return 0 != string_to_uint (p);
  // handle special words
  if (strncasecmp (p, "ON", 2) == 0)
    return 1;
  if (strncasecmp (p, "OFF", 3) == 0)
    return 0;
  // empty string
  if (!p[0])
    return fallback;
  // anything else needs to resemble "yes" or "true"
  return strchr ("YyTt", p[0]);
}

// The strrstr() function finds the last occurrence of the substring `needle` in the string `haystack`
const char*
strrstr (const char *haystack, const char *needle)
{
  const ssize_t n = strlen (needle);
  return_unless (n > 0, haystack);
  const ssize_t h = strlen (haystack);
  return_unless (h >= n, nullptr);
  for (ssize_t i = h - n; i >= 0; i--)
    if (strncmp (haystack + i, needle, n) == 0)
      return haystack + i;
  return nullptr;
}

/// Convert a boolean value into a string.
String
string_from_bool (bool value)
{
  return String (value ? "1" : "0");
}

/// Parse a string into a 64bit unsigned integer, optionally specifying the expected number base.
uint64
string_to_uint (const String &string, size_t *consumed, uint base)
{
  const char *const start = string.c_str(), *p = start;
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
    p++;
  const bool hex = p[0] == '0' && (p[1] == 'X' || p[1] == 'x');
  const char *const number = hex ? p + 2 : p;
  char *endptr = NULL;
  const uint64 result = strtoull (number, &endptr, hex ? 16 : base);
  if (consumed)
    {
      if (!endptr || endptr <= number)
        *consumed = 0;
      else
        *consumed = endptr - start;
    }
  return result;
}

/// Convert a 64bit unsigned integer into a string.
String
string_from_uint (uint64 value)
{
  return string_format ("%u", value);
}

/// Checks if a string contains a digit, optionally preceeded by whitespaces.
bool
string_has_int (const String &string)
{
  const char *p = string.c_str();
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
    p++;
  return p[0] >= '0' && p[0] <= '9';
}

/// Parse a string into a 64bit integer, optionally specifying the expected number base.
int64
string_to_int (const String &string, size_t *consumed, uint base)
{
  const char *const start = string.c_str(), *p = start;
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
    p++;
  const bool negate = p[0] == '-';
  if (negate)
    p++;
  const bool hex = p[0] == '0' && (p[1] == 'X' || p[1] == 'x');
  const char *const number = hex ? p + 2 : p;
  char *endptr = NULL;
  const uint64_t result = strtoull (number, &endptr, hex ? 16 : base);
  if (consumed)
    {
      if (!endptr || endptr <= number)
        *consumed = 0;
      else
        *consumed = endptr - start;
    }
  if (result < 9223372036854775808ull)
    return negate ? -int64_t (result) : result;
  return negate ? -9223372036854775807ll - 1 : 9223372036854775808ull - 1;
}

/// Convert a 64bit signed integer into a string.
String
string_from_int (int64 value)
{
  return string_format ("%d", value);
}

static long double
libc_strtold (const char *nptr, char **endptr)
{
  const long double result = strtold (nptr, endptr);
  if (std::isnan (result) && std::signbit (result) == 0)
    {
      const char *p = nptr;
      while (isspace (*p))
        p++;
      if (strncasecmp (p, "-nan", 4) == 0)
        return -result; // glibc-2.19 doesn't get the NAN sign right
    }
  return result;
}

/// Parse a double from a string ala strtod(), trying locale specific characters and POSIX/C formatting.
long double
posix_locale_strtold (const char *nptr, char **endptr)
{
  Lib::ScopedPosixLocale posix_locale_scope; // pushes POSIX/C locale for this scope
  char *fail_pos = NULL;
  const long double val = libc_strtold (nptr, &fail_pos);
  if (endptr)
    *endptr = fail_pos;
  return val;
}

/// Parse a double from a string ala strtod(), trying locale specific characters and POSIX/C formatting.
long double
current_locale_strtold (const char *nptr, char **endptr)
{
  char *fail_pos_1 = NULL;
  const long double val_1 = posix_locale_strtold (nptr, &fail_pos_1);
  if (fail_pos_1 && fail_pos_1[0] != 0)
    {
      char *fail_pos_2 = NULL;
      const long double val_2 = libc_strtold (nptr, &fail_pos_2);
      if (fail_pos_2 > fail_pos_1)
        {
          if (endptr)
            *endptr = fail_pos_2;
          return val_2;
        }
    }
  if (endptr)
    *endptr = fail_pos_1;
  return val_1;
}

/// Parse a long double from a string, trying locale specific characters and POSIX/C formatting.
long double
string_to_long_double (const String &string)
{
  return current_locale_strtold (string.c_str(), NULL);
}

/// Similar to string_to_long_double(const String&), but returns the first failing character position in @a endptr.
long double
string_to_long_double (const char *dblstring, const char **endptr)
{
  return current_locale_strtold (dblstring, (char**) endptr);
}

/// Parse a double from a string, trying locale specific characters and POSIX/C formatting.
double
string_to_double (const String &string)
{
  return current_locale_strtold (string.c_str(), NULL);
}

/// Similar to string_to_double(const String&), but returns the first failing character position in @a endptr.
double
string_to_double (const char *dblstring, const char **endptr)
{
  return current_locale_strtold (dblstring, (char**) endptr);
}

/// Convert a float into a string, using the POSIX/C locale.
String
string_from_float (float value)
{
  if (std::isnan (value))
    return std::signbit (value) ? "-NaN" : "+NaN";
  if (std::isinf (value))
    return std::signbit (value) ? "-Infinity" : "+Infinity";
  return string_format ("%.7g", value);
}

/// Convert a double into a string, using the POSIX/C locale.
String
string_from_double (double value)
{
  if (std::isnan (value))
    return std::signbit (value) ? "-NaN" : "+NaN";
  if (std::isinf (value))
    return std::signbit (value) ? "-Infinity" : "+Infinity";
  return string_format ("%.17g", value);
}

/// Convert a long double into a string, using the POSIX/C locale.
String
string_from_long_double (long double value)
{
  if (std::isnan (value))
    return std::signbit (value) ? "-NaN" : "+NaN";
  if (std::isinf (value))
    return std::signbit (value) ? "-Infinity" : "+Infinity";
  return string_format ("%.20g", value);
}

/// Parse a string into a list of doubles, expects ';' as delimiter.
std::vector<double>
string_to_double_vector (const String &string)
{
  std::vector<double> dvec;
  const char *spaces = " \t\n";
  const char *obrace = "{([";
  const char *delims = ";";
  const char *cbrace = "])}";
  const char *number = "+-0123456789eE.,";
  const char *s = string.c_str();
  /* skip spaces */
  while (*s && strchr (spaces, *s))
    s++;
  /* skip opening brace */
  if (*s && strchr (obrace, *s))
    s++;
  const char *d = s;
  while (*d && !strchr (cbrace, *d))
    {
      while (*d && strchr (spaces, *d))         /* skip spaces */
        d++;
      s = d;                                    /* start of number */
      if (!*d || (!strchr (number, *d) &&       /* ... if any */
                  !strchr (delims, *d)))
        break;
      while (*d && strchr (number, *d))         /* pass across number */
        d++;
      dvec.push_back (string_to_double (String (s, d - s)));
      while (*d && strchr (spaces, *d))         /* skip spaces */
        d++;
      if (*d && strchr (delims, *d))
        d++;                                    /* eat delimiter */
    }
  // printerr ("vector: %d: %s\n", dvec.size(), string_from_double_vector (dvec).c_str());
  return dvec;
}

/// Construct a string out of all double values passed in @a dvec, separated by @a delim.
String
string_from_double_vector (const std::vector<double> &dvec, const String &delim)
{
  String s;
  for (uint i = 0; i < dvec.size(); i++)
    {
      if (i > 0)
        s += delim;
      s += string_from_double (dvec[i]);
    }
  return s;
}

/// Parse string into seconds
double
string_to_seconds (const String &string, double fallback)
{
  const char *fail = nullptr;
  const char *const cs = string.c_str();
  double d = string_to_double (cs, &fail);
  if (fail == cs)
    return fallback;
  if (!fail || fail[0] == 's')
    return d;
  if (strncmp (fail, "ns", 2) == 0)
    return d * 0.000000001;
  const char *usec = "µs";
  if (strncmp (fail, "us", 2) == 0 || strncmp (fail, usec, strlen (usec)) == 0)
    return d * 0.000001;
  if (strncmp (fail, "ms", 2) == 0)
    return d * 0.001;
  if (fail[0] == 'm')
    return d * 60;
  if (fail[0] == 'h')
    return d * 3600;
  if (fail[0] == 'd')
    return d * 3600 * 24;
  if (fail[0] == 'w')
    return d * 3600 * 24 * 7;
  return d; // treat as seconds
}

/// Returns a String describing the passed in errno value, similar to strerror().
String
string_from_errno (int errno_val)
{
  if (errno_val < 0)
    errno_val = -errno_val;     // fixup library return values
  char buffer[1024] = { 0, };
  const char *errstr = strerror_r (errno_val, buffer, sizeof (buffer));
  if (!errstr || !errstr[0]) // fallback for possible strerror_r breakage encountered on _GNU_SOURCE systems
    return strerror (errno_val);
  return errstr;
}

/// Returns whether @a uuid_string contains a properly formatted UUID string.
bool
string_is_uuid (const String &uuid_string) /* check uuid formatting */
{
  int i, l = uuid_string.size();
  if (l != 36)
    return false;
  // 00000000-0000-0000-0000-000000000000
  for (i = 0; i < l; i++)
    if (i == 8 || i == 13 || i == 18 || i == 23)
      {
        if (uuid_string[i] != '-')
          return false;
        continue;
      }
    else if ((uuid_string[i] >= '0' && uuid_string[i] <= '9') ||
             (uuid_string[i] >= 'a' && uuid_string[i] <= 'f') ||
             (uuid_string[i] >= 'A' && uuid_string[i] <= 'F'))
      continue;
    else
      return false;
  return true;
}

/// Returns whether @a uuid_string1 compares smaller (-1), equal (0) or greater (+1) to @a uuid_string2.
int
string_cmp_uuid (const String &uuid_string1, const String &uuid_string2)
{
  return strcasecmp (uuid_string1.c_str(), uuid_string2.c_str()); /* good enough for numeric equality and defines stable order */
}

/// Returns whether `string` starts with `fragment`.
bool
string_startswith (const String &string, const String &fragment)
{
  return fragment.size() <= string.size() && 0 == string.compare (0, fragment.size(), fragment);
}

/// Returns whether @a string starts with any element of `fragments`.
bool
string_startswith (const String &string, const StringS &fragments)
{
  for (const String &frag : fragments)
    if (string_startswith (string, frag))
      return true;
  return false;
}

/// Returns whether `string` ends with `fragment`.
bool
string_endswith (const String &string, const String &fragment)
{
  return fragment.size() <= string.size() && 0 == string.compare (string.size() - fragment.size(), fragment.size(), fragment);
}

/// Returns whether @a string ends with any element of `fragments`.
bool
string_endswith (const String &string, const StringS &fragments)
{
  for (const String &frag : fragments)
    if (string_endswith (string, frag))
      return true;
  return false;
}

static inline bool
c_isalnum (uint8 c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

static inline char
identifier_char_canon (char c)
{
  if (c >= '0' && c <= '9')
    return c;
  else if (c >= 'A' && c <= 'Z')
    return c - 'A' + 'a';
  else if (c >= 'a' && c <= 'z')
    return c;
  else
    return '-';
}

static inline bool
identifier_match (const char *str1, const char *str2)
{
  while (*str1 && *str2)
    {
      const uint8 s1 = identifier_char_canon (*str1++);
      const uint8 s2 = identifier_char_canon (*str2++);
      if (s1 != s2)
        return false;
    }
  return *str1 == 0 && *str2 == 0;
}

static bool
match_identifier_detailed (const String &ident, const String &tail)
{
  assert_return (ident.size() >= tail.size(), false);
  const char *word = ident.c_str() + ident.size() - tail.size();
  if (word > ident.c_str()) // allow partial matches on word boundary only
    {
      if (c_isalnum (word[-1]) && c_isalnum (word[0])) // no word boundary
        return false;
    }
  return identifier_match (word, tail.c_str());
}

/// Variant of string_match_identifier() that matches @a tail against @a ident at word boundary.
bool
string_match_identifier_tail (const String &ident, const String &tail)
{
  return ident.size() >= tail.size() && match_identifier_detailed (ident, tail);
}

/// Check equality of strings canonicalized to "[0-9a-z_]+".
bool
string_match_identifier (const String &ident1, const String &ident2)
{
  return ident1.size() == ident2.size() && match_identifier_detailed (ident1, ident2);
}

/// Extract the full function name from __PRETTY_FUNCTION__.
/// See also ASE_SIMPLE_FUNCTION.
String
string_from_pretty_function_name (const char *cxx_pretty_function)
{
  // get rid of g++'s anon prefixes
  const String p1 = string_replace (cxx_pretty_function, "{anonymous}::", "");
  // get rid of clang++'s anon prefixes
  const String p2 = string_replace (p1, "(anonymous namespace)::", "");
  const char *const pretty_function = p2.c_str();
  /* finding the function name is non-trivial in the presence of function pointer
   * return types. the following code assumes the function name preceedes the
   * first opening parenthesis not followed by a star.
   */
  const char *op = strchr (pretty_function, '(');
  while (op && op[1] == '*')
    op = strchr (op + 1, '(');
  if (!op)
    return pretty_function;
  // *op == '(' && op[1] != '*'
  const char *last = op - 1;
  while (last >= pretty_function && strchr (" \t\n", *last))
    last--; // skip spaces before '('
  if (last < pretty_function)
    return pretty_function;
  // scan across function name characters
  const char *first = last;
  while (first >= pretty_function && strchr ("0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ:abcdefghijklmnopqrstuvwxyz$", *first))
    first--;
  String result = String (first + 1, last - first);
  return result;
}
/// Decode URL %-sequences in a string, decode '+' if `form_url_encoded`.
String
string_url_decode (const String &urlstr, const bool form_url_encoded)
{
  String s;
  s.reserve (urlstr.size());
  for (size_t i = 0; i < urlstr.size(); i++)
    {
      const char c = urlstr[i];
      if (c == '%' && isxdigit (urlstr[i+1]) && isxdigit (urlstr[i+2]))
        {
          const char buf[3] = { urlstr[i+1], urlstr[i+2], 0 };
          const long l = strtol (buf, nullptr, 16);
          s.push_back (char (l));
          i += 2;
        }
      else if (form_url_encoded && c == '+')
        s.push_back (' ');
      else
        s.push_back (c);
    }
  return s;
}

/// Encode special characters to URL %-sequences, encode space as '+' if `form_url_encoded`.
String
string_url_encode (const String &rawstr, const bool form_url_encoded)
{
  String s;
  s.reserve (rawstr.size());
  const char *const unescaped = /*unreserved:*/ "-._~" /*ok-in-FF:*/ "[]!()*";
  for (const uint8_t c : rawstr)
    if (isalnum (c) || strchr (unescaped, c))
      s.push_back (c);
    else if (form_url_encoded && c == ' ')
      s.push_back ('+');
    else
      {
        const char *const hex = "0123456789ABCDEF";
        const char buf[4] = { '%', hex[c >> 4], hex[c & 0x0f], 0 };
        s += buf;
      }
  return s;
}

/// Escape text like a C string.
/// Returns a string that escapes all characters with a backslash '\\' that need escaping in C language string syntax.
String
string_to_cescape (const String &str)
{
  String buffer;
  for (String::const_iterator it = str.begin(); it != str.end(); it++)
    {
      const uint8 d = *it;
      if      (d == '\a')       buffer +=  "\\a";
      else if (d == '\b')       buffer +=  "\\b";
      else if (d == '\t')       buffer +=  "\\t";
      else if (d == '\n')       buffer +=  "\\n";
      else if (d == '\v')       buffer +=  "\\v";
      else if (d == '\f')       buffer +=  "\\f";
      else if (d == '\r')       buffer +=  "\\r";
      else if (d == '"')        buffer += "\\\"";
      else if (d == '\\')       buffer += "\\\\";
      else if (d < 32 || d > 126)
        buffer += string_format ("\\%03o", d);
      else
        buffer += d;
    }
  return buffer;
}

/// Returns a string as C string including double quotes.
String
string_to_cquote (const String &str)
{
  return String() + "\"" + string_to_cescape (str) + "\"";
}

/// Parse a possibly quoted C string into regular string.
String
string_from_cquote (const String &input)
{
  uint i = 0;
  if (i < input.size() && (input[i] == '"' || input[i] == '\''))
    {
      const char qchar = input[i];
      i++;
      String out;
      bool be = false;
      while (i < input.size() && (input[i] != qchar || be))
        {
          if (!be && input[i] == '\\')
            be = true;
          else
            {
              if (be)
                switch (input[i])
                  {
                    uint k, oc;
                  case '0': case '1': case '2': case '3':
                  case '4': case '5': case '6': case '7':
                    k = MIN (input.size(), i + 3);
                    oc = input[i++] - '0';
                    while (i < k && input[i] >= '0' && input[i] <= '7')
                      oc = oc * 8 + input[i++] - '0';
                    out += char (oc);
                    i--;
                    break;
                  case 'a':     out += '\a';            break;
                  case 'n':     out += '\n';            break;
                  case 'r':     out += '\r';            break;
                  case 't':     out += '\t';            break;
                  case 'b':     out += '\b';            break;
                  case 'f':     out += '\f';            break;
                  case 'v':     out += '\v';            break;
                  default:      out += input[i];        break;
                  }
              else
                out += input[i];
              be = false;
            }
          i++;
        }
      if (i < input.size() && input[i] == qchar)
        {
          i++;
          if (i < input.size())
            return input; // extraneous characters after string quotes
          return out;
        }
      else
        return input; // unclosed string quotes
    }
  else if (i == input.size())
    return input; // empty string arg: ""
  else
    return input; // missing string quotes
}

static const char *whitespaces = " \t\v\f\n\r";

/// Strip whitespaces from the left of a string.
String
string_lstrip (const String &input)
{
  uint64 i = 0;
  while (i < input.size() && strchr (whitespaces, input[i]))
    i++;
  return i ? input.substr (i) : input;
}

/// Strip whitespaces from the right of a string.
String
string_rstrip (const String &input)
{
  uint64 i = input.size();
  while (i > 0 && strchr (whitespaces, input[i - 1]))
    i--;
  return i < input.size() ? input.substr (0, i) : input;
}

/// Strip whitespaces from the left and right of a string.
String
string_strip (const String &input)
{
  uint64 a = 0;
  while (a < input.size() && strchr (whitespaces, input[a]))
    a++;
  uint64 b = input.size();
  while (b > 0 && strchr (whitespaces, input[b - 1]))
    b--;
  if (a == 0 && b == input.size())
    return input;
  else if (b == 0)
    return "";
  else
    return input.substr (a, b - a);
}

/// Replace substring @a marker in @a input with @a replacement, at most @a maxn times.
String
string_replace (const String &input, const String &marker, const String &replacement, size_t maxn)
{
  String s = input;
  size_t i = s.find (marker);
  while (i != String::npos && maxn-- > 0)
    {
      s = s.substr (0, i) + replacement + s.substr (i + marker.size());
      i = s.find (marker, i + replacement.size());
    }
  return s;
}


/// Replace all occouranes of @a match in @a input with @a subst.
String
string_substitute_char (const String &input, const char match, const char subst)
{
  String output = input;
  if (match != subst)
    for (String::size_type i = 0; i < output.size(); i++)
      if (output.data()[i] == match)
        output[i] = subst; // unshares string
  return output;
}

/// Convert bytes in string `input` to hexadecimal numbers.
String
string_to_hex (const String &input)
{
  String s;
  s.reserve (input.size() * 2);
  for (const char &c : input)
    s += string_format ("%02x", c);
  return s;
}

/** Produce hexdump of a memory region.
 * Each output line consists of its hexadecimal offset, 16 hexadecimal bytes and the ASCII representation of the same 16 bytes.
 */
String
string_hexdump (const void *addr, size_t length, size_t initial_offset)
{
  // 000000d0  00 34 00 00 08 00 00 00  40 00 00 00 61 00 00 00  |.4......@...a...|
  const unsigned char *data = (const unsigned char*) addr;
  size_t i;
  String out, cx, cc = "|";
  for (i = 0; i < length;)
    {
      if (i % 8 == 0)
        cx += " ";
      cx += string_format (" %02x", data[i]);
      cc += string_format ("%c", data[i] < ' ' || data[i] > '~' ? '.' : data[i]);
      i++;
      if (i && i % 16 == 0)
        {
          cc += "|";
          out += string_format ("%08x%s  %s\n", initial_offset + i - 16, cx.c_str(), cc.c_str());
          cx = "";
          cc = "|";
        }
    }
  if (i % 16)
    {
      for (; i % 16; i++)
        {
          if (i % 8 == 0)
            cx += " ";
          cx += "   ";
        }
      cc += "|";
      out += string_format ("%08x%s  %s\n", initial_offset + i - 16, cx.c_str(), cc.c_str());
    }
  return out;
}

/// Fill a memory area with a 32-bit quantitiy.
void
memset4 (uint32 *mem, uint32 filler, uint length)
{
  static_assert (sizeof (*mem) == 4, "");
  static_assert (sizeof (filler) == 4, "");
  static_assert (sizeof (wchar_t) == 4, "");
  wmemset ((wchar_t*) mem, filler, length);
}

/**
 * Search for @a prefix in @a svector and return the matching element.
 * If multiple matches are possible, the last one is returned.
 * @returns @a fallback if no match was found.
 */
String
string_vector_find (const StringS &svector, const String &prefix, const String &fallback)
{
  for (size_t i = svector.size(); i > 0; i--)
    {
      const String &s = svector[i-1];
      if (s.size() >= prefix.size() && strncmp (s.data(), prefix.data(), prefix.size()) == 0)
        return s;
    }
  return fallback;
}

/**
 * Search for @a prefix in @a svector and return reminder of the matching string.
 * If multiple matches are possible, the last one is returned.
 * @returns @a fallback if no match was found.
 */
String
string_vector_find_value (const StringS &svector, const String &prefix, const String &fallback)
{
  for (size_t i = svector.size(); i > 0; i--)
    {
      const String &s = svector[i-1];
      if (s.size() >= prefix.size() && strncmp (s.data(), prefix.data(), prefix.size()) == 0)
        return s.substr (prefix.size());
    }
  return fallback;
}

/// Construct a StringS from a NULL terminated list of string arguments.
StringS
cstrings_to_vector (const char *s, ...)
{
  StringS sv;
  if (s)
    {
      sv.push_back (s);
      va_list args;
      va_start (args, s);
      s = va_arg (args, const char*);
      while (s)
        {
          sv.push_back (s);
          s = va_arg (args, const char*);
        }
      va_end (args);
    }
  return sv;
}

// == Key=Value Pairs ==
String
kvpair_key (const String &key_value_pair)
{
  const char *const eq = strchr (key_value_pair.c_str(), '=');
  return eq ? key_value_pair.substr (0, eq - key_value_pair.c_str()) : key_value_pair;
}

String
kvpair_value (const String &key_value_pair)
{
  const char *const eq = strchr (key_value_pair.c_str(), '=');
  return eq ? key_value_pair.substr (eq - key_value_pair.c_str() + 1) : "";
}

String
kvpairs_fetch (const StringS &kvs, const String &key, bool casesensitive)
{
  const ssize_t i = kvpairs_search (kvs, key, casesensitive);
  return i >= 0 ? kvs[i].data() + key.size() + 1 : "";
}

ssize_t
kvpairs_search (const StringS &kvs, const String &k, const bool casesensitive)
{
  const size_t l = k.size();
  for (size_t i = 0; i < kvs.size(); i++)
    if (kvs[i].size() > l && kvs[i][l] == '=') {
      if (casesensitive) {
        if (strncmp (kvs[i].data(), k.data(), l) == 0)
          return i;
      } else { // !casesensitive
        if (strncasecmp (kvs[i].data(), k.data(), l) == 0)
          return i;
      }
    }
  return -1;
}

bool
kvpairs_assign (StringS &kvs, const String &key_value_pair, bool casesensitive)
{
  const char *const eq = strchr (key_value_pair.c_str(), '=');
  const String key = eq ? key_value_pair.substr (0, eq - key_value_pair.c_str()) : "";
  assert_return (key.size() > 0, false);
  const ssize_t i = kvpairs_search (kvs, key, casesensitive);
  if (key_value_pair.size() == key.size() + 1 && key_value_pair[key_value_pair.size()-1] == '=') {
    // value is empty
    if (i >= 0)
      kvs.erase (kvs.begin() + i);      // empty value, erase
  } else if (i >= 0)
    kvs[i] = key_value_pair;            // replace
  else
    kvs.push_back (key_value_pair);     // insert
  return i >= 0;                        // replaced old value
}

// === String Options ===
static bool is_separator (char c) { return c == ';' || c == ':'; }

static const char*
find_option (const char *haystack, const char *const needle, const size_t l, const int allowoption)
{
  const char *match = nullptr;
  for (const char *c = strcasestr (haystack, needle); c; c = strcasestr (c + 1, needle))
    if (!allowoption &&
        (c[l] == 0 && is_separator (c[l])) &&
        ((c == haystack + 3 && strncasecmp (haystack, "no-", 3) == 0) ||
         (c >= haystack + 4 && is_separator (haystack[0]) && strncasecmp (haystack + 1, "no-", 3) == 0)))
      match = c;
    else if (allowoption &&
             ((allowoption >= 2 && c[l] == '=') || c[l] == 0 || is_separator (c[l])) &&
             (c == haystack || is_separator (c[-1])))
      match = c;
  return match;
}

static size_t
separator_strlen (const char *const s)
{
  const char *c = s;
  while (c[0] && !is_separator (c[0]))
    c++;
  return c - s;
}

static std::string_view
string_option_find_value (const char *string, const char *feature, const char *fallback, const char *denied, const int matching)
{
  if (!string || !feature || !string[0] || !feature[0])
    return { fallback, strlen (fallback) };                     // not-found
  const size_t l = strlen (feature);
  const char *match = find_option (string, feature, l, 2);      // .prio=2
  if (matching >= 2) {
    const char *deny = find_option (string, feature, l, 0);     // .prio=1
    if (deny > match)
      return { denied, strlen (denied) };                       // denied
  }
  if (match && match[l] == '=')
    return { match + l + 1, separator_strlen (match + l + 1) }; // value
  if (match)
    return { "1", 1 };                                          // allowed
  if (matching >= 3) {
    if (find_option (string, "all", 3, 1))                      // .prio=3
      return { "1", 1 };                                        // allowed
    if (find_option (string, "none", 4, 1))                     // .prio=4
      return { denied, strlen (denied) };                       // denied
  }
  return { fallback, strlen (fallback) };                       // not-found
}

/// Low level option search, avoids dynamic allocations.
std::string_view
string_option_find_value (const char *string, const char *feature, const String &fallback, const String &denied, bool matchallnone)
{
  return string_option_find_value (string, feature, fallback.c_str(), denied.c_str(), matchallnone ? 3 : 2);
}

/// Retrieve the option value from an options list separated by ':' or ';' or `fallback`.
String
string_option_find (const String &optionlist, const String &feature, const String &fallback)
{
  std::string_view sv = string_option_find_value (optionlist.data(), feature.data(), fallback.data(), "0", 3);
  return { sv.data(), sv.size() };
}

/// Check if an option is set/unset in an options list string.
bool
string_option_check (const String &optionlist, const String &feature)
{
  return string_to_bool (string_option_find (optionlist, feature, "0"), true);
}

// == Strings ==
Strings::Strings (CS &s1)
{ push_back (s1); }
Strings::Strings (CS &s1, CS &s2)
{ push_back (s1); push_back (s2); }
Strings::Strings (CS &s1, CS &s2, CS &s3)
{ push_back (s1); push_back (s2); push_back (s3); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6); push_back (s7); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6);
  push_back (s7); push_back (s8); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6);
  push_back (s7); push_back (s8); push_back (s9); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9, CS &sA)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6);
  push_back (s7); push_back (s8); push_back (s9); push_back (sA); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9, CS &sA, CS &sB)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6);
  push_back (s7); push_back (s8); push_back (s9); push_back (sA); push_back (sB); }
Strings::Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9, CS &sA, CS &sB, CS &sC)
{ push_back (s1); push_back (s2); push_back (s3); push_back (s4); push_back (s5); push_back (s6);
  push_back (s7); push_back (s8); push_back (s9); push_back (sA); push_back (sB); push_back (sC); }

// === Charset Conversions ===
static bool
unalias_encoding (String &name)
{
  /* list of common aliases for MIME encodings */
  static const char *encoding_aliases[] = {
    /* alias            MIME (GNU CANONICAL) */
    "UTF8",             "UTF-8",
    /* ascii */
    "646",              "ASCII",
    "ISO_646.IRV:1983", "ASCII",
    "CP20127",          "ASCII",
    /* iso8859 aliases */
    "LATIN1",           "ISO-8859-1",
    "LATIN2",           "ISO-8859-2",
    "LATIN3",           "ISO-8859-3",
    "LATIN4",           "ISO-8859-4",
    "LATIN5",           "ISO-8859-9",
    "LATIN6",           "ISO-8859-10",
    "LATIN7",           "ISO-8859-13",
    "LATIN8",           "ISO-8859-14",
    "LATIN9",           "ISO-8859-15",
    "LATIN10",          "ISO-8859-16",
    "ISO8859-1",        "ISO-8859-1",
    "ISO8859-2",        "ISO-8859-2",
    "ISO8859-3",        "ISO-8859-3",
    "ISO8859-4",        "ISO-8859-4",
    "ISO8859-5",        "ISO-8859-5",
    "ISO8859-6",        "ISO-8859-6",
    "ISO8859-7",        "ISO-8859-7",
    "ISO8859-8",        "ISO-8859-8",
    "ISO8859-9",        "ISO-8859-9",
    "ISO8859-13",       "ISO-8859-13",
    "ISO8859-15",       "ISO-8859-15",
    "CP28591",          "ISO-8859-1",
    "CP28592",          "ISO-8859-2",
    "CP28593",          "ISO-8859-3",
    "CP28594",          "ISO-8859-4",
    "CP28595",          "ISO-8859-5",
    "CP28596",          "ISO-8859-6",
    "CP28597",          "ISO-8859-7",
    "CP28598",          "ISO-8859-8",
    "CP28599",          "ISO-8859-9",
    "CP28603",          "ISO-8859-13",
    "CP28605",          "ISO-8859-15",
    /* EUC aliases */
    "eucCN",            "GB2312",
    "IBM-eucCN",        "GB2312",
    "dechanzi",         "GB2312",
    "eucJP",            "EUC-JP",
    "IBM-eucJP",        "EUC-JP",
    "sdeckanji",        "EUC-JP",
    "eucKR",            "EUC-KR",
    "IBM-eucKR",        "EUC-KR",
    "deckorean",        "EUC-KR",
    "eucTW",            "EUC-TW",
    "IBM-eucTW",        "EUC-TW",
    "CNS11643",         "EUC-TW",
    "CP20866",          "KOI8-R",
    /* misc */
    "PCK",              "SHIFT_JIS",
    "SJIS",             "SHIFT_JIS",
  };
  /* find a MIME encoding from alias list */
  for (uint i = 0; i < sizeof (encoding_aliases) / sizeof (encoding_aliases[0]); i += 2)
    if (strcasecmp (encoding_aliases[i], name.c_str()) == 0)
      {
        name = encoding_aliases[i + 1];
        return true;
      }
  /* last resort, try upper-casing the encoding name */
  String upper = name;
  for (uint i = 0; i < upper.size(); i++)
    if (upper[i] >= 'a' && upper[i] <= 'z')
      upper[i] += 'A' - 'a';
  if (upper != name)
    {
      name = upper;
      return true;
    }
  /* alias not found */
  return false;
}

static iconv_t
aliased_iconv_open (const String &tocode,
                    const String &fromcode)
{
  const iconv_t icNONE = (iconv_t) -1;
  iconv_t cd = iconv_open (tocode.c_str(), fromcode.c_str());
  if (cd != icNONE)
    return cd;
  /* lookup destination encoding from alias and retry */
  String to_encoding = tocode;
  if (unalias_encoding (to_encoding))
    {
      cd = iconv_open (to_encoding.c_str(), fromcode.c_str());
      if (cd != icNONE)
        return cd;
      /* lookup source and destination encoding from alias and retry */
      String from_encoding = fromcode;
      if (unalias_encoding (from_encoding))
        {
          cd = iconv_open (to_encoding.c_str(), from_encoding.c_str());
          if (cd != icNONE)
            return cd;
        }
    }
  /* lookup source encoding from alias and retry */
  String from_encoding = fromcode;
  if (unalias_encoding (from_encoding))
    {
      cd = iconv_open (tocode.c_str(), from_encoding.c_str());
      if (cd != icNONE)
        return cd;
    }
  return icNONE; /* encoding not found */
}

/** Convert a string from one encoding to another.
 * Convert @a input_string from encoding @a from_charset to @a to_charset, returning @a output_string.
 * Interpret unknown characters according to @a fallback_charset. Use @a output_mark in place of unconvertible characters.
 * Returns whether the conversion was successful.
 */
bool
text_convert (const String &to_charset,
              String       &output_string,
              const String &from_charset,
              const String &input_string,
              const String &fallback_charset,
              const String &output_mark)
{
  output_string = "";
  const iconv_t icNONE = (iconv_t) -1;
  iconv_t alt_cd = icNONE, cd = aliased_iconv_open (to_charset, from_charset);
  if (cd == icNONE)
    return false;                       /* failed to perform the requested conversion */
  const char *iptr = input_string.c_str();
  size_t ilength = input_string.size();
  char obuffer[1024];                   /* declared outside loop to spare re-initialization */
  String alt_charset = fallback_charset;
  while (ilength)
    {
      /* convert */
      char *optr = obuffer;
      size_t olength = sizeof (obuffer);
      size_t n = iconv (cd, const_cast<char**> (&iptr), &ilength, &optr, &olength);
      /* transfer output */
      output_string.append (obuffer, optr - obuffer);
      /* handle conversion errors */
      if (ilength &&                    /* ignore past end errors */
          n == (size_t) -1)
        {
          if (errno == EINVAL ||        /* unfinished multibyte sequences follows (near end of string) */
              errno == EILSEQ)          /* invalid multibyte sequence follows */
            {
              /* open alternate converter */
              if (alt_cd == icNONE && alt_charset.size())
                {
                  alt_cd = aliased_iconv_open (to_charset, alt_charset);
                  alt_charset = ""; /* don't retry iconv_open() */
                }
              size_t former_ilength = ilength;
              if (alt_cd != icNONE)
                {
                  /* convert from alt_charset */
                  optr = obuffer;
                  olength = sizeof (obuffer);
                  n = iconv (alt_cd, const_cast<char**> (&iptr), &ilength, &optr, &olength);
                  (void) n;
                  /* transfer output */
                  output_string.append (obuffer, optr - obuffer);
                }
              if (ilength == former_ilength)
                {
                  /* failed alternate conversion, mark invalid character */
                  output_string += output_mark;
                  iptr++;
                  ilength--;
                }
            }
          else  /* all other errors are considered fatal */
            return false;               /* failed to perform the requested conversion */
        }
    }
  iconv_close (cd);
  if (alt_cd != icNONE)
    iconv_close (alt_cd);
  return true;

}

/// Get POSIX locale strerror.
const char*
strerror (int errno_num)
{
  const int old_errno = errno;
  const char *result;
  {
    Lib::ScopedPosixLocale posix_locale_scope; // pushes POSIX/C locale for this scope
    result = ::strerror (errno_num);
  }
  errno = old_errno;
  return result;
}

const char*
strerror()
{
  return strerror (errno);
}

} // Ase

#include "testing.hh"

namespace { // Anon
using namespace Ase;

TEST_INTEGRITY (string_tests);
static void
string_tests()
{
  const char *s;
  s = "abcabc"; TASSERT (strrstr (s, "bc") == s + 4);
  TASSERT (Ase::kvpair_key ("foo=bar=baz") == "foo");
  TASSERT (Ase::kvpair_value ("foo=bar=baz") == "bar=baz");
  StringS sv;
  sv = string_split_any ("a, b, c", ", ");
  TCMP (string_join (";", sv), ==, "a;;b;;c");
  sv = string_split_any ("a, b, c", ", ", 1);
  TCMP (string_join (";", sv), ==, "a; b, c");
  sv = string_split_any ("abcdef", "");
  TCMP (string_join (";", sv), ==, "a;b;c;d;e;f");
  sv = string_split_any ("abcdef", "", 2);
  TCMP (string_join (";", sv), ==, "a;b;cdef");
  sv = string_split_any ("  foo  , bar     , \t\t baz \n", ",");
  TCMP (string_join (";", sv), ==, "  foo  ; bar     ; \t\t baz \n");
  TASSERT (string_option_check (":foo:", "foo") == true);
  TASSERT (string_option_check (":foo9:", "foo9") == true);
  TASSERT (string_option_check (":foo7:", "foo9") == false);
  TASSERT (string_option_check (":bar:", "bar") == true);
  TASSERT (string_option_check (":bar=:", "bar") == true);
  TASSERT (string_option_find (":bar:", "bar") == "1");
  TASSERT (string_option_check (":bar=0:", "bar") == false);
  TASSERT (string_option_find (":bar=0:", "bar") == "0");
  TASSERT (string_option_find (":no-bar:", "bar") == "");
  TASSERT (string_option_check (":bar=no:", "bar") == false);
  TASSERT (string_option_check (":bar=false:", "bar") == false);
  TASSERT (string_option_check (":bar=off:", "bar") == false);
  TASSERT (string_option_check (":bar=1:", "bar") == true);
  TASSERT (string_option_check (":bar=2:", "bar") == true);
  TASSERT (string_option_check (":bar=3:", "bar") == true);
  TASSERT (string_option_check (":bar=4:", "bar") == true);
  TASSERT (string_option_check (":bar=5:", "bar") == true);
  TASSERT (string_option_check (":bar=6:", "bar") == true);
  TASSERT (string_option_check (":bar=7:", "bar") == true);
  TASSERT (string_option_check (":bar=8:", "bar") == true);
  TASSERT (string_option_check (":bar=9:", "bar") == true);
  TASSERT (string_option_check (":bar=09:", "bar") == true);
  TASSERT (string_option_check (":bar=yes:", "bar") == true);
  TASSERT (string_option_check (":bar=true:", "bar") == true);
  TASSERT (string_option_check (":bar=on:", "bar") == true);
  TASSERT (string_option_check (":bar=1false:", "bar") == true);
  TASSERT (string_option_check (":bar=0true:", "bar") == false);
  TASSERT (string_option_check (":foo:", "foo") == true);
  TASSERT (string_option_check (":foo9:", "foo9") == true);
  TASSERT (string_option_check (":foo7:", "foo9") == false);
  TASSERT (string_option_check (":bar:", "bar") == true);
  TASSERT (string_option_check (":bar=:", "bar") == true);
  TASSERT (string_option_check (":bar=0:", "bar") == false);
  TASSERT (string_option_check (":bar=no:", "bar") == false);
  TASSERT (string_option_check (":bar=false:", "bar") == false);
  TASSERT (string_option_check (":bar=off:", "bar") == false);
  TASSERT (string_option_check (":bar=1:", "bar") == true);
  TASSERT (string_option_check (":bar=2:", "bar") == true);
  TASSERT (string_option_check (":bar=3:", "bar") == true);
  TASSERT (string_option_check (":bar=4:", "bar") == true);
  TASSERT (string_option_check (":bar=5:", "bar") == true);
  TASSERT (string_option_check (":bar=6:", "bar") == true);
  TASSERT (string_option_check (":bar=7:", "bar") == true);
  TASSERT (string_option_check (":bar=8:", "bar") == true);
  TASSERT (string_option_check (":bar=9:", "bar") == true);
  TASSERT (string_option_check (":bar=09:", "bar") == true);
  TASSERT (string_option_check (":bar=yes:", "bar") == true);
  TASSERT (string_option_check (":bar=true:", "bar") == true);
  TASSERT (string_option_check (":bar=on:", "bar") == true);
  TASSERT (string_option_check (":bar=1false:", "bar") == true);
  TASSERT (string_option_check (":bar=0true:", "bar") == false);
  String r;
  r = string_option_find ("a:b", "a"); TCMP (r, ==, "1");
  r = string_option_find ("a:b", "b"); TCMP (r, ==, "1");
  r = string_option_find ("a:b", "c", "0"); TCMP (r, ==, "0");
  r = string_option_find ("a:b", "c", "7"); TCMP (r, ==, "7");
  r = string_option_find ("a:no-b", "b", "0"); TCMP (r, ==, "0");
  r = string_option_find ("no-a:b", "a", "0"); TCMP (r, ==, "0");
  r = string_option_find ("no-a:b:a", "a"); TCMP (r, ==, "1");
  r = string_option_find ("no-a:b:a=5", "a"); TCMP (r, ==, "5");
  r = string_option_find ("no-a:b:a=5:c", "a"); TCMP (r, ==, "5");
  bool b;
  b = string_option_check ("", "a"); TCMP (b, ==, false);
  b = string_option_check ("a:b:c", "a"); TCMP (b, ==, true);
  b = string_option_check ("no-a:b:c", "a"); TCMP (b, ==, false);
  b = string_option_check ("no-a:b:a=5:c", "b"); TCMP (b, ==, true);
  b = string_option_check ("x:all", ""); TCMP (b, ==, false); // must have feature?
  TASSERT (typeid_name<int>() == String ("int"));
  TASSERT (typeid_name<bool>() == String ("bool"));
  TASSERT (typeid_name<::Ase::Strings>() == String ("Ase::Strings"));
  TASSERT (string_from_double (1.0) == "1");
  TASSERT (string_from_double (-1.0) == "-1");
  TASSERT (string_from_double (0.0) == "0");
  TASSERT (string_from_double (0.5) == "0.5");
  TASSERT (string_from_double (-0.5) == "-0.5");
  TASSERT (string_to_int ("-1") == -1);
  TASSERT (string_to_int ("9223372036854775807") == 9223372036854775807LL);
  TASSERT (string_to_int ("-9223372036854775808") == -9223372036854775807LL - 1);
  TASSERT (string_to_uint ("0") == 0);
  TASSERT (string_to_uint ("1") == 1);
  TASSERT (string_to_uint ("18446744073709551615") == 18446744073709551615ULL);
  TASSERT (string_to_bool ("0") == false);
  TASSERT (string_to_bool ("1") == true);
  TASSERT (string_to_bool ("true") == true);
  TASSERT (string_to_bool ("false") == false);
  TASSERT (string_to_bool ("on") == 1);
  TASSERT (string_to_bool ("off") == 0);
  TCMP (string_to_cquote ("\""), ==, "\"\\\"\"");
  TCMP (string_to_cquote ("\1"), ==, "\"\\001\"");
  TCMP (string_to_cquote ("A\nB"), ==, "\"A\\nB\"");
  TASSERT (string_startswith ("foo", "fo") == true);
  TASSERT (string_startswith ("foo", "o") == false);
  TASSERT (string_match_identifier_tail ("x.FOO", "Foo") == true);
  TASSERT (string_match_identifier_tail ("x.FOO", "X-Foo") == true);
  TASSERT (string_match_identifier_tail ("xFOO", "Foo") == false);
  TASSERT (string_is_uuid ("c18888f8-f026-4f70-92dd-78d4b16e54d5") == true);
  TASSERT (string_cmp_uuid ("c18888f8-f026-4f70-92dd-78D4B16E54D5", "C18888F8-f026-4f70-92dd-78d4b16e54d5") == 0);
  TASSERT (string_cmp_uuid ("c18888f8-f026-4f70-92dd-78d4b16e54d4", "c18888f8-f026-4f70-92dd-78d4b16e54d5") < 0);
  TASSERT (string_cmp_uuid ("c18888f8-f026-4f70-92dd-78d4b16e54d5", "c18888f8-f026-4f70-92dd-78d4b16e54d4") > 0);
  TCMP (string_url_encode ("x + z"), ==, "x%20%2B%20z");
  TCMP (string_url_encode ("x + z", true), ==, "x+%2B+z");
  TCMP (string_url_decode ("x%20%2B%20z"), ==, "x + z");
  TCMP (string_url_decode ("x+%2B+z"), ==, "x+++z");
  TCMP (string_url_decode ("x+%2B+z", true), ==, "x + z");
}

} // Anon
