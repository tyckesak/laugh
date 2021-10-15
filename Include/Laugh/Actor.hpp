///
/// \file Actor.hpp
/// \brief Interface file to the Laughably Simple Actor Framework.
/// \author Josip Palavra
/// \copyright MIT License. Copyright (c)  2021  Josip Palavra
/// 

#ifndef Laugh_Actor_C317BB7F_D3FC_4A19_B6AB_CB26D5EEE948
#define Laugh_Actor_C317BB7F_D3FC_4A19_B6AB_CB26D5EEE948


#include <list>
#include <mutex>
#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <optional>
#include <iostream>
#include <type_traits>
#include <shared_mutex>
#include <unordered_map>
#include <condition_variable>

// Thank you cameron314 and co-contributors on Github!
#include <concurrentqueue/moodycamel/concurrentqueue.h>


namespace laugh
{


///
/// \brief Quality of reference binding to an Actor object managed
///        through an ActorRef.
///
/// This distinction between reference binding qualities is the same
/// found in the standard library: using `std::shared_ptr` prevents
/// a managed objects to be destroyed as long as the pointer is in
/// scope; `std::weak_ptr` does not.
///
enum struct ActorRefQuality
{
    Strong, Weak
};



struct Actor;
struct ActorContext;


///
/// \brief Types qualifying as Actors.
/// 
template <typename A>
concept ActorLike = std::is_convertible_v<std::remove_cvref_t<A>*, Actor*>
                 || std::is_same_v<Actor, std::remove_cvref_t<A>>;


template <ActorLike A, ActorRefQuality = ActorRefQuality::Strong>
struct ActorRef;
struct ActorCell;

///
/// \internal
/// \brief Mutex type used to lock \link Actor `Actor`s \endlink
///        in order to keep the invariant of sequential message
///        processing intact.
///
using ActorMutex = std::recursive_mutex;

template <typename A>
struct EventualResponse;

template <typename A>
struct MaybeLater;

struct Discard_t {};
struct Stash_t {};

constexpr inline auto Stash = Stash_t{};
constexpr inline auto Discard = Discard_t{};


///
/// \brief In-situ ability to invoke an object of type `C`
///        with argument types `Args...` as per the definition
///        of `std::invoke`.
///
/// \tparam C The object to be invoked. May be a pointer to
///           function, a pointer to member, a lambda,
///           a custom type with an `operator()`
///           overload or a reference thereof.
///
template <typename C, typename... Args>
concept Callable = requires(C f, Args&&... args)
{
    std::invoke(f, std::forward<Args>(args)...);
};


namespace detail
{
    template <typename A>
    struct TypeVessel
    {
        using type = A;
    };

    template <typename>
    struct DetectTemplateTemplate: std::false_type
    {
        template <template <typename...> typename>
        struct EqualToTemplateTemplate: std::false_type {};
    };

    template <template <typename...> typename Nested, typename... Targs>
    struct DetectTemplateTemplate<Nested<Targs...>>: std::true_type
    {
        using type = Nested<Targs...>;

        template <typename>
        struct ExtractFirst_s;

        template <template <typename...> typename T, typename A, typename... S>
        struct ExtractFirst_s<T<A, S...>>
        {
            using First = A;
        };

        template <template <typename...> typename S>
        using Remap = S<Targs...>;

        template <template <typename...> typename OtherNested, typename = void>
        struct EqualToTemplateTemplate: std::false_type {};

        template <template <typename...> typename OtherNested>
        requires std::is_same_v<Nested<Targs...>, OtherNested<Targs...>>
        struct EqualToTemplateTemplate<OtherNested>
            : std::true_type {};
    };

    template <typename A>
    auto DelayedReturnType_f()
    {
        if constexpr(DetectTemplateTemplate<A>::template EqualToTemplateTemplate<MaybeLater>::value)
        {
            return TypeVessel<typename DetectTemplateTemplate<A>
                                     ::template ExtractFirst_s<A>
                                     ::First>{};
        }
        else
        {
            return TypeVessel<A>{};
        }
    }

