// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __ASE_STRINGS_HH__
#define __ASE_STRINGS_HH__

#include <ase/cxxaux.hh>
#include <ase/formatter.hh>
#include <cstring>              // strerror

namespace Ase {
typedef std::string String;

#define ASE_STRING_SET_ASCII_ALNUM      "0123456789" "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdefghijklmnopqrstuvwxyz"
#define ASE_STRING_SET_LOWER_ALNUM      "0123456789" "abcdefghijklmnopqrstuvwxyz"

// == C-String ==
bool    		       cstring_to_bool       (const char *string, bool fallback = false);
const char*                    strrstr               (const char *haystack, const char *needle);

// == String Formatting ==
template<class... Args> String string_format         (const char *format, const Args &...args) ASE_PRINTF (1, 0);
template<class... Args> String string_locale_format  (const char *format, const Args &...args) ASE_PRINTF (1, 0);
String                         string_vprintf        (const char *format, va_list vargs);
String                         string_locale_vprintf (const char *format, va_list vargs);

// == String ==
String              string_multiply           (const String &s, uint64 count);
String              string_canonify           (const String &s, const String &valid_chars, const String &substitute);
String              string_to_identifier      (const String &input);
bool                string_is_canonified      (const String &s, const String &valid_chars);
const String&       string_set_a2z            ();
const String&       string_set_A2Z            ();
const String&       string_set_ascii_alnum    ();
bool                string_is_ascii_alnum     (const String &str);
bool     	    string_islower            (const String &str);
String  	    string_tolower            (const String &str);
bool      	    string_isupper            (const String &str);
String  	    string_toupper            (const String &str);
String  	    string_totitle            (const String &str);
String              string_capitalize         (const String &str, size_t maxn = size_t (-1), bool rest_tolower = true);
StringS             string_split              (const String &string, const String &splitter = "", size_t maxn = size_t (-1));
StringS       	    string_split_any          (const String &string, const String &splitchars = "", size_t maxn = size_t (-1));
String  	    string_join               (const String &junctor, const StringS &strvec);
bool    	    string_to_bool            (const String &string, bool fallback = false);
String  	    string_from_bool          (bool value);
uint64  	    string_to_uint            (const String &string, size_t *consumed = NULL, uint base = 10);
String  	    string_from_uint          (uint64 value);
bool    	    string_has_int            (const String &string);
int64   	    string_to_int             (const String &string, size_t *consumed = NULL, uint base = 10);
String  	    string_from_int           (int64 value);
String  	    string_from_float         (float value);
long double  	    string_to_long_double     (const String &string);
long double  	    string_to_long_double     (const char *dblstring, const char **endptr);
String              string_from_long_double   (long double value);
double  	    string_to_double          (const String &string);
double  	    string_to_double          (const char *dblstring, const char **endptr);
String              string_from_double        (double value);
inline String       string_from_float         (double value)         { return string_from_double (value); }
inline double       string_to_float           (const String &string) { return string_to_double (string); }
std::vector<double> string_to_double_vector   (const String         &string);
String              string_from_double_vector (const std::vector<double> &dvec,
                                               const String         &delim = " ");
double   	    string_to_seconds         (const String &string, double fallback = NAN);
String  	    string_from_errno         (int         errno_val);
bool                string_is_uuid            (const String &uuid_string); /* check uuid formatting */
int                 string_cmp_uuid           (const String &uuid_string1,
                                               const String &uuid_string2); /* -1=smaller, 0=equal, +1=greater (assuming valid uuid strings) */
bool                string_startswith         (const String &string, const String &fragment);
bool                string_startswith         (const String &string, const StringS &fragments);
bool                string_endswith           (const String &string, const String &fragment);
bool                string_endswith           (const String &string, const StringS &fragments);
bool    string_match_identifier                          (const String &ident1, const String &ident2);
bool    string_match_identifier_tail                     (const String &ident, const String &tail);
String  string_from_pretty_function_name                 (const char *cxx_pretty_function);
String  string_to_cescape                                (const String &str);
String  string_to_cquote                                 (const String &str);
String  string_from_cquote                               (const String &input);
String  string_url_decode                                (const String &urlstr, bool form_url_encoded = false);
String  string_url_encode                                (const String &rawstr, bool form_url_encoded = false);
String  string_to_hex                                    (const String &input);
String  string_hexdump                                   (const void *addr, size_t length, size_t initial_offset = 0);
String  string_lstrip                                    (const String &input);
String  string_rstrip                                    (const String &input);
String  string_strip                                     (const String &input);
String  string_replace                                   (const String &input, const String &marker, const String &replacement, size_t maxn = ~size_t (0));
String  string_substitute_char                           (const String &input, const char match, const char subst);
void    string_vector_lstrip       (StringS &svector);
void    string_vector_rstrip       (StringS &svector);
void    string_vector_strip        (StringS &svector);
void    string_vector_erase_empty  (StringS &svector);
String  string_vector_find         (const StringS &svector, const String &prefix, const String &fallback = "");
String  string_vector_find_value   (const StringS &svector, const String &prefix, const String &fallback = "");
StringS      cstrings_to_vector    (const char*, ...) ASE_SENTINEL;
void         memset4		   (uint32 *mem, uint32 filler, uint length);
long double posix_locale_strtold   (const char *nptr, char **endptr);
long double current_locale_strtold (const char *nptr, char **endptr);

StringS strings_version_sort (const StringS &strings, bool reverse = false);
void    strings_version_sort (StringS *strings, bool reverse = false);

// == UTF-8 String Helpers ==
String string_normalize_nfc  (const String &src);                       // Normalized, composed form UTF-8 string
String string_normalize_nfd  (const String &src);
String string_normalize_nfkc (const String &src);
String string_normalize_nfkd (const String &src);
String string_casefold       (const String &src);
int    string_casecmp        (const String &s1, const String &s2);      // UTF-8 version of strcasecmp(3)
int    string_cmp            (const String &s1, const String &s2);      // UTF-8 version of strcmp(3)

// == Templated String Conversions ==

/// Convert a @a string to template argument type, such as bool, int, double.
template<typename Type> Type    string_to_type           (const String &string)
{ static_assert (!sizeof (Type), "string_to_type<>: unsupported Type");  __builtin_unreachable(); }

/// Create a @a string from a templated argument value, such as bool, int, double.
template<typename Type> String  string_from_type         (Type          value)
{ static_assert (!sizeof (Type), "string_from_type<>: unsupported Type");  __builtin_unreachable(); }

// specialisations for templated string conversions
template<> inline long double   string_to_type<long double>   (const String &string) { return string_to_long_double (string); }
template<> inline String        string_from_type<long double> (long double    value) { return string_from_long_double (value); }
template<> inline double        string_to_type<double>   (const String &string) { return string_to_double (string); }
template<> inline String        string_from_type<double> (double         value) { return string_from_double (value); }
template<> inline float         string_to_type<float>    (const String &string) { return string_to_float (string); }
template<> inline String        string_from_type<float>  (float         value)  { return string_from_float (value); }
template<> inline bool          string_to_type<bool>     (const String &string) { return string_to_bool (string); }
template<> inline String        string_from_type<bool>   (bool         value)   { return string_from_bool (value); }
template<> inline int16         string_to_type<int16>    (const String &string) { return string_to_int (string); }
template<> inline String        string_from_type<int16>  (int16         value)  { return string_from_int (value); }
template<> inline uint16        string_to_type<uint16>   (const String &string) { return string_to_uint (string); }
template<> inline String        string_from_type<uint16> (uint16        value)  { return string_from_uint (value); }
template<> inline int           string_to_type<int>      (const String &string) { return string_to_int (string); }
template<> inline String        string_from_type<int>    (int         value)    { return string_from_int (value); }
template<> inline uint          string_to_type<uint>     (const String &string) { return string_to_uint (string); }
template<> inline String        string_from_type<uint>   (uint           value) { return string_from_uint (value); }
template<> inline int64         string_to_type<int64>    (const String &string) { return string_to_int (string); }
template<> inline String        string_from_type<int64>  (int64         value)  { return string_from_int (value); }
template<> inline uint64        string_to_type<uint64>   (const String &string) { return string_to_uint (string); }
template<> inline String        string_from_type<uint64> (uint64         value) { return string_from_uint (value); }
template<> inline String        string_to_type<String>   (const String &string) { return string; }
template<> inline String        string_from_type<String> (String         value) { return value; }


// == String Options ==
bool             string_option_check      (const String &optionlist, const String &feature);
String           string_option_find       (const String &optionlist, const String &feature, const String &fallback = "");
std::string_view string_option_find_value (const char *string, const char *feature, const String &fallback, const String &denied, bool matchallnone);

// == Generic Key-Value-Pairs ==
String  kvpair_key      (const String &key_value_pair);
String  kvpair_value    (const String &key_value_pair);
String  kvpairs_fetch   (const StringS &kvs, const String &key, bool casesensitive = true);
ssize_t kvpairs_search  (const StringS &kvs, const String &key, bool casesensitive = true);
bool    kvpairs_assign  (StringS &kvs, const String &key_value_pair, bool casesensitive = true);

// == Strings ==
/// Convenience Constructor for StringSeq or std::vector<std::string>
class Strings : public std::vector<std::string>
{
  typedef const std::string CS;
public:
  explicit Strings (CS &s1);
  explicit Strings (CS &s1, CS &s2);
  explicit Strings (CS &s1, CS &s2, CS &s3);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9, CS &sA);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9, CS &sA, CS &sB);
  explicit Strings (CS &s1, CS &s2, CS &s3, CS &s4, CS &s5, CS &s6, CS &s7, CS &s8, CS &s9, CS &sA, CS &sB, CS &sC);
};

