#ifndef Laugh_Actor_Inline_721D2F93_180E_4F28_BB57_70898B91828B
#define Laugh_Actor_Inline_721D2F93_180E_4F28_BB57_70898B91828B

#include "Actor.hpp"

namespace laugh
{

// EventualResponse<T> {{{

template <typename T>
EventualResponse<T>::EventualResponse(ActorRef<Actor> recipient):
    m_recipient{std::move(recipient)}
{
}


template <typename A>
EventualResponse<A>* EventualResponse<A>::AndThen(AndThenType<A> f)
{
    std::lock_guard<std::mutex> lck{m_fset};

    if(HasScheduledResponse() || m_f)
    {
        throw std::exception();
    }

    m_f = std::move(f);

    if(IsReplyFormulated())
    {
        ScheduleResponse();
    }

    return this;
}


template <typename A>
void EventualResponse<A>
   ::ScheduleResponse(ResponseSchedulingArg<A>&& arg)
{
    if constexpr(std::is_void_v<DelayedReturnType<A>>)
    {
        m_recipient.GetContext().ScheduleMessage(
            std::make_unique<FollowupMessage<std::remove_cvref_t<decltype(m_f)>, ActorRef<Actor>>>(
                m_recipient
              , std::move(m_f)
              , std::make_tuple(m_recipient)
              , nullptr));
    }
    else
    {
        m_recipient.GetContext().ScheduleMessage(
            std::make_unique<FollowupMessage<std::remove_cvref_t<decltype(m_f)>, ActorRef<Actor>, A>>(
                m_recipient
              , std::move(m_f)
              , std::make_tuple(m_recipient, std::forward<ResponseSchedulingArg<A>>(arg))
              , nullptr));
    }
    HasScheduledResponse() = true;
}


template <typename A>
void EventualResponse<A>::ScheduleResponse()
{
    ScheduleResponse(std::move(*m_returned));
}


template <typename __T>
template <typename C
        , typename... Args>
requires Callable<C, Args...>
      && (not DelayedReturn<C> or (std::is_copy_constructible_v<Args> and ...))
struct EventualResponse<__T>::FollowupMessage: ActorContext::Task
{
    using RawR = std::invoke_result_t<C, Args...>;
    using R = DelayedReturnType<RawR>;

    void Let() override { LetCut(std::make_index_sequence<sizeof...(Args)>()); }

    FollowupMessage(ActorRef<Actor> wherein
                  , C f
                  , std::tuple<Args...>&& args
                  , std::shared_ptr<EventualResponse<R>> responseHandle):
        m_wherein{std::move(wherein)}
      , m_f{std::move(f)}
      , m_args{std::move(args)}
      , m_responseHandle{responseHandle}
    {
    };

private:

    template <size_t... is>
    void LetCut(const std::index_sequence<is...>)
    {
        // Flag for exception. If any exception occurred, we do not want to
        // notify EventualResponse<R> of it.
        // TODO Add exception callback possibility to EventualResponse<A>
        bool failed = false;

        // We don't want the compiler to blow up just because
        // ResponseSchedulingArg<R> is not default-constructible.
        // A lot of types aren't, for that matter.
        std::optional<ResponseSchedulingArg<R>> responseForwarded;

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
                            // Set the optional to contain a null pointer.
                            // Silly, I know.
                            responseForwarded = ResponseSchedulingArg<R>{};
                        }
                        else if constexpr(std::is_lvalue_reference_v<R>)
                        {
                            // Make sure a std::reference_wrapper takes the reference in,
                            // since std containers generally don't play nice with
                            // lvalue reference types.
                            responseForwarded.template emplace<std::reference_wrapper<std::remove_reference_t<R>>>(delayedPossibly.operator R&());
                        }
                        else
                        {
                            // R is a value type or an rvalue reference type.
                            // Either way, we keep the return value moving.
                            // The effect will be the same - a move-constructed value
                            // inside the variant.
                            responseForwarded.template emplace<std::remove_reference_t<R>>(std::move((delayedPossibly)));
                        }
                    }
                    // We know now that the actor has decided not to return a value.
                    // Check if the actor wants the message stashed or completely discarded.
                    else if(delayedPossibly.IsMessageKept())
                    {
                        // Return early and try again once the actor decides to unstash
                        // the message.
                        // Return type analysis is a moot point here; it will be done once
                        // the actor decides to actually return something.
                        cell.StashTask(std::make_unique<std::remove_reference_t<decltype(*this)>>(
                                    m_wherein
                                  , std::move(m_f)
                                  , std::move(m_args)  // <-- Observe that for _this_ reason right here,
                                                       // we need copy-constructible arguments in the
                                                       // case of MaybeLater<A> typed return values.
                                                       // We can't allow the message to have changed
                                                       // once stashed only because the actor decided to
                                                       // move-construct from one of the arguments in
                                                       // call to std::invoke above,
                                                       // which would be leaving those arguments in a valid
                                                       // but unknown state - and that state change will
                                                       // neither be known by the sender nor by the receiver
                                                       // once he decides to unstash and look at the
                                                       // arguments again.
                                  , m_responseHandle));
                        return;
                    }
                    else
                    {
                        // Do nothing; the actor has decided to ignore this message.
                        // Clean up response callback asap, if present.
                        return;
                    }

                }
                // We now know that RawR is not a delayed return type, so
                // std::is_same_v<RawR, R> == true.
                else if constexpr(std::is_void_v<R>)
                {
                    std::invoke(m_f, std::forward<Args>(std::get<is>(m_args))...);
                    responseForwarded = ResponseSchedulingArg<R>{};
                }
                else if constexpr(std::is_lvalue_reference_v<R>)
                {
                    responseForwarded.template emplace<std::reference_wrapper<std::remove_reference_t<R>>>(std::ref(
                            std::invoke(m_f, std::forward<Args>(std::get<is>(m_args))...)));
                }
                else
                {
                    responseForwarded.template emplace<std::remove_reference_t<R>>(
                            std::invoke(m_f, std::forward<Args>(std::get<is>(m_args))...));
                }
            }
            catch(const std::exception& e)
            {
                failed = true;
                cell.Get()->Deactivate();
                if(!m_wherein.GetParent()) [[unlikely]]
                {
                    // _This_ in all likelihood means that the program
                    // will call std::terminate next, since this exception is not
                    // caught by any worker thread right now.
                    // std::threads failing to catch an exception that completely
                    // unwinds their call stack invoke std::terminate.
                    // TODO Root-level exception handling in ActorContext.
                    std::rethrow_exception(std::current_exception());
                }
                else [[likely]]
                {
                    // Send the exception up the chain of parents.
                    m_wherein.GetParent()
                             .Bang(&Actor::OnChildFailed
                                 , Actor::ChildFailure{.m_why = std::current_exception()
                                                     , .m_failed = m_wherein});
                }
            }

            // In any case, terminate the actor that's been swapped out and
            // left without any reference.
            // It still might decide to send some messages, so keep the lock intact.
            cell.TerminateDyingActor();
        }

        const auto& resp = m_responseHandle;

        if(!resp || failed)
        {
            // Either the sender did not want to be notified of a response
            // or the actor has already failed with an exception.
            return;
        }

        std::lock_guard<std::mutex> lck{resp->m_fset};

        resp->IsReplyFormulated() = true;

        // Has a callback to the response already been formulated by the sender?
        if(resp->m_f)
        {
            // If so, send a response back to the sender with the return
            // value in hand right away.
            resp->ScheduleResponse(std::move(*responseForwarded));
        }
        // If not, allocate some space for the return value.
        else
        {
            // Move the return value from the variant, whichever type it may be now,
            // into a newly allocated object on the heap, and let it sit there until
            // the sender decides to set a callback or discard the EventualResponse<R>
            // object.
            resp->m_returned.reset(new ResponseSchedulingArg<R>{
                    std::move(*responseForwarded)});
        }
    }

    const ActorRef<Actor> m_wherein;
    const C m_f;
    std::tuple<Args...> m_args;
    const std::shared_ptr<EventualResponse<R>> m_responseHandle;
};



// }}}

// ActorLock {{{


template <ActorLike A, typename... ConsArgs>
requires std::is_constructible_v<A, ConsArgs...>
    void ActorLock::Restart(ConsArgs&&... args)
{
    auto actorNew = std::make_unique<A>(std::forward<ConsArgs>(args)...);

    if constexpr(std::is_default_constructible_v<A>)
    {
        RestartWith(std::move(actorNew));
    }
    else
    {
        RestartWithNonDefaultConstructible(std::move(actorNew));
    }
}


template <ActorLike A>
requires std::is_default_constructible_v<A>
    void ActorLock::RestartWith(std::unique_ptr<A> freshActor)
{
    m_cell.m_actor = std::move(freshActor);
    PostActorReset<A>();
}