    template <typename A>
    using DelayedReturnType = typename decltype(DelayedReturnType_f<A>())::type;

}


///
/// \brief Ability of a type to delay the return value in response to the sender.
///
template <typename Nested>
concept DelayedReturn = requires
{
    // typename detail::DetectTemplateTemplate<Nested>::type;
    requires detail::DetectTemplateTemplate<Nested>
                   ::template EqualToTemplateTemplate<MaybeLater>::value;
};



///
/// \brief Crude thread pool with worker threads to pass messages
///        to their receivers.
/// \todo   - Allocator support.
///         - A more sophisticated scheduler than first come - first serve.
///
struct ActorContext
{
    friend Actor;


    ///
    /// \brief Interface class to hook into the worker thread
    ///        execution loop.
    ///
    /// This interface class is subclassed internally to
    /// achieve the message passing between actors.
    struct Task
    {
        virtual void Let() = 0;
        virtual ~Task() = default;
    };

    
    ///
    /// \brief Constructs a worker thread pool with the given
    ///        number of worker threads to do the message processing.
    ///
    ActorContext(int workers);
    ActorContext(ActorContext&) = delete;
   ~ActorContext();

    
    ///
    /// \brief Schedules a \link ActorContext::Task task \endlink
    ///        to be executed by the worker thread pool.
    ///
    void ScheduleMessage(std::unique_ptr<Task>&&);


    ///
    /// \brief Construct an Actor with arguments on the heap, and
    ///        hide it behind an ActorRef.
    ///
    /// The resulting actor will have no parent.
    ///        
    template <ActorLike A, typename... ConsArgs>
    ActorRef<A> Make(ConsArgs&&... args);


    template <ActorLike A>
    requires std::is_default_constructible_v<A>
      static
        auto ConstructDefault()
          -> std::unique_ptr<Actor>;

private:


    ///
    /// \brief Join all worker threads, which wait until all messages
    ///        have been processed and none are coming in.
    ///
    void Terminate();


    ///
    /// \brief Spawns one worker thread and registers it in
    ///        the ActorContext::m_threadsInfo mapping.
    ///
    void SpawnWorker();

    
    ///
    /// \brief The worker thread loop.
    /// \param isWorkerEnrolled Reference to a bool flag from the main
    ///        thread waiting on confirmation that this thread has
    ///        listed itself in the ActorContext::m_threadsInfo.
    void WorkerRun(std::atomic_flag* isWorkerEnrolled);


    struct TerminateMessage: Task
    {
        void Let() override;

        TerminateMessage(ActorContext& self);

        ActorContext& m_self;
    };


    struct ThreadContext
    {
        ThreadContext() = default;
        std::atomic<bool> m_waiting;
        std::thread m_worker;
    };
    

    /// Exclusive mutex to constrain access to the non-thread-safe
    /// ActorContext::m_threadsInfo map functions.
    std::mutex m_threadInfoBottleneck;
    std::unordered_map<std::thread::id, ThreadContext> m_threadsInfo;

    /// Flag indicating to prepare the worker pool for termination.
    std::atomic_flag m_terminationDesired;
    /// Notifies waiting worker threads that a new task has come in.
    std::condition_variable_any m_hasWork;
    /// Shared mutex linked to ActorContext::m_hasWork
    std::shared_mutex m_workSchedule;
    /// The message queue itself.
    moodycamel::ConcurrentQueue<std::unique_ptr<Task>> m_unassigned;
};


///
/// \brief Access to subroutines that require synchronization on the
/// actor.
///
/// Oftentimes, a fire-and-forget message to an actor ought to do
/// the job quite well. There are situations however where one finds
/// himself in need of synchronous direct access to the actor behind
/// an ActorRef, which requires the actor to stop processing messages
/// for the time being, so that you may get your live-operation on that
/// actor done undisturbed.
/// 
/// All operations in this class on a bare pointer to an Actor are safe
/// as long as the instance of this class is in scope; it locks
/// the actor for the time being until the lock's destructor is called.
///
struct ActorLock
{
    template <typename R>
    friend struct EventualResponse;
    friend struct ActorCell;
    friend struct Actor;

    ///
    /// \brief Immediately replaces the actor behind this
    ///        lock with another freshly constructed one.
    ///
    /// This lock assumes ownership of that actor and arranges
    /// the immediate deletion of the replaced actor.
    /// 
    /// \note Observe that the replacing actor need not be of the
    ///       same type as the old one, or even be aware of the
    ///       replaced actor's type at all.
    /// 
    /// \param ... The arguments forwarded to the constructor.
    ///
    /// \tparam A The new actor's type, which is then recorded verbatim
    ///         for default-constructibility.
    /// 
    template <ActorLike A, typename... ConsArgs>
    requires std::is_constructible_v<A, ConsArgs...>
        void Restart(ConsArgs&&...);


