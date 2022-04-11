#include <type_traits>

#define STATIC_ASSERT(PRED) \
{ const char _code_static_assert[(PRED) ? 1 : -1] = {0}; }

namespace workarounds {

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

template <class F, class... Args>
invoke_result_t<F, Args...>
invoke(F &&f, Args &&... args) {
		return std::forward<F>(f)(std::forward<Args>(args)...);
}

namespace detail {
template <class F, class Tuple, std::size_t... I>
decltype(auto) apply_impl(F &&f, Tuple &&t, std::index_sequence<I...>) {
	return workarounds::invoke(std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);
}
} // namespace detail

template <class F, class Tuple> constexpr decltype(auto) apply(F &&f, Tuple &&t) {
	return detail::apply_impl(
	    std::forward<F>(f), std::forward<Tuple>(t),
	    std::make_index_sequence<std::tuple_size<std::remove_reference_t<Tuple>>::value>{});
}

// Scoped lock

class scoped_lock {
	struct value_holder {
		virtual ~value_holder() noexcept {}
	};

	template <typename... Args> struct templated_value_holder : value_holder {
		template <std::size_t I = 0, typename... Tp>
		inline typename std::enable_if<I == sizeof...(Tp), void>::type
		unlock(std::tuple<Tp...> &t) {}

		template <std::size_t I = 0, typename... Tp>
		    inline typename std::enable_if <
		    I<sizeof...(Tp), void>::type unlock(std::tuple<Tp...> &t) {
			std::get<I>(t).unlock();
			unlock<I + 1, Tp...>(t);
		}

		explicit templated_value_holder(Args &... mutexes) : _mutexes(mutexes...) {
			std::lock(mutexes...);
		}
		explicit templated_value_holder(std::adopt_lock_t, Args &... mutexes)
		    : _mutexes(mutexes...) {} // construct but don't lock

		virtual ~templated_value_holder() noexcept { unlock(_mutexes); }

		std::tuple<Args &...> _mutexes;
	};

	template <typename Arg> struct templated_value_holder<Arg> : value_holder {
		explicit templated_value_holder(Arg &mutex) : _mutex(mutex) { _mutex.lock(); }
		explicit templated_value_holder(std::adopt_lock_t, Arg &mutex)
		    : _mutex(mutex) {} // construct but don't lock

		virtual ~templated_value_holder() noexcept { _mutex.unlock(); }

		Arg &_mutex;
	};

	value_holder *_val;

public:
	template <typename... Args>
	explicit scoped_lock(Args &... mutexes)
	    : _val(new templated_value_holder<Args &...>(mutexes...)) {}

	template <typename... Args>
	explicit scoped_lock(std::adopt_lock_t, Args &... mutexes)
	    : _val(new templated_value_holder<Args &...>(std::adopt_lock, mutexes...)) {}

	explicit scoped_lock(scoped_lock &&other) : _val(other._val) { other._val = nullptr; }

	scoped_lock() noexcept : _val(nullptr) {}
	~scoped_lock() noexcept { delete _val; }

	scoped_lock &operator=(scoped_lock &&other) {
		_val = other._val;
		other._val = nullptr;
		return *this;
	}

	scoped_lock(const scoped_lock &) = delete;
	scoped_lock &operator=(const scoped_lock &) = delete;
};



} // namespace workarounds
