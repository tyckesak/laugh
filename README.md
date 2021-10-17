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
 - Stash and discard messages using the special return type `MaybeLater<A>`.
 - _Don't ask for permission, ask for forgiveness._ Thrown exceptions are
   handled by parent actors up the chain.
 - Restart actors on failure seamlessly.
 - Synchronous access to any actor on demand, if your situation requires it.

Here is a short taste.

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

I have tested this on Clang 13.0.0 and g++-11 on an Apple macOS machine, but I'm
confident that this project will compile without any hassle on the vc++
compiler as well, since this project only depends on the standard library and the
[concurrentqueue](https://github.com/cameron314/concurrentqueue) module, which
is itself C++11 compliant.

_Note_ that Apple Clang 13.0.0 will not compile
this as of October 14, 2021, and neither will all Clang compilers of version 12
and lower; on a Macintosh I recommend getting a fresh Clang 13.0.0 build and
doing:

```
# If you're using homebrew, great! Easy-peasy.
brew install llvm doxygen
```

To build the project and examples, do

```
# Clone the repo.
git clone https://github.com/tyckesak/laugh
cd laugh

# If you don't have a recent enough C++20 compiler in your $PATH, append
# the CMake flag '-DCMAKE_CXX_COMPILER=«Path to compiler»'
cmake .

# Then do
cmake --build .
```

For installing the library and the dependency headers, do
```
cmake --install .
```

To build the docs, do

```
cmake --build . --target docs
```

To re-download the [concurrentqueue](https://github.com/cameron314/concurrentqueue) module,
do
```
# Might need to do this if the compiler can't find the
# concurrentqueue headers.
cmake --build . --target concurrentqueue
```

The examples and documentation will tell you the rest.

## Memory footprint

Whenever a foreign library owns your memory, it might be a good idea to
ask how much memory it occupies and in what way. I am planning for allocator
support some time in the future; in the meantime, I have taken to finding out
how much memory each of the critical pieces of infrastructure use. The numbers
below have been obtained by compiling the project and examples under a Macintosh
x64 using the `g++` 11 compiler - the results might change depending on the compiler,
architecture and OS used to compile it on.

 - `Actor`'s minimum footprint is 32 bytes plus x, depending on how many
   members your actor subclasses add.
 - One `ActorRef<A>` takes up 16 bytes of space.
 - The internal bookkeeping structure occupies 160 bytes, and its memory
   is separate from your own `Actor`s.
 - One sent message is about ~50 bytes in size plus storage for each of
   your arguments passed alongside it.
 - If playing with the thought to be called back when the receiver responds, plan with
   an additional 144 bytes for the `EventualResponse<A>`.

## Project structure

I try to keep a clean online repo with as few files as possible to make
exploration easier - upon building the project on your local machine
however, you'll see a couple more files and directories spring into
existence inside the project root directory, including - but not limited to

 - `Extern/` - external project dependencies are cloned there, i.e. [concurrentqueue](https://github.com/cameron314/concurrentqueue)
 - `Library/` - the directory containing the shared/static library after building
 - `Doxygen/` - contains the Doxygen generated doc files
 - Makefiles for your platform
 - Documentation cache files created by Doxygen
 - `CMakeFiles/` - intermediate object files

## Why that name?

I thought I would do a quick sketch of what a code-light C++ actor framework would
look like, and it started to become something quite usable. The contraction
of "Light Actor Framework" would be _LAF_, spoken quickly it eventually turned
into "laugh", which turned out to be fitting because this project is laughably
small, and easy to grasp and use.

## Thanks

@cameron314 and contributors on making a [concurrent queue](https://github.com/cameron314/concurrentqueue)
that really stands out!

