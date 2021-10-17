#include <deque>
#include <iostream>
#include <algorithm>

#include <Laugh/Actor.inl>

using namespace laugh;


// A pretty slow, iterative prime number finding algorithm.
// But, with the right tools, even slow workers can do the
// right things pretty fast.
struct SlowPrimeWorker: Actor
{
    bool IsPrime(int n)
    {
        if(n == 2)
        {
            return true;
        }
        else if(n % 2 == 0)
        {
            return false;
        }
        else
        {
            for(int i = 3; i < 1000000 && i <= n / 2; i += 2)
            {
                if(n % i == 0)
                {
                    return false;
                }
            }
            return true;
        }
    }
};


struct PatientBoss: Actor
{
    std::deque<ActorRef<SlowPrimeWorker>> m_workers;
    // A counter for how many primes we've found so far.
    int foundPrimes = 0;


    void Hire(int workers)
    {
        while(workers --> 0)
        {
            // Make a new actor that is a child of this one.
            // Child actors propagate their exceptions as a message
            // to their parent up the chain, which can then decide
            // how to deal with the failure by overriding the
            // virtual function Actor::OnChildFailed(const ChildFailure&)
            auto workerNew = Self().Make<SlowPrimeWorker>();
            m_workers.push_back(workerNew);
        }
    }


    MaybeLater<void> DispatchFindPrime(int prime)
    {
        if(m_workers.empty())
        {
            // Messages are not necessarily received in the order they are sent in. 
            // For this example, this means that - even though the main thread is
            // clearly sending the request to hire workers before it asks to
            // compute primes - we have to safeguard the invocation and check
            // if there are any workers around.
            //
            // This can be alleviated by using the 'Ask' member function, an example
            // of which is a little down further.
            std::cout << "Oops! Can't find out whether " << prime << " is "
                      << "a prime right now. I have no workers. I ought to hire some..."
                      << std::endl;

            // Return early. std::deque provokes undefined behavior if
            // it is empty and its .front() member function is called.
            //
            // Since we might want to calculate that prime number
            // later again, we stash this invocation.
            return Stash;

            // You might also try this, see what happens!
            // return Discard;
        }

        // Catch up on calculating those prime numbers that we could not do
        // because of a worker shortage.
        UnstashAll();

        // 'Ask' is only available as a protected method inside Actor
        // objects, since the response needs to run on the same ActorRef.
        //
        // You can't make other actors 'Ask' another actor to do something.
        // You need to ask yourself.
        Ask(m_workers.front(), &SlowPrimeWorker::IsPrime, prime)
            // Call 'AndThen' to specify what should be done once the asked
            // actor sends a return value back.
            //
            // It is bad practice to capture the 'this' pointer inside of
            // the response callback, since this actor might replace its
            // ActorRef with another actor, might reset itself or
            // not exist anymore for other reasons.
            // That is why it is required for the callback to take
            // the self reference as the first parameter, so that
            // the lambda can get a fresh pointer to itself.
            ->AndThen([prime](ActorRef<Actor> self, bool isPrime)
        {
            std::cout << "Telegraph for you!  --  Thanks! " << prime
                      << " is " << (isPrime? "a " : "no") << " prime."
                      << std::endl;

            // The 'Get' member function on ActorRef returns a tuple
            // containing a pointer to the referenced actor and a lock
            // object needed to ensure synchronous access to the pointer
            // as long as the lock is in scope.
            // An actor can very cheaply acquire a lock to its own ActorRef
            // object.
            auto [selfPtr, lock] = self.Get<PatientBoss>();
            (selfPtr->foundPrimes += isPrime);
        });

        // Once the message has been sent, we can release the worker.
        // The message for him to work will reach him eventually.
        m_workers.pop_front();

        return {};
    }


    // Destructors of actors are called immediately once they are found
    // to not receive any more messages and to have no ActorRef
    // left referencing them.
    ~PatientBoss()
    {
        std::cout << "We are done here. Found " << foundPrimes << " primes today."
                  << std::endl;
    }
};


int main()
{
    std::unique_ptr<ActorContext> acts{new ActorContext{3}};

    auto boss = acts->Make<PatientBoss>();

    // Send messages asynchronously to the boss to calculate primes.
    // Notice that we have not given him workers to calculate primes yet,
    // so these messages will likely end up stashed.
    boss.Bang(&PatientBoss::DispatchFindPrime, 3);
    boss.Bang(&PatientBoss::DispatchFindPrime, 5);

    // However, the following message might reach the boss first, after all.
    // Important thing to remember: messages are not necessarily delivered
    // in order.
    //
    // This is a fact so important that I want you to say it again.


    // Make 100 child workers (actors).
    boss.Bang(&PatientBoss::Hire, 100 + 2);

    for(int i = 5000; i < 5000 + 100; ++i)
    {
        // Messages are pointers to any member function of a class deriving
        // from Actor, and arguments to the invocation are forwarded along
        // with the message.
        boss.Bang(&PatientBoss::DispatchFindPrime, i);
    }

    std::cout << "The main thread could go for a coffee right now."
              << std::endl;
}