    ///
    /// \brief Immediately replaces the actor behind this
    ///        lock with another freshly constructed one.
    ///
    /// When an actor has been constructed before, its type is recorded
    /// and a pointer to a function is set if that type is
    /// default-constructible. If it is, this actor can be restarted
    /// without later knowledge of that type by anyone obtaining
    /// an ActorLock to this actor.
    ///
    void RestartWithDefaultConstructor();


    ///
    /// \brief Immediately replaces the actor behind this
    ///        lock with another unique actor.
    ///
    /// This lock assumes ownership of that actor and arranges
    /// the immediate deletion of the replaced actor.
    ///
    /// \sa ActorLock::RestartWithDefaultConstructor for details
    ///     about advantages of default-constructibility of actors
    ///     and about type recording.
    ///
    /// \note Observe that the replacing actor need not be of the
    ///       same type as the old one, or even be aware of the
    ///       replaced actor's type at all.
    ///
    /// \tparam A The new actor's type, which is then recorded verbatim
    ///         for default-constructibility.
    ///
    template <ActorLike A>
    requires std::is_default_constructible_v<A>
        void RestartWith(std::unique_ptr<A> freshActor);


    ///
    /// \sa ActorLock::RestartWith
    ///
    /// \sa ActorLock::RestartWithDefaultConstructor for details
    ///     about advantages of default-constructibility of actors
    ///     and about type recording.
    ///
    /// \tparam A The new actor's type, which is then recorded verbatim
    ///         for default-constructibility.
    ///
    template <ActorLike A>
    requires (not std::is_default_constructible_v<A>)
        void RestartWithNonDefaultConstructible(std::unique_ptr<A> freshActor);


    ///
    /// \brief Swaps the underlying actor with another one.
    ///
    /// Transfers ownership of the incoming actor to the lock
    /// and the ownership of the outgoing actor back to the caller.
    /// Since the incoming and outgoing actors' types may be completely
    /// unrelated, the type parameter needs to be mentioned explicitly
    /// here; and the parameter is upcast into an actor pointer.
    ///
    /// \note Make sure that you know which actor's types are stored
    ///       in the lock and your `unique_ptr` after this operation.
    ///       The two actors' types may be only related through their
    ///       Actor parent class if set up that way.
    ///
    /// \param[in, out] with The `unique_ptr` containing the actor
    ///                 to be hot-swapped in. Will contain the pointer
    ///                 to the swapped-out actor after this call or 
    ///                 `nullptr` if this lock did not own any actor.
    ///
    /// \tparam A The exact type of the incoming actor, required
    ///         for recording default-constructibility of the
    ///         incoming actor type.
    ///
    /// \sa ActorLock::RestartWithDefaultConstructor for details
    ///     about advantages of default-constructibility of actors
    ///     and about type recording.
    ///
    template <ActorLike A>
    requires std::is_default_constructible_v<A>
        void Swap(std::unique_ptr<Actor>& with);

    ///
    /// \sa ActorLock::Swap
    /// \sa ActorLock::RestartWithDefaultConstructor
    ///
    template <ActorLike A>
    requires (not std::is_default_constructible_v<A>)
        void SwapNonDefaultConstructible(std::unique_ptr<Actor>& with);


    ///
    /// \brief Returns a pointer to the managed actor for direct access.
    ///
    /// Access to the actor behind that pointer is safe as long as `this`
    /// lock is in scope and prevents that actor from processing any
    /// messages.
    ///
    /// Accessing members of that actor pointer while no lock is in
    /// place is not thread-safe and might lead to undefined behaviour.
    ///
    template <ActorLike A>
    A* Get() const;


private:

    ///
    /// \brief Implementation of become, outsourced to the lock
    ///        since becoming another actor requires synchronous
    ///        access to its reference as well - this condition
    ///        is however trivially satisfied inside of actors.
    ///
    template <ActorLike S>
          S& Become(std::unique_ptr<S> what);

    template <ActorLike S>
        void PostActorReset();

    ActorCell& m_cell;
    std::unique_lock<ActorMutex> m_lock;


