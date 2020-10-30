#ifndef COMMON_MACROS_HPP
#define COMMON_MACROS_HPP

namespace pds{
#define INIT_EPOCH 3
#define NULL_EPOCH 0

#define ASSERT_DERIVE(der, base)\
    static_assert(std::is_convertible<der*, base*>::value,\
        #der " must inherit " #base " as public");

#define ASSERT_COPY(t)\
    static_assert(std::is_copy_constructible<t>::value,\
        "type" #t "requires copying");

}

#endif