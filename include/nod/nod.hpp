#ifndef IG_NOD_INCLUDE_NOD_HPP
#define IG_NOD_INCLUDE_NOD_HPP

#include <vector>       // std::vector
#include <functional>   // std::function
#include <mutex>        // std::mutex, std::lock_guard
#include <memory>       // std::shared_ptr, std::weak_ptr
#include <algorithm>    // std::find_if()
#include <cassert>      // assert()
#include <thread>       // std::this_thread::yield()

namespace nod {
	// implementational details
	namespace detail {
		/// Interface for type erasure when disconnecting slots
		struct disconnector {
			virtual void operator()( std::size_t index ) const = 0;
		};		
		/// Deleter that doesn't delete
		inline void no_delete(disconnector*){			
		};
	} // namespace detail

	/// Base template for the signal class
	template <class P, class T>
	class signal_type;


	/// Connection class.
	///
	/// This is used to be able to disconnect slots after they have been connected.
	/// Used as return type for the connect method of the signals.
	///
	/// Connections are default constructible.
	/// Connections are not copy constructible or copy assignable.
	/// Connections are move constructible and move assignable.
	///
	class connection {
		public:
			/// Default constructor
			connection() :
				_index()
			{}

			// Connection are not copy constructible or copy assignable
			connection( connection const& ) = delete;
			connection& operator=( connection const& ) = delete;

			/// Move constructor
			/// @param other   The instance to move from.
			connection( connection&& other ) :
				_weak_disconnector( std::move(other._weak_disconnector) ),
				_index( other._index )
			{}

			/// Move assign operator.
			/// @param other   The instance to move from.
			connection& operator=( connection&& other ) {
				_weak_disconnector = std::move( other._weak_disconnector );
				_index = other._index;
				return *this;
			}

			/// @returns `true` if the connection is connected to a signal object,
			///          and `false` otherwise.
			bool connected() const {
				return !_weak_disconnector.expired();
			}

			/// Disconnect the slot from the connection.
			///
			/// If the connection represents a slot that is connected to a signal object, calling
			/// this method will disconnect the slot from that object. The result of this operation
			/// is that the slot will stop recieving calls when the signal is invoked.
			void disconnect();

		private:
			/// The signal template is a friend of the connection, since it is the
			/// only one allowed to create instances using the meaningful constructor.
			template<class P,class T> friend class signal_type;	

			/// Create a connection.
			/// @param shared_disconnector   Disconnector instance that will be used to disconnect
			///                              the connection when the time comes. A weak pointer 
			///                              to the disconnector will be held within the connection
			///                              object.
			/// @param index                 The slot index of the connection.
			connection( std::shared_ptr<detail::disconnector> const& shared_disconnector, std::size_t index ) :
				_weak_disconnector( shared_disconnector ),
				_index( index )
			{}

			/// Weak pointer to the current disconnector functor.
			std::weak_ptr<detail::disconnector> _weak_disconnector;
			/// Slot index of the connected slot.
			std::size_t _index;
	};

	/// Scoped connection class.
	/// 
	/// This type of connection is automatically disconnected when
	/// the connection object is destructed.
	///
	class scoped_connection
	{
		public:
			/// Scoped are default constructible
			scoped_connection() = default;
			/// Scoped connections are not copy constructible
			scoped_connection( scoped_connection const& ) = delete;
			/// Scoped connections are not copy assingable
			scoped_connection& operator=( scoped_connection const& ) = delete;

			/// Move constructor
			scoped_connection( scoped_connection&& other ) :
				_connection( std::move(other._connection) )
			{}

			/// Construct a scoped connection from a connection object
			/// @param connection   The connection object to manage
			scoped_connection( connection&& c ) :
				_connection( std::forward<connection>(c) )
			{}

			/// destructor
			~scoped_connection() {
				disconnect();
			}

			/// Assignment operator moving a new connection into the instance.
			/// @note If the scoped_connection instance already contains a
			///       connection, that connection will be disconnected as if
			///       the scoped_connection was destroyed.
			/// @param c   New connection to manage
			scoped_connection& operator=( connection&& c ) {
				reset( std::forward<connection>(c) );
				return *this;
			}

			/// Reset the underlying connection to another connection.
			/// @note The connection currently managed by the scoped_connection
			///       instance will be disconnected when resetting.
			/// @param c   New connection to manage
			void reset( connection&& c = {} ) {
				disconnect();
				_connection = std::move(c);
			}