    ///
    /// \brief Locks may only be constructed by the internal ActorCell
    ///        bookkeeping class.
    ///
    ActorLock(ActorCell&);

};


///
/// \brief Holds a reference to an actor, provides messaging abilities.
///
/// Objects of this class are held as references to actors instead
/// of bare pointers or other pointer wrapper objects, since they
/// communicate that access to the pointed-to object
/// is unfettered and may be done at any time. Actors however may
/// not be accessed directly in general, and must be passed messages
/// for them to do any work, which they then receive one at a time - 
/// preserving the synchronous nature only inside of an actor while
/// allowing for concurrent message passing and processing by a
/// multitude of actors. These objects come very cheap in terms of
/// memory compared to 'tough' `std::thread` objects, which may represent
/// one logical CPU thread or some other heavyweight string of execution
/// managed by the OS.
///
/// `ActorRef`s do not own their actor themselves, but rather point to
/// a reference-counted bookkeeping structure, which in turn owns the
/// actor. This double indirection allows for multiple `ActorRef`s to
/// point to a single ActorCell, which then makes hot actor changes
/// visible to all `ActorRef`s pointing to it, such as done by
/// ActorLock::Swap or ActorLock::Reset .
///
/// \note Observe that an ActorRef may freely be up-, down- or even
///       sidecast while copying, so that an ActorRef may mistakenly
///       believe its managed actor to be of one type when it is in fact
///       of a completely unrelated type.
///
/// \tparam A The type of actor pointed to by this. May not correspond
///         to the actual type of the pointed-to actor.
/// 
template <ActorLike A, ActorRefQuality RefQuality>
struct ActorRef
{
    template <ActorLike, ActorRefQuality>
    friend struct ActorRef;
    friend struct Actor;
    friend ActorCell;

    // Constructors {{{

    ActorRef(ActorRef<Actor> parent, ActorContext& context)
    requires (RefQuality == ActorRefQuality::Strong);

    ActorRef(std::nullptr_t, ActorContext& context)
    requires (RefQuality == ActorRefQuality::Strong);

    template <ActorLike B, ActorRefQuality Q>
    ActorRef(const ActorRef<B, Q>&);

    // template <ActorLike B, ActorRefQuality Q>
    // ActorRef(ActorRef<B, Q>&&);

    ActorRef() = default;

    // }}}

    // Direct (i.e. synchronous) Actor access {{{

    auto LockActor() const
      -> ActorLock
         requires (RefQuality == ActorRefQuality::Strong);

    template <ActorLike S = A>
    auto Get() const
      -> std::tuple<S*, ActorLock>
         requires (RefQuality == ActorRefQuality::Strong);

    // }}}

    // Observers {{{

    bool IsAutomaticallyResettable() const
         requires (RefQuality == ActorRefQuality::Strong);

    auto GetParent() const
      -> ActorRef<Actor>
         requires (RefQuality == ActorRefQuality::Strong);

    auto GetContext() const
      -> ActorContext&
         requires (RefQuality == ActorRefQuality::Strong);

    // }}}


    ///
    /// \brief Makes this reference point to another actor.
    ///
    /// \note The new actor must already be managed by some other
    ///       ActorRef.
    ///
    void Point(A&)
         requires (RefQuality == ActorRefQuality::Strong);


    // Variations on 'Bang' {{{


    ///
    /// \brief Sends a message along with required arguments to the
    ///        actor behind this reference. Fire-and-forget.
    ///
    /// \note Undefined behavior is provoked if the type of the actor
    ///       behind this reference is not compatible with the
    ///       the message.
    ///
    template <ActorLike S
            , typename R
            , typename... Params
            , typename... Args>
    requires Callable<R (S::*)(Params...), S&, std::remove_reference_t<Args>...>
          && std::is_convertible_v<A*, S*>
          && (RefQuality == ActorRefQuality::Strong)
        void Bang(R (S::* const msg)(Params...), Args&&... args) const;


    ///
    /// \brief Overload of `Bang` for const-qualified member functions.
    ///
    /// \note Undefined behavior is provoked if the type of the actor
    ///       behind this reference is not compatible with the
    ///       the message.
    ///
    template <ActorLike S
            , typename R
            , typename... Params
            , typename... Args>
    requires Callable<R (S::*)(Params...) const, const S&, std::remove_reference_t<Args>...>
          && std::is_convertible_v<A*, const S*>
          && (RefQuality == ActorRefQuality::Strong)
        void Bang(R (S::* const msg)(Params...) const, Args&&... args) const;


