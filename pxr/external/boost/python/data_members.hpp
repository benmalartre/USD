//
// Copyright 2024 Pixar
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// Copyright David Abrahams 2002.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
#ifndef PXR_EXTERNAL_BOOST_PYTHON_DATA_MEMBERS_HPP
# define PXR_EXTERNAL_BOOST_PYTHON_DATA_MEMBERS_HPP

#include "pxr/pxr.h"
#include "pxr/external/boost/python/common.hpp"

# include "pxr/external/boost/python/detail/prefix.hpp"

# include "pxr/external/boost/python/handle.hpp"

# include "pxr/external/boost/python/return_value_policy.hpp"
# include "pxr/external/boost/python/return_by_value.hpp"
# include "pxr/external/boost/python/return_internal_reference.hpp"
# include "pxr/external/boost/python/make_function.hpp"
# include "pxr/external/boost/python/type_list.hpp"

# include "pxr/external/boost/python/converter/builtin_converters.hpp"

# include "pxr/external/boost/python/detail/indirect_traits.hpp"
# include "pxr/external/boost/python/detail/not_specified.hpp"
# include "pxr/external/boost/python/detail/value_arg.hpp"
# include "pxr/external/boost/python/detail/type_traits.hpp"

# include "pxr/external/boost/python/detail/mpl2/eval_if.hpp"
# include "pxr/external/boost/python/detail/mpl2/if.hpp"

namespace PXR_BOOST_NAMESPACE { namespace python { 

//
// This file defines the make_getter and make_setter function
// families, which are responsible for turning pointers, references,
// and pointers-to-data-members into callable Python objects which
// can be used for attribute access on wrapped classes.
//

namespace detail
{

  // A small function object which handles the getting and setting of
  // data members.
  template <class Data, class Class>
  struct member
  {
   public:      
      member(Data Class::*which) : m_which(which) {}
      
      Data& operator()(Class& c) const
      {
          return c.*m_which;
      }

      void operator()(Class& c, typename value_arg<Data>::type d) const
      {
          c.*m_which = d;
      }
   private:
      Data Class::*m_which;
  };

  // A small function object which handles the getting and setting of
  // non-member objects.
  template <class Data>
  struct datum
  {
   public:      
      datum(Data *which) : m_which(which) {}
      
      Data& operator()() const
      {
          return *m_which;
      }

      void operator()(typename value_arg<Data>::type d) const
      {
          *m_which = d;
      }
   private:
      Data *m_which;
  };
  
  //
  // Helper metafunction for determining the default CallPolicy to use
  // for attribute access.  If T is a [reference to a] class type X
  // whose conversion to python would normally produce a new copy of X
  // in a wrapped X class instance (as opposed to types such as
  // std::string, which are converted to native Python types, and
  // smart pointer types which produce a wrapped class instance of the
  // pointee type), to-python conversions will attempt to produce an
  // object which refers to the original C++ object, rather than a
  // copy.  See default_member_getter_policy for rationale.
  // 
  template <class T>
  struct default_getter_by_ref
      : detail::mpl2::and_<
          detail::mpl2::bool_<
              to_python_value<
                  typename value_arg<T>::type
              >::uses_registry
          >
        , indirect_traits::is_reference_to_class<
              typename value_arg<T>::type
          >
       >
  {
  };

  // Metafunction computing the default CallPolicy to use for reading
  // data members
  //
  // If it's a regular class type (not an object manager or other
  // type for which we have to_python specializations, use
  // return_internal_reference so that we can do things like
  //    x.y.z =  1
  // and get the right result.
  template <class T>
  struct default_member_getter_policy
    : detail::mpl2::if_<
          default_getter_by_ref<T>
        , return_internal_reference<>
        , return_value_policy<return_by_value>
      >
  {};

  // Metafunction computing the default CallPolicy to use for reading
  // non-member data.
  template <class T>
  struct default_datum_getter_policy
    : detail::mpl2::if_<
          default_getter_by_ref<T>
        , return_value_policy<reference_existing_object>
        , return_value_policy<return_by_value>
      >
  {};

  //
  // make_getter helper function family -- These helpers to
  // PXR_BOOST_NAMESPACE::python::make_getter are used to dispatch behavior.  The
  // third argument is a workaround for a CWPro8 partial ordering bug
  // with pointers to data members.  It should be convertible to
  // detail::true_ iff the first argument is a pointer-to-member, and
  // detail::false_ otherwise.  The fourth argument is for compilers
  // which don't support partial ordering at all and should always be
  // passed 0L.