			/// Release the underlying connection, without disconnecting it.
			/// @returns The newly released connection instance is returned.
			connection release() {
				connection c = std::move(_connection);
				_connection = connection{};
				return c;
			}

			///
			/// @returns `true` if the connection is connected to a signal object,
			///          and `false` otherwise.
			bool connected() const {
				return _connection.connected();
			}

			/// Disconnect the slot from the connection.
			///
			/// If the connection represents a slot that is connected to a signal object, calling
			/// this method will disconnect the slot from that object. The result of this operation
			/// is that the slot will stop recieving calls when the signal is invoked.
			void disconnect() {
				_connection.disconnect();
			}

		private:
			/// Underlying connection object
			connection _connection;
	};

	/// Policy for multi threaded use of signals.
	///
	/// This policy provides mutex and lock types for use in
	/// a multithreaded environment, where signals and slots
	/// may exists in different threads.
	///
	/// This policy is used in the `nod::signal` type provided
	/// by the library.
	struct multithread_policy 
	{
		using mutex_type = std::mutex;
		using mutex_lock_type = std::lock_guard<mutex_type>;
		/// Function that yields the current thread, allowing
		/// the OS to reschedule.
		static void yield_thread() {
			std::this_thread::yield();
		}
	};

	/// Policy for single threaded use of singals.
	///
	/// This policy provides dummy implementations for mutex
	/// and lock types, resulting in that no syncronization
	/// will take place.
	///
	/// This policy is used in the `nod::unsafe_signal` type
	/// provided by the library.
	struct singlethread_policy
	{
		/// Dummy mutex type that doesn't do anything
		struct mutex_type{};
		/// Dummy lock type, that doesn't do any locking.
		struct mutex_lock_type
		{
			/// A lock type must be constructible from a
			/// mutex type from the same thread policy.
			explicit mutex_lock_type( mutex_type const& ) {
			}
		};
		/// Dummy implementation of thread yielding, that
		/// doesn't do any actual yielding.
		static void yield_thread() {
		}
	};

	/// Signal accumulator class template.
	///
	/// This acts sort of as a proxy for triggering a signal and
	/// accumulating the slot return values.
	///
	/// This class is not really intended to instansiate by client code.
	/// Instanses are aquired as return values of the method `accumulate()` 
	/// called on signals.
	///
	/// @tparam S      Type of signal. The signal_accumulator acts
	///                as a type of proxy for a signal instance of 
	///                this type.
	/// @tparam T      Type of initial value of the accumulate algorithm.
	///                This type must meet the requirements of `CopyAssignable`
	///                and `CopyConstructible`
	/// @tparam F      Type of accumulation function.
	/// @tparam A...   Argument types of the underlying signal type.
	///
	template <class S, class T, class F, class...A>
	class signal_accumulator
	{
		public:
			/// Result type when calling the accumulating function operator.
			using result_type = typename std::result_of<F(T, typename S::slot_type::result_type)>::type;

			/// Construct a signal_accumulator as a proxy to a given singal
			//
			/// @param signal   Signal instance.
			/// @param init     Initial value of the accumulate algorithm.
			/// @param func     Binary operation funcion object that will be
			///                 applied to all slot return values.
			///                 The signature of the funciton should be 
			///                 equivalent of the following:
			///                   `R func( T1 const& a, T2 const& b )`
			///                  - The signature does not need to have `const&`.
			///                  - The initial value, type `T`, must be implicitly
			///                    convertible to `R`
			///                  - The return type `R` must be implicitly convertible 
			///                    to type `T1`.
			///                  - The type `R` must be `CopyAssignable`.
			///                  - The type `S::slot_type::result_type` (return type of
			///                    the signals slots) must be implicitly convertible to
			///                    type `T2`.
			signal_accumulator( S const& signal, T init, F func ) :
				_signal( signal ),
				_init( init ),
				_func( func )
			{}

			/// Function call operator.
			///
			/// Calling this will trigger the underlying signal and accumulate
			/// all of the connected slots return values with the current 
			/// initial value and accumulator function.
			///
			/// When called, this will invoke the accumulator function will
			/// be called for each return value of the slots. The semantics
			/// are similar to the `std::accumulate` algorithm.
			///
			/// @param args   Arguments to propagate to the slots of the
			///               underlying when triggering the signal.
			result_type operator()( A&&... args ) const {
				return _signal.trigger_with_accumulator( _init, _func, std::forward<A>(args)... );
			}

		private:
			