    // }}}


    ///
    /// \brief Constructs a child actor of the actor pointed to by
    ///        this ActorRef.
    ///
    template <ActorLike B
            , typename... ConsArgs>
    requires std::is_constructible_v<B, ConsArgs...>
          && (RefQuality == ActorRefQuality::Strong)
        auto Make(ConsArgs&&... args) const
          -> ActorRef<B>;


    template <ActorLike B, ActorRefQuality Q>
        bool operator ==(const ActorRef<B, Q>& other) const;


    template <ActorLike B, ActorRefQuality Q>
    requires std::is_convertible_v<B*, A*>
        auto operator =(const ActorRef<B, Q>& other);

    operator bool()
             requires (RefQuality == ActorRefQuality::Strong)
    { return bool{m_cell} && bool{m_cell->GetActor()}; }

private:

    // Variations on 'Ask' {{{

    template <typename R
            , ActorLike S
            , typename... Params
            , typename... Args>
    requires Callable<R (S::*)(Params...), S&, std::remove_reference_t<Args>...>
          && (RefQuality == ActorRefQuality::Strong)
        auto Ask(ActorRef<S> who
               , R (S::* const what)(Params...)
               , Args&&... with) const
          -> std::shared_ptr<EventualResponse<R>>;


    template <typename R
            , ActorLike S
            , typename... Params
            , typename... Args>
    requires Callable<R (S::*)(Params...) const, const S&, std::remove_reference_t<Args>...>
          && (RefQuality == ActorRefQuality::Strong)
        auto Ask(ActorRef<const S> who
               , R (S::* const what)(Params...) const
               , Args&&... with) const
          -> std::shared_ptr<EventualResponse<R>>;

    // }}}


    template <typename T>
    using PtrType = std::conditional_t<RefQuality == ActorRefQuality::Weak
                                     , std::weak_ptr<T>
                                     , std::shared_ptr<T>>;
    PtrType<ActorCell> m_cell;

    ActorRef(PtrType<ActorCell> cell);

};


///
/// \internal
/// \brief Bookkeeping structure used internally that owns the actor.
///
struct ActorCell
{
    friend ActorLock;

    // We can't actually set the self reference in the constructor
    // since the cell gets constructed in the constructor of a fresh
    // ActorRef, which itself needs to initialize the shared pointer
    // count first.
    ActorCell(const ActorRef<Actor>& parent
            , ActorContext* context);

    ActorCell(std::nullptr_t parent
            , ActorContext* context);

    template <ActorLike A = Actor>
    A* Get() const { return static_cast<A*>(m_actor.get()); }

    ActorLock LockActor();

    ///
    /// \brief Can this actor be reset to a clean object without explicit arguments?
    ///
    bool IsAutomaticallyResettable() const;

    template <ActorLike A = Actor>
    void Replace(std::unique_ptr<A>&& with);

    ActorRef<Actor> GetParent() const { return *m_parent; }
    ActorRef<Actor> GetSelfReference() const { return m_selfReference; }

    void SetSelfReference(ActorRef<Actor> a) { m_selfReference = ActorRef<Actor, ActorRefQuality::Weak>{a}; }

    ActorContext* GetContext() const { return m_context; }
    void SetContext(ActorContext* c) { m_context = c; }

    void TerminateDyingActor();

    std::unique_ptr<Actor>& GetActor() { return m_actor; }

    std::unique_ptr<ActorContext::Task> UnstashTask();
    void StashTask(std::unique_ptr<ActorContext::Task> task);

private:

    ActorRef<Actor, ActorRefQuality::Weak> m_selfReference;
    const std::optional<ActorRef<Actor, ActorRefQuality::Strong>> m_parent;
    std::unique_ptr<Actor> m_actor;
    std::unique_ptr<Actor> m_dyingActor;
    std::unique_ptr<Actor> (* m_defaultConstruct)();
    std::list<std::unique_ptr<ActorContext::Task>> m_stash;
    ActorMutex m_blocking;
    /// \brief Reference to context. Stays pretty much the same once set.
    ActorContext* m_context;
};


///
/// \brief Parent class for generic messaging capabilities to arbitrary
///        actors by messaging with Actor::Receive
///
struct GenericMessage
{
    virtual ~GenericMessage();
};


struct Actor
{
    friend struct ActorLock;

