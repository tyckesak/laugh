#include "Actor.inl"

using namespace laugh;

struct Pong: Actor
{
    int i = 0;

    void PrintInt()
    {
        std::cout << "Pong " << this << " is at " << ++i << std::endl;
    }
};


struct Ping: Actor
{
    void Who(ActorRef<Pong> actor)
    {
        void (Pong::* message)() = &Pong::PrintInt;
        std::cout << "Pinging a pong from thread "
                  << std::this_thread::get_id() << std::endl;

        actor.Bang(message);
    }

    ~Ping()
    {
        std::cout << "Observe that the log really has counted "
                  << "up to exactly the number 1000." << std::endl;
    }
};


int main()
{
    // Allocate three OS threads to do the message passing.
    std::unique_ptr<ActorContext> acts{new ActorContext{3}};

    // Tell the pool to create two distinct actors.
    // These actors are not bound to OS threads at all, and come very
    // cheap memory-wise.
    //
    // An ActorRef is in essence nothing but a container for
    // a std::shared_ptr. It is also cheap to make.
    ActorRef<Ping> ping = acts->Make<Ping>();
    ActorRef<Pong> pong = acts->Make<Pong>();

    // Make them do (non-)meaningful work.
    for(int i = 0; i < 1000; ++i)
    {
        ping.Bang(&Ping::Who, pong);
    }

    std::cout << "Meanwhile, the main thread might be off "
              << "doing its own thing." << std::endl;

    // The destructor of the ActorContext takes care of joining
    // the worker threads, waits until the last messages have been processed.
}

