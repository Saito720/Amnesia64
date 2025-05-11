#ifndef SYSTEM_TYPE_H_INCLUDED
#define SYSTEM_TYPE_H_INCLUDED
#include <cstddef>

#define ARRAY_COUNT( array ) (sizeof( array ) / (sizeof( array[0])))

#if defined(_MSC_VER)

#define COMPILER_ASSERT(exp)
#define COMPILER_ASSERT_MSG(exp, msg)

#else

#if !defined(__cplusplus)
#define COMPILER_ASSERT(exp) _Static_assert(exp, #exp)
#else
#define COMPILER_ASSERT(exp) static_assert(exp, #exp)
#endif

#if !defined(__cplusplus)
#define COMPILER_ASSERT_MSG(exp, msg) _Static_assert(exp, msg)
#else
#define COMPILER_ASSERT_MSG(exp, msg) static_assert(exp, msg)
#endif
#endif

template <class P, class M> size_t hpl_offsetof(const M P::*member) {
  return (size_t)&(reinterpret_cast<P *>(0)->*member);
}

template <class P, class M> P *hpl_container_of(M *ptr, const M P::*member) {
  return (P *)((char *)ptr - hpl_offsetof(member));
}

#endif