template <ActorLike A>
requires (not std::is_default_constructible_v<A>)
    void ActorLock::RestartWithNonDefaultConstructible(std::unique_ptr<A> freshActor)
{
    m_cell.m_actor = std::move(freshActor);
    PostActorReset<A>();
}


template <ActorLike A>
A* ActorLock::Get() const
{
    return static_cast<A*>(m_cell.m_actor.get());
}


template <ActorLike S>
    void ActorLock::PostActorReset()
{
    m_cell.m_actor->m_self = m_cell.m_selfReference;

    if constexpr(std::is_default_constructible_v<S>)
    {
        m_cell.m_defaultConstruct = &ActorContext::ConstructDefault<S>;
    }
    else
    {
        m_cell.m_defaultConstruct = nullptr;
    }
}


template <ActorLike S>
      S& ActorLock::Become(std::unique_ptr<S> what)
{
    if(!m_cell.m_dyingActor)
    {
        std::swap(m_cell.m_actor, m_cell.m_dyingActor);
    }

    m_cell.m_actor = std::move(what);
    PostActorReset<S>();
    return *static_cast<S*>(m_cell.m_actor.get());
}


template <ActorLike A>
requires std::is_default_constructible_v<A>
    void ActorLock::Swap(std::unique_ptr<Actor>& with)
{
    std::swap(m_cell.m_actor, with);
    PostActorReset<A>();
}


template <ActorLike A>
requires (not std::is_default_constructible_v<A>)
    void ActorLock::SwapNonDefaultConstructible(std::unique_ptr<Actor>& with)
{
    std::swap(m_cell.m_actor, with);
    PostActorReset<A>();
}


// }}}

// ActorRef<A, Q> {{{

// ActorRef<A, Q>::ActorRef {{{

template <ActorLike A, ActorRefQuality Q>
ActorRef<A, Q>::ActorRef(ActorRef<Actor> parent
                       , ActorContext& context)
requires (Q == ActorRefQuality::Strong):
    m_cell{std::make_shared<ActorCell>(parent, &context)}
{
    m_cell->SetSelfReference(*this);
}


template <ActorLike A, ActorRefQuality Q>
ActorRef<A, Q>::ActorRef(std::nullptr_t parent
                       , ActorContext& context)
requires (Q == ActorRefQuality::Strong):
    m_cell{std::make_shared<ActorCell>(parent, &context)}
{
    m_cell->SetSelfReference(*this);
}


template <ActorLike A, ActorRefQuality Q>
template <ActorLike B, ActorRefQuality R>
ActorRef<A, Q>::ActorRef(const ActorRef<B, R>& other)
{
    if constexpr(Q == R or Q == ActorRefQuality::Weak)
    {
        m_cell = other.m_cell;
    }
    else
    {
        m_cell = other.m_cell.lock();
    }
}


// }}}

// ActorRef<A, Q>::«Various Member Functions» {{{

template <ActorLike A, ActorRefQuality Q>
template <ActorLike B, ActorRefQuality R>
    bool ActorRef<A, Q>::operator ==(const ActorRef<B, R>& other) const
{
    return m_cell == other.m_cell;
}

template <ActorLike A, ActorRefQuality Q>
template <ActorLike B, ActorRefQuality R>
requires std::is_convertible_v<B*, A*>
    auto ActorRef<A, Q>::operator =(const ActorRef<B, R>& other)
{
    if constexpr(Q == R or Q == ActorRefQuality::Weak)
    {
        m_cell = other.m_cell;
    }
    else
    {
        m_cell = other.m_cell.lock();
    }
}


// ActorRef<A, Q>::«Variations on Messaging» {{{

template <ActorLike A, ActorRefQuality Q>
template <ActorLike S, typename R, typename... Params, typename... Args>
requires Callable<R (S::*)(Params...), S&, std::remove_reference_t<Args>...>
      && std::is_convertible_v<A*, S*>
      && (Q == ActorRefQuality::Strong)
    void ActorRef<A, Q>::Bang(R (S::* const fn)(Params...)
                            , Args&&... args) const
{
    // The implementation of 'Bang' might not look much different
    // from the implementation of 'Ask', but there is one crucial
    // distinction:
    // there is no EventualResponse<R> object necessary to construct
    // here, and so we can save some initialization work as well
    // as on the response infrastructure; the sender does not
    // immediately care about how a message gets back to him or
    // even whether he wants a response at all.
    auto recall = [fn, cell = m_cell](std::remove_reference_t<Args>&&... args) -> R
    {
        return (cell->LockActor().template Get<S>()->*fn)(std::forward<Args>(args)...);
    };

    m_cell->GetContext()->ScheduleMessage(
        std::make_unique<typename EventualResponse<R>::template
                                  FollowupMessage<decltype(recall), std::remove_reference_t<Args>...>>(*this
         , std::move(recall)
         , std::make_tuple(std::forward<Args>(args)...)
         , nullptr));
}