  // Handle non-member pointers with policies
  template <class D, class Policies>
  inline object make_getter(D* d, Policies const& policies, detail::false_, int)
  {
      return python::make_function(
          detail::datum<D>(d), policies, python::type_list<D&>()
      );
  }
  
  // Handle non-member pointers without policies
  template <class D>
  inline object make_getter(D* d, not_specified, detail::false_, long)
  {
      typedef typename default_datum_getter_policy<D>::type policies;
      return detail::make_getter(d, policies(), detail::false_(), 0);
  }

  // Handle pointers-to-members with policies
  template <class C, class D, class Policies>
  inline object make_getter(D C::*pm, Policies const& policies, detail::true_, int)
  {
      typedef C Class;
      return python::make_function(
          detail::member<D,Class>(pm)
        , policies
        , python::type_list<D&,Class&>()
      );
  }
      
  // Handle pointers-to-members without policies
  template <class C, class D>
  inline object make_getter(D C::*pm, not_specified, detail::true_, long)
  {
      typedef typename default_member_getter_policy<D>::type policies;
      return detail::make_getter(pm, policies(), detail::true_(), 0);
  }

  // Handle references
  template <class D, class P>
  inline object make_getter(D& d, P& p, detail::false_, ...)
  {
      // Just dispatch to the handler for pointer types.
      return detail::make_getter(&d, p, detail::false_(), 0L);
  }

  //
  // make_setter helper function family -- These helpers to
  // PXR_BOOST_NAMESPACE::python::make_setter are used to dispatch behavior.  The
  // third argument is for compilers which don't support partial
  // ordering at all and should always be passed 0.
  //

  
  // Handle non-member pointers
  template <class D, class Policies>
  inline object make_setter(D* p, Policies const& policies, detail::false_, int)
  {
      return python::make_function(
          detail::datum<D>(p), policies, python::type_list<void,D const&>()
      );
  }

  // Handle pointers-to-members
  template <class C, class D, class Policies>
  inline object make_setter(D C::*pm, Policies const& policies, detail::true_, int)
  {
      return python::make_function(
          detail::member<D,C>(pm)
        , policies
        , python::type_list<void, C&, D const&>()
      );
  }

  // Handle references
  template <class D, class Policies>
  inline object make_setter(D& x, Policies const& policies, detail::false_, ...)
  {
      return detail::make_setter(&x, policies, detail::false_(), 0L);
  }
}

//
// make_getter function family -- build a callable object which
// retrieves data through the first argument and is appropriate for
// use as the `get' function in Python properties .  The second,
// policies argument, is optional.  We need both D& and D const&
// overloads in order be able to handle rvalues.
//
template <class D, class Policies>
inline object make_getter(D& d, Policies const& policies)
{
    return detail::make_getter(d, policies, detail::is_member_pointer<D>(), 0L);
}

template <class D, class Policies>
inline object make_getter(D const& d, Policies const& policies)
{
    return detail::make_getter(d, policies, detail::is_member_pointer<D>(), 0L);
}

template <class D>
inline object make_getter(D& x)
{
    detail::not_specified policy
        = detail::not_specified(); // suppress a SunPro warning
    return detail::make_getter(x, policy, detail::is_member_pointer<D>(), 0L);
}

template <class D>
inline object make_getter(D const& d)
{
    detail::not_specified policy
        = detail::not_specified(); // Suppress a SunPro warning
    return detail::make_getter(d, policy, detail::is_member_pointer<D>(), 0L);
}

//
// make_setter function family -- build a callable object which
// writes data through the first argument and is appropriate for
// use as the `set' function in Python properties .  The second,
// policies argument, is optional.  We need both D& and D const&
// overloads in order be able to handle rvalues.
//
template <class D, class Policies>
inline object make_setter(D& x, Policies const& policies)
{
    return detail::make_setter(x, policies, detail::is_member_pointer<D>(), 0);
}

template <class D, class Policies>
inline object make_setter(D const& x, Policies const& policies)
{
    return detail::make_setter(x, policies, detail::is_member_pointer<D>(), 0);
}

template <class D>
inline object make_setter(D& x)
{
    return detail::make_setter(x, default_call_policies(), detail::is_member_pointer<D>(), 0);
}

template <class D>
inline object make_setter(D const& x)
{
    return detail::make_setter(x, default_call_policies(), detail::is_member_pointer<D>(), 0);
}

}} // namespace PXR_BOOST_NAMESPACE::python

#endif // PXR_EXTERNAL_BOOST_PYTHON_DATA_MEMBERS_HPP
