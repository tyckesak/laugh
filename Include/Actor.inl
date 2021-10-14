#ifndef Laugh_Actor_Inline_721D2F93_180E_4F28_BB57_70898B91828B
#define Laugh_Actor_Inline_721D2F93_180E_4F28_BB57_70898B91828B

#include "Actor.hpp"

namespace laugh
{

// EventualResponse<T> {{{

template <typename T>
EventualResponse<T>::EventualResponse(ActorRef<Actor> recipient
                                    , std::future<detail::DelayedReturnType<T>>&& returned):
    m_recipient{std::move(recipient)}
  , m_returned{std::move(returned)}
  , m_isReplyFormulated{false}
  , m_hasScheduledResponse{false}
{
}


template <typename A>
EventualResponse<A>& EventualResponse<A>::AndThen(std::function<typename AndThenComposeType<detail::DelayedReturnType<A>>::type> f)
{
    std::lock_guard<std::mutex> lck{m_fset};

    if(m_hasScheduledResponse || m_f)
    {
        throw std::exception();
    }

    m_f = std::move(f);

    if(m_isReplyFormulated)
    {
        ScheduleResponse();
    }

    return *this;
}

template <typename A>
void EventualResponse<A>::ScheduleResponse()
{
    if constexpr(std::is_void_v<detail::DelayedReturnType<A>>)
    {
        m_recipient.GetContext().ScheduleMessage(
            std::make_unique<FollowupMessage<std::remove_cvref_t<decltype(m_f)>>>(
                std::move(m_recipient)
              , std::promise<void>{}
              , std::move(m_f)
              , std::make_tuple()
              , nullptr));
    }
    else
    {
        m_recipient.GetContext().ScheduleMessage(
            std::make_unique<FollowupMessage<std::remove_cvref_t<decltype(m_f)>, A>>(
                std::move(m_recipient)
              , std::promise<void>{}
              , std::move(m_f)
              , std::make_tuple(m_returned.get())
              , nullptr));
    }
    m_hasScheduledResponse = true;
}


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
         , std::promise<detail::DelayedReturnType<R>>{}
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
         , std::promise<detail::DelayedReturnType<R>>{}
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
      -> std::shared_ptr<EventualResponse<R>>
{
    std::promise<R> returnPush;

    std::shared_ptr<EventualResponse<R>> responseHandle{
        std::make_shared<EventualResponse<R>>(*this
                                            , returnPush.get_future())};

    auto recall = [what, cell = who.m_cell](std::remove_reference_t<Args>&&... args) -> R
    {
        return (cell->LockActor().template Get<S>()->*what)(std::forward<Args>(args)...);
    };

    m_cell->GetContext()->ScheduleMessage(
        std::make_unique<typename EventualResponse<R>::template 
                                  FollowupMessage<decltype(recall), std::remove_reference_t<Args>...>>(
            who
          , std::move(returnPush)
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
      -> std::shared_ptr<EventualResponse<R>>
{
    std::promise<R> returnPush;

    std::shared_ptr<EventualResponse<R>> responseHandle{
        std::make_shared<EventualResponse<R>>(*this
                                            , returnPush.get_future())};

    auto recall = [what, cell = who.m_cell](std::remove_reference_t<Args>&&... args) -> R
    {
        return (cell->LockActor().template Get<const S>()->*what)(std::forward<Args>(args)...);
    };

    m_cell->GetContext()->ScheduleMessage(
        std::make_unique<typename EventualResponse<R>::template 
                                  FollowupMessage<decltype(recall), std::remove_reference_t<Args>...>>(
            who
          , std::move(returnPush)
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
      -> std::shared_ptr<EventualResponse<R>>
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
      -> std::shared_ptr<EventualResponse<R>>
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