    ///
    /// \brief Actors - due to their hot-swap ability - are
    ///        not copyable, but movable.
    ///
    Actor(Actor&) = delete;


    ///
    /// \brief Reference to the worker thread pool this actor
    ///        is being worked in.
    ///
    ActorContext& GetContext() const { return Self().GetContext(); }

    ///
    /// \brief Obtains a strong ActorRef to self.
    /// \note Observe that the returned ActorRef is untyped
    ///       unless the type is explicitly specified
    ///       through the type argument.
    ///
    template <ActorLike A = Actor>
    ActorRef<A> Self() const { return ActorRef<A>{m_self}; }

    virtual ~Actor() = default;

    ///
    /// \sa GenericMessage
    ///
    virtual bool Receive(GenericMessage&) { return false; };

    struct ChildFailure
    {
        const std::exception_ptr m_why;
        const ActorRef<Actor>    m_failed;
    };

    virtual void OnChildFailed(const ChildFailure& fail)
    {
        std::rethrow_exception(fail.m_why);
    }

    virtual void OnConstructed() {}

    void Deactivate() { m_active = false; }
    void Activate() { m_active = true; }
    bool IsActive() { return m_active; }

protected:

    Actor() = default;


    ///
    /// \brief Ask another actor to do some work and get back to
    ///        the questioner with the returned result once finished.
    ///
    /// \sa ActorRef::Bang
    ///
    template <ActorLike S
            , typename R
            , typename... Params
            , typename... Args>
    requires Callable<R (S::*)(Params...), S&, Args...>
        auto Ask(ActorRef<S> who
               , R (S::* const what)(Params...)
               , Args&&... with)
          -> std::shared_ptr<EventualResponse<R>>;


    ///
    /// \brief Overload of `Ask` for const-qualified member functions.
    ///
    /// \sa ActorRef::Bang
    ///
    template <ActorLike S
            , ActorLike T
            , typename R
            , typename... Params
            , typename... Args>
    requires Callable<R (S::*)(Params...) const, const S&, Args...>
          && std::is_convertible_v<std::remove_reference_t<T>*, const S*>
        auto Ask(ActorRef<T> who
               , R (S::* const what)(Params...) const
               , Args&&... with)
          -> std::shared_ptr<EventualResponse<R>>;


    ///
    /// \brief Prepare this actor to be swapped out with another
    ///        actor once this actor is done processing the current message.
    ///
    /// This actor is immediately destroyed once it is finished processing
    /// the current message.
    ///
    /// \note Observe that the new actor's type may be completely unrelated
    ///       to this actor - although it certainly helps to be
    ///       related through a more concrete parent class than Actor.
    ///
    /// \sa ActorLock::RestartWithDefaultConstructor for advantages of
    ///     default constructible actor types.
    ///
    template <ActorLike S
            , typename... Args>
    requires std::is_constructible_v<S, Args...>
          S& Become(Args&&...) const;


    ///
    /// \brief Executes all messages that have been stashed using MaybeLater in the
    ///        order they have been stashed in.
    ///
    /// During this time, the actor is unresponsive and is processing all messages it
    /// has in its stash.
    ///
    /// \note Observe that the message types are compatible with the receiving actor's
    ///       interface as described in that respective actor's class declaration.
    ///       Failure to abide by interfaces might result in undefined behavior being provoked.
    ///
    void UnstashAll();


    ///
    /// \brief Unstashes the number of messages given in the argument, or - if
    ///        there are less messages stashed than it is requested to unstash - 
    ///        unstashes all messages.
    ///
    /// During this time, the actor is unresponsive and is processing all messages it
    /// has stashed.
    ///
    /// \sa Actor::UnstashAll
    ///
    void Unstash(int n = 1);


    ///
    /// \brief Prepare this actor to be swapped out with given
    ///        actor once this actor is done processing the current message.
    ///
    /// This actor is immediately destroyed once it is finished processing
    /// the current message.
    ///
    /// \note Observe that the new actor's type may be completely unrelated
    ///       to this actor - although it certainly helps to be
    ///       related through a more concrete parent class than Actor.
    ///
    /// \sa ActorLock::RestartWithDefaultConstructor for advantages of
    ///     default constructible actor types.
    ///
    /// \param what Unique pointer to a new actor to replace this one.
    ///        It is probably a bad idea for it to be `nullptr`.
    ///
    template <ActorLike S>
    requires std::is_default_constructible_v<S>
          S& Become(std::unique_ptr<S> what) const;