template <ActorLike A, ActorRefQuality Q>
template <ActorLike S, typename R, typename... Params, typename... Args>
requires Callable<R (S::*)(Params...) const, const S&, std::remove_reference_t<Args>...>
      && std::is_convertible_v<A*, const S*>
      && (Q == ActorRefQuality::Strong)
    void ActorRef<A, Q>::Bang(R (S::* const fn)(Params...) const
                            , Args&&... args) const
{
    // The implementation of 'Bang' might not look much different
    // from the implementation of 'Ask', but there is one crucial
    // distinction:
    // there is no EventualResponse<R> object necessary to construct
    // here, and so we can save some initialization work as well
    // as on the response infrastructure; the sender does not
    // immediately care about how a message gets back to him or
    // even whether he wants a response at all.
    auto recall = [fn, cell = m_cell](std::remove_reference_t<Args>&&... args) -> R
    {
        return (cell->LockActor().template Get<const S>()->*fn)(std::forward<Args>(args)...);
    };

    m_cell->GetContext()->ScheduleMessage(
        std::make_unique<typename EventualResponse<R>::template
                                  FollowupMessage<decltype(recall), std::remove_reference_t<Args>...>>(*this
         , std::move(recall)
         , std::make_tuple(std::forward<Args>(args)...)
         , nullptr));
}


template <ActorLike A, ActorRefQuality RefQuality>
template <typename R, ActorLike S, typename... Params, typename... Args>
requires Callable<R (S::*)(Params...), S&, std::remove_reference_t<Args>...>
      && (RefQuality == ActorRefQuality::Strong)
    auto ActorRef<A, RefQuality>::Ask(ActorRef<S> who
                                    , R (S::* const what)(Params...)
                                    , Args&&... args) const
      -> std::shared_ptr<EventualResponse<DelayedReturnType<R>>>
{
    auto responseHandle = std::make_shared<EventualResponse<DelayedReturnType<R>>>(*this);

    auto recall = [what, cell = who.m_cell](std::remove_reference_t<Args>&&... args) -> R
    {
        return (cell->LockActor().template Get<S>()->*what)(std::forward<Args>(args)...);
    };

    m_cell->GetContext()->ScheduleMessage(
        std::make_unique<typename EventualResponse<R>::template 
                                  FollowupMessage<decltype(recall), std::remove_reference_t<Args>...>>(
            who
          , std::move(recall)
          , std::make_tuple(std::forward<Args>(args)...)
          , responseHandle));

    return responseHandle;
}


template <ActorLike A, ActorRefQuality RefQuality>
template <typename R, ActorLike S, typename... Params, typename... Args>
requires Callable<R (S::*)(Params...) const, const S&, std::remove_reference_t<Args>...>
      && (RefQuality == ActorRefQuality::Strong)
    auto ActorRef<A, RefQuality>::Ask(ActorRef<const S> who
                                    , R (S::* const what)(Params...) const
                                    , Args&&... args) const
      -> std::shared_ptr<EventualResponse<DelayedReturnType<R>>>
{
    auto responseHandle = std::make_shared<EventualResponse<DelayedReturnType<R>>>(*this);

    auto recall = [what, cell = who.m_cell](std::remove_reference_t<Args>&&... args) -> R
    {
        return (cell->LockActor().template Get<const S>()->*what)(std::forward<Args>(args)...);
    };

    m_cell->GetContext()->ScheduleMessage(
        std::make_unique<typename EventualResponse<R>::template 
                                  FollowupMessage<decltype(recall), std::remove_reference_t<Args>...>>(
            who
          , std::move(recall)
          , std::make_tuple(std::forward<Args>(args)...)
          , responseHandle));

    return responseHandle;
}

// }}}


