#ifndef ASH_BASE_API_H
#define ASH_BASE_API_H

#if defined(__GNUC__)
#  define ASH_API      __attribute__((visibility("default")))
#  define ASH_LOCAL    __attribute__((visibility("hidden")))
#  define ASH_WUR      __attribute__((warn_unused_result))
#  define ASH_PRINTF(fmt_arg, first_var) \
       __attribute__((format(printf, fmt_arg, first_var)))
#  define ASH_NORETURN _Noreturn
#else
#  define ASH_API
#  define ASH_LOCAL
#  define ASH_WUR
#  define ASH_PRINTF(fmt_arg, first_var)
#  define ASH_NORETURN
#endif

#define ASH_IGNORE(expr) \
    do { __typeof__(expr) ash_ignored_ = (expr); (void)ash_ignored_; } while (0)

#endif