    ///
    /// \sa Actor::Become
    /// \sa ActorLock::RestartWithDefaultConstructor
    ///
    template <ActorLike S>
    requires (not std::is_default_constructible_v<S>)
          S& BecomeNonDefaultConstructible(std::unique_ptr<S> what) const;


    ///
    /// \sa ActorLock::Swap
    ///
    template <ActorLike S>
    requires std::is_default_constructible_v<S>
        void Swap(std::unique_ptr<Actor>& with) const;


    ///
    /// \sa ActorLock::Swap
    ///
    template <ActorLike S>
    requires (not std::is_default_constructible_v<S>)
        void SwapNonDefaultConstructible(std::unique_ptr<Actor>& with) const;

private:

    /// Does this actor receive messages?
    bool m_active = true;
    /// Weak actor reference to `this`
    ActorRef<Actor, ActorRefQuality::Weak> m_self;

};


template <typename A>
struct MaybeLater
{
    MaybeLater(const Stash_t):
        m_optret{std::nullopt}
      , m_keep{true}
    {
    }

    MaybeLater(const Discard_t):
        m_optret{std::nullopt}
      , m_keep{false}
    {
    }

    explicit(std::is_lvalue_reference_v<A>)
    MaybeLater(A&& val):
        m_optret{std::forward<A>(val)}
      , m_keep{false}
    {
    }

    template <typename B>
    requires std::is_convertible_v<B, A>
    MaybeLater(MaybeLater<B>&& other):
        m_keep{other.IsMessageKept()}
      , m_optret{std::move(other).operator A&&()}
    {
    }

    template <typename B>
    requires std::is_convertible_v<B, A>
    MaybeLater(const MaybeLater<B>& other):
        m_keep{other.IsMessageKept()}
      , m_optret{other.operator A&()}
    {
    }


    bool IsMessageKept() { return m_keep && !bool{m_optret}; }
    bool HasValue() { return bool{m_optret}; }

    operator A&() &
    {
        if constexpr(std::is_lvalue_reference_v<A>)
        {
            return m_optret->get();
        }
        else
        {
            return *m_optret;
        }
    }

    operator A&&() &&
    {
        if constexpr(std::is_lvalue_reference_v<A>)
        {
            return m_optret->get();
        }
        else
        {
            return *std::move(m_optret);
        }
    }

private:

    using OptType =
        std::conditional_t<std::is_lvalue_reference_v<A>
                         , std::reference_wrapper<A>
                         , A>;

    std::optional<OptType> m_optret;
    bool m_keep;
};


template <typename V>
requires std::is_void_v<V>
struct MaybeLater<V>
{

    MaybeLater(const Stash_t):
        m_returnsUnit{false}
      , m_keep{true}
    {
    }

    MaybeLater(const Discard_t):
        m_returnsUnit{false}
      , m_keep{false}
    {
    }

    MaybeLater():
        m_returnsUnit{true}
      , m_keep{false}
    {
    }

    MaybeLater(MaybeLater<V>& other):
        m_returnsUnit{other.HasValue()}
      , m_keep{!m_returnsUnit && other.IsMessageKept()}
    {
    }

    MaybeLater(MaybeLater<V>&&) = default;


    bool IsMessageKept() { return m_keep && !m_returnsUnit; }
    bool HasValue() { return m_returnsUnit; }

private:
    bool m_returnsUnit;
    bool m_keep;
};

template <typename A>
MaybeLater(A&&) -> MaybeLater<A>;
template <typename = void>
MaybeLater() -> MaybeLater<void>;


template <typename T>
struct EventualResponse
{
    template <typename>
    friend struct EventualResponse;
    friend ActorContext;

    template <typename A>
    struct AndThenComposeType
    {
        using type = void(A);
    };

    template <typename A>
    requires std::is_void_v<A>
    struct AndThenComposeType<A>
    {
        using type = void();
    };

    EventualResponse(ActorRef<Actor> whose
                   , std::future<detail::DelayedReturnType<T>>&& returned);

    EventualResponse(EventualResponse&) = delete;


    ///
    /// /brief Queues a response message handler to the value that
    ///        is eventually to be returned by the asked actor.
    ///
    /// \throw std::exception if callback has already been set.
    ///
    /// \note Once registering a callback this way, the EventualResponse object
    ///       does not need to be kept inside the actor.
    ///
    EventualResponse<T>& AndThen(std::function<typename AndThenComposeType<detail::DelayedReturnType<T>>::type> f);


