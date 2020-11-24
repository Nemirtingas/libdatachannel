#include <type_traits>

namespace nonstd {
	// Implementation from: https://en.cppreference.com/w/cpp/types/result_of
namespace detail {
template <class T> struct is_reference_wrapper : std::false_type {};
template <class U> struct is_reference_wrapper<std::reference_wrapper<U>> : std::true_type {};

template <class T> struct invoke_impl {
	template <class F, class... Args>
	static auto call(F &&f, Args &&... args)
	    -> decltype(std::forward<F>(f)(std::forward<Args>(args)...));
};

template <class B, class MT> struct invoke_impl<MT B::*> {
	template <class T, class Td = typename std::decay<T>::type,
	          class = typename std::enable_if<std::is_base_of<B, Td>::value>::type>
	static auto get(T &&t) -> T &&;

	template <class T, class Td = typename std::decay<T>::type,
	          class = typename std::enable_if<is_reference_wrapper<Td>::value>::type>
	static auto get(T &&t) -> decltype(t.get());

	template <class T, class Td = typename std::decay<T>::type,
	          class = typename std::enable_if<!std::is_base_of<B, Td>::value>::type,
	          class = typename std::enable_if<!is_reference_wrapper<Td>::value>::type>
	static auto get(T &&t) -> decltype(*std::forward<T>(t));

	template <class T, class... Args, class MT1,
	          class = typename std::enable_if<std::is_function<MT1>::value>::type>
	static auto call(MT1 B::*pmf, T &&t, Args &&... args)
	    -> decltype((invoke_impl::get(std::forward<T>(t)).*pmf)(std::forward<Args>(args)...));

	template <class T>
	static auto call(MT B::*pmd, T &&t) -> decltype(invoke_impl::get(std::forward<T>(t)).*pmd);
};

template <class F, class... Args, class Fd = typename std::decay<F>::type>
auto INVOKE(F &&f, Args &&... args)
    -> decltype(invoke_impl<Fd>::call(std::forward<F>(f), std::forward<Args>(args)...));

} // namespace detail

// Conforming C++14 implementation (is also a valid C++11 implementation):
namespace detail {
template <typename AlwaysVoid, typename, typename...> struct invoke_result {};
template <typename F, typename... Args>
struct invoke_result<decltype(void(detail::INVOKE(std::declval<F>(), std::declval<Args>()...))), F,
                     Args...> {
	using type = decltype(detail::INVOKE(std::declval<F>(), std::declval<Args>()...));
};
} // namespace detail

template <class> struct result_of;
template <class F, class... ArgTypes>
struct result_of<F(ArgTypes...)> : detail::invoke_result<void, F, ArgTypes...> {};

template <class F, class... ArgTypes>
struct invoke_result : detail::invoke_result<void, F, ArgTypes...> {};

template <class F, class... ArgTypes>
using invoke_result_t = typename invoke_result<F, ArgTypes...>::type;
} // namespace nonstd

namespace rtc {
enum class byte : unsigned char {};

template <class _IntType, std::enable_if_t<std::is_integral_v<_IntType>, int> = 0>
constexpr byte
operator<<(const byte _Arg,
           const _IntType _Shift) noexcept { // bitwise LEFT SHIFT, every static_cast is intentional
	return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(_Arg) << _Shift));
}

template <class _IntType, std::enable_if_t<std::is_integral_v<_IntType>, int> = 0>
constexpr byte operator>>(
    const byte _Arg,
    const _IntType _Shift) noexcept { // bitwise RIGHT SHIFT, every static_cast is intentional
	return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(_Arg) >> _Shift));
}

constexpr byte
operator|(const byte _Left,
          const byte _Right) noexcept { // bitwise OR, every static_cast is intentional
	return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(_Left) |
	                                                    static_cast<unsigned int>(_Right)));
}

constexpr byte
operator&(const byte _Left,
          const byte _Right) noexcept { // bitwise AND, every static_cast is intentional
	return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(_Left) &
	                                                    static_cast<unsigned int>(_Right)));
}

constexpr byte
operator^(const byte _Left,
          const byte _Right) noexcept { // bitwise XOR, every static_cast is intentional
	return static_cast<byte>(static_cast<unsigned char>(static_cast<unsigned int>(_Left) ^
	                                                    static_cast<unsigned int>(_Right)));
}

constexpr byte
operator~(const byte _Arg) noexcept { // bitwise NOT, every static_cast is intentional
	return static_cast<byte>(static_cast<unsigned char>(~static_cast<unsigned int>(_Arg)));
}

template <class _IntType, std::enable_if_t<std::is_integral_v<_IntType>, int> = 0>
constexpr byte &operator<<=(byte &_Arg, const _IntType _Shift) noexcept { // bitwise LEFT SHIFT
	return _Arg = _Arg << _Shift;
}

template <class _IntType, std::enable_if_t<std::is_integral_v<_IntType>, int> = 0>
constexpr byte &operator>>=(byte &_Arg, const _IntType _Shift) noexcept { // bitwise RIGHT SHIFT
	return _Arg = _Arg >> _Shift;
}

constexpr byte &operator|=(byte &_Left, const byte _Right) noexcept { // bitwise OR
	return _Left = _Left | _Right;
}

constexpr byte &operator&=(byte &_Left, const byte _Right) noexcept { // bitwise AND
	return _Left = _Left & _Right;
}

constexpr byte &operator^=(byte &_Left, const byte _Right) noexcept { // bitwise XOR
	return _Left = _Left ^ _Right;
}

template <class _IntType, std::enable_if_t<std::is_integral_v<_IntType>, int> = 0>
constexpr _IntType to_integer(const byte _Arg) noexcept { // convert byte to integer
	return static_cast<_IntType>(_Arg);
}
} // namespace rtc