template <ActorLike A, ActorRefQuality Q>
template <ActorLike B, typename... ConsArgs>
requires std::is_constructible_v<B, ConsArgs...>
      && (Q == ActorRefQuality::Strong)
    auto ActorRef<A, Q>::Make(ConsArgs&&... args) const
      -> ActorRef<B>
{
    ActorRef<B> actorNew{*this, *m_cell->GetContext()};
    ActorLock lck{actorNew.LockActor()};
    lck.Restart<B>(std::forward<ConsArgs>(args)...);
    return actorNew;
}


template <ActorLike A, ActorRefQuality Q>
    auto ActorRef<A, Q>::LockActor() const
      -> ActorLock
         requires (Q == ActorRefQuality::Strong)
{
    return m_cell->LockActor();
}


template <ActorLike A, ActorRefQuality Q>
template <ActorLike S>
    auto ActorRef<A, Q>::Get() const
      -> std::tuple<S*, ActorLock>
         requires (Q == ActorRefQuality::Strong)
{
    auto lck = LockActor();
    S* ret = lck.template Get<S>();
    return std::make_tuple(ret, std::move(lck));
}


template <ActorLike A, ActorRefQuality Q>
    auto ActorRef<A, Q>::GetContext() const
      -> ActorContext&
         requires (Q == ActorRefQuality::Strong)
{
    return *m_cell->GetContext();
}


template <ActorLike A, ActorRefQuality Q>
    auto ActorRef<A, Q>::GetParent() const
      -> ActorRef<Actor>
         requires (Q == ActorRefQuality::Strong)
{
    return m_cell->GetParent();
}


template <ActorLike A, ActorRefQuality Q>
    void ActorRef<A, Q>::Point(A& that)
         requires (Q == ActorRefQuality::Strong)
{
    m_cell = that.Self().m_cell;
}

// }}}

// }}}

// ActorContext {{{

template <ActorLike A>
requires std::is_default_constructible_v<A>
    auto ActorContext::ConstructDefault()
      -> std::unique_ptr<Actor>
{
    return std::make_unique<A>();
}


template <ActorLike A, typename... ConsArgs>
ActorRef<A> ActorContext::Make(ConsArgs&&... args)
{
    ActorRef<A> actorNew{nullptr, *this};
    ActorLock lck = actorNew.LockActor();
    lck.Restart<A>(std::forward<ConsArgs>(args)...);
    return actorNew;
}

// }}}

// Actor {{{


template <ActorLike S
        , typename R
        , typename... Params
        , typename... Args>
requires Callable<R (S::*)(Params...), S&, Args...>
    auto Actor::Ask(ActorRef<S> who
           , R (S::* const what)(Params...)
           , Args&&... with)
      -> std::shared_ptr<EventualResponse<DelayedReturnType<R>>>
{
    return Self().Ask(who, what, std::forward<Args>(with)...);
}


template <ActorLike S
        , ActorLike T
        , typename R
        , typename... Params
        , typename... Args>
requires Callable<R (S::*)(Params...) const, const S&, Args...>
      && std::is_convertible_v<std::remove_reference_t<T>*, const S*>
    auto Actor::Ask(ActorRef<T> who
           , R (S::* const what)(Params...) const
           , Args&&... with)
      -> std::shared_ptr<EventualResponse<DelayedReturnType<R>>>
{
    return Self().Ask(ActorRef<const S>{who}, what, std::forward<Args>(with)...);
}


template <ActorLike S
        , typename... Args>
requires std::is_constructible_v<S, Args...>
      S& Actor::Become(Args&&... args) const
{
    return Self().LockActor().Become<S>(std::make_unique<S>(std::forward<Args>(args)...));
}


template <ActorLike S>
requires std::is_default_constructible_v<S>
      S& Actor::Become(std::unique_ptr<S> what) const
{
    return Self().LockActor().Become<S>(std::move(what));
}


template <ActorLike S>
requires (not std::is_default_constructible_v<S>)
      S& Actor::BecomeNonDefaultConstructible(std::unique_ptr<S> what) const
{
    return Self().LockActor().Become<S>(std::move(what));
}


template <ActorLike S>
requires std::is_default_constructible_v<S>
    void Actor::Swap(std::unique_ptr<Actor>& with) const
{
    Self().LockActor().Swap<S>(with);
}


template <ActorLike S>
requires (not std::is_default_constructible_v<S>)
    void Actor::SwapNonDefaultConstructible(std::unique_ptr<Actor>& with) const
{
    Self().LockActor().Swap<S>(with);
}


// }}}

}

#endif
