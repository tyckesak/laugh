#include "Laugh/Actor.inl"

#include <limits>
#include <numeric>
#include <iostream>

using namespace laugh;

// ActorContext {{{


ActorContext::ActorContext(int workers):
    m_unassigned()
  , m_producerChip(m_unassigned)
{
    m_terminationDesired.clear();
    for(int i = 0; i < workers; ++i)
    {
        SpawnWorker();
    }
}

void ActorContext::SpawnWorker()
{
    std::atomic_flag isWorkerEnrolled;
    isWorkerEnrolled.clear();

    std::thread t{&ActorContext::WorkerRun, std::ref(*this), &isWorkerEnrolled};
    std::thread::id tid = t.get_id();

    isWorkerEnrolled.wait(false);

    std::lock_guard<decltype(m_threadInfoBottleneck)> _{m_threadInfoBottleneck};
    m_threadsInfo[tid].m_worker = std::move(t);
}

void ActorContext::WorkerRun(std::atomic_flag* isWorkerEnrolled)
{
    {
        std::lock_guard<decltype(m_threadInfoBottleneck)> _{m_threadInfoBottleneck};

        m_threadsInfo[std::this_thread::get_id()].m_waiting = true;

        isWorkerEnrolled->test_and_set();
        isWorkerEnrolled->notify_all();
    }

    // By now, the atomic flag's destructor has likely already been
    // called, this is a dangling pointer.
    // We do not need this flag anymore anyway.
    isWorkerEnrolled = nullptr;

    std::unique_ptr<Task> current;
    ThreadContext& info = m_threadsInfo[std::this_thread::get_id()];

    while(true)
    {
        {
            std::shared_lock<std::shared_mutex> lk{m_workSchedule};

            m_hasWork.wait(lk, [&info, &current, this]()
            {
                m_unassigned.try_dequeue(current);
                const bool willWork = !(info.m_waiting = current == nullptr);
                if(willWork)
                {
                    --m_unassignedCount;
                }

                if (m_terminationDesired.test())
                {
                    return willWork || m_unassignedCount.load() == 0; 
                }
                else
                {
                    return willWork;
                }
            });
        }

        // The worker has succeeded in dequeuing a message, or has
        // determined in a volatile manner that all other worker threads
        // are idling.
        // m_hasWork.notify_all();

        if(current)
        {
            // Execute and delete the message, start anew.
            current->Let();
            current = nullptr;
            continue;
        }
        else
        {
            // This thread has been allowed to die, notify all other worker
            // threads stuck in the wait call above so that they may follow suit.
            m_hasWork.notify_all();
            break;
        }
    }
}

ActorContext::TerminateMessage
     ::TerminateMessage(ActorContext& self):
    m_self{self}
{
}

void ActorContext::TerminateMessage::Let()
{
    m_self.m_terminationDesired.test_and_set();
}

void ActorContext::Terminate()
{
    ScheduleMessage(std::make_unique<TerminateMessage>(*this));
}

void ActorContext::ScheduleMessage(std::unique_ptr<Task>&& task)
{
    {
        std::lock_guard<std::shared_mutex> lck{m_workSchedule};
        m_unassigned.enqueue(m_producerChip, std::move(task));
    }
    ++m_unassignedCount;
    m_hasWork.notify_all();
}

ActorContext::~ActorContext()
{
    std::lock_guard<std::mutex> lck{m_threadInfoBottleneck};
    Terminate();

    for(auto& [tid, ct]: m_threadsInfo)
    {
        ct.m_worker.join();
    }
}


// }}}


// ActorLock {{{


ActorLock::ActorLock(ActorCell& cell):
    m_cell{cell}
  , m_lock{m_cell.m_blocking}
{
}


void ActorLock::RestartWithDefaultConstructor()
{
    if(m_cell.m_defaultConstruct == nullptr)
    {
        throw std::exception();
    }
    else
    {
        m_cell.m_actor = m_cell.m_defaultConstruct();
        m_cell.m_actor->m_self = m_cell.m_selfReference;
    }
}


// }}}


// ActorCell {{{

void ActorCell::TerminateDyingActor()
{
    m_dyingActor = nullptr;
}


bool ActorCell::IsAutomaticallyResettable() const
{
    return m_defaultConstruct != nullptr;
}


ActorLock ActorCell::LockActor()
{
    return ActorLock{*this};
}


void ActorCell::StashTask(std::unique_ptr<ActorContext::Task> task)
{
    m_stash.push_back(std::move(task));
}


void ActorCell::Unstash(int n)
{
    if(n <= -2)
    {
        throw std::exception();
    }
    else if(m_toUnstash != 0)
    {
        if(n > 0)
        {
            // Overflow-safe addition necessary.
            // Unsigned integers are added modulo this big number.
            // See C++ specification.
            m_toUnstash = m_toUnstash + static_cast<decltype(m_toUnstash)>(n) < m_toUnstash?
                          std::numeric_limits<decltype(m_toUnstash)>::max() :
                          m_toUnstash + static_cast<decltype(m_toUnstash)>(n);
        }
        else if(n == -1)
        {
            m_toUnstash = std::numeric_limits<decltype(m_toUnstash)>::max();
        }
        return;
    }
    else if(n == 0)
    {
        return;
    }


    // RAII cleanup of the unstash count.
    // Reset it to zero in any case once this function
    // returns from this point on.
    struct UnstashCountReset
    {
        ActorCell& m_cell;
        UnstashCountReset(ActorCell& cell): m_cell{cell} {}
        ~UnstashCountReset()
        {
            m_cell.m_toUnstash = 0;
        }
    } _{*this};


    auto it = m_stash.begin();
    auto last = m_stash.end();

    if(it == last)
    {
        // No messages to unstash, forgo std::advance, return early.
        return;
    }
    else
    {
        std::advance(last, -1);
    }


    m_toUnstash = n > 0? n : std::numeric_limits<decltype(m_toUnstash)>::max();

    while(it != m_stash.end()
       && (m_toUnstash == std::numeric_limits<decltype(m_toUnstash)>::max()? true : (m_toUnstash --> 0)))
    {
        // Execute this loop one more time for the last
        // known stashed element, if this is true,
        // and quit the loop after this message.
        const bool hasReachedKnownEnd = it == last;

        std::unique_ptr<ActorContext::Task> tsk{std::move(*it)};
        // Erase the now invalid pointer from the stash before
        // executing the message; it could throw an exception after all
        it = m_stash.erase(it);
        tsk->Let();
        tsk = nullptr;

        if(hasReachedKnownEnd)
        {
            break;
        }
    }
}


ActorCell::ActorCell(const ActorRef<Actor>& parent
                   , ActorContext* context):
    m_parent{parent}
  , m_context{context}
  , m_defaultConstruct{nullptr}
  , m_actor{nullptr}
  , m_selfReference()
  , m_toUnstash(0)
{
}


ActorCell::ActorCell(std::nullptr_t parent
                   , ActorContext* context):
    m_parent{std::nullopt}
  , m_context{context}
  , m_defaultConstruct{nullptr}
  , m_actor{nullptr}
  , m_selfReference()
{
}

// }}}


// Actor {{{


void Actor::UnstashAll()
{
    auto cell = Self().m_cell;
    cell->Unstash(-1);
}


void Actor::Unstash(int n)
{
    auto cell = Self().m_cell;
    cell->Unstash(n);
}


// }}}

