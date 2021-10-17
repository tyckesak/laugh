#include <string>
#include <iostream>

#include <Laugh/Actor.inl>

using namespace laugh;


struct Alternate: Actor
{
    // Virtual functions have a nice property when used with pointers
    // to members.
    // Pointers to members respect the virtuality of member functions,
    // so if an Alternate actor is sent a 'WhichDirection' message,
    // it will be dispatched to the actual implementation.
    virtual std::string WhichDirection() const = 0;
    virtual ~Alternate() = default;
};


// Two implementations of the 'Alternate' interface.

struct Left: Alternate
{
    std::string WhichDirection() const override final;
};


struct Right: Alternate
{
    std::string WhichDirection() const override final;
};


std::string Left::WhichDirection() const
{
    // Schedule the change to a Right instance after processing
    // this message, and - while we are at it - return "left!"
    // to sender.
    Become<Right>();
    return "Left!";
}


std::string Right::WhichDirection() const
{
    // Vice-versa.
    Become<Left>();
    return "Right!";
}


// A test actor where we may use Ask yet again.
struct Receiver: Actor
{
    // This works just like a pointer to base!
    ActorRef<Alternate> decide;

    void PickDirection()
    {
        // Make a child actor which extends 'Alternate'.
        decide = Self().Make<Left>();

        for(int i = 0; i < 20; ++i)
        {
            // The Ask member function is explained in detail in the
            // Ask.cpp example.
            Ask(decide, &Alternate::WhichDirection)
                ->AndThen([](ActorRef<Actor> self, const std::string& str)
            {
                std::cout << str << std::endl;
            });
        }
    }
};


int main()
{
    std::unique_ptr<ActorContext> acts{new ActorContext{3}};

    ActorRef<Receiver> alt = acts->Make<Receiver>();
    alt.Bang(&Receiver::PickDirection);

    std::cout << "Coffee time!" << std::endl;
}