    template <typename C
            , typename... Args>
    requires Callable<C, Args...>
          && (not DelayedReturn<C> or (std::is_copy_constructible_v<Args> and ...))
    struct FollowupMessage: ActorContext::Task
    {
        using RawR = std::invoke_result_t<C, Args...>;
        using R = detail::DelayedReturnType<RawR>;

        void Let() override { LetCut(std::make_index_sequence<sizeof...(Args)>()); }

        FollowupMessage(ActorRef<Actor> wherein
                      , std::promise<R>&& promise
                      , C f
                      , std::tuple<Args...>&& args
                      , std::shared_ptr<EventualResponse<RawR>> responseHandle):
            m_wherein{std::move(wherein)}
          , m_promise{std::move(promise)}
          , m_f{std::move(f)}
          , m_args{std::move(args)}
          , m_responseHandle{responseHandle}
        {
        };

        const ActorRef<Actor> m_wherein;
        std::promise<R> m_promise;
        const C m_f;
        std::tuple<Args...> m_args;
        const std::shared_ptr<EventualResponse<RawR>> m_responseHandle;

    private:
        template <size_t... is>
        void LetCut(const std::index_sequence<is...>)
        {
            bool failed = false;
            {
                auto lck = m_wherein.LockActor();
                auto& cell = lck.m_cell;
                try
                {
                    // Check if the return type is one of 'MaybeLater< «Return Type» >'
                    if constexpr(DelayedReturn<RawR>)
                    {
                        // Since we are using a delayed return, all argument types
                        // must be copyable, in order to have them at our disposal later,
                        // should the receiver decide to rewind to this exact message at
                        // some point in the future.
                        MaybeLater<R> delayedPossibly
                            = std::invoke(m_f, Args{std::get<is>(m_args)}...);

                        if(delayedPossibly.HasValue())
                        {
                            if constexpr(std::is_void_v<R>)
                            {
                                m_promise.set_value();
                            }
                            else
                            {
                                m_promise.set_value(std::move(delayedPossibly));
                            }
                        }
                        else if(delayedPossibly.IsMessageKept())
                        {
                            cell.StashTask(std::make_unique<std::remove_reference_t<decltype(*this)>>(
                                        m_wherein
                                      , std::move(m_promise)
                                      , std::move(m_f)
                                      , std::move(m_args)
                                      , m_responseHandle));
                            // Return early and try again once the actor decides to unstash
                            // the message.
                            return;
                        }
                        else
                        {
                            // Do nothing; the actor has decided to ignore this message.
                            // Unwind the promise-future infrastructure and clean up asap.
                            return;
                        }

                    }
                    else if constexpr(std::is_void_v<R>)
                    {
                        std::invoke(m_f, std::forward<Args>(std::get<is>(m_args))...);
                        m_promise.set_value();
                    }
                    else
                    {
                        m_promise.set_value(std::invoke(m_f, std::forward<Args>(std::get<is>(m_args))...));
                    }
                }
                catch(const std::exception& e)
                {
                    failed = true;
                    std::cerr << "Error in message " << e.what() << std::endl;
                    cell.Get()->Deactivate();
                    m_promise.set_exception(std::current_exception());
                    if(!m_wherein.GetParent())
                    {
                        std::rethrow_exception(std::current_exception());
                    }
                    m_wherein.GetParent().Bang(&Actor::OnChildFailed
                                             , Actor::ChildFailure{.m_why = std::current_exception(), .m_failed = m_wherein});
                }
                cell.TerminateDyingActor();
            }

            const auto& resp = m_responseHandle;

            if(!resp || failed)
            {
                return;
            }

            std::lock_guard<std::mutex> lck{resp->m_fset};

            resp->m_isReplyFormulated = true;

            if(resp->m_f)
            {
                resp->ScheduleResponse();
            }
        }
    };


private:

    void ScheduleResponse();

    ActorRef<Actor> m_recipient;
    std::function<typename AndThenComposeType<detail::DelayedReturnType<T>>::type> m_f;
    std::future<detail::DelayedReturnType<T>> m_returned;
    std::mutex m_fset;
    bool m_isReplyFormulated, m_hasScheduledResponse;

};

}

#endif