			/// Reference to the underlying signal to proxy.
			S const& _signal;
			/// Initial value of the accumulate algorithm.
			T _init;
			/// Accumulator function.
			F _func;

	};

	/// Signal template specialization.
	///
	/// This is the main signal implementation, and it is used to
	/// implement the observer pattern whithout the overhead 
	/// boilerplate code that typically comes with it.
	///
	/// Any function or function object is considered a slot, and 
	/// can be connected to a signal instance, as long as the signature
	/// of the slot matches the signature of the signal.
	///
	/// @tparam P      Threading policy for the signal.
	///                A threading policy must provide two type definitions:
	///                 - P::mutex_type, this type will be used as a mutex
	///                   in the singal_type class template.
	///                 - P::mutex_lock_type, this type must implement a 
	///                   constructor that takes a P::mutex_type as a parameter,
	///                   and it must have the semantics of a scoped mutex lock
	///                   like std::lock_guard, i.e. locking in the constructor
	///                   and unlocking in the destructor.
	///                   
	/// @tparam R      Return value type of the slots connected to the signal.
	/// @tparam A...   Argument types of the slots connected to the signal.  
	template <class P, class R, class... A >
	class signal_type<P,R(A...)>
	{
		public:
			/// signals are not copy constructible
			signal_type( signal_type const& ) = delete;
			/// signals are not copy assignable
			signal_type& operator=( signal_type const& ) = delete;

			/// signals are default constructible
			signal_type() = default;

			// Destruct the signal object.
			~signal_type() {
				// If we are unlucky, some of the connected slots
				// might be in the process of disconnecting from other threads.
				// If this happens, we are risking to destruct the disconnector
				// object managed by our shared pointer before they are done 
				// disconnecting. This would be bad. To solve this problem, we
				// discard the shared pointer (that is pointing to the disconnector
				// object within our own instance), but keep a weak pointer to that
				// instance. We then stall the destruction until all other weak
				// pointers have released their "lock" (indicated by the fact that
				// we will get a nullptr when locking our weak pointer).
				std::weak_ptr<detail::disconnector> weak{_shared_disconnector};
				_shared_disconnector.reset();
				while( weak.lock() != nullptr )	{
					// we just yield here, allowing the OS to reschedule. We do
					// this until all threads has released the disconnector object.
					thread_policy::yield_thread();
				}
			}

			/// Type that will be used to store the slots for this signal type.
			using slot_type = std::function<R(A...)>;

			/// Connect a new slot to the signal.
			///
			/// The connected slot will be called every time the signal
			/// is triggered.
			/// @param slot   The slot to connect. This must be a callable with
			///               the same signature as the singal itself.
			/// @return       A connection object is returned, and can be used to
			///               disconnect the slot.
			template <class T>
			connection connect( T&& slot ) {
				mutex_lock_type lock{ _mutex };
				_slots.push_back( std::forward<T>(slot) );
				std::size_t index = _slots.size()-1;
				if( _shared_disconnector == nullptr ) {
					_disconnector = disconnector{ this };
					_shared_disconnector = std::shared_ptr<detail::disconnector>{&_disconnector, detail::no_delete};
				}
				return connection{ _shared_disconnector, index };
			}
	
			/// Function call operator.
			///
			/// Calling this is how the signal is triggered and the
			/// connected slots are called.
			///
			/// @note The slots will be called in the order they were 
			///       connected to the signal.
			///
			/// @param args   Arguments that will be propagated to the
			///               connected slots when they are called.
			void operator()( A const&... args ) const {
				mutex_lock_type lock{ _mutex };
				for( auto const& slot : _slots ) {
					if( slot ) {
						slot( args... );
					}
				}
			}

			/// Construct a accumulator proxy object for the signal.
			///
			/// The intended purpose of this function is to create a function
			/// object that can be used to trigger the signal and accumulate
			/// all the slot return values.
			///
			/// The algorithm used to accumulate slot return values is similar
			/// to `std::accumulate`. A given binary function is called for
			/// each return value with the parameters consisting of the
			/// return value of the accumulator function applied to the 
			/// previous slots return value, and the current slots return value.
			/// A inital value must be provided for the first slot return type.
			/// 
			/// @note This can only be used on signals that have slots with
			///       non-void return types, since we can't accumulate void
			///       values.
			///       
			/// @tparam T      The type of the initial value given to the accumulator.
			/// @tparam F      The accumulator function type.
			/// @param init    Initial value given to the accumulator.
			/// @param op      Binary operator function object to apply by the accumulator.
			///                The signature of the funciton should be 
			///                equivalent of the following:
			///                  `R func( T1 const& a, T2 const& b )`
			///                 - The signature does not need to have `const&`.
			///                 - The initial value, type `T`, must be implicitly
			///                   convertible to `R`
			///                 - The return type `R` must be implicitly convertible 
			///                   to type `T1`.
			///                 - The type `R` must be `CopyAssignable`.
			///                 - The type `S::slot_type::result_type` (return type of
			///                   the signals slots) must be implicitly convertible to
			///                   type `T2`.
			template <class T, class F>
			signal_accumulator<signal_type, T, F, A...> accumulate( T init, F op ) const {
				static_assert( std::is_same<R,void>::value == false, "Unable to accumulate slots with 'void' as return type." );
				return { *this, init, op };
			}