// == Charset Conversions ==
bool    text_convert    (const String &to_charset,
                         String       &output_string,
                         const String &from_charset,
                         const String &input_string,
                         const String &fallback_charset = "ISO-8859-15",
                         const String &output_mark = "");

// == C strings ==
using         ::strerror;       // introduce (const char* strerror (int))
const char*     strerror ();    // simple wrapper for strerror (errno)

// == Implementations ==
#define ASE_STRING_VECTOR_FROM_ARRAY(ConstCharArray)               ({ \
  Ase::StringS __a;                                           \
  const Ase::uint64 __l = ASE_ARRAY_SIZE (ConstCharArray);    \
  for (Ase::uint64 __ai = 0; __ai < __l; __ai++)                   \
    __a.push_back (ConstCharArray[__ai]);                               \
  __a; })
#define ASE_CQUOTE(str)    (Ase::string_to_cquote (str).c_str())

/// Formatted printing ala printf() into a String, using the POSIX/C locale.
template<class... Args> ASE_NOINLINE String
string_format (const char *format, const Args &...args)
{
  return Lib::StringFormatter::format (NULL, format, args...);
}

/// Formatted printing ala printf() into a String, using the current locale.
template<class... Args> ASE_NOINLINE String
string_locale_format (const char *format, const Args &...args)
{
  return Lib::StringFormatter::format<Lib::StringFormatter::CURRENT_LOCALE> (NULL, format, args...);
}

} // Ase

#endif  // __ASE_STRINGS_HH__

