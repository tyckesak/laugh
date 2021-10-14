# Light Actor Framework

Concurrency is a breeze. Also a nightmare, if you ever used synchronization
techniques. Mostly a nightmare, though. This tiny library allows you to
forego sync primitives in favor of object-oriented concurrent _actors_,
executing independently from each other in parallel and receiving
_messages_ to react to - one at a time each.

Take a listen.

## Quick feature rundown
 - Quick and scalable concurrency
 - Memory-cheap actors for concurrent execution instead
   of heavy `std::thread` objects.
 - Cross-platform, C++20 compliant.
 - Hot-swap actors at any time to respond to messages differently.
 - _Don't ask for permission, ask for forgiveness._ Thrown exceptions are
   handled by parent actors up the chain.
 - Restart actors on failure seamlessly.
 - Synchronous access to any actor on demand, if your situation requires it.

```cpp
#include <iostream>
#include "Actor.inl"
using namespace laugh;

struct Counter: Actor
{
    int i = 0;
    int Increment() { return ++i; }
};

struct User: Actor
{
    void PlayAround()
    {
        auto counter = Self().Make<Counter>();
        for(int i = 0; i < 50; ++i)
        {
            // Fifty concurrent messages!
            counter.Bang(&Counter::Increment);
        }
        
        // How far has the counter come so far?
        Ask(counter, &Counter::Increment)
            ->AndThen([](int there)
        {
            std::cout << "So far, he's at " << there
                      << std::endl;
        });
    }
};

int main()
{
    // Let a set of three OS threads do the message processing.
    ActorContext acts{3};
    // Just create an actor like this. Even with arguments,
    // if you want to. And if the constructor allows it.
    auto user = acts.Make<User>();
    
    // Done and done.
    user.Bang(&User::PlayAround);
    
    std::cout << "Meanwhile, main thread sips coffee." << std::endl;
}
```

For more information on actors as concurrency units, you'll find plenty
on the internet. A good place to get to know actors is
[Akka](https://akka.io), which this project is trying to ape a bit (and
fail horribly at it).

## Build and requirements

You'll need a fairly compliant C++20 compiler and CMake. For generating
the documentation, Doxygen will be necessary.

I have tested this on Clang 13.0.0 on an Apple macOS machine, but I'm relatively
certain that this project will compile without any hassle on the g++ and vc++
compilers, since this project only depends on the standard library and the
[concurrentqueue](https://github.com/cameron314/concurrentqueue) module, which
is itself C++11 compliant.

_Note_ that Apple Clang 13.0.0 will not compile
this as of October 13, 2021; I recommend getting a fresh Clang 13.0.0 build and
doing:

```
# If you're using homebrew, great! Easy-peasy.
brew install llvm doxygen
```

To build the project and examples, do

```
# Clone the repo and initialize the submodule.
git clone https://github.com/tyckesak/laugh && cd laugh && git submodule update --init
# Invoke CMake in the root directory. Builds all the examples for you too.
cmake -DCMAKE_CXX_COMPILER=«Path to compliant compiler, if not in $PATH already» .
make all
```

To build the docs, do

```
make docs
```

The examples and documentation will tell you the rest.

## Why that name?

I thought I would do a quick sketch of what a code-light C++ actor framework would
look like, and it started to become something quite usable. The contraction
of "Light Actor Framework" would be _LAF_, spoken quickly it eventually turned
into "laugh", which turned out to be fitting because this project is laughably
small, and easy to grasp and use.

## Thanks

@cameron314 and contributors on making a [concurrent queue](https://github.com/cameron314/concurrentqueue)
that really stands out!