		private:
			template<class, class, class, class...> friend class signal_accumulator;
			/// Thread policy currently in use
			using thread_policy = P;
			/// Type of mutex, provided by threading policy
			using mutex_type = typename thread_policy::mutex_type;
			/// Type of mutex lock, provided by threading policy
			using mutex_lock_type = typename thread_policy::mutex_lock_type;

			template <class T, class F>
			typename signal_accumulator<signal_type, T, F, A...>::result_type trigger_with_accumulator( T value, F& func, A const&... args ) const {
				mutex_lock_type lock{ _mutex };
				for( auto const& slot : _slots ) {
					if( slot ) {
						value = func( value, slot( args... ) );
					}
				}
				return value;
			}

			/// Implementation of the disconnection operation.
			///
			/// This is private, and only called by the connection
			/// objects created when connecting slots to this signal.
			/// @param index   The slot index of the slot that should
			///                be disconnected.
			void disconnect( std::size_t index ) {
				mutex_lock_type lock( _mutex );				
				assert( _slots.size() > index );				
				_slots[ index ] = slot_type{};
				while( _slots.size()>0 && !_slots.back() ) {
					_slots.pop_back();
				}
			}
			
			/// Implementation of the shared disconnection state
			/// used by all connection created by signal instances.
			/// 
			/// This inherits the @ref detail::disconnector interface
			/// for type erasure.
			struct disconnector :
				detail::disconnector
			{
				/// Default constructor, resulting in a no-op disconnector.
				disconnector() :
					_ptr(nullptr)
				{}

				/// Create a disconnector that works with a given signal instance.
				/// @param ptr   Pointer to the signal instance that the disconnector
				///              should work with.
				disconnector( signal_type<P,R(A...)>* ptr ) :
					_ptr( ptr )
				{}

				/// Disconnect a given slot on the current signal instance.
				/// @note If the instance is default constructed, or created
				///       with `nullptr` as signal pointer this operation will
				///       effectively be a no-op.
				/// @param index   The index of the slot to disconnect.
				void operator()( std::size_t index ) const override {
					if( _ptr ) {
						_ptr->disconnect( index );
					}
				}

				/// Pointer to the current signal.
				signal_type<P,R(A...)>* _ptr;
			};

			/// Mutex to syncronize access to the slot vector
			mutable mutex_type _mutex;
			/// Vector of all connected slots
			std::vector<slot_type> _slots;
			/// Disconnector operation, used for executing disconnection in a
			/// type erased manner.
			disconnector _disconnector;
			/// Shared pointer to the disconnector. All connection objects has a 
			/// weak pointer to this pointer for performing disconnections.
			std::shared_ptr<detail::disconnector> _shared_disconnector;
	};

	// Implementation of the disconnect operation of the connection class
	inline void connection::disconnect() {
		auto ptr = _weak_disconnector.lock();
		if( ptr ) {
			(*ptr)( _index );
		}
		_weak_disconnector.reset();
	}

	/// Signal type that is safe to use in multithreaded environments,
	/// where the signal and slots exists in different threads.
	/// The multithreaded policy provides mutexes and locks to syncronize
	/// access to the signals internals. 
	///
	/// This is the reccomended signal type, even for single threaded
	/// environments.
	template <class T> using signal = signal_type<multithread_policy, T>;

	/// Signal type that is unsafe in multithreaded environments.
	/// No syncronizations are provided to the signal_type for accessing 
	/// the internals.
	///
	/// Only use this signal type if you are sure that your environment is
	/// single threaded and performance is of importance.
	template <class T> using unsafe_signal = signal_type<singlethread_policy, T>;
} // namespace nod

#endif // IG_NOD_INCLUDE_NOD_HPP